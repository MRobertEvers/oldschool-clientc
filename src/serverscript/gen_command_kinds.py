#!/usr/bin/env python3
"""Generate the per-argument and return TYPE table for every engine command.

    python3 src/serverscript/gen_command_kinds.py [path-to-LostCity_Server]

Output (checked in; this never runs at build time):

    ssc_command_kinds.gen.h    opcode id -> declared argument types, return type

Source, inside the reference server:

    content/scripts/engine.rs2      `[command,name](args)(returns)`

This is the same file `gen_opcode_meta.py` derives arity from, read for the half
it throws away. `ss_meta.gen.h` keeps counts -- how many values a command pops
and pushes -- and drops the type each of those values was declared with, which
is exactly the knowledge a bare name needs: `inv_total(inv, shark)` declares its
second argument `obj`, and without that the resolver picks npc 1830 because NPC
sorts before OBJ. Every ad-hoc hint list in ssc_compile.c was a hand-copy of one
line of this file.

The words are emitted verbatim rather than mapped to SSC_SymbolKind here: the
mapping from a ScriptVarType to a pack namespace already exists once, in C
(`param_type_kind`), and a second copy in Python is a copy that stops agreeing.

Ids come from ss_opcode.h, which the sibling generator wrote from the same
checkout; a command engine.rs2 declares and ScriptOpcode.ts does not name is
reported and skipped.
"""

import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_REF = HERE.parent.parent.parent / "LostCity_Server"

# Commands this tree added itself (gen_opcode_meta.py EXTRA_OPCODES) whose
# arguments name a namespace a bare word must resolve in. engine.rs2 cannot
# declare them -- the reference has no such command -- so the declaration is
# stated here, in engine.rs2's own spelling, and merged as if it had been read
# from that file. Only commands whose argument TYPES matter belong here; an
# extra with no row keeps parse_command's hand-written hints.
EXTRA_DECLARATIONS = [
    "[command,if_setangle](component $component, int $xan, int $yan, int $zoom)",
    "[command,if_setrotatespeed](component $component, int $xspeed, int $yspeed)",
    "[command,spotanim_npc_slot](spotanim $spotanim, int $height, int $delay, int $slot)",
    "[command,spotanim_npc_slot_loop](spotanim $spotanim, int $height, int $delay, int $slot)",
    "[command,spotanim_pl_slot](spotanim $spotanim, int $height, int $delay, int $slot)",
    # A bare sound name collides with a spotanim of the same name (176
    # `shadow_blitz_impact` is also spotanim 381): untyped, the Maiden's storm
    # impact went out as sound 381, `creeper_attack`.
    "[command,sound_area](synth $sound, int $delay, coord $coord, int $range)",
    "[command,sound_area_size](synth $sound, int $delay, coord $coord, int $range, int $size)",
    "[command,npc_projanim_pl](coord $from, player_uid $to, spotanim $spotanim, int $fromHeight, int $toHeight, int $delay, int $duration, int $peak, int $arc)",
    "[command,npc_projanim_npc](coord $from, npc_uid $to, spotanim $spotanim, int $fromHeight, int $toHeight, int $delay, int $duration, int $peak, int $arc)",
    "[command,npc_projanim_map](coord $from, coord $to, spotanim $spotanim, int $fromHeight, int $toHeight, int $delay, int $duration, int $peak, int $arc)",
    "[command,npc_setdeathtype](npc $type)",
    "[command,npc_exactmove](coord $start, coord $end, int $start_cycle, int $end_cycle, int $direction)",
    "[command,npc_headbar_from](int $hp)",
    "[command,hpbar_hud_npc](npc_uid $npc)",
    "[command,npc_approach](coord $coord)",
    "[command,spotanim_map_specific](spotanim $spotanim, coord $coord, int $height, int $delay)",
    "[command,p_tint](int $start_cycle, int $end_cycle, int $hue, int $saturation, int $lightness, int $weight)",
    "[command,p_faceangle](int $angle, boolean $instant)",
    "[command,p_transparency](int $start_cycle, int $end_cycle, int $from, int $to, boolean $usestart)",
    "[command,loc_add_revert](coord $coord, loc $loc, int $angle, locshape $shape, int $duration, loc $revert, int $revert_angle)",
    "[command,loc_add_opflags](coord $coord, loc $loc, int $angle, locshape $shape, int $duration, int $opflags)",
    "[command,npc_transparency](int $start_cycle, int $end_cycle, int $from, int $to)",
    "[command,npc_headbar_timed](healthbar $bar, int $startfill, int $endfill, int $endtime)",
    "[command,npc_faceangle](int $angle, boolean $instant)",
    "[command,npc_facereset]",
    "[command,npc_basready](seq $seq)",
    "[command,npc_basreset]",
    "[command,npc_scriptedproj_pl](coord $from, player_uid $to, spotanim $spotanim, int $fromHeight, int $toHeight, int $delay, int $duration, int $curve_h, int $curve_a, int $curve_t)",
    "[command,npc_telejump](coord $coord)",
    "[command,npc_basturn](seq $left, seq $right)",
    "[command,npc_teleslide](coord $coord)",
    "[command,npc_baswalk](seq $seq)",
    "[command,npc_bodyhide]",
    "[command,npc_setlevel](int $level)",
    "[command,npc_facewalktype](int $type)",
    "[command,npc_facesquareinstant](coord $coord)",
    "[command,npc_facenpc](npc_uid $uid)",
    "[command,npc_faceplayer]",
    "[command,npc_faceplayerinstant]",
    "[command,p_dodgewindow](seq $roll, seq $strafe, int $from, int $to)",
    "[command,p_dodgezone](seq $roll, seq $strafe, int $from, int $to, coord $sw, coord $ne)",
    "[command,npc_routeapproach](coord $coord)",
    "[command,npc_tint](int $start_cycle, int $end_cycle, int $hue, int $saturation, int $lightness, int $weight)",
    "[command,hitmark_bar](int $uid, int $self, int $other, int $amount, int $healthbar, int $fill)",
    "[command,npc_facedir](coord $coord)",
    "[command,npc_setblockwalk](int $blockwalk)",
    "[command,cam_moveto_cycles](coord $coord, int $height, int $cycles, int $ignore_terrain, int $easing)",
    "[command,cam_rotateto_coordinate](coord $coord, int $height, int $cycles, int $easing)",
    "[command,npc_setbasestat](npc_stat $stat, int $level)",
]

