#pragma once

#include <QMainWindow>
#include <QModelIndex>

class QCheckBox;
class QComboBox;
class QDialog;
class QStackedWidget;
class QToolButton;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QSplitter;
class QTableView;
class QTextEdit;
class QTimer;

namespace busscope {

class CanFrameModel;
class CanLink;
class DbcDatabase;
class FrameLogModel;
class ModbusLink;
class PollEngine;
class PollTableModel;
class WaveChart;
struct WaveSeries;
struct CanFrame;
struct Frame;

/// 主窗口：左侧连接配置、中间帧列表 + 帧详情、右侧手动发送。
/// 界面用代码搭建，不放 .ui 文件，方便和设计稿逐像素对照。
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

private slots:
    void refreshPorts();
    void toggleConnection();
    void onLinkStateChanged(bool open);
    void onFrameReady(const busscope::Frame &frame);
    void onCountersChanged(qint64 rxBytes, qint64 txBytes);
    void onLinkError(const QString &text);
    void sendManual();
    void sendPeriodic();
    void clearLog();
    void exportLog();
    void importPoints();
    void exportPointsAs(bool asText);
    void exportScopeImage();
    void exportScopeData(bool asText);
    void onPollContextMenu(const QPoint &pos);
    void toggleScopeWindow();
    void loadSamplePoints();
    void clearPoints();
    void togglePolling();
    void togglePause();
    void writeSelectedPoint();
    void onPollStateChanged(bool running, bool paused);
    void onPollSummary();
    void onPollTaskUpdated(int row);
    void onPollLog(const QString &text);

    void toggleCan();
    void loadDbc();
    void sendCan();
    void clearCanFrames();
    void onCanFrame(const busscope::CanFrame &frame);
    void onCanState(bool open);
    void onCanCounters(quint64 rx, quint64 tx);
    void showFrameDetail(const QModelIndex &current, const QModelIndex &previous);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *buildProtocolRail();
    QWidget *buildModbusPage();
    QWidget *buildSlavePage();
    QWidget *buildLeftPanel();
    QWidget *buildCenter();
    QWidget *buildPointPage();
    QWidget *buildCanPage();
    void     setProtocol(int index);   ///< 0=Modbus, 1=CAN
    QWidget *buildDetailPane();
    QWidget *buildRightPanel();
    void     buildStatusBar();

    void        fillQuiet(const QString &hex);
    QByteArray  parsePayload(bool *ok) const;
    void        setConnectionUi(bool open);
    static QString humanBytes(qint64 bytes);

    // 协议导航（左）+ 协议页栈
    QStackedWidget *m_protocolStack = nullptr;
    QToolButton    *m_railModbus    = nullptr;
    QToolButton    *m_railCan       = nullptr;

    ModbusLink    *m_link  = nullptr;
    FrameLogModel *m_model = nullptr;

    // 点表 / 轮询
    PollEngine     *m_poll      = nullptr;
    PollTableModel *m_pollModel = nullptr;
    QTableView     *m_pollTable = nullptr;
    QPushButton    *m_pollBtn   = nullptr;
    QPushButton    *m_pauseBtn  = nullptr;
    QLabel         *m_pollSummary = nullptr;

    // 示波器：嵌入区 + 独立窗口，共用同一份通道列表
    WaveChart      *m_wave      = nullptr;   ///< 点表页内嵌的那块
    WaveChart      *m_waveFloat = nullptr;   ///< 独立窗口里的那块
    QDialog        *m_scopeWin  = nullptr;
    QTimer         *m_waveTimer = nullptr;
    QVector<int>    m_scopeRows;             ///< 已加入示波器的点表行号
    QPushButton    *m_scopeBtn  = nullptr;
    QPushButton    *m_followBtn = nullptr;   ///< 回看时可用，一键跳回最新
    void            refreshWave();
    void            updateFollowUi(bool following);
    QList<WaveSeries> collectScopeSeries();
    void            addRowToScope(int row);
    void            removeRowFromScope(int row);

    // CAN
    CanLink        *m_can      = nullptr;
    CanFrameModel  *m_canModel = nullptr;
    DbcDatabase    *m_dbc      = nullptr;
    QPushButton    *m_dbcBtn   = nullptr;
    QTableView     *m_canTable = nullptr;
    QComboBox      *m_canTypeBox    = nullptr;
    QComboBox      *m_canChannelBox = nullptr;
    QComboBox      *m_canBaudBox    = nullptr;
    QPushButton    *m_canBtn        = nullptr;
    QLabel         *m_canStatus     = nullptr;
    QLabel         *m_canCount      = nullptr;
    QLineEdit      *m_canIdEdit     = nullptr;
    QLineEdit      *m_canDataEdit   = nullptr;
    QCheckBox      *m_canExtBox     = nullptr;
    QCheckBox      *m_canRemoteBox  = nullptr;

    QComboBox   *m_portBox   = nullptr;
    QComboBox   *m_baudBox   = nullptr;
    QComboBox   *m_frameBox  = nullptr;
    QPushButton *m_connectBtn = nullptr;
    QLabel      *m_connDot   = nullptr;
    QLabel      *m_connText  = nullptr;

    QTableView *m_table      = nullptr;
    QCheckBox  *m_autoScroll = nullptr;
    QLabel     *m_frameCount = nullptr;
    QLabel     *m_emptyHint  = nullptr;
    QTabWidget *m_tabs       = nullptr;

    QComboBox      *m_formatBox = nullptr;
    QPlainTextEdit *m_sendEdit  = nullptr;
    QCheckBox      *m_autoCrc   = nullptr;
    QPushButton    *m_sendBtn   = nullptr;

    // 以下三项从原型 ModbusDebuger 移植：周期发送 / 串口刷新 / 日志导出
    QCheckBox *m_periodic  = nullptr;
    QSpinBox  *m_interval  = nullptr;
    QTimer    *m_sendTimer = nullptr;

    QLabel    *m_detailTitle   = nullptr;
    QTextEdit *m_fieldView     = nullptr;
    QTextEdit *m_hexView       = nullptr;
    QLabel    *m_timingValue   = nullptr;
    QLabel    *m_timingCaption = nullptr;
    QLabel    *m_timingMetrics = nullptr;

    QLabel *m_statusConn     = nullptr;
    QLabel *m_statusCounters = nullptr;

    qint64 m_rxBytes = 0;
    qint64 m_txBytes = 0;
};

} // namespace busscope
