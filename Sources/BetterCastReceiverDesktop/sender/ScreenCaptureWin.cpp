#include "ScreenCaptureWin.h"
#include "../MainWindow.h"  // LogManager

#include <d3d11.h>
#include <dxgi1_2.h>
#include <Windows.h>
#include <QDebug>
extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/pixfmt.h>
}
#include <algorithm>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "gdi32.lib")

ScreenCaptureWin::ScreenCaptureWin(int targetFPS, QObject* parent)
    : ScreenCapture(parent)
    , m_targetFPS(targetFPS)
{
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, [this] { captureFrame(); if (m_running) scheduleNext(); });
}

void ScreenCaptureWin::setMonitorIndex(int adapterIndex, int outputIndex) {
    m_adapterIndex = adapterIndex;
    m_outputIndex = outputIndex;
}

ScreenCaptureWin::~ScreenCaptureWin() {
    stop();
}

bool ScreenCaptureWin::initD3D() {
    D3D_FEATURE_LEVEL featureLevel;
    UINT flags = 0;
#ifdef QT_DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    // Get the specific adapter for the selected monitor
    IDXGIFactory1* factory = nullptr;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory);
    if (FAILED(hr)) {
        qWarning() << "Sender: CreateDXGIFactory1 failed";
        return false;
    }

    IDXGIAdapter1* selectedAdapter = nullptr;
    hr = factory->EnumAdapters1(m_adapterIndex, &selectedAdapter);
    factory->Release();

    if (FAILED(hr)) {
        qWarning() << "Sender: Adapter" << m_adapterIndex << "not found, falling back to default";
        return false;
    }

    hr = D3D11CreateDevice(
        selectedAdapter,            // specific adapter (or nullptr for default)
        selectedAdapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        nullptr, 0,                 // default feature levels
        D3D11_SDK_VERSION,
        &m_device,
        &featureLevel,
        &m_context
    );

    if (selectedAdapter) selectedAdapter->Release();

    if (FAILED(hr)) {
        qWarning() << "Sender: D3D11CreateDevice failed, hr=" << Qt::hex << hr;
        return false;
    }
    qDebug() << "Sender: D3D11 device created on adapter" << m_adapterIndex
             << ", feature level:" << Qt::hex << featureLevel;
    return true;
}

bool ScreenCaptureWin::initDuplication() {
    // Get DXGI device → adapter → output → output1 → duplicate
    IDXGIDevice* dxgiDevice = nullptr;
    HRESULT hr = m_device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
    if (FAILED(hr)) { qWarning() << "Sender: QueryInterface IDXGIDevice failed"; return false; }

    IDXGIAdapter* adapter = nullptr;
    hr = dxgiDevice->GetAdapter(&adapter);
    dxgiDevice->Release();
    if (FAILED(hr)) { qWarning() << "Sender: GetAdapter failed"; return false; }

    IDXGIOutput* output = nullptr;
    hr = adapter->EnumOutputs(m_outputIndex, &output);  // selected monitor
    adapter->Release();
    if (FAILED(hr)) {
        qWarning() << "Sender: EnumOutputs failed for output" << m_outputIndex;
        return false;
    }

    DXGI_OUTPUT_DESC selectedDesc = {};
    output->GetDesc(&selectedDesc);
    if (!m_displayName.isEmpty() && QString::fromWCharArray(selectedDesc.DeviceName) != m_displayName) {
        output->Release(); return false; // never capture a different display after topology changes
    }
    m_originX = selectedDesc.DesktopCoordinates.left; m_originY = selectedDesc.DesktopCoordinates.top;
    IDXGIOutput1* output1 = nullptr;
    hr = output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1);
    output->Release();
    if (FAILED(hr)) { qWarning() << "Sender: QueryInterface IDXGIOutput1 failed"; return false; }

    hr = output1->DuplicateOutput(m_device, &m_duplication);
    output1->Release();
    if (FAILED(hr)) {
        qWarning() << "Sender: DuplicateOutput failed, hr=" << Qt::hex << hr;
        return false;
    }

    // Get desktop dimensions
    DXGI_OUTDUPL_DESC desc;
    m_duplication->GetDesc(&desc);
    m_resolution = QSize(desc.ModeDesc.Width, desc.ModeDesc.Height);
    qDebug() << "Sender: Desktop duplication ready," << m_resolution;

    // Create staging texture for CPU readback (BGRA → we'll convert to NV12)
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = m_resolution.width();
    texDesc.Height = m_resolution.height();
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_STAGING;
    texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    hr = m_device->CreateTexture2D(&texDesc, nullptr, &m_stagingTex);
    if (FAILED(hr)) { qWarning() << "Sender: CreateTexture2D staging failed"; return false; }

    return true;
}

