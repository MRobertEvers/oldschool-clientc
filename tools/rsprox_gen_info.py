#!/usr/bin/env python3
"""
Generate the stream prots' files under 3rd/rsprot/streams/ -- player_info,
npc_info_{small,large}_{v5,v6}, a .h and .c each, named and versioned as the
packets/ files are -- from RSProx's PlayerInfoClient.kt and NpcInfoClient.kt.

    tools/rsprox_gen_info.py             # write them, revisions 227-241
    tools/rsprox_gen_info.py --check     # fail if a checked-in one is stale

See 3rd/rsprot/streams/rsprot_info.h for the split between what is generated
here (flag values, block order, every read's byte transform, a flat block's
field order, the npc entering-view record) and what is hand-written C (the bit
sections and each block's shape).

## The templates are the contract

Every decode function maps to a KIND (FUNC_KIND) and every kind has a TEMPLATE:
the Kotlin variable names its reads land in, in order, and the RsprotInfoVar
each one fills. A program extracted from the Kotlin (tools/rsprox_info_extract.py)
must match its kind's template exactly -- same names, same order -- except for a
FLAT kind, whose reads may come in any order (that order is the one thing the
generated steps then carry). A structural kind also carries MUST, literal
fragments its body has to contain: the branch constants the C hard-codes
(0x7FFF, `flag and 0x2`, ...). A revision that fails either check is refused
with the reason, because the C would read it wrong.
"""

import argparse
from textwrap import wrap
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import rsprox_info_extract as ex  # noqa: E402
import rsprox_prot  # noqa: E402

OUT_DIR = os.path.join(REPO_ROOT, "3rd", "rsprot", "streams")
REVS = range(227, 242)

PRIM = {
    "g1": "G1", "g1Alt1": "G1_ALT1", "g1Alt2": "G1_ALT2", "g1Alt3": "G1_ALT3",
    "g1s": "G1S", "g1sAlt1": "G1S_ALT1", "g1sAlt2": "G1S_ALT2", "g1sAlt3": "G1S_ALT3",
    "g2": "G2", "g2Alt1": "G2_ALT1", "g2Alt2": "G2_ALT2", "g2Alt3": "G2_ALT3",
    "g2s": "G2S", "g2sAlt1": "G2S_ALT1", "g2sAlt2": "G2S_ALT2", "g2sAlt3": "G2S_ALT3",
    "g4": "G4", "g4Alt1": "G4_ALT1", "g4Alt2": "G4_ALT2", "g4Alt3": "G4_ALT3",
    "gSmart1or2": "SMART", "gSmart1or2null": "SMARTNULL", "gSmart2or4null": "SMART2OR4NULL",
    "gjstr": "JSTR",
    "gdata": "DATA", "gdataAlt1": "DATA_ALT1", "gdataAlt2": "DATA_ALT2", "gdataAlt3": "DATA_ALT3",
}

FUNC_KIND = {
    "decodeSay": "SAY",
    "decodeMoveSpeed": "MOVE_SPEED",
    "decodeTemporaryMoveSpeed": "TEMP_MOVE_SPEED",
    "decodeSequence": "SEQUENCE",
    "decodeFacePathingEntity": "FACE_PATHING_ENTITY",
    "decodeFaceAngle": "FACE_ANGLE",
    "decodeFaceCoord": "FACE_COORD",
    "decodeFacing": "FACING",
    "decodeNameExtras": "NAME_EXTRAS",
    "decodeChat": "CHAT",
    "decodeExactMove": "EXACT_MOVE",
    "decodeSpotanims": "SPOTANIM",
    "decodeSpotanimsOld": "SPOTANIM",
    "decodeSpotanim": "SPOTANIM",
    "decodeSpotanimOld": "SPOTANIM",
    "decodeHit": "HITS",
    "decodeHits": "HITS",
    "decodeHitsV1": "HITS",
    "decodeHitmark": "HITMARKS",
    "decodeHitmarks": "HITMARKS",
    "decodeHeadbar": "HEADBARS",
    "decodeHeadbars": "HEADBARS",
    "decodeTinting": "TINTING",
    "decodeTransparency": "TRANSPARENCY",
    "decodePlayerReset": "RESET",
    "decodeFreeze": "FREEZE",
    "decodeTransformation": "TRANSFORMATION",
    "decodeNameChange": "NAME_CHANGE",
    "decodeCombatLevelChange": "LEVEL_CHANGE",
    "decodeEnabledOps": "ENABLED_OPS",
    "decodeBaseAnimationSet": "BAS_CHANGE",
    "decodeBodyCustomisation": "CUSTOM_TYPED",
    "decodeBodyCustomisationV1": "CUSTOM_TYPED",
    "decodeBodyCustomisationV2": "CUSTOM_TYPED",
    "decodeHeadCustomisation": "HEAD_CUSTOM_TYPED",
    "decodeHeadCustomisationV1": "HEAD_CUSTOM_TYPED",
    "decodeHeadCustomisationV2": "HEAD_CUSTOM_TYPED",
    "decodeBodyCustomisationV3": "CUSTOM_V3",
    "decodeBodyCustomisationV4": "CUSTOM_V4",
    "decodeHeadiconCustomisation": "HEADICON_CUSTOM",
}

