#include "VirtualDisplayVDD.h"
#include "../MainWindow.h"
#ifdef _WIN32
#include <Windows.h>
#include <dxgi.h>
#endif
#define VDD_LOG(msg) LogManager::instance().log(msg)

VirtualDisplayVDD::VirtualDisplayVDD(QObject* parent) : QObject(parent) { refreshInstallStatus(); }
VirtualDisplayVDD::~VirtualDisplayVDD() = default;
bool VirtualDisplayVDD::isVddInstalled() const { return m_vddInstalled; }
QString VirtualDisplayVDD::vddInstallPath() const { return m_vddPath; }
void VirtualDisplayVDD::refreshInstallStatus() {
    m_vddInstalled = false;
    for (const auto& monitor : enumerateMonitors()) if (monitor.isVirtual) m_vddInstalled = true;
    emit statusChanged(m_vddInstalled ? "Installed virtual monitor detected" : "Install/configure one VDD monitor explicitly; see docs/ethernet-second-display.md");
}

bool VirtualDisplayVDD::createVirtualDisplay(int width, int height, int refreshRate) {
    // VDD 25.7.23 exposes monitors at driver initialization. There is no JSON add/remove API.
    // Reuse exactly one existing device. Never rewrite its XML or create an extra device node.
    QVector<MonitorInfo> virtualMonitors;
    for (const auto& monitor : enumerateMonitors()) if (monitor.isVirtual) virtualMonitors.append(monitor);
    if (virtualMonitors.size() != 1) {
        emit error("Configure exactly one VDD monitor using its installer/settings, then enable Extend in Windows Display Settings"); return false;
    }
#ifdef _WIN32
    const auto device = virtualMonitors.first().name;
    DEVMODEW current = {}; current.dmSize = sizeof(current);
    if (!EnumDisplaySettingsW(reinterpret_cast<LPCWSTR>(device.utf16()), ENUM_CURRENT_SETTINGS, &current)) {
        emit error("Virtual monitor is not active. Choose Extend desktop in Windows Display Settings"); return false;
    }
    bool supported = false;
    DEVMODEW enumerated = {}; enumerated.dmSize = sizeof(enumerated);
    for (DWORD i = 0; EnumDisplaySettingsW(reinterpret_cast<LPCWSTR>(device.utf16()), i, &enumerated); ++i) {
        if (int(enumerated.dmPelsWidth) == width && int(enumerated.dmPelsHeight) == height &&
            int(enumerated.dmDisplayFrequency) == refreshRate && enumerated.dmBitsPerPel == 32) { supported = true; break; }
    }
    if (!supported) { emit error("Driver does not advertise this resolution/refresh rate; configure supported VDD modes first"); return false; }
    DEVMODEW requested = current; // preserve desktop position and orientation
    requested.dmPelsWidth = width; requested.dmPelsHeight = height; requested.dmDisplayFrequency = refreshRate;
    requested.dmFields |= DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
    const auto name = reinterpret_cast<LPCWSTR>(device.utf16());
    if (ChangeDisplaySettingsExW(name, &requested, nullptr, CDS_TEST, nullptr) != DISP_CHANGE_SUCCESSFUL ||
        ChangeDisplaySettingsExW(name, &requested, nullptr, 0, nullptr) != DISP_CHANGE_SUCCESSFUL) {
        emit error("Windows refused display mode; previous configuration retained"); return false;
    }
    if (!verifyDisplayMode(device, width, height, refreshRate)) {
        ChangeDisplaySettingsExW(name, &current, nullptr, 0, nullptr);
        emit error("Actual display mode differs from request; previous mode restored"); return false;
    }
    emit statusChanged(QString("Reused %1: verified %2×%3 @ %4 Hz").arg(device).arg(width).arg(height).arg(refreshRate));
    emit virtualDisplayCreated(findVirtualDisplayOutput());
    return true;
#else
    Q_UNUSED(width); Q_UNUSED(height); Q_UNUSED(refreshRate); return false;
#endif
}
bool VirtualDisplayVDD::verifyDisplayMode(const QString& device, int width, int height, int refreshRate) const {
#ifdef _WIN32
    DEVMODEW mode = {}; mode.dmSize = sizeof(mode);
    if (device.isEmpty() || !EnumDisplaySettingsW(reinterpret_cast<LPCWSTR>(device.utf16()), ENUM_CURRENT_SETTINGS, &mode) ||
        (width > 0 && int(mode.dmPelsWidth) != width) || (height > 0 && int(mode.dmPelsHeight) != height) ||
        int(mode.dmDisplayFrequency) != refreshRate) return false;
    UINT pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return false;
    QVector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    QVector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS) return false;
    for (UINT i = 0; i < pathCount; ++i) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = paths[i].sourceInfo.adapterId;
        source.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS ||
            QString::fromWCharArray(source.viewGdiDeviceName) != device) continue;
        for (UINT j = 0; j < pathCount; ++j) {
            if (j == i) continue;
            const auto& a = paths[i].sourceInfo; const auto& b = paths[j].sourceInfo;
            if (a.id == b.id && a.adapterId.HighPart == b.adapterId.HighPart && a.adapterId.LowPart == b.adapterId.LowPart)
                return false; // clone paths share a source; this must be an extended desktop
        }
        const auto rate = paths[i].targetInfo.refreshRate;
        return rate.Denominator != 0 && qAbs(double(rate.Numerator) / rate.Denominator - refreshRate) < 0.5;
    }
    return false;
