#pragma once

#include <QColor>
#include <QString>

class QApplication;

namespace busscope {
namespace theme {

// ---- 设计令牌（与 Ardot 设计稿 BusScope 一致）----
inline QColor canvas()      { return QColor(0x0B, 0x0C, 0x0E); }
inline QColor panel()       { return QColor(0x10, 0x11, 0x13); }
inline QColor card()        { return QColor(0x15, 0x16, 0x1A); }
inline QColor border()      { return QColor(0x1D, 0x1E, 0x22); }
inline QColor textPrimary() { return QColor(0xE8, 0xE8, 0xEA); }
inline QColor textSecondary(){ return QColor(0x9A, 0x9B, 0xA0); }
inline QColor textMuted()   { return QColor(0x6A, 0x6B, 0x70); }
inline QColor accent()      { return QColor(0x5E, 0x6A, 0xD2); }
inline QColor tx()          { return QColor(0x8B, 0x93, 0xF8); }
inline QColor rx()          { return QColor(0x4E, 0xC9, 0xB0); }
inline QColor error()       { return QColor(0xF8, 0x51, 0x49); }
inline QColor warning()     { return QColor(0xDC, 0xDC, 0xAA); }

/// 应用 Fusion 风格 + 深色调色板 + 资源内 QSS。
void apply(QApplication &app);

} // namespace theme
} // namespace busscope
