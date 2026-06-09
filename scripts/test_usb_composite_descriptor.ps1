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
    [string]$Compiler = "gcc"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$testExe = Join-Path $repoRoot "build\usb_composite_descriptor_test.exe"

New-Item -ItemType Directory -Force -Path (Split-Path $testExe) | Out-Null

$includes = @(
    "-I$repoRoot\firmware\app",
    "-I$repoRoot\firmware\bsp",
    "-I$repoRoot\firmware\usb",
    "-isystem$repoRoot\vendor\GD32_CMSIS",
    "-isystem$repoRoot\vendor\GD32_CMSIS\GD\GD32F30x\Include",
    "-isystem$repoRoot\vendor\GD32F30x_standard_peripheral\Include",
    "-isystem$repoRoot\vendor\GD32F30x_usbd_library\device\Include",
    "-isystem$repoRoot\vendor\GD32F30x_usbd_library\usbd\Include",
    "-isystem$repoRoot\vendor\GD32F30x_usbd_library\class\device\msc\Include",
    "-isystem$repoRoot\vendor\GD32F30x_usbd_library\class\device\cdc\Include"
)

& $Compiler -std=c11 -Wall -Wextra -Werror -fanalyzer `
    -DGD32F30X_HD @includes `
    "$repoRoot\tests\usb_composite_descriptor_test.c" `
    "$repoRoot\firmware\usb\usb_composite.c" `
    "$repoRoot\firmware\usb\usb_standard_request.c" `
    "$repoRoot\firmware\usb\usb_vendor_request.c" `
    -o $testExe
if ($LASTEXITCODE -ne 0) {
    throw "USB composite descriptor test compilation failed"
}

& $testExe
if ($LASTEXITCODE -ne 0) {
    throw "USB composite descriptor tests failed"
}

Write-Host "USB composite descriptor tests passed"
