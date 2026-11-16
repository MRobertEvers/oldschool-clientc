#!/usr/bin/env python3
"""Read the web client's black-box reports (io_server POST /telemetry).

io_server appends each report the page posts to <telemetry-dir>/<UTC date>.jsonl
(default logs/telemetry/ under the server's working directory; on the NUC
~/torirs/live/logs/telemetry/). src/web/torirs_telemetry.js says what a report
holds and when one is sent.

    tools/web/telemetry_report.py                     # every session, one line each
    tools/web/telemetry_report.py --incidents         # + a timeline before each incident
    tools/web/telemetry_report.py --sid 3f9a...       # one session's whole timeline
    tools/web/telemetry_report.py --ua iPad DIR_OR_FILES...
    tools/web/telemetry_report.py --ua iPad --touch   # one line per finger, lost taps marked

--touch reads the input breadcrumbs a touch device sends (touch_js, touch,
ui_click, menu_use; torirs_telemetry.js) as one row per finger: what the
browser delivered, what the client's touch policy took it for, where the click
landed in the UI tree and which menu row ran. A row is marked when the finger
did nothing a tap should have done:
    NO-CLIENT   the browser saw it, the client's touch layer never did
    DRAG        a short finger the policy took for a drag (travel vs slop)
    HOLD/MULTI  taken for a long press / part of a two-finger gesture
    NOTHING     a tap whose click hit no component and ran no menu row

An INCIDENT is one of: the WebGL context lost, the main thread blocked (the
watchdog's report), a tab that ended without a pagehide while visible (the
next load's `previous_unclean`), a stall of 2 s or more, a wasm abort, a GL
error, a world buffer that could not be allocated.
"""

import argparse
import json
import os
import sys
from collections import defaultdict

INCIDENT_KINDS = {
    'gl_context_lost', 'gl_context_failed', 'gl_context_creation_error', 'wasm_abort',
    'gl_error', 'js_error', 'promise_rejection',
}


def mb(n):
    if not isinstance(n, (int, float)):
        return '?'
    return f'{n / 1048576:.0f}MB'


def load(paths):
    files = []
    for p in paths:
        if os.path.isdir(p):
            files += sorted(os.path.join(p, f) for f in os.listdir(p) if f.endswith('.jsonl'))
        else:
            files.append(p)
    reports = []
    for f in files:
        with open(f, encoding='utf-8', errors='replace') as fh:
            for line in fh:
                line = line.strip()
                if not line:
                    continue
                try:
                    reports.append(json.loads(line))
                except json.JSONDecodeError:
                    print(f'{f}: unreadable line skipped', file=sys.stderr)
    return reports


class Session:
    def __init__(self, sid):
        self.sid = sid
        self.device = None
        self.args = None
        self.boot_wall = None
        self.events = {}           # n -> event
        self.blocked = []          # watchdog reports
        self.unclean = None        # filled from the NEXT session's previous_unclean
        self.last_snapshot = None
        self.last_wall = 0
        self.gl_peak = 0
        self.heap_peak = 0

    def note_snapshot(self, snap):
        if not isinstance(snap, dict):
            return
        self.last_snapshot = snap
        self.heap_peak = max(self.heap_peak, snap.get('heap_peak') or snap.get('heap') or 0)
        self.gl_peak = max(self.gl_peak, (snap.get('gl') or {}).get('peak') or 0)

    def add_events(self, events):
        for ev in events or []:
            if isinstance(ev, dict) and 'n' in ev:
                self.events[ev['n']] = ev

    def timeline(self):
        return [self.events[n] for n in sorted(self.events)]


