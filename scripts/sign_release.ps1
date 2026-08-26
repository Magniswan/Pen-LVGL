[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$DevelopmentPackage,
    [Parameter(Mandatory)][string]$PrivateKey,
    [Parameter(Mandatory)][string]$PublicKey,
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{64}$')][string]$ExpectedPublicKeyHex,
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{128}$')][string]$ExpectedDevelopmentSha512,
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{40}$')][string]$ExpectedSourceCommit,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$Node = "node"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$SignerRepository = Split-Path -Parent $PSScriptRoot
$SignerDevelopment = [IO.Path]::GetFullPath($DevelopmentPackage)
$SignerPrivate = [IO.Path]::GetFullPath($PrivateKey)
$SignerPublic = [IO.Path]::GetFullPath($PublicKey)
$SignerOutput = [IO.Path]::GetFullPath($OutputDirectory)
$SignerForbiddenTestKey = 'd75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a'

function Get-SignerRegularFile {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][long]$MaximumBytes)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
        $item.Length -le 0 -or $item.Length -gt $MaximumBytes) {
        throw "Signer input must be a bounded regular non-link file: $Path"
    }
    return $item
}

function Test-SignerPathWithin {
    param([Parameter(Mandatory)][string]$Child, [Parameter(Mandatory)][string]$Parent)
    $parentPrefix = [IO.Path]::GetFullPath($Parent).TrimEnd('\', '/') +
        [IO.Path]::DirectorySeparatorChar
    return [IO.Path]::GetFullPath($Child).StartsWith(
        $parentPrefix, [StringComparison]::OrdinalIgnoreCase)
}

function Get-LockedSignerNode {
    param([Parameter(Mandatory)][string]$Executable)
    $command = Get-Command $Executable -CommandType Application -ErrorAction Stop
    $resolved = [IO.Path]::GetFullPath($command.Source)
    $item = Get-Item -LiteralPath $resolved -Force
    if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Signer Node executable must be a regular non-link file"
    }
    $version = (& $resolved --version).Trim()
    if ($LASTEXITCODE -ne 0 -or $version -cne 'v18.20.8') {
        throw "Signer tooling requires exactly Node v18.20.8; observed $version"
    }
    return [pscustomobject]@{ Path = $resolved; Version = $version }
}

if ($ExpectedPublicKeyHex -eq ('0' * 64) -or $ExpectedPublicKeyHex -eq $SignerForbiddenTestKey) {
    throw "The all-zero and RFC 8032 test public keys are forbidden"
}
$signerStatus = @(& git -C $SignerRepository status --porcelain=v1 --untracked-files=all)
$signerCommit = (& git -C $SignerRepository rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $signerStatus.Count -ne 0 -or $signerCommit -cne $ExpectedSourceCommit) {
    throw "Offline signer requires the exact reviewed clean source commit"
}
$signerNode = Get-LockedSignerNode -Executable $Node
if (-not $SignerDevelopment.EndsWith('.lvapp.dev', [StringComparison]::Ordinal)) {
    throw "Signer input must be an explicit .lvapp.dev artifact"
}
$null = Get-SignerRegularFile -Path $SignerDevelopment -MaximumBytes (257MB)
$null = Get-SignerRegularFile -Path $SignerPrivate -MaximumBytes (1MB)
$null = Get-SignerRegularFile -Path $SignerPublic -MaximumBytes (1MB)
$developmentSha512 = (Get-FileHash -LiteralPath $SignerDevelopment -Algorithm SHA512).Hash.ToLowerInvariant()
if ($developmentSha512 -cne $ExpectedDevelopmentSha512) {
    throw "Development package does not match the independently approved SHA-512 digest"
}
if (Test-SignerPathWithin -Child $SignerPrivate -Parent $SignerRepository) {
    throw "Private signing keys must never be stored inside the repository"
}
if (Test-Path -LiteralPath $SignerOutput) {
    throw "Refusing to overwrite signer output: $SignerOutput"
}
$outputParent = Split-Path -Parent $SignerOutput
if (-not (Test-Path -LiteralPath $outputParent -PathType Container)) {
    New-Item -ItemType Directory -Path $outputParent | Out-Null
}
$parentItem = Get-Item -LiteralPath $outputParent -Force
if ($parentItem.Attributes -band [IO.FileAttributes]::ReparsePoint) {
    throw "Signer output parent must not be a reparse point"
}

