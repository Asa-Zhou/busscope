#pragma once

#include <QByteArray>
#include <QString>

class QFrame;
class QIcon;
class QLabel;
class QPushButton;
class QWidget;

namespace busscope {

/// 各页面共用的界面小构件：卡片、标题行、字段行。
/// 只做纯装配，不含任何业务逻辑。
QLabel *sectionTitle(const QString &text);
QWidget *fieldRow(const QString &label, QWidget *field);
QFrame *cardFrame();
QFrame *hairLine();
void applyIcon(QPushButton *button, const QIcon &icon);

/// 把 "01 03 00 0A" / "0103000a" 这类文本转成字节。空串视为合法空数据。
QByteArray hexToBytes(const QString &text, bool *ok);

} // namespace busscope