BAS_NAMES = ["turnLeftAnim", "turnRightAnim", "walkAnim", "walkAnimBack", "walkAnimLeft",
             "walkAnimRight", "runAnim", "runAnimBack", "runAnimLeft", "runAnimRight",
             "crawlAnim", "crawlAnimBack", "crawlAnimLeft", "crawlAnimRight", "readyAnim"]

# kind -> (flat?, [alternative templates: [(kotlin name, VAR)]], [MUST fragments])
TEMPLATES = {
    "SAY": (False, [[("value", "TEXT0")], [("text", "TEXT0")]], []),
    "NAME_CHANGE": (False, [[("text", "TEXT0")]], []),
    "MOVE_SPEED": (False, [[("value", "VALUE")]], []),
    "TEMP_MOVE_SPEED": (False, [[("value", "VALUE")]], []),
    "RESET": (False, [[("value", "VALUE")]], []),
    "TRANSFORMATION": (False, [[("id", "ID")]], []),
    "LEVEL_CHANGE": (False, [[("level", "LEVEL")]], []),
    "ENABLED_OPS": (False, [[("flag", "FLAG")]], []),
    "SEQUENCE": (True, [[("id", "ID"), ("delay", "DELAY")]], []),
    "FACE_PATHING_ENTITY": (False, [[("index", "INDEX"), ("index+", "INDEX_HI")]], ["shl 16"]),
    "FACE_ANGLE": (True, [[("value", "ANGLE")], [("angle", "ANGLE"), ("instant", "INSTANT")]], []),
    "FACE_COORD": (True, [[("x", "X"), ("z", "Z"), ("instant", "INSTANT")]], []),
    "FACING": (False, [[("flag", "FLAG"), ("entityTypeValue", "ENTITY_TYPE"), ("index", "INDEX"),
                        ("fallbackAngle", "FALLBACK_ANGLE"), ("x", "X"), ("z", "Z"),
                        ("bitpackedSize", "SIZE"), ("angle", "ANGLE")]],
               ["flag ushr 3 and 0x7", "flag ushr 6 and 0x1", "flag and 0x7"]),
    "NAME_EXTRAS": (False, [[("beforeName", "TEXT0"), ("afterName", "TEXT1"),
                             ("afterCombatLevel", "TEXT2")]], []),
    "CHAT": (False, [[("colourAndEffectsPacked", "COLOUR_EFFECTS"), ("modIcon", "MOD_ICON"),
                      ("autotyper", "AUTOTYPER"), ("huffmanLength", "DATA_LEN"), ("data", "DATA"),
                      ("array[]", "PATTERN")]],
             ["colour in 13..20", "colour - 12", "colourAndEffectsPacked ushr 8"]),
    "EXACT_MOVE": (True, [[("deltaX1", "DX1"), ("deltaZ1", "DZ1"), ("deltaX2", "DX2"),
                           ("deltaZ2", "DZ2"), ("delay1", "DELAY1"), ("delay2", "DELAY2"),
                           ("direction", "DIRECTION")]], []),
    "SPOTANIM": (False, [[("count", "COUNT"), ("slot", "SLOT"), ("id", "ID"),
                          ("heightAndDelay", "HEIGHT_DELAY")],
                         [("count", "COUNT"), ("slot", "SLOT"), ("id", "ID"),
                          ("heightAndDelay", "HEIGHT_DELAY"), ("loop", "LOOP")],
                         [("count", "COUNT"), ("slot", "SLOT"), ("id", "ID"),
                          ("heightAndDelay", "HEIGHT_DELAY"), ("loops", "LOOP")]], []),
    "HITS": (False, [[("hitCount", "COUNT"), ("type", "TYPE"), ("delay", "DELAY"),
                      ("mainType", "MAIN_TYPE"), ("value", "VALUE"), ("soakType", "SOAK_TYPE"),
                      ("soakValue", "SOAK_VALUE"), ("delay", "DELAY"), ("value", "VALUE"),
                      ("delay", "DELAY"), ("headbarCount", "COUNT2"), ("type", "TYPE"),
                      ("endTime", "END_TIME"), ("startTime", "START_TIME"),
                      ("startFill", "START_FILL"), ("endFill", "END_FILL")]],
             ["0x7FFE ->", "0x7FFF ->", "endTime == 0x7FFF", "endTime > 0"]),
    "HITMARKS": (False, [[("hitCount", "COUNT"), ("type", "TYPE"), ("value", "VALUE"),
                          ("delay", "DELAY"), ("limit", "LIMIT")]], []),
    "HEADBARS": (False, [[("headbarCount", "COUNT"), ("type", "TYPE"), ("endTime", "END_TIME"),
                          ("startTime", "START_TIME"), ("startFill", "START_FILL"),
                          ("endFill", "END_FILL")]], ["endTime == 0x7FFF", "endTime > 0"]),
    "TINTING": (True, [[("start", "START"), ("end", "END"), ("hue", "HUE"),
                        ("saturation", "SATURATION"), ("lightness", "LIGHTNESS"),
                        ("weight", "WEIGHT")]], []),
    "TRANSPARENCY": (True, [[("start", "START"), ("end", "END"), ("startTransparency", "ALPHA_START"),
                             ("endTransparency", "ALPHA_END"),
                             ("useStartTransparency", "USE_START")]], []),
    "FREEZE": (True, [[("delay", "DELAY"), ("duration", "DURATION"),
                       ("cancelSequence", "CANCEL")]], []),
    "BAS_CHANGE": (False, [[("flag", "FLAG")] + [(n, f"BAS0 + {i}") for i, n in enumerate(BAS_NAMES)]],
                   [f"flag and 0x{1 << i:x} != 0" for i in range(15)]),
    "CUSTOM_TYPED": (False, [[("flag", "FLAG"), ("count", "MODEL_COUNT"), ("modelId", "MODEL"),
                              ("recolours+", "RECOLOUR"), ("retextures+", "RETEXTURE"),
                              ("mirror", "MIRROR")]],
                     ["flag and 0x1 != 0", "flag and 0x2 != 0", "flag and 0x4 != 0",
                      "flag and 0x8 != 0", "flag and 0x10 != 0", "recoldest.size",
                      "retexdest.size"]),
    "CUSTOM_V3": (False, [[("flag", "FLAG"), ("count", "MODEL_COUNT"), ("modelId", "MODEL"),
                           ("count", "RECOLOUR_COUNT"), ("recolours+", "RECOLOUR"),
                           ("count", "RETEXTURE_COUNT"), ("retextures+", "RETEXTURE"),
                           ("bodyType", "BODY_TYPE"), ("identKitCount", "IDENTKIT_COUNT"),
                           ("identKit", "IDENTKIT")]],
                  ["flag and 0x1 != 0", "flag and 0x2 != 0", "flag and 0x4 != 0",
                   "flag and 0x8 != 0", "flag and 0x10 != 0", "flag and 0x20 != 0"]),
    "CUSTOM_V4": (False, [[("flag", "FLAG"), ("count", "MODEL_COUNT"), ("modelId", "MODEL"),
                           ("recolours", "RECOL_ALL_FLAG"), ("recolAll", "RECOL_ALL"),
                           ("count", "RECOLOUR_COUNT"), ("recolours+", "RECOLOUR"),
                           ("count", "RETEXTURE_COUNT"), ("retextures+", "RETEXTURE"),
                           ("bodyType", "BODY_TYPE"), ("identKitCount", "IDENTKIT_COUNT"),
                           ("identKit", "IDENTKIT")]],
                  ["flag and 0x1 != 0", "flag and 0x2 != 0", "flag and 0x4 != 0",
                   "flag and 0x8 != 0", "flag and 0x10 != 0", "flag and 0x20 != 0",
                   "recolAll == null || recolAll == 65535"]),
    "HEADICON_CUSTOM": (False, [[("flag", "FLAG"), ("groups[]", "GROUP"), ("indices[]", "INDEX")]],
                        ["for (i in 0..<8)", "flag and (1 shl i) != 0"]),
}

