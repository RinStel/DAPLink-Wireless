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
