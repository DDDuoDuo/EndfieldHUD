[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [ValidateSet('Validate', 'Install', 'Update', 'Rollback', 'Uninstall')][string]$Mode = 'Validate',
    [string]$PackagePath,
    [ValidatePattern('^[0-9a-fA-F]{64}$')][string]$ExpectedSHA256,
    [Parameter(Mandatory = $true)][string]$ExpectedVersion,
    [string]$InstallRoot,
    [string]$BackupRoot,
    [string]$RestoreDataSnapshot,
    [string]$DataRoot = (Join-Path $env:LOCALAPPDATA 'EndfieldHUD'),
    [string]$DataBackupRoot = (Join-Path $env:LOCALAPPDATA 'EndfieldHUD.UpdateBackups')
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$identityName = 'DDDuoDuo.EndfieldHUD.Windows'
Add-Type -AssemblyName System.IO.Compression.FileSystem
. (Join-Path $PSScriptRoot 'data-snapshots.ps1')

function Test-PortableRelativePath([string]$Value) {
    if (-not $Value -or $Value -match '[:\\]' -or $Value.StartsWith('/') -or @($Value.Split('/') | Where-Object { $_ -eq '' -or $_ -eq '.' -or $_ -eq '..' }).Count -gt 0) { throw 'Unsafe portable payload path.' }
}

function Read-PortableInventory([string]$Text, [string]$Version) {
    $inventory = $Text | ConvertFrom-Json
    if ($inventory.schema -ne 1 -or $inventory.kind -cne 'portable-consumer-release' -or $inventory.distribution -cne 'portable' -or $inventory.platform -cne 'Windows' -or $inventory.architecture -cne 'x64' -or $inventory.author -cne 'DDDuoDuo' -or $inventory.windows_version -cne $Version -or $inventory.source_dirty -ne $false) { throw 'Portable Windows release identity/version/architecture mismatch. Developer previews cannot be deployed by this helper.' }
    if ($Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?$') { throw 'Invalid chosen portable Windows version.' }
    $records = @{}
    foreach ($record in $inventory.files) {
        Test-PortableRelativePath $record.path
        if ($record.path -eq 'package-inventory.json' -or $records.ContainsKey($record.path) -or $record.sha256 -notmatch '^[0-9a-f]{64}$' -or $record.bytes -lt 0 -or $record.bytes -gt 134217728) { throw 'Invalid or duplicate portable inventory record.' }
        if ($record.path -notlike 'Resources/*' -and $record.path -cnotin @('EndfieldHUDWindows.exe', 'portable-update.ps1', 'data-snapshots.ps1', 'PORTABLE.txt')) { throw 'Unapproved portable payload location.' }
        $records[$record.path] = $record
    }
    foreach ($required in @('EndfieldHUDWindows.exe', 'portable-update.ps1', 'data-snapshots.ps1', 'PORTABLE.txt', 'Resources/resources-inventory.json', 'Resources/LICENSE.txt', 'Resources/CREDITS.md', 'Resources/OrbiPom/Matter-LICENSE.txt', 'Resources/zlib-LICENSE.txt')) {
        if (-not $records.ContainsKey($required)) { throw 'Portable release is missing required payload/notices.' }
    }
    if ($records.Count -gt 10000 -or ($records.Values | Measure-Object -Property bytes -Sum).Sum -gt 1073741824) { throw 'Portable payload exceeds its bounded inventory.' }
    return [pscustomobject]@{ Inventory = $inventory; Records = $records }
}

function Read-BoundedPortableManifest($Entry) {
    if ($null -eq $Entry -or $Entry.Length -gt 4194304) { throw 'Missing or oversized portable inventory.' }
    $source = $Entry.Open()
    $memory = [IO.MemoryStream]::new()
    try {
        $buffer = [byte[]]::new(65536)
        while (($count = $source.Read($buffer, 0, $buffer.Length)) -gt 0) {
            if ($memory.Length + $count -gt 4194304) { throw 'Portable inventory exceeds its bound.' }
            $memory.Write($buffer, 0, $count)
        }
        return [Text.UTF8Encoding]::new($false, $true).GetString($memory.ToArray())
    } finally { $memory.Dispose(); $source.Dispose() }
}

function Test-PortableEntry($Entry, $Record, [string]$Destination) {
    if ($Entry.Length -ne $Record.bytes) { throw 'Portable payload length differs from its inventory.' }
    $source = $Entry.Open()
    $hash = [Security.Cryptography.SHA256]::Create()
    $target = $null
    try {
        if ($Destination) { $target = [IO.File]::Open($Destination, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write) }
        $buffer = [byte[]]::new(65536)
        $total = 0L
        while (($count = $source.Read($buffer, 0, $buffer.Length)) -gt 0) {
            $total += $count
            if ($total -gt $Record.bytes) { throw 'Portable stream exceeds its declared bound.' }
            $hash.TransformBlock($buffer, 0, $count, $buffer, 0) | Out-Null
            if ($null -ne $target) { $target.Write($buffer, 0, $count) }
        }
        $hash.TransformFinalBlock([byte[]]::new(0), 0, 0) | Out-Null
        $digest = [BitConverter]::ToString($hash.Hash).Replace('-', '').ToLowerInvariant()
        if ($total -ne $Record.bytes -or $digest -cne $Record.sha256) { throw 'Portable payload SHA-256/length differs from its inventory.' }
    } finally { if ($null -ne $target) { $target.Dispose() }; $hash.Dispose(); $source.Dispose() }
}

function Read-VerifiedPortableZip([string]$Path, [string]$Hash, [string]$Version, [string]$Destination) {
    $resolvedPath = Get-CanonicalDirectory $Path
    $file = Get-Item -LiteralPath $resolvedPath -Force
    if ($file.PSIsContainer -or $file.Length -gt 1073741824 -or [IO.Path]::GetExtension($resolvedPath) -ne '.zip') { throw 'An ordinary bounded portable ZIP is required.' }
    if ((Get-FileHash -LiteralPath $resolvedPath -Algorithm SHA256).Hash -ne $Hash) { throw 'Portable ZIP SHA-256 differs from the chosen published release.' }
    $archive = [IO.Compression.ZipFile]::OpenRead($resolvedPath)
    try {
        $prefix = 'EndfieldHUD-Windows/'
        $manifestText = Read-BoundedPortableManifest ($archive.GetEntry($prefix + 'package-inventory.json'))
        $package = Read-PortableInventory $manifestText $Version
        if ($archive.Entries.Count -ne $package.Records.Count + 1) { throw 'Portable ZIP contains missing, duplicate or untracked entries.' }
        $seen = @{}
        foreach ($entry in $archive.Entries) {
            if (-not $entry.FullName.StartsWith($prefix, [StringComparison]::Ordinal)) { throw 'Portable ZIP escaped its Windows payload root.' }
            $relative = $entry.FullName.Substring($prefix.Length)
            Test-PortableRelativePath $relative
            if ($seen.ContainsKey($relative)) { throw 'Duplicate portable ZIP entry.' }
            $seen[$relative] = $true
            if ($relative -eq 'package-inventory.json') { continue }
            if (-not $package.Records.ContainsKey($relative)) { throw 'Portable ZIP contains an untracked payload.' }
            $target = $null
            if ($Destination) {
                $target = [IO.Path]::GetFullPath((Join-Path $Destination $relative))
                if (-not (Test-ContainedPath $target $Destination)) { throw 'Portable extraction escaped its staging directory.' }
                New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
            }
            Test-PortableEntry $entry $package.Records[$relative] $target
        }
        if ($Destination) { [IO.File]::WriteAllText((Join-Path $Destination 'package-inventory.json'), $manifestText, [Text.UTF8Encoding]::new($false)) }
        return $package.Inventory
    } finally { $archive.Dispose() }
}

function Read-InstalledPortable([string]$Root) {
    $manifest = Join-Path $Root 'package-inventory.json'
    if ((Get-Item -LiteralPath $manifest -Force).Length -gt 4194304) { throw 'Installed portable inventory is oversized.' }
    $text = Get-Content -LiteralPath $manifest -Raw
    $descriptor = $text | ConvertFrom-Json
    $package = Read-PortableInventory $text $descriptor.windows_version
    $items = @(Get-ChildItem -LiteralPath $Root -Recurse -Force)
    if (@($items | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count -gt 0) { throw 'Installed portable payload refuses links/reparse points.' }
    $files = @($items | Where-Object { -not $_.PSIsContainer })
    if ($files.Count -ne $package.Records.Count + 1) { throw 'Installed portable payload contains untracked files; keep app data outside the executable/resource folder.' }
    foreach ($record in $package.Records.Values) {
        $path = Join-Path $Root $record.path
        $item = Get-Item -LiteralPath $path -Force
        if ($item.Length -ne $record.bytes -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -cne $record.sha256.ToUpperInvariant()) { throw 'Installed portable payload hash/length differs.' }
    }
    return $package.Inventory
}

function Compare-PortableVersions([string]$Left, [string]$Right) {
    $leftParts = $Left.Split('-', 2)
    $rightParts = $Right.Split('-', 2)
    $leftCore = $leftParts[0].Split('.')
    $rightCore = $rightParts[0].Split('.')
    for ($index = 0; $index -lt 3; $index++) {
        $leftNumber = $leftCore[$index].TrimStart('0'); $rightNumber = $rightCore[$index].TrimStart('0')
        if ($leftNumber.Length -ne $rightNumber.Length) { return [Math]::Sign($leftNumber.Length - $rightNumber.Length) }
        $comparison = [string]::CompareOrdinal($leftNumber, $rightNumber)
        if ($comparison) { return [Math]::Sign($comparison) }
    }
    if ($leftParts.Count -eq 1 -and $rightParts.Count -eq 1) { return 0 }
    if ($leftParts.Count -eq 1) { return 1 }
    if ($rightParts.Count -eq 1) { return -1 }
    $leftPre = $leftParts[1].Split('.'); $rightPre = $rightParts[1].Split('.')
    for ($index = 0; $index -lt [Math]::Min($leftPre.Count, $rightPre.Count); $index++) {
        $leftNumber = $leftPre[$index] -match '^[0-9]+$'; $rightNumber = $rightPre[$index] -match '^[0-9]+$'
        if ($leftNumber -and $rightNumber -and $leftPre[$index].Length -ne $rightPre[$index].Length) { return [Math]::Sign($leftPre[$index].Length - $rightPre[$index].Length) }
        if ($leftNumber -ne $rightNumber) { if ($leftNumber) { return -1 } else { return 1 } }
        $comparison = [string]::CompareOrdinal($leftPre[$index], $rightPre[$index])
        if ($comparison) { return [Math]::Sign($comparison) }
    }
    return [Math]::Sign($leftPre.Count - $rightPre.Count)
}

$candidate = $null
if ($Mode -ne 'Uninstall') {
    if (-not $PackagePath -or -not $ExpectedSHA256) { throw 'Choose the portable ZIP and its published SHA-256 explicitly.' }
    $candidate = Read-VerifiedPortableZip $PackagePath $ExpectedSHA256 $ExpectedVersion $null
}
if ($Mode -eq 'Validate') {
    [ordered]@{ valid = $true; distribution = 'portable'; version = $ExpectedVersion; authenticode = $candidate.authenticode; deployment = 'not requested' } | ConvertTo-Json -Compress
    return
}
if (-not $InstallRoot) { throw 'Choose an app-specific portable installation directory.' }
$installation = Get-CanonicalDirectory $InstallRoot
if (-not $BackupRoot) { $BackupRoot = Join-Path (Split-Path -Parent $installation) 'EndfieldHUD.PortableBackups' }
$backups = Get-CanonicalDirectory $BackupRoot
$data = Get-CanonicalDirectory $DataRoot
$dataBackups = Get-CanonicalDirectory $DataBackupRoot
$roots = @($installation, $backups, $data, $dataBackups)
for ($left = 0; $left -lt $roots.Count; $left++) {
    for ($right = $left + 1; $right -lt $roots.Count; $right++) {
        if ($roots[$left] -eq $roots[$right] -or (Test-ContainedPath $roots[$left] $roots[$right]) -or (Test-ContainedPath $roots[$right] $roots[$left])) { throw 'Portable payload, previous versions, data and data snapshots must use separate directories.' }
    }
}
if ([IO.Path]::GetPathRoot($installation) -ne [IO.Path]::GetPathRoot($backups) -or [IO.Path]::GetPathRoot($data) -ne [IO.Path]::GetPathRoot($dataBackups)) { throw 'Payload promotion and data restoration each require backups on their own volume.' }
if (@(Get-Process -Name EndfieldHUDWindows -ErrorAction SilentlyContinue).Count -gt 0) { throw 'Save and quit EndfieldHUD before changing its portable payload. Unsaved work is never force-stopped.' }
$current = if (Test-Path -LiteralPath $installation) { Read-InstalledPortable $installation } else { $null }
if ($Mode -eq 'Install' -and $null -ne $current) { throw 'Portable app already exists; choose Update explicitly.' }
if ($Mode -ne 'Install' -and $null -eq $current) { throw 'A verified portable consumer installation is required.' }
if ($Mode -eq 'Update' -and (Compare-PortableVersions $candidate.windows_version $current.windows_version) -le 0) { throw 'Update requires a newer portable version; choose Rollback for a downgrade.' }
if ($Mode -eq 'Rollback' -and (Compare-PortableVersions $candidate.windows_version $current.windows_version) -ge 0) { throw 'Rollback requires an older chosen portable version.' }
if ($Mode -eq 'Uninstall' -and $ExpectedVersion -cne $current.windows_version) { throw 'Uninstall must identify the installed portable version.' }
if ($RestoreDataSnapshot -and $Mode -ne 'Rollback') { throw 'Choose Rollback explicitly before restoring a data snapshot.' }
if (-not $PSCmdlet.ShouldProcess($installation, "$Mode portable Windows version $ExpectedVersion; preserve previous payload and local data")) { return }

New-Item -ItemType Directory -Path $backups -Force | Out-Null
$ready = Join-Path $backups ('ready-' + [guid]::NewGuid().ToString('N'))
$preserved = Join-Path $backups ('previous-' + [guid]::NewGuid().ToString('N'))
if (-not (Test-ContainedPath $ready $backups) -or -not (Test-ContainedPath $preserved $backups)) { throw 'Portable preparation escaped its explicit backup root.' }
if ($Mode -ne 'Uninstall') {
    New-Item -ItemType Directory -Path $ready | Out-Null
    Read-VerifiedPortableZip $PackagePath $ExpectedSHA256 $ExpectedVersion $ready | Out-Null
    Read-InstalledPortable $ready | Out-Null
}
$currentVersion = if ($null -ne $current) { $current.windows_version } else { '' }
$snapshot = New-DataSnapshot $data $dataBackups $currentVersion
$moved = $false
try {
    if ($null -ne $current) { Move-Item -LiteralPath $installation -Destination $preserved -Force; $moved = $true }
    if ($Mode -ne 'Uninstall') {
        New-Item -ItemType Directory -Path (Split-Path -Parent $installation) -Force | Out-Null
        Move-Item -LiteralPath $ready -Destination $installation -Force
        Read-InstalledPortable $installation | Out-Null
    }
    if ($RestoreDataSnapshot) { Restore-VerifiedSnapshot $RestoreDataSnapshot $data $dataBackups $ExpectedVersion }
} catch {
    # Preserve a failed candidate before returning the verified previous payload.
    if (Test-Path -LiteralPath $installation) { Move-Item -LiteralPath $installation -Destination (Join-Path $backups ('failed-' + [guid]::NewGuid().ToString('N'))) -Force }
    if ($moved) { Move-Item -LiteralPath $preserved -Destination $installation -Force }
    throw
}
[ordered]@{ completed = $true; mode = $Mode; version = $ExpectedVersion; previous_payload = $(if ($moved) { $preserved } else { $null }); data_snapshot = $snapshot; app_launched = $false; automatic_updates = $false } | ConvertTo-Json -Compress