TEMPLATES["HEAD_CUSTOM_TYPED"] = TEMPLATES["CUSTOM_TYPED"]

# Reads a flat block's C ignores the value of but must still step over: none --
# every template entry above is a real field. Kept as the place to say so.


class Refused(Exception):
    pass


def match_template(kind, prog, body):
    flat, alts, must = TEMPLATES[kind]
    names = [n for n, _f, _a in prog]
    for frag in must:
        if frag not in re.sub(r"\s+", " ", body):
            raise Refused(f"{kind}: body lacks `{frag}`")
    for alt in alts:
        want = [n for n, _v in alt]
        if (sorted(names) == sorted(want)) if flat else (names == want):
            steps = []
            prims = {}
            for i, (name, fn, _arg) in enumerate(prog):
                if fn not in PRIM:
                    raise Refused(f"{kind}: read {fn} has no RsprotInfoPrim")
                if flat:
                    var = dict(alt)[name]
                else:
                    var = alt[i][1]
                # One variable, one prim: the structural C reads a variable
                # wherever its shape puts it, with the prim the table gives.
                if var in prims and prims[var] != PRIM[fn]:
                    raise Refused(f"{kind}: {name} read as {prims[var]} and {PRIM[fn]}")
                prims[var] = PRIM[fn]
                steps.append((var, PRIM[fn]))
            if not flat:
                # Structural: one step per variable is enough -- the C knows
                # where each is read.
                seen = set()
                steps = [s for s in steps if not (s[0] in seen or seen.add(s[0]))]
            return steps
    raise Refused(f"{kind}: reads {names} match no template")


