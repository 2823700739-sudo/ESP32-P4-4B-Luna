import ctypes as C
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from windows_dashboard import CounterItem, GpuCounters, WindowsDashboard


class WindowsDashboardTests(unittest.TestCase):
    def pdh(self):
        dll = Mock()

        def open_query(_name, _userdata, query):
            query._obj.value = 123
            return 0

        def add_counter(_query, _name, _userdata, counter):
            counter._obj.value = 456
            return 0

        dll.PdhOpenQueryW.side_effect = open_query
        dll.PdhAddEnglishCounterW.side_effect = add_counter
        dll.PdhCollectQueryData.return_value = 0
        return dll

    def collector(self):
        collector = WindowsDashboard.__new__(WindowsDashboard)
        collector.kernel = Mock()

        def times(idle, kernel, user):
            idle._obj.value, kernel._obj.value, user._obj.value = 20, 150, 50
            return 1

        def memory(status):
            status._obj.total, status._obj.available = 16 * 1024**3, 4 * 1024**3
            return 1

        collector.kernel.GetSystemTimes.side_effect = times
        collector.kernel.GlobalMemoryStatusEx.side_effect = memory
        collector.previous = (10, 100)
        collector.read_project = Mock(return_value='Luna')
        collector.gpu = {'name': 'Test GPU', 'luid': 'luid_x', 'total': 8 * 1024**3}
        collector.counters = Mock()
        collector._gpu_retry_at = 0
        return collector

    def test_failed_pdh_initialization_closes_open_query(self):
        for failure in ('missing', 'exception'):
            with self.subTest(failure=failure):
                dll = self.pdh()
                if failure == 'missing':
                    dll.PdhAddEnglishCounterW.side_effect = None
                    dll.PdhAddEnglishCounterW.return_value = 1
                else:
                    dll.PdhAddEnglishCounterW.side_effect = OSError('provider failure')
                with patch('windows_dashboard.C.WinDLL', return_value=dll, create=True):
                    with self.assertRaises(OSError):
                        GpuCounters()
                dll.PdhCloseQuery.assert_called_once()

    def test_failed_collection_signals_recovery_and_close_is_idempotent(self):
        dll = self.pdh()
        with patch('windows_dashboard.C.WinDLL', return_value=dll, create=True):
            counters = GpuCounters()
        dll.PdhCollectQueryData.return_value = 1
        with self.assertRaises(OSError):
            counters.collect()
        counters.close()
        counters.close()
        dll.PdhCloseQuery.assert_called_once()

    def test_malformed_counter_count_is_not_dereferenced(self):
        dll = self.pdh()

        def malformed(_counter, _format, size, count, buffer):
            size._obj.value = C.sizeof(CounterItem)
            count._obj.value = 1000000
            return 0

        dll.PdhGetFormattedCounterArrayW.side_effect = malformed
        with patch('windows_dashboard.C.WinDLL', return_value=dll, create=True):
            counters = GpuCounters()
        self.assertEqual(counters.collect(), {})
        counters.close()

    def test_null_gpu_instance_name_does_not_discard_cpu_ram_or_project(self):
        collector = self.collector()
        collector.counters.collect.return_value = {
            'engines': [(None, 42), ('pid_1_luid_x_phys_0_eng_0_engtype_3D', 9)],
            'memory': [(None, 1024), ('luid_x', 2 * 1024**3)],
        }
        sample = collector.sample()
        self.assertEqual((sample['cpu'], sample['ram_used_gb'], sample['project']), (90, 12, 'Luna'))
        self.assertEqual((sample['gpu'], sample['vram_used_gb']), (9, 2))

    def test_oversized_gpu_memory_does_not_invalidate_other_metrics(self):
        collector = self.collector()
        collector.counters.collect.return_value = {
            'engines': [('pid_1_luid_x_phys_0_eng_0_engtype_3D', 9)],
            'memory': [('luid_x', 6 * 1024**3), ('luid_x', 6 * 1024**3)],
        }
        sample = collector.sample()
        self.assertEqual((sample['cpu'], sample['gpu'], sample['ram_used_gb'], sample['project']),
                         (90, 9, 12, 'Luna'))
        self.assertIsNone(sample['vram_used_gb'])
        self.assertEqual(sample['vram_total_gb'], 8)

    def test_pdh_null_instance_name_is_filtered(self):
        dll = self.pdh()

        def unnamed(_counter, _format, size, count, buffer):
            size._obj.value = C.sizeof(CounterItem)
            count._obj.value = 1
            if buffer is not None:
                item = C.cast(buffer, C.POINTER(CounterItem))[0]
                item.name = None
                item.value.status = 0
                item.value.number = 42
            return 0

        dll.PdhGetFormattedCounterArrayW.side_effect = unnamed
        with patch('windows_dashboard.C.WinDLL', return_value=dll, create=True):
            counters = GpuCounters()
        self.assertEqual(counters.collect(), {'engines': [], 'memory': []})
        counters.close()

    def test_gpu_failure_preserves_cpu_ram_project_and_recovers_after_cooldown(self):
        collector = self.collector()
        failed = collector.counters
        failed.collect.side_effect = OSError('driver reset')
        with patch('windows_dashboard.time.monotonic', return_value=100):
            with self.assertLogs('luna.ble.link', level='WARNING'):
                sample = collector.sample()
        self.assertEqual((sample['cpu'], sample['ram_used_gb'], sample['project']), (90, 12, 'Luna'))
        self.assertIsNone(sample['gpu'])
        failed.close.assert_called_once()
        self.assertIsNone(collector.counters)
        self.assertIsNone(collector.gpu)

        recovered = Mock()
        recovered.collect.return_value = {'engines': [('pid_1_luid_y_phys_0_eng_0_engtype_3D', 9)],
                                         'memory': [('luid_y', 2 * 1024**3)]}
        adapter = {'name': 'Recovered GPU', 'luid': 'luid_y', 'total': 8 * 1024**3}
        with patch('windows_dashboard.GpuCounters', return_value=recovered) as factory:
            with patch('windows_dashboard.main_gpu', return_value=adapter) as dxgi:
                with patch('windows_dashboard.time.monotonic', return_value=129):
                    self.assertIsNone(collector.sample()['gpu'])
                factory.assert_not_called()
                dxgi.assert_not_called()
                with patch('windows_dashboard.time.monotonic', return_value=130):
                    sample = collector.sample()
                factory.assert_called_once()
                dxgi.assert_called_once()
        self.assertEqual((sample['gpu'], sample['vram_used_gb'], sample['name']), (9, 2, 'Recovered GPU'))
        collector.close()
        collector.close()
        recovered.close.assert_called_once()

    def test_gpu_providers_initialize_independently_and_retry_is_bounded(self):
        collector = self.collector()
        collector.gpu = collector.counters = None
        with patch('windows_dashboard.main_gpu', side_effect=OSError()) as dxgi:
            with patch('windows_dashboard.GpuCounters', side_effect=OSError()) as factory:
                with patch('windows_dashboard.time.monotonic', return_value=100):
                    sample = collector.sample()
                    collector.sample()
                factory.assert_called_once()
                dxgi.assert_called_once()
                with patch('windows_dashboard.time.monotonic', return_value=130):
                    collector.sample()
                self.assertEqual(factory.call_count, 2)
                self.assertEqual(dxgi.call_count, 2)
        self.assertEqual((sample['cpu'], sample['ram_used_gb'], sample['project']), (90, 12, 'Luna'))
        self.assertIsNone(sample['gpu'])


if __name__ == '__main__':
    unittest.main()
