[CmdletBinding()]
param([switch]$Foreground, [switch]$Music)
$ErrorActionPreference = 'Stop'
$pythonName = if ($Foreground) { 'python.exe' } else { 'pythonw.exe' }
$pythonPath = Join-Path $PSScriptRoot ".venv-ble\Scripts\$pythonName"
$entryPath = Join-Path $PSScriptRoot 'luna_ble_link.py'
if (-not (Test-Path -LiteralPath $pythonPath)) {
    throw 'Install the separate .venv-ble environment using requirements-ble.txt first.'
}
if ($Foreground) {
    $lunaTask = Get-ScheduledTask -TaskName 'Luna BLE Agent' -ErrorAction SilentlyContinue
    if ($lunaTask -and $lunaTask.State -eq 'Running') {
        throw 'Stop the Luna BLE Agent task before foreground diagnostics; start it again afterward.'
    }
    $options = if ($Music) { @('--music') } else { @() }
    & $pythonPath $entryPath @options
} else {
    $lunaTask = Get-ScheduledTask -TaskName 'Luna BLE Agent' -ErrorAction SilentlyContinue
    if ($lunaTask) {
        if (-not $Music) { Write-Host 'The installed Luna BLE resident includes music synchronization.' }
        & (Join-Path $PSScriptRoot 'manage-ble-resident.ps1') -Action Start
        return
    }
    $entryArgument = '"' + $entryPath + '"'
    $options = @($entryArgument, '--background')
    if ($Music) { $options += '--music' }
    $process = Start-Process -FilePath $pythonPath -ArgumentList $options -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -PassThru
    Write-Host "Luna BLE link launch PID: $($process.Id). Log: $(Join-Path $PSScriptRoot 'ble-link.log')"
    Write-Host 'Current Luna BLE companion; no COM ports opened. Login startup is not installed.'
}
