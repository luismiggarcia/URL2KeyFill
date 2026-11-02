import subprocess,sys,json,csv
from pathlib import Path
sys.path.insert(0,'tools/audio-audit')
from analyze_live_sync import analyse
root=Path('/tmp/u2k-negative-final-matrix');root.mkdir(exist_ok=False)
results=[]
for mode,initial,final in [(m,0,d) for m in ['1080p25','1080p50'] for d in [-1,-5,-21]]+[('1080p25',-5,5),('1080p50',5,-5)]:
 case=root/f'{mode}_{initial}_to_{final}'
 cmd=[sys.executable,'tools/audio-audit/run-content-reference.py','--app','/tmp/u2k-negative-adjustment-v2/URL2KeyFill.app/Contents/MacOS/URL2KeyFill','--capture','/tmp/u2k-phase-capture','--mode',mode,'--selection','content','--seconds','35','--sync-offset-ms',str(initial),'--sync-change-ms',str(final),'--directory',str(case)]
 rc=subprocess.call(cmd);result=dict(mode=mode,initial=initial,final=final,process_exit=rc)
 try:
  p=case/'loopback';result.update(analyse((p/'playback.log').read_text(),list(csv.DictReader((p/'application.csv').open())),list(csv.DictReader((p/'capture.csv').open())),final,initial));result['passed']=rc==0
 except Exception as e:result.update(passed=False,error=str(e))
 case.mkdir(exist_ok=True);(case/'live-analysis.json').write_text(json.dumps(result,indent=2)+'\n');results.append(result);(root/'summary.json').write_text(json.dumps(results,indent=2)+'\n');print(result,flush=True)
