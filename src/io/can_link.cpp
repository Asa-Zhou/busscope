#include "io/can_link.h"

#include <QDateTime>
#include <QLibrary>
#include <QStringList>
#include <QTimer>

#include <cstring>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace busscope {

namespace {

struct BaudEntry {
    int rate;
    int t0;
    int t1;
};

// 16 MHz 时钟下的标准波特率分频值（创芯 / 致远通用表）
const BaudEntry kBauds[] = {
    { 1000000, 0x00, 0x14 },
    {  800000, 0x00, 0x16 },
    {  500000, 0x00, 0x1C },
    {  250000, 0x01, 0x1C },
    {  125000, 0x03, 0x1C },
    {  100000, 0x04, 0x1C },
    {   50000, 0x09, 0x1C },
    {   20000, 0x18, 0x1C },
    {   10000, 0x31, 0x1C },
};
const int kBaudCount = int(sizeof(kBauds) / sizeof(kBauds[0]));

bool timingFor(int bitrate, int *t0, int *t1)
{
    for (int i = 0; i < kBaudCount; ++i) {
        if (kBauds[i].rate == bitrate) {
            *t0 = kBauds[i].t0;
            *t1 = kBauds[i].t1;
            return true;
        }
    }
    return false;
}

} // namespace

#ifdef Q_OS_WIN

struct CanLink::Impl
{
    // ---- ControlCAN.dll 的结构体（必须与厂商头文件逐字节一致）----
    struct CanObj {
        DWORD ID;
        DWORD TimeStamp;
        BYTE  TimeFlag;
        BYTE  SendType;
        BYTE  RemoteFlag;
        BYTE  ExternFlag;
        BYTE  DataLen;
        BYTE  Data[8];
        BYTE  Reserved[3];
    };

    struct InitConfig {
        DWORD AccCode;
        DWORD AccMask;
        DWORD Reserved;
        BYTE  Filter;
        BYTE  Timing0;
        BYTE  Timing1;
        BYTE  Mode;
    };

    struct BoardInfo {
        WORD  hw_Version;
        WORD  fw_Version;
        WORD  dr_Version;
        WORD  in_Version;
        WORD  irq_Num;
        BYTE  can_Num;
        CHAR  str_Serial_Num[20];
        CHAR  str_hw_Type[40];
        WORD  Reserved[4];
    };

    // 厂商 DLL 是 32 位 __stdcall，写错调用约定会直接把栈搞坏
    typedef DWORD (__stdcall *FnOpen)(DWORD, DWORD, DWORD);
    typedef DWORD (__stdcall *FnClose)(DWORD, DWORD);
    typedef DWORD (__stdcall *FnInit)(DWORD, DWORD, DWORD, void *);
    typedef DWORD (__stdcall *FnStart)(DWORD, DWORD, DWORD);
    typedef DWORD (__stdcall *FnReset)(DWORD, DWORD, DWORD);
    typedef DWORD (__stdcall *FnInfo)(DWORD, DWORD, void *);
    typedef ULONG (__stdcall *FnTransmit)(DWORD, DWORD, DWORD, void *, ULONG);
    typedef ULONG (__stdcall *FnReceive)(DWORD, DWORD, DWORD, void *, ULONG, INT);

    QLibrary lib;
    FnOpen     openFn     = nullptr;
    FnClose    closeFn    = nullptr;
    FnInit     initFn     = nullptr;
    FnStart    startFn    = nullptr;
    FnReset    resetFn    = nullptr;
    FnInfo     infoFn     = nullptr;
    FnTransmit transmitFn = nullptr;
    FnReceive  receiveFn   = nullptr;
    bool       loaded     = false;
};

#else // 非 Windows：空实现，保证代码仍可编译

struct CanLink::Impl {};

#endif // Q_OS_WIN

CanLink::CanLink(QObject *parent)
    : QObject(parent)
{
    d = new Impl;
    m_timer = new QTimer(this);
    m_timer->setInterval(5);
    connect(m_timer, &QTimer::timeout, this, &CanLink::poll);
}

