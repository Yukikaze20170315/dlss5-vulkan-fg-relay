@echo off
rem Stage plan test for Generic: all 125 legal plans, nested stage scopes, the
rem bridge carrier's explicit plan, and a concurrent configuration writer,
rem compiled against Generic's own stage_plan.hpp. CPU only.
rem Usage: run.cmd <src/addons/dlss5> <new-output-directory>
setlocal
if "%~2"=="" ( echo usage: run.cmd ^<src/addons/dlss5^> ^<new-output-directory^> & exit /b 2 )
if not exist "%~f1\stage_plan.hpp" ( echo run.cmd: no stage_plan.hpp in "%~f1" & exit /b 2 )
set "OUT=%~f2"
if exist "%OUT%" ( echo run.cmd: "%OUT%" already exists & exit /b 2 )
mkdir "%OUT%" || exit /b 2
if defined VCVARS goto found
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" ( echo vswhere.exe not found; set VCVARS to vcvars64.bat & exit /b 2 )
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%I\VC\Auxiliary\Build\vcvars64.bat"
:found
call "%VCVARS%" >nul 2>&1 || exit /b 2
pushd "%OUT%"
cl /nologo /utf-8 /EHsc /std:c++20 /O2 /W4 /WX /I"%~f1" "%~dp0stage-plan-same-frame.cpp" /Fe:stage-plan-same-frame.exe > build.log 2>&1
if errorlevel 1 ( type build.log & popd & exit /b 2 )
stage-plan-same-frame.exe > run.txt 2>&1
set "RESULT=%ERRORLEVEL%"
type run.txt
popd
exit /b %RESULT%
