#include "UpdateService.h"

#include <QString>
#include <QtGlobal>

#ifndef APP_VERSION
#define APP_VERSION "1.1.0"
#endif

// 仓库 slug（owner/repo），由 CMake 变量 APP_UPDATE_REPO 注入。
// 留空表示尚未配置更新源，界面应提示而不是拼出一个无效 URL。
#ifndef APP_UPDATE_REPO
#define APP_UPDATE_REPO ""
#endif

QString UpdateService::currentVersion()
{
    return QStringLiteral(APP_VERSION);
}

QString UpdateService::platformTag()
{
#if defined(Q_OS_MAC)
#if defined(Q_PROCESSOR_X86_64)
    return QStringLiteral("macos-x64");
#else
    return QStringLiteral("macos-arm64");
#endif
#elif defined(Q_OS_WIN)
    return QStringLiteral("win-x64");
#else
    return QStringLiteral("linux-x64");
#endif
}

QString UpdateService::channelName()
{
    return QStringLiteral("stable");
}

QString UpdateService::repository()
{
    return QString::fromLatin1(APP_UPDATE_REPO).trimmed();
}

QString UpdateService::latestReleaseApiUrl()
{
    const QString repo = repository();
    if (repo.isEmpty()) {
        return QString();
    }
    return QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(repo);
}

QString UpdateService::releasePageUrl()
{
    const QString repo = repository();
    if (repo.isEmpty()) {
        return QString();
    }
    return QStringLiteral("https://github.com/%1/releases/latest").arg(repo);
}
