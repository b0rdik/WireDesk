#include "VideoEncoderFF.h"
#include <QDebug>
#include <QElapsedTimer>
#include <cstring>
#include <algorithm>
#include <QtEndian>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

#ifndef FF_PROFILE_H264_HIGH
#define FF_PROFILE_H264_HIGH 100
#endif

VideoEncoderFF::VideoEncoderFF(QObject* parent)
    : QObject(parent)
{
}

VideoEncoderFF::~VideoEncoderFF() {
    shutdown();
}

bool VideoEncoderFF::tryEncoder(const char* codecName, int width, int height, int fps, int bitrate) {
    const AVCodec* codec = avcodec_find_encoder_by_name(codecName);
    if (!codec) return false;

    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (!ctx) return false;

    ctx->width = width;
    ctx->height = height;
    ctx->time_base = {1, fps};
    ctx->framerate = {fps, 1};
    ctx->pix_fmt = AV_PIX_FMT_NV12;
    ctx->bit_rate = bitrate;
    ctx->rc_max_rate = bitrate * 3 / 2;              // allow 1.5x peak for motion
    ctx->rc_buffer_size = std::max(bitrate / fps * 2, 65536);                     // two-frame VBV target (encoder may clamp)
    ctx->gop_size = fps;                           // IDR recovery at least once per second
    ctx->max_b_frames = 0;                             // no B-frames for low latency
    ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    ctx->thread_count = 0;                             // auto — let FFmpeg pick
    ctx->profile = FF_PROFILE_H264_HIGH;
    ctx->color_range = AVCOL_RANGE_MPEG;
    ctx->colorspace = AVCOL_SPC_BT709;
    ctx->color_primaries = AVCOL_PRI_BT709;
    ctx->color_trc = AVCOL_TRC_BT709;

    // Log unsupported options instead of claiming a preset was applied.
    auto setOption = [ctx, codecName](const char* key, const char* value) {
        int result = av_opt_set(ctx->priv_data, key, value, 0);
        if (result < 0) qWarning() << "Encoder option unavailable:" << codecName << key << result;
    };
    // Low-latency tuning per encoder
    if (strcmp(codecName, "libx264") == 0) {
        setOption("preset", "veryfast");  // better quality than ultrafast
        setOption("tune", "zerolatency");
        setOption("forced-idr", "1");
        setOption("aq-mode", "2");        // variance AQ — helps motion
    } else if (strstr(codecName, "nvenc")) {
        setOption("preset", "p4");        // balanced speed/quality
        setOption("tune", "ull");         // ultra-low-latency
        setOption("zerolatency", "1");
        setOption("forced-idr", "1");
        setOption("rc", "vbr");           // VBR — better motion quality
        setOption("spatial-aq", "1");     // spatial adaptive quantization
        setOption("temporal-aq", "0");    // temporal adaptive quantization
        setOption("rc-lookahead", "0");   // no lookahead — low latency
    } else if (strstr(codecName, "amf")) {
        setOption("usage", "ultralowlatency");
        setOption("rc", "vbr_peak");      // VBR with peak constraint
        setOption("quality", "balanced");
        setOption("vbaq", "1");           // adaptive quantization
    } else if (strstr(codecName, "qsv")) {
        setOption("preset", "veryfast");
        setOption("async_depth", "1");
        setOption("look_ahead", "0");
    } else if (strstr(codecName, "vaapi")) {
        // VAAPI: pixel format must be vaapi for HW surface
        // For simplicity, we upload NV12 frames — FFmpeg handles the rest
        // Note: VAAPI init requires a hw_device_ctx which we skip for now
    }

    int ret = avcodec_open2(ctx, codec, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        qDebug() << "Sender: Encoder" << codecName << "failed to open:" << errbuf;
        avcodec_free_context(&ctx);
        return false;
    }

    // FFmpeg encoders may expose Annex-B or AVCDecoderConfigurationRecord extradata.
    m_sps.clear(); m_pps.clear(); m_nalLengthSize = 4; m_lengthPrefixedPackets = false;
    if (ctx->extradata_size > 0 && !cacheParameterSets(ctx->extradata, ctx->extradata_size)) {
        qWarning() << "Invalid H.264 encoder extradata:" << codecName;
        avcodec_free_context(&ctx); return false;
    }

    m_ctx = ctx;
    m_encoderName = QString::fromUtf8(codecName);
    qDebug() << "Sender: Using encoder:" << codecName << "at" << width << "x" << height << "@" << fps;
    return true;
}

