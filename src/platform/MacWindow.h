#pragma once

#include <QtGlobal>

class QWidget;

/**
 * @brief 少量必须落到原生窗口系统的操作（目前只有窗口置顶）
 *
 * Windows 上 QWidget::winId() 是 HWND，macOS 上是 NSView*。
 * 两者都不能用 QWidget::setWindowFlag() 来切置顶——那会重建原生窗口，
 * LibVLC 已经绑定的渲染目标会失效（表现为黑屏）。
 *
 * 非 macOS 平台这里是空实现，调用方不需要写 #if。
 */
namespace MacWindow {

#if defined(Q_OS_MAC)
void setAlwaysOnTop(QWidget *widget, bool onTop);
#else
inline void setAlwaysOnTop(QWidget *, bool) {}
#endif

/** 是否走原生菜单栏（macOS 的 QMenuBar 在系统菜单栏里，hide/show 无效） */
inline bool usesNativeMenuBar()
{
#if defined(Q_OS_MAC)
    return true;
#else
    return false;
#endif
}

} // namespace MacWindow
