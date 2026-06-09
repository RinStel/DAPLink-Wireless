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
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$testExe = Join-Path $repoRoot "build\cmsis_dap_protocol_test.exe"

New-Item -ItemType Directory -Force -Path (Split-Path $testExe) |
    Out-Null

& gcc -std=c11 -Wall -Wextra -Werror `
    "-I$repoRoot\firmware\app" `
    "-I$repoRoot\firmware\bsp" `
    "-I$repoRoot\firmware\drivers\radio" `
    "-I$repoRoot\firmware\drivers\swd" `
    "$repoRoot\tests\cmsis_dap_protocol_test.c" `
    "$repoRoot\firmware\app\cmsis_dap.c" `
    -o $testExe
if ($LASTEXITCODE -ne 0) {
    throw "CMSIS-DAP protocol test compilation failed"
}

& $testExe
if ($LASTEXITCODE -ne 0) {
    throw "CMSIS-DAP protocol tests failed"
}

Write-Host "CMSIS-DAP protocol tests passed"
