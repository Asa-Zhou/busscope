#include "app/poll_page.h"

#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QTableView>
#include <QVBoxLayout>

#include "app/icons.h"
#include "app/scope_panel.h"
#include "app/theme.h"
#include "app/widget_helpers.h"
#include "core/point_file.h"
#include "core/poll_table_model.h"
#include "core/poll_task.h"
#include "io/modbus_link.h"
#include "io/poll_engine.h"

namespace busscope {

PollPage::PollPage(PollEngine *poll, ModbusLink *link, QWidget *parent)
    : QWidget(parent), m_poll(poll), m_link(link)
{
    m_pollModel = new PollTableModel(m_poll, this);

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    v->addWidget(buildToolbar());

    m_pollTable = new QTableView;
    m_pollTable->setModel(m_pollModel);
    m_pollTable->setAlternatingRowColors(true);
    m_pollTable->setShowGrid(false);
    m_pollTable->setWordWrap(false);
    m_pollTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pollTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pollTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pollTable->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_pollTable->verticalHeader()->setVisible(false);
    m_pollTable->verticalHeader()->setDefaultSectionSize(28);
    m_pollTable->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);

    auto *hh = m_pollTable->horizontalHeader();
    hh->setHighlightSections(false);
    hh->setSectionResizeMode(QHeaderView::Fixed);
    hh->resizeSection(PollTableModel::ColEnable, 52);
    hh->resizeSection(PollTableModel::ColName, 170);
    hh->resizeSection(PollTableModel::ColSlave, 58);
    hh->resizeSection(PollTableModel::ColFunction, 92);
    hh->resizeSection(PollTableModel::ColAddr, 74);
    hh->resizeSection(PollTableModel::ColPeriod, 88);
    hh->resizeSection(PollTableModel::ColValue, 150);
    hh->resizeSection(PollTableModel::ColStatus, 96);
    hh->resizeSection(PollTableModel::ColCount, 92);
    // 让「名称」吃掉多余宽度，计数列保持紧凑（否则最后一列被拉得很空）
    hh->setSectionResizeMode(PollTableModel::ColName, QHeaderView::Stretch);

    connect(m_pollTable, &QTableView::doubleClicked, this, &PollPage::writeSelectedPoint);

    m_pollTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_pollTable, &QTableView::customContextMenuRequested,
            this, &PollPage::onPollContextMenu);

    v->addWidget(m_pollTable, 1);

    // ---- 示波器（嵌入形态）----
    m_scope = new ScopePanel(m_poll, m_pollTable, this);
    v->addWidget(m_scope);

    auto *hint = new QLabel(QStringLiteral("双击任意一行可直接写单寄存器（FC06）；CSV 列顺序："
                                           "名称,从站,功能码,地址,数量,周期ms,超时ms,重试,类型,系数,单位"));
    hint->setObjectName(QStringLiteral("Hint"));
    hint->setContentsMargins(12, 6, 12, 6);
    hint->setWordWrap(true);
    v->addWidget(hint);

    connect(m_poll, &PollEngine::stateChanged, this, &PollPage::onPollStateChanged);
    connect(m_poll, &PollEngine::summaryChanged, this, &PollPage::onPollSummary);
    connect(m_poll, &PollEngine::taskUpdated, this, &PollPage::onPollTaskUpdated);

    // 选中行变化时让示波器刷新预览曲线
    connect(m_pollTable->selectionModel(), &QItemSelectionModel::currentRowChanged,
            m_scope, &ScopePanel::refresh);

    // 示波器的提示与错误原样上抛
    connect(m_scope, &ScopePanel::statusMessage, this, &PollPage::statusMessage);
    connect(m_scope, &ScopePanel::linkError, this, &PollPage::linkError);
    connect(m_scope, &ScopePanel::scopeWindowToggled, this, [this](bool visible) {
        if (m_scopeBtn)
            m_scopeBtn->setText(visible ? QStringLiteral("隐藏示波器") : QStringLiteral("示波器"));
    });
}

