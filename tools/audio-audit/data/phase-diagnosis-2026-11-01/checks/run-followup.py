import subprocess,sys,json,datetime,time,csv
from pathlib import Path
from zoneinfo import ZoneInfo
sys.path.insert(0,'tools/audio-audit')
from analyze_event_sync import analyse as events
from analyze_identified_sync import analyse as identified
from analyze_device_clocks import analyse as clocks
root=Path('/tmp/u2k-phase-followup-20261101');root.mkdir(exist_ok=False)
prior=Path('/tmp/u2k-phase-long-series-20261101/1080p50/manifest.json')
def remaining():
 now=datetime.datetime.now(ZoneInfo('Europe/Lisbon'));return (now.replace(hour=15,minute=0,second=0,microsecond=0)-now).total_seconds()
while not prior.exists():
 if remaining()<480:raise RuntimeError('Insufficient time before 15:00')
 time.sleep(1)
manifest=json.loads(prior.read_text())
if manifest.get('error'):raise RuntimeError('Prior physical run failed: '+manifest['error'])
if remaining()<480:raise RuntimeError('Insufficient time before 15:00')
case=root/'clocks-25';app='/tmp/u2k-phase-clock-diagnostic/URL2KeyFill.app/Contents/MacOS/URL2KeyFill'
command=[sys.executable,'-u','tools/audio-audit/run-endurance.py','--app',app,'--capture','/tmp/u2k-phase-capture','--directory',str(case),'--mode','1080p25','--seconds','240','--sample-interval-seconds','120']
print('START clocks',datetime.datetime.now().astimezone().isoformat(),flush=True)
with (root/'clocks-runner.log').open('w') as out:result=subprocess.run(command,stdout=out,stderr=subprocess.STDOUT)
manifest=json.loads((case/'manifest.json').read_text());print('DONE clocks exit',result.returncode,'accepted',manifest.get('accepted'),'error',manifest.get('error'),flush=True)
if manifest.get('error'):raise RuntimeError(manifest['error'])
try:
 d=clocks((case/'playback.log').read_text());(case/'clock-analysis.json').write_text(json.dumps(d,indent=2)+'\n');print('CLOCKS',json.dumps(d),flush=True)
except ValueError as error:
 (case/'clock-analysis-error.json').write_text(json.dumps({'error':str(error)})+'\n');print('CLOCK ANALYSIS FAILED',str(error),flush=True)
for mode,source in [('1080p50',25),('1080p25',50)]:
 if remaining()<250:
  print('SKIP mixed to retain review time',mode,source,flush=True);break
 case=root/(mode+'-source'+str(source))
 command=[sys.executable,'tools/audio-audit/run-content-reference.py','--app',app,'--capture','/tmp/u2k-phase-capture','--mode',mode,'--source-fps',str(source),'--seconds','40','--selection','content','--directory',str(case)]
 print('START mixed',case.name,flush=True);result=subprocess.run(command)
 if result.returncode:raise RuntimeError('Mixed cadence physical run failed')
 r=case/'loopback';log=(r/'playback.log').read_text();rows=list(csv.DictReader((r/'application.csv').open()));cap=list(csv.DictReader((r/'capture.csv').open()));physical=json.loads((r/'result.json').read_text())
 a=events(log,cap);(case/'events.json').write_text(json.dumps(a,indent=2)+'\n')
 b=identified(log,rows,physical,'content',40,cap);(case/'identified.json').write_text(json.dumps(b,indent=2)+'\n')
 print('DONE mixed',case.name,'matches',len(a['physical_id_matches']),'av',a['physical_av'],flush=True)
print('FOLLOWUP COMPLETE',datetime.datetime.now().astimezone().isoformat(),flush=True)
