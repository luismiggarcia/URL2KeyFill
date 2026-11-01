#!/usr/bin/env python3
"""Compare accepted PCM onsets with selected video events, off the output thread."""
import argparse
import bisect
import csv
import json
import math
from pathlib import Path
import statistics
from analyze_identified_sync import records
from analyze_loopback import analyse as captured


def stats(values):
    if not values:
        raise ValueError('No matched event values')
    return dict(count=len(values), min_ms=min(values), median_ms=statistics.median(values), max_ms=max(values))


def analyse(log, capture_rows=None, periodic_host_matching=False):
    if 'FAIL ' in log:
        raise ValueError('Playback contains a failure')
    summaries = records(log, 'SYNC_TRACE')
    for kind in ('audio', 'video'):
        summary = [r for r in summaries if r['kind'] == kind]
        if len(summary) != 1 or int(summary[0]['omitted']):
            raise ValueError('Missing, repeated or truncated '+kind+' event trace')
    audio = records(log, 'SYNC_AUDIO')
    video = records(log, 'SYNC_VIDEO')
    for kind, events in [('audio', audio), ('video', video)]:
        count = next(int(r['stored']) for r in summaries if r['kind'] == kind)
        stamps = [int(r['stream_samples']) for r in events]
        if len(events) != count or any(b <= a for a, b in zip(stamps, stamps[1:])):
            raise ValueError('Incomplete or non-monotonic '+kind+' event trace')
        for event in events:
            ratio = float(event['ratio'])
            mapped = float(event['mapping_source'])+(int(event['stream_samples'])-int(event['mapping_stream']))*ratio
            delay = int(event['delay']) if kind == 'audio' else max(0, -int(event['adjustment']))
            observed = float(event['source_samples' if kind == 'audio' else 'source_target'])
            if not .9998 <= ratio <= 1.0002 or not math.isfinite(mapped) or not math.isfinite(observed) or abs(mapped-delay-observed) > .001:
                raise ValueError('Inconsistent '+kind+' affine mapping')
            if kind == 'video' and 'selection_samples' in event:
                if int(event['selection_samples'])-observed > int(event['max_ahead'])+.501:
                    raise ValueError('Selected image exceeds the future-frame tolerance')
    audio = [r for r in audio if r['transition'] == '0']
    stamps = [int(r['stream_samples']) for r in audio]
    used, events = set(), []
    for v in video:
        t = int(v['stream_samples'])
        if t < 7*48000:
            continue
        i = bisect.bisect_left(stamps, t)
        candidates = [j for j in (i-1, i) if 0 <= j < len(stamps) and j not in used]
        if not candidates:
            continue
        j = min(candidates, key=lambda j: abs(stamps[j]-t))
        if abs(stamps[j]-t) >= 19200:
            continue
        used.add(j)
        a = audio[j]
        events.append(dict(video_stream=t, audio_stream=stamps[j], identity=int(v['id']), sequence=int(v['sequence']),
                           video_host_observed=int(v['host_samples']) if 'host_samples' in v else None,
                           scheduled_av_ms=(stamps[j]-t)/48,
                           target_minus_paint_ms=(float(v['source_target'])-int(v['paint_samples']))/48,
                           target_minus_selection_ms=(float(v['source_target'])-int(v['selection_samples']))/48 if 'selection_samples' in v else None,
                           audio_source_minus_paint_ms=(float(a['source_samples'])-int(v['paint_samples']))/48,
                           ratio=float(a['ratio'])))
    if len(events) < 5:
        raise ValueError('Fewer than five steady accepted-PCM/video matches')
    result = dict(scheduled_av=stats([e['scheduled_av_ms'] for e in events]),
                  target_minus_paint=stats([e['target_minus_paint_ms'] for e in events]), events=events,
                  limits='Accepted output PCM, not physical emission. Transition pulses excluded. Periodic nearest pairing assumes less than 400 ms offset. Input A/V offset remains included; no absolute latency or arbitrary URL guarantee.')
    if capture_rows:
        physical = captured(capture_rows)
        result['physical_av'] = {k: {q: v[q] for q in ('pairs', 'min_ms', 'median_ms', 'max_ms')} for k, v in physical['channels'].items()}
        if physical['video_timestamp_gaps_within_matched_range']:
            raise ValueError('Physical capture has timestamp gaps')
        ids = {int(r['pts_48k']): int(float(r['value'])) for r in capture_rows if r['kind'] == 'video_id'}
        if not ids and any(e['identity'] >= 0 for e in events):
            raise ValueError('Identified schedule requires physical frame IDs; rebuild the capture probe')
        if ids and any(e['identity'] < 0 for e in events):
            raise ValueError('Physical IDs present without identified schedule')
        if ids:
            schedule = {e['identity']: e for e in events}
            if len(schedule) != len(events):
                raise ValueError('Repeated reference ID in schedule; analyse one content epoch')
            matched = []
            for event in physical['channels']['audio_L']['events']:
                identity = ids.get(event['video_pts_48k'])
                if identity not in schedule:
                    # Warmup windows differ; never match a different frame ID.
                    continue
                s = schedule[identity]
                matched.append(dict(identity=identity, scheduled_av_ms=s['scheduled_av_ms'],
                                    physical_av_ms=event['audio_minus_video_ms'],
                                    capture_minus_scheduled_ms=event['audio_minus_video_ms']-s['scheduled_av_ms']))
            if len(matched) < 5:
                raise ValueError('Fewer than five exact physical frame-ID matches')
            result['physical_id_matches'] = matched
            result['capture_minus_scheduled'] = stats([e['capture_minus_scheduled_ms'] for e in matched])
        else:
            result['physical_id_matches'] = None
            if periodic_host_matching:
                hosts = {int(r['pts_48k']): int(r['host_48k']) for r in capture_rows if r['kind'] == 'video'}
                matches, used = [], set()
                for e in physical['channels']['audio_L']['events']:
                    host = hosts[e['video_pts_48k']]
                    candidates = [s for s in events if s['video_host_observed'] is not None
                                  and 0 <= host-s['video_host_observed'] < 19200
                                  and s['video_stream'] not in used]
                    if len(candidates) != 1:
                        continue
                    s = candidates[0];used.add(s['video_stream'])
                    matches.append(dict(video_stream=s['video_stream'],
                                        input_epoch_samples=e['video_pts_48k']-s['video_stream'],
                                        observation_gap_ms=(host-s['video_host_observed'])/48,
                                        scheduled_av_ms=s['scheduled_av_ms'], physical_av_ms=e['audio_minus_video_ms'],
                                        capture_minus_scheduled_ms=e['audio_minus_video_ms']-s['scheduled_av_ms']))
                if len(matches) < 5:
                    raise ValueError('Insufficient unambiguous periodic/host matches')
                if len({e['input_epoch_samples'] for e in matches}) != 1:
                    raise ValueError('Periodic matches disagree on input/output video epoch')
                result['periodic_host_matches'] = matches
                result['capture_minus_scheduled'] = stats([e['capture_minus_scheduled_ms'] for e in matches])
                result['periodic_pairing_limit'] = 'No content IDs: assumes schedule-to-input observation delay in [0,400) ms and verifies a constant video timestamp epoch per capture. Not equivalent to identified content matching.'
    return result


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory', help='Directory containing playback.log and capture.csv')
    p.add_argument('--periodic-host-matching', action='store_true', help='Explicit 0..400 ms observation assumption for generic diagnostic pages')
    args = p.parse_args()
    root = Path(args.directory)
    print(json.dumps(analyse((root/'playback.log').read_text(), list(csv.DictReader((root/'capture.csv').open())), args.periodic_host_matching), indent=2))
