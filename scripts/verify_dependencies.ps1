$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$lockPath = Join-Path $repoRoot "dependencies.lock.json"

function Get-DirectoryTreeFingerprint([string]$directory) {
    $root = (Resolve-Path -LiteralPath $directory).Path
    $entries = @(
        Get-ChildItem -LiteralPath $root -Recurse -File |
            Sort-Object FullName |
            ForEach-Object {
                $relative = $_.FullName.Substring($root.Length).
                    TrimStart('\').Replace('\', '/')
                $hash = (Get-FileHash -Algorithm SHA256 `
                    -LiteralPath $_.FullName).Hash.ToLowerInvariant()
                "$relative|$hash"
            }
    )
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
    $fingerprint = Get-DirectoryTreeFingerprint $path
    if (($fingerprint.sha256 -ne $snapshot.sha256) -or
        ([int]$fingerprint.file_count -ne [int]$snapshot.file_count)) {
        throw "Vendor snapshot differs from dependency lock: " +
            "$($snapshot.path)"
    }
}

Write-Host "Dependency verification passed"
