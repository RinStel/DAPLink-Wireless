# SPDX-License-Identifier: GPL-3.0-or-later
# Run the software checks and build the GCC Debug/Release artifacts.
$ToolchainBin = ""
for ($argumentIndex = 0; $argumentIndex -lt $args.Count; $argumentIndex++) {
    switch ($args[$argumentIndex]) {
        "-ToolchainBin" { $ToolchainBin = [string]$args[++$argumentIndex] }
        default { throw "Unknown argument: $($args[$argumentIndex])" }
    }
}

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$powerShellExecutable = (Get-Process -Id $PID).Path

function Invoke-CheckedScript([string]$path, [string[]]$arguments = @()) {
    & $script:powerShellExecutable -NoProfile -File $path @arguments
    if ($LASTEXITCODE -ne 0) { throw "Script failed: $path" }
}

Invoke-CheckedScript (Join-Path $PSScriptRoot "verify_source_tree.ps1")
Invoke-CheckedScript (Join-Path $PSScriptRoot "verify_repository.ps1")
Invoke-CheckedScript (Join-Path $PSScriptRoot "verify_dependencies.ps1")
Invoke-CheckedScript (Join-Path $PSScriptRoot "test_host.ps1")

$python = (Get-Command python -ErrorAction Stop).Source
& $python -m unittest discover -s (Join-Path $repoRoot "tests") `
    -p "*_test.py" -v
if ($LASTEXITCODE -ne 0) { throw "Python regression suite failed" }

$buildScript = Join-Path $PSScriptRoot "build_gcc.ps1"
foreach ($configuration in @("Debug", "Release")) {
    $buildArguments = @("-Configuration", $configuration)
    if (-not [string]::IsNullOrWhiteSpace($ToolchainBin)) {
        $buildArguments += @("-ToolchainBin", $ToolchainBin)
    }
    Invoke-CheckedScript $buildScript $buildArguments
}

Invoke-CheckedScript (Join-Path $repoRoot "tests/firmware_layout_test.ps1")
Write-Host "Software verification passed"
