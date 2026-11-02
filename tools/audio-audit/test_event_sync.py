import unittest
from analyze_event_sync import analyse


def fixture():
    log, rows = [], []
    for second in range(7, 19):
        t = second*48000
        mapping=f'mapping_source={t+1000000} mapping_stream={t} ratio=1'
        log += [f'SYNC_AUDIO stream_samples={t+240} source_samples={t+1000240} transition=0 delay=0 {mapping}',
                f'SYNC_VIDEO stream_samples={t} host_samples={t+1000000} paint_samples={t+1000000} source_target={t+1000000} id={second*50} sequence={second} adjustment=0 {mapping}']
        for channel in ('audio_L', 'audio_R'):
            rows.append(dict(kind=channel, pts_48k=str(t+273), duration_48k='0', value='1'))
    log += ['SYNC_TRACE kind=audio stored=12 omitted=0', 'SYNC_TRACE kind=video stored=12 omitted=0']
    for t in range(0, 20*48000, 1920):
        flash = t % 48000 == 0 and 7*48000 <= t < 19*48000
        rows.append(dict(kind='video', pts_48k=str(t), duration_48k='1920', value='235' if flash else '16', host_48k=str(t+1005000)))
        rows.append(dict(kind='video_id', pts_48k=str(t), duration_48k='1920', value=str(t//960)))
    return '\n'.join(log), rows


class EventSyncTests(unittest.TestCase):
    def test_accepted_pcm_and_physical_ids(self):
        result = analyse(*fixture())
        self.assertEqual(result['scheduled_av']['median_ms'], 5)
        self.assertEqual(result['capture_minus_scheduled']['median_ms'], 33/48)
        self.assertEqual(len(result['physical_id_matches']), 12)

    def test_overflow_is_not_a_complete_trace(self):
        log, rows = fixture()
        with self.assertRaisesRegex(ValueError, 'truncated'):
            analyse(log.replace('omitted=0', 'omitted=1', 1), rows)

    def test_missing_event_rejected(self):
        log, rows = fixture()
        log = '\n'.join(log.splitlines()[1:])
        with self.assertRaisesRegex(ValueError, 'Incomplete'):
            analyse(log, rows)

    def test_duplicate_stream_position_rejected(self):
        log, rows = fixture()
        with self.assertRaisesRegex(ValueError, 'non-monotonic'):
            analyse(log.replace('stream_samples=384240', 'stream_samples=336240'), rows)

    def test_transitions_excluded(self):
        log, rows = fixture()
        result = analyse(log.replace('transition=0', 'transition=1', 1), rows)
        self.assertEqual(result['scheduled_av']['count'], 11)

    def test_wrong_physical_ids_not_nearest_matched(self):
        log, rows = fixture()
        for r in rows:
            if r['kind'] == 'video_id':
                r['value'] = '9999'
        with self.assertRaisesRegex(ValueError, 'exact physical'):
            analyse(log, rows)

    def test_generic_pages_do_not_claim_id_matches(self):
        log, rows = fixture()
        for second in range(7, 19):
            log = log.replace(f'id={second*50} ', 'id=-1 ')
        result = analyse(log, [r for r in rows if r['kind'] != 'video_id'])
        self.assertIsNone(result['physical_id_matches'])

    def test_old_probe_cannot_validate_identified_events(self):
        log, rows = fixture()
        with self.assertRaisesRegex(ValueError, 'rebuild the capture'):
            analyse(log, [r for r in rows if r['kind'] != 'video_id'])

    def test_periodic_host_pairing_requires_explicit_assumption(self):
        log, rows = fixture()
        for second in range(7, 19):
            log = log.replace(f'id={second*50} ', 'id=-1 ')
        rows = [r for r in rows if r['kind'] != 'video_id']
        self.assertNotIn('periodic_host_matches', analyse(log, rows))
        result = analyse(log, rows, True)
        self.assertEqual(len(result['periodic_host_matches']), 12)
        self.assertEqual(result['capture_minus_scheduled']['median_ms'], 33/48)
        with self.assertRaisesRegex(ValueError, 'video epoch'):
            changed=log.replace('SYNC_VIDEO stream_samples=384000 ', 'SYNC_VIDEO stream_samples=385920 ').replace('source_target=1384000 ', 'source_target=1385920 ')
            analyse(changed, rows, True)

    def test_mapping_corruption_rejected(self):
        log, rows = fixture()
        with self.assertRaisesRegex(ValueError, 'affine mapping'):
            analyse(log.replace('source_samples=1336240', 'source_samples=1336241'), rows)

    def test_fractional_audio_and_common_video_delay(self):
        log, rows = fixture();changed=[]
        for line in log.splitlines():
            prefix,*fields=line.split();r=dict(field.split('=',1) for field in fields)
            if prefix=='SYNC_AUDIO':
                r['delay']='720.125';r['source_samples']=str(float(r['source_samples'])-720.125)
            elif prefix=='SYNC_VIDEO':
                r['adjustment']='-240';r['video_delay_source']='960.125';r['source_target']=str(float(r['source_target'])-960.125)
            changed.append(prefix+' '+' '.join(k+'='+v for k,v in r.items()))
        result=analyse('\n'.join(changed),rows)
        self.assertEqual(result['scheduled_av']['median_ms'],5)

    def test_future_selection_limit_is_checked_when_available(self):
        log, rows = fixture()
        valid=log.replace('sequence=7 ', 'sequence=7 selection_samples=1336480 max_ahead=480 ')
        analyse(valid, rows)
        with self.assertRaisesRegex(ValueError,'future-frame'):
            analyse(valid.replace('selection_samples=1336480','selection_samples=1336481'), rows)


if __name__ == '__main__':
    unittest.main()
