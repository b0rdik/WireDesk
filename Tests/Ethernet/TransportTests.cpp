#include "NetworkSender.h"
#include <QCoreApplication>
#include <QFile>
#include <QTimer>
#include <QElapsedTimer>
#include <QNetworkInterface>
#include <QtEndian>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 5) return 2; // pin, framed fixture, loopback-interface, good|bad|forbidden
    QFile file(argv[2]); if (!file.open(QIODevice::ReadOnly)) return 2;
    QVector<QByteArray> frames;
    while (!file.atEnd()) {
        auto header = file.read(4); if (header.size() != 4) return 3;
        auto size = qFromBigEndian<quint32>(header.constData());
        if (size > 4*1024*1024) return 3;
        auto payload = file.read(size); if (payload.size() != size) return 3;
        frames.append(payload);
    }
    if (frames.size() != 240) return 3;
    NetworkSender sender;
    const QString mode = argv[4];
    EthernetConfig config; config.enabled = true; config.localIP = "127.0.0.1";
    config.interfaceName = argv[3]; config.certificateSHA256 = argv[1];
    if (mode == "bad") config.certificateSHA256 = QString(64, '0');
    if (mode == "forbidden") { config.interfaceName = "definitely-no-ethernet"; config.localIP = "192.0.2.254"; }
    sender.setEthernetConfig(config); sender.setStreamMode(120, 100);
    bool connected = false;
    QTimer paced; paced.setSingleShot(true); paced.setTimerType(Qt::PreciseTimer);
    QElapsedTimer clock; int index = 0; int connections = 0;
    QObject::connect(&paced, &QTimer::timeout, &app, [&] {
        if (!sender.isConnected()) return;
        sender.sendVideo(frames[index++]);
        if (mode == "reconnect" && connections == 1 && index == 120) {
            sender.resetStream(); return; // entire stream reset; next session starts at IDR/SPS/PPS
        }
        if (index == frames.size()) {
            QTimer::singleShot(500, &app, [&] {
                if (sender.rttMS() < 0) std::exit(4);
                std::printf("PASS Qt TLS pinned transport: 240 moving payloads paced at 120 FPS, RTT %.1fms; %s\n",
                    sender.rttMS(), qPrintable(sender.endpoints()));
                if (mode == "reconnect" && connections != 2) std::exit(7);
                if (mode == "reconnect") std::printf("PASS reconnect resumed with a fresh keyframe session\n");
                sender.disconnect(); app.exit(0);
            });
        } else {
            const qint64 delay = index * 1000000000LL / 120 - clock.nsecsElapsed();
            paced.start(int(qMax<qint64>(1, (delay + 999999) / 1000000)));
        }
    });
    QObject::connect(&sender, &NetworkSender::connected, &app, [&] {
        if (mode != "good" && mode != "reconnect" && mode != "overload") std::exit(5);
        ++connections;
        if (mode == "overload" && connections > 1) std::exit(5);
        connected = true; index = 0;
        sender.sendControl({{"op", "format"}, {"width", 1920}, {"height", 1080}, {"fps", 120}, {"codec", "h264"}, {"encoder", "fixture"}});
        if (mode == "overload") { sender.sendVideo(QByteArray(4 * 1024 * 1024 + 1, 'x')); return; }
        clock.start(); paced.start(1);
    });
    auto handleError = [&](const QString& error) {
        std::printf("Transport status: %s\n", qPrintable(error)); fflush(stdout);
        if ((mode == "bad" || mode == "forbidden") && !connected || mode == "overload" && error.contains("queue limit")) {
            sender.disconnect();
            QTimer::singleShot(2500, &app, [&] {
                std::printf("PASS rejected %s; no connection, Stop cancels retry\n", qPrintable(mode)); app.exit(0);
            });
        }
    };
    QObject::connect(&sender, &NetworkSender::error, &app, handleError);
    QObject::connect(&sender, &NetworkSender::fatalError, &app, handleError);
    QTimer::singleShot(15000, &app, [&] { std::fprintf(stderr, "FAIL TLS timeout\n"); app.exit(6); });
    sender.connectTo("127.0.0.1", 51820);
    return app.exec();
}
