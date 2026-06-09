param(
    [string]$ToolchainBin = "",
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "release_common.ps1")
$configurationName = $Configuration.ToLowerInvariant()
$buildDir = Join-Path $repoRoot "build\gcc\$configurationName"

if ([string]::IsNullOrWhiteSpace($ToolchainBin)) {
    $ToolchainBin = $env:ARM_GCC_BIN
}
if ([string]::IsNullOrWhiteSpace($ToolchainBin)) {
    $gccCommand = Get-Command "arm-none-eabi-gcc.exe" `
        -ErrorAction SilentlyContinue
    if ($null -ne $gccCommand) {
        $ToolchainBin = Split-Path -Parent $gccCommand.Source
    }
}
if ([string]::IsNullOrWhiteSpace($ToolchainBin)) {
    throw "GNU Arm toolchain not found; pass -ToolchainBin, set " +
        "ARM_GCC_BIN, or add it to PATH"
}

$gcc = Join-Path $ToolchainBin "arm-none-eabi-gcc.exe"
$objcopy = Join-Path $ToolchainBin "arm-none-eabi-objcopy.exe"
$size = Join-Path $ToolchainBin "arm-none-eabi-size.exe"
$firmwareVersionHeader =
    Join-Path $repoRoot "firmware\app\firmware_version.h"

if (-not (Test-Path $gcc)) {
    throw "arm-none-eabi-gcc not found: $gcc"
}

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
    "-I$repoRoot\firmware\app",
    "-I$repoRoot\firmware\bsp",
    "-I$repoRoot\firmware\drivers\radio",
    "-I$repoRoot\firmware\drivers\serial",
    "-I$repoRoot\firmware\drivers\swd",
    "-I$repoRoot\firmware\usb",
    "-I$repoRoot\vendor\GD32_CMSIS",
    "-I$repoRoot\vendor\GD32_CMSIS\GD\GD32F30x\Include",
    "-I$repoRoot\vendor\GD32F30x_standard_peripheral\Include",
    "-I$repoRoot\vendor\GD32F30x_usbd_library\device\Include",
    "-I$repoRoot\vendor\GD32F30x_usbd_library\usbd\Include",
    "-I$repoRoot\vendor\GD32F30x_usbd_library\class\device\msc\Include",
    "-I$repoRoot\vendor\GD32F30x_usbd_library\class\device\cdc\Include"
)

$sources = @(
    "$repoRoot\firmware\app\main.c",
    "$repoRoot\firmware\app\cmsis_dap.c",
    "$repoRoot\firmware\app\frequency_hopping.c",
    "$repoRoot\firmware\app\link_adaptation.c",
    "$repoRoot\firmware\app\radio_protocol.c",
    "$repoRoot\firmware\app\serial_service.c",
    "$repoRoot\firmware\app\serial_bridge.c",
    "$repoRoot\firmware\app\swd_bridge_service.c",
    "$repoRoot\firmware\app\swd_tunnel.c",
    "$repoRoot\firmware\app\device_config.c",
    "$repoRoot\firmware\app\device_config_storage.c",
    "$repoRoot\firmware\usb\usb_config_disk.c",
    "$repoRoot\firmware\usb\usb_composite.c",
    "$repoRoot\firmware\usb\usb_standard_request.c",
    "$repoRoot\firmware\usb\usb_vendor_request.c",
    "$repoRoot\firmware\usb\cdc_acm_transport.c",
    "$repoRoot\firmware\usb\cmsis_dap_usb.c",
    "$repoRoot\firmware\bsp\board.c",
    "$repoRoot\firmware\drivers\radio\radio_hal.c",
    "$repoRoot\firmware\drivers\radio\sx128x.c",
    "$repoRoot\firmware\drivers\serial\target_uart.c",
    "$repoRoot\firmware\drivers\swd\target_swd.c",
    "$repoRoot\firmware\toolchain\gcc\syscalls.c",
    "$repoRoot\vendor\GD32F30x_standard_peripheral\Source\gd32f30x_gpio.c",
    "$repoRoot\vendor\GD32F30x_standard_peripheral\Source\gd32f30x_rcu.c",
    "$repoRoot\vendor\GD32F30x_standard_peripheral\Source\gd32f30x_spi.c",
    "$repoRoot\vendor\GD32F30x_standard_peripheral\Source\gd32f30x_fmc.c",
    "$repoRoot\vendor\GD32F30x_standard_peripheral\Source\gd32f30x_fwdgt.c",
    "$repoRoot\vendor\GD32F30x_standard_peripheral\Source\gd32f30x_misc.c",
    "$repoRoot\vendor\GD32F30x_standard_peripheral\Source\gd32f30x_usart.c",
    "$repoRoot\vendor\GD32F30x_usbd_library\device\Source\usbd_core.c",
    "$repoRoot\vendor\GD32F30x_usbd_library\device\Source\usbd_enum.c",
    "$repoRoot\vendor\GD32F30x_usbd_library\device\Source\usbd_pwr.c",
    "$repoRoot\vendor\GD32F30x_usbd_library\device\Source\usbd_transc.c",
    "$repoRoot\vendor\GD32F30x_usbd_library\usbd\Source\usbd_lld_core.c",
    "$repoRoot\vendor\GD32F30x_usbd_library\usbd\Source\usbd_lld_int.c",
    "$repoRoot\vendor\GD32F30x_usbd_library\class\device\msc\Source\usbd_msc_core.c",
    "$repoRoot\vendor\GD32F30x_usbd_library\class\device\msc\Source\usbd_msc_bbb.c",
    "$repoRoot\vendor\GD32F30x_usbd_library\class\device\msc\Source\usbd_msc_scsi.c",
    "$repoRoot\vendor\GD32_CMSIS\GD\GD32F30x\Source\system_gd32f30x.c",
    "$repoRoot\vendor\GD32_CMSIS\GD\GD32F30x\Source\GCC\startup_gd32f30x_hd.S"
)

$objects = @()
foreach ($source in $sources) {
    $relative = $source.Substring($repoRoot.Length).TrimStart('\')
    $objectName = ($relative -replace '[\\/:]', '_') + ".o"
    $object = Join-Path $buildDir $objectName
    $sourceFlags = @()

    if ($relative.StartsWith(
            "vendor\", [System.StringComparison]::OrdinalIgnoreCase)) {
        # Vendor sources are immutable and may use intentionally unused
        # callback parameters. Project-owned code remains fully -Werror.
        $sourceFlags += @(
            "-Wno-unused-parameter",
            "-Wno-unused-but-set-variable"
        )
    }
    if ($relative.EndsWith(
            "vendor\GD32F30x_usbd_library\device\Source\usbd_enum.c",
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
$linkerScript = Join-Path $repoRoot "firmware\linker\gd32f303xC_app.ld"

& $gcc @cpuFlags @objects "-T$linkerScript" "-Wl,--gc-sections" "-Wl,-Map=$buildDir\daplink_wireless.map" "--specs=nosys.specs" -o $elf
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
