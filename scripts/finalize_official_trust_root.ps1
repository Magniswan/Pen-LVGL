[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Challenge,
    [Parameter(Mandatory)][string]$ProofSignature,
    [Parameter(Mandatory)][string]$PublicKey,
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{64}$')][string]$ExpectedPublicKeyHex,
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{128}$')][string]$ExpectedChallengeSha512,
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{40}$')][string]$ExpectedSourceCommit,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$Node = "node"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$TrustRepository = Split-Path -Parent $PSScriptRoot
$TrustChallenge = [IO.Path]::GetFullPath($Challenge)
$TrustProof = [IO.Path]::GetFullPath($ProofSignature)
$TrustPublic = [IO.Path]::GetFullPath($PublicKey)
$TrustOutput = [IO.Path]::GetFullPath($OutputDirectory)
$TrustForbiddenTestKey = 'd75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a'

function Test-TrustPathWithin {
    param([Parameter(Mandatory)][string]$Child, [Parameter(Mandatory)][string]$Parent)
    $prefix = [IO.Path]::GetFullPath($Parent).TrimEnd('\', '/') +
        [IO.Path]::DirectorySeparatorChar
    return [IO.Path]::GetFullPath($Child).StartsWith(
        $prefix, [StringComparison]::OrdinalIgnoreCase)
}

function Get-TrustRegularFile {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][long]$MaximumBytes)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
        $item.Length -le 0 -or $item.Length -gt $MaximumBytes) {
        throw "Trust-root input must be a bounded regular non-link file: $Path"
    }
    return $item
}

function Get-TrustNode {
    param([Parameter(Mandatory)][string]$Executable)
    $command = Get-Command $Executable -CommandType Application -ErrorAction Stop
    $resolved = [IO.Path]::GetFullPath($command.Source)
    $item = Get-Item -LiteralPath $resolved -Force
    if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Trust-root Node executable must be a regular non-link file"
    }
    $version = (& $resolved --version).Trim()
    if ($LASTEXITCODE -ne 0 -or $version -cne 'v18.20.8') {
        throw "Trust-root finalization requires exactly Node v18.20.8; observed $version"
    }
    return [pscustomobject]@{ Path = $resolved; Version = $version }
}

