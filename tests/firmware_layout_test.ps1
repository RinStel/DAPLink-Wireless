param([string]$RepoRoot = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = "Stop"
$scripts = @{
    bootloader = @{ path = "firmware/linker/gd32f303xC_bootloader.ld"; origin = "0x08000000"; length = "16K" }
    slot_a = @{ path = "firmware/linker/gd32f303xC_slot_a.ld"; origin = "0x08004000"; length = "116K" }
    slot_b = @{ path = "firmware/linker/gd32f303xC_slot_b.ld"; origin = "0x08021000"; length = "116K" }
}
foreach ($item in $scripts.GetEnumerator()) {
    $file = Join-Path $RepoRoot $item.Value.path
    if (-not (Test-Path -LiteralPath $file)) { throw "Missing linker script: $file" }
    $text = Get-Content -Raw $file
    if ($text -notmatch "ORIGIN\s*=\s*$($item.Value.origin)") { throw "$($item.Key) origin mismatch" }
    if ($text -notmatch "LENGTH\s*=\s*$($item.Value.length)") { throw "$($item.Key) length mismatch" }
    if ($text -notmatch "ASSERT\s*\(") { throw "$($item.Key) lacks region assertion" }
}
Write-Host "FIRMWARE_LAYOUT_TEST=PASS"
