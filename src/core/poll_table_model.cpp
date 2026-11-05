#include "core/poll_table_model.h"

#include <QBrush>
#include <QColor>

#include "io/poll_engine.h"

namespace busscope {

namespace {

const QColor kTextPrimary(0xE8, 0xE8, 0xEA);
const QColor kTextSecondary(0x9A, 0x9B, 0xA0);
const QColor kTextMuted(0x7A, 0x7B, 0x80);
const QColor kOk(0x4E, 0xC9, 0xB0);
const QColor kError(0xF8, 0x51, 0x49);
const QColor kWarning(0xDC, 0xDC, 0xAA);

QString functionShortName(quint8 fc)
{
    switch (fc) {
    case 0x01: return QStringLiteral("01 线圈");
    case 0x02: return QStringLiteral("02 离散");
    case 0x03: return QStringLiteral("03 保持");
    case 0x04: return QStringLiteral("04 输入");
    default:   return QString::number(fc);
    }
}

} // namespace

PollTableModel::PollTableModel(PollEngine *engine, QObject *parent)
    : QAbstractTableModel(parent)
    , m_engine(engine)
{
}

int PollTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_engine)
        return 0;
    return m_engine->taskCount();
}

int PollTableModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return ColumnCount;
}

QVariant PollTableModel::data(const QModelIndex &index, int role) const
{
    if (!m_engine || !index.isValid())
        return QVariant();

    const PollTask *t = m_engine->taskAt(index.row());
    if (!t)
        return QVariant();

    const int col = index.column();

    if (role == Qt::CheckStateRole && col == ColEnable)
        return t->enabled ? Qt::Checked : Qt::Unchecked;

    if (role == Qt::DisplayRole) {
        switch (col) {
        case ColName:     return t->name;
        case ColSlave:    return QString::number(t->slave);
        case ColFunction: return functionShortName(t->function);
        case ColAddr:     return QString::number(t->addr);
        case ColPeriod:   return QStringLiteral("%1 ms").arg(t->periodMs);
        case ColValue:    return t->valueText.isEmpty() ? QStringLiteral("--") : t->valueText;
        case ColStatus:   return t->status.isEmpty() ? QStringLiteral("未执行") : t->status;
        case ColCount:    return QStringLiteral("%1 / %2").arg(t->okCount).arg(t->errCount);
        default:          break;
        }
    }

    if (role == Qt::ForegroundRole) {
        switch (col) {
        case ColName:   return QBrush(kTextPrimary);
        case ColSlave:
        case ColFunction:
        case ColAddr:
        case ColPeriod: return QBrush(kTextSecondary);
        case ColValue:  return QBrush(kTextPrimary);
        case ColStatus:
            if (t->status == QLatin1String("正常"))
                return QBrush(kOk);
            if (t->status.startsWith(QLatin1String("异常")))
                return QBrush(kError);
            if (t->status == QLatin1String("未执行"))
                return QBrush(kTextMuted);
            return QBrush(kWarning);
        case ColCount:  return QBrush(kTextMuted);
        default:        break;
        }
    }

    if (role == Qt::TextAlignmentRole) {
        if (col == ColValue)
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        if (col != ColName && col != ColStatus)
            return QVariant(Qt::AlignCenter);
        return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
    }

    if (role == Qt::ToolTipRole) {
        return QStringLiteral("%1\n类型 %2 × 系数 %3\n超时 %4 ms · 重试 %5 次")
            .arg(t->name, valueTypeName(t->type))
            .arg(t->scale)
            .arg(t->timeoutMs)
            .arg(t->retries);
    }

    return QVariant();
}

QVariant PollTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal)
        return QVariant();

    // 表头与单元格保持同一套对齐规则，否则看起来像列错位
    if (role == Qt::TextAlignmentRole) {
        if (section == ColName || section == ColStatus)
            return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
        return QVariant(Qt::AlignCenter);
    }

    if (role != Qt::DisplayRole)
        return QVariant();

    switch (section) {
    case ColEnable:   return QStringLiteral("启用");
    case ColName:     return QStringLiteral("名称");
    case ColSlave:    return QStringLiteral("从站");
    case ColFunction: return QStringLiteral("功能");
    case ColAddr:     return QStringLiteral("地址");
    case ColPeriod:   return QStringLiteral("周期");
    case ColValue:    return QStringLiteral("当前值");
    case ColStatus:   return QStringLiteral("状态");
    case ColCount:    return QStringLiteral("成功/失败");
    default:          return QVariant();
    }
}

bool PollTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!m_engine || !index.isValid() || index.column() != ColEnable || role != Qt::CheckStateRole)
        return false;

    PollTask *t = m_engine->taskAt(index.row());
    if (!t)
        return false;

    t->enabled = (value.toInt() == Qt::Checked);
    emit dataChanged(index, index, QVector<int>() << Qt::CheckStateRole);
    return true;
}

Qt::ItemFlags PollTableModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (index.column() == ColEnable)
        f |= Qt::ItemIsUserCheckable;
    return f;
}

void PollTableModel::notifyRow(int row)
{
    if (row < 0 || row >= rowCount())
        return;
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
}

void PollTableModel::notifyAll()
{
    beginResetModel();
    endResetModel();
}

} // namespace busscope
