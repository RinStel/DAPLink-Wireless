# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2025 RinStel <me@rinx.nz>
param(
    [ValidateSet(
        "all",
        "cmsis-dap",
        "radio-protocol",
        "device-config",
        "config-storage",
        "swd-tunnel",
        "link-adaptation",
        "sx1281",
        "usb-descriptor",
        "usb-disk"
    )]
    [string]$Name = "all",
    [string]$Compiler = "gcc"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $repoRoot "build/host-tests"
$isWindowsHost = $env:OS -eq "Windows_NT"

function Repo-Path([string]$relativePath) {
    return Join-Path $repoRoot $relativePath
}

$tests = [ordered]@{
    "cmsis-dap" = @{
        label = "CMSIS-DAP protocol"
        includes = @(
            "firmware/app",
            "firmware/bsp",
            "firmware/drivers/radio",
            "firmware/drivers/swd"
        )
        sources = @(
            "tests/cmsis_dap_protocol_test.c",
            "firmware/app/cmsis_dap.c"
        )
    }
    "radio-protocol" = @{
        label = "Radio protocol"
        analyzer = $true
        includes = @("firmware/app")
        sources = @(
            "tests/radio_protocol_test.c",
            "firmware/app/radio_protocol.c",
            "firmware/app/frequency_hopping.c"
        )
    }
    "device-config" = @{
        label = "Device configuration"
        analyzer = $true
        includes = @("firmware/app", "firmware/drivers/radio")
        sources = @(
            "tests/device_config_test.c",
            "firmware/app/device_config.c"
        )
    }
    "config-storage" = @{
        label = "Device configuration storage"
        analyzer = $true
        defines = @("DEVICE_CONFIG_STORAGE_HOST_TEST")
        includes = @("firmware/app", "firmware/drivers/radio")
        sources = @(
            "tests/device_config_storage_test.c",
            "firmware/app/device_config.c",
            "firmware/app/device_config_storage.c"
        )
    }
    "swd-tunnel" = @{
        label = "SWD tunnel protocol"
        analyzer = $true
        compile_flags = @("-ffunction-sections", "-fdata-sections")
        link_flags = @("-Wl,--gc-sections")
        includes = @(
            "firmware/app",
            "firmware/bsp",
            "firmware/drivers/swd"
        )
        sources = @(
            "tests/swd_tunnel_protocol_test.c",
            "firmware/app/swd_tunnel.c"
        )
    }
    "link-adaptation" = @{
        label = "Link adaptation"
        includes = @("firmware/app", "firmware/drivers/radio")
        sources = @(
            "tests/link_adaptation_test.c",
            "firmware/app/link_adaptation.c"
        )
    }
    "sx1281" = @{
        label = "SX1281 driver"
        includes = @("firmware/drivers/radio")
        sources = @(
            "tests/sx128x_driver_test.c",
            "firmware/drivers/radio/sx128x.c"
        )
    }
    "usb-descriptor" = @{
        label = "USB composite descriptor"
        analyzer = $true
        defines = @("GD32F30X_HD")
        includes = @("firmware/app", "firmware/bsp", "firmware/usb")
        system_includes = @(
            "vendor/GD32_CMSIS",
            "vendor/GD32_CMSIS/GD/GD32F30x/Include",
            "vendor/GD32F30x_standard_peripheral/Include",
            "vendor/GD32F30x_usbd_library/device/Include",
            "vendor/GD32F30x_usbd_library/usbd/Include",
            "vendor/GD32F30x_usbd_library/class/device/msc/Include",
            "vendor/GD32F30x_usbd_library/class/device/cdc/Include"
        )
        sources = @(
            "tests/usb_composite_descriptor_test.c",
            "firmware/usb/usb_composite.c",
            "firmware/usb/usb_standard_request.c",
            "firmware/usb/usb_vendor_request.c"
        )
    }
    "usb-disk" = @{
        label = "USB disk geometry"
        includes = @("firmware/usb")
        sources = @("tests/usb_disk_geometry_test.c")
    }
}

$compilerCommand = Get-Command $Compiler -ErrorAction SilentlyContinue
if ($null -eq $compilerCommand) {
    throw "Host C compiler not found: $Compiler"
}
New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null

$selectedTests = if ($Name -eq "all") {
    @($tests.Keys)
} else {
    @($Name)
}

foreach ($testName in $selectedTests) {
    $test = $tests[$testName]
    $executableName = $testName
    if ($isWindowsHost) {
        $executableName += ".exe"
    }
    $testExecutable = Join-Path $buildRoot $executableName
    $arguments = @("-std=c11", "-Wall", "-Wextra", "-Werror")
    if ($test.analyzer) {
        $arguments += "-fanalyzer"
    }
    if ($test.ContainsKey("compile_flags")) {
        $arguments += $test.compile_flags
    }
    if ($test.ContainsKey("defines")) {
        $arguments += @($test.defines | ForEach-Object { "-D$_" })
    }
    $arguments += @($test.includes | ForEach-Object {
        "-I$(Repo-Path $_)"
    })
    if ($test.ContainsKey("system_includes")) {
        foreach ($include in $test.system_includes) {
            $arguments += @("-isystem", (Repo-Path $include))
        }
    }
    $arguments += @($test.sources | ForEach-Object { Repo-Path $_ })
    if ($test.ContainsKey("link_flags")) {
        $arguments += $test.link_flags
    }
    $arguments += @("-o", $testExecutable)

    & $compilerCommand.Source @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$($test.label) test compilation failed"
    }
    & $testExecutable
    if ($LASTEXITCODE -ne 0) {
        throw "$($test.label) tests failed"
    }
    Write-Host "$($test.label) tests passed"
}
