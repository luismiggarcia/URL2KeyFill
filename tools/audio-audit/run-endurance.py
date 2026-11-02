#!/usr/bin/env python3
"""Scoped macOS awake assertions, watchdog and start/end SDI captures.
Preparing this tool does not start an overnight test. Run explicitly when ready.
"""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
from analyze_loopback import analyse

HEALTH = ['missing_total','api_errors','recoveries','late_video','dropped_video','clock_resets',
          'video_history_misses','video_history_startup_misses','video_conversion_fallbacks']


def summarize(rows, first, last, mode, expected_seconds):
    active = [r for r in rows if r['running'] == '1' and r['on_air'] == '1']
    if not active:
        raise ValueError('No active SDI telemetry')
    duration = float(active[-1]['elapsed_s'])-float(active[0]['elapsed_s'])
    if duration < expected_seconds-5:
        raise ValueError('Incomplete active duration')
    health = {k: max(float(r[k]) for r in active) for k in HEALTH}
    if any(health.values()):
        raise ValueError('Continuity counters failed: '+str(health))
    if any(r['scheduled_activity'] != '1' for r in active):
        raise ValueError('Missing application playback activity')
    ready = [r for r in active if r.get('audio_drift_ready') == '1']
    if not ready or float(ready[-1]['elapsed_s']) < float(active[-1]['elapsed_s'])-2:
        raise ValueError('Drift controller did not remain ready')
    delta = last['channels']['audio_L']['median_ms']-first['channels']['audio_L']['median_ms']
    limit = {'1080p25':40.,'1080p50':20.,'1080p5994':1001/60}[mode]
    if abs(delta) > limit:
        raise ValueError('A/V start/end change exceeds one output frame')
    quality=all(abs(value)<=limit+1 for capture in (first,last) for channel in capture['channels'].values() for value in (channel.get('min_ms',channel['median_ms']),channel.get('max_ms',channel['median_ms'])))
    return dict(av_quality_passed=quality, av_frame_limit_ms=limit, measurement_allowance_ms=1, active_duration_s=duration, health=health, first_av_ms=first['channels']['audio_L']['median_ms'],
                last_av_ms=last['channels']['audio_L']['median_ms'], av_change_ms=delta,
                rate_ppm_range=[min(float(r['audio_rate_ppm']) for r in ready),max(float(r['audio_rate_ppm']) for r in ready)],
                phase_error_ms_range=[min(float(r['audio_phase_error_ms']) for r in ready),max(float(r['audio_phase_error_ms']) for r in ready)],
                minimum_source_headroom_ms=min(float(r['headroom_min_ms']) for r in active if r['headroom_min_ms']),
                limits='One Mac/device/mode; same-device SDI captures at start/end, not continuous independent physical capture.')


def validate_power(power, assertions, pid):
    if "'AC Power'" not in power:
        raise RuntimeError('AC power lost; test stopped rather than accepted without the required power state')
    owned = '\n'.join(line for line in assertions.splitlines() if f'pid {pid}(' in line)
    for kind in ('PreventUserIdleSystemSleep','PreventUserIdleDisplaySleep','PreventSystemSleep'):
        if kind not in owned:
            raise RuntimeError('Required scoped sleep/display assertion is absent: '+kind)


class TelemetryTail:
    def __init__(self):
        self.position=0;self.pending='';self.headers=None;self.last=None

    def latest(self, path):
        if not path.exists():return None
        with path.open() as stream:
            stream.seek(self.position);chunk=stream.read();self.position=stream.tell()
        text=self.pending+chunk;complete,_,self.pending=text.rpartition('\n')
        if not complete:return self.last
        for values in csv.reader(complete.splitlines()):
            if self.headers is None:self.headers=values
            elif len(values)==len(self.headers):self.last=dict(zip(self.headers,values))
        return self.last


def validate_source_flow(previous, latest):
    if latest is None:raise RuntimeError('No complete telemetry rows')
    if latest.get('running')!='1' or latest.get('on_air')!='1':return
    if any(float(latest.get(key,0)) for key in HEALTH):raise RuntimeError('Continuity watchdog detected an error')
    if 'audio_drift_ready' in latest and latest['audio_drift_ready']!='1':raise RuntimeError('Drift controller lost readiness')
    if previous:
        for key in ('source_flash_count','source_pulse_count'):
            if int(latest[key])<=int(previous[key]):raise RuntimeError('Source diagnostic stopped advancing: '+key)


