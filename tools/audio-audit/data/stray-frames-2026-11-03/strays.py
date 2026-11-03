"""Count isolated white SDI frames before a flash, and out-of-order frame IDs, in capture CSVs."""
import csv, gzip, sys

def rows(path):
    op = gzip.open(path, 'rt') if path.endswith('.gz') else open(path)
    return list(csv.DictReader(op))

for path in sys.argv[1:]:
    r = rows(path)
    video = [(int(x['pts_48k']), float(x['value'])) for x in r if x['kind'] == 'video']
    runs, cur = [], None
    for i, (_, v) in enumerate(video):
        if v > 136:
            if cur is None: cur = [i, 1]
            else: cur[1] += 1
        elif cur: runs.append(cur); cur = None
    lengths = {}
    for _, n in runs: lengths[n] = lengths.get(n, 0) + 1
    stray = [runs[k + 1][0] - s for k, (s, n) in enumerate(runs) if n == 1 and k + 1 < len(runs) and runs[k + 1][1] > 1 and runs[k + 1][0] - s < 48]
    ids = [int(float(x['value'])) for x in r if x['kind'] == 'video_id']
    disorder = [(i, ids[i - 1], ids[i], ids[i + 1]) for i in range(1, len(ids) - 1) if not (ids[i - 1] <= ids[i] <= ids[i + 1])]
    print(path.rsplit('/', 2)[-2], f'frames {len(video)} flashes {len(runs)} lengths {dict(sorted(lengths.items()))} stray {len(stray)} {stray}', f'ids {len(ids)} out-of-order {len(disorder)} {disorder[:12]}')
