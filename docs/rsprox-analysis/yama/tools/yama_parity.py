#!/usr/bin/env python3
"""yama_parity.py -- every thing Jagex sends in a Yama fight, against what we send.

    python3 docs/rsprox-analysis/yama/tools/yama_parity.py \
        --rec ~/.rsprox-index-cache/home-default/.rsprox/binary/archive/idx{4498,4505,4578}.txt \
        --ours <scriptrun session dir>... [--all]

The recordings are the real-game sessions (index 4498, 4505, 4578: 31 fights; the Grid Master
sessions are left out, their recorders are seasonal). Each side is split into fights, Yama's add
to his removal. Every event in a fight is keyed:

    synth:<id>:<delay>          a synth_sound to the recorder (ours: seat 0)
    area:<id>:<delay>           a sound_area heard by the recorder (ours: seat 0's row)
    npcseq:<type>:<seq>:<delay> an npc's sequence, for the domain's types
    npcspot:<type>:<spot>       an npc's spotanim
    pseq:<seq>:<delay>          the recorder's own sequence (ours: seat 0)
    pspot:<spot>                the recorder's own spotanim
    mapanim:<id>                a map spotanim (once a tick per id: a wave is 514 tiles)
    proj:<id>                   a projectile
    loc:<id>                    a loc added or changed
    npcadd:<type>               an npc added (by type)
    say:<type>:<text>           an npc's overhead line
    msg:<text>                  a chat line (numbers -> #, player names -> <name>; ours from the
                                wirelog, both seats, so counted per raider)

and printed with its rate (events a fight) on each side and the offset of each event from the
last anchor before it (Yama's attack seq, his add, the Judge's death seq), mode and share. A row
is flagged REC-ONLY (recorded in 3+ fights, never ours), OURS-ONLY, RATE (a ratio outside 0.5..2)
or OFFSET (the modes differ and the recorded one holds half its events). The recorder's own kit
(spells, thralls, prayers, food, equipment) is listed apart as KIT: our bots wear the same kit, so
a KIT row is checked, but a gap there is the kit's, not Yama's.
"""
import argparse
import collections
import glob
import os
import re

YAMA, JUDGE, FLARE, METEOR, SURGE, THRONE = 14176, 14180, 14179, 14182, 13507, 123
DOMAIN_TYPES = {YAMA, JUDGE, FLARE, METEOR, SURGE, THRONE}
ATTACK_SEQS = {12144, 12145, 12146, 12147, 12148}
JUDGE_DEATH = 9042
KIT = re.compile(r'demonbane|thrall|resurrect|ghost|zombie|skeleton|human_eat|^eat$|liquid|'
                 r'drink|protect_from|mystic|sharp_eye|hawk_eye|eagle|retribution|cancel_prayer|'
                 r'prayer|equip|human_hit|sword_hit|hacksword|mark_of_darkness|xamphur|spellfail|'
                 r'splash|antivenom|venom|poison|karambwan|shark|manta|restore|staff_block|'
                 r'spellcast|imp_|leagues_6|longbow|waterstrike|fireblast|pick|clock_tick|'
                 r'light_creature|you drink|you eat|heals some|doses? of potion|finished your potion|'
                 r'barbarian|smokepuff|grim_reaper|halloween_death|staff|human_cast|selfimbue|'
                 r'contact_darkness|bind_impact|cast that|ayak|sword_stab|sword_slash|unarmedblock|'
                 r'human_block|hacksword|resurrection spells|wait before you can resurrect', re.I)

NAMES = {}


def norm_msg(text, names):
    text = re.sub(r'<p=\d>', '', text)
    for n in names:
        if n:
            text = text.replace(n, '<name>')
    text = re.sub(r'\d[\d,:]*', '#', text)
    return text


class Fight:
    def __init__(self, src, start):
        self.src = src
        self.start = start
        self.end = None
        self.events = []          # (tick, key)
        self.anchors = []         # ticks


