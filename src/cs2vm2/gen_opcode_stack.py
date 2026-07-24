#!/usr/bin/env python3
"""Generate cs2vm2_opcode_stack.gen.h from cs2_opcode.h and cs2_opcode_meta.c."""

import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
OPCODE_H = HERE / "cs2_opcode.h"
META_C = HERE / "cs2_opcode_meta.c"
OUT = HERE / "cs2vm2_opcode_stack.gen.h"

MAX_OPCODE = 7602

MANUAL_STACK: dict[int, tuple[int, int, int, int]] = {
    106: (2, 0, 0, 0),  # CC_CREATECHILD
    107: (2, 0, 0, 0),  # CC_CREATESIBLING
    202: (0, 0, 1, 0),  # CC_FINDROOT
    203: (1, 0, 0, 0),  # CC_CHILDREN_FIND
    204: (0, 0, 1, 0),  # CC_CHILDREN_FINDNEXTID
    205: (2, 0, 0, 0),  # IF_CHILDREN_FIND
    # OC_* obj-config getters (4201/4202/4208/4210-4213/4217/4218/4222). Most
    # match the generic "OC_" heuristic below (pop item -> push int); listed
    # explicitly here anyway to keep this whole family's contracts in one place.
    # OC_OP/OC_IOP/OC_EXAMINE/OC_ISUBOP push a STRING, not an int — the plain
    # heuristic default would be wrong for these and did dispatch through
    # StackMetaStub as (1,0,1,0) [int out] before they got real handlers.
    4201: (1, 0, 0, 1),  # OC_OP(item) -> ground action string (slot = operand, not popped)
    4202: (1, 0, 0, 1),  # OC_IOP(item) -> inventory action string (slot = operand)
    4208: (1, 0, 1, 0),  # OC_PLACEHOLDER(item) -> placeholder id (stub: identity)
    4210: (0, 1, 1, 0),  # OC_FIND(query:str) -> match count (item-name search)
    4211: (0, 0, 1, 0),  # OC_FINDNEXT -> next matched item id (or -1 when exhausted)
    4212: (0, 0, 0, 0),  # OC_FINDRESET (clears the search state; no stack effect)
    4213: (1, 0, 1, 0),  # OC_SHIFTCLICKIOP(item) -> op index (stub: -1, no data)
    4217: (1, 0, 1, 0),  # OC_WEIGHT(item) -> weight (stub: 0, no data)
    4218: (1, 0, 0, 1),  # OC_EXAMINE(item) -> examine text (real data: obj.desc)
    4222: (3, 0, 0, 1),  # OC_ISUBOP(obj, opIndex, subIndex) -> sub-op string (stub: "")
    206: (0, 0, 1, 0),  # IF_CHILDREN_FINDNEXTID
    6200: (2, 0, 0, 0),  # VIEWPORT_SETFOV
    6201: (2, 0, 0, 0),  # VIEWPORT_SETZOOM
    6202: (4, 0, 0, 0),  # VIEWPORT_CLAMPFOV
    6203: (0, 0, 2, 0),  # VIEWPORT_GETEFFECTIVESIZE
    6204: (0, 0, 2, 0),  # VIEWPORT_GETZOOM
    6205: (0, 0, 2, 0),  # VIEWPORT_GETFOV
    # UI zoom (6210..6214; 6213 unconfirmed, left out). Dedicated dispatch in
    # cs2vm2.c forwards them to the host (CS2VM_HOST_REQUEST_UIZOOM).
    6210: (1, 0, 0, 0),  # UIZOOM_SET(value)
    6211: (0, 0, 1, 0),  # UIZOOM_GET -> value
    6212: (0, 0, 0, 0),  # UIZOOM_RESET
    6214: (0, 0, 1, 0),  # UIZOOM_GETDEFAULT -> value
    # Safe-area bounds (6220..6223, plus the 6231 alternate MAXY). Dedicated
    # dispatch in cs2vm2.c forwards them to the host
    # (CS2VM_HOST_REQUEST_SAFEAREA).
    6220: (0, 0, 1, 0),  # SAFEAREA_GETMINX -> value
    6221: (0, 0, 1, 0),  # SAFEAREA_GETMINY -> value
    6222: (0, 0, 1, 0),  # SAFEAREA_GETMAXX -> value
    6223: (0, 0, 1, 0),  # SAFEAREA_GETMAXY -> value
    6231: (0, 0, 1, 0),  # SAFEAREA_GETMAXY_ALT -> value
    # CAM_GETYAW: no-arg getter. Dedicated dispatch in cs2vm2.c forwards it to the
    # host (CS2VM_HOST_REQUEST_CAM_GETYAW), like CAM_GETFOLLOWHEIGHT.
    6232: (0, 0, 1, 0),
    3328: (0, 0, 1, 0),  # idle-time getter (script 5327 logout warning polls it)
    # Minimap zoom (7250..7254). Dedicated dispatch in cs2vm2.c forwards them to
    # the host (CS2VM_HOST_REQUEST_MINIMAP), so they never reach StackMetaStub —
    # these document the contracts. Setters pop one value; GETZOOM pushes the zoom
    # (polled by toplevel cc_setontimer scripts, 7052).
    7250: (1, 0, 0, 0),  # MINIMAP_SETZOOMABLE(flag)
    7252: (1, 0, 0, 0),  # MINIMAP_SETZOOM(zoom)
    7253: (0, 0, 1, 0),  # MINIMAP_GETZOOM -> zoom (2..8)
    7254: (1, 0, 0, 0),  # MINIMAP_SETICONZOOMLIMIT(limit)
    # MINIMENU_* (7100..7110): mouseover / right-click-menu queries, all no-arg
    # getters. Dedicated dispatch in cs2vm2.c forwards them to the host
    # (CS2VM_HOST_REQUEST_MINIMENU), so they never reach StackMetaStub — these just
    # document the contracts. MINIMENU_ENTRY pushes two strings (option, target);
    # the rest push one int (a bool for the FIND*/ISOPEN queries).
    7100: (0, 0, 1, 0),  # MINIMENU_TYPE:          -> 1 int (hovered target type)
    7101: (0, 0, 0, 2),  # MINIMENU_ENTRY:         -> 2 strings (option, target)
    7102: (0, 0, 1, 0),  # MINIMENU_FINDNPC:       -> bool
    7103: (0, 0, 1, 0),  # MINIMENU_FINDLOC:       -> bool
    7104: (0, 0, 1, 0),  # MINIMENU_FINDOBJ:       -> bool
    7105: (0, 0, 1, 0),  # MINIMENU_FINDPLAYER:    -> bool
    7108: (0, 0, 1, 0),  # MINIMENU_ISOPEN:        -> bool
    7109: (0, 0, 1, 0),  # MINIMENU_FINDCOMPONENT: -> bool
    7110: (0, 0, 1, 0),  # MINIMENU_NUMOPS:        -> 1 int (option count)
    # _7500..7503: newer host getters used by loot-tools formatter script 7603.
    # {0,0,0,0} left the args on the stack every call -> the stack grew until
    # PUSH_INT_LOCAL overflowed and the hook aborted (runaway cc_create/hover,
    # panel covering the viewport). Signatures derived from 7603's balanced
    # int/string stack trace (args pushed before the op, results popped after):
    7500: (3, 0, 1, 0),  # (id,int,int) -> 1 int
    7501: (0, 0, 1, 0),  # no-arg getter -> 1 int
    7502: (3, 0, 2, 2),  # (id,int,int) -> 2 ints + 2 strings
    7503: (2, 0, 1, 0),  # (id,int) -> 1 int (loop-count getter)
    # FRIEND_COUNT is a no-arg getter (total friends), but the name heuristic gave
    # it (1,0,1,0) like the indexed FRIEND_GET* ops -> the stub popped a non-existent
    # arg and underflowed, aborting the friends-list builder (script 125).
    3600: (0, 0, 1, 0),  # FRIEND_COUNT: no args -> 1 int
    2702: (1, 0, 1, 0),  # IF_HASSUB(component) -> bool; gates gameframe tab reveal (script 908)
    # LOGOUT: no args, no return -- triggers the client's logout flow (a request
    # kind the host just flags, since nothing drives an actual disconnect yet).
    5630: (0, 0, 0, 0),
    # HIGHLIGHT_LOC_* (7011..7014): scene-object highlight family, keyed by
    # (locTypeId, coordPacked, slot, group). Dedicated dispatch in cs2vm2.c
    # forwards them to the host (CS2VM_HOST_REQUEST_HIGHLIGHT, stubbed for now), so
    # these never reach StackMetaStub — the entries just document the real
    # contracts. See CS2VM2_Op_Highlight.
    7011: (4, 0, 0, 0),  # HIGHLIGHT_LOC_ON:   pop 4
    7012: (4, 0, 0, 0),  # HIGHLIGHT_LOC_OFF:  pop 4
    7013: (4, 0, 1, 0),  # HIGHLIGHT_LOC_GET:  pop 4 -> bool
    7014: (1, 0, 0, 0),  # HIGHLIGHT_LOC_CLEAR: pop 1
    # HIGHLIGHT_OBJ_* (7021..7025): ground-item highlight, same key shape as LOC.
    7021: (4, 0, 0, 0),  # HIGHLIGHT_OBJ_ON:   pop 4
    7022: (4, 0, 0, 0),  # HIGHLIGHT_OBJ_OFF:  pop 4
    7023: (4, 0, 1, 0),  # HIGHLIGHT_OBJ_GET:  pop 4 -> bool
    7025: (5, 0, 0, 0),  # HIGHLIGHT_OBJTYPE_SETUP: pop 5
    # Client-preference / mobile stub cluster (3130..3135). Confirmed against the
    # Kronos client (Messages.java): each just discards N ints off the stack and
    # does nothing — a host no-op is enough for fidelity, so only the pop count
    # matters. Without these the (0,0,0,0) default left the args on the stack and
    # StackMetaStub aborted on the unknown signature.
    3130: (2, 0, 0, 0),  # _3130: pop 2 ints, discard
    3131: (1, 0, 0, 0),  # _3131: pop 1 int, discard
    3133: (1, 0, 0, 0),  # MOBILE_SETFPS(fps): pop 1 int, discard
    3134: (0, 0, 0, 0),  # MOBILE_OPENSTORE: no args, no-op (marked known)
    3135: (2, 0, 0, 0),  # MOBILE_OPENSTORECATEGORY: pop 2 ints, discard
    # Audio volume (3203..3208) + client/game/device options (3209..3217).
    # Dedicated dispatch in cs2vm2.c forwards them to the host
    # (CS2VM_HOST_REQUEST_CLIENT_OPTION), so they never reach StackMetaStub — these
    # document the contracts. Volume setters take just a value; the OPTION families
    # are keyed by an option id (SET pops id+value, GET pops id, GETRANGE pops id
    # and pushes min+max).
    3203: (1, 0, 0, 0),  # SETVOLUMEMUSIC(value)
    3204: (0, 0, 1, 0),  # GETVOLUMEMUSIC -> value
    3205: (1, 0, 0, 0),  # SETVOLUMESOUNDS(value)
    3206: (0, 0, 1, 0),  # GETVOLUMESOUNDS -> value
    3207: (1, 0, 0, 0),  # SETVOLUMEAREASOUNDS(value)
    3208: (0, 0, 1, 0),  # GETVOLUMEAREASOUNDS -> value
    3209: (2, 0, 0, 0),  # CLIENTOPTION_SET(id, value)
    3210: (1, 0, 1, 0),  # CLIENTOPTION_GET(id) -> value
    3212: (2, 0, 0, 0),  # DEVICEOPTION_SET(id, value)
    3213: (2, 0, 0, 0),  # GAMEOPTION_SET(id, value)
    3214: (1, 0, 1, 0),  # DEVICEOPTION_GET(id) -> value
    3215: (1, 0, 1, 0),  # GAMEOPTION_GET(id) -> value
    3217: (1, 0, 2, 0),  # DEVICEOPTION_GETRANGE(id) -> min, max
    # CLIENTOP_* (6700..6709): enhanced client-side context-menu hooks. SET pops
    # (slot, scriptId) + string label; DEL pops slot. Dedicated dispatch in
    # cs2vm2.c forwards them to the host (CS2VM_HOST_REQUEST_CLIENTOP, stubbed),
    # so they never reach StackMetaStub — these document the contracts.
    6700: (2, 1, 0, 0),  # CLIENTOP_NPC_SET(slot, scriptId) + label
    6701: (1, 0, 0, 0),  # CLIENTOP_NPC_DEL(slot)
    6702: (2, 1, 0, 0),  # CLIENTOP_LOC_SET(slot, scriptId) + label
    6703: (1, 0, 0, 0),  # CLIENTOP_LOC_DEL(slot)
    6704: (2, 1, 0, 0),  # CLIENTOP_OBJ_SET(slot, scriptId) + label
    6705: (1, 0, 0, 0),  # CLIENTOP_OBJ_DEL(slot)
    6706: (2, 1, 0, 0),  # CLIENTOP_PLAYER_SET(slot, scriptId) + label
    6707: (1, 0, 0, 0),  # CLIENTOP_PLAYER_DEL(slot)
    6708: (2, 1, 0, 0),  # CLIENTOP_TILE_SET(slot, scriptId) + label
    6709: (1, 0, 0, 0),  # CLIENTOP_TILE_DEL(slot)
    # CC_SETOPFORCELEFTCLICK / CC_OP1309 / CLEAROPSUBMENU / SETOPSUBMENU /
    # SETTARGETPRIORITY (1308..1312). Dedicated dispatch in cs2vm2.c; SETPINCH
    # was wrongly numbered 1308 and is now 1004.
    1308: (1, 0, 0, 0),  # CC_SETOPFORCELEFTCLICK(flag)
    1309: (1, 0, 0, 0),  # CC_OP1309: pop 1, discard
    1310: (1, 0, 0, 0),  # CC_CLEAROPSUBMENU(opIndex)
    1311: (2, 1, 0, 0),  # CC_SETOPSUBMENU(opIndex, subIndex) + text
    1312: (1, 0, 0, 0),  # CC_SETTARGETPRIORITY(priority)
    1004: (1, 0, 0, 0),  # CC_SETPINCH(flag)
    2309: (2, 0, 0, 0),  # IF_OP2309: pop component + 1 int, discard
    # NOTE: CAM_SETFOLLOWHEIGHT (5530) / CAM_GETFOLLOWHEIGHT (5531) are NOT stubbed
    # here — they have dedicated dispatch cases in cs2vm2.c that hand off to the
    # host (rs_cs2_host.c stores/returns host->cam_follow_height), so they never
    # reach StackMetaStub.
}


