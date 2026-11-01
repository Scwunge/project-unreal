@echo off
rem Asks the server started by Server.bat to save the world and quit.
set SERVERDIR=%LOCALAPPDATA%\McClient\Saved\Server
if not exist "%SERVERDIR%" (
  echo No server has run on this PC yet.
  pause
  exit /b 1
)
type nul > "%SERVERDIR%\stop"
echo Asked the server to stop. It saves the world and quits within a few seconds.
timeout /t 6 /nobreak >nul
type "%SERVERDIR%\status.txt"
pause
