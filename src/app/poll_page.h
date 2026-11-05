#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QTableView;

namespace busscope {

class ModbusLink;
class PollEngine;
class PollTableModel;
class ScopePanel;

/// Modbus「Poll · 轮询」页：点表工具条 + 任务表 + 内嵌示波器。
/// 点表文件读写走 core/point_file，页面只做装配与提示。
class PollPage : public QWidget
{
    Q_OBJECT

public:
    explicit PollPage(PollEngine *poll, ModbusLink *link, QWidget *parent = nullptr);

    /// 导入点表文件（命令行 / 手动导入 / 启动恢复共用）：
    /// 装载任务、清示波器、记忆路径，并请求切到本页。
    bool openPointFile(const QString &path);

signals:
    void statusMessage(const QString &text, int ms);
    void linkError(const QString &text);      ///< 失败要进帧记录（主窗口路由到 Slave 页）
    void requestShow();                       ///< 载入点表后请求切到轮询页
    void pollingStarting();                   ///< 即将开始轮询（与周期发送互斥，主窗口处理）

private:
    QWidget *buildToolbar();

    void importPoints();
    void exportPointsAs(bool asText);
    void loadSamplePoints();
    void clearPoints();
    void togglePolling();
    void togglePause();
    void writeSelectedPoint();
    void onPollContextMenu(const QPoint &pos);
    void onPollStateChanged(bool running, bool paused);
    void onPollSummary();
    void onPollTaskUpdated(int row);

    PollEngine     *m_poll      = nullptr;
    ModbusLink     *m_link      = nullptr;   ///< 不开轮询时直接写单寄存器用
    PollTableModel *m_pollModel = nullptr;

    QTableView  *m_pollTable   = nullptr;
    QPushButton *m_pollBtn     = nullptr;
    QPushButton *m_pauseBtn    = nullptr;
    QPushButton *m_scopeBtn    = nullptr;
    QLabel      *m_pollSummary = nullptr;

    ScopePanel *m_scope = nullptr;
};

} // namespace busscope
