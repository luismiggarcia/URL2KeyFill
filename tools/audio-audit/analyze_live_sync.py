#!/usr/bin/env python3
"""Compare physical A/V before/after a live adjustment, excluding its transition."""
import argparse
import csv
import json
from pathlib import Path
import statistics
from analyze_identified_sync import records


def analyse(log, rows, capture, expected_ms, initial_ms=0):
    if f'SYNC_ADJUSTMENT ms={expected_ms} ' not in log:
        raise ValueError('Missing live adjustment marker')
    origins = {}
    for r in records(log,'REFERENCE_SCHEDULE'):
        identity=int(r['id'])
        origins.setdefault(identity,int(r['stream_samples']))
    flashes=[];white=False
    for r in capture:
        if r['kind']!='video_id':continue
        identity=int(float(r['value']));now_white=identity>=50 and identity%50<2
        if now_white and not white:
            flashes.append((int(r['pts_48k']),origins[identity]/48000,identity,r.get('host_48k')))
        white=now_white
    audio=[int(r['pts_48k']) for r in capture if r['kind']=='audio_L'];used=set();before=[];after=[]
    latency_before=[];latency_after=[]
    paints=records(log,'REFERENCE_PAINT')
    source_origins={int(r['audio_origin']) for r in paints if int(r['audio_origin'])!=-(1<<63)}
    source_origin=next(iter(source_origins)) if len(source_origins)==1 else None
    packets=[r for r in capture if r['kind']=='packet']
    for video,stream,identity,host in flashes:
        available=[i for i in range(len(audio)) if i not in used and abs(audio[i]-video)<19200]
        if not available:continue
        i=min(available,key=lambda i:abs(audio[i]-video));used.add(i);offset=(audio[i]-video)/48
        window=None
        if 7<=stream<=14:before.append(offset);window=latency_before
        elif 17<=stream<=28:after.append(offset);window=latency_after
        if window is not None and host is not None and source_origin is not None:
            packet=next((r for r in packets if int(r['pts_48k'])<=audio[i]<int(r['pts_48k'])+int(r['duration_48k'])),None)
            if packet is None or 'host_48k' not in packet:raise ValueError('Missing timestamped audio packet for latency estimate')
            # Remove event position within its input packet before comparing
            # callback timing. Includes input buffering/callback latency.
            audio_host=int(packet['host_48k'])-int(packet['duration_48k'])+audio[i]-int(packet['pts_48k'])
            window.append(dict(video=(int(host)-source_origin-identity*960)/48,
                               audio=(audio_host-source_origin-(identity//50)*48000-5)/48))
    if len(before)<5 or len(after)<5:raise ValueError('Too few steady before/after physical pairs')
    delta=statistics.median(after)-statistics.median(before)
    requested_change=expected_ms-initial_ms
    if abs(delta-requested_change)>1:raise ValueError('Live adjustment did not produce the requested steady A/V change')
    health={k:max(float(r[k]) for r in rows) for k in ['missing_total','api_errors','recoveries','dropped_video','late_video','clock_resets','video_history_misses','video_conversion_fallbacks']}
    if any(health.values()):raise ValueError('I/O continuity failed: '+str(health))
    if any(float(r.get('video_history_startup_misses',0)) for r in rows):raise ValueError('I/O continuity failed: startup history misses')
    result=dict(before_pairs=len(before),after_pairs=len(after),before_av_ms=statistics.median(before),after_av_ms=statistics.median(after),change_ms=delta,requested_ms=expected_ms,initial_ms=initial_ms,requested_change_ms=requested_change,health=health,limits='Steady windows exclude the 5 ms fade and SDK queue drain; this is not subjective validation of the transition.')
    if latency_before and latency_after:
        result['incremental_callback_latency_estimate']={kind:dict(before_median_ms=statistics.median(r[kind] for r in latency_before),after_median_ms=statistics.median(r[kind] for r in latency_after),change_ms=statistics.median(r[kind] for r in latency_after)-statistics.median(r[kind] for r in latency_before)) for kind in ('video','audio')}
        result['incremental_latency_limits']='Same-session content-to-input-callback estimates, with nominal audio packet-position correction; includes input and software scheduling. Changes are not absolute wire/display latency measurements.'
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory');p.add_argument('--expected-ms',required=True,type=int);p.add_argument('--initial-ms',type=int,default=0);args=p.parse_args();root=Path(args.directory)/'loopback'
    result=analyse((root/'playback.log').read_text(),list(csv.DictReader((root/'application.csv').open())),list(csv.DictReader((root/'capture.csv').open())),args.expected_ms,args.initial_ms)
    print(json.dumps(result,indent=2))
