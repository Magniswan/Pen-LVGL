[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$CertifiedProfileEnv,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{64}$')][string]$OfficialPublicKeyHex,
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [Parameter(Mandatory)][ValidateRange(1, [long]::MaxValue)][long]$ReleaseCounter,
    [Parameter(Mandatory)][ValidateRange(0, [uint32]::MaxValue)][long]$SecurityEpoch,
    [string]$Node = "node"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ReleaseRepository = Split-Path -Parent $PSScriptRoot
$ReleaseBuild = [IO.Path]::GetFullPath($BuildDirectory)
$ReleaseProfile = [IO.Path]::GetFullPath($CertifiedProfileEnv)
$ReleaseOutput = [IO.Path]::GetFullPath($OutputDirectory)
$ReleaseForbiddenTestKey = 'd75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a'

function Get-ReleaseRegularFile {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][long]$MaximumBytes)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
        $item.Length -le 0 -or $item.Length -gt $MaximumBytes) {
        throw "Release input must be a bounded regular non-link file: $Path"
    }
    return $item
}

function Assert-ReleaseAarch64Elf {
    param([Parameter(Mandatory)][string]$Path)
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
        [IO.FileShare]::Read)
    try {
        $header = [byte[]]::new(20)
        if ($stream.Read($header, 0, $header.Length) -ne $header.Length -or
            $header[0] -ne 0x7f -or $header[1] -ne 0x45 -or $header[2] -ne 0x4c -or
            $header[3] -ne 0x46 -or $header[4] -ne 2 -or $header[5] -ne 1 -or
            $header[18] -ne 0xb7 -or $header[19] -ne 0x00) {
            throw "Release binary is not ELF64 little-endian AArch64: $Path"
        }
    } finally {
        $stream.Dispose()
    }
}

function Read-CertifiedReleaseProfile {
    param([Parameter(Mandatory)][string]$Path)
    $raw = [IO.File]::ReadAllText($Path, [Text.UTF8Encoding]::new($false, $true))
    if ($raw.Length -gt 4096 -or $raw.Contains("`r") -or -not $raw.EndsWith("`n")) {
        throw "Certified profile must be bounded UTF-8 with LF endings"
    }
    $required = @(
        'PROFILE_ID', 'MACHINE', 'LOGICAL_WIDTH', 'LOGICAL_HEIGHT', 'DRM_DEVICE',
        'DRM_CONNECTOR_ID', 'DRM_CRTC_ID', 'DRM_OVERLAY_PLANE_ID', 'DRM_OVERLAY_ZPOS',
        'DISPLAY_X', 'DISPLAY_Y', 'DISPLAY_WIDTH', 'DISPLAY_HEIGHT', 'PIXEL_FORMAT',
        'DISPLAY_ROTATION', 'HOLE_SESSION_CERTIFIED'
    )
    $values = @{}
    foreach ($line in $raw.Substring(0, $raw.Length - 1).Split("`n")) {
        if ($line -notmatch '^([A-Z_]+)=([^=]+)$' -or $required -notcontains $Matches[1] -or
            $values.ContainsKey($Matches[1])) {
            throw "Certified profile contains an unknown, duplicate, or malformed field"
        }
        $values[$Matches[1]] = $Matches[2]
    }
    if ($values.Count -ne $required.Count -or $values.HOLE_SESSION_CERTIFIED -ne '1' -or
        $values.PROFILE_ID -notmatch '^[a-z0-9]+(?:[.-][a-z0-9]+)+$' -or
        $values.MACHINE -notmatch '^[a-z0-9_+-]{2,32}$') {
        throw "Profile is incomplete, uncertified, or has an invalid identity"
    }
    return $values
}

if ($OfficialPublicKeyHex -eq ('0' * 64) -or $OfficialPublicKeyHex -eq $ReleaseForbiddenTestKey) {
    throw "The all-zero and RFC 8032 test public keys are forbidden"
}
if (Test-Path -LiteralPath $ReleaseOutput) {
    throw "Refusing to overwrite release output: $ReleaseOutput"
}
$outputParent = Split-Path -Parent $ReleaseOutput
if (-not (Test-Path -LiteralPath $outputParent -PathType Container)) {
    New-Item -ItemType Directory -Path $outputParent | Out-Null
}
$parentItem = Get-Item -LiteralPath $outputParent -Force
if ($parentItem.Attributes -band [IO.FileAttributes]::ReparsePoint) {
    throw "Release output parent must not be a reparse point"
}

