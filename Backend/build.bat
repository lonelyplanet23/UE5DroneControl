@echo off
setlocal

for %%i in ("%~dp0.") do set "SCRIPT_DIR=%%~fi"
set "BUILD_DIR=%SCRIPT_DIR%\build"
set "INSTALL_DIR=%SCRIPT_DIR%\vcpkg_installed\x64-windows"
set "RUN_TESTS=0"

for %%i in ("%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe") do set "VSWHERE=%%~fsi"

if /I "%~1"=="--test" set "RUN_TESTS=1"

if not exist "%VSWHERE%" (
    echo [ERROR] vswhere.exe not found: %VSWHERE%
    exit /b 1
)

for /f "usebackq delims=" %%i in (`""%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath"`) do (
    set "VS_INSTALL=%%i"
)

if not defined VS_INSTALL (
    echo [ERROR] Visual Studio with C++ toolchain not found.
    exit /b 1
)

set "VS_DEV_CMD=%VS_INSTALL%\Common7\Tools\VsDevCmd.bat"
if not exist "%VS_DEV_CMD%" (
    echo [ERROR] VsDevCmd.bat not found: %VS_DEV_CMD%
    exit /b 1
)

set "VCPKG_ROOT=%VS_INSTALL%\VC\vcpkg"
if not exist "%VCPKG_ROOT%\vcpkg.exe" (
    echo [ERROR] Visual Studio bundled vcpkg not found: %VCPKG_ROOT%\vcpkg.exe
    exit /b 1
)

set "VCPKG_TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"

call "%VS_DEV_CMD%" -arch=x64 >nul
if errorlevel 1 (
    echo [ERROR] Failed to initialize Visual Studio build environment.
    exit /b 1
)

echo [1/4] Installing C++ dependencies via vcpkg manifest...
"%VCPKG_ROOT%\vcpkg.exe" install --x-manifest-root="%SCRIPT_DIR%" --triplet x64-windows
if errorlevel 1 (
    echo [ERROR] vcpkg install failed.
    exit /b 1
)

echo [2/4] Configuring CMake...
if exist "%BUILD_DIR%\CMakeCache.txt" (
    cmake -S "%SCRIPT_DIR%" -B "%BUILD_DIR%" -G Ninja ^
        -DCMAKE_PREFIX_PATH="%INSTALL_DIR%"
) else (
    cmake -S "%SCRIPT_DIR%" -B "%BUILD_DIR%" -G Ninja ^
        -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" ^
        -DVCPKG_TARGET_TRIPLET=x64-windows ^
        -DCMAKE_PREFIX_PATH="%INSTALL_DIR%"
)
if errorlevel 1 (
    echo [ERROR] CMake configure failed.
    exit /b 1
)

echo [3/4] Building backend...
cmake --build "%BUILD_DIR%" --config Release
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

if "%RUN_TESTS%"=="1" (
    echo [4/4] Running unit tests...
    set "PATH=%INSTALL_DIR%\bin;%PATH%"
    ctest --test-dir "%BUILD_DIR%" -C Release --output-on-failure
    if errorlevel 1 (
        echo [ERROR] Unit tests failed.
        exit /b 1
    )
)

echo.
echo [OK] Build succeeded.
echo      Executable: "%BUILD_DIR%\DroneBackend.exe"
if "%RUN_TESTS%"=="1" echo      Unit tests: passed

endlocal
