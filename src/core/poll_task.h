#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>

namespace busscope {

/// 一次成功采样的记录：时间戳 + 数值。波形和导出都用它。
struct WaveSample {
    qint64 tMs   = 0;   ///< 采样时刻（epoch 毫秒）
    qreal  value = 0;
};

/// 点表里一个值怎么解释。32 位类型占两个寄存器（高字在前）。
enum class ValueType {
    Bool,
    Int16,
    UInt16,
    Int32,
    UInt32,
    Float
};

int        registersForType(ValueType type);
QString    valueTypeName(ValueType type);
ValueType  valueTypeFromName(const QString &name);

/// 一条轮询任务 == 一次 Modbus 读请求。点表就是这些行的集合。
struct PollTask {
    bool    enabled   = true;
    QString name;
    quint8  slave     = 1;
    quint8  function  = 3;      ///< 01 02 03 04
    quint16 addr      = 0;
    quint16 count     = 1;
    int     periodMs  = 500;
    int     timeoutMs = 200;
    int     retries   = 1;

    ValueType type  = ValueType::UInt16;
    double    scale = 1.0;
    QString   unit;

    // ---- 运行期状态，由 PollEngine 维护 ----
    QString          status;      ///< 正常 / 超时 / 异常 xx / 未执行
    QString          valueText;
    QVector<quint16> raw;
    quint32          okCount  = 0;
    quint32          errCount = 0;
    qint64           lastOkMs = 0;

    /// 最近若干次成功采样，供波形图与数据导出使用（超过上限从头部丢弃）。
    /// 5000 点按 500 ms 周期约 40 分钟，够回看一段完整工况。
    QVector<WaveSample> history;
    static const int    kHistoryMax = 5000;

    bool wantsBits() const { return function == 0x01 || function == 0x02; }
    int  registersNeeded() const { return qMax<int>(1, registersForType(type)); }

    /// 把原始寄存器换算成数值（含系数）；不能解释时返回 false。
    bool numericValue(const QVector<quint16> &src, double *out) const;

    /// 追加一个采样点，自动裁剪到 kHistoryMax。tMs 传 0 表示"用当前时间"。
    void pushHistory(qreal value, qint64 tMs = 0);
};

/// 把原始寄存器按类型 / 系数 / 单位格式化成可读字符串。
QString formatValue(const PollTask &task, const QVector<quint16> &raw);

} // namespace busscope
