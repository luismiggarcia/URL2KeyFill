#!/usr/bin/env python3
"""Embed the standalone controls in the restricted, single-file native UI."""
from pathlib import Path
import argparse

ROOT = Path(__file__).resolve().parents[1]
START = '<!-- BEGIN GENERATED AUDIO CONTROLS -->'
END = '<!-- END GENERATED AUDIO CONTROLS -->'

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    target = ROOT / 'assets/interface.html'
    html = target.read_text()
    sources = [ROOT / 'components' / name / (name + '.js') for name in ('gain-slider', 'audio-level-meter')]
    body = '\n'.join(p.read_text().replace('export class ', 'class ', 1) for p in sources)
    block = START + '\n<script>\n' + body + '</script>\n' + END
    if START in html:
        before, rest = html.split(START, 1)
        _, after = rest.split(END, 1)
        updated = before + block + after
    else:
        updated = html.replace('<script>', block + '\n<script>', 1)
    if args.check:
        if updated != html:
            raise SystemExit('Embedded audio controls are stale. Run tools/sync-ui-controls.py.')
        print('Embedded audio controls match components/.')
    else:
        target.write_text(updated)

if __name__ == '__main__':
    main()
