# SPDX-License-Identifier: GPL-3.0-or-later
# Flash one address-bearing GCC artifact through pyOCD.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Artifact,
    [string]$Target = "stm32f103rc",
    [string]$Probe = "",
    [string]$Frequency = "1M",
    [ValidateSet("halt", "pre-reset", "under-reset", "attach")]
    [string]$Connect = "under-reset",
    [ValidateSet("auto", "chip", "sector")]
    [string]$Erase = "sector",
    [switch]$NoReset,
    [switch]$WhatIf
)

$ErrorActionPreference = "Stop"

$artifactPath = (Resolve-Path -LiteralPath $Artifact -ErrorAction Stop).Path
$artifactName = Split-Path -Leaf $artifactPath
$artifactStem = [IO.Path]::GetFileNameWithoutExtension($artifactName)
$artifactExtension = [IO.Path]::GetExtension($artifactName).ToLowerInvariant()
$allowedStems = @(
    "daplink_factory",
    "daplink_bootloader",
    "daplink_slot_a",
    "daplink_slot_b",
    "daplink_wireless"
)
if (($allowedStems -notcontains $artifactStem) -or
    ($artifactExtension -notin @(".hex", ".elf"))) {
    throw "Artifact must be a supported firmware artifact (.hex or .elf): $artifactName"
}

$arguments = @(
    "load",
    "--no-wait",
    "--target", $Target,
    "--frequency", $Frequency,
    "--connect", $Connect,
    "--erase", $Erase
)
if (-not [string]::IsNullOrWhiteSpace($Probe)) {
    $arguments += @("--probe", $Probe)
}
if ($NoReset) {
    $arguments += "--no-reset"
}
$arguments += $artifactPath

Write-Host ("pyocd " + ($arguments -join " "))
if ($WhatIf) {
    Write-Host "WhatIf: no probe connection or Flash write was attempted."
    exit 0
}

$pyocd = Get-Command pyocd -ErrorAction SilentlyContinue
if ($null -eq $pyocd) {
    throw "pyocd is not installed or is not available on PATH"
}

& $pyocd.Source @arguments
if ($LASTEXITCODE -ne 0) {
    throw "pyocd flash failed with exit code $LASTEXITCODE"
}
Write-Host "pyocd flash completed and pyOCD verification passed."
