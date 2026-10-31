#!/usr/bin/env python3
"""Run an SDI capture beside an isolated app test or a scheduled SDK reference.
Build decklink_capture and decklink_clock first. Uses only child processes it owns.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import threading
from analyze_loopback import analyse


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--capture', required=True)
    source = p.add_mutually_exclusive_group(required=True)
    source.add_argument('--app', help='Path to app Contents/MacOS/URL2KeyFill')
    source.add_argument('--reference', help='Path to decklink_clock with --av-reference support')
    p.add_argument('--sync-change-ms', type=int, help='Apply an isolated live adjustment at 15 s')
    p.add_argument('--timing-profile', choices=['reliable','low'], default='reliable')
    p.add_argument('--sync-offset-ms', type=int, default=0)
    p.add_argument('--reference-ids', action='store_true', help='Decode the controlled reference frame/sample IDs')
    p.add_argument('--content-pts', action='store_true', help='Select by identified content PTS; requires --reference-ids')
    p.add_argument('--source-url', help='Diagnostic source override, only with --app')
    p.add_argument('--mode', choices=['1080p25', '1080p50', '1080p5994'], default='1080p25')
    p.add_argument('--seconds', type=int, default=30)
    p.add_argument('--settle-seconds', type=float, default=4, help='Start playback first and settle 1..10 seconds before locked capture')
    p.add_argument('--directory', required=True, help='New output directory; never overwritten')
    args = p.parse_args()
    if not 1 <= args.settle_seconds <= 10:
        p.error('--settle-seconds must be 1..10 with the locked input probe')
    if not 15 <= args.seconds <= 240:
        p.error('--seconds must be 15..240')
    if args.source_url and not args.app:
        p.error('--source-url requires --app')
    if (args.reference_ids and not args.source_url) or (args.content_pts and not args.reference_ids):
        p.error('--reference-ids requires --source-url; --content-pts requires --reference-ids')
    root = Path(args.directory).resolve()
    root.mkdir(parents=True, exist_ok=False)
    capture = str(Path(args.capture).resolve())
    playback = str(Path(args.app or args.reference).resolve())
    cap_cmd = [capture, '--seconds', str(args.seconds+10), '--mode', args.mode, '--csv', str(root/'capture.csv')]
    play_cmd = ([playback, '--sdi-av-test', '--hardware-test-mode', args.mode, '--audio-baseline-seconds', str(args.seconds)] if args.app else
                [playback, '--av-reference', '1', '--mode', args.mode, '--seconds', str(args.seconds), '--csv', str(root/'clock.csv')])
    if args.source_url:
        play_cmd += ['--sdi-av-source-url', args.source_url]
    if args.reference_ids:
        play_cmd += ['--sdi-av-reference-ids']
    if args.content_pts:
        play_cmd += ['--sdi-av-content-pts']
    if args.app:
        play_cmd += ['--timing-profile', args.timing_profile, '--sync-offset-ms', str(args.sync_offset_ms)]
    if args.sync_change_ms is not None:
        if not args.app:p.error('--sync-change-ms requires --app')
        play_cmd += ['--sync-change-ms',str(args.sync_change_ms)]
    metadata = dict(mode=args.mode, seconds=args.seconds, settle_seconds=args.settle_seconds, source='application' if args.app else 'scheduled SDK reference',
                    playback_sha256=digest(playback), capture_sha256=digest(capture), capture_command=cap_cmd, playback_command=play_cmd)
    (root/'manifest.json').write_text(json.dumps(metadata, indent=2)+'\n')
    children = []
    stop_timer = None
    try:
        with (root/'capture.log').open('w') as cl, (root/'playback.log').open('w') as pl:
            env = os.environ.copy()
            env['TMPDIR'] = str(root)
            env['U2K_AUDIO_TRACE'] = str(root/'application.csv')
            play = None
            if args.settle_seconds:
                play = subprocess.Popen(play_cmd, stdout=pl, stderr=pl, env=env)
                children.append(play)
                marker = 'START SDI loopback source:' if args.app else 'READY SDK reference output'
                deadline = time.monotonic()+25
                while marker not in (root/'playback.log').read_text():
                    if play.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError('Playback did not become ready; inspect playback.log')
                    time.sleep(.05)
                play_ready = time.monotonic()
                time.sleep(args.settle_seconds)
                if play.poll() is not None:
                    raise RuntimeError('Playback stopped before input start; inspect playback.log')
            cap = subprocess.Popen(cap_cmd, stdout=cl, stderr=cl)
            children.append(cap)
            deadline = time.monotonic()+8
            while 'READY SDI capture' not in (root/'capture.log').read_text():
                if cap.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError('Capture did not become ready; inspect capture.log')
                time.sleep(.05)
            if play is None:
                play = subprocess.Popen(play_cmd, stdout=pl, stderr=pl, env=env)
                children.append(play)
            # End input measurement before playback disables/reconfigures SDI.
            # No-signal timestamps during receiver reacquisition are not media times.
            remaining = play_ready+args.seconds-1-time.monotonic()
            if remaining < 8:
                raise RuntimeError('Too little locked recording time; use --seconds 25 or longer')
            metadata['input_recording_limit_s'] = remaining
            stop_timer = threading.Timer(remaining, lambda: cap.terminate() if cap.poll() is None else None)
            stop_timer.daemon = True
            stop_timer.start()
            metadata['playback_exit'] = play.wait(timeout=args.seconds+50)
            metadata['capture_exit'] = cap.wait(timeout=25)
        if metadata['playback_exit'] or metadata['capture_exit']:
            raise RuntimeError('Playback or capture failed; inspect logs')
        import csv
        try:
            with (root/'capture.csv').open() as f:
                result = analyse(list(csv.DictReader(f)))
        except ValueError as error:
            metadata['analysis_error'] = str(error)
            raise RuntimeError(str(error)) from error
        (root/'result.json').write_text(json.dumps(result, indent=2)+'\n')
        for channel, data in result['channels'].items():
            print(f"{channel}: audio - video = {data['median_ms']:.3f} ms, {data['pairs']} pairs")
    finally:
        if stop_timer is not None:
            stop_timer.cancel()
        for child in children:
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()
        (root/'manifest.json').write_text(json.dumps(metadata, indent=2)+'\n')


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as error:
        print(f'FAIL {error}', file=sys.stderr)
        raise SystemExit(1)
