"""Project-local CS2 opcode knowledge layered over vendor/Opcodes.kt.

The vendored RuneStar table lags this client: it leaves placeholder names
(_NNNN) for opcodes we have since identified, and omits some ids entirely.
These overlays were recovered from the hand-maintained tables that had drifted
out of src/cs2vm2/, so regenerating reproduces them instead of dropping them.
"""

from __future__ import annotations

# id -> name. Replaces a vendor placeholder, or adds an id vendor never listed.
LOCAL_NAMES: dict[int, str] = {
    1004: "CC_SETPINCH",  # not in vendor
    1133: "CC_INPUT_SETSUBMITMODE",  # not in vendor
    1134: "CC_INPUT_SETSELECTCOLOUR",  # not in vendor
    1135: "CC_INPUT_SETACCEPTMODE",  # not in vendor
    1136: "CC_INPUT_SETWRAPMODE",  # not in vendor
    1137: "CC_INPUT_SETLINEWRAPPINGWIDTH",  # not in vendor
    1138: "CC_INPUT_SETSELECTBGCOLOUR",  # not in vendor
    1139: "CC_INPUT_SETLINECOUNTLIMIT",  # not in vendor
    1140: "CC_INPUT_SETCURSORCOLOUR",  # not in vendor
    1141: "CC_INPUT_SETCURSORTRANS",  # not in vendor
    1142: "CC_INPUT_SETCURSORWIDTH",  # not in vendor
    1143: "CC_INPUT_SETCURSORHEIGHT",  # not in vendor
    1144: "CC_INPUT_SETCURSOROFFSET",  # not in vendor
    1145: "CC_INPUT_SETLINEWIDTHLIMIT",  # not in vendor
    1146: "CC_INPUT_SETCHARFILTER",  # not in vendor
    1308: "CC_SETOPFORCELEFTCLICK",  # vendor: _1308
    1309: "CC_OP1309",  # vendor: _1309
    1310: "CC_CLEAROPSUBMENU",  # not in vendor
    1311: "CC_SETOPSUBMENU",  # not in vendor
    1312: "CC_SETTARGETPRIORITY",  # not in vendor
    2004: "IF_SETPINCH",  # not in vendor
    2133: "IF_INPUT_SETSUBMITMODE",  # not in vendor
    2134: "IF_INPUT_SETSELECTCOLOUR",  # not in vendor
    2135: "IF_INPUT_SETACCEPTMODE",  # not in vendor
    2136: "IF_INPUT_SETWRAPMODE",  # not in vendor
    2137: "IF_INPUT_SETLINEWRAPPINGWIDTH",  # not in vendor
    2138: "IF_INPUT_SETSELECTBGCOLOUR",  # not in vendor
    2139: "IF_INPUT_SETLINECOUNTLIMIT",  # not in vendor
    2140: "IF_INPUT_SETCURSORCOLOUR",  # not in vendor
    2141: "IF_INPUT_SETCURSORTRANS",  # not in vendor
    2142: "IF_INPUT_SETCURSORWIDTH",  # not in vendor
    2143: "IF_INPUT_SETCURSORHEIGHT",  # not in vendor
    2144: "IF_INPUT_SETCURSOROFFSET",  # not in vendor
    2145: "IF_INPUT_SETLINEWIDTHLIMIT",  # not in vendor
    2146: "IF_INPUT_SETCHARFILTER",  # not in vendor
    2309: "IF_OP2309",  # vendor: _2309
    3129: "SETKEYINPUTENABLED",  # vendor: _3129
    3138: "SETKEYINPUTMODE_ALL",  # vendor: _3138
    3139: "SETKEYINPUTMODE_KEYBOARD",  # vendor: _3139
    3140: "GETKEYINPUTMODE",  # vendor: _3140
    3209: "CLIENTOPTION_SET",  # vendor: _3209
    3210: "CLIENTOPTION_GET",  # vendor: _3210
    3212: "DEVICEOPTION_SET",  # not in vendor
    3213: "GAMEOPTION_SET",  # not in vendor
    3214: "DEVICEOPTION_GET",  # not in vendor
    3215: "GAMEOPTION_GET",  # not in vendor
    3217: "DEVICEOPTION_GETRANGE",  # not in vendor
    3500: "KEYHELD",  # not in vendor
    3501: "KEYPRESSED",  # not in vendor
    4213: "OC_SHIFTCLICKIOP",  # not in vendor
    4214: "OC_WEARPOS",  # not in vendor
    4215: "OC_WEARPOS2",  # not in vendor
    4216: "OC_WEARPOS3",  # not in vendor
    4217: "OC_WEIGHT",  # not in vendor
    4218: "OC_EXAMINE",  # not in vendor
    4222: "OC_ISUBOP",  # not in vendor
    6210: "UIZOOM_SET",  # vendor: _6210
    6211: "UIZOOM_GET",  # not in vendor
    6212: "UIZOOM_RESET",  # vendor: _6212
    6214: "UIZOOM_GETDEFAULT",  # not in vendor
    6220: "SAFEAREA_GETMINX",  # vendor: _6220
    6221: "SAFEAREA_GETMINY",  # vendor: _6221
    6222: "SAFEAREA_GETMAXX",  # vendor: _6222
    6223: "SAFEAREA_GETMAXY",  # vendor: _6223
    6231: "SAFEAREA_GETMAXY_ALT",  # not in vendor
    6232: "CAM_GETYAW",  # not in vendor
    6600: "WORLDMAP_INIT",  # vendor: _6600
    6615: "WORLDMAP_GETDISPLAYCOORD_CURRENT",  # vendor: _6615
    6618: "WORLDMAP_GETSOURCECOORD",  # vendor: _6618
    6619: "WORLDMAP_JUMPTOMAP",  # vendor: _6619
    6620: "WORLDMAP_JUMPTOMAP_INSTANT",  # vendor: _6620
    6623: "WORLDMAP_GETMAP",  # vendor: _6623
    6624: "WORLDMAP_SETMAXFLASHCOUNT",  # vendor: _6624
    6625: "WORLDMAP_RESETMAXFLASHCOUNT",  # vendor: _6625
    6626: "WORLDMAP_SETCYCLESPERFLASH",  # vendor: _6626
    6627: "WORLDMAP_RESETCYCLESPERFLASH",  # vendor: _6627
    6638: "WORLDMAP_GETNEARESTICON",  # vendor: _6638
    6698: "WORLDMAP_ELEMENTCOORD1",  # vendor: _6698
    6700: "CLIENTOP_NPC_SET",  # vendor: _6700
    6701: "CLIENTOP_NPC_DEL",  # vendor: _6701
    6702: "CLIENTOP_LOC_SET",  # vendor: _6702
    6703: "CLIENTOP_LOC_DEL",  # vendor: _6703
    6704: "CLIENTOP_OBJ_SET",  # vendor: _6704
    6705: "CLIENTOP_OBJ_DEL",  # vendor: _6705
    6706: "CLIENTOP_PLAYER_SET",  # vendor: _6706
    6707: "CLIENTOP_PLAYER_DEL",  # vendor: _6707
    6708: "CLIENTOP_TILE_SET",  # vendor: _6708
    6709: "CLIENTOP_TILE_DEL",  # vendor: _6709
    7013: "HIGHLIGHT_LOC_GET",  # vendor: _7013
    7014: "HIGHLIGHT_LOC_CLEAR",  # vendor: _7014
    7023: "HIGHLIGHT_OBJ_GET",  # vendor: _7023
    7025: "HIGHLIGHT_OBJTYPE_SETUP",  # vendor: _7025
    7100: "MINIMENU_TYPE",  # vendor: _7100
    7101: "MINIMENU_ENTRY",  # vendor: _7101
    7102: "MINIMENU_FINDNPC",  # vendor: _7102
    7103: "MINIMENU_FINDLOC",  # vendor: _7103
    7104: "MINIMENU_FINDOBJ",  # vendor: _7104
    7105: "MINIMENU_FINDPLAYER",  # vendor: _7105
    7108: "MINIMENU_ISOPEN",  # vendor: _7108
    7109: "MINIMENU_FINDCOMPONENT",  # vendor: _7109
    7110: "MINIMENU_NUMOPS",  # vendor: _7110
    7250: "MINIMAP_SETZOOMABLE",  # vendor: SETMINIMAPLOCK
    7252: "MINIMAP_SETZOOM",  # vendor: _7252
    7500: "DB_FIND_WITH_COUNT",  # not in vendor
    7501: "DB_FINDNEXT",  # not in vendor
    7502: "DB_GETFIELD",  # not in vendor
    7503: "DB_GETFIELDCOUNT",  # not in vendor
    7504: "DB_FINDALL_WITH_COUNT",  # not in vendor
    7505: "DB_GETROWTABLE",  # not in vendor
    7506: "DB_GETROW",  # not in vendor
    7507: "DB_FIND_FILTER_WITH_COUNT",  # not in vendor
    7508: "DB_FIND",  # not in vendor
    7509: "DB_FINDALL",  # not in vendor
    7510: "DB_FIND_FILTER",  # not in vendor
}

