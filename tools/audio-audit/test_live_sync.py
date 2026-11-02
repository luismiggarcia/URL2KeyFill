import unittest
from analyze_live_sync import analyse


def fixture(change=20):
    log='SYNC_ADJUSTMENT ms=20 active_s=15\n';capture=[]
    for second in range(7,29):
        identity=second*50
        log+=f'REFERENCE_SCHEDULE stream_samples={second*48000} id={identity}\n'
        pts=second*48000
        capture.extend([dict(kind='video_id',value=str(identity),pts_48k=str(pts)),dict(kind='video_id',value=str(identity+2),pts_48k=str(pts+1920)),dict(kind='audio_L',value='1',pts_48k=str(pts+(change*48 if second>=16 else 0)))])
        log+=f'REFERENCE_SCHEDULE stream_samples={second*48000+1920} id={identity+2}\n'
    row=dict.fromkeys(['missing_total','api_errors','recoveries','dropped_video','late_video','clock_resets','video_history_misses','video_conversion_fallbacks'],'0')
    return log,[row],capture


class LiveSyncTests(unittest.TestCase):
    def test_requested_change(self):
        self.assertEqual(analyse(*fixture(),20)['change_ms'],20)

    def test_wrong_change_rejected(self):
        with self.assertRaisesRegex(ValueError,'requested'):analyse(*fixture(5),20)

    def test_nonzero_initial_offset_uses_net_change(self):
        result=analyse(*fixture(15),20,5)
        self.assertEqual(result['requested_change_ms'],15)
        self.assertEqual(result['change_ms'],15)

    def test_no_marker_rejected(self):
        log,rows,capture=fixture()
        with self.assertRaisesRegex(ValueError,'marker'):analyse(log.replace('SYNC_ADJUSTMENT','NO_CHANGE'),rows,capture,20)

    def test_loss_rejected(self):
        log,rows,capture=fixture();rows[0]['missing_total']='1'
        with self.assertRaisesRegex(ValueError,'continuity'):analyse(log,rows,capture,20)

    def test_packet_position_corrects_incremental_audio_latency(self):
        log,rows,capture=fixture();origin=1000000
        for r in capture:
            pts=int(r['pts_48k']);second=pts//48000;t=second*48000
            r['host_48k']=str(origin+(pts if r['kind']=='video_id' else t+1920)+5000)
        for second in range(7,29):
            t=second*48000
            log+=f'REFERENCE_PAINT id={second*50} host_samples={origin+t+200} audio_origin={origin}\n'
            capture.append(dict(kind='packet',pts_48k=str(t),duration_48k='1920',value='1',host_48k=str(origin+t+1920+5000)))
        result=analyse(log,rows,capture,20)['incremental_callback_latency_estimate']
        self.assertAlmostEqual(result['video']['change_ms'],0)
        self.assertAlmostEqual(result['audio']['change_ms'],20)


if __name__=='__main__':unittest.main()
