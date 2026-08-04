#!/usr/bin/env python3
"""Generate cs2vm2_opcode_stack.gen.h.

Three sources, in strict precedence order (highest first):

  1. `cs2_opcode.h` stack doc-comments -- a signature somebody wrote down here
     after reading a client that executes the opcode.
  2. `MANUAL_STACK` below -- the same, for opcodes with no doc comment.
  3. the name heuristics in `heuristic()` -- guesses from the opcode's name.
  4. `3rd/rscache/src/cs2/cs2_command.gen.h` -- the *decompiler's* signature
     table (vendored RuneStar Command.kt layered with
     `3rd/rscache/tools/cs2/local_commands.py`).

(4) is the bridge added on 2026-08-02. Before it, the two signature tables in
this repo could not see each other: `gen_cs2_tables.py` had established ~120
arities by corpus inference that the client VM had no way to inherit, so an
opcode the decompiler understood perfectly still hit
`CS2VM2_Op_StackMetaStub`'s `assert(0)` at run time. `[7604]` was the clean
proof -- `(STRING)->(INT)` in cs2_command.gen.h, SIGABRT in the client.

It is ranked LAST, and applied *additively*: it only ever fills a row that
nothing else established (`known == 0`). It never overrides a row (1)-(3)
already own. That is deliberate and stricter than "above the heuristics":
53 opcodes disagree between the two tables (see BRIDGE_CONFLICTS_OK), and
nearly all of them are CC_SET*/IF_SET* rows whose (0,0,0,0) heuristic value is
a *marker* -- those opcodes have dedicated dispatch in cs2vm2.c that owns the
stack, so the table entry only feeds the debug trace. Rewriting them from a
table the runtime does not consult would change nothing at best and silently
re-shape a working UI path at worst, for no gain. Every disagreement is
instead required to be acknowledged in BRIDGE_CONFLICTS_OK, so a *new* one --
which means one of the two tables is genuinely wrong -- fails generation
loudly rather than being absorbed.

Why "additive" is safe and "override" is not, concretely: the failure mode of
this whole area is silent-wrong-arity (`local_commands.py:16-18`). A wrong pop
count does not fail loudly; it desynchronises the operand stack and the script
dies several opcodes later somewhere unrelated. Filling a `known = 0` row can
only replace a hard abort with a signature; overriding a `known = 1` row can
replace working behaviour with corruption.
"""

import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
OPCODE_H = HERE / "cs2_opcode.h"
META_C = HERE / "cs2_opcode_meta.c"
OUT = HERE / "cs2vm2_opcode_stack.gen.h"

REPO = HERE.parent.parent
COMMAND_GEN_H = REPO / "3rd" / "rscache" / "src" / "cs2" / "cs2_command.gen.h"
CS2_TYPES_C = REPO / "3rd" / "rscache" / "src" / "cs2" / "cs2_types.c"


# RSCACHE_CS2_OPCODE_TABLE_SIZE (3rd/rscache/src/cs2/cs2_command.gen.h) — the
# highest defined opcode there is 8024 (ARRAY_INSERT / append), so this is the
# measured ceiling, not a guess. Used to top out at 7602, which put the entire
# 8000-series permanently out of reach: not implemented, not asserted, just
# silently absent from this table and from CS2VM2_Op_StackMetaStub's
# `opcode < CS2VM2_OPCODE_STACK_MAX` gate. Raised 8023→8025 when Overview's
# 8023/8024 landed (skill_guide.md §5).
MAX_OPCODE = 8025

