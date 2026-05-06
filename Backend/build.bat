@echo off
REM ============================================================
REM  build.bat — 使用 VS2022 + vcpkg 编译 DroneBackend
REM
REM  前提：
REM    1. 已安装 Visual Studio 2022（Community 或 BuildTools）
REM    2. 已安装 vcpkg 并集成到 VS（vcpkg integrate install）
REM    3. 已通过 vcpkg 安装以下包（x64-windows）：
REM         vcpkg install boost-asio boost-beast boost-json boost-system
REM         vcpkg install yaml-cpp nlohmann-json spdlog gtest
REM ============================================================

setlocal

REM ---- 配置路径 ----
set VS_BAT="C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
if not exist %VS_BAT% (
    set VS_BAT="C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
)

REM ---- 查找 vcpkg toolchain ----
REM 优先用 VS 内置 vcpkg，其次用用户自定义路径
set VCPKG_TOOLCHAIN=
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg\scripts\buildsystems\vcpkg.cmake" (
    set VCPKG_TOOLCHAIN=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg\scripts\buildsystems\vcpkg.cmake
)
REM 独立安装的 vcpkg 路径（修改为你实际的 vcpkg 安装目录）
set VCPKG_TOOLCHAIN=C:\dev\vcpkg\scripts\buildsystems\vcpkg.cmake

if "%VCPKG_TOOLCHAIN%"=="" (
    echo [ERROR] vcpkg toolchain not found. Please set VCPKG_TOOLCHAIN manually.
    exit /b 1
)

REM ---- 创建 build 目录 ----
set BUILD_DIR=%~dp0build
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

REM ---- 调用 VS 环境 + cmake ----
call %VS_BAT% -arch=x64 2>nul

cmake -S "%~dp0" -B "%BUILD_DIR%" ^
    -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" ^
    -DCMAKE_BUILD_TYPE=Release

if errorlevel 1 (
    echo [ERROR] CMake configure failed.
    exit /b 1
)

cmake --build "%BUILD_DIR%" --config Release --target DroneBackend

if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo.
echo [OK] Build succeeded.
echo      Executable: %BUILD_DIR%\Release\DroneBackend.exe

endlocal
