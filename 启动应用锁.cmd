@echo off
cd /d "%~dp0"
start "" powershell.exe -NoProfile -WindowStyle Hidden -Command "Start-Process -FilePath 'C:\Program Files\nodejs\node.exe' -ArgumentList '%~dp0server.js' -WorkingDirectory '%~dp0' -WindowStyle Hidden"
exit /b 0
