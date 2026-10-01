"""Task Scheduler entry point with UTF-8 logging and the Agent virtualenv."""

from __future__ import annotations

import site
import sys
import traceback
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parent
VENV_SITE_PACKAGES = ROOT / ".venv" / "Lib" / "site-packages"
LOG_PATH = ROOT / "agent.log"


def main() -> int:
    with LOG_PATH.open("a", encoding="utf-8", buffering=1) as log:
        original_stdout, original_stderr = sys.stdout, sys.stderr
        sys.stdout = log
        sys.stderr = log
        try:
            print(f"\n[{datetime.now(timezone.utc).astimezone().isoformat()}] Starting Luna Agent")
            if not VENV_SITE_PACKAGES.is_dir():
                print("Luna Agent virtual environment is missing; run setup-agent.ps1.")
                return 1
            site.addsitedir(str(VENV_SITE_PACKAGES))
            sys.path.remove(str(VENV_SITE_PACKAGES))
            sys.path.insert(0, str(VENV_SITE_PACKAGES))
            from luna_agent import main as run_agent

            return run_agent()
        except BaseException:
            traceback.print_exc()
            return 1
        finally:
            sys.stdout, sys.stderr = original_stdout, original_stderr


if __name__ == "__main__":
    raise SystemExit(main())
