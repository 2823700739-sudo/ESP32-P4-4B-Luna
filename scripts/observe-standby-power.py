"""Finite reset-free UART observation. Never sends commands or changes power."""
import argparse
import re
import time
import serial

parser=argparse.ArgumentParser()
parser.add_argument('--port',required=True,help='Current UART port detected on this machine')
parser.add_argument('--seconds',type=int,default=235)
parser.add_argument('--until-roundtrip',action='store_true',help='End once a standby is followed by an active log')
args=parser.parse_args()
if not 1<=args.seconds<=360:parser.error('--seconds must be 1..360')
port=serial.Serial();port.port=args.port;port.baudrate=115200;port.timeout=.2
port.dtr=False;port.rts=False
active=standby=errors=0
try:
    port.open();print(f'Observing {args.port} for {args.seconds}s; DTR/RTS disabled, no writes',flush=True)
    deadline=time.monotonic()+args.seconds;pending=b''
    while time.monotonic()<deadline:
        pending+=port.read(max(1,port.in_waiting))
        while b'\n' in pending:
            raw,pending=pending.split(b'\n',1)
            line=re.sub(r'\x1b\[[0-9;]*m','',raw.decode('utf-8','replace')).strip()
            bad=bool(re.search(r'Guru Meditation|panic|Brownout|abort\(|watchdog|underrun|can.t fetch data|Blit failed|Unsupported tear mode',line,re.I))
            if bad:errors+=1
            if ('luna_power:' in line or 'Frequency switching config:' in line or
                    'Display tear avoidance mode=' in line or
                    'Wi-Fi online; device-owned HTTPS weather enabled' in line or
                    'Weather configured=' in line or bad):
                print(line,flush=True)
            if 'luna_power:' in line and 'ACTIVE:' in line:
                active+=1
                if args.until_roundtrip and standby:deadline=0
            if 'luna_power:' in line and 'STANDBY:' in line:standby+=1
        if len(pending)>4096:pending=pending[-4096:]
finally:
    port.close();print(f'COM released; ACTIVE={active}, STANDBY={standby}, error markers={errors}',flush=True)
raise SystemExit(1 if errors else 0)
