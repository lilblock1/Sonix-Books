param(
    [string]$Adb = 'adb',
    [string]$DeviceSerial,
    [string]$OutputPath = './r1-device-inspection.json'
)

$ErrorActionPreference = 'Stop'
# Read-only device inspection. No push, shell writes, reboot, recovery command
# or flash operation is issued. Enable ADB on the R1 yourself before running.
$connected = & $Adb devices
if ($LASTEXITCODE -ne 0) { throw 'ADB did not start or could not list devices.' }
$readyDevices = @($connected | Where-Object { $_ -match '^\S+\s+device\s*$' } | ForEach-Object { ($_ -split '\s+')[0] })
if (-not $DeviceSerial) {
    if ($readyDevices.Count -ne 1) { throw 'Connect exactly one authorized device, or specify -DeviceSerial.' }
    $DeviceSerial = $readyDevices[0]
}
if ($DeviceSerial -notin $readyDevices) { throw 'The selected device is not authorized and ready in ADB.' }

function Read-R1Command([string]$Command) {
    $lines = & $Adb -s $DeviceSerial shell $Command
    if ($LASTEXITCODE -ne 0) { throw "Device read failed: $Command" }
    return ($lines -join "`n").Trim()
}

$systemInfo = Read-R1Command 'cat /usr/resource/sonix/components/system-info.json' | ConvertFrom-Json
if ($systemInfo.'device-name' -ne 'HiBy R1') { throw 'Device identity is not HiBy R1; no more checks will run.' }
$report = [ordered]@{
    inspected_utc = [DateTime]::UtcNow.ToString('o')
    device = $systemInfo.'device-name'
    version = $systemInfo.OS_version
    build = $systemInfo.build_version
    kernel = Read-R1Command 'uname -a'
    partitions = Read-R1Command 'cat /proc/mtd'
    player_sha256 = Read-R1Command 'sha256sum /usr/bin/sonix_player'
    battery = Read-R1Command 'for p in /sys/class/power_supply/*/capacity; do if [ -f "$p" ]; then cat "$p"; fi; done'
    flash_approved = $false
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $OutputPath -Encoding utf8
Write-Output "Read-only inspection saved to $OutputPath. No device files or partitions were written."
