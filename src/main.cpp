#include <QApplication>

#include "app/icons.h"
#include "app/main_window.h"
#include "app/theme.h"

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("BusScope"));
    QApplication::setOrganizationName(QStringLiteral("BusScope"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    busscope::theme::apply(app);
    app.setWindowIcon(busscope::icons::app());

    busscope::MainWindow window;
    window.show();

    // 命令行参数：
    //   xxx.csv              打开点表
    //   --modbus COM3:115200 直连串口（8-N-1）
    //   --poll               打开串口后立即开始轮询
    //   --can 4:0:500000     直连 CAN
    const QStringList args = QApplication::arguments();
    QString csvPath, portSpec, canSpec, dbcPath;
    bool wantPoll = false;

    for (int i = 1; i < args.size(); ++i) {
        const QString arg = args.at(i);

        // 点表文件：CSV 或 TXT（.txt 按 Tab / 空白分隔解析）
        if (arg.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)
            || arg.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive)) {
            csvPath = arg;
        } else if (arg == QStringLiteral("--modbus") && i + 1 < args.size()) {
            portSpec = args.at(++i);
        } else if (arg == QStringLiteral("--poll")) {
            wantPoll = true;
        } else if (arg == QStringLiteral("--can") && i + 1 < args.size()) {
            canSpec = args.at(++i);
        } else if (arg == QStringLiteral("--dbc") && i + 1 < args.size()) {
            dbcPath = args.at(++i);
        }
    }

    // 点表必须先加载，轮询才有任务可跑
    if (!csvPath.isEmpty())
        window.openPointFile(csvPath);

    if (!portSpec.isEmpty()) {
        const QStringList p = portSpec.split(QLatin1Char(':'));
        const QString port = p.value(0);
        const int baud = p.size() > 1 ? p.at(1).toInt() : 115200;
        window.openSerialDirect(port, baud > 0 ? baud : 115200, wantPoll);
    }

    if (!dbcPath.isEmpty())
        window.loadDbcFile(dbcPath);

    if (!canSpec.isEmpty()) {
        const QStringList parts = canSpec.split(QLatin1Char(':'));
        if (parts.size() == 3) {
            window.openCanDirect(parts.at(0).toUInt(),
                                 parts.at(1).toUInt(),
                                 parts.at(2).toInt());
        }
    }

    return app.exec();
}
