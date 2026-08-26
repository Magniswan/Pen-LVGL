[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PublicKey,
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{64}$')][string]$ExpectedPublicKeyHex,
    [Parameter(Mandatory)][ValidatePattern('^[a-z0-9][a-z0-9._-]{7,63}$')][string]$CeremonyId,
    [Parameter(Mandatory)][ValidateRange(0, [uint32]::MaxValue)][long]$SecurityEpoch,
    [Parameter(Mandatory)][ValidatePattern('^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$')][string]$ActivationUtc,
    [Parameter(Mandatory)][ValidatePattern('^[a-z0-9][a-z0-9._-]{2,31}$')][string]$WitnessOne,
    [Parameter(Mandatory)][ValidatePattern('^[a-z0-9][a-z0-9._-]{2,31}$')][string]$WitnessTwo,
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{40}$')][string]$ExpectedSourceCommit,
    [Parameter(Mandatory)][string]$OutputFile,
    [string]$Node = "node"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$CeremonyRepository = Split-Path -Parent $PSScriptRoot
$CeremonyPublic = [IO.Path]::GetFullPath($PublicKey)
$CeremonyOutput = [IO.Path]::GetFullPath($OutputFile)
$CeremonyForbiddenTestKey = 'd75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a'

function Test-CeremonyPathWithin {
    param([Parameter(Mandatory)][string]$Child, [Parameter(Mandatory)][string]$Parent)
    $prefix = [IO.Path]::GetFullPath($Parent).TrimEnd('\', '/') +
        [IO.Path]::DirectorySeparatorChar
    return [IO.Path]::GetFullPath($Child).StartsWith(
        $prefix, [StringComparison]::OrdinalIgnoreCase)
}

function Get-CeremonyRegularFile {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][long]$MaximumBytes)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
        $item.Length -le 0 -or $item.Length -gt $MaximumBytes) {
        throw "Ceremony input must be a bounded regular non-link file: $Path"
    }
    return $item
}

function Get-CeremonyNode {
    param([Parameter(Mandatory)][string]$Executable)
    $command = Get-Command $Executable -CommandType Application -ErrorAction Stop
    $resolved = [IO.Path]::GetFullPath($command.Source)
    $item = Get-Item -LiteralPath $resolved -Force
    if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Ceremony Node executable must be a regular non-link file"
    }
    $version = (& $resolved --version).Trim()
    if ($LASTEXITCODE -ne 0 -or $version -cne 'v18.20.8') {
        throw "Key ceremony requires exactly Node v18.20.8; observed $version"
    }
    return [pscustomobject]@{ Path = $resolved; Version = $version }
}

if ($ExpectedPublicKeyHex -eq ('0' * 64) -or $ExpectedPublicKeyHex -eq $CeremonyForbiddenTestKey) {
    throw "The all-zero and RFC 8032 test public keys are forbidden"
}
if ($WitnessOne -ceq $WitnessTwo) { throw "Key ceremony requires two distinct witness IDs" }
$parsedActivation = [DateTimeOffset]::ParseExact(
    $ActivationUtc, 'yyyy-MM-ddTHH:mm:ssZ', [Globalization.CultureInfo]::InvariantCulture,
    [Globalization.DateTimeStyles]::AssumeUniversal)
if ($parsedActivation.ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ') -cne $ActivationUtc) {
    throw "ActivationUtc must be a canonical UTC timestamp"
}
if (Test-CeremonyPathWithin -Child $CeremonyOutput -Parent $CeremonyRepository) {
    throw "Key ceremony evidence must be written outside the source repository"
}
if (Test-Path -LiteralPath $CeremonyOutput) {
    throw "Refusing to overwrite key ceremony challenge: $CeremonyOutput"
}
$null = Get-CeremonyRegularFile -Path $CeremonyPublic -MaximumBytes 16384
$publicText = [IO.File]::ReadAllText($CeremonyPublic, [Text.UTF8Encoding]::new($false, $true))
if ($publicText -match 'PRIVATE KEY') { throw "Key ceremony accepts public material only" }
$status = @(& git -C $CeremonyRepository status --porcelain=v1 --untracked-files=all)
$commit = (& git -C $CeremonyRepository rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $status.Count -ne 0 -or $commit -cne $ExpectedSourceCommit) {
    throw "Key ceremony requires the exact reviewed clean source commit"
}
$nodeInfo = Get-CeremonyNode -Executable $Node
$keyInfoText = @(& $nodeInfo.Path (Join-Path $CeremonyRepository 'tools/lvapp/cli.mjs') key-info `
    --public-key $CeremonyPublic)
if ($LASTEXITCODE -ne 0) { throw "Official public key inspection failed" }
$keyInfo = ($keyInfoText -join "`n") | ConvertFrom-Json
if ($keyInfo.algorithm -ne 'Ed25519' -or $keyInfo.rawPublicKeyHex -cne $ExpectedPublicKeyHex -or
    [string]$keyInfo.keyId -notmatch '^[0-9a-f]{32}$') {
    throw "Public key does not match the independently approved official trust root"
}

$challenge = [ordered]@{
    domain = 'lvgl-platform-official-key-ceremony-v1'
    ceremonyId = $CeremonyId
    algorithm = 'Ed25519'
    keyId = [string]$keyInfo.keyId
    rawPublicKeyHex = $ExpectedPublicKeyHex
    publicKeyPemSha256 = (Get-FileHash -LiteralPath $CeremonyPublic -Algorithm SHA256).Hash.ToLowerInvariant()
    securityEpoch = $SecurityEpoch
    activationUtc = $ActivationUtc
    sourceCommit = $commit
    nodeVersion = $nodeInfo.Version
    witnesses = @(
        [ordered]@{ role = 'primary'; id = $WitnessOne },
        [ordered]@{ role = 'secondary'; id = $WitnessTwo }
    )
}
$json = (($challenge | ConvertTo-Json -Depth 5) -replace "`r`n", "`n") + "`n"
$parent = Split-Path -Parent $CeremonyOutput
if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
    New-Item -ItemType Directory -Path $parent | Out-Null
}
$parentItem = Get-Item -LiteralPath $parent -Force
if ($parentItem.Attributes -band [IO.FileAttributes]::ReparsePoint) {
    throw "Key ceremony output parent must not be a reparse point"
}
$temporary = Join-Path $parent ('.key-challenge-' + [guid]::NewGuid().ToString('N'))
try {
    [IO.File]::WriteAllText($temporary, $json, [Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $temporary -Destination $CeremonyOutput
} finally {
    if (Test-Path -LiteralPath $temporary -PathType Leaf) {
        Remove-Item -LiteralPath $temporary -Force
    }
}
$sha512 = (Get-FileHash -LiteralPath $CeremonyOutput -Algorithm SHA512).Hash.ToLowerInvariant()
Write-Output "KEY_CEREMONY_CHALLENGE_PASS sha512=$sha512 output=$CeremonyOutput"