$gitStatus = @(& git -C $ReleaseRepository status --porcelain=v1 --untracked-files=all)
if ($LASTEXITCODE -ne 0 -or $gitStatus.Count -ne 0) {
    throw "Release staging requires a clean Git worktree"
}
$gitCommit = (& git -C $ReleaseRepository rev-parse HEAD).Trim()
$gitTimestamp = [DateTimeOffset]::FromUnixTimeSeconds(
    [long]((& git -C $ReleaseRepository show -s --format=%ct HEAD).Trim()))
$cache = Get-Content -Raw -LiteralPath (Join-Path $ReleaseBuild 'CMakeCache.txt')
if ($cache -notmatch '(?m)^CMAKE_BUILD_TYPE:STRING=Release$' -or
    $cache -notmatch '(?m)^LVGL_PLATFORM_PRODUCTION_BUILD:BOOL=ON$' -or
    $cache -notmatch ("(?m)^LVGL_PLATFORM_OFFICIAL_PUBLIC_KEY_HEX:STRING=" +
        [regex]::Escape($OfficialPublicKeyHex) + '$')) {
    throw "Platform binaries must come from a Release production build pinned to this public key"
}

$profile = Read-CertifiedReleaseProfile -Path $ReleaseProfile
$inputs = [ordered]@{
    'bin/lvgl-2048' = Join-Path $ReleaseBuild 'lvgl-2048'
    'bin/lvgl-desktop' = Join-Path $ReleaseBuild 'lvgl-desktop'
    'bin/lvgl-installer' = Join-Path $ReleaseBuild 'lvgl-installer'
    'bin/lvgl-sessiond' = Join-Path $ReleaseBuild 'lvgl-sessiond'
}
foreach ($source in $inputs.Values) {
    $null = Get-ReleaseRegularFile -Path $source -MaximumBytes (64MB)
    Assert-ReleaseAarch64Elf -Path $source
}
$null = Get-ReleaseRegularFile -Path $ReleaseProfile -MaximumBytes 4096