QWidget *PollPage::buildToolbar()
{
    auto *bar = new QWidget;
    bar->setFixedHeight(40);
    bar->setStyleSheet(QStringLiteral("background:#101113;border-bottom:1px solid #1D1E22;"));
    auto *bh = new QHBoxLayout(bar);
    bh->setContentsMargins(12, 0, 12, 0);
    bh->setSpacing(8);

    m_pollBtn = new QPushButton(QStringLiteral("开始轮询"));
    m_pollBtn->setObjectName(QStringLiteral("Primary"));
    m_pollBtn->setCursor(Qt::PointingHandCursor);
    applyIcon(m_pollBtn, icons::play(Qt::white));
    connect(m_pollBtn, &QPushButton::clicked, this, &PollPage::togglePolling);
    bh->addWidget(m_pollBtn);

    m_pauseBtn = new QPushButton(QStringLiteral("暂停"));
    m_pauseBtn->setObjectName(QStringLiteral("Ghost"));
    m_pauseBtn->setCursor(Qt::PointingHandCursor);
    m_pauseBtn->setEnabled(false);
    applyIcon(m_pauseBtn, icons::pause(theme::textSecondary()));
    connect(m_pauseBtn, &QPushButton::clicked, this, &PollPage::togglePause);
    bh->addWidget(m_pauseBtn);

    auto *importBtn = new QPushButton(QStringLiteral("导入点表"));
    importBtn->setObjectName(QStringLiteral("Ghost"));
    importBtn->setCursor(Qt::PointingHandCursor);
    applyIcon(importBtn, icons::importTray(theme::textSecondary()));
    connect(importBtn, &QPushButton::clicked, this, &PollPage::importPoints);
    bh->addWidget(importBtn);

    auto *exportBtn = new QPushButton(QStringLiteral("导出点表"));
    exportBtn->setObjectName(QStringLiteral("Ghost"));
    exportBtn->setCursor(Qt::PointingHandCursor);
    applyIcon(exportBtn, icons::exportTray(theme::textSecondary()));
    // 点一下直接弹出格式选择，避免在保存对话框的过滤器里翻找不到 TXT
    connect(exportBtn, &QPushButton::clicked, this, [this, exportBtn] {
        QMenu menu(this);
        menu.addAction(QStringLiteral("导出为 CSV  (逗号分隔)"),
                       this, [this] { exportPointsAs(false); });
        menu.addAction(QStringLiteral("导出为 TXT  (Tab 分隔)"),
                       this, [this] { exportPointsAs(true); });
        menu.exec(exportBtn->mapToGlobal(QPoint(0, exportBtn->height() + 2)));
    });
    bh->addWidget(exportBtn);

    auto *sampleBtn = new QPushButton(QStringLiteral("示例点表"));
    sampleBtn->setObjectName(QStringLiteral("Ghost"));
    sampleBtn->setCursor(Qt::PointingHandCursor);
    applyIcon(sampleBtn, icons::folder(theme::textSecondary()));
    connect(sampleBtn, &QPushButton::clicked, this, &PollPage::loadSamplePoints);
    bh->addWidget(sampleBtn);

    auto *clearBtn = new QPushButton(QStringLiteral("清空"));
    clearBtn->setObjectName(QStringLiteral("Ghost"));
    clearBtn->setCursor(Qt::PointingHandCursor);
    applyIcon(clearBtn, icons::trash(theme::textSecondary()));
    connect(clearBtn, &QPushButton::clicked, this, &PollPage::clearPoints);
    bh->addWidget(clearBtn);

    m_scopeBtn = new QPushButton(QStringLiteral("示波器"));
    m_scopeBtn->setObjectName(QStringLiteral("Ghost"));
    m_scopeBtn->setCursor(Qt::PointingHandCursor);
    m_scopeBtn->setToolTip(QStringLiteral("打开/隐藏独立示波器窗口；右键点表行可添加曲线"));
    connect(m_scopeBtn, &QPushButton::clicked, m_scope, &ScopePanel::toggleWindow);
    bh->addWidget(m_scopeBtn);

    bh->addStretch(1);

    m_pollSummary = new QLabel(QStringLiteral("0 项"));
    m_pollSummary->setObjectName(QStringLiteral("Hint"));
    bh->addWidget(m_pollSummary);
    return bar;
}

bool PollPage::openPointFile(const QString &path)
{
    QString err;
    const QVector<PollTask> tasks = loadPointFile(path, &err);
    if (!err.isEmpty()) {
        emit linkError(err);
        return false;
    }

    m_poll->stop();
    m_poll->setTasks(tasks);
    m_pollModel->notifyAll();
    m_scope->clearRows();      // 行号已失效，示波器一起清掉
    m_scope->refresh();
    onPollSummary();

    emit requestShow();

    // 默认选中第一行：波形区不至于空着
    if (m_pollTable && m_pollModel->rowCount() > 0)
        m_pollTable->setCurrentIndex(m_pollModel->index(0, 0));

    {
        QSettings st;
        st.setValue(QStringLiteral("poll/lastFile"), path);
    }

    emit statusMessage(QStringLiteral("已导入 %1 项轮询任务").arg(tasks.size()), 4000);
    return true;
}

