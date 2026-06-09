# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2025 RinStel <me@rinx.nz>
param(
    [string]$ToolchainBin = "",
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "release_common.ps1")
$configurationName = $Configuration.ToLowerInvariant()
$buildDir = Join-Path $repoRoot "build/gcc/$configurationName"

function Find-Tool([string]$name, [string]$directory = "") {
    $fileNames = if ($IsWindows -or $env:OS -eq "Windows_NT") {
        @("$name.exe", $name)
    } else {
        @($name)
    }
    if (-not [string]::IsNullOrWhiteSpace($directory)) {
        foreach ($fileName in $fileNames) {
            $candidate = Join-Path $directory $fileName
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                return $candidate
            }
        }
        throw "Tool not found in ${directory}: $name"
    }
    foreach ($fileName in $fileNames) {
        $command = Get-Command $fileName -ErrorAction SilentlyContinue
        if ($null -ne $command) {
            return $command.Source
        }
    }
    throw "Tool not found on PATH: $name"
}

if ([string]::IsNullOrWhiteSpace($ToolchainBin)) {
    $ToolchainBin = $env:ARM_GCC_BIN
}
if ([string]::IsNullOrWhiteSpace($ToolchainBin)) {
    try {
        $gccPath = Find-Tool "arm-none-eabi-gcc"
        $ToolchainBin = Split-Path -Parent $gccPath
    } catch {
        throw "GNU Arm toolchain not found; pass -ToolchainBin, set " +
            "ARM_GCC_BIN, or add it to PATH"
    }
}

$gcc = Find-Tool "arm-none-eabi-gcc" $ToolchainBin
$objcopy = Find-Tool "arm-none-eabi-objcopy" $ToolchainBin
$size = Find-Tool "arm-none-eabi-size" $ToolchainBin
$firmwareVersionHeader =
    Join-Path $repoRoot "firmware/app/firmware_version.h"

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$cpuFlags = @(
    "-mcpu=cortex-m4",
    "-mthumb",
    "-mfpu=fpv4-sp-d16",
    "-mfloat-abi=hard"
)

$commonFlags = @(
    "-DGD32F30X_HD",
    "-DHXTAL_VALUE=8000000U",
    "-ffunction-sections",
    "-fdata-sections",
    "-fstack-usage",
    "-Wall",
    "-Wextra",
    "-Werror"
)

if ($Configuration -eq "Release") {
    $commonFlags += "-Os"
} else {
    $commonFlags += @("-Og", "-g3")
}

$includeFlags = @(
    "firmware/app",
    "firmware/bsp",
    "firmware/drivers/radio",
    "firmware/drivers/serial",
    "firmware/drivers/swd",
    "firmware/usb",
    "vendor/GD32_CMSIS",
    "vendor/GD32_CMSIS/GD/GD32F30x/Include",
    "vendor/GD32F30x_standard_peripheral/Include",
    "vendor/GD32F30x_usbd_library/device/Include",
    "vendor/GD32F30x_usbd_library/usbd/Include",
    "vendor/GD32F30x_usbd_library/class/device/msc/Include",
    "vendor/GD32F30x_usbd_library/class/device/cdc/Include"
) | ForEach-Object {
    "-I$(Join-Path $repoRoot $_)"
}

