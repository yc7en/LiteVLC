#include "VlcPlayer.h"
#include "StreamProbe.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QUrl>
#include <cstdio>

#include <vlc/vlc.h>

VlcPlayer::VlcPlayer(QObject *parent)
    : QObject(parent)
{
    // 自动探测 plugins 目录并设置 VLC_PLUGIN_PATH 环境变量，免手动配置
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidatePluginDirs = {
        QDir(appDir).filePath(QStringLiteral("plugins")),
        QDir(appDir).filePath(QStringLiteral("vlc/plugins")),
        QDir(appDir).filePath(QStringLiteral("../vlc/plugins")),
        QStringLiteral("e:/02code/player/vlc/plugins")
    };

    for (const QString &dirPath : candidatePluginDirs) {
        if (QDir(dirPath).exists()) {
            qputenv("VLC_PLUGIN_PATH", QDir::toNativeSeparators(dirPath).toLocal8Bit());
            break;
        }
    }

    // 精简启动参数：禁用标题叠加、禁用统计、禁用网络隐私弹窗与屏幕保护干扰
    const char *vlcArgs[] = {
        "--no-video-title-show",
        "--no-stats",
        "--no-sub-autodetect-file", // 由我们自己的 QDir 精确扫描挂载，支持更灵活的同名匹配
        "--quiet"
    };

    m_vlcInstance = libvlc_new(sizeof(vlcArgs) / sizeof(vlcArgs[0]), vlcArgs);
    if (!m_vlcInstance) {
        return;
    }

    m_mediaPlayer = libvlc_media_player_new(m_vlcInstance);
    if (m_mediaPlayer) {
        // 关键：禁止 LibVLC 内部接管鼠标和键盘事件，将双击、滚轮、按键完全交还给 Qt QWidget
        libvlc_video_set_mouse_input(m_mediaPlayer, 0);
        libvlc_video_set_key_input(m_mediaPlayer, 0);
        attachEvents();
    }

    // 收一份内核日志：网络流失败时（403 / 无法连接 / 不支持格式）只有这里能拿到真实原因
    libvlc_log_set(m_vlcInstance, &VlcPlayer::vlcLogCallback, this);
}

VlcPlayer::~VlcPlayer()
{
    if (m_vlcInstance) {
        libvlc_log_unset(m_vlcInstance);
    }
    if (m_mediaPlayer) {
        libvlc_media_player_stop(m_mediaPlayer);
        libvlc_media_player_release(m_mediaPlayer);
        m_mediaPlayer = nullptr;
    }
    if (m_vlcInstance) {
        libvlc_release(m_vlcInstance);
        m_vlcInstance = nullptr;
    }
}

void VlcPlayer::setVideoWindowId(WId winId)
{
    m_videoWinId = winId;
    if (!m_mediaPlayer || winId == 0) {
        return;
    }
#if defined(Q_OS_WIN)
    libvlc_media_player_set_hwnd(m_mediaPlayer, reinterpret_cast<void *>(winId));
#elif defined(Q_OS_MAC)
    libvlc_media_player_set_nsobject(m_mediaPlayer, reinterpret_cast<void *>(winId));
#else
    libvlc_media_player_set_xwindow(m_mediaPlayer, static_cast<uint32_t>(winId));
#endif
}

