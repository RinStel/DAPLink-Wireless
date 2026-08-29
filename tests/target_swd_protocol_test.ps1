$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$source = Get-Content -Raw (Join-Path $repoRoot "firmware/drivers/swd/target_swd.c")

function Assert-Source([string]$pattern, [string]$message) {
    if ($source -notmatch $pattern) {
        throw $message
    }
}

# Arm SW_DP.c: ACK=OK performs configured idle cycles, then parks SWDIO high
# while still driving it; it must not release the line at transfer end.
Assert-Source `
    'swdio_write\(false\);\s*write_bits\(0U, s_idle_cycles\);[\s\S]*?swdio_write\(true\);' `
    "ACK=OK must park SWDIO high after idle cycles"
if ($source -match 'write_bits\(0U, s_idle_cycles\);\s*swdio_input\(\)') {
    throw "Transfer end must not release SWDIO after configured idle cycles"
}

# Arm SW_DP.c: WAIT/FAULT skip configured idle cycles and park high.
Assert-Source `
    'ack == TARGET_SWD_ACK_WAIT[\s\S]*?swdio_write\(true\);' `
    "WAIT/FAULT path must park SWDIO high"

# Arm SW_DP.c: protocol errors back off the full dummy data phase (33 clocks)
# in addition to turnaround before parking high.
Assert-Source `
    's_turnaround\s*\+\s*33U' `
    "Protocol error path must back off turnaround plus 33 data-phase clocks"

Write-Host "Target SWD Arm protocol comparison passed"