def captured(path):
    result=analyse(list(csv.DictReader(path.open())))
    if result['video_timestamp_gaps_within_matched_range']:
        raise RuntimeError('Input timestamps are not contiguous in the measurement window')
    channels=result['channels']
    if abs(channels['audio_L']['median_ms']-channels['audio_R']['median_ms'])>1/48:
        raise RuntimeError('Stereo source channels do not coincide in physical capture')
    return result


def intermediate_due(elapsed, next_at, total, interval):
    # Leave time for lock, 30 s of acquisition and shutdown before the final
    # capture at total-45. Never overlap input ownership with start/end probes.
    return interval > 0 and elapsed >= next_at and elapsed+40 < total-45


def sampled_quality(captures, frame_limit):
    values=[v for c in captures for channel in c['channels'].values()
            for v in (channel.get('min_ms',channel['median_ms']),channel.get('max_ms',channel['median_ms']))]
    lo,hi=min(values),max(values)
    return dict(av_quality_passed=max(abs(lo),abs(hi))<=frame_limit+1 and hi-lo<=frame_limit+1,
                sampled_av_min_ms=lo,sampled_av_max_ms=hi,sampled_av_span_ms=hi-lo,
                sampled_quality_limit='Absolute A/V and full sampled envelope <= one frame + 1 ms allowance; not a continuous capture guarantee.')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--app', required=True, help='Signed Contents/MacOS/URL2KeyFill')
    p.add_argument('--capture', required=True)
    p.add_argument('--directory', required=True, help='New evidence directory')
    p.add_argument('--mode', choices=['1080p25','1080p50','1080p5994'], default='1080p25')
    p.add_argument('--seconds', type=int, default=28800)
    p.add_argument('--timing-profile', choices=['reliable','low'], default='reliable')
    p.add_argument('--no-calibrate', action='store_true', help='Diagnostic raw run; absolute A/V quality still must pass')
    p.add_argument('--sample-interval-seconds', type=int, default=0,
                   help='Optional intermediate 30 s captures, every 90+ s; 0 disables')
    p.add_argument('--diagnostic-stop-after-seconds',type=int,default=0,help='Deliberately fail the watchdog after this many active seconds, to verify trace preservation')
    p.add_argument('--dry-run', action='store_true')
    args = p.parse_args()
    if args.diagnostic_stop_after_seconds and not 2 <= args.diagnostic_stop_after_seconds < args.seconds:
        p.error('--diagnostic-stop-after-seconds must be 2..seconds-1')
    if not 120 <= args.seconds <= 28800:
        p.error('--seconds must be 120..28800')
    if args.sample_interval_seconds != 0 and args.sample_interval_seconds < 90:
        p.error('--sample-interval-seconds must be 0 or at least 90')
    app, capture = str(Path(args.app).resolve()), str(Path(args.capture).resolve())
    root = Path(args.directory).resolve()
    total_seconds=args.seconds+(0 if args.no_calibrate else 45)
    stop_file=root/'diagnostic-stop.request'
    command = [app,'--sdi-diagnostic-stop-file',str(stop_file),'--sdi-av-test','--hardware-test-mode',args.mode,'--audio-baseline-seconds',str(total_seconds),'--timing-profile',args.timing_profile]
    if not args.no_calibrate:command += ['--sdi-sync-calibration-file',str(root/'sync-calibration.txt')]
    if args.dry_run:
        print(json.dumps(dict(command=command, awake=['/usr/bin/caffeinate','-dims','-w','APP_PID'], requirements='AC power, lid open, SDI OUT to IN; no overnight test started'), indent=2))
        return 0
    power = subprocess.check_output(['/usr/bin/pmset','-g','batt'], text=True)
    if "'AC Power'" not in power:
        raise RuntimeError('Connect AC power before starting the endurance test')
    root.mkdir(parents=True, exist_ok=False)
    metadata = dict(mode=args.mode, seconds=args.seconds, timing_profile=args.timing_profile,
                    app_sha256=hashlib.sha256(Path(app).read_bytes()).hexdigest(), capture_sha256=hashlib.sha256(Path(capture).read_bytes()).hexdigest(), command=command, initial_power=power,
                    runner_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    children = []; start_capture = None; end_capture = None;calibration=None;calibration_applied=None;measured_began=None
    samples=[];sample_child=None;next_sample=args.sample_interval_seconds
    metadata['sample_interval_seconds']=args.sample_interval_seconds
    try:
        with (root/'playback.log').open('w') as log:
            env = dict(os.environ, TMPDIR=str(root), U2K_AUDIO_TRACE=str(root/'application.csv'))
            play = subprocess.Popen(command, stdout=log, stderr=log, env=env);children.append(play)
            awake = subprocess.Popen(['/usr/bin/caffeinate','-dims','-w',str(play.pid)]);children.append(awake);metadata['playback_pid']=play.pid;metadata['awake_pid']=awake.pid
            deadline = time.monotonic()+30
            while 'START SDI loopback source:' not in (root/'playback.log').read_text():
                if play.poll() is not None or time.monotonic()>deadline:
                    raise RuntimeError('Output startup failed; inspect playback.log')
                time.sleep(.1)
            began = time.monotonic(); checked = -100.; snapshot=0;tail=TelemetryTail();previous_flow=None;last_trace_size=0;trace_progress=began
            if args.no_calibrate:measured_began=began
            while play.poll() is None:
                elapsed = time.monotonic()-began
                if args.diagnostic_stop_after_seconds and elapsed>=args.diagnostic_stop_after_seconds:raise RuntimeError('Deliberate diagnostic watchdog stop')
                if awake.poll() is not None:
                    # The assertion can end normally only when its watched app exits.
                    if play.poll() is None:raise RuntimeError('Awake assertion process ended during playback')
                if elapsed-checked >= 15:
                    checked=elapsed
                    battery = subprocess.check_output(['/usr/bin/pmset','-g','batt'], text=True)
                    assertions = subprocess.check_output(['/usr/bin/pmset','-g','assertions'], text=True)
                    (root/f'power-{snapshot:04d}.txt').write_text(battery+'\n'+'\n'.join(line for line in assertions.splitlines() if f'pid {awake.pid}(' in line));snapshot+=1
                    validate_power(battery, assertions, awake.pid)
                    trace=root/'application.csv'
                    size=trace.stat().st_size if trace.exists() else 0
                    if size>last_trace_size:last_trace_size=size;trace_progress=time.monotonic()
                    if elapsed>10 and time.monotonic()-trace_progress>15:
                        raise RuntimeError('Telemetry watchdog timed out')
                    latest=tail.latest(trace)
                    if elapsed>45:validate_source_flow(previous_flow,latest)
                    if latest:previous_flow=latest
                if not args.no_calibrate and calibration is None and elapsed>=8:
                    with (root/'capture-calibration.log').open('w') as caplog:
                        calibration=subprocess.Popen([capture,'--mode',args.mode,'--seconds','20','--csv',str(root/'capture-calibration.csv')],stdout=caplog,stderr=caplog)
                    children.append(calibration)
                if calibration and calibration.poll() is not None and calibration_applied is None:
                    if calibration.returncode:raise RuntimeError('Calibration capture failed')
                    initial=captured(root/'capture-calibration.csv')
                    values=initial['channels']['audio_L'];centre=(values['min_ms']+values['max_ms'])/2
                    adjustment=-round(centre)
                    if abs(adjustment)>200:raise RuntimeError('Measured calibration exceeds adjustment range')
                    temp=root/'sync-calibration.tmp';temp.write_text(str(adjustment)+'\n');temp.replace(root/'sync-calibration.txt')
                    metadata['calibration']=dict(initial_av=values,adjustment_ms=adjustment,method='Negative midpoint of physically measured min/max; same output session')
                    (root/'calibration.json').write_text(json.dumps(metadata['calibration'],indent=2)+'\n')
                    calibration_applied=time.monotonic()
                if calibration_applied and measured_began is None and time.monotonic()-calibration_applied>=2:
                    marker=f"SYNC_CALIBRATION ms={metadata['calibration']['adjustment_ms']} "
                    if marker in (root/'playback.log').read_text():measured_began=time.monotonic();metadata['measurement_started_after_s']=measured_began-began
                if measured_began is None and elapsed>43:raise RuntimeError('Calibration did not finish within its preparation budget')
                measured_elapsed=0 if measured_began is None else time.monotonic()-measured_began
                if sample_child and sample_child.poll() is not None:
                    if sample_child.returncode:raise RuntimeError('Intermediate capture failed')
                    sample_child=None
                if measured_began is not None and measured_elapsed>(8 if args.no_calibrate else 2) and start_capture is None:
                    with (root/'capture-start.log').open('w') as caplog:
                        start_capture=subprocess.Popen([capture,'--mode',args.mode,'--seconds','30','--csv',str(root/'capture-start.csv')],stdout=caplog,stderr=caplog)
                    children.append(start_capture)
                if measured_began is not None and measured_elapsed>=args.seconds-45 and end_capture is None:
                    if start_capture and start_capture.poll() is None:raise RuntimeError('Initial capture did not finish')
                    if sample_child:raise RuntimeError('Intermediate capture overlaps final capture')
                    with (root/'capture-end.log').open('w') as caplog:
                        end_capture=subprocess.Popen([capture,'--mode',args.mode,'--seconds','30','--csv',str(root/'capture-end.csv')],stdout=caplog,stderr=caplog)
                    children.append(end_capture)
                if (measured_began is not None and start_capture and start_capture.poll() is not None
                        and sample_child is None and intermediate_due(measured_elapsed,next_sample,args.seconds,args.sample_interval_seconds)):
                    if start_capture.returncode:raise RuntimeError('Initial capture failed')
                    name=f'capture-sample-{len(samples):03d}'
                    with (root/(name+'.log')).open('w') as caplog:
                        sample_child=subprocess.Popen([capture,'--mode',args.mode,'--seconds','30','--csv',str(root/(name+'.csv'))],stdout=caplog,stderr=caplog)
                    children.append(sample_child);samples.append(dict(name=name,measurement_elapsed_s=measured_elapsed))
                    next_sample=measured_elapsed+args.sample_interval_seconds
                if elapsed>total_seconds+30:raise RuntimeError('Playback exceeded duration watchdog')
                time.sleep(.25)
            metadata['playback_exit']=play.wait()
            for child in (start_capture,end_capture):
                if child is None or child.wait(timeout=5)!=0:raise RuntimeError('A required physical capture failed')
            if metadata['playback_exit']:raise RuntimeError('Playback failed')
        rows=list(csv.DictReader((root/'application.csv').open()))
        first=captured(root/'capture-start.csv')
        last=captured(root/'capture-end.csv')
        result=summarize(rows,first,last,args.mode,args.seconds)
        quality_captures=[first,last]
        for sample in samples:
            measured=captured(root/(sample['name']+'.csv'))
            quality_captures.append(measured)
            sample['channels']={k:{q:v[q] for q in ('pairs','median_ms','min_ms','max_ms')} for k,v in measured['channels'].items()}
            (root/(sample['name']+'.json')).write_text(json.dumps(measured,indent=2)+'\n')
        result.update(sampled_quality(quality_captures,result['av_frame_limit_ms']))
        result['intermediate_captures']=samples
        if measured_began is None or time.monotonic()-measured_began<args.seconds-1:raise RuntimeError('Incomplete post-calibration measurement duration')
        result['post_calibration_duration_s']=time.monotonic()-measured_began
        for name,data in [('capture-start.json',first),('capture-end.json',last),('result.json',result)]:
            (root/name).write_text(json.dumps(data,indent=2)+'\n')
        metadata['continuity_passed']=True;metadata['accepted']=result['av_quality_passed'];print(json.dumps(result,indent=2));return 0 if metadata['accepted'] else 1
    except Exception as e:
        metadata['accepted']=False;metadata['error']=str(e);raise
    finally:
        # Ask this diagnostic app to flush its bounded traces and stop on the UI
        # thread. Force termination remains bounded if that thread is unresponsive.
        if children and children[0].poll() is None:
            stop_file.write_text('stop\n')
            try:children[0].wait(timeout=8)
            except subprocess.TimeoutExpired:pass
        text=(root/'playback.log').read_text() if (root/'playback.log').exists() else ''
        metadata['diagnostic_stop_acknowledged']='SYNC_DIAGNOSTIC_STOP ' in text
        metadata['diagnostic_trace_preserved']='SYNC_TRACE kind=callback ' in text and 'SYNC_TRACE kind=recovery ' in text and 'SYNC_TRACE kind=completion ' in text
        # Stop only this runner's app and captures, then release its own assertions.
        for child in reversed(children):
            if child.poll() is None:
                child.terminate()
                try:child.wait(timeout=8)
                except subprocess.TimeoutExpired:child.kill();child.wait()
        (root/'manifest.json').write_text(json.dumps(metadata,indent=2)+'\n')


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (RuntimeError,ValueError,OSError,subprocess.SubprocessError) as e:
        print('FAIL '+str(e),file=sys.stderr);raise SystemExit(1)