bool VlcPlayer::openMedia(const QString &filePath, bool hwAccelEnabled, const QString &hwBackend)
{
    if (!isValid() || filePath.isEmpty()) {
        return false;
    }

    const QString nativePath = QDir::toNativeSeparators(QFileInfo(filePath).absoluteFilePath());
    libvlc_media_t *media = libvlc_media_new_path(m_vlcInstance, nativePath.toUtf8().constData());
    if (!media) {
        return false;
    }

    // 配置 GPU 硬件加速选项
    if (hwAccelEnabled && hwBackend != QStringLiteral("none")) {
        const QString opt = QStringLiteral(":avcodec-hw=%1").arg(hwBackend.isEmpty() ? QStringLiteral("any") : hwBackend);
        libvlc_media_add_option(media, opt.toUtf8().constData());
    } else {
        libvlc_media_add_option(media, ":avcodec-hw=none");
    }

    m_currentFilePath = QFileInfo(filePath).absoluteFilePath();
    m_currentUrl.clear();
    m_isStreaming = false;
    m_currentTimeMs = 0;
    m_durationMs = 0;
    m_subtitleDelayMs = 0;

    libvlc_media_player_set_media(m_mediaPlayer, media);
    libvlc_media_release(media);

    if (m_videoWinId != 0) {
        setVideoWindowId(m_videoWinId);
    }

    // 再次确保鼠标和键盘交由 Qt 处理
    libvlc_video_set_mouse_input(m_mediaPlayer, 0);
    libvlc_video_set_key_input(m_mediaPlayer, 0);

    // 启动播放
    int ret = libvlc_media_player_play(m_mediaPlayer);
    if (ret != 0) {
        return false;
    }

    // 保持当前设定的倍速与宽高比
    if (qAbs(m_rate - 1.0f) > 0.01f) {
        libvlc_media_player_set_rate(m_mediaPlayer, m_rate);
    }
    setAspectRatio(m_aspectRatio);

    // 自动扫描并挂载同目录下的同名外挂字幕 (.srt, .ass, .ssa, .vtt)
    autoLoadExternalSubtitles(m_currentFilePath);

    return true;
}

bool VlcPlayer::openUrl(const QString &url, const QString &userAgent, const QString &referer,
                        bool hwAccelEnabled, const QString &hwBackend)
{
    const QString cleanUrl = sanitizeMediaUrl(url);
    if (!isValid() || cleanUrl.isEmpty()) {
        return false;
    }

    m_recentErrors.clear();

    libvlc_media_t *media = libvlc_media_new_location(m_vlcInstance, cleanUrl.toUtf8().constData());
    if (!media) {
        return false;
    }

    // 硬件加速选项与本地播放保持一致
    if (hwAccelEnabled && hwBackend != QStringLiteral("none")) {
        const QString opt = QStringLiteral(":avcodec-hw=%1").arg(hwBackend.isEmpty() ? QStringLiteral("any") : hwBackend);
        libvlc_media_add_option(media, opt.toUtf8().constData());
    } else {
        libvlc_media_add_option(media, ":avcodec-hw=none");
    }

    // 网络缓冲：远端 m3u8 抖动比本地文件大得多，1~2 秒缓冲能显著减少卡顿
    libvlc_media_add_option(media, ":network-caching=2000");

    // 关键：不少源站用 UA + Referer 做防盗链，缺一个就 403
    if (!userAgent.isEmpty()) {
        const QString opt = QStringLiteral(":http-user-agent=%1").arg(userAgent);
        libvlc_media_add_option(media, opt.toUtf8().constData());
    }
    if (!referer.isEmpty()) {
        const QString opt = QStringLiteral(":http-referrer=%1").arg(referer);
        libvlc_media_add_option(media, opt.toUtf8().constData());
    }

    m_currentUrl = cleanUrl;
    // 让 UI 侧「是否已加载媒体」的判断继续成立（进度记忆另有 isStreaming() 兜住）
    m_currentFilePath = cleanUrl;
    m_isStreaming = true;
    m_currentTimeMs = 0;
    m_durationMs = 0;
    m_subtitleDelayMs = 0;

    libvlc_media_player_set_media(m_mediaPlayer, media);
    libvlc_media_release(media);

    if (m_videoWinId != 0) {
        setVideoWindowId(m_videoWinId);
    }

    libvlc_video_set_mouse_input(m_mediaPlayer, 0);
    libvlc_video_set_key_input(m_mediaPlayer, 0);

    if (libvlc_media_player_play(m_mediaPlayer) != 0) {
        return false;
    }

    if (qAbs(m_rate - 1.0f) > 0.01f) {
        libvlc_media_player_set_rate(m_mediaPlayer, m_rate);
    }
    setAspectRatio(m_aspectRatio);

    return true;
}

