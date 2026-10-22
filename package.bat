@echo off
chcp 65001 >nul
setlocal
set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "APPVER=1.1.3"
set "QTDIR=E:\Qt\6.8.0\mingw_64"
set "PATH=%QTDIR%\bin;E:\Qt\Tools\mingw1310_64\bin;E:\Qt\Tools\CMake_64\bin;E:\Qt\Tools\Ninja;%PATH%"

echo [1/5] 编译 Release ...
cmake -B "%ROOT%\build" -S "%ROOT%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QTDIR%"
if errorlevel 1 goto :fail
cmake --build "%ROOT%\build"
if errorlevel 1 goto :fail

echo [2/5] 准备发布目录 ...
if exist "%ROOT%\dist\LiteVlcPlayer" rmdir /s /q "%ROOT%\dist\LiteVlcPlayer"
mkdir "%ROOT%\dist\LiteVlcPlayer"
if errorlevel 1 goto :fail
copy /y "%ROOT%\build\LiteVlcPlayer.exe" "%ROOT%\dist\LiteVlcPlayer\" >nul
if errorlevel 1 goto :fail

echo [3/5] 部署 Qt 运行库 ...
windeployqt --release --compiler-runtime --no-translations --no-system-d3d-compiler --no-opengl-sw --no-quick-import --dir "%ROOT%\dist\LiteVlcPlayer" "%ROOT%\dist\LiteVlcPlayer\LiteVlcPlayer.exe"
if errorlevel 1 goto :fail

echo [4/5] 复制 LibVLC 运行库与插件 ...
copy /y "%ROOT%\vlc\libvlc.dll" "%ROOT%\dist\LiteVlcPlayer\" >nul
if errorlevel 1 goto :fail
copy /y "%ROOT%\vlc\libvlccore.dll" "%ROOT%\dist\LiteVlcPlayer\" >nul
if errorlevel 1 goto :fail
xcopy "%ROOT%\vlc\plugins" "%ROOT%\dist\LiteVlcPlayer\plugins\" /e /i /y /q >nul
if errorlevel 1 goto :fail

echo [5/5] 生成 Windows 11 安装包 ...
set "ISCC=%INNO_SETUP%"
if "%ISCC%"=="" set "ISCC=E:\tools\innosetup\ISCC.exe"
if not exist "%ISCC%" set "ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=C:\Program Files\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
    echo 未找到 Inno Setup 编译器 ISCC.exe。
    echo 方式一：安装 Inno Setup 6 到默认目录；
    echo 方式二：把便携版放到 E:\tools\innosetup\ISCC.exe；
    echo 方式三：设置环境变量 INNO_SETUP 指向 ISCC.exe。
    goto :fail
)
"%ISCC%" /DMyAppVersion=%APPVER% "%ROOT%\installer.iss"
if errorlevel 1 goto :fail

echo.
echo ============================================
echo 打包完成: %ROOT%\dist\installer\LiteVlcPlayer-%APPVER%-Setup.exe
echo ============================================
exit /b 0

:fail
echo.
echo 打包失败。
exit /b 1
