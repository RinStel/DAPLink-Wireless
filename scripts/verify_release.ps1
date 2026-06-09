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
    [switch]$SkipKeil
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "release_common.ps1")
$powerShellExecutable = (Get-Process -Id $PID).Path

function Invoke-CheckedScript([string]$path, [string[]]$arguments = @()) {
    & $script:powerShellExecutable -NoProfile -File $path @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Script failed: $path"
    }
}

$tests = @(
    "test_cmsis_dap_protocol.ps1",
    "test_radio_protocol.ps1",
    "test_device_config.ps1",
    "test_device_config_storage.ps1",
    "test_swd_tunnel_protocol.ps1",
    "test_link_adaptation.ps1",
    "test_sx128x_driver.ps1",
    "test_usb_composite_descriptor.ps1",
    "test_usb_disk_geometry.ps1"
)
Invoke-CheckedScript (Join-Path $PSScriptRoot "verify_source_tree.ps1") `
    @("-CleanGenerated")
Invoke-CheckedScript (Join-Path $PSScriptRoot "verify_repository.ps1")
Invoke-CheckedScript (Join-Path $PSScriptRoot "verify_dependencies.ps1")
foreach ($test in $tests) {
    Invoke-CheckedScript (Join-Path $PSScriptRoot $test)
}

$buildScript = Join-Path $PSScriptRoot "build_gcc.ps1"
$buildArgs = @("-Configuration", "Debug")
if (-not [string]::IsNullOrWhiteSpace($ToolchainBin)) {
    $buildArgs += @("-ToolchainBin", $ToolchainBin)
}
Invoke-CheckedScript $buildScript $buildArgs
$buildArgs[1] = "Release"
Invoke-CheckedScript $buildScript $buildArgs

$releaseBuild = Join-Path $repoRoot "build\gcc\release"
$reproducibleFiles = @(
    "daplink_wireless.elf",
    "daplink_wireless.hex",
    "daplink_wireless.bin",
    "manifest.json"
)
$firstBuildHashes = @{}
foreach ($file in $reproducibleFiles) {
    $path = Join-Path $releaseBuild $file
    $firstBuildHashes[$file] =
        (Get-FileHash -Algorithm SHA256 $path).Hash
}
Invoke-CheckedScript $buildScript $buildArgs
foreach ($file in $reproducibleFiles) {
    $path = Join-Path $releaseBuild $file
    $secondHash = (Get-FileHash -Algorithm SHA256 $path).Hash
    if ($secondHash -ne $firstBuildHashes[$file]) {
        throw "Release build is not reproducible: $file"
    }
}

$manifestPath = Join-Path $repoRoot "build\gcc\release\manifest.json"
$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
$versionHeader = Get-Content `
    (Join-Path $repoRoot "firmware\app\firmware_version.h") -Raw
$versionMatch = [regex]::Match(
    $versionHeader, '#define\s+FIRMWARE_VERSION_STRING\s+"([^"]+)"')
if (-not $versionMatch.Success -or
    $manifest.version -ne $versionMatch.Groups[1].Value) {
    throw "Firmware header and release manifest versions do not match"
}
$sourceFingerprint = Get-ReleaseSourceFingerprint $repoRoot
if (($manifest.source_tree_sha256 -ne $sourceFingerprint.sha256) -or
    ($manifest.source_file_count -ne $sourceFingerprint.file_count)) {
    throw "Release manifest source fingerprint does not match the worktree"
}
foreach ($artifact in $manifest.artifacts) {
    $path = Join-Path (Split-Path $manifestPath) $artifact.file
    $actualHash = (Get-FileHash -Algorithm SHA256 $path).Hash.ToLowerInvariant()
    if ($actualHash -ne $artifact.sha256) {
        throw "Artifact hash mismatch: $($artifact.file)"
    }
}

