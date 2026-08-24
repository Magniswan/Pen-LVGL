[CmdletBinding(SupportsShouldProcess, ConfirmImpact = "High")]
param(
    [string]$Adb = "adb",
    [switch]$Force
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "device_app_common.ps1")
$DeviceAppId = "8080992608050002"
$DeviceAppName = "LVGL $([char]0x7BA1)$([char]0x7406)$([char]0x5668)"
Set-DeviceAdb -Path $Adb
Assert-OneDevice

if (-not $Force -and -not $PSCmdlet.ShouldProcess(
    "Falcon AppID $DeviceAppId", "Uninstall only the LVGL platform manager AMR")) {
    return
}
$manifest = Get-InstalledManifest
if ($null -ne $manifest) {
    if ($manifest.appName -ne $DeviceAppName) {
        throw "Refusing to uninstall AppID $DeviceAppId owned by '$($manifest.appName)'"
    }
    Invoke-DeviceShell -Command "miniapp_cli uninstall $DeviceAppId"
    if ($null -ne (Get-InstalledManifest)) { throw "Falcon manager package is still installed" }
}
Write-Output "MANAGER_UNINSTALL_PASS appid=$DeviceAppId"
Write-Output "No platform payload, policy state, process, launcher, or Falcon component was removed."
