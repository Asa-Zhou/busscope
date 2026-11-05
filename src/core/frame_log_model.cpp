#include "core/frame_log_model.h"

#include <QBrush>
#include <QColor>
#include <QDateTime>

namespace busscope {

namespace {

// 与设计稿 BusScope 深色主题一致的语义色
const QColor kTextPrimary   (0xE8, 0xE8, 0xEA);
const QColor kTextSecondary (0x9A, 0x9B, 0xA0);
const QColor kTextMuted     (0x7A, 0x7B, 0x80);
const QColor kTx            (0x8B, 0x93, 0xF8);
const QColor kRx            (0x4E, 0xC9, 0xB0);
const QColor kError         (0xF8, 0x51, 0x49);
const QColor kWarning       (0xDC, 0xDC, 0xAA);

QString hexByte(quint8 v)
{
    return QStringLiteral("%1").arg(v, 2, 16, QChar('0')).toUpper();
}

} // namespace

FrameLogModel::FrameLogModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int FrameLogModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_frames.size();
}

int FrameLogModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return ColumnCount;
}

void FrameLogModel::setCapacity(int rows)
{
    m_capacity = qMax(1000, rows);
}

void FrameLogModel::clear()
{
    beginResetModel();
    m_frames.clear();
    m_nextIndex = 1;
    endResetModel();
}

void FrameLogModel::append(const Frame &frame)
{
    if (m_frames.size() >= m_capacity) {
        const int drop = qMax(1, m_capacity / 10);
        beginRemoveRows(QModelIndex(), 0, drop - 1);
        m_frames.remove(0, drop);
        endRemoveRows();
    }

    Frame f = frame;
    f.index = m_nextIndex++;

    const int row = m_frames.size();
    beginInsertRows(QModelIndex(), row, row);
    m_frames.append(f);
    endInsertRows();

    emit appended();
}

const Frame *FrameLogModel::frameAt(int row) const
{
    if (row < 0 || row >= m_frames.size())
        return nullptr;
    return &m_frames.at(row);
}

QVariant FrameLogModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_frames.size())
        return QVariant();

    const Frame &f = m_frames.at(index.row());
    const int col = index.column();

    if (role == Qt::DisplayRole) {
        switch (col) {
        case ColTime:
            return QDateTime::fromMSecsSinceEpoch(f.timestampMs).toString(QStringLiteral("HH:mm:ss.zzz"));
        case ColDir:
            if (f.dir == Direction::Tx) return QStringLiteral("TX");
            if (f.dir == Direction::Rx) return QStringLiteral("RX");
            return QStringLiteral("--");
        case ColSlave:
            return f.slave ? hexByte(f.slave) : QStringLiteral("--");
        case ColFunction:
            return f.function ? hexByte(f.function) : QStringLiteral("--");
        case ColType:
            return f.typeName;
        case ColData:
            return QString::fromLatin1(f.raw.toHex(' ').toUpper());
        case ColSummary: {
            QString s = f.summary;
            if (f.elapsedMs >= 0)
                s += QStringLiteral(" · %1 ms").arg(f.elapsedMs);
            return s;
        }
        default:
            break;
        }
    }

    if (role == Qt::ForegroundRole) {
        if (f.kind == FrameKind::Exception)
            return QBrush(kError);
        if (f.kind == FrameKind::CrcError || f.kind == FrameKind::Timeout)
            return QBrush(kWarning);

        switch (col) {
        case ColTime:     return QBrush(kTextMuted);
        case ColDir:
            if (f.dir == Direction::Tx) return QBrush(kTx);
            if (f.dir == Direction::Rx) return QBrush(kRx);
            return QBrush(kTextMuted);
        case ColSlave:    return QBrush(QColor(0xB5, 0xCE, 0xA8));
        case ColFunction: return QBrush(kTextPrimary);
        case ColType:     return QBrush(kTextSecondary);
        case ColData:     return QBrush(QColor(0xCC, 0xCC, 0xCC));
        case ColSummary:  return QBrush(kTextSecondary);
        default:          break;
        }
    }

    if (role == Qt::TextAlignmentRole) {
        if (col == ColDir)
            return QVariant(Qt::AlignCenter);
        return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
    }

    if (role == Qt::ToolTipRole && col == ColSummary)
        return f.summary;

    return QVariant();
}

QVariant FrameLogModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal)
        return QVariant();

    if (role == Qt::TextAlignmentRole)
        return QVariant(section == ColDir ? Qt::AlignCenter
                                          : (Qt::AlignLeft | Qt::AlignVCenter));

    if (role != Qt::DisplayRole)
        return QVariant();

    switch (section) {
    case ColTime:     return QStringLiteral("时间");
    case ColDir:      return QStringLiteral("方向");
    case ColSlave:    return QStringLiteral("从站");
    case ColFunction: return QStringLiteral("功能码");
    case ColType:     return QStringLiteral("类型");
    case ColData:     return QStringLiteral("数据 (HEX)");
    case ColSummary:  return QStringLiteral("解析");
    default:          return QVariant();
    }
}

} // namespace busscope