def parse_rec(path):
    me = None
    names = set()
    tick = 0
    who = None
    fights = []
    cur = None
    pending_end = None
    for raw in open(path, errors='replace'):
        m = re.match(r'^\[(\d+)\]\s*$', raw)
        if m:
            tick = int(m.group(1))
            if cur and pending_end is not None and tick > pending_end:
                cur.end = pending_end
                fights.append(cur)
                cur = None
                pending_end = None
            continue
        if me is None:
            m = re.search(r'local player index: (\d+)', raw)
            if m:
                me = m.group(1)
            continue
        s = raw.strip()
        m = re.match(r'\[(idle|walk|run|teleport|telejump|crawl|add|del)\] player=\(index=(\d+), name=([^,]*),', s)
        if m:
            names.add(m.group(3))
            who = 'P' if m.group(2) == me else 'O'
            continue
        m = re.match(r'\[(idle|walk|run|teleport|telejump|crawl|add|del)\] npc=\(index=\d+, id=(\S+) \((\d+)\)', s)
        if m:
            kind, name, nid = m.group(1), m.group(2), int(m.group(3))
            NAMES.setdefault(('npc', nid), name)
            who = nid if nid in DOMAIN_TYPES else None
            if kind == 'add' and nid == YAMA and cur is None:
                cur = Fight(path, tick)
                cur.anchors.append(tick)
            if kind == 'del' and nid == YAMA and cur is not None:
                pending_end = tick
            if cur is not None and kind == 'add' and who is not None:
                cur.events.append((tick, 'npcadd:%d' % nid))
            continue
        if s.startswith('[player_info]') or s.startswith('[npc_info'):
            who = None
            continue
        if cur is None:
            continue

        def idn(text):
            mm = re.search(r'id=(\S+) \((-?\d+)\)', text)
            if not mm:
                return None, None
            return mm.group(1), int(mm.group(2))

        def num(field, text, default=0):
            mm = re.search(field + r'=(-?\d+)', text)
            return int(mm.group(1)) if mm else default

        ev = None
        if s.startswith('[synth_sound]'):
            name, i = idn(s)
            NAMES.setdefault(('snd', i), name)
            ev = 'synth:%d:%d' % (i, num('delay', s))
        elif s.startswith('[sound_area]'):
            name, i = idn(s)
            if i is not None:
                NAMES.setdefault(('snd', i), name)
                ev = 'area:%d:%d' % (i, num('delay', s))
        elif s.startswith('[map_anim]') or s.startswith('[map_anim_specific]'):
            name, i = idn(s)
            NAMES.setdefault(('spot', i), name)
            ev = 'mapanim:%d' % i
        elif s.startswith('[map_projanim'):
            name, i = idn(s)
            NAMES.setdefault(('spot', i), name)
            ev = 'proj:%d' % i
        elif s.startswith('[loc_add_change'):
            name, i = idn(s)
            NAMES.setdefault(('loc', i), name)
            # v3 is the zone's restatement as it comes back into view (every loc the zone
            # holds, on the recorder's teleport home): the engine's, listed as `locz`.
            ev = ('locz:%d' if s.startswith('[loc_add_change_v3]') else 'loc:%d') % i
        elif s.startswith('[message_game]'):
            mm = re.search(r'type=(\w+), message="(.*)"$', s)
            if mm and mm.group(1) not in ('npc_say', 'dialogue'):
                ev = 'msg:' + norm_msg(mm.group(2), names)
        elif s.startswith('[sequence]') and who is not None:
            name, i = idn(s)
            if i is not None and i >= 0:
                NAMES.setdefault(('seq', i), name)
                if who == 'P':
                    ev = 'pseq:%d:%d' % (i, num('delay', s))
                elif who != 'O':
                    ev = 'npcseq:%d:%d:%d' % (who, i, num('delay', s))
                    if who == YAMA and i in ATTACK_SEQS or who == JUDGE and i == JUDGE_DEATH:
                        cur.anchors.append(tick)
        elif s.startswith('[spotanim]') and who is not None:
            name, i = idn(s)
            if i is not None and i >= 0:
                NAMES.setdefault(('spot', i), name)
                if who == 'P':
                    ev = 'pspot:%d' % i
                elif who != 'O':
                    ev = 'npcspot:%d:%d' % (who, i)
        elif s.startswith('[say]') and who not in (None, 'P', 'O'):
            mm = re.search(r'text="(.*)"', s)
            if mm:
                ev = 'say:%d:%s' % (who, norm_msg(mm.group(1), names))
        if ev:
            cur.events.append((tick, ev))
    if cur is not None:
        cur.end = pending_end or tick
        fights.append(cur)
    return fights


