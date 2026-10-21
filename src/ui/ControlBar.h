#pragma once

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QSlider>
#include "ClickableSlider.h"

/**
 * @brief 底部控制栏控件
 * 集成：
 * - 进度条 (ClickableSlider) + 当前时间/总时长显示
 * - 播放/暂停、停止、上一集、下一集
 * - 0.5x ~ 3.0x 倍速选择下拉框
 * - 音轨切换菜单按钮、字幕菜单按钮 (切换字幕/加载外挂字幕/微调延迟)
 * - 一键静音按钮 + 音量滑块
 * - 置顶切换按钮、设置按钮、播放列表抽屉开关、全屏切换按钮
 */
class ControlBar : public QWidget
{
    Q_OBJECT
public:
    explicit ControlBar(QWidget *parent = nullptr);

    void setPlayingState(bool playing);
    void updateTime(qint64 currentMs, qint64 durationMs);
    void setVolumeValue(int volume, bool muted);
    void setStayOnTopChecked(bool onTop);
    void setSpeedValue(float rate);

    QPushButton* audioTrackButton() const { return m_btnAudioTrack; }
    QPushButton* subtitleButton() const { return m_btnSubtitle; }

signals:
    void playPauseClicked();
    void stopClicked();
    void prevClicked();
    void nextClicked();
    void seekRequested(qint64 targetMs);
    void volumeChanged(int volume);
    void muteClicked();
    void speedChanged(float rate);
    void stayOnTopToggled(bool checked);
    void settingsClicked();
    void playlistToggled();
    void fullscreenClicked();
    void onlineSearchClicked();

private:
    static QString formatTimeMs(qint64 ms);

    ClickableSlider *m_progressSlider = nullptr;
    QLabel *m_timeLabel = nullptr;

    QPushButton *m_btnPlayPause = nullptr;
    QPushButton *m_btnStop = nullptr;
    QPushButton *m_btnPrev = nullptr;
    QPushButton *m_btnNext = nullptr;

    QComboBox *m_speedCombo = nullptr;
    QPushButton *m_btnAudioTrack = nullptr;
    QPushButton *m_btnSubtitle = nullptr;

    QPushButton *m_btnMute = nullptr;
    QSlider *m_volumeSlider = nullptr;

    QPushButton *m_btnStayOnTop = nullptr;
    QPushButton *m_btnSearch = nullptr;
    QPushButton *m_btnSettings = nullptr;
    QPushButton *m_btnPlaylist = nullptr;
    QPushButton *m_btnFullscreen = nullptr;

    qint64 m_durationMs = 0;
};
