@echo off
chcp 65001 >nul
set "PATH=E:\Qt\6.8.0\mingw_64\bin;E:\Qt\Tools\mingw1310_64\bin;E:\Qt\Tools\CMake_64\bin;E:\Qt\Tools\Ninja;%PATH%"

echo [1/2] Configuring CMake...
cmake -B "%~dp0build" -S "%~dp0" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="E:/Qt/6.8.0/mingw_64"
if errorlevel 1 (
    echo CMake configuration failed!
    pause
    exit /b 1
)

echo [2/2] Building LiteVlcPlayer.exe...
cmake --build "%~dp0build"
if errorlevel 1 (
    echo Build failed!
    pause
    exit /b 1
)

echo Build succeeded: %~dp0build\LiteVlcPlayer.exe
