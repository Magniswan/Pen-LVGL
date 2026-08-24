$DeviceAppId = "8080992608050001"
$DeviceAppName = "LVGL $([char]0x5E94)$([char]0x7528)"
$script:DeviceAdb = "adb"
$script:DeviceSerial = ""
$script:ExpectedDeviceIdentitySha256 = ""

function Set-DeviceTarget {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Serial,
        [Parameter(Mandatory)][string]$ExpectedIdentitySha256
    )
    if ($Serial -notmatch '^[A-Za-z0-9._:-]{1,128}$') {
        throw "Unsafe adb serial"
    }
    if ($ExpectedIdentitySha256 -notmatch '^[A-Fa-f0-9]{64}$' -or
        $ExpectedIdentitySha256 -match '^0{64}$') {
        throw "Expected device identity must be a nonzero SHA-256 digest"
    }
    $script:DeviceAdb = $Path
    $script:DeviceSerial = $Serial
    $script:ExpectedDeviceIdentitySha256 = $ExpectedIdentitySha256.ToLowerInvariant()
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

function Get-DeviceEvidenceFile {
    param([Parameter(Mandatory)][ValidateSet(
        "/etc/miniapp/resources/local_packages.json",
        "/etc/miniapp/resources/cfg.json"
    )][string]$Path)
    $encoded = ((Invoke-DeviceShell -Command "base64 '$Path'") -join "")
    if ([string]::IsNullOrWhiteSpace($encoded) -or
        $encoded -notmatch '^[A-Za-z0-9+/]*={0,2}$') {
        throw "Device identity evidence is not valid base64"
    }
    try {
        return [Convert]::FromBase64String($encoded)
    } catch {
        throw "Device identity evidence could not be decoded"
    }
}

function Get-DeviceIdentitySha256 {
    $machine = Get-DeviceText -Command "uname -m"
    if ($machine -notmatch '^[A-Za-z0-9_+-]{1,32}$') {
        throw "Unsafe device machine identity"
    }
    $packages = Get-DeviceEvidenceFile -Path "/etc/miniapp/resources/local_packages.json"
    $screen = Get-DeviceEvidenceFile -Path "/etc/miniapp/resources/cfg.json"
    $stream = [IO.MemoryStream]::new()
    try {
        foreach ($part in @(
            [Text.Encoding]::UTF8.GetBytes($machine),
            $packages,
            $screen
        )) {
            $stream.Write($part, 0, $part.Length)
            $stream.WriteByte(0)
        }
        $sha256 = [Security.Cryptography.SHA256]::Create()
        try {
            return ([BitConverter]::ToString(
                $sha256.ComputeHash($stream.ToArray()))).Replace("-", "").ToLowerInvariant()
        } finally {
            $sha256.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
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
    $actualIdentity = Get-DeviceIdentitySha256
    if ($actualIdentity -cne $script:ExpectedDeviceIdentitySha256) {
        throw "Selected device identity does not match the certified digest"
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