bool ScreenCaptureWin::start() {
    if (m_running) return true;

    m_useGdiFallback = false;

    if (!initD3D()) {
        LogManager::instance().log("Sender: D3D11 init failed, trying GDI fallback");
        if (!initGdiFallback()) {
            emit error("Failed to initialize screen capture (both DXGI and GDI failed)");
            return false;
        }
    } else if (!initDuplication()) {
        LogManager::instance().log("Sender: DXGI Desktop Duplication unavailable (virtual display?), using GDI fallback");
        cleanup();  // Release D3D resources
        if (!initGdiFallback()) {
            emit error("Failed to initialize screen capture (DXGI unsupported, GDI failed)");
            return false;
        }
    }

    if (m_resolution.width() % 2 || m_resolution.height() % 2) {
        cleanup(); emit error("NV12 requires even display dimensions"); return false;
    }
    m_running = true;
    m_clock.start(); m_tick = 0;
    scheduleNext();
    if (m_useGdiFallback) {
        LogManager::instance().log(QString("Sender: Screen capture started (GDI mode) at %1 FPS, %2x%3")
                                       .arg(m_targetFPS).arg(m_resolution.width()).arg(m_resolution.height()));
    } else {
        LogManager::instance().log(QString("Sender: Screen capture started (DXGI) at %1 FPS").arg(m_targetFPS));
    }
    return true;
}

void ScreenCaptureWin::stop() {
    m_running = false;
    m_timer.stop();
    cleanup();
}

bool ScreenCaptureWin::initGdiFallback() {
    // Use GDI to capture a specific display by name (works with virtual displays)
    if (m_displayName.isEmpty()) {
        // Fall back to primary display
        m_gdiDC = CreateDCA("DISPLAY", nullptr, nullptr, nullptr);
    } else {
        m_gdiDC = CreateDCW(L"DISPLAY", reinterpret_cast<LPCWSTR>(m_displayName.utf16()), nullptr, nullptr);
    }


    if (!m_gdiDC) {
        LogManager::instance().log("Sender: GDI CreateDC failed");
        return false;
    }

    int w = GetDeviceCaps(m_gdiDC, HORZRES);
    int h = GetDeviceCaps(m_gdiDC, VERTRES);

    if (w <= 0 || h <= 0) {
        LogManager::instance().log(QString("Sender: GDI invalid resolution: %1x%2").arg(w).arg(h));
        DeleteDC(m_gdiDC);
        m_gdiDC = nullptr;
        return false;
    }

    m_memDC = CreateCompatibleDC(m_gdiDC);
    m_bitmap = CreateCompatibleBitmap(m_gdiDC, w, h);
    if (!m_memDC || !m_bitmap) { cleanup(); return false; }
    m_originalBitmap = static_cast<HBITMAP>(SelectObject(m_memDC, m_bitmap));

    DEVMODEW mode = {}; mode.dmSize = sizeof(mode);
    if (!m_displayName.isEmpty() && EnumDisplaySettingsW(reinterpret_cast<LPCWSTR>(m_displayName.utf16()), ENUM_CURRENT_SETTINGS, &mode)) {
        m_originX = mode.dmPosition.x; m_originY = mode.dmPosition.y;
    }
    m_resolution = QSize(w, h);
    m_useGdiFallback = true; m_overlayPointer = true;

    LogManager::instance().log(QString("Sender: GDI capture initialized for %1 (%2x%3)")
                                   .arg(m_displayName.isEmpty() ? "primary" : m_displayName)
                                   .arg(w).arg(h));
    return true;
}

void ScreenCaptureWin::captureFrameGdi() {
    if (!m_running || !m_gdiDC || !m_memDC) return;

    int w = m_resolution.width();
    int h = m_resolution.height();

    // Capture screen to memory DC
    if (!BitBlt(m_memDC, 0, 0, w, h, m_gdiDC, 0, 0, SRCCOPY | CAPTUREBLT)) return;

    // Read pixel data from bitmap
    BITMAPINFOHEADER bi = {};
    bi.biSize = sizeof(bi);
    bi.biWidth = w;
    bi.biHeight = -h;  // Top-down
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    QByteArray bgraData(w * h * 4, Qt::Uninitialized);
    HGDIOBJ previous = SelectObject(m_memDC, m_originalBitmap);
    int rows = GetDIBits(m_gdiDC, m_bitmap, 0, h, bgraData.data(),
              reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);
    SelectObject(m_memDC, previous);
    if (rows != h) return;

    QByteArray nv12 = convertBGRA(reinterpret_cast<const uint8_t*>(bgraData.constData()), w * 4);
    if (nv12.isEmpty()) return;
    if (nv12 == m_previousNV12) return; // static GDI captures are not new video frames
    m_previousNV12 = nv12;
    emit frameCaptured(nv12, w, h);
}

