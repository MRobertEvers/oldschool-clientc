#!/usr/bin/env python3
"""Generate the ServerScript opcode/trigger tables from the LostCity engine.

    python3 src/serverscript/gen_opcode_meta.py [path-to-LostCity_Server]

Outputs (checked in; this never runs at build time):

    ss_opcode.h        SS_OP_* ids, SS_OPCODE_MAX, SS_OPCODE_COUNT
    ss_trigger.h       SS_TRIGGER_* ids, SS_TRIGGER_MAX
    ss_meta.gen.h      opcode-name table, opcode-meta table, trigger-name table

Sources, all inside the reference server:

    engine/src/engine/script/ScriptOpcode.ts          opcode id <-> name
    engine/src/engine/script/ServerTriggerType.ts     trigger id <-> name
    engine/src/engine/script/ScriptOpcodePointers.ts  require / require2
    content/scripts/engine.rs2                        arity and argument types

engine.rs2 is the authoritative signature file: every command the language can
call is declared there as `[command,name](args)(returns)`, so the arity table is
derived rather than hand-typed. That matters because a wrong arity is not a
crash — it silently pops the wrong number of values and corrupts the stack for
everything after it.

Modeled on src/cs2vm2/gen_opcode_stack.py, including its convention that manual
overrides live in a dict up top and each carries a comment saying why.
"""

import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_REF = HERE.parent.parent.parent / "LostCity_Server"

# ---------------------------------------------------------------------------
# Manual overrides
# ---------------------------------------------------------------------------

# Opcodes with no `[command,...]` entry in engine.rs2. Two kinds:
#
#   - core language ops (0..46), whose arity is structural rather than declared;
#   - a handful of commands the reference implements but never declared.
#
# (int_in, str_in, int_out, str_out). Anything absent here AND absent from
# engine.rs2 gets known=0 and dispatches to the loud stub.
MANUAL_META: dict[str, tuple[int, int, int, int]] = {
    # --- core language (ids 0..46) -----------------------------------------
    # Operand-carrying pushes take nothing off the stack.
    "PUSH_CONSTANT_INT": (0, 0, 1, 0),
    "PUSH_CONSTANT_STRING": (0, 0, 0, 1),
    "PUSH_VARP": (0, 0, 1, 0),
    "POP_VARP": (1, 0, 0, 0),
    "PUSH_VARN": (0, 0, 1, 0),
    "POP_VARN": (1, 0, 0, 0),
    "PUSH_VARS": (0, 0, 1, 0),
    "POP_VARS": (1, 0, 0, 0),
    "PUSH_VARBIT": (0, 0, 1, 0),
    "POP_VARBIT": (1, 0, 0, 0),
    "PUSH_INT_LOCAL": (0, 0, 1, 0),
    "POP_INT_LOCAL": (1, 0, 0, 0),
    "PUSH_STRING_LOCAL": (0, 0, 0, 1),
    "POP_STRING_LOCAL": (0, 1, 0, 0),
    "POP_INT_DISCARD": (1, 0, 0, 0),
    "POP_STRING_DISCARD": (0, 1, 0, 0),
    "BRANCH": (0, 0, 0, 0),
    "BRANCH_NOT": (2, 0, 0, 0),
    "BRANCH_EQUALS": (2, 0, 0, 0),
    "BRANCH_LESS_THAN": (2, 0, 0, 0),
    "BRANCH_GREATER_THAN": (2, 0, 0, 0),
    "BRANCH_LESS_THAN_OR_EQUALS": (2, 0, 0, 0),
    "BRANCH_GREATER_THAN_OR_EQUALS": (2, 0, 0, 0),
    "SWITCH": (1, 0, 0, 0),
    # RETURN pops nothing: the callee's return values are already on the stack
    # and the caller reads them there.
    "RETURN": (0, 0, 0, 0),
    # GOSUB/JUMP pop the script id (they are declared in engine.rs2 as taking a
    # proc/label, but the operand is the dot flag, not the id).
    "GOSUB": (1, 0, 0, 0),
    "JUMP": (1, 0, 0, 0),
    # GOSUB_WITH_PARAMS/JUMP_WITH_PARAMS carry the script id as the operand and
    # pop as many args as the callee declares, which is only knowable at run
    # time. Handled structurally by the VM; the table must not claim an arity.
    "GOSUB_WITH_PARAMS": (0, 0, 0, 0),
    "JUMP_WITH_PARAMS": (0, 0, 0, 0),
    # JOIN_STRING pops `operand` strings. Variadic on the operand, not on a type
    # string, so it gets its own handling in the VM.
    "JOIN_STRING": (0, 0, 0, 1),
    # Arrays: the reference throws 'unimplemented' on all three and the corpus
    # never emits them. Declared so the verifier's depth model stays honest.
    "DEFINE_ARRAY": (1, 0, 0, 0),
    "PUSH_ARRAY_INT": (1, 0, 1, 0),
    "POP_ARRAY_INT": (2, 0, 0, 0),

    # --- commands the reference implements but never declared ---------------
    # Read straight off their handlers; engine.rs2 has no entry for any of them.
    "STAT_TOTAL": (0, 0, 1, 0),   # PlayerOps.ts: sums baseLevels -> int
    "MAP_LOC": (1, 0, 1, 0),      # ServerOps.ts: any active loc covers coord
    "NC_VISLEVEL": (1, 0, 1, 0),  # NpcConfigOps.ts: npc -> vislevel
    "TEXT_SWITCH": (1, 2, 0, 1),  # StringOps.ts: (int, str, str) -> str
    #
    # LC_OP, OC_IOP and OC_OP are deliberately absent: the reference declares
    # the opcode ids but implements no handler, so their arity is genuinely
    # unknown. Leaving them known=0 routes them to the loud stub, which is the
    # correct outcome — guessing an arity here would silently corrupt the stack.
}

