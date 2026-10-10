#include "WireDeskUpdater.h"
#include "Version.h"
#include <QCoreApplication>
#include <QSettings>
#include <QTimer>
std::atomic_bool WireDeskUpdater::s_sessionActive{false};
WireDeskUpdater::WireDeskUpdater(QObject* parent) : QObject(parent) {}
WireDeskUpdater::~WireDeskUpdater() {
    if (m_initialized) { auto cleanup = reinterpret_cast<void (*)()>(m_library.resolve("win_sparkle_cleanup")); if (cleanup) cleanup(); }
}
int WireDeskUpdater::canShutdown() { return !s_sessionActive.load(); }
void WireDeskUpdater::requestShutdown() {
    if (s_sessionActive.load()) return;
    QMetaObject::invokeMethod(QCoreApplication::instance(), [] { QCoreApplication::quit(); }, Qt::QueuedConnection);
}
void WireDeskUpdater::setSessionActive(bool active) { s_sessionActive.store(active); }
void WireDeskUpdater::start() {
#ifdef _WIN32
    if (m_initialized) return;
    m_library.setFileName(QCoreApplication::applicationDirPath() + "/WinSparkle.dll");
    if (!m_library.load()) { m_status = "WinSparkle.dll отсутствует. Используйте полный установщик WireDesk."; return; }
    using Text = void (*)(const char*); using Int = void (*)(int); using Void = void (*)();
    auto feed = reinterpret_cast<Text>(m_library.resolve("win_sparkle_set_appcast_url"));
    auto key = reinterpret_cast<int (*)(const char*)>(m_library.resolve("win_sparkle_set_eddsa_public_key"));
    auto details = reinterpret_cast<void (*)(const wchar_t*, const wchar_t*, const wchar_t*)>(m_library.resolve("win_sparkle_set_app_details"));
    auto shutdown = reinterpret_cast<void (*)(int (*)())>(m_library.resolve("win_sparkle_set_can_shutdown_callback"));
    auto request = reinterpret_cast<void (*)(void (*)())>(m_library.resolve("win_sparkle_set_shutdown_request_callback"));
    auto initialize = reinterpret_cast<Void>(m_library.resolve("win_sparkle_init"));
    auto automatic = reinterpret_cast<Int>(m_library.resolve("win_sparkle_set_automatic_check_for_updates"));
    auto interval = reinterpret_cast<Int>(m_library.resolve("win_sparkle_set_update_check_interval"));
    if (!feed || !key || !details || !shutdown || !request || !initialize || !automatic || !interval ||
        !key("VEYSCJQc+66Sr3KML1c5zUnadF58jarTjNG0mzFRCC4=")) { m_status = "Требуется WinSparkle 0.9.4 с Ed25519-подписью."; m_library.unload(); return; }
    feed("https://github.com/b0rdik/WireDesk/releases/latest/download/windows-appcast.xml");
    const auto version = QStringLiteral(BC_VERSION_STRING).toStdWString();
    details(L"b0rdik", L"WireDesk", version.c_str());
    shutdown(&WireDeskUpdater::canShutdown); request(&WireDeskUpdater::requestShutdown);
    automatic(QSettings().value("updates/automatic", true).toBool() ? 1 : 0); interval(86400);
    initialize(); m_initialized = true;
    m_status = "Подписанные OTA-обновления готовы. Установка после подтверждения, вне активного сеанса.";
#else
    m_status = "Windows OTA использует WinSparkle; на Mac-приложении доступен Sparkle.";
#endif
}
void WireDeskUpdater::check() {
    if (s_sessionActive.load()) { m_status = "Остановите активный сеанс перед установкой обновления."; return; }
    start();
    if (m_initialized) { auto check = reinterpret_cast<void (*)()>(m_library.resolve("win_sparkle_check_update_with_ui")); if (check) check(); }
}
void WireDeskUpdater::setAutomatic(bool enabled) {
    QSettings().setValue("updates/automatic", enabled);
    if (m_initialized) { auto set = reinterpret_cast<void (*)(int)>(m_library.resolve("win_sparkle_set_automatic_check_for_updates")); if (set) set(enabled ? 1 : 0); }
}
