#include "app/main_window.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QSerialPortInfo>
#include <QShortcut>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTableView>
#include <QTextEdit>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QtGlobal>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <dwmapi.h>
#endif

#include <cctype>

#include "app/icons.h"
#include "app/theme.h"
#include "app/wave_chart.h"
#include "core/can_frame.h"
#include "core/can_frame_model.h"
#include "core/frame.h"
#include "core/frame_log_model.h"
#include "core/poll_table_model.h"
#include "core/poll_task.h"
#include "io/can_link.h"
#include "proto/can/dbc.h"
#include "io/modbus_link.h"
#include "io/poll_engine.h"

namespace busscope {

namespace {

QLabel *sectionTitle(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("SectionTitle"));
    return label;
}

QWidget *fieldRow(const QString &label, QWidget *field)
{
    auto *row = new QWidget;
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(8);

    auto *l = new QLabel(label);
    l->setObjectName(QStringLiteral("FieldLabel"));
    l->setFixedWidth(40);
    h->addWidget(l);
    h->addWidget(field, 1);
    return row;
}

QFrame *cardFrame()
{
    auto *frame = new QFrame;
    frame->setObjectName(QStringLiteral("Card"));
    return frame;
}

QFrame *hairLine()
{
    auto *line = new QFrame;
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background:#232428;border:none;"));
    return line;
}

void applyIcon(QPushButton *button, const QIcon &icon)
{
    button->setIcon(icon);
    button->setIconSize(QSize(14, 14));
}

/// 把 "01 03 00 0A" / "0103000a" 这类文本转成字节。空串视为合法空数据。
QByteArray hexToBytes(const QString &text, bool *ok){
    QString hex;
    for (const QChar c : text) {
        if (c.isSpace() || c == QLatin1Char(',') || c == QLatin1Char('-') || c == QLatin1Char(':'))
            continue;
        if (!std::isxdigit(static_cast<unsigned char>(c.toLatin1()))) {
            *ok = false;
            return QByteArray();
        }
        hex.append(c);
    }
    if (hex.isEmpty()) {
        *ok = true;
        return QByteArray();
    }
    if (hex.size() % 2 != 0) {
        *ok = false;
        return QByteArray();
    }
    *ok = true;
    return QByteArray::fromHex(hex.toLatin1());
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

    m_link = new ModbusLink(this);
    m_model = new FrameLogModel(this);
    m_poll = new PollEngine(m_link, this);
    m_pollModel = new PollTableModel(m_poll, this);
    m_can = new CanLink(this);
    m_canModel = new CanFrameModel(this);

    // 协议页栈：左侧选协议，右侧显示对应页面
    m_protocolStack = new QStackedWidget;
    m_protocolStack->addWidget(buildModbusPage());   // 0
    m_protocolStack->addWidget(buildCanPage());      // 1

    auto *central = new QWidget;
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildProtocolRail());
    root->addWidget(m_protocolStack, 1);
    setCentralWidget(central);

    buildStatusBar();

    connect(m_link, &ModbusLink::frameReady, this, &MainWindow::onFrameReady);
    connect(m_link, &ModbusLink::stateChanged, this, &MainWindow::onLinkStateChanged);
    connect(m_link, &ModbusLink::errorText, this, &MainWindow::onLinkError);
    connect(m_link, &ModbusLink::countersChanged, this, &MainWindow::onCountersChanged);

    connect(m_poll, &PollEngine::stateChanged, this, &MainWindow::onPollStateChanged);
    connect(m_poll, &PollEngine::summaryChanged, this, &MainWindow::onPollSummary);
    connect(m_poll, &PollEngine::taskUpdated, this, &MainWindow::onPollTaskUpdated);
    connect(m_poll, &PollEngine::logMessage, this, &MainWindow::onPollLog);

    connect(m_can, &CanLink::frameReady, this, &MainWindow::onCanFrame);
    connect(m_can, &CanLink::stateChanged, this, &MainWindow::onCanState);
    connect(m_can, &CanLink::countersChanged, this, &MainWindow::onCanCounters);

    auto *send1 = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_Return), this);
    connect(send1, &QShortcut::activated, this, &MainWindow::sendManual);
    auto *send2 = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_Enter), this);
    connect(send2, &QShortcut::activated, this, &MainWindow::sendManual);

    m_sendTimer = new QTimer(this);
    connect(m_sendTimer, &QTimer::timeout, this, &MainWindow::sendPeriodic);
    connect(m_periodic, &QCheckBox::toggled, this, [this](bool on) {
        if (!on) {
            m_sendTimer->stop();
            return;
        }
        if (!m_link->isOpen()) {
            onLinkError(QStringLiteral("请先打开串口，再启用周期发送"));
            m_periodic->setChecked(false);
            return;
        }
        if (m_poll->isRunning()) {
            // 半双工总线上手动周期帧会不断抢走轮询的事务槽，两边解析都会错乱
            onLinkError(QStringLiteral("轮询进行中，请先停止轮询再启用周期发送"));
            m_periodic->setChecked(false);
            return;
        }
        m_sendTimer->start(m_interval->value());
    });
    connect(m_interval, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int ms) {
        if (m_periodic->isChecked())
            m_sendTimer->start(ms);
    });

    refreshPorts();
    setConnectionUi(false);

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

        if (autoOpen && !lastPort.isEmpty()) {
            bool exists = false;
            for (int i = 0; i < m_portBox->count(); ++i) {
                if (m_portBox->itemData(i).toString() == lastPort) {
                    exists = true;
                    break;
                }
            }
            if (exists) {
                QTimer::singleShot(350, this, [this, lastPort, lastBaud] {
                    // 点表已经有任务就直接开轮询，省得再点一次
                    const bool autoPoll = (m_poll && m_poll->taskCount() > 0);
                    openSerialDirect(lastPort, lastBaud, autoPoll);
                });
            }
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

// ---------------------------------------------------------------- 左侧面板

QWidget *MainWindow::buildLeftPanel()
{
    auto *w = new QWidget;
    w->setObjectName(QStringLiteral("LeftPanel"));
    w->setMinimumWidth(300);
    w->setMaximumWidth(348);

    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(12, 12, 12, 12);
    v->setSpacing(14);

    v->addWidget(sectionTitle(QStringLiteral("连接配置")));

    auto *configCard = cardFrame();
    auto *cv = new QVBoxLayout(configCard);
    cv->setContentsMargins(10, 10, 10, 10);
    cv->setSpacing(7);

    m_portBox = new QComboBox;
    m_portBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);

    m_baudBox = new QComboBox;
    const QList<int> bauds = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
    for (int baud : bauds)
        m_baudBox->addItem(QString::number(baud), baud);
    m_baudBox->setCurrentText(QStringLiteral("115200"));

    m_frameBox = new QComboBox;
    m_frameBox->addItem(QStringLiteral("8-N-1"), QVariantList() << 8 << 1 << 0);
    m_frameBox->addItem(QStringLiteral("8-E-1"), QVariantList() << 8 << 1 << 2);
    m_frameBox->addItem(QStringLiteral("8-O-1"), QVariantList() << 8 << 1 << 1);
    m_frameBox->addItem(QStringLiteral("8-N-2"), QVariantList() << 8 << 2 << 0);
    m_frameBox->addItem(QStringLiteral("7-N-1"), QVariantList() << 7 << 1 << 0);

    // 串口下拉 + 一键重扫。刷新做成 26×26 的图标按钮，把宽度让给下拉框，
    // 否则 "COM3 + 设备描述" 会被截断（Ctrl+R 同效）。
    auto *portRowWithRefresh = new QWidget;
    auto *portRowLayout = new QHBoxLayout(portRowWithRefresh);
    portRowLayout->setContentsMargins(0, 0, 0, 0);
    portRowLayout->setSpacing(6);
    portRowLayout->addWidget(m_portBox, 1);

    auto *refreshBtn = new QPushButton;
    refreshBtn->setObjectName(QStringLiteral("IconButton"));
    refreshBtn->setFixedSize(26, 26);
    refreshBtn->setCursor(Qt::PointingHandCursor);
    refreshBtn->setToolTip(QStringLiteral("重新扫描串口列表（Ctrl+R）"));
    applyIcon(refreshBtn, icons::refresh(theme::textSecondary()));
    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshPorts);
    portRowLayout->addWidget(refreshBtn);

    auto *refreshShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+R")), this);
    connect(refreshShortcut, &QShortcut::activated, this, &MainWindow::refreshPorts);

    cv->addWidget(fieldRow(QStringLiteral("串口"), portRowWithRefresh));
    cv->addWidget(fieldRow(QStringLiteral("波特率"), m_baudBox));
    cv->addWidget(fieldRow(QStringLiteral("帧格式"), m_frameBox));
    cv->addWidget(hairLine());

    auto *statusRow = new QWidget;
    auto *sh = new QHBoxLayout(statusRow);
    sh->setContentsMargins(0, 0, 0, 0);
    sh->setSpacing(6);

    m_connDot = new QLabel;
    m_connDot->setFixedSize(6, 6);
    m_connDot->setStyleSheet(QStringLiteral("background:#F85149;border-radius:3px;"));
    m_connText = new QLabel(QStringLiteral("未连接"));
    m_connText->setObjectName(QStringLiteral("StatusBad"));
    sh->addWidget(m_connDot);
    sh->addWidget(m_connText);
    sh->addStretch(1);
    cv->addWidget(statusRow);

    v->addWidget(configCard);

    m_connectBtn = new QPushButton(QStringLiteral("打开串口"));
    m_connectBtn->setObjectName(QStringLiteral("Primary"));
    m_connectBtn->setMinimumHeight(32);
    m_connectBtn->setCursor(Qt::PointingHandCursor);
    applyIcon(m_connectBtn, icons::power(Qt::white));
    connect(m_connectBtn, &QPushButton::clicked, this, &MainWindow::toggleConnection);
    v->addWidget(m_connectBtn);

    v->addWidget(sectionTitle(QStringLiteral("会话")));

    auto *sessionCard = cardFrame();
    auto *sv = new QVBoxLayout(sessionCard);
    sv->setContentsMargins(8, 8, 8, 8);
    sv->setSpacing(3);
    auto *sTitle = new QLabel(QStringLiteral("Modbus RTU"));
    sTitle->setStyleSheet(QStringLiteral("font-weight:600;color:#E8E8EA;"));
    auto *sMeta = new QLabel(QStringLiteral("主站 · 单会话 · 200 ms 超时"));
    sMeta->setObjectName(QStringLiteral("Hint"));
    sv->addWidget(sTitle);
    sv->addWidget(sMeta);
    v->addWidget(sessionCard);

    v->addWidget(sectionTitle(QStringLiteral("轮询表")));
    auto *pollHint = new QLabel(QStringLiteral("点表在中间「点表 · 轮询」页维护，\n支持 CSV 导入导出、双击写值"));
    pollHint->setObjectName(QStringLiteral("Hint"));
    pollHint->setWordWrap(true);
    v->addWidget(pollHint);

    v->addStretch(1);

    auto *longHint = new QLabel(QStringLiteral("Ctrl + Enter 发送\n失败帧以琥珀色标记"));
    longHint->setObjectName(QStringLiteral("Hint"));
    longHint->setWordWrap(true);
    v->addWidget(longHint);

    return w;
}

