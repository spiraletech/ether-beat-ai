@echo off
setlocal
cd /d "%~dp0"

echo ============================================================
echo ETHERBEAT // ACE-Step 1.5 local model setup
echo ============================================================
echo.
echo This installs the local generation runtime into:
echo   %~dp0runtime\ACE-Step-1.5
echo.
echo Core model files are large ^(~10 GB^) and download from ACE-Step
 echo / Hugging Face on first setup or first launch.
echo.

where git >nul 2>nul
if errorlevel 1 (
  echo ERROR: Git is required. Install Git for Windows, then run this again.
  pause
  exit /b 1
)

where uv >nul 2>nul
if errorlevel 1 (
  echo Installing uv...
  powershell -ExecutionPolicy ByPass -Command "irm https://astral.sh/uv/install.ps1 | iex"
  if errorlevel 1 (
    echo ERROR: uv installation failed.
    pause
    exit /b 1
  )
  set "PATH=%USERPROFILE%\.local\bin;%PATH%"
)

if not exist "runtime" mkdir "runtime"
if not exist "runtime\ACE-Step-1.5\.git" (
  echo Cloning official ACE-Step 1.5...
  git clone https://github.com/ace-step/ACE-Step-1.5.git "runtime\ACE-Step-1.5"
  if errorlevel 1 (
    echo ERROR: ACE-Step clone failed.
    pause
    exit /b 1
  )
) else (
  echo ACE-Step repo already exists. Leaving your local checkout intact.
)

pushd "runtime\ACE-Step-1.5"
echo Installing Python dependencies...
uv sync
if errorlevel 1 (
  popd
  echo ERROR: ACE-Step dependency setup failed.
  pause
  exit /b 1
)
popd

echo.
echo ============================================================
echo SETUP COMPLETE
echo ============================================================
echo Next:
echo   1. Run START_ACE_STEP.bat
 echo   2. Keep its console window open
 echo   3. Launch ETHERBEAT.exe and press GENERATE
 echo.
echo The first model start may download additional checkpoints.
pause
