[CmdletBinding()]
param(
    [switch]$Production,
    [string]$WslDistribution = "Ubuntu",
    [string]$NodeExecutable = "node"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$LauncherRepository = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'falcon_build_common.ps1')
$lockedNode = Get-LockedFalconNode -NodeExecutable $NodeExecutable

function Convert-LauncherWslPath {
    param([Parameter(Mandatory)][string]$Path)
    $resolved = [IO.Path]::GetFullPath($Path)
    $converted = & wsl.exe -d $WslDistribution -- bash -lc 'wslpath -a "$1"' _ $resolved
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($converted)) {
        throw "Unable to convert path for WSL: $resolved"
    }
    return $converted.Trim()
}

$repositoryWsl = Convert-LauncherWslPath -Path $LauncherRepository
& wsl.exe -d $WslDistribution -- bash "$repositoryWsl/launcher/tools/build-native.sh"
if ($LASTEXITCODE -ne 0) { throw "Launcher AArch64 native build failed" }
$report = Invoke-LockedFalconPackage -ProjectDirectory (Join-Path $LauncherRepository 'launcher') `
    -NodeExecutable $lockedNode -NativeLibrary 'libjsapi_lvgl_launcher.so' `
    -Production $Production.IsPresent
Write-Output "LAUNCHER_BUILD_PASS production=$($Production.IsPresent) amr=$($report.Path) sha256=$($report.Sha256)"
