@echo off
rem Kill switch for VR: starts the game in a normal window, no headset needed.
rem If a problem also happens here, say so in the report - it is not a VR problem then.
cd /d "%~dp0"
arx.exe --no-vr --windowed %*
if errorlevel 1 (
	echo.
	echo The game ended with an error ^(code %errorlevel%^). Send the support bundle: double-click "Collect support bundle.cmd".
	pause
)