def if_blocks(body, flag_var):
    """[(flag constant name, block text)] for `if (flag_var and NAME != 0) { ... }`,
    in order."""
    out = []
    pat = re.compile(r"if \(\(?" + flag_var + r" and (\w+)\)? != 0\)\s*\{")
    pos = 0
    while True:
        m = pat.search(body, pos)
        if not m:
            break
        depth = 0
        for q in range(m.end() - 1, len(body)):
            if body[q] == "{":
                depth += 1
            elif body[q] == "}":
                depth -= 1
                if depth == 0:
                    out.append((m.group(1), body[m.end():q]))
                    pos = q
                    break
    return out


def side(cls, rev, dispatch_fn, flag_var, header_fn):
    fns, consts = ex.load(cls, rev)
    blocks = []
    illegal = 0
    appearance = None
    for name, text in if_blocks(fns[dispatch_fn], flag_var):
        if name not in consts:
            raise Refused(f"{cls} {rev}: flag {name} has no constant")
        flag = consts[name]
        if name.startswith("EXTENDED_"):
            continue  # the header's continuation bits, where header and blocks share a function
        if "throw" in text or "error(" in text:
            illegal |= flag
            continue
        call = re.search(r"(decode\w+)\(", text)
        if "gdata" in text and "decodeAppearance" in text:
            lenm = re.search(r"val len = buffer\.(g\w+)\(\)", text)
            datam = re.search(r"buffer\.(gdata\w*)\(data\)", text)
            if not lenm or not datam:
                raise Refused(f"{cls} {rev}: appearance block not in the known shape")
            appearance = (PRIM[lenm.group(1)], PRIM[datam.group(1)])
            blocks.append((flag, "APPEARANCE", []))
            continue
        if not call:
            raise Refused(f"{cls} {rev}: block {name} calls no decoder")
        fname = call.group(1)
        kind = FUNC_KIND.get(fname)
        if kind is None:
            raise Refused(f"{cls} {rev}: {fname} has no kind")
        prog = ex.program(fns[fname])
        steps = match_template(kind, prog, fns[fname])
        blocks.append((flag, kind, steps))
    # The header: plain bytes, continued by the EXTENDED_* bits.
    head = fns[header_fn]
    hprog = [p for p in ex.program(head) if p[0] in ("flag", "flag+")]
    if any(f != "g1" for _n, f, _a in hprog):
        raise Refused(f"{cls} {rev}: extended-info flag bytes are not plain g1")
    ext = [consts[m.group(1)] for m in
           re.finditer(r"if \(\(?flag and (EXTENDED_\w+)\)? != 0\)*\s*\{\s*flag \+= buffer\.g1\(\)", head)]
    if len(ext) != len(hprog) - 1:
        raise Refused(f"{cls} {rev}: {len(hprog)} flag bytes but {len(ext)} continuation bits")
    m = re.search(r"if \(" + flag_var + r" and UNUSED_FLAGS != 0\)", head + fns[dispatch_fn])
    if m:
        illegal |= consts["UNUSED_FLAGS"]
    known = illegal
    for e in ext:
        known |= e
    for flag, _k, _s in blocks:
        known |= flag
    return {"ext": ext, "known": known, "illegal": illegal, "blocks": blocks,
            "appearance": appearance}


