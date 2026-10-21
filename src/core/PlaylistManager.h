#pragma once

#include <QObject>
#include <QStringList>
#include "ConfigManager.h"

/**
 * @brief 轻量级播放列表管理器
 * 负责维护播放队列、当前索引以及播放完成后的连播/循环调度策略。
 */
class PlaylistManager : public QObject
{
    Q_OBJECT
public:
    explicit PlaylistManager(QObject *parent = nullptr);

    const QStringList& files() const { return m_files; }
    int currentIndex() const { return m_currentIndex; }
    QString currentFile() const;

    // 添加文件（自动去重），返回首个新增文件的索引
    int addFiles(const QStringList &filePaths);
    void removeAt(int index);
    void clear();

    void setCurrentIndex(int index);

    // 供手动点击“上一集 / 下一集”调用
    QString nextFileManual();
    QString previousFileManual();

    // 供视频自然播放结束或跳过片尾时调用（遵循 顺序/列表循环/单曲循环）
    QString nextFileOnEnd(ConfigManager::PlayMode mode);

signals:
    void playlistChanged();
    void currentIndexChanged(int index);

private:
    QStringList m_files;
    int m_currentIndex = -1;
};
