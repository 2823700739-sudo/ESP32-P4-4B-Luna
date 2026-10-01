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
$previousAgentStart = $null
Write-Host "Recording Luna USB link to $outputFile"
do {
    $sample = [ordered]@{
        timestamp_utc = (Get-Date).ToUniversalTime().ToString('o')
        agent_started_at = ''
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
        last_frame_at = ''
        last_frame_age_seconds = ''
        error = ''
    }
    try {
        $result = Invoke-RestMethod -Uri $uri -Headers @{ 'X-Luna-Token' = $configuration.token } `
            -TimeoutSec 3
        if (-not $result.ok -or -not $result.usb) {
            throw 'Invalid diagnostics response.'
        }
        $usb = $result.usb
        if ($result.agent_started_at -is [datetime]) {
            $sample.agent_started_at = $result.agent_started_at.ToUniversalTime().ToString('o')
        }
        else {
            $sample.agent_started_at = [string]$result.agent_started_at
        }
        foreach ($key in @('connected', 'port', 'connected_seconds', 'connections',
                           'disconnects', 'errors_total', 'consecutive_errors',
                           'state_requests', 'action_requests', 'cover_info_requests',
                           'cover_chunk_requests', 'last_frame_age_seconds')) {
            $sample[$key] = $usb.$key
        }
        foreach ($key in @('last_snapshot_at', 'last_frame_at')) {
            if ($usb.$key -is [datetime]) {
                $sample[$key] = $usb.$key.ToUniversalTime().ToString('o')
            }
            else {
                $sample[$key] = $usb.$key
            }
        }
        $observations = @()
        if (-not $usb.connected) { $observations += 'USB disconnected at sample time' }
        if ($previousAgentStart -and $sample.agent_started_at -ne $previousAgentStart) {
            $observations += 'Agent restarted'
        }
        if ($null -ne $previousDisconnects -and
            [int]$usb.disconnects -lt $previousDisconnects) {
            $observations += 'USB disconnect counter reset'
        }
        if ($null -ne $previousErrors -and [int]$usb.errors_total -lt $previousErrors) {
            $observations += 'USB error counter reset'
        }
        if ($null -ne $previousDisconnects -and
            [int]$usb.disconnects -gt $previousDisconnects) {
            $observations += 'USB disconnect counter increased'
        }
        if ($null -ne $previousErrors -and [int]$usb.errors_total -gt $previousErrors) {
            $observations += 'USB error counter increased'
        }
        $previousDisconnects = [int]$usb.disconnects
        $previousErrors = [int]$usb.errors_total
        $previousAgentStart = $sample.agent_started_at
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

Write-Host "Samples: $samples; samples with issues: $failures"
Write-Host "CSV: $outputFile"
if ($failures -gt 0) {
    throw 'Luna link monitoring detected an issue. Inspect the CSV.'
}
