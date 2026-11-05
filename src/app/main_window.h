#pragma once

#include <QMainWindow>

class QLabel;
class QStackedWidget;
class QTabWidget;
class QToolButton;

namespace busscope {

class CanPage;
class ModbusLink;
class PollEngine;
class PollPage;
class SlavePage;

/// 主窗口：只做三件事——整体框架（协议导航 / 页栈 / 状态栏）、跨页接线、
/// 命令行入口转发。各协议页自持控件与槽函数，页面之间不互相引用，
/// 跨页规则（互斥、切页、提示帧汇总）全部经由这里的信号接线完成。
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    /// 打开一个点表 CSV（命令行参数 / 关联文件打开时使用）。
    bool openPointFile(const QString &path);

    /// 命令行直连 CAN：devType:channel:bitrate，例如 "4:0:500000"。
    bool openCanDirect(quint32 deviceType, quint32 channel, int bitrate);

    /// 命令行直连 Modbus：打开指定串口，可选立即开始轮询（8-N-1）。
    bool openSerialDirect(const QString &portName, int baudRate, bool autoPoll);

    /// 命令行 / 拖放载入 DBC
    bool loadDbcFile(const QString &path);

private:
    QWidget *buildProtocolRail();
    QWidget *buildModbusPage();
    void     buildStatusBar();
    void     setProtocol(int index);   ///< 0=Modbus, 1=CAN

    QStackedWidget *m_protocolStack = nullptr;
    QToolButton    *m_railModbus    = nullptr;
    QToolButton    *m_railCan       = nullptr;

    // ModbusLink 与 PollEngine 被 Slave / Poll 两页共用，由主窗口持有
    ModbusLink *m_link = nullptr;
    PollEngine *m_poll = nullptr;

    SlavePage  *m_slavePage = nullptr;
    PollPage   *m_pollPage  = nullptr;
    CanPage    *m_canPage   = nullptr;
    QTabWidget *m_tabs      = nullptr;

    QLabel *m_statusConn     = nullptr;
    QLabel *m_statusCounters = nullptr;

    qint64 m_rxBytes = 0;
    qint64 m_txBytes = 0;
};

} // namespace busscope
