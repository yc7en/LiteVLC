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

SOURCES += \
    src/main.cpp \
    src/core/ConfigManager.cpp \
    src/core/SourceManager.cpp \
    src/core/CmsClient.cpp \
    src/core/StreamProbe.cpp \
    src/core/PageSniffer.cpp \
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
    src/core/ConfigManager.h \
    src/core/SourceManager.h \
    src/core/CmsClient.h \
    src/core/StreamProbe.h \
    src/core/PageSniffer.h \
    src/core/VlcPlayer.h \
    src/core/PlaylistManager.h \
    src/ui/ClickableSlider.h \
    src/ui/VideoWidget.h \
    src/ui/ControlBar.h \
    src/ui/PlaylistWidget.h \
    src/ui/SettingsDialog.h \
    src/ui/SourceDialog.h \
    src/ui/MainWindow.h
