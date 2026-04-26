@echo off
setlocal

for %%i in ("%~dp0.") do set "SCRIPT_DIR=%%~fi"
set "PYTHON_SCRIPT=%SCRIPT_DIR%\tests\run_mock_ue_integration.py"
set "VCPKG_BIN=%SCRIPT_DIR%\vcpkg_installed\x64-windows\bin"

if not exist "%PYTHON_SCRIPT%" (
    echo [ERROR] Integration test script not found: %PYTHON_SCRIPT%
    exit /b 1
)

if exist "%VCPKG_BIN%" (
    set "PATH=%VCPKG_BIN%;%PATH%"
)

python "%PYTHON_SCRIPT%"
set "EXIT_CODE=%ERRORLEVEL%"

if not "%EXIT_CODE%"=="0" (
    echo [ERROR] Integration test failed.
    exit /b %EXIT_CODE%
)

echo [OK] Integration test passed.
endlocal