# id -> extra names emitted as additional #defines. Not an alternate spelling to
# pick between: both names are referenced in C, so both have to exist.
LOCAL_ALIASES: dict[int, tuple[str, ...]] = {
    4030: ("SETBIT_RANGE_VALUE",),  # canonical name is vendor's SETBIT_RANGE_TOINT
}

# id -> operand kind for the VM meta table (cs2_opcode_meta.c) only.
# This field feeds debug tracing (CS2VM2_DEBUG_OPS). The operand width actually
# used to decode bytecode is hardcoded in 3rd/rscache/src/datatypes/clientscript.c
# (opcode >= 100 -> int8), not read from here.
META_OPERAND_OVERRIDES: dict[int, str] = {
    1133: "CS2_OPERAND_INT32",
    1134: "CS2_OPERAND_INT32",
    1135: "CS2_OPERAND_INT32",
    1136: "CS2_OPERAND_INT32",
    1137: "CS2_OPERAND_INT32",
    1138: "CS2_OPERAND_INT32",
    1139: "CS2_OPERAND_INT32",
    1140: "CS2_OPERAND_INT32",
    1141: "CS2_OPERAND_INT32",
    1142: "CS2_OPERAND_INT32",
    1143: "CS2_OPERAND_INT32",
    1144: "CS2_OPERAND_INT32",
    1145: "CS2_OPERAND_INT32",
    1146: "CS2_OPERAND_INT32",
    1430: "CS2_OPERAND_INT32",
    1431: "CS2_OPERAND_INT32",
    1433: "CS2_OPERAND_INT32",
    1436: "CS2_OPERAND_INT32",
    1437: "CS2_OPERAND_INT32",
    1438: "CS2_OPERAND_INT32",
    1439: "CS2_OPERAND_INT32",
    2133: "CS2_OPERAND_INT32",
    2134: "CS2_OPERAND_INT32",
    2135: "CS2_OPERAND_INT32",
    2136: "CS2_OPERAND_INT32",
    2137: "CS2_OPERAND_INT32",
    2138: "CS2_OPERAND_INT32",
    2139: "CS2_OPERAND_INT32",
    2140: "CS2_OPERAND_INT32",
    2141: "CS2_OPERAND_INT32",
    2142: "CS2_OPERAND_INT32",
    2143: "CS2_OPERAND_INT32",
    2144: "CS2_OPERAND_INT32",
    2145: "CS2_OPERAND_INT32",
    2146: "CS2_OPERAND_INT32",
    2436: "CS2_OPERAND_INT32",
    2437: "CS2_OPERAND_INT32",
    2438: "CS2_OPERAND_INT32",
    2439: "CS2_OPERAND_INT32",
    3170: "CS2_OPERAND_INT32",
    3171: "CS2_OPERAND_INT32",
    3172: "CS2_OPERAND_INT32",
    3173: "CS2_OPERAND_INT32",
    3212: "CS2_OPERAND_INT32",
    3213: "CS2_OPERAND_INT32",
    3214: "CS2_OPERAND_INT32",
    3215: "CS2_OPERAND_INT32",
    3217: "CS2_OPERAND_INT32",
    3500: "CS2_OPERAND_INT32",
    3501: "CS2_OPERAND_INT32",
    6211: "CS2_OPERAND_INT32",
    6214: "CS2_OPERAND_INT32",
    6231: "CS2_OPERAND_INT32",
    6232: "CS2_OPERAND_INT32",
}