# ---------------------------------------------------------------------------
# Commands this engine has and the reference does not
# ---------------------------------------------------------------------------
#
# LostCity's engine speaks the 2004 protocol, where every interface component
# the cache marks as a button is clickable the moment it is drawn. At rev 230
# the opposite holds: **nothing is clickable until the server says so**, per
# component and per op, with IF_SETEVENTS. Without it no content script can arm
# a widget, and every "clicking it does nothing" in the gameframe is this.
#
# The reference does not implement these, so `parse_engine_rs2` finds no
# signature to derive. It is worth being exact about what it *does* have, since
# "absent" and "commented out" are different claims:
#
#   engine.rs2:1042  // [command,if_setevents](component, int, int, boolean,
#                    //                        int, int, int, boolean, boolean)
#
# — a commented-out nine-argument form, carried over from a later RuneScript
# dialect that spells the mask out as separate flags. It is NOT the shape used
# here. The wire packet is (component, start_slot, end_slot, events-bitmask),
# and that is what this declares, matching what OpenRune's `ifSetEvents` and
# xrsps's `set_flags_range` both send. Reproducing the nine-argument form would
# be reproducing a declaration the reference never wired to anything, against a
# packet that does not have those fields.
#
# Ids live at 11000+, one band past the reference's highest (10003), so a
# future LostCity opcode can never collide with one of these. Format matches
# MANUAL_META: name -> (id, int_in, str_in, int_out, str_out).
EXTRA_OPCODES: dict[str, tuple[int, int, int, int, int]] = {
    # if_setevents(component, from_slot, to_slot, events)
    #
    # `from`/`to` are the sub-id range for a grid (an inventory's 28 slots);
    # a plain component uses 0,0. `events` is the rev-230 bitmask: bit 0 is
    # the op-less click, bits 1..10 are ops 1..10, and the high bits are the
    # drag/target flags.
    "IF_SETEVENTS": (11000, 4, 0, 0, 0),

    # if_opensub(component, interface, type)
    #
    # Mount an interface *inside* a component of an already-open one. The
    # reference's if_openmain / if_openside / if_openoverlay are the 2004
    # vocabulary — a handful of fixed slots named by the command — and at rev
    # 230 that is not enough vocabulary: the gameframe alone has 24 slots, and
    # panels nest (the side journal's five tabs all mount into 629:43). The
    # target is an ordinary component, so it is an argument.
    #
    # `type` is the reference's: 0 modal, 1 overlay, 3 side-modal.
    "IF_OPENSUB": (11001, 3, 0, 0, 0),

    # if_closesub(component)
    #
    # The inverse of the above, and it needs to be a separate command for the
    # same reason if_opensub does: the slot is an argument. The reference's
    # `if_close` (2033) closes "the interface", singular, because a 2004
    # gameframe has one modal slot; at rev 230 a panel that mounted itself into
    # a named component has to name that component again to come out, and this
    # server's IF_CLOSE is specialised to the chatbox modal (the dialogue
    # unmount) — every `[if_close]` caller in the tree wants exactly that, so it
    # cannot be generalised in place.
    #
    # No require mask: the encoder writes to `srv->active_player`, which is the
    # same access if_opensub has.
    "IF_CLOSESUB": (11005, 1, 0, 0, 0),

    # if_opentop(interface)
    #
    # Open a gameframe root (rev-230 IF_OPENTOP) and remount the HUD/tabs from
    # the content `gameframe.enum` block named after that interface. LostCity
    # has no equivalent — rev 254 is fixed-only. OpenRune's ifOpenTop +
    # GameframeLoader.mount is the shape.
    "IF_OPENTOP": (11006, 1, 0, 0, 0),

    # if_movesub(component, component)
    #
    # Move a mounted sub from one gameframe slot to another (rev-230
    # IF_MOVESUB). Args are source then destination (declaration order); the
    # wire encodes destination then source.
    "IF_MOVESUB": (11007, 2, 0, 0, 0),

    # runclientscript_ss(clientscript, string, string)
    #
    # Run a clientscript with two string arguments. The 2004 protocol has no
    # RUNCLIENTSCRIPT at all, so the reference has nothing to port here either.
    #
    # It exists for one shape the rev-230 UI has and 2004 does not: an interface
    # whose contents are *built by a clientscript* rather than shipped as
    # components. The multi-choice dialogue is the case that forced it —
    # `chatmenu` (219) has exactly two components, a root and an empty layer, and
    # its option rows are `cc_create`d by `chatbox_multi_init(title, options)`.
    # There is nothing for `if_settext` to address, so without this the server
    # cannot put a choice on screen at all.
    #
    # The clientscript id is an ARGUMENT, not baked into the command name: it is
    # a cache id, and the engine holding one is the arrangement this server is
    # organised against. Content names it (see interface_chat/configs/chat.constant).
    "RUNCLIENTSCRIPT_SS": (11002, 1, 2, 0, 0),

    # runclientscript*(clientscript)(args...) — the general form of the above.
    #
    # RUNCLIENTSCRIPT_SS is fixed at one int and two strings because the one
    # caller it was written for (chatbox_multi_init) takes exactly that. The
    # wire is not: RUNCLIENTSCRIPT carries a per-argument type string, and
    # `mock230_send_run_clientscript_mixed` has taken one since it was written.
    # Everything else in the rev-230 UI needs other shapes — the skill guide's
    # clientscript 1902 takes four ints — and there is no way to spell that
    # with the _SS form.
    #
    # So: the vararg form, in the reference's own vararg convention.
    # `queue*(queue, delay)(args...)` compiles to QUEUEVARARG (2094) with the
    # declared arguments first, the vararg values next, and a type string on
    # top describing them; `parse_command` builds that type string from the
    # static type of each vararg expression. The arity below counts only the
    # fixed part — the script id — exactly as QUEUEVARARG's {2,0,0,0,1,1,...}
    # counts only `queue, delay`. The variadic flag is what says the rest is on
    # the stack, and it is why the VM refuses to *stub* this rather than
    # popping a wrong number of values (unimplemented_stub in ssvm.c).
    #
    # RUNCLIENTSCRIPT_SS stays. It is spelled by content already, and a command
    # that can be written without the star is the cheaper thing to read.
    "RUNCLIENTSCRIPTVARARG": (11003, 1, 0, 0, 0),

    # p_countdialog_noprompt — park the script for a number, WITHOUT opening the
    # engine's own "Enter amount" prompt.
    #
    # `p_countdialog` (2071) is two things at once: it writes PCountDialog and
    # it sets ScriptState.COUNTDIALOG (PlayerOps.ts, and this engine copies it
    # exactly). At 2004 those are inseparable, because the chatbox prompt is the
    # only thing that can produce a number.
    #
    # At rev 230 it is not. `resume_countdialog` is a plain CS2 opcode (3104),
    # so ANY interface can answer a parked script, and the cache ships one that
    # does: the bank PIN keypad (213) assembles four clicked digits and sends
    # the whole number itself. Calling `p_countdialog` to wait for it would pop
    # the chatbox prompt over the keypad — which echoes the digits as they are
    # typed and so defeats the exact thing the keypad exists for.
    #
    # The reference offers no design: `p_countdialog` is the only shape it has,
    # and there is no bank PIN anywhere in it (zero hits, content or engine).
    # So this is PORTING_GUIDE §5.1 — the client already implements the
    # feature, and the server's job is to drive it. The split is the smallest
    # one that says so: the packet stays with `p_countdialog`, and the *wait*
    # becomes something content can ask for on its own.
    #
    # Same require mask as 2071 (see EXTRA_POINTERS): it parks the active
    # player's script, so it needs protected access to that player.
    "P_COUNTDIALOG_NOPROMPT": (11004, 0, 0, 0, 0),

    # if_getmain()(interface)
    #
    # Which interface is mounted in the gameframe mainmodal slot, or null.
    # LostCity has no reader for this — its content never needed to ask which
    # modal was up, only whether *a* modal was (`busy`). At rev 230 the bank
    # embeds equipment-bonus rows inside bankmain while the worn tab opens a
    # separate equipment interface; content that repaints those rows on a worn
    # change has to know which view is mounted, and inventing a content-side
    # flag beside the encoder's own `mainmodal_group` would drift. Returns the
    # same id `if_openmain` / `if_openmain_side` wrote, or null when nothing is
    # mounted (the encoder stores 0 on close; this maps that to null).
    "IF_GETMAIN": (11008, 0, 0, 1, 0),

    # ---- map instances (11009..11014) -------------------------------------
    #
    # A private copy of a piece of the map, assembled out of 8x8 zones taken
    # from anywhere in the cache: the POH, the Pest Control island, a Barrows
    # tunnel, a cutscene set. Six commands, and the reason there are six rather
    # than one is that both behaviour references agree on the same four steps
    # plus two queries — 2009scape `DynamicRegion` and Kronos `DynamicMap`:
    #
    #   allocate            reserveArea(8,8) / new DynamicMap()
    #   fill, zone by zone  replaceChunk(z,x,y,chunk,from) / DynamicChunk.pos()
    #   commit + show       spec.build() + updateSceneGraph() / map.build()
    #   address a tile      base.transform(dx,dz) / map.convertX/convertY
    #   release             flagInactive() / map.destroy()
    #   which instance?     RegionManager.forId(id) is DynamicRegion
    #
    # LostCity has NONE of this, and that is measured rather than assumed:
    # `engine.rs2` declares no map-allocation command (its only near misses are
    # the commented-out `region_findbycoord` / `controller_*` block at
    # engine.rs2:1051-1060, wired to nothing in Engine-TS), `BuildArea.ts`
    # always writes RebuildNormal, and there is no construction content in the
    # tree at all. So there is no reference name to port and no reference
    # signature to copy — which is exactly why these sit in the EXTRA band
    # rather than being invented into the reference's own 1000+ map band.
    #
    # PORTING_GUIDE §2.4 item 4 is the whole justification for the shape: the
    # 2009scape port of the POH and of Pest Control cannot be written as content
    # without a way to *say* "give me a private map", and the alternative on the
    # table was a C house-builder. The engine gets the mechanism (reserve, copy
    # zones, encode the scene), content keeps the policy (which zones, which
    # rotation, where the player lands, when it is released).

    # map_instance_alloc(int $zone_w, int $zone_h)(int)
    #
    # Reserve an unused rectangle of the map, `$zone_w` x `$zone_h` zones, and
    # return a handle — or -1 when the pool is exhausted, which content must
    # check (an instance is a resource, not a coordinate). Zones rather than
    # tiles or map squares because a zone is the unit the fill step and the wire
    # both work in; the allocator rounds up to whole map squares itself, since
    # that is the granularity the cache's own archives come in.
    "MAP_INSTANCE_ALLOC": (11009, 2, 0, 1, 0),

    # map_instance_setchunk(int $handle, int $level, int $zone_x, int $zone_z,
    #                       coord $src, int $rotation)
    #
    # Copy one 8x8 zone of the cache into one zone of the instance, turned
    # `$rotation` quarter-turns clockwise. `$level`/`$zone_x`/`$zone_z` are
    # instance-relative (0-based); `$src` is any coord inside the source zone
    # and its plane is the source plane, so content can name a source with
    # `movecoord` off a landmark instead of doing shift arithmetic.
    #
    # A zone nothing sets stays void, which is what makes an empty house floor
    # empty rather than a copy of whatever the pool's previous tenant put there.
    "MAP_INSTANCE_SETCHUNK": (11010, 6, 0, 0, 0),

    # map_instance_build(int $handle)
    #
    # Commit the zones set so far: rebuild the server's collision over the
    # instance and re-send the scene to anyone standing in it. Separate from
    # setchunk because a house is dozens of zones and rebuilding collision
    # per zone would be quadratic — and because a half-assembled instance is
    # not a thing a player should ever be shown.
    "MAP_INSTANCE_BUILD": (11011, 1, 0, 0, 0),

    # map_instance_coord(int $handle, int $dx, int $dz, int $level)(coord)
    #
    # The instance-relative offset as an absolute coord. Content cannot compute
    # this: the base is whatever the allocator handed out. Kronos spells the
    # same thing `map.convertX(absX)` / `convertY`, 2009scape
    # `region.getBaseLocation().transform(dx, dz, 0)`. Returns null for a dead
    # handle rather than a coord in the void.
    "MAP_INSTANCE_COORD": (11012, 4, 0, 1, 0),

    # map_instance_free(int $handle)
    #
    # Release the reservation. Not automatic on the last player leaving —
    # 2009scape's 50-tick `checkInactive` pulse is a *policy* about when a
    # minigame is over, and policy is content's (a Pest Control lander that
    # empties for one tick has not ended). Freeing an instance a player is
    # still standing in is content's bug and the engine will not cover it.
    "MAP_INSTANCE_FREE": (11013, 1, 0, 0, 0),

    # map_instance_find(coord $coord)(int)
    #
    # The handle of the instance containing `$coord`, or -1. This is the query
    # that makes a handle in a varp safe to distrust: a handle is per-session,
    # a varp outlives the session, and "am I in a house?" has to be answerable
    # from where the player actually is. 2009scape asks it as
    # `RegionManager.forId(location.getRegionId()) instanceof DynamicRegion`.
    "MAP_INSTANCE_FIND": (11014, 1, 0, 1, 0),

    # npc_setrespawn(int $delay)
    #
    # Arm (or clear) the active npc's respawn clock. LostCity has no command for
    # this — God Wars minion sync is a 2009scape Java field write
    # (`NPC.setRespawnTick`). Content needs it to say "this dead minion comes
    # back with the boss", which is policy, not a C boss table.
    #
    # `$delay` is ticks from *now*: negative means "respawn on the next pass"
    # (2009scape `setRespawnTick(-1)` when the boss returns). While the npc is
    # still in its death animation, a pre-set clock survives the despawn step
    # instead of being overwritten by `NpcType.respawnrate`.
    "NPC_SETRESPAWN": (11015, 1, 0, 0, 0),

    # npc_freeze(int $ticks)
    #
    # Stop the active npc MOVING for `$ticks`. It keeps attacking anything
    # already in reach, keeps retaliating and keeps draining its queues — which
    # is what OldSchool's Ice spells do, and the reason this is not `npc_delay`
    # (2511), whose `delayed_until` gates the queue drain and would quietly stop
    # a frozen npc fighting as well.
    #
    # LostCity has no such command, and the shape of its absence is the whole
    # justification. Its freeze is content: `%npc_stunned` (a **varn**) holds
    # the expiry and `npc_walktrigger` arms a script that vetoes the step. That
    # is the better design and it is not portable here yet, because
    # `content.ini` declares no varn namespace — so content has nowhere to put
    # the expiry and no amount of script can say "this npc cannot move".
    #
    # So the engine takes the mechanism and content keeps the policy, the same
    # split map_instance_alloc records below: how long a freeze lasts comes from
    # the spell's `freeze_time` column, which spell freezes comes from content,
    # and all the engine knows is a tick count and that a frozen npc does not
    # step. When a varn namespace lands, this should move back to the
    # reference's walktrigger shape and this entry should go.
    "NPC_FREEZE": (11018, 1, 0, 0, 0),

    # npc_frozen()(int) — ticks of freeze the active npc has left, 0 if none.
    #
    # The read-back half of npc_freeze, and it is not optional decoration: the
    # rule OldSchool actually has is that a frozen target cannot be re-frozen,
    # and without a way to ask, content spamming Barrage would re-up the timer
    # every cast and hold an npc still for ever. The refusal is the content's
    # (it is the reference's `~pvm_freeze_allowed`); this is only how it asks.
    "NPC_FROZEN": (11019, 0, 0, 1, 0),

    # ---- inv slot vars (11016..11017) ------------------------------------
    #
    # Per-item ints on an inventory slot, keyed by an obj id. LostCity declares
    # both signatures commented-out in engine.rs2 (same band as region_find /
    # controller_* — never wired in Engine-TS). OSRS content needs them for
    # crystal / ethereum / wilderness weapon charges: a player-scoped varp
    # cannot track two bracelets. Shape is transcription of the LC comments:
    #
    #   inv_setvar(inv, slot, obj, value)
    #   inv_getvar(inv, slot, obj)(int)
    #
    # Empty slot or missing key → get returns 0. Clearing the slot clears vars.
    "INV_SETVAR": (11016, 4, 0, 0, 0),
    "INV_GETVAR": (11017, 3, 0, 1, 0),

    # ---- bank placeholders (11020..11021) --------------------------------
    #
    # oc_placeholder(obj)(obj) / oc_unplaceholder(obj)(obj)
    #
    # The note pair's twin. The reference has `oc_cert` / `oc_uncert` and no
    # placeholder equivalent for the plain reason that its era has no
    # placeholders — the feature arrives in OldSchool years after rev 254 — so
    # there is nothing to transcribe and these are allocated here rather than
    # squeezed into the reference's numbering.
    #
    # The *client* already has both as CS2 commands 4208/4209, and
    # `bankmain_drawitem` decides a cell is a placeholder with exactly
    # `oc_unplaceholder($obj) ! $obj`. Naming them the same thing on this side
    # means the server's test for "is this slot a placeholder" is the client's
    # test, character for character, rather than a second spelling that can
    # drift.
    #
    # Both return the input unchanged when the obj has no other form, which is
    # what `oc_cert` / `oc_uncert` do and what every caller branches on.
    "OC_PLACEHOLDER": (11020, 1, 0, 1, 0),
    "OC_UNPLACEHOLDER": (11021, 1, 0, 1, 0),

    # ---- owner-bound runtime npcs (11022..11024) --------------------------
    #
    # A familiar is a world npc with one engine relation content cannot encode:
    # the exact player login it belongs to. The generation is intentionally not
    # exposed; it is the engine's stale-handle guard, not gameplay state.
    "NPC_SETOWNER": (11022, 0, 0, 0, 0),
    "NPC_OWNER": (11023, 0, 0, 1, 0),
    "NPC_FINDOWNED": (11024, 0, 0, 1, 0),

    # ---- script-owned per-npc runtime state (11025..11026) ---------------
    #
    # Varps belong to players and NPC config params belong to a type. Boss and
    # encounter scripts need the third kind: a few integers belonging to one
    # live NPC instance, preserved when that instance changes type and cleared
    # when it respawns. The slot namespace is deliberately small and engine
    # defined; content gives each slot its own named constant.
    "NPC_VAR_GET": (11025, 1, 0, 1, 0),
    "NPC_VAR_SET": (11026, 2, 0, 0, 0),

    # ---- player action lock (11027..11028) -------------------------------
    #
    # A boss mechanic may temporarily reject the player's own movement and
    # action packets without making the player `busy`: busy/canAccess is the
    # queue and timer gate, while a time-stop must keep delayed damage and
    # queued encounter scripts moving. The lock owns only input/pathing state;
    # content owns its duration and calls the inverse explicitly.
    "PLAYER_LOCK": (11027, 0, 0, 0, 0),
    "PLAYER_UNLOCK": (11028, 0, 0, 0, 0),

    # ---- content-owned per-step movement policy (11029) -----------------
    #
    # walkstep_coord()(coord)
    #
    # The destination tile of the step for which the active player's armed
    # `[walktrigger,...]` is currently running, or null outside that hook.
    # LostCity's controller `checkWalkStep(lastX,lastY,nextX,nextY)` gives
    # encounter content this coordinate before *each* tile, but RuneScript's
    # ordinary `coord` command only exposes the tile the player still occupies.
    # The engine supplies geometry; the hook remains content-owned policy and
    # can veto by doing the reference-standard `p_walk(coord)`.
    "WALKSTEP_COORD": (11029, 0, 0, 1, 0),

    # npc_findcombat()(boolean)
    # Resolve the active player's live NPC combat target and make it the active
    # NPC. Summoning specials use this instead of opening a cursor when their
    # source behavior says "current combat target".
    "NPC_FINDCOMBAT": (11030, 0, 0, 1, 0),

    # npc_findowned2()(boolean)
    # Resolve the active player's familiar into the secondary NPC context. A
    # targeted trigger can retain its primary target while `.npc_*` addresses
    # the familiar actor.
    "NPC_FINDOWNED2": (11031, 0, 0, 1, 0),

    # obj_add_private(coord, obj, count, duration, private_ticks)
    #
    # The familiar foragers create owner-only floor loot before it becomes
    # public.  OBJ_ADD cannot express that receiver window; assigning it to the
    # engine rather than publishing drops and trying to hide them in content
    # keeps the ownership check on every packet and take path.
    "OBJ_ADD_PRIVATE": (11032, 5, 0, 0, 0),

    # npc_poison(int $severity)
    #
    # Apply an owner-attributed poison timer to the active NPC. The reference
    # keeps this in an entity timer: it replaces a weaker timer, carries its
    # attacker for credit, and ticks every 30 game ticks. This host has no NPC
    # varn/timer namespace for content to express that state.
    "NPC_POISON": (11033, 1, 0, 0, 0),

    # ---- npcs fighting things (11034..11036) -----------------------------
    #
    # npc_attacknpc(npc_uid $target)
    # npc_attackplayer()
    # npc_hastarget()(boolean)
    #
    # An npc's combat target is engine state — it decides facing, pathing, the
    # attack clock and which trigger fires — and content had no way to read or
    # write it. Two consequences, both of which this tree hit:
    #
    #   * a monster could only ever be made to fight a *player*, because the
    #     target was a pid. An encounter whose monsters attack scenery (the
    #     Inferno's adds and the Ancestral Glyph) had to drive the fight from
    #     `[ai_timer]` and carry its own clock, facing and reach beside the
    #     engine's, which is two combat systems racing each other on one npc.
    #   * "and now fight this player normally" could only be spelled
    #     `npc_setmode(applayer2)`, which fires one AP handler and falls back to
    #     `none`, so content had to re-arm it every single tick.
    #
    # `npc_attacknpc` fires `[ai_opnpc2,<attacker>]` with the target armed as
    # the secondary npc. NPC_FINDCOMBAT is the same question asked from the
    # player's end.
    "NPC_ATTACKNPC": (11034, 1, 0, 0, 0),
    "NPC_ATTACKPLAYER": (11035, 0, 0, 0, 0),
    "NPC_HASTARGET": (11036, 0, 0, 1, 0),

    # npc_attackdelay(int $ticks)
    #
    # "My next swing is N ticks away" — the combat attack clock, which content
    # could not reach. The only word it had for a cadence was `npc_delay`, and
    # that means something else: `Npc.isValid()` goes false for the whole turn,
    # so the npc runs no timers, no modes and — the part that bites — no QUEUE.
    #
    # In this tree an npc's queue is where every hit the player lands arrives
    # (`npc_queue(2, $damage, 0)`), so a monster that paced itself on
    # `npc_delay(4)` took one turn in five and its hitsplats were 0-4 ticks late
    # and bunched four-to-a-tick past the client's hitmark ceiling. 51 sites in
    # 13 boss files did exactly that, because there was no other way to say it.
    #
    # The distinction is real and worth two commands: `npc_delay` is "I am
    # running a scripted sequence, leave me alone", `npc_attackdelay` is "my
    # weapon is on cooldown". Only the first should stop damage landing.
    "NPC_ATTACKDELAY": (11037, 1, 0, 0, 0),

    # last_subop()(int)
    #
    # Which entry of an obj's `subaction=<op>,<slot>,<name>` Rub-style submenu
    # was clicked (Giantsoul amulet's "Bryophyta"/"Obor"/"Branda and Eldric",
    # Xeric's talisman's five Kourend destinations, and others) — the same
    # "an index selecting among sub-options had no path into a script" shape
    # IF_BUTTON1..10 above already describes for a component's op index, just
    # for a submenu's slot index instead.
    #
    # The wire already carries it (mock239_interface_inbound.c decodes
    # `button.subop`) and mock230_world.c already stores it
    # (`player->last_subop`), same as `last_slot`/`last_item` — this command
    # is only the missing read side. LostCity's reference predates rev-239's
    # Rub submenu convention entirely: ScriptOpcode.ts has no subop/subaction
    # entry to port (checked directly, not assumed), so — like the map-
    # instance band above — there is no reference name and no reference
    # signature, which is exactly why this sits in the EXTRA band.
    "LAST_SUBOP": (11039, 0, 0, 1, 0),

    # ---- a helper joining its owner's fight (11040..11041) ----------------
    #
    # npc_combatplayer()(boolean)
    # combat_assist_singles()(boolean)
    #
    # NPC_FINDCOMBAT answers "who is the player fighting". These two answer the
    # other half of the same question, and a summoned helper needs both.
    #
    # `npc_combatplayer` is the npc's own side: is the ACTIVE npc's combat
    # target the ACTIVE player? Those are not the same question in single-way
    # combat, where a player can be swinging at something that has turned on
    # somebody else, and the difference is the whole of the rule OldSchool's
    # thralls follow — a thrall attacks its owner's target and never generates
    # aggression of its own, so it may only ever join a fight its owner is
    # already in. Content could ask the player's side and not the npc's, so
    # "the fight I am joining is my owner's" was not expressible.
    #
    # `combat_assist_singles` is the server policy flag those helpers are gated
    # on: may a player's helper swing in a single-way area at all. It is engine
    # state rather than a content constant because it is a server-operator
    # decision — one live world may run pre-EoC-faithful (multiway only) and
    # another may run the thrall rule — and a content constant can only be
    # answered by rebuilding both script packs.
    "NPC_COMBATPLAYER": (11040, 0, 0, 1, 0),
    "COMBAT_ASSIST_SINGLES": (11041, 0, 0, 1, 0),

    # ambientsound(int $soundscape)
    #
    # The region's background bed: `AMBIENTSOUND_START` when the argument is
    # >= 0, `AMBIENTSOUND_STOP` when it is negative. The id names a config
    # group-15 soundscape record — a set of continuous loops plus timed random
    # sets — not a sound effect, so this is not a spelling of `sound_synth`.
    #
    # The reference has no such command because at its revision the type does
    # not exist (group 15 is an OldSchool 231+ addition). But a place whose
    # ambience is carried some *other* way still has to be able to say so, and
    # the QBD arena is exactly that place: it is a foreign rev-727 region whose
    # cave noise comes entirely from loc ambient emitters on its own scenery,
    # so an OldSchool bed underneath it is a second, wrong soundscape playing
    # over the authored one.
    #
    # `midi_song` is the model, down to -1 meaning stop, because the two
    # answer the same question about the same square.
    "AMBIENTSOUND": (11038, 1, 0, 0, 0),
}

