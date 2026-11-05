#pragma once

#include <QAbstractTableModel>
#include <QVector>

#include "core/frame.h"

namespace busscope {

/// 帧记录表模型。固定容量环形裁剪，不逐帧刷新界面，
/// 视图侧用 setUniformRowHeights 思路保持滚动性能。
class FrameLogModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        ColTime = 0,
        ColDir,
        ColSlave,
        ColFunction,
        ColType,
        ColData,
        ColSummary,
        ColumnCount
    };

    explicit FrameLogModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void append(const Frame &frame);
    void clear();
    void setCapacity(int rows);

    const Frame *frameAt(int row) const;

signals:
    void appended();

private:
    QVector<Frame> m_frames;
    quint64        m_nextIndex = 1;
    int            m_capacity  = 100000;
};

} // namespace busscope
