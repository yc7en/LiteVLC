#include "MainWindow.h"
#include "SettingsDialog.h"
#include "SourceDialog.h"
#include "core/ConfigManager.h"
#include "core/SourceManager.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QFileDialog>
#include <QFileInfo>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QActionGroup>
#include <QMessageBox>
#include <QUrl>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("LiteVLC Player"));
    resize(1060, 640);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);

    m_player   = new VlcPlayer(this);
    m_playlist = new PlaylistManager(this);

    setupUi();
    setupMenuBar();
    setupConnections();

    // 绑定视频渲染窗口句柄
    m_player->setVideoWindowId(m_videoWidget->winId());

    // 恢复上次保存的音量、播放模式与置顶状态
    auto &cfg = ConfigManager::instance();

    // 触发一次在线源配置加载：首次运行会在程序目录生成 sources.json 模板，方便用户手改
    SourceManager::instance();
    m_player->setVolume(cfg.volume());
    m_controlBar->setVolumeValue(cfg.volume(), false);
    m_playlistWidget->setPlayMode(cfg.playMode());
    if (cfg.stayOnTop()) {
        onSetStayOnTop(true);
    }
}

MainWindow::~MainWindow()
{
    saveCurrentProgress();
}

void MainWindow::setupUi()
{
    auto *central = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // 上半部分：左侧视频渲染画布 + 右侧可折叠轻量播放列表
    auto *upperLayout = new QHBoxLayout();
    upperLayout->setContentsMargins(0, 0, 0, 0);
    upperLayout->setSpacing(0);

    m_videoWidget = new VideoWidget(central);
    m_playlistWidget = new PlaylistWidget(central);

    upperLayout->addWidget(m_videoWidget, 1);
    upperLayout->addWidget(m_playlistWidget, 0);

    // 下半部分：底部控制栏
    m_controlBar = new ControlBar(central);

    rootLayout->addLayout(upperLayout, 1);
    rootLayout->addWidget(m_controlBar, 0);

    setCentralWidget(central);
}

