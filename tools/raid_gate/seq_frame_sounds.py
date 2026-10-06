#!/usr/bin/env python3
"""seq_frame_sounds -- the sounds a sequence plays from its own frames.

A seq's FRAME sounds are played by the CLIENT, from the seq record, as each
frame is crossed (src/world/world_cycle.c World_EmitAnimFrameSound). The server
never sends them, so the tick log has no `sound` row for them: a test asserts a
frame sound through the `npc_anim` row that started the seq (its `seq` field)
and asserts a SCRIPT sound (`sound_synth`, `~sound_area`, an npc's
attack/defend/death sound) through the `sound` row. This prints, for each seq,
what its frames will play and when, so an author or the spec pass can name a
sound and the tick it lands on without opening the config.

    python3 tools/raid_gate/seq_frame_sounds.py <seq> [<seq> ...] [--tsv]

<seq> is a seq id (14399) or its cache name (maiden_spawn). Reads three files
and nothing else: OSRS-Content/osrs239-content/configs/all.seq (the records),
configs/all.seq.compack (id=name) and pack/4_soundeffects.pack (sound
id=name). A frame sound line is `sound=frame,id,loops,location,retain,weight`
(rscache dat2_config_sequence.h RSCache_Dat2ConfigFrameSound; tools/
gen_npc_combat.py s0). `frame` indexes the seq's frame list; the sound plays as
that frame is entered, which is the sum of the earlier frames' lengths in
client cycles after the animation starts (30 client cycles = one 600 ms tick).
A maya (skeletal) seq has no frame list here; its offsets are printed as
"maya" and its frame index is the animation's own.
"""
import argparse
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CONTENT = os.path.join(ROOT, "OSRS-Content", "osrs239-content")
SEQ = os.path.join(CONTENT, "configs", "all.seq")
SEQ_NAMES = os.path.join(CONTENT, "configs", "all.seq.compack")
SOUND_NAMES = os.path.join(CONTENT, "pack", "4_soundeffects.pack")
CYCLES_PER_TICK = 30


def load_id_names(path):
    by_id, by_name = {}, {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("//") or "=" not in line:
                continue
            key, name = line.split("=", 1)
            if not key.isdigit():
                continue
            by_id[int(key)] = name
            by_name[name] = int(key)
    return by_id, by_name


def load_records(names):
    """{name: {"frames": [length, ...], "sounds": [(frame, id, loops, location,
    retain, weight)], "maya": bool}} for the wanted names only."""
    wanted = set(names)
    out = {}
    current = None
    with open(SEQ, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith("[") and line.endswith("]"):
                name = line[1:-1]
                current = None
                if name in wanted:
                    current = out.setdefault(name, {"frames": [], "sounds": [], "maya": False})
                continue
            if current is None or "=" not in line:
                continue
            key, value = line.split("=", 1)
            if key == "frame":
                parts = value.split(",")
                current["frames"].append(int(parts[1]) if len(parts) > 1 else 0)
            elif key == "sound":
                current["sounds"].append(tuple(int(v) for v in value.split(",")))
            elif key == "mayaid":
                current["maya"] = True
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("seqs", nargs="+", help="seq ids or cache names")
    ap.add_argument("--tsv", action="store_true", help="one row per frame sound, tab separated")
    args = ap.parse_args()

    seq_by_id, seq_by_name = load_id_names(SEQ_NAMES)
    sound_by_id, _ = load_id_names(SOUND_NAMES)
    resolved = []
    for text in args.seqs:
        if text.isdigit():
            if int(text) not in seq_by_id:
                print(f"seq_frame_sounds: no seq {text} in {SEQ_NAMES}", file=sys.stderr)
                return 2
            resolved.append((int(text), seq_by_id[int(text)]))
        else:
            if text not in seq_by_name:
                print(f"seq_frame_sounds: no seq named {text} in {SEQ_NAMES}", file=sys.stderr)
                return 2
            resolved.append((seq_by_name[text], text))
    records = load_records([name for _, name in resolved])

    if args.tsv:
        print("seq\tseq_name\tframe\tcycle\ttick\tsound\tsound_name\tloops\tlocation\tretain\tweight")
    for seq_id, name in resolved:
        rec = records.get(name)
        if rec is None:
            print(f"seq_frame_sounds: {seq_id} {name} has no record in {SEQ}", file=sys.stderr)
            return 2
        frames = rec["frames"]
        total = sum(frames)
        if not args.tsv:
            length = (f"{len(frames)} frames, {total} cycles = {total / CYCLES_PER_TICK:.2f} ticks"
                      if frames else "maya (no frame list)")
            print(f"seq {seq_id} {name}: {length}; {len(rec['sounds'])} frame sound(s)")
        for frame, sound, loops, location, retain, weight in sorted(rec["sounds"]):
            if frames and frame < len(frames):
                cycle = sum(frames[:frame])
                cycle_text, tick_text = str(cycle), f"{cycle / CYCLES_PER_TICK:.2f}"
                tick_floor = str(cycle // CYCLES_PER_TICK)
            else:
                cycle_text, tick_text, tick_floor = "maya", "maya", "maya"
            sound_name = sound_by_id.get(sound, "?")
            if args.tsv:
                print(f"{seq_id}\t{name}\t{frame}\t{cycle_text}\t{tick_text}\t{sound}\t{sound_name}"
                      f"\t{loops}\t{location}\t{retain}\t{weight}")
            else:
                print(f"  frame {frame:>3}  cycle {cycle_text:>5}  tick +{tick_text} "
                      f"(npc_anim tick + {tick_floor})  sound {sound} {sound_name}  "
                      f"loops {loops} location {location} retain {retain} weight {weight}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
