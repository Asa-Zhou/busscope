#pragma once

#include <QObject>
#include <QQueue>
#include <QVector>

#include "core/poll_task.h"

class QTimer;

namespace busscope {

class ModbusLink;

/// 轮询引擎。Modbus RTU 是半双工，所有请求必须串行，所以这里用
/// 一个 tick 定时器挑「最早到期且未完成」的任务发出去，等它回来再发下一条。
/// 写请求（点表双击写值）插到读请求前面优先执行。
class PollEngine : public QObject
{
    Q_OBJECT

public:
    explicit PollEngine(ModbusLink *link, QObject *parent = nullptr);

    const QVector<PollTask> &tasks() const { return m_tasks; }
    PollTask *taskAt(int index);
    int       taskCount() const { return m_tasks.size(); }

    void setTasks(const QVector<PollTask> &tasks);
    void appendTask(const PollTask &task);
    void clearTasks();

    void start();
    void stop();
    void setPaused(bool paused);
    bool isRunning() const { return m_running; }
    bool isPaused() const { return m_paused; }

    /// 写单寄存器（FC06），不参与轮询统计。
    void enqueueWrite(quint8 slave, quint16 addr, quint16 value);

    quint32 okTotal() const { return m_okTotal; }
    quint32 errTotal() const { return m_errTotal; }
    int     requestCount() const { return m_requestCount; }

signals:
    void taskUpdated(int index);
    void stateChanged(bool running, bool paused);
    void logMessage(const QString &text);
    void summaryChanged();

private slots:
    void onTick();
    void onTransaction(bool ok, const QByteArray &response, int elapsedMs);

private:
    struct WriteJob {
        quint8  slave = 1;
        quint16 addr  = 0;
        quint16 value = 0;
    };

    void sendRead(int index);
    void finishRead(int index, bool ok, const QString &statusText, const QVector<quint16> &values);
    void processWriteQueue();

    ModbusLink       *m_link = nullptr;
    QTimer           *m_tick = nullptr;
    QVector<PollTask> m_tasks;
    QVector<qint64>   m_nextDue;

    bool m_running = false;
    bool m_paused  = false;

    int    m_pendingIndex = -1;
    int    m_attempt      = 0;
    bool   m_writing      = false;
    QQueue<WriteJob> m_writeQueue;

    quint32 m_okTotal      = 0;
    quint32 m_errTotal     = 0;
    int     m_requestCount = 0;
};

} // namespace busscope