void MainWindow::setupMenuBar()
{
    // 1. 媒体菜单
    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("媒体(&F)"));
    fileMenu->addAction(QStringLiteral("在线片源搜索(&S)..."), QKeySequence(QStringLiteral("Ctrl+F")),
                        this, &MainWindow::onOpenOnlineSearch);
    fileMenu->addSeparator();
    QAction *actOpen = fileMenu->addAction(QStringLiteral("打开视频文件(&O)..."), QKeySequence::Open, this, &MainWindow::onOpenFiles);
    Q_UNUSED(actOpen);
    fileMenu->addAction(QStringLiteral("加载外挂字幕(&S)..."), this, &MainWindow::onOpenSubtitleFile);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("设置 (GPU硬解 / 片头片尾 / 记忆续播)(&P)..."), this, &MainWindow::onOpenSettings);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("退出(&X)"), QKeySequence::Quit, this, &QWidget::close);

    // 2. 播放控制菜单
    QMenu *playMenu = menuBar()->addMenu(QStringLiteral("播放(&P)"));
    playMenu->addAction(QStringLiteral("播放 / 暂停 (Space)"), this, [this]() { m_player->togglePlayPause(); });
    playMenu->addAction(QStringLiteral("停止"), this, [this]() {
        saveCurrentProgress();
        m_player->stop();
    });
    playMenu->addSeparator();
    playMenu->addAction(QStringLiteral("快退 5 秒 (Left)"), this, [this]() { m_player->seekRelative(-5000); });
    playMenu->addAction(QStringLiteral("快进 5 秒 (Right)"), this, [this]() { m_player->seekRelative(5000); });
    playMenu->addSeparator();
    playMenu->addAction(QStringLiteral("上一集 (PgUp)"), this, [this]() {
        if (hasOnlineQueue()) {
            playPrevOnlineEpisode();
            return;
        }
        const QString prev = m_playlist->previousFileManual();
        if (!prev.isEmpty()) loadAndPlayFile(prev);
    });
    playMenu->addAction(QStringLiteral("下一集 (PgDn)"), this, [this]() {
        if (hasOnlineQueue()) {
            playNextOnlineEpisode();
            return;
        }
        const QString next = m_playlist->nextFileManual();
        if (!next.isEmpty()) loadAndPlayFile(next);
    });

    // 3. 音轨菜单 (绑定到底部按钮与顶部菜单)
    m_audioTrackMenu = menuBar()->addMenu(QStringLiteral("音轨(&A)"));
    m_controlBar->audioTrackButton()->setMenu(m_audioTrackMenu);

    // 4. 字幕菜单 (绑定到底部按钮与顶部菜单)
    m_subtitleMenu = menuBar()->addMenu(QStringLiteral("字幕(&S)"));
    m_controlBar->subtitleButton()->setMenu(m_subtitleMenu);

    // 5. 画面与显示菜单
    QMenu *viewMenu = menuBar()->addMenu(QStringLiteral("画面(&V)"));
    viewMenu->addAction(QStringLiteral("全屏 / 退出全屏 (Enter / 双击)"), this, &MainWindow::onToggleFullScreen);

    m_actStayOnTop = viewMenu->addAction(QStringLiteral("窗口始终置顶 (T)"));
    m_actStayOnTop->setCheckable(true);
    m_actStayOnTop->setChecked(ConfigManager::instance().stayOnTop());
    connect(m_actStayOnTop, &QAction::toggled, this, &MainWindow::onSetStayOnTop);

    viewMenu->addAction(QStringLiteral("显示 / 隐藏播放列表 (L)"), this, [this]() {
        m_playlistWidget->setVisible(!m_playlistWidget->isVisible());
    });
    viewMenu->addSeparator();

    // 画面宽高比子菜单
    m_aspectRatioMenu = viewMenu->addMenu(QStringLiteral("宽高比 (Aspect Ratio)"));
    auto *ratioGroup = new QActionGroup(this);
    const QList<QPair<QString, QString>> ratios = {
        {QStringLiteral("默认 (保持原视频比例)"), QString()},
        {QStringLiteral("16:9"), QStringLiteral("16:9")},
        {QStringLiteral("4:3"),  QStringLiteral("4:3")},
        {QStringLiteral("21:9"), QStringLiteral("21:9")},
        {QStringLiteral("1:1"),  QStringLiteral("1:1")}
    };
    for (const auto &pair : ratios) {
        QAction *act = m_aspectRatioMenu->addAction(pair.first);
        act->setCheckable(true);
        if (pair.second.isEmpty()) {
            act->setChecked(true);
        }
        ratioGroup->addAction(act);
        const QString ratioVal = pair.second;
        connect(act, &QAction::triggered, this, [this, ratioVal]() {
            m_player->setAspectRatio(ratioVal);
            m_videoWidget->showOsdMessage(ratioVal.isEmpty()
                ? QStringLiteral("📐 宽高比: 保持原比例")
                : QStringLiteral("📐 宽高比: %1").arg(ratioVal));
        });
    }

    rebuildTrackMenus();
}

