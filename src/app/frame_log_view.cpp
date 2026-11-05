#include "app/frame_log_view.h"

#include <QCheckBox>
#include <QDateTime>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableView>
#include <QTextStream>
#include <QTextEdit>
#include <QVBoxLayout>

#include "app/icons.h"
#include "app/theme.h"
#include "app/widget_helpers.h"
#include "core/frame.h"
#include "core/frame_log_model.h"

namespace busscope {

FrameLogView::FrameLogView(FrameLogModel *model, QWidget *parent)
    : QWidget(parent), m_model(model)
{
    setObjectName(QStringLiteral("CenterArea"));

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    v->addWidget(buildToolbar());

    // 帧表
    m_table = new QTableView;
    m_table->setModel(m_model);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->setWordWrap(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(30);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);

    auto *hh = m_table->horizontalHeader();
    hh->setHighlightSections(false);
    hh->setSectionResizeMode(QHeaderView::Fixed);
    hh->resizeSection(FrameLogModel::ColTime, 96);
    hh->resizeSection(FrameLogModel::ColDir, 48);
    hh->resizeSection(FrameLogModel::ColSlave, 48);
    hh->resizeSection(FrameLogModel::ColFunction, 62);
    hh->resizeSection(FrameLogModel::ColType, 96);
    hh->resizeSection(FrameLogModel::ColData, 236);
    hh->setSectionResizeMode(FrameLogModel::ColSummary, QHeaderView::Stretch);

    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &FrameLogView::showFrameDetail);

    // 空态提示：贴住 viewport，跟随尺寸变化
    m_emptyHint = new QLabel(QStringLiteral("等待数据 —— 打开串口后，用右侧「常用指令」发起一次请求"),
                             m_table->viewport());
    m_emptyHint->setObjectName(QStringLiteral("EmptyHint"));
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_emptyHint->setGeometry(m_table->viewport()->rect());
    m_table->viewport()->installEventFilter(this);

    // 详情区固定高度：QSplitter 在窗口高度不足时会把子控件压到 minHeight 以下，
    // 这里直接用固定高度布局，保证 3 列始终完整可见。
    auto *detailPane = buildDetailPane();
    detailPane->setFixedHeight(200);

    v->addWidget(m_table, 1);
    v->addWidget(detailPane);
}

QWidget *FrameLogView::buildToolbar()
{
    auto *bar = new QWidget;
    bar->setFixedHeight(40);
    bar->setStyleSheet(QStringLiteral("background:#101113;border-bottom:1px solid #1D1E22;"));
    auto *bh = new QHBoxLayout(bar);
    bh->setContentsMargins(12, 0, 12, 0);
    bh->setSpacing(10);

    m_autoScroll = new QCheckBox(QStringLiteral("自动滚动"));
    m_autoScroll->setChecked(true);
    bh->addWidget(m_autoScroll);

    auto *clearBtn = new QPushButton(QStringLiteral("清空"));
    clearBtn->setObjectName(QStringLiteral("Ghost"));
    clearBtn->setCursor(Qt::PointingHandCursor);
    applyIcon(clearBtn, icons::trash(theme::textSecondary()));
    connect(clearBtn, &QPushButton::clicked, this, &FrameLogView::clearLog);
    bh->addWidget(clearBtn);

    auto *exportBtn = new QPushButton(QStringLiteral("导出 CSV"));
    exportBtn->setObjectName(QStringLiteral("Ghost"));
    exportBtn->setCursor(Qt::PointingHandCursor);
    applyIcon(exportBtn, icons::exportTray(theme::textSecondary()));
    connect(exportBtn, &QPushButton::clicked, this, &FrameLogView::exportLog);
    bh->addWidget(exportBtn);

    bh->addStretch(1);

    m_frameCount = new QLabel(QStringLiteral("0 帧"));
    m_frameCount->setObjectName(QStringLiteral("Hint"));
    bh->addWidget(m_frameCount);
    return bar;
}

