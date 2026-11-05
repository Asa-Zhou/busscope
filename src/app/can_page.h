#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableView;

namespace busscope {

class CanFrameModel;
class CanLink;
class DbcDatabase;
struct CanFrame;

/// CAN 页：连接配置条 + 帧列表 + 发送条。
/// 自持 CanLink / 帧模型 / DBC 数据库，与其他页面没有直接引用。
class CanPage : public QWidget
{
    Q_OBJECT

public:
    explicit CanPage(QWidget *parent = nullptr);

    /// 命令行直连：同步界面参数后打开通道，返回是否成功。
    bool openDirect(quint32 deviceType, quint32 channel, int bitrate);

    /// 载入 DBC（命令行 / 拖放 / 按钮共用）。
    bool loadDbcFile(const QString &path);

signals:
    void statusMessage(const QString &text, int ms);
    void linkError(const QString &text);      ///< 失败要进帧记录（主窗口路由到 Slave 页）

private:
    void buildUi();

    void loadDbc();
    void toggleCan();
    void sendCan();
    void clearCanFrames();
    void onCanFrame(const busscope::CanFrame &frame);
    void onCanState(bool open);
    void onCanCounters(quint64 rx, quint64 tx);

    CanLink       *m_can      = nullptr;
    CanFrameModel *m_canModel = nullptr;
    DbcDatabase   *m_dbc      = nullptr;

    QTableView  *m_canTable      = nullptr;
    QComboBox   *m_canTypeBox    = nullptr;
    QComboBox   *m_canChannelBox = nullptr;
    QComboBox   *m_canBaudBox    = nullptr;
    QPushButton *m_canBtn        = nullptr;
    QPushButton *m_dbcBtn        = nullptr;
    QLabel      *m_canStatus     = nullptr;
    QLabel      *m_canCount      = nullptr;
    QLineEdit   *m_canIdEdit     = nullptr;
    QLineEdit   *m_canDataEdit   = nullptr;
    QCheckBox   *m_canExtBox     = nullptr;
    QCheckBox   *m_canRemoteBox  = nullptr;
};

} // namespace busscope
