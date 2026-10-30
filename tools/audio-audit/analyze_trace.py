#!/usr/bin/env python3
"""Summarise a CEF audio packet trace.

Usage: analyze_trace.py <trace> [--jitter-margins 0,1,2,5,10,20,40]

Accepted formats (one line per packet):
  9 columns (capture-trace.sh): browser raw_pts_ms adjusted_pts_ms frames channels
            system_us steady_us mach_absolute_us mach_continuous_us
  6 columns (first audit trace): browser system_us steady_us frames channels adjusted_pts_ms
"""
import argparse, collections, statistics

def load(path):
    lanes = collections.defaultdict(list)
    for line in open(path):
        c = line.split()
        if len(c) == 9:
            lanes[c[0]].append(dict(raw=int(c[1]), pts=int(c[2]), n=int(c[3]), ch=int(c[4]),
                system=int(c[5]), steady=int(c[6]), mach=float(c[7]), cont=float(c[8])))
        elif len(c) == 6:
            lanes[c[0]].append(dict(raw=None, pts=int(c[5]), n=int(c[3]), ch=int(c[4]),
                system=int(c[1]), steady=int(c[2]), mach=None, cont=None))
        elif c:
            raise SystemExit("Unexpected trace line: " + line)
    return lanes

def pct(values, p):
    values = sorted(values)
    return values[round(p * (len(values) - 1))]

def ppm(a, b):
    """Rate of clock a against clock b, both in microseconds, first to last packet."""
    return ((a[-1] - a[0]) - (b[-1] - b[0])) / (b[-1] - b[0]) * 1e6

def report(lane, packets, margins, domain):
    first, last = packets[0], packets[-1]
    span = (last['steady'] - first['steady']) / 1e6
    print(f"Lane {lane}: {len(packets)} packets over {span:.1f} s, frames {sorted({p['n'] for p in packets})}, channels {sorted({p['ch'] for p in packets})}")
    deltas = collections.Counter(b['pts'] - a['pts'] for a, b in zip(packets, packets[1:]))
    print("  PTS step (ms):", dict(sorted(deltas.items())))
    overlaps = gaps = 0
    for a, b in zip(packets, packets[1:]):
        end, start = a['pts'] * 48 + a['n'], b['pts'] * 48
        overlaps += start < end
        gaps += start > end
    print(f"  48 kHz continuity: {overlaps} overlaps, {gaps} gaps")
    # Historical traces used a first-arrival wall-clock offset. Current traces
    # preserve raw uptime PTS; choose the matching domain explicitly.
    if domain == 'uptime' and first['mach'] is None:
        raise SystemExit('Uptime PTS analysis requires a nine-column trace')
    lateness = [(p['mach'] if domain == 'uptime' else p['system']) / 1000 - p['pts'] for p in packets]
    print("  Arrival after packet start (ms; selected domain): p50 %.2f  p99 %.2f  max %.2f" % (pct(lateness, .5), pct(lateness, .99), max(lateness)))
    arrivals = [(b['steady'] - a['steady']) / 1000 for a, b in zip(packets, packets[1:])]
    print("  Inter-arrival (ms): p1 %.2f  p50 %.2f  p99 %.2f  max %.2f" % (pct(arrivals, .01), pct(arrivals, .5), pct(arrivals, .99), max(arrivals)))
    print("  Packets arriving after an SDI read ending at now-margin first needs them:")
    for m in margins:
        late = sum(1 for x in lateness if x > m)
        print(f"    margin {m:>4g} ms: {late} ({100 * late / len(packets):.2f}%)")
    steady = [p['steady'] for p in packets]
    print("  system_clock vs steady_clock: %+.1f ppm" % ppm([p['system'] for p in packets], steady))
    print("  adjusted PTS vs steady_clock: %+.1f ppm (1 ms quantisation: ±%.1f ppm)" % (ppm([p['pts'] * 1000 for p in packets], steady), 2000 / (steady[-1] - steady[0]) * 1e6))
    if first['raw'] is not None:
        mach = [p['mach'] for p in packets]
        lag = [p['mach'] / 1000 - p['raw'] for p in packets]
        print("  Raw PTS domain: mach_absolute_time - raw PTS = p1 %.2f  p50 %.2f  p99 %.2f ms" % (pct(lag, .01), pct(lag, .5), pct(lag, .99)))
        print("  raw PTS vs mach_absolute_time: %+.1f ppm" % ppm([p['raw'] * 1000 for p in packets], mach))
        print("  steady_clock - mach_continuous_time: %.3f ms (median)" % (statistics.median(p['steady'] - p['cont'] for p in packets) / 1000))
        print("  mach_continuous - mach_absolute (time asleep since boot): %.1f s" % ((first['cont'] - first['mach']) / 1e6))

parser = argparse.ArgumentParser()
parser.add_argument('trace')
parser.add_argument('--jitter-margins', default='0,1,2,5,10,20,40')
parser.add_argument('--pts-domain', choices=['wall', 'uptime'], default='wall')
args = parser.parse_args()
margins = [float(x) for x in args.jitter_margins.split(',')]
for lane, packets in load(args.trace).items():
    report(lane, packets, margins, args.pts_domain)
