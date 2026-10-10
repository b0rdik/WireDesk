#include "MainWindow.h"
#include "VideoDecoder.h"
#include "VideoRenderer.h"
#include "VideoWindow.h"
#include "NetworkListener.h"
#include "InputHandler.h"
#include "InputEvent.h"
#include "ServiceDiscovery.h"
#include "AudioDecoder.h"
#include "AudioPlayer.h"
#include "AdbHelper.h"
#include "ServiceDiscovery.h"
#ifdef ENABLE_SENDER
#include "sender/SenderController.h"
#include "sender/VirtualDisplayVDD.h"
#include "sender/InviteListener.h"
#endif

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QScreen>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QNetworkInterface>
#include <QUrl>
#include <QKeyEvent>
#include <QMouseEvent>
#include <thread>
#include <QSettings>
#include <QFormLayout>
#include <QFileDialog>
#include <QToolButton>
#include <QResizeEvent>
#include <QStyle>
#include <QPainter>
#include <QUrlQuery>
#include <QSignalBlocker>
#include "PairingProfile.h"
#include "WireDeskUpdater.h"

// ─── Dark theme stylesheet ─────────────────────────────────────────────────────

static const char* kDarkStylesheet = R"(
    QMainWindow { background-color: #09111b; }
    QFrame#wireHero { background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #103333,stop:1 #142139); border:1px solid #244449; border-radius:22px; }
    QPushButton#primaryAction { background-color:#6de5c6; color:#0b2020; font-weight:700; border:none; padding:12px 20px; border-radius:10px; }
    QPushButton#primaryAction:hover { background-color:#95f0d9; }
    QPushButton#primaryAction:disabled { background-color:#20354a; color:#71899e; }
    QToolButton { color:#a3bacd; border:none; padding:10px 0; }
    QLabel#connectionStatus { background-color:#112234; border:1px solid #20394f; border-radius:12px; padding:16px; }
    QScrollBar:vertical { background:#09111b; width:8px; }
    QScrollBar::handle:vertical { background:#294257; border-radius:4px; min-height:24px; }
    QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical { height:0; }
    QSplitter { background-color: #09111b; }
    QSplitter::handle { background-color: #20354a; width: 1px; }

    QListWidget {
        background-color: #0d1825;
        border: none;
        outline: none;
        font-size: 13px;
        padding-top: 8px;
    }
    QListWidget::item {
        color: #ccc;
        padding: 7px 14px;
        border-radius: 6px;
        margin: 1px 8px;
    }
    QListWidget::item:selected {
        background-color: rgba(0, 120, 212, 0.18);
        color: #75e5ce;
    }
    QListWidget::item:hover:!selected {
        background-color: rgba(255, 255, 255, 0.05);
    }

    QStackedWidget { background-color: #09111b; }
    QScrollArea { background-color: #09111b; border: none; }
    QScrollArea > QWidget > QWidget { background-color: #09111b; }

    QLabel { color: #e4edf7; }

    QLineEdit {
        background-color: #132337;
        color: white;
        border: 1px solid #2b465f;
        border-radius: 6px;
        padding: 7px 10px;
        font-size: 13px;
        selection-background-color: #53c9ae;
    }
    QLineEdit:focus { border-color: #53c9ae; }

    QPushButton {
        background-color: #20354a;
        color: white;
        border: 1px solid #555;
        border-radius: 6px;
        padding: 8px 16px;
        font-size: 13px;
    }
    QPushButton:hover { background-color: #2b465f; border-color: #666; }
    QPushButton:pressed { background-color: #555; }
    QPushButton:disabled { background-color: #132337; color: #666; border-color: #20354a; }

    QGroupBox {
        color: #888;
        border: 1px solid #20354a;
        background-color: #0e1c2b;
        border-radius: 18px;
        margin-top: 16px;
        padding: 20px 16px 12px 16px;
        font-size: 12px;
        font-weight: bold;
    }
    QGroupBox::title {
        subcontrol-origin: margin;
        left: 16px;
        padding: 0 6px;
        color: #888;
    }

    QTextEdit {
        background-color: #111;
        color: #888;
        border: none;
        font-family: "Cascadia Code", "Consolas", "SF Mono", monospace;
        font-size: 11px;
    }

    QSpinBox {
        background-color: #132337;
        color: white;
        border: 1px solid #2b465f;
        border-radius: 6px;
        padding: 5px 8px;
        font-size: 13px;
    }
    QSpinBox:focus { border-color: #53c9ae; }
    QSpinBox::up-button, QSpinBox::down-button {
        background-color: #20354a;
        border: none;
        width: 20px;
    }

    QComboBox {
        background-color: #132337;
        color: white;
        border: 1px solid #2b465f;
        border-radius: 6px;
        padding: 5px 8px;
        font-size: 13px;
    }
    QComboBox:focus { border-color: #53c9ae; }
    QComboBox::drop-down { border: none; }
    QComboBox QAbstractItemView {
        background-color: #132337;
        color: white;
        selection-background-color: #53c9ae;
    }

    QCheckBox { color: #e4edf7; font-size: 13px; spacing: 8px; }
    QCheckBox::indicator {
        width: 16px; height: 16px;
        border: 1px solid #555; border-radius: 4px;
        background-color: #132337;
    }
    QCheckBox::indicator:checked { background-color: #53c9ae; border-color: #53c9ae; }
)";

// ─── Sidebar section header helper ──────────────────────────────────────────────

static QListWidgetItem* addSidebarSection(QListWidget* list, const QString& title) {
    auto* item = new QListWidgetItem(title);
    item->setFlags(Qt::ItemIsEnabled); // not selectable
    item->setData(Qt::UserRole, -1);
    QFont f = item->font();
    f.setPointSize(9);
    f.setBold(true);
    item->setFont(f);
    item->setForeground(QColor("#777"));
    // Add extra spacing above sections (except the first)
    if (list->count() > 0) {
        item->setSizeHint(QSize(0, 32));
    }
    list->addItem(item);
    return item;
}

static QListWidgetItem* addSidebarItem(QListWidget* list, const QString& icon,
                                        const QString& title, int pageIndex) {
    Q_UNUSED(icon);
    QPixmap pixels(88,88); pixels.fill(Qt::transparent); pixels.setDevicePixelRatio(4);
    QPainter painter(&pixels); painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor("#78dfc6"),1.7); pen.setCapStyle(Qt::RoundCap); pen.setJoinStyle(Qt::RoundJoin); painter.setPen(pen);
    if(title=="Второй экран" || title=="Приём экрана") {
        const bool sending=title=="Второй экран"; const int tip=sending?18:4, tail=sending?4:18, elbow=sending?12:10;
        painter.drawLine(tail,11,tip,11); painter.drawLine(elbow,5,tip,11); painter.drawLine(tip,11,elbow,17);
    } else if(title=="Настройки") {
        painter.setBrush(QColor("#0d1825"));
        for(int i=0;i<3;++i) { const int y=5+6*i, x=i==0?7:i==1?15:10; painter.drawLine(3,y,19,y); painter.drawEllipse(QPointF(x,y),2,2); }
    } else if(title=="Диагностика") {
        for(int y: {6,11,16}) { painter.drawPoint(3,y); painter.drawLine(8,y,19,y); }
    } else { painter.drawRoundedRect(QRectF(3,4,16,11),2,2); painter.drawLine(11,15,11,19); painter.drawLine(7,19,15,19); }
    painter.end();
    auto* item = new QListWidgetItem(QIcon(pixels),title);
    item->setData(Qt::UserRole+1,title);
    item->setData(Qt::UserRole, pageIndex);
    item->setSizeHint(QSize(0, 34));
    list->addItem(item);
    return item;
}

// ─── Card widget helper ─────────────────────────────────────────────────────────

static QGroupBox* makeCard(const QString& title) {
    auto* card = new QGroupBox(title);
    return card;
}

// ─── Constructor ────────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("WireDesk");
    setMinimumSize(620, 500);
    m_updater = new WireDeskUpdater(this);

    // Crash detection: check if previous session exited cleanly
    QString crashMarker = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/running.lock";
    if (QFile::exists(crashMarker)) {
        // Previous session crashed — offer to report
        QTimer::singleShot(500, this, [this]() {
            auto* dialog = new QMessageBox(this);
            dialog->setIcon(QMessageBox::Warning);
            dialog->setWindowTitle("WireDesk crashed last time");
            dialog->setText("WireDesk didn't exit cleanly last time. Would you like to report this issue on GitHub?");
            dialog->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            dialog->setDefaultButton(QMessageBox::Yes);
            if (dialog->exec() == QMessageBox::Yes) {
                onReportIssue();
            }
        });
    }
    // Write crash marker (removed on clean exit)
    QDir().mkpath(QFileInfo(crashMarker).absolutePath());
    QFile marker(crashMarker);
    marker.open(QIODevice::WriteOnly);
    marker.close();

    // Create core components
    m_decoder = new VideoDecoder(this);
    m_renderer = new VideoRenderer();
    m_network = new NetworkListener(this);
    m_inputHandler = new InputHandler(this);
    m_discovery = new ServiceDiscovery(this);
    m_audioDecoder = new AudioDecoder(this);
    m_audioPlayer = new AudioPlayer(this);
    m_adbHelper = new AdbHelper(this);
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setInterval(3000);
    connect(m_reconnectTimer, &QTimer::timeout, this, &MainWindow::attemptAdbReconnect);

#ifdef ENABLE_SENDER
    m_sender = new SenderController(this);
    connect(m_sender, &SenderController::statusChanged, this, [this](const QString& status) {
        if (m_senderStatusLabel) m_senderStatusLabel->setText(status);
        LogManager::instance().log("Sender: " + status);
    });
    connect(m_sender, &SenderController::error, this, [this](const QString& msg) {
        QTimer::singleShot(0, this, [this, msg] { if (m_senderStatusLabel) m_senderStatusLabel->setText("Error: " + msg); });
        LogManager::instance().log("Sender error: " + msg);
    });
    connect(m_sender, &SenderController::connected, this, [this]() {
        m_streamConnected = true;
        m_updater->setSessionActive(true);
        if (m_senderStatusLabel) m_senderStatusLabel->setText("Второй экран подключён");
        LogManager::instance().log("Sender: Connected and streaming");
    });
    connect(m_sender, &SenderController::disconnected, this, [this]() { m_streamConnected=false; m_updater->setSessionActive(m_sender->isSending() || !m_network->clients().isEmpty()); });
    connect(m_sender, &SenderController::stopped, this, [this]() {
        m_streamConnected=false; m_updater->setSessionActive(m_sender->isSending() || !m_network->clients().isEmpty());
        if (m_sendBtn) m_sendBtn->setEnabled(true);
        if (m_stopSendBtn) m_stopSendBtn->setEnabled(false);
        if (m_sendHostEdit) m_sendHostEdit->setEnabled(true);
        if (m_dedicatedEthernet) m_dedicatedEthernet->setEnabled(true);
        if (m_ethernetCombo) m_ethernetCombo->setEnabled(true);
        if (m_certificatePinEdit) m_certificatePinEdit->setEnabled(true);
        if (m_fpsCombo) m_fpsCombo->setEnabled(true);
        if (m_bitrateSpinBox) m_bitrateSpinBox->setEnabled(true);
        if (m_monitorCombo) m_monitorCombo->setEnabled(true);
    });

    // mDNS browsing for receiver discovery
    connect(m_discovery, &ServiceDiscovery::serviceFound,
            this, &MainWindow::onReceiverDiscovered);

    // Let receivers invite us, the same way they invite a Mac.
    m_inviteListener = new InviteListener(this);
    connect(m_inviteListener, &InviteListener::inviteReceived,
            this, &MainWindow::onInviteReceived);
#endif

    // Wire up core components
    m_network->setup(m_decoder, m_renderer, m_audioDecoder);

    connect(m_audioDecoder, &AudioDecoder::pcmDecoded,
            m_audioPlayer, &AudioPlayer::onPcmDecoded);

    connect(m_decoder, &VideoDecoder::frameDecoded,
            m_renderer, &VideoRenderer::onFrameDecoded);
    connect(m_decoder, &VideoDecoder::dimensionsChanged,
            m_renderer, [this](int w, int h) {
                m_inputHandler->setContentSize(QSize(w, h));
            });
    connect(m_decoder, &VideoDecoder::keyframeNeeded,
            this, [this]() {
                // Request IDR from sender on decode errors (throttled to every 2s)
                static QDateTime lastRequest;
                if (lastRequest.msecsTo(QDateTime::currentDateTime()) > 2000) {
                    LogManager::instance().log("Decoder: Requesting keyframe from sender (decode error recovery)");
                    m_network->sendInputEvent(InputEvent(InputEventType::Command, 0, 0, kIDRRequestKeyCode));
                    lastRequest = QDateTime::currentDateTime();
                }
            });

    m_inputHandler->attach(m_renderer);
    connect(m_inputHandler, &InputHandler::inputEvent,
            m_network, &NetworkListener::sendInputEvent);

    connect(m_adbHelper, &AdbHelper::statusChanged,
            this, &MainWindow::onStatusChanged);

    connect(m_network, &NetworkListener::connectionEstablished,
            this, &MainWindow::onConnectionEstablished);
    connect(m_network, &NetworkListener::connectionLost,
            this, &MainWindow::onConnectionLost);
    connect(m_network, &NetworkListener::statusChanged,
            this, &MainWindow::onStatusChanged);

    // Create video window (separate window, like Mac app)
    m_videoWindow = new VideoWindow(m_renderer, m_inputHandler, this);
    connect(m_videoWindow, &VideoWindow::windowClosed, this, [this]() {
        LogManager::instance().log("Video window closed by user");
    });

    connect(m_renderer, &VideoRenderer::videoSizeChanged,
            this, &MainWindow::onVideoSizeChanged);

    // LogManager
    connect(&LogManager::instance(), &LogManager::logAdded,
            this, &MainWindow::onLogAdded);

    setupUi();
    QTimer::singleShot(1000,m_updater,&WireDeskUpdater::start);

    // Start services
#ifdef ENABLE_SENDER
    if (!m_dedicatedEthernet->isChecked()) m_network->start();
#else
    m_network->start();
#endif
    uint16_t actualPort = m_network->actualTcpPort();
#ifdef ENABLE_SENDER
    if (m_dedicatedEthernet->isChecked()) { applyEthernetMode(true); }
    else {
#endif
    m_discovery->startAdvertising(actualPort);
#ifdef ENABLE_SENDER
    m_discovery->startBrowsing();

    // Announce that this machine can send, so phones and tablets can invite it.
    // Without this the Windows box was only ever visible as a receiver, and never
    // appeared in the Android or iOS "choose a sender" list.
    if (uint16_t invitePort = m_inviteListener->start()) {
        m_discovery->advertiseSenderService(invitePort);
    }
    }
#endif
    LogManager::instance().log(QString("WireDesk started — listening on port %1").arg(actualPort));
#ifdef _WIN32
    LogManager::instance().log("Firewall: not modified by application; configure explicit scoped rules if needed (docs/ethernet-second-display.md)");
#endif

    // Default window size (landscape 16:9, 70% screen)
    QScreen* screen = QApplication::primaryScreen();
    if (screen) {
        QRect available = screen->availableGeometry();
        int w = static_cast<int>(available.width() * 0.7);
        int h = w * 9 / 16;
        int x = available.x() + (available.width() - w) / 2;
        int y = available.y() + (available.height() - h) / 2;
        setGeometry(x, y, w, h);
    }
}

MainWindow::~MainWindow() {
    m_discovery->stopAdvertising();
    // Clean exit — remove crash marker
    QString crashMarker = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/running.lock";
    QFile::remove(crashMarker);
}

// ─── UI Setup ───────────────────────────────────────────────────────────────────

void MainWindow::setupUi() {
    setStyleSheet(kDarkStylesheet);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    setCentralWidget(m_splitter);

    // Sidebar
    m_sidebarList = new QListWidget();
    m_sidebarList->setFixedWidth(200);
    m_sidebarList->setIconSize(QSize(22,22));
    m_sidebarList->setFocusPolicy(Qt::NoFocus);
    m_sidebarList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // Detail stack
    m_stack = new QStackedWidget();

    // Build pages — order matters for page indices
    setupOverviewPage();
#ifdef ENABLE_SENDER
    setupSendPage();
#endif
    setupReceivePage();
    setupSettingsPage();
    setupLogsPage();

    // Build sidebar
    setupSidebar();

    // Assemble splitter
    m_splitter->addWidget(m_sidebarList);
    m_splitter->addWidget(m_stack);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setCollapsible(0, false);
    m_splitter->setCollapsible(1, false);

    // Connect sidebar selection
    connect(m_sidebarList, &QListWidget::currentRowChanged,
            this, &MainWindow::onSidebarSelectionChanged);

#ifdef ENABLE_SENDER
    selectSidebarItem(m_pageSend);
#else
    selectSidebarItem(m_pageOverview);
#endif
}

void MainWindow::setupSidebar() {
    addSidebarSection(m_sidebarList, "DEVICES");
    addSidebarItem(m_sidebarList, QString::fromUtf8("\xF0\x9F\x96\xA5"), "Все устройства", m_pageOverview);

#ifdef ENABLE_SENDER
    addSidebarSection(m_sidebarList, "SEND");
    addSidebarItem(m_sidebarList, QString::fromUtf8("\xF0\x9F\x93\xA4"), "Второй экран", m_pageSend);
#endif

    addSidebarSection(m_sidebarList, "RECEIVE");
    addSidebarItem(m_sidebarList, QString::fromUtf8("\xF0\x9F\x93\xA5"), "Приём экрана", m_pageReceive);

    addSidebarSection(m_sidebarList, "");
    addSidebarItem(m_sidebarList, QString::fromUtf8("\xE2\x9A\x99"), "Настройки", m_pageSettings);
    addSidebarItem(m_sidebarList, QString::fromUtf8("\xF0\x9F\x93\x9C"), "Диагностика", m_pageLogs);
}

// ─── Overview Page ──────────────────────────────────────────────────────────────

void MainWindow::setupOverviewPage() {
    auto* page = new QWidget();
    auto* scroll = new QScrollArea();
    scroll->setWidget(page);
    scroll->setWidgetResizable(true);

    auto* layout = new QVBoxLayout(page);
    layout->setAlignment(Qt::AlignCenter);
    layout->setContentsMargins(40, 40, 40, 40);
    layout->setSpacing(12);

    // App icon
    auto* iconLabel = new QLabel();
    QPixmap appIcon(":/appicon.png");
    if (!appIcon.isNull()) {
        iconLabel->setPixmap(appIcon.scaled(80, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    iconLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(iconLabel);

    // Title
    auto* title = new QLabel("WireDesk");
    title->setStyleSheet("font-size: 28px; font-weight: bold; color: white;");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto* subtitle = new QLabel("Turn any device into a wireless extended display");
    subtitle->setStyleSheet("font-size: 14px; color: #888;");
    subtitle->setAlignment(Qt::AlignCenter);
    layout->addWidget(subtitle);

    layout->addSpacing(24);

    // Getting started steps
    auto* stepsCard = makeCard("Getting Started");
    auto* stepsLayout = new QVBoxLayout(stepsCard);
    stepsLayout->setSpacing(16);

    auto addStep = [&](int num, const QString& title, const QString& desc) {
        auto* row = new QHBoxLayout();
        row->setSpacing(12);

        auto* numLabel = new QLabel(QString::number(num));
        numLabel->setFixedSize(28, 28);
        numLabel->setAlignment(Qt::AlignCenter);
        numLabel->setStyleSheet(
            "background-color: #53c9ae; color: white; font-weight: bold; "
            "font-size: 13px; border-radius: 14px;");
        row->addWidget(numLabel);

        auto* textLayout = new QVBoxLayout();
        textLayout->setSpacing(2);
        auto* titleLabel = new QLabel(title);
        titleLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #e4edf7;");
        textLayout->addWidget(titleLabel);
        auto* descLabel = new QLabel(desc);
        descLabel->setStyleSheet("font-size: 12px; color: #888;");
        descLabel->setWordWrap(true);
        textLayout->addWidget(descLabel);
        row->addLayout(textLayout, 1);

        stepsLayout->addLayout(row);
    };

    addStep(1, "Download the Receiver",
            "Install WireDesk Receiver on your iPad, Android, Windows, Linux, or Mac.");
    addStep(2, "Connect to the Same Network",
            "Make sure both devices are on the same Wi-Fi network.");
    addStep(3, "Open the Receiver App",
            "Your device will appear automatically, or use Manual IP to connect.");

    layout->addWidget(stepsCard);

    layout->addSpacing(12);

    // Status
    m_overviewStatusLabel = new QLabel("Searching for devices on your network...");
    m_overviewStatusLabel->setStyleSheet("font-size: 12px; color: #888;");
    m_overviewStatusLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_overviewStatusLabel);

    // Local IP
    m_overviewIpLabel = new QLabel();
    m_overviewIpLabel->setStyleSheet("font-size: 12px; color: #666;");
    m_overviewIpLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_overviewIpLabel);

    layout->addStretch();

    m_pageOverview = m_stack->addWidget(scroll);
    updateLocalIpDisplay();
}

// ─── Send Screen Page (ENABLE_SENDER) ───────────────────────────────────────────

#ifdef ENABLE_SENDER
void MainWindow::setupSendPage() {
    auto* page = new QWidget();
    auto* scroll = new QScrollArea(); scroll->setWidget(page); scroll->setWidgetResizable(true);
    auto* layout = new QVBoxLayout(page); m_sendLayout = layout;
    layout->setContentsMargins(28, 28, 28, 28); layout->setSpacing(20);
    auto* header = new QHBoxLayout();
    auto* icon = new QLabel(); const auto scale=page->devicePixelRatioF(); auto brandPixels=QPixmap(":/appicon.png").scaled(qRound(48*scale),qRound(48*scale),Qt::KeepAspectRatio,Qt::SmoothTransformation); brandPixels.setDevicePixelRatio(scale); icon->setPixmap(brandPixels); header->addWidget(icon);
    auto* brand = new QLabel("WireDesk"); brand->setStyleSheet("font-size: 24px; font-weight: 700; color: #eaf4ff;"); header->addWidget(brand); header->addStretch();
    auto* tag = new QLabel("WINDOWS → MAC"); tag->setStyleSheet("color: #73eaca; font-size: 11px; letter-spacing: 1px;"); header->addWidget(tag); layout->addLayout(header);
    auto* hero = new QFrame(); hero->setObjectName("wireHero");
    auto* heroLayout = new QVBoxLayout(hero); heroLayout->setContentsMargins(24,24,24,24); heroLayout->setSpacing(10);
    auto* title = new QLabel("Ваш Mac. Ещё один экран."); title->setWordWrap(true); title->setStyleSheet("font-size: 28px; font-weight: 700; color: white;"); heroLayout->addWidget(title);
    auto* subtitle = new QLabel("Соедините устройства кабелем и переносите окна на Mac. Интернет может оставаться на другой сети."); subtitle->setWordWrap(true); subtitle->setStyleSheet("color: #a7bfd1; font-size: 14px;"); heroLayout->addWidget(subtitle);
    auto* badge = new QLabel("До 120 FPS   ·   Защищённое соединение   ·   Без облака"); badge->setWordWrap(true); badge->setStyleSheet("color: #73eaca; font-size: 12px;"); heroLayout->addWidget(badge); layout->addWidget(hero);
    m_setupCards = new QBoxLayout(QBoxLayout::LeftToRight); m_setupCards->setSpacing(16);
    auto* ethernetCard = makeCard("1  Подключите кабель"); auto* eth = new QVBoxLayout(ethernetCard);
    auto* cableText = new QLabel("Свободный Ethernet-порт Windows → адаптер Mac. Выберите выделенное подключение."); cableText->setWordWrap(true); eth->addWidget(cableText);
    m_ethernetCombo = new QComboBox(); m_ethernetCombo->setObjectName("ethernetAdapter"); m_ethernetCombo->setMinimumContentsLength(12); m_ethernetCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); eth->addWidget(m_ethernetCombo);
    auto* networkButtons = new QHBoxLayout();
    auto* refresh = new QPushButton("Обновить"); connect(refresh,&QPushButton::clicked,this,&MainWindow::refreshEthernetInterfaces); networkButtons->addWidget(refresh);
    auto* networkSettings = new QPushButton("Настройки сети"); connect(networkSettings,&QPushButton::clicked,this,[]{ QDesktopServices::openUrl(QUrl("ms-settings:network-ethernet")); }); networkButtons->addWidget(networkSettings); eth->addLayout(networkButtons);
    auto* networkHelp = new QLabel("Автоматические 169.254.x.x работают без роутера. При ручной настройке используйте отдельную подсеть без шлюза и DNS."); networkHelp->setWordWrap(true); networkHelp->setStyleSheet("font-size: 11px; color: #8cabc2;"); eth->addWidget(networkHelp); eth->addStretch();
    m_setupCards->addWidget(ethernetCard,1);
    auto* pairingCard = makeCard("2  Свяжите устройства"); auto* pair = new QVBoxLayout(pairingCard);
    auto* pairingHelp = new QLabel("На Mac нажмите «Скопировать код». Вставьте его здесь или откройте файл сопряжения."); pairingHelp->setWordWrap(true); pair->addWidget(pairingHelp);
    auto* codeEdit = new QLineEdit(); codeEdit->setObjectName("pairingCode"); codeEdit->setPlaceholderText("wiredesk://pair?…"); pair->addWidget(codeEdit);
    auto* pairActions = new QHBoxLayout();
    auto* paste = new QPushButton("Вставить код"); connect(paste,&QPushButton::clicked,this,[this,codeEdit]{ const auto code=QApplication::clipboard()->text(); codeEdit->setText(code); importPairingCode(code); }); pairActions->addWidget(paste);
    auto* import = new QPushButton("Открыть файл"); connect(import,&QPushButton::clicked,this,[this]{ const auto path=QFileDialog::getOpenFileName(this,"Файл сопряжения Mac",{},"WireDesk (*.wiredesk-pair)"); if(path.isEmpty())return; QFile file(path); if(file.open(QIODevice::ReadOnly)&&file.size()<=4096)importPairingCode(QString::fromUtf8(file.readAll())); }); pairActions->addWidget(import); pair->addLayout(pairActions);
    connect(codeEdit,&QLineEdit::returnPressed,this,[this,codeEdit]{ importPairingCode(codeEdit->text()); });
    auto* trust = new QLabel("Первое подключение нужно подтвердить на Mac. Приватный ключ по сети не передаётся."); trust->setWordWrap(true); trust->setStyleSheet("font-size: 11px; color: #8cabc2;"); pair->addWidget(trust); pair->addStretch(); m_setupCards->addWidget(pairingCard,1); layout->addLayout(m_setupCards);
    QSettings saved;
    auto* displayCard=makeCard("3  Выберите второй экран"); auto* display=new QVBoxLayout(displayCard);
    m_vddStatusLabel=new QLabel(); m_vddStatusLabel->setWordWrap(true); display->addWidget(m_vddStatusLabel);
    m_monitorCombo=new QComboBox(); m_monitorCombo->setObjectName("displayPicker"); m_monitorCombo->setMinimumContentsLength(15); m_monitorCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); display->addWidget(m_monitorCombo);
    connect(m_monitorCombo,QOverload<int>::of(&QComboBox::currentIndexChanged),this,&MainWindow::onMonitorSelected);
    auto* displayActions=new QHBoxLayout();
    m_recheckVddBtn=new QPushButton("Обновить экраны"); connect(m_recheckVddBtn,&QPushButton::clicked,this,&MainWindow::onRefreshMonitors); displayActions->addWidget(m_recheckVddBtn);
    auto* windowsDisplay=new QPushButton("Расширить рабочий стол"); connect(windowsDisplay,&QPushButton::clicked,this,[]{ QDesktopServices::openUrl(QUrl("ms-settings:display")); }); displayActions->addWidget(windowsDisplay); display->addLayout(displayActions);
    auto* quality=new QFormLayout(); quality->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_vddResolutionCombo=new QComboBox();
    for(const auto& size: {QSize(1920,1080),QSize(2560,1440),QSize(3840,2160),QSize(1920,1200),QSize(2560,1600),QSize(1280,720),QSize(1024,768)}) m_vddResolutionCombo->addItem(QString("%1 × %2").arg(size.width()).arg(size.height()),size);
    quality->addRow("Разрешение",m_vddResolutionCombo);
    m_fpsCombo=new QComboBox(); m_fpsCombo->setObjectName("streamFPS"); m_fpsCombo->addItem("60 FPS · совместимость",60); m_fpsCombo->addItem("120 FPS · аппаратный кодер",120); m_fpsCombo->setCurrentIndex(saved.value("sender/fps",60).toInt()==120?1:0); quality->addRow("Видеопоток",m_fpsCombo);
    m_refreshRateCombo=new QComboBox(); m_refreshRateCombo->addItem("60 Гц",60); m_refreshRateCombo->addItem("120 Гц",120); m_refreshRateCombo->setCurrentIndex(saved.value("sender/virtualHz",60).toInt()==120?1:0); quality->addRow("Виртуальный монитор",m_refreshRateCombo);
    connect(m_fpsCombo,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int index){ m_refreshRateCombo->setCurrentIndex(index); });
    m_bitrateSpinBox=new QSpinBox(); m_bitrateSpinBox->setRange(2,100); m_bitrateSpinBox->setSuffix(" Mbps"); m_bitrateSpinBox->setValue(saved.value("sender/bitrate",20).toInt()); quality->addRow("Качество / битрейт",m_bitrateSpinBox); display->addLayout(quality);
    m_autoDisplay=new QCheckBox("Применять режим VDD автоматически"); m_autoDisplay->setChecked(saved.value("sender/autoDisplay",true).toBool()); display->addWidget(m_autoDisplay);
    m_createVddBtn=new QPushButton("Применить режим VDD"); connect(m_createVddBtn,&QPushButton::clicked,this,&MainWindow::onCreateVirtualDisplay); display->addWidget(m_createVddBtn);
    m_removeVddBtn=new QPushButton("Установка и отключение монитора"); connect(m_removeVddBtn,&QPushButton::clicked,this,[]{ QDesktopServices::openUrl(QUrl("https://github.com/b0rdik/WireDesk/blob/main/docs/ethernet-second-display.md#виртуальный-второй-монитор")); }); display->addWidget(m_removeVddBtn);
    layout->addWidget(displayCard);
    m_autoConnect=new QCheckBox("Подключаться к сохранённому Mac при запуске"); m_autoConnect->setChecked(saved.value("sender/autoConnect",true).toBool()); layout->addWidget(m_autoConnect);
    connect(m_autoConnect,&QCheckBox::toggled,this,[](bool value){ QSettings().setValue("sender/autoConnect",value); });
    connect(m_autoDisplay,&QCheckBox::toggled,this,[](bool value){ QSettings().setValue("sender/autoDisplay",value); });
    auto* actions=new QHBoxLayout(); m_sendBtn=new QPushButton("Подключить второй экран"); m_sendBtn->setObjectName("primaryAction"); connect(m_sendBtn,&QPushButton::clicked,this,&MainWindow::onSendScreenClicked); actions->addWidget(m_sendBtn);
    m_stopSendBtn=new QPushButton("Отключить"); m_stopSendBtn->setEnabled(false); connect(m_stopSendBtn,&QPushButton::clicked,this,&MainWindow::onStopSendingClicked); actions->addWidget(m_stopSendBtn); actions->addStretch(); layout->addLayout(actions);
    m_senderStatusLabel=new QLabel("Начните с кабеля и кода сопряжения Mac."); m_senderStatusLabel->setObjectName("connectionStatus"); m_senderStatusLabel->setWordWrap(true); m_senderStatusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(m_senderStatusLabel);
    auto* detailsButton=new QToolButton(); detailsButton->setText("Дополнительные настройки"); detailsButton->setCheckable(true); detailsButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); detailsButton->setArrowType(Qt::RightArrow); layout->addWidget(detailsButton);
    auto* details=new QGroupBox(); auto* detail=new QFormLayout(details); detail->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_sendHostEdit=new QLineEdit(saved.value("sender/host").toString()); m_sendHostEdit->setPlaceholderText("IPv4 Mac"); detail->addRow("Адрес Mac",m_sendHostEdit);
    m_certificatePinEdit=new QLineEdit(saved.value("ethernet/pin").toString()); m_certificatePinEdit->setPlaceholderText("SHA-256 сертификата Mac"); detail->addRow("Сертификат",m_certificatePinEdit);
    m_dedicatedEthernet=new QCheckBox("Только выделенный Ethernet (TLS)"); m_dedicatedEthernet->setChecked(saved.value("ethernet/enabled",true).toBool()); detail->addRow(m_dedicatedEthernet);
    m_receiverCombo=new QComboBox(); m_receiverCombo->addItem("Обнаружение доступно в общем режиме"); detail->addRow("Другие приёмники",m_receiverCombo);
    connect(m_receiverCombo,QOverload<int>::of(&QComboBox::currentIndexChanged),this,&MainWindow::onReceiverSelected); connect(m_dedicatedEthernet,&QCheckBox::toggled,this,&MainWindow::applyEthernetMode);
    details->setVisible(false); connect(detailsButton,&QToolButton::toggled,this,[details,detailsButton](bool open){ details->setVisible(open); detailsButton->setArrowType(open?Qt::DownArrow:Qt::RightArrow); }); layout->addWidget(details);
    layout->addStretch(); m_pageSend=m_stack->addWidget(scroll);
    const QSize savedResolution(saved.value("sender/virtualWidth",1920).toInt(),saved.value("sender/virtualHeight",1080).toInt()); for(int i=0;i<m_vddResolutionCombo->count();++i)if(m_vddResolutionCombo->itemData(i).toSize()==savedResolution)m_vddResolutionCombo->setCurrentIndex(i);
    connect(qApp,&QGuiApplication::screenAdded,this,[this](QScreen*){ if(!m_sender->isSending())onRefreshMonitors(); });
    connect(qApp,&QGuiApplication::screenRemoved,this,[this](QScreen*){ if(!m_sender->isSending())onRefreshMonitors(); });
    onRefreshMonitors(); refreshEthernetInterfaces();
    auto* timer=new QTimer(this); timer->setInterval(2000); connect(timer,&QTimer::timeout,this,[this]{ refreshEthernetInterfaces(); attemptAutomaticConnection(); }); timer->start();
}

#endif

// ─── Receive Page ───────────────────────────────────────────────────────────────

void MainWindow::setupReceivePage() {
    auto* page = new QWidget();
    auto* scroll = new QScrollArea();
    scroll->setWidget(page);
    scroll->setWidgetResizable(true);

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(40, 30, 40, 30);
    layout->setSpacing(16);

    auto* pageTitle = new QLabel("Receive Screen");
    pageTitle->setStyleSheet("font-size: 22px; font-weight: bold; color: white;");
    layout->addWidget(pageTitle);

    // Listening status card (prominent, like Mac's Start Listening)
    auto* listenCard = makeCard("Listening for Senders");
    auto* listenLayout = new QVBoxLayout(listenCard);
    listenLayout->setSpacing(10);

    // Status indicator
    m_recvStatusLabel = new QLabel("Listening on port 51820");
    m_recvStatusLabel->setStyleSheet("font-size: 15px; font-weight: bold; color: #75e5ce;");
    listenLayout->addWidget(m_recvStatusLabel);

    m_recvIpLabel = new QLabel();
    m_recvIpLabel->setStyleSheet("font-size: 13px; color: #888;");
    m_recvIpLabel->setWordWrap(true);
    listenLayout->addWidget(m_recvIpLabel);

    auto* instrLabel = new QLabel(
        "This device is ready to receive. On the sender device:\n"
        "  1. Open WireDesk and go to Send Screen\n"
        "  2. This device should appear automatically\n"
        "  3. Or enter this device's IP address manually");
    instrLabel->setStyleSheet("color: #888; font-size: 12px;");
    instrLabel->setWordWrap(true);
    listenLayout->addWidget(instrLabel);

    layout->addWidget(listenCard);

    // Manual connect card (secondary)
    auto* manualCard = makeCard("Connect to a Sender (Manual)");
    auto* manualLayout = new QVBoxLayout(manualCard);
    manualLayout->setSpacing(10);

    auto* manualDesc = new QLabel("Connect to a sender that isn't auto-discovered:");
    manualDesc->setStyleSheet("font-size: 12px; color: #888;");
    manualLayout->addWidget(manualDesc);

    auto* connRow = new QHBoxLayout();
    connRow->setSpacing(8);

    m_hostEdit = new QLineEdit();
    m_hostEdit->setPlaceholderText("Sender IP address");
    m_hostEdit->setFixedWidth(180);
    connRow->addWidget(m_hostEdit);

    m_portEdit = new QLineEdit("51820");
    m_portEdit->setPlaceholderText("Port");
    m_portEdit->setFixedWidth(80);
    connRow->addWidget(m_portEdit);

    m_connectBtn = new QPushButton("Connect");
    m_connectBtn->setStyleSheet(
        "QPushButton { background-color: #53c9ae; color: white; font-weight: bold; "
        "padding: 8px 20px; border-radius: 6px; border: none; }"
        "QPushButton:hover { background-color: #1a8ae8; }"
        "QPushButton:disabled { background-color: #132337; color: #666; }");
    connect(m_connectBtn, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connRow->addWidget(m_connectBtn);

    connRow->addStretch();
    manualLayout->addLayout(connRow);

    layout->addWidget(manualCard);

    // ADB card
    auto* adbCard = makeCard("Android (ADB)");
    auto* adbLayout = new QVBoxLayout(adbCard);
    adbLayout->setSpacing(10);

    m_adbBtn = new QPushButton("Connect to Android (ADB)");
    m_adbBtn->setStyleSheet(
        "QPushButton { background-color: #3ddc84; color: black; font-weight: bold; "
        "padding: 10px 20px; border-radius: 8px; font-size: 14px; border: none; }"
        "QPushButton:hover { background-color: #50e898; }"
        "QPushButton:disabled { background-color: #132337; color: #666; }");
    connect(m_adbBtn, &QPushButton::clicked, this, &MainWindow::onAdbConnectClicked);
    adbLayout->addWidget(m_adbBtn);

    m_adbHelpLabel = new QLabel(
        "To mirror your Android screen:\n"
        "1. Enable Developer Options (tap Build Number 7x in Settings > About)\n"
        "2. Enable USB Debugging in Developer Options\n"
        "3. Connect Android to this computer via USB\n"
        "4. Open BetterCast Receiver on Android and tap \"Start Casting\"\n"
        "5. Click the button above to connect");
    m_adbHelpLabel->setStyleSheet("color: #666; font-size: 11px;");
    m_adbHelpLabel->setWordWrap(true);
    adbLayout->addWidget(m_adbHelpLabel);

    layout->addWidget(adbCard);

    layout->addStretch();

    m_pageReceive = m_stack->addWidget(scroll);
}

// ─── Settings Page ──────────────────────────────────────────────────────────────

void MainWindow::setupSettingsPage() {
    auto* page = new QWidget();
    auto* scroll = new QScrollArea();
    scroll->setWidget(page);
    scroll->setWidgetResizable(true);

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(40, 30, 40, 30);
    layout->setSpacing(16);

    auto* pageTitle = new QLabel("Settings");
    pageTitle->setStyleSheet("font-size: 22px; font-weight: bold; color: white;");
    layout->addWidget(pageTitle);

    // About card
    auto* aboutCard = makeCard("About");
    auto* aboutLayout = new QVBoxLayout(aboutCard);
    aboutLayout->setSpacing(8);

    m_versionLabel = new QLabel(QString("WireDesk v%1")
        .arg(QApplication::applicationVersion()));
    m_versionLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #e4edf7;");
    aboutLayout->addWidget(m_versionLabel);

    auto* descLabel = new QLabel(
        "Turn any device into a wireless extended display. "
        "Works with iPad, Android, Windows, Linux, and Mac receivers.");
    descLabel->setStyleSheet("font-size: 12px; color: #888;");
    descLabel->setWordWrap(true);
    aboutLayout->addWidget(descLabel);

    layout->addWidget(aboutCard);

    // Connection card
    auto* connCard = makeCard("Connection");
    auto* connLayout = new QVBoxLayout(connCard);
    connLayout->setSpacing(10);

    auto* portInfo = new QLabel("Listening on port 51820 (TCP)");
    portInfo->setStyleSheet("font-size: 13px; color: #ccc;");
    connLayout->addWidget(portInfo);

    auto* ipInfo = new QLabel();
    QStringList ips;
    for (const auto& iface : QNetworkInterface::allInterfaces()) {
        if (iface.flags().testFlag(QNetworkInterface::IsUp) &&
            iface.flags().testFlag(QNetworkInterface::IsRunning) &&
            !iface.flags().testFlag(QNetworkInterface::IsLoopBack)) {
            for (const auto& entry : iface.addressEntries()) {
                if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                    ips.append(entry.ip().toString());
                }
            }
        }
    }
    ipInfo->setText(ips.isEmpty() ? "No network detected"
                                  : "Local IPs: " + ips.join(", "));
    ipInfo->setStyleSheet("font-size: 12px; color: #888;");
    ipInfo->setWordWrap(true);
    connLayout->addWidget(ipInfo);

    layout->addWidget(connCard);

    auto* updatesCard=makeCard("Обновления WireDesk"); auto* updates=new QVBoxLayout(updatesCard);
    auto* autoUpdate=new QCheckBox("Проверять подписанные обновления автоматически"); autoUpdate->setChecked(QSettings().value("updates/automatic",true).toBool()); updates->addWidget(autoUpdate);
    connect(autoUpdate,&QCheckBox::toggled,this,[this](bool enabled){ m_updater->setAutomatic(enabled); });
    auto* updateButton=new QPushButton("Проверить обновления"); updates->addWidget(updateButton);
    auto* updateStatus=new QLabel("HTTPS + Ed25519. Установка после подтверждения, вне активного сеанса."); updateStatus->setWordWrap(true); updates->addWidget(updateStatus);
    connect(updateButton,&QPushButton::clicked,this,[this,updateStatus]{ m_updater->check(); updateStatus->setText(m_updater->status()); }); layout->addWidget(updatesCard);
    // Changelog card
    auto* changeCard = makeCard("What's New");
    auto* changeLayout = new QVBoxLayout(changeCard);
    changeLayout->setSpacing(10);

    struct ChangeEntry {
        QString version, date;
        QStringList items;
    };
    QVector<ChangeEntry> changelog = {
        {"WireDesk 1.0.0", "2026-10-10", {"Windows → Mac по выделенному Ethernet", "Сопряжение одним кодом, автоматический сертификат и восстановление", "60/120 FPS, ограниченные очереди и подписанные OTA-пакеты"}},
        {"BetterCast v8", "2026-03-30", {
            "Unified sender + receiver in a single app",
            "Apple Music-style sidebar with tinted selection",
            "Windows sender with sidebar UI",
            "In-app update checker via GitHub Releases",
        }},
        {"v7", "2026-03-23", {
            "Android ADB wireless auto-reconnect",
            "Orientation fix for rotated displays",
        }},
        {"v6", "2026-03-19", {
            "Windows sender Phase 1",
            "DMG signing improvements",
        }},
    };

    for (const auto& entry : changelog) {
        auto* verLabel = new QLabel(QString("%1  —  %2").arg(entry.version, entry.date));
        verLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #ccc;");
        changeLayout->addWidget(verLabel);

        for (const auto& item : entry.items) {
            auto* bulletLabel = new QLabel(QString("  \xE2\x80\xA2  %1").arg(item));
            bulletLabel->setStyleSheet("font-size: 11px; color: #888;");
            changeLayout->addWidget(bulletLabel);
        }

        changeLayout->addSpacing(4);
    }

    layout->addWidget(changeCard);

    layout->addStretch();

    m_pageSettings = m_stack->addWidget(scroll);
}

// ─── Logs Page ──────────────────────────────────────────────────────────────────

void MainWindow::setupLogsPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);

    // Title row with buttons
    auto* titleRow = new QHBoxLayout();

    auto* pageTitle = new QLabel("Logs");
    pageTitle->setStyleSheet("font-size: 22px; font-weight: bold; color: white;");
    titleRow->addWidget(pageTitle);

    titleRow->addStretch();

    auto* reportBtn = new QPushButton("Report Issue");
    reportBtn->setStyleSheet(
        "QPushButton { background-color: #20354a; color: #ccc; padding: 6px 14px; "
        "border-radius: 6px; font-size: 12px; border: 1px solid #555; }"
        "QPushButton:hover { background-color: #2b465f; }");
    connect(reportBtn, &QPushButton::clicked, this, &MainWindow::onReportIssue);
    titleRow->addWidget(reportBtn);

    auto* copyBtn = new QPushButton("Copy");
    copyBtn->setStyleSheet(
        "QPushButton { background-color: #20354a; color: #ccc; padding: 6px 14px; "
        "border-radius: 6px; font-size: 12px; border: 1px solid #555; }"
        "QPushButton:hover { background-color: #2b465f; }");
    connect(copyBtn, &QPushButton::clicked, this, &MainWindow::onCopyLogs);
    titleRow->addWidget(copyBtn);

    auto* clearBtn = new QPushButton("Clear");
    clearBtn->setStyleSheet(
        "QPushButton { background-color: #20354a; color: #ccc; padding: 6px 14px; "
        "border-radius: 6px; font-size: 12px; border: 1px solid #555; }"
        "QPushButton:hover { background-color: #2b465f; }");
    connect(clearBtn, &QPushButton::clicked, this, &MainWindow::onClearLogs);
    titleRow->addWidget(clearBtn);

    layout->addLayout(titleRow);

    // Log viewer
    m_logViewer = new QTextEdit();
    m_logViewer->setReadOnly(true);
    m_logViewer->setPlaceholderText("No log entries yet...");
    layout->addWidget(m_logViewer);

    m_pageLogs = m_stack->addWidget(page);
}

// ─── Sidebar Selection ──────────────────────────────────────────────────────────

void MainWindow::onSidebarSelectionChanged(int row) {
    auto* item = m_sidebarList->item(row);
    if (!item) return;

    int page = item->data(Qt::UserRole).toInt();
    if (page < 0) {
        // Section header — skip to next selectable item
        if (row + 1 < m_sidebarList->count()) {
            m_sidebarList->setCurrentRow(row + 1);
        }
        return;
    }

    m_stack->setCurrentIndex(page);
}

void MainWindow::selectSidebarItem(int pageIndex) {
    for (int i = 0; i < m_sidebarList->count(); i++) {
        auto* item = m_sidebarList->item(i);
        if (item && item->data(Qt::UserRole).toInt() == pageIndex) {
            m_sidebarList->setCurrentRow(i);
            return;
        }
    }
}

// ─── Connection Handlers ────────────────────────────────────────────────────────

void MainWindow::onConnectClicked() {
    bool ok = false;
    uint16_t port = m_portEdit->text().toUShort(&ok);
    if (!ok) port = 51820;

    m_network->connectTo(m_hostEdit->text(), port);
    m_connectBtn->setEnabled(false);
    m_recvStatusLabel->setText("Connecting...");
    m_recvStatusLabel->setStyleSheet("font-size: 15px; font-weight: bold; color: #75e5ce;");
    LogManager::instance().log("Connecting to " + m_hostEdit->text() + ":" + QString::number(port));
}

void MainWindow::onAdbConnectClicked() {
    m_adbBtn->setEnabled(false);
    m_adbBtn->setText("Setting up ADB...");
    m_recvStatusLabel->setText("Looking for Android device...");
    m_recvStatusLabel->setStyleSheet("font-size: 15px; font-weight: bold; color: #75e5ce;");
    LogManager::instance().log("Starting ADB setup...");

    std::thread([this]() {
        bool success = m_adbHelper->setupForward(51820);
        uint16_t localPort = m_adbHelper->lastLocalPort();
        QMetaObject::invokeMethod(this, [this, success, localPort]() {
            m_adbBtn->setEnabled(true);
            m_adbBtn->setText("Connect to Android (ADB)");

            if (success) {
                m_recvStatusLabel->setText("ADB tunnel ready — connecting...");
                LogManager::instance().log(QString("ADB tunnel established, connecting to localhost:%1...").arg(localPort));
                m_network->connectTo("localhost", localPort);
            }
        });
    }).detach();
}

void MainWindow::onConnectionEstablished() {
    m_updater->setSessionActive(true);
    m_connectBtn->setEnabled(true);
    m_reconnectTimer->stop();
    LogManager::instance().log("Connection established — streaming video");

    // Open the video in a separate window
    if (m_videoWindow) {
        m_videoWindow->showForVideo();
    }

    m_recvStatusLabel->setText("Connected — video window opened");
    m_recvStatusLabel->setStyleSheet("font-size: 15px; font-weight: bold; color: #4caf50;");

    // Reset reconnect counter only after video actually starts flowing
    // (delayed so brief connect-then-disconnect during reconnect doesn't reset it)
    QTimer::singleShot(3000, this, [this]() {
        // Only reset if we're still connected (not in a reconnect cycle)
        if (!m_network->clients().isEmpty()) {
            m_reconnectAttempts = 0;
        }
    });

    if (m_adbHelper->wasAdbConnection() && !m_wirelessAdbEnabled) {
        // Delay wireless ADB by 5 seconds — adb tcpip 5555 temporarily kills
        // the USB connection (and our forward tunnel). Give the stream time to
        // start before we switch to wireless mode.
        QTimer::singleShot(5000, this, [this]() {
            if (m_wirelessAdbEnabled) return; // already done
            m_wirelessAdbEnabled = true;
            LogManager::instance().log("Enabling wireless ADB (USB can be disconnected after)...");
            std::thread([this]() {
                m_adbHelper->enableWirelessAdb();
            }).detach();
        });
    }
}

void MainWindow::onConnectionLost() {
    m_updater->setSessionActive(m_streamConnected);
    m_connectBtn->setEnabled(true);

    if (m_adbHelper->wasAdbConnection()) {
        // Don't reset m_reconnectAttempts here — if the reconnect itself
        // succeeds briefly then disconnects, we'd loop forever.
        // The counter only resets after a sustained connection (3s in onConnectionEstablished).
        m_recvStatusLabel->setText("Connection lost — reconnecting in 2s...");
        m_recvStatusLabel->setStyleSheet("font-size: 15px; font-weight: bold; color: orange;");
        LogManager::instance().log("Connection lost — will reconnect via ADB in 2s...");
        // Delay before first reconnect to avoid rapid cycling
        QTimer::singleShot(2000, this, [this]() {
            attemptAdbReconnect();
            m_reconnectTimer->start();
        });
    } else {
        // Close video window when non-ADB connection is lost
        if (m_videoWindow && m_videoWindow->isVisible()) {
            m_videoWindow->close();
        }
        m_recvStatusLabel->setText("Connection lost — still listening on port 51820");
        m_recvStatusLabel->setStyleSheet("font-size: 15px; font-weight: bold; color: orange;");
        LogManager::instance().log("Connection lost");
    }
}

void MainWindow::onStatusChanged(const QString& status) {
    m_recvStatusLabel->setText(status);
    LogManager::instance().log(status);
}

void MainWindow::onVideoSizeChanged(QSize size) {
    if (size.width() > 0 && size.height() > 0 && m_videoWindow) {
        LogManager::instance().log(QString("Video size: %1x%2").arg(size.width()).arg(size.height()));
        m_videoWindow->resizeToFitVideo(size.width(), size.height());
    }
}

void MainWindow::attemptAdbReconnect() {
    m_reconnectAttempts++;

    if (m_reconnectAttempts > 15) {
        m_reconnectTimer->stop();
        m_recvStatusLabel->setText("Auto-reconnect failed. Click 'Connect to Android (ADB)' to retry.");
        m_recvStatusLabel->setStyleSheet("font-size: 15px; font-weight: bold; color: #d32f2f;");
        LogManager::instance().log("ADB auto-reconnect failed after 15 attempts");
        return;
    }

    m_recvStatusLabel->setText(QString("Reconnecting via ADB... (attempt %1)").arg(m_reconnectAttempts));
    LogManager::instance().log(QString("ADB reconnect attempt %1").arg(m_reconnectAttempts));

    std::thread([this]() {
        bool success = m_adbHelper->setupForward(51820);
        uint16_t localPort = m_adbHelper->lastLocalPort();
        QMetaObject::invokeMethod(this, [this, success, localPort]() {
            if (success) {
                m_reconnectTimer->stop();
                m_recvStatusLabel->setText("ADB tunnel restored — connecting...");
                LogManager::instance().log("ADB tunnel restored, reconnecting...");
                m_network->connectTo("localhost", localPort);
            }
        });
    }).detach();
}

// ─── Sender Slots ───────────────────────────────────────────────────────────────

#ifdef ENABLE_SENDER
void MainWindow::onSendScreenClicked() {
    QString host = m_sendHostEdit->text().trimmed();
    if (host.isEmpty()) {
        m_senderStatusLabel->setText("Enter a receiver IP address first");
        m_senderStatusLabel->setStyleSheet("font-size: 12px; color: #d32f2f;");
        return;
    }

    m_autoConnectAttempted = true;
    if (m_autoDisplay && m_autoDisplay->isChecked() && m_monitorCombo->currentData().toMap().value("virtual").toBool()) {
        const auto size = m_vddResolutionCombo->currentData().toSize();
        const int hz = m_refreshRateCombo->currentData().toInt();
        if (!m_sender->vdd() || !m_sender->vdd()->createVirtualDisplay(size.width(),size.height(),hz)) return;
        onRefreshMonitors();
        for (int i=0;i<m_monitorCombo->count();++i) if(m_monitorCombo->itemData(i).toMap().value("virtual").toBool()) { m_monitorCombo->setCurrentIndex(i); break; }
    }
    // Apply selected monitor to sender controller
    if (m_monitorCombo && m_monitorCombo->currentIndex() >= 0) {
        QVariantMap monData = m_monitorCombo->currentData().toMap();
        int adapterIdx = monData.value("adapter", 0).toInt();
        int outputIdx = monData.value("output", 0).toInt();
        QString displayName = monData.value("displayName").toString();
        m_sender->setMonitorIndex(adapterIdx, outputIdx);
        m_sender->setDisplayName(displayName);
        LogManager::instance().log(QString("Capturing monitor: %1 (adapter %2, output %3)")
                                       .arg(m_monitorCombo->currentText())
                                       .arg(adapterIdx).arg(outputIdx));
    }

    const int requestedFPS = m_fpsCombo->currentData().toInt();
    if (requestedFPS != 60 && requestedFPS != 120) {
        m_senderStatusLabel->setText("Choose 60 or 120 FPS"); return;
    }
    EthernetConfig config;
    config.enabled = m_dedicatedEthernet->isChecked();
    if (config.enabled) {
        const auto data = m_ethernetCombo->currentData().toMap();
        config.interfaceName = data.value("name").toString();
        config.localIP = data.value("ip").toString();
        config.certificateSHA256 = m_certificatePinEdit->text().trimmed();
        m_selectedReceiverPort = 51820;
    }
    m_sender->setEthernetConfig(config);
    QSettings saved;
    saved.setValue("ethernet/enabled", config.enabled);
    saved.setValue("ethernet/interface", config.interfaceName);
    saved.setValue("ethernet/localIP", config.localIP);
    saved.setValue("ethernet/pin", config.certificateSHA256);
    saved.setValue("sender/host", host);
    saved.setValue("sender/fps", requestedFPS);
    saved.setValue("sender/bitrate", m_bitrateSpinBox->value());
    saved.setValue("sender/display", m_monitorCombo->currentData().toMap().value("displayName"));
    m_dedicatedEthernet->setEnabled(false); m_ethernetCombo->setEnabled(false);
    m_certificatePinEdit->setEnabled(false); m_monitorCombo->setEnabled(false);
    m_sendBtn->setEnabled(false);
    m_stopSendBtn->setEnabled(true);
    m_sendHostEdit->setEnabled(false);
    m_fpsCombo->setEnabled(false);
    m_bitrateSpinBox->setEnabled(false);
    m_senderStatusLabel->setText("Starting sender...");
    m_senderStatusLabel->setStyleSheet("font-size: 12px; color: #75e5ce;");

    int fps = m_fpsCombo->currentData().toInt();
    int bitrate = m_bitrateSpinBox->value();
    LogManager::instance().log(QString("Starting sender to %1 at %2 FPS, %3 Mbps")
                                   .arg(host).arg(fps).arg(bitrate));
    m_updater->setSessionActive(true);
    if (!m_sender->startSending(host, m_selectedReceiverPort, fps, bitrate)) onStopSendingClicked();
}

void MainWindow::onCreateVirtualDisplay() {
    if (!m_sender || !m_sender->vdd()) return;

    QSize res = m_vddResolutionCombo->currentData().toSize();
    int w = res.width(), h = res.height();
    if (w <= 0 || h <= 0) { w = 1920; h = 1080; }

    m_createVddBtn->setEnabled(false);
    m_vddStatusLabel->setText("Creating virtual display...");
    m_vddStatusLabel->setStyleSheet("font-size: 12px; color: #75e5ce;");
    LogManager::instance().log(QString("Creating virtual display %1x%2...").arg(w).arg(h));

    const int hz = m_refreshRateCombo->currentData().toInt();
    QSettings settings; settings.setValue("sender/virtualHz", hz);
    settings.setValue("sender/virtualWidth", w); settings.setValue("sender/virtualHeight", h);
    const bool ok = m_sender->vdd()->createVirtualDisplay(w, h, hz);
    m_createVddBtn->setEnabled(true);
    m_vddStatusLabel->setText(ok ? QString("Verified virtual display %1×%2 @ %3 Hz").arg(w).arg(h).arg(hz)
                                : "Virtual mode unavailable; install/configure one VDD monitor and extend it in Windows Settings. See logs.");
    if (ok) {
        onRefreshMonitors();
        for (int i = 0; i < m_monitorCombo->count(); ++i) {
            if (m_monitorCombo->itemData(i).toMap().value("virtual").toBool()) { m_monitorCombo->setCurrentIndex(i); break; }
        }
    }
}

void MainWindow::onRemoveVirtualDisplay() {
    if (!m_sender || !m_sender->vdd()) return;

    m_vddStatusLabel->setText("The installed VDD monitor is reused. Disable/remove it explicitly in Windows Display Settings or the VDD installer; WireDesk does not remove other applications' monitors.");
}

void MainWindow::onRefreshMonitors() {
    if (!m_monitorCombo || !m_sender || !m_sender->vdd()) return;

    const auto currentDisplay = m_monitorCombo->currentData().toMap().value("displayName").toString();
    m_sender->vdd()->refreshInstallStatus();
    m_monitorCombo->clear();

    auto monitors = m_sender->vdd()->enumerateMonitors();
    if (monitors.isEmpty()) {
        m_monitorCombo->addItem("No monitors detected (adapter 0, output 0)");
        QVariantMap defaultData;
        defaultData["adapter"] = 0;
        defaultData["output"] = 0;
        defaultData["virtual"] = false;
        m_monitorCombo->setItemData(0, defaultData);
        return;
    }

    for (const auto& mon : monitors) {
        QString label = QString("%1  %2x%3  (%4)")
                            .arg(mon.name)
                            .arg(mon.width).arg(mon.height)
                            .arg(mon.adapterName);
        if (mon.isVirtual) {
            label += "  [Virtual]";
        }

        QVariantMap data;
        data["adapter"] = mon.adapterIndex;
        data["output"] = mon.outputIndex;
        data["virtual"] = mon.isVirtual;
        data["displayName"] = mon.name;
        m_monitorCombo->addItem(label, data);
    }

    const auto selected = currentDisplay.isEmpty() ? QSettings().value("sender/display").toString() : currentDisplay;
    if(selected.isEmpty())for(int i=0;i<m_monitorCombo->count();++i)if(m_monitorCombo->itemData(i).toMap().value("virtual").toBool()) { m_monitorCombo->setCurrentIndex(i); break; }
    if(m_vddStatusLabel)m_vddStatusLabel->setText(m_sender->vdd()->isVddInstalled()?"VDD найден. Для отдельного экрана в Windows выберите «Расширить рабочий стол».":"Нужен установленный Virtual Display Driver. Приложение не устанавливает драйвер и не меняет Secure Boot самостоятельно.");
    for (int i = 0; i < m_monitorCombo->count(); ++i)
        if (m_monitorCombo->itemData(i).toMap().value("displayName").toString() == selected) m_monitorCombo->setCurrentIndex(i);
    LogManager::instance().log(QString("Found %1 monitor(s)").arg(monitors.size()));
}

void MainWindow::onMonitorSelected(int index) {
    if (!m_monitorCombo || index < 0) return;
    QVariantMap data = m_monitorCombo->itemData(index).toMap();
    LogManager::instance().log(QString("Selected monitor: %1 (adapter %2, output %3)")
                                   .arg(m_monitorCombo->currentText())
                                   .arg(data.value("adapter", 0).toInt())
                                   .arg(data.value("output", 0).toInt()));
}

void MainWindow::onStopSendingClicked() {
    m_autoConnectAttempted = true;
    m_sender->stopSending();
    m_updater->setSessionActive(!m_network->clients().isEmpty());
    m_sendBtn->setEnabled(true); m_stopSendBtn->setEnabled(false); m_sendHostEdit->setEnabled(true);
    m_dedicatedEthernet->setEnabled(true); m_ethernetCombo->setEnabled(true);
    m_certificatePinEdit->setEnabled(true); m_monitorCombo->setEnabled(true);
    m_senderStatusLabel->setText("Sender stopped");
    m_senderStatusLabel->setStyleSheet("font-size: 12px; color: #888;");
    m_fpsCombo->setEnabled(true);
    m_bitrateSpinBox->setEnabled(true);
    LogManager::instance().log("Sender stopped");
}

void MainWindow::onReceiverDiscovered(const DiscoveredService& service) {
    if (m_dedicatedEthernet && m_dedicatedEthernet->isChecked()) return;
    if (!m_receiverCombo) return;

    // Remove the "Searching..." placeholder on first discovery
    if (m_receiverCombo->count() == 1 && !m_receiverCombo->isEnabled()) {
        m_receiverCombo->clear();
        m_receiverCombo->setEnabled(true);
        m_receiverCombo->addItem("Select a receiver...");
    }

    // All BetterCast receivers listen on port 51820. mDNS may report a different port
    // (e.g., from the P2P/AWDL listener) which is unreachable from Windows/Linux.
    // Always use the standard port for reliability.
    uint16_t port = 51820;

    // Check if already in the list
    QString entry = QString("%1  (%2:%3)").arg(service.name, service.host).arg(port);
    for (int i = 0; i < m_receiverCombo->count(); i++) {
        QVariantMap existing = m_receiverCombo->itemData(i).toMap();
        if (existing.value("host").toString() == service.host) {
            m_receiverCombo->setItemText(i, entry);
            QVariantMap updated;
            updated["host"] = service.host;
            updated["port"] = port;
            m_receiverCombo->setItemData(i, updated);
            return;
        }
    }

    QVariantMap data;
    data["host"] = service.host;
    data["port"] = port;
    m_receiverCombo->addItem(entry, data);
    if (service.port != port) {
        LogManager::instance().log(QString("Discovered receiver: %1 at %2 (mDNS reported port %3, using standard port %4)")
                                       .arg(service.name, service.host).arg(service.port).arg(port));
    } else {
        LogManager::instance().log(QString("Discovered receiver: %1 at %2:%3")
                                       .arg(service.name, service.host).arg(port));
    }
}

void MainWindow::onReceiverSelected(int index) {
    if (!m_receiverCombo || !m_sendHostEdit) return;
    QVariantMap data = m_receiverCombo->itemData(index).toMap();
    QString host = data.value("host").toString();
    if (!host.isEmpty()) {
        m_sendHostEdit->setText(host);
        m_selectedReceiverPort = static_cast<uint16_t>(data.value("port", 51820).toUInt());
    }
}

void MainWindow::onInviteReceived(const QString& deviceName, const QString& peerAddress) {
    if (m_dedicatedEthernet && m_dedicatedEthernet->isChecked()) return;
    if (!m_sender || !m_sendHostEdit) return;

    // Already streaming — an invite is not a reason to tear down the current session.
    if (m_sendBtn && !m_sendBtn->isEnabled()) {
        LogManager::instance().log(
            QString("Invite from '%1' ignored — already sending").arg(deviceName));
        return;
    }

    // Prefer the address the invite arrived from. The Mac looks the name up in its
    // browse list because it wants an AWDL route; there is no AWDL here, and the peer
    // address is by definition reachable — it just reached us.
    QString host = peerAddress;
    uint16_t port = 51820;

    // An IPv6 peer address is still worth swapping for the IPv4 the receiver
    // advertised, since that is the address the stream socket expects.
    for (const auto& svc : m_discovery->discoveredServices()) {
        if (svc.name.compare(deviceName, Qt::CaseInsensitive) == 0) {
            host = svc.host;
            break;
        }
    }

    if (host.startsWith("::ffff:")) host = host.mid(7);  // IPv4-mapped IPv6
    if (host.isEmpty()) {
        LogManager::instance().log(
            QString("Invite from '%1' had no usable address").arg(deviceName));
        return;
    }

    LogManager::instance().log(
        QString("Accepting invite from '%1' — sending to %2:%3")
            .arg(deviceName, host).arg(port));

    m_sendHostEdit->setText(host);
    m_selectedReceiverPort = port;

    // Show the user what just happened rather than starting a stream invisibly.
    if (m_sidebarList && m_pageSend >= 0) {
        m_sidebarList->setCurrentRow(m_pageSend);
    }

    onSendScreenClicked();
}
#endif

// ─── Log Slots ──────────────────────────────────────────────────────────────────

void MainWindow::onLogAdded(const QString& entry) {
    if (m_logViewer) {
        m_logViewer->append(entry);
    }
}

void MainWindow::onCopyLogs() {
    QApplication::clipboard()->setText(
        LogManager::instance().entries().join("\n"));
    LogManager::instance().log("Logs copied to clipboard");
}

void MainWindow::onClearLogs() {
    LogManager::instance().clear();
    if (m_logViewer) m_logViewer->clear();
}

void MainWindow::onReportIssue() {
    QString sysInfo = QString("Platform: %1, WireDesk %2")
        .arg(
#ifdef _WIN32
            "Windows"
#elif __linux__
            "Linux"
#else
            "Unknown"
#endif
        )
        .arg(QApplication::applicationVersion());

    QStringList recentLogs = LogManager::instance().entries();
    if (recentLogs.size() > 30) {
        recentLogs = recentLogs.mid(recentLogs.size() - 30);
    }

    QString body = QString(
        "**Describe the issue:**\n\n\n"
        "**Steps to reproduce:**\n1. \n\n"
        "**Expected behavior:**\n\n\n"
        "**System info:** %1\n\n"
        "<details><summary>Recent Logs</summary>\n\n```\n%2\n```\n\n</details>"
    ).arg(sysInfo, recentLogs.join("\n"));

    QString url = QString("https://github.com/b0rdik/WireDesk/issues/new?title=%1&body=%2")
        .arg(QString("Bug: ").toUtf8().toPercentEncoding(),
             body.toUtf8().toPercentEncoding());

    QDesktopServices::openUrl(QUrl(url));
    LogManager::instance().log("Opened GitHub issue form");
}

// Key/mouse events for video are handled by VideoWindow

// ─── Local IP Display ───────────────────────────────────────────────────────────

void MainWindow::updateLocalIpDisplay() {
    QStringList ips;
    for (const auto& iface : QNetworkInterface::allInterfaces()) {
        if (iface.flags().testFlag(QNetworkInterface::IsUp) &&
            iface.flags().testFlag(QNetworkInterface::IsRunning) &&
            !iface.flags().testFlag(QNetworkInterface::IsLoopBack)) {
            for (const auto& entry : iface.addressEntries()) {
                if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                    ips.append(entry.ip().toString());
                }
            }
        }
    }

    QString text = ips.isEmpty()
        ? "No network detected"
        : "This device: " + ips.join(" / ") + " : 51820";

    if (m_overviewIpLabel) m_overviewIpLabel->setText(text);
    if (m_recvIpLabel) m_recvIpLabel->setText(text);
}

#ifdef ENABLE_SENDER
void MainWindow::applyEthernetMode(bool enabled) {
    QSettings().setValue("ethernet/enabled", enabled);
    if (enabled) {
        m_discovery->stopBrowsing(); m_discovery->stopAdvertising(); m_discovery->stopAdvertisingSenderService();
        m_inviteListener->stop(); m_network->stop();
        m_connectBtn->setEnabled(false); m_adbBtn->setEnabled(false);
        m_receiverCombo->clear(); m_receiverCombo->addItem("Discovery disabled; enter Mac IPv4"); m_receiverCombo->setEnabled(false);
    } else {
        m_network->start(); m_connectBtn->setEnabled(true); m_adbBtn->setEnabled(true);
        m_discovery->startAdvertising(m_network->actualTcpPort()); m_discovery->startBrowsing();
        if (auto port = m_inviteListener->start()) m_discovery->advertiseSenderService(port);
    }
}
#endif

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    const bool compact=width()<860;
    if(m_sidebarList) {
        m_sidebarList->setFixedWidth(compact?64:200);
        if(m_splitter)m_splitter->setSizes({compact?64:200,qMax(1,width()-(compact?64:200))});
        for(int i=0;i<m_sidebarList->count();++i) {
            auto* item=m_sidebarList->item(i);
            const auto label=item->data(Qt::UserRole+1).toString();
            if(label.isEmpty()) item->setHidden(compact);
            else { item->setText(compact?QString():label); item->setToolTip(label); }
        }
    }
#ifdef ENABLE_SENDER
    if(m_setupCards)m_setupCards->setDirection(width()<1120?QBoxLayout::TopToBottom:QBoxLayout::LeftToRight);
    if(m_sendLayout)m_sendLayout->setContentsMargins(compact?16:28,24,compact?16:28,24);
#endif
}

bool MainWindow::importPairingCode(const QString& code) {
#ifdef ENABLE_SENDER
    if(!m_sender || m_sender->isSending())return false;
    PairingProfile profile;
    if(!PairingProfile::parse(code,&profile)) { m_senderStatusLabel->setText("Неверный код WireDesk. Скопируйте полный код на Mac или откройте файл сопряжения."); return false; }
    m_sendHostEdit->setText(profile.ip); m_certificatePinEdit->setText(profile.fingerprint); m_dedicatedEthernet->setChecked(true);
    QSettings saved; saved.setValue("sender/host",profile.ip); saved.setValue("ethernet/pin",profile.fingerprint); saved.setValue("ethernet/enabled",true);
    refreshEthernetInterfaces();
    int match=-1,count=0; const QHostAddress remote(profile.ip);
    for(int i=0;i<m_ethernetCombo->count();++i) {
        const auto data=m_ethernetCombo->itemData(i).toMap();
        const auto iface=QNetworkInterface::interfaceFromName(data.value("name").toString());
        for(const auto& address:iface.addressEntries())if(address.ip().toString()==data.value("ip").toString()&&address.prefixLength()>0&&remote.isInSubnet(address.ip(),address.prefixLength())&&address.ip()!=remote) { match=i; ++count; break; }
    }
    m_ethernetCombo->setCurrentIndex(count==1?match:-1);
    m_senderStatusLabel->setText(count==1?"Mac сохранён. Ethernet выбран; подтвердите подключение на Mac.":"Mac сохранён. Выберите подходящий Ethernet-адрес в одной подсети с Mac.");
    m_autoConnectAttempted=false;
    return true;
#else
    Q_UNUSED(code); return false;
#endif
}
#ifdef ENABLE_SENDER
void MainWindow::refreshEthernetInterfaces() {
    if(!m_ethernetCombo || m_sender->isSending())return;
    const auto current=m_ethernetCombo->currentData().toMap();
    QSettings saved;
    const QString wantedName=current.value("name",saved.value("ethernet/interface")).toString();
    const QString wantedIP=current.value("ip",saved.value("ethernet/localIP")).toString();
    QSignalBlocker block(m_ethernetCombo); m_ethernetCombo->clear();
    int selected=-1;
    for(const auto& iface:QNetworkInterface::allInterfaces()) {
        if(iface.type()!=QNetworkInterface::Ethernet || !iface.flags().testFlag(QNetworkInterface::IsRunning))continue;
        for(const auto& entry:iface.addressEntries()) {
            if(entry.ip().protocol()!=QAbstractSocket::IPv4Protocol || entry.ip()==QHostAddress::AnyIPv4)continue;
            QVariantMap data; data["name"]=iface.name(); data["ip"]=entry.ip().toString();
            m_ethernetCombo->addItem(QString("%1 · %2").arg(iface.humanReadableName(),entry.ip().toString()),data);
            if(iface.name()==wantedName&&entry.ip().toString()==wantedIP)selected=m_ethernetCombo->count()-1;
        }
    }
    if(wantedName.isEmpty()&&m_ethernetCombo->count()==1)selected=0;
    m_ethernetCombo->setCurrentIndex(selected);
    m_ethernetCombo->setPlaceholderText(wantedName.isEmpty()?"Ожидание Ethernet IPv4…":"Выбранный Ethernet/IP недоступен");
}
void MainWindow::attemptAutomaticConnection() {
    if(!m_autoConnect || !m_autoConnect->isChecked() || m_autoConnectAttempted || m_sender->isSending() || !m_dedicatedEthernet->isChecked())return;
    if(m_sendHostEdit->text().isEmpty()||m_certificatePinEdit->text().size()!=64||m_ethernetCombo->currentIndex()<0||m_monitorCombo->currentIndex()<0)return;
    if(!m_sender->vdd()||!m_sender->vdd()->isVddInstalled())return;
    const auto monitor=m_monitorCombo->currentData().toMap();
    if(!monitor.value("virtual").toBool())return; // never automatically stream a user's primary display
    if(!m_sender->vdd()->verifyDisplayMode(monitor.value("displayName").toString(),0,0,m_fpsCombo->currentData().toInt())&& !m_autoDisplay->isChecked())return;
    m_autoConnectAttempted=true; onSendScreenClicked();
}
#endif
