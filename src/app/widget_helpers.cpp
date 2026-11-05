#include "app/widget_helpers.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QSize>

#include <cctype>

namespace busscope {

QLabel *sectionTitle(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("SectionTitle"));
    return label;
}

QWidget *fieldRow(const QString &label, QWidget *field)
{
    auto *row = new QWidget;
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(8);

    auto *l = new QLabel(label);
    l->setObjectName(QStringLiteral("FieldLabel"));
    l->setFixedWidth(40);
    h->addWidget(l);
    h->addWidget(field, 1);
    return row;
}

QFrame *cardFrame()
{
    auto *frame = new QFrame;
    frame->setObjectName(QStringLiteral("Card"));
    return frame;
}

QFrame *hairLine()
{
    auto *line = new QFrame;
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background:#232428;border:none;"));
    return line;
}

void applyIcon(QPushButton *button, const QIcon &icon)
{
    button->setIcon(icon);
    button->setIconSize(QSize(14, 14));
}

QByteArray hexToBytes(const QString &text, bool *ok)
{
    QString hex;
    for (const QChar c : text) {
        if (c.isSpace() || c == QLatin1Char(',') || c == QLatin1Char('-') || c == QLatin1Char(':'))
            continue;
        if (!std::isxdigit(static_cast<unsigned char>(c.toLatin1()))) {
            *ok = false;
            return QByteArray();
        }
        hex.append(c);
    }
    if (hex.isEmpty()) {
        *ok = true;
        return QByteArray();
    }
    if (hex.size() % 2 != 0) {
        *ok = false;
        return QByteArray();
    }
    *ok = true;
    return QByteArray::fromHex(hex.toLatin1());
}

} // namespace busscope
