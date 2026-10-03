"""Windows-owned Luna BLE worker lifetime. Never opens COM or creates a pairing."""
from __future__ import annotations

import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path
import subprocess
import sys
import time

from agent_instance import single_agent_instance

LOG = logging.getLogger('luna.ble.resident')

def supervise_worker(command, *, launch=subprocess.Popen, sleep=time.sleep, clock=time.monotonic):
    """Only supervise this child; no retry of music requests or old sessions."""
    backoff = 2
    while True:
        started = clock()
        child = launch(command)
        LOG.info('WORKER_STARTED pid=%d', child.pid)
        code = child.wait()
        elapsed = max(0, clock()-started)
        if code == 0:
            LOG.info('WORKER_STOPPED clean exit; resident stopping')
            return 0
        if elapsed >= 60:
            backoff = 2
        LOG.warning('WORKER_EXIT code=%d uptime=%.0fs; new process in %ds; no request replay', code, elapsed, backoff)
        sleep(backoff)
        backoff = min(backoff*2, 30)

def main():
    directory = Path(__file__).resolve().parent
    handler = RotatingFileHandler(directory/'ble-resident.log', maxBytes=262144, backupCount=2, encoding='utf-8')
    handler.setFormatter(logging.Formatter('%(asctime)s %(levelname)s %(message)s'))
    LOG.addHandler(handler); LOG.setLevel(logging.INFO)
    with single_agent_instance('Local\\LunaBleResident') as acquired:
        if not acquired:
            LOG.info('Resident already running'); return 0
        LOG.info('RESIDENT_STARTED Windows task owner; paired BLE only; no serial/HTTP')
        try:
            return supervise_worker([sys.executable, str(directory/'luna_ble_link.py'), '--background', '--music'])
        except KeyboardInterrupt:
            LOG.info('Resident stopped by user'); return 0
        except Exception as error:
            LOG.error('Resident failure: %s', type(error).__name__); return 1

if __name__=='__main__':
    raise SystemExit(main())