# id -> operand kind for the rscache decode table (dat2a_cs2_opcode_decode.c).
# Deliberately separate from META_OPERAND_OVERRIDES: the two tables disagreed in
# the tree, neither is load-bearing yet, so both are preserved exactly as found.
DECODE_OPERAND_OVERRIDES: dict[int, str] = {
    3170: "CS2_OPERAND_INT32",
    3171: "CS2_OPERAND_INT32",
    3172: "CS2_OPERAND_INT32",
    3173: "CS2_OPERAND_INT32",
    4122: "CS2_OPERAND_INT32",
}

# id -> handler kind, where the VM executes the opcode instead of the host.
HANDLER_OVERRIDES: dict[int, str] = {
    4016: "CS2_HANDLER_VM",
    4017: "CS2_HANDLER_VM",
}

# Free-form banner comments emitted just above an opcode's #define.
SECTION_COMMENTS: dict[int, tuple[str, ...]] = {
    1430: (
        "/* More SETON* listeners with no runtime model yet — signature-driven operand",
        " * counts, so they are dispatched to the parse-and-discard helper like the rest",
        " * of the family (see the CC_SETON* discard group in cs2vm2.c). */",
    ),
    1436: (
        "/*",
        " * Input-field (widget type 16) listeners. Signature-driven operand counts like",
        " * the other SETON* opcodes, so they are dispatched to the parse-and-discard",
        " * helpers rather than described here; the counts below are placeholders the",
        " * generator ignores for SETON names.",
        " */",
    ),
    2133: (
        "/*",
        " * IF_ variants of the input-field config setters. No UITree model for these",
        " * fields yet, so they are left to the stack-meta stub -- but they must still be",
        " * described, or the stub pops nothing and desyncs the operand stack. Each takes",
        " * the component uid on top of the value.",
        " */",
    ),
    2430: (
        "/* IF_ counterparts of CC_SETONITEMONITEM/CLANSETTINGS/MAPPOST (1430/1431/1433):",
        " * set the listener by widget UID instead of on the active/dot child. No runtime",
        " * model yet — signature-driven operand counts, so they are dispatched to the",
        " * parse-and-discard helper (see the IF_SETON* discard group in cs2vm2.c). Used",
        " * by the magic spellbook redraw (script 2610) — without a dispatch case op 2430",
        " * fell through to StackMetaStub and aborted, blanking the spell icons. */",
    ),
    3170: (
        "/*",
        " * Mobile local (push) notifications, 3170..3173. Newer than the vendored RuneStar",
        " * table, which has nothing between 3157 and 3181. Desktop has no notification",
        " * centre, so the host stubs the whole family: scheduling is a no-op and",
        " * SUPPORTED answers 0, which is what the scripts branch on.",
        " */",
    ),
    4030: (
        "/* SETBIT_RANGE_VALUE (a.k.a. SETBIT_RANGE_TOINT): clear the [low,high] bit range",
        " * of `value` then write `newBits` (clamped to the range width) into it.",
        " * int in: value, newBits, low, high (high = top)  int out: result */",
    ),
    6600: (
        "/* World map (interface 595). Coords are packed as plane<<28 | x<<14 | y; a",
        " * \"display\" coord is a position on the map surface, a \"source\" coord is a real",
        " * world coord. Map ids index the worldmap \"details\" archive (cache table 19).",
        " */",
    ),
    7500: (
        "/* Client database family. Read DBROW config (kind 38) and the DBTABLEINDEX",
        " * (cache table 21); see CS2VM2_Op_Db / exec_db. */",
    ),
}
