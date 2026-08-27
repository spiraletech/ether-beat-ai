@echo off
setlocal
cd /d "%~dp0"

if not exist "runtime\ACE-Step-1.5\start_api_server.bat" (
  echo ACE-Step is not installed yet.
  echo Run SETUP_ACE_STEP.bat first.
  pause
  exit /b 1
)

echo ============================================================
echo ETHERBEAT // LOCAL MODEL SERVICE
 echo ACE-Step 1.5 @ http://127.0.0.1:8001
 echo ============================================================
echo.
echo Keep this window open while generating inside ETHERBEAT.exe.
echo Press Ctrl+C here when you want to stop the local model service.
echo.

pushd "runtime\ACE-Step-1.5"
call start_api_server.bat
set "ACE_EXIT=%ERRORLEVEL%"
popd
exit /b %ACE_EXIT%
