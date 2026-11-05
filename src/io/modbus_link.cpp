#include "io/modbus_link.h"

#include <QDateTime>

#include "proto/modbus/crc16.h"
#include "proto/modbus/modbus_rtu.h"

namespace busscope {

namespace {

QSerialPort::DataBits toDataBits(int v)
{
    switch (v) {
    case 5: return QSerialPort::Data5;
    case 6: return QSerialPort::Data6;
    case 7: return QSerialPort::Data7;
    default: return QSerialPort::Data8;
    }
}

QSerialPort::StopBits toStopBits(int v)
{
    switch (v) {
    case 2: return QSerialPort::TwoStop;
    default: return QSerialPort::OneStop;
    }
}

QSerialPort::Parity toParity(int v)
{
    switch (v) {
    case 1: return QSerialPort::OddParity;
    case 2: return QSerialPort::EvenParity;
    default: return QSerialPort::NoParity;
    }
}

QSerialPort::FlowControl toFlowControl(int v)
{
    switch (v) {
    case 1: return QSerialPort::HardwareControl;
    case 2: return QSerialPort::SoftwareControl;
    default: return QSerialPort::NoFlowControl;
    }
}

} // namespace

ModbusLink::ModbusLink(QObject *parent)
    : QObject(parent)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, &ModbusLink::onResponseTimeout);

    m_silence.setSingleShot(true);
    connect(&m_silence, &QTimer::timeout, this, &ModbusLink::onSilenceTimeout);
}

ModbusLink::~ModbusLink()
{
    close();
}

qint64 ModbusLink::now()
{
    return QDateTime::currentMSecsSinceEpoch();
}

bool ModbusLink::open(const PortConfig &cfg)
{
    if (m_port.isOpen())
        close();

    if (cfg.portName.isEmpty()) {
        emit errorText(QStringLiteral("请先选择串口"));
        return false;
    }

    m_port.setPortName(cfg.portName);
    m_port.setBaudRate(cfg.baudRate);
    m_port.setDataBits(toDataBits(cfg.dataBits));
    m_port.setStopBits(toStopBits(cfg.stopBits));
    m_port.setParity(toParity(cfg.parity));
    m_port.setFlowControl(toFlowControl(cfg.flowControl));

    if (!m_port.open(QIODevice::ReadWrite)) {
        emit errorText(QStringLiteral("打开 %1 失败：%2").arg(cfg.portName, m_port.errorString()));
        return false;
    }

    connect(&m_port, &QSerialPort::readyRead, this, &ModbusLink::onReadyRead, Qt::UniqueConnection);
    connect(&m_port, &QSerialPort::errorOccurred, this, &ModbusLink::onSerialError, Qt::UniqueConnection);

    m_cfg = cfg;
    m_buffer.clear();
    m_pending.clear();
    m_pendingValid = false;
    m_rxBytes = 0;
    m_txBytes = 0;
    m_elapsed.invalidate();
    m_silence.stop();
    m_rawStreak = 0;
    m_baudWarned = false;
    // RTU 以 3.5 字符静默期分帧；Windows 定时器精度 ~15ms，下限 30ms 防止把
    // readyRead 的分段误切成两帧。低波特率下整帧传输时间长，按字符时间放大。
    const int charBits = 1 + cfg.dataBits + (cfg.parity == 0 ? 0 : 1) + cfg.stopBits;
    m_silenceMs = qMax(30, qRound(3.5 * 1000.0 * charBits / qMax(1200, cfg.baudRate)));

    m_port.clear();
    emit stateChanged(true);
    emit countersChanged(m_rxBytes, m_txBytes);
    return true;
}

void ModbusLink::close()
{
    if (!m_port.isOpen())
        return;
    const bool wasBusy = m_pendingValid;
    m_timeout.stop();
    m_silence.stop();
    m_pendingValid = false;
    m_port.close();
    emit stateChanged(false);
    if (wasBusy)
        emit transactionFinished(false, QByteArray(), -1);
}

bool ModbusLink::isOpen() const
{
    return m_port.isOpen();
}

int ModbusLink::send(const QByteArray &payload, bool appendCrc)
{
    if (!m_port.isOpen() || payload.isEmpty())
        return -1;

    QByteArray out = payload;
    if (appendCrc)
        modbus::appendCrc(out);

    const qint64 written = m_port.write(out);
    if (written < 0) {
        emit errorText(QStringLiteral("写入失败：%1").arg(m_port.errorString()));
        return -1;
    }

    m_txBytes += out.size();

    Frame tx;
    tx.timestampMs = now();
    tx.dir = Direction::Tx;
    tx.raw = out;
    modbus::decode(tx);
    emit frameReady(tx);

    m_pending = out;
    m_pendingValid = true;
    m_pendingAddr = 0;
    m_pendingQty = 0;
    if (out.size() >= 6) {
        const quint8 fc = static_cast<quint8>(out.at(1));
        if ((fc >= 0x01 && fc <= 0x06) || fc == 0x0F || fc == 0x10) {
            m_pendingAddr = static_cast<quint16>((static_cast<quint8>(out.at(2)) << 8)
                                                 | static_cast<quint8>(out.at(3)));
            m_pendingQty = static_cast<quint16>((static_cast<quint8>(out.at(4)) << 8)
                                                | static_cast<quint8>(out.at(5)));
        }
    }

    m_elapsed.start();
    m_timeout.start(m_timeoutMs);
    emit countersChanged(m_rxBytes, m_txBytes);
    return out.size();
}

