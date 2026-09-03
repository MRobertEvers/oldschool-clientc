"""Project-local CS2 opcode knowledge layered over vendor/Opcodes.kt.

The vendored RuneStar table lags this client: it leaves placeholder names
(_NNNN) for opcodes we have since identified, and omits some ids entirely.
These overlays were recovered from the hand-maintained tables that had drifted
out of src/cs2vm2/, so regenerating reproduces them instead of dropping them.
The rev-239 command spellings added in the second audit come from the adjacent
osrs-cache/data/commands catalogue and were checked against the rev-239 Java
dispatcher plus the symbol-rich rev-216 native client decompile.
"""

from __future__ import annotations

# id -> name. Replaces a vendor placeholder, or adds an id vendor never listed.
LOCAL_NAMES: dict[int, str] = {
    # Modern nullable array/string-stack literal. The cache command catalogue
    # calls this PUSH_CONSTANT_NULL; older RuneLite tables use PUSH_NULL.
    63: "PUSH_CONSTANT_NULL",
    # RS2-era (rev 634) BRANCH_IF_ONE: pop int, branch by operand if value == 1.
    # Free in the OSRS numbering (nothing between 76 and 100), so claimed outright
    # rather than dialect-translated. See engine/cs2_opcode_dialect.h.
    86: "BRANCH_IF_ONE",
    # Dynamic-child traversal helpers used by the modern gameframe. The vendor
    # table skips these ids entirely; their stack shapes are documented in
    # src/cs2vm2/gen_opcode_stack.py.
    103: "OVERLAY_CC_CREATE",
    104: "OVERLAY_CC_DELETEALL",
    106: "CC_CREATECHILD",
    107: "CC_CREATESIBLING",
    202: "OVERLAY_FIND",
    203: "OVERLAY_CC_FIND",
    204: "CC_CHILDREN_FINDNEXTID",
    205: "IF_CHILDREN_FIND",
    206: "IF_CHILDREN_FINDNEXTID",
    209: "CC_PARENTID",
    210: "CC_FIND_PARAM",
    211: "IF_CHILDREN_COLLECT",
    212: "CC_CHILDREN_FIND_COUNT",
    213: "CC_CHILDREN_FINDNEXT",
    215: "CHILDREN_ARRAY",
    222: "CC_ASSERT",
    # Rev-239 component runtime-param setter. This id was previously assigned
    # the rev-634 IF_HASCHILD_MODAL name; keep that spelling as a source alias
    # below, but use the current client semantics as the canonical name.
    2704: "IF_SETPARAM",
    2705: "IF_HASCHILD_OVERLAY",
    2929: "IF_TRIGGEROPLOCAL",
    # Canonical command spellings from osrs-cache/data/commands/6900_player.txt.
    6900: "P_NAME",
    6901: "P_FINDSELF",
    6902: "P_ROUTELENGTH",
    6903: "P_ROUTE",
    6904: "UID",
    6905: "SELF_PLAYER_UID",
    # Rev 634 Class66.method704: push Class24.anInt359 (signed 24-bit login /
    # packet-54 field, updated with membership). No authoritative English name;
    # scripts compare against 8388605 (0x7FFFFD). Offline default is 0.
    6910: "LOGIN_INT24",
    # RS2-era (rev 634) widget param read: pop a param id, push the ParamType's
    # value for the active widget. A version -1 widget carries no param table
    # (the two id-keyed side tables in Class46.method433 are `version >= 0`
    # only), so this always answers with the ParamType's own default — which is
    # why it routes through STRUCT_PARAM with struct -1. Vendor placeholder
    # _1613; nothing in the OldSchool numbering claims it.
    1613: "CC_GETPARAM",
    # The arc/pie shaper for widget type 10, missing from the vendored table
    # (which leaves both as _NNNN placeholders). The reference pops two ints and
    # writes them to IfType +0x9c and +0xa0 -- the arc's START and END angle,
    # 65536 to a full turn -- which NXTPix2D::DrawCircularArc then draws as an
    # annulus sector: `cc_setfill(true)` gives the whole disc, `cc_setfill(false)`
    # plus `cc_setlinewid(n)` an n-pixel band along the arc.
    #
    # Clientscript 5480 (the overlay countdown pie) is the whole reason these
    # are named: it builds three type-10 children and shapes each with 1128.
    1128: "CC_SETARC",
    2128: "IF_SETARC",
    # OldSchool-era component param store, newer than the vendored table (which
    # stops at 1702). Not the same thing as 1613: these read and write a param
    # table the component owns at RUNTIME, and OldSchool IF3 files carry no param
    # section at all — every one of the 24,382 IF3 components in cache.osrs239
    # consumes its bytes exactly with none left over. So the table starts empty
    # and only a CC_SETCOMPONENTPARAM puts anything in it; a read that misses
    # answers with the ParamType's own default.
    #
    # The gameframe scripts use it to tag the widgets they build (cc_create, tag
    # with a "kind" param, later cc_find + read the tag back to recognise it):
    # script 8368 creates a component and immediately writes params 2365/2366/2367,
    # and script 8383 reads 2362 back with 1703. 153 write sites and 30 read sites
    # across cache.osrs239.
    #
    # 1704's arity depends on its last argument -- see the CC_SETCOMPONENTPARAM
    # notes in opcode_docs.py. The vendored solver's flat "three ints" is the
    # int-param case only.
    1703: "CC_GETCOMPONENTPARAM",
    1704: "CC_SETCOMPONENTPARAM",
    # The IF form of 1703, missing from the vendored table and from
    # 3rd/rscache's cs2_command.gen.h (which is why 20 scripts in cache.osrs239
    # fail to decompile at it). Reads a component's runtime param table for a
    # component named by argument instead of the active one, with a caller
    # supplied fallback:
    #
    #     if_getcomponentparam(param, component, fallback) -> int
    #
    # Arity is unambiguous from the bytecode -- three pushed, one consumed, at
    # all 16 call sites (script 8304's whole body is
    # `push 2356; push local0; push -1; 2703; return`, and script 9181 feeds the
    # result straight into a 3-argument if_setscrollsize). The *third* argument
    # is not: it is the literal -1 at every one of those sites, so "fallback for
    # a miss" and "sub-id of a dynamic child, -1 meaning the component itself"
    # are indistinguishable in this cache. It is treated as the fallback,
    # because every read site guards the result against -1 (`> 4`, `= -1`), and
    # that is what a table this port starts empty answers with.
    2703: "IF_GETCOMPONENTPARAM",
    1004: "CC_SETPINCH",  # not in vendor
    1122: "CC_SETGRAPHIC2",  # vendor: _1122
    1124: "CC_SETTRANSBOT",  # vendor: _1124
    1125: "CC_SETFILLMODE",  # vendor: _1125
    1129: "CC_SETHTTPSPRITE",
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
    1152: "CC_CRMVIEW_DISMISS",
    1203: "CC_SETPLAYERMODEL_SELF",  # not in vendor
    1204: "CC_SETMODEL_PLAYERCHATHEAD",  # not in vendor
    1214: "CC_SETLOCMODEL",
    1308: "CC_SETOPFORCELEFTCLICK",  # vendor: _1308
    1309: "CC_OP1309",  # vendor: _1309
    1310: "CC_CLEAROPSUBMENU",  # not in vendor
    1311: "CC_SETOPSUBMENU",  # not in vendor
    1312: "CC_SETTARGETPRIORITY",  # not in vendor
    1610: "CC_GETBLENDTRANS",
    1615: "CC_GETARCSTART",
    1616: "CC_GETARCEND",
    1624: "CC_INPUT_GETFOCUS",
    1628: "CC_INPUT_GETCARETPOSITION",
    2004: "IF_SETPINCH",  # not in vendor
    2122: "IF_SETGRAPHIC2",  # vendor: _2122
    2124: "IF_SETTRANSBOT",  # vendor: _2124
    2125: "IF_SETFILLMODE",  # vendor: _2125
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
    2203: "IF_SETMODEL_PLAYERCHATHEAD",  # not in vendor
    2214: "IF_SETLOCMODEL",
    2215: "IF_SETNPCMODEL",
    2308: "IF_SETCLICKMASK",  # vendor: _2308
    2309: "IF_OP2309",  # vendor: _2309
    3102: "MES_TYPED",
    3120: "SETDRAWPLAYERNAMES_FRIENDS",
    3122: "SETDRAWPLAYERNAMES_OTHERS",
    3124: "RESETDRAWPLAYERNAMES",
    3130: "SETFEEDBACKSPRITE",
    3131: "SETFEEDBACKSHOWPOPUPTEXT",
    3157: "SHOP_OPENCATEGORIES",
    3177: "MARKETING_INITANALYTICS",
    3178: "MARKETING_SENDANALYTICSEVENT",
    3179: "MARKETING_INITATTRIBUTION",
    3180: "MARKETING_SENDATTRIBUTIONEVENT",
    3189: "SEQ_PREFETCH",
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
    3221: "SOUND_SONG_WITHSECONDARY",  # vendor: _3221
    3223: "RT7_SETENABLED",
    3224: "RT7_SD",
    3225: "RT7_HD",
    3328: "IDLETIMER_GET",
    3329: "IDLETIMER_RESET",
    3330: "DESTINATIONCOORD",
    3500: "KEYHELD",  # not in vendor
    3501: "KEYPRESSED",  # not in vendor
    3628: "FRIENDLIST_SORT_RESET",
    3629: "FRIENDLIST_SORT_LEGACY",
    3630: "FRIENDLIST_SORT_NAME",
    3639: "FRIENDLIST_SORT_APPLY",
    3644: "FRIENDSCHAT_SORT_RESET",
    3645: "FRIENDSCHAT_SORT_LEGACY",
    3646: "FRIENDSCHAT_SORT_NAME",
    3647: "FRIENDSCHAT_SORT_WORLD",
    3648: "FRIENDSCHAT_SORT_LASTWORLDCHANGE",
    3652: "FRIENDSCHAT_SORT_ONLINE_WORLD",
    3655: "FRIENDSCHAT_SORT_APPLY",
    3656: "FRIENDLIST_SORT_RANK",
    3657: "FRIENDSCHAT_SORT_RANK",
    3700: "STEAM_SETACHIEVEMENT",
    3701: "STEAM_SETSTAT",
    3702: "STEAM_STORESTATS",
    3931: "STOCKMARKET_SELLABLE",
    3932: "STOCKMARKET_VALUE",
    4036: "STRING_TO_INT",  # not in vendor
    4123: "TEXT_PRONOUN",
    4124: "PRONOUN",
    4213: "OC_SHIFTCLICKIOP",  # not in vendor
    4214: "OC_WEARPOS",  # not in vendor
    4215: "OC_WEARPOS2",  # not in vendor
    4216: "OC_WEARPOS3",  # not in vendor
    4217: "OC_WEIGHT",  # not in vendor
    4218: "OC_EXAMINE",  # not in vendor
    4222: "OC_ISUBOP",  # not in vendor
    5632: "FEDERATED_LOGIN_STATE",
    5633: "FEDERATED_SHOP",
    6522: "MOBILE_KEYBOARDSHOWSTRING",
    6523: "MOBILE_KEYBOARDSHOWINTEGER",
    6527: "PLATFORMTYPE",
    6531: "CLIENT_VERSION",
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
    6750: "NPC_NAME",
    6751: "NPC_UID",
    6752: "NPC_CREATIONCYCLE",
    6753: "NPC_TYPE",
    6761: "NC_GETOPBASE",
    6762: "NC_GETOP",
    6801: "LOC_COORD",
    6802: "LOC_TYPE",
    6803: "LOC_FIND",  # vendor: _6803
    6806: "LC_GETOPBASE",
    6807: "LC_GETOP",
    6809: "LC_NAME",
    6851: "OBJ_COORD",
    6852: "OBJ_TYPE",
    6857: "OC_GETOPBASE",
    6858: "OC_GETOP",
    # The ground-item pile's own queries, read off the rev-216 native client
    # (ScriptRunnerImpl_6800To6899.cpp / _7100To7199.cpp) rather than guessed
    # from the call sites. 6859 is the SELECTOR -- it runs
    # Client::FindClientObjInStack(coord, index) and, on a hit, SetActiveObj --
    # which is why the four getters beside it take no arguments: they read the
    # ClientObj it left active. Same shape as LOC_FIND (6803) and the loc
    # getters around it.
    6859: "OBJ_FIND",
    6860: "OBJ_DESPAWNTIME",
    6861: "OBJ_VISIBLETIME",
    6862: "OBJ_ISPUBLIC",
    6863: "OBJ_OWNER",
    6950: "TILE_COORD",
    6951: "COORD_INSCENE",  # vendor: _6951
    # HIGHLIGHT_* runs in groups of five per subject -- SETUP, ON, OFF, GET,
    # CLEAR, in that order -- and the vendor table names only the ON/OFF pair of
    # each. The pair fixes which subject the group is, and the other three fall
    # out of the fixed order; every one of them is confirmed by the arity in
    # 3rd/rscache/src/cs2/cs2_command.gen.h (SETUP pops 5, GET pushes a bool,
    # CLEAR pops the group alone, and ON/OFF/GET all share the subject's key).
    #
    # 7040..7044 are a NINTH group of the same shape -- (5), (int,str), (int,str),
    # (int,str)->int, (int) -- and are used by the cache (script 6689 toggles
    # 7041/7042 behind 7043, script 6698 reads 7043 beside HIGHLIGHT_NPC_GET on
    # the same group). The reference identifies the subject as an OpGroup,
    # built from a right-click subject name and its operation list.
    # The pile on a tile, by absolute coord: Client::GetObjectsOnTile(coord)
    # and an index into the list it answers. Numbered inside the minimenu block
    # but nothing to do with the menu -- the ground-items overlay walks them.
    7120: "OBJSTACK_COUNT",
    7121: "OBJSTACK_ID",
    7122: "OBJSTACK_QUANTITY",
    7000: "HIGHLIGHT_NPC_SETUP",  # vendor: _7000
    7003: "HIGHLIGHT_NPC_GET",  # vendor: _7003
    7004: "HIGHLIGHT_NPC_CLEAR",  # vendor: _7004
    7005: "HIGHLIGHT_NPCTYPE_SETUP",  # vendor: _7005
    7008: "HIGHLIGHT_NPCTYPE_GET",  # vendor: _7008
    7009: "HIGHLIGHT_NPCTYPE_CLEAR",  # vendor: _7009
    7010: "HIGHLIGHT_LOC_SETUP",  # vendor: _7010
    7013: "HIGHLIGHT_LOC_GET",  # vendor: _7013
    7014: "HIGHLIGHT_LOC_CLEAR",  # vendor: _7014
    7015: "HIGHLIGHT_LOCTYPE_SETUP",  # vendor: _7015
    7018: "HIGHLIGHT_LOCTYPE_GET",  # vendor: _7018
    7019: "HIGHLIGHT_LOCTYPE_CLEAR",  # vendor: _7019
    7020: "HIGHLIGHT_OBJ_SETUP",  # vendor: _7020
    7023: "HIGHLIGHT_OBJ_GET",  # vendor: _7023
    7024: "HIGHLIGHT_OBJ_CLEAR",  # vendor: _7024
    7025: "HIGHLIGHT_OBJTYPE_SETUP",  # vendor: _7025
    7028: "HIGHLIGHT_OBJTYPE_GET",  # vendor: _7028
    7029: "HIGHLIGHT_OBJTYPE_CLEAR",  # vendor: _7029
    7030: "HIGHLIGHT_PLAYER_SETUP",  # vendor: _7030
    7033: "HIGHLIGHT_PLAYER_GET",  # vendor: _7033
    7034: "HIGHLIGHT_PLAYER_CLEAR",  # vendor: _7034
    7035: "HIGHLIGHT_TILE_SETUP",  # vendor: _7035
    7038: "HIGHLIGHT_TILE_GET",  # vendor: _7038
    7039: "HIGHLIGHT_TILE_CLEAR",  # vendor: _7039
    7040: "HIGHLIGHT_GROUP_SETUP",  # not in vendor
    7041: "HIGHLIGHT_GROUP_ON",  # not in vendor
    7042: "HIGHLIGHT_GROUP_OFF",  # not in vendor
    7043: "HIGHLIGHT_GROUP_GET",  # not in vendor
    7044: "HIGHLIGHT_GROUP_CLEAR",  # not in vendor
    7100: "MINIMENU_TYPE",  # vendor: _7100
    7101: "MINIMENU_ENTRY",  # vendor: _7101
    7102: "MINIMENU_FINDNPC",  # vendor: _7102
    7103: "MINIMENU_FINDLOC",  # vendor: _7103
    7104: "MINIMENU_FINDOBJ",  # vendor: _7104
    7105: "MINIMENU_FINDPLAYER",  # vendor: _7105
    7108: "MINIMENU_ISOPEN",  # vendor: _7108
    7109: "MINIMENU_FINDCOMPONENT",  # vendor: _7109
    7110: "MINIMENU_NUMOPS",  # vendor: _7110
    7200: "OVERLAY_NPC_CREATE",  # vendor: _7200
    7201: "OVERLAY_LOC_CREATE",  # vendor: _7201
    7203: "OVERLAY_PLAYER_CREATE",  # vendor: _7203
    7204: "OVERLAY_COORD_CREATE",  # vendor: _7204
    7205: "OVERLAY_NPC_GET",  # vendor: _7205
    7206: "OVERLAY_LOC_GET",  # vendor: _7206
    7208: "OVERLAY_PLAYER_GET",  # vendor: _7208
    7209: "OVERLAY_COORD_GET",  # vendor: _7209
    7210: "OVERLAY_NPC_DESTROY",  # vendor: _7210
    7211: "OVERLAY_LOC_DESTROY",  # vendor: _7211
    7213: "OVERLAY_PLAYER_DESTROY",  # vendor: _7213
    7214: "OVERLAY_COORD_DESTROY",  # vendor: _7214
    7250: "MINIMAP_SETZOOMABLE",  # vendor: SETMINIMAPLOCK
    7252: "MINIMAP_SETZOOM",  # vendor: _7252
    7253: "MINIMAP_GETZOOM",  # not in vendor
    7254: "MINIMAP_SETICONZOOMLIMIT",  # not in vendor
    # Loot-tracker auxiliary list. These live inside the reference client's
    # broad 7200..7499 native-extension group.
    7400: "LOOT_AUX_UPSERT2",
    7401: "LOOT_AUX_UPSERT",
    7404: "LOOT_AUX_REMOVE",
    7406: "LOOT_AUX_GET",
    7407: "LOOT_AUX_COUNT",
    7408: "LOOT_AUX_LOOKUP",
    7409: "LOOT_AUX_CLEAR",
    7460: "MINIMENU_HOVERED_INDEX",
    7462: "MINIMENU_SETBLOCKMODE",
    7465: "MINIMENU_RESETORDER",
    7466: "MINIMENU_SETORDEREDIT",
    7470: "MINIMENU_TOGGLESCROLL",
    7471: "MINIMENU_GETSCROLL",
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
    # Loot-tracker native store. The rev-239 Java handler exists at this range
    # but returns unhandled; this port supplies the host implementation used by
    # the cache scripts.
    7601: "LOOT_SOURCE_COUNT",
    7602: "LOOT_SOURCE_NAME",
    7603: "LOOT_SOURCE_ITEMCOUNT",
    7604: "LOOT_SOURCE_TOTALVAL",
    7605: "LOOT_BEGIN_QUERY",
    7606: "LOOT_QUERY_ID",
    7608: "LOOT_AUX_COUNT_TOTAL",
    7609: "LOOT_ROW_COUNT_BYNAME",
    7610: "LOOT_ROW_COUNT_BYID",
    7611: "LOOT_ROW_BYNAME",
    7612: "LOOT_ROW_BYID",
    7613: "LOOT_CLEAR_ALL",
    7614: "LOOT_CLEAR_SOURCE",
    7615: "LOOT_REMOVE_BYID",
    7616: "LOOT_IGNORE_ADD",
    7617: "LOOT_IGNORE_REMOVE",
    7619: "LOOT_GROUND_COUNT",
    7620: "LOOT_GROUND_NAME",
    7621: "LOOT_IGNORE_CLEAR",
    7622: "LOOT_SOURCE_IGNORE_ADD",
    7623: "LOOT_SOURCE_IGNORE_REMOVE",
    7625: "LOOT_SRCLIST_COUNT",
    7626: "LOOT_SRCLIST_NAME",
    7628: "LOOT_ADD",
    7630: "LOOT_SOURCE_NAME2",
    7809: "HISCORES_STATUS",
    7811: "HISCORES_ERROR",
    7801: "HISCORE_GETRANK",
    7802: "HISCORE_GETVALUE",
    7810: "HISCORE_CLEAR",
    7812: "HISCORE_SETAPI",
    7819: "HISCORE_GETMEMBERLEVEL",
    7823: "HISCORE_GETMEMBERNAME",
    7824: "HISCORE_GETMEMBERHISCORES",
    # Modern array handles live on the string stack. These ids are beyond the
    # vendored table's maximum but are used by the Overview widget library.
    8000: "ARRAY_SORT_ALL",
    8003: "ARRAY_LENGTH",
    8007: "ARRAY_COUNT_MATCHES",
    8018: "ARRAY_SPLIT",
    8019: "ARRAY_JOIN",
    8021: "ENUM_GETOUTPUTS",
    8022: "ARRAY_NEW",
    8023: "ARRAY_SETLENGTH",
    8024: "ARRAY_APPEND",
}

