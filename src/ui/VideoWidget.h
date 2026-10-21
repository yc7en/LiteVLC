#pragma once

#include <QWidget>
#include <QLabel>
#include <QTimer>

/**
 * @brief 视频渲染画布 + 左上角悬浮 OSD 提示层
 * - 提供原生窗口句柄 (winId) 供 LibVLC 硬解/软解直接绘制画面
 * - 捕获鼠标双击（切换全屏）、滚轮（调节音量）、鼠标移动（全屏时唤醒底部控制栏）
 * - 内置独立无边框透明顶层 OSD 气泡，确保在 D3D11/DXVA2 硬解画面之上清晰可见
 */
class VideoWidget : public QWidget
{
    Q_OBJECT
public:
    explicit VideoWidget(QWidget *parent = nullptr);
    ~VideoWidget() override;

    // 在视频左上角显示 OSD 状态文字（如音量、快进、字幕延迟、记忆续播提示）
    void showOsdMessage(const QString &text, int durationMs = 2000);
    void syncOsdPosition();

signals:
    void doubleClicked();
    void wheelVolumeChanged(int delta);
    void mouseMovedInCanvas(const QPoint &globalPos);

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    QLabel *m_osdLabel = nullptr;
    QTimer *m_osdTimer = nullptr;
};
