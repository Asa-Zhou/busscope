#pragma once

#include <QColor>
#include <QIcon>

namespace busscope {
namespace icons {

// 界面图标全部用 QPainter 现画：不引 QtSvg、不放 PNG，
// 换主题色只要把颜色传进来，深色/浅色都是一套代码。
QIcon refresh(const QColor &c);
QIcon trash(const QColor &c);
QIcon exportTray(const QColor &c);
QIcon importTray(const QColor &c);
QIcon power(const QColor &c);
QIcon send(const QColor &c);
QIcon play(const QColor &c);
QIcon stop(const QColor &c);
QIcon pause(const QColor &c);
QIcon folder(const QColor &c);
QIcon gauge(const QColor &c);   ///< Modbus：半圆仪表
QIcon bus(const QColor &c);     ///< CAN：双绞线 + 节点

/// 应用图标（窗口 / 任务栏），来自资源里的 .ico
QIcon app();

} // namespace icons
} // namespace busscope