def lowres(rev):
    fns, _consts = ex.load("NpcInfoClient", rev)
    body = fns["processLowResolution"]
    if "val indexBitCount = 16" not in body or "indexBitCount + 12" not in body:
        raise Refused(f"npc {rev}: entering-view index is not 16 bits with a 12-bit lookahead")
    rec = body[body.index("npc.lastTransmitCycle = cycle"):body.index("npc.addRouteWaypoint(")]

    def fields(part):
        out = []
        for line in part.split("\n"):
            t = line.strip()
            if re.search(r"deltaX = decodeDelta", t):
                out.append("DX")
            elif re.search(r"deltaZ = decodeDelta", t):
                out.append("DZ")
            elif re.match(r"(?:val )?jump = buffer\.gBits\(1\)", t):
                out.append("JUMP")
            elif "NPC_TURN_ANGLES[buffer.gBits(3)]" in t:
                out.append("ANGLE")
            elif re.match(r"val hasSpawnCycle = buffer\.gBits\(1\) == 1", t):
                out.append("SPAWN_SEL" if "spawnClockBitcodes" in part else "SPAWN32")
            elif re.match(r"npc\.id = buffer\.gBits\(14\)", t):
                out.append("ID14")
            elif re.match(r"val idBitCount = buffer\.gBits\(2\)", t):
                out.append("ID_SEL")
            elif re.match(r"val extendedInfo = buffer\.gBits\(1\)", t):
                out.append("EXTENDED")
            elif "gBits" in t and not re.search(
                    r"spawnCycle = buffer\.gBits\((32|spawnClockBitcodes\[index\])\)|"
                    r"val index = buffer\.gBits\(2\)|npc\.id = buffer\.gBits\(typeBitcodes", t):
                raise Refused(f"npc {rev}: entering-view read not modelled: {t}")
        if sorted(out) not in (sorted(["DX", "DZ", "JUMP", "ANGLE", "SPAWN32", "ID14", "EXTENDED"]),
                               sorted(["DX", "DZ", "JUMP", "ANGLE", "SPAWN_SEL", "ID_SEL", "EXTENDED"])):
            raise Refused(f"npc {rev}: entering-view fields {out}")
        return out

    v5 = v6 = []
    if "if (version == 6) {" in rec:
        a = rec.index("if (version == 6) {")
        depth = 0
        for q in range(rec.index("{", a), len(rec)):
            if rec[q] == "{":
                depth += 1
            elif rec[q] == "}":
                depth -= 1
                if depth == 0:
                    break
        v6 = fields(rec[a:q])
        v5 = fields(rec[q:])
    else:
        v5 = fields(rec)
    names = {r["name"] for r in rsprox_prot.load_prots(rev)["server"]}
    if "NPC_INFO_SMALL_V5" not in names:
        v5 = []
    if "NPC_INFO_SMALL_V6" not in names:
        v6 = []
    src = ex.strip_comments(open(ex.client_path("NpcInfoClient", rev)).read())

    def arr(name):
        m = re.search(name + r"\s*(?::\s*IntArray)?\s*=\s*intArrayOf\(([^)]*)\)", src)
        return [int(v.strip()) for v in m.group(1).split(",") if v.strip()] if m else None

    spawn, types = arr("spawnClockBitcodes"), arr("typeBitcodes")
    if v6 and (not spawn or not types or len(spawn) != 4 or len(types) != 4):
        raise Refused(f"npc {rev}: V6 width selectors not found")
    return v5, v6, spawn or [0, 0, 0, 0], types or [0, 0, 0, 0]