$transaction = Join-Path $outputParent ('.platform-release-' + [guid]::NewGuid().ToString('N'))
$transactionFull = [IO.Path]::GetFullPath($transaction)
try {
    $root = Join-Path $transactionFull 'root'
    $artifact = Join-Path $transactionFull 'artifact'
    New-Item -ItemType Directory -Path (Join-Path $root 'bin') -Force | Out-Null
    New-Item -ItemType Directory -Path $artifact | Out-Null
    foreach ($entry in $inputs.GetEnumerator()) {
        Copy-Item -LiteralPath $entry.Value -Destination (Join-Path $root $entry.Key)
    }
    Copy-Item -LiteralPath $ReleaseProfile -Destination (Join-Path $root 'profile.env')

    $manifest = [ordered]@{
        formatVersion = 1
        appId = 'top.lvgl.platform'
        name = 'LVGL Platform'
        version = $Version
        releaseCounter = $ReleaseCounter
        securityEpoch = $SecurityEpoch
        sdkAbi = '1.0'
        minPlatformVersion = '1.0.0'
        entry = 'bin/lvgl-sessiond'
        supportedProfiles = @([string]$profile.PROFILE_ID)
        supportedMachines = @([string]$profile.MACHINE)
        capabilities = @()
        limits = [ordered]@{ memoryMiB = 512; cpuSeconds = 86400; maxFiles = 64; dataMiB = 8 }
        onlinePolicy = [ordered]@{ mode = 'offline-v1' }
    }
    $manifestPath = Join-Path $transactionFull 'manifest.source.json'
    [IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 8) + "`n",
        [Text.UTF8Encoding]::new($false))
    $developmentPackage = Join-Path $artifact 'platform.lvapp.dev'
    $buildReport = @(& $Node (Join-Path $ReleaseRepository 'tools/lvapp/cli.mjs') build `
        --manifest $manifestPath --root $root --out $developmentPackage)
    if ($LASTEXITCODE -ne 0) { throw "Deterministic platform package build failed" }
    [IO.File]::WriteAllLines((Join-Path $artifact 'inspection.json'), $buildReport,
        [Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $artifact 'manifest.source.json')

    $files = @()
    foreach ($entry in $inputs.GetEnumerator()) {
        $sourceHash = (Get-FileHash -LiteralPath $entry.Value -Algorithm SHA256).Hash.ToLowerInvariant()
        $files += [ordered]@{ path = $entry.Key; sha256 = $sourceHash }
    }
    $profileHash = (Get-FileHash -LiteralPath $ReleaseProfile -Algorithm SHA256).Hash.ToLowerInvariant()
    $files += [ordered]@{ path = 'profile.env'; sha256 = $profileHash }
    $evidence = [ordered]@{
        schemaVersion = 1
        commit = $gitCommit
        commitTimestampUtc = $gitTimestamp.UtcDateTime.ToString('yyyy-MM-ddTHH:mm:ssZ')
        officialPublicKeyHex = $OfficialPublicKeyHex
        profileId = [string]$profile.PROFILE_ID
        machine = [string]$profile.MACHINE
        packageVersion = $Version
        releaseCounter = $ReleaseCounter
        securityEpoch = $SecurityEpoch
        files = $files
    }
    [IO.File]::WriteAllText((Join-Path $artifact 'build-evidence.json'),
        ($evidence | ConvertTo-Json -Depth 8) + "`n", [Text.UTF8Encoding]::new($false))

    $spdxFiles = @()
    $index = 1
    foreach ($file in $files) {
        $spdxFiles += [ordered]@{
            SPDXID = "SPDXRef-File-$index"
            fileName = './' + $file.path
            checksums = @([ordered]@{ algorithm = 'SHA256'; checksumValue = $file.sha256 })
            licenseConcluded = 'NOASSERTION'
            copyrightText = 'NOASSERTION'
        }
        $index++
    }
    $relationships = @()
    for ($relationshipIndex = 1; $relationshipIndex -le $spdxFiles.Count; $relationshipIndex++) {
        $relationships += [ordered]@{
            spdxElementId = 'SPDXRef-Package-platform'
            relationshipType = 'CONTAINS'
            relatedSpdxElement = "SPDXRef-File-$relationshipIndex"
        }
    }
    $sbom = [ordered]@{
        spdxVersion = 'SPDX-2.3'
        dataLicense = 'CC0-1.0'
        SPDXID = 'SPDXRef-DOCUMENT'
        name = "lvgl-platform-$Version"
        documentNamespace = "https://lvgl.top/spdx/$gitCommit/platform-$ReleaseCounter"
        creationInfo = [ordered]@{
            created = $gitTimestamp.UtcDateTime.ToString('yyyy-MM-ddTHH:mm:ssZ')
            creators = @('Tool: lvgl-platform-stage-1')
        }
        packages = @([ordered]@{
            name = 'top.lvgl.platform'
            SPDXID = 'SPDXRef-Package-platform'
            versionInfo = $Version
            downloadLocation = 'NOASSERTION'
            filesAnalyzed = $true
            licenseConcluded = 'NOASSERTION'
            licenseDeclared = 'NOASSERTION'
            copyrightText = 'NOASSERTION'
        })
        files = $spdxFiles
        relationships = $relationships
    }
    [IO.File]::WriteAllText((Join-Path $artifact 'sbom.spdx.json'),
        ($sbom | ConvertTo-Json -Depth 12) + "`n", [Text.UTF8Encoding]::new($false))
    $lvglLicense = Get-Content -Raw -LiteralPath (Join-Path $ReleaseRepository 'third_party/lvgl/LICENCE.txt')
    $notices = "LVGL is statically linked and distributed under the following MIT license:`n`n" +
        $lvglLicense + "`nRuntime dependency libdrm is supplied by the certified device image and is not bundled.`n"
    [IO.File]::WriteAllText((Join-Path $artifact 'THIRD_PARTY-NOTICES.txt'), $notices,
        [Text.UTF8Encoding]::new($false))

    $sha256 = (Get-FileHash -LiteralPath $developmentPackage -Algorithm SHA256).Hash.ToLowerInvariant()
    $sha512 = (Get-FileHash -LiteralPath $developmentPackage -Algorithm SHA512).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText((Join-Path $artifact 'checksums.txt'),
        "SHA256 platform.lvapp.dev $sha256`nSHA512 platform.lvapp.dev $sha512`n",
        [Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $artifact -Destination $ReleaseOutput
} finally {
    $resolvedTransaction = [IO.Path]::GetFullPath($transactionFull)
    $resolvedParent = [IO.Path]::GetFullPath($outputParent).TrimEnd('\', '/') +
        [IO.Path]::DirectorySeparatorChar
    if ($resolvedTransaction.StartsWith($resolvedParent, [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolvedTransaction -PathType Container)) {
        Remove-Item -LiteralPath $resolvedTransaction -Recurse -Force
    }
}

Write-Output "PLATFORM_RELEASE_STAGE_PASS output=$ReleaseOutput"
