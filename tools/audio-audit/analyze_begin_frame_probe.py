#!/usr/bin/env python3
"""Attribute each CEF paint to the BeginFrame that produced it (begin-frame-probe.html).

The probe page encodes its requestAnimationFrame time (0.1 ms units, mod 300 ms) in the
reference barcode; the app logs BEGINFRAME_PAINT with arrival and the last two BeginFrame
send times (48 kHz host samples). The constant offset between rAF time and the producing
BeginFrame is taken as the mode against the previous BeginFrame; each paint's lag is then
0 (latest BeginFrame, the one the prototype stamps) or 1 (the one before).

Usage: analyze_begin_frame_probe.py playback.log [--json]
"""
import argparse, collections, json, re


def analyse(lines):
    rows = [{k: int(v) for k, v in re.findall(r'(\w+)=(-?\d+)', l)} for l in lines if l.startswith('BEGINFRAME_PAINT')]
    rows = [r for r in rows if r['id'] >= 0 and r['previous'] > 0]
    if len(rows) < 100:
        raise ValueError('Too few decoded probe paints')
    off = lambda s, i: (s / 4.8 - i) % 3000
    mode = collections.Counter(round(off(r['previous'], r['id'])) for r in rows).most_common(1)[0][0]
    t0, lags, latency, series = rows[0]['arrival'], collections.Counter(), [], []
    for r in rows:
        d = ((off(r['previous'], r['id']) - mode + 1500) % 3000) - 1500
        content = r['previous'] - d * 4.8
        lag = 0 if abs(content - r['latest']) < 4 * 48 else 1 if abs(content - r['previous']) < 4 * 48 else -1
        lags[lag] += 1
        latency.append((r['arrival'] - content) / 48)
        series.append(((r['arrival'] - t0) / 48000, lag))
    q = lambda v, p: sorted(v)[int(p * (len(v) - 1))]
    switches = sum(1 for (_, a), (_, b) in zip(series, series[1:]) if a != b)
    return dict(paints=len(rows), lag_latest=lags[0], lag_previous=lags[1], unresolved=lags[-1], lag_switches=switches,
                first_lag_change_s=next((t for (t, a), (_, b) in zip(series, series[1:]) if a != b), None),
                latency_ms=dict(p1=q(latency, .01), p50=q(latency, .5), p99=q(latency, .99), max=max(latency)),
                note='lag 0 = content of the latest BeginFrame (prototype stamp correct); lag 1 = previous BeginFrame')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('log')
    a = p.parse_args()
    print(json.dumps(analyse(open(a.log)), indent=2))