void MainWindow::setupConnections()
{
    // 播放器核心信号 -> UI 更新
    connect(m_player, &VlcPlayer::stateChanged, m_controlBar, &ControlBar::setPlayingState);
    connect(m_player, &VlcPlayer::stateChanged, this, [this](bool playing) {
        if (playing) {
            m_manualPlayAttempt = false;   // 真出画面了，后续错误不必再弹窗
        }
    });
    connect(m_player, &VlcPlayer::durationChanged, this, &MainWindow::onDurationChanged);
    connect(m_player, &VlcPlayer::timeChanged, this, &MainWindow::onTimeChanged);
    connect(m_player, &VlcPlayer::tracksUpdated, this, &MainWindow::rebuildTrackMenus);
    connect(m_player, &VlcPlayer::endReached, this, &MainWindow::onMediaEndReached);
    connect(m_player, &VlcPlayer::errorOccurred, this, &MainWindow::onPlayerError);
    connect(m_player, &VlcPlayer::osdMessageRequested, m_videoWidget, [this](const QString &msg) {
        m_videoWidget->showOsdMessage(msg);
    });

    // 视频画布交互信号 -> 全屏 / 滚轮音量 / 全屏底部唤醒
    connect(m_videoWidget, &VideoWidget::doubleClicked, this, &MainWindow::onToggleFullScreen);
    connect(m_videoWidget, &VideoWidget::wheelVolumeChanged, this, [this](int delta) {
        m_player->adjustVolume(delta);
        const int vol = m_player->volume();
        ConfigManager::instance().setVolume(vol);
        m_controlBar->setVolumeValue(vol, m_player->isMuted());
    });
    connect(m_videoWidget, &VideoWidget::mouseMovedInCanvas, this, [this](const QPoint &globalPos) {
        if (isFullScreen()) {
            const int distFromBottom = height() - mapFromGlobal(globalPos).y();
            m_controlBar->setVisible(distFromBottom <= 85);
        }
    });

    // 底部控制栏信号 -> 播放器动作
    connect(m_controlBar, &ControlBar::playPauseClicked, this, [this]() {
        if (m_player->currentFilePath().isEmpty() && !m_playlist->files().isEmpty()) {
            int idx = m_playlist->currentIndex() >= 0 ? m_playlist->currentIndex() : 0;
            m_playlist->setCurrentIndex(idx);
            loadAndPlayFile(m_playlist->currentFile());
            return;
        }
        m_player->togglePlayPause();
    });
    connect(m_controlBar, &ControlBar::stopClicked, this, [this]() {
        saveCurrentProgress();
        m_player->stop();
    });
    connect(m_controlBar, &ControlBar::prevClicked, this, [this]() {
        if (hasOnlineQueue()) {
            playPrevOnlineEpisode();
            return;
        }
        const QString prev = m_playlist->previousFileManual();
        if (!prev.isEmpty()) loadAndPlayFile(prev);
    });
    connect(m_controlBar, &ControlBar::nextClicked, this, [this]() {
        if (hasOnlineQueue()) {
            playNextOnlineEpisode();
            return;
        }
        const QString next = m_playlist->nextFileManual();
        if (!next.isEmpty()) loadAndPlayFile(next);
    });
    connect(m_controlBar, &ControlBar::seekRequested, m_player, &VlcPlayer::seekTime);
    connect(m_controlBar, &ControlBar::volumeChanged, this, [this](int vol) {
        m_player->setVolume(vol);
        if (vol > 0 && m_player->isMuted()) {
            m_player->setMute(false);
        }
        ConfigManager::instance().setVolume(vol);
        m_controlBar->setVolumeValue(vol, m_player->isMuted());
        m_videoWidget->showOsdMessage(QStringLiteral("🔊 音量: %1%").arg(vol));
    });
    connect(m_controlBar, &ControlBar::muteClicked, this, [this]() {
        m_player->toggleMute();
        m_controlBar->setVolumeValue(m_player->volume(), m_player->isMuted());
    });
    connect(m_controlBar, &ControlBar::speedChanged, m_player, &VlcPlayer::setRate);
    connect(m_controlBar, &ControlBar::stayOnTopToggled, this, &MainWindow::onSetStayOnTop);
    connect(m_controlBar, &ControlBar::settingsClicked, this, &MainWindow::onOpenSettings);
    connect(m_controlBar, &ControlBar::onlineSearchClicked, this, &MainWindow::onOpenOnlineSearch);
    connect(m_controlBar, &ControlBar::playlistToggled, this, [this]() {
        m_playlistWidget->setVisible(!m_playlistWidget->isVisible());
    });
    connect(m_controlBar, &ControlBar::fullscreenClicked, this, &MainWindow::onToggleFullScreen);

    // 播放列表信号 -> PlaylistManager & 播放控制
    connect(m_playlist, &PlaylistManager::playlistChanged, this, [this]() {
        m_playlistWidget->updateList(m_playlist->files(), m_playlist->currentIndex());
    });
    connect(m_playlist, &PlaylistManager::currentIndexChanged, m_playlistWidget, &PlaylistWidget::setCurrentIndex);

    connect(m_playlistWidget, &PlaylistWidget::itemDoubleClicked, this, [this](int index) {
        m_playlist->setCurrentIndex(index);
        loadAndPlayFile(m_playlist->currentFile());
    });
    connect(m_playlistWidget, &PlaylistWidget::addFilesRequested, this, &MainWindow::onOpenFiles);
    connect(m_playlistWidget, &PlaylistWidget::removeIndexRequested, m_playlist, &PlaylistManager::removeAt);
    connect(m_playlistWidget, &PlaylistWidget::clearRequested, m_playlist, &PlaylistManager::clear);
    connect(m_playlistWidget, &PlaylistWidget::playModeChanged, this, [](ConfigManager::PlayMode mode) {
        ConfigManager::instance().setPlayMode(mode);
    });
}

void MainWindow::loadAndPlayFile(const QString &filePath)
{
    if (filePath.isEmpty() || !QFileInfo::exists(filePath)) {
        return;
    }

    // 切换视频前先保存上一个视频的观看进度
    saveCurrentProgress();

    // 打开本地文件即退出在线连播模式
    clearOnlineQueue();

    m_initialSeekApplied = false;
    m_outroSkipTriggered = false;

    auto &cfg = ConfigManager::instance();
    if (m_player->openMedia(filePath, cfg.hwAccelEnabled(), cfg.hwAccelBackend())) {
        const QFileInfo fi(filePath);
        setWindowTitle(QStringLiteral("%1 - LiteVLC Player").arg(fi.fileName()));
        m_player->setVolume(cfg.volume());
    }
}

