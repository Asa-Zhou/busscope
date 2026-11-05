#include "app/main_window.h"

#include <QApplication>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <dwmapi.h>
#endif

#include "app/can_page.h"
#include "app/icons.h"
#include "app/poll_page.h"
#include "app/slave_page.h"
#include "app/theme.h"
#include "io/modbus_link.h"
#include "io/poll_engine.h"

namespace busscope {

namespace {

QString humanBytes(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("BusScope — Modbus RTU 调试台"));

    // 高 DPI 下手动 clamp：设计稿 1440x900 在 125% 缩放的 1080p 屏上放不下，
    // 直接 resize 会被系统压缩，导致底部详情栏被挤扁。
    const QRect avail = QApplication::primaryScreen()->availableGeometry();
    resize(qMin(1440, avail.width() - 40), qMin(900, avail.height() - 40));
    setMinimumSize(1180, 700);

    // ModbusLink 与 PollEngine 被 Slave / Poll 两页共用，放在主窗口持有
    m_link = new ModbusLink(this);
    m_poll = new PollEngine(m_link, this);

    m_slavePage = new SlavePage(m_link, m_poll);
    m_pollPage  = new PollPage(m_poll, m_link);
    m_canPage   = new CanPage;

    // 协议页栈：左侧选协议，右侧显示对应页面
    m_protocolStack = new QStackedWidget;
    m_protocolStack->addWidget(buildModbusPage());   // 0
    m_protocolStack->addWidget(m_canPage);           // 1

    auto *central = new QWidget;
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildProtocolRail());
    root->addWidget(m_protocolStack, 1);
    setCentralWidget(central);

    buildStatusBar();

    // ---- 跨页接线：三页的状态轻提示统一汇到状态栏 ----
    auto relayStatus = [this](const QString &text, int ms) {
        statusBar()->showMessage(text, ms);
    };
    connect(m_slavePage, &SlavePage::statusMessage, this, relayStatus);
    connect(m_pollPage, &PollPage::statusMessage, this, relayStatus);
    connect(m_canPage, &CanPage::statusMessage, this, relayStatus);

    // ---- 跨页接线：其他页的错误提示帧统一落到 Slave 页的帧记录 ----
    connect(m_pollPage, &PollPage::linkError, m_slavePage, &SlavePage::appendNote);
    connect(m_canPage, &CanPage::linkError, m_slavePage, &SlavePage::appendNote);

    // ---- 跨页接线：点表载入后切到轮询页 ----
    connect(m_pollPage, &PollPage::requestShow, this, [this] {
        if (m_tabs)
            m_tabs->setCurrentIndex(1);
    });

    // ---- 跨页接线：周期发送与轮询互斥（规则收口在这里，两页互不知晓） ----
    connect(m_pollPage, &PollPage::pollingStarting, this, [this] {
        if (m_slavePage->isPeriodicActive()) {
            m_slavePage->stopPeriodic();
            statusBar()->showMessage(QStringLiteral("周期发送与轮询互斥，已自动停止周期发送"), 4000);
        }
    });

    // ---- 链路状态：连接记忆与状态栏计数归属主窗口 ----
    connect(m_link, &ModbusLink::stateChanged, this, [this](bool open) {
        if (open) {
            // 连上就记住这次用的口，下次启动自动重连
            const PortConfig cfg = m_link->config();
            QSettings st;
            st.setValue(QStringLiteral("conn/port"), cfg.portName);
            st.setValue(QStringLiteral("conn/baud"), cfg.baudRate);
            m_statusConn->setText(QStringLiteral("已连接 %1 · %2 8N1 · 超时 %3 ms")
                                      .arg(cfg.portName)
                                      .arg(cfg.baudRate)
                                      .arg(m_link->responseTimeout()));
        } else {
            m_statusConn->setText(QStringLiteral("未连接"));
        }
    });
    connect(m_link, &ModbusLink::countersChanged, this, [this](qint64 rxBytes, qint64 txBytes) {
        m_rxBytes = rxBytes;
        m_txBytes = txBytes;
        m_statusCounters->setText(QStringLiteral("RX %1   TX %2   错误 %3")
                                      .arg(humanBytes(m_rxBytes), humanBytes(m_txBytes))
                                      .arg(0));
    });

    // ---- 轮询引擎的日志走状态栏 ----
    connect(m_poll, &PollEngine::logMessage, this, [this](const QString &text) {
        statusBar()->showMessage(text, 4000);
    });

