cmake_minimum_required(VERSION 3.16)
project(LiteVlcPlayer VERSION 1.1.5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg Network)

# ==============================================================================
# 平台默认值：macOS 默认出 Apple Silicon（arm64）版本；x86_64（Intel Mac）由 CI
# 矩阵显式传 -DCMAKE_OSX_ARCHITECTURES=x86_64（见 .github/workflows/macos.yml）
# ==============================================================================
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "macOS target architecture" FORCE)
    message(STATUS "macOS 目标架构: arm64（仅 Apple Silicon）")
endif()
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    # Qt 6.8 要求 macOS 12+
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "macOS deployment target" FORCE)
endif()

# ==============================================================================
# LibVLC SDK 路径自动探测
#   Windows：vlc/sdk/{include,lib}、vlc/{include,lib}、thirdparty/vlc/{include,lib}
#   macOS  ：VLC.app/Contents/MacOS/{lib,plugins}
#            （VLC.app 不带头文件，头文件用仓库内的 thirdparty/vlc/include）
# ==============================================================================
if(APPLE)
    set(VLC_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/vlc/include")
    set(_vlc_app_roots
        "${CMAKE_CURRENT_SOURCE_DIR}/vlc/VLC.app/Contents/MacOS"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/VLC.app/Contents/MacOS"
        "/Applications/VLC.app/Contents/MacOS")
    foreach(_root IN LISTS _vlc_app_roots)
        if(EXISTS "${_root}/lib/libvlc.dylib")
            set(VLC_ROOT "${_root}")
            set(VLC_LIB_DIR "${_root}/lib")
            set(VLC_PLUGIN_DIR "${_root}/plugins")
            break()
        endif()
    endforeach()
    if(NOT VLC_LIB_DIR)
        message(FATAL_ERROR
            "未找到 LibVLC（macOS）！请把 VLC.app 放到 ${CMAKE_CURRENT_SOURCE_DIR}/vlc/ 下，"
            "或安装到 /Applications。CI 里用 vlc-3.0.21-arm64.dmg 解开即可。")
    endif()
    if(NOT EXISTS "${VLC_INCLUDE_DIR}/vlc/vlc.h")
        message(FATAL_ERROR "缺少 VLC 头文件：${VLC_INCLUDE_DIR}/vlc/vlc.h")
    endif()
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vlc/sdk/include")
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
    # CI / 只有仓库内 SDK 的形态：头文件与导入库在 thirdparty，运行时 DLL 与插件
    # 由 vlc/ 下解出的官方发行包提供（官方 win64 zip 不含 sdk/，所以库要自带）。
    set(VLC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/vlc")
    set(VLC_INCLUDE_DIR "${VLC_ROOT}/include")
    set(VLC_LIB_DIR "")
    foreach(_libdir "${VLC_ROOT}/lib/win64" "${VLC_ROOT}/lib/macos" "${VLC_ROOT}/lib")
        if(EXISTS "${_libdir}/libvlc.lib" OR EXISTS "${_libdir}/libvlc.dll.a" OR EXISTS "${_libdir}/libvlc.dylib")
            set(VLC_LIB_DIR "${_libdir}")
            break()
        endif()
    endforeach()
    if(NOT VLC_LIB_DIR)
        message(FATAL_ERROR "thirdparty/vlc 下找不到 LibVLC 导入库（lib/win64 或 lib/macos）")
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vlc/libvlc.dll")
        set(VLC_BIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vlc")
    else()
        set(VLC_BIN_DIR "${VLC_ROOT}/bin")
    endif()
else()
    message(FATAL_ERROR "未找到 LibVLC SDK！请确保 vlc 目录存在于 ${CMAKE_CURRENT_SOURCE_DIR}/vlc")
endif()

message(STATUS "Found LibVLC Include: ${VLC_INCLUDE_DIR}")
message(STATUS "Found LibVLC Lib Dir: ${VLC_LIB_DIR}")
if(VLC_BIN_DIR)
    message(STATUS "Found LibVLC Bin Dir: ${VLC_BIN_DIR}")
endif()
if(VLC_PLUGIN_DIR)
    message(STATUS "Found VLC Plugins: ${VLC_PLUGIN_DIR}")
endif()

set(PROJECT_SOURCES
    src/main.cpp
    src/platform/MacWindow.h
    src/core/AppPaths.h
    src/core/AppPaths.cpp
    src/core/ConfigManager.h
    src/core/ConfigManager.cpp
    src/core/SourceManager.h
    src/core/SourceManager.cpp
    src/core/LiveParser.h
    src/core/LiveParser.cpp
    src/core/LiveClient.h
    src/core/LiveClient.cpp
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

# Windows 资源（exe 图标 + 版本信息）/ macOS bundle 资源（icns）
if(WIN32)
    enable_language(RC)
    # app.rc 里的 ICON "app.ico" 是相对文件名，windres 只按 -I 搜索该文件；
    # 不写死绝对路径，否则 CI（或换台机器、换个克隆目录）就编不过。
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I\"${CMAKE_CURRENT_SOURCE_DIR}/resources\"")
    list(APPEND PROJECT_SOURCES resources/app.rc)
endif()

if(APPLE)
    enable_language(OBJCXX)
    list(APPEND PROJECT_SOURCES src/platform/MacWindow.mm)
    set_source_files_properties(resources/app.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    list(APPEND PROJECT_SOURCES resources/app.icns)
endif()

# GitHub Releases 更新源：owner/repo。留空 = 未配置（检查更新只回显版本）。
# 换仓库时改这里，或命令行覆盖：-DAPP_UPDATE_REPO=owner/repo
set(APP_UPDATE_REPO "yc7en/LiteVLC" CACHE STRING "GitHub repository (owner/repo) used as the release/update source")

qt_add_executable(LiteVlcPlayer
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
)

target_compile_definitions(LiteVlcPlayer PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
    APP_UPDATE_REPO="${APP_UPDATE_REPO}"
)

target_include_directories(LiteVlcPlayer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${VLC_INCLUDE_DIR}
)

target_link_directories(LiteVlcPlayer PRIVATE
    ${VLC_LIB_DIR}
)

# MSVC 与 MinGW 均兼容链接 libvlc / libvlccore；macOS 上 libvlc 已传递依赖 libvlccore。
# 注意 macOS 不能用裸名 libvlc：ld 会把 -llibvlc 解析成 liblibvlc.dylib（实际文件名是
# libvlc.dylib），报 "library not found for -llibvlc"，所以这里直接给绝对路径。
if(APPLE)
    set(_vlc_dylibs "")
    foreach(_name libvlc libvlccore)
        if(EXISTS "${VLC_LIB_DIR}/${_name}.dylib")
            list(APPEND _vlc_dylibs "${VLC_LIB_DIR}/${_name}.dylib")
        endif()
    endforeach()
    if(NOT _vlc_dylibs)
        message(FATAL_ERROR "在 ${VLC_LIB_DIR} 下找不到 libvlc.dylib")
    endif()
    message(STATUS "Link LibVLC (macOS): ${_vlc_dylibs}")
    target_link_libraries(LiteVlcPlayer PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Svg
        Qt6::Network
        ${_vlc_dylibs}
    )
else()
    target_link_libraries(LiteVlcPlayer PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Svg
        Qt6::Network
        libvlc
        libvlccore
    )
endif()

if(APPLE)
    # MacWindow.mm 用到 NSWindow / NSFloatingWindowLevel（AppKit 符号），显式链接避免依赖 Qt 的传递配置
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    target_link_libraries(LiteVlcPlayer PRIVATE ${COCOA_FRAMEWORK})
endif()

if(WIN32)
    target_link_libraries(LiteVlcPlayer PRIVATE user32)
    set_target_properties(LiteVlcPlayer PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

if(APPLE)
    # .app bundle：Info.plist 由模板注入版本号，图标 app.icns 进 Resources。
    # rpath 指向 Contents/Frameworks —— 打包脚本会把 libvlc / plugins 放到那里。
    set_target_properties(LiteVlcPlayer PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "LiteVlcPlayer"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.litevlc.player"
        MACOSX_BUNDLE_ICON_FILE "app.icns"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        INSTALL_RPATH "@executable_path/lib;@executable_path/../Frameworks"
        BUILD_WITH_INSTALL_RPATH TRUE
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
