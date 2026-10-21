#include "AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QtGlobal>

namespace {

bool underProgramFiles(const QString &dir)
{
    const QStringList roots = {
        QDir::fromNativeSeparators(qEnvironmentVariable("ProgramFiles")),
        QDir::fromNativeSeparators(qEnvironmentVariable("ProgramFiles(x86)")),
        QDir::fromNativeSeparators(qEnvironmentVariable("ProgramW6432")),
    };
    for (const QString &root : roots) {
        if (!root.isEmpty() && dir.startsWith(root, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

} // namespace

namespace AppPaths {

QString dataDir()
{
    static QString cached;
    if (!cached.isEmpty()) {
        return cached;
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty() && !underProgramFiles(appDir) && QFileInfo(appDir).isWritable()) {
        cached = appDir; // 绿色版：就地存放
        return cached;
    }

    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) {
        dir = QDir::home().filePath(QStringLiteral("LiteVlcPlayer"));
    }
    QDir().mkpath(dir);
    cached = dir;
    return cached;
}

QString filePath(const QString &fileName)
{
    return QDir(dataDir()).filePath(fileName);
}

} // namespace AppPaths
