@echo off
title NX Torrent Player - webOS Preview
echo ========================================================
echo  NX Torrent Player - webOS Version Local Preview
echo ========================================================
echo.
echo Starting local web server on port 8085...
start http://localhost:8085
python -m http.server 8085 --directory webos
pause