// ---------------------------------------------------------------- 中间区域

QWidget *MainWindow::buildCenter()
{
    auto *w = new QWidget;
    w->setObjectName(QStringLiteral("CenterArea"));

    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto *page = new QWidget;
    auto *pv = new QVBoxLayout(page);
    pv->setContentsMargins(0, 0, 0, 0);
    pv->setSpacing(0);

    // 工具条
    auto *bar = new QWidget;
    bar->setFixedHeight(40);
    bar->setStyleSheet(QStringLiteral("background:#101113;border-bottom:1px solid #1D1E22;"));
    auto *bh = new QHBoxLayout(bar);
    bh->setContentsMargins(12, 0, 12, 0);
    bh->setSpacing(10);

    m_autoScroll = new QCheckBox(QStringLiteral("自动滚动"));
    m_autoScroll->setChecked(true);
    bh->addWidget(m_autoScroll);

    auto *clearBtn = new QPushButton(QStringLiteral("清空"));
    clearBtn->setObjectName(QStringLiteral("Ghost"));
    clearBtn->setCursor(Qt::PointingHandCursor);
    applyIcon(clearBtn, icons::trash(theme::textSecondary()));
    connect(clearBtn, &QPushButton::clicked, this, &MainWindow::clearLog);
    bh->addWidget(clearBtn);

    auto *exportBtn = new QPushButton(QStringLiteral("导出 CSV"));
    exportBtn->setObjectName(QStringLiteral("Ghost"));
    exportBtn->setCursor(Qt::PointingHandCursor);
    applyIcon(exportBtn, icons::exportTray(theme::textSecondary()));
    connect(exportBtn, &QPushButton::clicked, this, &MainWindow::exportLog);
    bh->addWidget(exportBtn);

    bh->addStretch(1);

    m_frameCount = new QLabel(QStringLiteral("0 帧"));
    m_frameCount->setObjectName(QStringLiteral("Hint"));
    bh->addWidget(m_frameCount);
    pv->addWidget(bar);

    // 帧表
    m_table = new QTableView;
    m_table->setModel(m_model);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->setWordWrap(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(30);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);

    auto *hh = m_table->horizontalHeader();
    hh->setHighlightSections(false);
    hh->setSectionResizeMode(QHeaderView::Fixed);
    hh->resizeSection(FrameLogModel::ColTime, 96);
    hh->resizeSection(FrameLogModel::ColDir, 48);
    hh->resizeSection(FrameLogModel::ColSlave, 48);
    hh->resizeSection(FrameLogModel::ColFunction, 62);
    hh->resizeSection(FrameLogModel::ColType, 96);
    hh->resizeSection(FrameLogModel::ColData, 236);
    hh->setSectionResizeMode(FrameLogModel::ColSummary, QHeaderView::Stretch);

    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &MainWindow::showFrameDetail);

    // 空态提示：贴住 viewport，跟随尺寸变化
    m_emptyHint = new QLabel(QStringLiteral("等待数据 —— 打开串口后，用右侧「常用指令」发起一次请求"),
                             m_table->viewport());
    m_emptyHint->setObjectName(QStringLiteral("EmptyHint"));
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_emptyHint->setGeometry(m_table->viewport()->rect());
    m_table->viewport()->installEventFilter(this);

    // 详情区固定高度：QSplitter 在窗口高度不足时会把子控件压到 minHeight 以下，
    // 这里直接用固定高度布局，保证 3 列始终完整可见。
    auto *detailPane = buildDetailPane();
    detailPane->setFixedHeight(200);
    pv->addWidget(m_table, 1);
    pv->addWidget(detailPane);

    v->addWidget(page);
    return w;
}

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
    tabs->addTab(buildSlavePage(), QStringLiteral("Slave · 收发"));
    tabs->addTab(buildPointPage(), QStringLiteral("Poll · 轮询"));

    v->addWidget(tabs);
    return page;
}

QWidget *MainWindow::buildSlavePage()
{
    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(1);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(buildLeftPanel());
    splitter->addWidget(buildCenter());
    splitter->addWidget(buildRightPanel());
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes(QList<int>() << 300 << 780 << 288);
    return splitter;
}

// ---------------------------------------------------------------- 点表页

