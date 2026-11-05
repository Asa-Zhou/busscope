#include "proto/modbus/modbus_rtu.h"
#include "proto/modbus/crc16.h"

#include <QStringList>

namespace busscope {
namespace modbus {

namespace {

QString hex8(quint8 v)
{
    return QStringLiteral("%1").arg(v, 2, 16, QChar('0')).toUpper();
}

QString hex16(quint16 v)
{
    return QStringLiteral("%1").arg(v, 4, 16, QChar('0')).toUpper();
}

inline quint8 u8(const QByteArray &b, int i)
{
    return static_cast<quint8>(b.at(i));
}

inline quint16 u16(const QByteArray &b, int i)
{
    return static_cast<quint16>((static_cast<quint16>(u8(b, i)) << 8) | u8(b, i + 1));
}

inline bool isBitArea(quint8 fc)
{
    return fc == 0x01 || fc == 0x02 || fc == 0x05 || fc == 0x0F;
}

} // namespace

QString functionName(quint8 fc)
{
    switch (fc) {
    case 0x01: return QStringLiteral("读线圈");
    case 0x02: return QStringLiteral("读离散输入");
    case 0x03: return QStringLiteral("读保持寄存器");
    case 0x04: return QStringLiteral("读输入寄存器");
    case 0x05: return QStringLiteral("写单线圈");
    case 0x06: return QStringLiteral("写单寄存器");
    case 0x0F: return QStringLiteral("写多线圈");
    case 0x10: return QStringLiteral("写多寄存器");
    case 0x17: return QStringLiteral("读/写多寄存器");
    case 0x2B: return QStringLiteral("设备标识");
    default:   return QStringLiteral("功能码 0x") + hex8(fc);
    }
}

QString exceptionText(quint8 code)
{
    switch (code) {
    case 0x01: return QStringLiteral("非法功能码");
    case 0x02: return QStringLiteral("非法数据地址");
    case 0x03: return QStringLiteral("非法数据值");
    case 0x04: return QStringLiteral("从站设备故障");
    case 0x05: return QStringLiteral("确认（处理中）");
    case 0x06: return QStringLiteral("从站设备忙");
    case 0x08: return QStringLiteral("存储奇偶校验错");
    case 0x0A: return QStringLiteral("网关路径不可用");
    case 0x0B: return QStringLiteral("网关目标无响应");
    default:   return QStringLiteral("未知异常");
    }
}

QString pointLabel(quint8 fc, quint16 addr)
{
    QChar prefix;
    switch (fc) {
    case 0x01: case 0x05: case 0x0F: prefix = QLatin1Char('0'); break;
    case 0x02:                        prefix = QLatin1Char('1'); break;
    case 0x03: case 0x06: case 0x10: prefix = QLatin1Char('4'); break;
    case 0x04:                        prefix = QLatin1Char('3'); break;
    default:
        return QStringLiteral("0x") + hex16(addr);
    }
    return QString(prefix) + QStringLiteral("%1").arg(static_cast<int>(addr) + 1, 4, 10, QChar('0'));
}

int requestLength(const QByteArray &head)
{
    if (head.size() < 2)
        return -1;
    const quint8 fc = u8(head, 1);
    if (fc & 0x80)
        return 5;
    switch (fc) {
    case 0x01: case 0x02: case 0x03: case 0x04:
    case 0x05: case 0x06:
        return 8;
    case 0x0F: case 0x10: {
        if (head.size() < 6)
            return -1;
        return 9 + u16(head, 4) * 2;
    }
    default:
        return -1;
    }
}

int responseLength(const QByteArray &request)
{
    if (request.size() < 6)
        return -1;
    const quint8 fc = u8(request, 1);
    switch (fc) {
    case 0x01: case 0x02: case 0x03: case 0x04: {
        const quint16 qty = u16(request, 4);
        const int bytes = (fc <= 0x02) ? (qty + 7) / 8 : qty * 2;
        return 3 + bytes + 2;
    }
    case 0x05: case 0x06: case 0x0F: case 0x10:
        return 8;
    default:
        return -1;
    }
}

void decode(Frame &f)
{
    const QByteArray &r = f.raw;
    const int n = r.size();

    if (n < 4) {
        f.kind = FrameKind::Raw;
        f.typeName = QStringLiteral("原始字节");
        f.summary = QStringLiteral("%1 字节").arg(n);
        f.detail = QStringLiteral("长度|%1 字节\n内容|%2")
                       .arg(n)
                       .arg(QString::fromLatin1(r.toHex(' ').toUpper()));
        return;
    }

    f.slave = u8(r, 0);
    f.function = u8(r, 1);
    const quint8 fc = f.function;
    const bool crcOk = checkCrc(r);
    const quint16 crcValue = static_cast<quint16>(u8(r, n - 2)
                             | (static_cast<quint16>(u8(r, n - 1)) << 8));

    QStringList rows;
    rows << QStringLiteral("从站地址|0x%1").arg(hex8(f.slave));

    if (fc & 0x80) {
        // ---------- 异常响应 ----------
        const quint8 code = n > 2 ? u8(r, 2) : 0;
        f.kind = FrameKind::Exception;
        f.typeName = QStringLiteral("异常响应");
        f.summary = QStringLiteral("异常 %1 · %2").arg(hex8(code), exceptionText(code));
        rows << QStringLiteral("功能码|0x%1  (%2 + 0x80)").arg(hex8(fc), functionName(fc & 0x7F))
             << QStringLiteral("异常码|0x%1  %2").arg(hex8(code), exceptionText(code));
    }
    else if (f.dir == Direction::Tx) {
        // ---------- 请求 ----------
        f.typeName = functionName(fc);
        switch (fc) {
        case 0x01: case 0x02: case 0x03: case 0x04: {
            if (n < 8) { f.kind = FrameKind::Raw; f.summary = QStringLiteral("请求长度不足"); break; }
            const quint16 addr = u16(r, 2);
            const quint16 qty = u16(r, 4);
            f.refAddr = addr;
            f.refQty = qty;
            f.hasRef = true;
            f.summary = QStringLiteral("请求 · 起始 %1 · %2 %3")
                            .arg(pointLabel(fc, addr))
                            .arg(qty)
                            .arg(isBitArea(fc) ? QStringLiteral("位") : QStringLiteral("个寄存器"));
            rows << QStringLiteral("功能码|0x%1  %2").arg(hex8(fc), f.typeName)
                 << QStringLiteral("起始地址|0x%1  (%2)").arg(hex16(addr), pointLabel(fc, addr))
                 << QStringLiteral("数量|%1 %2").arg(qty).arg(isBitArea(fc) ? QStringLiteral("位") : QStringLiteral("寄存器"));
            break;
        }
        case 0x05: {
            if (n < 8) { f.kind = FrameKind::Raw; f.summary = QStringLiteral("请求长度不足"); break; }
            const quint16 addr = u16(r, 2);
            const quint16 val = u16(r, 4);
            f.summary = QStringLiteral("请求 · %1 ← %2")
                            .arg(pointLabel(fc, addr))
                            .arg(val == 0xFF00 ? QStringLiteral("ON") : QStringLiteral("OFF"));
            rows << QStringLiteral("功能码|0x%1  %2").arg(hex8(fc), f.typeName)
                 << QStringLiteral("线圈地址|0x%1  (%2)").arg(hex16(addr), pointLabel(fc, addr))
                 << QStringLiteral("输出值|0x%1").arg(hex16(val));
            break;
        }
        case 0x06: {
            if (n < 8) { f.kind = FrameKind::Raw; f.summary = QStringLiteral("请求长度不足"); break; }
            const quint16 addr = u16(r, 2);
            const quint16 val = u16(r, 4);
            f.summary = QStringLiteral("请求 · %1 ← %2 (0x%3)")
                            .arg(pointLabel(fc, addr))
                            .arg(val)
                            .arg(hex16(val));
            rows << QStringLiteral("功能码|0x%1  %2").arg(hex8(fc), f.typeName)
                 << QStringLiteral("寄存器|0x%1  (%2)").arg(hex16(addr), pointLabel(fc, addr))
                 << QStringLiteral("写入值|%1  (0x%2)").arg(val).arg(hex16(val));
            break;
        }
        case 0x0F: case 0x10: {
            if (n < 9) { f.kind = FrameKind::Raw; f.summary = QStringLiteral("请求长度不足"); break; }
            const quint16 addr = u16(r, 2);
            const quint16 qty = u16(r, 4);
            f.refAddr = addr;
            f.refQty = qty;
            f.hasRef = true;
            f.summary = QStringLiteral("请求 · 起始 %1 · 写 %2 %3")
                            .arg(pointLabel(fc, addr))
                            .arg(qty)
                            .arg(isBitArea(fc) ? QStringLiteral("位") : QStringLiteral("个寄存器"));
            rows << QStringLiteral("功能码|0x%1  %2").arg(hex8(fc), f.typeName)
                 << QStringLiteral("起始地址|0x%1  (%2)").arg(hex16(addr), pointLabel(fc, addr))
                 << QStringLiteral("数量|%1").arg(qty)
                 << QStringLiteral("字节数|%1").arg(u8(r, 6));
            break;
        }
        default:
            f.kind = FrameKind::Raw;
            f.summary = QStringLiteral("未解析的请求");
            break;
        }
    }
    else {
        // ---------- 响应 ----------
        f.typeName = functionName(fc);
        switch (fc) {
        case 0x01: case 0x02: {
            const int bc = n > 2 ? u8(r, 2) : 0;
            f.summary = QStringLiteral("%1 位状态（%2 字节）").arg(bc * 8).arg(bc);
            rows << QStringLiteral("功能码|0x%1  %2").arg(hex8(fc), f.typeName)
                 << QStringLiteral("字节数|%1").arg(bc);
            break;
        }
        case 0x03: case 0x04: {
            const int bc = n > 2 ? u8(r, 2) : 0;
            const int count = bc / 2;
            QStringList vals;
            for (int i = 0; i < count && 3 + i * 2 + 1 < n; ++i) {
                const quint16 v = u16(r, 3 + i * 2);
                if (f.hasRef)
                    vals << QStringLiteral("%1 = %2").arg(pointLabel(fc, f.refAddr + i)).arg(v);
                else
                    vals << QString::number(v);
            }
            f.summary = vals.isEmpty() ? QStringLiteral("空响应") : vals.join(QStringLiteral(" · "));
            rows << QStringLiteral("功能码|0x%1  %2").arg(hex8(fc), f.typeName)
                 << QStringLiteral("字节数|%1").arg(bc);
            for (int i = 0; i < count && 3 + i * 2 + 1 < n; ++i) {
                const quint16 v = u16(r, 3 + i * 2);
                rows << QStringLiteral("%1|%2  (0x%3)")
                            .arg(f.hasRef ? pointLabel(fc, f.refAddr + i) : QStringLiteral("寄存器 %1").arg(i))
                            .arg(v)
                            .arg(hex16(v));
            }
            break;
        }
        case 0x05: case 0x06: case 0x0F: case 0x10:
            f.summary = QStringLiteral("响应 · 回显一致");
            rows << QStringLiteral("功能码|0x%1  %2").arg(hex8(fc), f.typeName)
                 << QStringLiteral("说明|写操作成功，从站原样回显");
            break;
        default:
            f.kind = FrameKind::Raw;
            f.summary = QStringLiteral("未解析的响应");
            break;
        }
    }

    if (!crcOk) {
        f.summary += QStringLiteral(" · CRC 错误");
        if (f.kind == FrameKind::Data)
            f.kind = FrameKind::CrcError;
    }

    rows << QStringLiteral("CRC16|0x%1  %2")
                .arg(hex16(crcValue), crcOk ? QStringLiteral("校验通过") : QStringLiteral("校验失败"))
         << QStringLiteral("长度|%1 字节").arg(n);

    f.detail = rows.join(QLatin1Char('\n'));
}

} // namespace modbus
} // namespace busscope