# id -> extra names emitted as additional #defines. Not an alternate spelling to
# pick between: both names are referenced in C, so both have to exist.
LOCAL_ALIASES: dict[int, tuple[str, ...]] = {
    213: ("_213",),  # spelling retained by older decompiled scripts
    2704: ("IF_HASCHILD_MODAL",),  # rev-634 spelling retained for old sources
    4030: ("SETBIT_RANGE_VALUE",),  # canonical name is vendor's SETBIT_RANGE_TOINT
    # Retain names already used by this tree while canonical command spellings
    # become the generated primary names.
    3330: ("_3330",),
    6750: ("_6750",),
    6751: ("_6751",),
    6752: ("_6752",),
    6753: ("_6753",),
    6801: ("_6801",),
    6802: ("_6802",),
    6851: ("_6851",),
    6852: ("_6852",),
    6900: ("_6900",),
    6901: ("ACTIVEPLAYER_SETLOCAL",),
    6902: ("ACTIVEPLAYER_GETROUTELENGTH",),
    6903: ("ACTIVEPLAYER_GETROUTECOORD",),
    6904: ("ACTIVEPLAYER_GETUID",),
    6905: ("LOCALPLAYER_GETUID",),
    6950: ("_6950",),
    7040: ("HIGHLIGHT_OPGROUP_SETUP",),
    7041: ("HIGHLIGHT_OPGROUP_ON",),
    7042: ("HIGHLIGHT_OPGROUP_OFF",),
    7043: ("HIGHLIGHT_OPGROUP_GET",),
    7044: ("HIGHLIGHT_OPGROUP_CLEAR",),
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
    47: "CS2_HANDLER_VM",  # PUSH_VARC_STRING_OLD — same as 49
    48: "CS2_HANDLER_VM",  # POP_VARC_STRING_OLD — same as 50
    86: "CS2_HANDLER_VM",  # BRANCH_IF_ONE (RS2-era)
    4016: "CS2_HANDLER_VM",
    4017: "CS2_HANDLER_VM",
    6910: "CS2_HANDLER_VM",  # LOGIN_INT24 — offline stub pushes 0
}

