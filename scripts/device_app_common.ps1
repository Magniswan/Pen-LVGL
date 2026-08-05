$DeviceAppId = "8080992608050001"
$DeviceAppName = "LVGL $([char]0x5E94)$([char]0x7528)"
$DeviceProfileId = "OVERHEAD_Y01_SKU_CHN_PRO"
$DevicePcba = "RK3562_Orange_V0"
$DeviceFirmware = "4.8.6"
$DeviceRoot = "/userdisk/apps/lvgl-poc"
$DeviceLogRoot = "/userdata/applog/lvgl-poc"
$DeviceRunRoot = "/run/lvgl-poc"
$script:DeviceAdb = "adb"

function Set-DeviceAdb {
    param([Parameter(Mandatory)][string]$Path)
    $script:DeviceAdb = $Path
}

function Invoke-DeviceAdb {
    param([Parameter(Mandatory)][string[]]$Arguments)
    $output = & $script:DeviceAdb @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "adb failed ($LASTEXITCODE): $($Arguments -join ' ')"
    }
    return $output
}

function Invoke-DeviceShell {
    param([Parameter(Mandatory)][string]$Command)
    return Invoke-DeviceAdb -Arguments @("shell", $Command)
}

function Get-DeviceText {
    param([Parameter(Mandatory)][string]$Command)
    return ((Invoke-DeviceShell -Command $Command) -join "`n").Trim()
}

function Assert-OneDevice {
    $lines = & $script:DeviceAdb devices
    if ($LASTEXITCODE -ne 0) { throw "Unable to query adb devices" }
    $devices = @($lines | Where-Object { $_ -match "\sdevice$" })
    if ($devices.Count -ne 1) {
        throw "Expected exactly one adb device, found $($devices.Count)"
    }
}

function Assert-TargetDevice {
    $abi = Get-DeviceText -Command "uname -m"
    $pcba = Get-DeviceText -Command "get_pcba_version"
    $packageConfig = Get-DeviceText -Command "cat /etc/miniapp/resources/local_packages.json"
    $screenConfig = Get-DeviceText -Command "cat /etc/miniapp/resources/cfg.json"
    try {
        $firmware = ($packageConfig | ConvertFrom-Json).version
        $screen = ($screenConfig | ConvertFrom-Json).screen
    } catch {
        throw "Unable to parse target firmware configuration: $($_.Exception.Message)"
    }
    if ($abi -ne "aarch64") { throw "Unsupported ABI: $abi" }
    if ($pcba -ne $DevicePcba) { throw "Unsupported PCBA: $pcba" }
    if ($firmware -ne $DeviceFirmware) { throw "Unsupported firmware: $firmware" }
    if ($screen.width -ne 960 -or $screen.height -ne 266 -or $screen.direction -ne 270) {
        throw "Unsupported screen profile: $($screen.width)x$($screen.height) direction=$($screen.direction)"
    }
    [pscustomobject]@{
        Profile = $DeviceProfileId
        Pcba = $pcba
        Abi = $abi
        Firmware = $firmware
    }
}

function Get-Sha256 {
    param([Parameter(Mandatory)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-InstalledManifest {
    $roots = @(
        "/userdisk/secondary/miniapp/data/mini_app/pkg/$DeviceAppId",
        "/userdata/miniapp/data/mini_app/pkg/$DeviceAppId"
    )
    $checks = ($roots | ForEach-Object {
        'for p in ''' + $_ + '''/*/manifest.json; do if [ -f "$p" ]; then cat "$p"; exit 0; fi; done'
    }) -join '; '
    $text = Get-DeviceText -Command $checks
    if ([string]::IsNullOrWhiteSpace($text)) { return $null }
    try { return $text | ConvertFrom-Json } catch { throw "Installed launcher manifest is invalid" }
}

function Assert-AppIdAvailable {
    $manifest = Get-InstalledManifest
    if ($null -ne $manifest -and $manifest.appName -ne $DeviceAppName) {
        throw "AppID $DeviceAppId is already owned by '$($manifest.appName)'"
    }
}

function Get-DeviceState {
    $text = Get-DeviceText -Command "if [ -f '$DeviceRunRoot/status.env' ]; then cat '$DeviceRunRoot/status.env'; fi"
    $values = @{}
    foreach ($line in ($text -split "`n")) {
        if ($line -match '^(?<key>[A-Z_]+)=(?<value>.*)$') {
            $values[$Matches.key] = $Matches.value.Trim()
        }
    }
    return $values
}

function Assert-AppNotRunning {
    $lock = Get-DeviceText -Command "if [ -d '$DeviceRunRoot/lock' ]; then echo locked; fi"
    if ($lock -eq "locked") { throw "LVGL launcher is currently running" }
}

function Assert-FalconReady {
    $processes = Get-DeviceText -Command "ps | grep -E 'guardian_run.*/usr/bin/runDictPen|/usr/bin/miniapp' | grep -v grep"
    if ($processes -notmatch 'guardian_run.*/usr/bin/runDictPen') {
        throw "Falcon guardian is not running"
    }
    if ($processes -notmatch '/usr/bin/miniapp') {
        throw "Falcon miniapp is not running"
    }
    return $processes
}
