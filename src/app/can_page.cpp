#include "app/can_page.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QTableView>
#include <QVBoxLayout>

#include "app/icons.h"
#include "app/theme.h"
#include "app/widget_helpers.h"
#include "core/can_frame.h"
#include "core/can_frame_model.h"
#include "io/can_link.h"
#include "proto/can/dbc.h"

namespace busscope {

CanPage::CanPage(QWidget *parent)
    : QWidget(parent)
{
    m_can = new CanLink(this);
    m_canModel = new CanFrameModel(this);

    buildUi();

    connect(m_can, &CanLink::frameReady, this, &CanPage::onCanFrame);
    connect(m_can, &CanLink::stateChanged, this, &CanPage::onCanState);
    connect(m_can, &CanLink::countersChanged, this, &CanPage::onCanCounters);
}

void CanPage::buildUi()
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto *bar = new QWidget;
    bar->setFixedHeight(40);
    bar->setStyleSheet(QStringLiteral("background:#101113;border-bottom:1px solid #1D1E22;"));
    auto *bh = new QHBoxLayout(bar);
    bh->setContentsMargins(12, 0, 12, 0);
    bh->setSpacing(8);

    m_canTypeBox = new QComboBox;
    m_canTypeBox->addItem(QStringLiteral("USB-CAN II (DevType 4)"), QVariant(4u));
    m_canTypeBox->addItem(QStringLiteral("USB-CAN I (DevType 3)"), QVariant(3u));
    m_canTypeBox->addItem(QStringLiteral("CANalyst-II (DevType 21)"), QVariant(21u));
    m_canTypeBox->setMinimumWidth(180);

    m_canChannelBox = new QComboBox;
    m_canChannelBox->addItem(QStringLiteral("通道 0"), QVariant(0u));
    m_canChannelBox->addItem(QStringLiteral("通道 1"), QVariant(1u));
    m_canChannelBox->setMinimumWidth(80);

    m_canBaudBox = new QComboBox;
    const QStringList baudLabels = CanLink::bitrateLabels();
    for (int i = 0; i < baudLabels.size(); ++i)
        m_canBaudBox->addItem(baudLabels.at(i), CanLink::bitrateAt(i));
    m_canBaudBox->setCurrentIndex(2);   // 500 kbit/s
    m_canBaudBox->setMinimumWidth(112);

    bh->addWidget(m_canTypeBox);
    bh->addWidget(m_canChannelBox);
    bh->addWidget(m_canBaudBox);

    m_canBtn = new QPushButton(QStringLiteral("打开 CAN"));
    m_canBtn->setObjectName(QStringLiteral("Primary"));
    m_canBtn->setCursor(Qt::PointingHandCursor);
    applyIcon(m_canBtn, icons::power(Qt::white));
    connect(m_canBtn, &QPushButton::clicked, this, &CanPage::toggleCan);
    bh->addWidget(m_canBtn);

    auto *clearBtn = new QPushButton(QStringLiteral("清空"));
    clearBtn->setObjectName(QStringLiteral("Ghost"));
    clearBtn->setCursor(Qt::PointingHandCursor);
    applyIcon(clearBtn, icons::trash(theme::textSecondary()));
    connect(clearBtn, &QPushButton::clicked, this, &CanPage::clearCanFrames);
    bh->addWidget(clearBtn);

    m_dbcBtn = new QPushButton(QStringLiteral("载入 DBC"));
    m_dbcBtn->setObjectName(QStringLiteral("Ghost"));
    m_dbcBtn->setCursor(Qt::PointingHandCursor);
    m_dbcBtn->setToolTip(QStringLiteral("载入 .dbc 后，帧表右侧会把原始字节解成信号值"));
    applyIcon(m_dbcBtn, icons::importTray(theme::textSecondary()));
    connect(m_dbcBtn, &QPushButton::clicked, this, &CanPage::loadDbc);
    bh->addWidget(m_dbcBtn);

    bh->addStretch(1);

    m_canStatus = new QLabel(QStringLiteral("未打开"));
    m_canStatus->setObjectName(QStringLiteral("Hint"));
    bh->addWidget(m_canStatus);