MANUAL_STACK: dict[int, tuple[int, int, int, int]] = {
    47: (0, 0, 0, 1),  # PUSH_VARC_STRING_OLD(varc id) -> string
    48: (0, 1, 0, 0),  # POP_VARC_STRING_OLD(varc id) <- string
    86: (1, 0, 0, 0),  # BRANCH_IF_ONE(value): branch if value == 1 (RS2-era)
    6910: (0, 0, 1, 0),  # LOGIN_INT24 -> Class24.anInt359 (stub: 0)
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
    # New in the rev-239 gameframe scripts and in neither vendored table. Arity
    # established by `cs2 infer-arity` over cache.osrs239: ten sites, every one
    # solving to the same six-int pop with nothing pushed, unanimously. What it
    # *means* is still unknown, so this only keeps the stack balanced — the six
    # arguments are dropped and nothing happens. Reached from script 8483 during
    # boot, where the alternative was StackMetaStub's assert.
    210: (6, 0, 0, 0),  # _210(component, int, int, int, int, int)
    211: (3, 0, 1, 0),  # _211(int, component, int) -> int
    212: (1, 0, 1, 0),  # CC_CHILDREN_FIND+count(start) -> count
    213: (0, 0, 1, 0),  # children find-next id (same as 204/206)
    215: (0, 0, 0, 1),  # _215() -> string (array handle)
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
    # Orbit camera angles (5504..5506). Dedicated dispatch in cs2vm2.c forwards
    # them to the host; these document the contracts. Units are the script's,
    # not the renderer's: pitch 128..383, yaw 0..2047.
    5504: (2, 0, 0, 0),  # CAM_FORCEANGLE(pitch, yaw)
    5505: (0, 0, 1, 0),  # CAM_GETANGLE_XA -> pitch
    5506: (0, 0, 1, 0),  # CAM_GETANGLE_YA -> yaw
    3328: (0, 0, 1, 0),  # idle-time getter (script 5327 logout warning polls it)
    # Account-standing getters in the 3300 family. All three are no-arg and push
    # one int (`listOf()` -> `listOf(INT/BOOLEAN/FLAGS)` in the vendored
    # Command.kt the cs2 tool reads); none of the name heuristics above match
    # them, so they were the only holes left in 3300..3327 and STAFFMODLEVEL
    # aborted the boot from script 73 (the chat-input handler, which gates
    # `::`/backtick command parsing on it).
    #
    # Stubs, deliberately: nothing in this port has the value. The rev-230 login
    # response the mock server sends is the bare `2` with no staff-level tail
    # (loginproto.c decodes one for the 2004 handshake, but that value stops at
    # `struct LoginProto` and no generation forwards it), so the honest answer is
    # "not staff, no world flags" — the same 0 rs_minimenu_build.c already
    # assumes when it omits the staff-only report row.
    3316: (0, 0, 1, 0),  # STAFFMODLEVEL -> staff rights level (stub: 0)
    3323: (0, 0, 1, 0),  # PLAYERMOD -> is-player-moderator bool (stub: 0)
    3324: (0, 0, 1, 0),  # WORLDFLAGS -> world flag bits (stub: 0)
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
    # DB_* client-database family (7500..7510). These have dedicated handlers
    # (CS2VM2_Op_Db -> exec_db) that own the stack, so these entries only feed the
    # debug trace. DB_FIND/GETFIELD have a value/tuple whose int-vs-string type is
    # runtime-dependent; the counts below are the common all-int shape.
    # Math / bit ops (4007..4030) — pure-VM handlers (CS2VM2_Op_*); listed so the
    # trace and known-flag are correct. All pop N ints and push one.
    4007: (2, 0, 1, 0),  # ADDPERCENT(value, percent)
    4014: (2, 0, 1, 0),  # AND(a, b)
    4016: (2, 0, 1, 0),  # MIN(a, b)
    4017: (2, 0, 1, 0),  # MAX(a, b)
    4025: (1, 0, 1, 0),  # BITCOUNT(value)
    4026: (2, 0, 1, 0),  # TOGGLEBIT(value, bit)
    4027: (3, 0, 1, 0),  # SETBIT_RANGE(value, low, high)
    4028: (3, 0, 1, 0),  # CLEARBIT_RANGE(value, low, high)
    4029: (3, 0, 1, 0),  # GETBIT_RANGE(value, low, high)
    4030: (4, 0, 1, 0),  # SETBIT_RANGE_VALUE(value, newBits, low, high)
    # ABS(value) -> |value|. Listed here rather than left to the bridge because
    # it is really implemented (CS2VM2_Op_Abs), so it must read `known = 1`
    # ("we know what it does") and not the bridge's 2 ("we only know its
    # shape"). cs2_command.gen.h agrees on the shape.
    4035: (1, 0, 1, 0),  # ABS(value)
    7500: (2, 0, 1, 0),  # DB_FIND_WITH_COUNT(dbcolumn, value) -> count
    7501: (0, 0, 1, 0),  # DB_FINDNEXT() -> rowId (or -1)
    7502: (3, 0, 1, 0),  # DB_GETFIELD(dbrow, dbcolumn, index) -> field value(s)
    7503: (2, 0, 1, 0),  # DB_GETFIELDCOUNT(dbrow, dbcolumn) -> tuple count
    7504: (1, 0, 1, 0),  # DB_FINDALL_WITH_COUNT(dbtable) -> count
    7505: (1, 0, 1, 0),  # DB_GETROWTABLE(dbrow) -> tableId
    7506: (1, 0, 0, 0),  # DB_GETROW(dbrow) -> (loads/activates the row)
    7507: (2, 0, 1, 0),  # DB_FIND_FILTER_WITH_COUNT(dbcolumn, value) -> count
    7508: (2, 0, 0, 0),  # DB_FIND(dbcolumn, value)
    7509: (1, 0, 0, 0),  # DB_FINDALL(dbtable)
    7510: (2, 0, 0, 0),  # DB_FIND_FILTER(dbcolumn, value)
    # IF_CALLONRESIZE(component): run that component's on-resize listener now.
    # Dedicated dispatch in cs2vm2.c (CS2VM_HOST_REQUEST_IF_CALLONRESIZE) owns
    # the stack, so this only documents the contract — but the contract was read
    # off the bytecode rather than inferred: script 1911 ends
    # `PUSH_INT_LOCAL 2; IF_CALLONRESIZE; RETURN`, and all seventeen call sites
    # in cache.osrs239 have that shape.
    #
    # 1927 CC_CALLONRESIZE is left out deliberately: cs2_command.gen.h gives it
    # one argument, which is not the shape any other `cc_*` component op has,
    # and no script in this cache calls it — so there is nothing to verify an
    # arity against, and a guess is exactly what StackMetaStub exists to catch.
    2927: (1, 0, 0, 0),  # IF_CALLONRESIZE(component)
    # IF_GETCOMPONENTPARAM(param, component, fallback) -> int. The IF form of
    # CC_GETCOMPONENTPARAM (1703), absent from the vendored table and from
    # 3rd/rscache's cs2_command.gen.h — which is why 20 scripts in cache.osrs239
    # fail to decompile at it. Dedicated dispatch in cs2vm2.c owns the stack;
    # see the handler for how the arity was read off the bytecode.
    2703: (3, 0, 1, 0),  # IF_GETCOMPONENTPARAM(param, component, fallback)
    # FRIEND_COUNT is a no-arg getter (total friends), but the name heuristic gave
    # it (1,0,1,0) like the indexed FRIEND_GET* ops -> the stub popped a non-existent
    # arg and underflowed, aborting the friends-list builder (script 125).
    3600: (0, 0, 1, 0),  # FRIEND_COUNT: no args -> 1 int
    # Same class as FRIEND_COUNT: the CLAN_*/FRIEND_*/IGNORE_* name heuristic
    # assumes an index argument, which is right for the per-entry getters
    # (CLAN_GETCHATUSERNAME(idx) etc.) but wrong for these whole-channel ones.
    # They pop an argument that was never pushed, underflow, and abort the panel
    # (clan sidepanel script 1658 died on CLAN_GETCHATCOUNT).
    3611: (0, 0, 0, 1),  # CLAN_GETCHATDISPLAYNAME: no args -> channel name
    3612: (0, 0, 1, 0),  # CLAN_GETCHATCOUNT: no args -> member count
    3616: (0, 0, 1, 0),  # CLAN_GETCHATMINKICK: no args -> min rank to kick
    3618: (0, 0, 1, 0),  # CLAN_GETCHATRANK: no args -> own rank
    3620: (0, 0, 0, 0),  # CLAN_LEAVECHAT: no args, no result
    3625: (0, 0, 0, 1),  # CLAN_GETCHATOWNERNAME: no args -> owner name
    3621: (0, 0, 1, 0),  # IGNORE_COUNT: no args -> ignore count
    3623: (0, 1, 1, 0),  # IGNORE_TEST(name) -> bool (string arg, not an index)
    # ------------------------------------------------------------------
    # The rest of the FRIEND_ / IGNORE_ / CLAN_ / CHAT_ families.
    #
    # These are NOT guesses and they are not "same class as above" reasoning:
    # every tuple below was read out of the decompiler's own proto pool,
    # 3rd/rscache/src/cs2/cs2_command.gen.h, whose arg/def offsets index
    # cs2_proto_pool[] and whose entries resolve through cs2_types.c to a base
    # type (STRING vs everything else). That table is the authority the
    # decompiler uses, which is why `cs2 decompile` prints
    # `$string0, $string1 = friend_getname($int)` while the heuristic below
    # believed it returned one string.
    #
    # The heuristic that produced the wrong ones is a single line further down
    # (`if name.startswith("FRIEND_") or name.startswith("CLAN_")`): it assumes
    # one int arg and reads "NAME" in the opcode name as "returns a string".
    # That is wrong in three separate ways for this family — the mutators take
    # a *string* and return nothing, the getname ops return *two* strings, and
    # the CHAT_ senders take strings too. Each wrong shape is a silent no-op
    # with a mis-set arity, i.e. a stack desync that kills the script several
    # opcodes later.
    #
    # The real fix is for this generator to read the proto pool instead of
    # guessing from names; that would re-shape several hundred opcodes at once
    # and is deliberately NOT done here. See docs/FRIENDS_PRIVATE_CHAT.md §4.2.
    3601: (1, 0, 0, 2),  # FRIEND_GETNAME(index) -> username, previous username
    3604: (1, 1, 0, 0),  # FRIEND_SETRANK(username, rank)
    3605: (0, 1, 0, 0),  # FRIEND_ADD(username)
    3606: (0, 1, 0, 0),  # FRIEND_DEL(username)
    3607: (0, 1, 0, 0),  # IGNORE_ADD(username)
    3608: (0, 1, 0, 0),  # IGNORE_DEL(username)
    3609: (0, 1, 1, 0),  # FRIEND_TEST(username) -> boolean
    3617: (0, 1, 0, 0),  # CLAN_KICKUSER(username)
    3619: (0, 1, 0, 0),  # CLAN_JOINCHAT(username)
    3622: (1, 0, 0, 2),  # IGNORE_GETNAME(index) -> username, previous username
    5001: (3, 0, 0, 0),  # CHAT_SETFILTER(public, private, trade)
    5002: (2, 1, 0, 0),  # CHAT_SENDABUSEREPORT(username, type, rule)
    5008: (1, 1, 0, 0),  # CHAT_SENDPUBLIC(mes, type)
    5009: (0, 2, 0, 0),  # CHAT_SENDPRIVATE(username, mes)
    5010: (2, 1, 0, 0),  # CHAT_SENDCLAN(mes, ...)
    5018: (1, 0, 1, 0),  # CHAT_GETNEXTUID(mesuid) -> mesuid
    5019: (1, 0, 1, 0),  # CHAT_GETPREVUID(mesuid) -> mesuid
    5020: (0, 1, 0, 0),  # DOCHEAT(text) -- the chatbox's "::foo" handler
    5021: (0, 1, 0, 0),  # CHAT_SETMESSAGEFILTER(string)
    5024: (1, 0, 0, 0),  # CHAT_SETTIMESTAMPS(mode)
    # The four chat-history readers. Not on the friends path and not reachable
    # today (CHAT_GETHISTORYLENGTH answers 0, which gates script 89's loop off),
    # but their shapes were measured in the same pass and a wrong shape here is
    # the same silent desync as any other.
    5003: (2, 0, 3, 3),  # CHAT_GETHISTORY_BYTYPEANDLINE(chattype, line)
    5004: (1, 0, 3, 3),  # CHAT_GETHISTORY_BYUID(mesuid)
    5030: (2, 0, 4, 4),  # CHAT_GETHISTORYEX_BYTYPEANDLINE(chattype, line)
    5031: (1, 0, 4, 4),  # CHAT_GETHISTORYEX_BYUID(mesuid)
    2702: (1, 0, 1, 0),  # IF_HASSUB(component) -> bool; gates gameframe tab reveal (script 908)
    2704: (5, 0, 0, 0),  # IF_SETPARAM(param, value, uid, child, type) — xrsps; was misnamed HASCHILD
    2705: (2, 0, 1, 0),  # IF_HASCHILD_OVERLAY(widget, parent) -> bool
    # Sort-builder families for the friend / ignore / clan lists (3628..3657).
    # These build a sort spec imperatively: CLEAR, then one ADD_* per key (each
    # taking a single "descending?" flag), then APPLY. The panels that use them
    # are the friends, clan and account tabs, so with no signature the clan tab
    # asserted the moment it mounted (CLAN_SORT_CLEAR 3644, script 1658). No host
    # state backs them yet — the ordering the list ends up in is simply whatever
    # the underlying enumeration gives.
    3628: (0, 0, 0, 0),  # FRIEND_SORT_CLEAR
    3629: (1, 0, 0, 0),  # FRIEND_SORT_ADD_NAME(desc)
    3630: (1, 0, 0, 0),  # FRIEND_SORT_ADD_WORLD(desc)
    3631: (1, 0, 0, 0),  # FRIEND_SORT_ADD_RANK(desc)
    3632: (1, 0, 0, 0),  # FRIEND_SORT_ADD_NAME_LEGACY(desc)
    3633: (1, 0, 0, 0),  # FRIEND_SORT_ADD_5(desc)
    3634: (1, 0, 0, 0),  # FRIEND_SORT_ADD_6(desc)
    3635: (1, 0, 0, 0),  # FRIEND_SORT_ADD_7(desc)
    3636: (1, 0, 0, 0),  # FRIEND_SORT_ADD_8(desc)
    3637: (1, 0, 0, 0),  # FRIEND_SORT_ADD_9(desc)
    3638: (1, 0, 0, 0),  # FRIEND_SORT_ADD_10(desc)
    3639: (0, 0, 0, 0),  # FRIEND_SORT_APPLY
    3640: (0, 0, 0, 0),  # IGNORE_SORT_CLEAR
    3641: (1, 0, 0, 0),  # IGNORE_SORT_ADD_NAME(desc)
    3642: (1, 0, 0, 0),  # IGNORE_SORT_ADD_2(desc)
    3643: (0, 0, 0, 0),  # IGNORE_SORT_APPLY
    3644: (0, 0, 0, 0),  # CLAN_SORT_CLEAR
    3645: (1, 0, 0, 0),  # CLAN_SORT_ADD_NAME(desc)
    3646: (1, 0, 0, 0),  # CLAN_SORT_ADD_RANK(desc)
    3647: (1, 0, 0, 0),  # CLAN_SORT_ADD_WORLD(desc)
    # 3648..3654 continue the same ADD run (the vendored name list stops early).
    # 3648 and 3655 are confirmed directly from script 1658's bytecode -- it does
    # `PUSH_CONSTANT_INT 1; _3648` (one flag) and then a bare `_3655` (the apply).
    # The rest are inferred from their position in the run; a wrong guess is loud
    # rather than silent (the stub underflows and names the opcode), and leaving
    # them unsigned asserts anyway.
    3648: (1, 0, 0, 0),  # CLAN_SORT_ADD_* (confirmed: pops one flag)
    3649: (1, 0, 0, 0),
    3650: (1, 0, 0, 0),
    3651: (1, 0, 0, 0),
    3652: (1, 0, 0, 0),
    3653: (1, 0, 0, 0),
    3654: (1, 0, 0, 0),  # FRIENDSCHAT_SORT_ADD(desc)
    3655: (0, 0, 0, 0),  # FRIENDSCHAT_SORT apply (confirmed: no args)
    3656: (0, 0, 0, 0),  # CLAN_SORT_APPLY
    3657: (1, 0, 0, 0),  # FRIENDSCHAT_SORT_ADD_RANK(desc)
    # ACTIVECLANSETTINGS/CHANNEL FIND_* (3800/3801, 3850/3851): pop clanType,
    # push bool. Script 84 (side_channels init) does `push 0; FIND_AFFINED;
    # push 1; BRANCH_EQUALS` — without a signature StackMetaStub asserts and
    # aborts the panel. Stub pushes 0 (no clan) so the not-found branch runs;
    # the subsequent GET* ops are skipped.
    3800: (1, 0, 1, 0),  # ACTIVECLANSETTINGS_FIND_LISTENED(clanType) -> bool
    3801: (1, 0, 1, 0),  # ACTIVECLANSETTINGS_FIND_AFFINED(clanType) -> bool
    3850: (1, 0, 1, 0),  # ACTIVECLANCHANNEL_FIND_LISTENED(clanType) -> bool
    3851: (1, 0, 1, 0),  # ACTIVECLANCHANNEL_FIND_AFFINED(clanType) -> bool
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
    # RESUME_COUNTDIALOG(text) — the answer to a server script parked on
    # P_COUNTDIALOG. The cache's own scripts spell it `resume_countdialog(
    # tostring($n))`, so the number arrives as a STRING; the wire packet
    # (RESUME_P_COUNTDIALOG) carries an int and the conversion is the host's,
    # exactly as it is for the chatbox's own "Enter amount" path in app.c.
    # Dedicated dispatch in cs2vm2.c forwards it to the host
    # (CS2VM_HOST_REQUEST_RESUME_COUNTDIALOG).
    #
    # It sat at the (0,0,0,0) default with known=0, which is not benign: the
    # bank PIN keypad's fourth digit is the one site in this cache that calls
    # it, and StackMetaStub asserted there rather than sending the PIN.
    3104: (0, 1, 0, 0),  # RESUME_COUNTDIALOG(text)
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
    #
    # The 8000-series array family (arrays are handles on the STRING stack at
    # this revision). 8000/8007 already carry real doc comments in cs2_opcode.h
    # and need nothing here; the rest were unreachable until MAX_OPCODE widened
    # past the old 7602 ceiling. Tuples below are from local_commands.py /
    # call-site evidence against cache.osrs239, not guessed. 8019 was corrected
    # 2026-08-03: it pushes the joined string (script 9153 → gosub 9182; xrsps
    # ARRAY_JOIN). 8023/8024 are the Overview-tab resize/append pair.
    8001: (3, 0, 0, 0),  # ARRAY_SORT_BY(array, start, end) — ints only in older deob
    8003: (0, 1, 1, 0),  # ARRAY_LENGTH(handle) -> int
    8012: (0, 1, 0, 0),  # _8012(handle) — mutate in place; meaning unknown
    8018: (0, 2, 0, 1),  # ARRAY_SPLIT(string, sep) -> handle
    8019: (0, 2, 0, 1),  # ARRAY_JOIN(handle, sep) -> string
    8021: (2, 0, 0, 1),  # _8021(int, int) -> string
    8022: (3, 0, 0, 1),  # ARRAY_NEW(type, length, capacity) -> handle
    8023: (1, 1, 0, 0),  # ARRAY_SETLENGTH(handle, n)
    8024: (2, 1, 0, 0),  # ARRAY_APPEND(handle, value, type) — int-typed form
    4036: (0, 1, 1, 0),  # STRING_TO_INT(string) -> int
    # Loot-tracker native store (7600-family). Arities from local_commands.py,
    # each verified at call sites in scripts 7166/4298/4452/7200/1792.
    7601: (0, 0, 1, 0),  # LOOT_SOURCE_COUNT() -> int
    7602: (1, 0, 0, 1),  # LOOT_SOURCE_NAME(id) -> string
    7603: (0, 1, 1, 0),  # LOOT_SOURCE_ITEMCOUNT(name) -> int
    7604: (0, 1, 1, 0),  # LOOT_SOURCE_TOTALVAL(name) -> int
    7605: (3, 0, 1, 0),  # LOOT_BEGIN_QUERY(start, limit, kind) -> count
    7606: (1, 0, 1, 0),  # LOOT_QUERY_ID(index) -> id
    7608: (0, 0, 1, 0),  # LOOT_AUX_COUNT_TOTAL() -> int
    7609: (0, 1, 1, 0),  # LOOT_ROW_COUNT_BYNAME(name) -> int
    7610: (1, 0, 1, 0),  # LOOT_ROW_COUNT_BYID(id) -> int
    7611: (1, 1, 2, 0),  # LOOT_ROW_BYNAME(name, index) -> (obj_id, qty)
    7612: (2, 0, 2, 0),  # LOOT_ROW_BYID(id, index) -> (obj_id, qty)
    7613: (0, 0, 0, 0),  # LOOT_CLEAR_ALL()
    7614: (0, 1, 0, 0),  # LOOT_CLEAR_SOURCE(name)
    7615: (1, 0, 0, 0),  # LOOT_REMOVE_BYID(id)
    7616: (0, 1, 0, 0),  # LOOT_IGNORE_ADD(name) — item ignore
    7617: (0, 1, 0, 0),  # LOOT_IGNORE_REMOVE(name)
    7619: (0, 0, 1, 0),  # LOOT_GROUND_COUNT() -> int (item-ignore count)
    7620: (1, 0, 0, 1),  # LOOT_GROUND_NAME(index) -> string (1-based)
    7621: (0, 0, 0, 0),  # LOOT_IGNORE_CLEAR()
    7622: (0, 1, 0, 0),  # LOOT_SOURCE_IGNORE_ADD(name)
    7623: (0, 1, 0, 0),  # LOOT_SOURCE_IGNORE_REMOVE(name)
    7625: (0, 0, 1, 0),  # LOOT_SRCLIST_COUNT() -> int (source-ignore count)
    7626: (1, 0, 0, 1),  # LOOT_SRCLIST_NAME(index) -> string (1-based)
    7628: (3, 1, 0, 0),  # LOOT_ADD(name, obj, qty, event_id)
    7630: (1, 0, 0, 1),  # LOOT_SOURCE_NAME2(id) -> string
    6754: (1, 0, 0, 1),  # NC_NAME(npc) -> string
    # Loot aux-list ops (7400-family).
    7400: (1, 1, 0, 0),  # LOOT_AUX_UPSERT2(kind, name)
    7401: (2, 1, 0, 0),  # LOOT_AUX_UPSERT(kind, name, flag)
    7404: (2, 1, 0, 0),  # LOOT_AUX_REMOVE(kind, name, flag)
    7406: (2, 0, 0, 1),  # LOOT_AUX_GET(kind, index) -> string
    7407: (1, 0, 1, 0),  # LOOT_AUX_COUNT(kind) -> int
    7408: (3, 1, 1, 0),  # LOOT_AUX_LOOKUP(kind, name, arg3, arg4) -> int
    7409: (1, 0, 0, 0),  # LOOT_AUX_CLEAR(kind)
    # Hiscores stubs (7809/7811). Arities from local_commands.py.
    7809: (0, 0, 1, 0),  # HISCORES_STATUS() -> int (non-2 = not success)
    7811: (0, 0, 0, 1),  # HISCORES_ERROR() -> string
}