# Free-form banner comments emitted just above an opcode's #define.
SECTION_COMMENTS: dict[int, tuple[str, ...]] = {
    86: (
        "/* BRANCH_IF_ONE — RS2-era (rev 634) conditional branch.",
        " * operand: branch offset",
        " * int stack in:   value",
        " * str stack in:   -",
        " * int stack out:  -",
        " * str stack out:  -",
        " * notes: pc += operand if value == 1. Free in the OSRS numbering",
        " *        (nothing between 76 and 100); claimed here rather than",
        " *        dialect-translated. See engine/cs2_opcode_dialect.h. */",
    ),
    6910: (
        "/* LOGIN_INT24 — rev 634 login/account int getter (Class24.anInt359).",
        " * operand: unused",
        " * int stack in:   -",
        " * str stack in:   -",
        " * int stack out:  value",
        " * str stack out:  -",
        " * notes: set at jagex-account login and by packet 54 (signed 24-bit",
        " *        alongside the membership flag). Scripts probe 8388605",
        " *        (0x7FFFFD). Offline/unlogged stub pushes 0 (static default). */",
    ),
    2705: (
        "/* IF_HASCHILD_OVERLAY (2705) — legacy interface-parent probe.",
        " * operand: unused",
        " * int stack in:   widget, parent  (parent = top)",
        " * str stack in:   -",
        " * int stack out:  1 if InterfaceParent[widget].group_id == parent, else 0",
        " * str stack out:  -",
        " * notes: retained from the rev-634 interface-parent family. Opcode",
        " *        2704 is IF_SETPARAM in the current rev-239 dialect. */",
    ),
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
        " * fields yet, so each is forwarded as its own exact host request. Their stack",
        " * signatures must still be described to keep the operand stack synchronized.",
        " * Each takes the component uid on top of the value.",
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
    7200: (
        "/* Scripted entity overlays (7200..7214) — jag::oldscape::EntityOverlays.",
        " * CREATE forms share the tail (slot, band, width, height, source_coord):",
        " * band is 0 middle / 1 above / 2 below, and source_coord selects world-",
        " * versus display-coordinate anchoring. GET and DESTROY address the same",
        " * subject-specific slot. See game/rs_entity_overlay.h. */",
    ),
    7400: (
        "/* Loot-tracker auxiliary-list ops inside the 7200..7499 native group. */",
    ),
    7500: (
        "/* Client database family. Read DBROW config (kind 38) and the DBTABLEINDEX",
        " * (cache table 21); see CS2VM2_Op_Db / exec_db. */",
    ),
    7601: (
        "/* Loot-tracker native store (7600-family host ops). The rev-239 Java",
        " * range handler returns unhandled; this port implements the cache's",
        " * native loot-tracker extension. */",
    ),
    7809: (
        "/* Hiscores native-extension stubs (7809/7811). */",
    ),
    8007: (
        "/* ARRAY_COUNT_MATCHES — count cells in [start, end) equal to a typed value.",
        " * A negative end means \"to the end\". The array is a handle on the",
        " * string stack; value_type selects whether the search value is popped",
        " * from the int or string stack, so this opcode has variable arity. */",
    ),
}
