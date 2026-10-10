#include "SenderController.h"
#include "ScreenCapture.h"
#include "VirtualDisplayVDD.h"
#include "VideoEncoderFF.h"
#include "NetworkSender.h"
#include <QDebug>
#include <QJsonDocument>

#ifdef _WIN32
#include "ScreenCaptureWin.h"
#endif
// TODO: #include "ScreenCaptureLinux.h" for PipeWire support

SenderController::SenderController(QObject* parent)
    : QObject(parent)
{
    m_statsTimer.setInterval(1000);
    connect(&m_statsTimer, &QTimer::timeout, this, [this] {
        const double seconds = m_statsClock.restart() / 1000.0;
        if (seconds <= 0 || !m_network) return;
        const QString rtt = m_network->rttMS() < 0 ? "no data" : QString::number(m_network->rttMS(), 'f', 1);
        const double wireMbps = m_network->takeWrittenBytes() * 8 / seconds / 1000000;
        const QString stats = QString("%1\nH.264 %2 FPS target; capture %3, encode %4 FPS; %5 Mbps\n%6; encode call %7ms; TCP queue %8B; raw drops %9; TCP RTT %10ms; raw queue 0; encoder pending %11 frames")
            .arg(m_network->endpoints()).arg(m_fps).arg(m_captured / seconds, 0, 'f', 1)
            .arg(m_encoded / seconds, 0, 'f', 1).arg(wireMbps, 0, 'f', 2)
            .arg(encoderInfo()).arg(m_encodeMS, 0, 'f', 2).arg(m_network->queuedBytes()).arg(m_rawDrops).arg(rtt).arg(m_encoder ? QString::number(m_encoder->queuedFrames()) : "no data");
        emit statusChanged(stats + "\nReceiver: " + QString::fromUtf8(QJsonDocument(m_peerStats).toJson(QJsonDocument::Compact)));
        m_network->sendControl({{"op", "senderStats"}, {"captureFPS", m_captured / seconds}, {"encodeFPS", m_encoded / seconds},
            {"wireMbps", wireMbps}, {"encodedMbps", m_bytes * 8 / seconds / 1000000}, {"encoder", encoderInfo()}, {"encodeCallMS", m_encodeMS},
            {"queueBytes", double(m_network->queuedBytes())}, {"rawDrops", double(m_rawDrops)}, {"tcpRTTMS", m_network->rttMS()}, {"controlRTTMS", m_network->controlRTTMS()}});
        if (m_network->queuedBytes() > 65536 || m_encodeMS > 1000.0 / m_fps)
            emit statusChanged(stats + "\nProcessing or TCP congestion: reduce resolution or choose 60 FPS. Mode has not changed.");
        m_captured = m_encoded = m_bytes = 0;
    });
#ifdef _WIN32
    m_vdd = new VirtualDisplayVDD(this);
    connect(m_vdd, &VirtualDisplayVDD::statusChanged,
            this, &SenderController::statusChanged);
    connect(m_vdd, &VirtualDisplayVDD::error,
            this, &SenderController::error);
#endif
}

void SenderController::setMonitorIndex(int adapterIndex, int outputIndex) {
    m_adapterIndex = adapterIndex;
    m_outputIndex = outputIndex;
}

SenderController::~SenderController() {
    stopSending();
}

