import unittest
from analyze_recovery import analyse

class RecoveryTests(unittest.TestCase):
    def test_reason_and_overlapping_notifications(self):
        log='''SYNC_TRACE kind=callback stored=0 incident=1
SYNC_TRACE kind=recovery stored=1 omitted=0
SYNC_TRACE kind=completion stored=3 omitted=0
SYNC_RECOVERY host_samples=100000 entry_gap=2400 query_span=2 stream_samples=200 cursor=300 consumed=200 buffered_before=0 duration=48 reason=1 phase_error=12 candidate_ratio=1.000001 preroll=0
SYNC_COMPLETION host_samples=99999 frame=1 result=1
SYNC_COMPLETION host_samples=100024 frame=2 result=2
SYNC_COMPLETION host_samples=100049 frame=3 result=1
'''
        r=analyse(log)['recoveries'][0]
        self.assertEqual(r['reason'],'empty_audio_queue')
        self.assertEqual([v['order'] for v in r['nearby_video_notifications']],['before_callback','within_callback_interval','after_callback'])
        self.assertAlmostEqual(r['candidate_rate_ppm'],1)

    def test_truncated_trace_rejected(self):
        with self.assertRaisesRegex(ValueError,'recovery'):
            analyse('SYNC_TRACE kind=callback stored=0\nSYNC_TRACE kind=recovery stored=0 omitted=1\nSYNC_TRACE kind=completion stored=0 omitted=0\n')

    def test_schedule_lead_uses_observation_age(self):
        log='''SYNC_TRACE kind=callback stored=0 incident=1
SYNC_TRACE kind=recovery stored=0 omitted=0
SYNC_TRACE kind=completion stored=1 omitted=0
SYNC_TRACE kind=schedule stored=1 incident=1
SYNC_COMPLETION host_samples=100000 frame=7 result=2
SYNC_SCHEDULE frame=7 begin=99000 end=99048 completion=98000 stream=100 stream_host=99000 scheduled=200
'''
        result=analyse(log)
        self.assertAlmostEqual(result['schedule_window']['minimum_estimated_lead_ms'],52/48)
        self.assertEqual(result['video_notifications'][0]['matching_schedules'][0]['duration_ms'],1)

    def test_absent_diagnostics_not_reported_as_no_fault(self):
        with self.assertRaises(ValueError):analyse('')

if __name__=='__main__':unittest.main()
