#!/usr/bin/env python3
"""Independently inspect Matroska timestamps and PCM onsets of the generated source.
Does not establish physical decoder or receiver latency. VP8 pixel decoding is
checked by the source page before playback and must be corroborated in its log.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct


def elements(data):
    pos = 0
    while pos < len(data):
        start = pos
        first = data[pos]
        width = next((n for n in range(1, 9) if first & (1 << (8-n))), None)
        if width is None or width > 4:
            raise ValueError('Invalid EBML id')
        tag = int.from_bytes(data[pos:pos+width], 'big')
        pos += width
        first = data[pos]
        width = next((n for n in range(1, 9) if first & (1 << (8-n))), None)
        if width is None:
            raise ValueError('Invalid EBML size')
        size = int.from_bytes(data[pos:pos+width], 'big') & ((1 << (7*width))-1)
        pos += width
        end = pos+size
        if end > len(data) or end <= start:
            raise ValueError('Truncated EBML payload')
        yield tag, data[pos:end]
        pos = end


def verify(path, identified=False):
    data = Path(path).read_bytes()
    top = dict(elements(data))
    segment = list(elements(top[0x18538067]))
    info = dict(elements(dict(segment)[0x1549a966]))
    if int.from_bytes(info[0x2ad7b1], 'big') != 1000000:
        raise ValueError('Expected one-millisecond timecode scale')
    tracks = {}
    for tag, payload in elements(dict(segment)[0x1654ae6b]):
        fields = dict(elements(payload))
        tracks[int.from_bytes(fields[0xd7], 'big')] = fields
    if tracks[1][0x86] != b'V_VP8' or tracks[2][0x86] != b'A_PCM/INT/LIT':
        raise ValueError('Unexpected codecs')
    af = dict(elements(tracks[2][0xe1]))
    if struct.unpack('>d', af[0xb5])[0] != 48000 or int.from_bytes(af[0x9f], 'big') != 2 or int.from_bytes(af[0x6264], 'big') != 16:
        raise ValueError('Expected stereo PCM16 at 48 kHz')
    video, audio = [], []
    for tag, payload in segment:
        if tag != 0x1f43b675:
            continue
        cluster = list(elements(payload))
        base = int.from_bytes(dict(cluster)[0xe7], 'big')
        for kind, block in cluster:
            if kind != 0xa3:
                continue
            if block[0] not in (0x81, 0x82) or block[3] != 0x80:
                raise ValueError('Expected independent unlaced blocks')
            stamp = base+struct.unpack('>h', block[1:3])[0]
            (video if block[0] == 0x81 else audio).append((stamp, block[4:]))
    if len(video) != 3000 or len(audio) != 3000:
        raise ValueError('Expected 60 seconds at 50 fps with matching PCM packets')
    if identified and len({v for _, v in video}) != 3000:
        raise ValueError('Expected 3000 distinct identified VP8 payloads')
    black, white = video[0][1], video[50][1]
    if black == white:
        raise ValueError('Missing distinct VP8 images')
    onsets, previous_quiet = [], 4800
    for i, ((vt, pixels), (at, pcm)) in enumerate(zip(video, audio)):
        if vt != i*20 or at != vt or len(pcm) != 960*4:
            raise ValueError('Non-contiguous A/V PTS or invalid PCM duration')
        is_white = i >= 50 and i % 50 < 2
        if not identified and pixels != (white if is_white else black):
            raise ValueError('Video content disagrees with the specified 40 ms flash PTS')
        if identified:
            word = 0
            for j in range(200, 264):
                word = (word << 1) | (struct.unpack_from('<h', pcm, j*4)[0] & 1)
            if word >> 32 != 0x55324b46 or (word >> 16) & 0xffff != i or word & 0xffff != i ^ 0xa55a:
                raise ValueError('PCM frame watermark disagrees with content PTS')
        for j, (left, right) in enumerate(struct.iter_unpack('<hh', pcm)):
            if left != right:
                raise ValueError('Stereo channels differ')
            n = i*960+j
            t = (n % 48000)/48000
            envelope = max(0, min(1, t/.002, (.04-t)/.002))
            expected_value = round(32767*10**(-18/20)*math.sin(2*math.pi*1000*t)*envelope) if n >= 48000 and t < .04 else 0
            if abs(left-expected_value) > 1:
                raise ValueError('PCM waveform differs from the specified pulse')
            active = abs(left/32768) > .003
            if active:
                if previous_quiet >= 4800:
                    onsets.append(n)
                previous_quiet = 0
            else:
                previous_quiet += 1
    expected = [second*48000+5 for second in range(1, 60)]
    if onsets != expected:
        raise ValueError('PCM onset samples differ from the reference specification')
    return dict(sha256=hashlib.sha256(data).hexdigest(), video_frames=len(video), audio_packets=len(audio),
                identified=identified, pulse_count=len(onsets), first_audio_onset_sample=onsets[0], first_flash_pts_ms=1000,
                source_audio_minus_video_ms=5/48, all_packet_pts_contiguous=True,
                limits='VP8 payload identity/PTS and raw PCM checked; decoded image luma is checked separately by the CEF source page.')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--identified', action='store_true')
    p.add_argument('media')
    args = p.parse_args()
    print(json.dumps(verify(args.media, identified=args.identified), indent=2))
