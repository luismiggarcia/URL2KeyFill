import csv,json,sys,gzip,importlib.util,statistics
from pathlib import Path
sys.path.insert(0,'tools/audio-audit')
from analyze_event_sync import analyse
spec=importlib.util.spec_from_file_location('endurance','tools/audio-audit/run-endurance.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
p=Path(sys.argv[1]);manifest=json.loads((p/'manifest.json').read_text());original=json.loads((p/'result.json').read_text())
if manifest.get('error') or not manifest.get('continuity_passed'):raise ValueError('Original run did not pass continuity')
log=(p/'playback.log').read_text();overall=analyse(log)
with gzip.GzipFile(filename=str(p/'events.json.gz'),mode='wb',mtime=0) as f:f.write((json.dumps(overall,indent=2)+'\n').encode())
points=[];captures=[];names=['capture-start']+[x['name'] for x in original.get('intermediate_captures',[])]+['capture-end']
for name in names:
 a=analyse(log,list(csv.DictReader((p/(name+'.csv')).open())),True);a.pop('events');(p/(name+'-event-analysis.json')).write_text(json.dumps(a,indent=2)+'\n');captures.append(m.captured(p/(name+'.csv')))
 matches=a['periodic_host_matches'];points.append({'name':name,'matched':len(matches),'stream_s':statistics.median(e['video_stream']/48000 for e in matches),'physical_median_ms':a['physical_av']['audio_L']['median_ms'],'physical_min_ms':a['physical_av']['audio_L']['min_ms'],'physical_max_ms':a['physical_av']['audio_L']['max_ms'],'scheduled_median_ms':statistics.median(e['scheduled_av_ms'] for e in matches),'capture_minus_scheduled':a['capture_minus_scheduled']})
quality=m.sampled_quality(captures,original['av_frame_limit_ms'])
rows=list(csv.DictReader((p/'application.csv').open()));active=[r for r in rows if r['running']=='1' and r['on_air']=='1'];times=[float(r['elapsed_s']) for r in active]
powers=sorted(p.glob('power-*.txt'))
for power in powers:m.validate_power(power.read_text(),power.read_text(),manifest['awake_pid'])
result={'points':points,'quality_revalidated':quality,'original_accepted':manifest['accepted'],'power_checks':len(powers),'max_active_telemetry_interval_s':max(b-a for a,b in zip(times,times[1:])),'event_trace_omitted':0,'method':'Accepted PCM and selected flash events. Generic periodic/host matching assumes observation gap <400 ms and validates a constant timestamp epoch per capture; no physical content IDs. Original runner result preserved. Same recomputation applied to every capture and full sampled envelope.'}
(p/'phase-analysis.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
