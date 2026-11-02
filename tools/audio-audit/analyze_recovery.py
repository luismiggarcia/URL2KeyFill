#!/usr/bin/env python3
"""Summarise bounded callback/recovery traces and SDK notification order.
Host ordering describes notifications, not the physical cause or emission order.
"""
import argparse
import json
from pathlib import Path
from analyze_identified_sync import records

REASONS={1:'empty_audio_queue',101:'clock_callback_gap',102:'clock_slow_query',103:'clock_nonmonotonic_host',104:'clock_backward_consumption',105:'clock_phase_jump',106:'clock_rate_outside_limit'}

def analyse(log):
    summaries=records(log,'SYNC_TRACE')
    for kind in ('callback','recovery','completion'):
        rows=[r for r in summaries if r['kind']==kind]
        if len(rows)!=1 or int(rows[0].get('omitted',0)):raise ValueError('Missing, repeated or truncated '+kind+' trace')
    callbacks=records(log,'SYNC_CALLBACK');recoveries=records(log,'SYNC_RECOVERY');completions=records(log,'SYNC_COMPLETION')
    for kind,events in [('callback',callbacks),('recovery',recoveries),('completion',completions)]:
        summary=next(r for r in summaries if r['kind']==kind)
        if int(summary['stored'])!=len(events):raise ValueError('Incomplete '+kind+' event body')
    result=dict(callbacks_stored=len(callbacks),recoveries=[],video_notifications=[],diagnostic_stop_acknowledged='SYNC_DIAGNOSTIC_STOP ' in log,
                callback_window_triggered=bool(int(next(r for r in summaries if r['kind']=='callback').get('incident',0))),
                limits='Bounded diagnostic window; callbacks freeze after the first incident and its tail. Completion host times describe SDK notifications, not wire emission or causality. No incident in a short run does not prove reliability.')
    if callbacks:
        result['callback_window_max_ms']={name:max(int(r[field]) for r in callbacks)/48 for name,field in [('entry_gap','entry_gap'),('query_span','query_span'),('duration','duration')]}
    for r in recoveries:
        reason=int(r['reason']);entry=int(r['host_samples']);end=entry+int(r['duration'])
        event=dict(host_samples=entry,callback_end_samples=end,reason=REASONS.get(reason,'unknown_'+str(reason)),buffered_before=int(r['buffered_before']),stream_samples=int(r['stream_samples']),cursor=int(r['cursor']),query_span_ms=int(r['query_span'])/48,entry_gap_ms=int(r['entry_gap'])/48,duration_ms=int(r['duration'])/48,phase_error_ms=float(r['phase_error'])/48,candidate_rate_ppm=(float(r['candidate_ratio'])-1)*1e6)
        event['nearby_video_notifications']=[]
        for v in completions:
            host=int(v['host_samples'])
            if abs(host-entry)>96000:continue
            order='before_callback' if host<entry else 'after_callback' if host>end else 'within_callback_interval'
            event['nearby_video_notifications'].append(dict(host_samples=host,frame=int(v['frame']),result=int(v['result']),order=order,from_callback_entry_ms=(host-entry)/48))
        result['recoveries'].append(event)
    result['video_notifications']=[dict(host_samples=int(v['host_samples']),frame=int(v['frame']),result=int(v['result'])) for v in completions]
    schedule_summaries=[r for r in summaries if r['kind']=='schedule']
    if schedule_summaries:
        schedule=records(log,'SYNC_SCHEDULE')
        if len(schedule_summaries)!=1 or int(schedule_summaries[0]['stored'])!=len(schedule):raise ValueError('Incomplete schedule event body')
        def timing(row):
            begin,end,host=(int(row[k]) for k in ('begin','end','stream_host'))
            completion,stream,scheduled=(int(row[k]) for k in ('completion','stream','scheduled'))
            return dict(frame=int(row['frame']),duration_ms=(end-begin)/48,worker_wait_ms=(begin-completion)/48 if completion else None,estimated_lead_ms=(scheduled-stream-(end-host))/48 if host and stream!=-(1<<63) else None)
        valid=[timing(r) for r in schedule]
        result['schedule_window']=dict(stored=len(valid),triggered=bool(int(schedule_summaries[0].get('incident',0))),maximum_duration_ms=max((r['duration_ms'] for r in valid),default=None),maximum_worker_wait_ms=max((r['worker_wait_ms'] for r in valid if r['worker_wait_ms'] is not None),default=None),minimum_estimated_lead_ms=min((r['estimated_lead_ms'] for r in valid if r['estimated_lead_ms'] is not None),default=None),limits='Lead extrapolates the existing SDK stream observation at nominal 1x speed to submission; it is not an independent hardware deadline measurement.')
        for event in result['video_notifications']:event['matching_schedules']=[r for r in valid if r['frame']==event['frame']]

    return result

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('log');args=parser.parse_args()
    print(json.dumps(analyse(Path(args.log).read_text()),indent=2))