QWidget *MainWindow::buildPointPage()
{
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto *bar = new QWidget;
    bar->setFixedHeight(40);
    bar->setStyleSheet(QStringLiteral("background:#101113;border-bottom:1px solid #1D1E22;"));
    auto *bh = new QHBoxLayout(bar);
    bh->setContentsMargins(12, 0, 12, 0);
    bh->setSpacing(8);

    m_pollBtn = new QPushButton(QStringLiteral("开始轮询"));
    m_pollBtn->setObjectName(QStringLiteral("Primary"));
    m_pollBtn->setCursor(Qt::PointingHandCursor);
    applyIcon(m_pollBtn, icons::play(Qt::white));
    connect(m_pollBtn, &QPushButton::clicked, this, &MainWindow::togglePolling);
    bh->addWidget(m_pollBtn);

    m_pauseBtn = new QPushButton(QStringLiteral("暂停"));
    m_pauseBtn->setObjectName(QStringLiteral("Ghost"));
    m_pauseBtn->setCursor(Qt::PointingHandCursor);
    m_pauseBtn->setEnabled(false);
    applyIcon(m_pauseBtn, icons::pause(theme::textSecondary()));
    connect(m_pauseBtn, &QPushButton::clicked, this, &MainWindow::togglePause);
    bh->addWidget(m_pauseBtn);

    auto *importBtn = new QPushButton(QStringLiteral("导入点表"));
    importBtn->setObjectName(QStringLiteral("Ghost"));
    importBtn->setCursor(Qt::PointingHandCursor);
    applyIcon(importBtn, icons::importTray(theme::textSecondary()));
    connect(importBtn, &QPushButton::clicked, this, &MainWindow::importPoints);
    bh->addWidget(importBtn);

    auto *exportBtn = new QPushButton(QStringLiteral("导出点表"));
    exportBtn->setObjectName(QStringLiteral("Ghost"));
    exportBtn->setCursor(Qt::PointingHandCursor);
    applyIcon(exportBtn, icons::exportTray(theme::textSecondary()));
    // 点一下直接弹出格式选择，避免在保存对话框的过滤器里翻找不到 TXT
    connect(exportBtn, &QPushButton::clicked, this, [this, exportBtn] {
        QMenu menu(this);
        menu.addAction(QStringLiteral("导出为 CSV  (逗号分隔)"),
                       this, [this] { exportPointsAs(false); });
        menu.addAction(QStringLiteral("导出为 TXT  (Tab 分隔)"),
                       this, [this] { exportPointsAs(true); });
        menu.exec(exportBtn->mapToGlobal(QPoint(0, exportBtn->height() + 2)));
    });
    bh->addWidget(exportBtn);

    auto *sampleBtn = new QPushButton(QStringLiteral("示例点表"));
    sampleBtn->setObjectName(QStringLiteral("Ghost"));
    sampleBtn->setCursor(Qt::PointingHandCursor);
    applyIcon(sampleBtn, icons::folder(theme::textSecondary()));
    connect(sampleBtn, &QPushButton::clicked, this, &MainWindow::loadSamplePoints);
    bh->addWidget(sampleBtn);

    auto *clearBtn = new QPushButton(QStringLiteral("清空"));
    clearBtn->setObjectName(QStringLiteral("Ghost"));
    clearBtn->setCursor(Qt::PointingHandCursor);
    applyIcon(clearBtn, icons::trash(theme::textSecondary()));
    connect(clearBtn, &QPushButton::clicked, this, &MainWindow::clearPoints);
    bh->addWidget(clearBtn);

    m_scopeBtn = new QPushButton(QStringLiteral("示波器"));
    m_scopeBtn->setObjectName(QStringLiteral("Ghost"));
    m_scopeBtn->setCursor(Qt::PointingHandCursor);
    m_scopeBtn->setToolTip(QStringLiteral("打开/隐藏独立示波器窗口；右键点表行可添加曲线"));
    connect(m_scopeBtn, &QPushButton::clicked, this, &MainWindow::toggleScopeWindow);
    bh->addWidget(m_scopeBtn);

    bh->addStretch(1);

    m_pollSummary = new QLabel(QStringLiteral("0 项"));
    m_pollSummary->setObjectName(QStringLiteral("Hint"));
    bh->addWidget(m_pollSummary);
    v->addWidget(bar);

    m_pollTable = new QTableView;
    m_pollTable->setModel(m_pollModel);
    m_pollTable->setAlternatingRowColors(true);
    m_pollTable->setShowGrid(false);
    m_pollTable->setWordWrap(false);
    m_pollTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pollTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pollTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pollTable->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_pollTable->verticalHeader()->setVisible(false);
    m_pollTable->verticalHeader()->setDefaultSectionSize(28);
    m_pollTable->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);

    auto *hh = m_pollTable->horizontalHeader();
    hh->setHighlightSections(false);
    hh->setSectionResizeMode(QHeaderView::Fixed);
    hh->resizeSection(PollTableModel::ColEnable, 52);
    hh->resizeSection(PollTableModel::ColName, 170);
    hh->resizeSection(PollTableModel::ColSlave, 58);
    hh->resizeSection(PollTableModel::ColFunction, 92);
    hh->resizeSection(PollTableModel::ColAddr, 74);
    hh->resizeSection(PollTableModel::ColPeriod, 88);
    hh->resizeSection(PollTableModel::ColValue, 150);
    hh->resizeSection(PollTableModel::ColStatus, 96);
    hh->resizeSection(PollTableModel::ColCount, 92);
    // 让「名称」吃掉多余宽度，计数列保持紧凑（否则最后一列被拉得很空）
    hh->setSectionResizeMode(PollTableModel::ColName, QHeaderView::Stretch);

    connect(m_pollTable, &QTableView::doubleClicked, this, &MainWindow::writeSelectedPoint);

    m_pollTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_pollTable, &QTableView::customContextMenuRequested,
            this, &MainWindow::onPollContextMenu);

    auto *hint = new QLabel(QStringLiteral("双击任意一行可直接写单寄存器（FC06）；CSV 列顺序："
                                           "名称,从站,功能码,地址,数量,周期ms,超时ms,重试,类型,系数,单位"));
    hint->setObjectName(QStringLiteral("Hint"));
    hint->setContentsMargins(12, 6, 12, 6);
    hint->setWordWrap(true);

    v->addWidget(m_pollTable, 1);

    // ---- 示波器（嵌入形态）----
    auto *waveWrap = new QWidget;
    auto *wl = new QVBoxLayout(waveWrap);
    wl->setContentsMargins(12, 2, 12, 6);
    wl->setSpacing(3);

    auto *waveHead = new QWidget;
    auto *whl = new QHBoxLayout(waveHead);
    whl->setContentsMargins(0, 0, 0, 0);
    whl->setSpacing(8);
    auto *waveTip = new QLabel(QStringLiteral("示波器 · 滚轮缩放  拖动平移  双击回最新  右键点表行加曲线"));
    waveTip->setObjectName(QStringLiteral("Hint"));
    whl->addWidget(waveTip);
    whl->addStretch(1);

    m_followBtn = new QPushButton(QStringLiteral("● 实时"));
    m_followBtn->setObjectName(QStringLiteral("GhostSmall"));
    m_followBtn->setCursor(Qt::PointingHandCursor);
    m_followBtn->setEnabled(false);      // 跟随时没什么可点的
    m_followBtn->setToolTip(QStringLiteral("回看历史时点这里跳回最新数据（也可双击波形）"));
    connect(m_followBtn, &QPushButton::clicked, this, [this] {
        if (m_wave)
            m_wave->followLatest();
        if (m_waveFloat)
            m_waveFloat->followLatest();
    });
    whl->addWidget(m_followBtn);

    auto *pauseBtn = new QPushButton(QStringLiteral("暂停"));
    pauseBtn->setObjectName(QStringLiteral("GhostSmall"));
    pauseBtn->setCursor(Qt::PointingHandCursor);
    pauseBtn->setCheckable(true);
    pauseBtn->setToolTip(QStringLiteral("冻结画面看细节，数据仍在后台累积"));
    connect(pauseBtn, &QPushButton::toggled, this, [this, pauseBtn](bool on) {
        if (m_wave)
            m_wave->setPaused(on);
        if (m_waveFloat)
            m_waveFloat->setPaused(on);
        pauseBtn->setText(on ? QStringLiteral("继续") : QStringLiteral("暂停"));
    });
    whl->addWidget(pauseBtn);

    auto *scaleBtn = new QPushButton(QStringLiteral("各自归一"));
    scaleBtn->setObjectName(QStringLiteral("GhostSmall"));
    scaleBtn->setCursor(Qt::PointingHandCursor);
    scaleBtn->setCheckable(true);
    scaleBtn->setChecked(false);
    scaleBtn->setToolTip(QStringLiteral("统一刻度：共用 Y 轴看真实数值；各自归一：只看形状"));
    connect(scaleBtn, &QPushButton::toggled, this, [this, scaleBtn](bool on) {
        if (m_wave)
            m_wave->setUnifiedScale(on);
        if (m_waveFloat)
            m_waveFloat->setUnifiedScale(on);
        scaleBtn->setText(on ? QStringLiteral("统一刻度") : QStringLiteral("各自归一"));
    });
    whl->addWidget(scaleBtn);

    auto *expBtn = new QPushButton(QStringLiteral("导出"));
    expBtn->setObjectName(QStringLiteral("GhostSmall"));
    expBtn->setCursor(Qt::PointingHandCursor);
    expBtn->setToolTip(QStringLiteral("把当前波形存成图片或数据文件"));
    connect(expBtn, &QPushButton::clicked, this, [this, expBtn] {
        QMenu menu(this);
        menu.addAction(QStringLiteral("保存为图片 (PNG)"), this, [this] { exportScopeImage(); });
        menu.addAction(QStringLiteral("导出数据 (CSV)"), this, [this] { exportScopeData(false); });
        menu.addAction(QStringLiteral("导出数据 (TXT / Tab 分隔)"), this, [this] { exportScopeData(true); });
        menu.exec(expBtn->mapToGlobal(QPoint(0, expBtn->height() + 2)));
    });
    whl->addWidget(expBtn);

    auto *popBtn = new QPushButton(QStringLiteral("独立窗口"));
    popBtn->setObjectName(QStringLiteral("GhostSmall"));
    popBtn->setCursor(Qt::PointingHandCursor);
    popBtn->setToolTip(QStringLiteral("把示波器弹出为可缩放的独立窗口"));
    connect(popBtn, &QPushButton::clicked, this, &MainWindow::toggleScopeWindow);
    whl->addWidget(popBtn);
    wl->addWidget(waveHead);

    m_wave = new WaveChart;
    m_wave->setFixedHeight(176);   // 多了底部时间轴滑块一行
    connect(m_wave, &WaveChart::followChanged, this, &MainWindow::updateFollowUi);
    wl->addWidget(m_wave);
    v->addWidget(waveWrap);

    connect(m_pollTable->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex &, const QModelIndex &) { refreshWave(); });

    m_waveTimer = new QTimer(this);
    m_waveTimer->setInterval(200);
    connect(m_waveTimer, &QTimer::timeout, this, &MainWindow::refreshWave);
    m_waveTimer->start();

    v->addWidget(hint);
    return page;
}

void MainWindow::updateFollowUi(bool following)
{
    if (!m_followBtn)
        return;
    m_followBtn->setText(following ? QStringLiteral("● 实时")
                                   : QStringLiteral("回到最新"));
    m_followBtn->setEnabled(!following);
}

void MainWindow::refreshWave()
{
    if (!m_wave)
        return;

    static const QColor kColors[4] = {
        QColor(0x7C, 0x86, 0xF0),   // 靛蓝
        QColor(0x4E, 0xC9, 0xB0),   // 青绿
        QColor(0xDC, 0xDC, 0xAA),   // 琥珀
        QColor(0xC5, 0x86, 0xC0),   // 紫
    };

    QList<WaveSeries> list = collectScopeSeries();

    // 通道列表为空时，回退成"预览当前选中行"，随手看一个值不用先右键
    if (list.isEmpty() && m_poll && m_pollTable) {
        const QModelIndex idx = m_pollTable->currentIndex();
        PollTask *t = idx.isValid() ? m_poll->taskAt(idx.row()) : nullptr;
        if (t && !t->history.isEmpty()) {
            WaveSeries w;
            w.name = QStringLiteral("%1 · %2").arg(t->name).arg(t->addr, 4, 10, QChar('0'));
            w.unit = t->unit;
            w.color = kColors[0];
            w.values.reserve(t->history.size());
            w.times.reserve(t->history.size());
            for (const WaveSample &smp : t->history) {
                w.values.append(smp.value);
                w.times.append(smp.tMs);
            }
            list.append(w);
        }
    }

    const QString emptyHint =
        QStringLiteral("右键点表任意一行 → 添加到示波器（最多 4 条曲线）");

    if (list.isEmpty())
        m_wave->clear(emptyHint);
    else
        m_wave->setSeries(list);

    // 独立窗口存在且可见时同步刷新
    if (m_waveFloat && m_scopeWin && m_scopeWin->isVisible()) {
        if (list.isEmpty())
            m_waveFloat->clear(emptyHint);
        else
            m_waveFloat->setSeries(list);
    }
}

QList<WaveSeries> MainWindow::collectScopeSeries()
{
    static const QColor kColors[4] = {
        QColor(0x7C, 0x86, 0xF0),
        QColor(0x4E, 0xC9, 0xB0),
        QColor(0xDC, 0xDC, 0xAA),
        QColor(0xC5, 0x86, 0xC0),
    };

    QList<WaveSeries> out;
    if (!m_poll)
        return out;

    for (int i = 0; i < m_scopeRows.size(); ++i) {
        PollTask *t = m_poll->taskAt(m_scopeRows.at(i));
        if (!t)
            continue;

        WaveSeries w;
        w.name = QStringLiteral("%1 · %2").arg(t->name).arg(t->addr, 4, 10, QChar('0'));
        w.unit = t->unit;
        w.color = kColors[out.size() % 4];
        w.values.reserve(t->history.size());
        w.times.reserve(t->history.size());
        for (const WaveSample &smp : t->history) {
            w.values.append(smp.value);
            w.times.append(smp.tMs);
        }
        out.append(w);
    }
    return out;
}

void MainWindow::addRowToScope(int row)
{
    if (!m_poll || row < 0 || row >= m_poll->taskCount())
        return;

    if (m_scopeRows.contains(row)) {
        statusBar()->showMessage(QStringLiteral("该行已经在示波器里了"), 2500);
        return;
    }
    if (m_scopeRows.size() >= 4) {
        statusBar()->showMessage(QStringLiteral("示波器最多 4 条曲线，先移除一条"), 3500);
        return;
    }

    m_scopeRows.append(row);
    refreshWave();

    const PollTask *t = m_poll->taskAt(row);
    if (t)
        statusBar()->showMessage(QStringLiteral("已加入示波器：%1").arg(t->name), 2500);
}