void MainWindow::addAndPlayFiles(const QStringList &filePaths)
{
    if (filePaths.isEmpty()) {
        return;
    }
    int firstIdx = m_playlist->addFiles(filePaths);
    if (firstIdx >= 0) {
        m_playlist->setCurrentIndex(firstIdx);
        loadAndPlayFile(m_playlist->currentFile());
    }
}

// ============================================================================
// 在线片源（搜索 / 播放 / 连播）
// ============================================================================

void MainWindow::onOpenOnlineSearch()
{
    if (!m_sourceDialog) {
        m_sourceDialog = new SourceDialog(this);
        connect(m_sourceDialog, &SourceDialog::playRequested, this, &MainWindow::onOnlinePlayRequested);
    }
    m_sourceDialog->focusSearchInput();
    m_sourceDialog->exec();
}

void MainWindow::onOnlinePlayRequested(const OnlinePlayRequest &request)
{
    if (!request.isValid()) {
        return;
    }
    m_onlineRequest = request;
    m_manualPlayAttempt = true;
    playOnlineEpisode(request.startIndex);
}

void MainWindow::playOnlineEpisode(int index)
{
    if (index < 0 || index >= m_onlineRequest.episodes.size()) {
        return;
    }

    m_onlineRequest.startIndex = index;
    m_initialSeekApplied = false;
    m_outroSkipTriggered = false;
    m_playRetryCount = 0;

    const VodEpisode episode = m_onlineRequest.episodes.at(index);

    if (!m_probe) {
        m_probe = new StreamProbe(this);
        connect(m_probe, &StreamProbe::finished, this, &MainWindow::onStreamProbeFinished);
    }
    m_probe->cancel();

    // 真实源站很常见的情况是「搜索能通、直链 403 / 是网页 / 链接过期」，
    // 直接丢给 LibVLC 只会黑屏且没有任何提示，所以先探一次再决定用什么 header 播。
    m_videoWidget->showOsdMessage(QStringLiteral("正在连接 %1 …").arg(episode.name), 8000);
    m_probe->start(episode.url, m_onlineRequest.userAgent, m_onlineRequest.referer);
}

void MainWindow::onStreamProbeFinished(const StreamProbeResult &result)
{
    if (!hasOnlineQueue()) {
        return;
    }

    const VodEpisode episode = m_onlineRequest.episodes.at(m_onlineRequest.startIndex);

    if (!result.ok) {
        // Qt 与 LibVLC 用的不是同一套 TLS 实现（Qt 走 Schannel，VLC 走 GnuTLS），
        // 预检挂在证书上并不代表内核也播不了，这种情况直接交给内核试一次。
        if (result.error.startsWith(QStringLiteral("ssl"))
            || result.error.startsWith(QStringLiteral("timeout"))) {
            auto &cfgTls = ConfigManager::instance();
            m_lastProbeHasHeaders = false;
            if (m_player->openUrl(episode.url, m_onlineRequest.userAgent, m_onlineRequest.referer,
                                  cfgTls.hwAccelEnabled(), cfgTls.hwAccelBackend())) {
                setWindowTitle(QStringLiteral("%1 · %2 - LiteVLC Player")
                                   .arg(m_onlineRequest.title, episode.name));
                m_videoWidget->showOsdMessage(
                    QStringLiteral("预检未通过（%1），已交给播放内核直连").arg(result.userMessage), 4000);
                return;
            }
        }

        m_videoWidget->showOsdMessage(
            QStringLiteral("播放失败：%1").arg(result.userMessage), 8000);
        if (m_manualPlayAttempt) {
            m_manualPlayAttempt = false;
            QMessageBox::warning(this, QStringLiteral("播放失败"),
                                 QStringLiteral("%1\n\n剧集：%2\n地址：%3")
                                     .arg(result.userMessage, episode.name, episode.url));
        }
        return;
    }

    saveCurrentProgress();  // 本地文件切走前先存进度（网络流内部直接跳过）

    m_lastProbe = result;
    m_lastProbeHasHeaders = true;
    m_playRetryCount = 0;

    auto &cfg = ConfigManager::instance();
    if (!m_player->openUrl(result.url, result.userAgent, result.referer,
                           cfg.hwAccelEnabled(), cfg.hwAccelBackend())) {
        m_videoWidget->showOsdMessage(
            QStringLiteral("无法播放该地址：%1").arg(episode.name), 5000);
        return;
    }

    setWindowTitle(QStringLiteral("%1 · %2 - LiteVLC Player").arg(m_onlineRequest.title, episode.name));
    m_player->setVolume(cfg.volume());
    m_videoWidget->showOsdMessage(
        QStringLiteral("▶ %1  %2  (%3/%4)")
            .arg(m_onlineRequest.title, episode.name)
            .arg(m_onlineRequest.startIndex + 1)
            .arg(m_onlineRequest.episodes.size()),
        3000);
}

