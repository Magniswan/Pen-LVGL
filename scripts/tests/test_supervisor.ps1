[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$TestRoot = Join-Path $ProjectRoot "build/supervisor-test"
$ProjectFull = [IO.Path]::GetFullPath($ProjectRoot)
$TestFull = [IO.Path]::GetFullPath($TestRoot)
if (-not $TestFull.StartsWith($ProjectFull + [IO.Path]::DirectorySeparatorChar)) {
    throw "Unsafe test directory: $TestFull"
}
if (Test-Path -LiteralPath $TestFull) {
    Remove-Item -LiteralPath $TestFull -Recurse -Force
}

$AppRoot = Join-Path $TestFull "app"
$RunRoot = Join-Path $TestFull "run"
$LogRoot = Join-Path $TestFull "log"
$ProcRoot = Join-Path $TestFull "proc"
New-Item -ItemType Directory -Force -Path @(
    (Join-Path $AppRoot "bin"),
    (Join-Path $AppRoot "assets"),
    $RunRoot,
    $LogRoot,
    $ProcRoot
) | Out-Null

function Write-Cmdline {
    param([int]$ProcessId, [string[]]$Arguments)
    $directory = Join-Path $ProcRoot $ProcessId
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
    $value = ($Arguments -join "`0") + "`0"
    [IO.File]::WriteAllBytes(
        (Join-Path $directory "cmdline"),
        [Text.Encoding]::ASCII.GetBytes($value)
    )
}

Write-Cmdline 101 @("/usr/bin/guardian_run", "/usr/bin/runDictPen")
Write-Cmdline 102 @("/bin/sh", "/usr/bin/runDictPen")
Write-Cmdline 103 @("/usr/bin/miniapp")
Write-Cmdline 104 @("/usr/bin/guardian_run", "/usr/bin/runWifiMgr")
Set-Content -Encoding ascii -NoNewline -LiteralPath (Join-Path $ProcRoot "103/exe.path") -Value "/usr/bin/miniapp"

$Manifest = @"
PROFILE_ID=OVERHEAD_Y01_SKU_CHN_PRO
VERSION=test
LVGL_SHA256=$('0' * 64)
"@
Set-Content -Encoding ascii -LiteralPath (Join-Path $AppRoot "manifest.env") -Value $Manifest
Set-Content -Encoding ascii -LiteralPath (Join-Path $AppRoot "assets/poc_badge.png") -Value "fixture"
Set-Content -Encoding ascii -LiteralPath (Join-Path $AppRoot "bin/lvgl_poc") -Value @"
#!/bin/sh
exit `${LVGL_TEST_APP_EXIT:-0}
"@

function ConvertTo-WslPath {
    param([Parameter(Mandatory)][string]$Path)
    $full = [IO.Path]::GetFullPath($Path)
    if ($full -notmatch '^(?<drive>[A-Za-z]):\\(?<rest>.*)$') {
        throw "Unsupported Windows path: $full"
    }
    return "/mnt/$($Matches.drive.ToLower())/$($Matches.rest.Replace('\', '/'))"
}

$Supervisor = ConvertTo-WslPath (Join-Path $ProjectRoot "launcher/device/lvgl-supervisor.sh")
$WslApp = ConvertTo-WslPath $AppRoot
$WslRun = ConvertTo-WslPath $RunRoot
$WslLog = ConvertTo-WslPath $LogRoot
$WslProc = ConvertTo-WslPath $ProcRoot
& wsl.exe -d Ubuntu -- chmod 755 "$WslApp/bin/lvgl_poc" $Supervisor
if ($LASTEXITCODE -ne 0) { throw "chmod failed" }

$Common = @(
    "LVGL_SUPERVISOR_TEST_MODE=1",
    "LVGL_TEST_APP_ROOT=$WslApp",
    "LVGL_TEST_RUN_ROOT=$WslRun",
    "LVGL_TEST_LOG_ROOT=$WslLog",
    "LVGL_TEST_PROC_ROOT=$WslProc"
)

$probe = & wsl.exe -d Ubuntu -- env @Common sh $Supervisor probe
if ($LASTEXITCODE -ne 0) { throw "probe failed" }
$probeText = $probe -join "`n"
foreach ($expected in @("GUARD_PID=101", "RUN_DICT_PID=102", "MINIAPP_PID=103")) {
    if ($probeText -notmatch [regex]::Escape($expected)) { throw "Missing probe result: $expected" }
}
if ($probeText -match "104") { throw "Unrelated guardian was selected" }

& wsl.exe -d Ubuntu -- env @Common sh $Supervisor run
if ($LASTEXITCODE -ne 0) { throw "normal run failed: $LASTEXITCODE" }
$log = Get-Content -Raw -LiteralPath (Join-Path $LogRoot "supervisor.log")
foreach ($expected in @("test stop pid=101", "test stop pid=102", "test stop pid=103", "falcon restored")) {
    if ($log -notmatch [regex]::Escape($expected)) { throw "Missing lifecycle log: $expected" }
}
if ($log -match "test stop pid=104") { throw "Unrelated guardian would be stopped" }

New-Item -ItemType Directory -Force -Path (Join-Path $RunRoot "lock") | Out-Null
& wsl.exe -d Ubuntu -- env @Common sh $Supervisor run
if ($LASTEXITCODE -ne 75) { throw "duplicate lock returned $LASTEXITCODE, expected 75" }
Remove-Item -LiteralPath (Join-Path $RunRoot "lock") -Recurse -Force

& wsl.exe -d Ubuntu -- env @Common "LVGL_TEST_APP_EXIT=42" sh $Supervisor run
if ($LASTEXITCODE -ne 42) { throw "failure propagation returned $LASTEXITCODE, expected 42" }
$state = Get-Content -Raw -LiteralPath (Join-Path $RunRoot "status.env")
if ($state -notmatch "STATE=error" -or $state -notmatch "RESULT=42") {
    throw "failure state was not recorded"
}

Write-Output "supervisor_tests=PASS"
