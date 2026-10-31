#!/usr/bin/env python3
"""Validate the controlled frame/sample-ID timing proof, not arbitrary web content."""
import argparse
import csv
import json
from pathlib import Path
import statistics

MISSING = -(1 << 63)


def stats(values):
    if not values:
        raise ValueError('No diagnostic observations')
    return dict(count=len(values), min_ms=min(values)/48, median_ms=statistics.median(values)/48, max_ms=max(values)/48)


def records(log, prefix):
    return [dict(pair.split('=', 1) for pair in line.split()[1:])
            for line in log.splitlines() if line.startswith(prefix+' ')]


def analyse(log, rows, captured, selection, seconds=30):
    paint = records(log, 'REFERENCE_PAINT')
    valid = [r for r in paint if int(r['audio_origin']) != MISSING]
    origins = {int(r['audio_origin']) for r in valid}
    if len(origins) != 1 or any(int(r['clock_mismatches']) for r in valid):
        raise ValueError('No single verified audio-content origin')
    origin = origins.pop()
    if 'REFERENCE_INVALID' in log or 'FAIL ' in log or '"error":' in log:
        raise ValueError('Reference playback contains an error or unidentified paint')
    if max(int(r['clock_matches']) for r in valid) < 10:
        raise ValueError('Too few verified audio markers')
    for r in rows:
        if r.get('source_pulse_pts'):
            relative = int(r['source_pulse_pts'])-origin
            error = relative-round(relative/48000)*48000-5
            if abs(error) > 1:
                raise ValueError('Observed audio pulse PTS disagrees with the identified content clock')
    schedules = records(log, 'REFERENCE_SCHEDULE')
    selected, flash_offsets, output_origins, streams = [], [], set(), []
    was_white = False
    for r in schedules:
        stream, identity, output_origin, paint_time, content_time = [int(r[k]) for k in
            ('stream_samples', 'id', 'audio_output_origin', 'paint_samples', 'content_time')]
        white = identity >= 50 and identity % 50 < 2
        in_window = 7*48000 <= stream < (seconds-1)*48000
        if in_window:
            output_origins.add(output_origin)
            streams.append(stream)
            actual_content = origin+identity*960
            if selection == 'content' and content_time != actual_content:
                raise ValueError('Scheduled image is not stamped with its identified content PTS')
            if selection == 'arrival' and content_time != MISSING:
                raise ValueError('Arrival control unexpectedly uses content PTS')
            selected.append(actual_content-(output_origin+stream))
            if white and not was_white:
                flash_offsets.append(origin+(identity//50)*48000+5-output_origin-stream)
        was_white = white
    if len(streams) < 2:
        raise ValueError('Missing scheduled content observations')
    step = streams[1]-streams[0]
    if step not in (960, 1920) or streams != list(range(7*48000, (seconds-1)*48000, step)):
        raise ValueError('Incomplete or non-contiguous identified schedule coverage')
    if len(output_origins) != 1:
        raise ValueError('Output audio origin changed inside the selection window')
    if len(flash_offsets) < 5:
        raise ValueError('Too few identified scheduled flashes')
    health_fields = ['missing_total', 'api_errors', 'recoveries', 'late_video', 'dropped_video',
                     'clock_resets', 'video_history_misses', 'video_history_startup_misses', 'video_conversion_fallbacks']
    health = {key: max(float(r[key]) for r in rows) for key in health_fields}
    if any(health.values()):
        raise ValueError('I/O health checks failed: '+str(health))
    source_flash_offsets = []
    was_white = False
    for r in paint:
        identity = int(r['id'])
        white = identity >= 50 and identity % 50 < 2
        if identity >= 350 and white and not was_white:
            source_flash_offsets.append((identity % 50)*960)
        was_white = white
    observed = captured['channels']['audio_L']
    predicted = stats(flash_offsets)
    return dict(selection=selection, audio_content_origin=origin, health=health,
                verified_audio_markers=max(int(r['clock_matches']) for r in valid),
                skipped_audio_marker_ids=max(int(r.get('clock_skipped', 0)) for r in valid),
                source_paint_minus_content=stats([int(r['host_samples'])-origin-int(r['id'])*960 for r in valid if int(r['id'])>=350]),
                received_flash_frame_after_content_onset=stats(source_flash_offsets),
                selected_content_minus_target=stats(selected), scheduled_audio_minus_video=predicted,
                captured_audio_minus_video_ms=observed['median_ms'], capture_pairs=observed['pairs'],
                captured_minus_scheduled_range_ms=[observed['min_ms']-predicted['max_ms'], observed['max_ms']-predicted['min_ms']],
                limits='Controlled watermarked VP8/PCM reference only; no independent receiver calibration or drift acceptance. Selection windows and capture windows may differ.')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory', help='run-content-reference output directory')
    p.add_argument('--selection', choices=['arrival', 'content'], required=True)
    p.add_argument('--seconds', type=int, default=30)
    args = p.parse_args()
    root = Path(args.directory)/'loopback'
    result = analyse((root/'playback.log').read_text(), list(csv.DictReader((root/'application.csv').open())),
                     json.loads((root/'result.json').read_text()), args.selection, args.seconds)
    print(json.dumps(result, indent=2))