void ScreenCaptureWin::captureFrame() {
    if (!m_running) return;

    if (m_useGdiFallback) {
        captureFrameGdi();
        return;
    }

    if (!m_duplication) return;

    IDXGIResource* desktopResource = nullptr;
    DXGI_OUTDUPL_FRAME_INFO frameInfo;

    HRESULT hr = m_duplication->AcquireNextFrame(0, &frameInfo, &desktopResource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return; // No new frame — desktop unchanged
    }
    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_ACCESS_LOST) {
            qDebug() << "Sender: Desktop duplication access lost, reinitializing...";
            if (m_duplication) { m_duplication->Release(); m_duplication = nullptr; }
            if (m_stagingTex) { m_stagingTex->Release(); m_stagingTex = nullptr; }
            if (!initDuplication()) {
                emit error("Failed to reinitialize desktop duplication");
                stop();
            }
        }
        return;
    }

    if (frameInfo.LastMouseUpdateTime.QuadPart != 0) m_overlayPointer = frameInfo.PointerPosition.Visible;
    // Copy desktop texture to staging texture for CPU read
    ID3D11Texture2D* desktopTex = nullptr;
    hr = desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&desktopTex);
    desktopResource->Release();
    if (FAILED(hr)) {
        m_duplication->ReleaseFrame();
        return;
    }

    m_context->CopyResource(m_stagingTex, desktopTex);
    desktopTex->Release();
    m_duplication->ReleaseFrame();

    // Map staging texture and convert BGRA → NV12
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = m_context->Map(m_stagingTex, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return;

    int w = m_resolution.width();
    int h = m_resolution.height();
    const uint8_t* bgra = static_cast<const uint8_t*>(mapped.pData);
    int pitch = mapped.RowPitch;

    QByteArray nv12 = convertBGRA(bgra, pitch);

    m_context->Unmap(m_stagingTex, 0);

    if (!nv12.isEmpty()) emit frameCaptured(nv12, w, h);
}