void MainWindow::removeRowFromScope(int row)
{
    if (!m_scopeRows.removeOne(row))
        return;
    refreshWave();
    statusBar()->showMessage(QStringLiteral("已从示波器移除"), 2500);
}

void MainWindow::onPollContextMenu(const QPoint &pos)
{
    if (!m_pollTable)
        return;

    const QModelIndex idx = m_pollTable->indexAt(pos);
    if (!idx.isValid())
        return;

    const int row = idx.row();

    QMenu menu(this);
    QAction *actAdd = menu.addAction(QStringLiteral("添加到示波器"));
    QAction *actDel = menu.addAction(QStringLiteral("从示波器移除"));
    menu.addSeparator();
    QAction *actWin   = menu.addAction(QStringLiteral("打开独立示波器窗口"));
    QAction *actClear = menu.addAction(QStringLiteral("清空示波器"));
    menu.addSeparator();
    QAction *actPng   = menu.addAction(QStringLiteral("波形存为图片 (PNG)"));
    QAction *actCsv   = menu.addAction(QStringLiteral("导出波形数据 (CSV)"));
    menu.addSeparator();
    QAction *actWrite = menu.addAction(QStringLiteral("写此寄存器…"));

    actAdd->setEnabled(!m_scopeRows.contains(row) && m_scopeRows.size() < 4);
    actDel->setEnabled(m_scopeRows.contains(row));
    actClear->setEnabled(!m_scopeRows.isEmpty());
    actPng->setEnabled(!m_scopeRows.isEmpty());
    actCsv->setEnabled(!m_scopeRows.isEmpty());

    QAction *chosen = menu.exec(m_pollTable->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == actAdd) {
        addRowToScope(row);
    } else if (chosen == actDel) {
        removeRowFromScope(row);
    } else if (chosen == actWin) {
        toggleScopeWindow();
    } else if (chosen == actClear) {
        m_scopeRows.clear();
        refreshWave();
        statusBar()->showMessage(QStringLiteral("示波器已清空"), 2500);
    } else if (chosen == actPng) {
        exportScopeImage();
    } else if (chosen == actCsv) {
        exportScopeData(false);
    } else if (chosen == actWrite) {
        m_pollTable->setCurrentIndex(idx);
        writeSelectedPoint();
    }
}

void MainWindow::toggleScopeWindow()
{
    if (!m_scopeWin) {
        m_scopeWin = new QDialog(this);
        m_scopeWin->setWindowTitle(QStringLiteral("BusScope · 示波器"));
        m_scopeWin->resize(980, 520);
        m_scopeWin->setAttribute(Qt::WA_DeleteOnClose, false);

        m_scopeWin->setStyleSheet(QStringLiteral("QDialog{background:#0F1013;}"));

        auto *l = new QVBoxLayout(m_scopeWin);
        l->setContentsMargins(10, 10, 10, 10);
        m_waveFloat = new WaveChart;
        connect(m_waveFloat, &WaveChart::followChanged, this, &MainWindow::updateFollowUi);
        l->addWidget(m_waveFloat);

#ifdef Q_OS_WIN
        // 对话框也要深色标题栏，否则一块白条特别跳
        HWND hwnd = reinterpret_cast<HWND>(m_scopeWin->winId());
        BOOL dark = TRUE;
        if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark))))
            DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
#endif
    }

    const bool show = !m_scopeWin->isVisible();
    if (show) {
        m_scopeWin->show();
        m_scopeWin->raise();
        m_scopeWin->activateWindow();
    } else {
        m_scopeWin->hide();
    }
    if (m_scopeBtn)
        m_scopeBtn->setText(show ? QStringLiteral("隐藏示波器") : QStringLiteral("示波器"));

    refreshWave();
}

// ---------------------------------------------------------------- CAN 页

QWidget *MainWindow::buildCanPage()
{
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto *bar = new QWidget;
    bar->setFixedHeight(40);
    bar->setStyleSheet(QStringLiteral("background:#101113;border-bottom:1px solid #1D1E22;"));
    auto *bh = new QHBoxLayout(bar);
    bh->setContentsMargins(12, 0, 12, 0);
    bh->setSpacing(8);

    m_canTypeBox = new QComboBox;
    m_canTypeBox->addItem(QStringLiteral("USB-CAN II (DevType 4)"), QVariant(4u));
    m_canTypeBox->addItem(QStringLiteral("USB-CAN I (DevType 3)"), QVariant(3u));
    m_canTypeBox->addItem(QStringLiteral("CANalyst-II (DevType 21)"), QVariant(21u));
    m_canTypeBox->setMinimumWidth(180);

    m_canChannelBox = new QComboBox;
    m_canChannelBox->addItem(QStringLiteral("通道 0"), QVariant(0u));
    m_canChannelBox->addItem(QStringLiteral("通道 1"), QVariant(1u));
    m_canChannelBox->setMinimumWidth(80);

    m_canBaudBox = new QComboBox;
    const QStringList baudLabels = CanLink::bitrateLabels();
    for (int i = 0; i < baudLabels.size(); ++i)
        m_canBaudBox->addItem(baudLabels.at(i), CanLink::bitrateAt(i));
    m_canBaudBox->setCurrentIndex(2);   // 500 kbit/s
    m_canBaudBox->setMinimumWidth(112);

    bh->addWidget(m_canTypeBox);
    bh->addWidget(m_canChannelBox);
    bh->addWidget(m_canBaudBox);

    m_canBtn = new QPushButton(QStringLiteral("打开 CAN"));
    m_canBtn->setObjectName(QStringLiteral("Primary"));
    m_canBtn->setCursor(Qt::PointingHandCursor);
    applyIcon(m_canBtn, icons::power(Qt::white));
    connect(m_canBtn, &QPushButton::clicked, this, &MainWindow::toggleCan);
    bh->addWidget(m_canBtn);

    auto *clearBtn = new QPushButton(QStringLiteral("清空"));
    clearBtn->setObjectName(QStringLiteral("Ghost"));
    clearBtn->setCursor(Qt::PointingHandCursor);
    applyIcon(clearBtn, icons::trash(theme::textSecondary()));
    connect(clearBtn, &QPushButton::clicked, this, &MainWindow::clearCanFrames);
    bh->addWidget(clearBtn);

    m_dbcBtn = new QPushButton(QStringLiteral("载入 DBC"));
    m_dbcBtn->setObjectName(QStringLiteral("Ghost"));
    m_dbcBtn->setCursor(Qt::PointingHandCursor);
    m_dbcBtn->setToolTip(QStringLiteral("载入 .dbc 后，帧表右侧会把原始字节解成信号值"));
    applyIcon(m_dbcBtn, icons::importTray(theme::textSecondary()));
    connect(m_dbcBtn, &QPushButton::clicked, this, &MainWindow::loadDbc);
    bh->addWidget(m_dbcBtn);

    bh->addStretch(1);

    m_canStatus = new QLabel(QStringLiteral("未打开"));
    m_canStatus->setObjectName(QStringLiteral("Hint"));
    bh->addWidget(m_canStatus);

    m_canCount = new QLabel(QStringLiteral("RX 0 · TX 0"));
    m_canCount->setObjectName(QStringLiteral("Hint"));
    bh->addWidget(m_canCount);
    v->addWidget(bar);

    m_canTable = new QTableView;
    m_canTable->setModel(m_canModel);
    m_canTable->setAlternatingRowColors(true);
    m_canTable->setShowGrid(false);
    m_canTable->setWordWrap(false);
    m_canTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_canTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_canTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_canTable->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_canTable->verticalHeader()->setVisible(false);
    m_canTable->verticalHeader()->setDefaultSectionSize(26);
    m_canTable->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);

    auto *chh = m_canTable->horizontalHeader();
    chh->setHighlightSections(false);
    chh->setSectionResizeMode(QHeaderView::Fixed);
    chh->resizeSection(CanFrameModel::ColTime, 104);
    chh->resizeSection(CanFrameModel::ColDir, 60);
    chh->resizeSection(CanFrameModel::ColId, 104);
    chh->resizeSection(CanFrameModel::ColType, 92);
    chh->resizeSection(CanFrameModel::ColDlc, 60);
    chh->resizeSection(CanFrameModel::ColData, 236);
    chh->resizeSection(CanFrameModel::ColDecoded, 260);
    chh->setSectionResizeMode(CanFrameModel::ColDecoded, QHeaderView::Stretch);
    v->addWidget(m_canTable, 1);

    auto *sendBar = new QWidget;
    sendBar->setFixedHeight(48);
    sendBar->setStyleSheet(QStringLiteral("background:#101113;border-top:1px solid #1D1E22;"));
    auto *sh = new QHBoxLayout(sendBar);
    sh->setContentsMargins(12, 0, 12, 0);
    sh->setSpacing(8);

    auto *idLabel = new QLabel(QStringLiteral("ID"));
    idLabel->setObjectName(QStringLiteral("FieldLabel"));
    m_canIdEdit = new QLineEdit(QStringLiteral("0x301"));
    m_canIdEdit->setFixedWidth(100);

    m_canExtBox = new QCheckBox(QStringLiteral("扩展帧"));
    m_canRemoteBox = new QCheckBox(QStringLiteral("远程帧"));

    auto *dataLabel = new QLabel(QStringLiteral("数据"));
    dataLabel->setObjectName(QStringLiteral("FieldLabel"));
    m_canDataEdit = new QLineEdit(QStringLiteral("11 22 33 44 55 66 77 88"));
    m_canDataEdit->setPlaceholderText(QStringLiteral("最多 8 字节，空格分隔"));

    auto *sendBtn = new QPushButton(QStringLiteral("发送"));
    sendBtn->setObjectName(QStringLiteral("Primary"));
    sendBtn->setCursor(Qt::PointingHandCursor);
    applyIcon(sendBtn, icons::send(Qt::white));
    connect(sendBtn, &QPushButton::clicked, this, &MainWindow::sendCan);

    sh->addWidget(idLabel);
    sh->addWidget(m_canIdEdit);
    sh->addWidget(m_canExtBox);
    sh->addWidget(m_canRemoteBox);
    sh->addWidget(dataLabel);
    sh->addWidget(m_canDataEdit, 1);
    sh->addWidget(sendBtn);
    v->addWidget(sendBar);

    return page;
}

