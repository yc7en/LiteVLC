#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include "ui/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("LiteVlcPlayer"));
    app.setApplicationVersion(QStringLiteral("1.0.0"));

    // 加载全局暗黑现代主题样式表
    QFile qssFile(QStringLiteral(":/icons/dark_modern.qss"));
    if (qssFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QString::fromUtf8(qssFile.readAll()));
        qssFile.close();
    }

    MainWindow window;
    window.show();

    // 支持命令行直接传入视频文件路径打开播放
    const QStringList args = app.arguments();
    if (args.size() > 1) {
        QStringList files;
        for (int i = 1; i < args.size(); ++i) {
            files.append(args.at(i));
        }
        window.addAndPlayFiles(files);
    }

    return app.exec();
}
