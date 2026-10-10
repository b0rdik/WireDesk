#pragma once
#include <QObject>
#include <QLibrary>
#include <atomic>
class WireDeskUpdater : public QObject {
    Q_OBJECT
public:
    explicit WireDeskUpdater(QObject* parent = nullptr);
    ~WireDeskUpdater();
    void start();
    void check();
    void setSessionActive(bool active);
    void setAutomatic(bool enabled);
    QString status() const { return m_status; }
private:
    QLibrary m_library;
    bool m_initialized = false;
    QString m_status = "Обновления ещё не запущены";
    static std::atomic_bool s_sessionActive;
    static int canShutdown();
    static void requestShutdown();
};
