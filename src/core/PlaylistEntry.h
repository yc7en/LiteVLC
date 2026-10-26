#pragma once

#include <QMetaType>
#include <QString>

/**
 * @brief 播放列表里的一个条目
 *
 * 本地文件与在线剧集共用一套结构，播放列表才能把「搜索到的剧集」和
 * 「本地文件」排在同一张队列里，由播放模式统一调度连播。
 *
 * - Local  ：location 是本地绝对路径，播放走 VlcPlayer::openMedia
 * - Online ：location 是接口返回的播放地址（可能是 m3u8，也可能是播放页），
 *            播放前要过 StreamResolver 解析链，并带上 UA / Referer
 */
struct PlaylistEntry
{
    enum Kind {
        Local = 0,
        Online = 1
    };

    Kind kind = Local;
    QString location;    // 本地绝对路径 或 在线播放地址
    QString title;       // 列表显示名
    QString subtitle;    // 辅助信息（在线为来源站名，本地为空）
    QString userAgent;   // 仅 Online 有效
    QString referer;     // 仅 Online 有效
    /** 直播条目：清单是滑动窗口，不能走 HLS 本地中继（中继会缓存清单，直播会卡死） */
    bool live = false;

    bool isValid() const { return !location.isEmpty(); }
    bool isOnline() const { return kind == Online; }

    static PlaylistEntry makeLocal(const QString &path, const QString &displayName = QString())
    {
        PlaylistEntry entry;
        entry.kind = Local;
        entry.location = path;
        entry.title = displayName;
        return entry;
    }

    static PlaylistEntry makeOnline(const QString &url,
                                    const QString &displayName,
                                    const QString &sourceName,
                                    const QString &ua,
                                    const QString &ref,
                                    bool isLive = false)
    {
        PlaylistEntry entry;
        entry.kind = Online;
        entry.location = url;
        entry.title = displayName;
        entry.subtitle = sourceName;
        entry.userAgent = ua;
        entry.referer = ref;
        entry.live = isLive;
        return entry;
    }
};

Q_DECLARE_METATYPE(PlaylistEntry)
Q_DECLARE_METATYPE(QList<PlaylistEntry>)