// ---------------------------------------------------------------- 帧详情

QWidget *MainWindow::buildDetailPane()
{
    auto *w = new QWidget;
    w->setMinimumHeight(190);
    w->setStyleSheet(QStringLiteral("background:#101113;border-top:1px solid #1D1E22;"));

    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(12, 10, 12, 10);
    h->setSpacing(20);

    // 字段解析
    auto *colA = new QWidget;
    colA->setFixedWidth(270);
    auto *va = new QVBoxLayout(colA);
    va->setContentsMargins(0, 0, 0, 0);
    va->setSpacing(6);

    m_detailTitle = new QLabel(QStringLiteral("帧详情"));
    m_detailTitle->setObjectName(QStringLiteral("SectionTitle"));
    va->addWidget(m_detailTitle);

    m_fieldView = new QTextEdit;
    m_fieldView->setReadOnly(true);
    m_fieldView->setFrameShape(QFrame::NoFrame);
    m_fieldView->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
    va->addWidget(m_fieldView, 1);
    h->addWidget(colA);

    // 原始字节
    auto *colB = new QWidget;
    auto *vb = new QVBoxLayout(colB);
    vb->setContentsMargins(0, 0, 0, 0);
    vb->setSpacing(6);

    auto *hexTitle = new QLabel(QStringLiteral("原始字节"));
    hexTitle->setObjectName(QStringLiteral("SectionTitle"));
    vb->addWidget(hexTitle);

    m_hexView = new QTextEdit;
    m_hexView->setReadOnly(true);
    m_hexView->setFrameShape(QFrame::NoFrame);
    m_hexView->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
    vb->addWidget(m_hexView, 1);
    h->addWidget(colB, 1);

    // 时序
    auto *colC = new QWidget;
    colC->setFixedWidth(190);
    auto *vc = new QVBoxLayout(colC);
    vc->setContentsMargins(0, 0, 0, 0);
    vc->setSpacing(4);

    auto *timingTitle = new QLabel(QStringLiteral("时序"));
    timingTitle->setObjectName(QStringLiteral("SectionTitle"));
    vc->addWidget(timingTitle);

    m_timingValue = new QLabel(QStringLiteral("--"));
    m_timingValue->setStyleSheet(QStringLiteral("font-family:Consolas;font-size:20px;color:#E8E8EA;"));
    vc->addWidget(m_timingValue);

    m_timingCaption = new QLabel(QStringLiteral("尚未选中帧"));
    m_timingCaption->setObjectName(QStringLiteral("Hint"));
    vc->addWidget(m_timingCaption);

    m_timingMetrics = new QLabel;
    m_timingMetrics->setObjectName(QStringLiteral("Hint"));
    m_timingMetrics->setWordWrap(true);
    vc->addWidget(m_timingMetrics);
    vc->addStretch(1);
    h->addWidget(colC);

    return w;
}

// ---------------------------------------------------------------- 右侧面板

QWidget *MainWindow::buildRightPanel()
{
    auto *w = new QWidget;
    w->setObjectName(QStringLiteral("RightPanel"));
    w->setMinimumWidth(268);

    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(12, 12, 12, 12);
    v->setSpacing(10);

    v->addWidget(sectionTitle(QStringLiteral("发送")));

    m_formatBox = new QComboBox;
    m_formatBox->addItem(QStringLiteral("HEX"));
    m_formatBox->addItem(QStringLiteral("ASCII"));
    v->addWidget(fieldRow(QStringLiteral("格式"), m_formatBox));

    m_sendEdit = new QPlainTextEdit;
    m_sendEdit->setPlaceholderText(QStringLiteral("01 03 00 00 00 02"));
    m_sendEdit->setFixedHeight(84);
    v->addWidget(m_sendEdit);

    m_autoCrc = new QCheckBox(QStringLiteral("自动追加 CRC16 (Modbus RTU)"));
    m_autoCrc->setChecked(true);
    v->addWidget(m_autoCrc);

    m_sendBtn = new QPushButton(QStringLiteral("发送   Ctrl + Enter"));
    m_sendBtn->setObjectName(QStringLiteral("Primary"));
    m_sendBtn->setMinimumHeight(32);
    m_sendBtn->setCursor(Qt::PointingHandCursor);
    applyIcon(m_sendBtn, icons::send(Qt::white));
    connect(m_sendBtn, &QPushButton::clicked, this, &MainWindow::sendManual);
    v->addWidget(m_sendBtn);

    // 周期发送：逻辑照搬原型的 sendTimer（QTimer + 间隔输入 + 勾选启停）
    auto *periodRow = new QWidget;
    auto *periodLayout = new QHBoxLayout(periodRow);
    periodLayout->setContentsMargins(0, 0, 0, 0);
    periodLayout->setSpacing(8);

    m_periodic = new QCheckBox(QStringLiteral("周期发送"));
    m_interval = new QSpinBox;
    m_interval->setRange(20, 60000);
    m_interval->setValue(200);
    m_interval->setSuffix(QStringLiteral(" ms"));
    periodLayout->addWidget(m_periodic);
    periodLayout->addWidget(m_interval, 1);
    v->addWidget(periodRow);

    v->addWidget(sectionTitle(QStringLiteral("常用指令")));

    auto addQuick = [this, v](const QString &label, const QString &hex) {
        auto *btn = new QPushButton(label);
        btn->setObjectName(QStringLiteral("Ghost"));
        btn->setCursor(Qt::PointingHandCursor);
        connect(btn, &QPushButton::clicked, this, [this, hex]() {
            m_sendEdit->setPlainText(hex);
            m_autoCrc->setChecked(true);
            sendManual();
        });
        v->addWidget(btn);
    };

    addQuick(QStringLiteral("读保持寄存器 40001~40002"), QStringLiteral("01 03 00 00 00 02"));
    addQuick(QStringLiteral("读输入寄存器 30001~30004"), QStringLiteral("01 04 00 00 00 04"));
    addQuick(QStringLiteral("读设备标识 (0x2B / MEI)"), QStringLiteral("01 2B 0E 01 00"));

    v->addStretch(1);

    auto *notice = new QLabel(QStringLiteral("HEX 输入可用空格 / 逗号分隔，\nCRC 由工具按小端序追加。"));
    notice->setObjectName(QStringLiteral("Hint"));
    notice->setWordWrap(true);
    v->addWidget(notice);

    return w;
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

// ---------------------------------------------------------------- 槽函数

void MainWindow::refreshPorts()
{
    const QString current = m_portBox->currentData().toString();
    m_portBox->clear();

    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : ports) {
        QString desc = info.description();
        // 描述过长会撑爆下拉框（而且下拉里看不全），截到 16 个字符
        if (desc.size() > 16)
            desc = desc.left(15) + QChar(0x2026);
        QString label = info.portName();
        if (!desc.isEmpty())
            label += QStringLiteral("  ") + desc;
        m_portBox->addItem(label, info.portName());
        m_portBox->setItemData(m_portBox->count() - 1,
                               info.portName() + QStringLiteral("  ") + info.description(),
                               Qt::ToolTipRole);
    }
    if (m_portBox->count() == 0)
        m_portBox->addItem(QStringLiteral("无可用串口"), QString());

    int index = m_portBox->findData(current);
    if (index < 0) {
        // 没选过就优先挑 USB 转串口：COM1 是主板自带的，日常用不上
        for (int i = 0; i < m_portBox->count(); ++i) {
            const QString tip = m_portBox->itemData(i, Qt::ToolTipRole).toString();
            if (tip.contains(QStringLiteral("USB"), Qt::CaseInsensitive)) {
                index = i;
                break;
            }
        }
    }
    if (index >= 0)
        m_portBox->setCurrentIndex(index);
}

void MainWindow::toggleConnection()
{
    if (m_link->isOpen()) {
        m_link->close();
        return;
    }

    PortConfig cfg;
    cfg.portName = m_portBox->currentData().toString();
    if (cfg.portName.isEmpty())
        cfg.portName = m_portBox->currentText();

    bool ok = false;
    const int baud = m_baudBox->currentText().toInt(&ok);
    if (ok)
        cfg.baudRate = baud;

    const QVariantList frame = m_frameBox->currentData().toList();
    if (frame.size() == 3) {
        cfg.dataBits = frame.at(0).toInt();
        cfg.stopBits = frame.at(1).toInt();
        cfg.parity = frame.at(2).toInt();
    }

    if (!m_link->open(cfg))
        refreshPorts();
}

void MainWindow::setConnectionUi(bool open)
{
    m_connDot->setStyleSheet(open
                                 ? QStringLiteral("background:#4EC9B0;border-radius:3px;")
                                 : QStringLiteral("background:#F85149;border-radius:3px;"));
    m_connText->setText(open ? QStringLiteral("已连接") : QStringLiteral("未连接"));
    m_connText->setObjectName(open ? QStringLiteral("StatusOk") : QStringLiteral("StatusBad"));
    m_connText->style()->unpolish(m_connText);
    m_connText->style()->polish(m_connText);

    m_connectBtn->setText(open ? QStringLiteral("断开串口") : QStringLiteral("打开串口"));
    m_portBox->setEnabled(!open);
    m_baudBox->setEnabled(!open);
    m_frameBox->setEnabled(!open);

    if (open) {
        const PortConfig cfg = m_link->config();
        m_statusConn->setText(QStringLiteral("已连接 %1 · %2 8N1 · 超时 %3 ms")
                                  .arg(cfg.portName)
                                  .arg(cfg.baudRate)
                                  .arg(m_link->responseTimeout()));
    } else {
        m_statusConn->setText(QStringLiteral("未连接"));
    }
}

void MainWindow::onLinkStateChanged(bool open)
{
    setConnectionUi(open);
    if (!open && m_periodic && m_periodic->isChecked())
        m_periodic->setChecked(false);

    // 连上就记住这次用的口，下次启动自动重连
    if (open) {
        const PortConfig cfg = m_link->config();
        QSettings st;
        st.setValue(QStringLiteral("conn/port"), cfg.portName);
        st.setValue(QStringLiteral("conn/baud"), cfg.baudRate);
    }
}

