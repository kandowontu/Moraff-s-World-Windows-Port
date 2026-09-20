param(
    [Parameter(Mandatory = $true)]
    [string]$VhdPath,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [string]$SevenZipPath = "",

    [string]$ExpectedSha256 = ""
)

$ErrorActionPreference = "Stop"

$source = (Resolve-Path -LiteralPath $VhdPath).Path
if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
    throw "VHD does not exist: $source"
}

if (-not $SevenZipPath) {
    $candidates = @(
        (Join-Path $env:ProgramFiles "7-Zip\7z.exe"),
        (Get-Command 7z.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1)
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) }
    if (-not $candidates) {
        throw "7-Zip was not found. Pass -SevenZipPath explicitly."
    }
    $SevenZipPath = $candidates[0]
} else {
    $SevenZipPath = (Resolve-Path -LiteralPath $SevenZipPath).Path
}

$actualHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
if ($ExpectedSha256 -and
    $actualHash -ne $ExpectedSha256.Replace(" ", "").ToUpperInvariant()) {
    throw "VHD SHA-256 mismatch. Expected $ExpectedSha256, found $actualHash."
}

$destination = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $destination) {
    if (Get-ChildItem -LiteralPath $destination -Force | Select-Object -First 1) {
        throw "Output directory is not empty: $destination"
    }
} else {
    New-Item -ItemType Directory -Path $destination | Out-Null
}

& $SevenZipPath x -y "-o$destination" $source "Revenge\*"
if ($LASTEXITCODE -ne 0) {
    throw "7-Zip extraction failed with exit code $LASTEXITCODE."
}

$gameDirectory = Join-Path $destination "Revenge"
if (-not (Test-Path -LiteralPath (Join-Path $gameDirectory "DUNSMALL.EXE") -PathType Leaf)) {
    throw "The VHD did not contain Revenge\DUNSMALL.EXE."
}

$files = Get-ChildItem -LiteralPath $gameDirectory -File | Sort-Object Name | ForEach-Object {
    [ordered]@{
        name = $_.Name
        length = $_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
$manifest = [ordered]@{
    source_name = [IO.Path]::GetFileName($source)
    source_length = (Get-Item -LiteralPath $source).Length
    source_sha256 = $actualHash.ToLowerInvariant()
    extracted_directory = $gameDirectory
    files = @($files)
}
$manifestPath = Join-Path $destination "revenge-vhd-manifest.json"
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestPath -Encoding utf8

Write-Output "Extracted game: $gameDirectory"
Write-Output "Manifest:       $manifestPath"
Write-Output "VHD SHA-256:    $($actualHash.ToLowerInvariant())"