    m_canCount = new QLabel(QStringLiteral("RX 0 · TX 0"));
    m_canCount->setObjectName(QStringLiteral("Hint"));
    bh->addWidget(m_canCount);
    v->addWidget(bar);

    m_canTable = new QTableView;
    m_canTable->setModel(m_canModel);
    m_canTable->setAlternatingRowColors(true);
    m_canTable->setShowGrid(false);
    m_canTable->setWordWrap(false);
    m_canTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_canTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_canTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_canTable->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_canTable->verticalHeader()->setVisible(false);
    m_canTable->verticalHeader()->setDefaultSectionSize(26);
    m_canTable->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);

    auto *chh = m_canTable->horizontalHeader();
    chh->setHighlightSections(false);
    chh->setSectionResizeMode(QHeaderView::Fixed);
    chh->resizeSection(CanFrameModel::ColTime, 104);
    chh->resizeSection(CanFrameModel::ColDir, 60);
    chh->resizeSection(CanFrameModel::ColId, 104);
    chh->resizeSection(CanFrameModel::ColType, 92);
    chh->resizeSection(CanFrameModel::ColDlc, 60);
    chh->resizeSection(CanFrameModel::ColData, 236);
    chh->resizeSection(CanFrameModel::ColDecoded, 260);
    chh->setSectionResizeMode(CanFrameModel::ColDecoded, QHeaderView::Stretch);
    v->addWidget(m_canTable, 1);

    auto *sendBar = new QWidget;
    sendBar->setFixedHeight(48);
    sendBar->setStyleSheet(QStringLiteral("background:#101113;border-top:1px solid #1D1E22;"));
    auto *sh = new QHBoxLayout(sendBar);
    sh->setContentsMargins(12, 0, 12, 0);
    sh->setSpacing(8);

    auto *idLabel = new QLabel(QStringLiteral("ID"));
    idLabel->setObjectName(QStringLiteral("FieldLabel"));
    m_canIdEdit = new QLineEdit(QStringLiteral("0x301"));
    m_canIdEdit->setFixedWidth(100);

    m_canExtBox = new QCheckBox(QStringLiteral("扩展帧"));
    m_canRemoteBox = new QCheckBox(QStringLiteral("远程帧"));

    auto *dataLabel = new QLabel(QStringLiteral("数据"));
    dataLabel->setObjectName(QStringLiteral("FieldLabel"));
    m_canDataEdit = new QLineEdit(QStringLiteral("11 22 33 44 55 66 77 88"));
    m_canDataEdit->setPlaceholderText(QStringLiteral("最多 8 字节，空格分隔"));

    auto *sendBtn = new QPushButton(QStringLiteral("发送"));
    sendBtn->setObjectName(QStringLiteral("Primary"));
    sendBtn->setCursor(Qt::PointingHandCursor);
    applyIcon(sendBtn, icons::send(Qt::white));
    connect(sendBtn, &QPushButton::clicked, this, &CanPage::sendCan);

    sh->addWidget(idLabel);
    sh->addWidget(m_canIdEdit);
    sh->addWidget(m_canExtBox);
    sh->addWidget(m_canRemoteBox);
    sh->addWidget(dataLabel);
    sh->addWidget(m_canDataEdit, 1);
    sh->addWidget(sendBtn);
    v->addWidget(sendBar);
}

void CanPage::loadDbc()
{
    const QString path = QFileDialog::getOpenFileName(
        window(), QStringLiteral("载入 DBC"), QString(),
        QStringLiteral("DBC 文件 (*.dbc);;所有文件 (*.*)"));
    if (!path.isEmpty())
        loadDbcFile(path);
}

