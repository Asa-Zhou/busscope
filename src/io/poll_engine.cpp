#include "io/poll_engine.h"

#include <QDateTime>
#include <QTimer>

#include "io/modbus_link.h"

namespace busscope {

namespace {
const int kTickMs = 20;
}

PollEngine::PollEngine(ModbusLink *link, QObject *parent)
    : QObject(parent)
    , m_link(link)
{
    m_tick = new QTimer(this);
    m_tick->setInterval(kTickMs);
    connect(m_tick, &QTimer::timeout, this, &PollEngine::onTick);

    connect(m_link, &ModbusLink::transactionFinished, this, &PollEngine::onTransaction);
    connect(m_link, &ModbusLink::stateChanged, this, [this](bool open) {
        if (!open && m_running) {
            stop();
            emit logMessage(QStringLiteral("串口已关闭，轮询停止"));
        }
    });
}

PollTask *PollEngine::taskAt(int index)
{
    if (index < 0 || index >= m_tasks.size())
        return nullptr;
    return &m_tasks[index];
}

void PollEngine::setTasks(const QVector<PollTask> &tasks)
{
    m_tasks = tasks;
    m_nextDue.fill(0, m_tasks.size());
    for (int i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks.at(i).status.isEmpty())
            m_tasks[i].status = QStringLiteral("未执行");
    }
    emit summaryChanged();
}

void PollEngine::appendTask(const PollTask &task)
{
    PollTask t = task;
    if (t.status.isEmpty())
        t.status = QStringLiteral("未执行");
    m_tasks.append(t);
    m_nextDue.append(0);
    emit summaryChanged();
}

void PollEngine::clearTasks()
{
    stop();
    m_tasks.clear();
    m_nextDue.clear();
    m_okTotal = 0;
    m_errTotal = 0;
    m_requestCount = 0;
    emit summaryChanged();
}

