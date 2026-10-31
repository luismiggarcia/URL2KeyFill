#!/usr/bin/env python3
import unittest
from analyze_loopback import analyse


def fixture(frame=1920, offset=4800, right=True):
    rows = []
    for pts in range(0, 12*48000, frame):
        rows.append(dict(kind='video', pts_48k=str(pts), duration_48k=str(frame),
                         value='235' if pts % 48000 < 1920 else '30'))
    for sec in range(12):
        for channel in ('audio_L', 'audio_R') if right else ('audio_L',):
            rows.append(dict(kind=channel, pts_48k=str(sec*48000+offset), duration_48k='0', value='.01'))
    return rows


class LoopbackTests(unittest.TestCase):
    def test_audio_late_and_frame_rate(self):
        for frame in (1920, 960):
            r = analyse(fixture(frame), 1)
            self.assertEqual(r['channels']['audio_L']['median_ms'], 100)
            self.assertEqual(r['video_frame_ms'], frame/48)
            self.assertEqual(r['video_timestamp_gaps'], 0)

    def test_audio_early(self):
        r = analyse(fixture(offset=-2400), 1)
        self.assertEqual(r['channels']['audio_R']['median_ms'], -50)

    def test_reference_near_zero(self):
        r = analyse(fixture(offset=33), 1)
        self.assertEqual(r['channels']['audio_L']['median_ms'], .6875)
        self.assertEqual(r['channels']['audio_L']['unmatched_flashes'], 0)

    def test_large_offset_rejected(self):
        with self.assertRaises(ValueError):
            analyse(fixture(offset=24000))

    def test_startup_gap_excluded_from_matched_range(self):
        rows = fixture()
        rows = [r for r in rows if not (r['kind'] == 'video' and int(r['pts_48k']) == 1920)]
        r = analyse(rows, 1)
        self.assertEqual(r['video_timestamp_gaps'], 1)
        self.assertEqual(r['video_timestamp_gaps_within_matched_range'], 0)

    def test_missing_channel(self):
        with self.assertRaises(ValueError):
            analyse(fixture(right=False))

    def test_key_or_missing_flashes(self):
        rows = fixture()
        for r in rows:
            if r['kind'] == 'video':
                r['value'] = '235'
        with self.assertRaises(ValueError):
            analyse(rows)

    def test_input_relock_rejected(self):
        rows = fixture();rows.append(dict(kind='no_signal', pts_48k='0', duration_48k='1920', value='0'))
        with self.assertRaises(ValueError):analyse(rows)

    def test_video_gap_is_reported(self):
        rows = fixture()
        rows = [r for r in rows if not (r['kind'] == 'video' and int(r['pts_48k']) == 48000*4)]
        r = analyse(rows, 1)
        self.assertEqual(r['video_timestamp_gaps'], 1)
        self.assertEqual(r['channels']['audio_L']['median_ms'], 100)


if __name__ == '__main__':
    unittest.main()
