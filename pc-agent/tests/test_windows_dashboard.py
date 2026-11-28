import ctypes as C
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from windows_dashboard import CounterItem, CpuCounter, GpuCounters, WindowsDashboard


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

        def memory(status):
            status._obj.total, status._obj.available = 16 * 1024**3, 4 * 1024**3
            return 1

        collector.kernel.GlobalMemoryStatusEx.side_effect = memory
        collector.cpu_counter = Mock()
        collector.cpu_counter.collect.return_value = 90
        collector._cpu_retry_at = 0
        collector.read_project = Mock(return_value='Luna')
        collector.gpu = {'name': 'Test GPU', 'luid': 'luid_x', 'total': 8 * 1024**3}
        collector.counters = Mock()
        collector._gpu_retry_at = 0
        return collector

    def test_cpu_uses_total_utility_without_scaling_busy_time(self):
        dll = self.pdh()
        def formatted(_counter, flags, _type, value):
            self.assertEqual(flags, 0x8200)
            value._obj.status, value._obj.number = 0, 51.7
            return 0
        dll.PdhGetFormattedCounterValue.side_effect = formatted
        with patch('windows_dashboard.C.WinDLL', return_value=dll, create=True):
            counter = CpuCounter()
        collector = self.collector(); collector.cpu_counter = counter
        collector.counters.collect.return_value = {}
        self.assertEqual(collector.sample()['cpu'], 51.7)
        collector.kernel.GetSystemTimes.assert_not_called()
        path = dll.PdhAddEnglishCounterW.call_args.args[1]
        self.assertEqual(path, r'\Processor Information(_Total)\% Processor Utility')
        counter.close(); counter.close()
        dll.PdhCloseQuery.assert_called_once()

    def test_cpu_warmup_is_unknown_then_valid_and_turbo_is_capped(self):
        dll = self.pdh()
        values = iter([(0xC0000BBA, 0), (1, 0), (0, 150.2), (0, float('nan')), (0, -1)])
        def formatted(_counter, _flags, _type, value):
            value._obj.status, value._obj.number = next(values)
            return 0
        dll.PdhGetFormattedCounterValue.side_effect = formatted
        with patch('windows_dashboard.C.WinDLL', return_value=dll, create=True):
            counter = CpuCounter()
        self.assertIsNone(counter.collect())
        self.assertEqual(counter.collect(), 0)
        self.assertEqual(counter.collect(), 100)
        self.assertIsNone(counter.collect())
        self.assertIsNone(counter.collect())
        counter.close()

    def test_cpu_initialization_failure_closes_partial_query(self):
        for stage in ('counter', 'warmup'):
            dll = self.pdh()
            if stage == 'counter':
                dll.PdhAddEnglishCounterW.side_effect = None
                dll.PdhAddEnglishCounterW.return_value = 1
            else:
                dll.PdhCollectQueryData.return_value = 1
            with patch('windows_dashboard.C.WinDLL', return_value=dll, create=True):
                with self.assertRaises(OSError): CpuCounter()
            dll.PdhCloseQuery.assert_called_once()

    def test_cpu_failure_preserves_other_metrics_and_recovers_after_cooldown(self):
        collector = self.collector()
        failed = collector.cpu_counter
        failed.collect.side_effect = OSError('unavailable')
        collector.counters.collect.return_value = {}
        recovered = Mock(); recovered.collect.return_value = 49.9
        with patch('windows_dashboard.CpuCounter', return_value=recovered) as factory:
            with patch('windows_dashboard.time.monotonic', return_value=100):
                with self.assertLogs('luna.ble.link', level='WARNING'):
                    sample = collector.sample()
                self.assertIsNone(sample['cpu'])
                self.assertEqual((sample['ram_used_gb'], sample['project']), (12, 'Luna'))
                self.assertIsNone(collector.sample()['cpu'])
                factory.assert_not_called()
            with patch('windows_dashboard.time.monotonic', return_value=130):
                self.assertEqual(collector.sample()['cpu'], 49.9)
            factory.assert_called_once()
        failed.close.assert_called_once()

    def test_cpu_creation_failure_is_independent_of_gpu(self):
        collector = self.collector(); collector.cpu_counter = None
        collector.counters.collect.return_value = {}
        with patch('windows_dashboard.CpuCounter', side_effect=OSError()) as factory:
            with patch('windows_dashboard.time.monotonic', return_value=100):
                self.assertIsNone(collector.sample()['cpu'])
                self.assertEqual(collector.sample()['ram_used_gb'], 12)
            factory.assert_called_once()

    def test_cpu_cleanup_failure_still_closes_gpu(self):
        collector = self.collector(); cpu, gpu = collector.cpu_counter, collector.counters
        cpu.close.side_effect = OSError()
        with self.assertRaises(OSError): collector.close()
        gpu.close.assert_called_once()
        self.assertIsNone(collector.cpu_counter)
        self.assertIsNone(collector.counters)

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

    def test_all_formatted_arrays_failing_triggers_gpu_recovery(self):
        for stage in ('size', 'read'):
            with self.subTest(stage=stage):
                dll = self.pdh()

                def unavailable(_counter, _format, size, count, buffer):
                    if stage == 'size' or buffer is not None:
                        return 1
                    size._obj.value = C.sizeof(CounterItem)
                    return 0x800007D2  # PDH_MORE_DATA: normal size discovery.

                dll.PdhGetFormattedCounterArrayW.side_effect = unavailable
                with patch('windows_dashboard.C.WinDLL', return_value=dll, create=True):
                    counters = GpuCounters()
                collector = self.collector()
                collector.counters = counters
                with patch('windows_dashboard.time.monotonic', return_value=100):
                    with self.assertLogs('luna.ble.link', level='WARNING'):
                        sample = collector.sample()
                self.assertEqual((sample['cpu'], sample['ram_used_gb'], sample['project']),
                                 (90, 12, 'Luna'))
                self.assertIsNone(sample['gpu'])
                self.assertIsNone(collector.counters)
                self.assertIsNone(collector.gpu)
                self.assertEqual(collector._gpu_retry_at, 130)
                dll.PdhCloseQuery.assert_called_once()

    def test_one_failed_array_preserves_the_other_gpu_metric(self):
        dll = self.pdh()

        def partial(counter, _format, size, count, buffer):
            if counter.value == 1:
                return 1
            size._obj.value = C.sizeof(CounterItem)
            count._obj.value = 1
            if buffer is None:
                return 0x800007D2
            item = C.cast(buffer, C.POINTER(CounterItem))[0]
            item.name = 'luid_x'
            item.value.status = 0
            item.value.number = 2 * 1024**3
            return 0

        dll.PdhGetFormattedCounterArrayW.side_effect = partial
        with patch('windows_dashboard.C.WinDLL', return_value=dll, create=True):
            counters = GpuCounters()
        counters.counters = {'engines': C.c_void_p(1), 'memory': C.c_void_p(2)}
        collector = self.collector()
        collector.counters = counters
        sample = collector.sample()
        self.assertIsNone(sample['gpu'])
        self.assertEqual(sample['vram_used_gb'], 2)
        self.assertIs(collector.counters, counters)
        dll.PdhCloseQuery.assert_not_called()
        collector.close()

    def test_successful_empty_arrays_do_not_trigger_query_rebuild(self):
        dll = self.pdh()
        dll.PdhGetFormattedCounterArrayW.return_value = 0
        with patch('windows_dashboard.C.WinDLL', return_value=dll, create=True):
            counters = GpuCounters()
        self.assertEqual(counters.collect(), {'engines': [], 'memory': []})
        dll.PdhCloseQuery.assert_not_called()
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
