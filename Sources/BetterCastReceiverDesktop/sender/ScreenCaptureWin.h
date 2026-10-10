#pragma once
// Note: this file is only compiled on Windows (gated in CMakeLists.txt).
// Do NOT wrap in #ifdef _WIN32 — AutoMoc cannot resolve preprocessor guards
// and will skip Q_OBJECT, causing linker errors.

#include "ScreenCapture.h"
#include <QTimer>
#include <QString>
#include <atomic>
#include <QElapsedTimer>
struct SwsContext;

// Forward declarations — avoid pulling Windows headers into every TU
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct IDXGIOutputDuplication;
struct HDC__;
typedef HDC__* HDC;
struct HBITMAP__;
typedef HBITMAP__* HBITMAP;

class ScreenCaptureWin : public ScreenCapture {
    Q_OBJECT
public:
    explicit ScreenCaptureWin(int targetFPS = 30, QObject* parent = nullptr);
    ~ScreenCaptureWin() override;

    // Set which monitor to capture (adapter + output index).
    // Must be called before start(). Default: adapter 0, output 0 (primary).
    void setMonitorIndex(int adapterIndex, int outputIndex);

    // Set the display device name for GDI fallback capture (e.g. "\\\\.\\DISPLAY17")
    void setDisplayName(const QString& name) { m_displayName = name; }

    bool start() override;
    void stop() override;
    bool isRunning() const override { return m_running; }
    QSize resolution() const override { return m_resolution; }

private:
    bool initD3D();
    bool initDuplication();
    bool initGdiFallback();
    void captureFrame();
    void captureFrameGdi();
    void cleanup();
    void overlayCursor(QByteArray& nv12, const uint8_t* bgra, int pitch);
    QByteArray convertBGRA(const uint8_t* data, int pitch);
    void scheduleNext();

    // D3D11 objects
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    IDXGIOutputDuplication* m_duplication = nullptr;
    ID3D11Texture2D* m_stagingTex = nullptr;

    // GDI fallback objects
    HDC m_gdiDC = nullptr;
    HDC m_memDC = nullptr;
    HBITMAP m_bitmap = nullptr;
    HBITMAP m_originalBitmap = nullptr;
    bool m_useGdiFallback = false;
    bool m_overlayPointer = false;

    SwsContext* m_sws = nullptr;
    SwsContext* m_cursorSws = nullptr;
    HDC m_cursorDC = nullptr;
    HBITMAP m_cursorBitmap = nullptr;
    HBITMAP m_cursorOriginalBitmap = nullptr;
    void* m_cursorBits = nullptr;
    QSize m_cursorSize;
    int m_originX = 0, m_originY = 0;
    QByteArray m_previousNV12;
    QElapsedTimer m_clock;
    qint64 m_tick = 0;
    QTimer m_timer;
    int m_targetFPS;
    int m_adapterIndex = 0;
    int m_outputIndex = 0;
    QString m_displayName;
    QSize m_resolution;
    std::atomic<bool> m_running{false};
};