void VlcPlayer::autoLoadExternalSubtitles(const QString &videoFilePath)
{
    QFileInfo videoInfo(videoFilePath);
    if (!videoInfo.exists()) {
        return;
    }

    const QDir dir = videoInfo.dir();
    const QString baseName = videoInfo.completeBaseName();
    const QStringList subExts = {
        QStringLiteral("srt"),
        QStringLiteral("ass"),
        QStringLiteral("ssa"),
        QStringLiteral("vtt"),
        QStringLiteral("sub")
    };

    const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::Readable);
    for (const QFileInfo &fi : files) {
        const QString ext = fi.suffix().toLower();
        if (!subExts.contains(ext)) {
            continue;
        }
        // 匹配完全同名 (movie.srt) 或带语言后缀 (movie.zh.srt / movie.chs.ass)
        const QString fileBase = fi.completeBaseName();
        if (fileBase.compare(baseName, Qt::CaseInsensitive) == 0 ||
            fileBase.startsWith(baseName + QLatin1Char('.'), Qt::CaseInsensitive)) {
            addSubtitleFile(fi.absoluteFilePath());
        }
    }
}

bool VlcPlayer::addSubtitleFile(const QString &subFilePath)
{
    if (!m_mediaPlayer || !QFileInfo::exists(subFilePath)) {
        return false;
    }
    const QByteArray uri = QUrl::fromLocalFile(QFileInfo(subFilePath).absoluteFilePath()).toEncoded();
    int res = libvlc_media_player_add_slave(
        m_mediaPlayer,
        libvlc_media_slave_type_subtitle,
        uri.constData(),
        true // 挂载后自动选中该字幕流
    );
    if (res == 0) {
        emit osdMessageRequested(QStringLiteral("💬 已加载外挂字幕: %1").arg(QFileInfo(subFilePath).fileName()));
        emit tracksUpdated();
        return true;
    }
    return false;
}

void VlcPlayer::play()
{
    if (m_mediaPlayer) {
        libvlc_media_player_play(m_mediaPlayer);
    }
}

void VlcPlayer::pause()
{
    if (m_mediaPlayer) {
        libvlc_media_player_set_pause(m_mediaPlayer, 1);
    }
}

void VlcPlayer::togglePlayPause()
{
    if (!m_mediaPlayer) {
        return;
    }
    if (isPlaying()) {
        pause();
        emit osdMessageRequested(QStringLiteral("⏸ 暂停"));
    } else {
        play();
        emit osdMessageRequested(QStringLiteral("▶ 播放"));
    }
}

void VlcPlayer::stop()
{
    if (m_mediaPlayer) {
        libvlc_media_player_stop(m_mediaPlayer);
        m_currentTimeMs = 0;
        emit timeChanged(0);
    }
}

bool VlcPlayer::isPlaying() const
{
    return m_mediaPlayer && (libvlc_media_player_is_playing(m_mediaPlayer) != 0);
}

void VlcPlayer::seekTime(qint64 timeMs)
{
    if (!m_mediaPlayer || m_durationMs <= 0) {
        return;
    }
    const qint64 clamped = qBound<qint64>(0, timeMs, m_durationMs);
    libvlc_media_player_set_time(m_mediaPlayer, clamped);
    m_currentTimeMs = clamped;
    emit timeChanged(m_currentTimeMs);
}

void VlcPlayer::seekRelative(qint64 deltaMs)
{
    if (!m_mediaPlayer || m_durationMs <= 0) {
        return;
    }
    const qint64 target = qBound<qint64>(0, m_currentTimeMs + deltaMs, m_durationMs);
    seekTime(target);

    const qint64 sec = target / 1000;
    const qint64 totalSec = m_durationMs / 1000;
    const QString timeStr = QStringLiteral("%1:%2 / %3:%4")
        .arg(sec / 60, 2, 10, QLatin1Char('0'))
        .arg(sec % 60, 2, 10, QLatin1Char('0'))
        .arg(totalSec / 60, 2, 10, QLatin1Char('0'))
        .arg(totalSec % 60, 2, 10, QLatin1Char('0'));

    if (deltaMs >= 0) {
        emit osdMessageRequested(QStringLiteral("⏩ 快进 +%1s  (%2)").arg(deltaMs / 1000).arg(timeStr));
    } else {
        emit osdMessageRequested(QStringLiteral("⏪ 快退 %1s  (%2)").arg(deltaMs / 1000).arg(timeStr));
    }
}

