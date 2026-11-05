#include "core/point_file.h"

#include <QFile>
#include <QRegExp>
#include <QTextStream>

namespace busscope {

QVector<PollTask> loadPointFile(const QString &path, QString *err)
{
    QVector<PollTask> tasks;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (err)
            *err = QStringLiteral("导入失败：%1").arg(file.errorString());
        return tasks;
    }

    QTextStream in(&file);
    in.setCodec("UTF-8");

    int lineNo = 0;
    bool headerSkipped = false;

    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        ++lineNo;
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        // CSV / TXT 都支持：按行自动识别分隔符（Tab / 逗号 / 分号 / 连续空白）
        QStringList cols;
        if (line.contains(QLatin1Char('\t')))
            cols = line.split(QLatin1Char('\t'));
        else if (line.contains(QLatin1Char(',')))
            cols = line.split(QLatin1Char(','));
        else if (line.contains(QLatin1Char(';')))
            cols = line.split(QLatin1Char(';'));
        else
            cols = line.split(QRegExp(QStringLiteral("\\s+")), QString::SkipEmptyParts);
        if (!headerSkipped && lineNo == 1) {
            headerSkipped = true;
            if (cols.value(0).contains(QStringLiteral("名称"))
                || cols.value(0).compare(QStringLiteral("name"), Qt::CaseInsensitive) == 0)
                continue;
        }
        if (cols.size() < 4)
            continue;

        PollTask t;
        t.name = cols.value(0).trimmed();
        t.slave = static_cast<quint8>(cols.value(1).toUInt());
        t.function = static_cast<quint8>(cols.value(2).toUInt());
        t.addr = static_cast<quint16>(cols.value(3).toUInt());
        t.count = static_cast<quint16>(cols.value(4).toUInt());
        t.periodMs = cols.value(5).toInt();
        t.timeoutMs = cols.value(6).toInt();
        t.retries = cols.value(7).toInt();
        t.type = valueTypeFromName(cols.value(8));
        t.scale = cols.value(9).isEmpty() ? 1.0 : cols.value(9).toDouble();
        t.unit = cols.value(10).trimmed();

        if (t.name.isEmpty())
            t.name = QStringLiteral("点 %1").arg(tasks.size() + 1);
        if (t.slave == 0)
            t.slave = 1;
        if (t.function < 0x01 || t.function > 0x04)
            t.function = 0x03;
        if (t.count == 0)
            t.count = static_cast<quint16>(qMax(1, registersForType(t.type)));
        if (t.periodMs < 20)
            t.periodMs = 500;
        if (t.timeoutMs < 20)
            t.timeoutMs = 200;
        if (t.retries < 0)
            t.retries = 0;

        tasks.append(t);
    }
    file.close();

    if (tasks.isEmpty() && err)
        *err = QStringLiteral("CSV 里没有解析出有效行");
    return tasks;
}

bool savePointFile(const QString &path, const QVector<PollTask> &tasks, bool asText, QString *err)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (err)
            *err = QStringLiteral("导出失败：%1").arg(file.errorString());
        return false;
    }
    file.write("\xEF\xBB\xBF", 3); // UTF-8 BOM，Excel / 记事本直接打开不乱码

    const QChar sep = asText ? QLatin1Char('\t') : QLatin1Char(',');

    QTextStream out(&file);
    out.setCodec("UTF-8");
    out << QStringLiteral("名称") << sep << QStringLiteral("从站") << sep
        << QStringLiteral("功能码") << sep << QStringLiteral("地址") << sep
        << QStringLiteral("数量") << sep << QStringLiteral("周期ms") << sep
        << QStringLiteral("超时ms") << sep << QStringLiteral("重试") << sep
        << QStringLiteral("类型") << sep << QStringLiteral("系数") << sep
        << QStringLiteral("单位") << QLatin1Char('\n');

    for (const PollTask &t : tasks) {
        out << t.name << sep
            << t.slave << sep
            << t.function << sep
            << t.addr << sep
            << t.count << sep
            << t.periodMs << sep
            << t.timeoutMs << sep
            << t.retries << sep
            << valueTypeName(t.type) << sep
            << QString::number(t.scale, 'f', 4) << sep
            << t.unit << QLatin1Char('\n');
    }
    file.close();
    return true;
}

} // namespace busscope
