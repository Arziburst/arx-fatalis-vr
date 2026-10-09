@echo off
rem Starts Arx Fatalis VR. VR is used automatically when SteamVR is running with a headset.
cd /d "%~dp0"
arx.exe %*
if errorlevel 1 (
	echo.
	echo The game ended with an error ^(code %errorlevel%^). Send the support bundle: double-click "Collect support bundle.cmd".
	pause
)
