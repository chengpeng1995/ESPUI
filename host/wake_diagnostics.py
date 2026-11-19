#!/usr/bin/env python3
"""Read RAM wake history after battery testing; never wakes or changes settings."""
import argparse, json, re, time
from pathlib import Path
import serial

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--port',default='/dev/cu.usbmodem101');p.add_argument('--out',required=True)
a=p.parse_args();out=Path(a.out);out.mkdir(parents=True,exist_ok=True);raw=''
with serial.Serial(a.port,115200,timeout=.03) as port:
    def read(seconds):
        global raw
        end=time.monotonic()+seconds;block=''
        while time.monotonic()<end:block+=port.read(port.in_waiting or 1).decode('utf-8','replace')
        raw+=block;return block
    read(.2)
    for command,marker in [('power history','POWER_HISTORY'),('state','DEVICE_STATE'),('power state','POWER state='),('power boot','POWER_BOOT')]:
        found=False
        for _ in range(4):
            port.write(('@UI '+command+'\n').encode());block=read(.9)
            if marker in block:found=True;break
        if not found:break
(out/'wake-diagnostics.log').write_text(raw)
traces=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in raw.splitlines() if 'PWR_TRACE ' in line]
states=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in raw.splitlines() if 'POWER state=' in line]
boots=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in raw.splitlines() if 'POWER_BOOT ' in line]
result={'read_only':True,'history_available':'POWER_HISTORY' in raw,'traces':traces,'latest_power':states[-1] if states else None,'boot_evidence':boots[-1] if boots else None,'fatal_error':bool(re.search(r'Guru Meditation|CORRUPT|assert failed|Task watchdog|abort\(',raw)),'boundary':'RAM history clears after reboot; Blue 2.6.2 also records reset reason and checksum-validated RTC phase across supported warm resets. Cold power-on invalidates prior evidence. Reconnecting and intentional touches must be described by the operator.'}
(out/'wake-diagnostics.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
print(json.dumps(result,ensure_ascii=False))
if not result['history_available'] or result['fatal_error']:raise SystemExit(1)
