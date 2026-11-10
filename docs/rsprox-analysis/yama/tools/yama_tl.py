#!/usr/bin/env python3
"""yama_tl.py <txt> <from> <to> [--raw]  -- compact tick timeline of the Yama domain.

Prints, per tick: boss/judge/flare npc-info blocks, sounds, projectiles, map anims,
locs, messages, varbits and the recorder's hits/seqs.
"""
import re, sys

HEADER = re.compile(r'^\[(\d+)\]\s*$')

def fights(path):
    pass

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    path, t0, t1 = args[0], int(args[1]), int(args[2])
    all_players = '--allp' in sys.argv
    keep = re.compile(r'yama|voidflare|judge|sound_area|synth_sound|map_anim|map_proj|projanim|loc_add|loc_del|loc_anim|message_game|\[hit\]|hitsplat|\[headbar\]|varbit|varp_|midi_|cam_|hint_arrow|if_settext|interface', re.I)
    tick = None
    cur_npc = None
    out = []
    with open(path, errors='replace') as f:
        for raw in f:
            m = HEADER.match(raw)
            if m:
                tick = int(m.group(1))
                continue
            if tick is None or tick < t0: continue
            if tick > t1: break
            line = raw.rstrip('\n')
            if not line.strip(): continue
            ind = len(line) - len(line.lstrip())
            s = line.strip()
            # skip noise
            if re.search(r'map_clock|date_|if_setcolour|update_stat|health_regen|clock \(8354', s): continue
            if ind == 4:
                cur_npc = None
                if re.match(r'\[(npc_info|player_info)', s):
                    continue
                if keep.search(s):
                    print(f'[{tick}] {s[:220]}')
            elif ind == 8:
                # entity block
                is_npc = 'npc=' in s
                cur_npc = s if is_npc else None
                if is_npc and re.search(r'yama|voidflare', s):
                    print(f'[{tick}]   {s[:200]}')
                elif not is_npc:
                    cur_npc = None
                    if s.startswith('[run]') or s.startswith('[walk]') or s.startswith('[teleport]'):
                        print(f'[{tick}]   {s[:180]}')
                    cur_me = True
            elif ind >= 12:
                if cur_npc and re.search(r'yama|voidflare', cur_npc):
                    print(f'[{tick}]       {s[:200]}')
                elif cur_npc is None and re.search(r'\[hit\]|\[sequence\]|\[spotanim\]|\[headbar\]|\[exact|\[facing\]|\[say\]', s):
                    print(f'[{tick}]       P {s[:200]}')

if __name__ == '__main__':
    main()
