QT       += core gui widgets network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

TARGET = LiteVlcPlayer
TEMPLATE = app

INCLUDEPATH += $$PWD/src

# 自动匹配 vlc 目录结构
exists($$PWD/vlc/sdk/include) {
    INCLUDEPATH += $$PWD/vlc/sdk/include
    LIBS += -L$$PWD/vlc/sdk/lib -llibvlc -llibvlccore
} else:exists($$PWD/vlc/include) {
    INCLUDEPATH += $$PWD/vlc/include
    LIBS += -L$$PWD/vlc/lib -llibvlc -llibvlccore
}

win32: LIBS += -luser32

# macOS：头文件用仓库内的 thirdparty/vlc/include，库来自 VLC.app
macx {
    INCLUDEPATH += $$PWD/thirdparty/vlc/include
    INCLUDEPATH += $$PWD/vlc/VLC.app/Contents/MacOS/include
    LIBS += -L$$PWD/vlc/VLC.app/Contents/MacOS/lib -llibvlc
    OBJECTIVE_SOURCES += src/platform/MacWindow.mm
    ICON = $$PWD/resources/app.icns
}

# Windows 资源（exe 图标 + 版本信息）
win32: RC_FILE = $$PWD/resources/app.rc
# app.rc 里 ICON "app.ico" 用相对文件名，需要把 resources 加进 windres 的搜索路径
win32: RC_INCLUDEPATH += $$PWD/resources
DEFINES += APP_VERSION=\\\"1.1.4\\\"
# GitHub Releases 更新源（owner/repo）；留空表示未配置
DEFINES += APP_UPDATE_REPO=\\\"yc7en/LiteVLC\\\"

SOURCES += \
    src/main.cpp \
    src/core/AppPaths.cpp \
    src/core/ConfigManager.cpp \
    src/core/SourceManager.cpp \
    src/core/CmsClient.cpp \
    src/core/StreamProbe.cpp \
    src/core/PageSniffer.cpp \
    src/core/StreamResolver.cpp \
    src/core/HlsFilter.cpp \
    src/core/HlsRelay.cpp \
    src/core/UpdateService.cpp \
    src/core/VlcPlayer.cpp \
    src/core/PlaylistManager.cpp \
    src/ui/ClickableSlider.cpp \
    src/ui/VideoWidget.cpp \
    src/ui/ControlBar.cpp \
    src/ui/PlaylistWidget.cpp \
    src/ui/SettingsDialog.cpp \
    src/ui/SourceDialog.cpp \
    src/ui/MainWindow.cpp

HEADERS += \
    src/platform/MacWindow.h \
    src/core/AppPaths.h \
    src/core/ConfigManager.h \
    src/core/SourceManager.h \
    src/core/CmsClient.h \
    src/core/StreamProbe.h \
    src/core/PageSniffer.h \
    src/core/StreamResolver.h \
    src/core/HlsFilter.h \
    src/core/HlsRelay.h \
    src/core/UpdateService.h \
    src/core/VlcPlayer.h \
    src/core/PlaylistEntry.h \
    src/core/PlaylistManager.h \
    src/ui/ClickableSlider.h \
    src/ui/VideoWidget.h \
    src/ui/ControlBar.h \
    src/ui/PlaylistWidget.h \
    src/ui/SettingsDialog.h \
    src/ui/SourceDialog.h \
    src/ui/MainWindow.h
