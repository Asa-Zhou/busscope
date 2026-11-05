#include "app/scope_panel.h"

#include <QDateTime>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QTableView>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <dwmapi.h>
#endif

#include "core/poll_task.h"
#include "io/poll_engine.h"

namespace busscope {

ScopePanel::ScopePanel(PollEngine *poll, QTableView *pollTable, QWidget *parent)
    : QWidget(parent), m_poll(poll), m_pollTable(pollTable)
{
    auto *wl = new QVBoxLayout(this);
    wl->setContentsMargins(12, 2, 12, 6);
    wl->setSpacing(3);

    wl->addWidget(buildHeadBar());

    m_wave = new WaveChart;
    m_wave->setFixedHeight(176);   // 多了底部时间轴滑块一行
    connect(m_wave, &WaveChart::followChanged, this, &ScopePanel::updateFollowUi);
    wl->addWidget(m_wave);

    m_waveTimer = new QTimer(this);
    m_waveTimer->setInterval(200);
    connect(m_waveTimer, &QTimer::timeout, this, &ScopePanel::refresh);
    m_waveTimer->start();
}

QWidget *ScopePanel::buildHeadBar()
{
    auto *waveHead = new QWidget;
    auto *whl = new QHBoxLayout(waveHead);
    whl->setContentsMargins(0, 0, 0, 0);
    whl->setSpacing(8);
    auto *waveTip = new QLabel(QStringLiteral("示波器 · 滚轮缩放  拖动平移  双击回最新  右键点表行加曲线"));
    waveTip->setObjectName(QStringLiteral("Hint"));
    whl->addWidget(waveTip);
    whl->addStretch(1);

    m_followBtn = new QPushButton(QStringLiteral("● 实时"));
    m_followBtn->setObjectName(QStringLiteral("GhostSmall"));
    m_followBtn->setCursor(Qt::PointingHandCursor);
    m_followBtn->setEnabled(false);      // 跟随时没什么可点的
    m_followBtn->setToolTip(QStringLiteral("回看历史时点这里跳回最新数据（也可双击波形）"));
    connect(m_followBtn, &QPushButton::clicked, this, [this] {
        if (m_wave)
            m_wave->followLatest();
        if (m_waveFloat)
            m_waveFloat->followLatest();
    });
    whl->addWidget(m_followBtn);

    auto *pauseBtn = new QPushButton(QStringLiteral("暂停"));
    pauseBtn->setObjectName(QStringLiteral("GhostSmall"));
    pauseBtn->setCursor(Qt::PointingHandCursor);
    pauseBtn->setCheckable(true);
    pauseBtn->setToolTip(QStringLiteral("冻结画面看细节，数据仍在后台累积"));
    connect(pauseBtn, &QPushButton::toggled, this, [this, pauseBtn](bool on) {
        if (m_wave)
            m_wave->setPaused(on);
        if (m_waveFloat)
            m_waveFloat->setPaused(on);
        pauseBtn->setText(on ? QStringLiteral("继续") : QStringLiteral("暂停"));
    });
    whl->addWidget(pauseBtn);

    auto *scaleBtn = new QPushButton(QStringLiteral("各自归一"));
    scaleBtn->setObjectName(QStringLiteral("GhostSmall"));
    scaleBtn->setCursor(Qt::PointingHandCursor);
    scaleBtn->setCheckable(true);
    scaleBtn->setChecked(false);
    scaleBtn->setToolTip(QStringLiteral("统一刻度：共用 Y 轴看真实数值；各自归一：只看形状"));
    connect(scaleBtn, &QPushButton::toggled, this, [this, scaleBtn](bool on) {
        if (m_wave)
            m_wave->setUnifiedScale(on);
        if (m_waveFloat)
            m_waveFloat->setUnifiedScale(on);
        scaleBtn->setText(on ? QStringLiteral("统一刻度") : QStringLiteral("各自归一"));
    });
    whl->addWidget(scaleBtn);

    auto *expBtn = new QPushButton(QStringLiteral("导出"));
    expBtn->setObjectName(QStringLiteral("GhostSmall"));
    expBtn->setCursor(Qt::PointingHandCursor);
    expBtn->setToolTip(QStringLiteral("把当前波形存成图片或数据文件"));
    connect(expBtn, &QPushButton::clicked, this, [this, expBtn] {
        QMenu menu(this);
        menu.addAction(QStringLiteral("保存为图片 (PNG)"), this, [this] { exportImage(); });
        menu.addAction(QStringLiteral("导出数据 (CSV)"), this, [this] { exportData(false); });
        menu.addAction(QStringLiteral("导出数据 (TXT / Tab 分隔)"), this, [this] { exportData(true); });
        menu.exec(expBtn->mapToGlobal(QPoint(0, expBtn->height() + 2)));
    });
    whl->addWidget(expBtn);

    auto *popBtn = new QPushButton(QStringLiteral("独立窗口"));
    popBtn->setObjectName(QStringLiteral("GhostSmall"));
    popBtn->setCursor(Qt::PointingHandCursor);
    popBtn->setToolTip(QStringLiteral("把示波器弹出为可缩放的独立窗口"));
    connect(popBtn, &QPushButton::clicked, this, &ScopePanel::toggleWindow);
    whl->addWidget(popBtn);

    return waveHead;
}

void ScopePanel::updateFollowUi(bool following)
{
    if (!m_followBtn)
        return;
    m_followBtn->setText(following ? QStringLiteral("● 实时")
                                   : QStringLiteral("回到最新"));
    m_followBtn->setEnabled(!following);
}

void ScopePanel::refresh()
{
    if (!m_wave)
        return;

    static const QColor kColors[4] = {
        QColor(0x7C, 0x86, 0xF0),   // 靛蓝
        QColor(0x4E, 0xC9, 0xB0),   // 青绿
        QColor(0xDC, 0xDC, 0xAA),   // 琥珀
        QColor(0xC5, 0x86, 0xC0),   // 紫
    };

    QList<WaveSeries> list = collectSeries();

    // 通道列表为空时，回退成"预览当前选中行"，随手看一个值不用先右键
    if (list.isEmpty() && m_poll && m_pollTable) {
        const QModelIndex idx = m_pollTable->currentIndex();
        PollTask *t = idx.isValid() ? m_poll->taskAt(idx.row()) : nullptr;
        if (t && !t->history.isEmpty()) {
            WaveSeries w;
            w.name = QStringLiteral("%1 · %2").arg(t->name).arg(t->addr, 4, 10, QChar('0'));
            w.unit = t->unit;
            w.color = kColors[0];
            w.values.reserve(t->history.size());
            w.times.reserve(t->history.size());
            for (const WaveSample &smp : t->history) {
                w.values.append(smp.value);
                w.times.append(smp.tMs);
            }
            list.append(w);
        }
    }

    const QString emptyHint =
        QStringLiteral("右键点表任意一行 → 添加到示波器（最多 4 条曲线）");

    if (list.isEmpty())
        m_wave->clear(emptyHint);
    else
        m_wave->setSeries(list);

    // 独立窗口存在且可见时同步刷新
    if (m_waveFloat && m_scopeWin && m_scopeWin->isVisible()) {
        if (list.isEmpty())
            m_waveFloat->clear(emptyHint);
        else
            m_waveFloat->setSeries(list);
    }
}

QList<WaveSeries> ScopePanel::collectSeries()
{
    static const QColor kColors[4] = {
        QColor(0x7C, 0x86, 0xF0),
        QColor(0x4E, 0xC9, 0xB0),
        QColor(0xDC, 0xDC, 0xAA),
        QColor(0xC5, 0x86, 0xC0),
    };

    QList<WaveSeries> out;
    if (!m_poll)
        return out;

    for (int i = 0; i < m_scopeRows.size(); ++i) {
        PollTask *t = m_poll->taskAt(m_scopeRows.at(i));
        if (!t)
            continue;

        WaveSeries w;
        w.name = QStringLiteral("%1 · %2").arg(t->name).arg(t->addr, 4, 10, QChar('0'));
        w.unit = t->unit;
        w.color = kColors[out.size() % 4];
        w.values.reserve(t->history.size());
        w.times.reserve(t->history.size());
        for (const WaveSample &smp : t->history) {
            w.values.append(smp.value);
            w.times.append(smp.tMs);
        }
        out.append(w);
    }
    return out;
}

void ScopePanel::addRow(int row)
{
    if (!m_poll || row < 0 || row >= m_poll->taskCount())
        return;

    if (m_scopeRows.contains(row)) {
        emit statusMessage(QStringLiteral("该行已经在示波器里了"), 2500);
        return;
    }
    if (m_scopeRows.size() >= 4) {
        emit statusMessage(QStringLiteral("示波器最多 4 条曲线，先移除一条"), 3500);
        return;
    }

    m_scopeRows.append(row);
    refresh();

    const PollTask *t = m_poll->taskAt(row);
    if (t)
        emit statusMessage(QStringLiteral("已加入示波器：%1").arg(t->name), 2500);
}

void ScopePanel::removeRow(int row)
{
    if (!m_scopeRows.removeOne(row))
        return;
    refresh();
    emit statusMessage(QStringLiteral("已从示波器移除"), 2500);
}

void ScopePanel::clearRows()
{
    m_scopeRows.clear();
}

void ScopePanel::toggleWindow()
{
    if (!m_scopeWin) {
        m_scopeWin = new QDialog(window());
        m_scopeWin->setWindowTitle(QStringLiteral("BusScope · 示波器"));
        m_scopeWin->resize(980, 520);
        m_scopeWin->setAttribute(Qt::WA_DeleteOnClose, false);

        m_scopeWin->setStyleSheet(QStringLiteral("QDialog{background:#0F1013;}"));

        auto *l = new QVBoxLayout(m_scopeWin);
        l->setContentsMargins(10, 10, 10, 10);
        m_waveFloat = new WaveChart;
        connect(m_waveFloat, &WaveChart::followChanged, this, &ScopePanel::updateFollowUi);
        l->addWidget(m_waveFloat);

#ifdef Q_OS_WIN
        // 对话框也要深色标题栏，否则一块白条特别跳
        HWND hwnd = reinterpret_cast<HWND>(m_scopeWin->winId());
        BOOL dark = TRUE;
        if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark))))
            DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
