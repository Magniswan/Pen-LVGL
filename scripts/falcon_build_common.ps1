Set-StrictMode -Version Latest

function Get-LockedFalconNode {
    param([Parameter(Mandatory)][string]$NodeExecutable)
    $command = Get-Command $NodeExecutable -CommandType Application -ErrorAction Stop
    $resolved = [IO.Path]::GetFullPath($command.Source)
    $item = Get-Item -LiteralPath $resolved -Force
    if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Falcon Node executable must be a regular non-link file"
    }
    $version = (& $resolved --version).Trim()
    if ($LASTEXITCODE -ne 0 -or $version -cne 'v18.20.8') {
        throw "Falcon releases require exactly Node v18.20.8; observed $version"
    }
    return $resolved
}

function Test-FalconArrayEqual {
    param([string[]]$Actual, [string[]]$Expected)
    if ($Actual.Count -ne $Expected.Count) { return $false }
    for ($index = 0; $index -lt $Expected.Count; $index++) {
        if ($Actual[$index] -cne $Expected[$index]) { return $false }
    }
    return $true
}

function Get-FalconArchiveReport {
    param(
        [Parameter(Mandatory)][string]$Archive,
        [Parameter(Mandatory)][string]$ExpectedAppId,
        [Parameter(Mandatory)][string]$ExpectedVersion,
        [Parameter(Mandatory)][string]$NativeLibrary,
        [Parameter(Mandatory)][ValidateSet('app.js', 'app.js.bin')][string]$ScriptEntry
    )
    $resolved = [IO.Path]::GetFullPath($Archive)
    $item = Get-Item -LiteralPath $resolved -Force -ErrorAction Stop
    if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
        $item.Length -le 0 -or $item.Length -gt 64MB) {
        throw "Falcon AMR must be a bounded regular non-link file"
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($resolved)
    try {
        $expectedEntries = @($ScriptEntry, 'app_icon.png', "libs/arm64-orange/$NativeLibrary", 'manifest.json')
        [Array]::Sort($expectedEntries, [StringComparer]::Ordinal)
        $actualEntries = @($zip.Entries | ForEach-Object { $_.FullName })
        [Array]::Sort($actualEntries, [StringComparer]::Ordinal)
        if (-not (Test-FalconArrayEqual -Actual $actualEntries -Expected $expectedEntries) -or
            ($actualEntries | Select-Object -Unique).Count -ne $actualEntries.Count -or
            ($zip.Entries | Measure-Object -Property Length -Sum).Sum -gt 64MB) {
            throw "Falcon AMR contains an unexpected, duplicate, or oversized entry set"
        }
        $manifestEntry = $zip.GetEntry('manifest.json')
        $reader = [IO.StreamReader]::new($manifestEntry.Open(), [Text.UTF8Encoding]::new($false, $true))
        try { $manifest = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
        if ([string]$manifest.appid -cne $ExpectedAppId -or
            [string]$manifest.version -cne $ExpectedVersion -or
            [string]$manifest.quickjs.version -cne '20200705' -or $manifest.quickjs.bigNum -ne $false) {
            throw "Falcon AMR manifest identity or QuickJS contract is invalid"
        }
        foreach ($certifiedPath in @($ScriptEntry, "libs/arm64-orange/$NativeLibrary")) {
            $certificateProperty = $manifest.cert.PSObject.Properties[$certifiedPath]
            if ($null -eq $certificateProperty) {
                throw "Falcon AMR manifest certificate is missing: $certifiedPath"
            }
            $certificate = $certificateProperty.Value
            $entry = $zip.GetEntry($certifiedPath)
            if ($null -eq $entry -or [long]$certificate.size -ne $entry.Length -or
                [string]$certificate.md5 -notmatch '^[0-9a-f]{32}$') {
                throw "Falcon AMR manifest certificate is missing or malformed: $certifiedPath"
            }
            $md5 = [Security.Cryptography.MD5]::Create()
            $stream = $entry.Open()
            try { $digest = [BitConverter]::ToString($md5.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
            finally { $stream.Dispose(); $md5.Dispose() }
            if ($digest -cne [string]$certificate.md5) {
                throw "Falcon AMR entry digest mismatch: $certifiedPath"
            }
        }
    } finally {
        $zip.Dispose()
    }
    return [pscustomobject]@{
        Path = $resolved
        Sha256 = (Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash.ToLowerInvariant()
        Size = $item.Length
        AppId = $ExpectedAppId
        Version = $ExpectedVersion
    }
}

function ConvertTo-DeterministicFalconArchive {
    param(
        [Parameter(Mandatory)][string]$Archive,
        [Parameter(Mandatory)][string]$ExpectedAppId,
        [Parameter(Mandatory)][string]$ExpectedVersion,
        [Parameter(Mandatory)][string]$NativeLibrary,
        [Parameter(Mandatory)][ValidateSet('app.js', 'app.js.bin')][string]$ScriptEntry
    )
    $resolved = [IO.Path]::GetFullPath($Archive)
    $null = Get-FalconArchiveReport -Archive $resolved -ExpectedAppId $ExpectedAppId `
        -ExpectedVersion $ExpectedVersion -NativeLibrary $NativeLibrary `
        -ScriptEntry $ScriptEntry
    $contents = [ordered]@{}
    $source = [IO.Compression.ZipFile]::OpenRead($resolved)
    try {
        foreach ($entry in ($source.Entries | Sort-Object FullName)) {
            $memory = [IO.MemoryStream]::new()
            $stream = $entry.Open()
            try { $stream.CopyTo($memory); $contents[$entry.FullName] = $memory.ToArray() }
            finally { $stream.Dispose(); $memory.Dispose() }
        }
    } finally {
        $source.Dispose()
    }
    $temporary = "$resolved.canonical-$([guid]::NewGuid().ToString('N'))"
    try {
        $file = [IO.File]::Open(
            $temporary, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        $target = [IO.Compression.ZipArchive]::new(
            $file, [IO.Compression.ZipArchiveMode]::Create, $false, [Text.Encoding]::UTF8)
        try {
            foreach ($name in $contents.Keys) {
                $entry = $target.CreateEntry($name, [IO.Compression.CompressionLevel]::Optimal)
                $entry.LastWriteTime = [DateTimeOffset]::new(
                    1980, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
                $entry.ExternalAttributes = 0
                $stream = $entry.Open()
                try {
                    $bytes = [byte[]]$contents[$name]
                    $stream.Write($bytes, 0, $bytes.Length)
                } finally { $stream.Dispose() }
            }
        } finally {
            $target.Dispose()
            $file.Dispose()
        }
        $null = Get-FalconArchiveReport -Archive $temporary -ExpectedAppId $ExpectedAppId `
            -ExpectedVersion $ExpectedVersion -NativeLibrary $NativeLibrary `
            -ScriptEntry $ScriptEntry
        [IO.File]::Move($temporary, $resolved, $true)
    } finally {
        if (Test-Path -LiteralPath $temporary -PathType Leaf) {
            Remove-Item -LiteralPath $temporary -Force
        }
    }
}

function Invoke-LockedFalconPackage {
    param(
        [Parameter(Mandatory)][string]$ProjectDirectory,
        [Parameter(Mandatory)][string]$NodeExecutable,
        [Parameter(Mandatory)][string]$NativeLibrary,
        [Parameter(Mandatory)][bool]$Production
    )
    $project = [IO.Path]::GetFullPath($ProjectDirectory)
    $packagePath = Join-Path $project 'package.json'
    $package = Get-Content -Raw -Encoding UTF8 -LiteralPath $packagePath | ConvertFrom-Json
    if ([string]$package.appid -notmatch '^\d{16}$' -or
        [string]$package.version -notmatch '^\d+\.\d+\.\d+$' -or
        [string]$package.engines.node -cne '18.20.8' -or
        [string]$package.devDependencies.'aiot-vue-cli' -cne '1.0.32') {
        throw "Falcon package identity, semantic version, Node lock, or packager lock is invalid"
    }
    $nativePath = Join-Path $project "libs/arm64-orange/$NativeLibrary"
    $native = Get-Item -LiteralPath $nativePath -Force -ErrorAction Stop
    if ($native.PSIsContainer -or ($native.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
        $native.Length -le 0 -or $native.Length -gt 16MB) {
        throw "Falcon native JSAPI artifact is missing, linked, or oversized"
    }
    $builder = Join-Path $project 'node_modules/aiot-vue-cli/src/cli.js'
    $builderItem = Get-Item -LiteralPath $builder -Force -ErrorAction Stop
    if ($builderItem.PSIsContainer -or ($builderItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Locked aiot-vue-cli installation is unavailable"
    }
    $builderPackagePath = Join-Path $project 'node_modules/aiot-vue-cli/package.json'
    $builderPackage = Get-Content -Raw -Encoding UTF8 -LiteralPath $builderPackagePath | ConvertFrom-Json
    if ([string]$builderPackage.name -cne 'aiot-vue-cli' -or [string]$builderPackage.version -cne '1.0.32') {
        throw "Installed Falcon packager does not match the locked aiot-vue-cli version"
    }
    Push-Location $project
    try {
        if ($Production) {
            & $NodeExecutable $builder -c -q -p | Out-Host
        } else {
            & $NodeExecutable $builder -p | Out-Host
        }
        if ($LASTEXITCODE -ne 0) { throw "Falcon project packager failed" }
    } finally {
        Pop-Location
    }
    $versionToken = ([string]$package.version).Replace('.', '_')
    $archive = Join-Path $project ("$($package.appid).$versionToken.amr")
    $scriptEntry = if ($Production) { 'app.js.bin' } else { 'app.js' }
    ConvertTo-DeterministicFalconArchive -Archive $archive `
        -ExpectedAppId ([string]$package.appid) -ExpectedVersion ([string]$package.version) `
        -NativeLibrary $NativeLibrary -ScriptEntry $scriptEntry
    return Get-FalconArchiveReport -Archive $archive -ExpectedAppId ([string]$package.appid) `
        -ExpectedVersion ([string]$package.version) -NativeLibrary $NativeLibrary `
        -ScriptEntry $scriptEntry
}
