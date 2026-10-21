#pragma once

#include <QWidget>
#include <QListWidget>
#include <QComboBox>
#include <QPushButton>
#include "core/ConfigManager.h"
#include "core/PlaylistEntry.h"

/**
 * @brief 右侧轻量级播放列表侧边栏 (基于 QListWidget)
 * 支持：
 * - 显示播放队列（本地文件 + 在线剧集混排）、双击切集、高亮当前正在播放的视频
 * - 切换播放模式 (顺序播放 / 列表循环 / 单曲循环)
 * - 添加文件 / 删除选中项 / 清空列表
 */
class PlaylistWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PlaylistWidget(QWidget *parent = nullptr);

    void updateList(const QList<PlaylistEntry> &entries, int currentIndex);
    void setCurrentIndex(int currentIndex);
    void setPlayMode(ConfigManager::PlayMode mode);

signals:
    void itemDoubleClicked(int index);
    void addFilesRequested();
    void removeIndexRequested(int index);
    void clearRequested();
    void playModeChanged(ConfigManager::PlayMode mode);

private:
    QListWidget *m_listWidget = nullptr;
    QComboBox *m_modeCombo = nullptr;
    QPushButton *m_btnAdd = nullptr;
    QPushButton *m_btnRemove = nullptr;
    QPushButton *m_btnClear = nullptr;
};