void MainWindow::onPlayerError(const QString &message)
{
    // 预检只看主播放列表，分片/子清单可能被另一套防盗链挡住。
    // 这种情况换个 header 组合重开一次往往就通了，所以先自动重试，再报错。
    if (m_player->isStreaming() && hasOnlineQueue() && m_playRetryCount < 2) {
        ++m_playRetryCount;

        QString ua = m_lastProbeHasHeaders ? m_lastProbe.userAgent : m_onlineRequest.userAgent;
        QString referer = m_lastProbeHasHeaders ? m_lastProbe.referer : m_onlineRequest.referer;
        if (m_playRetryCount == 1) {
            referer.clear();          // 常见：Referer 反而触发 403
        } else {
            ua.clear();               // 再退一步：完全裸请求
            referer.clear();
        }

        auto &cfg = ConfigManager::instance();
        const VodEpisode episode = m_onlineRequest.episodes.at(m_onlineRequest.startIndex);
        if (m_player->openUrl(episode.url, ua, referer, cfg.hwAccelEnabled(), cfg.hwAccelBackend())) {
            m_videoWidget->showOsdMessage(
                QStringLiteral("换一组请求头重试（%1/2）…").arg(m_playRetryCount), 3000);
            return;
        }
    }

    m_videoWidget->showOsdMessage(QStringLiteral("播放失败：%1").arg(message), 8000);
    if (m_manualPlayAttempt) {
        m_manualPlayAttempt = false;
        QMessageBox::warning(this, QStringLiteral("播放失败"),
                             QStringLiteral("%1\n\n当前地址：%2")
                                 .arg(message, m_player->currentUrl()));
    }
}

void MainWindow::playNextOnlineEpisode()
{
    if (!hasOnlineQueue()) {
        return;
    }
    if (m_onlineRequest.startIndex + 1 >= m_onlineRequest.episodes.size()) {
        m_videoWidget->showOsdMessage(QStringLiteral("已经是最后一集了"));
        return;
    }
    playOnlineEpisode(m_onlineRequest.startIndex + 1);
}

void MainWindow::playPrevOnlineEpisode()
{
    if (!hasOnlineQueue()) {
        return;
    }
    if (m_onlineRequest.startIndex <= 0) {
        m_videoWidget->showOsdMessage(QStringLiteral("已经是第一集了"));
        return;
    }
    playOnlineEpisode(m_onlineRequest.startIndex - 1);
}

void MainWindow::clearOnlineQueue()
{
    if (m_probe) {
        m_probe->cancel();
    }
    m_onlineRequest = OnlinePlayRequest();
}

void MainWindow::onDurationChanged(qint64 totalMs)
{
    m_controlBar->updateTime(m_player->currentTimeMs(), totalMs);

    // 视频首次获得有效总时长时，执行【记忆续播】或【跳过片头】
    if (!m_initialSeekApplied && totalMs > 0) {
        m_initialSeekApplied = true;
        auto &cfg = ConfigManager::instance();
        const QString curFile = m_player->currentFilePath();

        // 1. 优先检查是否有上次未看完的历史进度（网络流的 URL 每次不同，不参与记忆续播）
        const qint64 savedPos = m_player->isStreaming() ? 0 : cfg.getSavedPosition(curFile);
        if (savedPos >= 5000 && savedPos < (totalMs - 8000)) {
            m_player->seekTime(savedPos);
            const qint64 sec = savedPos / 1000;
            m_videoWidget->showOsdMessage(
                QStringLiteral("⏱ 已自动恢复上次播放进度 (%1:%2)")
                    .arg(sec / 60, 2, 10, QLatin1Char('0'))
                    .arg(sec % 60, 2, 10, QLatin1Char('0')),
                3000
            );
            return;
        }

        // 2. 若无历史记录且启用了跳过片头，且视频足够长，自动跳过片头
        if (cfg.skipIntroEnabled()) {
            const qint64 introMs = static_cast<qint64>(cfg.skipIntroSeconds()) * 1000;
            const qint64 outroMs = cfg.skipOutroEnabled() ? static_cast<qint64>(cfg.skipOutroSeconds()) * 1000 : 0;
            if (introMs > 0 && totalMs > (introMs + outroMs + 15000)) {
                m_player->seekTime(introMs);
                m_videoWidget->showOsdMessage(
                    QStringLiteral("⏭ 已自动跳过片头 (%1 秒)").arg(cfg.skipIntroSeconds()),
                    2500
                );
            }
        }
    }
}

