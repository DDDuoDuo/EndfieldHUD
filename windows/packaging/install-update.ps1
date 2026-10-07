[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [ValidateSet('Validate', 'Install', 'Update', 'Rollback', 'Uninstall')][string]$Mode = 'Validate',
    [Parameter(Mandatory = $true)][string]$PackagePath,
    [Parameter(Mandatory = $true)][ValidatePattern('^[0-9a-fA-F]{64}$')][string]$ExpectedSHA256,
    [Parameter(Mandatory = $true)][string]$Publisher,
    [Parameter(Mandatory = $true)][ValidatePattern('^[0-9a-fA-F]{40}$')][string]$SignerThumbprint,
    [Parameter(Mandatory = $true)][string]$ExpectedPackageVersion,
    [string]$RollbackPackagePath,
    [ValidatePattern('^[0-9a-fA-F]{64}$')][string]$RollbackSHA256,
    [string]$RestoreDataSnapshot,
    [string]$DataRoot = (Join-Path $env:LOCALAPPDATA 'EndfieldHUD'),
    [string]$BackupRoot = (Join-Path $env:LOCALAPPDATA 'EndfieldHUD.UpdateBackups')
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$identityName = 'DDDuoDuo.EndfieldHUD.Windows'
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Get-CanonicalDirectory([string]$Value) {
    $full = [IO.Path]::GetFullPath($Value).TrimEnd([IO.Path]::DirectorySeparatorChar)
    $driveRoot = [IO.Path]::GetPathRoot($full).TrimEnd([IO.Path]::DirectorySeparatorChar)
    if ($full -eq $driveRoot) { throw 'An app-specific data/backup directory is required.' }
    $cursor = $full
    while ($cursor) {
        if (Test-Path -LiteralPath $cursor) {
            # AppData and user-chosen ancestors can be hidden. Inspect them
            # explicitly so the reparse check does not depend on visibility.
            if ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'App data/backup directory ancestors must not be symbolic/reparse paths.' }
        }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
    return $full
}

function Test-ContainedPath([string]$Path, [string]$Parent) {
    $resolvedPath = [IO.Path]::GetFullPath($Path)
    $resolvedParent = [IO.Path]::GetFullPath($Parent).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    return $resolvedPath.StartsWith($resolvedParent, [StringComparison]::OrdinalIgnoreCase)
}

function Read-VerifiedPackage([string]$Path, [string]$Hash, [string]$Version) {
    $resolvedPath = [IO.Path]::GetFullPath($Path)
    if ([IO.Path]::GetExtension($resolvedPath) -ne '.msix') { throw 'Only a signed x64 MSIX package is accepted.' }
    $item = Get-Item -LiteralPath $resolvedPath
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Symbolic/reparse package paths are not accepted.' }
    $actualHash = (Get-FileHash -LiteralPath $resolvedPath -Algorithm SHA256).Hash
    if ($actualHash -ne $Hash) { throw 'Downloaded MSIX SHA-256 differs from the chosen release manifest.' }
    $signature = Get-AuthenticodeSignature -LiteralPath $resolvedPath
    if ($signature.Status -ne 'Valid' -or $null -eq $signature.SignerCertificate -or $signature.SignerCertificate.Thumbprint -ne $SignerThumbprint -or $signature.SignerCertificate.Subject -cne $Publisher) {
        throw 'MSIX signature must validate through Windows trust and match the chosen publisher/signing identity.'
    }
    $archive = [IO.Compression.ZipFile]::OpenRead($resolvedPath)
    try {
        $entry = $archive.GetEntry('AppxManifest.xml')
        if ($null -eq $entry -or $entry.Length -gt 65536) { throw 'Missing or oversized MSIX manifest.' }
        $stream = $entry.Open()
        try {
            $settings = [Xml.XmlReaderSettings]::new()
            $settings.DtdProcessing = [Xml.DtdProcessing]::Prohibit
            $settings.XmlResolver = $null
            $reader = [Xml.XmlReader]::Create($stream, $settings)
            try {
                $document = [Xml.XmlDocument]::new()
                $document.XmlResolver = $null
                $document.Load($reader)
            } finally { $reader.Dispose() }
        } finally { $stream.Dispose() }
    } finally { $archive.Dispose() }
    $identities = @($document.GetElementsByTagName('Identity'))
    if ($identities.Count -ne 1) { throw 'MSIX identity is missing or ambiguous.' }
    $identity = $identities[0]
    if ($identity.GetAttribute('Name') -cne $identityName -or $identity.GetAttribute('Publisher') -cne $Publisher -or $identity.GetAttribute('ProcessorArchitecture') -ne 'x64' -or $identity.GetAttribute('Version') -cne $Version) {
        throw 'MSIX identity/publisher/version/architecture does not match this Windows release. Preview and Mac assets cannot be installed here.'
    }
    return [pscustomobject]@{ Path = $resolvedPath; Version = [version]$Version; SHA256 = $actualHash; Publisher = $Publisher }
}

function New-DataSnapshot([string]$Root, [string]$Backups, [string]$PackageVersion) {
    $snapshot = Join-Path $Backups ([guid]::NewGuid().ToString('N'))
    if (-not (Test-ContainedPath $snapshot $Backups)) { throw 'Snapshot escaped its explicit backup directory.' }
    New-Item -ItemType Directory -Path $snapshot -Force | Out-Null
    $files = @()
    $data = Join-Path $snapshot 'data'
    if (Test-Path -LiteralPath $Root) {
        $source = Get-Item -LiteralPath $Root -Force
        if (-not $source.PSIsContainer -or ($source.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Data root must be an ordinary app data directory.' }
        $items = @(Get-ChildItem -LiteralPath $Root -Recurse -Force)
        if (@($items | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count -gt 0) { throw 'Data snapshot refuses links/reparse points; relink referenced files through the app.' }
        Copy-Item -LiteralPath $Root -Destination $data -Recurse -Force
        foreach ($file in @(Get-ChildItem -LiteralPath $data -Recurse -File -Force)) {
            $relative = $file.FullName.Substring($data.Length + 1)
            $files += [ordered]@{ path = $relative; bytes = $file.Length; sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash }
        }
    } else {
        New-Item -ItemType Directory -Path $data | Out-Null
    }
    [ordered]@{ schema = 1; identity = $identityName; data_root = $Root; package_version = $PackageVersion; files = $files; created_utc = [DateTime]::UtcNow.ToString('o') } |
        ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $snapshot 'snapshot.json') -Encoding UTF8
    return $snapshot
}

function Restore-VerifiedSnapshot([string]$Snapshot, [string]$Root, [string]$Backups, [string]$Version) {
    $snapshotRoot = Get-CanonicalDirectory $Snapshot
    if (-not (Test-ContainedPath $snapshotRoot $Backups)) { throw 'Rollback snapshot must be inside the explicit local backup directory.' }
    $descriptor = Get-Content -LiteralPath (Join-Path $snapshotRoot 'snapshot.json') -Raw | ConvertFrom-Json
    if ($descriptor.schema -ne 1 -or $descriptor.identity -cne $identityName -or $descriptor.data_root -cne $Root -or $descriptor.package_version -cne $Version) { throw 'Rollback data snapshot identity/root/version mismatch.' }
    $data = Join-Path $snapshotRoot 'data'
    foreach ($record in $descriptor.files) {
        $path = [IO.Path]::GetFullPath((Join-Path $data $record.path))
        if (-not (Test-ContainedPath $path $data)) { throw 'Unsafe rollback snapshot file path.' }
        $item = Get-Item -LiteralPath $path -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $item.Length -ne $record.bytes -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $record.sha256) { throw 'Rollback snapshot hash/length differs.' }
    }
    $actualFiles = @(Get-ChildItem -LiteralPath $data -Recurse -File -Force)
    if ($actualFiles.Count -ne @($descriptor.files).Count) { throw 'Rollback snapshot contains untracked files.' }
    $ready = Join-Path $Backups ('restore-ready-' + [guid]::NewGuid().ToString('N'))
    if (-not (Test-ContainedPath $ready $Backups)) { throw 'Restore preparation escaped backup root.' }
    Copy-Item -LiteralPath $data -Destination $ready -Recurse -Force
    foreach ($record in $descriptor.files) {
        $prepared = Join-Path $ready $record.path
        if ((Get-FileHash -LiteralPath $prepared -Algorithm SHA256).Hash -ne $record.sha256) { throw 'Prepared rollback copy differs; current data remains intact.' }
    }
    $preserved = Join-Path $Backups ('before-restore-' + [guid]::NewGuid().ToString('N'))
    if (-not (Test-ContainedPath $preserved $Backups)) { throw 'Preservation path escaped backup root.' }
    if (Test-Path -LiteralPath $Root) {
        # Both resolved absolute move targets are explicit app/backup roots.
        Move-Item -LiteralPath $Root -Destination $preserved -Force
    }
    try {
        Move-Item -LiteralPath $ready -Destination $Root -Force
    } catch {
        if (-not (Test-Path -LiteralPath $Root) -and (Test-Path -LiteralPath $preserved)) { Move-Item -LiteralPath $preserved -Destination $Root -Force }
        throw
    }
}

$candidate = Read-VerifiedPackage $PackagePath $ExpectedSHA256 $ExpectedPackageVersion
if ($Mode -eq 'Validate') {
    [ordered]@{ valid = $true; identity = $identityName; architecture = 'x64'; version = $ExpectedPackageVersion; sha256 = $candidate.SHA256; deployment = 'not requested' } | ConvertTo-Json -Compress
    return
}
$dataDirectory = Get-CanonicalDirectory $DataRoot
$backupDirectory = Get-CanonicalDirectory $BackupRoot
if ($dataDirectory -eq $backupDirectory -or (Test-ContainedPath $backupDirectory $dataDirectory) -or (Test-ContainedPath $dataDirectory $backupDirectory)) { throw 'Data and backup directories must be separate.' }
if (-not [string]::Equals([IO.Path]::GetPathRoot($dataDirectory), [IO.Path]::GetPathRoot($backupDirectory), [StringComparison]::OrdinalIgnoreCase)) { throw 'Data and backup directories must share a volume for atomic rollback promotion.' }
$running = @(Get-Process -Name EndfieldHUDWindows -ErrorAction SilentlyContinue)
if ($running.Count -gt 0) { throw 'Save your work and quit EndfieldHUD before installation/update/rollback. The updater does not force-stop unsaved work.' }
$installed = @(Get-AppxPackage -Name $identityName)
if ($installed.Count -gt 1) { throw 'Multiple current-user Windows package registrations require reconciliation.' }
$current = if ($installed.Count -eq 1) { $installed[0] } else { $null }
if ($null -ne $current -and $current.Publisher -cne $Publisher) { throw 'Installed publisher differs from the chosen Windows publisher.' }
if ($Mode -eq 'Install' -and $null -ne $current) { throw 'Windows package is already installed; choose Update explicitly.' }
if ($Mode -ne 'Install' -and $null -eq $current) { throw 'The consumer Windows package is not installed; developer builds are protected.' }
if ($Mode -eq 'Update' -and $candidate.Version -le [version]$current.Version) { throw 'Update requires a newer package version; downgrade requires explicit Rollback.' }
if ($Mode -eq 'Rollback' -and $candidate.Version -ge [version]$current.Version) { throw 'Rollback requires an older signed package version.' }
if ($Mode -eq 'Uninstall' -and $candidate.Version -ne [version]$current.Version) { throw 'Uninstall evidence must match the current package version.' }
$previous = $null
if ($Mode -eq 'Update') {
    if (-not $RollbackPackagePath -or -not $RollbackSHA256) { throw 'Update requires the previous signed MSIX and its trusted SHA-256 for rollback.' }
    $previous = Read-VerifiedPackage $RollbackPackagePath $RollbackSHA256 ([string]$current.Version)
}
if ($RestoreDataSnapshot -and $Mode -ne 'Rollback') { throw 'Data snapshot restore is only available during explicit Rollback.' }
$action = "$Mode signed Windows package $ExpectedPackageVersion; preserve data in $backupDirectory"
if (-not $PSCmdlet.ShouldProcess($identityName, $action)) { return }
$currentVersion = if ($null -ne $current) { [string]$current.Version } else { '' }
$snapshot = New-DataSnapshot $dataDirectory $backupDirectory $currentVersion
try {
    if ($Mode -eq 'Uninstall') {
        Remove-AppxPackage -Package $current.PackageFullName -Confirm:$false
    } elseif ($Mode -eq 'Rollback') {
        Add-AppxPackage -Path $candidate.Path -ForceUpdateFromAnyVersion
        if ($RestoreDataSnapshot) { Restore-VerifiedSnapshot $RestoreDataSnapshot $dataDirectory $backupDirectory $ExpectedPackageVersion }
    } else {
        Add-AppxPackage -Path $candidate.Path
    }
    $after = @(Get-AppxPackage -Name $identityName)
    if ($Mode -eq 'Uninstall') {
        if ($after.Count -ne 0) { throw 'Windows still reports the package installed.' }
    } elseif ($after.Count -ne 1 -or [version]$after[0].Version -ne $candidate.Version -or $after[0].Publisher -cne $Publisher) {
        throw 'Post-deployment package identity/version/publisher verification failed.'
    }
    [ordered]@{ completed = $true; mode = $Mode; identity = $identityName; version = $ExpectedPackageVersion; snapshot = $snapshot; app_launched = $false } | ConvertTo-Json -Compress
} catch {
    $failure = $_
    if ($null -ne $previous) {
        $afterFailure = @(Get-AppxPackage -Name $identityName)
        if ($afterFailure.Count -ne 1 -or [version]$afterFailure[0].Version -ne $previous.Version) {
            Add-AppxPackage -Path $previous.Path -ForceUpdateFromAnyVersion
        }
        $rolledBack = @(Get-AppxPackage -Name $identityName)
        if ($rolledBack.Count -ne 1 -or [version]$rolledBack[0].Version -ne $previous.Version) { throw "Update failed and rollback requires attention. Data snapshot: $snapshot" }
    }
    throw "Deployment failed; existing data/snapshot preserved at $snapshot. $($failure.Exception.Message)"
}
