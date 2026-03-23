@echo off
chcp 65001 >nul

:: --- Paths ---
set SCRIPT_DIR=%~dp0
set VENV_DIR=%SCRIPT_DIR%drone_env

echo ============================================================
echo [1/2] Activate virtual environment...
if not exist "%VENV_DIR%\\Scripts\\activate.bat" (
  echo [ERROR] Virtual env not found: %VENV_DIR%
  echo Please create drone_env first.
  pause
  exit /b 1
)
call "%VENV_DIR%\\Scripts\\activate.bat"

echo [2/2] Start bridge...
echo ============================================================
cd /d "%SCRIPT_DIR%"
python drone_data_bridge.py

pause
