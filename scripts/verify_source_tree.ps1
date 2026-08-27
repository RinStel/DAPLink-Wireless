# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2025 RinStel <me@rinx.nz>
# 拒绝源码树中的固件生成物。
$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$firmwareRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $repoRoot "firmware"))
$forbiddenExtensions = @(
    ".o", ".d", ".su", ".axf", ".map", ".lnp", ".dep", ".htm",
    ".elf", ".hex", ".bin"
)
$forbiddenFiles = Get-ChildItem -LiteralPath $firmwareRoot -Recurse -File |
    Where-Object {
        $forbiddenExtensions -contains $_.Extension.ToLowerInvariant()
    }

if ($forbiddenFiles.Count -gt 0) {
    $paths = @($forbiddenFiles.FullName)
    $relativePaths = $paths | Sort-Object -Unique | ForEach-Object {
        $_.Substring($repoRoot.Length).TrimStart('\', '/').
            Replace('\', '/')
    }
    throw "Generated files polluted the firmware source tree:`n$(
        $relativePaths -join "`n")"
}

Write-Host "Firmware source tree is clean"
