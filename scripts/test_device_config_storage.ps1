$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$testExe = Join-Path $repoRoot "build\device_config_storage_test.exe"

New-Item -ItemType Directory -Force -Path (Split-Path $testExe) |
    Out-Null

& gcc -std=c11 -Wall -Wextra -Werror -fanalyzer `
    -DDEVICE_CONFIG_STORAGE_HOST_TEST `
    "-I$repoRoot\firmware\app" `
    "-I$repoRoot\firmware\drivers\radio" `
    "$repoRoot\tests\device_config_storage_test.c" `
    "$repoRoot\firmware\app\device_config.c" `
    "$repoRoot\firmware\app\device_config_storage.c" `
    -o $testExe
if ($LASTEXITCODE -ne 0) {
    throw "Device configuration storage test compilation failed"
}

& $testExe
if ($LASTEXITCODE -ne 0) {
    throw "Device configuration storage tests failed"
}

Write-Host "Device configuration storage tests passed"
