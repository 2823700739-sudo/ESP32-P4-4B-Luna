[CmdletBinding()]
param(
    [ValidateRange(1, 86400)]
    [int]$DurationSeconds = 600,
    [ValidateRange(1, 3600)]
    [int]$IntervalSeconds = 10,
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
$agentRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$configurationPath = Join-Path $agentRoot 'config.local.json'
if (-not (Test-Path -LiteralPath $configurationPath)) {
    throw "Missing $configurationPath. Configure the Luna Agent first."
}
$configuration = Get-Content -LiteralPath $configurationPath -Raw | ConvertFrom-Json
if (-not $configuration.token) {
    throw 'The Luna Agent configuration has no token.'
}
$agentPort = if ($configuration.port) { [int]$configuration.port } else { 8765 }
$uri = "http://127.0.0.1:$agentPort/api/v1/diagnostics"
if (-not $OutputPath) {
    $stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')
    $OutputPath = Join-Path $agentRoot "usb-soak-$stamp.csv"
}
$outputFile = [System.IO.Path]::GetFullPath($OutputPath)
if (Test-Path -LiteralPath $outputFile) {
    throw "Output already exists: $outputFile"
}
$outputDirectory = Split-Path -Parent $outputFile
if (-not (Test-Path -LiteralPath $outputDirectory)) {
    throw "Output directory does not exist: $outputDirectory"
}

$timer = [System.Diagnostics.Stopwatch]::StartNew()
$samples = 0
$failures = 0
$previousDisconnects = $null
$previousErrors = $null
Write-Host "Recording Luna USB link to $outputFile"
do {
    $sample = [ordered]@{
        timestamp_utc = (Get-Date).ToUniversalTime().ToString('o')
        connected = $false
        port = ''
        connected_seconds = ''
        connections = ''
        disconnects = ''
        errors_total = ''
        consecutive_errors = ''
        state_requests = ''
        action_requests = ''
        cover_info_requests = ''
        cover_chunk_requests = ''
        last_snapshot_at = ''
        error = ''
    }
    try {
        $result = Invoke-RestMethod -Uri $uri -Headers @{ 'X-Luna-Token' = $configuration.token } `
            -TimeoutSec 3
        if (-not $result.ok -or -not $result.usb) {
            throw 'Invalid diagnostics response.'
        }
        $usb = $result.usb
        foreach ($key in @('connected', 'port', 'connected_seconds', 'connections',
                           'disconnects', 'errors_total', 'consecutive_errors',
                           'state_requests', 'action_requests', 'cover_info_requests',
                           'cover_chunk_requests')) {
            $sample[$key] = $usb.$key
        }
        if ($usb.last_snapshot_at -is [datetime]) {
            $sample.last_snapshot_at = $usb.last_snapshot_at.ToUniversalTime().ToString('o')
        }
        else {
            $sample.last_snapshot_at = $usb.last_snapshot_at
        }
        $observations = @()
        if (-not $usb.connected) { $observations += 'USB disconnected at sample time' }
        if ($null -ne $previousDisconnects -and
            [int]$usb.disconnects -gt $previousDisconnects) {
            $observations += 'USB disconnect counter increased'
        }
        if ($null -ne $previousErrors -and [int]$usb.errors_total -gt $previousErrors) {
            $observations += 'USB error counter increased'
        }
        $previousDisconnects = [int]$usb.disconnects
        $previousErrors = [int]$usb.errors_total
        if ($observations.Count -gt 0) {
            $sample.error = $observations -join '; '
            $failures++
        }
    }
    catch {
        $sample.error = $_.Exception.Message
        $failures++
    }
    [pscustomobject]$sample | Export-Csv -LiteralPath $outputFile -NoTypeInformation `
        -Encoding UTF8 -Append
    $samples++
    if ($timer.Elapsed.TotalSeconds -ge $DurationSeconds) { break }
    $remainingMilliseconds = [int](($DurationSeconds - $timer.Elapsed.TotalSeconds) * 1000)
    if ($remainingMilliseconds -gt 0) {
        Start-Sleep -Milliseconds ([Math]::Min($IntervalSeconds * 1000, $remainingMilliseconds))
    }
} while ($true)

Write-Host "Samples: $samples; samples with disconnects or errors: $failures"
Write-Host "CSV: $outputFile"
if ($failures -gt 0) {
    throw 'Luna USB link had disconnected or failed samples. Inspect the CSV.'
}