def build(reports):
    sessions = {}

    def get(sid):
        if sid not in sessions:
            sessions[sid] = Session(sid)
        return sessions[sid]

    for wrapped in reports:
        r = wrapped.get('report') if isinstance(wrapped, dict) else None
        if not isinstance(r, dict) or 'sid' not in r:
            continue
        s = get(r['sid'])
        if r.get('device'):
            s.device = r['device']
        if r.get('args'):
            s.args = r['args']
        if r.get('boot_wall'):
            s.boot_wall = r['boot_wall']
        s.last_wall = max(s.last_wall, r.get('wall') or 0)
        if r.get('reason') == 'main_thread_blocked':
            s.blocked.append(r)
            state = r.get('state') or {}
            s.add_events(state.get('tail'))
            continue
        s.add_events(r.get('events'))
        if r.get('snapshot'):
            s.note_snapshot(r['snapshot'])
        prev = r.get('previous')
        if r.get('reason') == 'previous_unclean' and isinstance(prev, dict) and prev.get('sid'):
            p = get(prev['sid'])
            p.unclean = prev
            p.add_events(prev.get('events'))
            if prev.get('snapshot'):
                p.note_snapshot(prev['snapshot'])
            if prev.get('boot_wall'):
                p.boot_wall = p.boot_wall or prev['boot_wall']
            if not p.device and r.get('device'):
                p.device = r['device']  # same browser, the next load
    return sessions


def gpu_name(s):
    for ev in s.timeline():
        if ev.get('k') == 'gl_context' and isinstance(ev.get('d'), dict):
            d = ev['d']
            return f"{d.get('type')} {d.get('gpu') or d.get('renderer') or ''}".strip()
    return '-'


def is_incident(ev):
    k = ev.get('k')
    d = ev.get('d')
    if k in INCIDENT_KINDS:
        return True
    if k == 'stall' and isinstance(d, dict) and (d.get('ms') or 0) >= 2000:
        return True
    if k == 'gpu_world_alloc' and isinstance(d, dict) and d.get('ok') == 0:
        return True
    if k == 'stderr' and isinstance(d, str) and any(
            w in d.lower() for w in ('abort', 'cannot enlarge', 'out of memory', 'glgeterror')):
        return True
    return False


def short_ua(device):
    if not device:
        return '?'
    ua = device.get('ua') or ''
    # iPadOS Safari asks for the desktop site: a "Macintosh" with a touch screen.
    if 'Macintosh' in ua and (device.get('touch') or 0) > 1:
        ua = ua.replace('Macintosh', 'iPad')
    for tag in ('iPad', 'iPhone', 'Android', 'Macintosh', 'Windows', 'Linux'):
        if tag in ua:
            ver = ''
            if 'Version/' in ua:
                ver = ' Safari ' + ua.split('Version/')[1].split(' ')[0]
            elif 'CriOS/' in ua:
                ver = ' Chrome ' + ua.split('CriOS/')[1].split(' ')[0]
            return tag + ver
    return ua[:40]


def fmt_mem_tree(d):
    tree = d.get('tree') or {}
    top = sorted(((k, v) for k, v in tree.items() if '/' not in k), key=lambda kv: -kv[1][0])
    parts = []
    for k, v in top[:8]:
        kids = sorted(((c[len(k) + 1:], cv) for c, cv in tree.items()
                       if c.startswith(k + '/') and '/' not in c[len(k) + 1:]), key=lambda kv: -kv[1][0])
        inner = ', '.join(f'{c} {mb(cv[0])}' for c, cv in kids[:4])
        parts.append(f"{k} {mb(v[0])}" + (f" ({inner})" if inner else ''))
    used = f" used {mb(d.get('used'))}" if d.get('used') else ''
    return f"[{d.get('why')}] heap {mb(d.get('heap'))}{used}: " + '; '.join(parts)


def fmt_event(ev):
    d = ev.get('d')
    if ev.get('k') == 'mem_tree' and isinstance(d, dict):
        return f"{ev.get('t', 0) / 1000:9.1f}s  mem_tree  {fmt_mem_tree(d)}"
    if isinstance(d, (dict, list)):
        d = json.dumps(d, separators=(',', ':'))
    text = f"{ev.get('t', 0) / 1000:9.1f}s  {ev.get('k')}"
    if d is not None:
        text += '  ' + str(d)[:300]
    return text


