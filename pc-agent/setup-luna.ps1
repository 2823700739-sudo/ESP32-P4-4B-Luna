[CmdletBinding()]
param([switch]$PrepareOnly, [string]$PythonPath, [switch]$WeatherOnly)
$ErrorActionPreference = 'Stop'

function Invoke-LunaPython {
    param([string[]]$PythonArguments)
    & $script:lunaPython @PythonArguments
    if ($LASTEXITCODE -ne 0) { throw "Python step failed (exit $LASTEXITCODE). Check the message above." }
}

try {
    if ([Environment]::OSVersion.Version.Build -lt 22000 -or -not [Environment]::Is64BitOperatingSystem) {
        throw 'Windows 11 x64 is required.'
    }
    $lunaManager = Join-Path $PSScriptRoot 'manage-ble-resident.ps1'
    $lunaEntry = Join-Path $PSScriptRoot 'luna_ble_resident.py'
    $lunaVenv = Join-Path $PSScriptRoot '.venv-ble'
    $script:lunaPython = Join-Path $lunaVenv 'Scripts\python.exe'
    $lunaPythonw = Join-Path $lunaVenv 'Scripts\pythonw.exe'

    # Refuse another checkout before changing dependencies or its resident task.
    # PrepareOnly never reads or changes tasks, scans BLE, or opens a device.
    $lunaTask = $null
    if (-not $PrepareOnly) {
        $lunaTask = Get-ScheduledTask -TaskName 'Luna BLE Agent' -ErrorAction SilentlyContinue
        if ($lunaTask) {
            $lunaActions = @($lunaTask.Actions)
            if ($lunaTask.Description -ne 'Luna secured BLE music companion, current interactive user; no COM or HTTP.' -or
                $lunaActions.Count -ne 1 -or $lunaActions[0].Execute -ne $lunaPythonw -or
                $lunaActions[0].Arguments -ne ('"' + $lunaEntry + '"')) {
                throw 'Luna is installed from a different folder. Run Start-Luna.cmd in that original folder. No existing task was changed.'
            }
        }
    }

    if (-not (Test-Path -LiteralPath $lunaPython)) {
        if (Test-Path -LiteralPath $lunaVenv) {
            throw 'The existing .venv-ble folder is incomplete. Keep it for diagnosis; no files were removed.'
        }
        $lunaBootstrapArguments = @()
        if ($PythonPath) {
            if (-not (Test-Path -LiteralPath $PythonPath -PathType Leaf)) { throw 'The specified PythonPath does not exist.' }
            $lunaBootstrap = (Resolve-Path -LiteralPath $PythonPath).Path
        } else {
            $lunaLauncher = Get-Command py.exe -ErrorAction SilentlyContinue
            if (-not $lunaLauncher) { throw 'Install Python 3.11 x64 from https://www.python.org/downloads/windows/ (include Python Launcher), then run Start-Luna.cmd again.' }
            $lunaBootstrap = $lunaLauncher.Source
            $lunaBootstrapArguments = @('-3.11')
        }
        & $lunaBootstrap @lunaBootstrapArguments -c "import platform,struct,sys; sys.exit(0 if sys.version_info[:2]==(3,11) and struct.calcsize('P')==8 and platform.machine().upper() in ('AMD64','X86_64') else 1)"
        if ($LASTEXITCODE -ne 0) { throw 'Python 3.11 x64 is required. Install it with Python Launcher, then retry.' }
        Write-Host 'Preparing the Luna Python environment (first run only)...'
        & $lunaBootstrap @lunaBootstrapArguments -m venv $lunaVenv
        if ($LASTEXITCODE -ne 0) { throw 'Could not create the Luna Python environment.' }
    }
    Invoke-LunaPython -PythonArguments @('-c', "import platform,struct,sys; sys.exit(0 if sys.version_info[:2] in ((3,10),(3,11)) and struct.calcsize('P')==8 and platform.machine().upper() in ('AMD64','X86_64') else 1)")
    & $lunaPython (Join-Path $PSScriptRoot 'check-deployment.py')
    if ($LASTEXITCODE -ne 0) {
        if ($lunaTask -and $lunaTask.State -eq 'Running') {
            throw 'The current resident is running but prerequisites need repair. No running environment was changed; stop it explicitly before repairing.'
        }
        Write-Host 'Installing pinned dependencies. Internet access is required on the first run...'
        Invoke-LunaPython -PythonArguments @('-m', 'pip', 'install', '--disable-pip-version-check', '-r', (Join-Path $PSScriptRoot 'requirements-ble-music.txt'))
    }
    Invoke-LunaPython -PythonArguments @('-m', 'pip', 'check')
    Invoke-LunaPython -PythonArguments @((Join-Path $PSScriptRoot 'check-deployment.py'))
    if ($PrepareOnly) {
        Write-Host 'Preparation complete. No scheduled task, Bluetooth connection, pairing, serial port or media action was performed.'
        exit 0
    }

    $lunaConfigure = $WeatherOnly
    if ($lunaConfigure) {
        $lunaResume = $lunaTask -and $lunaTask.State -eq 'Running'
        if ($lunaResume) { & $lunaManager -Action Stop }
        try {
            Invoke-LunaPython -PythonArguments @((Join-Path $PSScriptRoot 'luna_weather_setup.py'))
        } finally {
            # Restore only a resident that the user explicitly invoked this wizard to pause.
            if ($lunaResume) { & $lunaManager -Action Start }
        }
    }
    if ($WeatherOnly) { exit 0 }
    if (-not $lunaTask) { & $lunaManager -Action Install }
    & $lunaManager -Action Start
    & $lunaManager -Action Status | Format-List
    Write-Host ''
    Write-Host 'Luna is started and will run when this Windows user signs in.'
    Write-Host 'Keep this folder in place. Do not run a second copy from another folder.'
    Write-Host 'Pair Luna in Windows Bluetooth settings first; confirm the same number on both screens.'
    Write-Host 'Allow a short time to connect, then check the Luna clock and CPU/RAM card.'
    Write-Host 'Set device Wi-Fi/city from your phone: tap Wi-Fi on the Luna weather card.'
    Write-Host 'Configure-Weather.cmd remains an optional BLE fallback for supported firmware.'
    Write-Host 'Codex quota needs an existing signed-in Codex installation.'
    exit 0
} catch {
    Write-Host ''
    Write-Host ('Luna setup stopped: ' + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