QWidget *FrameLogView::buildDetailPane()
{
    auto *w = new QWidget;
    w->setMinimumHeight(190);
    w->setStyleSheet(QStringLiteral("background:#101113;border-top:1px solid #1D1E22;"));

    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(12, 10, 12, 10);
    h->setSpacing(20);

    // 字段解析
    auto *colA = new QWidget;
    colA->setFixedWidth(270);
    auto *va = new QVBoxLayout(colA);
    va->setContentsMargins(0, 0, 0, 0);
    va->setSpacing(6);

    m_detailTitle = new QLabel(QStringLiteral("帧详情"));
    m_detailTitle->setObjectName(QStringLiteral("SectionTitle"));
    va->addWidget(m_detailTitle);

    m_fieldView = new QTextEdit;
    m_fieldView->setReadOnly(true);
    m_fieldView->setFrameShape(QFrame::NoFrame);
    m_fieldView->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
    va->addWidget(m_fieldView, 1);
    h->addWidget(colA);

    // 原始字节
    auto *colB = new QWidget;
    auto *vb = new QVBoxLayout(colB);
    vb->setContentsMargins(0, 0, 0, 0);
    vb->setSpacing(6);

    auto *hexTitle = new QLabel(QStringLiteral("原始字节"));
    hexTitle->setObjectName(QStringLiteral("SectionTitle"));
    vb->addWidget(hexTitle);

    m_hexView = new QTextEdit;
    m_hexView->setReadOnly(true);
    m_hexView->setFrameShape(QFrame::NoFrame);
    m_hexView->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
    vb->addWidget(m_hexView, 1);
    h->addWidget(colB, 1);

    // 时序
    auto *colC = new QWidget;
    colC->setFixedWidth(190);
    auto *vc = new QVBoxLayout(colC);
    vc->setContentsMargins(0, 0, 0, 0);
    vc->setSpacing(4);

    auto *timingTitle = new QLabel(QStringLiteral("时序"));
    timingTitle->setObjectName(QStringLiteral("SectionTitle"));
    vc->addWidget(timingTitle);

    m_timingValue = new QLabel(QStringLiteral("--"));
    m_timingValue->setStyleSheet(QStringLiteral("font-family:Consolas;font-size:20px;color:#E8E8EA;"));
    vc->addWidget(m_timingValue);

    m_timingCaption = new QLabel(QStringLiteral("尚未选中帧"));
    m_timingCaption->setObjectName(QStringLiteral("Hint"));
    vc->addWidget(m_timingCaption);

    m_timingMetrics = new QLabel;
    m_timingMetrics->setObjectName(QStringLiteral("Hint"));
    m_timingMetrics->setWordWrap(true);
    vc->addWidget(m_timingMetrics);
    vc->addStretch(1);
    h->addWidget(colC);

    return w;
}

void FrameLogView::onFrameReady(const busscope::Frame &frame)
{
    m_model->append(frame);
    if (m_emptyHint)
        m_emptyHint->setVisible(false);
    if (m_autoScroll->isChecked())
        m_table->scrollToBottom();
    m_frameCount->setText(QStringLiteral("%1 帧").arg(m_model->rowCount()));
}

void FrameLogView::appendNote(const QString &text)
{
    busscope::Frame f;
    f.timestampMs = QDateTime::currentMSecsSinceEpoch();
    f.dir = Direction::Note;
    f.kind = FrameKind::CrcError;
    f.typeName = QStringLiteral("提示");
    f.summary = text;
    f.detail = QStringLiteral("说明|%1").arg(text);
    m_model->append(f);

    emit statusMessage(text, 4000);
    m_frameCount->setText(QStringLiteral("%1 帧").arg(m_model->rowCount()));
}

void FrameLogView::clearLog()
{
    m_model->clear();
    m_frameCount->setText(QStringLiteral("0 帧"));
    if (m_emptyHint)
        m_emptyHint->setVisible(true);
    m_fieldView->clear();
    m_hexView->clear();
    m_detailTitle->setText(QStringLiteral("帧详情"));
    m_timingValue->setText(QStringLiteral("--"));
    m_timingCaption->setText(QStringLiteral("尚未选中帧"));
    m_timingMetrics->clear();
}

void FrameLogView::exportLog()
{
    const int rows = m_model->rowCount();
    if (rows == 0) {
        emit statusMessage(QStringLiteral("当前没有可导出的帧"), 3000);
        return;
    }

    const QString suggested = QStringLiteral("busscope-%1.csv")
                                  .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    QString selectedFilter;
    const QString path = QFileDialog::getSaveFileName(
        window(), QStringLiteral("导出帧记录"), suggested,
        QStringLiteral("CSV 文件 (*.csv);;文本文件 (*.txt)"), &selectedFilter);
    if (path.isEmpty())
        return;

    const bool asText = path.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive)
                        || selectedFilter.startsWith(QStringLiteral("文本文件"));

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        appendNote(QStringLiteral("导出失败：%1").arg(file.errorString()));
        return;
    }
    file.write("\xEF\xBB\xBF", 3); // UTF-8 BOM，Excel / 记事本直接打开不乱码

    QTextStream out(&file);
    out.setCodec("UTF-8");

    if (asText) {
        // 制表符分隔的纯文本：记事本直接看，粘到 Excel/剪贴板也能按列对齐
        out << QStringLiteral("BusScope 帧记录\n")
            << QStringLiteral("导出时间\t%1\n")
                   .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
            << QStringLiteral("共 %1 条\n\n").arg(rows)
            << QStringLiteral("序号\t时间\t方向\t从站\t功能码\t类型\t数据(HEX)\t解析\n");

        for (int row = 0; row < rows; ++row) {
            const busscope::Frame *f = m_model->frameAt(row);
            if (!f)
                continue;

            const QString dir = (f->dir == Direction::Tx) ? QStringLiteral("TX")
                                : (f->dir == Direction::Rx ? QStringLiteral("RX") : QStringLiteral("--"));
            QString summary = f->summary;
            if (f->elapsedMs >= 0)
                summary += QStringLiteral(" (%1 ms)").arg(f->elapsedMs);

            out << f->index << QLatin1Char('\t')
                << QDateTime::fromMSecsSinceEpoch(f->timestampMs).toString(QStringLiteral("HH:mm:ss.zzz")) << QLatin1Char('\t')
                << dir << QLatin1Char('\t')
                << QString::number(f->slave) << QLatin1Char('\t')
                << QStringLiteral("0x%1").arg(f->function, 2, 16, QChar('0')).toUpper() << QLatin1Char('\t')
                << f->typeName << QLatin1Char('\t')
                << QString::fromLatin1(f->raw.toHex(' ').toUpper()) << QLatin1Char('\t')
                << summary << QLatin1Char('\n');
        }
    } else {
        out << QStringLiteral("序号,时间,方向,从站,功能码,类型,数据(HEX),解析\n");

        for (int row = 0; row < rows; ++row) {
            const busscope::Frame *f = m_model->frameAt(row);
            if (!f)
                continue;

            const QString dir = (f->dir == Direction::Tx) ? QStringLiteral("TX")
                                : (f->dir == Direction::Rx ? QStringLiteral("RX") : QStringLiteral("--"));
            QString summary = f->summary;
            if (f->elapsedMs >= 0)
                summary += QStringLiteral(" (%1 ms)").arg(f->elapsedMs);
            summary.replace(QLatin1Char('"'), QStringLiteral("\"\""));

            out << f->index << QLatin1Char(',')
                << QDateTime::fromMSecsSinceEpoch(f->timestampMs).toString(QStringLiteral("HH:mm:ss.zzz")) << QLatin1Char(',')
                << dir << QLatin1Char(',')
                << QString::number(f->slave) << QLatin1Char(',')
                << QString::number(f->function) << QLatin1Char(',')
                << f->typeName << QLatin1Char(',')
                << QString::fromLatin1(f->raw.toHex(' ').toUpper()) << QLatin1Char(',')
                << QLatin1Char('"') << summary << QLatin1Char('"') << QLatin1Char('\n');
        }
    }
    file.close();

    emit statusMessage(
        QStringLiteral("已导出 %1 条记录（%2）").arg(rows).arg(asText ? QStringLiteral("TXT") : QStringLiteral("CSV")),
        5000);
}