void MainWindow::onFrameReady(const busscope::Frame &frame)
{
    m_model->append(frame);
    if (m_emptyHint)
        m_emptyHint->setVisible(false);
    if (m_autoScroll->isChecked())
        m_table->scrollToBottom();
    m_frameCount->setText(QStringLiteral("%1 帧").arg(m_model->rowCount()));
}

void MainWindow::onCountersChanged(qint64 rxBytes, qint64 txBytes)
{
    m_rxBytes = rxBytes;
    m_txBytes = txBytes;
    m_statusCounters->setText(QStringLiteral("RX %1   TX %2   错误 %3")
                                  .arg(humanBytes(m_rxBytes), humanBytes(m_txBytes))
                                  .arg(0));
}

void MainWindow::onLinkError(const QString &text)
{
    busscope::Frame f;
    f.timestampMs = QDateTime::currentMSecsSinceEpoch();
    f.dir = Direction::Note;
    f.kind = FrameKind::CrcError;
    f.typeName = QStringLiteral("提示");
    f.summary = text;
    f.detail = QStringLiteral("说明|%1").arg(text);
    m_model->append(f);

    statusBar()->showMessage(text, 4000);
    m_frameCount->setText(QStringLiteral("%1 帧").arg(m_model->rowCount()));
}

void MainWindow::sendManual()
{
    if (!m_link->isOpen()) {
        onLinkError(QStringLiteral("请先打开串口"));
        return;
    }

    // 覆盖正在等待响应的事务会污染切帧上下文，请求-响应配对从此错乱
    if (m_link->isBusy()) {
        onLinkError(QStringLiteral("上一条请求还在等响应，稍候再发（或缩短超时）"));
        return;
    }
    if (m_poll->isRunning()) {
        onLinkError(QStringLiteral("轮询进行中，请先在「点表 · 轮询」页停止轮询再手动发送"));
        return;
    }

    bool ok = false;
    const QByteArray payload = parsePayload(&ok);
    if (!ok || payload.isEmpty()) {
        onLinkError(QStringLiteral("发送内容不是合法的 %1 数据")
                        .arg(m_formatBox->currentIndex() == 0 ? QStringLiteral("HEX")
                                                              : QStringLiteral("ASCII")));
        return;
    }

    const bool isHex = (m_formatBox->currentIndex() == 0);
    m_link->send(payload, isHex && m_autoCrc->isChecked());
}

void MainWindow::sendPeriodic()
{
    // 周期触发时链路忙就让过这一轮：提示帧会以帧记录形式刷屏，只在状态栏轻提示
    if (!m_link->isOpen() || m_link->isBusy() || m_poll->isRunning())
        return;

    bool ok = false;
    const QByteArray payload = parsePayload(&ok);
    if (!ok || payload.isEmpty())
        return;

    const bool isHex = (m_formatBox->currentIndex() == 0);
    m_link->send(payload, isHex && m_autoCrc->isChecked());
}

QByteArray MainWindow::parsePayload(bool *ok) const
{
    const QString text = m_sendEdit->toPlainText();

    if (m_formatBox->currentIndex() != 0) {
        *ok = !text.trimmed().isEmpty();
        return text.toUtf8();
    }

    QString hex;
    hex.reserve(text.size());
    for (const QChar c : text) {
        if (c.isSpace() || c == QLatin1Char(',') || c == QLatin1Char('-') || c == QLatin1Char(':'))
            continue;
        if (!std::isxdigit(static_cast<unsigned char>(c.toLatin1()))) {
            *ok = false;
            return QByteArray();
        }
        hex.append(c);
    }

    if (hex.isEmpty() || hex.size() % 2 != 0) {
        *ok = false;
        return QByteArray();
    }

    *ok = true;
    return QByteArray::fromHex(hex.toLatin1());
}

void MainWindow::clearLog()
{
    m_model->clear();
    m_frameCount->setText(QStringLiteral("0 帧"));
    if (m_emptyHint)
        m_emptyHint->setVisible(true);
    m_fieldView->clear();
    m_hexView->clear();
    m_detailTitle->setText(QStringLiteral("帧详情"));
    m_timingValue->setText(QStringLiteral("--"));
    m_timingCaption->setText(QStringLiteral("尚未选中帧"));
    m_timingMetrics->clear();
}

void MainWindow::exportLog()
{
    const int rows = m_model->rowCount();
    if (rows == 0) {
        statusBar()->showMessage(QStringLiteral("当前没有可导出的帧"), 3000);
        return;
    }

    const QString suggested = QStringLiteral("busscope-%1.csv")
                                  .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    QString selectedFilter;
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出帧记录"), suggested,
        QStringLiteral("CSV 文件 (*.csv);;文本文件 (*.txt)"), &selectedFilter);
    if (path.isEmpty())
        return;

    const bool asText = path.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive)
                        || selectedFilter.startsWith(QStringLiteral("文本文件"));

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        onLinkError(QStringLiteral("导出失败：%1").arg(file.errorString()));
        return;
    }
    file.write("\xEF\xBB\xBF", 3); // UTF-8 BOM，Excel / 记事本直接打开不乱码

    QTextStream out(&file);
    out.setCodec("UTF-8");

    if (asText) {
        // 制表符分隔的纯文本：记事本直接看，粘到 Excel/剪贴板也能按列对齐
        out << QStringLiteral("BusScope 帧记录\n")
            << QStringLiteral("导出时间\t%1\n")
                   .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
            << QStringLiteral("共 %1 条\n\n").arg(rows)
            << QStringLiteral("序号\t时间\t方向\t从站\t功能码\t类型\t数据(HEX)\t解析\n");

        for (int row = 0; row < rows; ++row) {
            const busscope::Frame *f = m_model->frameAt(row);
            if (!f)
                continue;

            const QString dir = (f->dir == Direction::Tx) ? QStringLiteral("TX")
                                : (f->dir == Direction::Rx ? QStringLiteral("RX") : QStringLiteral("--"));
            QString summary = f->summary;
            if (f->elapsedMs >= 0)
                summary += QStringLiteral(" (%1 ms)").arg(f->elapsedMs);

            out << f->index << QLatin1Char('\t')
                << QDateTime::fromMSecsSinceEpoch(f->timestampMs).toString(QStringLiteral("HH:mm:ss.zzz")) << QLatin1Char('\t')
                << dir << QLatin1Char('\t')
                << QString::number(f->slave) << QLatin1Char('\t')
                << QStringLiteral("0x%1").arg(f->function, 2, 16, QChar('0')).toUpper() << QLatin1Char('\t')
                << f->typeName << QLatin1Char('\t')
                << QString::fromLatin1(f->raw.toHex(' ').toUpper()) << QLatin1Char('\t')
                << summary << QLatin1Char('\n');
        }
    } else {
        out << QStringLiteral("序号,时间,方向,从站,功能码,类型,数据(HEX),解析\n");

        for (int row = 0; row < rows; ++row) {
            const busscope::Frame *f = m_model->frameAt(row);
            if (!f)
                continue;

            const QString dir = (f->dir == Direction::Tx) ? QStringLiteral("TX")
                                : (f->dir == Direction::Rx ? QStringLiteral("RX") : QStringLiteral("--"));
            QString summary = f->summary;
            if (f->elapsedMs >= 0)
                summary += QStringLiteral(" (%1 ms)").arg(f->elapsedMs);
            summary.replace(QLatin1Char('"'), QStringLiteral("\"\""));

            out << f->index << QLatin1Char(',')
                << QDateTime::fromMSecsSinceEpoch(f->timestampMs).toString(QStringLiteral("HH:mm:ss.zzz")) << QLatin1Char(',')
                << dir << QLatin1Char(',')
                << QString::number(f->slave) << QLatin1Char(',')
                << QString::number(f->function) << QLatin1Char(',')
                << f->typeName << QLatin1Char(',')
                << QString::fromLatin1(f->raw.toHex(' ').toUpper()) << QLatin1Char(',')
                << QLatin1Char('"') << summary << QLatin1Char('"') << QLatin1Char('\n');
        }
    }
    file.close();

    statusBar()->showMessage(
        QStringLiteral("已导出 %1 条记录（%2）").arg(rows).arg(asText ? QStringLiteral("TXT") : QStringLiteral("CSV")),
        5000);
}

void MainWindow::importPoints()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("导入点表"), QString(),
        QStringLiteral("点表文件 (*.csv *.txt);;CSV 文件 (*.csv);;文本文件 (*.txt);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    openPointFile(path);
}

bool MainWindow::openPointFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        onLinkError(QStringLiteral("导入失败：%1").arg(file.errorString()));
        return false;
    }

    QTextStream in(&file);
    in.setCodec("UTF-8");

    QVector<PollTask> tasks;
    int lineNo = 0;
    bool headerSkipped = false;

    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        ++lineNo;
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        // CSV / TXT 都支持：按行自动识别分隔符（Tab / 逗号 / 分号 / 连续空白）
        QStringList cols;
        if (line.contains(QLatin1Char('\t')))
            cols = line.split(QLatin1Char('\t'));
        else if (line.contains(QLatin1Char(',')))
            cols = line.split(QLatin1Char(','));
        else if (line.contains(QLatin1Char(';')))
            cols = line.split(QLatin1Char(';'));
        else
            cols = line.split(QRegExp(QStringLiteral("\\s+")), QString::SkipEmptyParts);
        if (!headerSkipped && lineNo == 1) {
            headerSkipped = true;
            if (cols.value(0).contains(QStringLiteral("名称"))
                || cols.value(0).compare(QStringLiteral("name"), Qt::CaseInsensitive) == 0)
                continue;
        }
        if (cols.size() < 4)
            continue;

        PollTask t;
        t.name = cols.value(0).trimmed();
        t.slave = static_cast<quint8>(cols.value(1).toUInt());
        t.function = static_cast<quint8>(cols.value(2).toUInt());
        t.addr = static_cast<quint16>(cols.value(3).toUInt());
        t.count = static_cast<quint16>(cols.value(4).toUInt());
        t.periodMs = cols.value(5).toInt();
        t.timeoutMs = cols.value(6).toInt();
        t.retries = cols.value(7).toInt();
        t.type = valueTypeFromName(cols.value(8));
        t.scale = cols.value(9).isEmpty() ? 1.0 : cols.value(9).toDouble();
        t.unit = cols.value(10).trimmed();

        if (t.name.isEmpty())
            t.name = QStringLiteral("点 %1").arg(tasks.size() + 1);
        if (t.slave == 0)
            t.slave = 1;
        if (t.function < 0x01 || t.function > 0x04)
            t.function = 0x03;
        if (t.count == 0)
            t.count = static_cast<quint16>(qMax(1, registersForType(t.type)));
        if (t.periodMs < 20)
            t.periodMs = 500;
        if (t.timeoutMs < 20)
            t.timeoutMs = 200;
        if (t.retries < 0)
            t.retries = 0;

        tasks.append(t);
    }
    file.close();

    if (tasks.isEmpty()) {
        onLinkError(QStringLiteral("CSV 里没有解析出有效行"));
        return false;
    }

    m_poll->stop();
    m_poll->setTasks(tasks);
    m_pollModel->notifyAll();
    m_scopeRows.clear();      // 行号已失效，示波器一起清掉
    refreshWave();
    onPollSummary();

    if (m_tabs)
        m_tabs->setCurrentIndex(1);

    // 默认选中第一行：波形区不至于空着
    if (m_pollTable && m_pollModel->rowCount() > 0)
        m_pollTable->setCurrentIndex(m_pollModel->index(0, 0));

    {
        QSettings st;
        st.setValue(QStringLiteral("poll/lastFile"), path);
    }

    statusBar()->showMessage(QStringLiteral("已导入 %1 项轮询任务").arg(tasks.size()), 4000);
    return true;
}