bool CanPage::loadDbcFile(const QString &path)
{
    auto *db = new DbcDatabase;
    QString err;
    if (!db->loadFile(path, &err)) {
        delete db;
        emit linkError(QStringLiteral("DBC 载入失败：%1").arg(err));
        return false;
    }

    delete m_dbc;
    m_dbc = db;
    m_canModel->setDatabase(m_dbc);

    if (m_dbcBtn)
        m_dbcBtn->setText(QStringLiteral("DBC：%1").arg(m_dbc->fileName()));

    emit statusMessage(
        QStringLiteral("已载入 %1 条报文定义（%2）")
            .arg(m_dbc->messageCount()).arg(m_dbc->fileName()),
        6000);
    return true;
}

void CanPage::toggleCan()
{
    if (m_can->isOpen()) {
        m_can->close();
        return;
    }

    CanLink::Config cfg;
    cfg.deviceType = m_canTypeBox->currentData().toUInt();
    cfg.channel = m_canChannelBox->currentData().toUInt();
    cfg.bitrate = m_canBaudBox->currentData().toInt();

    if (!m_can->open(cfg)) {
        m_canStatus->setText(m_can->lastError());
        m_canStatus->setObjectName(QStringLiteral("StatusBad"));
        m_canStatus->style()->unpolish(m_canStatus);
        m_canStatus->style()->polish(m_canStatus);
        emit statusMessage(m_can->lastError(), 8000);
    }
}

void CanPage::onCanState(bool open)
{
    m_canBtn->setText(open ? QStringLiteral("关闭 CAN") : QStringLiteral("打开 CAN"));
    m_canTypeBox->setEnabled(!open);
    m_canChannelBox->setEnabled(!open);
    m_canBaudBox->setEnabled(!open);

    if (open) {
        const QString info = m_can->boardInfo();
        m_canStatus->setText(info.isEmpty() ? QStringLiteral("已打开") : info);
        m_canStatus->setObjectName(QStringLiteral("StatusOk"));
    } else {
        m_canStatus->setText(QStringLiteral("未打开"));
        m_canStatus->setObjectName(QStringLiteral("Hint"));
    }
    m_canStatus->style()->unpolish(m_canStatus);
    m_canStatus->style()->polish(m_canStatus);
}

void CanPage::onCanFrame(const busscope::CanFrame &frame)
{
    m_canModel->append(frame);
    m_canTable->scrollToBottom();
}

void CanPage::onCanCounters(quint64 rx, quint64 tx)
{
    m_canCount->setText(QStringLiteral("RX %1 · TX %2").arg(rx).arg(tx));
}

void CanPage::clearCanFrames()
{
    m_canModel->clear();
}

void CanPage::sendCan()
{
    if (!m_can->isOpen()) {
        emit statusMessage(QStringLiteral("先打开 CAN 通道"), 3000);
        return;
    }

    QString idText = m_canIdEdit->text().trimmed();
    if (idText.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
        idText = idText.mid(2);

    bool idOk = false;
    const uint id = idText.toUInt(&idOk, 16);
    if (!idOk) {
        emit statusMessage(QStringLiteral("ID 不是合法十六进制"), 3000);
        return;
    }

    bool dataOk = false;
    const QByteArray data = hexToBytes(m_canDataEdit->text(), &dataOk);
    if (!dataOk || data.size() > 8) {
        emit statusMessage(QStringLiteral("数据必须是 0~8 字节的十六进制"), 3000);
        return;
    }

    if (!m_can->send(id, m_canExtBox->isChecked(), m_canRemoteBox->isChecked(), data))
        emit statusMessage(QStringLiteral("发送失败"), 3000);
}

bool CanPage::openDirect(quint32 deviceType, quint32 channel, int bitrate)
{
    const int typeIdx = m_canTypeBox->findData(QVariant(deviceType));
    if (typeIdx >= 0)
        m_canTypeBox->setCurrentIndex(typeIdx);

    const int chIdx = m_canChannelBox->findData(QVariant(channel));
    if (chIdx >= 0)
        m_canChannelBox->setCurrentIndex(chIdx);

    const int baudIdx = m_canBaudBox->findData(QVariant(bitrate));
    if (baudIdx >= 0)
        m_canBaudBox->setCurrentIndex(baudIdx);

    if (!m_can->isOpen())
        toggleCan();

    return m_can->isOpen();
}

} // namespace busscope
