[CmdletBinding()]
param([ValidateSet('Install', 'Start', 'Stop', 'Status', 'Remove')][string]$Action = 'Status')
$ErrorActionPreference = 'Stop'
$taskName = 'Luna BLE Agent'
$pythonPath = Join-Path $PSScriptRoot '.venv-ble\Scripts\pythonw.exe'
$entryPath = Join-Path $PSScriptRoot 'luna_ble_resident.py'
$description = 'Luna secured BLE music companion, current interactive user; no COM or HTTP.'
$existing = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
if ($existing) {
    $taskAction = @($existing.Actions)
    if ($existing.Description -ne $description -or $taskAction.Count -ne 1 -or
        $taskAction[0].Execute -ne $pythonPath -or $taskAction[0].Arguments -ne ('"' + $entryPath + '"')) {
        throw 'Task name belongs to another configuration; no task changed.'
    }
}
function Stop-LunaBackground {
    # Task Scheduler can end the venv launcher but leave its real Python child.
    # Stop only this project's background guardian/worker, never a foreground
    # diagnostic, another Python app, or the Codex desktop application.
    foreach ($lunaEntry in @($entryPath, (Join-Path $PSScriptRoot 'luna_ble_link.py'))) {
        $lunaPattern = '(?:^|\s|"|\x27)' + [regex]::Escape($lunaEntry) + '(?:\s|"|\x27|$)'
        Get-CimInstance Win32_Process | Where-Object {
            $_.Name -eq 'pythonw.exe' -and $_.CommandLine -match $lunaPattern -and
            ($lunaEntry -eq $entryPath -or $_.CommandLine -match '(?:^|\s)--background(?:\s|$)')
        } | ForEach-Object { Stop-Process -Id $_.ProcessId -ErrorAction SilentlyContinue }
    }
}
function Get-LunaLinkState {
    param([string]$TaskState, [string]$LogPath, [datetime]$Now)
    if ($TaskState -ne 'Running') { return 'Stopped' }
    if (-not (Test-Path -LiteralPath $LogPath)) { return 'Unknown' }
    try { $lines = @(Get-Content -LiteralPath $LogPath -Tail 80 -ErrorAction Stop) }
    catch { return 'Unknown' }
    for ($index = $lines.Count - 1; $index -ge 0; $index--) {
        if ($lines[$index] -notmatch '^(?<stamp>\d{4}-\d\d-\d\d \d\d:\d\d:\d\d,\d{3}) (?:INFO|WARNING) (?<event>CONNECTED:|HEALTHY:|OFFLINE:|Luna BLE resident starting)') {
            continue
        }
        try { $seen = [datetime]::ParseExact($Matches.stamp, 'yyyy-MM-dd HH:mm:ss,fff', [cultureinfo]::InvariantCulture) }
        catch { return 'Unknown' }
        if (($Now - $seen).TotalSeconds -gt 90 -or ($seen - $Now).TotalSeconds -gt 5) { return 'Stale' }
        switch ($Matches.event) {
            'OFFLINE:' { return 'Offline' }
            'Luna BLE resident starting' { return 'Starting' }
            default { return 'Connected' }
        }
    }
    return 'Unknown'
}
switch ($Action) {
    'Install' {
        if (-not (Test-Path -LiteralPath $pythonPath) -or -not (Test-Path -LiteralPath $entryPath)) {
            throw 'Install the BLE environment and resident source first.'
        }
        if ($existing -and $existing.State -eq 'Running') { throw 'Stop the current Luna BLE task before updating it.' }
        $lunaUser = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
        $lunaPrincipal = New-ScheduledTaskPrincipal -UserId $lunaUser -LogonType Interactive -RunLevel Limited
        $lunaAction = New-ScheduledTaskAction -Execute $pythonPath -Argument ('"' + $entryPath + '"') -WorkingDirectory $PSScriptRoot
        $lunaTrigger = New-ScheduledTaskTrigger -AtLogOn -User $lunaUser
        $lunaSettings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
            -StartWhenAvailable -MultipleInstances IgnoreNew -ExecutionTimeLimit ([TimeSpan]::Zero) `
            -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1)
        Register-ScheduledTask -TaskName $taskName -Action $lunaAction -Trigger $lunaTrigger `
            -Principal $lunaPrincipal -Settings $lunaSettings -Description $description -Force | Out-Null
        Write-Host 'Installed Luna BLE Agent: interactive user, login startup, crash recovery; no elevation.'
    }
    'Start' {
        if (-not $existing) { throw 'Install Luna BLE Agent first.' }
        if ($existing.State -eq 'Running') { Write-Host 'Luna BLE Agent task is already running.'; break }
        # Migrate only this project's old background link, never a foreground
        # diagnostic or another Python application. Otherwise its mutex would
        # make the Windows-owned worker exit while the tool-owned one remains.
        $lunaWorkerPath = Join-Path $PSScriptRoot 'luna_ble_link.py'
        Get-CimInstance Win32_Process | Where-Object {
            $_.Name -eq 'pythonw.exe' -and $_.CommandLine -match [regex]::Escape($lunaWorkerPath) -and
            $_.CommandLine -match '(?:^|\s)--background(?:\s|$)'
        } | ForEach-Object { Stop-Process -Id $_.ProcessId -ErrorAction SilentlyContinue }
        Start-ScheduledTask -TaskName $taskName
        Write-Host 'Luna BLE Agent started through Windows Task Scheduler.'
    }
    'Stop' {
        if ($existing) { Stop-ScheduledTask -TaskName $taskName }
        Stop-LunaBackground
        Write-Host 'Luna BLE Agent task stopped; login startup remains installed.'
    }
    'Remove' {
        if ($existing) {
            Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
            Stop-LunaBackground
            Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
        }
        Write-Host 'Removed Luna BLE Agent task; source and pairing retained.'
    }
    'Status' {
        if (-not $existing) { Write-Host 'Luna BLE Agent is not installed.'; break }
        $lunaTaskInfo = Get-ScheduledTaskInfo -TaskName $taskName
        [pscustomobject]@{
            TaskName = $existing.TaskName
            State = $existing.State
            LinkState = Get-LunaLinkState -TaskState $existing.State -LogPath (Join-Path $PSScriptRoot 'ble-link.log') -Now (Get-Date)
            LastRunTime = $lunaTaskInfo.LastRunTime
            LastTaskResult = $lunaTaskInfo.LastTaskResult
        }
    }
}
