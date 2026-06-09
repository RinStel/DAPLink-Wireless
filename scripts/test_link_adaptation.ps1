param(
    [string]$Compiler = "gcc"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$testExe = Join-Path $repoRoot "build\link_adaptation_test.exe"

New-Item -ItemType Directory -Force -Path (Split-Path $testExe) | Out-Null

& $Compiler `
    "-std=c11" "-Wall" "-Wextra" "-Werror" `
    "-I$repoRoot\firmware\app" `
    "-I$repoRoot\firmware\drivers\radio" `
    "$repoRoot\tests\link_adaptation_test.c" `
    "$repoRoot\firmware\app\link_adaptation.c" `
    "-o" $testExe
if ($LASTEXITCODE -ne 0) {
    throw "Link adaptation test compilation failed"
}

& $testExe
if ($LASTEXITCODE -ne 0) {
    throw "Link adaptation tests failed"
}

Write-Host "Link adaptation tests passed"
