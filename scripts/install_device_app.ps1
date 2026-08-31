[CmdletBinding()]
param(
    [string]$Adb = "adb",
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9._:-]{1,128}$')][string]$Serial,
    [string]$AmrPath = ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "device_app_common.ps1")
Set-DeviceTarget -Path $Adb -Serial $Serial

$package = Get-Content -Raw -Encoding UTF8 (Join-Path $ProjectRoot "launcher/package.json") |
    ConvertFrom-Json
$version = [string]$package.version
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw "Unsafe launcher version: $version" }
if ([string]::IsNullOrWhiteSpace($AmrPath)) {
    $artifactVersion = $version.Replace('.', '_')
    $AmrPath = Join-Path $ProjectRoot "launcher/$($package.appid).$artifactVersion.amr"
}
$resolvedAmr = [IO.Path]::GetFullPath($AmrPath)
if (-not (Test-Path -LiteralPath $resolvedAmr -PathType Leaf)) {
    throw "Launcher AMR not found: $resolvedAmr"
}

Assert-DeviceTarget
$abi = Get-DeviceText -Command "uname -m"
if ($abi -ne "aarch64") { throw "Unsupported ABI: $abi" }
Assert-AppIdAvailable

$amrHash = Get-Sha256 -Path $resolvedAmr
Invoke-DeviceAdb -Arguments @("push", $resolvedAmr, "/tmp/lvgl-launcher.amr") | Out-Host
Invoke-DeviceShell -Command "miniapp_cli install /tmp/lvgl-launcher.amr"
$installed = Get-InstalledManifest
if ($null -eq $installed -or $installed.appid -ne $DeviceAppId -or
    $installed.appName -ne $DeviceAppName) {
    throw "Launcher AMR installation could not be verified"
}

Write-Output "INSTALL_PASS appid=$DeviceAppId version=$version amr_sha256=$amrHash"
Write-Output "The signed LVGL platform payload is managed separately; this script never writes it."
