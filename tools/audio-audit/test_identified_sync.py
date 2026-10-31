#!/usr/bin/env python3
import unittest
from analyze_identified_sync import analyse


def fixture():
    log, rows = [], []
    health = dict.fromkeys(['missing_total','api_errors','recoveries','late_video','dropped_video','clock_resets','video_history_misses','video_history_startup_misses','video_conversion_fallbacks'], '0')
    for frame in range(350, 1450):
        stamp = 480000+frame*960
        log.append(f'REFERENCE_PAINT host_samples={stamp+720} id={frame} content_samples={frame*960} audio_origin=480000 clock_matches=1000 clock_skipped=0 clock_mismatches=0 selection=content')
        log.append(f'REFERENCE_SCHEDULE stream_samples={frame*960} id={frame} audio_output_origin=480000 paint_samples={stamp+720} content_time={stamp}')
    for second in range(7, 29):
        rows.append(dict(health, source_pulse_pts=str(480000+second*48000+5)))
    capture = {'channels': {'audio_L': dict(pairs=5, median_ms=.7, min_ms=.7, max_ms=.7)}}
    return '\n'.join(log), rows, capture


class IdentifiedTests(unittest.TestCase):
    def test_known_timeline(self):
        result = analyse(*fixture(), 'content')
        self.assertEqual(result['selected_content_minus_target']['max_ms'], 0)
        self.assertAlmostEqual(result['scheduled_audio_minus_video']['median_ms'], 5/48)

    def test_wrong_content_stamp(self):
        log, rows, capture = fixture()
        with self.assertRaisesRegex(ValueError, 'identified content PTS'):
            analyse(log.replace('content_time=816000', 'content_time=816001'), rows, capture, 'content')

    def test_clock_change_rejected(self):
        log, rows, capture = fixture()
        with self.assertRaisesRegex(ValueError, 'audio-content origin'):
            analyse(log.replace('clock_mismatches=0', 'clock_mismatches=1', 1), rows, capture, 'content')

    def test_audio_marker_and_pulse_must_agree(self):
        log, rows, capture = fixture()
        rows[0]['source_pulse_pts'] = str(int(rows[0]['source_pulse_pts'])+48)
        with self.assertRaisesRegex(ValueError, 'audio pulse PTS'):
            analyse(log, rows, capture, 'content')

    def test_arrival_control_cannot_use_content_stamp(self):
        with self.assertRaisesRegex(ValueError, 'unexpectedly uses content'):
            analyse(*fixture(), 'arrival')

    def test_missing_schedule_observation_rejected(self):
        log, rows, capture = fixture()
        log = '\n'.join(line for line in log.splitlines() if not line.startswith('REFERENCE_SCHEDULE stream_samples=336000 '))
        with self.assertRaisesRegex(ValueError, 'coverage'):
            analyse(log, rows, capture, 'content')

    def test_dropped_video_rejected(self):
        log, rows, capture = fixture()
        rows[0]['dropped_video'] = '1'
        with self.assertRaisesRegex(ValueError, 'health'):
            analyse(log, rows, capture, 'content')


if __name__ == '__main__':
    unittest.main()
