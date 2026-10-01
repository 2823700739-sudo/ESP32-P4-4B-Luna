[CmdletBinding()]
param(
    [ValidateSet('install', 'status', 'start', 'stop', 'remove')]
    [string]$Mode = 'status',
    [switch]$StartNow
)

$ErrorActionPreference = 'Stop'
$taskName = 'Luna PC Agent'
$description = 'Luna PC Agent (ESP32-P4-4B-Luna)'
$agentRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$backgroundScript = Join-Path $agentRoot 'background_agent.py'
$configuration = Join-Path $agentRoot 'config.local.json'
$venvPython = Join-Path $agentRoot '.venv\Scripts\python.exe'
$venvConfig = Join-Path $agentRoot '.venv\pyvenv.cfg'
$task = Get-ScheduledTask -TaskName $taskName -TaskPath '\' -ErrorAction SilentlyContinue

if ($StartNow -and $Mode -ne 'install') {
    throw '-StartNow is only valid with -Mode install.'
}

if ($task -and $task.Description -ne $description) {
    throw "A different scheduled task already uses the name '$taskName'."
}

if ($Mode -eq 'status') {
    if ($task) {
        Write-Host "Luna auto-start: $($task.State)"
        Write-Host "Action: $($task.Actions[0].Execute) $($task.Actions[0].Arguments)"
    }
    else {
        Write-Host 'Luna auto-start is not installed.'
    }
    return
}

if ($Mode -eq 'start') {
    if (-not $task) { throw 'Luna auto-start is not installed.' }
    Start-ScheduledTask -TaskName $taskName -TaskPath '\'
    Write-Host 'Luna auto-start task launched.'
    return
}

if ($Mode -eq 'stop') {
    if (-not $task) { throw 'Luna auto-start is not installed.' }
    if ($task.State -eq 'Running') {
        Stop-ScheduledTask -TaskName $taskName -TaskPath '\'
        Write-Host 'Luna auto-start task stopped. Login auto-start remains enabled.'
    }
    else {
        Write-Host 'Luna auto-start task is not running. A manually started Agent is unaffected.'
    }
    return
}

if ($Mode -eq 'remove') {
    if ($task) {
        Unregister-ScheduledTask -TaskName $taskName -TaskPath '\' -Confirm:$false
        Write-Host 'Luna auto-start removed. Any currently running Agent is unchanged.'
    }
    else {
        Write-Host 'Luna auto-start was not installed.'
    }
    return
}

if (-not (Test-Path -LiteralPath $configuration)) {
    throw "Missing $configuration. Configure the Luna Agent before enabling auto-start."
}
if (-not (Test-Path -LiteralPath $venvPython)) {
    throw "Missing $venvPython. Run .\pc-agent\setup-agent.ps1 once first."
}
$homeLine = Get-Content -LiteralPath $venvConfig | Where-Object { $_ -match '^\s*home\s*=' } |
    Select-Object -First 1
if (-not $homeLine) {
    throw "Could not find the base Python location in $venvConfig."
}
$pythonHome = $homeLine -replace '^\s*home\s*=\s*', ''
$pythonw = Join-Path $pythonHome 'pythonw.exe'
if (-not (Test-Path -LiteralPath $pythonw)) {
    throw "Missing $pythonw. Recreate the Luna Agent virtual environment."
}
$identity = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
$action = New-ScheduledTaskAction -Execute $pythonw -Argument ('"{0}"' -f $backgroundScript) `
    -WorkingDirectory $agentRoot
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $identity
$principal = New-ScheduledTaskPrincipal -UserId $identity -LogonType Interactive -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -StartWhenAvailable -MultipleInstances IgnoreNew -ExecutionTimeLimit (New-TimeSpan -Seconds 0) `
    -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1)

Register-ScheduledTask -TaskName $taskName -TaskPath '\' -Action $action -Trigger $trigger `
    -Principal $principal -Settings $settings -Description $description -Force | Out-Null
Write-Host "Luna auto-start installed for $identity at Windows sign-in."
if ($StartNow) {
    Start-ScheduledTask -TaskName $taskName -TaskPath '\'
    Write-Host 'Luna auto-start task launched.'
}
