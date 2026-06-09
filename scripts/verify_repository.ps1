$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$lockPath = Join-Path $repoRoot "dependencies.lock.json"

& git -C $repoRoot rev-parse --is-inside-work-tree | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "The project is not inside a Git worktree"
}

$headExists = (& git -C $repoRoot rev-parse --verify HEAD 2>$null)
if ($LASTEXITCODE -eq 0) {
    $whitespaceErrors = & git -C $repoRoot diff-tree --check --root `
        --no-commit-id -r HEAD 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Git HEAD whitespace check failed:`n$(
            $whitespaceErrors -join "`n")"
    }
}

$whitespaceErrors = & git -C $repoRoot diff --cached --check 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "Git index whitespace check failed:`n$(
        $whitespaceErrors -join "`n")"
}

$indexEntries = @{}
foreach ($line in (& git -C $repoRoot ls-files --stage)) {
    if ($line -notmatch '^(\d{6}) ([0-9a-f]{40,64}) \d+\t(.+)$') {
        throw "Unable to parse Git index entry: $line"
    }
    $indexEntries[$Matches[3]] = [ordered]@{
        mode = $Matches[1]
        object = $Matches[2]
    }
}

$forbiddenPatterns = @(
    '(?i)\.pdf$',
    '(?i)\.(o|d|su|axf|map|lnp|dep|htm|elf|hex|bin|zip)$',
    '(?i)(^|/)project\.sct$',
    '(?i)\.uvguix\.',
    '(?i)^(build|dist)/',
    '(?i)^vendor/GD32F30x_usbfs_library/'
)
$forbidden = foreach ($path in $indexEntries.Keys) {
    foreach ($pattern in $forbiddenPatterns) {
        if ($path -match $pattern) {
            $path
            break
        }
    }
}
if ($forbidden) {
    throw "Forbidden generated or local files are indexed:`n$(
        ($forbidden | Sort-Object -Unique) -join "`n")"
}

if (-not (Test-Path -LiteralPath $lockPath)) {
    throw "Dependency lock file is missing: $lockPath"
}
$lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json

foreach ($submodule in $lock.submodules) {
    $path = $submodule.path.Replace('\', '/')
    if (-not $indexEntries.ContainsKey($path)) {
        throw "Locked submodule is not indexed: $path"
    }
    $entry = $indexEntries[$path]
    if ($entry.mode -ne "160000") {
        throw "Dependency must be a Git submodule, not copied files: $path"
    }
    if ($entry.object -ne $submodule.commit) {
        throw "Indexed submodule commit differs from dependency lock: $path"
    }
    $url = (& git -C $repoRoot config -f .gitmodules `
        --get "submodule.$path.url").Trim()
    if ($LASTEXITCODE -ne 0 -or $url -ne $submodule.url) {
        throw "Submodule URL differs from dependency lock: $path"
    }
}

$snapshotRoots = @(
    $lock.vendor_snapshots | ForEach-Object {
        $_.path.Replace('\', '/').TrimEnd('/') + '/'
    }
)
$trackedVendor = @(
    $indexEntries.Keys | Where-Object { $_.StartsWith("vendor/") }
)
$unexpectedVendor = $trackedVendor | Where-Object {
    $path = $_
    -not ($snapshotRoots | Where-Object { $path.StartsWith($_) })
}
if ($unexpectedVendor) {
    throw "Files outside locked vendor snapshots are indexed:`n$(
        ($unexpectedVendor | Sort-Object) -join "`n")"
}
foreach ($snapshot in $lock.vendor_snapshots) {
    $prefix = $snapshot.path.Replace('\', '/').TrimEnd('/') + '/'
    $count = @($trackedVendor | Where-Object { $_.StartsWith($prefix) }).Count
    if ($count -ne [int]$snapshot.file_count) {
        throw "Indexed vendor file count differs from dependency lock: " +
            "$($snapshot.path) ($count != $($snapshot.file_count))"
    }
}

Write-Host "Repository verification passed"