def parse_ours(run):
    rows = []
    with open(os.path.join(run, 'ticklog.tsv'), errors='replace') as f:
        for line in f.read().split('\n')[1:]:
            r = line.split('\t')
            if len(r) > 9:
                rows.append(r)
    fights = []
    cur = None
    for r in rows:
        tick, kind = int(r[1]), r[2]
        a, b = r[3], r[4]
        c = r[5] if len(r) > 5 else '0'
        d = r[6] if len(r) > 6 else '0'
        label = r[9] if len(r) > 9 else ''
        if kind == 'npc_spawn' and int(b) == YAMA and cur is None:
            cur = Fight(run, tick)
            cur.anchors.append(tick)
        if cur is None:
            continue
        ev = None
        if kind == 'npc_free' and int(b) == YAMA:
            cur.end = tick
            fights.append(cur)
            cur = None
            continue
        if kind == 'sound' and a == '0':
            if label == 'area':
                ev = 'area:%s:%s' % (b, d)
            else:
                ev = 'synth:%s:%s' % (b, d)
        elif kind == 'npc_anim' and int(b) in DOMAIN_TYPES and int(c) >= 0:
            ev = 'npcseq:%s:%s:%s' % (b, c, d)
            if int(b) == YAMA and int(c) in ATTACK_SEQS or int(b) == JUDGE and int(c) == JUDGE_DEATH:
                cur.anchors.append(tick)
        elif kind == 'npc_spotanim' and int(b) in DOMAIN_TYPES and int(c) >= 0:
            ev = 'npcspot:%s:%s' % (b, c)
        elif kind == 'player_anim' and a == '0' and int(b) >= 0:
            ev = 'pseq:%s:%s' % (b, c)
        elif kind == 'player_spotanim' and a == '0' and int(b) >= 0:
            ev = 'pspot:%s' % b
        elif kind in ('map_spotanim', 'map_spotanim_specific'):
            ev = 'mapanim:%s' % b
        elif kind == 'projectile':
            ev = 'proj:%s' % d
        elif kind == 'loc_set':
            ev = 'loc:%s' % b
        elif kind == 'npc_spawn' and int(b) in DOMAIN_TYPES:
            ev = 'npcadd:%s' % b
        elif kind == 'npc_say' and int(b) in DOMAIN_TYPES:
            ev = 'say:%s:%s' % (b, norm_msg(label, []))
        if ev:
            cur.events.append((tick, ev))
    if cur is not None:
        cur.end = rows[-1] and int(rows[-1][1])
        fights.append(cur)
    # Chat lines from the wirelog: both seats share it, so each fight's count is halved.
    wire = os.path.join(run, 'wire.log')
    if os.path.exists(wire) and fights:
        names = set()
        for line in open(wire, errors='replace'):
            p = line.rstrip('\n').split('\t')
            if len(p) >= 4 and p[1] == 'message_game':
                # The wirelog numbers a tick one lower than the tick log (a synth in both: 158 / 159).
                t = int(p[0]) + 1
                for fi in fights:
                    if fi.start <= t <= (fi.end or t):
                        fi.events.append((t, 'msg2:' + norm_msg(p[3], ['d0' + x for x in '0123456789'])))
    return fights


def dedupe_mapanims(fights):
    for f in fights:
        seen = set()
        out = []
        for t, e in f.events:
            if e.startswith('mapanim:') or e.startswith('loc:'):
                if (t, e) in seen:
                    continue
                seen.add((t, e))
            out.append((t, e))
        f.events = out


def stats(fights):
    count = collections.Counter()
    infights = collections.Counter()
    offs = collections.defaultdict(collections.Counter)
    for f in fights:
        anchors = sorted(f.anchors)
        present = set()
        for t, e in f.events:
            key = e
            w = 1.0
            if e.startswith('msg2:'):
                key = 'msg:' + e[5:]
                w = 0.5
            count[key] += w
            present.add(key)
            prev = [a for a in anchors if a <= t]
            if prev:
                offs[key][t - prev[-1]] += 1
        for k in present:
            infights[k] += 1
    return count, infights, offs


