#pragma once

#include <QModelIndex>
#include <QWidget>

class QCheckBox;
class QLabel;
class QTableView;
class QTextEdit;

namespace busscope {

class FrameLogModel;
struct Frame;

/// Slave 页中间列：工具条 + 帧列表 + 帧详情三栏。
/// 只管帧的展示与导出；串口连接与发送逻辑在 SlavePage。
class FrameLogView : public QWidget
{
    Q_OBJECT

public:
    explicit FrameLogView(FrameLogModel *model, QWidget *parent = nullptr);

    /// 收到一帧：追加记录、按需滚动、更新计数。
    void onFrameReady(const busscope::Frame &frame);

    /// 追加一条提示帧（错误/说明文本），并向状态栏轻提示。
    void appendNote(const QString &text);

signals:
    void statusMessage(const QString &text, int ms);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *buildToolbar();
    QWidget *buildDetailPane();

    void showFrameDetail(const QModelIndex &current, const QModelIndex &previous);
    void clearLog();
    void exportLog();

    FrameLogModel *m_model = nullptr;

    QTableView *m_table      = nullptr;
    QCheckBox  *m_autoScroll = nullptr;
    QLabel     *m_frameCount = nullptr;
    QLabel     *m_emptyHint  = nullptr;

    QLabel    *m_detailTitle   = nullptr;
    QTextEdit *m_fieldView     = nullptr;
    QTextEdit *m_hexView       = nullptr;
    QLabel    *m_timingValue   = nullptr;
    QLabel    *m_timingCaption = nullptr;
    QLabel    *m_timingMetrics = nullptr;
};

} // namespace busscope
