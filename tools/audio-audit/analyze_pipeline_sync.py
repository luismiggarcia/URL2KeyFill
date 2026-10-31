#!/usr/bin/env python3
"""Separate source flash/audio timing from scheduled and captured relative A/V.
Diagnostic snapshots exist only for --sdi-av-test. A stable calibrated audio origin
is required. Periodic nearest pairing assumes offsets smaller than 400 ms.
"""
import argparse
import bisect
import csv
import json
import statistics


def paired(first, second):
    second = sorted(set(second))
    used, values = set(), []
    for t in sorted(set(first)):
        i = bisect.bisect_left(second, t)
        choices = [j for j in (i-1, i) if 0 <= j < len(second) and j not in used]
        if choices:
            j = min(choices, key=lambda j: abs(second[j]-t))
            if abs(second[j]-t) < 19200:
                used.add(j)
                values.append((t-second[j])/48)
    if len(values) < 5:
        raise ValueError('Fewer than five matched diagnostic events')
    return dict(pairs=len(values), median_ms=statistics.median(values), min_ms=min(values), max_ms=max(values))


def analyse(rows, capture=None):
    rows = [r for r in rows if r['running'] == '1' and r.get('clock_calibrated') == '1'
            and float(r['elapsed_s']) >= 5 and r.get('source_pulse_pts')]
    if not rows or any(not r.get('audio_source_origin') for r in rows):
        raise ValueError('No calibrated source/output diagnostic snapshots')
    origins = {int(r['audio_source_origin']) for r in rows}
    if len(origins) != 1:
        raise ValueError('Recording spans multiple audio origins; analyse clock phases separately')
    origin = origins.pop()
    pulses = {int(r['source_pulse_pts']) for r in rows if int(r['source_pulse_pts'])-origin >= 7*48000}
    flashes = {int(r['source_flash_pts']) for r in rows if r.get('source_flash_pts') and int(r['source_flash_pts'])-origin >= 7*48000}
    scheduled = {int(r['scheduled_flash_pts']) for r in rows if r.get('scheduled_flash_pts') and int(r['scheduled_flash_pts']) >= 7*48000}
    mapped_pulses = {}
    for r in rows:
        pulse = int(r['source_pulse_pts'])
        if pulse not in pulses or pulse in mapped_pulses:
            continue
        if r.get('mapping_ratio'):
            ratio = float(r['mapping_ratio'])
            if not .9998 <= ratio <= 1.0002:
                raise ValueError('Invalid source/output rate')
            delay = max(0., float(r.get('sync_offset_ms', 0)))*48
            mapped_pulses[pulse] = float(r['mapping_stream'])+(pulse-float(r['mapping_source'])+delay)/ratio
        else:
            mapped_pulses[pulse] = pulse-origin
    result = dict(source_flash_minus_audio=paired(flashes, pulses),
                  scheduled_audio_minus_video=paired(set(mapped_pulses.values()), scheduled))
    if capture:
        measured = capture['channels']['audio_L']['median_ms']
        result['captured_audio_minus_video_ms'] = measured
        scheduled = result['scheduled_audio_minus_video']
        captured = capture['channels']['audio_L']
        # Snapshots and input capture cover different windows; do not subtract
        # medians when a run contains multiple discrete video phases.
        if scheduled['min_ms'] == scheduled['max_ms'] and captured.get('min_ms', measured) == captured.get('max_ms', measured):
            result['captured_minus_scheduled_ms'] = measured-scheduled['median_ms']
        else:
            result['captured_minus_scheduled_ms'] = None
        result['captured_minus_scheduled_range_ms'] = [captured.get('min_ms', measured)-scheduled['max_ms'], captured.get('max_ms', measured)-scheduled['min_ms']]
    result['limits'] = 'Adaptive inverse mapping uses the first telemetry snapshot of each pulse (not a per-sample output trace). Diagnostic source only; snapshot sampling can omit events. Input offset is included in capture, not independently corrected.'
    return result


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('application_csv')
    p.add_argument('--capture-result')
    args = p.parse_args()
    try:
        with open(args.application_csv) as f:
            rows = list(csv.DictReader(f))
        capture = json.load(open(args.capture_result)) if args.capture_result else None
        print(json.dumps(analyse(rows, capture), indent=2))
    except (ValueError, KeyError) as e:
        p.error(str(e))
