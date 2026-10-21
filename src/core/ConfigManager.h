#pragma once

#include <QObject>
#include <QString>
#include <QHash>

/**
 * @brief 全局配置与历史续播管理类
 * 负责持久化存储：
 * 1. 视频历史播放进度 (自动续播 / 记忆播放)
 * 2. GPU 硬件加速开关与后端选择 (dxva2 / d3d11va / any / none)
 * 3. 跳过片头 / 片尾开关与秒数
 * 4. 音量、置顶状态、播放循环模式
 */
class ConfigManager : public QObject
{
    Q_OBJECT
public:
    enum PlayMode {
        Sequential = 0, // 顺序播放
        LoopList   = 1, // 列表循环
        LoopOne    = 2  // 单曲循环
    };

    static ConfigManager& instance();

    void load();
    void save();

    // --- 历史播放进度 (记忆续播) ---
    bool autoResumeEnabled() const { return m_autoResumeEnabled; }
    void setAutoResumeEnabled(bool enabled);

    qint64 getSavedPosition(const QString &filePath) const;
    void savePosition(const QString &filePath, qint64 positionMs, qint64 durationMs);
    void clearHistory();

    // --- GPU 硬件加速配置 ---
    bool hwAccelEnabled() const { return m_hwAccelEnabled; }
    void setHwAccelEnabled(bool enabled);

    QString hwAccelBackend() const { return m_hwAccelBackend; }
    void setHwAccelBackend(const QString &backend);

    // --- 跳过片头片尾配置 ---
    bool skipIntroEnabled() const { return m_skipIntroEnabled; }
    void setSkipIntroEnabled(bool enabled);
    int skipIntroSeconds() const { return m_skipIntroSeconds; }
    void setSkipIntroSeconds(int sec);

    bool skipOutroEnabled() const { return m_skipOutroEnabled; }
    void setSkipOutroEnabled(bool enabled);
    int skipOutroSeconds() const { return m_skipOutroSeconds; }
    void setSkipOutroSeconds(int sec);

    // --- 基础播放器状态 ---
    int volume() const { return m_volume; }
    void setVolume(int vol);

    bool stayOnTop() const { return m_stayOnTop; }
    void setStayOnTop(bool onTop);

    PlayMode playMode() const { return m_playMode; }
    void setPlayMode(PlayMode mode);

private:
    explicit ConfigManager(QObject *parent = nullptr);
    ~ConfigManager() override = default;

    QString configFilePath() const;
    static QString normalizePath(const QString &filePath);

    bool m_autoResumeEnabled = true;
    bool m_hwAccelEnabled = true;
    QString m_hwAccelBackend = QStringLiteral("any"); // "any", "d3d11va", "dxva2", "none"

    bool m_skipIntroEnabled = false;
    int m_skipIntroSeconds = 90;

    bool m_skipOutroEnabled = false;
    int m_skipOutroSeconds = 120;

    int m_volume = 80;
    bool m_stayOnTop = false;
    PlayMode m_playMode = LoopList;

    QHash<QString, qint64> m_historyPositions;
};