def record_reader(name, fields, large, spawn, types):
    """One entering-view record, straight-line, in the revision's field order."""
    bits = 8 if large else 6
    sel = lambda arr: "{ " + ", ".join(str(v) for v in arr) + " }"
    body = []
    for f in fields:
        if f in ("DX", "DZ"):
            body.append(f"    r->{f.lower()} = rsprot_gbits(b, {bits});")
            body.append(f"    if (r->{f.lower()} >= {1 << (bits - 1)})")
            body.append(f"        r->{f.lower()} -= {1 << bits};")
        elif f == "JUMP":
            body.append("    r->jump = rsprot_gbits(b, 1);")
        elif f == "ANGLE":
            body.append("    r->angle = rsprot_gbits(b, 3);")
        elif f == "SPAWN32":
            body.append("    r->has_spawn_cycle = rsprot_gbits(b, 1);")
            body.append("    if (r->has_spawn_cycle)")
            body.append("        r->spawn_cycle = (uint32_t)rsprot_gbits(b, 32);")
        elif f == "SPAWN_SEL":
            body.append("    r->has_spawn_cycle = rsprot_gbits(b, 1);")
            body.append("    if (r->has_spawn_cycle)")
            body.append("        r->spawn_cycle = (uint32_t)rsprot_gbits(b, k_spawn_bits[rsprot_gbits(b, 2)]);")
        elif f == "ID14":
            body.append("    r->type = rsprot_gbits(b, 14);")
        elif f == "ID_SEL":
            body.append("    r->type = rsprot_gbits(b, k_type_bits[rsprot_gbits(b, 2)]);")
        elif f == "EXTENDED":
            body.append("    r->extended = rsprot_gbits(b, 1);")
    out = ["static void", f"{name}(RsprotBuf *b, RsprotNpcRecord *r)", "{"]
    if "SPAWN_SEL" in fields:
        out.append(f"    static const int k_spawn_bits[4] = {sel(spawn)};")
    if "ID_SEL" in fields:
        out.append(f"    static const int k_type_bits[4] = {sel(types)};")
    if len(out) > 3:
        out.append("")
    return out + body + ["}", ""]


def c_steps(steps):
    return ", ".join(f"{{ RSPROT_IV_{v}, RSPROT_IP_{p} }}" for v, p in steps)


# Each npc stream prot's file: (PROT, file stem, decoder class, which half of
# the revision's data it reads, delta width). PLAYER_INFO's files are its
# format generations (player_generations).
STREAMS = [
    ("NPC_INFO_SMALL_V5", "npc_info_small_v5", "NpcInfoClient", "v5", False),
    ("NPC_INFO_LARGE_V5", "npc_info_large_v5", "NpcInfoClient", "v5", True),
    ("NPC_INFO_SMALL_V6", "npc_info_small_v6", "NpcInfoClient", "v6", False),
    ("NPC_INFO_LARGE_V6", "npc_info_large_v6", "NpcInfoClient", "v6", True),
]
FIRST_VERSION = 100


def rev_ranges(revs):
    """[227, 228, 230] -> [(227, 228), (230, 230)]"""
    out = []
    for r in revs:
        if out and out[-1][1] == r - 1:
            out[-1] = (out[-1][0], r)
        else:
            out.append((r, r))
    return out


def rev_text(revs):
    return ", ".join(f"{lo}" if lo == hi else f"{lo}-{hi}" for lo, hi in rev_ranges(revs))


