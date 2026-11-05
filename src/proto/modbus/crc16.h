#pragma once

#include <QByteArray>
#include <QtGlobal>

namespace busscope {
namespace modbus {

/// 标准 Modbus CRC16（多项式 0xA001，初值 0xFFFF，反射算法）。
quint16 crc16(const quint8 *data, int len);
quint16 crc16(const QByteArray &data);

/// 在帧尾追加 CRC16，低字节在前（Modbus RTU 线序）。
void appendCrc(QByteArray &frame);

/// 校验整帧（含末尾 2 字节 CRC）。
bool checkCrc(const QByteArray &frame);

} // namespace modbus
} // namespace busscope
