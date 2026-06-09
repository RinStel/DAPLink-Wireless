# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2025 RinStel <me@rinx.nz>
function Get-ReleaseSourceFingerprint([string]$repoRoot) {
    $sourceRoots = @(
        "firmware/app",
        "firmware/bsp",
        "firmware/drivers",
        "firmware/usb",
        "firmware/linker",
        "vendor/GD32F30x_standard_peripheral",
        "vendor/GD32_CMSIS",
        "vendor/GD32F30x_usbd_library"
    )
    $extensions = @(".c", ".h", ".s", ".ld")
    $files = foreach ($relativeRoot in $sourceRoots) {
        Get-ChildItem (Join-Path $repoRoot $relativeRoot) -Recurse -File |
            Where-Object {
                $extensions -contains $_.Extension.ToLowerInvariant()
            }
    }
    $files += Get-Item (Join-Path $repoRoot "firmware/project.uvprojx")
    $files += Get-Item (Join-Path $repoRoot "scripts/build_gcc.ps1")
    $files += Get-Item (
        Join-Path $repoRoot "firmware/toolchain/gcc/syscalls.c")
    $files += Get-Item (Join-Path $repoRoot "scripts/release_common.ps1")
    $files += Get-Item (Join-Path $repoRoot "scripts/verify_dependencies.ps1")
    $files += Get-Item (Join-Path $repoRoot "dependencies.lock.json")

    $entries = $files | Sort-Object FullName | ForEach-Object {
        $relative = $_.FullName.Substring($repoRoot.Length).
            TrimStart('\', '/').Replace('\', '/')
        $hash = (Get-FileHash -Algorithm SHA256 $_.FullName).
            Hash.ToLowerInvariant()
        "$relative|$hash"
    }
    $text = $entries -join "`n"
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($text)
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
