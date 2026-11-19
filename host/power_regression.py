#!/usr/bin/env python3
"""USB sleep/wake regression. Physical touch and battery current require separate checks.
Does not erase NVS, change wireless settings, or claim simulated touch as physical input.
"""
import argparse,json,re,time
from pathlib import Path
import serial

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port',required=True)
parser.add_argument('--cycles',type=int,default=100)
parser.add_argument('--out',default='logs/power-2.4')
args=parser.parse_args();out=Path(args.out);out.mkdir(parents=True,exist_ok=True)
log=(out/'device-regression.log').open('w');checks=[];latencies=[];query_retries=0
with serial.Serial(args.port,115200,timeout=.04) as port:
    def read(seconds):
        until=time.monotonic()+seconds;chunks=[]
        while time.monotonic()<until:
            data=port.read(port.in_waiting or 1).decode('utf-8','replace')
            if data:log.write(data);log.flush();chunks.append(data)
        result=''.join(chunks)
        if re.search(r'Guru Meditation|CORRUPT|assert failed|ESP_ERROR_CHECK|Task watchdog|abort\(\)',result):raise AssertionError('device fatal error')
        return result
    def send(command,delay=.3):
        log.write('\nCHECKPOINT '+command+'\n');log.flush();port.write(('@UI '+command+'\n').encode());return read(delay)
    def values(raw,marker):
        matches=re.findall(marker+r' ([^\n]+)',raw)
        if not matches:
            # USB delivery can be batched by the host; wait for the response,
            # without sending a duplicate action or treating silence as success.
            raw+=read(1.5);matches=re.findall(marker+r' ([^\n]+)',raw)
        if not matches:raise AssertionError('missing '+marker+' in '+raw[-800:])
        return dict(re.findall(r'(\w+)=([^\s]+)',matches[-1]))
    def query(command,marker):
        global query_retries
        for attempt in range(5):
            raw=send(command,.4)
            if re.search(marker+r' ([^\n]+)',raw):return values(raw,marker)
            query_retries+=1
        return values(raw,marker)
    try:
        read(2);send('wake');send('page 0');baseline=values(send('state'),'DEVICE_STATE')
        start=values(send('power state'),'POWER')
        for i in range(args.cycles):
            send('sleep',.45)
            state=query('power state','POWER')
            assert state['state']=='2' and state['suspended']=='1',('not standby',i,state)
            assert all(state[k]=='0' for k in ['frames_delta','te_delta','imu_delta']),('work during standby',i,state)
            response=send('wake',.45)
            if 'AWAKE reason=' in response:
                awake=values(response,'AWAKE');latency=int(awake['visible_ms'])
            else:
                awake=query('power state','POWER');assert awake['state']=='0',awake;latency=int(awake['wake_ms'])
            assert awake['failures']=='0',awake
            latencies.append(latency)
            if i%10==0:print(f'cycle {i+1}/{args.cycles}',flush=True)
        finish=values(send('state'),'DEVICE_STATE')
        assert finish['asleep']=='0' and finish['page']==baseline['page'],finish
        assert finish['timer_running']==baseline['timer_running'],finish
        checks.append(f'{args.cycles} USB-triggered suspend/resume cycles; frozen frame/TE/IMU counters in each standby')
        # Real countdown in the dark, with an unsaved form retained underneath the alert.
        send('app 2');send('page 29');send('set timer.minutes 1');send('page 5');send('click timer.toggle');
        send('page 19');send('click time.edit');send('set input 2028-02-29 12:34');draft=values(send('state'),'DEVICE_STATE');assert draft['page']=='20',draft
        send('sleep',.5);deadline=time.monotonic()+64;alert=''
        while time.monotonic()<deadline:
            chunk=read(.5);alert+=chunk
            if 'AWAKE reason=2' in alert:break
        assert 'AWAKE reason=2' in alert,'countdown did not wake'
        state=values(send('state'),'DEVICE_STATE');assert state['alert']=='1' and state['page']=='20' and state['timer_running']=='0',state
        send('click alert.stop');state=values(send('state'),'DEVICE_STATE');assert state['alert']=='0' and state['page']=='20',state
        checks.append('countdown wakes sleeping form and stopping alert returns to the editor')
        # Discard only this script's draft; do not save the test date to the RTC.
        send('exit');send('click confirm.accept');send('app 2');send('click timer.reset');send('click confirm.accept');send('page 29');send('set timer.minutes 5');send('exit');send('page 0');send('power state');send('state')
        result={'pass':True,'cycles':args.cycles,'query_retries':query_retries,'wake_ms':{'min':min(latencies),'max':max(latencies),'p95':sorted(latencies)[int(.95*(len(latencies)-1))]},'checks':checks,'evidence':'Physical device, USB connected. Wake commands are not physical touch. Light sleep and battery current are separate tests.'}
        (out/'device-regression.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n');print(json.dumps(result,ensure_ascii=False),flush=True)
    finally:
        port.write(b'@UI wake\n');read(.4);log.close()
