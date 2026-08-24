$DeviceAppId = "8080992608050001"
$DeviceAppName = "LVGL $([char]0x5E94)$([char]0x7528)"
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