def summary_line(s):
    tl = s.timeline()
    kinds = defaultdict(int)
    for ev in tl:
        kinds[ev.get('k')] += 1
    flags = []
    if kinds.get('gl_context_lost'):
        flags.append(f"LOST x{kinds['gl_context_lost']}")
    if s.blocked:
        flags.append(f"BLOCKED {max(b.get('blocked_ms', 0) for b in s.blocked) / 1000:.0f}s")
    if s.unclean:
        flags.append('KILLED(visible)' if s.unclean.get('ended_visible') else 'ended(hidden)')
    if kinds.get('wasm_abort'):
        flags.append('ABORT')
    big = [ev for ev in tl if ev.get('k') == 'stall' and isinstance(ev.get('d'), dict)
           and (ev['d'].get('ms') or 0) >= 2000]
    if big:
        flags.append(f'stalls>=2s x{len(big)}')
    rebuilds = kinds.get('world_rebuild_begin', 0)
    return (f"{s.sid}  {short_ua(s.device):22s} {gpu_name(s)[:34]:34s} "
            f"rebuilds={rebuilds:<3d} heap={mb(s.heap_peak)} "
            f"gl_peak={mb(s.gl_peak)}  {' '.join(flags)}")


def print_timeline(s, around=None, before=12):
    tl = s.timeline()
    if around is None:
        for ev in tl:
            print('   ', fmt_event(ev))
        return
    idx = [i for i, ev in enumerate(tl) if is_incident(ev)]
    shown = set()
    for i in idx:
        lo = max(0, i - before)
        if lo > 0 and (lo - 1) not in shown:
            print('    ...')
        for j in range(lo, i + 1):
            if j not in shown:
                mark = '>>' if j == i else '  '
                print(f'  {mark}', fmt_event(tl[j]))
                shown.add(j)
    if s.blocked:
        for b in s.blocked:
            snap = (b.get('state') or {}).get('snapshot') or {}
            print(f"  >> watchdog: main thread blocked {b.get('blocked_ms', 0) / 1000:.1f}s; "
                  f"last world event {json.dumps(snap.get('world'))}, heap {mb(snap.get('heap'))}, "
                  f"gl {json.dumps(snap.get('gl'), separators=(',', ':'))}")
    if s.unclean:
        u = s.unclean
        snap = u.get('snapshot') or {}
        gap = (u.get('alive_wall') or 0) - (u.get('boot_wall') or 0)
        print(f"  >> session ended without pagehide ({'VISIBLE' if u.get('ended_visible') else 'hidden'}) "
              f"{gap / 1000:.0f}s after boot; last world event {json.dumps(snap.get('world'))}, "
              f"heap {mb(snap.get('heap'))}, gl {json.dumps(snap.get('gl'), separators=(',', ':'))}")


INPUT_KINDS = ('touch_js', 'touch', 'ui_click', 'menu_use')


def touch_rows(s):
    """One dict per finger the browser lifted, with the client's events after it."""
    rows = []
    cur = None
    stalls = []
    for ev in s.timeline():
        k = ev.get('k')
        if k == 'stall' and isinstance(ev.get('d'), dict):
            stalls.append(ev)
            if cur is not None and cur['touch'] is None:
                cur['stall'] = max(cur['stall'], ev['d'].get('ms') or 0)
            continue
        if k not in INPUT_KINDS:
            continue
        d = ev.get('d') if isinstance(ev.get('d'), dict) else {}
        if k == 'touch_js':
            cur = {'t': ev.get('t', 0), 'js': d, 'touch': None, 'clicks': [], 'menus': [], 'stall': 0}
            rows.append(cur)
            continue
        if cur is None:
            continue
        if k == 'touch':
            if cur['touch'] is None:
                cur['touch'] = d
            else:  # a second native lift for one browser lift: its own row
                cur = {'t': ev.get('t', 0), 'js': {}, 'touch': d, 'clicks': [], 'menus': [], 'stall': 0}
                rows.append(cur)
        elif k == 'ui_click':
            cur['clicks'].append(d)
        elif k == 'menu_use':
            cur['menus'].append(d)
    return rows


