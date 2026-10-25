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
            blocks.append((flag, "APPEARANCE", [], "decodeAppearance"))
            continue
        if not call:
            raise Refused(f"{cls} {rev}: block {name} calls no decoder")
        fname = call.group(1)
        kind = FUNC_KIND.get(fname)
        if kind is None:
            raise Refused(f"{cls} {rev}: {fname} has no kind")
        prog = ex.program(fns[fname])
        steps = match_template(kind, prog, fns[fname])
        blocks.append((flag, kind, steps, fname))
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
    return {"ext": ext, "illegal": illegal, "blocks": blocks, "appearance": appearance}


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


def record_reader(name, fields, spawn, types):
    """One entering-view record, straight-line, in the revision's field order.
    The delta width is the prot's (SMALL 6, LARGE 8), so it is a parameter."""
    sel = lambda arr: "{ " + ", ".join(str(v) for v in arr) + " }"
    body = []
    for f in fields:
        if f in ("DX", "DZ"):
            body.append(f"    r->{f.lower()} = rsprot_info_npc_delta(b, delta_bits);")
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
    out = ["static void", f"{name}(RsprotBuf *b, int delta_bits, RsprotNpcRecord *r)", "{"]
    if "SPAWN_SEL" in fields:
        out.append(f"    static const int k_spawn_bits[4] = {sel(spawn)};")
    if "ID_SEL" in fields:
        out.append(f"    static const int k_type_bits[4] = {sel(types)};")
    if len(out) > 3:
        out.append("")
    return out + body + ["}", ""]


# --- the block readers each kind runs, and their encodings ------------------------

# kind -> (reader in rsprot_info_blocks.c, its encoding type or None). The
# reader is the shape; the revision fills the encoding.
READERS = {
    "SAY": ("rsprot_info_say", None),
    "NAME_CHANGE": ("rsprot_info_name_change", None),
    "NAME_EXTRAS": ("rsprot_info_name_extras", None),
    "MOVE_SPEED": ("rsprot_info_move_speed", "RsprotInfoValueEnc"),
    "TEMP_MOVE_SPEED": ("rsprot_info_temp_move_speed", "RsprotInfoValueEnc"),
    "RESET": ("rsprot_info_reset", "RsprotInfoValueEnc"),
    "TRANSFORMATION": ("rsprot_info_transformation", "RsprotInfoValueEnc"),
    "LEVEL_CHANGE": ("rsprot_info_level_change", "RsprotInfoValueEnc"),
    "ENABLED_OPS": ("rsprot_info_enabled_ops", "RsprotInfoValueEnc"),
    "SEQUENCE": ("rsprot_info_sequence", "RsprotInfoSequenceEnc"),
    "FACE_PATHING_ENTITY": ("rsprot_info_face_pathing_entity", "RsprotInfoFacePathingEntityEnc"),
    "FACE_ANGLE": ("rsprot_info_face_angle", "RsprotInfoFaceAngleEnc"),
    "FACE_ANGLE_INSTANT": ("rsprot_info_face_angle_instant", "RsprotInfoFaceAngleInstantEnc"),
    "FACE_COORD": ("rsprot_info_face_coord", "RsprotInfoFaceCoordEnc"),
    "FACING": ("rsprot_info_facing", "RsprotInfoFacingEnc"),
    "CHAT": ("rsprot_info_chat", "RsprotInfoChatEnc"),
    "EXACT_MOVE": ("rsprot_info_exact_move", "RsprotInfoExactMoveEnc"),
    "SPOTANIM": ("rsprot_info_spotanim", "RsprotInfoSpotanimEnc"),
    "SPOTANIM_LOOP": ("rsprot_info_spotanim_loop", "RsprotInfoSpotanimLoopEnc"),
    "HITS": ("rsprot_info_hits", "RsprotInfoHitsEnc"),
    "HITMARKS": ("rsprot_info_hitmarks", "RsprotInfoHitmarksEnc"),
    "HEADBARS": ("rsprot_info_headbars", "RsprotInfoHeadbarsEnc"),
    "TINTING": ("rsprot_info_tinting", "RsprotInfoTintingEnc"),
    "TRANSPARENCY": ("rsprot_info_transparency", "RsprotInfoTransparencyEnc"),
    "FREEZE": ("rsprot_info_freeze", "RsprotInfoFreezeEnc"),
    "APPEARANCE": ("rsprot_info_appearance", "RsprotInfoAppearanceEnc"),
    "BAS_CHANGE": ("rsprot_info_bas_change", "RsprotInfoBasChangeEnc"),
    "CUSTOM_TYPED": ("rsprot_info_custom_typed", "RsprotInfoCustomTypedEnc"),
    "HEAD_CUSTOM_TYPED": ("rsprot_info_head_custom_typed", "RsprotInfoCustomTypedEnc"),
    "CUSTOM_V3": ("rsprot_info_custom_v3", "RsprotInfoCustomEnc"),
    "CUSTOM_V4": ("rsprot_info_custom_v4", "RsprotInfoCustomEnc"),
    "HEADICON_CUSTOM": ("rsprot_info_headicon_custom", "RsprotInfoHeadiconCustomEnc"),
}
VALUE_KINDS = {"MOVE_SPEED", "TEMP_MOVE_SPEED", "RESET", "TRANSFORMATION", "LEVEL_CHANGE",
               "ENABLED_OPS"}


