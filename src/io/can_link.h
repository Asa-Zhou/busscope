#pragma once

#include <QObject>
#include <QString>

#include "core/can_frame.h"

class QTimer;

namespace busscope {

/// 创芯科技 / 兼容 ZLG 的 USB-CAN（ControlCAN.dll，VCI_* 接口）。
///
/// 注意：厂商只提供 32 位 DLL，本程序必须编成 32 位才能加载它。
/// DLL 没有回调，只能定时轮询 VCI_Receive，所以这里用 5 ms 定时器拉取。
class CanLink : public QObject
{
    Q_OBJECT

public:
    struct Config {
        quint32 deviceType  = 4;      ///< 3=USB-CAN I, 4=USB-CAN II, 21=CANalyst-II
        quint32 deviceIndex = 0;
        quint32 channel     = 0;
        int     bitrate     = 500000;
    };

    explicit CanLink(QObject *parent = nullptr);
    ~CanLink() override;

    bool open(const Config &cfg);
    void close();
    bool isOpen() const { return m_open; }

    /// 打开成功后读到的板卡信息，用于界面上自证「连的是哪块卡」。
    QString boardInfo() const { return m_boardInfo; }
    QString lastError() const { return m_lastError; }
    quint64 rxCount() const { return m_rx; }
    quint64 txCount() const { return m_tx; }

    bool send(quint32 id, bool extended, bool remote, const QByteArray &data);

    static QStringList bitrateLabels();
    static int         bitrateAt(int index);

signals:
    void frameReady(const busscope::CanFrame &frame);
    void stateChanged(bool open);
    void countersChanged(quint64 rx, quint64 tx);

private slots:
    void poll();

private:
    bool loadLibrary();
    void unloadLibrary();

    struct Impl;
    Impl *d = nullptr;

    Config  m_cfg;
    bool    m_open = false;
    QString m_boardInfo;
    QString m_lastError;
    quint64 m_rx = 0;
    quint64 m_tx = 0;
    QTimer *m_timer = nullptr;
};

} // namespace busscope