def touch_verdict(row):
    js = row['js']
    t = row['touch']
    if js.get('target') not in (None, 'canvas'):
        return ''  # a page button, not the game
    if t is None:
        return 'NO-CLIENT'
    out = t.get('out')
    if out == 'drag':
        return 'DRAG' if (t.get('travel') or 0) <= 3 * (t.get('slop') or 12) else ''
    if out == 'hold':
        return 'HOLD' if not row['menus'] else ''
    if out == 'multi':
        return 'MULTI' if (js.get('fingers') or 0) == 0 else ''
    hit = any(c.get('clicked', -1) >= 0 for c in row['clicks'])
    return '' if (row['menus'] or hit) else 'NOTHING'


def print_touch(s):
    rows = touch_rows(s)
    if not rows:
        print('    (no touch breadcrumbs: page or client older than the touch telemetry)')
        return
    counts = defaultdict(int)
    for row in rows:
        v = touch_verdict(row)
        t = row['touch'] or {}
        js = row['js']
        counts[t.get('out', 'none')] += 1
        if v:
            counts['flag:' + v] += 1
        click = row['clicks'][-1] if row['clicks'] else None
        menu = row['menus'][-1] if row['menus'] else None
        parts = [f"{row['t'] / 1000:9.1f}s",
                 f"{v or 'ok':9s}",
                 f"js {js.get('ms', '?')}ms {js.get('travel', '?')}pt"
                 f"{' CANCEL' if js.get('cancel') else ''} x{js.get('scale')}",
                 (f"-> {t.get('out')}/{t.get('btn')} {t.get('ms')}ms travel {t.get('travel')}px"
                  f"/slop {t.get('slop')} cross {t.get('cross_ms')}ms at {t.get('x')},{t.get('y')}")
                 if t else '-> (client saw nothing)']
        if click:
            parts.append(f"click com=0x{(click.get('com') or 0) & 0xffffffff:08x} hit={click.get('clicked')}")
        if menu:
            parts.append(f"menu '{menu.get('text')}' a={menu.get('action')} r={menu.get('result')}")
        if row['stall']:
            parts.append(f"STALL {row['stall']}ms")
        print('   ', '  '.join(parts))
    print('    totals', ' '.join(f'{k}={v}' for k, v in sorted(counts.items())))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('paths', nargs='*', default=['logs/telemetry'])
    ap.add_argument('--sid', help='print this session (a prefix will do) in full')
    ap.add_argument('--ua', help='only sessions whose user agent contains this')
    ap.add_argument('--incidents', action='store_true', help='timeline before each incident')
    ap.add_argument('--touch', action='store_true', help='one row per finger; lost taps marked')
    a = ap.parse_args()

    sessions = build(load(a.paths))
    rows = sorted(sessions.values(), key=lambda s: s.boot_wall or s.last_wall or 0)
    if a.ua:
        rows = [s for s in rows if s.device and a.ua.lower() in short_ua(s.device).lower()]
    if a.sid:
        rows = [s for s in rows if s.sid.startswith(a.sid)]
        for s in rows:
            print(summary_line(s))
            print('    device', json.dumps(s.device))
            print('    args  ', json.dumps(s.args))
            print_timeline(s)
            if s.blocked or s.unclean:
                print_timeline(s, around=True, before=0)
        return
    for s in rows:
        print(summary_line(s))
        if a.touch:
            print_touch(s)
        if a.incidents and (s.blocked or s.unclean or any(is_incident(ev) for ev in s.timeline())):
            print_timeline(s, around=True)


if __name__ == '__main__':
    main()
