#pragma once

#include <QObject>
#include <QByteArray>
#include <QSize>
#include <cstdint>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;

// FFmpeg H.264 encoder with hardware acceleration probing.
// Probes NVENC → AMF → QSV → VAAPI → libx264 in order.
// Outputs AVCC-framed NALUs matching BetterCast wire protocol.
class VideoEncoderFF : public QObject {
    Q_OBJECT
public:
    explicit VideoEncoderFF(QObject* parent = nullptr);
    ~VideoEncoderFF() override;

    bool init(int width, int height, int fps = 30, int bitrateMbps = 8);
    void encode(const QByteArray& nv12Data, int width, int height);
    void requestKeyframe();
    void shutdown();

    bool isInitialized() const { return m_ctx != nullptr; }
    int queuedFrames() const { return int(m_frameCount - m_outputPackets); }
    QString encoderName() const { return m_encoderName; }

signals:
    // data: BetterCast video payload = [8B PTS nanoseconds][AVCC NALUs]
    void encoded(const QByteArray& data);
    void error(const QString& message);

private:
#ifdef BETTERCAST_ETHERNET_TEST
    friend struct EncoderFramingTests;
#endif
    bool tryEncoder(const char* codecName, int width, int height, int fps, int bitrate);
    bool cacheParameterSets(const uint8_t* data, int size);
    QByteArray annexBtoAVCC(const uint8_t* data, int size);
    int m_nalLengthSize = 4;
    bool m_lengthPrefixedPackets = false;

    AVCodecContext* m_ctx = nullptr;
    AVFrame* m_frame = nullptr;
    AVPacket* m_pkt = nullptr;
    int64_t m_frameCount = 0;
    int64_t m_outputPackets = 0;
    int m_fps = 30;
    int m_bitrateMbps = 8;
    bool m_forceKeyframe = false;
    QString m_encoderName;

    // Cached SPS/PPS for prepending to keyframes
    QByteArray m_sps;
    QByteArray m_pps;
};
