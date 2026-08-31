[CmdletBinding()]
param(
    [string]$Adb = "adb",
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9._:-]{1,128}$')][string]$Serial
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "device_app_common.ps1")
Set-DeviceTarget -Path $Adb -Serial $Serial
Assert-DeviceTarget

$abi = Get-DeviceText -Command "uname -m"
$installed = Get-InstalledManifest
$state = Get-DeviceText -Command "if [ -f /run/lvgl-platform/session.status ]; then cat /run/lvgl-platform/session.status; fi"
$processes = Get-DeviceText -Command "ps | grep -E 'lvgl-sessiond|/usr/bin/miniapp' | grep -v grep"

Write-Output "abi=$abi"
Write-Output "launcher_installed=$($null -ne $installed) appid=$DeviceAppId"
Write-Output "session_status:"
Write-Output $state
Write-Output "observed_processes:"
Write-Output $processes
Write-Output "Status collection is read-only and never signals Falcon or LVGL processes."