int VlcPlayer::volume() const
{
    if (!m_mediaPlayer) {
        return 80;
    }
    int vol = libvlc_audio_get_volume(m_mediaPlayer);
    return vol < 0 ? 80 : vol;
}

void VlcPlayer::setVolume(int vol)
{
    if (!m_mediaPlayer) {
        return;
    }
    const int clamped = qBound(0, vol, 100);
    libvlc_audio_set_volume(m_mediaPlayer, clamped);
}

void VlcPlayer::adjustVolume(int delta)
{
    const int newVol = qBound(0, volume() + delta, 100);
    setVolume(newVol);
    if (isMuted() && delta > 0) {
        setMute(false);
    }
    emit osdMessageRequested(QStringLiteral("🔊 音量: %1%").arg(newVol));
}

bool VlcPlayer::isMuted() const
{
    return m_mediaPlayer && (libvlc_audio_get_mute(m_mediaPlayer) > 0);
}

void VlcPlayer::setMute(bool mute)
{
    if (m_mediaPlayer) {
        libvlc_audio_set_mute(m_mediaPlayer, mute ? 1 : 0);
    }
}

void VlcPlayer::toggleMute()
{
    const bool nextMute = !isMuted();
    setMute(nextMute);
    emit osdMessageRequested(nextMute ? QStringLiteral("🔇 已静音")
                                      : QStringLiteral("🔊 取消静音 (%1%)").arg(volume()));
}

QList<TrackInfo> VlcPlayer::audioTracks() const
{
    QList<TrackInfo> result;
    if (!m_mediaPlayer) {
        return result;
    }
    libvlc_track_description_t *head = libvlc_audio_get_track_description(m_mediaPlayer);
    for (libvlc_track_description_t *curr = head; curr != nullptr; curr = curr->p_next) {
        TrackInfo info;
        info.id = curr->i_id;
        info.name = curr->psz_name ? QString::fromUtf8(curr->psz_name) : QStringLiteral("Track %1").arg(curr->i_id);
        result.append(info);
    }
    if (head) {
        libvlc_track_description_list_release(head);
    }
    return result;
}

int VlcPlayer::currentAudioTrack() const
{
    return m_mediaPlayer ? libvlc_audio_get_track(m_mediaPlayer) : -1;
}

void VlcPlayer::setAudioTrack(int trackId)
{
    if (m_mediaPlayer) {
        libvlc_audio_set_track(m_mediaPlayer, trackId);
        emit osdMessageRequested(QStringLiteral("🎵 已切换音轨 (ID: %1)").arg(trackId));
    }
}

void VlcPlayer::setAspectRatio(const QString &ratio)
{
    m_aspectRatio = ratio;
    if (!m_mediaPlayer) {
        return;
    }
    if (ratio.isEmpty()) {
        // nullptr 表示保持视频原始宽高比并自动加黑边，防止画面拉伸变形
        libvlc_video_set_aspect_ratio(m_mediaPlayer, nullptr);
    } else {
        libvlc_video_set_aspect_ratio(m_mediaPlayer, ratio.toUtf8().constData());
    }
}

QList<TrackInfo> VlcPlayer::subtitleTracks() const
{
    QList<TrackInfo> result;
    if (!m_mediaPlayer) {
        return result;
    }
    libvlc_track_description_t *head = libvlc_video_get_spu_description(m_mediaPlayer);
    for (libvlc_track_description_t *curr = head; curr != nullptr; curr = curr->p_next) {
        TrackInfo info;
        info.id = curr->i_id;
        info.name = curr->psz_name ? QString::fromUtf8(curr->psz_name) : QStringLiteral("Subtitle %1").arg(curr->i_id);
        result.append(info);
    }
    if (head) {
        libvlc_track_description_list_release(head);
    }
    return result;
}

int VlcPlayer::currentSubtitleTrack() const
{
    return m_mediaPlayer ? libvlc_video_get_spu(m_mediaPlayer) : -1;
}

