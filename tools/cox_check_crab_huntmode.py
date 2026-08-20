#!/usr/bin/env python3
"""Pin huntmode/retaliate on the jewelled-crab npc forms in cox.npc.

Why this exists
----------------
The huntmode/retaliate trap has already bitten this package five separate
times (Lizardman shamans x2, Vanguards, Muttadiles, Vasa, Vespula -- see
docs/minigames/cox/COX_PLAN.md): a scripted-damage npc form left
huntmode=aggressive/retaliate=yes lets the engine's own attackrate-driven
swing land ALONGSIDE the script's own damage call, doubling every hit; an
engine-native form left huntmode=none never aggros at all.

There is no script-side getter for `huntmode` -- `npc_sethuntmode` sets it,
nothing reads it back (mock230_scripts.c has no NPC_GETHUNTMODE opcode; see
`SS_OP_NPC_SETHUNTMODE`'s own comment). That is the same shape
tools/cox_check_timers.py documents for `timer_interval`: no script opcode
can read the field back, so the pairing has to be checked statically, against
the source text, or not at all.

This checks two crab-room npc groups, by exact narrow name match (not a
prefix guess, so a stray `raids_lasercrabs_bigcrystal_something` npc line
would never be silently swept in):

  - raids_lasercrabs_crab_{grey,red,green,blue}: damage is engine-native (no
    queue*(combat_damage_player, ...) reachable from any crab ai_timer/
    ai_queue in cox_crabs.rs2) -- these MUST carry huntmode=aggressive AND
    retaliate=yes.
  - raids_lasercrabs_energy_{white,red,green,blue} (the beam): damage is
    entirely scripted (~cox_crab_beam_collide, from the beam's own
    [ai_timer]) -- these MUST NOT carry huntmode=aggressive, and MUST state
    retaliate=no explicitly (an absent retaliate line is not good enough:
    unstated retaliate is not textually provable to still default to "no"
    without re-deriving the engine's own default from this text alone, and a
    later stray `retaliate=yes` line should fail loudly rather than pass
    because the checker was lenient).

Usage:
    tools/cox_check_crab_huntmode.py [cox.npc path ...]   # default: cox.npc
    tools/cox_check_crab_huntmode.py --selftest           # prove the gate can fail
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT = [
    ROOT
    / "OSRS-Content/osrs239-content/server/scripts/minigames/minigame_cox/configs/cox.npc"
]

CRAB_FORMS = [
    "raids_lasercrabs_crab_grey",
    "raids_lasercrabs_crab_red",
    "raids_lasercrabs_crab_green",
    "raids_lasercrabs_crab_blue",
]
BEAM_FORMS = [
    "raids_lasercrabs_energy_white",
    "raids_lasercrabs_energy_red",
    "raids_lasercrabs_energy_green",
    "raids_lasercrabs_energy_blue",
]

BLOCK = re.compile(r"^\[([a-z_0-9]+)\]\s*$", re.M)


def parse_blocks(text: str):
    """name -> block body text, up to the next [name] header or EOF."""
    matches = list(BLOCK.finditer(text))
    blocks = {}
    for i, m in enumerate(matches):
        name = m.group(1)
        start = m.end()
        end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
        blocks[name] = text[start:end]
    return blocks


def check(paths) -> int:
    failures = 0
    for path in paths:
        text = path.read_text(encoding="utf-8", errors="replace")
        blocks = parse_blocks(text)

        for name in CRAB_FORMS:
            body = blocks.get(name)
            if body is None:
                print(f"MISSING BLOCK: [{name}] not found in {path}")
                failures += 1
                continue
            if not re.search(r"^huntmode=aggressive\s*$", body, re.M):
                print(f"BAD HUNTMODE: [{name}] must state huntmode=aggressive "
                      f"(engine-native damage, correction: was defaulting to "
                      f"MOCK230_HUNT_NONE, crabs never aggroed)")
                failures += 1
            if not re.search(r"^retaliate=yes\s*$", body, re.M):
                print(f"BAD RETALIATE: [{name}] must state retaliate=yes "
                      f"(engine-native damage, no scripted queue*(combat_damage_player, ...) "
                      f"reachable from its ai_timer/ai_queue)")
                failures += 1

        for name in BEAM_FORMS:
            body = blocks.get(name)
            if body is None:
                print(f"MISSING BLOCK: [{name}] not found in {path}")
                failures += 1
                continue
            if re.search(r"^huntmode=aggressive\s*$", body, re.M):
                print(f"BAD HUNTMODE: [{name}] must NOT state huntmode=aggressive "
                      f"(scripted damage -- an aggressive orb would let the engine's "
                      f"own attack clock throw a second, unscripted hit)")
                failures += 1
            if not re.search(r"^retaliate=no\s*$", body, re.M):
                print(f"BAD RETALIATE: [{name}] must explicitly state retaliate=no "
                      f"(scripted damage, from its own [ai_timer]'s "
                      f"queue*(combat_damage_player, ...))")
                failures += 1

        print(f"{path.name}: {len(CRAB_FORMS)} crab form(s), {len(BEAM_FORMS)} beam form(s) checked")
    return failures


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    paths = [Path(a) for a in args] if args else DEFAULT

    if "--selftest" in sys.argv:
        # A gate that cannot fail is not a gate.
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            broken = Path(tmp) / "cox.npc"
            text = paths[0].read_text(encoding="utf-8", errors="replace")
            mutated = text.replace(
                "[raids_lasercrabs_crab_red]\nhitpoints=5000\nattack=140\nstrength=140\ndefence=100\nparam=attackrate,4\nparam=attackrange,1\nparam=huntrange,^cox_crab_huntrange\nhuntmode=aggressive\n",
                "[raids_lasercrabs_crab_red]\nhitpoints=5000\nattack=140\nstrength=140\ndefence=100\nparam=attackrate,4\nparam=attackrange,1\nparam=huntrange,^cox_crab_huntrange\n",
                1,
            )
            if mutated == text:
                print("FAIL: mutation target text not found -- selftest cannot prove anything")
                return 1
            broken.write_text(mutated, encoding="utf-8")
            print("--- selftest: with huntmode=aggressive deleted from raids_lasercrabs_crab_red only ---")
            found = check([broken])
            if found != 1:
                print(f"FAIL: expected exactly 1 failure (the one block touched), got {found}")
                return 1
            print("OK: the gate flagged exactly the mutated block, no more, no less\n")

    failures = check(paths)
    if failures:
        print(f"\nRESULT: {failures} huntmode/retaliate mismatch(es).")
        return 1
    print("\nRESULT: every crab/beam form has the correct huntmode/retaliate pairing.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
