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

    def test_no_marker_rejected(self):
        log,rows,capture=fixture()
        with self.assertRaisesRegex(ValueError,'marker'):analyse(log.replace('SYNC_ADJUSTMENT','NO_CHANGE'),rows,capture,20)

    def test_loss_rejected(self):
        log,rows,capture=fixture();rows[0]['missing_total']='1'
        with self.assertRaisesRegex(ValueError,'continuity'):analyse(log,rows,capture,20)


if __name__=='__main__':unittest.main()
