#pragma once

#include <QString>
#include <QVector>

#include "core/poll_task.h"

namespace busscope {

/// 点表 CSV/TXT 读取：按行自动识别分隔符（Tab / 逗号 / 分号 / 连续空白）。
/// 失败时 *err 给出可读原因（文件打不开 / 无有效行），返回空列表。
QVector<PollTask> loadPointFile(const QString &path, QString *err);

/// 点表导出：asText 为 true 用 Tab 分隔，否则逗号；带 UTF-8 BOM。
/// 只负责文件内容，界面侧的空表判断与提示由调用方处理。
bool savePointFile(const QString &path, const QVector<PollTask> &tasks, bool asText, QString *err);

} // namespace busscope