# ---------------------------------------------------------------------------

# Triggers with no entry in the reference's ServerTriggerType.ts, allocated
# above its highest id (167) for the same reason EXTRA_OPCODES sits at 11000:
# the reference's numbering is an enum this port reproduces exactly, so
# rev-230-only surface goes strictly outside it.
#
# IF_BUTTON1..IF_BUTTON10 — *which op* was clicked on an interface button.
#
# The reference has one IF_BUTTON (147) because a 2004 interface component has
# one action. A rev-230 component has up to ten and the packet says which:
# IF_BUTTON1..IF_BUTTON10 are ten distinct opcodes on the wire
# (src/net/rev/osrs230/packetout.h), all ten of which this server collapsed
# into the single trigger. Content could not tell "View <skill> guide" (op 2)
# from "Toggle <skill> XP" (op 1) on the *same* component, and there is no
# `last_verb` command in the reference to read the index out of either — the
# op index simply had no path into a script.
#
# The reference names this shape itself, in ClientGameProt.ts:71-75:
#
#     INV_BUTTON1 = new ClientGameProt(181, 6);
#         // NXT has "IF_BUTTON1" but for our interface system, this makes
#         // more sense
#
# i.e. the numbered form of this family *is* IF_BUTTON<n> at the revision this
# client speaks, and LostCity renamed it INV_BUTTON<n> because the only
# multi-op components its interface system has are inventories. Those five
# triggers exist here already (149..153) and already dispatch per op. This is
# the same mechanism for the other half of the family, under the wire's name.
EXTRA_TRIGGERS: dict[str, int] = {
    "IF_BUTTON1": 168,
    "IF_BUTTON2": 169,
    "IF_BUTTON3": 170,
    "IF_BUTTON4": 171,
    "IF_BUTTON5": 172,
    "IF_BUTTON6": 173,
    "IF_BUTTON7": 174,
    "IF_BUTTON8": 175,
    "IF_BUTTON9": 176,
    "IF_BUTTON10": 177,
    # Rev-230 nested mounts (OpenRune onIfOpen). LostCity stops at IF_CLOSE —
    # its three fixed slots never needed an open-side twin. At rev 230 a panel
    # mounts into an arbitrary component of another panel, so content that
    # fills a nested slot (side_journal's tab_container → account_summary)
    # has to run *when that parent opens*, not from [login] a tick later: the
    # child's IF_OPENSUB otherwise races the parent's bake on the client.
    "IF_OPEN": 178,
    # Rev-230 friend presence. LostCity's client derived "X has logged in."
    # from UPDATE_FRIENDLIST world-id transitions; at rev 230 that derivation
    # is gone and the sentence is a server MESSAGE_GAME — so the engine names
    # the event and content words it (`[friendlogin,_]` / `[friendlogout,_]`).
    "FRIENDLOGIN": 179,
    "FRIENDLOGOUT": 180,
    # Engine detects HP hitting 0; content's [playerdeath,_] queues the
    # sequence. LostCity has no death trigger — content wrappers queue after
    # damage — but raw SS_OP_DAMAGE / C hit paths still need an event name.
    "PLAYERDEATH": 181,
}