def parse_stack_line(line: str) -> int:
    rhs = line.split(":", 1)[1]
    if "-" in rhs and not re.search(r"[a-zA-Z0-9_]+", rhs.replace("-", "").strip()):
        return 0
    parts = [p.strip() for p in rhs.split(",") if p.strip() and p.strip() != "-"]
    return len(parts)


def parse_opcode_h() -> tuple[dict[int, tuple[int, int, int, int]], set[int]]:
    """Returns (entries, documented). `documented` is every opcode carrying a
    stack doc comment, including ones whose counts are all zero — those are
    otherwise indistinguishable from "no comment at all" and would be handed to
    the name heuristics, which guess wrong for e.g. SETKEYINPUTMODE_ALL."""
    text = OPCODE_H.read_text()
    entries: dict[int, tuple[int, int, int, int]] = {}
    documented: set[int] = set()
    pattern = re.compile(r"/\*(.*?)\*/\s*#define\s+CS2_OP_[A-Z0-9_]+\s+(\d+)", re.DOTALL)
    for m in pattern.finditer(text):
        comment, num_s = m.group(1), m.group(2)
        num = int(num_s)
        int_in = str_in = int_out = str_out = 0
        saw_stack_line = False
        for line in comment.split("\n"):
            line = line.strip().lstrip("*").strip()
            if line.startswith("int stack in:"):
                int_in = parse_stack_line(line)
                saw_stack_line = True
            elif line.startswith("str stack in:"):
                str_in = parse_stack_line(line)
                saw_stack_line = True
            elif line.startswith("int stack out:"):
                int_out = parse_stack_line(line)
                saw_stack_line = True
            elif line.startswith("str stack out:"):
                str_out = parse_stack_line(line)
                saw_stack_line = True
        entries[num] = (int_in, str_in, int_out, str_out)
        if saw_stack_line:
            documented.add(num)
    return entries, documented


