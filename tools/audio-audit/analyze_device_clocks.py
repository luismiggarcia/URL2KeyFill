#!/usr/bin/env python3
"""Fit SDK hardware and consumed-queue clocks against their host observations."""
import argparse
import json
from pathlib import Path
import statistics
from analyze_identified_sync import records


def fit(points):
    if len(points) < 10 or points[-1][0]-points[0][0] < 60*48000:
        raise ValueError('Need at least 60 seconds of valid clock observations')
    x0, y0 = points[0]
    x = [a-x0 for a, b in points];y = [b-y0 for a, b in points]
    xm, ym = statistics.mean(x), statistics.mean(y)
    xx = sum((a-xm)**2 for a in x)
    slope = sum((a-xm)*(b-ym) for a, b in zip(x,y))/xx
    residual = [b-ym-slope*(a-xm) for a,b in zip(x,y)]
    return dict(points=len(points), span_s=(x[-1]-x[0])/48000,
                device_per_host_ppm=(slope-1)*1e6,
                host_per_device_ppm=(1/slope-1)*1e6,
                residual_rms_ms=(sum(e*e for e in residual)/len(residual))**.5/48,
                residual_max_ms=max(abs(e) for e in residual)/48,
                slope_standard_error_ppm=(sum(e*e for e in residual)/(len(residual)-2)/xx)**.5*1e6)


def analyse(log):
    summary = [r for r in records(log,'SYNC_TRACE') if r['kind']=='clock']
    rows = records(log,'SYNC_CLOCK')
    if len(summary)!=1 or int(summary[0]['omitted']) or int(summary[0]['stored'])!=len(rows):
        raise ValueError('Incomplete or truncated clock trace')
    queue, hardware, stream, applied, phase = [], [], [], [], []
    rejected_queue=rejected_hardware=0
    for r in rows:
        if int(r['stream'])<20*48000:
            continue
        qb,qa,hb,ha=[int(r[k]) for k in ('queue_before','queue_after','hardware_before','hardware_after')]
        if 0<=qa-qb<=48:
            queue.append(((qb+qa)/2,int(r['consumed'])))
        else:rejected_queue+=1
        if r['hardware_valid']=='1' and 0<=ha-hb<=48:
            hardware.append(((hb+ha)/2,int(r['hardware'])))
        else:rejected_hardware+=1
        if 0<=hb-qa<=48:
            stream.append(((qa+hb)/2,int(r['stream'])))
        applied.append((float(r['ratio'])-1)*1e6);phase.append(float(r['phase_error'])/48)
    return dict(consumed_queue=fit(queue),hardware=fit(hardware),scheduled_stream=fit(stream),
                applied_rate_ppm=dict(min=min(applied),median=statistics.median(applied),max=max(applied)),
                queue_phase_error_ms=dict(min=min(phase),median=statistics.median(phase),max=max(phase)),
                rejected_queue_observations=rejected_queue,rejected_hardware_observations=rejected_hardware,
                limits='Read-only SDK queries inside the owning diagnostic instance. Fits cover a finite window with correlated query noise; standard error is descriptive, not independent physical clock certification. Scheduled-stream host timing is bracketed approximately. These fits do not measure content A/V sync.')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('playback_log');a=p.parse_args()
    print(json.dumps(analyse(Path(a.playback_log).read_text()),indent=2))