$sourcePaths = @(
    "firmware/app/main.c",
    "firmware/app/cmsis_dap.c",
    "firmware/app/frequency_hopping.c",
    "firmware/app/link_adaptation.c",
    "firmware/app/radio_protocol.c",
    "firmware/app/serial_service.c",
    "firmware/app/serial_bridge.c",
    "firmware/app/swd_bridge_service.c",
    "firmware/app/swd_tunnel.c",
    "firmware/app/device_config.c",
    "firmware/app/device_config_storage.c",
    "firmware/usb/usb_config_disk.c",
    "firmware/usb/usb_composite.c",
    "firmware/usb/usb_standard_request.c",
    "firmware/usb/usb_vendor_request.c",
    "firmware/usb/cdc_acm_transport.c",
    "firmware/usb/cmsis_dap_usb.c",
    "firmware/bsp/board.c",
    "firmware/drivers/radio/radio_hal.c",
    "firmware/drivers/radio/sx128x.c",
    "firmware/drivers/serial/target_uart.c",
    "firmware/drivers/swd/target_swd.c",
    "firmware/toolchain/gcc/syscalls.c",
    "vendor/GD32F30x_standard_peripheral/Source/gd32f30x_gpio.c",
    "vendor/GD32F30x_standard_peripheral/Source/gd32f30x_rcu.c",
    "vendor/GD32F30x_standard_peripheral/Source/gd32f30x_spi.c",
    "vendor/GD32F30x_standard_peripheral/Source/gd32f30x_fmc.c",
    "vendor/GD32F30x_standard_peripheral/Source/gd32f30x_fwdgt.c",
    "vendor/GD32F30x_standard_peripheral/Source/gd32f30x_misc.c",
    "vendor/GD32F30x_standard_peripheral/Source/gd32f30x_usart.c",
    "vendor/GD32F30x_usbd_library/device/Source/usbd_core.c",
    "vendor/GD32F30x_usbd_library/device/Source/usbd_enum.c",
    "vendor/GD32F30x_usbd_library/device/Source/usbd_pwr.c",
    "vendor/GD32F30x_usbd_library/device/Source/usbd_transc.c",
    "vendor/GD32F30x_usbd_library/usbd/Source/usbd_lld_core.c",
    "vendor/GD32F30x_usbd_library/usbd/Source/usbd_lld_int.c",
    "vendor/GD32F30x_usbd_library/class/device/msc/Source/usbd_msc_core.c",
    "vendor/GD32F30x_usbd_library/class/device/msc/Source/usbd_msc_bbb.c",
    "vendor/GD32F30x_usbd_library/class/device/msc/Source/usbd_msc_scsi.c",
    "vendor/GD32_CMSIS/GD/GD32F30x/Source/system_gd32f30x.c",
    "vendor/GD32_CMSIS/GD/GD32F30x/Source/GCC/startup_gd32f30x_hd.S"
)

$sources = $sourcePaths | ForEach-Object { Join-Path $repoRoot $_ }

