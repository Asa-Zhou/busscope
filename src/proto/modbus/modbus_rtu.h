#pragma once

#include <QByteArray>
#include <QString>

#include "core/frame.h"

namespace busscope {
namespace modbus {

QString functionName(quint8 fc);
QString exceptionText(quint8 code);

/// 把 PDU 地址翻译成人习惯的点表编号：线圈 0xxxx、离散输入 1xxxx、
/// 输入寄存器 3xxxx、保持寄存器 4xxxx。
QString pointLabel(quint8 fc, quint16 addr);

/// 由帧头推算一条“请求”的总字节数，无法判断返回 -1（至少需要 6 字节）。
int requestLength(const QByteArray &head);

/// 由一条请求推算它期望的“响应”总字节数，无法判断返回 -1。
int responseLength(const QByteArray &request);

/// 解析 frame.raw，填充 typeName / summary / detail / kind 等字段。
/// 解析失败时把 kind 置为 Raw，不会抛异常。
void decode(Frame &frame);

} // namespace modbus
} // namespace busscope