def label(key):
    p = key.split(':')
    kind = p[0]
    try:
        if kind in ('synth', 'area'):
            return NAMES.get(('snd', int(p[1])), '?')
        if kind in ('npcseq', 'pseq'):
            i = int(p[2]) if kind == 'npcseq' else int(p[1])
            return NAMES.get(('seq', i), '?')
        if kind == 'locz':
            return NAMES.get(('loc', int(p[1])), '?')
        if kind in ('npcspot', 'pspot', 'mapanim', 'proj'):
            i = int(p[2]) if kind == 'npcspot' else int(p[1])
            return NAMES.get(('spot', i), '?')
        if kind == 'loc':
            return NAMES.get(('loc', int(p[1])), '?')
        if kind == 'npcadd':
            return NAMES.get(('npc', int(p[1])), '?')
    except ValueError:
        pass
    return ''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rec', nargs='+', required=True)
    ap.add_argument('--ours', nargs='+', required=True)
    ap.add_argument('--all', action='store_true', help='print matched rows too')
    args = ap.parse_args()
    rec = []
    for p in args.rec:
        rec += parse_rec(os.path.expanduser(p))
    ours = []
    for p in args.ours:
        for run in glob.glob(p):
            ours += parse_ours(run)
    dedupe_mapanims(rec)
    dedupe_mapanims(ours)
    nr, no = len(rec), len(ours)
    print('fights: recorded %d (%s), ours %d' % (nr, ', '.join(sorted({os.path.basename(f.src) for f in rec})), no))
    rc, rf, ro = stats(rec)
    oc, of, oo = stats(ours)
    rows = []
    for k in set(rc) | set(oc):
        r_rate = rc[k] / max(nr, 1)
        o_rate = oc[k] / max(no, 1)
        rmode = ro[k].most_common(1)[0] if ro[k] else (None, 0)
        omode = oo[k].most_common(1)[0] if oo[k] else (None, 0)
        rshare = rmode[1] / max(sum(ro[k].values()), 1)
        flag = ''
        if rc[k] and not oc[k]:
            if rf[k] >= 3:
                flag = 'REC-ONLY'
        elif oc[k] and not rc[k]:
            flag = 'OURS-ONLY'
        else:
            ratio = o_rate / r_rate if r_rate else 99
            if r_rate >= 1 and (ratio < 0.5 or ratio > 2):
                flag = 'RATE'
            elif rmode[0] is not None and omode[0] != rmode[0] and rshare >= 0.5 and rf[k] >= 3:
                flag = 'OFFSET'
        name = label(k)
        kit = bool(KIT.search(name) or KIT.search(k) or k.startswith('locz:'))
        rows.append((flag, kit, k, name, r_rate, rf[k], o_rate, of[k], rmode, rshare, omode))
    order = {'REC-ONLY': 0, 'OFFSET': 1, 'RATE': 2, 'OURS-ONLY': 3, '': 4}
    rows.sort(key=lambda r: (r[1], order[r[0]], -r[4], r[2]))
    print('%-9s %-48s %-34s %8s %5s %8s %5s  %s' % ('flag', 'key', 'name', 'rec/fgt', 'recF', 'ours/fgt', 'ourF', 'offset rec(share) / ours'))
    last_kit = None
    for flag, kit, k, name, r_rate, rfn, o_rate, ofn, rmode, rshare, omode in rows:
        if not flag and not args.all:
            continue
        if kit != last_kit:
            print('---- %s' % ('KIT (the recorder\'s own) and ENGINE (locz: zone restates)' if kit else 'YAMA'))
            last_kit = kit
        print('%-9s %-48s %-34s %8.2f %5d %8.2f %5d  %s(%.0f%%) / %s' % (
            flag, k[:48], name[:34], r_rate, rfn, o_rate, ofn, rmode[0], 100 * rshare, omode[0]))


if __name__ == '__main__':
    main()
