"""Run Luna's Windows test lanes without coupling CI to a local virtualenv."""

import argparse
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / "pc-agent" / "tests"
# These compile real C sources; several also need ignored ESP-IDF managed components.
NATIVE_MODULES = {
    "test_ble_advertising_recovery",
    "test_luna_ble_music_c",
    "test_luna_dashboard_c",
    "test_luna_power_c",
    "test_luna_time_policy_c",
    "test_luna_weather_parse_c",
}
NATIVE_CLASSES = {("test_luna_ble_protocol", "CParityTests")}


def cases(suite):
    for item in suite:
        if isinstance(item, unittest.TestSuite):
            yield from cases(item)
        else:
            yield item


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hosted", action="store_true",
                        help="Windows CI lane without GCC or ESP-IDF managed components")
    args = parser.parse_args()
    sys.path.insert(0, str(TESTS))
    suite = unittest.defaultTestLoader.discover(str(TESTS), pattern="test_*.py")
    selected = []
    for case in cases(suite):
        module = case.__class__.__module__
        native = module in NATIVE_MODULES or (module, case.__class__.__name__) in NATIVE_CLASSES
        if not (args.hosted and native):
            selected.append(case)
    if not selected:
        parser.error("no tests selected")
    print(f"Luna {'hosted' if args.hosted else 'full'} lane: {len(selected)} tests", flush=True)
    result = unittest.TextTestRunner(verbosity=1).run(unittest.TestSuite(selected))
    if result.skipped:
        lane = "Hosted" if args.hosted else "Full"
        print(f"{lane} lane unexpectedly skipped tests:", result.skipped, file=sys.stderr)
        return 1
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