def parse_meta_names() -> dict[int, str]:
    text = META_C.read_text()
    names: dict[int, str] = {}
    for m in re.finditer(r"\[(\d+)\]\s*=\s*\{\s*\"([^\"]+)\"", text):
        names[int(m.group(1))] = m.group(2)
    return names


def heuristic(name: str) -> tuple[int, int, int, int] | None:
    if name in ("POP_VAR", "POP_VARBIT"):
        return (1, 0, 0, 0)
    if name == "DEFINE_ARRAY":
        return (1, 0, 0, 0)
    if name == "PUSH_ARRAY_INT":
        return (1, 0, 1, 0)
    if name == "POP_ARRAY_INT":
        return (2, 0, 0, 0)
    if name in ("SETBIT", "CLEARBIT", "TESTBIT", "OR", "AND", "INVPOW"):
        return (2, 0, 1, 0)
    if name == "SCALE":
        return (3, 0, 1, 0)
    if name == "RANDOM":
        return (0, 0, 1, 0)
    if name == "RANDOMINC":
        return (1, 0, 1, 0)
    if name == "INTERPOLATE":
        return (5, 0, 1, 0)
    if name == "GETBIT_RANGE":
        return (2, 0, 1, 0)
    if name == "COMPARE":
        return (0, 2, 1, 0)
    if name == "SUBSTRING":
        return (2, 1, 0, 1)
    if name == "STRING_LENGTH":
        return (0, 1, 1, 0)
    if name == "APPEND":
        return (1, 1, 0, 1)
    if name in ("LOWERCASE", "REMOVETAGS", "FROMDATE"):
        return (0, 1, 0, 1)
    if name == "STRING_INDEXOF_STRING":
        return (1, 2, 1, 0)
    if name == "STRING_INDEXOF_CHAR":
        return (2, 1, 1, 0)
    if name == "STRUCT_PARAM":
        return (2, 0, 1, 0)
    if name in ("ON_MOBILE", "CLIENTTYPE", "COORD", "RUNWEIGHT_VISIBLE", "RUNENERGY_VISIBLE"):
        return (0, 0, 1, 0)
    if name in ("CLIENTCLOCK", "REBOOTTIMER"):
        return (0, 0, 1, 0)
    if name in ("MOUSE_GETX", "MOUSE_GETY"):
        return (0, 0, 1, 0)
    if name == "GETCANVASSIZE":
        return (0, 0, 2, 0)
    if name == "GETWINDOWMODE":
        return (0, 0, 1, 0)
    if name == "GETDEFAULTWINDOWMODE":
        return (0, 0, 1, 0)
    if name == "IF_GETTOP":
        return (0, 0, 1, 0)
    if name == "IF_FIND":
        return (1, 0, 1, 0)
    if name.startswith("CC_GET"):
        if name in ("CC_GETTEXT", "CC_GETOP", "CC_GETOPBASE"):
            return (0, 0, 0, 1)
        return (0, 0, 1, 0)
    if name.startswith("IF_GET"):
        if name in ("IF_GETOP",):
            return (2, 0, 0, 1)
        if name in ("IF_GETOPBASE",):
            return (1, 0, 0, 1)
        if name == "IF_GETTEXT":
            return (1, 0, 0, 1)
        return (1, 0, 1, 0)
    if name.startswith("OC_"):
        return (1, 0, 1, 0)
    if name.startswith("STAT"):
        return (1, 0, 1, 0)
    if name.startswith("INVOTHER_"):
        return (2, 0, 1, 0)
    if name.startswith("FRIEND_") or name.startswith("CLAN_"):
        return (1, 0, 0, 1) if "NAME" in name else (1, 0, 1, 0)
    if name.startswith("STOCKMARKET_"):
        return (1, 0, 1, 0)
    if name.startswith("CHAT_"):
        if name == "CHAT_PLAYERNAME":
            return (0, 0, 0, 1)
        if name == "CHAT_GETMESSAGEFILTER":
            return (0, 0, 0, 1)
        if "NAME" in name:
            return (1, 0, 0, 1)
        if "GETHISTORYLENGTH" in name:
            return (1, 0, 1, 0)
        if "HISTORY" in name:
            return (2, 0, 0, 1)
        return (0, 0, 1, 0)
    if name.startswith("VIEWPORT_GET"):
        return (0, 0, 2, 0)
    if name.startswith("VIEWPORT_SET"):
        return (2, 0, 0, 0)
    if name == "VIEWPORT_CLAMPFOV":
        return (4, 0, 0, 0)
    if name == "MES":
        return (0, 1, 0, 0)
    if name == "IF_CLOSE":
        return (0, 0, 0, 0)
    if name == "SOUND_SYNTH":
        return (3, 0, 0, 0)
    if name.startswith("SET") and "SETON" not in name:
        return (1, 0, 0, 0)
    if "SETON" in name or name == "CC_SETTARGETVERB":
        return None
    if name.startswith("IF_SET") or name.startswith("CC_SET"):
        return (0, 0, 0, 0)
    # No rule matched: the opcode's identity/signature is genuinely unknown.
    # Return None so it is marked "not known" and the runtime stub asserts on it
    # instead of silently no-oping (which corrupts the stack and aborts the
    # script at some unrelated downstream opcode).
    return None


