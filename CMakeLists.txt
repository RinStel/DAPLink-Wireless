cmake_minimum_required(VERSION 3.21)
project(DAPLinkWireless LANGUAGES C ASM)

option(CMSIS_DAP_DIAGNOSTICS "Enable CMSIS-DAP timing diagnostics" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_FLAGS_DEBUG "-Og -g3" CACHE STRING "Debug C flags" FORCE)
set(CMAKE_C_FLAGS_RELEASE "-Os" CACHE STRING "Release C flags" FORCE)

set(FIRMWARE_CPU_FLAGS -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)
set(FIRMWARE_COMMON_DEFINES GD32F30X_HD HXTAL_VALUE=8000000U)
set(FIRMWARE_INCLUDE_DIRS
    firmware/app firmware/bsp firmware/update firmware/bootloader
    firmware/drivers/radio firmware/drivers/serial firmware/drivers/swd firmware/usb
    vendor/GD32_CMSIS vendor/GD32_CMSIS/GD/GD32F30x/Include
    vendor/GD32F30x_standard_peripheral/Include
    vendor/GD32F30x_usbd_library/device/Include
    vendor/GD32F30x_usbd_library/usbd/Include
    vendor/GD32F30x_usbd_library/class/device/msc/Include
    vendor/GD32F30x_usbd_library/class/device/cdc/Include)

set(FIRMWARE_VENDOR_PERIPHERAL
    vendor/GD32F30x_standard_peripheral/Source/gd32f30x_gpio.c
    vendor/GD32F30x_standard_peripheral/Source/gd32f30x_rcu.c
    vendor/GD32F30x_standard_peripheral/Source/gd32f30x_spi.c
    vendor/GD32F30x_standard_peripheral/Source/gd32f30x_fmc.c
    vendor/GD32F30x_standard_peripheral/Source/gd32f30x_fwdgt.c
    vendor/GD32F30x_standard_peripheral/Source/gd32f30x_misc.c
    vendor/GD32F30x_standard_peripheral/Source/gd32f30x_usart.c
    vendor/GD32F30x_standard_peripheral/Source/gd32f30x_dma.c
    vendor/GD32F30x_standard_peripheral/Source/gd32f30x_bkp.c
    vendor/GD32F30x_standard_peripheral/Source/gd32f30x_pmu.c)
set(FIRMWARE_VENDOR_USB
    vendor/GD32F30x_usbd_library/device/Source/usbd_core.c
    vendor/GD32F30x_usbd_library/device/Source/usbd_enum.c
    vendor/GD32F30x_usbd_library/device/Source/usbd_pwr.c
    vendor/GD32F30x_usbd_library/device/Source/usbd_transc.c
    vendor/GD32F30x_usbd_library/usbd/Source/usbd_lld_core.c
    vendor/GD32F30x_usbd_library/usbd/Source/usbd_lld_int.c)
set(FIRMWARE_MSC
    # 厂商 SCSI 层不编译：它把 512 字节的 media packet 整块交给 usbd_ep_send，
    # 而驱动不按 PMA 槽夹取，会踩过 CDC/DAP 缓冲并越出 PMA。BOT 状态机仍用厂商
    # usbd_msc_bbb.c（CBW 31 字节、CSW 13 字节，均在单个事务内），SCSI 命令与
    # 分片由 firmware/usb/usb_msc_scsi.c 承担。
    vendor/GD32F30x_usbd_library/class/device/msc/Source/usbd_msc_bbb.c)
set(FIRMWARE_STARTUP
    firmware/toolchain/gcc/syscalls.c
    vendor/GD32_CMSIS/GD/GD32F30x/Source/system_gd32f30x.c
    vendor/GD32_CMSIS/GD/GD32F30x/Source/GCC/startup_gd32f30x_hd.S)
set(FIRMWARE_APPLICATION
    firmware/app/main.c firmware/app/cmsis_dap.c firmware/app/dap_diagnostics.c firmware/app/frequency_hopping.c
    firmware/app/link_adaptation.c firmware/app/radio_protocol.c firmware/app/radio_window.c
    firmware/app/serial_service.c firmware/app/serial_bridge.c firmware/app/status_indicator.c
    firmware/app/swd_bridge_service.c firmware/app/swd_tunnel.c firmware/app/device_config.c
    firmware/app/device_config_storage.c firmware/update/boot_confirm_once.c
    firmware/update/boot_mailbox.c firmware/update/boot_state.c firmware/update/firmware_image.c
    firmware/update/dfu_config_command.c firmware/usb/usb_config_disk.c
    firmware/usb/usb_composite.c firmware/usb/usb_msc_class.c
    firmware/usb/usb_msc_scsi.c firmware/usb/usb_standard_request.c
    firmware/usb/usb_vendor_request.c firmware/usb/cdc_acm_transport.c
    firmware/usb/cmsis_dap_usb.c firmware/bsp/board.c firmware/drivers/radio/radio_hal.c
    firmware/drivers/radio/sx128x.c firmware/drivers/serial/target_uart.c
    firmware/drivers/serial/target_uart_ring.c firmware/drivers/swd/target_swd.c)
