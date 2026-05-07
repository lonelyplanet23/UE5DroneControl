@echo off
setlocal
REM 1. 自动定位 VS 安装路径 (适配 VS 2026/2022)
set "VS_WHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VS_WHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
  set "VS_PATH=%%i"
)

REM --- 解决冲突后的部分：保留动态路径，确保 VS_BAT 被定义 ---
set "VS_BAT=%VS_PATH%\Common7\Tools\VsDevCmd.bat"

REM 如果你的 vcpkg 是随 VS 安装的，保留这一行：
set "VCPKG_TOOLCHAIN=%VS_PATH%\VC\vcpkg\scripts\buildsystems\vcpkg.cmake"

REM 如果你使用了独立安装的 vcpkg (比如在 C:\dev\vcpkg)，请取消下面这行的注释并修改路径：
REM set "VCPKG_TOOLCHAIN=C:\dev\vcpkg\scripts\buildsystems\vcpkg.cmake"
REM -------------------------------------------------------

echo [INFO] 使用 VS 路径: "%VS_PATH%"

REM 2. 创建 build 目录
set "BUILD_DIR=%~dp0build"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

REM 3. 环境初始化与编译
call "%VS_BAT%" -arch=x64
cmake -S "%~dp0." -B "%BUILD_DIR%" -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" -DCMAKE_BUILD_TYPE=Release
cmake --build "%BUILD_DIR%" --config Release --target DroneBackend

if %errorlevel% equ 0 (
    echo [OK] 编译成功！可执行文件在 %BUILD_DIR%\Release\DroneBackend.exe
) else (
    echo [ERROR] 编译失败，请检查上方输出。
)
pause