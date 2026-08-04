[CmdletBinding()]
param(
    [string]$Adb = "adb",
    [string]$AppPath = "",
    [int]$RunSeconds = 300,
    [int]$WrapperLimitSeconds = 330,
    [switch]$CollectOnly
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($AppPath)) {
    $AppPath = Join-Path $ProjectRoot "build/m5/lvgl_poc"
}

$RemoteApp = "/tmp/lvgl_poc"
$RemoteWrapper = "/tmp/device_run_poc.sh"
$RemoteLogo = "/tmp/lvgl-poc-logo.png"
$RemoteLog = "/tmp/lvgl-poc-wrapper.log"
$RemoteCapture = "/tmp/lvgl-poc-m5.ppm"
$ResultDirectory = Join-Path $ProjectRoot "test-results/m5"
$ResultLog = Join-Path $ResultDirectory "five-minute-wrapper.log"
$ProcessLog = Join-Path $ResultDirectory "falcon-processes.log"
$ResultCapture = Join-Path $ResultDirectory "five-minute.ppm"

function Invoke-Adb {
    param([Parameter(Mandatory)][string[]]$Arguments)
    & $Adb @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "adb failed ($LASTEXITCODE): $($Arguments -join ' ')"
    }
}

function Assert-OneDevice {
    $lines = & $Adb devices
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to query adb devices"
    }
    $devices = @($lines | Where-Object { $_ -match "\sdevice$" })
    if ($devices.Count -ne 1) {
        throw "Expected exactly one adb device, found $($devices.Count)"
    }
}

if ($RunSeconds -lt 300) {
    throw "M5 requires RunSeconds >= 300"
}
if ($WrapperLimitSeconds -le $RunSeconds) {
    throw "WrapperLimitSeconds must be greater than RunSeconds"
}

Assert-OneDevice
New-Item -ItemType Directory -Force -Path $ResultDirectory | Out-Null

if (-not $CollectOnly) {
    $WrapperPath = Join-Path $PSScriptRoot "device_run_poc.sh"
    $LogoPath = Join-Path $ProjectRoot "assets/poc_badge.png"
    foreach ($path in @($AppPath, $WrapperPath, $LogoPath)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required artifact not found: $path"
        }
    }

    Invoke-Adb -Arguments @("shell", "rm -f $RemoteCapture")
    Invoke-Adb -Arguments @("push", $AppPath, $RemoteApp)
    Invoke-Adb -Arguments @("push", $WrapperPath, $RemoteWrapper)
    Invoke-Adb -Arguments @("push", $LogoPath, $RemoteLogo)
    Invoke-Adb -Arguments @("shell", "chmod 755 $RemoteApp $RemoteWrapper")

    & $Adb shell "POC_RUN_SECONDS=$RunSeconds POC_CAPTURE_PATH=$RemoteCapture POC_CAPTURE_FRAME=180 $RemoteWrapper $RemoteApp $WrapperLimitSeconds"
    $runExit = $LASTEXITCODE
    if ($runExit -ne 0) {
        Write-Warning "Device run returned $runExit; attempting evidence collection"
        Invoke-Adb -Arguments @("wait-for-device")
    }
}

Invoke-Adb -Arguments @("pull", $RemoteLog, $ResultLog)
Invoke-Adb -Arguments @("pull", $RemoteCapture, $ResultCapture)
$processOutput = & $Adb shell "ps | grep -E 'guardian_run.*/usr/bin/runDictPen|/usr/bin/runDictPen|/usr/bin/miniapp'"
if ($LASTEXITCODE -ne 0) {
    throw "Unable to read Falcon process state"
}
$processOutput | Set-Content -Encoding utf8 $ProcessLog

$log = Get-Content -Raw -LiteralPath $ResultLog
$summary = [regex]::Match(
    $log,
    "SUMMARY runtime_s=(?<runtime>[0-9.]+).*result=0",
    [System.Text.RegularExpressions.RegexOptions]::Singleline
)
if (-not $summary.Success) {
    throw "M5 log has no successful SUMMARY"
}
$runtime = [double]::Parse(
    $summary.Groups["runtime"].Value,
    [System.Globalization.CultureInfo]::InvariantCulture
)
if ($runtime -lt ($RunSeconds - 1)) {
    throw "M5 runtime was $runtime seconds; expected at least $RunSeconds"
}
if ($log -notmatch "falcon miniapp restored") {
    throw "Wrapper did not confirm Falcon miniapp recovery"
}
if (($processOutput -join "`n") -notmatch "/usr/bin/miniapp") {
    throw "Falcon miniapp is not present after the run"
}
if (($processOutput -join "`n") -notmatch "guardian_run.*/usr/bin/runDictPen") {
    throw "Falcon guardian is not present after the run"
}

Write-Output "M5_PASS runtime_s=$runtime"
Write-Output "wrapper_log=$ResultLog"
Write-Output "process_log=$ProcessLog"
Write-Output "capture=$ResultCapture"