void MainWindow::exportScopeImage()
{
    const QList<WaveSeries> list = collectScopeSeries();
    if (list.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("示波器里还没有曲线"), 3000);
        return;
    }

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存波形图片"),
        QStringLiteral("busscope-wave-%1.png").arg(stamp),
        QStringLiteral("PNG 图片 (*.png)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive))
        path += QStringLiteral(".png");

    // 独立窗口开着就抓它（画面更大），否则抓嵌入区
    QWidget *src = (m_scopeWin && m_scopeWin->isVisible() && m_waveFloat)
                       ? static_cast<QWidget *>(m_waveFloat)
                       : static_cast<QWidget *>(m_wave);
    if (!src || !src->grab().save(path)) {
        onLinkError(QStringLiteral("保存图片失败"));
        return;
    }
    statusBar()->showMessage(QStringLiteral("波形图片已保存"), 4000);
}

void MainWindow::exportScopeData(bool asText)
{
    const QList<WaveSeries> list = collectScopeSeries();
    if (list.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("示波器里还没有数据"), 3000);
        return;
    }

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出波形数据"),
        QStringLiteral("busscope-wave-%1.%2").arg(stamp, asText ? QStringLiteral("txt")
                                                               : QStringLiteral("csv")),
        asText ? QStringLiteral("文本文件 (*.txt)") : QStringLiteral("CSV 文件 (*.csv)"));
    if (path.isEmpty())
        return;
    if (asText && !path.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive))
        path += QStringLiteral(".txt");
    if (!asText && !path.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive))
        path += QStringLiteral(".csv");

    const QChar sep = asText ? QLatin1Char('\t') : QLatin1Char(',');

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        onLinkError(QStringLiteral("导出失败：%1").arg(file.errorString()));
        return;
    }
    file.write("\xEF\xBB\xBF", 3);

    QTextStream out(&file);
    out.setCodec("UTF-8");
    out << QStringLiteral("时间") << sep << QStringLiteral("通道") << sep
        << QStringLiteral("数值") << sep << QStringLiteral("单位") << QLatin1Char('\n');

    int rows = 0;
    for (const WaveSeries &w : list) {
        for (int i = 0; i < w.values.size(); ++i) {
            const qint64 t = (i < w.times.size()) ? w.times.at(i) : 0;
            const QString ts = (t > 0)
                ? QDateTime::fromMSecsSinceEpoch(t).toString(QStringLiteral("HH:mm:ss.zzz"))
                : QString::number(i);
            out << ts << sep << w.name << sep
                << QString::number(w.values.at(i), 'f', 4) << sep
                << w.unit << QLatin1Char('\n');
            ++rows;
        }
    }
    file.close();

    statusBar()->showMessage(
        QStringLiteral("已导出 %1 行波形数据（%2）").arg(rows)
            .arg(asText ? QStringLiteral("TXT") : QStringLiteral("CSV")),
        4000);
}

void MainWindow::exportPointsAs(bool asText)
{
    if (m_poll->taskCount() == 0) {
        statusBar()->showMessage(QStringLiteral("点表为空"), 3000);
        return;
    }

    QString path = QFileDialog::getSaveFileName(
        this,
        asText ? QStringLiteral("导出为 TXT") : QStringLiteral("导出为 CSV"),
        asText ? QStringLiteral("busscope-points.txt") : QStringLiteral("busscope-points.csv"),
        asText ? QStringLiteral("文本文件 (*.txt)") : QStringLiteral("CSV 文件 (*.csv)"));

    if (path.isEmpty())
        return;

    // 用户没敲后缀时补上，避免导出成无扩展名文件
    if (asText && !path.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive))
        path += QStringLiteral(".txt");
    if (!asText && !path.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive))
        path += QStringLiteral(".csv");

    const QChar sep = asText ? QLatin1Char('\t') : QLatin1Char(',');

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        onLinkError(QStringLiteral("导出失败：%1").arg(file.errorString()));
        return;
    }
    file.write("\xEF\xBB\xBF", 3);

    QTextStream out(&file);
    out.setCodec("UTF-8");
    out << QStringLiteral("名称") << sep << QStringLiteral("从站") << sep
        << QStringLiteral("功能码") << sep << QStringLiteral("地址") << sep
        << QStringLiteral("数量") << sep << QStringLiteral("周期ms") << sep
        << QStringLiteral("超时ms") << sep << QStringLiteral("重试") << sep
        << QStringLiteral("类型") << sep << QStringLiteral("系数") << sep
        << QStringLiteral("单位") << QLatin1Char('\n');

    for (int i = 0; i < m_poll->taskCount(); ++i) {
        const PollTask *t = m_poll->taskAt(i);
        if (!t)
            continue;
        out << t->name << sep
            << t->slave << sep
            << t->function << sep
            << t->addr << sep
            << t->count << sep
            << t->periodMs << sep
            << t->timeoutMs << sep
            << t->retries << sep
            << valueTypeName(t->type) << sep
            << QString::number(t->scale, 'f', 4) << sep
            << t->unit << QLatin1Char('\n');
    }
    file.close();

    statusBar()->showMessage(
        QStringLiteral("已导出 %1 项（%2）").arg(m_poll->taskCount())
            .arg(asText ? QStringLiteral("TXT / Tab 分隔") : QStringLiteral("CSV")),
        4000);
}

void MainWindow::loadSamplePoints()
{
    QVector<PollTask> tasks;

    PollTask v;
    v.name = QStringLiteral("母线电压");
    v.slave = 1;
    v.function = 0x03;
    v.addr = 0;
    v.count = 1;
    v.periodMs = 500;
    v.type = ValueType::UInt16;
    v.scale = 0.1;
    v.unit = QStringLiteral("V");
    tasks.append(v);

    PollTask i2;
    i2.name = QStringLiteral("母线电流");
    i2.slave = 1;
    i2.function = 0x03;
    i2.addr = 1;
    i2.count = 1;
    i2.periodMs = 500;
    i2.type = ValueType::Int16;
    i2.scale = 0.1;
    i2.unit = QStringLiteral("A");
    tasks.append(i2);

    PollTask soc;
    soc.name = QStringLiteral("SOC");
    soc.slave = 1;
    soc.function = 0x03;
    soc.addr = 2;
    soc.count = 1;
    soc.periodMs = 1000;
    soc.type = ValueType::UInt16;
    soc.scale = 0.1;
    soc.unit = QStringLiteral("%");
    tasks.append(soc);

    m_poll->stop();
    m_poll->setTasks(tasks);
    m_pollModel->notifyAll();
    m_scopeRows.clear();      // 行号已失效，示波器一起清掉
    refreshWave();
    onPollSummary();
    if (m_pollTable && m_pollModel->rowCount() > 0)
        m_pollTable->setCurrentIndex(m_pollModel->index(0, 0));
    statusBar()->showMessage(QStringLiteral("已载入示例点表，改成本机从站地址后即可启动"), 5000);
}

void MainWindow::clearPoints()
{
    m_scopeRows.clear();
    refreshWave();
    m_poll->clearTasks();
    m_pollModel->notifyAll();
    onPollSummary();
}

void MainWindow::togglePolling()
{
    if (m_poll->isRunning()) {
        m_poll->stop();
        return;
    }
    if (m_periodic && m_periodic->isChecked()) {
        m_periodic->setChecked(false);
        statusBar()->showMessage(QStringLiteral("周期发送与轮询互斥，已自动停止周期发送"), 4000);
    }
    m_poll->start();
}

void MainWindow::togglePause()
{
    if (!m_poll->isRunning())
        return;
    m_poll->setPaused(!m_poll->isPaused());
}

void MainWindow::writeSelectedPoint()
{
    const QModelIndex idx = m_pollTable->currentIndex();
    if (!idx.isValid()) {
        statusBar()->showMessage(QStringLiteral("先选中一行再写"), 3000);
        return;
    }

    PollTask *t = m_poll->taskAt(idx.row());
    if (!t)
        return;

    bool ok = false;
    const int current = t->raw.isEmpty() ? 0 : static_cast<int>(t->raw.first());
    const int value = QInputDialog::getInt(
        this, QStringLiteral("写单寄存器"),
        QStringLiteral("%1\n从站 %2 · 地址 %3（FC06 写保持寄存器）\n写入值 0~65535：")
            .arg(t->name)
            .arg(t->slave)
            .arg(t->addr),
        current, 0, 65535, 1, &ok);
    if (!ok)
        return;

    if (m_poll->isRunning()) {
        m_poll->enqueueWrite(t->slave, t->addr, static_cast<quint16>(value));
        statusBar()->showMessage(QStringLiteral("写请求已排队"), 3000);
        return;
    }

    if (!m_link->isOpen()) {
        onLinkError(QStringLiteral("请先打开串口"));
        return;
    }

    QByteArray req;
    req.append(static_cast<char>(t->slave));
    req.append(static_cast<char>(0x06));
    req.append(static_cast<char>((t->addr >> 8) & 0xFF));
    req.append(static_cast<char>(t->addr & 0xFF));
    req.append(static_cast<char>((value >> 8) & 0xFF));
    req.append(static_cast<char>(value & 0xFF));
    m_link->send(req, true);
}