int ModbusLink::sendRaw(const QByteArray &payload)
{
    if (!m_port.isOpen() || payload.isEmpty())
        return -1;
    const qint64 written = m_port.write(payload);
    if (written < 0)
        return -1;
    m_txBytes += payload.size();
    emit countersChanged(m_rxBytes, m_txBytes);
    return static_cast<int>(written);
}

void ModbusLink::onReadyRead()
{
    const QByteArray chunk = m_port.readAll();
    if (chunk.isEmpty())
        return;

    m_buffer.append(chunk);
    m_rxBytes += chunk.size();
    emit countersChanged(m_rxBytes, m_txBytes);
    consumeBuffer();
}

void ModbusLink::consumeBuffer()
{
    for (;;) {
        if (m_buffer.isEmpty()) {
            m_silence.stop();
            return;
        }

        if (m_buffer.size() < 5) {
            if (m_buffer.size() > 512) {
                flushRaw();
                continue;
            }
            waitForMore();
            return;
        }

        const quint8 fc = static_cast<quint8>(m_buffer.at(1));
        int need = -1;
        if (fc & 0x80)
            need = 5;
        else if (m_pendingValid)
            need = modbus::responseLength(m_pending);
        else
            need = modbus::requestLength(m_buffer);

        if (need < 0) {
            flushRaw();
            return;
        }
        if (m_buffer.size() < need) {
            if (m_buffer.size() > 512) {
                flushRaw();
                continue;
            }
            waitForMore();
            return;
        }

        const QByteArray raw = m_buffer.left(need);
        m_buffer.remove(0, need);

        Frame f;
        f.timestampMs = now();
        f.dir = Direction::Rx;
        f.raw = raw;
        if (m_pendingValid) {
            f.hasRef = true;
            f.refAddr = m_pendingAddr;
            f.refQty = m_pendingQty;
        }
        modbus::decode(f);
        if (m_pendingValid && m_elapsed.isValid())
            f.elapsedMs = static_cast<int>(m_elapsed.elapsed());

        if (f.kind != FrameKind::Raw)
            m_rawStreak = 0;   // 解析出合法帧，乱码连击清零

        emit frameReady(f);

        if (m_pendingValid) {
            m_pendingValid = false;
            m_timeout.stop();
            emit transactionFinished(true, raw, f.elapsedMs);
        }
    }
}

void ModbusLink::waitForMore()
{
    // 事务在身时由响应超时统一兜底（超时时间由用户设置，足够覆盖整帧传输）；
    // 无事务时（被动监听、透传调试）没有事务超时可用，只能靠帧间静默兜底，
    // 否则凑不齐期望长度的字节会永远留在缓冲区里不显示
    if (m_pendingValid)
        return;
    m_silence.start(m_silenceMs);
}

void ModbusLink::flushRaw(const QString &reason)
{
    if (m_buffer.isEmpty())
        return;

    Frame f;
    f.timestampMs = now();
    f.dir = Direction::Rx;
    f.kind = FrameKind::Raw;
    f.raw = m_buffer;
    f.typeName = QStringLiteral("原始字节");
    f.summary = reason.isEmpty()
                    ? QStringLiteral("未识别数据 · %1 字节").arg(m_buffer.size())
                    : QStringLiteral("%1 · %2 字节").arg(reason).arg(m_buffer.size());
    f.detail = QStringLiteral("长度|%1 字节\n内容|%2")
                   .arg(m_buffer.size())
                   .arg(QString::fromLatin1(m_buffer.toHex(' ').toUpper()));
    m_buffer.clear();

    // 连续多帧都解不出任何 Modbus 结构：若总线上确有通信，几乎可以断定
    // 波特率/校验位与总线不一致（错位采样出来的字节多为 00/FF 填充）。
    // 每次打开串口只提示一次，避免刷屏。
    ++m_rawStreak;
    if (m_rawStreak == 5 && !m_baudWarned) {
        m_baudWarned = true;
        emitNote(QStringLiteral("连续 %1 帧数据无法解析——请检查波特率/校验位是否与总线一致").arg(m_rawStreak),
                 FrameKind::CrcError);
    }
    emit frameReady(f);
}

void ModbusLink::emitNote(const QString &text, FrameKind kind)
{
    Frame f;
    f.timestampMs = now();
    f.dir = Direction::Note;
    f.kind = kind;
    f.typeName = QStringLiteral("提示");
    f.summary = text;
    f.detail = QStringLiteral("说明|%1").arg(text);
    emit frameReady(f);
}

void ModbusLink::onResponseTimeout()
{
    if (!m_pendingValid)
        return;
    m_pendingValid = false;
    // 半截响应留在缓冲区会把下一个事务的切帧开头带偏，之后所有帧跟着错位，
    // 表现为「卡住不显示」；超时即放弃：残留字节整块显示后清零
    if (!m_buffer.isEmpty())
        flushRaw(QStringLiteral("超时残留"));
    emitNote(QStringLiteral("响应超时（%1 ms 无应答）").arg(m_timeoutMs), FrameKind::Timeout);
    emit transactionFinished(false, QByteArray(), -1);
}

void ModbusLink::onSilenceTimeout()
{
    // 帧间静默超时：凑不齐期望长度、也不再进新字节，说明这不是一个完整的
    // Modbus 帧（非标设备 / 半截数据 / 纯透传字节），整块吐出，不再死等
    if (m_buffer.isEmpty() || m_pendingValid)
        return;
    flushRaw(QStringLiteral("静默期分帧"));
}

void ModbusLink::onSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError)
        return;
    if (error == QSerialPort::ResourceError || error == QSerialPort::PermissionError) {
        emit errorText(QStringLiteral("串口异常：%1").arg(m_port.errorString()));
        close();
    }
}

} // namespace busscope
