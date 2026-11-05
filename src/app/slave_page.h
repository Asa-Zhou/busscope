#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QLabel;
class QTimer;

namespace busscope {

class FrameLogModel;
class FrameLogView;
class ModbusLink;
class PollEngine;

/// Modbus「Slave · 收发」页：左侧连接配置、中间帧记录、右侧手动发送。
/// 页内自持全部控件与槽函数；跨页规则（与轮询互斥等）经信号交给主窗口接线。
class SlavePage : public QWidget
{
    Q_OBJECT

public:
    explicit SlavePage(ModbusLink *link, PollEngine *poll, QWidget *parent = nullptr);

    void refreshPorts();

    /// 命令行/自动重连用：同步下拉框到指定参数并打开串口。
    bool openSerial(const QString &portName, int baudRate);

    /// 串口下拉里是否已有该口（启动时判断要不要自动重连）。
    bool hasPort(const QString &portName) const;

    /// 手动发送入口。Ctrl+Enter 是窗口级快捷键，由 MainWindow 转发进来。
    void sendManual();

    /// 提示帧进帧记录（其他页面的错误也经主窗口汇到这里）。
    void appendNote(const QString &text);

    bool isPeriodicActive() const;
    void stopPeriodic();

    /// 链路通断的界面状态（连接记忆、状态栏计数由 MainWindow 负责）。
    void onLinkStateChanged(bool open);

signals:
    void statusMessage(const QString &text, int ms);

private:
    QWidget *buildLeftPanel();
    QWidget *buildRightPanel();

    ModbusLink    *m_link    = nullptr;
    PollEngine    *m_poll    = nullptr;   ///< 只读状态（与周期发送互斥判断）
    FrameLogModel *m_model   = nullptr;
    FrameLogView  *m_logView = nullptr;

    QComboBox   *m_portBox    = nullptr;
    QComboBox   *m_baudBox    = nullptr;
    QComboBox   *m_frameBox   = nullptr;
    QPushButton *m_connectBtn = nullptr;
    QLabel      *m_connDot    = nullptr;
    QLabel      *m_connText   = nullptr;

    QComboBox      *m_formatBox = nullptr;
    QPlainTextEdit *m_sendEdit  = nullptr;
    QCheckBox      *m_autoCrc   = nullptr;
    QPushButton    *m_sendBtn   = nullptr;

    // 周期发送：QTimer + 间隔输入 + 勾选启停
    QCheckBox *m_periodic  = nullptr;
    QSpinBox  *m_interval  = nullptr;
    QTimer    *m_sendTimer = nullptr;

    void toggleConnection();
    void setLinkUi(bool open);
    void sendPeriodic();
    QByteArray parsePayload(bool *ok) const;
};

} // namespace busscope