CanLink::~CanLink()
{
    close();
    delete d;
    d = nullptr;
}

QStringList CanLink::bitrateLabels()
{
    return QStringList() << QStringLiteral("1000 kbit/s") << QStringLiteral("800 kbit/s")
                         << QStringLiteral("500 kbit/s") << QStringLiteral("250 kbit/s")
                         << QStringLiteral("125 kbit/s") << QStringLiteral("100 kbit/s")
                         << QStringLiteral("50 kbit/s") << QStringLiteral("20 kbit/s")
                         << QStringLiteral("10 kbit/s");
}

int CanLink::bitrateAt(int index)
{
    if (index < 0 || index >= kBaudCount)
        return 500000;
    return kBauds[index].rate;
}

bool CanLink::loadLibrary()
{
#ifdef Q_OS_WIN
    if (d->loaded)
        return true;

    // 优先找程序目录，其次找 CANTest 安装目录（本机实测路径）
    const QStringList candidates = {
        QStringLiteral("ControlCAN.dll"),
        QStringLiteral("E:/CANTest/ControlCAN.dll"),
    };

    for (const QString &path : candidates) {
        d->lib.setFileName(path);
        if (!d->lib.load())
            continue;

        d->openFn = reinterpret_cast<Impl::FnOpen>(d->lib.resolve("VCI_OpenDevice"));
        d->closeFn = reinterpret_cast<Impl::FnClose>(d->lib.resolve("VCI_CloseDevice"));
        d->initFn = reinterpret_cast<Impl::FnInit>(d->lib.resolve("VCI_InitCAN"));
        d->startFn = reinterpret_cast<Impl::FnStart>(d->lib.resolve("VCI_StartCAN"));
        d->resetFn = reinterpret_cast<Impl::FnReset>(d->lib.resolve("VCI_ResetCAN"));
        d->infoFn = reinterpret_cast<Impl::FnInfo>(d->lib.resolve("VCI_ReadBoardInfo"));
        d->transmitFn = reinterpret_cast<Impl::FnTransmit>(d->lib.resolve("VCI_Transmit"));
        d->receiveFn = reinterpret_cast<Impl::FnReceive>(d->lib.resolve("VCI_Receive"));

        if (d->openFn && d->initFn && d->startFn && d->transmitFn && d->receiveFn) {
            d->loaded = true;
            return true;
        }
        d->lib.unload();
    }

    m_lastError = QStringLiteral("找不到 ControlCAN.dll（需与 exe 同目录，或装在 E:/CANTest）");
    return false;
#else
    m_lastError = QStringLiteral("当前平台不支持 ControlCAN");
    return false;
#endif
}

void CanLink::unloadLibrary()
{
#ifdef Q_OS_WIN
    if (d->lib.isLoaded())
        d->lib.unload();
    d->loaded = false;
#endif
}

