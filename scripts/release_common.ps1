<#
 * DAPLink-Wireless — Wireless CMSIS-DAP v2 debug probe firmware
 * Copyright (C) 2025 RinStel <me@rinx.nz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
#>
function Get-ReleaseSourceFingerprint([string]$repoRoot) {
    $sourceRoots = @(
        "firmware\app",
        "firmware\bsp",
        "firmware\drivers",
        "firmware\usb",
        "firmware\linker",
        "vendor\GD32F30x_standard_peripheral",
        "vendor\GD32_CMSIS",
        "vendor\GD32F30x_usbd_library"
    )
    $extensions = @(".c", ".h", ".s", ".ld")
    $files = foreach ($relativeRoot in $sourceRoots) {
        Get-ChildItem (Join-Path $repoRoot $relativeRoot) -Recurse -File |
            Where-Object {
                $extensions -contains $_.Extension.ToLowerInvariant()
            }
    }
    $files += Get-Item (Join-Path $repoRoot "firmware\project.uvprojx")
    $files += Get-Item (Join-Path $repoRoot "scripts\build_gcc.ps1")
    $files += Get-Item (
        Join-Path $repoRoot "firmware\toolchain\gcc\syscalls.c")
    $files += Get-Item (Join-Path $repoRoot "scripts\release_common.ps1")
    $files += Get-Item (Join-Path $repoRoot "scripts\verify_dependencies.ps1")
    $files += Get-Item (Join-Path $repoRoot "dependencies.lock.json")

    $entries = $files | Sort-Object FullName | ForEach-Object {
        $relative = $_.FullName.Substring($repoRoot.Length).
            TrimStart('\').Replace('\', '/')
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
