import subprocess,sys,json,datetime
from pathlib import Path
from zoneinfo import ZoneInfo
root=Path('/tmp/u2k-phase-long-series-20261101');root.mkdir(exist_ok=False)
for mode,seconds in [('1080p25',1200),('1080p50',600)]:
 now=datetime.datetime.now(ZoneInfo('Europe/Lisbon'));deadline=now.replace(hour=15,minute=0,second=0,microsecond=0)
 if (deadline-now).total_seconds()<seconds+225:raise RuntimeError('Insufficient time before 15:00 including final review reserve')
 case=root/mode
 command=[sys.executable,'-u','tools/audio-audit/run-endurance.py','--app','/tmp/u2k-phase-diagnostic/URL2KeyFill.app/Contents/MacOS/URL2KeyFill','--capture','/tmp/u2k-phase-capture','--directory',str(case),'--mode',mode,'--seconds',str(seconds),'--timing-profile','reliable','--sample-interval-seconds','120']
 print('START',mode,now.isoformat(),flush=True)
 with (root/(mode+'-runner.log')).open('w') as out: result=subprocess.run(command,stdout=out,stderr=subprocess.STDOUT)
 manifest=json.loads((case/'manifest.json').read_text())
 print('DONE',mode,'exit',result.returncode,'accepted',manifest.get('accepted'),'error',manifest.get('error'),flush=True)
 if manifest.get('error'):raise RuntimeError(manifest['error'])
print('SERIES COMPLETE',datetime.datetime.now(ZoneInfo('Europe/Lisbon')).isoformat(),flush=True)
