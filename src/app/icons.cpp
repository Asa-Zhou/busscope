#include "app/icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace busscope {
namespace icons {

namespace {

const int   kSize = 16;
const qreal kDpr  = 2.0;   // 按 2 倍画，HiDPI 下不糊

QPixmap blank()
{
    QPixmap p(static_cast<int>(kSize * kDpr), static_cast<int>(kSize * kDpr));
    p.setDevicePixelRatio(kDpr);
    p.fill(Qt::transparent);
    return p;
}

QPen stroke(const QColor &c, qreal width = 1.4)
{
    return QPen(c, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

} // namespace

QIcon refresh(const QColor &c)
{
    QPixmap p = blank();
    QPainter g(&p);
    g.setRenderHint(QPainter::Antialiasing);
    g.setPen(stroke(c));
    g.drawArc(QRectF(3.2, 3.2, 9.6, 9.6), 40 * 16, 280 * 16);

    QPainterPath head;
    head.moveTo(12.4, 2.6);
    head.lineTo(13.0, 6.2);
    head.lineTo(9.4, 5.4);
    head.closeSubpath();
    g.setBrush(c);
    g.setPen(Qt::NoPen);
    g.drawPath(head);
    return QIcon(p);
}

QIcon trash(const QColor &c)
{
    QPixmap p = blank();
    QPainter g(&p);
    g.setRenderHint(QPainter::Antialiasing);
    g.setPen(stroke(c, 1.3));

    g.drawLine(QPointF(2.8, 4.6), QPointF(13.2, 4.6));
    g.drawLine(QPointF(6.4, 4.6), QPointF(6.4, 2.6));
    g.drawLine(QPointF(6.4, 2.6), QPointF(9.6, 2.6));
    g.drawLine(QPointF(9.6, 2.6), QPointF(9.6, 4.6));

    QPainterPath body;
    body.moveTo(4.6, 4.6);
    body.lineTo(5.4, 13.2);
    body.lineTo(10.6, 13.2);
    body.lineTo(11.4, 4.6);
    g.drawPath(body);

    g.drawLine(QPointF(6.9, 6.8), QPointF(7.1, 11.2));
    g.drawLine(QPointF(9.1, 6.8), QPointF(8.9, 11.2));
    return QIcon(p);
}

namespace {

void drawTray(QPainter &g)
{
    g.drawLine(QPointF(2.8, 10.4), QPointF(2.8, 13.4));
    g.drawLine(QPointF(2.8, 13.4), QPointF(13.2, 13.4));
    g.drawLine(QPointF(13.2, 13.4), QPointF(13.2, 10.4));
}

} // namespace

QIcon exportTray(const QColor &c)
{
    QPixmap p = blank();
    QPainter g(&p);
    g.setRenderHint(QPainter::Antialiasing);
    g.setPen(stroke(c, 1.3));

    g.drawLine(QPointF(8, 9.4), QPointF(8, 2.4));
    g.drawLine(QPointF(5.2, 5.2), QPointF(8, 2.4));
    g.drawLine(QPointF(10.8, 5.2), QPointF(8, 2.4));
    drawTray(g);
    return QIcon(p);
}

QIcon importTray(const QColor &c)
{
    QPixmap p = blank();
    QPainter g(&p);
    g.setRenderHint(QPainter::Antialiasing);
    g.setPen(stroke(c, 1.3));

    g.drawLine(QPointF(8, 2.4), QPointF(8, 9.4));
    g.drawLine(QPointF(5.2, 6.6), QPointF(8, 9.4));
    g.drawLine(QPointF(10.8, 6.6), QPointF(8, 9.4));
    drawTray(g);
    return QIcon(p);
}

QIcon power(const QColor &c)
{
    QPixmap p = blank();
    QPainter g(&p);
    g.setRenderHint(QPainter::Antialiasing);
    g.setPen(stroke(c, 1.5));
    g.drawArc(QRectF(3.2, 3.4, 9.6, 9.6), 60 * 16, 300 * 16);
    g.drawLine(QPointF(8, 1.8), QPointF(8, 7.2));
    return QIcon(p);
}

QIcon send(const QColor &c)
{
    QPixmap p = blank();
    QPainter g(&p);
    g.setRenderHint(QPainter::Antialiasing);

    QPainterPath path;
    path.moveTo(2.4, 8.0);
    path.lineTo(13.6, 2.8);
    path.lineTo(9.8, 13.2);
    path.lineTo(8.1, 9.0);
    path.closeSubpath();

    g.setPen(QPen(c, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    g.setBrush(c);
    g.drawPath(path);
    return QIcon(p);
}

QIcon play(const QColor &c)
{
    QPixmap p = blank();
    QPainter g(&p);
    g.setRenderHint(QPainter::Antialiasing);

    QPainterPath path;
    path.moveTo(4.6, 2.8);
    path.lineTo(13.2, 8.0);
    path.lineTo(4.6, 13.2);
    path.closeSubpath();

    g.setPen(Qt::NoPen);
    g.setBrush(c);
    g.drawPath(path);
    return QIcon(p);
}

QIcon stop(const QColor &c)
{
    QPixmap p = blank();
    QPainter g(&p);
    g.setRenderHint(QPainter::Antialiasing);
    g.setPen(Qt::NoPen);
    g.setBrush(c);
    g.drawRoundedRect(QRectF(4.0, 4.0, 8.0, 8.0), 1.2, 1.2);
    return QIcon(p);
}

QIcon pause(const QColor &c)
{
    QPixmap p = blank();
    QPainter g(&p);
    g.setRenderHint(QPainter::Antialiasing);
    g.setPen(Qt::NoPen);
    g.setBrush(c);
    g.drawRoundedRect(QRectF(4.6, 3.2, 2.4, 9.6), 1.0, 1.0);
    g.drawRoundedRect(QRectF(9.0, 3.2, 2.4, 9.6), 1.0, 1.0);
    return QIcon(p);
}

QIcon folder(const QColor &c)
{
    QPixmap p = blank();
    QPainter g(&p);
    g.setRenderHint(QPainter::Antialiasing);
    g.setPen(stroke(c, 1.3));

    QPainterPath path;
    path.moveTo(2.4, 12.8);
    path.lineTo(2.4, 4.4);
    path.lineTo(6.4, 4.4);
    path.lineTo(7.6, 6.0);
    path.lineTo(13.6, 6.0);
    path.lineTo(13.6, 12.8);
    path.closeSubpath();
    g.drawPath(path);
    return QIcon(p);
}

QIcon app()
{
    return QIcon(QStringLiteral(":/icons/busscope.ico"));
}

QIcon gauge(const QColor &c)
{
    QPixmap p = blank();
    QPainter g(&p);
    g.setRenderHint(QPainter::Antialiasing);
    g.setPen(stroke(c));
    g.setBrush(Qt::NoBrush);

    // 半圆表盘
    g.drawArc(QRectF(2.6, 4.0, 10.8, 10.8), 0, 180 * 16);
    // 底部横线
    g.drawLine(QPointF(2.6, 14.4), QPointF(13.4, 14.4));
    // 指针
    g.drawLine(QPointF(8.0, 14.4), QPointF(11.2, 8.4));
    // 轴心
    g.setBrush(c);
    g.setPen(Qt::NoPen);
    g.drawEllipse(QPointF(8.0, 14.4), 1.1, 1.1);
    return QIcon(p);
}

QIcon bus(const QColor &c)
{
    QPixmap p = blank();
    QPainter g(&p);
    g.setRenderHint(QPainter::Antialiasing);
    g.setPen(stroke(c));
    g.setBrush(Qt::NoBrush);

    // 两条总线
    g.drawLine(QPointF(2.0, 5.6), QPointF(14.0, 5.6));
    g.drawLine(QPointF(2.0, 10.4), QPointF(14.0, 10.4));

    // 三个节点，压在两条线之间
    g.setBrush(c);
    g.setPen(Qt::NoPen);
    for (double x : { 4.2, 8.0, 11.8 })
        g.drawEllipse(QPointF(x, 8.0), 1.5, 1.5);

    return QIcon(p);
}

} // namespace icons
} // namespace busscope