bool VideoEncoderFF::init(int width, int height, int fps, int bitrateMbps) {
    shutdown();
    if (width <= 0 || height <= 0 || width % 2 || height % 2 || fps <= 0 || fps > 120 || bitrateMbps <= 0 || bitrateMbps > 100) {
        emit error("Invalid NV12 encoder mode"); return false;
    }
    m_fps = fps;
    m_bitrateMbps = bitrateMbps;
    int bitrate = bitrateMbps * 1000000;

    // Probe encoders in preference order
    const char* encoders[] = {
        "h264_nvenc",     // NVIDIA
        "h264_amf",       // AMD
        "h264_qsv",       // Intel QuickSync
        "h264_vaapi",     // Linux VA-API
        "libx264",        // Software fallback
        nullptr
    };

    for (int i = 0; encoders[i]; i++) {
        if (fps == 120 && strcmp(encoders[i], "libx264") == 0) continue;
        if (tryEncoder(encoders[i], width, height, fps, bitrate)) {
            break;
        }
    }

    if (!m_ctx) {
        emit error("No compatible H.264 encoder. 120 FPS requires working NVENC/AMF/QSV; choose 60 FPS for software fallback.");
        return false;
    }

    m_frame = av_frame_alloc();
    if (!m_frame) { shutdown(); return false; }
    m_frame->format = AV_PIX_FMT_NV12;
    m_frame->color_range = AVCOL_RANGE_MPEG;
    m_frame->colorspace = AVCOL_SPC_BT709;
    m_frame->color_primaries = AVCOL_PRI_BT709;
    m_frame->color_trc = AVCOL_TRC_BT709;
    m_frame->width = width;
    m_frame->height = height;
    if (av_frame_get_buffer(m_frame, 32) < 0) {
        emit error("Failed to allocate encoder frame buffer");
        shutdown();
        return false;
    }

    m_pkt = av_packet_alloc();
    if (!m_pkt) { shutdown(); return false; }
    m_frameCount = 0;
    m_forceKeyframe = false;
    return true;
}

bool VideoEncoderFF::cacheParameterSets(const uint8_t* data, int size) {
    if (!data || size <= 0) return false;
    if (data[0] != 1) return !annexBtoAVCC(data, size).isEmpty();
    if (size < 7) return false;
    m_lengthPrefixedPackets = true;
    m_nalLengthSize = (data[4] & 3) + 1;
    int offset = 6;
    auto readSets = [&](int count) {
        for (int i = 0; i < count; ++i) {
            if (offset + 2 > size) return false;
            const int length = (int(data[offset]) << 8) | data[offset + 1]; offset += 2;
            if (length == 0 || length > size - offset) return false;
            const int type = data[offset] & 31;
            if (type == 7) m_sps = QByteArray(reinterpret_cast<const char*>(data + offset), length);
            if (type == 8) m_pps = QByteArray(reinterpret_cast<const char*>(data + offset), length);
            offset += length;
        }
        return true;
    };
    if (!readSets(data[5] & 31) || offset >= size) return false;
    const int ppsCount = data[offset++];
    return readSets(ppsCount) && !m_sps.isEmpty() && !m_pps.isEmpty();
}

QByteArray VideoEncoderFF::annexBtoAVCC(const uint8_t* data, int size) {
    if (!data || size <= 0) return {};
    QByteArray result;
    result.reserve(size + 16);
    auto appendNAL = [&](int start, int length) {
        if (length <= 0) return false;
        const int type = data[start] & 31;
        if (type == 7) m_sps = QByteArray(reinterpret_cast<const char*>(data + start), length);
        if (type == 8) m_pps = QByteArray(reinterpret_cast<const char*>(data + start), length);
        const quint32 bigLength = qToBigEndian(quint32(length));
        result.append(reinterpret_cast<const char*>(&bigLength), 4);
        result.append(reinterpret_cast<const char*>(data + start), length);
        return true;
    };
    auto startCode = [&](int offset) {
        if (offset + 2 >= size || data[offset] != 0 || data[offset + 1] != 0) return 0;
        if (data[offset + 2] == 1) return 3;
        if (offset + 3 < size && data[offset + 2] == 0 && data[offset + 3] == 1) return 4;
        return 0;
    };
    if (!m_lengthPrefixedPackets && startCode(0)) {
        int offset = 0;
        while (offset < size) {
            const int code = startCode(offset);
            if (!code) return {};
            const int start = offset + code;
            int end = start;
            while (end < size && !startCode(end)) ++end;
            if (!appendNAL(start, end - start)) return {};
            offset = end;
        }
    } else {
        // Normalize length-prefixed packets using the advertised avcC length size.
        int offset = 0;
        while (offset < size) {
            if (size - offset < m_nalLengthSize) return {};
            quint32 length = 0;
            for (int i = 0; i < m_nalLengthSize; ++i) length = (length << 8) | data[offset++];
            if (length == 0 || length > quint32(size - offset)) return {};
            if (!appendNAL(offset, int(length))) return {};
            offset += int(length);
        }
    }
    return result;
}