void VlcPlayer::setSubtitleTrack(int trackId)
{
    if (m_mediaPlayer) {
        libvlc_video_set_spu(m_mediaPlayer, trackId);
        emit osdMessageRequested(trackId < 0 ? QStringLiteral("💬 已关闭字幕")
                                             : QStringLiteral("💬 已切换字幕流 (ID: %1)").arg(trackId));
    }
}

qint64 VlcPlayer::subtitleDelayMs() const
{
    if (!m_mediaPlayer) {
        return 0;
    }
    // LibVLC 字幕延迟单位为微秒 (us)
    return libvlc_video_get_spu_delay(m_mediaPlayer) / 1000;
}

void VlcPlayer::setSubtitleDelayMs(qint64 delayMs)
{
    m_subtitleDelayMs = delayMs;
    if (m_mediaPlayer) {
        libvlc_video_set_spu_delay(m_mediaPlayer, delayMs * 1000);
        const double sec = delayMs / 1000.0;
        emit osdMessageRequested(QStringLiteral("💬 字幕时间轴延迟: %1%2 s")
            .arg(sec >= 0 ? QStringLiteral("+") : QStringLiteral(""))
            .arg(sec, 0, 'f', 1));
    }
}

void VlcPlayer::adjustSubtitleDelayMs(qint64 deltaMs)
{
    setSubtitleDelayMs(subtitleDelayMs() + deltaMs);
}

float VlcPlayer::rate() const
{
    return m_mediaPlayer ? libvlc_media_player_get_rate(m_mediaPlayer) : m_rate;
}

void VlcPlayer::setRate(float playbackRate)
{
    m_rate = qBound(0.5f, playbackRate, 3.0f);
    if (m_mediaPlayer) {
        libvlc_media_player_set_rate(m_mediaPlayer, m_rate);
    }
    emit osdMessageRequested(QStringLiteral("⚡ 播放倍速: %1x").arg(m_rate, 0, 'f', 2));
}

void VlcPlayer::attachEvents()
{
    if (!m_mediaPlayer) {
        return;
    }
    libvlc_event_manager_t *em = libvlc_media_player_event_manager(m_mediaPlayer);
    if (!em) {
        return;
    }
    libvlc_event_attach(em, libvlc_MediaPlayerPlaying,       &VlcPlayer::handleVlcEvent, this);
    libvlc_event_attach(em, libvlc_MediaPlayerPaused,        &VlcPlayer::handleVlcEvent, this);
    libvlc_event_attach(em, libvlc_MediaPlayerStopped,       &VlcPlayer::handleVlcEvent, this);
    libvlc_event_attach(em, libvlc_MediaPlayerEndReached,    &VlcPlayer::handleVlcEvent, this);
    libvlc_event_attach(em, libvlc_MediaPlayerTimeChanged,   &VlcPlayer::handleVlcEvent, this);
    libvlc_event_attach(em, libvlc_MediaPlayerLengthChanged, &VlcPlayer::handleVlcEvent, this);
    libvlc_event_attach(em, libvlc_MediaPlayerESAdded,       &VlcPlayer::handleVlcEvent, this);
    libvlc_event_attach(em, libvlc_MediaPlayerEncounteredError, &VlcPlayer::handleVlcEvent, this);
}

void VlcPlayer::vlcLogCallback(void *data, int level, const struct vlc_log_t *ctx,
                               const char *fmt, va_list args)
{
    Q_UNUSED(ctx);
    auto *self = static_cast<VlcPlayer *>(data);
    if (!self || !fmt || level < LIBVLC_WARNING) {
        return;
    }

    char buffer[1024] = {0};
    std::vsnprintf(buffer, sizeof(buffer) - 1, fmt, args);
    const QString line = QString::fromUtf8(buffer).trimmed();
    if (line.isEmpty()) {
        return;
    }

    // 回调来自 LibVLC 自己的线程，必须切回对象线程再改成员
    QMetaObject::invokeMethod(self, [self, line]() { self->appendVlcLog(line); }, Qt::QueuedConnection);
}

