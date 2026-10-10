#include <QApplication>
#include <QSurfaceFormat>
#include <QIcon>
#include <QProcess>
#include <QStandardPaths>
#include <QFile>
#include <QDebug>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QDir>
#include <QCryptographicHash>
#include "MainWindow.h"
#include "Version.h"


int main(int argc, char* argv[]) {
    // Use Compatibility Profile for GL_LUMINANCE/GL_LUMINANCE_ALPHA support
    // Core Profile removes these, breaking NV12 texture uploads on Windows
    QSurfaceFormat format;
    format.setVersion(2, 1);
    format.setProfile(QSurfaceFormat::CompatibilityProfile);
    format.setSwapInterval(1); // VSync
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    app.setApplicationName("WireDesk");
    app.setOrganizationName("WireDesk");
    app.setApplicationVersion(QStringLiteral(BC_VERSION_STRING));
    app.setWindowIcon(QIcon(":/appicon.png"));



    QString pairing;
    const auto args = app.arguments();
    int index = args.indexOf("--pair");
    if(index >= 0 && index+1 < args.size()) pairing=args[index+1];
    index = args.indexOf("--pair-file");
    if(index >= 0 && index+1 < args.size()) {
        QFile file(args[index+1]);
        if(file.open(QIODevice::ReadOnly) && file.size() <= 4096) pairing=QString::fromUtf8(file.readAll());
    }
    const auto stateDir=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(stateDir);
    QLockFile instanceLock(stateDir+"/instance.lock");
    instanceLock.setStaleLockTime(0);
    QLocalServer server;
    const auto serverName=QStringLiteral("WireDesk-")+QString::fromLatin1(QCryptographicHash::hash(stateDir.toUtf8(),QCryptographicHash::Sha256).toHex().left(16));
    if (!instanceLock.tryLock(100)) {
        QLocalSocket socket; socket.connectToServer(serverName);
        if(socket.waitForConnected(2000)) {
            socket.write(pairing.toUtf8()+"\n"); socket.waitForBytesWritten(2000);
        }
        return 0;
    }
    QLocalServer::removeServer(serverName);
    server.setSocketOptions(QLocalServer::UserAccessOption);
    server.listen(serverName);
    MainWindow window;
    window.resize(1120,820);
    window.show();
    if(!pairing.isEmpty())window.importPairingCode(pairing);
    QObject::connect(&server,&QLocalServer::newConnection,&window,[&] {
        auto* socket=server.nextPendingConnection();
        if(!socket)return;
        auto* data=new QByteArray;
        const auto readPairing=[socket,data,&window] {
            data->append(socket->readAll());
            if(data->size()>4097) { socket->disconnectFromServer(); return; }
            if(data->endsWith('\n')) {
                const auto code=QString::fromUtf8(*data).trimmed();
                if(!code.isEmpty())window.importPairingCode(code);
                window.showNormal(); window.raise(); window.activateWindow();
                socket->disconnectFromServer();
            }
        };
        QObject::connect(socket,&QLocalSocket::readyRead,&window,readPairing);
        if(socket->bytesAvailable())readPairing();
        QObject::connect(socket,&QLocalSocket::disconnected,socket,[socket,data]{ delete data; socket->deleteLater(); });
    });

    return app.exec();
}