def main() -> None:
    entries, documented = parse_opcode_h()
    names = parse_meta_names()

    # `known` = the opcode's stack signature comes from a real source (an explicit
    # stack doc comment, a MANUAL_STACK override, or a specific name heuristic).
    # Opcodes NOT in this set only ever got the (0,0,0,0) default because nobody
    # knows what they do — the runtime stub asserts on those so an unimplemented
    # opcode surfaces at the opcode itself, not as a corrupted-stack failure later.
    known: set[int] = set(documented)

    for op, stack in MANUAL_STACK.items():
        entries[op] = stack
        documented.add(op)
        known.add(op)

    for op, name in names.items():
        if op in documented:
            continue
        if op in entries and any(entries[op]):
            known.add(op)
            continue
        h = heuristic(name)
        if h is not None:
            entries[op] = h
            known.add(op)

    lines = [
        "/* Generated by src/cs2vm2/gen_opcode_stack.py — do not edit by hand. */",
        "#ifndef CS2VM2_OPCODE_STACK_GEN_H",
        "#define CS2VM2_OPCODE_STACK_GEN_H",
        "",
        f"#define CS2VM2_OPCODE_STACK_MAX {MAX_OPCODE}",
        "",
        "struct CS2VM2OpcodeStack {",
        "    unsigned char int_in;",
        "    unsigned char str_in;",
        "    unsigned char int_out;",
        "    unsigned char str_out;",
        "    /* 1 when the signature above is real; 0 when the opcode is",
        "     * unimplemented and only got the (0,0,0,0) default. */",
        "    unsigned char known;",
        "};",
        "",
        "static struct CS2VM2OpcodeStack const g_cs2vm2_opcode_stack[CS2VM2_OPCODE_STACK_MAX] = {",
    ]
    for op in range(MAX_OPCODE):
        ii, si, io, so = entries.get(op, (0, 0, 0, 0))
        kn = 1 if op in known else 0
        lines.append(f"    [{op}] = {{ {ii}, {si}, {io}, {so}, {kn} }},")
    lines.extend(["};", "", "#endif", ""])
    OUT.write_text("\n".join(lines))
    print(f"wrote {OUT} ({len(entries)} opcodes with metadata)")


if __name__ == "__main__":
    main()
