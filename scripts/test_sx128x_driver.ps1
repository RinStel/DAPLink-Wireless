$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$testExe = Join-Path $repoRoot "build\sx128x_driver_test.exe"

New-Item -ItemType Directory -Force -Path (Split-Path $testExe) |
    Out-Null

& gcc -std=c11 -Wall -Wextra -Werror `
    "-I$repoRoot\firmware\drivers\radio" `
    "$repoRoot\tests\sx128x_driver_test.c" `
    "$repoRoot\firmware\drivers\radio\sx128x.c" `
    -o $testExe
if ($LASTEXITCODE -ne 0) {
    throw "SX1281 driver test compilation failed"
}

& $testExe
if ($LASTEXITCODE -ne 0) {
    throw "SX1281 driver tests failed"
}

Write-Host "SX1281 driver tests passed"