void MainWindow::onPollStateChanged(bool running, bool paused)
{
    if (m_pollBtn) {
        m_pollBtn->setText(running ? QStringLiteral("停止轮询") : QStringLiteral("开始轮询"));
        applyIcon(m_pollBtn, running ? icons::stop(Qt::white) : icons::play(Qt::white));
    }
    if (m_pauseBtn) {
        m_pauseBtn->setEnabled(running);
        m_pauseBtn->setText(paused ? QStringLiteral("继续") : QStringLiteral("暂停"));
    }
}

void MainWindow::onPollSummary()
{
    if (!m_pollSummary)
        return;
    m_pollSummary->setText(QStringLiteral("%1 项 · 成功 %2 · 失败 %3 · 请求 %4")
                               .arg(m_poll->taskCount())
                               .arg(m_poll->okTotal())
                               .arg(m_poll->errTotal())
                               .arg(m_poll->requestCount()));
}

void MainWindow::onPollTaskUpdated(int row)
{
    m_pollModel->notifyRow(row);
}

void MainWindow::onPollLog(const QString &text)
{
    statusBar()->showMessage(text, 4000);
}

// ---------------------------------------------------------------- CAN

void MainWindow::loadDbc()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("载入 DBC"), QString(),
        QStringLiteral("DBC 文件 (*.dbc);;所有文件 (*.*)"));
    if (!path.isEmpty())
        loadDbcFile(path);
}

bool MainWindow::loadDbcFile(const QString &path)
{
    auto *db = new DbcDatabase;
    QString err;
    if (!db->loadFile(path, &err)) {
        delete db;
        onLinkError(QStringLiteral("DBC 载入失败：%1").arg(err));
        return false;
    }

    delete m_dbc;
    m_dbc = db;
    m_canModel->setDatabase(m_dbc);

    if (m_dbcBtn)
        m_dbcBtn->setText(QStringLiteral("DBC：%1").arg(m_dbc->fileName()));

    statusBar()->showMessage(
        QStringLiteral("已载入 %1 条报文定义（%2）")
            .arg(m_dbc->messageCount()).arg(m_dbc->fileName()),
        6000);
    return true;
}

void MainWindow::toggleCan()
{
    if (m_can->isOpen()) {
        m_can->close();
        return;
    }

    CanLink::Config cfg;
    cfg.deviceType = m_canTypeBox->currentData().toUInt();
    cfg.channel = m_canChannelBox->currentData().toUInt();
    cfg.bitrate = m_canBaudBox->currentData().toInt();

    if (!m_can->open(cfg)) {
        m_canStatus->setText(m_can->lastError());
        m_canStatus->setObjectName(QStringLiteral("StatusBad"));
        m_canStatus->style()->unpolish(m_canStatus);
        m_canStatus->style()->polish(m_canStatus);
        statusBar()->showMessage(m_can->lastError(), 8000);
    }
}

void MainWindow::onCanState(bool open)
{
    m_canBtn->setText(open ? QStringLiteral("关闭 CAN") : QStringLiteral("打开 CAN"));
    m_canTypeBox->setEnabled(!open);
    m_canChannelBox->setEnabled(!open);
    m_canBaudBox->setEnabled(!open);

    if (open) {
        const QString info = m_can->boardInfo();
        m_canStatus->setText(info.isEmpty() ? QStringLiteral("已打开") : info);
        m_canStatus->setObjectName(QStringLiteral("StatusOk"));
    } else {
        m_canStatus->setText(QStringLiteral("未打开"));
        m_canStatus->setObjectName(QStringLiteral("Hint"));
    }
    m_canStatus->style()->unpolish(m_canStatus);
    m_canStatus->style()->polish(m_canStatus);
}

void MainWindow::onCanFrame(const busscope::CanFrame &frame)
{
    m_canModel->append(frame);
    m_canTable->scrollToBottom();
}

void MainWindow::onCanCounters(quint64 rx, quint64 tx)
{
    m_canCount->setText(QStringLiteral("RX %1 · TX %2").arg(rx).arg(tx));
}

void MainWindow::clearCanFrames()
{
    m_canModel->clear();
}

void MainWindow::sendCan()
{
    if (!m_can->isOpen()) {
        statusBar()->showMessage(QStringLiteral("先打开 CAN 通道"), 3000);
        return;
    }

    QString idText = m_canIdEdit->text().trimmed();
    if (idText.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
        idText = idText.mid(2);

    bool idOk = false;
    const uint id = idText.toUInt(&idOk, 16);
    if (!idOk) {
        statusBar()->showMessage(QStringLiteral("ID 不是合法十六进制"), 3000);
        return;
    }

    bool dataOk = false;
    const QByteArray data = hexToBytes(m_canDataEdit->text(), &dataOk);
    if (!dataOk || data.size() > 8) {
        statusBar()->showMessage(QStringLiteral("数据必须是 0~8 字节的十六进制"), 3000);
        return;
    }

    if (!m_can->send(id, m_canExtBox->isChecked(), m_canRemoteBox->isChecked(), data))
        statusBar()->showMessage(QStringLiteral("发送失败"), 3000);
}

bool MainWindow::openSerialDirect(const QString &portName, int baudRate, bool autoPoll)
{
    if (m_link->isOpen())
        m_link->close();

    // 让下拉框同步到实际参数，界面上能看出在用哪个口
    refreshPorts();
    int idx = m_portBox->findData(portName);
    if (idx < 0) {
        m_portBox->addItem(portName, portName);
        idx = m_portBox->count() - 1;
    }
    m_portBox->setCurrentIndex(idx);

    const int bi = m_baudBox->findText(QString::number(baudRate));
    if (bi >= 0)
        m_baudBox->setCurrentIndex(bi);

    PortConfig cfg;
    cfg.portName = portName;
    cfg.baudRate = baudRate;
    if (!m_link->open(cfg))
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
    if (!m_canTypeBox || !m_canChannelBox || !m_canBaudBox)
        return false;

    const int typeIdx = m_canTypeBox->findData(QVariant(deviceType));
    if (typeIdx >= 0)
        m_canTypeBox->setCurrentIndex(typeIdx);

    const int chIdx = m_canChannelBox->findData(QVariant(channel));
    if (chIdx >= 0)
        m_canChannelBox->setCurrentIndex(chIdx);

    const int baudIdx = m_canBaudBox->findData(QVariant(bitrate));
    if (baudIdx >= 0)
        m_canBaudBox->setCurrentIndex(baudIdx);

    if (m_tabs)
        setProtocol(1);

    if (!m_can->isOpen())
        toggleCan();

    return m_can->isOpen();
}

void MainWindow::showFrameDetail(const QModelIndex &current, const QModelIndex &previous)
{
    Q_UNUSED(previous)

    const busscope::Frame *f = m_model->frameAt(current.row());
    if (!f) {
        clearLog();
        return;
    }

    const QString dirText = (f->dir == Direction::Tx) ? QStringLiteral("TX")
                            : (f->dir == Direction::Rx ? QStringLiteral("RX") : QStringLiteral("--"));
    m_detailTitle->setText(QStringLiteral("帧 #%1 · %2").arg(f->index).arg(dirText));

    QString html = QStringLiteral("<table cellspacing='0' cellpadding='3'>");
    const QStringList rows = f->detail.split(QLatin1Char('\n'));
    for (const QString &row : rows) {
        if (row.isEmpty())
            continue;
        const int split = row.indexOf(QLatin1Char('|'));
        const QString key = (split > 0) ? row.left(split) : row;
        const QString value = (split > 0) ? row.mid(split + 1) : QString();
        html += QStringLiteral("<tr><td style='color:#7A7B80;white-space:nowrap'>%1</td>"
                               "<td style='color:#CCCCCC;padding-left:14px'>%2</td></tr>")
                    .arg(key.toHtmlEscaped(), value.toHtmlEscaped());
    }
    html += QStringLiteral("</table>");
    m_fieldView->setHtml(html);

    QString color = QStringLiteral("#CCCCCC");
    if (f->kind == FrameKind::Exception)
        color = QStringLiteral("#F85149");
    else if (f->kind == FrameKind::CrcError || f->kind == FrameKind::Timeout)
        color = QStringLiteral("#DCDCAA");
    else if (f->dir == Direction::Tx)
        color = QStringLiteral("#8B93F8");
    else if (f->dir == Direction::Rx)
        color = QStringLiteral("#4EC9B0");

    if (f->raw.isEmpty()) {
        m_hexView->setHtml(QStringLiteral("<span style='color:#6A6B70'>（无原始字节）</span>"));
    } else {
        m_hexView->setHtml(QStringLiteral("<div style='color:%1'>%2&nbsp;&nbsp;%3</div>")
                               .arg(color, dirText,
                                    QString::fromLatin1(f->raw.toHex(' ').toUpper())));
    }

    if (f->elapsedMs >= 0) {
        m_timingValue->setText(QStringLiteral("%1 ms").arg(f->elapsedMs));
        m_timingCaption->setText(QStringLiteral("请求 → 响应耗时"));
    } else {
        m_timingValue->setText(QStringLiteral("--"));
        m_timingCaption->setText(QStringLiteral("单向帧，无往返耗时"));
    }
    m_timingMetrics->setText(QStringLiteral("帧号 %1\n长度 %2 字节")
                                 .arg(f->index)
                                 .arg(f->raw.size()));
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (m_table && watched == m_table->viewport()
        && event->type() == QEvent::Resize && m_emptyHint) {
        m_emptyHint->setGeometry(m_table->viewport()->rect());
    }
    return QMainWindow::eventFilter(watched, event);
}

QString MainWindow::humanBytes(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
}

} // namespace busscope
