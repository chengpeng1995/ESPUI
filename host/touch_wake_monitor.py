#!/usr/bin/env python3
"""Read-only monitor for a human touch followed by automatic screen timeout.

Does not create timers, save settings, inject touch events, or wake the display.
Touch reports in the log require separate confirmation from the human operator.
"""
import argparse, json, re, time
from pathlib import Path
import serial

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--port',required=True)
p.add_argument('--seconds',type=float,default=90)
p.add_argument('--out',default='logs/blue-2.5')
a=p.parse_args();out=Path(a.out);out.mkdir(parents=True,exist_ok=True)
lines=[];start=time.monotonic();next_query=start
with (out/'physical-touch-monitor.log').open('w') as log, serial.Serial(a.port,115200,timeout=.05) as port:
    while time.monotonic()-start<a.seconds:
        now=time.monotonic()
        if now>=next_query:
            port.write(b'@UI state\n');next_query=now+5
        data=port.read(port.in_waiting or 1).decode('utf-8','replace')
        if data:
            lines.append(data);log.write(data);log.flush()
raw=''.join(lines)
wakes=re.findall(r'AWAKE reason=(\d+) visible_ms=(\d+) failures=(\d+)',raw)
reports=re.findall(r'TOUCH_WAKE_REPORT x=(\d+) y=(\d+) irq=(\d+)',raw)
acks=re.findall(r'TOUCH reads=(\d+) acks=(\d+) ack_errors=(\d+)',raw)
standby=re.findall(r'POWER state=2 [^\n]*standby_ms=(\d+) frames_delta=(\d+) te_delta=(\d+) imu_delta=(\d+)',raw)
fatal=bool(re.search(r'Guru Meditation|CORRUPT|assert failed|Task watchdog|abort\(',raw))
result={'monitor_seconds':round(time.monotonic()-start,1),'touch_reports':reports,'wake_events':wakes,'touch_ack_counters':acks[-1] if acks else None,'last_standby_counters':standby[-1] if standby else None,'fatal_error':fatal,'boundary':'Read-only serial evidence. A touch report alone is not proof of an intentional human touch; compare with operator feedback.'}
(out/'physical-touch-monitor.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result),flush=True)
if fatal:raise SystemExit(1)
