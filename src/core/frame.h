#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace busscope {

/// 帧方向。Note 用于工具自身插入的提示行（超时、错误说明等）。
enum class Direction {
    Tx,
    Rx,
    Note
};

/// 帧的语义分类，决定表格着色。
enum class FrameKind {
    Data,       ///< 正常数据帧
    Exception,  ///< Modbus 异常响应（功能码最高位置 1）
    CrcError,   ///< CRC 校验失败
    Timeout,    ///< 请求无响应
    Raw         ///< 无法按协议解析的原始字节
};

/// 一条总线记录。UI 层只读，由 ModbusLink 生产。
struct Frame {
    quint64    index       = 0;      ///< 全局帧号，从 1 开始
    qint64     timestampMs = 0;      ///< 本地时间的 epoch 毫秒
    Direction  dir         = Direction::Rx;
    FrameKind  kind        = FrameKind::Data;
    quint8     slave       = 0;
    quint8     function    = 0;
    QByteArray raw;                  ///< 原始字节（不含工具附加信息）
    QString    typeName;             ///< 功能码中文名，如“读保持寄存器”
    QString    summary;              ///< 单行摘要，进表格「解析」列
    QString    detail;               ///< 多行字段，格式 "键|值"，进详情面板
    int        elapsedMs   = -1;     ///< 请求到响应的耗时，-1 表示未知

    /// 响应帧由请求推导出的起始地址 / 数量，用于还原 4xxxx 点表编号。
    bool       hasRef      = false;
    quint16    refAddr     = 0;
    quint16    refQty      = 0;

    bool isError() const
    {
        return kind == FrameKind::Exception
            || kind == FrameKind::CrcError
            || kind == FrameKind::Timeout;
    }
};

} // namespace busscope
