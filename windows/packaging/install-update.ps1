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

. (Join-Path $PSScriptRoot 'data-snapshots.ps1')

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
