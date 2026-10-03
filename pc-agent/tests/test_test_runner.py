"""The release test entry must not report skipped verification as success."""
import contextlib
import importlib.util
import io
from pathlib import Path
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location(
    "luna_test_runner", Path(__file__).resolve().parents[2] / "scripts/run-pc-agent-tests.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class TestRunnerTests(unittest.TestCase):
    def run_lane(self, suite, hosted=False):
        argv = ["run-pc-agent-tests.py"] + (["--hosted"] if hosted else [])
        output = io.StringIO()
        with patch.object(runner.sys, "argv", argv), \
                patch.object(runner.unittest.defaultTestLoader, "discover", return_value=suite), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            status = runner.main()
        return status, output.getvalue()

    def test_skipped_verification_fails_both_lanes(self):
        def missing_prerequisite():
            raise unittest.SkipTest("native compiler unavailable")

        for hosted in (False, True):
            with self.subTest(hosted=hosted):
                suite = unittest.TestSuite([unittest.FunctionTestCase(missing_prerequisite)])
                status, output = self.run_lane(suite, hosted)
                self.assertEqual(status, 1)
                self.assertIn("unexpectedly skipped tests", output)
                self.assertIn("native compiler unavailable", output)

    def test_complete_verification_passes_both_lanes(self):
        for hosted in (False, True):
            with self.subTest(hosted=hosted):
                suite = unittest.TestSuite([unittest.FunctionTestCase(lambda: None)])
                status, _ = self.run_lane(suite, hosted)
                self.assertEqual(status, 0)

    def test_hosted_native_exclusion_is_not_an_unexpected_skip(self):
        class NativeCase(unittest.TestCase):
            def runTest(self):
                self.fail("hosted lane must exclude native verification")

        NativeCase.__module__ = "test_luna_dashboard_c"
        suite = unittest.TestSuite([NativeCase(), unittest.FunctionTestCase(lambda: None)])
        status, output = self.run_lane(suite, hosted=True)
        self.assertEqual(status, 0)
        self.assertIn("hosted lane: 1 tests", output)
