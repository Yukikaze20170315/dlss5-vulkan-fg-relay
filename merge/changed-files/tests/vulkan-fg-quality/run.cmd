@echo off
rem Offline tests for the 2026-10-01 update. Usage: run.cmd SUITE NEW_OUTPUT_DIRECTORY [ARGUMENT]
rem Suites: registry hooks guides colour recovery compatibility carrier carrier-range minhook-range guard focus gpu racehelper
rem Every run builds into a NEW directory and refuses an existing one. No game is started.
setlocal
if "%~2"=="" goto usage
call "%~dp0..\vulkan-fg-input\vcvars.cmd" || exit /b 2
for %%I in ("%~dp0..\..\src") do set "SRC=%%~fI"
set "HERE=%~dp0"
set "OUT=%~f2"
if exist "%OUT%" ( echo run.cmd: "%OUT%" already exists & exit /b 2 )
mkdir "%OUT%" || exit /b 2
pushd "%OUT%"
set "CL_BRIDGE=cl /nologo /W4 /WX /O2 /MT /EHsc /std:c++17 /utf-8 /I"%SRC%" /I"%SRC%\reshade" /I"%SRC%\minhook\include" /I"%SRC%\minhook\src""
set "LIBS=user32.lib advapi32.lib bcrypt.lib"
goto suite_%~1 2>nul || ( echo run.cmd: unknown suite "%~1" & popd & exit /b 2 )

:suite_registry
cl /nologo /W4 /WX /O2 /MT /EHsc /std:c++17 /I"%SRC%" "%HERE%test-registry.cpp" /Fe:test.exe > build.log 2>&1 || goto failed
goto run

:suite_guides
cl /nologo /W4 /O2 /MT /EHsc /std:c++17 /utf-8 /I"%SRC%" "%HERE%test-guides-contract.cpp" /Fe:test.exe /link user32.lib > build.log 2>&1 || goto failed
goto run

:suite_focus
if "%~3"=="" ( echo run.cmd focus needs the path of the game's sl.dlss_g.dll & popd & exit /b 2 )
cl /nologo /W4 /WX /wd4505 /utf-8 /O2 /MT /EHsc /std:c++17 /I"%SRC%" "%HERE%test-focus.cpp" /Fe:test.exe /link user32.lib /OPT:NOICF > build.log 2>&1 || goto failed
test.exe "%~3" > run.txt 2>&1
goto result

:suite_hooks
call :bridge test-hooks.cpp || goto failed
( echo [Adapter]& echo Enabled=1& echo Source=fg-input& echo Pipeline=2 ) > vk-present-adapter.ini
goto run

:suite_colour
call :bridge test-color.cpp || goto failed
goto run

:suite_recovery
call :bridge test-recovery.cpp || goto failed
goto run

:suite_guard
call :bridge test-guard.cpp || goto failed
( echo [Adapter]& echo Enabled=1& echo Source=fg-input& echo Pipeline=2 ) > vk-present-adapter.ini
goto run

:suite_compatibility
cl /nologo /W4 /WX /O2 /MT /EHsc /std:c++17 /utf-8 /LD "%HERE%dummy-generic.cpp" /Fe:renodx-dlss5.addon64 > build.log 2>&1 || goto failed
call :bridge test-compatibility.cpp || goto failed
( echo [Adapter]& echo Enabled=1& echo Source=fg-input& echo Follow=1& echo Pipeline=0 ) > vk-present-adapter.ini
goto run

:suite_carrier
rc /nologo /fo sr-fixture.res "%HERE%sr-fixture.rc" > build.log 2>&1 || goto failed
rc /nologo /d RR_FIXTURE /fo rr-fixture.res "%HERE%sr-fixture.rc" >> build.log 2>&1 || goto failed
cl /nologo /W4 /WX /Od /MT /LD "%HERE%sr-fixture.cpp" sr-fixture.res /Fe:sr-a.dll >> build.log 2>&1 || goto failed
copy /y sr-a.dll sr-b.dll >> build.log || goto failed
cl /nologo /W4 /WX /Od /MT /LD "%HERE%sr-fixture.cpp" rr-fixture.res /Fe:rr-fixture.dll >> build.log 2>&1 || goto failed
call :bridge test-carrier.cpp || goto failed
goto run

:suite_minhook-range
%CL_BRIDGE% "%HERE%test-minhook-range.cpp" "%SRC%\minhook.c" /Fe:test.exe /link %LIBS% > build.log 2>&1 || goto failed
goto run

:suite_carrier-range
rc /nologo /fo sr-fixture.res "%HERE%sr-fixture.rc" > build.log 2>&1 || goto failed
ml64 /nologo /c "%HERE%sr-low-fixture.asm" >> build.log 2>&1 || goto failed
cl /nologo /W4 /WX /Od /MT /LD /DSR_LOW_FIXTURE "%HERE%sr-fixture.cpp" sr-fixture.res sr-low-fixture.obj /Fe:sr-low.dll /link /EXPORT:NVSDK_NGX_D3D12_EvaluateFeature /BASE:0x128AD0000 /DYNAMICBASE:NO /FIXED >> build.log 2>&1 || goto failed
call :bridge test-carrier.cpp || goto failed
test.exe --crowded > run.txt 2>&1
goto result

:suite_racehelper
rem Builds only. carrier-race-helper.addon64 is a test-only ReShade add-on; see its source header.
cl /nologo /W4 /WX /O2 /MT /EHsc /std:c++17 /I"%SRC%\reshade" /LD "%HERE%carrier-race-helper.cpp" /Fe:carrier-race-helper.addon64 > build.log 2>&1 || goto failed
echo built carrier-race-helper.addon64 (test only; put it next to the game executable and remove it after the test)
popd
exit /b 0

:suite_gpu
if not defined VULKAN_SDK ( echo run.cmd gpu needs VULKAN_SDK & popd & exit /b 2 )
ml64 /nologo /c "%SRC%\present-nr-witness.asm" >> build.log 2>&1 || goto failed
%CL_BRIDGE% /I"%VULKAN_SDK%\Include" "%HERE%test-gpu.cpp" "%SRC%\minhook.c" present-nr-witness.obj /Fe:test.exe /link /LIBPATH:"%VULKAN_SDK%\Lib" vulkan-1.lib d3d12.lib %LIBS% >> build.log 2>&1 || goto failed
goto run

:bridge
ml64 /nologo /c "%SRC%\present-nr-witness.asm" >> build.log 2>&1 || exit /b 1
%CL_BRIDGE% "%HERE%%~1" "%SRC%\minhook.c" present-nr-witness.obj /Fe:test.exe /link %LIBS% >> build.log 2>&1 || exit /b 1
exit /b 0

:run
test.exe %3 > run.txt 2>&1
:result
set "RESULT=%errorlevel%"
type run.txt
popd
exit /b %RESULT%

:failed
type build.log
popd
exit /b 2

:usage
echo Usage: run.cmd SUITE NEW_OUTPUT_DIRECTORY [ARGUMENT]
exit /b 2
