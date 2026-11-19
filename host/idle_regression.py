#!/usr/bin/env python3
"""Real-device empty-touch fault injection and untouched automatic standby."""
import argparse,json,re,time
from pathlib import Path
import serial
p=argparse.ArgumentParser();p.add_argument('--port',required=True);p.add_argument('--out',default='logs/blue-2.5');a=p.parse_args();out=Path(a.out);out.mkdir(parents=True,exist_ok=True)
log=(out/'idle-regression.log').open('w')
with serial.Serial(a.port,115200,timeout=.03) as port:
    def read(seconds):
        end=time.monotonic()+seconds;raw=''
        while time.monotonic()<end:
            d=port.read(port.in_waiting or 1).decode('utf-8','replace');raw+=d;log.write(d);log.flush()
        assert not re.search(r'Guru Meditation|CORRUPT|assert failed|Task watchdog|ESP_ERROR_CHECK|abort\(',raw),raw
        return raw
    def send(c,t=.4):
        log.write('\nCHECKPOINT '+c+'\n');port.write(('@UI '+c+'\n').encode());return read(t)
    def query(c,marker):
        for i in range(5):
            raw=send(c,.45);m=re.findall(marker+r' ([^\n]+)',raw)
            if m:return dict(re.findall(r'(\w+)=([^\s]+)',m[-1]))
        raise AssertionError('Missing '+marker)
    read(1);send('wake');send('page 0');initial=query('state','DEVICE_STATE');timeout=int(initial['timeout_s']);assert timeout>0
    # No host actions until automatic idle enters standby.
    read(timeout+2);start=query('power state','POWER');assert start['state']=='2',start
    rejected=int(start['touch_rejected']);cycles=start['cycles']
    for i in range(100):
        send('power test empty-touch',.1)
        if i%20==19:
            s=query('power state','POWER');assert s['state']=='2' and s['cycles']==cycles and s['failures']=='0',s
            assert all(s[k]=='0' for k in ['frames_delta','te_delta','imu_delta']),s
            print('empty-touch',i+1,flush=True)
    read(20);finish=query('power state','POWER')
    assert finish['state']=='2' and finish['cycles']==cycles and int(finish['touch_rejected'])>=rejected+100,finish
    assert all(finish[k]=='0' for k in ['frames_delta','te_delta','imu_delta'])
    result={'pass':True,'automatic_timeout_s':timeout,'empty_touch_injections':100,'touch_rejected_delta':int(finish['touch_rejected'])-rejected,'standby_ms':int(finish['standby_ms']),'spontaneous_panel_wakes':0,'frames_delta':finish['frames_delta'],'te_delta':finish['te_delta'],'imu_delta':finish['imu_delta'],'boundary':'Real coordinator and CST9217 reads; injected wake notifications are not physical touches.'}
    (out/'idle-regression.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
log.close()