$objects = @()
foreach ($source in $sources) {
    $relative = $source.Substring($repoRoot.Length).TrimStart('\', '/').
        Replace('\', '/')
    $objectName = ($relative -replace '[\\/:]', '_') + ".o"
    $object = Join-Path $buildDir $objectName
    $sourceFlags = @()

    if ($relative.StartsWith(
            "vendor/", [System.StringComparison]::OrdinalIgnoreCase)) {
        # Vendor sources are immutable and may use intentionally unused
        # callback parameters. Project-owned code remains fully -Werror.
        $sourceFlags += @(
            "-Wno-unused-parameter",
            "-Wno-unused-but-set-variable"
        )
    }
    if ($relative.EndsWith(
            "vendor/GD32F30x_usbd_library/device/Source/usbd_enum.c",
            [System.StringComparison]::OrdinalIgnoreCase)) {
        $sourceFlags += @(
            "-Dusbd_standard_request=gd32_usbd_standard_request_unchecked",
            "-Dusbd_vendor_request=gd32_usbd_vendor_request_default"
        )
    }
    $objects += $object

    & $gcc @cpuFlags @commonFlags @sourceFlags @includeFlags `
        -c $source -o $object
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $source"
    }
}

$elf = Join-Path $buildDir "daplink_wireless.elf"
$hex = Join-Path $buildDir "daplink_wireless.hex"
$bin = Join-Path $buildDir "daplink_wireless.bin"
$linkerScript = Join-Path $repoRoot "firmware/linker/gd32f303xC_app.ld"
$map = Join-Path $buildDir "daplink_wireless.map"

& $gcc @cpuFlags @objects "-T$linkerScript" "-Wl,--gc-sections" `
    "-Wl,--fatal-warnings" "-Wl,-Map=$map" "--specs=nosys.specs" -o $elf
if ($LASTEXITCODE -ne 0) {
    throw "Link failed"
}

& $objcopy -O ihex $elf $hex
if ($LASTEXITCODE -ne 0) {
    throw "HEX generation failed"
}

$sizeOutput = & $size $elf | Out-String
if ($LASTEXITCODE -ne 0) {
    throw "Size reporting failed"
}
$sizeOutput.TrimEnd() | Write-Host

$sizeMatch = [regex]::Match(
    $sizeOutput,
    "(?m)^\s*(\d+)\s+(\d+)\s+(\d+)\s+\d+\s+[0-9a-fA-F]+\s+")
if (-not $sizeMatch.Success) {
    throw "Unable to parse firmware size"
}
$textBytes = [int]$sizeMatch.Groups[1].Value
$dataBytes = [int]$sizeMatch.Groups[2].Value
$bssBytes = [int]$sizeMatch.Groups[3].Value
if (($textBytes + $dataBytes) -gt (252 * 1024)) {
    throw "Firmware exceeds the 252 KiB application flash budget"
}
if (($dataBytes + $bssBytes) -gt (48 * 1024)) {
    throw "Firmware exceeds the 48 KiB RAM budget"
}

$stackLimitBytes = 512
$stackUsage = Get-ChildItem -Path $buildDir -Filter "*.su" |
    ForEach-Object {
        Get-Content $_.FullName | ForEach-Object {
            if ($_ -match "`t(\d+)`t") {
                [pscustomobject]@{
                    bytes = [int]$Matches[1]
                    source = $_
                }
            }
        }
    } |
    Sort-Object bytes -Descending
if (($stackUsage.Count -gt 0) -and
    ($stackUsage[0].bytes -gt $stackLimitBytes)) {
    throw "Function stack usage exceeds $stackLimitBytes bytes: $($stackUsage[0].source)"
}

& $objcopy -O binary $elf $bin
if ($LASTEXITCODE -ne 0) {
    throw "BIN generation failed"
}

Write-Host "Built: $elf"
Write-Host "Built: $hex"
Write-Host "Built: $bin"

if ($Configuration -eq "Release") {
    $versionText = Get-Content $firmwareVersionHeader -Raw
    $versionMatch = [regex]::Match(
        $versionText,
        '#define\s+FIRMWARE_VERSION_STRING\s+"([^"]+)"')
    if (-not $versionMatch.Success) {
        throw "Unable to read firmware version"
    }
    $version = $versionMatch.Groups[1].Value
    $sourceFingerprint = Get-ReleaseSourceFingerprint $repoRoot
    $compilerVersion = (& $gcc --version | Select-Object -First 1)
    $artifacts = @($elf, $hex, $bin)
    $hashes = foreach ($artifact in $artifacts) {
        $hash = Get-FileHash -Algorithm SHA256 $artifact
        [ordered]@{
            file = Split-Path -Leaf $artifact
            bytes = (Get-Item $artifact).Length
            sha256 = $hash.Hash.ToLowerInvariant()
        }
    }
    $manifest = [ordered]@{
        product = "DAPLink-Wireless"
        version = $version
        release_status = "release-candidate"
        hardware = "v0.5"
        configuration = "Release"
        cmsis_dap = "v2"
        radio_protocol = 3
        compiler = $compilerVersion
        source_tree_sha256 = $sourceFingerprint.sha256
        source_file_count = $sourceFingerprint.file_count
        flash_bytes = $textBytes + $dataBytes
        ram_bytes = $dataBytes + $bssBytes
        maximum_function_stack_bytes = if ($stackUsage.Count -gt 0) {
            $stackUsage[0].bytes
        } else {
            0
        }
        artifacts = $hashes
    }
    $manifestPath = Join-Path $buildDir "manifest.json"
    $manifest | ConvertTo-Json -Depth 4 |
        Set-Content -Encoding UTF8 $manifestPath
    Write-Host "Built: $manifestPath"
}
