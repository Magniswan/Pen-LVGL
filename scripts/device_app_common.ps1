$DeviceAppId = "8080992608050001"
$DeviceAppName = "LVGL $([char]0x5E94)$([char]0x7528)"
$script:DeviceAdb = "adb"
$script:DeviceSerial = ""

function Set-DeviceTarget {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Serial
    )
    if ($Serial -notmatch '^[A-Za-z0-9._:-]{1,128}$') {
        throw "Unsafe adb serial"
    }
    $script:DeviceAdb = $Path
    $script:DeviceSerial = $Serial
}

function Invoke-DeviceAdb {
    param([Parameter(Mandatory)][string[]]$Arguments)
    if ([string]::IsNullOrWhiteSpace($script:DeviceSerial)) {
        throw "Device target is not configured"
    }
    $targetedArguments = @("-s", $script:DeviceSerial) + $Arguments
    $output = & $script:DeviceAdb @targetedArguments
    if ($LASTEXITCODE -ne 0) {
        throw "adb failed ($LASTEXITCODE) for the selected device"
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

function Assert-DeviceTarget {
    $lines = & $script:DeviceAdb devices
    if ($LASTEXITCODE -ne 0) { throw "Unable to query adb devices" }
    $matches = @($lines | Where-Object {
        $_ -match '^([^\s]+)\s+([^\s]+)$' -and $Matches[1] -eq $script:DeviceSerial
    })
    if ($matches.Count -ne 1 -or $matches[0] -notmatch '\sdevice$') {
        throw "Selected adb serial is absent, unauthorized, offline, or ambiguous"
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