if ($ExpectedPublicKeyHex -eq ('0' * 64) -or $ExpectedPublicKeyHex -eq $TrustForbiddenTestKey) {
    throw "The all-zero and RFC 8032 test public keys are forbidden"
}
if (Test-TrustPathWithin -Child $TrustOutput -Parent $TrustRepository) {
    throw "Final trust-root evidence must be written outside the source repository"
}
if (Test-Path -LiteralPath $TrustOutput) {
    throw "Refusing to overwrite trust-root output: $TrustOutput"
}
$challengeItem = Get-TrustRegularFile -Path $TrustChallenge -MaximumBytes 16384
$proofItem = Get-TrustRegularFile -Path $TrustProof -MaximumBytes 64
$null = Get-TrustRegularFile -Path $TrustPublic -MaximumBytes 16384
if ($proofItem.Length -ne 64) { throw "Ed25519 proof signature must be exactly 64 bytes" }
$publicText = [IO.File]::ReadAllText($TrustPublic, [Text.UTF8Encoding]::new($false, $true))
if ($publicText -match 'PRIVATE KEY') { throw "Trust-root finalization accepts public material only" }
$challengeText = [IO.File]::ReadAllText($TrustChallenge, [Text.UTF8Encoding]::new($false, $true))
if ($challengeText.Contains("`r") -or -not $challengeText.EndsWith("`n")) {
    throw "Key ceremony challenge must be canonical UTF-8 with LF endings"
}
$challengeSha512 = (Get-FileHash -LiteralPath $TrustChallenge -Algorithm SHA512).Hash.ToLowerInvariant()
if ($challengeSha512 -cne $ExpectedChallengeSha512) {
    throw "Challenge does not match the independently approved SHA-512 digest"
}
$status = @(& git -C $TrustRepository status --porcelain=v1 --untracked-files=all)
$commit = (& git -C $TrustRepository rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $status.Count -ne 0 -or $commit -cne $ExpectedSourceCommit) {
    throw "Trust-root finalization requires the exact reviewed clean source commit"
}
$nodeInfo = Get-TrustNode -Executable $Node
$challengeData = $challengeText | ConvertFrom-Json
if ($challengeData.domain -cne 'lvgl-platform-official-key-ceremony-v1' -or
    $challengeData.algorithm -cne 'Ed25519' -or
    $challengeData.rawPublicKeyHex -cne $ExpectedPublicKeyHex -or
    $challengeData.sourceCommit -cne $commit -or
    [string]$challengeData.keyId -notmatch '^[0-9a-f]{32}$' -or
    [string]$challengeData.ceremonyId -notmatch '^[a-z0-9][a-z0-9._-]{7,63}$' -or
    [string]$challengeData.activationUtc -notmatch '^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$' -or
    [long]$challengeData.securityEpoch -lt 0 -or
    @($challengeData.witnesses).Count -ne 2 -or
    $challengeData.witnesses[0].role -cne 'primary' -or
    $challengeData.witnesses[1].role -cne 'secondary' -or
    $challengeData.witnesses[0].id -ceq $challengeData.witnesses[1].id) {
    throw "Challenge policy fields are incomplete or inconsistent"
}
$publicSha256 = (Get-FileHash -LiteralPath $TrustPublic -Algorithm SHA256).Hash.ToLowerInvariant()
if ($challengeData.publicKeyPemSha256 -cne $publicSha256 -or
    $challengeData.nodeVersion -cne $nodeInfo.Version) {
    throw "Challenge public key or locked tool identity changed"
}
$proofText = @(& $nodeInfo.Path (Join-Path $TrustRepository 'tools/lvapp/cli.mjs') `
    verify-key-proof --challenge $TrustChallenge --signature $TrustProof --public-key $TrustPublic)
if ($LASTEXITCODE -ne 0) { throw "Official key proof-of-possession verification failed" }
$proof = ($proofText -join "`n") | ConvertFrom-Json
if (-not $proof.proofValid -or $proof.challengeSha512 -cne $ExpectedChallengeSha512 -or
    $proof.rawPublicKeyHex -cne $ExpectedPublicKeyHex -or $proof.keyId -cne $challengeData.keyId) {
    throw "Proof does not bind the approved challenge to the pinned official key"
}

$parent = Split-Path -Parent $TrustOutput
if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
    New-Item -ItemType Directory -Path $parent | Out-Null
}
$parentItem = Get-Item -LiteralPath $parent -Force
if ($parentItem.Attributes -band [IO.FileAttributes]::ReparsePoint) {
    throw "Trust-root output parent must not be a reparse point"
}
$transaction = Join-Path $parent ('.trust-root-' + [guid]::NewGuid().ToString('N'))
try {
    $artifact = Join-Path $transaction 'artifact'
    New-Item -ItemType Directory -Path $artifact -Force | Out-Null
    Copy-Item -LiteralPath $TrustChallenge -Destination (Join-Path $artifact 'official-key-challenge.json')
    Copy-Item -LiteralPath $TrustProof -Destination (Join-Path $artifact 'official-key-proof.bin')
    Copy-Item -LiteralPath $TrustPublic -Destination (Join-Path $artifact 'official-public-key.pem')
    [IO.File]::WriteAllText((Join-Path $artifact 'proof-verification.json'),
        (($proof | ConvertTo-Json -Depth 4) -replace "`r`n", "`n") + "`n",
        [Text.UTF8Encoding]::new($false))
    $trustRoot = [ordered]@{
        schemaVersion = 1
        policy = 'single-official-ed25519-offline-v1'
        ceremonyId = [string]$challengeData.ceremonyId
        algorithm = 'Ed25519'
        keyId = [string]$challengeData.keyId
        rawPublicKeyHex = $ExpectedPublicKeyHex
        publicKeyPemSha256 = $publicSha256
        challengeSha512 = $ExpectedChallengeSha512
        proofSha256 = (Get-FileHash -LiteralPath $TrustProof -Algorithm SHA256).Hash.ToLowerInvariant()
        securityEpoch = [long]$challengeData.securityEpoch
        activationUtc = [string]$challengeData.activationUtc
        sourceCommit = $commit
        witnesses = @($challengeData.witnesses)
        privateKeyMaterialAccepted = $false
    }
    [IO.File]::WriteAllText((Join-Path $artifact 'official-trust-root.json'),
        (($trustRoot | ConvertTo-Json -Depth 5) -replace "`r`n", "`n") + "`n",
        [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $artifact 'cmake-arguments.txt'),
        "-DLVGL_PLATFORM_PRODUCTION_BUILD=ON`n" +
        "-DLVGL_PLATFORM_OFFICIAL_PUBLIC_KEY_HEX=$ExpectedPublicKeyHex`n",
        [Text.UTF8Encoding]::new($false))
    $checksumLines = foreach ($name in @(
        'cmake-arguments.txt', 'official-key-challenge.json', 'official-key-proof.bin',
        'official-public-key.pem', 'official-trust-root.json', 'proof-verification.json')) {
        $hash = (Get-FileHash -LiteralPath (Join-Path $artifact $name) -Algorithm SHA256).Hash.ToLowerInvariant()
        "SHA256 $name $hash"
    }
    [IO.File]::WriteAllText((Join-Path $artifact 'checksums.txt'),
        ($checksumLines -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $artifact -Destination $TrustOutput
} finally {
    $resolved = [IO.Path]::GetFullPath($transaction)
    $prefix = [IO.Path]::GetFullPath($parent).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if ($resolved.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolved -PathType Container)) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
Write-Output "OFFICIAL_TRUST_ROOT_FINALIZE_PASS output=$TrustOutput"
