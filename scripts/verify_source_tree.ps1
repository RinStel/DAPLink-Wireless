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
param(
    [switch]$CleanGenerated
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$firmwareRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $repoRoot "firmware"))

function Remove-FirmwareGeneratedFile([System.IO.FileInfo]$file) {
    $fullPath = [System.IO.Path]::GetFullPath($file.FullName)
    $prefix = $firmwareRoot.TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith(
            $prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove a file outside firmware: $fullPath"
    }
    Remove-Item -LiteralPath $fullPath -Force
}

if ($CleanGenerated) {
    $generatedFiles = @()
    $scatterFile = Join-Path $firmwareRoot "project.sct"
    if (Test-Path -LiteralPath $scatterFile -PathType Leaf) {
        $generatedFiles += Get-Item -LiteralPath $scatterFile
    }
    $generatedFiles += Get-ChildItem -LiteralPath $firmwareRoot -File |
        Where-Object { $_.Name -like "*.uvguix.*" }
    foreach ($file in $generatedFiles) {
        Remove-FirmwareGeneratedFile $file
    }
}

$forbiddenExtensions = @(
    ".o", ".d", ".su", ".axf", ".map", ".lnp", ".dep", ".htm",
    ".elf", ".hex", ".bin"
)
$forbiddenFiles = Get-ChildItem -LiteralPath $firmwareRoot -Recurse -File |
    Where-Object {
        ($forbiddenExtensions -contains $_.Extension.ToLowerInvariant()) -or
        ($_.Name -like "*.uvguix.*") -or
        ($_.Name -eq "project.sct")
    }
$forbiddenDirectories = @("Objects", "Listings", "RTE")
$pollutedDirectories = Get-ChildItem -LiteralPath $firmwareRoot `
    -Recurse -Directory |
    Where-Object { $forbiddenDirectories -contains $_.Name }

if ($forbiddenFiles.Count -gt 0 -or $pollutedDirectories.Count -gt 0) {
    $paths = @($forbiddenFiles.FullName) + @($pollutedDirectories.FullName)
    $relativePaths = $paths | Sort-Object -Unique | ForEach-Object {
        $_.Substring($repoRoot.Length).TrimStart('\')
    }
    throw "Generated files polluted the firmware source tree:`n$(
        $relativePaths -join "`n")"
}

Write-Host "Firmware source tree is clean"