#else
    Q_UNUSED(device); Q_UNUSED(width); Q_UNUSED(height); Q_UNUSED(refreshRate); return false;
#endif
}
bool VirtualDisplayVDD::removeVirtualDisplay(int index) {
    Q_UNUSED(index); emit error("Disable/remove the installed monitor explicitly with Windows Settings or the VDD installer"); return false;
}
bool VirtualDisplayVDD::removeAllVirtualDisplays() { return removeVirtualDisplay(); }
int VirtualDisplayVDD::virtualDisplayCount() const {
    int count = 0; for (const auto& monitor : enumerateMonitors()) if (monitor.isVirtual) ++count; return count;
}

QVector<VirtualDisplayVDD::MonitorInfo> VirtualDisplayVDD::enumerateMonitors() const {
    QVector<MonitorInfo> result;

#ifdef _WIN32
    // Method 1: DXGI enumeration (only sees attached/active displays)
    IDXGIFactory1* factory = nullptr;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory);
    if (FAILED(hr)) {
        VDD_LOG("VDD: CreateDXGIFactory1 failed");
    } else {
        IDXGIAdapter1* adapter = nullptr;
        for (UINT adapterIdx = 0;
             factory->EnumAdapters1(adapterIdx, &adapter) != DXGI_ERROR_NOT_FOUND;
             adapterIdx++) {

            DXGI_ADAPTER_DESC1 adapterDesc;
            adapter->GetDesc1(&adapterDesc);
            QString adapterName = QString::fromWCharArray(adapterDesc.Description);

            IDXGIOutput* output = nullptr;
            for (UINT outputIdx = 0;
                 adapter->EnumOutputs(outputIdx, &output) != DXGI_ERROR_NOT_FOUND;
                 outputIdx++) {

                DXGI_OUTPUT_DESC outputDesc;
                output->GetDesc(&outputDesc);

                MonitorInfo info;
                info.adapterIndex = static_cast<int>(adapterIdx);
                info.outputIndex = static_cast<int>(outputIdx);
                info.name = QString::fromWCharArray(outputDesc.DeviceName);
                info.adapterName = adapterName;
                info.width = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
                info.height = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;

                // Detect virtual displays by adapter name patterns
                QString lowerAdapter = adapterName.toLower();
                info.isVirtual = lowerAdapter == "virtual display driver" || lowerAdapter.contains("mttvdd") || lowerAdapter.contains("iddsampledriver");

                result.append(info);
                output->Release();
            }
            adapter->Release();
        }
        factory->Release();
    }

    // Method 2: EnumDisplayDevices — finds ALL displays including disconnected virtual ones
    // that DXGI misses. This is needed because virtual displays may not be "attached"
    // to the desktop yet.
    DISPLAY_DEVICEW dd = {};
    dd.cb = sizeof(dd);
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); i++) {
        // Skip inactive devices unless they look virtual
        QString devName = QString::fromWCharArray(dd.DeviceName);
        QString devString = QString::fromWCharArray(dd.DeviceString);
        QString lowerString = devString.toLower();

        bool isVirtual = lowerString == "virtual display driver" || lowerString.contains("mttvdd") || lowerString.contains("iddsampledriver");

        // Check if this device is already in our DXGI results
        bool alreadyFound = false;
        for (const auto& existing : result) {
            if (existing.name == devName) {
                alreadyFound = true;
                break;
            }
        }

        if (!alreadyFound && isVirtual) {
            MonitorInfo info;
            info.adapterIndex = -1;
            info.outputIndex = -1;
            info.name = devName;
            info.adapterName = devString;
            info.isVirtual = true;

            // Get resolution from display settings
            DEVMODEW dm = {};
            dm.dmSize = sizeof(dm);
            if (EnumDisplaySettingsW(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm)) {
                info.width = static_cast<int>(dm.dmPelsWidth);
                info.height = static_cast<int>(dm.dmPelsHeight);
            } else if (EnumDisplaySettingsW(dd.DeviceName, ENUM_REGISTRY_SETTINGS, &dm)) {
                info.width = static_cast<int>(dm.dmPelsWidth);
                info.height = static_cast<int>(dm.dmPelsHeight);
            } else {
                // Use requested resolution from settings
                info.width = 0;
                info.height = 0;
            }

            VDD_LOG(QString("VDD: Found virtual display via EnumDisplayDevices: %1 (%2) %3x%4 flags=0x%5")
                        .arg(devName, devString).arg(info.width).arg(info.height)
                        .arg(dd.StateFlags, 0, 16));
            result.append(info);
        }
        dd = {};
        dd.cb = sizeof(dd);
    }
#endif

    return result;
}

int VirtualDisplayVDD::findVirtualDisplayOutput() const {
    auto monitors = enumerateMonitors();
    // Return the last virtual display found (most recently created)
    for (int i = monitors.size() - 1; i >= 0; i--) {
        if (monitors[i].isVirtual) {
            return i;
        }
    }
    return -1;
}