void MainWindow::onTimeChanged(qint64 currentMs)
{
    const qint64 totalMs = m_player->durationMs();
    m_controlBar->updateTime(currentMs, totalMs);

    // 检查是否触发自动跳过片尾
    auto &cfg = ConfigManager::instance();
    if (!m_outroSkipTriggered && cfg.skipOutroEnabled() && totalMs > 0) {
        const qint64 outroMs = static_cast<qint64>(cfg.skipOutroSeconds()) * 1000;
        const qint64 introMs = cfg.skipIntroEnabled() ? static_cast<qint64>(cfg.skipIntroSeconds()) * 1000 : 0;
        if (outroMs > 0 && totalMs > (introMs + outroMs + 15000) && (totalMs - currentMs) <= outroMs) {
            m_outroSkipTriggered = true;
            m_videoWidget->showOsdMessage(
                QStringLiteral("⏭ 已触发跳过片尾 (%1 秒)，即将播放下一集").arg(cfg.skipOutroSeconds()),
                2500
            );
            // 清除本集历史记录（视为已看完）并切到下一集
            if (!m_player->isStreaming()) {
                cfg.savePosition(m_player->currentFilePath(), totalMs, totalMs);
            }
            onMediaEndReached();
        }
    }
}

void MainWindow::onMediaEndReached()
{
    auto &cfg = ConfigManager::instance();

    // 在线源：本站点线路还有下一集就直接续播，不走本地播放列表逻辑
    if (hasOnlineQueue()) {
        if (m_onlineRequest.startIndex + 1 < m_onlineRequest.episodes.size()) {
            playOnlineEpisode(m_onlineRequest.startIndex + 1);
        } else {
            clearOnlineQueue();
            m_player->stop();
            m_controlBar->updateTime(0, m_player->durationMs());
            m_videoWidget->showOsdMessage(QStringLiteral("这条线路已经播完了"));
        }
        return;
    }

    // 正常播完时移除旧进度记忆
    if (!m_player->isStreaming()) {
        cfg.savePosition(m_player->currentFilePath(), m_player->durationMs(), m_player->durationMs());
    }

    const QString nextFile = m_playlist->nextFileOnEnd(cfg.playMode());
    if (!nextFile.isEmpty()) {
        loadAndPlayFile(nextFile);
    } else {
        m_player->stop();
        m_controlBar->updateTime(0, m_player->durationMs());
    }
}

void MainWindow::rebuildTrackMenus()
{
    // 1. 重建声轨菜单
    m_audioTrackMenu->clear();
    const QList<TrackInfo> aTracks = m_player->audioTracks();
    const int curAudio = m_player->currentAudioTrack();
    if (aTracks.isEmpty()) {
        QAction *emptyAct = m_audioTrackMenu->addAction(QStringLiteral("(当前无可用声轨)"));
        emptyAct->setEnabled(false);
    } else {
        auto *aGroup = new QActionGroup(m_audioTrackMenu);
        for (const TrackInfo &t : aTracks) {
            QAction *act = m_audioTrackMenu->addAction(t.name);
            act->setCheckable(true);
            act->setChecked(t.id == curAudio);
            aGroup->addAction(act);
            const int trackId = t.id;
            connect(act, &QAction::triggered, this, [this, trackId]() {
                m_player->setAudioTrack(trackId);
            });
        }
    }

    // 2. 重建字幕菜单
    m_subtitleMenu->clear();
    m_subtitleMenu->addAction(QStringLiteral("加载外挂字幕文件 (.srt/.ass)..."), this, &MainWindow::onOpenSubtitleFile);
    m_subtitleMenu->addSeparator();
    m_subtitleMenu->addAction(QStringLiteral("字幕提前 0.5 秒 (快捷键 [ )"), this, [this]() {
        m_player->adjustSubtitleDelayMs(-500);
    });
    m_subtitleMenu->addAction(QStringLiteral("字幕延后 0.5 秒 (快捷键 ] )"), this, [this]() {
        m_player->adjustSubtitleDelayMs(500);
    });
    m_subtitleMenu->addAction(QStringLiteral("重置字幕时间轴延迟 (0.0s)"), this, [this]() {
        m_player->setSubtitleDelayMs(0);
    });
    m_subtitleMenu->addSeparator();

    const QList<TrackInfo> sTracks = m_player->subtitleTracks();
    const int curSub = m_player->currentSubtitleTrack();
    if (sTracks.isEmpty()) {
        QAction *emptySub = m_subtitleMenu->addAction(QStringLiteral("(无内嵌/外挂字幕流)"));
        emptySub->setEnabled(false);
    } else {
        auto *sGroup = new QActionGroup(m_subtitleMenu);
        for (const TrackInfo &t : sTracks) {
            QAction *act = m_subtitleMenu->addAction(t.name);
            act->setCheckable(true);
            act->setChecked(t.id == curSub);
            sGroup->addAction(act);
            const int trackId = t.id;
            connect(act, &QAction::triggered, this, [this, trackId]() {
                m_player->setSubtitleTrack(trackId);
            });
        }
    }
}

