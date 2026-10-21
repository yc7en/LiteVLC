cmake_minimum_required(VERSION 3.16)
project(LiteVlcPlayer VERSION 1.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg Network)

# ==============================================================================
# LibVLC SDK 路径自动探测 (优先查找项目目录下的 vlc/ 或 thirdparty/vlc/)
# ==============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vlc/sdk/include")
    set(VLC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vlc")
    set(VLC_INCLUDE_DIR "${VLC_ROOT}/sdk/include")
    set(VLC_LIB_DIR "${VLC_ROOT}/sdk/lib")
    set(VLC_BIN_DIR "${VLC_ROOT}")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vlc/include")
    set(VLC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vlc")
    set(VLC_INCLUDE_DIR "${VLC_ROOT}/include")
    set(VLC_LIB_DIR "${VLC_ROOT}/lib")
    set(VLC_BIN_DIR "${VLC_ROOT}/bin")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/vlc/include")
    set(VLC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/vlc")
    set(VLC_INCLUDE_DIR "${VLC_ROOT}/include")
    set(VLC_LIB_DIR "${VLC_ROOT}/lib")
    set(VLC_BIN_DIR "${VLC_ROOT}/bin")
else()
    message(FATAL_ERROR "未找到 LibVLC SDK！请确保 vlc 目录存在于 ${CMAKE_CURRENT_SOURCE_DIR}/vlc")
endif()

message(STATUS "Found LibVLC Include: ${VLC_INCLUDE_DIR}")
message(STATUS "Found LibVLC Lib Dir: ${VLC_LIB_DIR}")
message(STATUS "Found LibVLC Bin Dir: ${VLC_BIN_DIR}")

set(PROJECT_SOURCES
    src/main.cpp
    src/core/AppPaths.h
    src/core/AppPaths.cpp
    src/core/ConfigManager.h
    src/core/ConfigManager.cpp
    src/core/SourceManager.h
    src/core/SourceManager.cpp
    src/core/CmsClient.h
    src/core/CmsClient.cpp
    src/core/StreamProbe.h
    src/core/StreamProbe.cpp
    src/core/PageSniffer.h
    src/core/PageSniffer.cpp
    src/core/StreamResolver.h
    src/core/StreamResolver.cpp
    src/core/HlsFilter.h
    src/core/HlsFilter.cpp
    src/core/HlsRelay.h
    src/core/HlsRelay.cpp
    src/core/UpdateService.h
    src/core/UpdateService.cpp
    src/core/VlcPlayer.h
    src/core/VlcPlayer.cpp
    src/core/PlaylistEntry.h
    src/core/PlaylistManager.h
    src/core/PlaylistManager.cpp
    src/ui/ClickableSlider.h
    src/ui/ClickableSlider.cpp
    src/ui/VideoWidget.h
    src/ui/VideoWidget.cpp
    src/ui/ControlBar.h
    src/ui/ControlBar.cpp
    src/ui/PlaylistWidget.h
    src/ui/PlaylistWidget.cpp
    src/ui/SettingsDialog.h
    src/ui/SettingsDialog.cpp
    src/ui/SourceDialog.h
    src/ui/SourceDialog.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    resources/resources.qrc
)

# Windows 资源（exe 图标 + 版本信息）
if(WIN32)
    enable_language(RC)
    list(APPEND PROJECT_SOURCES resources/app.rc)
endif()

qt_add_executable(LiteVlcPlayer
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
)

target_compile_definitions(LiteVlcPlayer PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
)

target_include_directories(LiteVlcPlayer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${VLC_INCLUDE_DIR}
)

target_link_directories(LiteVlcPlayer PRIVATE
    ${VLC_LIB_DIR}
)

# MSVC 与 MinGW 均兼容链接 libvlc / libvlccore
target_link_libraries(LiteVlcPlayer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
    Qt6::Network
    libvlc
    libvlccore
)

if(WIN32)
    target_link_libraries(LiteVlcPlayer PRIVATE user32)
    set_target_properties(LiteVlcPlayer PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

# 编译完成后自动将 libvlc.dll, libvlccore.dll 复制到可执行文件目录
if(WIN32 AND EXISTS "${VLC_BIN_DIR}/libvlc.dll")
    add_custom_command(TARGET LiteVlcPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VLC_BIN_DIR}/libvlc.dll"
            "$<TARGET_FILE_DIR:LiteVlcPlayer>/libvlc.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VLC_BIN_DIR}/libvlccore.dll"
            "$<TARGET_FILE_DIR:LiteVlcPlayer>/libvlccore.dll"
        COMMENT "Copying LibVLC DLLs to output directory..."
    )
endif()

qt_finalize_executable(LiteVlcPlayer)
