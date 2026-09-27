[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$Location,

    [Parameter(Mandatory)]
    [ValidateRange(-90.0, 90.0)]
    [double]$Latitude,

    [Parameter(Mandatory)]
    [ValidateRange(-180.0, 180.0)]
    [double]$Longitude,

    [ValidateRange(5, 1440)]
    [int]$RefreshMinutes = 20
)

$ErrorActionPreference = 'Stop'
$agentRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$configuration = Join-Path $agentRoot 'config.local.json'
if (-not (Test-Path -LiteralPath $configuration)) {
    throw "Missing $configuration. Create it from config.example.json first."
}

$config = Get-Content -LiteralPath $configuration -Raw -Encoding UTF8 | ConvertFrom-Json
$weather = [ordered]@{
    location = $Location.Trim()
    latitude = $Latitude
    longitude = $Longitude
    refresh_minutes = $RefreshMinutes
}
if ([string]::IsNullOrWhiteSpace($weather.location)) {
    throw 'Location must not be empty.'
}

if ($null -eq $config.PSObject.Properties['weather']) {
    $config | Add-Member -NotePropertyName weather -NotePropertyValue $weather
} else {
    $config.weather = $weather
}

$json = $config | ConvertTo-Json -Depth 8
$utf8WithoutBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($configuration, $json, $utf8WithoutBom)
Write-Host "Weather location saved: $($weather.location) ($Latitude, $Longitude)"
Write-Host 'Restart .\pc-agent\start-agent.ps1 to apply it.'