void MainWindow::onOpenFiles()
{
    const QString filter = QStringLiteral(
        "视频文件 (*.mp4 *.mkv *.avi *.mov *.flv *.wmv *.webm *.ts *.m4v *.rmvb *.mpg *.mpeg);;所有文件 (*.*)"
    );
    const QStringList files = QFileDialog::getOpenFileNames(this, QStringLiteral("选择视频文件"), QString(), filter);
    if (!files.isEmpty()) {
        addAndPlayFiles(files);
    }
}

void MainWindow::onOpenSubtitleFile()
{
    if (m_player->currentFilePath().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先打开并播放一个视频文件，然后再加载外挂字幕。"));
        return;
    }
    const QString filter = QStringLiteral("字幕文件 (*.srt *.ass *.ssa *.vtt *.sub);;所有文件 (*.*)");
    const QString subPath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("加载外挂字幕"),
        QFileInfo(m_player->currentFilePath()).absolutePath(),
        filter
    );
    if (!subPath.isEmpty()) {
        m_player->addSubtitleFile(subPath);
    }
}

void MainWindow::onToggleFullScreen()
{
    if (isFullScreen()) {
        showNormal();
        menuBar()->show();
        m_controlBar->show();
        m_playlistWidget->setVisible(m_playlistWasVisibleBeforeFs);
        m_videoWidget->showOsdMessage(QStringLiteral("⛶ 已退出全屏"));
    } else {
        m_playlistWasVisibleBeforeFs = m_playlistWidget->isVisible();
        menuBar()->hide();
        m_playlistWidget->hide();
        m_controlBar->hide();
        showFullScreen();
        m_videoWidget->showOsdMessage(QStringLiteral("⛶ 全屏模式 (鼠标移至底部呼出控制栏，按 Esc/Enter 退出)"));
    }
}