void VideoEncoderFF::encode(const QByteArray& nv12Data, int width, int height) {
    if (!m_ctx || !m_frame || !m_pkt) return;
    if (width != m_ctx->width || height != m_ctx->height) {
        qWarning() << "Sender: Frame size mismatch, reinitializing encoder";
        init(width, height, m_fps, m_bitrateMbps);
        if (!m_ctx) return;
    }

    if (nv12Data.size() < qint64(width) * height * 3 / 2 || av_frame_make_writable(m_frame) < 0) {
        emit error("Invalid or unwritable encoder frame"); return;
    }

    // Copy NV12 data into AVFrame
    int ySize = width * height;
    const uint8_t* src = reinterpret_cast<const uint8_t*>(nv12Data.constData());

    // Y plane
    for (int y = 0; y < height; y++) {
        memcpy(m_frame->data[0] + y * m_frame->linesize[0], src + y * width, width);
    }
    // UV plane
    const uint8_t* uvSrc = src + ySize;
    for (int y = 0; y < height / 2; y++) {
        memcpy(m_frame->data[1] + y * m_frame->linesize[1], uvSrc + y * width, width);
    }

    m_frame->pts = m_frameCount++;

    if (m_forceKeyframe) {
        m_frame->pict_type = AV_PICTURE_TYPE_I;
        m_frame->flags |= AV_FRAME_FLAG_KEY;
        m_forceKeyframe = false;
    } else {
        m_frame->pict_type = AV_PICTURE_TYPE_NONE;
        m_frame->flags &= ~AV_FRAME_FLAG_KEY;
    }

    int ret = avcodec_send_frame(m_ctx, m_frame);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        emit error(QString("H.264 encode failed: %1").arg(errbuf));
        return;
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(m_ctx, m_pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) {
            char errbuf[256]; av_strerror(ret, errbuf, sizeof(errbuf));
            emit error(QString("H.264 output failed: %1").arg(errbuf)); return;
        }

        // Build BetterCast video payload: [8B PTS nanoseconds][AVCC NALUs]
        // PTS in nanoseconds: frame_pts * (1e9 / fps)
        uint64_t ptsNanos = static_cast<uint64_t>(m_pkt->pts) * (1000000000ULL / m_fps);

        QByteArray payload;
        payload.reserve(8 + m_pkt->size + 64);

        // 8-byte PTS (written as raw bytes — receiver reads as uint64)
        ptsNanos = qToLittleEndian(ptsNanos);
        payload.append(reinterpret_cast<const char*>(&ptsNanos), 8);

        QByteArray avccData = annexBtoAVCC(m_pkt->data, m_pkt->size);
        if (avccData.isEmpty()) {
            av_packet_unref(m_pkt); emit error("Invalid H.264 encoder packet; reset stream"); return;
        }

        // Check if this is a keyframe — prepend cached SPS/PPS
        bool isKeyframe = (m_pkt->flags & AV_PKT_FLAG_KEY);
        if (isKeyframe && (m_sps.isEmpty() || m_pps.isEmpty())) {
            av_packet_unref(m_pkt); emit error("H.264 keyframe has no complete SPS/PPS; reset stream"); return;
        }
        if (isKeyframe && !m_sps.isEmpty() && !m_pps.isEmpty()) {
            // SPS as AVCC NALU
            int spsLen = m_sps.size();
            uint8_t lenBuf[4];
            lenBuf[0] = (spsLen >> 24) & 0xFF;
            lenBuf[1] = (spsLen >> 16) & 0xFF;
            lenBuf[2] = (spsLen >> 8) & 0xFF;
            lenBuf[3] = spsLen & 0xFF;
            payload.append(reinterpret_cast<const char*>(lenBuf), 4);
            payload.append(m_sps);

            // PPS as AVCC NALU
            int ppsLen = m_pps.size();
            lenBuf[0] = (ppsLen >> 24) & 0xFF;
            lenBuf[1] = (ppsLen >> 16) & 0xFF;
            lenBuf[2] = (ppsLen >> 8) & 0xFF;
            lenBuf[3] = ppsLen & 0xFF;
            payload.append(reinterpret_cast<const char*>(lenBuf), 4);
            payload.append(m_pps);
        }

        // Convert packet data from Annex-B to AVCC and append
        payload.append(avccData);

        ++m_outputPackets;
        av_packet_unref(m_pkt);
        emit encoded(payload);
        if (!m_ctx || !m_pkt) return; // signal handlers may stop/reset the whole stream
    }
}

void VideoEncoderFF::requestKeyframe() {
    m_forceKeyframe = true;
}

void VideoEncoderFF::shutdown() {
    if (m_pkt) { av_packet_free(&m_pkt); m_pkt = nullptr; }
    if (m_frame) { av_frame_free(&m_frame); m_frame = nullptr; }
    if (m_ctx) { avcodec_free_context(&m_ctx); m_ctx = nullptr; }
    m_sps.clear();
    m_pps.clear();
    m_nalLengthSize = 4;
    m_lengthPrefixedPackets = false;
    m_frameCount = 0; m_outputPackets = 0;
    m_encoderName.clear();
}
