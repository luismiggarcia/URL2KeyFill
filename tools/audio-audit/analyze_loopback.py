#!/usr/bin/env python3
"""Measure captured audio-minus-video onset using a common 48 kHz input timeline.
Positive means audio follows video. This is relative A/V through output + input,
not absolute end-to-end latency. No implicit correction for the input device.
"""
import argparse
import bisect
import csv
import json
import statistics


def analyse(rows, warmup=3):
    video = sorted((int(r['pts_48k']), int(r['duration_48k']), float(r['value']))
                   for r in rows if r['kind'] == 'video')
    if len(video) < 10:
        raise ValueError('Insufficient valid video: check SDI Fill cable, mode and duplex')
    duration = statistics.median(d for _, d, _ in video)
    if duration <= 0 or any(t2 <= t1 for (t1, _, _), (t2, _, _) in zip(video, video[1:])):
        raise ValueError('Invalid video timestamps')
    origin = video[0][0]
    flashes, previous, previous_pts = [], None, None
    gaps = 0
    for pts, d, luma in video:
        white = luma >= 200
        contiguous = previous_pts is not None and abs(pts-previous_pts-duration) <= 2
        if previous_pts is not None and not contiguous:
            gaps += 1
        if white and previous is False and contiguous and pts >= origin+warmup*48000:
            flashes.append(pts)
        previous, previous_pts = white, pts
    if len(flashes) < 5:
        raise ValueError('Need at least five clean flash onsets after warmup; check Fill versus Key')
    channels = {}
    for channel in ('audio_L', 'audio_R'):
        audio = sorted(int(r['pts_48k']) for r in rows if r['kind'] == channel)
        offsets, pairs, used = [], [], set()
        for flash in flashes:
            index = bisect.bisect_left(audio, flash)
            candidates = [i for i in (index-1, index) if 0 <= i < len(audio) and i not in used]
            if not candidates:
                continue
            match = min(candidates, key=lambda i: abs(audio[i]-flash))
            if abs(audio[match]-flash) > 48000*.4:
                continue
            used.add(match)
            delta = (audio[match]-flash)/48
            offsets.append(delta)
            pairs.append(dict(video_pts_48k=flash, audio_pts_48k=audio[match], audio_minus_video_ms=delta))
        if len(offsets) < 5:
            raise ValueError(f'Insufficient matched pulses for {channel}')
        channels[channel] = dict(pairs=len(offsets), unmatched_flashes=len(flashes)-len(offsets), median_ms=statistics.median(offsets),
            min_ms=min(offsets), max_ms=max(offsets), stdev_ms=statistics.pstdev(offsets), events=pairs)
    matched_pts = [e['video_pts_48k'] for c in channels.values() for e in c['events']]
    first_match, last_match = min(matched_pts), max(matched_pts)
    matched_gaps = sum(abs(t2-t1-duration) > 2 for (t1, _, _), (t2, _, _) in zip(video, video[1:])
                       if t1 >= first_match and t2 <= last_match)
    return dict(sign='positive = audio later than video', measurement='output plus input relative A/V; uncalibrated input',
        video_frame_ms=duration/48, valid_video_frames=len(video), video_timestamp_gaps=gaps, video_timestamp_gaps_within_matched_range=matched_gaps,
        flashes_after_warmup=len(flashes), channels=channels,
        limitations=['Periodic 1 Hz pattern: nearest pairing assumes offset within 400 ms; larger offsets are ambiguous.',
            'Browser visual onset is frame-quantised; this does not establish sub-frame source synchrony.',
            'Input A/V offset requires a known scheduled reference; no input correction applied.',
            'Shared-device loopback does not test drift between independent devices.'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv')
    parser.add_argument('--warmup-seconds', type=float, default=3)
    args = parser.parse_args()
    try:
        with open(args.csv) as stream:
            result = analyse(list(csv.DictReader(stream)), args.warmup_seconds)
        print(json.dumps(result, indent=2))
    except (ValueError, KeyError) as error:
        parser.error(str(error))
