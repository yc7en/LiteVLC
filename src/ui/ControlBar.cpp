#include "ControlBar.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStyle>

ControlBar::ControlBar(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 4, 10, 8);
    mainLayout->setSpacing(4);

    // 1. 顶部进度条行
    auto *progressLayout = new QHBoxLayout();
    progressLayout->setSpacing(8);

    m_progressSlider = new ClickableSlider(Qt::Horizontal, this);
    m_progressSlider->setRange(0, 1000);
    m_progressSlider->setValue(0);

    m_timeLabel = new QLabel(QStringLiteral("00:00 / 00:00"), this);
    m_timeLabel->setMinimumWidth(105);
    m_timeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    progressLayout->addWidget(m_progressSlider, 1);
    progressLayout->addWidget(m_timeLabel);

    // 2. 底部控制按钮行
    auto *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(6);

    setObjectName(QStringLiteral("controlBarContainer"));

    m_btnPlayPause = new QPushButton(QIcon(QStringLiteral(":/icons/icons/play.svg")), QStringLiteral(" 播放"), this);
    m_btnPlayPause->setObjectName(QStringLiteral("btnPlayPause"));
    m_btnPlayPause->setIconSize(QSize(16, 16));

    m_btnStop      = new QPushButton(QIcon(QStringLiteral(":/icons/icons/stop.svg")), QString(), this);
    m_btnStop->setObjectName(QStringLiteral("btnStop"));
    m_btnStop->setToolTip(QStringLiteral("停止"));
    m_btnStop->setIconSize(QSize(15, 15));

    m_btnPrev      = new QPushButton(QIcon(QStringLiteral(":/icons/icons/prev.svg")), QString(), this);
    m_btnPrev->setObjectName(QStringLiteral("btnPrev"));
    m_btnPrev->setToolTip(QStringLiteral("上一集 (PgUp)"));
    m_btnPrev->setIconSize(QSize(15, 15));

    m_btnNext      = new QPushButton(QIcon(QStringLiteral(":/icons/icons/next.svg")), QString(), this);
    m_btnNext->setObjectName(QStringLiteral("btnNext"));
    m_btnNext->setToolTip(QStringLiteral("下一集 (PgDn)"));
    m_btnNext->setIconSize(QSize(15, 15));

    // 倍速下拉框 (0.5x ~ 3.0x)
    m_speedCombo = new QComboBox(this);
    m_speedCombo->setToolTip(QStringLiteral("倍速播放 (快捷键 C 减速 / V 加速)"));
    const QList<float> speeds = {0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 2.5f, 3.0f};
    for (float s : speeds) {
        m_speedCombo->addItem(QStringLiteral("%1x").arg(s, 0, 'f', (s == 0.75f || s == 1.25f) ? 2 : 1), s);
    }
    m_speedCombo->setCurrentIndex(2); // 默认 1.0x

    // 音轨与字幕菜单按钮
    m_btnAudioTrack = new QPushButton(QStringLiteral("音轨"), this);
    m_btnAudioTrack->setToolTip(QStringLiteral("切换多语言声轨"));

    m_btnSubtitle = new QPushButton(QStringLiteral("字幕"), this);
    m_btnSubtitle->setToolTip(QStringLiteral("切换字幕 / 加载外挂字幕 / 微调时间轴 ([ / ])"));

    // 静音与音量滑块
    m_btnMute = new QPushButton(QIcon(QStringLiteral(":/icons/icons/volume.svg")), QString(), this);
    m_btnMute->setObjectName(QStringLiteral("btnMute"));
    m_btnMute->setToolTip(QStringLiteral("一键静音 (M)"));
    m_btnMute->setIconSize(QSize(16, 16));

    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(80);
    m_volumeSlider->setFixedWidth(90);
    m_volumeSlider->setToolTip(QStringLiteral("音量调节 (滚轮 / 上下方向键)"));

    // 右侧功能按钮：置顶、设置、播放列表、全屏
    m_btnStayOnTop = new QPushButton(QIcon(QStringLiteral(":/icons/icons/pin.svg")), QStringLiteral(" 置顶"), this);
    m_btnStayOnTop->setCheckable(true);
    m_btnStayOnTop->setToolTip(QStringLiteral("窗口始终置顶 (T)"));
    m_btnStayOnTop->setIconSize(QSize(14, 14));

    m_btnSettings = new QPushButton(QIcon(QStringLiteral(":/icons/icons/settings.svg")), QString(), this);
    m_btnSettings->setToolTip(QStringLiteral("GPU 硬解开关 / 跳过片头片尾 / 记忆播放设置"));
    m_btnSettings->setIconSize(QSize(15, 15));

    m_btnSearch = new QPushButton(QIcon(QStringLiteral(":/icons/icons/search.svg")), QStringLiteral(" 在线"), this);
    m_btnSearch->setToolTip(QStringLiteral("在线片源搜索（Ctrl+F）"));
    m_btnSearch->setIconSize(QSize(14, 14));

    m_btnPlaylist = new QPushButton(QIcon(QStringLiteral(":/icons/icons/playlist.svg")), QStringLiteral(" 列表"), this);
    m_btnPlaylist->setToolTip(QStringLiteral("显示/隐藏轻量播放列表 (L)"));
    m_btnPlaylist->setIconSize(QSize(14, 14));

    m_btnFullscreen = new QPushButton(QIcon(QStringLiteral(":/icons/icons/fullscreen.svg")), QString(), this);
    m_btnFullscreen->setObjectName(QStringLiteral("btnFullscreen"));
    m_btnFullscreen->setToolTip(QStringLiteral("全屏切换 (双击画面 / Enter)"));
    m_btnFullscreen->setIconSize(QSize(16, 16));

    btnLayout->addWidget(m_btnPlayPause);
    btnLayout->addWidget(m_btnStop);
    btnLayout->addWidget(m_btnPrev);
    btnLayout->addWidget(m_btnNext);
    btnLayout->addSpacing(8);
    btnLayout->addWidget(new QLabel(QStringLiteral("倍速:"), this));
    btnLayout->addWidget(m_speedCombo);
    btnLayout->addWidget(m_btnAudioTrack);
    btnLayout->addWidget(m_btnSubtitle);
    btnLayout->addSpacing(8);
    btnLayout->addWidget(m_btnMute);
    btnLayout->addWidget(m_volumeSlider);
    btnLayout->addStretch(1);
    btnLayout->addWidget(m_btnStayOnTop);
    btnLayout->addWidget(m_btnSearch);
    btnLayout->addWidget(m_btnSettings);
    btnLayout->addWidget(m_btnPlaylist);
    btnLayout->addWidget(m_btnFullscreen);

    mainLayout->addLayout(progressLayout);
    mainLayout->addLayout(btnLayout);

    // 信号连接
    connect(m_btnPlayPause, &QPushButton::clicked, this, &ControlBar::playPauseClicked);
    connect(m_btnStop,      &QPushButton::clicked, this, &ControlBar::stopClicked);
    connect(m_btnPrev,      &QPushButton::clicked, this, &ControlBar::prevClicked);
    connect(m_btnNext,      &QPushButton::clicked, this, &ControlBar::nextClicked);

    connect(m_progressSlider, &ClickableSlider::seekRequested, this, [this](int sliderVal) {
        if (m_durationMs > 0) {
            const qint64 targetMs = (static_cast<qint64>(sliderVal) * m_durationMs) / 1000;
            emit seekRequested(targetMs);
        }
    });

    connect(m_speedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (idx >= 0) {
            emit speedChanged(m_speedCombo->itemData(idx).toFloat());
        }
    });

    connect(m_btnMute, &QPushButton::clicked, this, &ControlBar::muteClicked);
    connect(m_volumeSlider, &QSlider::valueChanged, this, &ControlBar::volumeChanged);

    connect(m_btnStayOnTop,  &QPushButton::toggled, this, &ControlBar::stayOnTopToggled);
    connect(m_btnSearch,     &QPushButton::clicked, this, &ControlBar::onlineSearchClicked);
    connect(m_btnSettings,   &QPushButton::clicked, this, &ControlBar::settingsClicked);
    connect(m_btnPlaylist,   &QPushButton::clicked, this, &ControlBar::playlistToggled);
    connect(m_btnFullscreen, &QPushButton::clicked, this, &ControlBar::fullscreenClicked);
}