# Opcodes whose operand is the script id / an index rather than the dot flag.
# The VM needs this to know when NOT to treat the operand as a pointer select.
NON_DOT_OPERAND = {
    "PUSH_VARP", "POP_VARP", "PUSH_VARN", "POP_VARN", "PUSH_VARS", "POP_VARS",
    "PUSH_VARBIT", "POP_VARBIT", "GOSUB_WITH_PARAMS", "JUMP_WITH_PARAMS",
    "JOIN_STRING", "SWITCH", "PUSH_INT_LOCAL", "POP_INT_LOCAL",
    "PUSH_STRING_LOCAL", "POP_STRING_LOCAL",
}

# JOIN_STRING and GOSUB/JUMP_WITH_PARAMS pop a count the table cannot express.
STRUCTURAL_VARIADIC = {
    "JOIN_STRING", "GOSUB_WITH_PARAMS", "JUMP_WITH_PARAMS",
    # Declared in EXTRA_OPCODES rather than engine.rs2, so there is no `*` in a
    # signature file for parse_engine_rs2 to notice.
    "RUNCLIENTSCRIPTVARARG",
}

# Var access is runtime-typed for the same reason the *_param family is: the
# variable's declared type decides which stack it touches. CoreOps.ts branches
# on `varpType.type === ScriptVarType.STRING` and pushes a string or an int
# accordingly. Most varps are ints, so MANUAL_META describes that case, but the
# flag has to say the type is not statically known — otherwise a static model
# silently mis-tracks every string varp.
RUNTIME_TYPED_VARS = {
    "PUSH_VARP", "POP_VARP", "PUSH_VARN", "POP_VARN", "PUSH_VARS", "POP_VARS",
}

