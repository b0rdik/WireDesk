#include "VideoEncoderFF.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QtEndian>
#include <cstdio>
#include <cstdlib>

struct EncoderFramingTests {
    static void run() {
        VideoEncoderFF encoder;
        auto convert = [&](const QByteArray& bytes) {
            return encoder.annexBtoAVCC(reinterpret_cast<const uint8_t*>(bytes.constData()), bytes.size());
        };
        auto configure = [&](const QByteArray& bytes) {
            return encoder.cacheParameterSets(reinterpret_cast<const uint8_t*>(bytes.constData()), bytes.size());
        };
        const auto annex = QByteArray::fromHex("000000016764abcd00000168eeaa");
        const auto avcc = QByteArray::fromHex("000000046764abcd0000000368eeaa");
        if (convert(annex) != avcc || encoder.m_sps.toHex() != "6764abcd" || encoder.m_pps.toHex() != "68eeaa") std::exit(10);
        // avcC advertises two-byte lengths; retain the complete final PPS.
        const auto config = QByteArray::fromHex("0164001ffde100046764abcd01000368eeaa");
        if (!configure(config) || encoder.m_nalLengthSize != 2 || encoder.m_pps.toHex() != "68eeaa") std::exit(11);
        if (convert(QByteArray::fromHex("000109000365abcd")) != QByteArray::fromHex("00000001090000000365abcd")) std::exit(12);
        if (!convert(QByteArray::fromHex("0004abcd")).isEmpty() || !convert(QByteArray::fromHex("0000")).isEmpty()) std::exit(13);
        if (configure(config.left(config.size() - 1))) std::exit(14);
        auto config4 = config; config4[4] = char(0xff);
        if (!configure(config4) || convert(QByteArray::fromHex("0000000109")) != QByteArray::fromHex("0000000109")) std::exit(15);
        std::printf("PASS mixed Annex-B start codes, complete SPS/PPS, avcC 2/4-byte packet lengths and malformed packet rejection\n");
    }
};

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    EncoderFramingTests::run();
    VideoEncoderFF encoder;
    int outputs = 0, idrs = 0;
    QObject::connect(&encoder, &VideoEncoderFF::encoded, &app, [&](const QByteArray& payload) {
        if (payload.size() < 13) std::exit(2);
        const auto pts = qFromLittleEndian<quint64>(payload.constData());
        if (pts != quint64(outputs) * (1000000000ULL / 60)) std::exit(3);
        bool idr = false, sps = false, pps = false;
        int offset = 8;
        while (offset + 4 <= payload.size()) {
            auto length = qFromBigEndian<quint32>(payload.constData() + offset);
            if (!length || length > quint32(payload.size() - offset - 4)) std::exit(4);
            auto type = quint8(payload[offset + 4]) & 31;
            idr |= type == 5; sps |= type == 7; pps |= type == 8;
            offset += 4 + int(length);
        }
        if (offset != payload.size() || idr && (!sps || !pps)) std::exit(5);
        if (idr) ++idrs;
        ++outputs;
    });
    if (encoder.init(641, 360, 60, 20)) return 6;
    if (!encoder.init(640, 360, 60, 20)) return 7;
    const auto name = encoder.encoderName();
    QByteArray image(640 * 360 * 3 / 2, char(128));
    QElapsedTimer elapsed; elapsed.start();
    for (int index = 0; index < 60; ++index) {
        image[index * 7] = char(index);
        if (index == 0 || index == 30) encoder.requestKeyframe();
        encoder.encode(image, 640, 360);
    }
    if (outputs != 60 || idrs < 2) return 8;
    std::printf("PASS encoder %s: 60 valid AVCC payloads, %d IDRs with SPS/PPS, %.1fms batch; invalid odd mode rejected\n",
        qPrintable(name), idrs, elapsed.nsecsElapsed() / 1000000.0);
    if (encoder.init(640, 360, 120, 20) && encoder.encoderName() == "libx264") return 9;
    std::printf("PASS 120fps never silently falls back to libx264\n");
    return 0;
}