# Opcodes where this file and 3rd/rscache/src/cs2/cs2_command.gen.h disagree,
# with the value THAT table holds. The bridge never overrides, so nothing here
# changes the generated header -- the point is that every disagreement has been
# looked at, and that a *new* one fails generation instead of passing silently.
# A disagreement means one of the two tables is wrong about a real opcode, and
# that is exactly the class of bug that produces a plausible decompile of a
# different program.
#
# Three classes, all measured 2026-08-02:
#
#  (a) CC_SET* / IF_SET* / CC_DELETEALL (102, 1006..1205, 2006..2202). Ours is
#      the `IF_SET`/`CC_SET` name heuristic's (0,0,0,0), which is a marker, not
#      a claim: every one of these has dedicated dispatch in cs2vm2.c that pops
#      its own arguments, so the table row only feeds the debug trace. The
#      decompiler's counts are the real ones and are almost certainly right;
#      adopting them would change no runtime behaviour and is left undone
#      because "change 40 rows in the UI hot path for a trace improvement" is
#      not a trade worth making inside this item.
#  (b) rows where OUR value is the measured one and the vendored table is stale
#      or era-wrong: 4201/4202 (oc_op/oc_iop take the op slot as the *operand*,
#      not off the stack -- see MANUAL_STACK), 3800/3850 (no-arg in this cache,
#      see MANUAL_STACK), 8000 (arrays are STRING-stack handles at rev 239 --
#      docs/cs2-arrays-are-handles.md).
#  (c) rows where the vendored table looks right and ours is a heuristic guess,
#      but which nothing reachable exercises today: 202, 3129, 3140, 3656,
#      4104, 4200, 4210, 6618..6696, 7506. Left as-is rather than flipped
#      blind; each needs its own witness before it moves.
BRIDGE_CONFLICTS_OK: dict[int, tuple[int, int, int, int]] = {
    102: (1, 0, 0, 0),   # cc_deleteall
    202: (1, 0, 1, 0),   # _203 (id 202 in that table's naming)
    1006: (1, 0, 0, 0),  # cc_setnoscrollthrough
    1100: (2, 0, 0, 0),  # cc_setscrollpos
    1104: (1, 0, 0, 0),  # cc_setlinewid
    1106: (1, 0, 0, 0),  # cc_set2dangle
    1108: (1, 0, 0, 0),  # cc_setmodel
    1109: (6, 0, 0, 0),  # cc_setmodelangle
    1110: (1, 0, 0, 0),  # cc_setmodelanim
    1111: (1, 0, 0, 0),  # cc_setmodelorthog
    1118: (1, 0, 0, 0),  # cc_setvflip
    1119: (1, 0, 0, 0),  # cc_sethflip
    1120: (2, 0, 0, 0),  # cc_setscrollsize
    1123: (1, 0, 0, 0),  # cc_setfillcolour
    1126: (1, 0, 0, 0),  # cc_setlinedirection
    1127: (1, 0, 0, 0),  # cc_setmodeltransparent
    1201: (1, 0, 0, 0),  # cc_setnpchead
    1205: (2, 0, 0, 0),  # cc_setobject_nonum
    2006: (2, 0, 0, 0),  # if_setnoscrollthrough
    2104: (2, 0, 0, 0),  # if_setlinewid
    2106: (2, 0, 0, 0),  # if_set2dangle
    2107: (2, 0, 0, 0),  # if_settiling
    2109: (7, 0, 0, 0),  # if_setmodelangle
    2110: (2, 0, 0, 0),  # if_setmodelanim
    2111: (2, 0, 0, 0),  # if_setmodelorthog
    2117: (2, 0, 0, 0),  # if_setgraphicshadow
    2118: (2, 0, 0, 0),  # if_setvflip
    2119: (2, 0, 0, 0),  # if_sethflip
    2123: (2, 0, 0, 0),  # if_setfillcolour
    2126: (2, 0, 0, 0),  # if_setlinedirection
    2201: (2, 0, 0, 0),  # if_setnpchead
    2202: (1, 0, 0, 0),  # if_setplayerhead_self
    3129: (2, 0, 0, 0),  # _3129
    3140: (0, 0, 0, 0),  # _3140
    3656: (1, 0, 0, 0),  # _3656 (CLAN_SORT_APPLY here; see MANUAL_STACK)
    3800: (0, 0, 1, 0),  # activeclansettings_find_listened
    3850: (0, 0, 1, 0),  # activeclanchannel_find_listened
    4104: (1, 0, 0, 1),  # fromdate
    4200: (1, 0, 0, 1),  # oc_name
    4201: (2, 0, 0, 1),  # oc_op
    4202: (2, 0, 0, 1),  # oc_iop
    4210: (1, 1, 1, 0),  # oc_find
    6618: (1, 0, 4, 0),  # _6618
    6623: (1, 0, 2, 0),  # _6623
    6638: (2, 0, 2, 0),  # _6638
    6639: (0, 0, 4, 0),  # worldmap_listelement_start
    6640: (0, 0, 4, 0),  # worldmap_listelement_next
    6693: (1, 0, 0, 2),  # mec_text
    6695: (1, 0, 2, 0),  # mec_category
    6696: (1, 0, 2, 0),  # mec_sprite
    7506: (1, 0, 1, 0),  # db_getrow
    8000: (1, 0, 0, 0),  # _8000
}


