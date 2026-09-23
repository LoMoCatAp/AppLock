@echo off
cd /d "%~dp0"
for %%P in (node.exe AppLock.exe) do taskkill /F /IM %%P >nul 2>&1