DECL = re.compile(r"^\[command,(\.?)([A-Za-z0-9_]+)\]\s*(\([^)]*\))?\s*(\([^)]*\))?")


def opcode_ids(header: Path) -> dict:
    ids = {}
    for line in header.read_text().splitlines():
        match = re.match(r"#define SS_OP_([A-Z0-9_]+)\s+(\d+)", line)
        if match:
            ids[match.group(1).lower()] = int(match.group(2))
    return ids


def words(group: str) -> list:
    """`(inv $inv, obj $obj)` -> ['inv', 'obj']."""
    if not group:
        return []
    inner = group[1:-1].strip()
    if not inner:
        return []
    out = []
    for part in inner.split(","):
        part = part.strip()
        if not part:
            continue
        out.append(part.split()[0])
    return out


def main() -> int:
    ref = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_REF
    engine = ref / "content" / "scripts" / "engine.rs2"
    if not engine.exists():
        print(f"no engine.rs2 under {ref}", file=sys.stderr)
        return 1
    ids = opcode_ids(HERE / "ss_opcode.h")

    rows = {}
    missing = []
    for line in engine.read_text().splitlines() + EXTRA_DECLARATIONS:
        line = line.strip()
        match = DECL.match(line)
        if not match:
            continue
        name = match.group(2)
        opcode = ids.get(name)
        if opcode is None:
            missing.append(name)
            continue
        args = words(match.group(3))
        returns = words(match.group(4))
        # A `.name` form is the same opcode with the dot in its operand, so it
        # declares the same types; the first declaration wins and the second is
        # only checked for disagreement.
        previous = rows.get(opcode)
        if previous and previous != (name, args, returns):
            if previous[1] != args or previous[2] != returns:
                print(
                    f"warning: {name} redeclared with different types: "
                    f"{previous[1:]} vs {(args, returns)}",
                    file=sys.stderr,
                )
            continue
        rows[opcode] = (name, args, returns)

    lines = []
    lines.append("/*")
    lines.append(" * Generated by src/serverscript/gen_command_kinds.py from the LostCity")
    lines.append(" * reference server's content/scripts/engine.rs2. Do not edit by hand --")
    lines.append(" * re-run the generator.")
    lines.append(" *")
    lines.append(f" * {len(rows)} commands.")
    lines.append(" */")
    lines.append("")
    lines.append("#ifndef SRC_SERVERSCRIPT_SSC_COMMAND_KINDS_GEN_H")
    lines.append("#define SRC_SERVERSCRIPT_SSC_COMMAND_KINDS_GEN_H")
    lines.append("")
    lines.append("/* Declared types, in order, exactly as engine.rs2 spells them. A command")
    lines.append(" * with no declared argument or no declared return carries \"\". The")
    lines.append(" * ScriptVarType -> pack namespace mapping is param_type_kind's, in C. */")
    lines.append("struct SSC_CommandTypes")
    lines.append("{")
    lines.append("    int opcode;")
    lines.append("    const char* name;")
    lines.append("    /** Space-separated declared argument types, in order. */")
    lines.append("    const char* args;")
    lines.append("    /** The single declared return type, or \"\" when the command returns")
    lines.append("     *  nothing or more than one value -- a two-value return cannot type")
    lines.append("     *  the other side of a comparison. */")
    lines.append("    const char* result;")
    lines.append("};")
    lines.append("")
    lines.append("/* Sorted by opcode id. */")
    lines.append("static const struct SSC_CommandTypes g_ssc_command_types[] = {")
    for opcode in sorted(rows):
        name, args, returns = rows[opcode]
        result = returns[0] if len(returns) == 1 else ""
        lines.append(
            '    { %d, "%s", "%s", "%s" },' % (opcode, name, " ".join(args), result)
        )
    lines.append("};")
    lines.append("")
    lines.append("#endif")
    lines.append("")

    out = HERE / "ssc_command_kinds.gen.h"
    out.write_text("\n".join(lines))
    print(f"wrote {out} ({len(rows)} commands)")
    if missing:
        print(
            f"{len(missing)} declared commands have no SS_OP id: "
            + ", ".join(sorted(set(missing))),
            file=sys.stderr,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