def side_c(prefix, s):
    """The block tables and the RsprotInfoSide for one layout."""
    out = []
    descs = []
    for i, (flag, kind, steps) in enumerate(s["blocks"]):
        arrn = f"{prefix}_{i}"
        if steps:
            out.append(f"static const RsprotInfoStep {arrn}[] = {{ {c_steps(steps)} }};")
            descs.append(f"    {{ 0x{flag:x}, RSPROT_IK_{kind}, {len(steps)}, {arrn} }},")
        else:
            descs.append(f"    {{ 0x{flag:x}, RSPROT_IK_{kind}, 0, NULL }},")
    out.append(f"static const RsprotInfoBlockDesc {prefix}_blocks[] = {{")
    out += descs
    out.append("};")
    e = list(s["ext"]) + [0] * (3 - len(s["ext"]))
    ap = s.get("appearance") or ("NONE", "NONE")
    out.append(f"static const RsprotInfoSide {prefix} = {{")
    out.append("    { " + ", ".join(f"0x{v:x}" for v in e) + " },")
    out.append(f"    0x{s['known']:x},")
    out.append(f"    0x{s['illegal']:x},")
    out.append(f"    {prefix}_blocks,")
    out.append(f"    (int)(sizeof({prefix}_blocks) / sizeof({prefix}_blocks[0])),")
    out.append(f"    RSPROT_IP_{ap[0]},")
    out.append(f"    RSPROT_IP_{ap[1]},")
    out.append("};")
    return out


def player_generations(revs):
    """[(n, [rev])]: PLAYER_INFO split where its set of block SHAPES changes.

    Jagex never versions the prot, but its format moves: 237 split the hits
    block, 238 added FACING, 239 FREEZE / RESET / TRANSPARENCY, 240 dropped
    the old face blocks. Each such set is a version, numbered from 1 in
    revision order -- player_info_v<n> -- and within it every revision still
    has its own flag values, block order and byte transforms."""
    gens = []
    for rev, d in revs:
        kinds = frozenset(kind for _flag, kind, _steps in d["player"]["blocks"])
        if gens and gens[-1][0] == kinds:
            gens[-1][1].append(rev)
        else:
            if any(g[0] == kinds for g in gens):
                raise Refused(f"player {rev}: block set returns after a change")
            gens.append((kinds, [rev]))
    return [(i + 1, g[1]) for i, g in enumerate(gens)]


def emit_stream(prot, stem, cls, half, large, revs, note=None):
    """(header text, source text) for one stream prot."""
    player = half is None
    # Layouts in order of first revision; a revision whose data matches an
    # earlier one's shares its version.
    versions = []  # [key, [revs], data]
    for rev, d in revs:
        if player:
            data = d["player"]
        elif d[half]:
            data = (d["npc"], d[half], d["spawn"], d["types"])
        else:
            continue
        key = repr(data)
        for v in versions:
            if v[0] == key:
                v[1].append(rev)
                break
        else:
            versions.append([key, [rev], data])
    if player:
        sig = ("RsprotPlayerInfo *pi, const uint8_t *data, int32_t len, RsprotInfoOp *ops, "
               "int cap, int *complete")
    else:
        sig = ("const uint8_t *data, int32_t len, RsprotNpcTypeLengths lengths, void *ctx, "
               "RsprotInfoOp *ops, int cap, int *complete")
    guard = f"RSPROT_STREAM_{stem.upper()}_H"
    banner = [f" * {prot} -- server -> client.",
              " *",
              f" * GENERATED by tools/rsprox_gen_info.py from RSProx ({cls}.kt;",
              f" * source: RSProx {rsprox_prot.source_hash()}) -- do not edit.",
              " *"]
    if player:
        banner += [f" * {note}", " *",
                   " * A layout is the extended-info tables; the bit sections and the 2048-slot",
                   " * table are the same at every revision (rsprot_info_player.c)."]
    else:
        banner += [" * A layout is the entering-view record -- its field order changes every",
                   f" * revision, and this prot's deltas are {8 if large else 6} bits -- and the extended-info",
                   " * tables; the rest of the stream is rsprot_info_npc.c."]
    banner += [" * Version numbers are local to this file and start at 100, the RSProx",
               " * numbering; a layout is numbered by the first revision that has it."]

    h = [f"#ifndef {guard}", f"#define {guard}", "", "/*"] + banner + [" */", "",
         '#include "rsprot_info.h"', ""]
    c = ["/*"] + banner + [" */", f'#include "{stem}.h"', '#include "rsprot_info_internal.h"', "",
                           "#include <stddef.h>", "", "/* clang-format off */"]
    fn_of = {}
    for i, (_key, vrevs, data) in enumerate(versions):
        vn = FIRST_VERSION + i
        fn = f"stream_{stem}_v{vn}_read"
        prefix = f"k_{stem}_v{vn}"
        for r in vrevs:
            fn_of[r] = fn
        h.append(f"/* v{vn} -- revs {rev_text(vrevs)}. */")
        h.append(f"int {fn}(\n    {sig});")
        c.append("")
        c.append(f"/* v{vn} -- revs {rev_text(vrevs)}. */")
        if player:
            c += side_c(prefix, data)
            c += ["", "int", f"{fn}(", f"    {sig})", "{",
                  f"    return rsprot_info_player_read(&{prefix}, pi, data, len, ops, cap, complete);",
                  "}"]
        else:
            npc, fields, spawn, types = data
            record = f"{stem}_v{vn}_record"
            c += record_reader(record, fields, large, spawn, types)
            c += side_c(prefix, npc)
            c += ["", "int", f"{fn}(", f"    {sig})", "{",
                  "    return rsprot_info_npc_read(",
                  f"        &{prefix}, {record}, data, len, lengths, ctx, ops, cap, complete);",
                  "}"]
    h.append("")
    h.append(f"/* Per-revision aliases: `stream_{stem}_rev{max(fn_of)}_read` is greppable. */")
    for r in sorted(fn_of):
        h.append(f"#define stream_{stem}_rev{r}_read {fn_of[r]}")
    range_t = "RsprotPlayerInfoRange" if player else "RsprotNpcInfoRange"
    h += ["", "/** Revision -> layout. */",
          f"extern const {range_t} rsprot_{stem}_read[];",
          f"extern const int rsprot_{stem}_read_count;", "", "#endif"]
    c.append("/* clang-format on */")
    c += ["", f"const {range_t} rsprot_{stem}_read[] = {{"]
    for i, (_key, vrevs, _data) in enumerate(versions):
        for lo, hi in rev_ranges(vrevs):
            c.append(f"    {{ {lo}, {hi}, stream_{stem}_v{FIRST_VERSION + i}_read }},")
    c += ["};", "",
          f"const int rsprot_{stem}_read_count =",
          f"    (int)(sizeof(rsprot_{stem}_read) / sizeof(rsprot_{stem}_read[0]));"]
    return "\n".join(h) + "\n", "\n".join(c) + "\n"


