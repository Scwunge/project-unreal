@echo off
rem Runs the game as a server: no window, a world friends join by address (port 25565 unless server.properties says
rem otherwise). Settings: %LOCALAPPDATA%\McClient\Saved\Server\server.properties (made on the first start).
rem Stop it with Stop-Server.bat, which saves the world first.
set SERVERDIR=%LOCALAPPDATA%\McClient\Saved\Server
if exist "%SERVERDIR%\stop" del "%SERVERDIR%\stop"
start "" "%~dp0Windows\McClient.exe" /Engine/Maps/Entry?listen -McServer -nullrhi -nosound -unattended
echo The server is starting. In a few seconds this shows its status (the world, the port, who is on):
timeout /t 15 /nobreak >nul
type "%SERVERDIR%\status.txt"
echo.
echo Settings:  %SERVERDIR%\server.properties  (edit, then stop and start the server again)
echo Status:    %SERVERDIR%\status.txt          (updated every 2 seconds)
echo To stop it, run Stop-Server.bat.
pause
