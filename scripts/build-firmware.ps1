[CmdletBinding()]
param(
    [ValidateSet('build', 'reconfigure', 'menuconfig', 'flash', 'monitor', 'flash-monitor', 'merge', 'flash-full')]
    [string]$Action = 'build',

    [string]$IdfPath = 'D:\.espressif\v6.0.2\esp-idf',

    [string]$IdfToolsPath = 'C:\Espressif',

    [string]$Port = 'COM27'
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $repositoryRoot 'firmware\luna-panel'
$buildPath = Join-Path $projectPath 'build'
$mergedImage = Join-Path $buildPath 'luna_panel_full.bin'
$exportScript = Join-Path $IdfPath 'export.ps1'

if (-not (Test-Path -LiteralPath $exportScript)) {
    throw "ESP-IDF v6.0.2 is not available at $IdfPath"
}

$env:IDF_TOOLS_PATH = $IdfToolsPath
$idfPython = Get-ChildItem -LiteralPath (Join-Path $IdfToolsPath 'python_env') `
    -Filter 'python.exe' -File -Recurse -ErrorAction SilentlyContinue |
    Where-Object FullName -Match 'idf6\.0_py' |
    Sort-Object FullName -Descending |
    Select-Object -First 1

if ($null -eq $idfPython) {
    $idfPython = Get-ChildItem -LiteralPath (Join-Path $IdfToolsPath 'tools\idf-python') `
    -Filter 'python.exe' -File -Recurse -ErrorAction SilentlyContinue |
    Select-Object -First 1
}

if ($null -eq $idfPython) {
    throw "ESP-IDF Python was not found under $IdfToolsPath"
}

$env:PATH = "$($idfPython.DirectoryName);$env:PATH"
$env:IDF_PYTHON_ENV_PATH = Split-Path -Parent $idfPython.DirectoryName
. $exportScript

Push-Location $projectPath
try {
    if ($Action -in @('merge', 'flash-full')) {
        & idf.py build
        if ($LASTEXITCODE -ne 0) {
            throw "idf.py build failed with exit code $LASTEXITCODE"
        }

        $mergeInputs = @(
            '0x2000', (Join-Path $buildPath 'bootloader\bootloader.bin'),
            '0x10000', (Join-Path $buildPath 'partition_table\partition-table.bin'),
            '0x20000', (Join-Path $buildPath 'luna_panel.bin')
        )
        & $idfPython.FullName -m esptool --chip esp32p4 merge-bin `
            --flash-mode dio --flash-freq 80m --flash-size 32MB `
            -o $mergedImage @mergeInputs
        if ($LASTEXITCODE -ne 0) {
            throw "esptool merge-bin failed with exit code $LASTEXITCODE"
        }

        Write-Host "Merged image: $mergedImage"
        if ($Action -eq 'flash-full') {
            & $idfPython.FullName -m esptool --chip esp32p4 -p $Port -b 460800 `
                --before default-reset --after hard-reset write-flash `
                --flash-mode dio --flash-freq 80m --flash-size 32MB `
                0x0 $mergedImage
            if ($LASTEXITCODE -ne 0) {
                throw "esptool write-flash failed with exit code $LASTEXITCODE"
            }
        }
    }
    elseif ($Action -eq 'flash-monitor') {
        & idf.py -p $Port flash monitor
    }
    elseif ($Action -in @('flash', 'monitor')) {
        & idf.py -p $Port $Action
    }
    else {
        & idf.py $Action
    }

    if (($Action -notin @('merge', 'flash-full')) -and ($LASTEXITCODE -ne 0)) {
        throw "idf.py $Action failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
