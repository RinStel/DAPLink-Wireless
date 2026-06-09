param(
    [string]$Compiler = "gcc"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$testExe = Join-Path $repoRoot "build\usb_composite_descriptor_test.exe"

New-Item -ItemType Directory -Force -Path (Split-Path $testExe) | Out-Null

$includes = @(
    "-I$repoRoot\firmware\app",
    "-I$repoRoot\firmware\bsp",
    "-I$repoRoot\firmware\usb",
    "-isystem$repoRoot\vendor\GD32_CMSIS",
    "-isystem$repoRoot\vendor\GD32_CMSIS\GD\GD32F30x\Include",
    "-isystem$repoRoot\vendor\GD32F30x_standard_peripheral\Include",
    "-isystem$repoRoot\vendor\GD32F30x_usbd_library\device\Include",
    "-isystem$repoRoot\vendor\GD32F30x_usbd_library\usbd\Include",
    "-isystem$repoRoot\vendor\GD32F30x_usbd_library\class\device\msc\Include",
    "-isystem$repoRoot\vendor\GD32F30x_usbd_library\class\device\cdc\Include"
)

& $Compiler -std=c11 -Wall -Wextra -Werror -fanalyzer `
    -DGD32F30X_HD @includes `
    "$repoRoot\tests\usb_composite_descriptor_test.c" `
    "$repoRoot\firmware\usb\usb_composite.c" `
    "$repoRoot\firmware\usb\usb_standard_request.c" `
    "$repoRoot\firmware\usb\usb_vendor_request.c" `
    -o $testExe
if ($LASTEXITCODE -ne 0) {
    throw "USB composite descriptor test compilation failed"
}

& $testExe
if ($LASTEXITCODE -ne 0) {
    throw "USB composite descriptor tests failed"
}

Write-Host "USB composite descriptor tests passed"
