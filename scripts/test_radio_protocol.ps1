$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$testExe = Join-Path $repoRoot "build\radio_protocol_test.exe"

New-Item -ItemType Directory -Force -Path (Split-Path $testExe) |
    Out-Null

& gcc -std=c11 -Wall -Wextra -Werror -fanalyzer `
    "-I$repoRoot\firmware\app" `
    "$repoRoot\tests\radio_protocol_test.c" `
    "$repoRoot\firmware\app\radio_protocol.c" `
    "$repoRoot\firmware\app\frequency_hopping.c" `
    -o $testExe
if ($LASTEXITCODE -ne 0) {
    throw "Radio protocol test compilation failed"
}

& $testExe
if ($LASTEXITCODE -ne 0) {
    throw "Radio protocol tests failed"
}

Write-Host "Radio protocol tests passed"