void MainWindow::onSetStayOnTop(bool onTop)
{
    ConfigManager::instance().setStayOnTop(onTop);
    m_controlBar->setStayOnTopChecked(onTop);
    if (m_actStayOnTop) {
        m_actStayOnTop->blockSignals(true);
        m_actStayOnTop->setChecked(onTop);
        m_actStayOnTop->blockSignals(false);
    }

#if defined(Q_OS_WIN)
    // 使用 Win32 SetWindowPos 实现零闪烁置顶切换，避免 Qt setWindowFlag 重建 HWND 导致 LibVLC 画面黑屏
    HWND hwnd = reinterpret_cast<HWND>(winId());
    SetWindowPos(hwnd, onTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#else
    setWindowFlag(Qt::WindowStaysOnTopHint, onTop);
    show();
#endif

    m_videoWidget->showOsdMessage(onTop ? QStringLiteral("📌 已开启窗口置顶")
                                        : QStringLiteral("📌 已取消窗口置顶"));
}

void MainWindow::onOpenSettings()
{
    SettingsDialog dlg(this);
    dlg.exec();
}

void MainWindow::saveCurrentProgress()
{
    // 网络流的地址带时效签名，存下来也没法复用，不写历史
    if (m_player->isStreaming()) {
        return;
    }
    if (!m_player->currentFilePath().isEmpty() && m_player->durationMs() > 0) {
        ConfigManager::instance().savePosition(
            m_player->currentFilePath(),
            m_player->currentTimeMs(),
            m_player->durationMs()
        );
    }
}

bool MainWindow::isVideoFile(const QString &filePath)
{
    static const QStringList videoExts = {
        QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("avi"),
        QStringLiteral("mov"), QStringLiteral("flv"), QStringLiteral("wmv"),
        QStringLiteral("webm"), QStringLiteral("ts"), QStringLiteral("m4v"),
        QStringLiteral("rmvb"), QStringLiteral("mpg"), QStringLiteral("mpeg"),
        QStringLiteral("3gp"), QStringLiteral("ogv")
    };
    return videoExts.contains(QFileInfo(filePath).suffix().toLower());
}

bool MainWindow::isSubtitleFile(const QString &filePath)
{
    static const QStringList subExts = {
        QStringLiteral("srt"), QStringLiteral("ass"), QStringLiteral("ssa"),
        QStringLiteral("vtt"), QStringLiteral("sub")
    };
    return subExts.contains(QFileInfo(filePath).suffix().toLower());
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    QStringList videoFiles;
    QStringList subFiles;

    for (const QUrl &url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QString localPath = url.toLocalFile();
        if (isSubtitleFile(localPath)) {
            subFiles.append(localPath);
        } else if (isVideoFile(localPath) || QFileInfo(localPath).isFile()) {
            videoFiles.append(localPath);
        }
    }

    if (!videoFiles.isEmpty()) {
        addAndPlayFiles(videoFiles);
    }
    for (const QString &sub : subFiles) {
        m_player->addSubtitleFile(sub);
    }
    event->acceptProposedAction();
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Space:
        m_player->togglePlayPause();
        event->accept();
        return;

    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_F:
        onToggleFullScreen();
        event->accept();
        return;

    case Qt::Key_Escape:
        if (isFullScreen()) {
            onToggleFullScreen();
            event->accept();
            return;
        }
        break;

    case Qt::Key_Left:
        m_player->seekRelative(-5000);
        event->accept();
        return;

    case Qt::Key_Right:
        m_player->seekRelative(5000);
        event->accept();
        return;

    case Qt::Key_Up:
        m_player->adjustVolume(5);
        ConfigManager::instance().setVolume(m_player->volume());
        m_controlBar->setVolumeValue(m_player->volume(), m_player->isMuted());
        event->accept();
        return;

    case Qt::Key_Down:
        m_player->adjustVolume(-5);
        ConfigManager::instance().setVolume(m_player->volume());
        m_controlBar->setVolumeValue(m_player->volume(), m_player->isMuted());
        event->accept();
        return;

    case Qt::Key_M:
        m_player->toggleMute();
        m_controlBar->setVolumeValue(m_player->volume(), m_player->isMuted());
        event->accept();
        return;

    case Qt::Key_BracketLeft: // '[' 字幕提前 0.5s
        m_player->adjustSubtitleDelayMs(-500);
        event->accept();
        return;

    case Qt::Key_BracketRight: // ']' 字幕延后 0.5s
        m_player->adjustSubtitleDelayMs(500);
        event->accept();
        return;

    case Qt::Key_C: { // 减速播放
        const float nextRate = qMax(0.5f, m_player->rate() - 0.25f);
        m_player->setRate(nextRate);
        m_controlBar->setSpeedValue(nextRate);
        event->accept();
        return;
    }

    case Qt::Key_V: { // 加速播放
        const float nextRate = qMin(3.0f, m_player->rate() + 0.25f);
        m_player->setRate(nextRate);
        m_controlBar->setSpeedValue(nextRate);
        event->accept();
        return;
    }

    case Qt::Key_T:
        onSetStayOnTop(!ConfigManager::instance().stayOnTop());
        event->accept();
        return;

    case Qt::Key_L:
        m_playlistWidget->setVisible(!m_playlistWidget->isVisible());
        event->accept();
        return;

    case Qt::Key_PageUp: {
        if (hasOnlineQueue()) {
            playPrevOnlineEpisode();
            event->accept();
            return;
        }
        const QString prev = m_playlist->previousFileManual();
        if (!prev.isEmpty()) loadAndPlayFile(prev);
        event->accept();
        return;
    }

    case Qt::Key_PageDown: {
        if (hasOnlineQueue()) {
            playNextOnlineEpisode();
            event->accept();
            return;
        }
        const QString next = m_playlist->nextFileManual();
        if (!next.isEmpty()) loadAndPlayFile(next);
        event->accept();
        return;
    }

    default:
        break;
    }

    QMainWindow::keyPressEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveCurrentProgress();
    QMainWindow::closeEvent(event);
}
