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
    [Parameter(Mandatory = $true)]
    [string]$Target,
    [string]$Probe = "",
    [string]$Frequency = "100k"
)

$ErrorActionPreference = "Stop"

if ($null -eq (Get-Command pyocd -ErrorAction SilentlyContinue)) {
    throw "pyOCD is not installed or is not available on PATH"
}

$probeList = & pyocd list --probes --no-header 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) {
    throw "pyOCD probe enumeration failed: $probeList"
}
if ($probeList -match "No available debug probes") {
    throw "No CMSIS-DAP probe is connected"
}
Write-Host $probeList.TrimEnd()

$arguments = @(
    "commander",
    "--no-wait",
    "--target", $Target,
    "--frequency", $Frequency,
    "--connect", "halt"
)
if (-not [string]::IsNullOrWhiteSpace($Probe)) {
    $arguments += @("--probe", $Probe)
}
$arguments += @(
    "--command",
    "status",
    "read32 0xE000ED00",
    "reset",
    "read32 0xE000ED00"
)

& pyocd @arguments
if ($LASTEXITCODE -ne 0) {
    throw "pyOCD hardware smoke test failed"
}

Write-Host "Hardware smoke test passed"
