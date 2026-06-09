param(
    [string]$Compiler = "gcc"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$testExe = Join-Path $repoRoot "build\usb_disk_geometry_test.exe"

New-Item -ItemType Directory -Force -Path (Split-Path $testExe) | Out-Null

& $Compiler `
    "-std=c11" "-Wall" "-Wextra" "-Werror" `
    "-I$repoRoot\firmware\usb" `
    "$repoRoot\tests\usb_disk_geometry_test.c" `
    "-o" $testExe
if ($LASTEXITCODE -ne 0) {
    throw "USB disk geometry test compilation failed"
}

& $testExe
if ($LASTEXITCODE -ne 0) {
    throw "USB disk geometry tests failed"
}

Write-Host "USB disk geometry tests passed"
