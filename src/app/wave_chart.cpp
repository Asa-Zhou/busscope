#include "app/wave_chart.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDateTime>
#include <algorithm>
#include <cmath>

namespace busscope {

namespace {

const QColor kBg          (0x0F, 0x10, 0x13);
const QColor kBorder      (0x23, 0x24, 0x28);
const QColor kGrid        (0x1E, 0x20, 0x26);
const QColor kAxisText    (0x5A, 0x5B, 0x60);
const QColor kTitleText   (0x8A, 0x8B, 0x90);
const QColor kValueText   (0xE8, 0xE8, 0xEA);
const QColor kHintText    (0x6A, 0x6B, 0x70);
const QColor kPausedTag   (0xDC, 0xDC, 0xAA);
const QColor kBackTag     (0xDC, 0xDC, 0xAA);
const QColor kTrackBg     (0x1B, 0x1C, 0x21);
const QColor kHandle      (0x3A, 0x3D, 0x4E);
const QColor kHandleDrag  (0x5E, 0x6A, 0xD2);

// 左侧通道列表
const int kListW = 138;
const int kRowH  = 20;

// 布局常量（paintEvent 与鼠标事件共用，避免两处算不一致）
const int kPadL   = 12;
const int kPadR   = 12;
const int kPadT   = 8;
const int kPadB   = 8;
const int kTitleH = 16;
const int kFootH  = 16;
const int kSliderH = 14;

/// 视窗最小跨度：再小就没有意义了
const double kMinSpanMs = 200.0;

QString fmtNum(double v, int decimals = -1)
{
    if (decimals >= 0)
        return QString::number(v, 'f', decimals);

    const double a = std::fabs(v);
    if (a >= 100000.0)
        return QString::number(v / 1000.0, 'f', 1) + QLatin1Char('k');
    if (a >= 1000.0)
        return QString::number(v, 'f', 0);
    if (a >= 100.0)
        return QString::number(v, 'f', 1);
    if (a >= 1.0)
        return QString::number(v, 'f', 2);
    return QString::number(v, 'f', 3);
}

/// 按跨度决定 Y 轴小数位，避免 466.00 / 0.300 这种不整齐刻度
int decimalsFor(double span)
{
    if (span >= 100.0) return 0;
    if (span >= 10.0)  return 1;
    if (span >= 1.0)   return 2;
    return 3;
}

/// 毫秒数转 "12.5 s" / "850 ms"
QString fmtSpan(double ms)
{
    if (ms >= 1000.0)
        return QStringLiteral("%1 s").arg(ms / 1000.0, 0, 'f', ms < 10000.0 ? 1 : 0);
    return QStringLiteral("%1 ms").arg(ms, 0, 'f', 0);
}

} // namespace

WaveChart::WaveChart(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(150);
    setMouseTracking(true);   // 悬停时换光标
}

void WaveChart::setSeries(const QList<WaveSeries> &series)
{
    m_series = series;
    m_hint.clear();
    if (m_paused)
        return;          // 暂停时不刷新画面
    update();
}

void WaveChart::clear(const QString &hint)
{
    m_series.clear();
    m_frozen.clear();
    m_paused = false;
    m_follow = true;
    m_zoomSet = false;
    m_viewSpan = 0;
    m_viewEnd = 0;
    m_drag = Drag::None;
    m_hint = hint.isEmpty()
                 ? QStringLiteral("右键点表任意一行 → 添加到示波器")
                 : hint;
    update();
}

void WaveChart::setPaused(bool paused)
{
    if (m_paused == paused)
        return;

    m_paused = paused;
    if (paused)
        m_frozen = m_series;   // 停在这一帧
    else
        m_frozen.clear();
    update();
}

void WaveChart::setUnifiedScale(bool on)
{
    if (m_unified == on)
        return;
    m_unified = on;
    if (!m_paused)
        update();
}

bool WaveChart::effectiveUnified() const
{
    int visible = 0;
    for (const WaveSeries &w : m_series) {
        if (w.visible && !w.values.isEmpty())
            ++visible;
    }
    if (visible == 0)
        return false;
    if (visible == 1)
        return true;        // 只有一条曲线时永远显示真实刻度
    return m_unified;       // 多条时按用户选择，默认归一化
}

void WaveChart::followLatest()
{
    const bool wasFollowing = m_follow;
    m_follow  = true;
    m_zoomSet = false;
    m_viewSpan = 0;
    m_viewEnd  = 0;
    m_drag     = Drag::None;
    if (!wasFollowing)
        emit followChanged(true);
    update();
}

// ---------------------------------------------------------------- 布局 / 视窗

WaveChart::Layout WaveChart::computeLayout() const
{
    Layout l;

    l.listW   = (m_series.size() > 1) ? kListW : 0;
    l.listTop = kPadT + kTitleH;
    l.yAxisW  = effectiveUnified() ? 48 : 6;

    const double plotTop = kPadT + kTitleH;
    const double plotH   = height() - kPadT - kPadB - kTitleH - kFootH - kSliderH;

    l.plot = QRectF(kPadL + l.listW + l.yAxisW, plotTop,
                    width() - kPadL - kPadR - l.listW - l.yAxisW,
                    plotH);

    l.slider = QRectF(l.plot.left(),
                      l.plot.bottom() + 2 + kFootH,
                      l.plot.width(),
                      kSliderH - 4);
    return l;
}

bool WaveChart::globalTimeRange(const QList<WaveSeries> &data, qint64 *lo, qint64 *hi)
{
    qint64 a = 0, b = 0;
    for (const WaveSeries &w : data) {
        if (!w.visible)
            continue;
        if (w.times.size() != w.values.size() || w.times.isEmpty())
            continue;
        if (a == 0 || w.times.first() < a)
            a = w.times.first();
        if (w.times.last() > b)
            b = w.times.last();
    }
    if (a <= 0 || b <= a)
        return false;
    *lo = a;
    *hi = b;
    return true;
}

void WaveChart::resolveWindow(qint64 gLo, qint64 gHi, qint64 *wLo, qint64 *wHi) const
{
    const qint64 total = qMax<qint64>(1, gHi - gLo);

    double span = (m_zoomSet && m_viewSpan > 0.0) ? m_viewSpan : double(total);
    span = qBound(kMinSpanMs, span, double(total) * 1.05);

    qint64 end = m_follow ? gHi : (m_viewEnd > 0 ? m_viewEnd : gHi);
    if (end > gHi)
        end = gHi;

    const qint64 minEnd = gLo + qint64(span);
    if (end < minEnd)
        end = qMin(minEnd, gHi);

    *wLo = end - qint64(span);
    *wHi = end;
}

void WaveChart::setViewEnd(qint64 end, qint64 gLo, qint64 gHi)
{
    const double span = (m_zoomSet && m_viewSpan > 0.0)
                            ? m_viewSpan
                            : double(qMax<qint64>(1, gHi - gLo));

    if (end >= gHi) {
        // 拖到最右 → 自动回到跟随最新
        m_viewEnd = gHi;
        if (!m_follow) {
            m_follow = true;
            emit followChanged(true);
        }
        update();
        return;
    }

    if (m_follow) {
        m_follow = false;
        emit followChanged(false);
    }

    const qint64 minEnd = gLo + qint64(span);
    if (end < minEnd)
        end = qMin(minEnd, gHi);
    m_viewEnd = end;
    update();
}

void WaveChart::setViewSpan(double spanMs, qint64 gLo, qint64 gHi)
{
    const double total = double(qMax<qint64>(1, gHi - gLo));
    spanMs = qBound(kMinSpanMs, spanMs, total * 1.02);
    m_viewSpan = spanMs;
    m_zoomSet  = true;
    update();
}

// ---------------------------------------------------------------- 绘制

void WaveChart::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(kBorder, 1));
    p.setBrush(kBg);
    p.drawRoundedRect(box, 6, 6);

    // 暂停时画冻结的那一份
    const QList<WaveSeries> &data = (m_paused && !m_frozen.isEmpty()) ? m_frozen : m_series;

    if (m_series.isEmpty()) {
        p.setPen(kHintText);
        p.drawText(rect(), Qt::AlignCenter,
                   m_hint.isEmpty()
                       ? QStringLiteral("右键点表任意一行 → 添加到示波器")
                       : m_hint);
        return;
    }

    QFont f = font();
    f.setPointSizeF(qMax(8.0, f.pointSizeF() - 0.5));
    p.setFont(f);

    // ---- 统计 ----
    int sampleMax    = 0;
    int visibleCount = 0;
    for (const WaveSeries &w : data) {
        if (!w.visible)
            continue;
        sampleMax = qMax(sampleMax, w.values.size());
        if (!w.values.isEmpty())
            ++visibleCount;
    }

    // ---- 标题 ----
    const QString title = (visibleCount == 1)
        ? QStringLiteral("%1  ·  %2 个采样").arg(data.first().name).arg(sampleMax)
        : QStringLiteral("示波器  ·  %1 个通道  ·  %2 个采样")
              .arg(visibleCount).arg(sampleMax);

    p.setPen(kTitleText);
    p.drawText(QRectF(kPadL, kPadT, width() - kPadL - kPadR, kTitleH),
               Qt::AlignLeft | Qt::AlignVCenter, title);

    if (!m_follow) {
        p.setPen(kBackTag);
        p.drawText(QRectF(kPadL, kPadT, width() - kPadL - kPadR, kTitleH),
                   Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("◀ 回看中"));
        p.setPen(kTitleText);
    } else if (m_paused) {
        p.setPen(kPausedTag);
        p.drawText(QRectF(kPadL, kPadT, width() - kPadL - kPadR, kTitleH),
                   Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("II 已暂停"));
        p.setPen(kTitleText);
    }

    // ---- 布局 ----
    const Layout lay = computeLayout();
    if (lay.plot.width() < 20 || lay.plot.height() < 20)
        return;

    // ---- 左侧通道列表（两条以上才显示）----
    if (lay.listW > 0) {
        const QFontMetrics fm(p.font());
        for (int i = 0; i < m_series.size(); ++i) {
            const WaveSeries &w = m_series.at(i);
            const int y = lay.listTop + i * kRowH;
            if (y + kRowH > lay.plot.bottom())
                break;

            // 勾选框
            const QRectF cb(kPadL, y + 4.5, 11, 11);
            p.setPen(QPen(w.visible ? w.color : kAxisText, 1.2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(cb, 2, 2);
            if (w.visible) {
                p.setPen(QPen(w.color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.drawLine(QPointF(cb.left() + 2.5, cb.center().y()),
                           QPointF(cb.center().x() - 0.5, cb.bottom() - 3));
                p.drawLine(QPointF(cb.center().x() - 0.5, cb.bottom() - 3),
                           QPointF(cb.right() - 2.5, cb.top() + 3.5));
            }

            // 颜色块
            p.setPen(Qt::NoPen);
            p.setBrush(w.color);
            p.drawRoundedRect(QRectF(kPadL + 17, y + 6.5, 9, 7), 2, 2);

            // 名称
            p.setPen(w.visible ? kValueText : kAxisText);
            p.drawText(QRectF(kPadL + 30, y, kListW - 34, kRowH),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       fm.elidedText(w.name, Qt::ElideRight, kListW - 34));
        }

        // 列表与绘图区的分隔线
        p.setPen(QPen(kGrid, 1, Qt::SolidLine));
        p.drawLine(QPointF(kPadL + kListW - 8, lay.listTop),
                   QPointF(kPadL + kListW - 8, lay.plot.bottom()));
    }

    // ---- 时间范围 / 视窗 ----
    qint64 gLo = 0, gHi = 0;
    const bool hasTime = globalTimeRange(data, &gLo, &gHi);

    qint64 wLo = 0, wHi = 0;
    if (hasTime)
        resolveWindow(gLo, gHi, &wLo, &wHi);
    const double wSpan = double(wHi - wLo);

    // 每条通道在视窗内的索引区间（二分查找）
    const int nSeries = data.size();
    QVector<int> froms(nSeries, 0), tos(nSeries, 0);
    for (int k = 0; k < nSeries; ++k) {
        const WaveSeries &w = data.at(k);
        int from = 0, to = w.values.size();
        if (hasTime && w.times.size() == w.values.size() && wSpan > 0) {
            from = int(std::lower_bound(w.times.begin(), w.times.end(), wLo) - w.times.begin());
            to   = int(std::upper_bound(w.times.begin(), w.times.end(), wHi) - w.times.begin());
            from = qMax(0, from - 1);   // 多带一个点，线能连到边界外
            to   = qMin(w.values.size(), to + 1);
        }
        froms[k] = from;
        tos[k]   = to;
    }

    // ---- Y 值域（统一刻度时只统计视窗内的点）----
    const bool unified = effectiveUnified();
    double lo = 0.0, hi = 1.0;
    double realMin = 0.0, realMax = 0.0;
    if (unified) {
        bool first = true;
        for (int k = 0; k < nSeries; ++k) {
            const WaveSeries &w = data.at(k);
            if (!w.visible || w.values.isEmpty())
                continue;
            for (int i = froms[k]; i < tos[k]; ++i) {
                const double v = w.values.at(i);
                if (first) {
                    realMin = realMax = v;
                    first = false;
                } else {
                    realMin = qMin(realMin, v);
                    realMax = qMax(realMax, v);
                }
            }
        }
        lo = realMin;
        hi = realMax;
        if (hi - lo < 1e-9) {
            const double span = qMax(1.0, std::fabs(hi) * 0.1);
            lo = hi - span;
            hi = hi + span;
        } else {
            const double span = hi - lo;
            lo -= span * 0.10;
            hi += span * 0.10;
        }
    }

    // ---- 点状网格 + 边框 ----
    p.setPen(QPen(kGrid, 1, Qt::DotLine));
    for (int i = 0; i <= 4; ++i) {
        const double y = lay.plot.bottom() - (i / 4.0) * lay.plot.height();
        p.drawLine(QPointF(lay.plot.left(), y), QPointF(lay.plot.right(), y));
    }
    for (int i = 0; i <= 4; ++i) {
        const double x = lay.plot.left() + (i / 4.0) * lay.plot.width();
        p.drawLine(QPointF(x, lay.plot.top()), QPointF(x, lay.plot.bottom()));
    }

    p.setPen(QPen(kBorder, 1, Qt::SolidLine));
    p.setBrush(Qt::NoBrush);
    p.drawRect(lay.plot);

    // ---- Y 轴刻度 ----
    if (unified) {
        p.setPen(kAxisText);
        const int dec = decimalsFor(hi - lo);
        for (int i = 0; i <= 4; ++i) {
            const double y = lay.plot.bottom() - (i / 4.0) * lay.plot.height();
            // 顶/底两档文字夹在绘图区内，否则会被裁掉
            const double ty = qBound(lay.plot.top(), y - 8.0, lay.plot.bottom() - 16.0);
            p.drawText(QRectF(kPadL - 4, ty, lay.yAxisW - 4, 16),
                       Qt::AlignRight | Qt::AlignVCenter,
                       fmtNum(lo + (i / 4.0) * (hi - lo), dec));
        }
    }

    // ---- 曲线 ----
    bool anyData = false;
    for (int k = 0; k < nSeries; ++k) {
        const WaveSeries &w = data.at(k);
        if (!w.visible || w.values.isEmpty())
            continue;

        const int from = froms[k];
        const int to   = tos[k];
        if (to - from < 1)
            continue;
        anyData = true;

        // 各通道自己的值域（归一化模式 / 单通道也走这里）
        double a = lo, b = hi;
        if (!unified) {
            a = b = w.values.at(from);
            for (int i = from + 1; i < to; ++i) {
                a = qMin(a, w.values.at(i));
                b = qMax(b, w.values.at(i));
            }
            realMin = qMin(realMin, a);
            realMax = qMax(realMax, b);
            if (b - a < 1e-9) {
                const double span = qMax(1.0, std::fabs(b) * 0.1);
                a = b - span;
                b = b + span;
            } else {
                const double span = b - a;
                a -= span * 0.08;
                b += span * 0.08;
            }
        }

        // 坐标映射：优先按时间，没有时间信息就按索引铺满
        const bool timeAxis = hasTime && w.times.size() == w.values.size() && wSpan > 0;
        auto xOf = [&](int i) -> double {
            if (timeAxis)
                return lay.plot.left()
                       + (w.times.at(i) - wLo) / wSpan * lay.plot.width();
            const int n = w.values.size();
            return lay.plot.left()
                   + (n <= 1 ? lay.plot.width() / 2.0
                             : lay.plot.width() * i / double(n - 1));
        };
        auto yOf = [&](double v) -> double {
            return lay.plot.bottom() - (v - a) / (b - a) * lay.plot.height();
        };

        // 点数超过像素预算时按像素分桶取 min/max，保留尖峰
        const int n = to - from;
        const int budget = qMax(64, int(lay.plot.width() * 1.5));
        QVector<QPointF> pts;
        if (n <= budget) {
            pts.reserve(n);
            for (int i = from; i < to; ++i)
                pts.append(QPointF(xOf(i), yOf(w.values.at(i))));
        } else {
            const int buckets = qMax(16, int(lay.plot.width()));
            const int per     = qMax(1, n / buckets);
            pts.reserve(buckets * 2 + 4);
            for (int bi = 0; bi < buckets; ++bi) {
                const int i0 = from + bi * per;
                if (i0 >= to)
                    break;
                const int i1 = (bi == buckets - 1) ? to : qMin(to, i0 + per);

                int imin = i0, imax = i0;
                for (int i = i0 + 1; i < i1; ++i) {
                    const double v = w.values.at(i);
                    if (v < w.values.at(imin)) imin = i;
                    if (v > w.values.at(imax)) imax = i;
                }
                // 按索引顺序加，避免线条自我交叉
                if (imin <= imax) {
                    pts.append(QPointF(xOf(imin), yOf(w.values.at(imin))));
                    if (imax != imin)
                        pts.append(QPointF(xOf(imax), yOf(w.values.at(imax))));
                } else {
                    pts.append(QPointF(xOf(imax), yOf(w.values.at(imax))));
                    pts.append(QPointF(xOf(imin), yOf(w.values.at(imin))));
                }
            }
            // 补上最后一点，避免右端缺一截
            if (to - 1 >= from)
                pts.append(QPointF(xOf(to - 1), yOf(w.values.at(to - 1))));
        }

        if (pts.isEmpty())
            continue;

        QPainterPath path;
        path.moveTo(pts.first());
        for (int i = 1; i < pts.size(); ++i)
            path.lineTo(pts.at(i));

        if (pts.size() > 1) {
            QPainterPath area = path;
            area.lineTo(pts.last().x(), lay.plot.bottom());
            area.lineTo(pts.first().x(), lay.plot.bottom());
            area.closeSubpath();

            QLinearGradient grad(lay.plot.topLeft(), lay.plot.bottomLeft());
            QColor c1 = w.color;
            c1.setAlpha(unified ? 30 : 20);
            QColor c2 = w.color;
            c2.setAlpha(0);
            grad.setColorAt(0.0, c1);
            grad.setColorAt(1.0, c2);
            p.setPen(Qt::NoPen);
            p.setBrush(grad);
            p.drawPath(area);
        }

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(w.color, 1.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);

        // 末点 + 末端标注：只有末点落在视窗内才画
        const int lastIdx = to - 1;
        const bool lastVisible = !timeAxis || (w.times.at(lastIdx) <= wHi);
        if (lastVisible) {
            const QPointF last = QPointF(xOf(lastIdx), yOf(w.values.at(lastIdx)));
            p.setPen(Qt::NoPen);
            p.setBrush(w.color);
            p.drawEllipse(last, 2.6, 2.6);

            if (visibleCount > 1) {
                const QFontMetrics fm(p.font());
                QString tag = w.name;
                if (tag.size() > 18)
                    tag = tag.left(17) + QChar(0x2026);
                const int tw = fm.horizontalAdvance(tag) + 4;
                const bool right = (last.x() + 8 + tw < lay.plot.right());
                const QRectF tr(right ? last.x() + 6 : last.x() - 6 - tw,
                                qBound(lay.plot.top(), last.y() - 8, lay.plot.bottom() - 14),
                                tw, 14);
                p.setPen(w.color);
                p.drawText(tr, right ? Qt::AlignLeft | Qt::AlignVCenter
                                     : Qt::AlignRight | Qt::AlignVCenter, tag);
            }
        }
    }

    if (!anyData) {
        p.setPen(kHintText);
        p.drawText(lay.plot, Qt::AlignCenter,
                   QStringLiteral("等待数据…（串口未连接，或轮询还没启动）"));
        return;
    }

    // ---- 底部左：每格时间 + 跨度 ----
    QString foot;
    if (hasTime && wSpan > 0) {
        foot = QStringLiteral("%1/div  ·  跨度 %2")
                   .arg(fmtSpan(wSpan / 4.0)).arg(fmtSpan(wSpan));
    }
    if (unified && realMax > realMin) {
        foot += QStringLiteral("  ·  min %1  max %2").arg(fmtNum(realMin)).arg(fmtNum(realMax));
    }

    const QRectF footRect(lay.plot.left(), lay.plot.bottom() + 2,
                          lay.plot.width(), kFootH);
    p.setPen(kAxisText);
    p.drawText(footRect, Qt::AlignLeft | Qt::AlignVCenter, foot);

    // ---- 底部右：回看时间范围 / 当前值 ----
    if (!m_follow && hasTime) {
        const QString rng = QStringLiteral("%1 → %2  (双击回到最新)")
            .arg(QDateTime::fromMSecsSinceEpoch(wLo).toString(QStringLiteral("HH:mm:ss")))
            .arg(QDateTime::fromMSecsSinceEpoch(wHi).toString(QStringLiteral("HH:mm:ss")));
        p.setPen(kBackTag);
        p.drawText(footRect, Qt::AlignRight | Qt::AlignVCenter, rng);
    } else if (visibleCount == 1) {
        for (const WaveSeries &w : data) {
            if (!w.visible || w.values.isEmpty())
                continue;
            QString cur = fmtNum(w.values.last());
            if (!w.unit.isEmpty())
                cur += QLatin1Char(' ') + w.unit;
            p.setPen(kValueText);
            p.drawText(footRect, Qt::AlignRight | Qt::AlignVCenter, cur);
        }
    } else {
        p.setPen(kAxisText);
        p.drawText(footRect, Qt::AlignRight | Qt::AlignVCenter,
                   m_follow ? QStringLiteral("● 实时") : QStringLiteral("回看"));
    }

    // ---- 时间轴滑块 ----
    if (!hasTime || gHi <= gLo || lay.slider.width() < 40)
        return;

    const double total = double(gHi - gLo);
    const double barH  = 6.0;
    const QRectF bar(lay.slider.left(), lay.slider.center().y() - barH / 2.0,
                     lay.slider.width(), barH);

    p.setPen(Qt::NoPen);
    p.setBrush(kTrackBg);
    p.drawRoundedRect(bar, 3, 3);

    double r0 = (double(wLo) - double(gLo)) / total;
    double r1 = (double(wHi) - double(gLo)) / total;
    r0 = qBound(0.0, r0, 1.0);
    r1 = qBound(0.0, r1, 1.0);

    QRectF handle(bar.left() + r0 * bar.width(), bar.top(),
                  qMax(10.0, (r1 - r0) * bar.width()), bar.height());
    if (handle.right() > bar.right())
        handle.moveRight(bar.right());
    if (handle.left() < bar.left())
        handle.moveLeft(bar.left());

    p.setBrush(m_drag == Drag::Slider ? kHandleDrag : kHandle);
    p.drawRoundedRect(handle, 3, 3);
}

// ---------------------------------------------------------------- 鼠标交互

void WaveChart::mousePressEvent(QMouseEvent *event)
{
    const Layout lay = computeLayout();

    // 1) 左侧通道列表：点勾选框切换显示
    if (m_series.size() > 1 && event->pos().x() <= kPadL + kListW
        && event->pos().y() >= lay.listTop) {
        const int idx = (event->pos().y() - lay.listTop) / kRowH;
        if (idx >= 0 && idx < m_series.size()) {
            const bool nowVisible = !m_series.at(idx).visible;
            m_series[idx].visible = nowVisible;
            if (m_paused && idx < m_frozen.size())
                m_frozen[idx].visible = nowVisible;   // 暂停时改快照，画面才跟着变
            update();
        }
        return;
    }

    if (event->button() != Qt::LeftButton)
        return;

    // 记录拖动起点（滑块和绘图区共用）
    const QList<WaveSeries> &data = (m_paused && !m_frozen.isEmpty()) ? m_frozen : m_series;
    qint64 gLo = 0, gHi = 0;
    if (!globalTimeRange(data, &gLo, &gHi))
        return;

    qint64 wLo = 0, wHi = 0;
    resolveWindow(gLo, gHi, &wLo, &wHi);

    m_dragPos  = event->pos();
    m_dragEnd  = wHi;
    m_dragSpan = double(qMax<qint64>(1, wHi - wLo));

    // 2) 时间轴滑块：拖动移动视窗
    const QRectF sliderHit = lay.slider.adjusted(-4, -6, 4, 6);
    if (sliderHit.contains(event->pos())) {
        m_drag = Drag::Slider;
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    // 3) 绘图区：拖动平移
    if (lay.plot.adjusted(-2, -2, 2, 2).contains(event->pos())) {
        m_drag = Drag::Plot;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
}

void WaveChart::mouseMoveEvent(QMouseEvent *event)
{
    const Layout lay = computeLayout();

    if (m_drag == Drag::None) {
        // 只更新光标
        if (lay.slider.adjusted(-4, -6, 4, 6).contains(event->pos()))
            setCursor(Qt::PointingHandCursor);
        else if (lay.plot.contains(event->pos()))
            setCursor(Qt::CrossCursor);
        else if (event->pos().x() <= kPadL + kListW && m_series.size() > 1)
            setCursor(Qt::PointingHandCursor);
        else
            unsetCursor();
        return;
    }

    const QList<WaveSeries> &data = (m_paused && !m_frozen.isEmpty()) ? m_frozen : m_series;
    qint64 gLo = 0, gHi = 0;
    if (!globalTimeRange(data, &gLo, &gHi) || gHi <= gLo)
        return;

    const int dx = event->pos().x() - m_dragPos.x();

    if (m_drag == Drag::Slider) {
        if (lay.slider.width() <= 0)
            return;
        const double dt = dx / lay.slider.width() * double(gHi - gLo);
        setViewEnd(qint64(m_dragEnd + dt), gLo, gHi);
    } else {
        // 绘图区：往右拖 = 看更早的数据
        if (lay.plot.width() <= 0)
            return;
        const double dt = -dx / lay.plot.width() * m_dragSpan;
        setViewEnd(qint64(m_dragEnd + dt), gLo, gHi);
    }
}

void WaveChart::mouseReleaseEvent(QMouseEvent *)
{
    if (m_drag == Drag::None)
        return;
    m_drag = Drag::None;
    unsetCursor();
    update();
}

void WaveChart::mouseDoubleClickEvent(QMouseEvent *)
{
    followLatest();
}

void WaveChart::wheelEvent(QWheelEvent *event)
{
    const QList<WaveSeries> &data = (m_paused && !m_frozen.isEmpty()) ? m_frozen : m_series;

    qint64 gLo = 0, gHi = 0;
    if (!globalTimeRange(data, &gLo, &gHi)) {
        event->ignore();
        return;
    }

    const Layout lay = computeLayout();
    if (!lay.plot.contains(event->pos()) || lay.plot.width() <= 0) {
        event->ignore();
        return;
    }

    const double steps = event->angleDelta().y() / 120.0;
    if (qFuzzyIsNull(steps)) {
        event->ignore();
        return;
    }

    qint64 wLo = 0, wHi = 0;
    resolveWindow(gLo, gHi, &wLo, &wHi);
    const double span = double(qMax<qint64>(1, wHi - wLo));

    // 鼠标位置作为缩放锚点
    const double ratio  = qBound(0.0, (event->pos().x() - lay.plot.left()) / lay.plot.width(), 1.0);
    const qint64 anchor = wLo + qint64(ratio * span);

    const double factor  = std::pow(0.80, steps);   // 上滚放大
    double       newSpan = span * factor;
    newSpan = qBound(kMinSpanMs, newSpan, double(gHi - gLo) * 1.02);

    const qint64 newLo = anchor - qint64(ratio * newSpan);

    m_viewSpan = newSpan;
    m_zoomSet  = true;
    setViewEnd(newLo + qint64(newSpan), gLo, gHi);
    event->accept();
}

} // namespace busscope
