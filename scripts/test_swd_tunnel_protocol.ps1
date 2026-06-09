$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$testExe = Join-Path $repoRoot "build\swd_tunnel_protocol_test.exe"

New-Item -ItemType Directory -Force -Path (Split-Path $testExe) |
    Out-Null

& gcc -std=c11 -Wall -Wextra -Werror -fanalyzer -ffunction-sections `
    -fdata-sections "-Wl,--gc-sections" `
    "-I$repoRoot\firmware\app" `
    "-I$repoRoot\firmware\bsp" `
    "-I$repoRoot\firmware\drivers\swd" `
    "$repoRoot\tests\swd_tunnel_protocol_test.c" `
    "$repoRoot\firmware\app\swd_tunnel.c" `
    -o $testExe
if ($LASTEXITCODE -ne 0) {
    throw "SWD tunnel protocol test compilation failed"
}

& $testExe
if ($LASTEXITCODE -ne 0) {
    throw "SWD tunnel protocol tests failed"
}

Write-Host "SWD tunnel protocol tests passed"
