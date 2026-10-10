#include "NetworkSender.h"
#include <QSslSocket>
#include <QSslCertificate>
#include <QCryptographicHash>
#include <QNetworkInterface>
#include <QJsonDocument>
#include <QtEndian>
#include <QRegularExpression>
#ifdef __APPLE__
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#endif

NetworkSender::NetworkSender(QObject* parent) : QObject(parent) {
    m_clock.start();
    m_retryTimer.setSingleShot(true);
    connect(&m_retryTimer, &QTimer::timeout, this, &NetworkSender::attemptConnect);
    m_watchdog.setInterval(1000);
    connect(&m_watchdog, &QTimer::timeout, this, [this] {
        if (!m_active || !m_config.enabled || !m_socket) return;
        if (m_clock.elapsed() - m_lastReply > (m_ready ? 5000 : 60000)) {
            retry("Ethernet/peer timeout; waiting for selected connection");
            return;
        }
        if (m_ready) {
            m_pingSent = m_clock.elapsed();
            sendControl({{"op", "ping"}, {"token", double(m_pingSent)}});
        }
    });
}
NetworkSender::~NetworkSender() { disconnect(); }
void NetworkSender::setStreamMode(int fps, int bitrateMbps) {
    m_fps = fps; m_bitrateMbps = bitrateMbps;
}
void NetworkSender::connectTo(const QString& host, uint16_t port) {
    disconnect(); m_host = host; m_port = port; m_active = true; attemptConnect();
}
void NetworkSender::attemptConnect() {
    if (!m_active) return;
    if (m_socket) { m_socket->disconnect(this); m_socket->abort(); m_socket->deleteLater(); }
    m_socket = nullptr;
    m_ready = false; m_incoming.clear(); m_backlogSince = -1;
    m_lastReply = m_clock.elapsed();
    if (m_config.enabled) {
        const auto iface = QNetworkInterface::interfaceFromName(m_config.interfaceName);
        const QHostAddress local(m_config.localIP), remote(m_host);
        bool addressMatches = false, subnetMatches = false;
        for (const auto& entry : iface.addressEntries()) {
            if (entry.ip() == local) {
                addressMatches = true;
                subnetMatches = entry.prefixLength() > 0 && remote.isInSubnet(local, entry.prefixLength());
            }
        }
        bool isAllowedInterface = iface.type() == QNetworkInterface::Ethernet;
        bool isDifferentPeer = local != remote;
#ifdef BETTERCAST_ETHERNET_TEST
        if (iface.flags().testFlag(QNetworkInterface::IsLoopBack) && local == QHostAddress("127.0.0.1") && remote == local) {
            isAllowedInterface = true; isDifferentPeer = true;
        }
#endif
        if (!isAllowedInterface || !iface.flags().testFlag(QNetworkInterface::IsRunning) ||
            !addressMatches || !subnetMatches || local.protocol() != QAbstractSocket::IPv4Protocol ||
            remote.protocol() != QAbstractSocket::IPv4Protocol || !isDifferentPeer) {
            retry("Selected Ethernet IPv4/subnet unavailable; no fallback permitted"); return;
        }
        auto pin = m_config.certificateSHA256.toLatin1().replace(":", "").trimmed().toLower();
        if (!QRegularExpression("^[0-9a-f]{64}$").match(QString::fromLatin1(pin)).hasMatch() || !QSslSocket::supportsSsl()) {
            m_active = false;
            emit fatalError("A valid SHA-256 certificate pin and Qt TLS backend are required"); return;
        }
        auto* ssl = new QSslSocket(this);
        ssl->setProtocol(QSsl::TlsV1_2OrLater);
        connect(ssl, &QSslSocket::encryptedBytesWritten, this, [this](qint64 count) { m_writtenBytes += quint64(count); });
        m_socket = ssl;
        connect(ssl, &QSslSocket::sslErrors, this, [this, ssl, pin](const QList<QSslError>& errors) {
            // Out-of-band pin authorizes this exact self-signed leaf only.
            if (ssl->peerCertificate().digest(QCryptographicHash::Sha256).toHex() == pin)
                ssl->ignoreSslErrors(errors);
            else {
                m_active = false; m_watchdog.stop();
                ssl->abort(); emit fatalError("Receiver certificate mismatch; check pairing fingerprint");
            }
        });
        connect(ssl, &QSslSocket::encrypted, this, [this, ssl, pin] {
            if (ssl->peerCertificate().digest(QCryptographicHash::Sha256).toHex() != pin) {
                m_active = false; ssl->abort(); emit fatalError("Receiver certificate pin mismatch"); return;
            }
            transportReady();
        });
        // bind() creates the native socket before setting IP_UNICAST_IF.
        if (!ssl->bind(local, 0)) { retry("Cannot bind selected Ethernet IP"); return; }
#ifdef _WIN32
        const DWORD index = htonl(static_cast<DWORD>(iface.index()));
        if (::setsockopt(static_cast<SOCKET>(ssl->socketDescriptor()), IPPROTO_IP, IP_UNICAST_IF,
                         reinterpret_cast<const char*>(&index), sizeof(index)) != 0) {
            retry("Cannot pin Windows IP_UNICAST_IF; refusing unpinned connection"); return;
        }
#elif defined(__APPLE__)
        // Used by the portable TLS integration test; macOS production receiver uses NWParameters.
        const unsigned index = static_cast<unsigned>(iface.index());
        if (::setsockopt(int(ssl->socketDescriptor()), IPPROTO_IP, IP_BOUND_IF, &index, sizeof(index)) != 0) {
            retry("Cannot pin test client interface"); return;
        }
#endif
    } else {
        m_socket = new QTcpSocket(this);
        connect(m_socket, &QTcpSocket::bytesWritten, this, [this](qint64 count) { m_writtenBytes += quint64(count); });
        connect(m_socket, &QTcpSocket::connected, this, &NetworkSender::transportReady);
    }
    auto* socket = m_socket;
    connect(socket, &QTcpSocket::disconnected, this, [this] { retry("Connection lost"); });
    connect(socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        retry(m_socket ? m_socket->errorString() : "Socket error");
    });
    connect(socket, &QTcpSocket::readyRead, this, &NetworkSender::readControl);
    m_watchdog.start();
    socket->setReadBufferSize(65540);
    if (m_config.enabled) static_cast<QSslSocket*>(socket)->connectToHostEncrypted(m_host, m_port);
    else socket->connectToHost(m_host, m_port);
}
void NetworkSender::transportReady() {
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    m_socket->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, 65536);
    m_socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
    if (m_config.enabled) {
        if (m_socket->localAddress() != QHostAddress(m_config.localIP)) {
            retry("Actual local endpoint differs from selected IP"); return;
        }
        sendControl({{"op", "hello"}, {"version", 1}, {"codec", "h264"}, {"fps", m_fps}});
    } else { m_ready = true; emit connected(); }
}
void NetworkSender::retry(const QString& reason) {
    const bool wasReady = m_ready;
    m_ready = false; m_watchdog.stop();
    if (m_socket) { m_socket->disconnect(this); m_socket->abort(); }
    if (wasReady) emit disconnected();
    if (!m_active) return;
    emit error(reason);
    m_retryTimer.start(2000); // indefinite, same interface/IP; Stop cancels it
}
void NetworkSender::disconnect() {
    m_active = false; m_ready = false; m_retryTimer.stop(); m_watchdog.stop();
    if (m_socket) { m_socket->disconnect(this); m_socket->abort(); }
}
bool NetworkSender::isConnected() const { return m_ready && m_socket && m_socket->state() == QAbstractSocket::ConnectedState; }
double NetworkSender::rttMS() const {
    if (!m_socket || !isConnected()) return -1;
#ifdef _WIN32
    TCP_INFO_v0 info = {}; DWORD version = 0, returned = 0;
    if (WSAIoctl(static_cast<SOCKET>(m_socket->socketDescriptor()), SIO_TCP_INFO, &version, sizeof(version),
                 &info, sizeof(info), &returned, nullptr, nullptr) == 0) return info.RttUs / 1000.0;
#elif defined(__APPLE__)
    tcp_connection_info info = {}; socklen_t size = sizeof(info);
    if (getsockopt(int(m_socket->socketDescriptor()), IPPROTO_TCP, TCP_CONNECTION_INFO, &info, &size) == 0)
        return double(info.tcpi_srtt); // SDK defines this field in milliseconds
#endif
    return -1;
}
qint64 NetworkSender::queuedBytes() const { return m_socket ? m_socket->bytesToWrite() : 0; }
bool NetworkSender::canAcceptFrame() const {
    // Drop RAW frames before encoding; never discard an encoded reference frame.
    return isConnected() && queuedBytes() <= qint64(m_bitrateMbps) * 1000000 / 8 * 15 / 1000;
}
QString NetworkSender::endpoints() const {
    if (!m_socket || !isConnected()) return "not connected";
    return QString("%1:%2 → %3:%4 (%5)").arg(m_socket->localAddress().toString()).arg(m_socket->localPort())
        .arg(m_socket->peerAddress().toString()).arg(m_socket->peerPort()).arg(m_config.interfaceName);
}
void NetworkSender::sendPacket(uint8_t type, const QByteArray& payload) {
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) return;
    constexpr qint64 maxPacket = 4 * 1024 * 1024;
    if (payload.size() + 1 > maxPacket || queuedBytes() + payload.size() + 5 > maxPacket + 65536) {
        retry("TCP queue limit reached: reset stream, recover with IDR; reduce resolution/FPS"); return;
    }
    if (queuedBytes() == 0) m_backlogSince = -1;
    else if (m_backlogSince < 0) m_backlogSince = m_clock.elapsed();
    if (m_backlogSince >= 0 && m_clock.elapsed() - m_backlogSince > 100) {
        retry("TCP backlog exceeded 100ms: reconnect with IDR; consider 60 FPS"); return;
    }
    QByteArray packet(4, Qt::Uninitialized);
    qToBigEndian<quint32>(quint32(payload.size() + 1), packet.data());
    packet.append(char(type)); packet.append(payload);
    if (m_socket->write(packet) != packet.size()) retry("Packet write failed");
}
void NetworkSender::sendVideo(const QByteArray& payload) { if (isConnected()) sendPacket(1, payload); }
void NetworkSender::sendAudio(const QByteArray& payload) { if (isConnected() && !m_config.enabled) sendPacket(2, payload); }
void NetworkSender::sendControl(const QJsonObject& control) {
    if (m_config.enabled) sendPacket(0x10, QJsonDocument(control).toJson(QJsonDocument::Compact));
}
void NetworkSender::resetStream() { retry("Reset compressed stream for keyframe recovery"); }
void NetworkSender::readControl() {
    m_incoming.append(m_socket->readAll());
    if (m_incoming.size() > 65540) { retry("Oversized control message"); return; }
    while (m_incoming.size() >= 4) {
        const auto length = qFromBigEndian<quint32>(m_incoming.constData());
        if (!length || length > 65536) { retry("Invalid control length"); return; }
        if (m_incoming.size() < qsizetype(4 + length)) return;
        QByteArray body = m_incoming.mid(4, length); m_incoming.remove(0, 4 + length);
        if (m_config.enabled) {
            if (quint8(body[0]) != 0x10) { retry("Unexpected control type"); return; }
            const auto obj = QJsonDocument::fromJson(body.mid(1)).object();
            const auto op = obj.value("op").toString();
            if (op == "ready" && !m_ready) {
                if (obj.value("version").toInt() != 1 || obj.value("fps").toInt() != m_fps || obj.value("codec") != "h264") {
                    m_active = false; retry("Stream negotiation mismatch"); emit fatalError("Unsupported receiver protocol/mode"); return;
                }
                m_ready = true; m_lastReply = m_clock.elapsed(); emit connected();
            } else if (op == "pong" && obj.value("token").toDouble() == double(m_pingSent)) {
                m_rttMS = double(m_clock.elapsed() - m_pingSent); m_lastReply = m_clock.elapsed();
            } else if (op == "keyframe") { emit keyframeRequested(); }
            else if (op == "stats") { emit peerStatistics(obj); }
            else if (op == "reject") {
                m_active = false; retry("Pairing rejected"); emit fatalError("Mac rejected connection/mode: " + obj.value("reason").toString()); return;
            }
        } else {
            const auto obj = QJsonDocument::fromJson(body).object();
            if (obj.value("type").toInt() == 99 && obj.value("keyCode").toInt() == 999) emit keyframeRequested();
        }
    }
}
