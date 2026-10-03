import json
from pathlib import Path
import sys
import time
import unittest
from unittest.mock import Mock, patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from luna_dashboard import DashboardService, quota_windows
from windows_dashboard import engine_usage, finite, vscode_project


class StopAfterWaits:
    def __init__(self, limit):
        self.limit = limit
        self.waits = []

    def is_set(self):
        return len(self.waits) >= self.limit

    def wait(self, seconds):
        self.waits.append(seconds)
        return self.is_set()

class DashboardTests(unittest.TestCase):
    def test_duration_not_position_selects_windows(self):
        q=quota_windows({"codex":{"windows":[{"usedPercent":40,"windowDurationMins":10080},{"usedPercent":56,"windowDurationMins":300}]}})
        self.assertEqual((q['primary_remaining'],q['weekly_remaining']),(44,60))
        self.assertIsNone(quota_windows({"codex":{"windows":[{"usedPercent":1,"windowDurationMins":15}]}})['primary_remaining'])

    def test_unknown_and_invalid_are_not_zero(self):
        for used in (None,True,float('nan'),float('inf'),101,-1,'40'):
            q=quota_windows({"codex":{"windows":[{"usedPercent":used,"windowDurationMins":300}]}})
            self.assertIsNone(q['primary_remaining'])
        self.assertEqual(quota_windows({"codex":{"windows":[{"usedPercent":100,"windowDurationMins":300}]}})['primary_remaining'],0)

    def test_busiest_engine_sums_processes_not_different_engines_or_adapters(self):
        rows=[('pid_1_luid_x_phys_0_eng_0_engtype_3D',20),('pid_2_luid_x_phys_0_eng_0_engtype_3D',30),
              ('pid_1_luid_x_phys_0_eng_1_engtype_Copy',40),('pid_1_luid_y_phys_0_eng_0_engtype_3D',99)]
        self.assertEqual(engine_usage(rows,'luid_x'),50)
        self.assertIsNone(engine_usage(rows,'luid_z'))

    def test_project_comes_from_vscode_workspace_not_filename_or_other_app(self):
        self.assertEqual(vscode_project('main.c - Luna - Visual Studio Code'),'Luna')
        self.assertEqual(vscode_project('main.c - Visual Studio Code'),'')
        self.assertEqual(vscode_project('main.c - Luna - Other Editor'),'')

    def test_ble_snapshot_is_cached_bounded_and_sensor_nulls_survive(self):
        codex=Mock();codex.snapshot.return_value={}
        service=DashboardService(collector=Mock(),codex=codex)
        service._cached={'cpu':3,'gpu':8,'name':'显卡'*200,'project':'工程'*200,'ram_used_gb':11,'ram_total_gb':16}
        service._sampled=time.monotonic()
        snapshot=service.snapshot()
        self.assertEqual(snapshot['computer']['cpu'],3)
        self.assertIsNone(snapshot['computer']['cpu_temp'])
        self.assertLessEqual(len(snapshot['project']['name'].encode()),191)
        self.assertLess(len(json.dumps(snapshot,ensure_ascii=False).encode()),1500)
        service.collector.sample.assert_not_called()
        service.codex._request.assert_not_called()
        self.assertIsNone(finite(True));self.assertIsNone(finite(float('nan')))

    def test_startup_failure_recovers_with_bounded_interruptible_backoff(self):
        collector = Mock()
        collector.sample.return_value = {'cpu': 7}
        service = DashboardService(codex=Mock())
        service.codex.snapshot.return_value = {}
        service._stop = StopAfterWaits(8)
        with patch('luna_dashboard.WindowsDashboard', side_effect=[OSError()] * 7 + [collector]) as factory:
            with self.assertLogs('luna.ble.link', level='WARNING'):
                service._loop()
        self.assertEqual(service._stop.waits, [2, 4, 8, 16, 30, 30, 30, 2])
        self.assertEqual(factory.call_count, 8)
        self.assertEqual(service.snapshot()['computer']['cpu'], 7)
        collector.close.assert_called_once()

    def test_stopping_during_retry_does_not_create_another_collector(self):
        service = DashboardService(codex=Mock())
        service.codex.snapshot.return_value = {}
        service._stop = StopAfterWaits(1)
        with patch('luna_dashboard.WindowsDashboard', side_effect=OSError()) as factory:
            with self.assertLogs('luna.ble.link', level='WARNING'):
                service._loop()
        factory.assert_called_once()
        self.assertIsNone(service.snapshot()['computer']['cpu'])

    def test_transient_sample_failure_recovers_without_duplicate_collectors(self):
        collector = Mock()
        collector.sample.side_effect = [OSError(), {'cpu': 3}]
        service = DashboardService(collector=collector, codex=Mock())
        service.codex.snapshot.return_value = {}
        service._stop = StopAfterWaits(2)
        with self.assertLogs('luna.ble.link', level='WARNING'):
            service._loop()
        self.assertEqual(service.snapshot()['computer']['cpu'], 3)
        self.assertEqual(service._stop.waits, [2, 2])
        collector.close.assert_called_once()

    def test_repeated_sample_failure_recreates_collector_and_recovers(self):
        broken = Mock()
        broken.sample.side_effect = OSError('persistent provider failure')
        recovered = Mock()
        recovered.sample.return_value = {'cpu': 7}
        service = DashboardService(collector=broken, codex=Mock())
        service.codex.snapshot.return_value = {}
        service._stop = StopAfterWaits(4)
        with patch('luna_dashboard.WindowsDashboard', return_value=recovered) as factory:
            with self.assertLogs('luna.ble.link', level='WARNING'):
                service._loop()
        self.assertEqual(service._stop.waits, [2, 2, 2, 2])
        self.assertEqual(broken.sample.call_count, 3)
        broken.close.assert_called_once()
        factory.assert_called_once()
        self.assertEqual(service.snapshot()['computer']['cpu'], 7)
        recovered.close.assert_called_once()

    def test_start_is_idempotent_and_close_failure_does_not_escape(self):
        collector, codex = Mock(), Mock()
        service = DashboardService(collector=collector, codex=codex)
        with patch('luna_dashboard.threading.Thread') as thread:
            service.start()
            service.start()
            thread.assert_called_once()
            thread.return_value.start.assert_called_once()
            codex.start.assert_called_once()
        service._stop = StopAfterWaits(0)
        collector.close.side_effect = OSError()
        with self.assertLogs('luna.ble.link', level='WARNING'):
            service._loop()
