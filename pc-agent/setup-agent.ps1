[CmdletBinding()]
param(
    [string]$Python = 'python'
)

$ErrorActionPreference = 'Stop'
$agentRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$venvRoot = Join-Path $agentRoot '.venv'
$venvPython = Join-Path $venvRoot 'Scripts\python.exe'
$requirements = Join-Path $agentRoot 'requirements.txt'

if (-not (Test-Path -LiteralPath $venvPython)) {
    & $Python -m venv $venvRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to create the Windows agent Python environment (exit code $LASTEXITCODE)."
    }
}

& $venvPython -m pip install --disable-pip-version-check -r $requirements
if ($LASTEXITCODE -ne 0) {
    throw "Failed to install the Windows agent dependencies (exit code $LASTEXITCODE)."
}

Write-Host 'Windows agent environment is ready.'
