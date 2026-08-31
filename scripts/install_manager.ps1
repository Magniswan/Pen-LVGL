[CmdletBinding()]
param(
    [string]$Adb = "adb",
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9._:-]{1,128}$')][string]$Serial,
    [string]$AmrPath = ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "device_app_common.ps1")
$DeviceAppId = "8080992608050002"
$DeviceAppName = "LVGL $([char]0x7BA1)$([char]0x7406)$([char]0x5668)"
Set-DeviceTarget -Path $Adb -Serial $Serial

$package = Get-Content -Raw -Encoding UTF8 (Join-Path $ProjectRoot "manager/package.json") |
    ConvertFrom-Json
$version = [string]$package.version
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw "Unsafe manager version: $version" }
if ([string]::IsNullOrWhiteSpace($AmrPath)) {
    $AmrPath = Join-Path $ProjectRoot (
        "manager/$($package.appid).$($version.Replace('.', '_')).amr")
}
$resolvedAmr = [IO.Path]::GetFullPath($AmrPath)
if (-not (Test-Path -LiteralPath $resolvedAmr -PathType Leaf)) {
    throw "Manager AMR not found: $resolvedAmr"
}

Assert-DeviceTarget
if ((Get-DeviceText -Command "uname -m") -ne "aarch64") { throw "Unsupported device ABI" }
Assert-AppIdAvailable
$hash = Get-Sha256 -Path $resolvedAmr
Invoke-DeviceAdb -Arguments @("push", $resolvedAmr, "/tmp/lvgl-manager.amr") | Out-Host
Invoke-DeviceShell -Command "miniapp_cli install /tmp/lvgl-manager.amr"
$installed = Get-InstalledManifest
if ($null -eq $installed -or $installed.appid -ne $DeviceAppId -or
    $installed.appName -ne $DeviceAppName) {
    throw "Manager AMR installation could not be verified"
}
Write-Output "MANAGER_INSTALL_PASS appid=$DeviceAppId version=$version amr_sha256=$hash"
Write-Output "This installer never writes or removes the native LVGL platform payload."
