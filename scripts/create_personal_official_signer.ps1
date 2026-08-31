[CmdletBinding()]
param(
    [string]$SignerRoot = "C:\Users\sunho\.lvgl-official-signer",
    [string]$Node = "node"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$Repository = Split-Path -Parent $PSScriptRoot
$Root = [IO.Path]::GetFullPath($SignerRoot)
$ForbiddenTestKey = 'd75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a'

function Test-Within {
    param([Parameter(Mandatory)][string]$Child, [Parameter(Mandatory)][string]$Parent)
    $prefix = [IO.Path]::GetFullPath($Parent).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    return [IO.Path]::GetFullPath($Child).StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)
}

function Assert-RegularNode {
    param([Parameter(Mandatory)][string]$Executable)
    $command = Get-Command $Executable -CommandType Application -ErrorAction Stop
    $resolved = [IO.Path]::GetFullPath($command.Source)
    $item = Get-Item -LiteralPath $resolved -Force
    if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Node executable must be a regular non-link file"
    }
    $version = (& $resolved --version).Trim()
    if ($LASTEXITCODE -ne 0 -or $version -cne 'v18.20.8') {
        throw "Personal signer requires exactly Node v18.20.8; observed $version"
    }
    return $resolved
}

if (Test-Within -Child $Root -Parent $Repository) {
    throw "Personal signer root must stay outside the source repository"
}
if (Test-Path -LiteralPath $Root -PathType Leaf) {
    throw "Personal signer root must be a directory"
}
New-Item -ItemType Directory -Path $Root -Force | Out-Null
$rootItem = Get-Item -LiteralPath $Root -Force
if ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) {
    throw "Personal signer root must not be a reparse point"
}
$node = Assert-RegularNode -Executable $Node
$privateDirectory = Join-Path $Root 'private'
$publicDirectory = Join-Path $Root 'public'
$evidenceDirectory = Join-Path $Root 'evidence'
$privateKey = Join-Path $privateDirectory 'official-ed25519-private.pem'
$publicKey = Join-Path $publicDirectory 'official-ed25519-public.pem'
$challenge = Join-Path $evidenceDirectory 'personal-key-challenge.json'
$proof = Join-Path $evidenceDirectory 'personal-key-proof.bin'
$trustRoot = Join-Path $evidenceDirectory 'personal-official-trust-root.json'

foreach ($path in @($privateKey, $publicKey, $challenge, $proof, $trustRoot)) {
    if (Test-Path -LiteralPath $path) {
        throw "Refusing to overwrite existing personal signer material: $path"
    }
}
$account = [Security.Principal.WindowsIdentity]::GetCurrent().Name
& icacls.exe $Root /inheritance:r /grant:r "$account`:(OI)(CI)F" "SYSTEM:(OI)(CI)F" | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Could not apply signer-root ACL" }
foreach ($directory in @($privateDirectory, $publicDirectory, $evidenceDirectory)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}

$temporaryScript = Join-Path $evidenceDirectory ('.generate-key-' + [guid]::NewGuid().ToString('N') + '.mjs')
$generator = @'
import { generateKeyPairSync, sign } from 'node:crypto';
import { writeFileSync } from 'node:fs';

const [privatePath, publicPath, challengePath, proofPath, activationUtc] = process.argv.slice(2);
const { privateKey, publicKey } = generateKeyPairSync('ed25519');
const challenge = Buffer.from(JSON.stringify({
  domain: 'lvgl-platform-personal-official-key-v1',
  algorithm: 'Ed25519',
  activationUtc,
}) + '\n', 'utf8');
writeFileSync(privatePath, privateKey.export({ type: 'pkcs8', format: 'pem' }), { encoding: 'utf8', mode: 0o600, flag: 'wx' });
writeFileSync(publicPath, publicKey.export({ type: 'spki', format: 'pem' }), { encoding: 'utf8', mode: 0o644, flag: 'wx' });
writeFileSync(challengePath, challenge, { mode: 0o600, flag: 'wx' });
writeFileSync(proofPath, sign(null, challenge, privateKey), { mode: 0o600, flag: 'wx' });
'@

