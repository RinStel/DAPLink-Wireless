$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$testExe = Join-Path $repoRoot "build\device_config_test.exe"

New-Item -ItemType Directory -Force -Path (Split-Path $testExe) |
    Out-Null

& gcc -std=c11 -Wall -Wextra -Werror -fanalyzer `
    "-I$repoRoot\firmware\app" `
    "-I$repoRoot\firmware\drivers\radio" `
    "$repoRoot\tests\device_config_test.c" `
    "$repoRoot\firmware\app\device_config.c" `
    -o $testExe
if ($LASTEXITCODE -ne 0) {
    throw "Device configuration test compilation failed"
}

& $testExe
if ($LASTEXITCODE -ne 0) {
    throw "Device configuration tests failed"
}

Write-Host "Device configuration tests passed"
