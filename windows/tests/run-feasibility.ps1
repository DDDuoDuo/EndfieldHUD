[CmdletBinding()]
param([string]$Configuration = 'Release', [ValidateRange(1,10000)][int]$ReopenCycles = 100)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$build = Join-Path $repository "windows\build\x64\$Configuration"
$evidence = Join-Path $repository 'windows\evidence\local'
$executable = Join-Path $build 'EndfieldHUDWindows.exe'
if (-not (Test-Path -LiteralPath $executable)) { throw 'Build the native target with windows/packaging/build.ps1 first.' }
New-Item -ItemType Directory -Path $evidence -Force | Out-Null
& (Join-Path $build 'scene_tests.exe') (Join-Path $repository 'Resources\WatchSource\Scene')
if ($LASTEXITCODE -ne 0) { throw 'Source scene contracts failed.' }
& (Join-Path $build 'editor_tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Projected editor contracts failed.' }
& (Join-Path $build 'desktop_shell_tests.exe') (Join-Path $repository 'Resources\WatchSource\Scene')
if ($LASTEXITCODE -ne 0) { throw 'Current desktop presentation contracts failed.' }
& (Join-Path $build 'monitor_policy_tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Source monitor selection contracts failed.' }
& (Join-Path $build 'presentation_adapter_tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Source-to-Windows presentation contracts failed.' }
& (Join-Path $build 'frozen_backdrop_tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Synthetic frozen backdrop contracts failed.' }
& (Join-Path $build 'backdrop_preparation_tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Cancelled and delayed backdrop preparation contracts failed.' }
& (Join-Path $build 'platform_probe.exe') (Join-Path $evidence 'platform-capabilities.txt')
if ($LASTEXITCODE -ne 0) { throw 'Native platform capability probe failed.' }
$graphicsOutput = Join-Path $evidence 'graphics-lifecycle.json'
# Start-Process joins ArgumentList into a Windows command line; quote paths.
$probe = Start-Process -FilePath $executable -ArgumentList '--graphics-probe', '--graphics-cycles', $ReopenCycles.ToString([Globalization.CultureInfo]::InvariantCulture), '--output', ('"' + $graphicsOutput + '"') -WorkingDirectory $repository -WindowStyle Hidden -PassThru
$probe.WaitForExit()
if ($probe.ExitCode -ne 0) { throw "Native graphics probe failed with exit $($probe.ExitCode); inspect its local JSON evidence." }
$graphics = Get-Content -LiteralPath $graphicsOutput -Raw | ConvertFrom-Json
if ($graphics.measured_reopen_cycles -ne $ReopenCycles) { throw 'Graphics probe did not run the requested measured cycle count.' }
$closed = Get-Content -LiteralPath (Join-Path $evidence 'graphics-lifecycle.closed.json') -Raw | ConvertFrom-Json
if ($closed.elapsed_seconds -lt 60 -or $closed.hud_frames_submitted -ne 0) { throw 'Closed-state frame submission contract failed.' }
$os = Get-CimInstance Win32_OperatingSystem
$computer = Get-CimInstance Win32_ComputerSystem
$hardware = [ordered]@{
  schema = 1
  os_version = $os.Version
  os_build = $os.BuildNumber
  os_architecture = $os.OSArchitecture
  cpu = @((Get-CimInstance Win32_Processor).Name)
  ram_bytes = $computer.TotalPhysicalMemory
  gpus = @((Get-CimInstance Win32_VideoController | Select-Object Name, DriverVersion, CurrentHorizontalResolution, CurrentVerticalResolution, CurrentRefreshRate))
  monitors = $closed.monitors
  executable_sha256 = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash.ToLowerInvariant()
  source_mode = 'restarted desktop adaptation; raw Watch preview rejected'
  canonical_resource_baseline = '4036174a3facf935260f4d0a9c63bfff33b98c37'
  settings = 'Synthetic 1280x720 hidden HWND; generated SDR backdrop pixels only; desktop profile/navigation/native labels; ambient off; no real data; source fixture pointer; source animation endpoints'
  measured_reopen_cycles = $ReopenCycles
  limits = 'No live desktop capture, visible frame pacing, recording, GPU/wakeup trace, real CJK IME or Mac font comparison; all unverified.'
}
$hardware | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $evidence 'hardware.json') -Encoding UTF8
Write-Output "Native diagnostics saved under $evidence"
Write-Output "Closed seconds: $($closed.elapsed_seconds); submitted HUD frames: $($closed.hud_frames_submitted); CPU seconds: $($closed.process_cpu_seconds)"
Write-Output "Warmed graphics reopen cycles: $($graphics.measured_reopen_cycles). Full migration/release acceptance remains unverified."
Write-Output "Drained memory samples: $($graphics.drained_sample_points.Count); source cache counts: $($graphics.source_cache_count_stability); owned backdrop close checks: $($graphics.owned_backdrop_release_checks). Process counter changes are informational."
