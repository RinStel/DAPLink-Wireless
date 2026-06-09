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
    [string]$ToolchainBin = "",
    [string]$KeilPath = "",
    [switch]$SkipKeil,
    [switch]$SkipVerification
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$powerShellExecutable = (Get-Process -Id $PID).Path

if (-not $SkipVerification) {
    $verifyArgs = @()
    if (-not [string]::IsNullOrWhiteSpace($ToolchainBin)) {
        $verifyArgs += @("-ToolchainBin", $ToolchainBin)
    }
    if (-not [string]::IsNullOrWhiteSpace($KeilPath)) {
        $verifyArgs += @("-KeilPath", $KeilPath)
    }
    if ($SkipKeil) {
        $verifyArgs += "-SkipKeil"
    }
    & $powerShellExecutable -NoProfile -File `
        (Join-Path $PSScriptRoot "verify_release.ps1") @verifyArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Release verification failed"
    }
}

$manifestPath = Join-Path $repoRoot "build\gcc\release\manifest.json"
$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
$distRoot = Join-Path $repoRoot "dist"
$packageName = "daplink-wireless-$($manifest.version)"
$packageDir = Join-Path $distRoot $packageName
$zipPath = Join-Path $distRoot "$packageName.zip"
$distFullPath = [System.IO.Path]::GetFullPath($distRoot).TrimEnd('\') + '\'
$packageFullPath = [System.IO.Path]::GetFullPath($packageDir)
if (-not $packageFullPath.StartsWith(
        $distFullPath, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Package path escaped the dist directory"
}

if (Test-Path $packageDir) {
    Remove-Item -LiteralPath $packageDir -Recurse -Force
}
if (Test-Path $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
New-Item -ItemType Directory -Force -Path $packageDir | Out-Null

$releaseBuild = Join-Path $repoRoot "build\gcc\release"
Copy-Item (Join-Path $releaseBuild "daplink_wireless.elf") $packageDir
Copy-Item (Join-Path $releaseBuild "daplink_wireless.hex") $packageDir
Copy-Item (Join-Path $releaseBuild "daplink_wireless.bin") $packageDir
Copy-Item $manifestPath $packageDir
Copy-Item (Join-Path $repoRoot "README.md") $packageDir
Copy-Item (Join-Path $repoRoot "CHANGELOG.md") $packageDir
Copy-Item (Join-Path $repoRoot "THIRD_PARTY_NOTICES.md") $packageDir
Copy-Item (Join-Path $repoRoot "dependencies.lock.json") $packageDir

$licensesDir = Join-Path $packageDir "licenses"
New-Item -ItemType Directory -Force -Path $licensesDir | Out-Null
Copy-Item (Join-Path $repoRoot "licenses\GigaDevice-BSD-3-Clause.txt") `
    $licensesDir
Copy-Item (Join-Path $repoRoot "Third-Party\CMSIS-DAP\LICENSE") `
    (Join-Path $licensesDir "CMSIS-DAP-Apache-2.0.txt")

$docsDir = Join-Path $packageDir "docs"
New-Item -ItemType Directory -Force -Path $docsDir | Out-Null
$releaseDocs = @(
    "release_checklist.md",
    "hardware_acceptance.md",
    "cmsis_dap_validation.md",
    "usb_config_disk.md",
    "radio_protocol_v3.md",
    "frequency_hopping.md"
)
foreach ($doc in $releaseDocs) {
    Copy-Item (Join-Path $repoRoot "docs\$doc") $docsDir
}

Compress-Archive -Path "$packageDir\*" -DestinationPath $zipPath

Add-Type -AssemblyName System.IO.Compression.FileSystem
$requiredEntries = @(
    "CHANGELOG.md",
    "daplink_wireless.bin",
    "daplink_wireless.elf",
    "daplink_wireless.hex",
    "manifest.json",
    "README.md",
    "THIRD_PARTY_NOTICES.md",
    "dependencies.lock.json",
    "licenses/CMSIS-DAP-Apache-2.0.txt",
    "licenses/GigaDevice-BSD-3-Clause.txt"
)
$archive = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    $archiveEntries = @(
        $archive.Entries |
            ForEach-Object { $_.FullName.Replace("\", "/") }
    )
    foreach ($requiredEntry in $requiredEntries) {
        if ($archiveEntries -notcontains $requiredEntry) {
            throw "Release package is missing required entry: $requiredEntry"
        }
    }
    $licenseSources = @{
        "licenses/CMSIS-DAP-Apache-2.0.txt" =
            (Join-Path $repoRoot "Third-Party\CMSIS-DAP\LICENSE")
        "licenses/GigaDevice-BSD-3-Clause.txt" =
            (Join-Path $repoRoot "licenses\GigaDevice-BSD-3-Clause.txt")
    }
    foreach ($entryName in $licenseSources.Keys) {
        $entry = $archive.Entries | Where-Object {
            $_.FullName.Replace("\", "/") -eq $entryName
        } | Select-Object -First 1
        $stream = $entry.Open()
        $sha = [System.Security.Cryptography.SHA256]::Create()
        try {
            $archiveHash = [System.BitConverter]::ToString(
                $sha.ComputeHash($stream)).Replace("-", "")
            $sourceHash = (Get-FileHash -Algorithm SHA256 `
                $licenseSources[$entryName]).Hash
            if ($archiveHash -ne $sourceHash) {
                throw "Release license differs from source: $entryName"
            }
        } finally {
            $sha.Dispose()
            $stream.Dispose()
        }
    }
} finally {
    $archive.Dispose()
}

$zipHashes = Get-ChildItem $distRoot -Filter "*.zip" -File |
    Sort-Object Name |
    ForEach-Object {
        $hash = (Get-FileHash -Algorithm SHA256 $_.FullName).
            Hash.ToLowerInvariant()
        "$hash  $($_.Name)"
    }
$zipHashes |
    Set-Content -Encoding ASCII (Join-Path $distRoot "SHA256SUMS.txt")

Write-Host "Packaged: $zipPath"