QByteArray ScreenCaptureWin::convertBGRA(const uint8_t* data, int pitch) {
    const int w = m_resolution.width(), h = m_resolution.height();
    m_sws = sws_getCachedContext(m_sws, w, h, AV_PIX_FMT_BGRA, w, h, AV_PIX_FMT_NV12,
                                SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_sws) return {};
    // Explicit BT.709 limited range on both encoder and receiver.
    const int* coefficients = sws_getCoefficients(SWS_CS_ITU709);
    sws_setColorspaceDetails(m_sws, coefficients, 1, coefficients, 0, 0, 1 << 16, 1 << 16);
    QByteArray nv12(w * h * 3 / 2, Qt::Uninitialized);
    const uint8_t* src[4] = {data, nullptr, nullptr, nullptr};
    int srcStride[4] = {pitch, 0, 0, 0};
    uint8_t* dst[4] = {reinterpret_cast<uint8_t*>(nv12.data()), reinterpret_cast<uint8_t*>(nv12.data()) + w * h, nullptr, nullptr};
    int dstStride[4] = {w, w, 0, 0};
    if (sws_scale(m_sws, src, srcStride, 0, h, dst, dstStride) != h) return {};
    if (m_overlayPointer) overlayCursor(nv12, data, pitch);
    return nv12;
}
void ScreenCaptureWin::overlayCursor(QByteArray& nv12, const uint8_t* bgra, int pitch) {
    CURSORINFO cursor = {}; cursor.cbSize = sizeof(cursor);
    if (!GetCursorInfo(&cursor) || !(cursor.flags & CURSOR_SHOWING)) return;
    ICONINFO icon = {};
    if (!GetIconInfo(cursor.hCursor, &icon)) return;
    BITMAP shape = {};
    GetObject(icon.hbmColor ? icon.hbmColor : icon.hbmMask, sizeof(shape), &shape);
    const int iconW = shape.bmWidth, iconH = icon.hbmColor ? shape.bmHeight : shape.bmHeight / 2;
    const int left = cursor.ptScreenPos.x - m_originX - int(icon.xHotspot);
    const int top = cursor.ptScreenPos.y - m_originY - int(icon.yHotspot);
    if (icon.hbmColor) DeleteObject(icon.hbmColor);
    if (icon.hbmMask) DeleteObject(icon.hbmMask);
    const int w = m_resolution.width(), h = m_resolution.height();
    const int x = std::max(0, left) & ~1, y = std::max(0, top) & ~1;
    const int endX = std::min(w, (left + iconW + 1) & ~1), endY = std::min(h, (top + iconH + 1) & ~1);
    const int cw = endX - x, ch = endY - y;
    if (cw <= 0 || ch <= 0 || iconW <= 0 || iconH <= 0) return;
    if (!m_cursorDC) m_cursorDC = CreateCompatibleDC(nullptr);
    if (!m_cursorDC) return;
    if (m_cursorSize != QSize(cw, ch)) {
        if (m_cursorOriginalBitmap) SelectObject(m_cursorDC, m_cursorOriginalBitmap);
        if (m_cursorBitmap) DeleteObject(m_cursorBitmap);
        BITMAPINFO info = {}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = cw; info.bmiHeader.biHeight = -ch;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        m_cursorBitmap = CreateDIBSection(m_cursorDC, &info, DIB_RGB_COLORS, &m_cursorBits, nullptr, 0);
        if (!m_cursorBitmap) { m_cursorBits = nullptr; return; }
        auto original = static_cast<HBITMAP>(SelectObject(m_cursorDC, m_cursorBitmap));
        if (!m_cursorOriginalBitmap) m_cursorOriginalBitmap = original;
        m_cursorSize = QSize(cw, ch);
    }
    for (int row = 0; row < ch; ++row)
        memcpy(static_cast<uint8_t*>(m_cursorBits) + row * cw * 4, bgra + (y + row) * pitch + x * 4, cw * 4);
    if (!DrawIconEx(m_cursorDC, left - x, top - y, cursor.hCursor, iconW, iconH, 0, nullptr, DI_NORMAL)) return;
    GdiFlush();
    m_cursorSws = sws_getCachedContext(m_cursorSws, cw, ch, AV_PIX_FMT_BGRA, cw, ch, AV_PIX_FMT_NV12, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_cursorSws) return;
    const int* coefficients = sws_getCoefficients(SWS_CS_ITU709);
    sws_setColorspaceDetails(m_cursorSws, coefficients, 1, coefficients, 0, 0, 1 << 16, 1 << 16);
    QByteArray converted(cw * ch * 3 / 2, Qt::Uninitialized);
    const uint8_t* src[4] = {static_cast<const uint8_t*>(m_cursorBits), nullptr, nullptr, nullptr};
    int srcStride[4] = {cw * 4, 0, 0, 0};
    uint8_t* dst[4] = {reinterpret_cast<uint8_t*>(converted.data()), reinterpret_cast<uint8_t*>(converted.data()) + cw * ch, nullptr, nullptr};
    int dstStride[4] = {cw, cw, 0, 0};
    if (sws_scale(m_cursorSws, src, srcStride, 0, ch, dst, dstStride) != ch) return;
    for (int row = 0; row < ch; ++row) memcpy(nv12.data() + (y + row) * w + x, dst[0] + row * cw, cw);
    for (int row = 0; row < ch / 2; ++row) memcpy(nv12.data() + w * h + (y / 2 + row) * w + x, dst[1] + row * cw, cw);
}
void ScreenCaptureWin::scheduleNext() {
    // Fractional 8.333ms cadence, absolute monotonic deadlines; never catch up with stale frames.
    const qint64 now = m_clock.nsecsElapsed();
    m_tick = std::max(m_tick + 1, now * m_targetFPS / 1000000000LL + 1);
    const qint64 deadline = m_tick * 1000000000LL / m_targetFPS;
    m_timer.start(int(std::max<qint64>(1, (deadline - now + 999999) / 1000000)));
}
void ScreenCaptureWin::cleanup() {
    if (m_sws) { sws_freeContext(m_sws); m_sws = nullptr; }
    if (m_cursorSws) { sws_freeContext(m_cursorSws); m_cursorSws = nullptr; }
    if (m_cursorDC && m_cursorOriginalBitmap) SelectObject(m_cursorDC, m_cursorOriginalBitmap);
    if (m_cursorBitmap) { DeleteObject(m_cursorBitmap); m_cursorBitmap = nullptr; }
    if (m_cursorDC) { DeleteDC(m_cursorDC); m_cursorDC = nullptr; }
    m_cursorOriginalBitmap = nullptr;
    m_cursorBits = nullptr; m_cursorSize = QSize(); m_previousNV12.clear();
    if (m_duplication) { m_duplication->Release(); m_duplication = nullptr; }
    if (m_stagingTex)  { m_stagingTex->Release();  m_stagingTex = nullptr; }
    if (m_context)     { m_context->Release();     m_context = nullptr; }
    if (m_device)      { m_device->Release();      m_device = nullptr; }
    if (m_memDC && m_originalBitmap) SelectObject(m_memDC, m_originalBitmap);
    if (m_bitmap)      { DeleteObject(m_bitmap);   m_bitmap = nullptr; }
    if (m_memDC)       { DeleteDC(m_memDC);        m_memDC = nullptr; }
    if (m_gdiDC)       { DeleteDC(m_gdiDC);        m_gdiDC = nullptr; }
    m_originalBitmap = nullptr;
    m_useGdiFallback = false; m_overlayPointer = false;
    m_resolution = QSize();
}