void FrameLogView::showFrameDetail(const QModelIndex &current, const QModelIndex &previous)
{
    Q_UNUSED(previous)

    const busscope::Frame *f = m_model->frameAt(current.row());
    if (!f) {
        clearLog();
        return;
    }

    const QString dirText = (f->dir == Direction::Tx) ? QStringLiteral("TX")
                            : (f->dir == Direction::Rx ? QStringLiteral("RX") : QStringLiteral("--"));
    m_detailTitle->setText(QStringLiteral("帧 #%1 · %2").arg(f->index).arg(dirText));

    QString html = QStringLiteral("<table cellspacing='0' cellpadding='3'>");
    const QStringList rows = f->detail.split(QLatin1Char('\n'));
    for (const QString &row : rows) {
        if (row.isEmpty())
            continue;
        const int split = row.indexOf(QLatin1Char('|'));
        const QString key = (split > 0) ? row.left(split) : row;
        const QString value = (split > 0) ? row.mid(split + 1) : QString();
        html += QStringLiteral("<tr><td style='color:#7A7B80;white-space:nowrap'>%1</td>"
                               "<td style='color:#CCCCCC;padding-left:14px'>%2</td></tr>")
                    .arg(key.toHtmlEscaped(), value.toHtmlEscaped());
    }
    html += QStringLiteral("</table>");
    m_fieldView->setHtml(html);

    QString color = QStringLiteral("#CCCCCC");
    if (f->kind == FrameKind::Exception)
        color = QStringLiteral("#F85149");
    else if (f->kind == FrameKind::CrcError || f->kind == FrameKind::Timeout)
        color = QStringLiteral("#DCDCAA");
    else if (f->dir == Direction::Tx)
        color = QStringLiteral("#8B93F8");
    else if (f->dir == Direction::Rx)
        color = QStringLiteral("#4EC9B0");

    if (f->raw.isEmpty()) {
        m_hexView->setHtml(QStringLiteral("<span style='color:#6A6B70'>（无原始字节）</span>"));
    } else {
        m_hexView->setHtml(QStringLiteral("<div style='color:%1'>%2&nbsp;&nbsp;%3</div>")
                               .arg(color, dirText,
                                    QString::fromLatin1(f->raw.toHex(' ').toUpper())));
    }

    if (f->elapsedMs >= 0) {
        m_timingValue->setText(QStringLiteral("%1 ms").arg(f->elapsedMs));
        m_timingCaption->setText(QStringLiteral("请求 → 响应耗时"));
    } else {
        m_timingValue->setText(QStringLiteral("--"));
        m_timingCaption->setText(QStringLiteral("单向帧，无往返耗时"));
    }
    m_timingMetrics->setText(QStringLiteral("帧号 %1\n长度 %2 字节")
                                 .arg(f->index)
                                 .arg(f->raw.size()));
}

bool FrameLogView::eventFilter(QObject *watched, QEvent *event)
{
    if (m_table && watched == m_table->viewport()
        && event->type() == QEvent::Resize && m_emptyHint) {
        m_emptyHint->setGeometry(m_table->viewport()->rect());
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace busscope
