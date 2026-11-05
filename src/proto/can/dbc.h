#pragma once

#include <QString>
#include <QVector>
#include <QByteArray>
#include <QtGlobal>

namespace busscope {

/// DBC 里的一个信号（SG_ 行）。
struct DbcSignal {
    QString name;
    int     startBit     = 0;
    int     length       = 1;
    bool    littleEndian = true;    ///< @1=Intel 小端，@0=Motorola 大端
    bool    isSigned     = false;
    double  factor       = 1.0;
    double  offset       = 0.0;
    double  minimum      = 0.0;
    double  maximum      = 0.0;
    QString unit;

    /// 从一帧数据里把这个信号的物理值算出来（raw * factor + offset）
    double decode(const QByteArray &data, bool *ok = nullptr) const;
};

/// DBC 里的一条报文（BO_ 行）。
struct DbcMessage {
    quint32 id       = 0;
    bool    extended = false;
    QString name;
    int     dlc      = 8;
    QString sender;
    QVector<DbcSignal> sigList;   // 不能叫 signals，Qt 宏会替换
};

/// 一个 .dbc 文件解析后的数据库。只做「报文—信号—解码」三件事，
/// 不处理注释、值表、多路复用（遇到就跳过，不影响已识别的部分）。
class DbcDatabase
{
public:
    /// 解析失败时 error 里放原因。空文件也不算失败，只是没有报文。
    bool loadFile(const QString &path, QString *error = nullptr);
    bool loadText(const QString &text, QString *error = nullptr);

    bool isEmpty() const { return m_messages.isEmpty(); }
    int  messageCount() const { return m_messages.size(); }
    QString fileName() const { return m_fileName; }

    /// 按 ID 找报文（extended 只做提示，ID 本身已含扩展标志位时不严格匹配）
    const DbcMessage *findMessage(quint32 id, bool extended) const;

    /// 把一帧解码成 "PackVolt=540.2 V · PackCurr=0.0 A"；
    /// 找不到报文返回空串，报文有信号但数据不够也返回已解出的部分。
    QString decode(quint32 id, bool extended, const QByteArray &data) const;

    /// 给表格用的列头/说明
    QVector<DbcMessage> messages() const { return m_messages; }

private:
    QVector<DbcMessage> m_messages;
    QString             m_fileName;
};

} // namespace busscope
