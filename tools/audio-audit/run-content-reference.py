#!/usr/bin/env python3
"""Serve the offline known-PTS reference on loopback and preserve its exact media."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import subprocess
import sys
import threading


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source-fps', type=int, choices=[25,50], help='Identified source cadence; defaults to the SDI mode')
    p.add_argument('--sync-change-ms', type=int)
    p.add_argument('--timing-profile', choices=['reliable','low'], default='reliable')
    p.add_argument('--sync-offset-ms', type=int, default=0)
    p.add_argument('--selection', choices=['arrival', 'content'], help='Use the identified reference, selecting arrival or experimental content PTS')
    p.add_argument('--app', required=True)
    p.add_argument('--cef-rate-multiplier', type=int, choices=[1,2], default=1)
    p.add_argument('--raw-av', action='store_true', help='Measure without the calibrated per-mode default')
    p.add_argument('--capture', required=True)
    p.add_argument('--mode', choices=['1080p25', '1080p50', '1080p5994'], required=True)
    p.add_argument('--seconds', type=int, default=30)
    p.add_argument('--directory', required=True)
    args = p.parse_args()
    if not 25 <= args.seconds <= 45:
        p.error('--seconds must be 25..45 for the finite 60-second reference')
    root = Path(args.directory).resolve()
    root.mkdir(parents=True, exist_ok=False)
    source_fps = args.source_fps or (25 if args.mode == '1080p25' else 50)
    page = Path(__file__).with_name('content-pts-reference.html' if args.selection else 'content-reference.html').read_bytes()
    if args.selection:
        page = page.replace(b'const fps=50,', f'const fps={source_fps},'.encode())

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path != '/':
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(page)))
            self.end_headers()
            self.wfile.write(page)

        def do_POST(self):
            if self.path not in ('/reference.mkv', '/source.json'):
                self.send_error(404)
                return
            size = int(self.headers.get('Content-Length', '0'))
            if not 0 < size <= 64*1024*1024 or (root/self.path[1:]).exists():
                self.send_error(400)
                return
            data = self.rfile.read(size)
            if len(data) != size:
                self.send_error(400)
                return
            (root/self.path[1:]).write_bytes(data)
            self.send_response(201)
            self.send_header('Content-Length', '0')
            self.end_headers()

        def log_message(self, *args):
            pass

    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        command = [sys.executable, str(Path(__file__).with_name('run-loopback.py')),
                   '--app', args.app, '--capture', args.capture, '--mode', args.mode,
                   '--seconds', str(args.seconds), '--directory', str(root/'loopback'),
                   '--source-url', f'http://127.0.0.1:{server.server_port}/',
                   '--timing-profile', args.timing_profile, '--sync-offset-ms', str(args.sync_offset_ms)]
        if args.sync_change_ms is not None:
            command += ['--sync-change-ms',str(args.sync_change_ms)]
        if args.raw_av:
            command += ['--raw-av']
        if args.cef_rate_multiplier!=1:
            command += ['--cef-rate-multiplier', str(args.cef_rate_multiplier)]
        if args.selection:
            command += ['--reference-ids']
        if args.selection == 'content':
            command += ['--content-pts']
        (root/'command.json').write_text(json.dumps(command, indent=2)+'\n')
        result = subprocess.call(command)
        if (root/'reference.mkv').exists():
            from verify_content_reference import verify
            verified = verify(root/'reference.mkv', identified=bool(args.selection))
            (root/'verified-source.json').write_text(json.dumps(verified, indent=2)+'\n')
            source = json.loads((root/'source.json').read_text())
            if source['media_sha256'] != verified['sha256'] or source['source_decoded_images_verified'] != (60*source_fps if args.selection else 2):
                raise ValueError('Source manifest disagrees with the independent verification')
        elif result == 0:
            raise ValueError('Missing source evidence')
        return result
    finally:
        server.shutdown()
        server.server_close()
        worker.join()


if __name__ == '__main__':
    raise SystemExit(main())