void VlcPlayer::appendVlcLog(const QString &line)
{
    m_recentErrors.append(line);
    while (m_recentErrors.size() > 12) {
        m_recentErrors.removeFirst();
    }
}

QString VlcPlayer::friendlyError(const QString &raw) const
{
    const QString text = raw.trimmed();
    const QString lower = text.toLower();

    if (text.contains(QStringLiteral("403")) || lower.contains(QStringLiteral("forbidden"))) {
        return QStringLiteral("源站拒绝访问（403）：防盗链校验没通过，换条线路或改源站 Referer");
    }
    if (text.contains(QStringLiteral("404")) || lower.contains(QStringLiteral("not found"))) {
        return QStringLiteral("播放地址不存在（404），该剧集链接已失效");
    }
    if (lower.contains(QStringLiteral("timed out")) || lower.contains(QStringLiteral("timeout"))) {
        return QStringLiteral("连接源站超时");
    }
    if (lower.contains(QStringLiteral("cannot resolve")) || lower.contains(QStringLiteral("name resolution"))) {
        return QStringLiteral("域名解析失败，源站可能已停用");
    }
    if (lower.contains(QStringLiteral("no suitable demux")) || lower.contains(QStringLiteral("unrecognized"))) {
        return QStringLiteral("无法识别的内容格式（可能是网页地址，不是视频直链）");
    }
    if (lower.contains(QStringLiteral("connection refused"))) {
        return QStringLiteral("源站拒绝连接");
    }
    return text.isEmpty() ? QStringLiteral("播放内核报错（无详细信息）") : text;
}

void VlcPlayer::handleVlcEvent(const struct libvlc_event_t *event, void *userData)
{
    auto *self = static_cast<VlcPlayer *>(userData);
    if (!self || !event) {
        return;
    }

    switch (event->type) {
    case libvlc_MediaPlayerPlaying:
        QMetaObject::invokeMethod(self, [self]() {
            if (self->m_mediaPlayer) {
                libvlc_video_set_mouse_input(self->m_mediaPlayer, 0);
                libvlc_video_set_key_input(self->m_mediaPlayer, 0);
            }
            self->m_recentErrors.clear();   // 成功起播后清掉之前探测阶段的噪声
            emit self->stateChanged(true);
            emit self->tracksUpdated();
        }, Qt::QueuedConnection);
        break;

    case libvlc_MediaPlayerEncounteredError:
        QMetaObject::invokeMethod(self, [self]() {
            const QString detail = self->m_recentErrors.isEmpty()
                                       ? QString()
                                       : self->m_recentErrors.last();
            const QString message = self->friendlyError(detail);
            self->m_recentErrors.clear();
            emit self->stateChanged(false);
            emit self->errorOccurred(message);
        }, Qt::QueuedConnection);
        break;

    case libvlc_MediaPlayerPaused:
    case libvlc_MediaPlayerStopped:
        QMetaObject::invokeMethod(self, [self]() {
            emit self->stateChanged(false);
        }, Qt::QueuedConnection);
        break;

    case libvlc_MediaPlayerTimeChanged: {
        const qint64 newTime = event->u.media_player_time_changed.new_time;
        QMetaObject::invokeMethod(self, [self, newTime]() {
            self->m_currentTimeMs = newTime;
            emit self->timeChanged(newTime);
        }, Qt::QueuedConnection);
        break;
    }

    case libvlc_MediaPlayerLengthChanged: {
        const qint64 newLen = event->u.media_player_length_changed.new_length;
        QMetaObject::invokeMethod(self, [self, newLen]() {
            self->m_durationMs = newLen;
            emit self->durationChanged(newLen);
            emit self->tracksUpdated();
        }, Qt::QueuedConnection);
        break;
    }

    case libvlc_MediaPlayerESAdded:
        QMetaObject::invokeMethod(self, [self]() {
            emit self->tracksUpdated();
        }, Qt::QueuedConnection);
        break;

    case libvlc_MediaPlayerEndReached:
        QMetaObject::invokeMethod(self, [self]() {
            emit self->stateChanged(false);
            emit self->endReached();
        }, Qt::QueuedConnection);
        break;

    default:
        break;
    }
}