set(FIRMWARE_BOOTLOADER
    firmware/bootloader/main.c firmware/bootloader/boot_board.c
    firmware/bootloader/boot_led.c firmware/bootloader/boot_policy.c
    firmware/bootloader/dfu_device.c firmware/bootloader/dfu_flash.c
    firmware/update/boot_mailbox.c firmware/update/boot_state.c
    firmware/update/firmware_image.c firmware/usb/usb_standard_request.c
    firmware/usb/usb_vendor_request.c)

function(add_firmware_target target linker)
    add_executable(${target} ${ARGN})
    set_target_properties(${target} PROPERTIES PREFIX "" SUFFIX ".elf"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    target_compile_definitions(${target} PRIVATE ${FIRMWARE_COMMON_DEFINES})
    target_include_directories(${target} PRIVATE ${FIRMWARE_INCLUDE_DIRS})
    target_compile_options(${target} PRIVATE ${FIRMWARE_CPU_FLAGS}
        $<$<CONFIG:Debug>:-Og;-g3> $<$<CONFIG:Release>:-Os>
        -ffunction-sections -fdata-sections -fstack-usage -Wall -Wextra -Werror)
    target_link_options(${target} PRIVATE ${FIRMWARE_CPU_FLAGS}
        -T${CMAKE_SOURCE_DIR}/${linker} -Wl,--gc-sections
        -Wl,--fatal-warnings -Wl,--no-warn-rwx-segments
        -Wl,-Map=${CMAKE_BINARY_DIR}/${target}.map --specs=nosys.specs)
    set_source_files_properties(${FIRMWARE_VENDOR_PERIPHERAL} ${FIRMWARE_VENDOR_USB}
        ${FIRMWARE_MSC} PROPERTIES COMPILE_OPTIONS
        "-Wno-unused-parameter;-Wno-unused-but-set-variable")
    set_source_files_properties(
        vendor/GD32F30x_usbd_library/device/Source/usbd_enum.c
        PROPERTIES COMPILE_DEFINITIONS
        "usbd_standard_request=gd32_usbd_standard_request_unchecked;usbd_vendor_request=gd32_usbd_vendor_request_default")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} --gap-fill 0x00 -O ihex $<TARGET_FILE:${target}>
                ${CMAKE_BINARY_DIR}/${target}.hex
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${target}>
                ${CMAKE_BINARY_DIR}/${target}.bin
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${target}>
        VERBATIM)
endfunction()

add_firmware_target(daplink_bootloader firmware/linker/gd32f303xC_bootloader.ld
    ${FIRMWARE_BOOTLOADER} ${FIRMWARE_VENDOR_PERIPHERAL} ${FIRMWARE_VENDOR_USB}
    ${FIRMWARE_STARTUP})
target_compile_definitions(daplink_bootloader PRIVATE DFU_DEVICE_USB_TARGET)
target_compile_options(daplink_bootloader PRIVATE $<$<COMPILE_LANGUAGE:C>:-Os>)

add_firmware_target(daplink_slot_a firmware/linker/gd32f303xC_slot_a.ld
    ${FIRMWARE_APPLICATION} ${FIRMWARE_VENDOR_PERIPHERAL} ${FIRMWARE_VENDOR_USB}
    ${FIRMWARE_MSC} ${FIRMWARE_STARTUP})
target_compile_definitions(daplink_slot_a PRIVATE FIRMWARE_LINK_SLOT=0)
if(CMSIS_DAP_DIAGNOSTICS)
    target_compile_definitions(daplink_slot_a PRIVATE CMSIS_DAP_DIAGNOSTICS_ENABLE=1)
endif()
add_custom_command(TARGET daplink_slot_a POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:daplink_slot_a> ${CMAKE_BINARY_DIR}/daplink_wireless.elf
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/daplink_slot_a.hex ${CMAKE_BINARY_DIR}/daplink_wireless.hex
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/daplink_slot_a.bin ${CMAKE_BINARY_DIR}/daplink_wireless.bin
    VERBATIM)

