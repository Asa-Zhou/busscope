#pragma once

#include <QAbstractTableModel>
#include <QVector>

#include "core/can_frame.h"
#include "proto/can/dbc.h"

namespace busscope {

class CanFrameModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        ColTime = 0,
        ColDir,
        ColId,
        ColType,
        ColDlc,
        ColData,
        ColDecoded,      ///< DBC 解码结果
        ColumnCount
    };

    explicit CanFrameModel(QObject *parent = nullptr);

    int      rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int      columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void append(const CanFrame &frame);
    void clear();
    const CanFrame *frameAt(int row) const;

    /// 挂上 DBC 后，ColDecoded 列才会有内容
    void setDatabase(const DbcDatabase *db);
    void refreshDecoded();      ///< DBC 换了之后重算已显示的帧

signals:
    void appended();

private:
    QVector<CanFrame>  m_frames;
    const DbcDatabase *m_db = nullptr;
    int                m_capacity = 50000;
};

} // namespace busscope