bool CanLink::open(const Config &cfg)
{
    if (m_open)
        close();

    if (!loadLibrary())
        return false;

#ifdef Q_OS_WIN
    if (d->openFn(cfg.deviceType, cfg.deviceIndex, 0) != 1) {
        m_lastError = QStringLiteral("打开失败（DevType=%1）。检查：盒子是否插好、是否被 CANTest 占用")
                          .arg(cfg.deviceType);
        return false;
    }

    if (d->infoFn) {
        Impl::BoardInfo info;
        std::memset(&info, 0, sizeof(info));
        if (d->infoFn(cfg.deviceType, cfg.deviceIndex, &info) == 1) {
            m_boardInfo = QStringLiteral("%1 · SN %2 · 通道 %3")
                              .arg(QString::fromLocal8Bit(info.str_hw_Type, 40).trimmed())
                              .arg(QString::fromLocal8Bit(info.str_Serial_Num, 20).trimmed())
                              .arg(info.can_Num);
        }
    }

    int t0 = 0x00;
    int t1 = 0x1C;
    if (!timingFor(cfg.bitrate, &t0, &t1)) {
        m_lastError = QStringLiteral("不支持的波特率：%1").arg(cfg.bitrate);
        d->closeFn(cfg.deviceType, cfg.deviceIndex);
        return false;
    }

    Impl::InitConfig ic;
    std::memset(&ic, 0, sizeof(ic));
    ic.AccCode = 0x00000000;
    ic.AccMask = 0xFFFFFFFF;   // 全接收
    ic.Filter = 1;
    ic.Timing0 = static_cast<BYTE>(t0);
    ic.Timing1 = static_cast<BYTE>(t1);
    ic.Mode = 0;               // 0=正常，1=只听

    if (d->initFn(cfg.deviceType, cfg.deviceIndex, cfg.channel, &ic) != 1) {
        m_lastError = QStringLiteral("初始化通道失败（通道 %1）").arg(cfg.channel);
        if (d->closeFn)
            d->closeFn(cfg.deviceType, cfg.deviceIndex);
        return false;
    }
    if (d->startFn(cfg.deviceType, cfg.deviceIndex, cfg.channel) != 1) {
        m_lastError = QStringLiteral("启动通道失败");
        if (d->closeFn)
            d->closeFn(cfg.deviceType, cfg.deviceIndex);
        return false;
    }

    m_cfg = cfg;
    m_open = true;
    m_rx = 0;
    m_tx = 0;
    m_timer->start();
    emit stateChanged(true);
    emit countersChanged(0, 0);
    return true;
#else
    Q_UNUSED(cfg)
    return false;
#endif
}
void CanLink::close()
{
    if (!m_open)
        return;
    m_timer->stop();
    m_open = false;
#ifdef Q_OS_WIN
    if (d->closeFn)
        d->closeFn(m_cfg.deviceType, m_cfg.deviceIndex);
#endif
    emit stateChanged(false);
}

void CanLink::poll()
{
#ifdef Q_OS_WIN
    if (!m_open || !d->receiveFn)
        return;

    Impl::CanObj buf[64];
    const ULONG n = d->receiveFn(m_cfg.deviceType, m_cfg.deviceIndex, m_cfg.channel,
                                 buf, 64, 0);
    if (n == 0)
        return;

    for (ULONG i = 0; i < n && i < 64; ++i) {
        CanFrame f;
        f.timestampMs = QDateTime::currentMSecsSinceEpoch();
        f.tx = false;
        f.extended = buf[i].ExternFlag != 0;
        f.remote = buf[i].RemoteFlag != 0;
        f.id = buf[i].ID & (f.extended ? 0x1FFFFFFFu : 0x7FFu);
        const int dlc = qBound(0, int(buf[i].DataLen), 8);
        f.data = QByteArray(reinterpret_cast<const char *>(buf[i].Data), dlc);
        ++m_rx;
        emit frameReady(f);
    }
    emit countersChanged(m_rx, m_tx);
#endif
}

bool CanLink::send(quint32 id, bool extended, bool remote, const QByteArray &data)
{
#ifdef Q_OS_WIN
    if (!m_open || !d->transmitFn)
        return false;

    Impl::CanObj obj;
    std::memset(&obj, 0, sizeof(obj));
    obj.ID = id;
    obj.SendType = 0;
    obj.RemoteFlag = remote ? 1 : 0;
    obj.ExternFlag = extended ? 1 : 0;

    const int len = qBound(0, data.size(), 8);
    obj.DataLen = static_cast<BYTE>(len);
    if (len > 0)
        std::memcpy(obj.Data, data.constData(), static_cast<size_t>(len));

    const ULONG sent = d->transmitFn(m_cfg.deviceType, m_cfg.deviceIndex, m_cfg.channel,
                                     &obj, 1);
    if (sent < 1)
        return false;

    CanFrame f;
    f.timestampMs = QDateTime::currentMSecsSinceEpoch();
    f.tx = true;
    f.extended = extended;
    f.remote = remote;
    f.id = id;
    f.data = data.left(8);
    ++m_tx;
    emit frameReady(f);
    emit countersChanged(m_rx, m_tx);
    return true;
#else
    Q_UNUSED(id) Q_UNUSED(extended) Q_UNUSED(remote) Q_UNUSED(data)
    return false;
#endif
}

} // namespace busscope