void ControlBar::setPlayingState(bool playing)
{
    if (playing) {
        m_btnPlayPause->setIcon(QIcon(QStringLiteral(":/icons/icons/pause.svg")));
        m_btnPlayPause->setText(QStringLiteral(" 暂停"));
    } else {
        m_btnPlayPause->setIcon(QIcon(QStringLiteral(":/icons/icons/play.svg")));
        m_btnPlayPause->setText(QStringLiteral(" 播放"));
    }
}

void ControlBar::updateTime(qint64 currentMs, qint64 durationMs)
{
    m_durationMs = durationMs;
    m_timeLabel->setText(QStringLiteral("%1 / %2")
        .arg(formatTimeMs(currentMs))
        .arg(formatTimeMs(durationMs)));

    if (!m_progressSlider->isUserDragging()) {
        m_progressSlider->blockSignals(true);
        if (durationMs > 0) {
            int val = static_cast<int>((currentMs * 1000) / durationMs);
            m_progressSlider->setValue(qBound(0, val, 1000));
        } else {
            m_progressSlider->setValue(0);
        }
        m_progressSlider->blockSignals(false);
    }
}

void ControlBar::setVolumeValue(int volume, bool muted)
{
    m_volumeSlider->blockSignals(true);
    m_volumeSlider->setValue(qBound(0, volume, 100));
    m_volumeSlider->blockSignals(false);

    if (muted || volume == 0) {
        m_btnMute->setIcon(QIcon(QStringLiteral(":/icons/icons/mute.svg")));
    } else {
        m_btnMute->setIcon(QIcon(QStringLiteral(":/icons/icons/volume.svg")));
    }
}

void ControlBar::setStayOnTopChecked(bool onTop)
{
    m_btnStayOnTop->blockSignals(true);
    m_btnStayOnTop->setChecked(onTop);
    m_btnStayOnTop->blockSignals(false);
}

void ControlBar::setSpeedValue(float rate)
{
    m_speedCombo->blockSignals(true);
    int bestIdx = 2;
    float minDiff = 999.0f;
    for (int i = 0; i < m_speedCombo->count(); ++i) {
        float r = m_speedCombo->itemData(i).toFloat();
        if (qAbs(r - rate) < minDiff) {
            minDiff = qAbs(r - rate);
            bestIdx = i;
        }
    }
    m_speedCombo->setCurrentIndex(bestIdx);
    m_speedCombo->blockSignals(false);
}

QString ControlBar::formatTimeMs(qint64 ms)
{
    if (ms <= 0) {
        return QStringLiteral("00:00");
    }
    const qint64 totalSeconds = ms / 1000;
    const qint64 hours   = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;

    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours,   2, 10, QLatin1Char('0'))
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}
