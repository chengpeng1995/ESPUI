#!/usr/bin/env python3
"""Additional USB checks: compatibility, audio inhibitors, paused CPU counters.
No recordings are exported. The short RAM recording is released before exit.
"""
import argparse,json,re,time
from pathlib import Path
import serial
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--port',required=True);args=p.parse_args()
out=Path('logs/power-2.4');log=(out/'device-followup.log').open('w')
with serial.Serial(args.port,115200,timeout=.04) as port:
    def read(seconds):
        end=time.monotonic()+seconds;raw=''
        while time.monotonic()<end:
            data=port.read(port.in_waiting or 1).decode('utf-8','replace');raw+=data;log.write(data);log.flush()
        assert not re.search(r'Guru Meditation|CORRUPT|assert failed|ESP_ERROR_CHECK|Task watchdog|abort\(\)',raw),raw
        return raw
    def send(command,delay=.5):
        log.write('\nCHECKPOINT '+command+'\n');log.flush();port.write(('@UI '+command+'\n').encode());return read(delay)
    def query(command,marker):
        for attempt in range(5):
            raw=send(command,.7);matches=re.findall(marker+r' ([^\n]+)',raw)
            if matches:return dict(re.findall(r'(\w+)=([^\s]+)',matches[-1])),raw
        raise AssertionError('No response: '+marker)
    try:
        read(1);send('wake');send('page 18');send('set power.saving 0');send('sleep')
        state,_=query('power state','POWER');assert state['state']=='4' and state['suspended']=='0',state
        send('wake');send('set power.saving 1');send('page 17');send('click audio.record',1.1)
        state,_=query('state','assistant_hw: STATE');assert state['recording']=='1',state
        send('sleep');state,_=query('state','DEVICE_STATE');assert state['asleep']=='0',state
        send('click audio.record');send('click audio.play',.2);send('sleep',.15)
        state,_=query('power state','POWER');assert state['state']=='0',state
        read(4);send('exit');send('page 0');send('sleep',.8)
        first,raw1=query('power state','POWER');read(12);second,raw2=query('power state','POWER')
        assert first['state']==second['state']=='2'
        assert all(second[key]=='0' for key in ['frames_delta','te_delta','imu_delta'])
        assert second['light_sleep_us']==first['light_sleep_us'],'USB should prevent light sleep'
        def cpu(raw):
            return {int(c):int(v) for c,v in re.findall(r'CPU core=(\d) idle_runtime=(\d+)',raw)}
        a,b=cpu(raw1),cpu(raw2);elapsed=int(second['uptime_us'])-int(first['uptime_us'])
        active={str(c):round(max(0,100*(1-(b[c]-a[c])/elapsed)),3) for c in a if c in b}
        # Clearly marked, expired fixture; only RAM is affected. Reset after this
        # script to return the quota view to its original waiting-for-data state.
        fixture={'type':'quota','version':1,'seq':24001,'now':int(time.time()),'age':86400,'bucket':'TEST power management','plan':'fixture','primary':None,'secondary':None}
        ack=''
        for attempt in range(3):
            port.write((json.dumps(fixture)+'\n').encode());ack+=read(.7)
            if '@CODEX_ACK 24001' in ack:break
        assert '@CODEX_ACK 24001' in ack,'no quota ACK during standby'
        quota,_=query('state','DEVICE_STATE');assert quota['asleep']=='1' and quota['quota_seq']=='24001',quota
        send('wake');send('state')
        result={'pass':True,'compatibility':True,'recording_blocks_sleep':True,'playback_blocks_sleep':True,'quota_ack_while_asleep':True,'standby_sample_us':elapsed,'cpu_active_percent':active,'standby_counters':{key:second[key] for key in ['frames_delta','te_delta','imu_delta','light_sleep_us']},'boundary':'USB connected, debug status sampling included; no battery-current measurement. Reset after test to clear RAM-only quota fixture.'}
        (out/'device-followup.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n');print(json.dumps(result,ensure_ascii=False),flush=True)
    finally:
        send('wake');send('exit');send('page 18');send('set power.saving 1');send('page 0')

log.close()
