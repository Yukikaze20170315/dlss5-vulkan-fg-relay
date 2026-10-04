@echo off
rem Stage protocol v1, bridge side: plan discovery, validation, carrier scope and
rem configuration, compiled from the production code without loading the GPU add-on.
rem Usage: run.cmd <new-output-directory>   (an existing directory is refused)
rem consumer.cpp stands in for Generic: a DLL that exports the protocol with a
rem plan and version the test sets at run time.
setlocal
if "%~1"=="" ( echo usage: run.cmd ^<new-output-directory^> & exit /b 2 )
call "%~dp0..\vulkan-fg-input\vcvars.cmd" || exit /b 2
for %%I in ("%~dp0..\..\src") do set "SRC=%%~fI"
set "OUT=%~f1"
if exist "%OUT%" ( echo run.cmd: "%OUT%" already exists & exit /b 2 )
mkdir "%OUT%" || exit /b 2
rem Copy the production carrier scope and configuration verbatim (delimited by
rem the declarations that open and follow them), as check.cpp includes them.
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$s = [IO.File]::ReadAllText('%SRC%\present-adapter.inc');" ^
  "$a = $s.IndexOf('static thread_local bool g_present_nr_scope;'); $b = $s.IndexOf('extern \"C\" void PresentAdapterNrEnter()');" ^
  "$c = $s.IndexOf('static bool PresentAdapterConfiguration()'); $d = $s.IndexOf('static bool PresentAdapterConsumer()');" ^
  "if ($a -lt 0 -or $b -le $a -or $c -lt 0 -or $d -le $c) { exit 3 }" ^
  "[IO.File]::WriteAllText('%OUT%\scope-under-test.inc', $s.Substring($a, $b - $a));" ^
  "[IO.File]::WriteAllText('%OUT%\configuration-under-test.inc', $s.Substring($c, $d - $c))" > "%OUT%\build.log" 2>&1
if errorlevel 1 ( echo run.cmd: production delimiters not found in present-adapter.inc & type "%OUT%\build.log" & exit /b 2 )
pushd "%OUT%"
cl /nologo /W4 /WX /O2 /MT /EHsc /std:c++17 /LD "%~dp0consumer.cpp" /Fe:renodx-dlss5.addon64 >> build.log 2>&1 || goto failed
cl /nologo /W4 /WX /wd4505 /O2 /MT /EHsc /std:c++17 /I"%OUT%" /I"%SRC%" "%~dp0check.cpp" /Fe:check.exe >> build.log 2>&1 || goto failed
set "RESULT=0"
for %%P in (2 3) do for %%F in (0 1) do (
    check.exe %%P %%F >> run.txt 2>&1
    if errorlevel 1 set "RESULT=1"
)
type run.txt
popd
exit /b %RESULT%

:failed
type build.log
popd
exit /b 2
