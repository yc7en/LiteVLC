#include "ConfigManager.h"

#include "AppPaths.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

ConfigManager& ConfigManager::instance()
{
    static ConfigManager inst;
    return inst;
}

ConfigManager::ConfigManager(QObject *parent)
    : QObject(parent)
{
    load();
}

QString ConfigManager::configFilePath() const
{
    // 程序目录可写就就地存（绿色版），Program Files 安装版回退到 %APPDATA%
    return AppPaths::filePath(QStringLiteral("player_config.json"));
}

QString ConfigManager::normalizePath(const QString &filePath)
{
    return QFileInfo(filePath).absoluteFilePath().toLower();
}

void ConfigManager::load()
{
    QFile file(configFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        return;
    }

    QJsonObject root = doc.object();
    m_autoResumeEnabled = root.value(QStringLiteral("autoResumeEnabled")).toBool(true);
    m_hwAccelEnabled    = root.value(QStringLiteral("hwAccelEnabled")).toBool(true);
    m_hwAccelBackend    = root.value(QStringLiteral("hwAccelBackend")).toString(QStringLiteral("any"));

    m_skipIntroEnabled  = root.value(QStringLiteral("skipIntroEnabled")).toBool(false);
    m_skipIntroSeconds  = root.value(QStringLiteral("skipIntroSeconds")).toInt(90);
    m_skipOutroEnabled  = root.value(QStringLiteral("skipOutroEnabled")).toBool(false);
    m_skipOutroSeconds  = root.value(QStringLiteral("skipOutroSeconds")).toInt(120);

    m_volume            = root.value(QStringLiteral("volume")).toInt(80);
    m_stayOnTop         = root.value(QStringLiteral("stayOnTop")).toBool(false);
    m_playMode          = static_cast<PlayMode>(root.value(QStringLiteral("playMode")).toInt(LoopList));

    m_historyPositions.clear();
    QJsonObject histObj = root.value(QStringLiteral("history")).toObject();
    for (auto it = histObj.begin(); it != histObj.end(); ++it) {
        m_historyPositions.insert(it.key(), static_cast<qint64>(it.value().toDouble(0)));
    }
}

void ConfigManager::save()
{
    QJsonObject root;
    root[QStringLiteral("autoResumeEnabled")] = m_autoResumeEnabled;
    root[QStringLiteral("hwAccelEnabled")]    = m_hwAccelEnabled;
    root[QStringLiteral("hwAccelBackend")]    = m_hwAccelBackend;

    root[QStringLiteral("skipIntroEnabled")]  = m_skipIntroEnabled;
    root[QStringLiteral("skipIntroSeconds")]  = m_skipIntroSeconds;
    root[QStringLiteral("skipOutroEnabled")]  = m_skipOutroEnabled;
    root[QStringLiteral("skipOutroSeconds")]  = m_skipOutroSeconds;

    root[QStringLiteral("volume")]            = m_volume;
    root[QStringLiteral("stayOnTop")]         = m_stayOnTop;
    root[QStringLiteral("playMode")]          = static_cast<int>(m_playMode);

    QJsonObject histObj;
    for (auto it = m_historyPositions.cbegin(); it != m_historyPositions.cend(); ++it) {
        histObj[it.key()] = static_cast<double>(it.value());
    }
    root[QStringLiteral("history")] = histObj;

    QFile file(configFilePath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
    }
}

void ConfigManager::setAutoResumeEnabled(bool enabled)
{
    m_autoResumeEnabled = enabled;
    save();
}

qint64 ConfigManager::getSavedPosition(const QString &filePath) const
{
    if (!m_autoResumeEnabled || filePath.isEmpty()) {
        return 0;
    }
    return m_historyPositions.value(normalizePath(filePath), 0);
}

void ConfigManager::savePosition(const QString &filePath, qint64 positionMs, qint64 durationMs)
{
    if (filePath.isEmpty() || durationMs <= 0) {
        return;
    }

    const QString key = normalizePath(filePath);
    // 若观看不足 5 秒，或距离视频结束不足 8 秒，视为从头开始（移除旧进度记录）
    if (positionMs < 5000 || (durationMs - positionMs) < 8000) {
        if (m_historyPositions.contains(key)) {
            m_historyPositions.remove(key);
            save();
        }
        return;
    }

    m_historyPositions.insert(key, positionMs);
    save();
}

void ConfigManager::clearHistory()
{
    m_historyPositions.clear();
    save();
}

void ConfigManager::setHwAccelEnabled(bool enabled)
{
    m_hwAccelEnabled = enabled;
    save();
}

void ConfigManager::setHwAccelBackend(const QString &backend)
{
    m_hwAccelBackend = backend;
    save();
}

void ConfigManager::setSkipIntroEnabled(bool enabled)
{
    m_skipIntroEnabled = enabled;
    save();
}

void ConfigManager::setSkipIntroSeconds(int sec)
{
    m_skipIntroSeconds = qMax(0, sec);
    save();
}

void ConfigManager::setSkipOutroEnabled(bool enabled)
{
    m_skipOutroEnabled = enabled;
    save();
}

void ConfigManager::setSkipOutroSeconds(int sec)
{
    m_skipOutroSeconds = qMax(0, sec);
    save();
}

void ConfigManager::setVolume(int vol)
{
    m_volume = qBound(0, vol, 100);
    save();
}

void ConfigManager::setStayOnTop(bool onTop)
{
    m_stayOnTop = onTop;
    save();
}

void ConfigManager::setPlayMode(PlayMode mode)
{
    m_playMode = mode;
    save();
}