$keyInfoText = @(& $signerNode.Path (Join-Path $SignerRepository 'tools/lvapp/cli.mjs') key-info `
    --public-key $SignerPublic)
if ($LASTEXITCODE -ne 0) { throw "Official public key inspection failed" }
$keyInfo = ($keyInfoText -join "`n") | ConvertFrom-Json
if ($keyInfo.algorithm -ne 'Ed25519' -or $keyInfo.rawPublicKeyHex -cne $ExpectedPublicKeyHex -or
    [string]$keyInfo.keyId -notmatch '^[0-9a-f]{32}$') {
    throw "Public key does not match the exact pinned official trust root"
}

$transaction = Join-Path $outputParent ('.release-sign-' + [guid]::NewGuid().ToString('N'))
$transactionFull = [IO.Path]::GetFullPath($transaction)
try {
    $artifact = Join-Path $transactionFull 'artifact'
    New-Item -ItemType Directory -Path $artifact -Force | Out-Null
    $leaf = [IO.Path]::GetFileName($SignerDevelopment)
    $signedLeaf = $leaf.Substring(0, $leaf.Length - 4)
    $signedPackage = Join-Path $artifact $signedLeaf
    $null = @(& $signerNode.Path (Join-Path $SignerRepository 'tools/lvapp/cli.mjs') sign `
        --input $SignerDevelopment --key $SignerPrivate --out $signedPackage)
    if ($LASTEXITCODE -ne 0) { throw "Offline Ed25519 signing failed" }
    $verificationText = @(& $signerNode.Path (Join-Path $SignerRepository 'tools/lvapp/cli.mjs') verify `
        --input $signedPackage --public-key $SignerPublic)
    if ($LASTEXITCODE -ne 0) { throw "Independent public-key verification failed" }
    $verification = ($verificationText -join "`n") | ConvertFrom-Json
    if (-not $verification.signatureValid -or
        $verification.manifest.signingKeyId -cne $keyInfo.keyId) {
        throw "Signed package identity does not match the pinned official key"
    }
    [IO.File]::WriteAllLines((Join-Path $artifact 'verification.json'), $verificationText,
        [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllLines((Join-Path $artifact 'key-info.json'), $keyInfoText,
        [Text.UTF8Encoding]::new($false))
    $sourceEvidence = [ordered]@{
        schemaVersion = 1
        sourceCommit = $signerCommit
        nodeVersion = $signerNode.Version
        approvedDevelopmentSha512 = $ExpectedDevelopmentSha512
    }
    [IO.File]::WriteAllText((Join-Path $artifact 'source-evidence.json'),
        ($sourceEvidence | ConvertTo-Json -Depth 4) + "`n", [Text.UTF8Encoding]::new($false))
    $sha256 = (Get-FileHash -LiteralPath $signedPackage -Algorithm SHA256).Hash.ToLowerInvariant()
    $sha512 = (Get-FileHash -LiteralPath $signedPackage -Algorithm SHA512).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText((Join-Path $artifact 'checksums.txt'),
        "SHA256 $signedLeaf $sha256`nSHA512 $signedLeaf $sha512`n",
        [Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $artifact -Destination $SignerOutput
} finally {
    $resolvedTransaction = [IO.Path]::GetFullPath($transactionFull)
    $resolvedParent = [IO.Path]::GetFullPath($outputParent).TrimEnd('\', '/') +
        [IO.Path]::DirectorySeparatorChar
    if ($resolvedTransaction.StartsWith($resolvedParent, [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolvedTransaction -PathType Container)) {
        Remove-Item -LiteralPath $resolvedTransaction -Recurse -Force
    }
}

Write-Output "OFFLINE_RELEASE_SIGN_PASS output=$SignerOutput"
