#include "VideoWidget.h"

#include <QMouseEvent>
#include <QWheelEvent>
#include <QPalette>

VideoWidget::VideoWidget(QWidget *parent)
    : QWidget(parent)
{
    // 确保拥有独立原生窗口句柄 (HWND)，避免 LibVLC 覆盖父窗口其他 Qt 控件
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setMouseTracking(true);
    setMinimumSize(480, 270);

    // 黑色背景
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setAutoFillBackground(true);
    setPalette(pal);

    // 创建悬浮 OSD 提示标签 (ToolTip 窗口层级可覆盖 DirectX 硬解 HWND 渲染表面)
    m_osdLabel = new QLabel(this);
    m_osdLabel->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowTransparentForInput);
    m_osdLabel->setAttribute(Qt::WA_ShowWithoutActivating, true);
    m_osdLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_osdLabel->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  background-color: rgba(20, 20, 20, 210);"
        "  color: #FFFFFF;"
        "  font-size: 14px;"
        "  font-weight: bold;"
        "  padding: 6px 12px;"
        "  border-radius: 4px;"
        "  border: 1px solid rgba(255, 255, 255, 45);"
        "}"
    ));
    m_osdLabel->hide();

    m_osdTimer = new QTimer(this);
    m_osdTimer->setSingleShot(true);
    connect(m_osdTimer, &QTimer::timeout, m_osdLabel, &QWidget::hide);
}

VideoWidget::~VideoWidget()
{
    if (m_osdLabel) {
        m_osdLabel->hide();
    }
}

void VideoWidget::showOsdMessage(const QString &text, int durationMs)
{
    if (!isVisible() || text.isEmpty()) {
        return;
    }
    m_osdLabel->setText(text);
    m_osdLabel->adjustSize();
    syncOsdPosition();
    m_osdLabel->show();
    m_osdLabel->raise();
    m_osdTimer->start(durationMs);
}

void VideoWidget::syncOsdPosition()
{
    if (!m_osdLabel) {
        return;
    }
    const QPoint topLeftGlobal = mapToGlobal(QPoint(18, 18));
    m_osdLabel->move(topLeftGlobal);
}

void VideoWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit doubleClicked();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void VideoWidget::wheelEvent(QWheelEvent *event)
{
    const int angle = event->angleDelta().y();
    if (angle != 0) {
        const int step = (angle > 0) ? 5 : -5;
        emit wheelVolumeChanged(step);
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

void VideoWidget::mouseMoveEvent(QMouseEvent *event)
{
    emit mouseMovedInCanvas(event->globalPosition().toPoint());
    QWidget::mouseMoveEvent(event);
}

void VideoWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    syncOsdPosition();
}

void VideoWidget::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    syncOsdPosition();
}

void VideoWidget::hideEvent(QHideEvent *event)
{
    if (m_osdLabel) {
        m_osdLabel->hide();
    }
    QWidget::hideEvent(event);
}
