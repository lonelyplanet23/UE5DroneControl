@echo off
setlocal

set VS_BAT="C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
if not exist %VS_BAT% (
    set VS_BAT="C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
)

set VCPKG_TOOLCHAIN=C:\dev\vcpkg\scripts\buildsystems\vcpkg.cmake

set BUILD_DIR=%~dp0build
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

call %VS_BAT% -arch=x64 2>nul

cmake -S "%~dp0." -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" -DCMAKE_BUILD_TYPE=Release

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
