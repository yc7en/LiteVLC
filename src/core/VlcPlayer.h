#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QPair>
#include <cstdarg>
#include <qwindowdefs.h>

struct libvlc_instance_t;
struct libvlc_media_player_t;
struct libvlc_media_t;
struct libvlc_event_t;
struct vlc_log_t;

struct TrackInfo {
    int id = -1;
    QString name;
};

/**
 * @brief LibVLC 核心播放器封装类
 * 封装所有 LibVLC C API，提供线程安全的 Qt 信号槽接口：
 * - 视频渲染窗口绑定 (HWND)
 * - 播放 / 暂停 / 停止 / 跳转 (Seek) / 快进快退
 * - 音量控制 / 一键静音 / 多声轨枚举与切换
 * - 宽高比保持与切换 (默认原比例 / 16:9 / 4:3 等)
 * - 外挂字幕自动同名扫描挂载 / 手动挂载 / 字幕时间轴微调
 * - 0.5x ~ 3.0x 倍速播放 (不变调)
 * - GPU 硬件加速开关 (dxva2 / d3d11va / any / none)
 */
class VlcPlayer : public QObject
{
    Q_OBJECT
public:
    explicit VlcPlayer(QObject *parent = nullptr);
    ~VlcPlayer() override;

    bool isValid() const { return m_vlcInstance != nullptr && m_mediaPlayer != nullptr; }

    // 绑定视频渲染窗口句柄 (Windows HWND)
    void setVideoWindowId(WId winId);

    // 加载并播放本地媒体文件
    bool openMedia(const QString &filePath, bool hwAccelEnabled = true, const QString &hwBackend = QStringLiteral("any"));
    QString currentFilePath() const { return m_currentFilePath; }

    /**
     * @brief 加载并播放网络流（http/https / m3u8 / mp4 直链等）
     * @param userAgent 留空则不注入（多数源站会校验 UA）
     * @param referer   留空则不注入（不少 m3u8 有防盗链，必须带站点 Referer）
     */
    bool openUrl(const QString &url, const QString &userAgent, const QString &referer,
                 bool hwAccelEnabled = true, const QString &hwBackend = QStringLiteral("any"));

    /** 当前是否为网络流（网络流不参与本地进度记忆，URL 每次都不一样的） */
    bool isStreaming() const { return m_isStreaming; }
    QString currentUrl() const { return m_currentUrl; }

    /** 最近一次播放失败的原因（来自 LibVLC 自己的报错，判断用） */
    QString lastError() const { return m_recentErrors.join(QStringLiteral(" | ")); }

    // --- 1. 播放控制 ---
    void play();
    void pause();
    void togglePlayPause();
    void stop();

    bool isPlaying() const;
    qint64 currentTimeMs() const { return m_currentTimeMs; }
    qint64 durationMs() const { return m_durationMs; }

    void seekTime(qint64 timeMs);
    void seekRelative(qint64 deltaMs);

    // --- 2. 音量与声轨控制 ---
    int volume() const;
    void setVolume(int vol);
    void adjustVolume(int delta);

    bool isMuted() const;
    void setMute(bool mute);
    void toggleMute();

    QList<TrackInfo> audioTracks() const;
    int currentAudioTrack() const;
    void setAudioTrack(int trackId);

    // --- 3. 画面宽高比控制 ---
    void setAspectRatio(const QString &ratio); // 空字符串 "" 表示保持原视频比例
    QString aspectRatio() const { return m_aspectRatio; }

    // --- 4. 字幕控制与时间轴微调 ---
    bool addSubtitleFile(const QString &subFilePath);
    QList<TrackInfo> subtitleTracks() const;
    int currentSubtitleTrack() const;
    void setSubtitleTrack(int trackId);

    // 字幕延迟微调 (毫秒，正值延后，负值提前)
    qint64 subtitleDelayMs() const;
    void setSubtitleDelayMs(qint64 delayMs);
    void adjustSubtitleDelayMs(qint64 deltaMs);

    // --- 5. 倍速播放 (0.5x ~ 3.0x) ---
    float rate() const;
    void setRate(float playbackRate);

signals:
    void stateChanged(bool playing);
    void timeChanged(qint64 currentMs);
    void durationChanged(qint64 totalMs);
    void tracksUpdated();
    void endReached();
    void osdMessageRequested(const QString &text);
    /** 播放内核报错（403 / 格式不支持 / 无法连接等），message 已尽量翻成人话 */
    void errorOccurred(const QString &message);

private:
    static void handleVlcEvent(const struct libvlc_event_t *event, void *userData);
    static void vlcLogCallback(void *data, int level, const struct vlc_log_t *ctx, const char *fmt, va_list args);
    void appendVlcLog(const QString &line);
    QString friendlyError(const QString &raw) const;
    void attachEvents();
    void autoLoadExternalSubtitles(const QString &videoFilePath);

    libvlc_instance_t *m_vlcInstance = nullptr;
    libvlc_media_player_t *m_mediaPlayer = nullptr;

    WId m_videoWinId = 0;
    QString m_currentFilePath;
    QString m_currentUrl;
    bool m_isStreaming = false;
    QString m_aspectRatio;

    qint64 m_currentTimeMs = 0;
    qint64 m_durationMs = 0;
    qint64 m_subtitleDelayMs = 0;
    float m_rate = 1.0f;

    // LibVLC 内部报错（只留最近几条），用于把失败原因喂给 UI
    QStringList m_recentErrors;
};
