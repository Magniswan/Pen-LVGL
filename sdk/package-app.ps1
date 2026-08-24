[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Manifest,
    [Parameter(Mandatory)][string]$Executable,
    [Parameter(Mandatory)][string]$Output,
    [string]$Node = "node"
)

$ErrorActionPreference = "Stop"
$RepositoryRoot = Split-Path -Parent $PSScriptRoot
$manifestPath = [IO.Path]::GetFullPath($Manifest)
$executablePath = [IO.Path]::GetFullPath($Executable)
$outputPath = [IO.Path]::GetFullPath($Output)

if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Manifest not found: $manifestPath"
}
if (-not (Test-Path -LiteralPath $executablePath -PathType Leaf)) {
    throw "Executable not found: $executablePath"
}
if (Test-Path -LiteralPath $outputPath) {
    throw "Refusing to overwrite output: $outputPath"
}
if (-not $outputPath.EndsWith('.lvapp.dev', [StringComparison]::Ordinal)) {
    throw "Development package output must end in .lvapp.dev"
}

$source = Get-Content -Raw -Encoding UTF8 -LiteralPath $manifestPath | ConvertFrom-Json
$entry = [string]$source.entry
if ($entry -notmatch '^[a-zA-Z0-9._-]+(?:/[a-zA-Z0-9._-]+)*$' -or
    $entry.StartsWith('/') -or $entry.Contains('..') -or $entry.Contains('\')) {
    throw "Manifest entry is not a safe package path: $entry"
}

$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) ("lvapp-stage-" + [guid]::NewGuid().ToString('N'))
$resolvedTempParent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
try {
    New-Item -ItemType Directory -Path $temporaryRoot | Out-Null
    $entryPath = Join-Path $temporaryRoot ($entry.Replace('/', [IO.Path]::DirectorySeparatorChar))
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $entryPath) | Out-Null
    Copy-Item -LiteralPath $executablePath -Destination $entryPath

    & $Node (Join-Path $RepositoryRoot "tools/lvapp/cli.mjs") build `
        --manifest $manifestPath --root $temporaryRoot --out $outputPath
    if ($LASTEXITCODE -ne 0) { throw "LVAPP development package build failed" }
} finally {
    $resolvedTemporary = [IO.Path]::GetFullPath($temporaryRoot)
    if ($resolvedTemporary.StartsWith($resolvedTempParent, [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolvedTemporary -PathType Container)) {
        Remove-Item -LiteralPath $resolvedTemporary -Recurse -Force
    }
}

Write-Output "LVAPP_DEV_BUILD_PASS output=$outputPath"