# ScriptPointer enum order, from engine/src/engine/script/ScriptPointer.ts.
# The names on the left are what ScriptOpcodePointers.ts writes.
POINTER_BITS = {
    "active_player": 0,
    "active_player2": 1,
    "p_active_player": 2,   # ProtectedActivePlayer
    "p_active_player2": 3,  # ProtectedActivePlayer2
    "active_npc": 4,
    "active_npc2": 5,
    "active_loc": 6,
    "active_loc2": 7,
    "active_obj": 8,
    "active_obj2": 9,
}

# Pointer requirements for EXTRA_OPCODES. `parse_pointers` reads the reference's
# ScriptOpcodePointers.ts, which by definition says nothing about an opcode the
# reference does not have, so every extra lands with an empty mask unless it is
# named here.
#
# Only P_COUNTDIALOG_NOPROMPT is listed, and only because it is the first extra
# with a reference twin that carries a mask: P_COUNTDIALOG is
# `ProtectedActivePlayer`, so matching it is transcription rather than a new
# judgement. The other four extras are deliberately left alone — giving them
# masks is a decision about opcodes this change does not touch.
EXTRA_POINTERS: dict[str, tuple[int, int]] = {
    "P_COUNTDIALOG_NOPROMPT": (1 << POINTER_BITS["p_active_player"], 0),
    "NPC_SETRESPAWN": (1 << POINTER_BITS["active_npc"], 0),
    # Freezing needs an npc to freeze; without the mask `npc_freeze` in a script
    # with no active npc is a null deref rather than a refusal.
    "NPC_FREEZE": (1 << POINTER_BITS["active_npc"], 0),
    "NPC_FROZEN": (1 << POINTER_BITS["active_npc"], 0),
    "NPC_SETOWNER": (
        (1 << POINTER_BITS["active_npc"]) | (1 << POINTER_BITS["p_active_player"]),
        0,
    ),
    "NPC_OWNER": (1 << POINTER_BITS["active_npc"], 0),
    "NPC_FINDOWNED": (1 << POINTER_BITS["p_active_player"], 0),
    "NPC_VAR_GET": (1 << POINTER_BITS["active_npc"], 0),
    "NPC_VAR_SET": (1 << POINTER_BITS["active_npc"], 0),
    "NPC_ATTACKNPC": (1 << POINTER_BITS["active_npc"], 0),
    "NPC_ATTACKPLAYER": (
        (1 << POINTER_BITS["active_npc"]) | (1 << POINTER_BITS["active_player"]),
        0,
    ),
    "NPC_HASTARGET": (1 << POINTER_BITS["active_npc"], 0),
    # Both halves of the question have to be in scope: the npc whose target is
    # being read, and the player it is being compared against.
    "NPC_COMBATPLAYER": (
        (1 << POINTER_BITS["active_npc"]) | (1 << POINTER_BITS["active_player"]),
        0,
    ),
    "NPC_ATTACKDELAY": (1 << POINTER_BITS["active_npc"], 0),
    "PLAYER_LOCK": (1 << POINTER_BITS["p_active_player"], 0),
    # Deliberately no PLAYER_UNLOCK mask. A player-bound softtimer has no
    # protected pointer, yet it is the emergency activity-cleanup context that
    # must be able to release a stale action lock after an external teleport.
    # The mock host command addresses the session's active player directly,
    # like IF_CLOSESUB above; it does not dereference an SSVM entity pointer.
    "WALKSTEP_COORD": (1 << POINTER_BITS["p_active_player"], 0),
    "NPC_FINDOWNED2": (1 << POINTER_BITS["p_active_player"], 0),
    "NPC_POISON": (
        (1 << POINTER_BITS["active_npc"]) | (1 << POINTER_BITS["p_active_player"]),
        0,
    ),
}