void PollPage::importPoints()
{
    const QString path = QFileDialog::getOpenFileName(
        window(), QStringLiteral("导入点表"), QString(),
        QStringLiteral("点表文件 (*.csv *.txt);;CSV 文件 (*.csv);;文本文件 (*.txt);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    openPointFile(path);
}

void PollPage::exportPointsAs(bool asText)
{
    if (m_poll->taskCount() == 0) {
        emit statusMessage(QStringLiteral("点表为空"), 3000);
        return;
    }

    QString path = QFileDialog::getSaveFileName(
        window(),
        asText ? QStringLiteral("导出为 TXT") : QStringLiteral("导出为 CSV"),
        asText ? QStringLiteral("busscope-points.txt") : QStringLiteral("busscope-points.csv"),
        asText ? QStringLiteral("文本文件 (*.txt)") : QStringLiteral("CSV 文件 (*.csv)"));

    if (path.isEmpty())
        return;

    // 用户没敲后缀时补上，避免导出成无扩展名文件
    if (asText && !path.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive))
        path += QStringLiteral(".txt");
    if (!asText && !path.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive))
        path += QStringLiteral(".csv");

    QVector<PollTask> tasks;
    tasks.reserve(m_poll->taskCount());
    for (int i = 0; i < m_poll->taskCount(); ++i) {
        const PollTask *t = m_poll->taskAt(i);
        if (t)
            tasks.append(*t);
    }

    QString err;
    if (!savePointFile(path, tasks, asText, &err)) {
        emit linkError(err);
        return;
    }

    emit statusMessage(
        QStringLiteral("已导出 %1 项（%2）").arg(tasks.size())
            .arg(asText ? QStringLiteral("TXT / Tab 分隔") : QStringLiteral("CSV")),
        4000);
}

void PollPage::loadSamplePoints()
{
    QVector<PollTask> tasks;

    PollTask v;
    v.name = QStringLiteral("母线电压");
    v.slave = 1;
    v.function = 0x03;
    v.addr = 0;
    v.count = 1;
    v.periodMs = 500;
    v.type = ValueType::UInt16;
    v.scale = 0.1;
    v.unit = QStringLiteral("V");
    tasks.append(v);

    PollTask i2;
    i2.name = QStringLiteral("母线电流");
    i2.slave = 1;
    i2.function = 0x03;
    i2.addr = 1;
    i2.count = 1;
    i2.periodMs = 500;
    i2.type = ValueType::Int16;
    i2.scale = 0.1;
    i2.unit = QStringLiteral("A");
    tasks.append(i2);

    PollTask soc;
    soc.name = QStringLiteral("SOC");
    soc.slave = 1;
    soc.function = 0x03;
    soc.addr = 2;
    soc.count = 1;
    soc.periodMs = 1000;
    soc.type = ValueType::UInt16;
    soc.scale = 0.1;
    soc.unit = QStringLiteral("%");
    tasks.append(soc);

    m_poll->stop();
    m_poll->setTasks(tasks);
    m_pollModel->notifyAll();
    m_scope->clearRows();      // 行号已失效，示波器一起清掉
    m_scope->refresh();
    onPollSummary();
    if (m_pollTable && m_pollModel->rowCount() > 0)
        m_pollTable->setCurrentIndex(m_pollModel->index(0, 0));
    emit statusMessage(QStringLiteral("已载入示例点表，改成本机从站地址后即可启动"), 5000);
}

void PollPage::clearPoints()
{
    m_scope->clearRows();
    m_scope->refresh();
    m_poll->clearTasks();
    m_pollModel->notifyAll();
    onPollSummary();
}

void PollPage::togglePolling()
{
    if (m_poll->isRunning()) {
        m_poll->stop();
        return;
    }
    // 周期发送与轮询互斥由主窗口处理；直连信号同步生效，先于 start
    emit pollingStarting();
    m_poll->start();
}

void PollPage::togglePause()
{
    if (!m_poll->isRunning())
        return;
    m_poll->setPaused(!m_poll->isPaused());
}

