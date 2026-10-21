#pragma once

#include <QMainWindow>
#include <QMenu>
#include "core/VlcPlayer.h"
#include "core/CmsClient.h"
#include "core/StreamProbe.h"
#include "core/PlaylistManager.h"
#include "ui/VideoWidget.h"
#include "ui/ControlBar.h"
#include "ui/PlaylistWidget.h"

class SourceDialog;

/**
 * @brief 极简播放器主窗口
 * 负责协调：
 * - 原生菜单栏、视频画布 (VideoWidget)、右侧播放列表 (PlaylistWidget)、底部控制栏 (ControlBar)
 * - 文件拖拽加载（视频自动加入列表并播放，字幕文件直接挂载到当前视频）
 * - 历史播放位置自动记忆与恢复、片头片尾自动跳过
 * - 全局快捷键（Space 暂停/播放、Enter/F 全屏、Left/Right 快进快退 5s、Up/Down 音量、[/] 字幕微调、C/V 倍速）
 * - Windows 丝滑无闪烁窗口置顶 (SetWindowPos)
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void loadAndPlayFile(const QString &filePath);
    void addAndPlayFiles(const QStringList &filePaths);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onOpenFiles();
    void onOpenSubtitleFile();
    void onToggleFullScreen();
    void onSetStayOnTop(bool onTop);
    void onOpenSettings();
    void onOpenOnlineSearch();
    void onOnlinePlayRequested(const OnlinePlayRequest &request);
    void onStreamProbeFinished(const StreamProbeResult &result);
    void onPlayerError(const QString &message);

    void onDurationChanged(qint64 totalMs);
    void onTimeChanged(qint64 currentMs);
    void onMediaEndReached();
    void rebuildTrackMenus();

private:
    void setupUi();
    void setupMenuBar();
    void setupConnections();
    void saveCurrentProgress();

    // --- 在线播放队列（来自在线源搜索的整条线路） ---
    void playOnlineEpisode(int index);
    void clearOnlineQueue();
    bool hasOnlineQueue() const { return m_onlineRequest.isValid(); }
    void playNextOnlineEpisode();
    void playPrevOnlineEpisode();

    static bool isVideoFile(const QString &filePath);
    static bool isSubtitleFile(const QString &filePath);

    VlcPlayer *m_player = nullptr;
    PlaylistManager *m_playlist = nullptr;

    VideoWidget *m_videoWidget = nullptr;
    PlaylistWidget *m_playlistWidget = nullptr;
    ControlBar *m_controlBar = nullptr;

    QMenu *m_audioTrackMenu = nullptr;
    QMenu *m_subtitleMenu = nullptr;
    QMenu *m_aspectRatioMenu = nullptr;

    QAction *m_actStayOnTop = nullptr;

    SourceDialog *m_sourceDialog = nullptr;
    OnlinePlayRequest m_onlineRequest;

    // 播放前预检：找出真正能让源站返回视频的那组 UA / Referer
    StreamProbe *m_probe = nullptr;
    StreamProbeResult m_lastProbe;
    bool m_lastProbeHasHeaders = false;   // m_lastProbe 里的 UA/Referer 是不是预检实测出来的
    int m_playRetryCount = 0;   // 经 probe 通过但内核仍失败时的重试次数
    bool m_manualPlayAttempt = false;   // 本次播放是用户手点的（失败要弹窗，连播失败只在 OSD 提示）

    // 标记当前打开的视频是否已执行过“记忆续播 / 跳过片头”初始化跳转
    bool m_initialSeekApplied = false;
    // 防止片尾跳过在同一视频末尾重复触发
    bool m_outroSkipTriggered = false;
    // 全屏前播放列表是否可见
    bool m_playlistWasVisibleBeforeFs = true;
};
