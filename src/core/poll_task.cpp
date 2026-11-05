#include "core/poll_task.h"

#include <QDateTime>

#include <QStringList>

#include <cmath>
#include <cstring>

namespace busscope {

int registersForType(ValueType type)
{
    switch (type) {
    case ValueType::Int32:
    case ValueType::UInt32:
    case ValueType::Float:
        return 2;
    default:
        return 1;
    }
}

QString valueTypeName(ValueType type)
{
    switch (type) {
    case ValueType::Bool:   return QStringLiteral("bool");
    case ValueType::Int16:  return QStringLiteral("int16");
    case ValueType::UInt16: return QStringLiteral("uint16");
    case ValueType::Int32:  return QStringLiteral("int32");
    case ValueType::UInt32: return QStringLiteral("uint32");
    case ValueType::Float:  return QStringLiteral("float");
    }
    return QStringLiteral("uint16");
}

ValueType valueTypeFromName(const QString &name)
{
    const QString n = name.trimmed().toLower();
    if (n == QLatin1String("bool") || n == QLatin1String("bit"))
        return ValueType::Bool;
    if (n == QLatin1String("int16") || n == QLatin1String("s16"))
        return ValueType::Int16;
    if (n == QLatin1String("int32") || n == QLatin1String("s32"))
        return ValueType::Int32;
    if (n == QLatin1String("uint32") || n == QLatin1String("u32"))
        return ValueType::UInt32;
    if (n == QLatin1String("float") || n == QLatin1String("fp32"))
        return ValueType::Float;
    return ValueType::UInt16;
}

bool PollTask::numericValue(const QVector<quint16> &src, double *out) const
{
    if (src.isEmpty() || !out)
        return false;

    double value = 0.0;
    switch (type) {
    case ValueType::Bool:
        value = src.at(0) ? 1.0 : 0.0;
        break;

    case ValueType::Int16:
        value = static_cast<qint16>(src.at(0));
        break;

    case ValueType::UInt16:
        value = src.at(0);
        break;

    case ValueType::Int32:
    case ValueType::UInt32:
    case ValueType::Float: {
        if (src.size() < 2)
            return false;
        const quint32 word = (static_cast<quint32>(src.at(0)) << 16) | src.at(1);
        if (type == ValueType::Int32) {
            value = static_cast<qint32>(word);
        } else if (type == ValueType::UInt32) {
            value = word;
        } else {
            float f = 0.0f;
            std::memcpy(&f, &word, sizeof(f));
            value = f;
        }
        break;
    }
    }

    *out = value * scale;
    return true;
}

void PollTask::pushHistory(qreal value, qint64 tMs)
{
    WaveSample smp;
    smp.tMs   = (tMs > 0) ? tMs : QDateTime::currentMSecsSinceEpoch();
    smp.value = value;
    history.append(smp);
    if (history.size() > kHistoryMax)
        history.remove(0, history.size() - kHistoryMax);
}

QString formatValue(const PollTask &task, const QVector<quint16> &raw)
{
    if (raw.isEmpty())
        return QStringLiteral("--");

    if (task.type == ValueType::Bool)
        return raw.at(0) ? QStringLiteral("ON") : QStringLiteral("OFF");

    double value = 0.0;
    if (!task.numericValue(raw, &value))
        return QStringLiteral("--");

    int decimals = 0;
    if (task.type == ValueType::Float) {
        decimals = 2;
    } else if (task.scale > 0.0 && task.scale < 1.0) {
        decimals = qBound(0, static_cast<int>(std::ceil(-std::log10(task.scale))), 4);
    }

    QString text = QString::number(value, 'f', decimals);
    if (!task.unit.isEmpty())
        text += QLatin1Char(' ') + task.unit;
    return text;
}

} // namespace busscope
