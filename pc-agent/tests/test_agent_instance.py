from __future__ import annotations

import os
from pathlib import Path
import sys
import unittest
from uuid import uuid4


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from agent_instance import single_agent_instance  # noqa: E402


class SingleAgentInstanceTests(unittest.TestCase):
    @unittest.skipUnless(os.name == "nt", "Windows named mutex test")
    def test_second_agent_is_rejected_until_first_exits(self) -> None:
        name = f"Local\\LunaDesktopAgentTest_{uuid4().hex}"
        with single_agent_instance(name) as first:
            self.assertTrue(first)
            with single_agent_instance(name) as second:
                self.assertFalse(second)
        with single_agent_instance(name) as third:
            self.assertTrue(third)


if __name__ == "__main__":
    unittest.main()
