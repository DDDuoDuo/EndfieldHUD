# Shared local data snapshot helpers; callers validate explicit app/backup roots.
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