# ScriptVarType: every type except `string` lives on the int stack. `any` means
# the command decides at run time what it pushes.
STRING_TYPES = {"string"}
# Return types meaning "the data decides". `any` is what the declared commands
# use; `dynamic` only shows up in the commented-out `enum` signature, and
# missing it there costs the whole runtime_typed flag for the one command whose
# return type varies most.
RUNTIME_TYPES = {"any", "dynamic"}


# ---------------------------------------------------------------------------
# Parsers
# ---------------------------------------------------------------------------

def parse_opcodes(path: Path) -> dict[str, int]:
    """Parse the `const enum ScriptOpcode` block.

    TypeScript enum members without an explicit `= N` take the previous value
    plus one, so this has to be a running counter — a regex for `NAME = N`
    alone would silently miss the ~340 implicit members.
    """
    text = path.read_text(encoding="utf-8")
    start = text.index("enum ScriptOpcode {")
    body = text[start:]
    body = body[: body.index("\n}")]

    out: dict[str, int] = {}
    nxt = 0
    member = re.compile(r"^\s*([A-Z][A-Z0-9_]*)\s*(?:=\s*(-?\d+))?\s*,?\s*(?://.*)?$")
    for line in body.split("\n")[1:]:
        stripped = line.strip()
        if not stripped or stripped.startswith("//") or stripped.startswith("/*"):
            continue
        m = member.match(line)
        if not m:
            continue
        name, explicit = m.group(1), m.group(2)
        value = int(explicit) if explicit is not None else nxt
        out[name] = value
        nxt = value + 1
    return out


