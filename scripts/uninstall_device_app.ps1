[CmdletBinding(SupportsShouldProcess, ConfirmImpact = "High")]
param(
    [string]$Adb = "adb",
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "device_app_common.ps1")
Set-DeviceAdb -Path $Adb

Assert-OneDevice
Assert-TargetDevice | Out-Null

if (-not $Force -and -not $PSCmdlet.ShouldProcess($DeviceRoot, "Uninstall LVGL launcher and dedicated payload")) {
    return
}

$state = Get-DeviceState
$supervisorPid = 0
if ($state.ContainsKey("SUPERVISOR_PID")) {
    [int]::TryParse($state.SUPERVISOR_PID, [ref]$supervisorPid) | Out-Null
}
if ($supervisorPid -gt 0) {
    $cmdline = Get-DeviceText -Command "tr '\000' ' ' < /proc/$supervisorPid/cmdline 2>/dev/null | sed 's/[[:space:]]*`$//'"
    $expectedSupervisor = "/bin/sh /userdisk/apps/lvgl-poc/current/bin/lvgl-supervisor.sh run"
    if ($state.STATE -in @("launching", "running", "restoring") -and $cmdline -ne $expectedSupervisor) {
        throw "Refusing to uninstall while supervisor PID $supervisorPid is not the expected LVGL supervisor"
    }
    if ($cmdline -eq $expectedSupervisor) {
        Invoke-DeviceShell -Command "kill -TERM $supervisorPid"
        $alive = "yes"
        for ($attempt = 0; $attempt -lt 40; $attempt += 1) {
            $alive = Get-DeviceText -Command "if kill -0 $supervisorPid 2>/dev/null; then echo yes; fi"
            if ($alive -ne "yes") { break }
            Start-Sleep -Milliseconds 250
        }
        if ($alive -eq "yes") {
            throw "LVGL supervisor did not stop within 10 seconds"
        }
    }
}

$manifest = Get-InstalledManifest
if ($null -ne $manifest) {
    Invoke-DeviceShell -Command "miniapp_cli uninstall $DeviceAppId"
    if ($null -ne (Get-InstalledManifest)) {
        throw "Falcon launcher package is still installed"
    }
}
Assert-FalconReady | Out-Null
Invoke-DeviceShell -Command "rm -rf '$DeviceRoot'"
Invoke-DeviceShell -Command "rm -rf '$DeviceLogRoot'"
Invoke-DeviceShell -Command "rm -rf '$DeviceRunRoot'"

Write-Output "UNINSTALL_PASS appid=$DeviceAppId"