    // ---- 窗口级快捷键：发送（Ctrl+Enter）与串口重扫（Ctrl+R） ----
    auto *send1 = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_Return), this);
    connect(send1, &QShortcut::activated, m_slavePage, &SlavePage::sendManual);
    auto *send2 = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_Enter), this);
    connect(send2, &QShortcut::activated, m_slavePage, &SlavePage::sendManual);
    auto *refreshShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+R")), this);
    connect(refreshShortcut, &QShortcut::activated, m_slavePage, &SlavePage::refreshPorts);

    m_slavePage->refreshPorts();

    // 恢复上次的点表 + 串口，打开就能看到数据
    {
        QSettings st;

        // 上次用的点表：没在命令行里指定过就自动载入
        const QString lastFile = st.value(QStringLiteral("poll/lastFile")).toString();
        if (m_poll->taskCount() == 0 && !lastFile.isEmpty() && QFile::exists(lastFile)) {
            QTimer::singleShot(0, this, [this, lastFile] {
                if (m_poll->taskCount() == 0)
                    openPointFile(lastFile);
            });
        }

        const QString lastPort = st.value(QStringLiteral("conn/port")).toString();
        const int lastBaud = st.value(QStringLiteral("conn/baud"), 115200).toInt();
        const bool autoOpen = st.value(QStringLiteral("conn/auto"), true).toBool();

        if (autoOpen && !lastPort.isEmpty() && m_slavePage->hasPort(lastPort)) {
            QTimer::singleShot(350, this, [this, lastPort, lastBaud] {
                // 点表已经有任务就直接开轮询，省得再点一次
                const bool autoPoll = (m_poll && m_poll->taskCount() > 0);
                openSerialDirect(lastPort, lastBaud, autoPoll);
            });
        }
    }

#ifdef Q_OS_WIN
    // 让系统标题栏跟着深色主题走；属性 20 是 Win10 20H1+，19 兼容 1809~1909
    {
        HWND hwnd = reinterpret_cast<HWND>(winId());
        BOOL dark = TRUE;
        if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark))))
            DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    }
#endif
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------- 协议导航

QWidget *MainWindow::buildProtocolRail()
{
    auto *w = new QWidget;
    w->setObjectName(QStringLiteral("ProtocolRail"));
    w->setFixedWidth(96);

    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(8, 12, 8, 12);
    v->setSpacing(6);

    auto makeRail = [](const QString &text, const QIcon &icon) {
        auto *b = new QToolButton;
        b->setObjectName(QStringLiteral("RailButton"));
        b->setText(text);
        b->setIcon(icon);
        b->setIconSize(QSize(20, 20));
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedHeight(64);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        return b;
    };

    m_railModbus = makeRail(QStringLiteral("Modbus"), icons::gauge(theme::textSecondary()));
    m_railCan    = makeRail(QStringLiteral("CAN"), icons::bus(theme::textSecondary()));

    connect(m_railModbus, &QToolButton::clicked, this, [this] { setProtocol(0); });
    connect(m_railCan, &QToolButton::clicked, this, [this] { setProtocol(1); });

    v->addWidget(m_railModbus);
    v->addWidget(m_railCan);
    v->addStretch(1);

    return w;
}

void MainWindow::setProtocol(int index)
{
    if (!m_protocolStack)
        return;

    m_protocolStack->setCurrentIndex(index);

    const QColor active(0xA5, 0xAE, 0xFF);
    const QColor idle = theme::textSecondary();

    if (m_railModbus) {
        m_railModbus->setChecked(index == 0);
        m_railModbus->setIcon(icons::gauge(index == 0 ? active : idle));
    }
    if (m_railCan) {
        m_railCan->setChecked(index == 1);
        m_railCan->setIcon(icons::bus(index == 1 ? active : idle));
    }
}

QWidget *MainWindow::buildModbusPage()
{
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto *tabs = new QTabWidget;
    m_tabs = tabs;
    tabs->setDocumentMode(true);
    tabs->addTab(m_slavePage, QStringLiteral("Slave · 收发"));
    tabs->addTab(m_pollPage, QStringLiteral("Poll · 轮询"));

    v->addWidget(tabs);
    return page;
}

// ---------------------------------------------------------------- 状态栏

void MainWindow::buildStatusBar()
{
    m_statusConn = new QLabel(QStringLiteral("未连接"));
    m_statusCounters = new QLabel(QStringLiteral("RX 0 B   TX 0 B   错误 0"));

    statusBar()->addWidget(m_statusConn);
    statusBar()->addPermanentWidget(m_statusCounters);
    statusBar()->setSizeGripEnabled(false);
}

// ---------------------------------------------------------------- 命令行入口

bool MainWindow::openPointFile(const QString &path)
{
    return m_pollPage->openPointFile(path);
}

bool MainWindow::loadDbcFile(const QString &path)
{
    return m_canPage->loadDbcFile(path);
}

bool MainWindow::openSerialDirect(const QString &portName, int baudRate, bool autoPoll)
{
    if (m_link->isOpen())
        m_link->close();

    // 让下拉框同步到实际参数，界面上能看出在用哪个口
    m_slavePage->refreshPorts();
    if (!m_slavePage->openSerial(portName, baudRate))
        return false;

    if (autoPoll) {
        if (m_tabs)
            m_tabs->setCurrentIndex(1);
        if (m_poll && !m_poll->isRunning())
            m_poll->start();
    }
    return true;
}

bool MainWindow::openCanDirect(quint32 deviceType, quint32 channel, int bitrate)
{
    setProtocol(1);
    return m_canPage->openDirect(deviceType, channel, bitrate);
}

} // namespace busscope
