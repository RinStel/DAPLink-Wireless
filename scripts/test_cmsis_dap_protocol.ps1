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
