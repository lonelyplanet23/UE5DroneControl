@echo off
setlocal

for %%i in ("%~dp0.") do set "SCRIPT_DIR=%%~fi"
set "EXE=%SCRIPT_DIR%\build\DroneBackend.exe"
set "DEFAULT_CONFIG=%SCRIPT_DIR%\config.yaml"
set "VCPKG_BIN=%SCRIPT_DIR%\vcpkg_installed\x64-windows\bin"

if not exist "%EXE%" (
    echo [ERROR] Backend executable not found: %EXE%
    echo         Run build.bat first.
    exit /b 1
)

if not exist "%VCPKG_BIN%" (
    echo [ERROR] Runtime dependency directory not found: %VCPKG_BIN%
    echo         Run build.bat first.
    exit /b 1
)

set "CONFIG_PATH=%DEFAULT_CONFIG%"
if not "%~1"=="" set "CONFIG_PATH=%~1"

if not exist "%CONFIG_PATH%" (
    echo [ERROR] Config file not found: %CONFIG_PATH%
    exit /b 1
)

set "PATH=%VCPKG_BIN%;%PATH%"
echo Starting backend with config: "%CONFIG_PATH%"
"%EXE%" "%CONFIG_PATH%"

endlocal
