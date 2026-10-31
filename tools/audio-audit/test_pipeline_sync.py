import unittest
from analyze_pipeline_sync import analyse

class PipelineSyncTests(unittest.TestCase):
    def rows(self):
        return [dict(running='1', clock_calibrated='1', elapsed_s=str(s), audio_source_origin='1000000',
                source_pulse_pts=str(1000000+s*48000), source_flash_pts=str(1000000+s*48000+480),
                scheduled_flash_pts=str(s*48000+960)) for s in range(15)]

    def test_decomposition(self):
        r = analyse(self.rows(), {'channels': {'audio_L': {'median_ms': -19.3}}})
        self.assertEqual(r['source_flash_minus_audio']['median_ms'], 10)
        self.assertEqual(r['scheduled_audio_minus_video']['median_ms'], -20)
        self.assertAlmostEqual(r['captured_minus_scheduled_ms'], .7)

    def test_different_windows_are_not_subtracted_as_exact_offset(self):
        rows = self.rows()
        for i, r in enumerate(rows):r['scheduled_flash_pts'] = str(i*48000+(480 if i%2 else 960))
        r = analyse(rows, {'channels': {'audio_L': {'median_ms': -19.3, 'min_ms': -19.3, 'max_ms': -9.3}}})
        self.assertIsNone(r['captured_minus_scheduled_ms'])
        self.assertLess(r['captured_minus_scheduled_range_ms'][0], .7)
        self.assertGreater(r['captured_minus_scheduled_range_ms'][1], .7)

    def test_clock_change_rejected(self):
        rows = self.rows();rows[-1]['audio_source_origin'] = '1000480'
        with self.assertRaises(ValueError):analyse(rows)

    def test_incomplete_diagnostics_rejected(self):
        with self.assertRaises(ValueError):analyse([])

if __name__ == '__main__':unittest.main()
