[CmdletBinding(SupportsShouldProcess, ConfirmImpact = "High")]
param(
    [string]$Adb = "adb",
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9._:-]{1,128}$')][string]$Serial,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "device_app_common.ps1")
Set-DeviceTarget -Path $Adb -Serial $Serial
Assert-DeviceTarget

if (-not $Force -and -not $PSCmdlet.ShouldProcess(
    "Falcon AppID $DeviceAppId", "Uninstall only the LVGL Falcon launcher AMR")) {
    return
}

$manifest = Get-InstalledManifest
if ($null -ne $manifest) {
    if ($manifest.appName -ne $DeviceAppName) {
        throw "Refusing to uninstall AppID $DeviceAppId owned by '$($manifest.appName)'"
    }
    Invoke-DeviceShell -Command "miniapp_cli uninstall $DeviceAppId"
    if ($null -ne (Get-InstalledManifest)) { throw "Falcon launcher package is still installed" }
}

Write-Output "UNINSTALL_PASS appid=$DeviceAppId"
Write-Output "No LVGL platform payload, application data, process, or Falcon component was removed."
