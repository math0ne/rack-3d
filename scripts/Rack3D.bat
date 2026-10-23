@echo off
rem Launches Rack 3D in normal mode (not development mode), so the VCV Library works: log in and sync plugins from
rem the Library menu. It uses its own user folder, userdata\ in the checkout, so stock VCV Rack's settings, plugins
rem and patches are never touched. Needs MSYS2's MinGW runtime DLLs (the build toolchain), from C:\msys64\mingw64\bin.
rem Pass a patch file to open it:  scripts\Rack3D.bat examples\chord-drone.vcv
setlocal
set "ROOT=%~dp0..\"
set "USERDIR=%ROOT%userdata"
set "PATH=C:\msys64\mingw64\bin;%PATH%"

if not exist "%USERDIR%\settings.json" call :firstRun

start "" "%ROOT%Rack.exe" -u "%USERDIR%" %*
exit /b

:firstRun
rem The built Fundamental plugin, so its modules are there before any Library sync
set "FUND=%USERDIR%\plugins-win-x64\Fundamental"
mkdir "%FUND%" 2>nul
copy /y "%ROOT%plugins\Fundamental\plugin.dll" "%FUND%\" >nul
copy /y "%ROOT%plugins\Fundamental\plugin.json" "%FUND%\" >nul
xcopy /e /i /q /y "%ROOT%plugins\Fundamental\res" "%FUND%\res" >nul
if exist "%ROOT%plugins\Fundamental\presets" xcopy /e /i /q /y "%ROOT%plugins\Fundamental\presets" "%FUND%\presets" >nul
rem Racks shown and enforced from the start
> "%USERDIR%\settings.json" echo {"rack3d": {"placementsEnabled": true}}
exit /b