try {
    [IO.File]::WriteAllText($temporaryScript, $generator, [Text.UTF8Encoding]::new($false))
    $activation = [DateTimeOffset]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    & $node $temporaryScript $privateKey $publicKey $challenge $proof $activation
    if ($LASTEXITCODE -ne 0) { throw "Ed25519 key generation failed" }
} finally {
    if (Test-Path -LiteralPath $temporaryScript -PathType Leaf) {
        Remove-Item -LiteralPath $temporaryScript -Force
    }
}

& icacls.exe $privateDirectory /inheritance:r /grant:r "$account`:(OI)(CI)F" "SYSTEM:(OI)(CI)F" | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Could not apply private-key ACL" }

$keyInfoText = @(& $node (Join-Path $Repository 'tools/lvapp/cli.mjs') key-info --public-key $publicKey)
if ($LASTEXITCODE -ne 0) { throw "Could not inspect generated official public key" }
$keyInfo = ($keyInfoText -join "`n") | ConvertFrom-Json
if ($keyInfo.algorithm -ne 'Ed25519' -or $keyInfo.rawPublicKeyHex -notmatch '^[0-9a-f]{64}$' -or
    $keyInfo.rawPublicKeyHex -eq ('0' * 64) -or $keyInfo.rawPublicKeyHex -eq $ForbiddenTestKey -or
    [string]$keyInfo.keyId -notmatch '^[0-9a-f]{32}$') {
    throw "Generated public key does not meet personal official-root policy"
}

$proofText = @(& $node (Join-Path $Repository 'tools/lvapp/cli.mjs') verify-key-proof `
    --challenge $challenge --signature $proof --public-key $publicKey)
if ($LASTEXITCODE -ne 0) { throw "Generated key proof could not be verified" }
$proofInfo = ($proofText -join "`n") | ConvertFrom-Json
if (-not $proofInfo.proofValid) { throw "Generated key proof is invalid" }

$trust = [ordered]@{
    schemaVersion = 1
    policy = 'single-official-ed25519-personal-v1'
    protection = 'plaintext-local-single-operator'
    commercialCertification = $false
    algorithm = 'Ed25519'
    keyId = [string]$keyInfo.keyId
    rawPublicKeyHex = [string]$keyInfo.rawPublicKeyHex
    publicKeyPemSha256 = (Get-FileHash -LiteralPath $publicKey -Algorithm SHA256).Hash.ToLowerInvariant()
    challengeSha512 = (Get-FileHash -LiteralPath $challenge -Algorithm SHA512).Hash.ToLowerInvariant()
    proofSha256 = (Get-FileHash -LiteralPath $proof -Algorithm SHA256).Hash.ToLowerInvariant()
    activationUtc = $activation
    sourceCommit = (& git -C $Repository rev-parse HEAD).Trim()
    nodeVersion = (& $node --version).Trim()
    privateKeyMaterialAccepted = $true
}
[IO.File]::WriteAllText($trustRoot, ($trust | ConvertTo-Json -Depth 4) + "`n",
    [Text.UTF8Encoding]::new($false))

$checksums = foreach ($item in @($publicKey, $challenge, $proof, $trustRoot)) {
    "SHA256 $([IO.Path]::GetFileName($item)) $((Get-FileHash -LiteralPath $item -Algorithm SHA256).Hash.ToLowerInvariant())"
}
[IO.File]::WriteAllText((Join-Path $evidenceDirectory 'checksums.txt'),
    ($checksums -join "`n") + "`n", [Text.UTF8Encoding]::new($false))

Write-Output "PERSONAL_OFFICIAL_SIGNER_PASS key_id=$($keyInfo.keyId) public_key_hex=$($keyInfo.rawPublicKeyHex) root=$Root"
