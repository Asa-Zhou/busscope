#pragma once

#include <QWidget>
#include <QVector>

#include "app/wave_chart.h"

class QDialog;
class QPushButton;
class QTableView;
class QTimer;

namespace busscope {

class PollEngine;

/// 点表页下方的示波器区：嵌入块 + 独立窗口共用同一份通道列表。
/// 曲线数据取自 PollEngine 的任务历史；右键菜单里的示波器操作由 PollPage 转发进来。
class ScopePanel : public QWidget
{
    Q_OBJECT

public:
    explicit ScopePanel(PollEngine *poll, QTableView *pollTable, QWidget *parent = nullptr);

    void addRow(int row);        ///< 把点表行加入示波器（最多 4 条）
    void removeRow(int row);     ///< 移除一条曲线
    void clearRows();            ///< 清空全部曲线（点表重载后行号失效时用）
    void refresh();              ///< 重建曲线数据并刷新嵌入/独立两块波形
    void toggleWindow();         ///< 显示/隐藏独立示波器窗口
    void exportImage();          ///< 波形存为 PNG
    void exportData(bool asText);///< 波形数据导出 CSV/TXT

    /// 右键菜单判断可用性用：该行是否已在示波器里。
    bool containsRow(int row) const { return m_scopeRows.contains(row); }
    /// 当前曲线条数（右键菜单判断「最多 4 条」用）。
    int seriesCount() const { return m_scopeRows.size(); }

signals:
    void statusMessage(const QString &text, int ms);
    void linkError(const QString &text);        ///< 失败要进帧记录时发（由主窗口路由到 Slave 页）
    void scopeWindowToggled(bool visible);      ///< 让工具栏按钮同步文案

private:
    QWidget *buildHeadBar();
    void updateFollowUi(bool following);
    QList<WaveSeries> collectSeries();

    PollEngine *m_poll = nullptr;
    QTableView *m_pollTable = nullptr;   ///< 预览当前选中行用

    WaveChart *m_wave      = nullptr;    ///< 页内嵌入的那块
    WaveChart *m_waveFloat = nullptr;    ///< 独立窗口里的那块
    QDialog   *m_scopeWin  = nullptr;
    QTimer    *m_waveTimer = nullptr;
    QVector<int> m_scopeRows;            ///< 已加入示波器的点表行号
    QPushButton *m_followBtn = nullptr;  ///< 回看时可用，一键跳回最新
};

} // namespace busscope
