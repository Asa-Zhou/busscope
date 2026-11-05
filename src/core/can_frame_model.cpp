#include "core/can_frame_model.h"

#include <QBrush>
#include <QColor>
#include <QDateTime>

namespace busscope {

namespace {

const QColor kTextPrimary(0xE8, 0xE8, 0xEA);
const QColor kTextSecondary(0x9A, 0x9B, 0xA0);
const QColor kTextMuted(0x7A, 0x7B, 0x80);
const QColor kTx(0x8B, 0x93, 0xF8);
const QColor kRx(0x4E, 0xC9, 0xB0);
const QColor kError(0xF8, 0x51, 0x49);

} // namespace

CanFrameModel::CanFrameModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int CanFrameModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_frames.size();
}

int CanFrameModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

void CanFrameModel::clear()
{
    beginResetModel();
    m_frames.clear();
    endResetModel();
}

void CanFrameModel::append(const CanFrame &frame)
{
    if (m_frames.size() >= m_capacity) {
        const int drop = m_capacity / 10;
        beginRemoveRows(QModelIndex(), 0, drop - 1);
        m_frames.remove(0, drop);
        endRemoveRows();
    }

    const int row = m_frames.size();
    beginInsertRows(QModelIndex(), row, row);
    m_frames.append(frame);
    endInsertRows();
}

void CanFrameModel::setDatabase(const DbcDatabase *db)
{
    m_db = db;
    refreshDecoded();
}

void CanFrameModel::refreshDecoded()
{
    if (m_frames.isEmpty())
        return;
    emit dataChanged(index(0, ColDecoded),
                     index(m_frames.size() - 1, ColDecoded),
                     { Qt::DisplayRole });
}

const CanFrame *CanFrameModel::frameAt(int row) const
{
    if (row < 0 || row >= m_frames.size())
        return nullptr;
    return &m_frames.at(row);
}

QVariant CanFrameModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_frames.size())
        return QVariant();

    const CanFrame &f = m_frames.at(index.row());
    const int col = index.column();

    if (role == Qt::DisplayRole) {
        switch (col) {
        case ColTime:
            return QDateTime::fromMSecsSinceEpoch(f.timestampMs)
                .toString(QStringLiteral("HH:mm:ss.zzz"));
        case ColDir:
            return f.tx ? QStringLiteral("TX") : QStringLiteral("RX");
        case ColId:
            return QStringLiteral("0x") + QString::number(f.id, 16).toUpper();
        case ColDecoded: {
            if (!m_db || f.error || f.remote || f.data.isEmpty())
                return QString();
            return m_db->decode(f.id, f.extended, f.data);
        }
        case ColType:
            if (f.error)
                return QStringLiteral("错误帧");
            if (f.remote)
                return QStringLiteral("远程帧");
            return QStringLiteral("数据帧");
        case ColDlc:
            return QString::number(f.data.size());
        case ColData:
            return QString::fromLatin1(f.data.toHex(' ').toUpper());
        default:
            break;
        }
    }

    if (role == Qt::ForegroundRole) {
        if (f.error)
            return QBrush(kError);
        switch (col) {
        case ColTime: return QBrush(kTextMuted);
        case ColDir:  return QBrush(f.tx ? kTx : kRx);
        case ColId:   return QBrush(QColor(0xB5, 0xCE, 0xA8));
        case ColType: return QBrush(kTextSecondary);
        case ColDlc:  return QBrush(kTextSecondary);
        case ColData: return QBrush(QColor(0xCC, 0xCC, 0xCC));
        default:      break;
        }
    }

    if (role == Qt::TextAlignmentRole) {
        if (col == ColTime || col == ColData)
            return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
        return QVariant(Qt::AlignCenter);
    }

    return QVariant();
}

QVariant CanFrameModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal)
        return QVariant();

    if (role == Qt::TextAlignmentRole) {
        if (section == ColTime || section == ColData || section == ColDecoded)
            return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
        return QVariant(Qt::AlignCenter);
    }

    if (role != Qt::DisplayRole)
        return QVariant();

    switch (section) {
    case ColTime: return QStringLiteral("时间");
    case ColDir:  return QStringLiteral("方向");
    case ColId:   return QStringLiteral("ID");
    case ColType: return QStringLiteral("帧类型");
    case ColDlc:  return QStringLiteral("DLC");
    case ColData: return QStringLiteral("数据 (HEX)");
    case ColDecoded: return QStringLiteral("解析（DBC）");
    default:      return QVariant();
    }
}

} // namespace busscope
