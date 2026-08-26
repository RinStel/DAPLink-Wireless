param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot
)

$ErrorActionPreference = "Stop"

function Require-Before([string]$text, [string]$first, [string]$second,
                         [string]$description) {
    $firstIndex = $text.IndexOf($first, [System.StringComparison]::Ordinal)
    $secondIndex = $text.IndexOf($second, [System.StringComparison]::Ordinal)
    if (($firstIndex -lt 0) -or ($secondIndex -lt 0) -or
        ($firstIndex -ge $secondIndex)) {
        throw "Startup sequence assertion failed: $description"
    }
}

$main = Get-Content -Raw (Join-Path $RepoRoot "firmware/app/main.c")
$serialBridge = Get-Content -Raw (
    Join-Path $RepoRoot "firmware/app/serial_bridge.c")

Require-Before $main "device_config_init();" "usb_config_disk_init();" `
    "device configuration must be loaded before the USB disk snapshot"
Require-Before $main "usb_config_disk_process();" "serial_bridge_process();" `
    "USB processing must run before wireless bridge processing"

${initStart} = $serialBridge.IndexOf("bool serial_bridge_init(void)",
    [System.StringComparison]::Ordinal)
${applyStart} = $serialBridge.IndexOf("bool serial_bridge_apply_config(void)",
    [System.StringComparison]::Ordinal)
if (($initStart -lt 0) -or ($applyStart -le $initStart)) {
    throw "Startup sequence assertion failed: serial bridge init boundaries are missing"
}
$initBody = $serialBridge.Substring($initStart, $applyStart - $initStart)
if ($initBody -match "s_radio_ready\s*=\s*radio_configure\(") {
    throw "Startup sequence assertion failed: serial_bridge_init must not synchronously configure the radio"
}

Write-Host "Startup sequence tests passed"