void PollPage::writeSelectedPoint()
{
    const QModelIndex idx = m_pollTable->currentIndex();
    if (!idx.isValid()) {
        emit statusMessage(QStringLiteral("先选中一行再写"), 3000);
        return;
    }

    PollTask *t = m_poll->taskAt(idx.row());
    if (!t)
        return;

    bool ok = false;
    const int current = t->raw.isEmpty() ? 0 : static_cast<int>(t->raw.first());
    const int value = QInputDialog::getInt(
        window(), QStringLiteral("写单寄存器"),
        QStringLiteral("%1\n从站 %2 · 地址 %3（FC06 写保持寄存器）\n写入值 0~65535：")
            .arg(t->name)
            .arg(t->slave)
            .arg(t->addr),
        current, 0, 65535, 1, &ok);
    if (!ok)
        return;

    if (m_poll->isRunning()) {
        m_poll->enqueueWrite(t->slave, t->addr, static_cast<quint16>(value));
        emit statusMessage(QStringLiteral("写请求已排队"), 3000);
        return;
    }

    if (!m_link->isOpen()) {
        emit linkError(QStringLiteral("请先打开串口"));
        return;
    }

    QByteArray req;
    req.append(static_cast<char>(t->slave));
    req.append(static_cast<char>(0x06));
    req.append(static_cast<char>((t->addr >> 8) & 0xFF));
    req.append(static_cast<char>(t->addr & 0xFF));
    req.append(static_cast<char>((value >> 8) & 0xFF));
    req.append(static_cast<char>(value & 0xFF));
    m_link->send(req, true);
}

void PollPage::onPollContextMenu(const QPoint &pos)
{
    if (!m_pollTable)
        return;

    const QModelIndex idx = m_pollTable->indexAt(pos);
    if (!idx.isValid())
        return;

    const int row = idx.row();

    QMenu menu(this);
    QAction *actAdd = menu.addAction(QStringLiteral("添加到示波器"));
    QAction *actDel = menu.addAction(QStringLiteral("从示波器移除"));
    menu.addSeparator();
    QAction *actWin   = menu.addAction(QStringLiteral("打开独立示波器窗口"));
    QAction *actClear = menu.addAction(QStringLiteral("清空示波器"));
    menu.addSeparator();
    QAction *actPng   = menu.addAction(QStringLiteral("波形存为图片 (PNG)"));
    QAction *actCsv   = menu.addAction(QStringLiteral("导出波形数据 (CSV)"));
    menu.addSeparator();
    QAction *actWrite = menu.addAction(QStringLiteral("写此寄存器…"));

    actAdd->setEnabled(!m_scope->containsRow(row) && m_scope->seriesCount() < 4);
    actDel->setEnabled(m_scope->containsRow(row));
    actClear->setEnabled(m_scope->seriesCount() > 0);
    actPng->setEnabled(m_scope->seriesCount() > 0);
    actCsv->setEnabled(m_scope->seriesCount() > 0);

    QAction *chosen = menu.exec(m_pollTable->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == actAdd) {
        m_scope->addRow(row);
    } else if (chosen == actDel) {
        m_scope->removeRow(row);
    } else if (chosen == actWin) {
        m_scope->toggleWindow();
    } else if (chosen == actClear) {
        m_scope->clearRows();
        m_scope->refresh();
        emit statusMessage(QStringLiteral("示波器已清空"), 2500);
    } else if (chosen == actPng) {
        m_scope->exportImage();
    } else if (chosen == actCsv) {
        m_scope->exportData(false);
    } else if (chosen == actWrite) {
        m_pollTable->setCurrentIndex(idx);
        writeSelectedPoint();
    }
}

void PollPage::onPollStateChanged(bool running, bool paused)
{
    if (m_pollBtn) {
        m_pollBtn->setText(running ? QStringLiteral("停止轮询") : QStringLiteral("开始轮询"));
        applyIcon(m_pollBtn, running ? icons::stop(Qt::white) : icons::play(Qt::white));
    }
    if (m_pauseBtn) {
        m_pauseBtn->setEnabled(running);
        m_pauseBtn->setText(paused ? QStringLiteral("继续") : QStringLiteral("暂停"));
    }
}

void PollPage::onPollSummary()
{
    if (!m_pollSummary)
        return;
    m_pollSummary->setText(QStringLiteral("%1 项 · 成功 %2 · 失败 %3 · 请求 %4")
                               .arg(m_poll->taskCount())
                               .arg(m_poll->okTotal())
                               .arg(m_poll->errTotal())
                               .arg(m_poll->requestCount()));
}

void PollPage::onPollTaskUpdated(int row)
{
    m_pollModel->notifyRow(row);
}

} // namespace busscope
