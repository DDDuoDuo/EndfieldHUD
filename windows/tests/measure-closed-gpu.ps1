[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidateRange(1, 2147483647)][int]$ProcessId,
    [Parameter(Mandatory = $true)][string]$Output,
    [ValidateRange(2, 50)][int]$SampleSeconds = 30
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Observe only the explicitly selected synthetic probe. This does not launch
# an application, capture pixels, collect system-wide traces or elevate.
$probe = Get-Process -Id $ProcessId
$executablePath = $probe.Path
$processInfo = Get-CimInstance Win32_Process -Filter "ProcessId = $ProcessId"
if ([IO.Path]::GetFileName($executablePath) -cne 'EndfieldHUDWindows.exe' -or
    $processInfo.CommandLine -notmatch '(^|\s)--graphics-probe(\s|$)') {
    throw 'Select the EndfieldHUDWindows synthetic graphics probe process.'
}
$outputPath = [IO.Path]::GetFullPath($Output)
$parent = Split-Path -Parent $outputPath
New-Item -ItemType Directory -Path $parent -Force | Out-Null
$started = [Diagnostics.Stopwatch]::StartNew()
$records = [Collections.Generic.List[object]]::new()
$failure = $null
$counterPath = "\GPU Engine(pid_${ProcessId}_*)\Utilization Percentage"
try {
    Get-Counter -Counter $counterPath -SampleInterval 1 -MaxSamples $SampleSeconds | ForEach-Object {
        $engines = @($_.CounterSamples | ForEach-Object {
            [ordered]@{
                instance = $_.InstanceName
                status = $_.Status
                utilization_percent = $(if ([double]::IsNaN($_.CookedValue) -or [double]::IsInfinity($_.CookedValue)) { $null } else { $_.CookedValue })
            }
        })
        $valid = @($engines | Where-Object { $_.status -eq 0 -and $null -ne $_.utilization_percent -and $_.utilization_percent -ge 0 })
        $probe.Refresh()
        $records.Add([ordered]@{
            elapsed_seconds = $started.Elapsed.TotalSeconds
            private_bytes = $probe.PrivateMemorySize64
            working_set_bytes = $probe.WorkingSet64
            handles = $probe.HandleCount
            cpu_seconds = $probe.TotalProcessorTime.TotalSeconds
            engines = $engines
            valid_engine_samples = $valid.Count
            busiest_engine_percent = $(if ($valid.Count) { ($valid.utilization_percent | Measure-Object -Maximum).Maximum } else { $null })
        })
    }
} catch {
    $failure = $_.Exception.Message
}
$started.Stop()
$allValid = @($records | Where-Object { $_.valid_engine_samples -gt 0 })
$result = [ordered]@{
    schema = 1
    scenario = 'external counter observation of a selected hidden synthetic probe after its lifecycle output is written'
    executable_sha256 = (Get-FileHash -LiteralPath $executablePath -Algorithm SHA256).Hash.ToLowerInvariant()
    elapsed_seconds = $started.Elapsed.TotalSeconds
    sample_seconds_requested = $SampleSeconds
    samples = @($records.ToArray())
    samples_with_valid_engines = $allValid.Count
    counter_error = $failure
    status = $(if ($failure -or $allValid.Count -ne $records.Count -or $records.Count -ne $SampleSeconds) { 'incomplete; GPU activity unverified' } else { 'counter samples collected; inspect observed engine values' })
    limits = 'Sampled per-process GPU engines only; no wakeup trace, visible pacing, ambient animation, desktop capture, compositor-wide attribution or Mac parity acceptance. Missing/invalid counters are not zero utilization.'
    counter_documentation = 'https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.diagnostics/get-counter'
}
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $outputPath -Encoding UTF8
Write-Output "GPU counter observation: $($result.status); $($result.samples_with_valid_engines)/$($records.Count) valid samples. Saved $outputPath"
