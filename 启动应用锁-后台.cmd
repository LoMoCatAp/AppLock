@echo off
cd /d "%~dp0"
set APPLOCK_NO_BROWSER=1
start "" powershell.exe -NoProfile -WindowStyle Hidden -Command "Start-Process -FilePath 'C:\Program Files\nodejs\node.exe' -ArgumentList '%~dp0server.js' -WorkingDirectory '%~dp0' -WindowStyle Hidden"
exit /b 0
