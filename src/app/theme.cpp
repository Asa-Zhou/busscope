#include "app/theme.h"

#include <QApplication>
#include <QFile>
#include <QPalette>
#include <QStyleFactory>

namespace busscope {
namespace theme {

namespace {

QPalette darkPalette()
{
    QPalette p;
    p.setColor(QPalette::Window, canvas());
    p.setColor(QPalette::WindowText, textPrimary());
    p.setColor(QPalette::Base, QColor(0x0D, 0x0E, 0x10));
    p.setColor(QPalette::AlternateBase, QColor(0x0F, 0x10, 0x13));
    p.setColor(QPalette::Text, textPrimary());
    p.setColor(QPalette::Button, card());
    p.setColor(QPalette::ButtonText, textPrimary());
    p.setColor(QPalette::Highlight, accent());
    p.setColor(QPalette::HighlightedText, QColor(0xFF, 0xFF, 0xFF));
    p.setColor(QPalette::ToolTipBase, card());
    p.setColor(QPalette::ToolTipText, textPrimary());
    p.setColor(QPalette::Disabled, QPalette::Text, textMuted());
    p.setColor(QPalette::Disabled, QPalette::ButtonText, textMuted());
    return p;
}

} // namespace

void apply(QApplication &app)
{
    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        app.setStyle(fusion);

    app.setPalette(darkPalette());

    QFile f(QStringLiteral(":/theme/dark.qss"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text))
        app.setStyleSheet(QString::fromUtf8(f.readAll()));
}

} // namespace theme
} // namespace busscope
