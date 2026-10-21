#pragma once

#include <QObject>
#include <QList>
#include <QStringList>

#include "ConfigManager.h"
#include "PlaylistEntry.h"

/**
 * @brief 轻量级播放列表管理器
 * 负责维护播放队列、当前索引以及播放完成后的连播/循环调度策略。
 *
 * 队列里同时可以放本地文件和在线剧集（见 PlaylistEntry），两者的连播、
 * 上一集/下一集、播放模式完全走同一套索引逻辑。
 */
class PlaylistManager : public QObject
{
    Q_OBJECT
public:
    explicit PlaylistManager(QObject *parent = nullptr);

    const QList<PlaylistEntry>& entries() const { return m_entries; }
    /** 兼容旧调用点：只取 location 列表 */
    QStringList files() const;

    int currentIndex() const { return m_currentIndex; }
    PlaylistEntry currentEntry() const;
    PlaylistEntry entryAt(int index) const;
    QString currentLocation() const { return currentEntry().location; }

    // 添加条目（按 location 去重），返回首个新增（或已存在）条目的索引，-1 表示都没加进去
    int addEntries(const QList<PlaylistEntry> &entries);
    // 添加本地文件（自动转绝对路径、校验存在性）
    int addFiles(const QStringList &filePaths);

    void removeAt(int index);
    void clear();

    void setCurrentIndex(int index);

    /** 按 location 找索引，找不到返回 -1 */
    int indexOf(const QString &location) const;

    // 供手动点击“上一集 / 下一集”调用
    PlaylistEntry nextEntryManual();
    PlaylistEntry previousEntryManual();

    // 供视频自然播放结束或跳过片尾时调用（遵循 顺序/列表循环/单曲循环）
    PlaylistEntry nextEntryOnEnd(ConfigManager::PlayMode mode);

signals:
    void playlistChanged();
    void currentIndexChanged(int index);

private:
    QList<PlaylistEntry> m_entries;
    int m_currentIndex = -1;
};
