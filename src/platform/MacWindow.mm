#include "MacWindow.h"

#import <Cocoa/Cocoa.h>

#include <QWidget>

namespace MacWindow {

void setAlwaysOnTop(QWidget *widget, bool onTop)
{
    if (!widget) {
        return;
    }
    // QWidget::winId() 在 macOS 上就是 NSView*
    NSView *view = reinterpret_cast<NSView *>(widget->winId());
    NSWindow *window = view ? [view window] : nil;
    if (!window) {
        return;
    }
    // 只改窗口层级，不动窗口本身：LibVLC 的渲染目标保持有效
    [window setLevel:(onTop ? NSFloatingWindowLevel : NSNormalWindowLevel)];
}

} // namespace MacWindow
