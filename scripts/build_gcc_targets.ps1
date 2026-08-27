# SPDX-License-Identifier: GPL-3.0-or-later
# Compatibility entry point; CMake owns all firmware source and linker lists.
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
$preset = $Configuration.ToLowerInvariant()
if ($preset -notin @("debug", "release")) {
    throw "Configuration must be Debug or Release"
}
$cmake = (Get-Command cmake -ErrorAction Stop).Source
$root = Split-Path -Parent $PSScriptRoot
$configureArgs = @("--preset", $preset)
if (-not [string]::IsNullOrWhiteSpace($ToolchainBin)) {
    $configureArgs += @("-DARM_GCC_BIN=$ToolchainBin")
}
& $cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed for $preset" }
& $cmake "--build" "--preset" $preset
if ($LASTEXITCODE -ne 0) { throw "CMake build failed for $preset" }
