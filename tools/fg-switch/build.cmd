@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
rc /nologo /fo build\fg-switch.res fg-switch.rc
if errorlevel 1 exit /b 1
cl /nologo /W4 /O2 /MT /EHsc /utf-8 /std:c++17 /DUNICODE /D_UNICODE fg-switch.cpp build\fg-switch.res /Fo:build\fg-switch.obj /Fe:build\EndfieldFGSwitch.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:NO "..\..\upstream\NVIDIA-nvapi-87dca62\amd64\nvapi64.lib" bcrypt.lib shell32.lib ole32.lib user32.lib gdi32.lib comctl32.lib
exit /b %errorlevel%
