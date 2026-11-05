#pragma once

#include <QWidget>
#include <QVector>
#include <QList>
#include <QString>
#include <QColor>
#include <QPoint>
#include <QRectF>
#include <QtGlobal>

namespace busscope {

/// 一条曲线：点名、单位、采样值（旧→新）、时间戳、颜色、是否可见。
struct WaveSeries {
    QString         name;
    QString         unit;
    QVector<qreal>  values;
    QVector<qint64> times;    ///< 与 values 等长（epoch ms）；空表示无时间信息
    QColor          color;
    bool            visible = true;
};

/// 实时波形控件。
///
/// 视图：
///   - 默认「跟随最新」，视窗自动铺满全部历史；
///   - 滚轮在绘图区缩放（以鼠标位置为锚点）；
///   - 左键拖动绘图区平移；拖底部时间轴滑块移动视窗；
///   - 双击任意位置、或外部调 followLatest() 回到最新。
///
/// 刻度：
///   - 统一刻度：所有可见通道共用一个 Y 轴（同量纲时直观）
///   - 归一化：各曲线按自己的值域铺满（量纲差很大时用，只看形状）
class WaveChart : public QWidget
{
    Q_OBJECT

public:
    explicit WaveChart(QWidget *parent = nullptr);

    void setSeries(const QList<WaveSeries> &series);
    void clear(const QString &hint = QString());

    /// 冻结画面（外部数据照常累积，恢复后一次画最新的）
    void setPaused(bool paused);
    bool isPaused() const { return m_paused; }

    /// true=统一 Y 轴，false=各自归一化
    void setUnifiedScale(bool on);
    bool isUnifiedScale() const { return m_unified; }

    /// 当前实际是否按统一刻度在画（要有可见通道才有意义）
    bool effectiveUnified() const;

    /// 是否贴在最新数据上（false = 正在回看历史）
    bool isFollowing() const { return m_follow; }

    /// 回到最新，并把视窗恢复成「全部历史」
    void followLatest();

    QSize sizeHint() const override { return QSize(560, 240); }

signals:
    /// 跟随状态变化（外部按钮据此更新文字）
    void followChanged(bool following);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    struct Layout {
        QRectF plot;      ///< 曲线绘制区
        QRectF slider;    ///< 底部时间轴滑块（整条 track）
        int    listW   = 0;
        int    listTop = 0;
        int    yAxisW  = 0;
    };

    Layout computeLayout() const;

    /// 可见通道的时间总范围。没有时间信息时返回 false。
    static bool globalTimeRange(const QList<WaveSeries> &data, qint64 *lo, qint64 *hi);

    /// 求当前视窗（不修改状态）。hasTime=false 时返回全 0。
    void resolveWindow(qint64 gLo, qint64 gHi, qint64 *wLo, qint64 *wHi) const;

    /// 把视窗挪到指定右边界（会夹紧并把跟随状态更新掉）
    void setViewEnd(qint64 end, qint64 gLo, qint64 gHi);

    /// 设置视窗跨度（缩放）
    void setViewSpan(double spanMs, qint64 gLo, qint64 gHi);

    QList<WaveSeries> m_series;
    QString           m_hint;
    bool              m_paused  = false;
    bool              m_unified = false;   ///< 多通道默认各自归一，单通道总是真实刻度
    QList<WaveSeries> m_frozen;   ///< 暂停瞬间的画面快照

    // ---- 视窗 ----
    bool   m_follow  = true;    ///< true=跟最新
    bool   m_zoomSet = false;   ///< 用户手动缩放过
    double m_viewSpan = 0.0;    ///< 视窗时间宽度（ms），0 = 自动（全部）
    qint64 m_viewEnd  = 0;      ///< 视窗右边界（epoch ms），仅 m_follow=false 时有效

    // ---- 拖动 ----
    enum class Drag { None, Plot, Slider };
    Drag   m_drag     = Drag::None;
    QPoint m_dragPos;
    qint64 m_dragEnd  = 0;
    double m_dragSpan = 0.0;
};

} // namespace busscope
