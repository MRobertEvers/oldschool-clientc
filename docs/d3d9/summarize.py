"""Turn a runner result file into the tables the report is written from."""
import json
import sys


def load(path):
    return json.load(open(path, encoding='utf-8'))


def arm_of(tag):
    return tag.split('-', 1)[1]


def table(res):
    rows = []
    for r in res['runs']:
        rows.append({
            'tag': r['tag'],
            'arm': arm_of(r['tag']),
            'ready_s': r.get('ready_at_s'),
            'win_s': r.get('window_s'),
            'fps_med': r.get('fps_median'),
            'fps_best': r.get('fps_best'),
            'wall_ms': r.get('ms_per_frame_wall'),
            'cpu_share': r.get('cpu_share'),
            'cpu_ms': r.get('ms_per_frame_cpu'),
            'rpd_share': r.get('rpdxp_share'),
            'ws_peak': r.get('ws_peak_mb'),
            'rc': r.get('rc'),
            'n_windows': len(r.get('fps_steady') or []),
        })
    return rows


def main(path):
    res = load(path)
    rows = table(res)
    hdr = ('tag', 'ready_s', 'win_s', 'n_windows', 'fps_med', 'wall_ms',
           'cpu_share', 'cpu_ms', 'rpd_share', 'ws_peak')
    print('\t'.join(hdr))
    for r in rows:
        print('\t'.join(str(r[h]) for h in hdr))

    print()
    print('per-arm (best = lowest cpu ms/frame):')
    arms = {}
    for r in rows:
        arms.setdefault(r['arm'], []).append(r)
    for arm in sorted(arms):
        rs = arms[arm]
        cpu = [x['cpu_ms'] for x in rs if x['cpu_ms']]
        wall = [x['wall_ms'] for x in rs if x['wall_ms']]
        fps = [x['fps_med'] for x in rs if x['fps_med']]
        if not cpu:
            print('%-8s no usable runs' % arm)
            continue
        print('%-8s runs=%d  cpu_ms best=%.2f all=%s | wall_ms best=%.2f all=%s'
              ' | fps_med=%s'
              % (arm, len(rs), min(cpu), [round(c, 2) for c in cpu],
                 min(wall), [round(w, 2) for w in wall], fps))


if __name__ == '__main__':
    main(sys.argv[1])
