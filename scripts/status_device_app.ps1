[CmdletBinding()]
param(
    [string]$Adb = "adb",
    [switch]$ShowLog
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "device_app_common.ps1")
Set-DeviceAdb -Path $Adb

Assert-OneDevice
$profile = Assert-TargetDevice
$installed = Get-InstalledManifest
$current = Get-DeviceText -Command "readlink '$DeviceRoot/current' 2>/dev/null || true"
$state = Get-DeviceState
$processes = Get-DeviceText -Command "ps | grep -E 'lvgl-supervisor|lvgl_poc|guardian_run.*/usr/bin/runDictPen|/usr/bin/miniapp' | grep -v grep"

Write-Output "profile=$($profile.Profile) firmware=$($profile.Firmware) pcba=$($profile.Pcba)"
Write-Output "launcher_installed=$($null -ne $installed) appid=$DeviceAppId"
Write-Output "current_release=$current"
if ($state.Count -eq 0) {
    Write-Output "state=idle"
} else {
    Write-Output "state=$($state.STATE) supervisor_pid=$($state.SUPERVISOR_PID) app_pid=$($state.APP_PID) result=$($state.RESULT) updated_at=$($state.UPDATED_AT)"
}
Write-Output "processes:"
Write-Output $processes
if ($ShowLog) {
    Write-Output "supervisor_log:"
    Invoke-DeviceShell -Command "tail -n 80 '$DeviceLogRoot/supervisor.log' 2>/dev/null || true"
}
