#!/usr/bin/env python3
"""Summarise per-second application telemetry, including gaps hidden by headroom.

Usage: analyze_headroom.py trace.csv [--warmup-seconds 5]
The slope is descriptive, not a direct measurement of the crystal or an acceptance
of physical A/V sync. Empty headroom fields mean no retained source sample.
"""
import argparse
import collections
import csv
import json


def summarise(rows, warmup):
    groups = collections.defaultdict(list)
    for row in rows:
        if row['running'] == '1' and row['on_air'] == '1' and int(row['reads']):
            groups[row['layer']].append(row)
    result = {}
    for layer, data in groups.items():
        first = float(data[0]['elapsed_s'])
        valid = [r for r in data if r['headroom_mean_ms'] and float(r['elapsed_s']) >= first + warmup]
        slope = None
        if len(valid) >= 2:
            # Subtract origin before fitting; use one reporting interval per point.
            x = [float(r['elapsed_s']) - first for r in valid]
            y = [float(r['headroom_mean_ms']) for r in valid]
            mx, my = sum(x)/len(x), sum(y)/len(y)
            den = sum((v-mx)**2 for v in x)
            if den:
                slope = sum((a-mx)*(b-my) for a, b in zip(x, y))/den*3600
        result[layer] = dict(
            reported_span_s=float(data[-1]['elapsed_s'])-first,
            intervals=len(data),
            missing_frames=sum(int(r['missing_frames']) for r in data),
            no_source_reads=sum(int(r['no_source_reads']) for r in data),
            headroom_min_ms=min((float(r['headroom_min_ms']) for r in data if r['headroom_min_ms']), default=None),
            headroom_max_ms=max((float(r['headroom_max_ms']) for r in data if r['headroom_max_ms']), default=None),
            filtered_trend_ms_per_hour=slope,
            api_errors=max(int(r['api_errors']) for r in data),
            recoveries=max(int(r['recoveries']) for r in data),
            late_video=max(int(r['late_video']) for r in data),
            dropped_video=max(int(r['dropped_video']) for r in data))
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace')
    parser.add_argument('--warmup-seconds', type=float, default=5)
    args = parser.parse_args()
    with open(args.trace) as stream:
        print(json.dumps(summarise(csv.DictReader(stream), args.warmup_seconds), indent=2))
