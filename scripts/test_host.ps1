# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2025 RinStel <me@rinx.nz>
# 使用严格警告编译并执行每个主机回归；all 是 verify_release.ps1 使用的软件门禁。
$Name = "all"
$CompilerName = "gcc"
for ($argumentIndex = 0; $argumentIndex -lt $args.Count; $argumentIndex++) {
    switch ($args[$argumentIndex]) {
        "-Name" {
            if ($argumentIndex + 1 -ge $args.Count) { throw "-Name requires a value" }
            $Name = [string]$args[++$argumentIndex]
        }
        "-Compiler" {
            if ($argumentIndex + 1 -ge $args.Count) { throw "-Compiler requires a value" }
            $CompilerName = [string]$args[++$argumentIndex]
        }
        default { throw "Unknown argument: $($args[$argumentIndex])" }
    }
}

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $repoRoot "build/host-tests"
$isWindowsHost = [System.Environment]::OSVersion.Platform -eq `
    [System.PlatformID]::Win32NT

function Repo-Path([string]$relativePath) {
    return Join-Path $repoRoot $relativePath
}

$tests = [ordered]@{
    "target-swd-protocol" = @{
        label = "Target SWD Arm protocol"
        script = $true
        script_path = "tests/target_swd_protocol_test.ps1"
    }
    "usb-config-dfu-entry" = @{
        label = "MSC DFU entry"
        includes = @("firmware/update")
        sources = @("tests/usb_config_dfu_entry_test.c",
                    "firmware/update/dfu_config_command.c")
    }
    "boot-led" = @{
        label = "Bootloader LED"
        includes = @("firmware/bootloader")
        sources = @("tests/boot_led_test.c",
                    "firmware/bootloader/boot_led.c")
    }
    "boot-runtime" = @{
        label = "Bootloader runtime policy"
        includes = @("firmware/update", "firmware/bootloader")
        sources = @("tests/boot_runtime_test.c",
                    "firmware/bootloader/boot_policy.c",
                    "firmware/bootloader/boot_led.c")
    }
    "app-boot-confirm" = @{
        label = "Application boot confirmation"
        includes = @("firmware/update")
        sources = @("tests/app_boot_confirm_test.c",
                    "firmware/update/boot_confirm_once.c")
    }
    "dfu-device" = @{
        label = "DFU device state machine"
        defines = @("BOOT_STATE_HOST_TEST")
        includes = @("firmware/update", "firmware/bootloader")
        sources = @("tests/dfu_device_test.c",
                    "firmware/bootloader/dfu_device.c",
                    "firmware/bootloader/dfu_flash.c",
                    "firmware/update/boot_state.c",
                    "firmware/update/firmware_image.c")
    }
    "dfu-usb-descriptor" = @{
        label = "DFU WinUSB descriptor"
        analyzer = $true
        defines = @("BOOT_STATE_HOST_TEST", "DFU_DEVICE_USB_TARGET",
                    "GD32F30X_HD")
        includes = @("firmware/update", "firmware/bootloader", "firmware/usb",
                     "firmware/bsp")
        system_includes = @(
            "vendor/GD32_CMSIS",
            "vendor/GD32_CMSIS/GD/GD32F30x/Include",
            "vendor/GD32F30x_standard_peripheral/Include",
            "vendor/GD32F30x_usbd_library/device/Include",
            "vendor/GD32F30x_usbd_library/usbd/Include"
        )
        sources = @("tests/dfu_usb_descriptor_test.c",
                    "firmware/bootloader/dfu_device.c")
    }
    "dfu-flash" = @{
        label = "DFU flash transaction"
        defines = @("BOOT_STATE_HOST_TEST")
        includes = @("firmware/update", "firmware/bootloader")
        sources = @("tests/dfu_flash_test.c",
                    "firmware/bootloader/dfu_flash.c",
                    "firmware/update/boot_state.c",
                    "firmware/update/firmware_image.c")
    }
    "boot-policy" = @{
        label = "Boot policy"
        includes = @("firmware/update", "firmware/bootloader")
        sources = @("tests/boot_policy_test.c",
                    "firmware/bootloader/boot_policy.c")
    }
    "boot-mailbox" = @{
        label = "Boot mailbox"
        defines = @("BOOT_MAILBOX_HOST_TEST")
        includes = @("firmware/update")
        sources = @("tests/boot_mailbox_test.c",
                    "firmware/update/boot_mailbox.c")
    }
    "boot-state" = @{
        label = "Boot state journal"
        defines = @("BOOT_STATE_HOST_TEST")
        includes = @("firmware/update")
        sources = @("tests/boot_state_test.c",
                    "firmware/update/boot_state.c",
                    "firmware/update/firmware_image.c")
    }
    "firmware-image" = @{
        label = "Firmware image validation"
        includes = @("firmware/update")
        sources = @("tests/firmware_image_test.c",
                    "firmware/update/firmware_image.c")
    }
    "board-pins" = @{
        label = "Board pin mapping"
        defines = @("GD32F30X_HD")
        includes = @("firmware/bsp", "firmware/usb")
        system_includes = @(
            "vendor/GD32_CMSIS",
            "vendor/GD32_CMSIS/GD/GD32F30x/Include",
            "vendor/GD32F30x_standard_peripheral/Include"
        )
        sources = @("tests/board_pins_test.c")
    }
    "status-indicator" = @{
        label = "Status indicator"
        includes = @("firmware/app")
        sources = @(
            "tests/status_indicator_test.c",
            "firmware/app/status_indicator.c"
        )
    }
    "cmsis-dap" = @{
        label = "CMSIS-DAP protocol"
        defines = @("CMSIS_DAP_DIAGNOSTICS_ENABLE=1")
        includes = @(
            "firmware/app",
            "firmware/bsp",
            "firmware/drivers/radio",
            "firmware/drivers/swd"
        )
        sources = @(
            "tests/cmsis_dap_protocol_test.c",
            "firmware/app/cmsis_dap.c",
            "firmware/app/dap_diagnostics.c"
        )
    }
    "target-swd-config" = @{
        label = "Target SWD configuration"
        includes = @("firmware/drivers/swd")
        sources = @("tests/target_swd_config_test.c")
    }
    "cdc-transport" = @{
        label = "CDC ACM transport"
        defines = @("GD32F30X_HD")
        includes = @("firmware/bsp", "firmware/usb", "firmware/app")
        system_includes = @(
            "vendor/GD32_CMSIS",
            "vendor/GD32_CMSIS/GD/GD32F30x/Include",
            "vendor/GD32F30x_standard_peripheral/Include",
            "vendor/GD32F30x_usbd_library/device/Include",
            "vendor/GD32F30x_usbd_library/usbd/Include",
            "vendor/GD32F30x_usbd_library/class/device/cdc/Include"
        )
        sources = @("tests/cdc_acm_transport_test.c")
    }
    "cmsis-dap-usb" = @{
        label = "CMSIS-DAP USB transport"
        defines = @("GD32F30X_HD")
        includes = @("firmware/app", "firmware/bsp", "firmware/usb")
        system_includes = @(
            "vendor/GD32_CMSIS",
            "vendor/GD32_CMSIS/GD/GD32F30x/Include",
            "vendor/GD32F30x_standard_peripheral/Include",
            "vendor/GD32F30x_usbd_library/device/Include",
            "vendor/GD32F30x_usbd_library/usbd/Include"
        )
        sources = @("tests/cmsis_dap_usb_transport_test.c")
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
    "serial-bridge-window" = @{
        label = "Serial bridge DATA window"
        includes = @("firmware/app")
        sources = @(
            "tests/serial_bridge_window_test.c",
            "firmware/app/radio_window.c"
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
    "swd-bridge-service" = @{
        label = "SWD bridge service"
        compile_flags = @("-ffunction-sections", "-fdata-sections")
        link_flags = @("-Wl,--gc-sections")
        includes = @(
            "firmware/app",
            "firmware/drivers/radio",
            "firmware/drivers/swd"
        )
        sources = @(
            "tests/swd_bridge_service_test.c",
            "firmware/app/swd_bridge_service.c"
        )
    }
    "target-uart-ring" = @{
        label = "Target UART ring"
        includes = @("firmware/drivers/serial")
        sources = @(
            "tests/target_uart_ring_test.c",
            "firmware/drivers/serial/target_uart_ring.c"
        )
    }
    "target-uart-irq" = @{
        label = "Target UART IRQ"
        defines = @("GD32F30X_HD")
        compile_flags = @(
            "-ffunction-sections",
            "-fdata-sections",
            "-Wno-pointer-to-int-cast"
        )
        link_flags = @("-Wl,--gc-sections")
        includes = @(
            "firmware/bsp",
            "firmware/drivers/serial"
        )
        system_includes = @(
            "vendor/GD32_CMSIS",
            "vendor/GD32_CMSIS/GD/GD32F30x/Include",
            "vendor/GD32F30x_standard_peripheral/Include"
        )
        sources = @(
            "tests/target_uart_irq_test.c",
            "firmware/drivers/serial/target_uart_ring.c"
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
        includes = @("firmware/app", "firmware/bsp", "firmware/usb",
                     "firmware/drivers/radio")
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
    "usb-msc-class" = @{
        label = "USB MSC class adapter"
        defines = @("GD32F30X_HD")
        includes = @("firmware/bsp", "firmware/usb")
        system_includes = @(
            "vendor/GD32_CMSIS",
            "vendor/GD32_CMSIS/GD/GD32F30x/Include",
            "vendor/GD32F30x_standard_peripheral/Include",
            "vendor/GD32F30x_usbd_library/device/Include",
            "vendor/GD32F30x_usbd_library/usbd/Include",
            "vendor/GD32F30x_usbd_library/class/device/msc/Include"
        )
        sources = @("tests/usb_msc_class_test.c",
                    "firmware/usb/usb_msc_class.c")
    }
    "usb-msc-scsi" = @{
        label = "USB MSC SCSI slicing"
        defines = @("GD32F30X_HD")
        includes = @("firmware/bsp", "firmware/usb")
        system_includes = @(
            "vendor/GD32_CMSIS",
            "vendor/GD32_CMSIS/GD/GD32F30x/Include",
            "vendor/GD32F30x_standard_peripheral/Include",
            "vendor/GD32F30x_usbd_library/device/Include",
            "vendor/GD32F30x_usbd_library/usbd/Include",
            "vendor/GD32F30x_usbd_library/class/device/msc/Include"
        )
        sources = @("tests/usb_msc_scsi_test.c",
                    "firmware/usb/usb_msc_scsi.c")
    }
}

$compilerCommand = Get-Command $CompilerName -ErrorAction SilentlyContinue
if ($null -eq $compilerCommand) {
    throw "Host C compiler not found: $CompilerName"
}
$compilerPath = $compilerCommand.Source
if ([string]::IsNullOrWhiteSpace($compilerPath)) {
    $compilerPath = $compilerCommand.Definition
}
if ([string]::IsNullOrWhiteSpace($compilerPath)) {
    throw "Unable to resolve host C compiler path: $CompilerName"
}
# GCC may need sibling DLLs and cc1.exe that are not found unless the
# directory containing the selected executable is on PATH.
if ($isWindowsHost) {
    $compilerDirectory = Split-Path -Parent $compilerPath
    $env:PATH = "$compilerDirectory;$env:PATH"
}
New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null

$selectedTests = if ($Name -eq "all") {
    @($tests.Keys)
} else {
    @($Name)
}

if (($Name -eq "all") -or ($Name -eq "startup-sequence")) {
    & (Join-Path $repoRoot "tests/startup_sequence_test.ps1") -RepoRoot $repoRoot
    if (-not $?) {
        throw "Startup sequence tests failed"
    }
    if ($Name -eq "startup-sequence") {
        exit 0
    }
    $selectedTests = @($selectedTests | Where-Object {
        $_ -ne "startup-sequence"
    })
}

foreach ($testName in $selectedTests) {
    $test = $tests[$testName]
    if ($test.ContainsKey("script") -and $test.script) {
        & (Repo-Path $test.script_path)
        if (-not $?) {
            throw "$($test.label) test failed"
        }
        continue
    }
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

    & $compilerPath @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$($test.label) test compilation failed"
    }
    & $testExecutable
    if ($LASTEXITCODE -ne 0) {
        throw "$($test.label) tests failed"
    }
    Write-Host "$($test.label) tests passed"
}