$projectText = Get-Content (Join-Path $repoRoot "firmware\project.uvprojx") -Raw
$buildText = Get-Content $buildScript -Raw
if (($projectText -match '(?i)[\\/]third-party[\\/]') -or
    ($buildText -match '(?i)[\\/]third-party[\\/]')) {
    throw "Firmware builds must not compile sources from Third-Party"
}
$requiredDependencies = @(
    "vendor\GD32_CMSIS",
    "vendor\GD32F30x_standard_peripheral",
    "vendor\GD32F30x_usbd_library"
)
foreach ($dependency in $requiredDependencies) {
    if (-not (Test-Path (Join-Path $repoRoot $dependency))) {
        throw "Required dependency is missing: $dependency"
    }
}

if (-not $SkipKeil) {
    if ([string]::IsNullOrWhiteSpace($KeilPath)) {
        $KeilPath = $env:KEIL_UV4_PATH
    }
    if ([string]::IsNullOrWhiteSpace($KeilPath)) {
        $keilCommand = Get-Command "UV4.exe" -ErrorAction SilentlyContinue
        if ($null -ne $keilCommand) {
            $KeilPath = $keilCommand.Source
        }
    }
    if ([string]::IsNullOrWhiteSpace($KeilPath)) {
        $candidates = @(
            "C:\Keil_v5\UV4\UV4.exe",
            "C:\Keil\UV4\UV4.exe",
            "D:\Keil_v5\UV4\UV4.exe",
            "D:\Keil\UV4\UV4.exe"
        )
        $mdkRegistry = Get-ItemProperty `
            "HKLM:\SOFTWARE\WOW6432Node\Keil\Products\MDK" `
            -ErrorAction SilentlyContinue
        if ($null -ne $mdkRegistry.Path) {
            $mdkRoot = Split-Path -Parent $mdkRegistry.Path
            $candidates += Join-Path $mdkRoot "UV4\UV4.exe"
        }
        $KeilPath = $candidates | Where-Object { Test-Path $_ } |
            Select-Object -First 1
    }
    if ([string]::IsNullOrWhiteSpace($KeilPath) -or
        -not (Test-Path $KeilPath)) {
        throw "Keil UV4.exe not found; pass -KeilPath, set KEIL_UV4_PATH, " +
            "add it to PATH, or use -SkipKeil"
    }
    $project = Join-Path $repoRoot "firmware\project.uvprojx"
    $projectDir = Split-Path $project
    New-Item -ItemType Directory -Force `
        -Path (Join-Path $repoRoot "build\keil\objects") | Out-Null
    New-Item -ItemType Directory -Force `
        -Path (Join-Path $repoRoot "build\keil\listings") | Out-Null
    $log = Join-Path $repoRoot "build\keil-release.log"
    if (Test-Path $log) {
        Remove-Item -LiteralPath $log -Force
    }
    $keilProcess = Start-Process -FilePath $KeilPath `
        -ArgumentList @("-r", "project.uvprojx", "-j0", "-o",
            "..\build\keil-release.log") `
        -WorkingDirectory $projectDir -WindowStyle Hidden -Wait -PassThru
    if ($keilProcess.ExitCode -ne 0) {
        throw "Keil build failed with exit code $($keilProcess.ExitCode)"
    }
    if (-not (Test-Path $log)) {
        throw "Keil did not generate the requested build log"
    }
    $logText = Get-Content $log -Raw
    if ($logText -notmatch '0 Error\(s\), 0 Warning\(s\)') {
        throw "Keil build did not finish with zero errors and warnings"
    }
    Invoke-CheckedScript (Join-Path $PSScriptRoot "verify_source_tree.ps1") `
        @("-CleanGenerated")
    $postKeilFingerprint = Get-ReleaseSourceFingerprint $repoRoot
    if (($manifest.source_tree_sha256 -ne $postKeilFingerprint.sha256) -or
        ($manifest.source_file_count -ne $postKeilFingerprint.file_count)) {
        throw "Keil modified release source files during the build"
    }
}

Write-Host "Release verification passed for $($manifest.version)"
