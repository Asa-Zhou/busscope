#pragma once

#include <QAbstractTableModel>

namespace busscope {

class PollEngine;

/// 点表视图模型。数据本身存在 PollEngine 里，这里只是只读投影 +
/// 「启用」列的勾选回写，避免同一份任务存两处导致状态不同步。
class PollTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        ColEnable = 0,
        ColName,
        ColSlave,
        ColFunction,
        ColAddr,
        ColPeriod,
        ColValue,
        ColStatus,
        ColCount,
        ColumnCount
    };

    explicit PollTableModel(PollEngine *engine, QObject *parent = nullptr);

    int      rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int      columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    bool     setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    /// 引擎里某行状态变了，通知视图重绘整行
    void notifyRow(int row);
    void notifyAll();

private:
    PollEngine *m_engine = nullptr;
};

} // namespace busscope