def shape(kind, steps):
    """The reader a block runs: its kind, split where RSProx has two shapes."""
    vars_ = {v for v, _p in steps}
    if kind == "SPOTANIM" and "LOOP" in vars_:
        return "SPOTANIM_LOOP"
    if kind == "FACE_ANGLE" and "INSTANT" in vars_:
        return "FACE_ANGLE_INSTANT"
    return kind


def c_read(prim):
    """`G2_ALT1` -> `rsprot_g2_alt1`."""
    special = {"SMART": "rsprot_gsmart1or2", "SMARTNULL": "rsprot_gsmart1or2null",
               "SMART2OR4NULL": "rsprot_gsmart2or4null"}
    return special.get(prim) or "rsprot_" + prim.lower()


def enc_init(kind, steps, side_appearance):
    """The encoding struct's initializer for one block."""
    if READERS[kind][1] is None:
        return None
    fields = []
    if kind == "APPEARANCE":
        lenp, datap = side_appearance
        return [f".data_len = {c_read(lenp)}", f".data_order = RSPROT_IP_{datap}"]
    seen = set()
    for v, p in steps:
        if v in seen:
            continue
        seen.add(v)
        if kind in VALUE_KINDS:
            fields.append(f".value = {c_read(p)}")
        elif v == "DATA":
            fields.append(f".data_order = RSPROT_IP_{p}")
        elif v.startswith("BAS0"):
            i = int(v.split("+")[1]) if "+" in v else 0
            fields.append(f".anims[{i}] = {c_read(p)}")
        else:
            fields.append(f".{v.lower()} = {c_read(p)}")
            if kind in ("CUSTOM_TYPED", "HEAD_CUSTOM_TYPED") and v == "MODEL":
                fields.append(".model_none = " + ("0xFFFF" if p.startswith("G2") else "-1"))
    return fields


def member(fname, shape_):
    """The struct member and flag name for RSProx's decode<Name>: name_snake.
    RSProx kept the name decodeSpotanim(s) when 240 added `loop` (the old shape
    became decodeSpotanim(s)Old), so that shape says so: spotanim_loop."""
    name = fname[len("decode"):] if fname.startswith("decode") else fname
    out = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", name).lower()
    return out + "_loop" if shape_ == "SPOTANIM_LOOP" else out


def version_kinds(rows, side_name):
    """The block shapes a version's revisions read, in first-seen order."""
    out = []
    for _r, d in rows:
        seen = []
        for _f, kind, steps, fname in d[side_name]["blocks"]:
            m = member(fname, shape(kind, steps))
            if m in seen:
                raise Refused(f"{side_name}: {fname} twice in one revision")
            seen.append(m)
            if (m, shape(kind, steps)) not in out:
                out.append((m, shape(kind, steps)))
    members = [m for m, _k in out]
    if len(set(members)) != len(members):
        raise Refused(f"{side_name}: one decoder reads two shapes across revisions: {out}")
    return out


def rev_ranges(revs):
    out = []
    for r in revs:
        if out and out[-1][1] == r - 1:
            out[-1] = (out[-1][0], r)
        else:
            out.append((r, r))
    return out


def rev_text(revs):
    return ", ".join(f"{lo}" if lo == hi else f"{lo}-{hi}" for lo, hi in rev_ranges(revs))


