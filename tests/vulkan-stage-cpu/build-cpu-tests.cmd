@echo off
rem Builds one generated CPU test. Usage: build-cpu-tests.cmd <output-path-without-extension>
rem Honors VCVARS, otherwise uses the latest installed Visual C++ tools.
setlocal
set "VSLANG=1033"
if defined VCVARS goto found
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" ( echo vswhere.exe not found; set VCVARS to vcvars64.bat & exit /b 1 )
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%I\VC\Auxiliary\Build\vcvars64.bat"
:found
if not exist "%VCVARS%" ( echo no vcvars64.bat at "%VCVARS%" & exit /b 1 )
call "%VCVARS%" >nul
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /EHsc /std:c++20 /O2 /W4 /WX /wd4101 /wd4244 "%~1.cpp" /Fo"%~1.obj" /Fd"%~1.pdb" /Fe"%~1.exe" /link /INCREMENTAL:NO
exit /b %errorlevel%
