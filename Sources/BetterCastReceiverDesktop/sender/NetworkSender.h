#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <QJsonObject>

// Dedicated mode: manually addressed, interface-pinned TLS/TCP, protocol v1.
struct EthernetConfig {
    bool enabled = false;
    QString interfaceName;
    QString localIP;
    QString certificateSHA256;
};

class NetworkSender : public QObject {
    Q_OBJECT
public:
    explicit NetworkSender(QObject* parent = nullptr);
    ~NetworkSender() override;
    void setEthernetConfig(const EthernetConfig& config) { m_config = config; }
    void setStreamMode(int fps, int bitrateMbps);
    void connectTo(const QString& host, uint16_t port);
    void disconnect();
    bool isConnected() const;
    bool canAcceptFrame() const;
    qint64 queuedBytes() const;
    quint64 takeWrittenBytes() { const auto bytes = m_writtenBytes; m_writtenBytes = 0; return bytes; }
    double rttMS() const;
    double controlRTTMS() const { return m_rttMS; }
    QString endpoints() const;
    void sendVideo(const QByteArray& payload);
    void sendAudio(const QByteArray& payload);
    void sendControl(const QJsonObject& control);
    void resetStream();
signals:
    void connected();
    void disconnected();
    void error(const QString& message);
    void keyframeRequested();
    void fatalError(const QString& message);
    void peerStatistics(const QJsonObject& stats);
private:
    void sendPacket(uint8_t type, const QByteArray& payload);
    void attemptConnect();
    void retry(const QString& reason);
    void readControl();
    void transportReady();
    QTcpSocket* m_socket = nullptr;
    EthernetConfig m_config;
    QString m_host;
    uint16_t m_port = 0;
    QTimer m_retryTimer;
    QTimer m_watchdog;
    QElapsedTimer m_clock;
    qint64 m_lastReply = 0;
    qint64 m_pingSent = 0;
    qint64 m_backlogSince = -1;
    double m_rttMS = -1;
    quint64 m_writtenBytes = 0;
    int m_fps = 60;
    int m_bitrateMbps = 20;
    bool m_active = false;
    bool m_ready = false;
    QByteArray m_incoming;
};