def parse_triggers(path: Path) -> dict[str, int]:
    text = path.read_text(encoding="utf-8")
    start = text.index("enum ServerTriggerType {")
    body = text[start:]
    body = body[: body.index("\n}")]

    out: dict[str, int] = {}
    nxt = 0
    member = re.compile(r"^\s*([A-Z][A-Z0-9_]*)\s*(?:=\s*(\d+))?\s*,?\s*(?://.*)?$")
    for line in body.split("\n")[1:]:
        m = member.match(line)
        if not m:
            continue
        value = int(m.group(2)) if m.group(2) is not None else nxt
        out[m.group(1)] = value
        nxt = value + 1
    return out


def split_params(text: str) -> list[str]:
    """Split `int $a, coord $b` into ['int', 'coord'].

    Returns are declared without a `$name`, arguments with one; taking the first
    whitespace-separated token handles both.
    """
    text = text.strip()
    if not text:
        return []
    return [part.strip().split()[0] for part in text.split(",") if part.strip()]


def parse_engine_rs2(path: Path) -> dict[str, dict]:
    """Parse `[command,name](args)(returns)` declarations.

    Three shapes appear:
        [command,cam_reset]                       no parens at all
        [command,map_clock]()(int)                empty args, one return
        [command,stat](stat $stat)(int)           the common case

    A leading `.` marks the secondary-pointer variant, which shares the base
    name's opcode and only contributes has_dot. A trailing `*` marks the vararg
    form, which is a *separate* opcode named <BASE>VARARG — `queue*` is
    QUEUEVARARG (2094), not QUEUE (2093). Merging them would both mislabel QUEUE
    as variadic and leave QUEUEVARARG with no signature at all.
    """
    decl = re.compile(
        r"^\[command,(\.?)([a-z0-9_]+)(\*?)\]"
        r"(?:\(([^)]*)\))?"
        r"(?:\(([^)]*)\))?"
    )
    # A bare `[command,x]` normally means genuinely zero-arity (cam_reset,
    # if_close, p_pausebutton...). But at least one entry parks its real
    # signature in a trailing comment:
    #
    #     [command,enum] // (type $input, type $output, enum $enum, dynamic $key)(dynamic)
    #
    # Reading that as zero-arity is not a harmless approximation: `enum` pops
    # four values and pushes one, so the stack silently slides by five for
    # everything after it. Recovering the signature from the comment keeps this
    # self-maintaining — if upstream un-comments it, nothing here changes.
    commented = re.compile(r"//\s*\(([^)]*)\)\s*(?:\(([^)]*)\))?")

    out: dict[str, dict] = {}
    for line in path.read_text(encoding="utf-8").split("\n"):
        m = decl.match(line.strip())
        if not m:
            continue
        dot, name, star, args_s, rets_s = m.groups()

        if args_s is None:
            recovered = commented.search(line)
            if recovered:
                args_s, rets_s = recovered.group(1), recovered.group(2)
        args = split_params(args_s or "")
        rets = split_params(rets_s or "")

        key = name.upper() + ("VARARG" if star else "")
        entry = out.setdefault(key, {
            "int_in": 0, "str_in": 0, "int_out": 0, "str_out": 0,
            "has_dot": 0, "variadic": 0, "runtime_typed": 0,
        })
        if dot:
            entry["has_dot"] = 1
        if star:
            # The vararg form pops a type string on top of its declared args and
            # decides from that string's characters what to pop next, so the
            # declared arity is only a lower bound. Callers must not use it.
            entry["variadic"] = 1
        # The base declaration wins; the `.dot` one only contributes has_dot.
        if dot and entry["int_in"] + entry["str_in"] + entry["int_out"] + entry["str_out"]:
            continue

        entry["int_in"] = sum(1 for t in args if t not in STRING_TYPES)
        entry["str_in"] = sum(1 for t in args if t in STRING_TYPES)
        entry["int_out"] = sum(1 for t in rets if t not in STRING_TYPES)
        entry["str_out"] = sum(1 for t in rets if t in STRING_TYPES)
        if any(t in RUNTIME_TYPES for t in rets):
            entry["runtime_typed"] = 1
    return out


def parse_pointers(path: Path, opcodes: dict[str, int]) -> dict[str, tuple[int, int]]:
    """Extract require / require2 masks per opcode.

    `set`, `corrupt` and `conditional` are deliberately ignored: they are
    semantic (npc_find only sets active_npc when it actually found one), so they
    stay handler responsibilities. Only `require` is table-drivable.
    """
    text = path.read_text(encoding="utf-8")
    entry = re.compile(
        r"\[ScriptOpcode\.([A-Z0-9_]+)\]:\s*\{(.*?)\n    \}", re.DOTALL
    )
    out: dict[str, tuple[int, int]] = {}
    for m in entry.finditer(text):
        name, body = m.group(1), m.group(2)
        if name not in opcodes:
            continue

        def mask_of(field: str) -> int:
            fm = re.search(rf"\b{field}\s*:\s*\[(.*?)\]", body, re.DOTALL)
            if not fm:
                return 0
            mask = 0
            for ptr in re.findall(r"'([a-z_0-9]+)'", fm.group(1)):
                # find_* / last_* are compiler-side validation concepts with no
                # ScriptPointer bit; the runtime cannot check them.
                if ptr in POINTER_BITS:
                    mask |= 1 << POINTER_BITS[ptr]
            return mask

        out[name] = (mask_of("require"), mask_of("require2"))
    return out


# ---------------------------------------------------------------------------
# Emitters
# ---------------------------------------------------------------------------

BANNER = """/*
 * Generated by src/serverscript/gen_opcode_meta.py from the LostCity reference
 * server. Do not edit by hand — re-run the generator.
 */
"""


def emit_opcode_h(opcodes: dict[str, int], out: Path) -> None:
    ordered = sorted(opcodes.items(), key=lambda kv: (kv[1], kv[0]))
    hi = max(opcodes.values())
    lines = [
        BANNER,
        "#ifndef SRC_SERVERSCRIPT_SS_OPCODE_H",
        "#define SRC_SERVERSCRIPT_SS_OPCODE_H",
        "",
        "/* ServerScript opcode ids (LostCity ScriptOpcode.ts).",
        " *",
        " * Ranges: core language 0-46, server 1000+, player 2000+, npc 2500+,",
        " * loc 3000+, obj 3500+, npc/loc/obj config 4000+, inv 4300+, enum 4400+,",
        " * string 4500+, number 4600+, struct 4700+, db 7500+, debug 10000+. */",
        "",
    ]
    last_band = None
    for name, value in ordered:
        band = value // 500
        if last_band is not None and band != last_band:
            lines.append("")
        last_band = band
        lines.append(f"#define SS_OP_{name} {value}")
    lines += [
        "",
        f"/** One past the highest opcode id; the size of any opcode-indexed table. */",
        f"#define SS_OPCODE_MAX {hi + 1}",
        f"/** Opcodes the reference actually defines (the table is sparse). */",
        f"#define SS_OPCODE_COUNT {len(opcodes)}",
        "",
        "#endif",
        "",
    ]
    out.write_text("\n".join(lines), encoding="utf-8")