def player_generations(revs):
    """[(n, [rev])]: PLAYER_INFO split where its set of block SHAPES changes.

    Jagex never versions the prot, but its format moves: 237 split the hits
    block, 238 added FACING, 239 FREEZE / RESET / TRANSPARENCY, 240 dropped
    the old face blocks. Each such set is a version, numbered from 1 in
    revision order -- player_info_v<n> -- and within it every revision still
    has its own flag values, block order and byte transforms."""
    gens = []
    for rev, d in revs:
        kinds = frozenset(kind for _flag, kind, _steps, _fn in d["player"]["blocks"])
        if gens and gens[-1][0] == kinds:
            gens[-1][1].append(rev)
        else:
            if any(g[0] == kinds for g in gens):
                raise Refused(f"player {rev}: block set returns after a change")
            gens.append((kinds, [rev]))
    return [(i + 1, g[1]) for i, g in enumerate(gens)]


def banner(lines):
    return (["/*"] + [f" * {l}" if l else " *" for l in lines] +
            [" *", " * GENERATED by tools/rsprox_gen_info.py from RSProx "
             f"({rsprox_prot.source_hash()}) -- do not edit.", " */"])


def struct_lines(type_name, kinds, extra):
    out = [f"typedef struct {type_name}", "{",
           "    /** The flag word's continuation bits (0 ends them) and the flags RSProx",
           "     *  throws on. */",
           "    uint32_t ext[3];",
           "    uint32_t illegal;"]
    out += extra
    out += ["    /** The blocks the flag word names, in this revision's wire order (the",
            "     *  revision's generated dispatch). */",
            f"    int (*blocks)(RsprotInfoBlocks *t, const struct {type_name} *k);"]
    for m, k in kinds:
        enc = READERS[k][1]
        if enc:
            out.append(f"    {enc} {m};")
    out += [f"}} {type_name};"]
    return out


# Readers a stream runs in place of the shared one: the player's exact move
# also cancels the move its slot made this cycle.
PLAYER_READERS = {"EXACT_MOVE": "rsprot_info_player_exact_move"}


def dispatch_lines(name, type_name, blocks, player=False):
    """`name`: the revision's blocks, in wire order."""
    out = ["static int", f"{name}(RsprotInfoBlocks *t, const {type_name} *k)", "{"]
    used = False
    for _flag, kind, steps, fname in blocks:
        k = shape(kind, steps)
        fn, enc = READERS[k]
        if player:
            fn = PLAYER_READERS.get(k, fn)
        bit = f"BIT_{member(fname, k).upper()}"
        arg = f", &k->{member(fname, k)}" if enc else ""
        used = used or bool(enc)
        one = f"    if ((t->flag & {bit}) != 0 && !{fn}(t, {bit}{arg}))"
        if len(one) <= 100:
            out += [one, "        return 0;"]
        else:
            out += [f"    if ((t->flag & {bit}) != 0 &&", f"        !{fn}(t, {bit}{arg}))",
                    "        return 0;"]
    if not used:
        out.append("    (void)k;")
    out += ["    return 1;", "}"]
    return out


def const_lines(name, type_name, side, extra):
    """The revision's constants object."""
    e = list(side["ext"]) + [0] * (3 - len(side["ext"]))
    out = [f"static const {type_name} {name} = {{",
           "    .ext = { " + ", ".join(f"0x{v:x}" for v in e) + " },",
           f"    .illegal = 0x{side['illegal']:x},"]
    out += extra
    for _flag, kind, steps, fname in side["blocks"]:
        k = shape(kind, steps)
        init = enc_init(k, steps, side["appearance"])
        if init is not None:
            out.append(f"    .{member(fname, k)} = {{")
            out += [f"        {f}," for f in init]
            out.append("    },")
    out.append("};")
    return out


def flag_defines(side):
    """The revision's flag bits, by RSProx's decoder name. The file #undefs them
    (flag_undefs): every streams/ file shares one unity translation unit."""
    return ([f"/* {side_label(side)}'s flag bits. */"] +
            [f"#define BIT_{member(fname, shape(kind, steps)).upper()} 0x{flag:x}"
             for flag, kind, steps, fname in side["blocks"]])


def flag_undefs(side):
    return [f"#undef BIT_{member(fname, shape(kind, steps)).upper()}"
            for _flag, kind, steps, fname in side["blocks"]]


def side_label(side):
    return "The flag word"


# --- PLAYER_INFO ------------------------------------------------------------------------

