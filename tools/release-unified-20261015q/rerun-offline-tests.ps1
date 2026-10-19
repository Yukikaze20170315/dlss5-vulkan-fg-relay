param([Parameter(Mandatory=$true)][string]$LogDir, [string[]]$Only)
if ($Only) { $Only = @($Only | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ }) }
# Re-runs the offline fixtures of unified-20261015q against the production source
# tree and the shipped Bridge DLL. Nothing here touches the game folder, the real
# NVIDIA driver profile or the ReShade registration: every fixture uses its own
# temporary folder under builds\unified-20261011\.
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$tools = Join-Path $root 'tools\unified-20261011'
$package = Join-Path $root 'dist\unified-20261015q-candidate'
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
# deploy.ps1 refuses to run while Endfield.exe exists, so test-deploy.ps1 needs the
# game closed; the other fixtures do not touch the game and may run at any time.
$gameRunning = [bool](Get-Process -Name Endfield -ErrorAction SilentlyContinue)

$verify = Join-Path $root 'builds\unified-20261011\verify-20261019'
New-Item -ItemType Directory -Force -Path (Join-Path $verify 'bridge') | Out-Null
Copy-Item -LiteralPath (Join-Path $package 'dlss5-bridge.addon64') -Destination (Join-Path $verify 'bridge\dlss5-bridge.addon64') -Force

$fenceOut = Join-Path $verify 'fence'
New-Item -ItemType Directory -Force -Path $fenceOut | Out-Null
$fenceCmd = Join-Path $fenceOut 'build-fence-test.cmd'
# Paths relative to %~dp0 (builds\unified-20261011\verify-20261019\fence\): cmd.exe
# reads a batch file in the OEM code page, so the project's non-ASCII folder name
# must not appear literally in it.
@"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul || exit /b 1
cd /d "%~dp0"
cl /nologo /W4 /WX /std:c++17 /EHsc /utf-8 /O2 /MT /I"%~dp0..\..\..\..\src\unified-20261011\bridge\src" "%~dp0..\..\..\..\tools\unified-20261011\test-fence-deadline-wait.cpp" /Fe:test-fence-deadline-wait.exe /link d3d12.lib dxgi.lib || exit /b 1
test-fence-deadline-wait.exe
"@ | Set-Content -LiteralPath $fenceCmd -Encoding ascii

$runs = @(
  @{ name = 'state-and-loader';      file = 'cmd.exe'; args = @('/c', "`"$tools\test.cmd`"") },
  @{ name = 'nr-selection';          file = 'cmd.exe'; args = @('/c', "`"$tools\build-nr-selection-test.cmd`"") },
  @{ name = 'event-log-and-logdir';  file = 'cmd.exe'; args = @('/c', "`"$tools\build-unified1q.cmd`" tests verify-20261019") },
  @{ name = 'mfg-policy';            file = 'cmd.exe'; args = @('/c', "`"$tools\build-unified1q.cmd`" mfg verify-20261019") },
  @{ name = 'drs-and-maintenance';   file = 'cmd.exe'; args = @('/c', "`"$tools\test-drs.cmd`"") },
  @{ name = 'fence-deadline-wait';   file = 'cmd.exe'; args = @('/c', "`"$fenceCmd`"") },
  @{ name = 'deploy-pwsh7';          file = 'pwsh.exe'; args = @('-NoProfile', '-File', "`"$tools\test-deploy.ps1`"") },
  @{ name = 'deploy-ps51';           file = 'powershell.exe'; args = @('-NoProfile', '-File', "`"$tools\test-deploy.ps1`"") },
  @{ name = 'clean-recovery-pwsh7';  file = 'pwsh.exe'; args = @('-NoProfile', '-File', "`"$tools\test-clean-recovery.ps1`"") },
  @{ name = 'clean-recovery-ps51';   file = 'powershell.exe'; args = @('-NoProfile', '-File', "`"$tools\test-clean-recovery.ps1`"") }
)
$summaryPath = Join-Path $LogDir 'summary.json'
$summary = @()
# Windows PowerShell 5.1 emits a JSON array as one object; ForEach-Object unrolls it.
if (Test-Path -LiteralPath $summaryPath) { $summary = @(Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json | ForEach-Object { $_ }) }
foreach ($run in $runs) {
  if ($Only -and $run.name -notin $Only) { continue }
  if ($gameRunning -and $run.name -like 'deploy-*') { Write-Host "skipped $($run.name): Endfield is running"; continue }
  if (-not (Get-Command $run.file -ErrorAction SilentlyContinue)) { Write-Host "skipped $($run.name): $($run.file) not installed"; continue }
  $summary = @($summary | Where-Object { $_.name -ne $run.name })
  $log = Join-Path $LogDir ($run.name + '.log')
  $started = Get-Date
  # Wait for the process itself only: -Wait would also wait for helpers such as
  # vctip.exe that the compiler leaves running.
  $p = Start-Process -FilePath $run.file -ArgumentList $run.args -NoNewWindow -PassThru `
        -RedirectStandardOutput $log -RedirectStandardError ($log + '.stderr')
  # Touching Handle keeps the process handle open, otherwise ExitCode stays empty.
  $null = $p.Handle
  $p.WaitForExit()
  $err = Get-Content -LiteralPath ($log + '.stderr') -Raw -ErrorAction SilentlyContinue
  if ($err) { Add-Content -LiteralPath $log -Value "`r`n--- stderr ---`r`n$err" }
  Remove-Item -LiteralPath ($log + '.stderr') -ErrorAction SilentlyContinue
  $summary += [pscustomobject]@{ name = $run.name; exit_code = $p.ExitCode; seconds = [math]::Round(((Get-Date) - $started).TotalSeconds, 1); log = ($run.name + '.log') }
}
ConvertTo-Json -InputObject @($summary) | Set-Content -LiteralPath $summaryPath -Encoding utf8
$summary | Format-Table -AutoSize | Out-String -Width 200