add_firmware_target(daplink_slot_b firmware/linker/gd32f303xC_slot_b.ld
    ${FIRMWARE_APPLICATION} ${FIRMWARE_VENDOR_PERIPHERAL} ${FIRMWARE_VENDOR_USB}
    ${FIRMWARE_MSC} ${FIRMWARE_STARTUP})
target_compile_definitions(daplink_slot_b PRIVATE FIRMWARE_LINK_SLOT=1)
if(CMSIS_DAP_DIAGNOSTICS)
    target_compile_definitions(daplink_slot_b PRIVATE CMSIS_DAP_DIAGNOSTICS_ENABLE=1)
endif()
set_source_files_properties(firmware/drivers/swd/target_swd.c PROPERTIES COMPILE_OPTIONS "-O3")

find_package(Python3 COMPONENTS Interpreter REQUIRED)
file(STRINGS firmware/app/firmware_version.h _version_line REGEX "FIRMWARE_VERSION_CODE")
string(REGEX MATCH "FIRMWARE_VERSION_CODE[ \t]+([0-9]+)U" _version_match "${_version_line}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "Unable to read FIRMWARE_VERSION_CODE")
endif()
set(FIRMWARE_VERSION_CODE "${CMAKE_MATCH_1}")
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/daplink_wireless.dwup
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build_dwup.py
            --slot-a ${CMAKE_BINARY_DIR}/daplink_slot_a.bin
            --slot-b ${CMAKE_BINARY_DIR}/daplink_slot_b.bin
            --output ${CMAKE_BINARY_DIR}/daplink_wireless.dwup
            --version-code ${FIRMWARE_VERSION_CODE}
        DEPENDS daplink_slot_a daplink_slot_b VERBATIM)
    add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/daplink_factory.hex
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build_factory_hex.py
            --output ${CMAKE_BINARY_DIR}/daplink_factory.hex
            --state-image ${CMAKE_BINARY_DIR}/daplink_slot_a.bin
            --state-version-code ${FIRMWARE_VERSION_CODE}
            ${CMAKE_BINARY_DIR}/daplink_bootloader.hex
            ${CMAKE_BINARY_DIR}/daplink_slot_a.hex
        DEPENDS daplink_bootloader daplink_slot_a VERBATIM)
    add_custom_target(firmware_release_artifacts ALL
        DEPENDS ${CMAKE_BINARY_DIR}/daplink_wireless.dwup
                ${CMAKE_BINARY_DIR}/daplink_factory.hex)
endif()

if(CMAKE_HOST_WIN32)
    find_program(POWERSHELL_EXECUTABLE NAMES pwsh powershell)
    if(POWERSHELL_EXECUTABLE AND CMAKE_BUILD_TYPE STREQUAL "Release")
        set(PYOCD_TARGET "stm32f103rc" CACHE STRING
            "pyOCD target name for the GD32F303-compatible flash algorithm")
        set(PYOCD_PROBE "" CACHE STRING
            "Optional pyOCD probe UID; set this when multiple probes are connected")
        set(PYOCD_FREQUENCY "1M" CACHE STRING "pyOCD SWD frequency")
        set(PYOCD_CONNECT "under-reset" CACHE STRING
            "pyOCD connection mode: halt, pre-reset, under-reset, or attach")
        set(PYOCD_ERASE "sector" CACHE STRING
            "pyOCD erase mode; sector preserves untouched Flash regions")

        function(add_pyocd_flash_target target artifact dependency)
            set(_pyocd_arguments
                -Artifact ${CMAKE_BINARY_DIR}/${artifact}
                -Target ${PYOCD_TARGET}
                -Frequency ${PYOCD_FREQUENCY}
                -Connect ${PYOCD_CONNECT}
                -Erase ${PYOCD_ERASE})
            if(PYOCD_PROBE)
                list(APPEND _pyocd_arguments -Probe ${PYOCD_PROBE})
            endif()
            add_custom_target(${target}
                COMMAND ${POWERSHELL_EXECUTABLE}
                    -NoProfile -ExecutionPolicy Bypass
                    -File ${CMAKE_SOURCE_DIR}/scripts/flash_pyocd.ps1
                    ${_pyocd_arguments}
                DEPENDS ${dependency}
                USES_TERMINAL
                VERBATIM)
        endfunction()

        add_pyocd_flash_target(flash_factory daplink_factory.hex firmware_release_artifacts)
        add_pyocd_flash_target(flash_bootloader daplink_bootloader.hex daplink_bootloader)
        add_pyocd_flash_target(flash_slot_a daplink_slot_a.hex daplink_slot_a)
        add_pyocd_flash_target(flash_slot_b daplink_slot_b.hex daplink_slot_b)
    endif()
endif()