def emit_player(revs):
    files = {}
    gens = player_generations(revs)
    public_h = ["#ifndef RSPROT_STREAM_PLAYER_INFO_H", "#define RSPROT_STREAM_PLAYER_INFO_H", ""]
    public_h += banner(["PLAYER_INFO -- server -> client: the per-revision entry points.", "",
                        "player_info_osrs<N> reads revision N's PLAYER_INFO through its version,",
                        "player_info_v<X> (a format generation: " +
                        "; ".join(f"v{n} {rev_text(g)}" for n, g in gens) + ")."])
    public_h += ["", '#include "rsprot_info.h"', ""]
    lookup = []
    for n, grevs in gens:
        rows = [(r, d) for r, d in revs if r in grevs]
        kinds = version_kinds(rows, "player")
        type_name = f"PlayerInfoV{n}"
        stem = f"player_info_v{n}"
        sig = "RsprotPlayerInfo *pi, const uint8_t *data, int32_t len, RsprotInfoList *out"
        h = [f"#ifndef RSPROT_STREAM_PLAYER_INFO_V{n}_H", f"#define RSPROT_STREAM_PLAYER_INFO_V{n}_H", ""]
        h += banner([f"PLAYER_INFO v{n} -- revs {rev_text(grevs)}: the version, its constants'",
                     "shape, and the reader each revision's entry point calls.", "",
                     ] + wrap("Its blocks: " + ", ".join(m for m, _k in kinds) + ".", 76))
        h += ["", '#include "rsprot_info_blocks.h"', ""]
        h += struct_lines(type_name, kinds, [])
        h += ["", f"int {stem}(", "    " + wrap_params(f"const {type_name} *k, " + sig) + ");", "", "#endif"]
        c = banner([f"PLAYER_INFO v{n} -- revs {rev_text(grevs)}. See {stem}.h."])
        c += [f'#include "{stem}.h"', "", "#include <assert.h>", "#include <string.h>", "",
              "int", f"{stem}(", "    " + wrap_params(f"const {type_name} *k, " + sig) + ")", "{",
              "    static int extended[RSPROT_INFO_PLAYER_SLOTS];",
              "    static RsprotPlayerPacket pp;",
              "    RsprotInfoBlocks t;", "    RsprotBuf b;", "    int count;", "",
              "    assert(k);", "    assert(pi);", "    assert(out);",
              "    rsprot_info_list_begin(out);",
              "    rsprot_buf_wrap_read(&b, data, len);",
              "    /* An unseeded table reads nothing: no GPI init has arrived. */",
              "    if (!pi->seeded) {",
              "        rsprot_info_stop(&b, out, RSPROT_STOP_ERROR, -1);",
              "        return out->count;",
              "    }",
              "    pp.pi = pi;",
              "    count = rsprot_info_player_sections(&pp, &b, out, extended);",
              "    memset(&t, 0, sizeof(t));", "    t.b = &b;", "    t.out = out;", "    t.player = &pp;",
              "    for (int i = 0; i < count && !b.err; i++) {",
              "        if (!rsprot_info_flags(&t, extended[i], k->ext, k->illegal))",
              "            break;",
              "        rsprot_info_player_target(out, pi, extended[i]);",
              "        if (!k->blocks(&t, k))",
              "            break;",
              "    }",
              "    return rsprot_info_finish(&b, out, len);", "}"]
        files[os.path.join(OUT_DIR, stem + ".h")] = "\n".join(h) + "\n"
        files[os.path.join(OUT_DIR, stem + ".c")] = "\n".join(c) + "\n"
        for rev, d in rows:
            rstem = f"player_info_osrs{rev}"
            rc = banner([f"PLAYER_INFO at revision {rev}: {stem}'s constants, and the entry point.",
                         "The flag bits and the dispatch's block order are this revision's, and so",
                         "is every field's byte transform; the shapes are rsprot_info_blocks.c."])
            rc += ['#include "player_info.h"', f'#include "{stem}.h"', ""]
            rc += flag_defines(d["player"]) + [""]
            rc += dispatch_lines(f"{rstem}_blocks", type_name, d["player"]["blocks"], player=True) + [""]
            rc += ["/* clang-format off */"]
            rc += const_lines(f"k_{rstem}", type_name, d["player"],
                              [f"    .blocks = {rstem}_blocks,"])
            rc += ["/* clang-format on */", "", "int", f"{rstem}(", "    " + wrap_params(sig) + ")", "{",
                   f"    return {stem}(&k_{rstem}, pi, data, len, out);", "}", ""]
            rc += flag_undefs(d["player"])
            files[os.path.join(OUT_DIR, rstem + ".c")] = "\n".join(rc) + "\n"
            public_h += [f"int {rstem}(", "    " + wrap_params(sig) + ");"]
            lookup.append((rev, rstem))
    public_h += ["", "/** player_info_osrs<revision>, or NULL outside 227-241. */",
                 "RsprotPlayerInfoReadFn player_info_osrs(int revision);", "", "#endif"]
    files[os.path.join(OUT_DIR, "player_info.h")] = "\n".join(public_h) + "\n"
    files[os.path.join(OUT_DIR, "player_info.c")] = "\n".join(
        banner(["The revision -> entry point lookup for PLAYER_INFO."]) +
        ['#include "player_info.h"', "", "#include <stddef.h>", "",
         "RsprotPlayerInfoReadFn", "player_info_osrs(int revision)", "{", "    switch (revision) {"] +
        [f"    case {r}: return {fn};" for r, fn in lookup] +
        ["    default: return NULL;", "    }", "}"]) + "\n"
    return files


