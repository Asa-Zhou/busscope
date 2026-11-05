#include "app/slave_page.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSerialPortInfo>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <cctype>

#include "app/frame_log_view.h"
#include "app/icons.h"
#include "app/theme.h"
#include "app/widget_helpers.h"
#include "core/frame_log_model.h"
#include "io/modbus_link.h"
#include "io/poll_engine.h"

namespace busscope {

SlavePage::SlavePage(ModbusLink *link, PollEngine *poll, QWidget *parent)
    : QWidget(parent), m_link(link), m_poll(poll)
{
    m_model = new FrameLogModel(this);
    m_logView = new FrameLogView(m_model, this);

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(1);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(buildLeftPanel());
    splitter->addWidget(m_logView);
    splitter->addWidget(buildRightPanel());
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes(QList<int>() << 300 << 780 << 288);

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addWidget(splitter);

    // 帧记录相关的链路信号：帧进列表、错误转提示帧、通断刷界面
    connect(m_link, &ModbusLink::frameReady, m_logView, &FrameLogView::onFrameReady);
    connect(m_link, &ModbusLink::errorText, m_logView, &FrameLogView::appendNote);
    connect(m_link, &ModbusLink::stateChanged, this, &SlavePage::onLinkStateChanged);

    // 帧记录视图的轻提示经本页信号汇给主窗口
    connect(m_logView, &FrameLogView::statusMessage, this, &SlavePage::statusMessage);

    // 周期发送：逻辑照搬原型的 sendTimer（QTimer + 间隔输入 + 勾选启停）
    m_sendTimer = new QTimer(this);
    connect(m_sendTimer, &QTimer::timeout, this, &SlavePage::sendPeriodic);
    connect(m_periodic, &QCheckBox::toggled, this, [this](bool on) {
        if (!on) {
            m_sendTimer->stop();
            return;
        }
        if (!m_link->isOpen()) {
            appendNote(QStringLiteral("请先打开串口，再启用周期发送"));
            m_periodic->setChecked(false);
            return;
        }
        if (m_poll->isRunning()) {
            // 半双工总线上手动周期帧会不断抢走轮询的事务槽，两边解析都会错乱
            appendNote(QStringLiteral("轮询进行中，请先停止轮询再启用周期发送"));
            m_periodic->setChecked(false);
            return;
        }
        m_sendTimer->start(m_interval->value());
    });
    connect(m_interval, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int ms) {
        if (m_periodic->isChecked())
            m_sendTimer->start(ms);
    });
}

// ---------------------------------------------------------------- 左侧面板

QWidget *SlavePage::buildLeftPanel()
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
    connect(refreshBtn, &QPushButton::clicked, this, &SlavePage::refreshPorts);
    portRowLayout->addWidget(refreshBtn);

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
    connect(m_connectBtn, &QPushButton::clicked, this, &SlavePage::toggleConnection);
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

// ---------------------------------------------------------------- 右侧面板

QWidget *SlavePage::buildRightPanel()
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
    connect(m_sendBtn, &QPushButton::clicked, this, &SlavePage::sendManual);
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

// ---------------------------------------------------------------- 槽函数

void SlavePage::refreshPorts()
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

void SlavePage::toggleConnection()
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

void SlavePage::setLinkUi(bool open)
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
}

void SlavePage::onLinkStateChanged(bool open)
{
    setLinkUi(open);
    if (!open && m_periodic && m_periodic->isChecked())
        m_periodic->setChecked(false);
}

void SlavePage::appendNote(const QString &text)
{
    m_logView->appendNote(text);
}

bool SlavePage::isPeriodicActive() const
{
    return m_periodic && m_periodic->isChecked();
}

void SlavePage::stopPeriodic()
{
    if (m_periodic)
        m_periodic->setChecked(false);
}

bool SlavePage::hasPort(const QString &portName) const
{
    for (int i = 0; i < m_portBox->count(); ++i) {
        if (m_portBox->itemData(i).toString() == portName)
            return true;
    }
    return false;
}

bool SlavePage::openSerial(const QString &portName, int baudRate)
{
    // 让下拉框同步到实际参数，界面上能看出在用哪个口
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
    return m_link->open(cfg);
}

void SlavePage::sendManual()
{
    if (!m_link->isOpen()) {
        appendNote(QStringLiteral("请先打开串口"));
        return;
    }

    // 覆盖正在等待响应的事务会污染切帧上下文，请求-响应配对从此错乱
    if (m_link->isBusy()) {
        appendNote(QStringLiteral("上一条请求还在等响应，稍候再发（或缩短超时）"));
        return;
    }
    if (m_poll->isRunning()) {
        appendNote(QStringLiteral("轮询进行中，请先在「点表 · 轮询」页停止轮询再手动发送"));
        return;
    }

    bool ok = false;
    const QByteArray payload = parsePayload(&ok);
    if (!ok || payload.isEmpty()) {
        appendNote(QStringLiteral("发送内容不是合法的 %1 数据")
                        .arg(m_formatBox->currentIndex() == 0 ? QStringLiteral("HEX")
                                                              : QStringLiteral("ASCII")));
        return;
    }

    const bool isHex = (m_formatBox->currentIndex() == 0);
    m_link->send(payload, isHex && m_autoCrc->isChecked());
}

void SlavePage::sendPeriodic()
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

QByteArray SlavePage::parsePayload(bool *ok) const
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

} // namespace busscope
