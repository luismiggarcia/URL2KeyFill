#!/usr/bin/env python3
"""Bounded, sequential SDI validation; owns only its child process groups.
No device reset, driver change, persistent configuration write or publication.
"""
import argparse
import csv
from datetime import datetime
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time
from zoneinfo import ZoneInfo
from analyze_live_sync import analyse as live_analysis
from analyze_event_sync import analyse as event_analysis
import importlib.util
_spec=importlib.util.spec_from_file_location('u2k_endurance',Path(__file__).with_name('run-endurance.py'))
_endurance=importlib.util.module_from_spec(_spec);_spec.loader.exec_module(_endurance)
validate_power,HEALTH=_endurance.validate_power,_endurance.HEALTH


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--app',required=True);p.add_argument('--capture',required=True)
    p.add_argument('--status-probe',required=True)
    p.add_argument('--until',required=True,help='ISO local date/time in Europe/Lisbon')
    p.add_argument('--directory',required=True);p.add_argument('--dry-run',action='store_true')
    args=p.parse_args();deadline=datetime.fromisoformat(args.until).replace(tzinfo=ZoneInfo('Europe/Lisbon')).timestamp()
    if deadline<=time.time():p.error('Deadline has already passed')
    root=Path(args.directory).resolve();base=Path(__file__).resolve().parent
    if args.dry_run:
        print(json.dumps(dict(until=args.until,timezone='Europe/Lisbon',sequence='8 live changes and repeated starts, repeated 20-minute sampled 25p/50p/59.94p',directory=str(root)),indent=2));return
    root.mkdir(parents=True,exist_ok=False)
    state=dict(started_at=datetime.now().astimezone().isoformat(),deadline=args.until,timezone='Europe/Lisbon',pid=os.getpid(),status='running',cases=[],limits='Same Mac and UltraStudio; sampled physical captures, no independent clock or human lip-sync certification.')
    current=None;awake=None;last_power=0;interrupted=False
    def save():
        temp=root/'state.tmp';temp.write_text(json.dumps(state,indent=2)+'\n');temp.replace(root/'state.json')
    def stop_signal(signum,frame):
        nonlocal interrupted
        interrupted=True
    signal.signal(signal.SIGTERM,stop_signal);signal.signal(signal.SIGINT,stop_signal)
    def cleanup():
        nonlocal current
        if current is not None:
            try:os.killpg(current.pid,signal.SIGINT)
            except ProcessLookupError:pass
            try:current.wait(timeout=4)
            except subprocess.TimeoutExpired:
                try:os.killpg(current.pid,signal.SIGKILL)
                except ProcessLookupError:pass
                current.wait()
            current=None
    def check():
        nonlocal last_power
        if interrupted:raise RuntimeError('Stopped by signal')
        if time.time()>=deadline:raise TimeoutError('09:00 deadline reached')
        if awake.poll() is not None:raise RuntimeError('Scoped awake assertions ended')
        if time.monotonic()-last_power>=30:
            battery=subprocess.check_output(['/usr/bin/pmset','-g','batt'],text=True,timeout=5)
            assertions=subprocess.check_output(['/usr/bin/pmset','-g','assertions'],text=True,timeout=5)
            validate_power(battery,assertions,awake.pid)
            with (root/'power.jsonl').open('a') as f:f.write(json.dumps(dict(at=datetime.now().astimezone().isoformat(),awake_pid=awake.pid,ac=True,assertions_valid=True))+'\n')
            last_power=time.monotonic()
    def command(cmd,log,timeout):
        nonlocal current
        check();began=time.monotonic()
        with log.open('w') as out:
            current=subprocess.Popen(cmd,stdout=out,stderr=subprocess.STDOUT,start_new_session=True,env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1'))
            state['active_process_group']=current.pid;state['active_command']=cmd;save()
            try:
                while current.poll() is None:
                    check()
                    if time.monotonic()-began>timeout:raise RuntimeError('Command watchdog timeout')
                    time.sleep(.25)
                rc=current.returncode
                cleanup();state.pop('active_process_group',None);state.pop('active_command',None);save();return rc
            except BaseException:cleanup();raise
    def free_device():
        rc=command([args.status_probe],root/'latest-device-status.txt',10)
        text=(root/'latest-device-status.txt').read_text()
        if rc or 'key=62757379 result=0 value=0' not in text:raise RuntimeError('Device absent, busy, or status unavailable; no other owner interrupted')
    def health(path):
        if not path.exists():raise RuntimeError('Missing application telemetry')
        with path.open() as f:rows=list(csv.DictReader(f))
        if not rows:raise RuntimeError('Empty application telemetry')
        result={k:max(float(r.get(k,0)) for r in rows) for k in HEALTH}
        if any(result.values()):raise RuntimeError('Continuity counters failed: '+str(result))
        return rows,result
    def record(name,mode,cmd,timeout,kind,initial=0,final=0):
        if time.time()+timeout+10>=deadline:return False
        free_device();case=root/name
        item=dict(name=name,mode=mode,kind=kind,started_at=datetime.now().astimezone().isoformat(),status='running');state['cases'].append(item);save()
        rc=command(cmd,root/(name+'.runner.log'),timeout)
        item.update(process_exit=rc,finished_at=datetime.now().astimezone().isoformat())
        if kind=='live':
            q=case/'loopback';rows,h=health(q/'application.csv')
            with (q/'capture.csv').open() as f:captured=list(csv.DictReader(f))
            log=(q/'playback.log').read_text();result=live_analysis(log,rows,captured,final,initial)
            (case/'live-analysis.json').write_text(json.dumps(result,indent=2)+'\n')
            events=event_analysis(log,captured);(case/'events.json').write_text(json.dumps(events,indent=2)+'\n')
            item['change_ms']=result['change_ms']
            if rc:raise RuntimeError('Live test subprocess failed')
            item['status']='passed'
        else:
            _,h=health(case/'application.csv')
            result=json.loads((case/'result.json').read_text()) if (case/'result.json').exists() else None
            if result is None:raise RuntimeError('Endurance test failed before producing an analysed result')
            item['status']='passed' if rc==0 else 'quality_failed'
            item['av_quality_passed']=result['av_quality_passed'];item['av_change_ms']=result['av_change_ms']
            if rc and result.get('health') and any(result['health'].values()):raise RuntimeError('Endurance I/O failure')
        save();return True
    try:
        awake=subprocess.Popen(['/usr/bin/caffeinate','-dims','-w',str(os.getpid())]);state['awake_pid']=awake.pid;save()
        # Give pmset time to publish the newly created assertions.
        time.sleep(1);check()
        cycle=0
        while time.time()<deadline-90:
            # Repeated starts exercise release/reacquisition without power cycling.
            for mode in ['1080p25','1080p50']:
                for initial,final in [(0,-1),(0,-5),(0,-21),(-5,5) if mode=='1080p25' else (5,-5)]:
                    name=f'cycle{cycle:02d}-live-{mode}-{initial}-to-{final}'
                    cmd=[sys.executable,str(base/'run-content-reference.py'),'--app',args.app,'--capture',args.capture,'--mode',mode,'--selection','content','--seconds','35','--sync-offset-ms',str(initial),'--sync-change-ms',str(final),'--directory',str(root/name)]
                    if not record(name,mode,cmd,100,'live',initial,final):break
            for mode in ['1080p25','1080p50','1080p5994']:
                name=f'cycle{cycle:02d}-sampled-{mode}'
                cmd=[sys.executable,str(base/'run-endurance.py'),'--app',args.app,'--capture',args.capture,'--mode',mode,'--seconds','1200','--sample-interval-seconds','120','--directory',str(root/name)]
                if not record(name,mode,cmd,1350,'sampled'):break
            cycle+=1
            if time.time()+150>=deadline:break
        state['status']='completed';state['stop_reason']='Insufficient time to start another complete test before 09:00'
    except TimeoutError as e:state.update(status='deadline_reached',stop_reason=str(e))
    except BaseException as e:state.update(status='failed',stop_reason=str(e));raise
    finally:
        cleanup()
        if awake is not None:
            awake.terminate();awake.wait(timeout=5)
        state['finished_at']=datetime.now().astimezone().isoformat();save()


if __name__=='__main__':main()
