[CmdletBinding()]
param(
    [ValidateSet('build', 'reconfigure', 'flash', 'monitor')]
    [string]$Action = 'build',

    [string]$IdfPath,

    [string]$IdfToolsPath,

    [string]$Port,

    [switch]$BleB0,
    [switch]$BleB1
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $repositoryRoot 'firmware\luna-panel'
if ($BleB0 -and $BleB1) { throw 'Choose one BLE profile.' }
$BleB1 = -not $BleB0

if (-not $IdfPath) { $IdfPath = $env:IDF_PATH }
if (-not $IdfPath) {
    throw 'Set IDF_PATH to ESP-IDF 6.0.2 or pass -IdfPath explicitly.'
}
if (-not (Test-Path -LiteralPath $IdfPath -PathType Container)) {
    throw "ESP-IDF 6.0.2 directory was not found at $IdfPath"
}
if (-not $IdfToolsPath) { $IdfToolsPath = $env:IDF_TOOLS_PATH }
if (-not $IdfToolsPath) {
    $IdfToolsPath = Join-Path ([Environment]::GetFolderPath('UserProfile')) '.espressif'
}
if ($Action -in @('flash', 'monitor') -and -not $Port) {
    throw "Specify -Port for $Action after checking the current device port."
}

$buildPath = Join-Path $projectPath $(if ($BleB1) { 'build-ble-b1' } else { 'build-ble-b0' })
$exportScript = Join-Path $IdfPath 'export.ps1'

if (-not (Test-Path -LiteralPath $exportScript)) {
    throw "ESP-IDF v6.0.2 is not available at $IdfPath"
}

$env:IDF_PATH = $IdfPath
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
    $profileArguments = @('-B', $buildPath, '-D', 'LUNA_BLE_B0_BUILD=ON',
      '-D', "LUNA_BLE_B1_BUILD=$(if ($BleB1) {'ON'} else {'OFF'})",
      '-D', "SDKCONFIG=$(Join-Path $projectPath $(if ($BleB1) {'sdkconfig.ble_b1'} else {'sdkconfig.ble_b0'}))")
    [string[]]$arguments = if ($Action -in @('flash', 'monitor')) { @('-p', $Port, $Action) } else { @($Action) }
    & idf.py @profileArguments @arguments 2>&1 | ForEach-Object {
        $line = [string]$_
        if ($line -match 'LUNA_WIFI_SSID|LUNA_WIFI_PASSWORD|Using default value from sdkconfig') {
            Write-Output '[ESP-IDF local credential line redacted]'
        } else { Write-Output $line }
    }
    if ($LASTEXITCODE -ne 0) { throw "idf.py $Action failed: $LASTEXITCODE" }
} finally { Pop-Location }