def parse_string_protos() -> set[str]:
    """The prototype ids whose base type lands on the STRING stack.

    `RSCache_CS2_TypeStack` (cs2_types.c:109) is a single line: only
    RSCACHE_CS2_TYPE_STRING is a string-stack type, everything else is an int.
    So the set is every proto declared CS2_PLAIN(STRING)/CS2_NAMED(STRING, ...)
    -- read out of the file rather than transcribed, so a new named string
    prototype cannot silently be counted as an int here.
    """
    text = CS2_TYPES_C.read_text()
    protos: set[str] = set()
    for m in re.finditer(
        r"\[RSCACHE_CS2_PROTO_([A-Z0-9_]+)\]\s*=\s*CS2_(?:PLAIN|NAMED)\(\s*([A-Z0-9_]+)",
        text,
    ):
        if m.group(2) == "STRING":
            protos.add("RSCACHE_CS2_PROTO_" + m.group(1))
    if "RSCACHE_CS2_PROTO_STRING" not in protos:
        raise SystemExit(f"{CS2_TYPES_C}: could not find the prototype table")
    return protos


def parse_command_gen() -> tuple[dict[int, tuple[int, int, int, int]], int]:
    """Signatures from the decompiler's table, as (int_in, str_in, int_out, str_out).

    Only RSCACHE_CS2_CMD_BASIC rows are returned. The other kinds are variadic
    by construction -- DB_FIND/DB_GETFIELD take their stack shape from the
    column at run time, CLIENTSCRIPT from its trigger's arg string, PARAM from
    the param's type -- so their arg/def counts in that table are placeholders,
    not signatures, and every one of them already has dedicated dispatch in
    cs2vm2.c. Handing a placeholder to StackMetaStub would be exactly the
    silent desync this file exists to avoid.

    Returns (signatures, table_size).
    """
    text = COMMAND_GEN_H.read_text()

    size_m = re.search(r"#define\s+RSCACHE_CS2_OPCODE_TABLE_SIZE\s+(\d+)", text)
    if not size_m:
        raise SystemExit(f"{COMMAND_GEN_H}: no RSCACHE_CS2_OPCODE_TABLE_SIZE")
    table_size = int(size_m.group(1))

    pool_m = re.search(r"cs2_proto_pool\[\]\s*=\s*\{(.*?)\n\};", text, re.DOTALL)
    if not pool_m:
        raise SystemExit(f"{COMMAND_GEN_H}: no cs2_proto_pool")
    pool = [p.strip() for p in pool_m.group(1).replace("\n", " ").split(",") if p.strip()]

    string_protos = parse_string_protos()
    out: dict[int, tuple[int, int, int, int]] = {}
    row = re.compile(
        r"\[(\d+)\]\s*=\s*\{\s*(?:\"[^\"]*\"|NULL)\s*,\s*(RSCACHE_CS2_CMD_[A-Z_]+)\s*,"
        r"\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,"
    )
    for m in row.finditer(text):
        if m.group(2) != "RSCACHE_CS2_CMD_BASIC":
            continue
        op = int(m.group(1))
        arg_off, arg_count = int(m.group(3)), int(m.group(4))
        def_off, def_count = int(m.group(5)), int(m.group(6))
        if arg_off + arg_count > len(pool) or def_off + def_count > len(pool):
            raise SystemExit(f"{COMMAND_GEN_H}: opcode {op} indexes past cs2_proto_pool")
        args = pool[arg_off:arg_off + arg_count]
        defs = pool[def_off:def_off + def_count]
        str_in = sum(1 for p in args if p in string_protos)
        str_out = sum(1 for p in defs if p in string_protos)
        out[op] = (arg_count - str_in, str_in, def_count - str_out, str_out)
    if not out:
        raise SystemExit(f"{COMMAND_GEN_H}: parsed no BASIC rows")
    return out, table_size


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
        return (1, 1, 1, 0)
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

    # ---- the bridge (source 4; see this file's docstring) -------------------
    bridge, cmd_table_size = parse_command_gen()

    conflicts = {
        op: (entries.get(op, (0, 0, 0, 0)), sig)
        for op, sig in sorted(bridge.items())
        if op in known and entries.get(op, (0, 0, 0, 0)) != sig
    }
    unacknowledged = {
        op: v for op, v in conflicts.items() if BRIDGE_CONFLICTS_OK.get(op) != v[1]
    }
    if unacknowledged:
        print(
            "gen_opcode_stack: the two signature tables disagree about opcodes that\n"
            "are NOT in BRIDGE_CONFLICTS_OK. One of the two is wrong about a real\n"
            "opcode; resolve it by hand (a wrong pop count desynchronises the operand\n"
            "stack silently) and then record the decision:",
            file=sys.stderr,
        )
        for op, (ours, theirs) in sorted(unacknowledged.items()):
            print(
                f"  opcode {op}: here {ours}, cs2_command.gen.h {theirs}",
                file=sys.stderr,
            )
        raise SystemExit(1)

    stale = sorted(set(BRIDGE_CONFLICTS_OK) - set(conflicts))
    if stale:
        print(
            "gen_opcode_stack: BRIDGE_CONFLICTS_OK lists opcodes that no longer "
            f"disagree; drop them: {stale}",
            file=sys.stderr,
        )
        raise SystemExit(1)

    # `known = 2`, not 1: the signature is real but *nothing in this repo
    # implements the opcode*, so StackMetaStub balances the stack and pushes
    # zeros. That is a silent wrong answer, which is worse than an abort unless
    # it is visible -- so the runtime reports every inherited opcode it reaches,
    # once each. Anything that has to distinguish "we know what this does" from
    # "we know its shape" reads the 2.
    inherited = set()
    for op, sig in bridge.items():
        if op in known or op >= MAX_OPCODE:
            continue
        entries[op] = sig
        known.add(op)
        inherited.add(op)
    bridged = len(inherited)

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
        "    /* 0 -- the opcode is unimplemented and unknown: it only got the",
        "     *      (0,0,0,0) default, and the runtime asserts on it rather than",
        "     *      no-oping with a made-up arity.",
        "     * 1 -- the signature above is real AND this repo knows what the",
        "     *      opcode does (a stack doc comment in cs2_opcode.h, a",
        "     *      MANUAL_STACK entry, or a name heuristic). Most of these also",
        "     *      have dedicated dispatch in cs2vm2.c.",
        "     * 2 -- the signature was inherited from the decompiler's table,",
        "     *      3rd/rscache/src/cs2/cs2_command.gen.h, and nothing here",
        "     *      implements the opcode. The stack stays balanced and the",
        "     *      results are zeros/\"\" -- a plausible wrong answer, so the",
        "     *      runtime prints one line the first time it reaches each. */",
        "    unsigned char known;",
        "};",
        "",
        "static struct CS2VM2OpcodeStack const g_cs2vm2_opcode_stack[CS2VM2_OPCODE_STACK_MAX] = {",
    ]
    for op in range(MAX_OPCODE):
        ii, si, io, so = entries.get(op, (0, 0, 0, 0))
        kn = 2 if op in inherited else (1 if op in known else 0)
        lines.append(f"    [{op}] = {{ {ii}, {si}, {io}, {so}, {kn} }},")
    lines.extend(["};", "", "#endif", ""])
    OUT.write_text("\n".join(lines))
    print(
        f"wrote {OUT} ({len(entries)} opcodes with metadata, "
        f"{len(known)} known; {bridged} inherited from cs2_command.gen.h "
        f"[table size {cmd_table_size}], {len(conflicts)} acknowledged conflicts)"
    )


if __name__ == "__main__":
    main()
