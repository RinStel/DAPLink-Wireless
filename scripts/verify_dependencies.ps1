# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2025 RinStel <me@rinx.nz>
$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$lockPath = Join-Path $repoRoot "dependencies.lock.json"

function Get-DirectoryTreeFingerprint([string]$relativeDirectory) {
    $entries = @(
        & git -C $repoRoot ls-files --stage -- $relativeDirectory |
            ForEach-Object {
                if ($_ -notmatch '^(\d+) ([0-9a-f]+) \d+\t(.+)$') {
                    throw "Cannot parse Git index entry: $_"
                }
                $path = $Matches[3].Replace('\', '/')
                $relative = $path.Substring(
                    $relativeDirectory.Length).TrimStart('/')
                "$relative|$($Matches[1])|$($Matches[2])"
            }
    )
    if ($LASTEXITCODE -ne 0) {
        throw "Cannot read Git index for: $relativeDirectory"
    }
    [Array]::Sort(
        $entries,
        [System.StringComparer]::Ordinal)
    $bytes = [System.Text.Encoding]::UTF8.GetBytes(
        $entries -join "`n")
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $digest = $sha.ComputeHash($bytes)
    } finally {
        $sha.Dispose()
    }
    return [ordered]@{
        sha256 = [System.BitConverter]::ToString($digest).
            Replace("-", "").ToLowerInvariant()
        file_count = $entries.Count
    }
}

if (-not (Test-Path -LiteralPath $lockPath)) {
    throw "Dependency lock file is missing: $lockPath"
}
$lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json

foreach ($submodule in $lock.submodules) {
    $path = Join-Path $repoRoot $submodule.path
    if (-not (Test-Path -LiteralPath (Join-Path $path ".git"))) {
        throw "Submodule is not initialized: $($submodule.path). Run " +
            "'git submodule update --init --recursive'."
    }
    $safePath = $path.Replace('\', '/')
    $head = (& git -c "safe.directory=$safePath" -C $path rev-parse HEAD).
        Trim()
    if ($LASTEXITCODE -ne 0 -or $head -ne $submodule.commit) {
        throw "Submodule commit mismatch: $($submodule.path)"
    }
    & git -c "safe.directory=$safePath" -C $path diff --quiet
    if ($LASTEXITCODE -ne 0) {
        throw "Submodule has modified tracked files: $($submodule.path)"
    }
    $untracked = & git -c "safe.directory=$safePath" -C $path `
        status --porcelain --untracked-files=normal
    if ($LASTEXITCODE -ne 0 -or $untracked) {
        throw "Submodule worktree is not clean: $($submodule.path)"
    }
}

foreach ($snapshot in $lock.vendor_snapshots) {
    $path = Join-Path $repoRoot $snapshot.path
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Vendor snapshot is missing: $($snapshot.path)"
    }
    & git -C $repoRoot diff --quiet HEAD -- $snapshot.path
    if ($LASTEXITCODE -ne 0) {
        throw "Vendor snapshot has modified tracked files: " +
            "$($snapshot.path)"
    }
    $untracked = & git -C $repoRoot ls-files --others `
        --exclude-standard -- $snapshot.path
    if ($LASTEXITCODE -ne 0 -or $untracked) {
        throw "Vendor snapshot has untracked files: $($snapshot.path)"
    }
    $fingerprint = Get-DirectoryTreeFingerprint $snapshot.path
    if (($fingerprint.sha256 -ne $snapshot.sha256) -or
        ([int]$fingerprint.file_count -ne [int]$snapshot.file_count)) {
        throw "Vendor snapshot differs from dependency lock: " +
            "$($snapshot.path)"
    }
}

Write-Host "Dependency verification passed"