bool SenderController::startSending(const QString& receiverHost, uint16_t port,
                                     int fps, int bitrateMbps) {
    if (m_sending) {
        qWarning() << "Sender: Already sending";
        return false;
    }

    m_fps = fps;
    m_bitrateMbps = bitrateMbps;
#ifdef _WIN32
    if (m_ethernet.enabled && (m_displayName.isEmpty() || !m_vdd->verifyDisplayMode(m_displayName, 0, 0, fps))) {
        emit error("Selected display must actually run at the requested 60/120 Hz. Apply the VDD mode or choose it in Windows Settings first."); return false;
    }
#endif

    // Create screen capture (targeting selected monitor)
#ifdef _WIN32
    auto* cap = new ScreenCaptureWin(fps, this);
    cap->setMonitorIndex(m_adapterIndex, m_outputIndex);
    cap->setDisplayName(m_displayName);
    m_capture = cap;
#else
    emit error("Screen capture not yet supported on this platform");
    emit statusChanged("Sender not available on this platform yet");
    return false;
#endif

    m_encoder = new VideoEncoderFF(this);
    m_network = new NetworkSender(this);
    m_network->setEthernetConfig(m_ethernet);
    m_network->setStreamMode(fps, bitrateMbps);
    connect(m_network, &NetworkSender::keyframeRequested, m_encoder, &VideoEncoderFF::requestKeyframe);
    connect(m_network, &NetworkSender::fatalError, this, [this](const QString& message) { stopSending(); emit error(message); });
    connect(m_network, &NetworkSender::peerStatistics, this, [this](const QJsonObject& stats) { m_peerStats = stats; });

    // Wire signals
    connect(m_capture, &ScreenCapture::frameCaptured,
            this, &SenderController::onFrameCaptured);
    connect(m_capture, &ScreenCapture::error,
            this, &SenderController::error);

    connect(m_encoder, &VideoEncoderFF::encoded,
            this, &SenderController::onEncoded);
    connect(m_encoder, &VideoEncoderFF::error, this, [this](const QString& message) {
        emit error(message);
        // A compressed frame was lost: start a new stream rather than send dependent P frames.
        if (m_network) m_network->resetStream();
    });

    connect(m_network, &NetworkSender::connected,
            this, &SenderController::onConnected);
    connect(m_network, &NetworkSender::disconnected,
            this, &SenderController::onDisconnected);
    connect(m_network, &NetworkSender::error,
            this, &SenderController::error);

    // Connect to receiver first
    emit statusChanged("Connecting to receiver...");
    m_sending = true;
    m_encoderReady = false;
    m_captured = m_encoded = m_bytes = m_rawDrops = 0;
    m_statsClock.start(); m_statsTimer.start();
    m_network->connectTo(receiverHost, port);
    return true;
}

void SenderController::stopSending() {
    if (!m_sending) return;

    m_statsTimer.stop();
    m_sending = false;
    m_encoderReady = false;

    if (m_capture) {
        m_capture->stop();
        m_capture->deleteLater();
        m_capture = nullptr;
    }
    if (m_encoder) {
        m_encoder->shutdown();
        m_encoder->deleteLater();
        m_encoder = nullptr;
    }
    if (m_network) {
        m_network->disconnect();
        m_network->deleteLater();
        m_network = nullptr;
    }

    emit stopped();
    emit statusChanged("Sender stopped");
}

void SenderController::onConnected() {
    qDebug() << "Sender: Connected to receiver, starting capture...";
    emit connected();
    emit statusChanged("Connected — starting screen capture...");

    if (m_capture && !m_capture->isRunning()) {
        if (!m_capture->start()) {
            emit error("Failed to start screen capture");
            stopSending();
        }
    }
}

void SenderController::onDisconnected() {
    qDebug() << "Sender: Disconnected from receiver";
    emit disconnected();
    if (m_capture) m_capture->stop();
    // A send can fail inside VideoEncoderFF::encoded. Defer teardown until encode() returns.
    if (m_encoder) { auto* encoder = m_encoder; QTimer::singleShot(0, encoder, [encoder] { encoder->shutdown(); }); }
    m_encoderReady = false;
    emit statusChanged("Ethernet disconnected; waiting on selected interface; no Wi-Fi fallback");
}

void SenderController::onFrameCaptured(const QByteArray& nv12, int width, int height) {
    if (!m_encoder || !m_sending || !m_network) return;
    ++m_captured;
    if (!m_network->canAcceptFrame()) { ++m_rawDrops; return; }

    // Lazy-init encoder on first frame (captures real resolution)
    if (!m_encoderReady || width != m_streamWidth || height != m_streamHeight) {
        if (!m_encoder->init(width, height, m_fps, m_bitrateMbps)) {
            stopSending();
            emit error("No compatible H.264 encoder for this mode; select 60 FPS or install GPU encoding support");
            return;
        }
        m_encoderReady = true;
        m_streamWidth = width; m_streamHeight = height;
        emit statusChanged(QString("Streaming %1x%2 via %3")
                               .arg(width).arg(height).arg(m_encoder->encoderName()));
        m_network->sendControl({{"op", "format"}, {"width", width}, {"height", height}, {"fps", m_fps}, {"codec", "h264"}, {"encoder", encoderInfo()}});
        // Force first frame to be a keyframe
        m_encoder->requestKeyframe();
    }

    QElapsedTimer processing; processing.start();
    m_encoder->encode(nv12, width, height);
    m_encodeMS = processing.nsecsElapsed() / 1000000.0;
}

void SenderController::onEncoded(const QByteArray& payload) {
    ++m_encoded; m_bytes += payload.size() + 5;
    if (m_network && m_network->isConnected()) {
        m_network->sendVideo(payload);
    }
}

QString SenderController::encoderInfo() const {
    if (m_encoder) {
        return m_encoder->encoderName();
    }
    return "Not initialized";
}
