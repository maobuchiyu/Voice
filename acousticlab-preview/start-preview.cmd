@echo off
title AcousticLab Preview
cd /d "%~dp0"
echo AcousticLab preview: http://127.0.0.1:8000/
echo Keep this window open. Press Ctrl+C to stop the server.
echo.
py -3 -m http.server 8000 --bind 127.0.0.1
echo.
echo Preview server stopped or failed to start.
pause
