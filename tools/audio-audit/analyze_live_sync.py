#!/usr/bin/env python3
"""Compare physical A/V before/after a live adjustment, excluding its transition."""
import argparse
import csv
import json
from pathlib import Path
import statistics
from analyze_identified_sync import records


def analyse(log, rows, capture, expected_ms):
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
            flashes.append((int(r['pts_48k']),origins[identity]/48000))
        white=now_white
    audio=[int(r['pts_48k']) for r in capture if r['kind']=='audio_L'];used=set();before=[];after=[]
    for video,stream in flashes:
        available=[i for i in range(len(audio)) if i not in used and abs(audio[i]-video)<19200]
        if not available:continue
        i=min(available,key=lambda i:abs(audio[i]-video));used.add(i);offset=(audio[i]-video)/48
        if 7<=stream<=14:before.append(offset)
        elif 17<=stream<=28:after.append(offset)
    if len(before)<5 or len(after)<5:raise ValueError('Too few steady before/after physical pairs')
    delta=statistics.median(after)-statistics.median(before)
    if abs(delta-expected_ms)>1:raise ValueError('Live adjustment did not produce the requested steady A/V change')
    health={k:max(float(r[k]) for r in rows) for k in ['missing_total','api_errors','recoveries','dropped_video','late_video','clock_resets','video_history_misses','video_conversion_fallbacks']}
    if any(health.values()):raise ValueError('I/O continuity failed: '+str(health))
    return dict(before_pairs=len(before),after_pairs=len(after),before_av_ms=statistics.median(before),after_av_ms=statistics.median(after),change_ms=delta,requested_ms=expected_ms,health=health,limits='Steady windows exclude the 5 ms fade and SDK queue drain; this is not subjective validation of the transition.')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory');p.add_argument('--expected-ms',required=True,type=int);args=p.parse_args();root=Path(args.directory)/'loopback'
    result=analyse((root/'playback.log').read_text(),list(csv.DictReader((root/'application.csv').open())),list(csv.DictReader((root/'capture.csv').open())),args.expected_ms)
    print(json.dumps(result,indent=2))
