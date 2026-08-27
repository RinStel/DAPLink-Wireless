# SPDX-License-Identifier: GPL-3.0-or-later
# Stable entry point for the partitioned GCC firmware build.
$ToolchainBin = ""
$Configuration = "Debug"
for ($argumentIndex = 0; $argumentIndex -lt $args.Count; $argumentIndex++) {
    switch ($args[$argumentIndex]) {
        "-ToolchainBin" { $ToolchainBin = [string]$args[++$argumentIndex] }
        "-Configuration" { $Configuration = [string]$args[++$argumentIndex] }
        default { throw "Unknown argument: $($args[$argumentIndex])" }
    }
}

$ErrorActionPreference = "Stop"
$targetBuilder = Join-Path $PSScriptRoot "build_gcc_targets.ps1"
& $targetBuilder -ToolchainBin $ToolchainBin -Configuration $Configuration
if ($LASTEXITCODE -ne 0) { throw "GCC target build failed" }