void PollEngine::start()
{
    if (m_tasks.isEmpty()) {
        emit logMessage(QStringLiteral("点表为空，先导入或新建轮询项"));
        return;
    }
    if (!m_link->isOpen()) {
        emit logMessage(QStringLiteral("请先打开串口再启动轮询"));
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_nextDue.fill(now, m_tasks.size());
    m_running = true;
    m_paused = false;
    m_pendingIndex = -1;
    m_attempt = 0;
    m_tick->start();

    emit stateChanged(true, false);
    emit logMessage(QStringLiteral("轮询已启动，共 %1 项").arg(m_tasks.size()));
}

void PollEngine::stop()
{
    if (!m_running)
        return;
    m_running = false;
    m_paused = false;
    m_tick->stop();
    m_pendingIndex = -1;
    emit stateChanged(false, false);
}

void PollEngine::setPaused(bool paused)
{
    if (!m_running || m_paused == paused)
        return;
    m_paused = paused;

    if (!paused) {
        // 恢复时把过期的任务立刻排上，避免暂停期间累积的时间一次性爆发
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (int i = 0; i < m_nextDue.size(); ++i) {
            if (m_nextDue.at(i) < now)
                m_nextDue[i] = now;
        }
    }
    emit stateChanged(true, m_paused);
}

void PollEngine::enqueueWrite(quint8 slave, quint16 addr, quint16 value)
{
    WriteJob job;
    job.slave = slave;
    job.addr = addr;
    job.value = value;
    m_writeQueue.enqueue(job);
}

void PollEngine::onTick()
{
    if (!m_running || m_paused)
        return;
    if (!m_link->isOpen() || m_link->isBusy() || m_pendingIndex >= 0)
        return;

    if (!m_writeQueue.isEmpty() && !m_writing) {
        processWriteQueue();
        return;
    }
    if (m_writing)
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    int best = -1;
    qint64 bestDue = 0;
    for (int i = 0; i < m_tasks.size(); ++i) {
        if (!m_tasks.at(i).enabled)
            continue;
        if (now < m_nextDue.at(i))
            continue;
        if (best < 0 || m_nextDue.at(i) < bestDue) {
            best = i;
            bestDue = m_nextDue.at(i);
        }
    }
    if (best >= 0) {
        m_attempt = 0;
        sendRead(best);
    }
}

void PollEngine::sendRead(int index)
{
    PollTask &t = m_tasks[index];

    quint16 qty = t.count;
    if (qty == 0)
        qty = 1;

    QByteArray req;
    req.append(static_cast<char>(t.slave));
    req.append(static_cast<char>(t.function));
    req.append(static_cast<char>((t.addr >> 8) & 0xFF));
    req.append(static_cast<char>(t.addr & 0xFF));
    req.append(static_cast<char>((qty >> 8) & 0xFF));
    req.append(static_cast<char>(qty & 0xFF));

    m_pendingIndex = index;
    ++m_requestCount;
    m_link->setResponseTimeout(t.timeoutMs);

    if (m_link->send(req, true) < 0) {
        m_pendingIndex = -1;
        finishRead(index, false, QStringLiteral("发送失败"), QVector<quint16>());
    }
}

void PollEngine::onTransaction(bool ok, const QByteArray &response, int elapsedMs)
{
    Q_UNUSED(elapsedMs)

    if (m_pendingIndex < 0) {
        if (m_writing) {
            m_writing = false;
            emit logMessage(ok ? QStringLiteral("写入完成")
                               : QStringLiteral("写入超时（从站无应答）"));
        }
        return;
    }

    const int index = m_pendingIndex;
    m_pendingIndex = -1;
    if (index < 0 || index >= m_tasks.size())
        return;

    PollTask &t = m_tasks[index];

    bool success = false;
    QString status;
    QVector<quint16> values;

    if (!ok) {
        status = QStringLiteral("超时");
    } else if (response.size() >= 5
               && (static_cast<quint8>(response.at(1)) & 0x80)) {
        const quint8 code = static_cast<quint8>(response.at(2));
        status = QStringLiteral("异常 %1")
                     .arg(static_cast<int>(code), 2, 16, QChar('0'))
                     .toUpper();
    } else if (response.size() >= 5) {
        const int byteCount = static_cast<quint8>(response.at(2));
        if (t.wantsBits()) {
            for (int b = 0; b < byteCount; ++b) {
                const int pos = 3 + b;
                if (pos >= response.size())
                    break;
                const quint8 byte = static_cast<quint8>(response.at(pos));
                for (int bit = 0; bit < 8; ++bit)
                    values.append((byte >> bit) & 0x01);
            }
            success = !values.isEmpty();
        } else {
            for (int i = 0; i + 1 < byteCount; i += 2) {
                const int pos = 3 + i;
                if (pos + 1 >= response.size())
                    break;
                values.append(static_cast<quint16>((static_cast<quint8>(response.at(pos)) << 8)
                                                   | static_cast<quint8>(response.at(pos + 1))));
            }
            success = !values.isEmpty();
        }
        if (!success)
            status = QStringLiteral("空响应");
    } else {
        status = QStringLiteral("响应过短");
    }

    if (!success && m_attempt < t.retries) {
        ++m_attempt;
        // 失败原因即时上屏：不等到最终失败才可见，核对时每一轮的结果都有迹可循
        t.status = QStringLiteral("%1 · 重试 %2/%3").arg(status).arg(m_attempt).arg(t.retries);
        emit taskUpdated(index);
        emit summaryChanged();
        emit logMessage(QStringLiteral("%1：%2，第 %3 次重试")
                            .arg(t.name, status)
                            .arg(m_attempt));
        sendRead(index);
        return;
    }

    finishRead(index, success, status, values);
}

void PollEngine::finishRead(int index, bool ok, const QString &statusText,
                            const QVector<quint16> &values)
{
    if (index < 0 || index >= m_tasks.size())
        return;

    PollTask &t = m_tasks[index];
    if (ok) {
        t.raw = values;
        t.valueText = formatValue(t, values);
        t.status = QStringLiteral("正常");
        t.lastOkMs = QDateTime::currentMSecsSinceEpoch();
        ++t.okCount;
        ++m_okTotal;

        double v = 0.0;
        if (t.numericValue(values, &v))
            t.pushHistory(v, t.lastOkMs);
    } else {
        t.status = statusText;
        ++t.errCount;
        ++m_errTotal;
    }

    m_nextDue[index] = QDateTime::currentMSecsSinceEpoch() + qMax(20, t.periodMs);
    emit taskUpdated(index);
    emit summaryChanged();
}

void PollEngine::processWriteQueue()
{
    if (m_writeQueue.isEmpty() || m_writing)
        return;

    const WriteJob job = m_writeQueue.dequeue();

    QByteArray req;
    req.append(static_cast<char>(job.slave));
    req.append(static_cast<char>(0x06));
    req.append(static_cast<char>((job.addr >> 8) & 0xFF));
    req.append(static_cast<char>(job.addr & 0xFF));
    req.append(static_cast<char>((job.value >> 8) & 0xFF));
    req.append(static_cast<char>(job.value & 0xFF));

    m_writing = true;
    m_link->setResponseTimeout(300);
    if (m_link->send(req, true) < 0) {
        m_writing = false;
        emit logMessage(QStringLiteral("写入失败：串口未打开"));
        return;
    }
    emit logMessage(QStringLiteral("写入 从站 %1 · 寄存器 %2 ← %3")
                        .arg(job.slave)
                        .arg(job.addr)
                        .arg(job.value));
}

} // namespace busscope
