#!/usr/bin/env python3
"""Check the independent verifier against the retained source and damaged files."""
import gzip
from pathlib import Path
import tempfile
import unittest
from verify_content_reference import verify


class ContentReferenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.media = gzip.decompress((Path(__file__).parent/'data/content-reference-2026-10-31/reference.mkv.gz').read_bytes())

    def inspect(self, data):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'reference.mkv'
            path.write_bytes(data)
            return verify(path)

    def test_known_source(self):
        result = self.inspect(self.media)
        self.assertEqual(result['video_frames'], 3000)
        self.assertEqual(result['pulse_count'], 59)
        self.assertAlmostEqual(result['source_audio_minus_video_ms'], 5/48)

    def test_truncated_file(self):
        with self.assertRaisesRegex(ValueError, 'Truncated'):
            self.inspect(self.media[:-1])

    def test_wrong_timebase(self):
        damaged = self.media.replace(bytes.fromhex('2ad7b1830f4240'), bytes.fromhex('2ad7b1830f4241'), 1)
        self.assertNotEqual(damaged, self.media)
        with self.assertRaisesRegex(ValueError, 'timecode scale'):
            self.inspect(damaged)

    def test_wrong_sample_rate(self):
        # SamplingFrequency: 8-byte float 48000 -> 44100, without changing sizes.
        import struct
        damaged = self.media.replace(bytes.fromhex('b588')+struct.pack('>d', 48000), bytes.fromhex('b588')+struct.pack('>d', 44100), 1)
        self.assertNotEqual(damaged, self.media)
        with self.assertRaisesRegex(ValueError, '48 kHz'):
            self.inspect(damaged)


if __name__ == '__main__':
    unittest.main()
