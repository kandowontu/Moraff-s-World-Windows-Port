param(
    [Parameter(Mandatory = $true)]
    [string]$RevengeDirectory,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [string]$Bcom30Library
)

$ErrorActionPreference = 'Stop'

$sourceDirectory = (Resolve-Path -LiteralPath $RevengeDirectory).Path
$toolDirectory = $PSScriptRoot
$sourceMap = Join-Path $toolDirectory 'revenge_source_map.json'
$runtime = Join-Path $sourceDirectory 'BRUN30.EXE'
$modules = @('BEGIN', 'CHCHAR', 'DUNSMALL', 'F8', 'NCD')

if (-not (Test-Path -LiteralPath $runtime -PathType Leaf)) {
    throw "BRUN30.EXE was not found in $sourceDirectory"
}

foreach ($module in $modules) {
    $executable = Join-Path $sourceDirectory ($module + '.EXE')
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "$module.EXE was not found in $sourceDirectory"
    }
}

$null = New-Item -ItemType Directory -Path $OutputDirectory -Force
$outputDirectoryPath = (Resolve-Path -LiteralPath $OutputDirectory).Path
$runtimeSymbols = Join-Path $outputDirectoryPath 'BRUN30.symbols.json'

if ($Bcom30Library) {
    $library = (Resolve-Path -LiteralPath $Bcom30Library).Path
    & python (Join-Path $toolDirectory 'map_brun30_symbols.py') `
        $library $runtime --json $runtimeSymbols
    if ($LASTEXITCODE -ne 0) { throw 'BRUN30 symbol recovery failed' }
}

$hasRuntimeSymbols = Test-Path -LiteralPath $runtimeSymbols -PathType Leaf

foreach ($module in $modules) {
    $executable = Join-Path $sourceDirectory ($module + '.EXE')
    $dataJson = Join-Path $outputDirectoryPath ($module + '.data.json')
    $readDataJson = Join-Path $outputDirectoryPath ($module + '.read-data.json')
    $assembly = Join-Path $outputDirectoryPath ($module + '.qb3.annotated.asm')
    $report = Join-Path $outputDirectoryPath ($module + '.qb3.annotated.json')
    $instructions = Join-Path $outputDirectoryPath ($module + '.qb3.instructions.json')

    & python (Join-Path $toolDirectory 'extract_qb3_data.py') `
        $executable --json $dataJson
    if ($LASTEXITCODE -ne 0) { throw "$module initialized-data recovery failed" }

    & python (Join-Path $toolDirectory 'extract_qb3_read_data.py') `
        $executable --json $readDataJson
    if ($LASTEXITCODE -ne 0) { throw "$module READ/DATA recovery failed" }

    $arguments = @(
        (Join-Path $toolDirectory 'disassemble_qb3.py'),
        $executable,
        '--runtime', $runtime,
        '--data', $dataJson,
        '--source-map', $sourceMap,
        '--asm', $assembly,
        '--json', $report,
        '--instructions-json', $instructions
    )
    if ($hasRuntimeSymbols) {
        $arguments += @('--runtime-symbols', $runtimeSymbols)
    }
    & python @arguments
    if ($LASTEXITCODE -ne 0) { throw "$module static disassembly failed" }

    $graphics = Join-Path $outputDirectoryPath ($module + '.qb3.graphics.json')
    & python (Join-Path $toolDirectory 'extract_qb3_graphics.py') `
        $instructions --json $graphics
    if ($LASTEXITCODE -ne 0) { throw "$module graphics IR extraction failed" }
}

$runtimeAssembly = Join-Path $outputDirectoryPath 'BRUN30.handlers.asm'
$runtimeReport = Join-Path $outputDirectoryPath 'BRUN30.handlers.json'
$runtimeArguments = @(
    (Join-Path $toolDirectory 'disassemble_brun30.py'),
    $runtime
)
foreach ($module in $modules) {
    $runtimeArguments += @('--program', (Join-Path $sourceDirectory ($module + '.EXE')))
}
if ($hasRuntimeSymbols) {
    $runtimeArguments += @('--symbols', $runtimeSymbols)
}
$runtimeArguments += @('--asm', $runtimeAssembly, '--json', $runtimeReport)
& python @runtimeArguments
if ($LASTEXITCODE -ne 0) { throw 'BRUN30 handler disassembly failed' }

Write-Host "Static Moraff's Revenge disassembly written to $outputDirectoryPath"
