param(
    [string]$Version = "1.1.01"
)

$ErrorActionPreference = "Stop"
$sourceRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildRoot = Join-Path $sourceRoot ("build-release-" + $Version)
$releaseRoot = Join-Path $sourceRoot "release"
$packageName = "Moraffs-World-Native-Port-$Version-win64"
$packageRoot = Join-Path $releaseRoot $packageName
$archivePath = Join-Path $releaseRoot ($packageName + ".zip")
$checksumPath = Join-Path $releaseRoot ($packageName + "-SHA256.txt")

if (Test-Path -LiteralPath $packageRoot) {
    throw "Release directory already exists: $packageRoot"
}
if (Test-Path -LiteralPath $archivePath) {
    throw "Release archive already exists: $archivePath"
}
if (Test-Path -LiteralPath $checksumPath) {
    throw "Release checksum file already exists: $checksumPath"
}

cmake -S $sourceRoot -B $buildRoot -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DMORAFF_COPY_ORIGINAL_DATA=OFF
if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed."
}

cmake --build $buildRoot
if ($LASTEXITCODE -ne 0) {
    throw "Release build failed."
}

New-Item -ItemType Directory -Path $releaseRoot -Force | Out-Null
New-Item -ItemType Directory -Path $packageRoot | Out-Null

$files = [ordered]@{
    (Join-Path $buildRoot "moraffs_world.exe") = "moraffs_world.exe"
    (Join-Path $sourceRoot "README_RELEASE.md") = "README.md"
    (Join-Path $sourceRoot "ORIGINAL_FILES_REQUIRED.md") = "ORIGINAL_FILES_REQUIRED.md"
    (Join-Path $sourceRoot "CREDITS.md") = "CREDITS.md"
    (Join-Path $sourceRoot "THIRD_PARTY_NOTICES.md") = "THIRD_PARTY_NOTICES.md"
    (Join-Path $sourceRoot "LICENSE_PORT.txt") = "LICENSE_PORT.txt"
    (Join-Path $sourceRoot "RELEASE_NOTES.md") = "RELEASE_NOTES.md"
    (Join-Path $sourceRoot "HOTKEYS.md") = "HOTKEYS.md"
    (Join-Path $sourceRoot "EXPERIENCE_MODES.md") = "EXPERIENCE_MODES.md"
    (Join-Path $sourceRoot "DEEP_DUNGEON.md") = "DEEP_DUNGEON.md"
    (Join-Path $sourceRoot "DEEP_SPELLS.md") = "DEEP_SPELLS.md"
    (Join-Path $sourceRoot "COLOSSEUM.md") = "COLOSSEUM.md"
}

foreach ($entry in $files.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $entry.Key -PathType Leaf)) {
        throw "Release input is missing: $($entry.Key)"
    }
    Copy-Item -LiteralPath $entry.Key `
        -Destination (Join-Path $packageRoot $entry.Value)
}

$originalFiles = @(
    "MW.EXE", "WORLD.EXE", "DUNG.BIN", "WORLDMAP.BIN", "H.BIN",
    "WORLD.PIC", "WALL.PIC", "360X480.FNT", "320X200.FNT", "ROLL.TXT"
)
foreach ($name in $originalFiles) {
    if (Test-Path -LiteralPath (Join-Path $packageRoot $name)) {
        throw "Refusing to package copyrighted original file: $name"
    }
}

Compress-Archive -LiteralPath $packageRoot -DestinationPath $archivePath

$exeHash = Get-FileHash -LiteralPath `
    (Join-Path $packageRoot "moraffs_world.exe") -Algorithm SHA256
$zipHash = Get-FileHash -LiteralPath $archivePath -Algorithm SHA256
$checksumLines = @(
    "$($exeHash.Hash.ToLowerInvariant()) *moraffs_world.exe"
    "$($zipHash.Hash.ToLowerInvariant()) *$([System.IO.Path]::GetFileName($archivePath))"
)
Set-Content -LiteralPath $checksumPath -Value $checksumLines -Encoding ascii

Write-Output "Release directory: $packageRoot"
Write-Output "Release archive:   $archivePath"
Write-Output "Checksums:         $checksumPath"
Write-Output "Executable SHA256: $($exeHash.Hash)"
Write-Output "Archive SHA256:    $($zipHash.Hash)"
