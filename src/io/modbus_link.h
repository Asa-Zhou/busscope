#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QSerialPort>
#include <QTimer>

#include "core/frame.h"

namespace busscope {

/// 串口参数。UI 只装配这个结构体，不直接碰 QSerialPort。
struct PortConfig {
    QString portName;
    int     baudRate    = 115200;
    int     dataBits    = 8;   ///< 5..8
    int     stopBits    = 1;   ///< 1 / 2
    int     parity      = 0;   ///< 0=None 1=Odd 2=Even
    int     flowControl = 0;   ///< 0=None 1=RTS-CTS
};

/// Modbus RTU 主站长连接：负责串口收发、按功能码切帧、超时判定、
/// 并把每一条记录以 Frame 形式抛给模型层。
class ModbusLink : public QObject
{
    Q_OBJECT

public:
    explicit ModbusLink(QObject *parent = nullptr);
    ~ModbusLink() override;

    bool open(const PortConfig &cfg);
    void close();
    bool isOpen() const;

    PortConfig config() const { return m_cfg; }

    /// 发送一帧。appendCrc 为 true 时自动补 CRC16。返回写入字节数，失败返回 -1。
    int send(const QByteArray &payload, bool appendCrc);

    /// 只写串口、不产生帧记录（用于原始透传调试）。
    int sendRaw(const QByteArray &payload);

    void setResponseTimeout(int ms) { m_timeoutMs = qMax(20, ms); }
    int  responseTimeout() const { return m_timeoutMs; }

    /// 是否有请求正处于「已发出、等响应」状态。
    bool isBusy() const { return m_pendingValid; }

signals:
    void frameReady(const busscope::Frame &frame);
    void stateChanged(bool open);
    void errorText(const QString &text);
    void countersChanged(qint64 rxBytes, qint64 txBytes);

    /// 一次事务结束：ok=true 收到响应（可能仍是异常响应，由上层判断），
    /// ok=false 表示超时或链路被关闭。
    void transactionFinished(bool ok, const QByteArray &response, int elapsedMs);

private slots:
    void onReadyRead();
    void onSerialError(QSerialPort::SerialPortError error);
    void onResponseTimeout();
    void onSilenceTimeout();

private:
    void consumeBuffer();
    void waitForMore();
    void flushRaw(const QString &reason = QString());
    void emitNote(const QString &text, FrameKind kind);
    static qint64 now();

    QSerialPort    m_port;
    QByteArray     m_buffer;
    QByteArray     m_pending;          ///< 当前等待响应的请求帧
    quint16        m_pendingAddr = 0;
    quint16        m_pendingQty  = 0;
    bool           m_pendingValid = false;
    QElapsedTimer  m_elapsed;
    QTimer         m_timeout;
    int            m_timeoutMs = 200;
    QTimer         m_silence;          ///< 帧间静默判定：无事务在身时凑不齐长度，超静默期就把缓冲区整帧吐出
    int            m_silenceMs = 30;
    int            m_rawStreak   = 0;  ///< 连续无法解析的原始帧计数（波特率不匹配预警用）
    bool           m_baudWarned  = false;
    PortConfig     m_cfg;
    qint64         m_rxBytes = 0;
    qint64         m_txBytes = 0;
};

} // namespace busscope
