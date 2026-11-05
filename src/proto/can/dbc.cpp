#include "proto/can/dbc.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>
#include <cmath>

namespace busscope {

namespace {

/// 按 DBC 的位序规则取一个比特。Intel 从 startBit 往高位走，
/// Motorola 从 startBit 往低位走（跨字节跳到下一字节的 bit7）。
inline int bitAt(const QByteArray &data, int bitIndex)
{
    const int byteIdx = bitIndex / 8;
    if (byteIdx < 0 || byteIdx >= data.size())
        return 0;
    return (quint8(data.at(byteIdx)) >> (bitIndex % 8)) & 1;
}

inline int nextMotorolaBit(int bit)
{
    const int byteIdx = bit / 8;
    const int inByte  = bit % 8;
    return (inByte == 0) ? (byteIdx + 1) * 8 + 7 : bit - 1;
}

} // namespace

double DbcSignal::decode(const QByteArray &data, bool *ok) const
{
    if (ok)
        *ok = false;
    if (length <= 0 || length > 64)
        return 0.0;
    if (data.size() * 8 < startBit + 1)
        return 0.0;

    quint64 raw = 0;
    if (littleEndian) {
        for (int i = 0; i < length; ++i)
            raw |= (quint64(bitAt(data, startBit + i)) << i);
    } else {
        int bit = startBit;
        for (int i = 0; i < length; ++i) {
            raw = (raw << 1) | quint64(bitAt(data, bit));
            bit = nextMotorolaBit(bit);
        }
    }

    if (isSigned && length < 64 && (raw & (quint64(1) << (length - 1)))) {
        raw |= ~((quint64(1) << length) - 1);   // 符号扩展到 64 位
    }

    const double value = static_cast<double>(static_cast<qint64>(raw)) * factor + offset;
    if (ok)
        *ok = true;
    return value;
}

bool DbcDatabase::loadFile(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("无法打开：%1").arg(f.errorString());
        return false;
    }

    QTextStream in(&f);
    in.setCodec("UTF-8");
    const QString text = in.readAll();
    f.close();

    m_fileName = QFileInfo(path).fileName();
    return loadText(text, error);
}

bool DbcDatabase::loadText(const QString &text, QString *error)
{
    m_messages.clear();

    // BO_ 768 ESC_Status: 8 VCU
    static const QRegularExpression reMsg(
        QStringLiteral("^BO_\\s+(\\d+)\\s+([A-Za-z0-9_]+)\\s*:\\s*(\\d+)\\s*([A-Za-z0-9_]*)"));

    //  SG_ PackVolt : 0|16@1+ (0.1,0) [0|6553.5] "V" VCU
    static const QRegularExpression reSig(
        QStringLiteral("^\\s*SG_\\s+([A-Za-z0-9_]+)\\s*(?:[Mm][0-9M]*)?\\s*:\\s*"
                       "(\\d+)\\|(\\d+)@([01])([+-])\\s*"
                       "\\(([^,]+),([^)]+)\\)\\s*"
                       "\\[([^|]*)\\|([^\\]]*)\\]\\s*"
                       "\"([^\"]*)\""));

    const QStringList lines = text.split(QLatin1Char('\n'));
    DbcMessage *current = nullptr;
    int parsedSignals = 0;

    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        const QRegularExpressionMatch mMsg = reMsg.match(rawLine);
        if (mMsg.hasMatch()) {
            DbcMessage msg;
            const quint32 id = mMsg.captured(1).toUInt();
            // DBC 用最高位标记扩展帧（0x80000000 + id）
            msg.extended = (id & 0x80000000u) != 0;
            msg.id       = id & 0x1FFFFFFFu;
            msg.name     = mMsg.captured(2);
            msg.dlc      = mMsg.captured(3).toInt();
            msg.sender   = mMsg.captured(4);
            m_messages.append(msg);
            current = &m_messages.last();
            continue;
        }

        if (current && line.startsWith(QLatin1String("SG_"))) {
            const QRegularExpressionMatch m = reSig.match(rawLine);
            if (!m.hasMatch())
                continue;

            DbcSignal s;
            s.name         = m.captured(1);
            s.startBit     = m.captured(2).toInt();
            s.length       = m.captured(3).toInt();
            s.littleEndian = (m.captured(4) == QLatin1String("1"));
            s.isSigned     = (m.captured(5) == QLatin1String("-"));
            s.factor       = m.captured(6).toDouble();
            s.offset       = m.captured(7).toDouble();
            s.minimum      = m.captured(8).toDouble();
            s.maximum      = m.captured(9).toDouble();
            s.unit         = m.captured(10);
            current->sigList.append(s);
            ++parsedSignals;
        }
    }

    if (m_messages.isEmpty()) {
        if (error)
            *error = QStringLiteral("文件里没有 BO_ 报文定义");
        return false;
    }
    Q_UNUSED(parsedSignals)
    return true;
}

const DbcMessage *DbcDatabase::findMessage(quint32 id, bool extended) const
{
    for (const DbcMessage &m : m_messages) {
        if (m.id == id && m.extended == extended)
            return &m;
    }
    // 扩展标志对不上时再宽松找一次（很多 DBC 的扩展位标得不规范）
    for (const DbcMessage &m : m_messages) {
        if (m.id == id)
            return &m;
    }
    return nullptr;
}

QString DbcDatabase::decode(quint32 id, bool extended, const QByteArray &data) const
{
    const DbcMessage *msg = findMessage(id, extended);
    if (!msg || msg->sigList.isEmpty())
        return QString();

    QStringList parts;
    for (const DbcSignal &s : msg->sigList) {
        bool ok = false;
        const double v = s.decode(data, &ok);
        if (!ok)
            continue;

        // 小数位由 factor 决定：1 → 整数，0.1 → 1 位，0.01 → 2 位
        int decimals = 0;
        if (s.factor > 0.0 && s.factor < 1.0)
            decimals = qBound(0, static_cast<int>(std::ceil(-std::log10(s.factor))), 4);
        else if (s.factor != 1.0)
            decimals = 2;
        const QString num = QString::number(v, 'f', decimals);

        QString one = s.name + QLatin1Char('=') + num;
        if (!s.unit.isEmpty())
            one += QLatin1Char(' ') + s.unit;
        parts << one;
    }

    return parts.join(QStringLiteral(" · "));
}

} // namespace busscope
