import logging
from pathlib import Path
import sys
import shutil
import subprocess
from types import SimpleNamespace
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from luna_ble_resident import supervise_worker

class ResidentTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('pwsh'),'PowerShell parser required')
    def test_status_distinguishes_link_health_from_task_state(self):
        directory=Path(__file__).resolve().parents[1]
        script=r'''
$lunaTestRoot='__ROOT__'
$lunaTokens=$null;$lunaErrors=$null
$lunaAst=[System.Management.Automation.Language.Parser]::ParseFile((Join-Path $lunaTestRoot 'manage-ble-resident.ps1'),[ref]$lunaTokens,[ref]$lunaErrors)
if($lunaErrors){throw 'Parse failed'}
$lunaFunction=$lunaAst.Find({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Get-LunaLinkState'},$true)
Invoke-Expression $lunaFunction.Extent.Text
$lunaLog=Join-Path ([System.IO.Path]::GetTempPath()) ([guid]::NewGuid().ToString() + '.log')
try {
$lunaNow=[datetime]::ParseExact('2026-10-03 10:00:00,000','yyyy-MM-dd HH:mm:ss,fff',[cultureinfo]::InvariantCulture)
if((Get-LunaLinkState -TaskState 'Running' -LogPath $lunaLog -Now $lunaNow) -ne 'Unknown'){throw 'Missing log'}
@('2026-10-03 09:59:00,000 INFO HEALTHY: connected', '2026-10-03 09:59:50,000 WARNING OFFLINE: discovery') | Set-Content -LiteralPath $lunaLog
if((Get-LunaLinkState -TaskState 'Running' -LogPath $lunaLog -Now $lunaNow) -ne 'Offline'){throw 'Latest offline'}
Add-Content -LiteralPath $lunaLog '2026-10-03 09:59:55,000 INFO Luna BLE resident starting; no serial ports opened'
if((Get-LunaLinkState -TaskState 'Running' -LogPath $lunaLog -Now $lunaNow) -ne 'Starting'){throw 'New process'}
Add-Content -LiteralPath $lunaLog '2026-10-03 09:59:56,000 INFO CONNECTED: authenticated hello'
if((Get-LunaLinkState -TaskState 'Running' -LogPath $lunaLog -Now $lunaNow) -ne 'Connected'){throw 'Recent connection'}
if((Get-LunaLinkState -TaskState 'Running' -LogPath $lunaLog -Now $lunaNow.AddMinutes(2)) -ne 'Stale'){throw 'Stale log'}
if((Get-LunaLinkState -TaskState 'Ready' -LogPath $lunaLog -Now $lunaNow) -ne 'Stopped'){throw 'Stopped task'}
} finally { Remove-Item -LiteralPath $lunaLog -ErrorAction SilentlyContinue }
'''.replace('__ROOT__',str(directory).replace("'","''"))
        result=subprocess.run(['pwsh','-NoProfile','-Command',script],capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stderr+result.stdout)

    @unittest.skipUnless(shutil.which('pwsh'),'PowerShell parser required')
    def test_stop_targets_only_project_background_launcher_and_real_python(self):
        directory=Path(__file__).resolve().parents[1]
        script=r'''
$lunaTestRoot='__ROOT__'
$entryPath=Join-Path $lunaTestRoot 'luna_ble_resident.py'
$lunaWorker=Join-Path $lunaTestRoot 'luna_ble_link.py'
$lunaTokens=$null;$lunaErrors=$null
$lunaAst=[System.Management.Automation.Language.Parser]::ParseFile((Join-Path $lunaTestRoot 'manage-ble-resident.ps1'),[ref]$lunaTokens,[ref]$lunaErrors)
if($lunaErrors){throw 'Parse failed'}
$lunaFunction=$lunaAst.Find({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Stop-LunaBackground'},$true)
Invoke-Expression ($lunaFunction.Extent.Text.Replace('$PSScriptRoot','$lunaTestRoot'))
$lunaEntries=@(
  @{ProcessId=1;Name='pythonw.exe';CommandLine=('pythonw.exe "'+$entryPath+'"')},
  @{ProcessId=2;Name='pythonw.exe';CommandLine=('pythonw.exe '+$entryPath)},
  @{ProcessId=3;Name='pythonw.exe';CommandLine=('pythonw.exe '+$lunaWorker+' --background --music')},
  @{ProcessId=4;Name='pythonw.exe';CommandLine=('pythonw.exe "'+$lunaWorker+'" --background --music')},
  @{ProcessId=5;Name='pythonw.exe';CommandLine=('pythonw.exe '+$lunaWorker+' --music')},
  @{ProcessId=6;Name='python.exe';CommandLine=('python.exe '+$lunaWorker+' --background')},
  @{ProcessId=7;Name='pythonw.exe';CommandLine=('pythonw.exe '+$lunaWorker+'.backup --background')},
  @{ProcessId=8;Name='pythonw.exe';CommandLine=('pythonw.exe copied-'+$lunaWorker+' --background')},
  @{ProcessId=9;Name='codex.exe';CommandLine=('codex.exe '+$lunaWorker+' --background')}
)|ForEach-Object{[pscustomobject]$_}
$script:lunaStopped=@()
function Get-CimInstance {param($ClassName) return $lunaEntries}
function Stop-Process {param($Id,$ErrorAction) $script:lunaStopped+= $Id}
Stop-LunaBackground
if(($lunaStopped -join ',') -ne '1,2,3,4'){throw ('Wrong stop targets: '+($lunaStopped -join ','))}
'''.replace('__ROOT__',str(directory).replace("'","''"))
        result=subprocess.run(['pwsh','-NoProfile','-Command',script],capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stderr+result.stdout)

    def test_clean_exit_is_not_restarted(self):
        calls=[]
        def launch(command):
            calls.append(command);return SimpleNamespace(pid=1,wait=lambda:0)
        self.assertEqual(supervise_worker(['worker'],launch=launch,sleep=lambda _:self.fail('retry'),clock=lambda:0),0)
        self.assertEqual(calls,[['worker']])

    def test_crash_restarts_fresh_worker_with_bounded_backoff(self):
        codes=iter([-1]*7+[0]); waits=[]; commands=[]
        def launch(command):
            commands.append(command);code=next(codes);return SimpleNamespace(pid=1,wait=lambda:code)
        with self.assertLogs('luna.ble.resident',level=logging.WARNING):
            self.assertEqual(supervise_worker(['worker','--music'],launch=launch,sleep=waits.append,clock=lambda:0),0)
        self.assertEqual(waits,[2,4,8,16,30,30,30]);self.assertEqual(len(commands),8)
        self.assertTrue(all(c==['worker','--music'] for c in commands))

    def test_stable_worker_resets_backoff(self):
        times=iter([0,1,2,3,4,65,66,67]);codes=iter([-1,-1,-1,0]);waits=[]
        def launch(_command):
            code=next(codes);return SimpleNamespace(pid=1,wait=lambda:code)
        with self.assertLogs('luna.ble.resident',level=logging.WARNING):
            supervise_worker(['worker'],launch=launch,sleep=waits.append,clock=lambda:next(times))
        self.assertEqual(waits,[2,4,2])

if __name__=='__main__':unittest.main()
