@echo off
cd /d "%~dp0build-pc"
if not exist "NX-torrent-player.exe" (
    echo [ERROR] NX-torrent-player.exe not found! Please run build-pc.bat first.
    pause
    exit /b 1
)
start "" "NX-torrent-player.exe"