def emit_trigger_h(triggers: dict[str, int], out: Path) -> None:
    ordered = sorted(triggers.items(), key=lambda kv: (kv[1], kv[0]))
    hi = max(triggers.values())
    lines = [
        BANNER,
        "#ifndef SRC_SERVERSCRIPT_SS_TRIGGER_H",
        "#define SRC_SERVERSCRIPT_SS_TRIGGER_H",
        "",
        "/* ServerScript trigger ids (LostCity ServerTriggerType.ts).",
        " *",
        " * A script's lookup key is trigger | (kind << 8) | (subject << 10); see",
        " * SSVM_LookupKey in ss_meta.h. The op form of an interaction trigger is",
        " * always the ap form + 7 (APNPC1 3 -> OPNPC1 10, APLOC1 59 -> OPLOC1 66),",
        " * which is why the engine stores the ap id and adds 7 rather than",
        " * carrying both. */",
        "",
    ]
    for name, value in ordered:
        lines.append(f"#define SS_TRIGGER_{name} {value}")
    lines += [
        "",
        f"/** One past the highest trigger id. */",
        f"#define SS_TRIGGER_MAX {hi + 1}",
        "",
        "/** Distance from an ap trigger to its matching op trigger. */",
        "#define SS_TRIGGER_AP_TO_OP 7",
        "",
        "#endif",
        "",
    ]
    out.write_text("\n".join(lines), encoding="utf-8")


def emit_meta_gen_h(
    opcodes: dict[str, int],
    triggers: dict[str, int],
    sigs: dict[str, dict],
    pointers: dict[str, tuple[int, int]],
    out: Path,
) -> tuple[int, int]:
    op_max = max(opcodes.values()) + 1
    trig_max = max(triggers.values()) + 1
    by_id = {v: k for k, v in opcodes.items()}
    trig_by_id = {v: k for k, v in triggers.items()}

    known = 0
    rows: list[str] = []
    for value in sorted(by_id):
        name = by_id[value]
        sig = sigs.get(name)
        manual = MANUAL_META.get(name)
        extra = EXTRA_OPCODES.get(name)

        if extra is not None:
            manual = extra[1:]

        if manual is not None:
            int_in, str_in, int_out, str_out = manual
            is_known = 1
        elif sig is not None:
            int_in = sig["int_in"]
            str_in = sig["str_in"]
            int_out = sig["int_out"]
            str_out = sig["str_out"]
            is_known = 1
        else:
            int_in = str_in = int_out = str_out = 0
            is_known = 0
        known += is_known

        variadic = 1 if name in STRUCTURAL_VARIADIC else (sig["variadic"] if sig else 0)
        runtime_typed = 1 if name in RUNTIME_TYPED_VARS else (sig["runtime_typed"] if sig else 0)
        has_dot = sig["has_dot"] if sig else 0
        req, req2 = pointers.get(name, (0, 0))

        rows.append(
            f"    [{value}] = {{ {int_in}, {str_in}, {int_out}, {str_out}, "
            f"{is_known}, {variadic}, {runtime_typed}, {has_dot}, "
            f"0x{req:03x}, 0x{req2:03x} }}, /* {name} */"
        )

    lines = [
        BANNER,
        "/* Included by exactly one translation unit: ss_meta.c. */",
        "",
        "/* Opcode names, for traces and the loud stub's report. */",
        f"static const char* const g_ss_opcode_names[{op_max}] = {{",
    ]
    for value in sorted(by_id):
        lines.append(f'    [{value}] = "{by_id[value]}",')
    lines += ["};", ""]

    lines += [
        "/* Per-opcode stack signature and runtime-safety metadata.",
        " *",
        " * Fields, in order: int_in, str_in, int_out, str_out, known, variadic,",
        " * runtime_typed, has_dot, require, require2. See struct SSVM_OpcodeMeta.",
        " *",
        " * known == 0 means neither engine.rs2 nor MANUAL_META declared this",
        " * opcode, so its arity is unknown and it must not be executed. */",
        f"static const struct SSVM_OpcodeMeta g_ss_opcode_meta[{op_max}] = {{",
    ]
    lines += rows
    lines += ["};", ""]

    lines += [
        "/* Trigger names, for script-name parsing and diagnostics. */",
        f"static const char* const g_ss_trigger_names[{trig_max}] = {{",
    ]
    for value in sorted(trig_by_id):
        lines.append(f'    [{value}] = "{trig_by_id[value].lower()}",')
    lines += ["};", ""]

    out.write_text("\n".join(lines), encoding="utf-8")
    return known, len(opcodes)


# ---------------------------------------------------------------------------

def main() -> int:
    ref = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_REF
    script_dir = ref / "engine/src/engine/script"
    for required in (
        script_dir / "ScriptOpcode.ts",
        script_dir / "ServerTriggerType.ts",
        script_dir / "ScriptOpcodePointers.ts",
        ref / "content/scripts/engine.rs2",
    ):
        if not required.exists():
            print(f"missing input: {required}", file=sys.stderr)
            print(f"usage: {sys.argv[0]} [path-to-LostCity_Server]", file=sys.stderr)
            return 1

    opcodes = parse_opcodes(script_dir / "ScriptOpcode.ts")
    for extra_name, extra_row in EXTRA_OPCODES.items():
        assert extra_name not in opcodes, f"{extra_name} collides with a reference opcode"
        opcodes[extra_name] = extra_row[0]
    triggers = parse_triggers(script_dir / "ServerTriggerType.ts")
    for extra_trigger, extra_id in EXTRA_TRIGGERS.items():
        assert extra_trigger not in triggers, f"{extra_trigger} collides with a reference trigger"
        assert extra_id not in triggers.values(), f"{extra_trigger} collides with trigger id {extra_id}"
        triggers[extra_trigger] = extra_id
    sigs = parse_engine_rs2(ref / "content/scripts/engine.rs2")
    pointers = parse_pointers(script_dir / "ScriptOpcodePointers.ts", opcodes)
    for extra_name, extra_mask in EXTRA_POINTERS.items():
        assert extra_name in EXTRA_OPCODES, f"{extra_name} is not an extra opcode"
        assert extra_name not in pointers, f"{extra_name} already has a reference mask"
        pointers[extra_name] = extra_mask

    emit_opcode_h(opcodes, HERE / "ss_opcode.h")
    emit_trigger_h(triggers, HERE / "ss_trigger.h")
    known, total = emit_meta_gen_h(opcodes, triggers, sigs, pointers, HERE / "ss_meta.gen.h")

    unmatched = sorted(set(sigs) - set(opcodes))
    print(f"opcodes   {total} defined, {known} with a known signature")
    print(f"triggers  {len(triggers)}")
    print(f"pointers  {len(pointers)} opcodes carry a require mask")
    print(f"engine.rs2 {len(sigs)} commands, {len(unmatched)} with no matching opcode")
    if unmatched:
        print(f"           {', '.join(unmatched)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
