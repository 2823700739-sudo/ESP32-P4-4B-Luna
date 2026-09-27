[CmdletBinding()]
param(
    [string]$Python = ''
)

$ErrorActionPreference = 'Stop'
$agentRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$entryPoint = Join-Path $agentRoot 'luna_agent.py'
$configuration = Join-Path $agentRoot 'config.local.json'
$venvPython = Join-Path $agentRoot '.venv\Scripts\python.exe'

if (-not (Test-Path -LiteralPath $configuration)) {
    throw "Missing $configuration. Create it from config.example.json first."
}

if ([string]::IsNullOrWhiteSpace($Python)) {
    if (-not (Test-Path -LiteralPath $venvPython)) {
        throw "The Windows agent environment is missing. Run .\pc-agent\setup-agent.ps1 first."
    }
    $Python = $venvPython
}

& $Python $entryPoint
if ($LASTEXITCODE -ne 0) {
    throw "Luna agent stopped with exit code $LASTEXITCODE"
}