def emit(revs):
    """{path: text} for every stream prot's header and source."""
    files = {}
    for n, grevs in player_generations(revs):
        kinds = sorted({k for r, d in revs if r in grevs for _f, k, _s in d["player"]["blocks"]})
        note = (f"Format v{n}, revs {rev_text(grevs)}: blocks " + " ".join(kinds).lower() + ".")
        note = "\n * ".join(wrap(note, 74))
        h, c = emit_stream("PLAYER_INFO", f"player_info_v{n}", "PlayerInfoClient", None, False,
                           [(r, d) for r, d in revs if r in grevs], note)
        files[os.path.join(OUT_DIR, f"player_info_v{n}.h")] = h
        files[os.path.join(OUT_DIR, f"player_info_v{n}.c")] = c
    for prot, stem, cls, half, large in STREAMS:
        h, c = emit_stream(prot, stem, cls, half, large, revs)
        files[os.path.join(OUT_DIR, stem + ".h")] = h
        files[os.path.join(OUT_DIR, stem + ".c")] = c
    return files


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    revs = []
    failed = False
    for rev in REVS:
        try:
            d = {"player": side("PlayerInfoClient", rev, "decodeExtendedInfoBlocks", "flags",
                                "decodeExtendedInfo"),
                 "npc": side("NpcInfoClient", rev, "processExtendedInfo", "flag",
                             "processExtendedInfo")}
            d["v5"], d["v6"], d["spawn"], d["types"] = lowres(rev)
            revs.append((rev, d))
            print(f"rev {rev}: player {len(d['player']['blocks'])} blocks, "
                  f"npc {len(d['npc']['blocks'])} blocks, lowres v5={len(d['v5'])} v6={len(d['v6'])}",
                  file=sys.stderr)
        except Refused as e:
            print(f"REFUSED rev {rev}: {e}", file=sys.stderr)
            failed = True
    if failed:
        raise SystemExit(1)
    files = emit(revs)
    if args.check:
        stale = [p for p, text in files.items()
                 if not os.path.exists(p) or open(p).read() != text]
        if stale:
            raise SystemExit(f"{', '.join(stale)} stale; run tools/rsprox_gen_info.py")
        return
    for p, text in files.items():
        with open(p, "w") as fh:
            fh.write(text)


if __name__ == "__main__":
    main()
