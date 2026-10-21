#pragma once

#include <QElapsedTimer>
#include <QMainWindow>
#include <QMenu>
#include "core/VlcPlayer.h"
#include "core/CmsClient.h"
#include "core/StreamResolver.h"
#include "core/PlaylistManager.h"
#include "core/PlaylistEntry.h"
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
 *
 * 播放列表是唯一的播放队列：本地文件与在线剧集（来自 SourceDialog）混排在
 * 同一张列表里，上一集/下一集/播完续播全部由 PlaylistManager 的索引驱动。
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
    void onCheckUpdate();
    void onAbout();

    /** 在线片源窗口：加入列表并立即播放某一集 */
    void onPlayEntriesRequested(const QList<PlaylistEntry> &entries, int startIndex);
    /** 在线片源窗口：只加入播放列表 */
    void onAddEntriesRequested(const QList<PlaylistEntry> &entries);

    void onStreamResolved(const ResolvedStream &stream);
    void onStreamResolveFailed(const ResolveFailure &failure);
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

    /** 播放列表第 index 项（本地直开 / 在线走解析链） */
    void playPlaylistIndex(int index, bool autoAdvance = false);
    void playNextPlaylistEntry();
    void playPrevPlaylistEntry();
    void playOnlineEntry(int index);
    void resetAutoAdvanceGuard();

    void ensureResolver();
    /** 中断在途解析并清掉「当前在线条目」标记 */
    void stopOnlineResolve();

    /** 播放失败的详细提示：分类 + 诊断字段 + 另存响应内容 */
    void showPlaybackFailure(const ResolveFailure &failure);
    void showResolveFailureDialog(const ResolveFailure &failure, const PlaylistEntry &entry);

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

    // 播放地址解析链：直连 → 页面嗅探 → 候选复检（见 StreamResolver）
    StreamResolver *m_resolver = nullptr;
    ResolvedStream m_lastStream;
    bool m_lastStreamHasHeaders = false;   // m_lastStream 里的 UA/Referer 是不是实测出来的
    int m_resolvingIndex = -1;             // 正在解析/正在播放的在线条目下标，-1 表示当前不是在线条目
    int m_playRetryCount = 0;              // 经 probe 通过但内核仍失败时的重试次数
    bool m_manualPlayAttempt = false;      // 本次播放是用户手点的（失败要弹窗，连播失败只在 OSD 提示）

    // 标记当前打开的视频是否已执行过“记忆续播 / 跳过片头”初始化跳转
    bool m_initialSeekApplied = false;
    // 防止片尾跳过在同一视频末尾重复触发
    bool m_outroSkipTriggered = false;
    // 全屏前播放列表是否可见
    bool m_playlistWasVisibleBeforeFs = true;

    // 防止「坏流」把整季瞬间跳完：短时间内连续自动连播超过阈值就停下来
    QElapsedTimer m_autoAdvanceWindow;
    int m_autoAdvanceCount = 0;
};
