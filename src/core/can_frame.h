#pragma once

#include <QByteArray>
#include <QtGlobal>

namespace busscope {

/// 一条 CAN 帧记录。CAN 只有 ID + 数据，没有从站/寄存器那套语义，
/// 所以单独一个结构体，不硬塞进 Modbus 的 Frame。
struct CanFrame {
    qint64     timestampMs = 0;
    bool       tx          = false;
    quint32    id          = 0;
    bool       extended    = false;   ///< 29 位扩展帧
    bool       remote      = false;   ///< 远程请求帧
    bool       error       = false;   ///< 错误帧
    QByteArray data;
};

} // namespace busscope