# --- NPC_INFO --------------------------------------------------------------------------

NPC_VERSIONS = (("v5", "V5"), ("v6", "V6"))


def emit_npc(revs):
    files = {}
    public_h = ["#ifndef RSPROT_STREAM_NPC_INFO_H", "#define RSPROT_STREAM_NPC_INFO_H", ""]
    public_h += banner(["NPC_INFO_{SMALL,LARGE}_V5 / _V6 -- server -> client: the per-revision",
                        "entry points.", "",
                        "npc_info_<size>_<version>_osrs<N> reads revision N's prot through its",
                        "version, npc_info_<size>_<version> (npc_info_v5.c, npc_info_v6.c)."])
    public_h += ["", '#include "rsprot_info.h"', ""]
    sig = ("const uint8_t *data, int32_t len, RsprotNpcTypeLengths lengths, void *ctx, "
           "RsprotInfoList *out")
    lookups = {}
    per_rev = {}
    for ver, VER in NPC_VERSIONS:
        rows = [(r, d) for r, d in revs if d[ver]]
        vrevs = [r for r, _d in rows]
        kinds = version_kinds(rows, "npc")
        type_name = f"NpcInfo{VER}"
        stem = f"npc_info_{ver}"
        h = [f"#ifndef RSPROT_STREAM_NPC_INFO_{VER}_H", f"#define RSPROT_STREAM_NPC_INFO_{VER}_H", ""]
        h += banner([f"NPC_INFO_*_{VER} -- revs {rev_text(vrevs)}: the version, its constants' shape,",
                     "and the SMALL and LARGE readers each revision's entry points call. SMALL",
                     "and LARGE differ only in the entering-view record's delta width (6 or 8",
                     "bits).", "",
                     ] + wrap("Its blocks: " + ", ".join(m for m, _k in kinds) + ".", 76))
        h += ["", '#include "rsprot_info_blocks.h"', ""]
        h += struct_lines(type_name, kinds, [
            "    /** The entering-view record, in this revision's field order. */",
            "    RsprotNpcRecordRead record;"])
        for size in ("small", "large"):
            h += ["", f"int npc_info_{size}_{ver}(", "    " + wrap_params(f"const {type_name} *k, " + sig) + ");"]
        h += ["", "#endif"]
        c = banner([f"NPC_INFO_*_{VER} -- revs {rev_text(vrevs)}. See {stem}.h."])
        c += [f'#include "{stem}.h"', "", "#include <assert.h>", "#include <string.h>", "",
              "static int", f"{stem}_read(",
              "    " + wrap_params(f"const {type_name} *k, int delta_bits, " + sig) + ")", "{",
              "    static int extended[RSPROT_INFO_NPC_EXTENDED_MAX];",
              "    RsprotInfoBlocks t;", "    RsprotBuf b;", "    int count;", "",
              "    assert(k);", "    assert(out);",
              "    rsprot_info_list_begin(out);",
              "    rsprot_buf_wrap_read(&b, data, len);",
              "    count = rsprot_info_npc_sections(&b, delta_bits, k->record, out, extended);",
              "    memset(&t, 0, sizeof(t));", "    t.b = &b;", "    t.out = out;",
              "    t.lengths = lengths;", "    t.ctx = ctx;",
              "    for (int i = 0; i < count && !b.err; i++) {",
              "        if (!rsprot_info_flags(&t, extended[i], k->ext, k->illegal))",
              "            break;",
              "        rsprot_info_emit_value(out, RSPROT_OP_SET_EXT, (uint64_t)extended[i]);",
              "        if (!k->blocks(&t, k))",
              "            break;",
              "    }",
              "    return rsprot_info_finish(&b, out, len);", "}"]
        for size, bits in (("small", 6), ("large", 8)):
            c += ["", "int", f"npc_info_{size}_{ver}(", "    " + wrap_params(f"const {type_name} *k, " + sig) + ")", "{",
                  f"    return {stem}_read(k, {bits}, data, len, lengths, ctx, out);", "}"]
        files[os.path.join(OUT_DIR, stem + ".h")] = "\n".join(h) + "\n"
        files[os.path.join(OUT_DIR, stem + ".c")] = "\n".join(c) + "\n"
        for rev, d in rows:
            per_rev.setdefault(rev, []).append((ver, VER, type_name, stem, d))
    for rev in sorted(per_rev):
        rstem = f"npc_info_osrs{rev}"
        d = per_rev[rev][0][4]
        rc = banner([f"NPC_INFO at revision {rev}: the constants of the npc prot version(s) it",
                     "carries, and their entry points. The flag bits, the dispatch's block order,",
                     "every field's byte transform and the entering-view record's field order are",
                     "this revision's; the shapes are rsprot_info_blocks.c."])
        rc += ['#include "npc_info.h"'] + [f'#include "{stem}.h"' for _v, _V, _t, stem, _d in per_rev[rev]] + [""]
        rc += flag_defines(d["npc"]) + [""]
        for ver, VER, type_name, stem, d in per_rev[rev]:
            vstem = f"{rstem}_{ver}"
            rc += ["/* clang-format off */"]
            rc += record_reader(f"{vstem}_record", d[ver], d["spawn"], d["types"])
            rc += ["/* clang-format on */"]
            rc += dispatch_lines(f"{vstem}_blocks", type_name, d["npc"]["blocks"]) + [""]
            rc += ["/* clang-format off */"]
            rc += const_lines(f"k_{vstem}", type_name, d["npc"],
                              [f"    .record = {vstem}_record,", f"    .blocks = {vstem}_blocks,"])
            rc += ["/* clang-format on */"]
            for size in ("small", "large"):
                entry = f"npc_info_{size}_{ver}_osrs{rev}"
                rc += ["", "int", f"{entry}(", "    " + wrap_params(sig) + ")", "{",
                       f"    return npc_info_{size}_{ver}(",
                       f"        &k_{vstem}, data, len, lengths, ctx, out);",
                       "}"]
                public_h += [f"int {entry}(", "    " + wrap_params(sig) + ");"]
                lookups.setdefault(f"npc_info_{size}_{ver}", []).append((rev, entry))
            rc.append("")
        rc += flag_undefs(d["npc"])
        files[os.path.join(OUT_DIR, rstem + ".c")] = "\n".join(rc).rstrip() + "\n"
    public_h += [""]
    lc = banner(["The revision -> entry point lookups for NPC_INFO."]) + \
        ['#include "npc_info.h"', "", "#include <stddef.h>"]
    for prot in sorted(lookups):
        public_h += [f"/** {prot}_osrs<revision>, or NULL where the revision has no such prot. */",
                     f"RsprotNpcInfoReadFn {prot}_osrs(int revision);"]
        lc += ["", "RsprotNpcInfoReadFn", f"{prot}_osrs(int revision)", "{", "    switch (revision) {"]
        lc += [f"    case {r}: return {fn};" for r, fn in lookups[prot]]
        lc += ["    default: return NULL;", "    }", "}"]
    public_h += ["", "#endif"]
    files[os.path.join(OUT_DIR, "npc_info.h")] = "\n".join(public_h) + "\n"
    files[os.path.join(OUT_DIR, "npc_info.c")] = "\n".join(lc) + "\n"
    return files


def wrap_params(sig, width=92):
    """A parameter list broken between parameters, never inside one."""
    lines = [""]
    for p in sig.split(", "):
        piece = p if not lines[-1] else ", " + p
        if lines[-1] and len(lines[-1]) + len(piece) > width:
            lines[-1] += ","
            lines.append(p)
        else:
            lines[-1] += piece
    return "\n    ".join(lines)


def emit(revs):
    """{path: text} for every PLAYER_INFO / NPC_INFO stream file."""
    files = {}
    files.update(emit_player(revs))
    files.update(emit_npc(revs))
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
