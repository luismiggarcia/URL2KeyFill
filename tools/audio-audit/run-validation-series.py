#!/usr/bin/env python3
"""Short serial hardware validation. Never starts the overnight endurance run."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from analyze_identified_sync import analyse


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cases', nargs='+', help='Run only named cases; default is the complete short series')
    p.add_argument('--app',required=True)
    p.add_argument('--capture',required=True)
    p.add_argument('--directory',required=True)
    args=p.parse_args()
    root=Path(args.directory).resolve();root.mkdir(parents=True,exist_ok=False)
    cases=[('reliable25-a','1080p25','reliable','content',0),('reliable25-b','1080p25','reliable','content',0),
           ('reliable50','1080p50','reliable','content',0),('arrival50','1080p50','reliable','arrival',0),
           ('audio-delay20','1080p50','reliable','arrival',20),('video-delay20','1080p50','reliable','arrival',-20),
           ('low25-a','1080p25','low','content',0),('low25-b','1080p25','low','content',0),
           ('low50-a','1080p50','low','content',0),('low50-b','1080p50','low','content',0)]
    if args.cases:
        unknown=set(args.cases)-{c[0] for c in cases}
        if unknown:p.error('Unknown cases: '+str(unknown))
        cases=[c for c in cases if c[0] in args.cases]
    summary=dict(app_sha256=hashlib.sha256(Path(args.app).read_bytes()).hexdigest(),capture_sha256=hashlib.sha256(Path(args.capture).read_bytes()).hexdigest(),cases={})
    for name,mode,profile,selection,offset in cases:
        print('START '+name,flush=True)
        command=[sys.executable,str(Path(__file__).with_name('run-content-reference.py')),'--app',args.app,'--capture',args.capture,'--mode',mode,'--selection',selection,'--timing-profile',profile,'--sync-offset-ms',str(offset),'--directory',str(root/name)]
        with (root/(name+'.runner.log')).open('w') as log:
            subprocess.run(command,stdout=log,stderr=log,check=True)
        run=root/name/'loopback'
        result=analyse((run/'playback.log').read_text(),list(csv.DictReader((run/'application.csv').open())),json.loads((run/'result.json').read_text()),selection,30,list(csv.DictReader((run/'capture.csv').open())))
        (root/name/'identified.json').write_text(json.dumps(result,indent=2)+'\n')
        summary['cases'][name]=result;(root/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
        print(f"PASS {name}: A-V {result['captured_audio_minus_video_ms']:.3f} ms; paint-to-input {result['paint_to_input_callback']['median_ms']:.3f} ms",flush=True)
    if args.cases:return
    print('START fractional5994',flush=True)
    command=[sys.executable,str(Path(__file__).with_name('run-loopback.py')),'--app',args.app,'--capture',args.capture,'--mode','1080p5994','--directory',str(root/'fractional5994')]
    with (root/'fractional5994.runner.log').open('w') as log:subprocess.run(command,stdout=log,stderr=log,check=True)
    print('PASS fractional5994',flush=True)


if __name__=='__main__':main()
