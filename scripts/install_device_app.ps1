[CmdletBinding()]
param(
    [string]$Adb = "adb",
    [string]$BinaryPath = "",
    [string]$AmrPath = ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "device_app_common.ps1")
Set-DeviceAdb -Path $Adb

if ([string]::IsNullOrWhiteSpace($BinaryPath)) {
    $BinaryPath = Join-Path $ProjectRoot "build/m5/lvgl_poc"
}
$package = Get-Content -Raw -Encoding UTF8 (Join-Path $ProjectRoot "launcher/package.json") | ConvertFrom-Json
$version = [string]$package.version
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw "Unsafe package version: $version" }
if ([string]::IsNullOrWhiteSpace($AmrPath)) {
    $AmrPath = Join-Path $ProjectRoot "launcher/$($DeviceAppId).0_1_0.amr"
}
$supervisor = Join-Path $ProjectRoot "launcher/device/lvgl-supervisor.sh"
$asset = Join-Path $ProjectRoot "assets/poc_badge.png"
foreach ($path in @($BinaryPath, $AmrPath, $supervisor, $asset)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required artifact not found: $path" }
}

Assert-OneDevice
$profile = Assert-TargetDevice
Assert-AppIdAvailable
Assert-AppNotRunning

$availableKbText = Get-DeviceText -Command "df -Pk /userdisk | awk 'NR==2 {print `$4}'"
[long]$availableKb = 0
if (-not [int64]::TryParse($availableKbText, [ref]$availableKb) -or $availableKb -lt 10240) {
    throw "Insufficient or unknown /userdisk space: $availableKbText KB"
}

$binaryHash = Get-Sha256 -Path $BinaryPath
$supervisorHash = Get-Sha256 -Path $supervisor
$assetHash = Get-Sha256 -Path $asset
$amrHash = Get-Sha256 -Path $AmrPath
$generated = Join-Path $ProjectRoot "build/device-app"
New-Item -ItemType Directory -Force -Path $generated | Out-Null
$manifestPath = Join-Path $generated "manifest.env"
@(
    "PROFILE_ID=$DeviceProfileId"
    "PCBA=$DevicePcba"
    "FIRMWARE=$DeviceFirmware"
    "VERSION=$version"
    "LVGL_SHA256=$binaryHash"
    "SUPERVISOR_SHA256=$supervisorHash"
    "ASSET_SHA256=$assetHash"
    "LAUNCHER_AMR_SHA256=$amrHash"
) | Set-Content -Encoding ascii -LiteralPath $manifestPath

$release = "$DeviceRoot/releases/$version"
$staging = "$DeviceRoot/releases/$version.staging"
Invoke-DeviceShell -Command "mkdir -p '$DeviceRoot/releases' '$DeviceLogRoot'"
Invoke-DeviceShell -Command "rm -rf '$staging'"
Invoke-DeviceShell -Command "mkdir -p '$staging/bin' '$staging/assets'"
Invoke-DeviceAdb -Arguments @("push", $BinaryPath, "$staging/bin/lvgl_poc") | Out-Host
Invoke-DeviceAdb -Arguments @("push", $supervisor, "$staging/bin/lvgl-supervisor.sh") | Out-Host
Invoke-DeviceAdb -Arguments @("push", $asset, "$staging/assets/poc_badge.png") | Out-Host
Invoke-DeviceAdb -Arguments @("push", $manifestPath, "$staging/manifest.env") | Out-Host
Invoke-DeviceShell -Command "chmod 755 '$staging/bin/lvgl_poc' '$staging/bin/lvgl-supervisor.sh'"
Invoke-DeviceShell -Command "chmod 644 '$staging/assets/poc_badge.png' '$staging/manifest.env'"

$remoteChecks = @{
    "$staging/bin/lvgl_poc" = $binaryHash
    "$staging/bin/lvgl-supervisor.sh" = $supervisorHash
    "$staging/assets/poc_badge.png" = $assetHash
}
foreach ($entry in $remoteChecks.GetEnumerator()) {
    $actual = (Get-DeviceText -Command "sha256sum '$($entry.Key)'").Split(' ')[0].ToLowerInvariant()
    if ($actual -ne $entry.Value) { throw "Device hash mismatch: $($entry.Key)" }
}

Invoke-DeviceShell -Command "rm -rf '$release'"
Invoke-DeviceShell -Command "mv '$staging' '$release'"
Invoke-DeviceShell -Command "ln -sfn 'releases/$version' '$DeviceRoot/current'"
Invoke-DeviceAdb -Arguments @("push", $AmrPath, "/tmp/lvgl-launcher.amr") | Out-Host
Invoke-DeviceShell -Command "miniapp_cli install /tmp/lvgl-launcher.amr"

$installed = Get-InstalledManifest
if ($null -eq $installed -or $installed.appid -ne $DeviceAppId -or $installed.appName -ne $DeviceAppName) {
    throw "Launcher installation could not be verified"
}

Write-Output "INSTALL_PASS appid=$DeviceAppId version=$version profile=$($profile.Profile)"
Write-Output "device_root=$DeviceRoot"
Write-Output "binary_sha256=$binaryHash"
Write-Output "amr_sha256=$amrHash"