#endif
    }

    const bool show = !m_scopeWin->isVisible();
    if (show) {
        m_scopeWin->show();
        m_scopeWin->raise();
        m_scopeWin->activateWindow();
    } else {
        m_scopeWin->hide();
    }
    emit scopeWindowToggled(show);

    refresh();
}

void ScopePanel::exportImage()
{
    const QList<WaveSeries> list = collectSeries();
    if (list.isEmpty()) {
        emit statusMessage(QStringLiteral("示波器里还没有曲线"), 3000);
        return;
    }

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString path = QFileDialog::getSaveFileName(
        window(), QStringLiteral("保存波形图片"),
        QStringLiteral("busscope-wave-%1.png").arg(stamp),
        QStringLiteral("PNG 图片 (*.png)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive))
        path += QStringLiteral(".png");

    // 独立窗口开着就抓它（画面更大），否则抓嵌入区
    QWidget *src = (m_scopeWin && m_scopeWin->isVisible() && m_waveFloat)
                       ? static_cast<QWidget *>(m_waveFloat)
                       : static_cast<QWidget *>(m_wave);
    if (!src || !src->grab().save(path)) {
        emit linkError(QStringLiteral("保存图片失败"));
        return;
    }
    emit statusMessage(QStringLiteral("波形图片已保存"), 4000);
}

void ScopePanel::exportData(bool asText)
{
    const QList<WaveSeries> list = collectSeries();
    if (list.isEmpty()) {
        emit statusMessage(QStringLiteral("示波器里还没有数据"), 3000);
        return;
    }

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString path = QFileDialog::getSaveFileName(
        window(), QStringLiteral("导出波形数据"),
        QStringLiteral("busscope-wave-%1.%2").arg(stamp, asText ? QStringLiteral("txt")
                                                               : QStringLiteral("csv")),
        asText ? QStringLiteral("文本文件 (*.txt)") : QStringLiteral("CSV 文件 (*.csv)"));
    if (path.isEmpty())
        return;
    if (asText && !path.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive))
        path += QStringLiteral(".txt");
    if (!asText && !path.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive))
        path += QStringLiteral(".csv");

    const QChar sep = asText ? QLatin1Char('\t') : QLatin1Char(',');

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit linkError(QStringLiteral("导出失败：%1").arg(file.errorString()));
        return;
    }
    file.write("\xEF\xBB\xBF", 3);

    QTextStream out(&file);
    out.setCodec("UTF-8");
    out << QStringLiteral("时间") << sep << QStringLiteral("通道") << sep
        << QStringLiteral("数值") << sep << QStringLiteral("单位") << QLatin1Char('\n');

    int rows = 0;
    for (const WaveSeries &w : list) {
        for (int i = 0; i < w.values.size(); ++i) {
            const qint64 t = (i < w.times.size()) ? w.times.at(i) : 0;
            const QString ts = (t > 0)
                ? QDateTime::fromMSecsSinceEpoch(t).toString(QStringLiteral("HH:mm:ss.zzz"))
                : QString::number(i);
            out << ts << sep << w.name << sep
                << QString::number(w.values.at(i), 'f', 4) << sep
                << w.unit << QLatin1Char('\n');
            ++rows;
        }
    }
    file.close();

    emit statusMessage(
        QStringLiteral("已导出 %1 行波形数据（%2）").arg(rows)
            .arg(asText ? QStringLiteral("TXT") : QStringLiteral("CSV")),
        4000);
}

} // namespace busscope
