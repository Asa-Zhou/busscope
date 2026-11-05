#include "proto/modbus/crc16.h"

namespace busscope {
namespace modbus {

quint16 crc16(const quint8 *data, int len)
{
    quint16 crc = 0xFFFF;
    for (int i = 0; i < len; ++i) {
        crc ^= static_cast<quint16>(data[i]);
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x0001)
                crc = static_cast<quint16>((crc >> 1) ^ 0xA001);
            else
                crc = static_cast<quint16>(crc >> 1);
        }
    }
    return crc;
}

quint16 crc16(const QByteArray &data)
{
    return crc16(reinterpret_cast<const quint8 *>(data.constData()), data.size());
}

void appendCrc(QByteArray &frame)
{
    const quint16 crc = crc16(frame);
    frame.append(static_cast<char>(crc & 0xFF));        // 低字节先发
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
}

bool checkCrc(const QByteArray &frame)
{
    if (frame.size() < 3)
        return false;
    const quint16 expect = crc16(reinterpret_cast<const quint8 *>(frame.constData()),
                                 frame.size() - 2);
    const quint16 actual = static_cast<quint16>(
        static_cast<quint8>(frame.at(frame.size() - 2))
        | (static_cast<quint16>(static_cast<quint8>(frame.at(frame.size() - 1))) << 8));
    return expect == actual;
}

} // namespace modbus
} // namespace busscope
