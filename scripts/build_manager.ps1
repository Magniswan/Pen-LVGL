[CmdletBinding()]
param(
    [switch]$Production,
    [string]$OfficialPublicKeyHex = "",
    [string]$CertifiedProfileId = "",
    [string]$CertifiedMachine = "",
    [string]$DeviceIdentitySha256Hex = "",
    [string]$PlatformPackage = "",
    [string]$WslDistribution = "Ubuntu",
    [string]$NodeRoot = "",
    [string]$NodeExecutable = "node"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'falcon_build_common.ps1')

function Convert-ToWslPath {
    param([Parameter(Mandatory)][string]$Path)
    $resolved = [IO.Path]::GetFullPath($Path)
    $converted = & wsl.exe -d $WslDistribution -- wslpath -a $resolved.Replace('\', '/')
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($converted)) {
        throw "Unable to convert path for WSL: $resolved"
    }
    return $converted.Trim()
}

$environment = @("MANAGER_PRODUCTION=$([int]$Production.IsPresent)")
if ($Production) {
    if ($OfficialPublicKeyHex -notmatch '^[0-9a-f]{64}$') {
        throw "OfficialPublicKeyHex must be exactly 64 lowercase hexadecimal characters"
    }
    if ($OfficialPublicKeyHex -eq [string]::new('0', 64) -or
        $OfficialPublicKeyHex -eq 'd75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a') {
        throw "The all-zero and RFC 8032 test public keys are forbidden"
    }
    if ($CertifiedProfileId -notmatch '^[a-z0-9]+(?:[.-][a-z0-9]+)+$') {
        throw "CertifiedProfileId is invalid"
    }
    if ($CertifiedMachine -notmatch '^[a-z0-9_+-]{2,32}$') {
        throw "CertifiedMachine is invalid"
    }
    if ($DeviceIdentitySha256Hex -notmatch '^[0-9a-f]{64}$') {
        throw "DeviceIdentitySha256Hex must be exactly 64 lowercase hexadecimal characters"
    }
    if ([string]::IsNullOrWhiteSpace($PlatformPackage)) {
        throw "PlatformPackage is required for production builds"
    }
    $payload = [IO.Path]::GetFullPath($PlatformPackage)
    if (-not (Test-Path -LiteralPath $payload -PathType Leaf)) {
        throw "Signed platform package not found: $payload"
    }
    $environment += @(
        "OFFICIAL_PUBLIC_KEY_HEX=$OfficialPublicKeyHex",
        "MANAGER_CERTIFIED_PROFILE_ID=$CertifiedProfileId",
        "MANAGER_CERTIFIED_MACHINE=$CertifiedMachine",
        "MANAGER_DEVICE_IDENTITY_SHA256_HEX=$DeviceIdentitySha256Hex",
        "MANAGER_PLATFORM_PACKAGE=$(Convert-ToWslPath -Path $payload)"
    )
}

$repository = Convert-ToWslPath -Path $ProjectRoot
& wsl.exe -d $WslDistribution -- env @environment bash "$repository/manager/tools/build-native.sh"
if ($LASTEXITCODE -ne 0) { throw "Manager AArch64 native build failed" }

if (-not [string]::IsNullOrWhiteSpace($NodeRoot)) {
    $resolvedNode = [IO.Path]::GetFullPath($NodeRoot)
    if (-not (Test-Path -LiteralPath (Join-Path $resolvedNode "node.exe") -PathType Leaf)) {
        throw "NodeRoot does not contain node.exe: $resolvedNode"
    }
    $NodeExecutable = Join-Path $resolvedNode "node.exe"
}
$lockedNode = Get-LockedFalconNode -NodeExecutable $NodeExecutable
$report = Invoke-LockedFalconPackage -ProjectDirectory (Join-Path $ProjectRoot 'manager') `
    -NodeExecutable $lockedNode -NativeLibrary 'libjsapi_lvgl_manager.so' `
    -Production $Production.IsPresent

Write-Output "MANAGER_BUILD_PASS production=$($Production.IsPresent) amr=$($report.Path) sha256=$($report.Sha256)"
