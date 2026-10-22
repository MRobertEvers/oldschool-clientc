---
name: rsprox-content
description: Implement or correct OSRS content (RuneScript in OSRS-Content) from RSProx recordings — loc/npc/obj ops and ap triggers, ladders/stairs/maplinks, doors and loc adds/changes, projectiles, area sounds and synths, spotanims and anims, interface packets (open/close/hide/move/text/highlights/hint arrows), npc info (movement, facing, transforms, ops), dialogues, player facing/movement/teleports/exactmove, damage (splats on the player, accuracy, max hits, prayer). Trigger on "implement X from RSProx", "make this match the recording", "port this content from RSProx", or any content work where a recording shows the behaviour.
---

# Implementing content from RSProx recordings

**Finding and transcribing sessions is the `rsprox` skill** (`tools/rsprox/rsprox_index.py`): run
it first. This skill covers what comes after: reading what Jagex sent for an interaction, and
writing RuneScript that sends the same thing on the same tick.

**[PACKETS.md](PACKETS.md) explains every packet kind**: its fields, units, revision variants, and
the script command that emits it. Keep it open.

## The loop

1. **Find the interaction.** Search by the loc/npc/obj/interface id, then
   `python3 tools/rsprox/rsprox_interact.py <txt> --list` to count every click in the session.
   Prefer a normal-world session (activity `-` or a place name). Use rev 238+ when facing matters.
2. **Trace it.** `rsprox_interact.py <txt> --op '<regex on the click line>' --max 5`. It prints
   every tick around the click, labelled `C+n` (from the click) and `L+n` (from the player's last
   step). Clock varps, other npcs' wandering and zone re-sends are filtered out. `--npc <word>`
   keeps an npc, `--all` keeps everything, and `--from T --to T` prints a plain window. Read
   **several** occurrences: one recording shows one roll.
3. **Unpack the npc info. Never skip it.** `rsprox_interact.py` and `rsprox_tells.py` filter
   other npcs' movement and most ext-info out, so a timeline built only on them has never seen
   how an npc moves, faces, transforms or leaves. For every npc type the content touches, run
   `python3 tools/rsprox/rsprox_npcinfo.py <txt>... --match '<npc name regex>' --raw-dir
   ~/.rsprox-index-cache/home-raw/.rsprox/binary/archive` and write its per-life record (§9):
   spawn angle and add-tick ext-info, movement kind and tiles per tick, facing kinds, sequences
   with delays (and which ones never appear, e.g. no defend seq), transformations, level_change,
   enabled_ops, headicons, hit and headbar ids, and death seq to `[del]`. Then compare each line
   with the npc's config and scripts (defaultmode, wanderrange, huntmode/huntrange, attackrange,
   death_delay, defend_anim, the forms' headicons). The generated npc blocks default to
   `hitpoints=10`, `attack=1`, null sounds and `huntmode=none`: assume they are wrong until a
   config states otherwise.

   **Then take the block-kind inventory, before any per-block diff:**
   `python3 tools/rsprox/npc_block_inventory.py <txt>... --ours <ticklog.tsv> --match '<regex>'
   --values`. It lists EVERY npc-info block kind each npc type gets on the wire (`[bas]`,
   `[body_customisation]`, `[headicon_customisation]`, `[transformation]`, `[transparency]`,
   `[exact_move]`, ...), with our count and the distinct values on both sides, and flags a kind
   the recording has and ours never sends. The timeline tools print a CHOSEN set of blocks:
   `npc_timeline.py` and `rsprox_tells.py` never show `[bas]` or `[body_customisation]`, so an
   audit built on them cannot see a kind is missing. Ezekiel's finale passed three "every
   block" audits that way and shipped with no stances or models: the fence swords played their
   type's animated template forever, the V swords drew nothing so the lightning struck bare
   floor, the death sword showed over him as he died (2026-10-22). Every MISSING row and
   every value only one side has is a defect or a named reason (the raiders' kit: a
   `windsurge_impact` on a sword is a raider's spell).
4. **Write the timeline down** in the analysis doc as `L+1 face; L+2 seq 827, busy=1; L+3
   teleport`, citing session and tick (`index 4286 [38]`).
5. **Find our side.** Grep `OSRS-Content/osrs-content/server/scripts` for the trigger (by name,
   or by `_category` from the loc's `.loc` config). Check the shared procs first:
   - `~climb*` / `~maplink_try` in `ladders_stairs/scripts/`
   - door procs in `doors/scripts/`
   - `~chatnpc` / `~chatplayer` / `~mesbox` / `~objbox` / `~p_choice*` in `interface_chat/scripts/chat.rs2`
6. **Implement**, mapping each recorded line to its command with PACKETS.md.
7. **Verify on our wire, not in our heads.** Run a scriptrun test (`test/<area>/<name>.lua`,
   `t.ticklog.start()`), then compare in two ways:
   - **Event timing:** `tools/rsprox/ticklog_tells.py` and `tools/rsprox_tells.py --anchor ... --ours`.
   - **Packet-exact checks:** `TORIRSSERVER_TRACE_OUT=1` (2 adds the bodies) prints our every
     packet with the tick. Count `SERVER_TICK_END` to line ticks up.

   Remember the tick log has no message/interface rows (see the `rsprox` skill's §5).
8. **The wire audit is REQUIRED, and nothing is done without it** (step 7 made concrete; see
   "The wire audit" below). Copying recorded numbers into a script and a green test are NOT
   parity: a quest gate checks that the quest completes, never what it sounded or looked like.

## The wire audit (required before calling any content done)

A Night at the Theatre (2026-10-20) went green on its quest gate with every dialogue page matching
the recording -- and every vyrewatch in the tree silent (attack/defend/death sounds `null` in the
generated npc block), a coffin splat missing, and a defend animation the recording never plays.
Nobody had asked our server what it sent. Do this for EVERY section of the content (each room,
fight, cutscene, interaction), before reporting it finished:

1. **Record ours.** The test calls `t.ticklog.start()` (quest tests: once, in the first leg run in
   the process, with a `t.ticklog.mark("leg.<name>")` per leg) and keeps
   `build/quest_gate/<run>/ticklog.tsv`.
2. **Print both sides as the same timeline**, per section window:
   `python3 tools/rsprox/rsprox_tells.py <txt> --from T0 --to T1 --timeline` (recording) and
   `python3 tools/rsprox/ticklog_tells.py <ticklog.tsv> --from t0 --to t1` (ours; filter to the
   content kinds: `sound npc_anim npc_spotanim player_anim player_spotanim map_spotanim projectile
   loc_set loc_anim music jingle npc_spawn npc_death hit_*`). Align on an event both have (the
   op, a spawn, the music change), never on the click.
3. **Walk every row of the recording and give it a verdict**: matched, fixed, the recorder's own kit
   (their weapon's swing seq and sound, their spec, their teleport out), or engine-generic (named,
   then checked once in a second session). Then walk every row of OURS the recording lacks:
   delete it, or explain it. Check, field by field:
   - **Music:** `midi_song_v2`'s whole envelope (our tick log's music row carries it: c..f), not just the id.
   - **Sounds:** every `synth_sound` (id, loops, delay) and every `sound_area` (id, loops, delay,
     range, tile). Name each id in our cache, rev 241 (`OSRS-Content/osrs-content/pack/4_soundeffects.pack`).
     Npc attack/defend/death sounds come from the npc config's `attack_sound`/`defend_sound`/
     `death_sound` params, and the generated block (`_explicit/configs/server_fields.generated.npc`)
     sets them to `null`: a fight is silent until content sets them.
   - **Animations:** every npc and player seq with its `delay`, and which ticks it is ABSENT on
     (an npc that never plays its defend seq in the recording must not play one here).
   - **Spotanims:** id, height, delay, and whose (npc/player/map tile).
   - **Projectiles:** every field -- source tile, target, id, `starttime`, `endtime`, `angle`,
     `progress`, start and end height -- against a fit across many shots (section 4).
   - **Locs, music, jingles, varbits, npc spawns (tile, angle), npc deaths and despawn delay.**
   - **Damage, every attack** (§13): the splats on the player per cause, prayed and not --
     zero rate, max, the prayed distribution -- against ours, and the npc's levels against
     the cache.
   - **NPC INFO, EVERY BLOCK, EVERY TICK -- never skipped.** Start from the per-type, per-life
     summary (`tools/rsprox/rsprox_npcinfo.py --match <regex>`, step 3 of the loop: movement kind
     and tiles per tick, wander vs standing, headicon frames per form, hitsplat and headbar ids,
     death seq to `[del]`), then diff tick by tick. Everything an npc does is in the
     npc info packet, and the inventory tools see only part of it: `rsprox_tells.py --ours` counts
     seqs and spotanims per window and **never compares facing**, and ticklog_tells prints no
     facing at all. For every npc the content drives (the boss above all), diff its whole
     per-tick npc-info stream against ours: `[sequence]` (id AND delay, on the same tick),
     `[spotanim]` (slot, id, height, delay), **`[facing]`** (`coord=`, `player=`/entity,
     `type=reset`, `angle=`), `[say]`, `[transformation]`, `level_change`, `enabled_ops`,
     `[headbar]`, `[transparency]`, `exact_move`, and the movement kind and steps. Align on the
     section's own anchor (the form change, the spawn), not on the fight's start, and count
     matched / recording-only / ours-only per block kind. Ocellus (2026-10-21) passed a tells
     inventory with his facing never sent (0 of 28 faces in phase 1, 0 of 72 in phase 4) and
     half his phase-1 seqs off by ticks; `docs/rsprox-analysis/guthixian_archive/tools/
     oc_stream.py` is the template for this diff. Our tick log's `npc_face` rows: a coord row
     from `npc_facesquare`, and `x = -1, z = target` for an entity face or its reset (`-1`),
     written at the end of the tick the block goes out.
4. **Write it down**: one table per section in the analysis doc (`R`, ours, verdict), citing the
   recording ticks, and the counts behind each rule ("6/6"). A section without a table is not
   audited.
5. **Report what is still open** -- a mismatch you could not fix is a named gap in the doc and in
   your reply, not silence.

## The timing model (measure everything from here)

- **C** = the click line's tick. It jitters ±1 (the proxy files it when it saw it). Never time from C.
- **L** = the player's last movement step on the way to the target. The tool finds it.
- **An op on a loc/npc the player walked to fires at L+1.** The engine holds ops for a player who
  moved. That tick carries the **face** (`face_angle` ≤237, `[facing] coord=..[, sizex, sizez]`
  238+) and the op's first packets. **Already in reach (no walk): it fires on C**, often with no
  face packet.
- **Script time from the op tick O:** `p_arrivedelay` waits one tick if the player moved (O+1).
  `p_delay(n)` resumes at **now + 1 + n**: `p_delay(0)` is the next tick.
- A dialogue click (`resume_pausebutton`) is answered on its own tick (the next page arrives with it).
- Recorded examples, cite them:
  - **Ladder down** (rsprox.net 1102 [13855..]):
    - L+1 face
    - L+2 seq `human_pickupfloor` 827, `busy`=1
    - L+3 `teleport` to the plane below, `busy`=0

    That is `p_arrivedelay; anim(..); p_delay(0); teleport`. Ladder up uses `human_reachforladder` in our reference (no recording checked yet).
  - **Spiral stairs** (index 4286 [36..39]): L+1 `[facing] coord=` the stairs; L+2 `teleport`, no anim. From a standing start ([41]), the teleport comes on C.
  - **Door** (rsprox.net 3216 [651..656]): at L+1, `face_angle`, then `loc_del` of the closed door, then `loc_add_change` of `*_open` on the neighbouring tile with the rotation turned one step, then `synth_sound` 62 `door_open`.
  - **Double gate** (rsprox.net 3226 [5513]): both leaves at L+1. `synth_sound` 67 opens it and 66 closes it.

## 1. Loc interactions: op vs ap

- **op** (`[oploc<N>,x]`): fires when the player is *adjacent* to (has reached) the loc, at L+1.
- **ap** (`[aploc<N>,x]`): fires as soon as the player is within the ap range **and** has line of
  sight, possibly mid-walk. The default range is 10 (`TORIRSSERVER_AP_RANGE_DEFAULT`, the
  reference's PathingEntity value). The script narrows it with `p_aprange(n)`; the engine
  re-tries and the script runs again when within n. If an ap trigger runs and never calls
  `p_aprange`, the interaction ends. The engine order is in `interaction_try`
  (`src/torirsserver/torirs_server_world.c`).
- **Read it from the recording:** the tick of the server's first answer, and the player's tile then.
  - The walk ends adjacent and the answer comes at L+1: **op**.
  - The walk stops short (the player halts at distance d) and the answer comes then: **ap** with `p_aprange(d)`.
- **Unreachable locs need ap.** A loc ringed by blocking scenery (the Wintertodt braziers' `wint_inviswall`) can never be "reached": plain oploc never fires. Use `[aploc1,..]` + `p_aprange(1)`.
- An op on a moving npc (Lara walking) re-paths. The recording shows the `opnpc` line repeated each time the client re-sent it.
- Check `enabled_ops` / `hidelocops` / `opflags` (rev 241) when an op should be unclickable for a while.

## 2. Maplinks: stairs, ladders, trapdoors, cave mouths

- The shape is fixed (see the timing model). The **destination** is the `newcoord` of the
  `teleport` line. A plane change is always `teleport`; there is no anim for stairs, rocks or vines.
- Ours: categories bind every climb loc (`ladders_stairs/configs/ladders.loc`, `scripts/ladders.rs2`).
  `~climb(delta)` is ±1 plane on the same tile, unless `~maplink_try` finds a row in
  `maplink.dbrow`.
  - That file is **generated** (`tools/maplink_import.py`): never edit it.
  - A destination the recording proves goes in `maplink_landings.dbrow`. Cite the session and tick.
- **Check the anim tick and the teleport tick against ours with a tick log.**
  `~climb_ladder_anim` does `anim(...); p_delay(1)`, but the recording puts the teleport one tick
  after the anim (`p_delay(0)` after an arrivedelay). Confirm before changing it: the op-tick
  hold and `p_arrivedelay` both shift it.
- After a plane change, the zone is re-sent (`obj_add_specific`/`loc_add_change` bursts). That is not content.

## 3. Loc changes and adds

- `loc_add_change` = a loc appears in a slot (coord + shape layer) or replaces the one there.
  `loc_del` = the slot empties. Shapes: 0-3 walls, 4-8 wall decor, 9 diagonal wall, 10/11
  centrepieces, 12-21 roofs, 22 ground decor. Rotation 0-3.
- Doors are `loc_del` (old tile) + `loc_add_change` (open id, neighbouring tile, rotation turned) in one
  tick. Our door procs do this. Copy the recorded tile and rotation, never derive them.
- Temporary locs: `loc_add(coord, loc, rot, shape, duration)` or `loc_del(duration)` revert
  after `duration` ticks (≤0 = permanent). Measure the duration from the recording (the tick the
  original comes back).
- **Room entry locs:** diff every `loc_add_change` at E+0..3 of a room against ours (ToA was missing statues, traps and blockers).
- CoX room locs carry **fixed** rotations, not room-turned ones.
- `loc_anim` plays on whatever loc is in the slot. Send it after the `loc_add`.
- Our `loc_add` makes the new loc the active loc, so a following `loc_coord` names it.
- `loc_merge` (player merged into a loc for start..end cycles): `p_locmerge`.

## 4. Projectiles

- Copy **every** field, per shooter: `starttime`, `endtime`, `angle`, `progress`, `startheight`,
  `endheight`, and the `[source]` tile.
- Script: `projanim_pl/npc/map(src, target, spot, startheight, endheight, starttime, endtime, angle, progress)`.
  **Heights depend on the recorded packet's version.** Our server sends the old packet: one byte
  per height, which the client multiplies by 4 (`projanim_*` writes its argument as that byte,
  torirs_server_encode.c). `map_projanim_v1` (recordings up to rev ~233) printed that same byte:
  **use it as printed**. `map_projanim_v2` / `projanim_specific_v3` (rev 234+) carry the raw height
  in 2 bytes: **divide by 4**. Read the packet name on the line before converting. (2026-10-20: the
  Hespori's rev-226 v1 heights 85 -> 39 were divided by 4 by mistake and flew 4x too low.)
  `npc_projanim_*` launches from the active npc.
- `endtime` usually grows with distance. Fit `endtime = a + b*tiles`, with tiles = Chebyshev
  from the `[source]` tile to the `[target]` tile, across many shots
  (`tools/rsprox_tells.py --kind proj` prints the fit).
- **Source tile:** a size-N npc fires from its centre (sw + N//2) unless the recording says otherwise. Maiden's flights are from her centre tile.
- **Shared projectile ids:** attribute each shot by the shooter's attack seq on that tick, using ticks where only one type attacked.
- **Impacts:** the target's spotanim `delay` and the impact `sound_area` `delay` equal the projectile's `endtime` (all in cycles). The hit lands on tick `(endtime + 15) // 30` after the throw.
- The recorder's own projectiles carry the recorder's kit: a dragon arrow is not npc content.

## 5. Area sounds

- `sound_area(synth, delay, coord, range)`, or `sound_area_size(..., size)` when the recording has `size=`.
- **Only players within `range` receive one**, so a quiet recording may just mean the recorder was far away. Grep other sessions before concluding there is no sound.
- Pair each sound with its event: `tools/cox/rsprox_sound_pairs.py <txt>` (same tick, same tile).
- Defend sounds go only to the dealer. Every red splat on a player has a `human_hit` synth.

## 6. Synths

- `synth_sound` is for the recorder alone, at full volume: own equips, doors, gates, UI, a hit taken.
- `sound_synth(synth, loops, delay)`; loops and delay are as printed (cycles).
- Do not turn a recorded synth into an area sound, or the reverse. Which packet Jagex used is the decision.

## 7. Spotanims and anims

- Npc: `npc_anim(seq, delay)` and `spotanim_npc(spot, height, delay)`; `_slot` variants for a non-zero `slot=`; `spotanim_npc_slot_loop` when the line ends `loop=true` (rev 241: the client replays it until the slot is sent again; a clear sent with `loop=true` is the same command with `null`).
- Player: `anim(seq, delay)` and `spotanim_pl(...)`.
- Tile: `spotanim_map(spot, coord, height, delay)`.
- Heights are raw (92 chest, 124 head) and delays are in cycles.
- `id=-1` stops (`anim(null, 0)`).
- **Check each seq id's name in our cache** (rev 241; an old recording's chathead seq can be an axe special in it).
- An npc type with no anim block inherits `human_*`. Give it its own death/defend anims from the recording's npc record.
- `npc_setdeathanimnow` puts the death seq on the killing tick.
- Rev 234+ player cast seqs are the walkmerge forms.

## 8. Interface packets

- Open/close: `if_opensub(com, iface, modal|overlay)` / `if_closesub(com)`. Frame-slot names
  (`toplevel_osrs_stretch:mainmodal`) depend on the client mode. The **subcomponent slot** is
  what to copy.
- Each `if_sethide`, `if_settext` (keep the `<br>`/`<col>` exactly), `if_setevents` (slot range and event set), `if_setanim`, `if_setobject` and `if_setposition` line maps to the command of the same name.
- **Highlights and tutorial UI are clientscripts.** Copy the clientscript and its argument list as printed:
  - `ui_highlight_update` 8478 `(3, 1, com, -1, dbrow style, 0)` / `ui_highlight_clear` 8484
  - `inv_highlight_item_on` 8461 `(obj, style, n)` / `inv_highlight_clear_all` 8465
  - `mesoverlay` 1974 (the instruction box)
  - `toplevel_sidebutton_switch` 915 (select a tab)
  - `tutorial_overlay_hint` 2584
  - `fade_overlay` 948 (screen fades: `0,255,0,0,50` out, `0,0,0,255,50` in)
- Tab flash = varbit `flashside` (3756).
- Hint arrows: `hint_npc` / `hint_pl` (the active entity), `hint_coord(position, coord, height)`, `hint_stop`.
- Jagex often sends the same setup **twice in one tick**. Copy it once.
- The `busy` varbit (12393) marks a held player.
- The hp hud's `if_setcolour` spam is not content.

## 9. Npc info (analyse in detail; REQUIRED, see the loop's step 3)

Sessions have skipped the npc_info packet and shipped npcs with default stats, no hunting, a
defend seq the recording never plays and the wrong death timing (the Gauntlet, 2026-10-21). For
each npc type, build the **per-life record** (spawn to despawn) with
`tools/rsprox/rsprox_npcinfo.py --match <regex>` (`tools/rsprox_nylocas.py`'s parser also handles
index reuse):

- **Spawn:** the `[add]` tick, `spawnangle` (our `npc_facedir`), `creationcycle`, and the first seq/spotanim (spawn anim on the add tick).
- **Movement kind per tick:**
  - `walk` / `run` / `crawl` carry steps.
  - `teleport` is a jump-clear shift that **the client walks** (glide; Bloat, Vasa, jugs).
  - `telejump` snaps.
  - Ours: `npc_walk` / modes give steps, `npc_tele` ≤2 tiles glides, and further moves are a `telejump` (removed and re-added under the same client index in one packet, as Jagex sends it). A boss that vanishes and reappears also fades: `npc_transparency` (Ocellus: `30, 45, 0, 255` out, `0, 20, 255, 0` in, `-1, -1, 0, 255` to stay unseen through a jump).
  - Count tiles per tick to get the speed.
- **Facing (in npc info: diff it tick by tick, section "The wire audit" 3):**
  - Record **every** `[facing]` line the npc gets: when it turns to a tile, when it faces a raider,
    when it resets, and what it does across an attack (Ocellus: `type=reset` as each volley starts,
    `coord=` the arena's middle 6 and 10 ticks later, `angle=` turns between his phase-1 corners).
    A fight whose facing is never compared is not audited, however well its seqs line up.
  - **Entity** (`face_pathingentity` / `[facing] player=..., fallbackangle=`) is the npc's target; it comes from modes (`npc_setmode(opplayer2)`, `playerfaceclose`) and clears on `entity=null` / `type=reset`.
  - **Square** (`face_coord` / `[facing] coord=`) is `npc_facesquare`.
  - `instant=true` on a coord face is `npc_facesquareinstant`; on an angle, `npc_faceangle(a, true)`; on a player, `npc_faceplayerinstant` (sent even when the latch already names them: Moia's blinks).
  - An entity face with no mode is dropped on the npc's next idle phase, so a script face re-set every tick goes out every tick and a one-off face sends a reset a tick later. Hold it with `npc_setmode(playerface)` beside the face, and `npc_setmode(none)` before a `npc_facereset` (Moia, 2026-10-21: 122 of 124 ticks re-sent).
  - **Npc entity** (`[facing] npc=`) with no fight is `npc_facenpc(uid)`, held until a mode or a reset.
  - A mode set from a player-phase script (a room tick, a loc op) latches a tick late: when the
    recording faces on the very tick, add `npc_faceplayer` beside `npc_setmode(playerface)`.
  - Many npcs `[facing] type=reset` with their first step after an angle face (Armacus, the
    Guardians): `npc_facereset` on that tick.
  - **Angle** is `npc_facedir`.
  - Note **when** it re-faces (every attack? only on target change?).
  - Several bosses swing with the facing **held** into the swing (Tekton).
  - `playerface` drops past the npc's `maxrange` from spawn (default 7).
- **Stances (`[bas]`) and models (`[body_customisation]`) -- read them on EVERY add and form:**
  - `[bas] ready=.., walk=.., turnleft=.., turnright=..` replaces the type's stance set (an
    empty `[bas]` is the reset to the type's): `npc_basready`, `npc_baswalk`, `npc_basturn`,
    `npc_basreset`. A vfx npc's TYPE often idles on an animated template (the Ezekiel fence:
    `vfx_ezekiel_fence_template`, 13 frames) that Jagex always overrides on the add; without
    the `[bas]` it animates for ever.
  - `[body_customisation] [model] id=N` draws model N in place of the type's:
    `npc_bodymodel(N)`; `id=-1` in the list is HIDDEN (`npc_bodyhide`), and a later bare
    `[body_customisation]` with no model line is the reset (`npc_bodymodel(-1)`). The
    transcript prints the V3 block and a second one (V2/V4) both as `[body_customisation]`;
    ours sends V3. Per-life values matter: the same npc type can take a different model and
    stance per copy (Ezekiel's V: 62639 / 62641 / 62642).
  - Which variant (drop seq, idle, model) a copy gets is often fixed by WHERE it is: tabulate
    variant x tile across every session before assuming loop order or random (the fence's
    seven drops are fixed per arena cell, 4300 and 4301 agree on every shared cell).
- **Transforms:**
  - `transformation` + `level_change` on every form change (`npc_changetype`), and Jagex re-sends `enabled_ops 0b11111`.
  - On death start, `enabled_ops 0b00000` (`npc_setops`).
- **Hits/headbars:** the hitsplat type is the dealer's view (16 damage_me, 17 damage_other, 12/13 block). `headbar` fill is out of the bar's width. Read the healthbar id (bosses use wider ones) and our `npc_setheadbarreserve`.
- **Ext-info timing:** the seq and spotanim `delay=` on the same tick as a projectile are the impact.
- **Npc vars survive slot reuse:** reset them on spawn.
- `npc_approach` both steps in-script and queues (2 tiles a tick).
- A tell that repeats every 2 ticks in a seasonal session may be the recorder's attack rate (see the `rsprox` skill).

## 10. Dialogues

Every page (index 4286 [464..474]) is:

```
message_game type=dialogue "Name|text"            (the chat-history echo)
runclientscript toplevel_chatbox_resetbackground 2379
if_opensub chatbox:chatmodal (162:568) <- chat_left 231 (npc) | chat_right 217 (player) | messagebox 229 | objectbox 193 | chatmenu 219
if_setnpchead / if_setplayerhead, if_setanim chat<mood><rows> (e.g. chathap3 = happy, 3 lines)
if_settext name, text (server-wrapped with <br>), runclientscript if_settextalign 600 (1,1,lineheight 16/20/..)
if_setevents continue -1..-1 [PAUSEBUTTON], if_settext continue "Click here to continue"
```

- **Options:**
  - Opening the menu sends `if_opensub chatmenu` + `chatbox_multi_init 58 ("Select an option", "a|b|c")` + `if_setevents options 1..n`.
  - The answer is `resume_pausebutton chatmenu:options sub=k`.
  - The **chosen line is then said by the player** as a `chat_right` page (often reworded: "Can you teach me" became "Could you teach me").
- **Close:** `if_closesub chatbox:chatmodal` on the last continue.
- **The `busy` varbit:** goes 1 while the dialogue runs and back to 0 after.
- **Mesbox:** `message_game type=mesbox` + messagebox 229.
- **Ours:** `~chatnpc` / `~chatplayer` / `~mesbox` / `~objbox` / `~p_choice2..5` (`interface_chat/scripts/chat.rs2`) already send this layout.
  - Content supplies the text and the **mood**.
  - Copy every page's text verbatim (strip the `<br>`, which our layout re-inserts) and its mood from the anim name.
  - `tools/rsprox/rsprox_pages.py` lists every page.
  - Where the recording has no line, write none.
- **Facing:**
  - The player gets `[facing] coord=` the npc's tile on each page.
  - The npc turns to the player (`[facing] player=`) when it is not walking. A wandering npc may keep walking for a page or two (Lara, 4286 [466..471]).
  - Ours freezes the npc for the whole dialogue (`playerfaceclose`). Check that against the recording for wanderers.

## 11. Player facing and player info

- **Face after an op:** the face to the loc/npc at the op tick.
  - It is a square face (`facesquare`, engine-automatic for loc ops).
  - Sizes >1 show `sizex/sizez` in 238+.
  - It is reset by a teleport (`[facing] type=reset`).
- **Entity facing** while talking or fighting comes from the interaction, not from script.
- **Anims** (`[sequence]`): note the `delay=` and the stop (`id=-1`).
- **Graphics** (`[spotanim]`): note the `slot=`.
- **Other player-info lines:** `tinting` is `p_tint`; `say` is `say`.
- **BAS and worn equipment** are filtered out of default transcripts. Use `rsprox_index.py get <id> --bas` for stances.
- **Other players** are filtered out too. Use `--all-players`, and never conclude a graphic is private from a default transcript.

## 12. Player movement, teleports, forced walks

| recorded | meaning | ours |
|---|---|---|
| `[walk]`/`[run]` steps | the route, at the move-speed state | click routes, `p_walk(coord)` (server walk: Jagex walks the player to a stand tile before canoe shaping, for example) |
| `[teleport]` far or across planes | snap | `p_telejump(coord)` / `p_teleport(coord)` |
| `[teleport]` with ≤2 tiles on one plane, or a walk-looking 1-2 tile shift during an action | **glide** | `p_teleport` (≤2 tiles same plane glides on our wire) |
| `[exactmove] to1, delay1, to2, delay2, angle` | forced slide (agility, knockback, push) | `p_exactmove(start, end, startcyc, endcyc, dir)`, then move the real tile with `p_teleport`. Copy all five numbers. |
| `[crawl]` | speed-0 movement (tightropes, balancing) | the obstacle's walk with crawl speed |
| a `walk` while the route says `run` | server-forced walk pace | `p_walk` with run suppressed |

- Line up **every step tick** with ours: a teleport one tick late is the common bug. Canoes found client input handled a tick late engine-wide (fixed 2026-10-19).
- A teleport usually comes with `if_opensub fade_overlay` and fade clientscripts, `minimap_toggle`, `cam_*`, and music. Those are part of the same content.

## 13. Damage: splats on the player, and the damage pattern behind them

A green fight with the right animations can still hit twice as hard, never miss, or let a
prayer through ten times too much. ToA (2026-10-21) had all three: Akkha's max 40 (the
recordings say 22), no accuracy roll on any attack, 15% through his prayer (9%), Quiet Prayers
as 50% through (10%), and 74 of its npcs fighting at level 1. Do this for every fight.

**Where the splats are.** A hit on a player rides in that player's player-info block: `[hit]
id=.., value=..` under `[player_info]` -> `[walk|run|idle] player=(index=N, ...)`. The type is
the VIEWER's: on the recorder 16 `hitsplat_damage_me`, 12 `hitsplat_block_me` (a zero: a miss, a
0 roll, or a fully prayed hit), 43 `_damage_max_me`, 65 `_poison_me`, 18 `_shield_me`; 17/13 are
another player's. Ours sends the generic `hitsplat_damage` (28) / `hitsplat_block` and the
server picks the viewer's variant (`torirs_server_combat.c`).

**The tool.** `tools/rsprox/rsprox_player_hits.py <txt>...` prints every splat on the recorder
(`--player NAME` on an `--all-players` transcript) with its cause and the protection prayer up;
`--summary` groups by cause x prayer (n, zeros, max, mean); `--ours <ticklog.tsv>` gives the
same view of our tick log. Attribution, in order:
- `proj`: a projectile aimed at the player, landing on throw + `(endtime + 15) // 30` (Akkha:
  30 -> +1, 48/54/60 -> +2; 14, 6, 7 of them), the splat's tick or one either side;
- `swing`: an npc attack sequence (an `attack` name) on the splat's tick or the one before, by
  an npc whose last face was the player;
- `tile`: a map spotanim on the player's tile; else `none` (poison, recoil, typeless, unseen).

Prayers: varbit 4101 `prayer_allactive`, which every revision sends: bit 12 Protect from Magic
(4096), 13 Missiles (8192), 14 Melee; checked against the named varbits 4116/4117 (rev 241).
Our tick log: `raider.prayers` (varp 83, same bits); a projectile row's `target` is the wire's
(-(pid + 1) a player, slot + 1 an npc, 0 a tile).

**The damage pattern: compute these, per attack, from every session (§ `rsprox` skill).**
1. **Group** by cause x (prayed against its style | not) x raid/level band x the modifiers that
   change damage (Quiet Prayers, a weapon special). Zebak fires mage and ranged with one
   sequence: attribute by projectile, never by sequence.
2. **Normalise** each value by the content's own scaling (ToA: x min(raid + path factor, 250%)),
   so sessions at different levels pool into one base-equivalent distribution.
3. **Accuracy test.** Unprayed, P(0) of a pure 0..max roll is 1 / (max + 1). Far above it means
   the attack rolls accuracy (ToA: Zebak's magic 16% of 116, the scarabs' shots 81% of 442,
   ours 0-4% before the fix). A dodge-based area hit (a slam, a fireball) is not a miss: leave
   those out.
4. **Max test.** The largest unprayed value bounds the base max; it takes ~30 accurate hits.
   Check the npc's own formula too (melee: floor(0.5 + (str + 8) x (strbonus + 64) / 640)).
5. **Through prayer.** At a fixed level, histogram the prayed nonzero values. `floor(roll x t /
   100)` over 0..max gives values 1..k with the top one rare unless t is tuned; fit t on the
   top value's share (Akkha at the cap, max 55: 1/2/3/4 at 34/37/33/33 of 242 -> t = 9).
6. **Seasonal sessions** (Leagues, Grid Master) change the recorder's damage taken. A ratio
   inside one session (prayed max / unprayed max, Quiet Prayers / not) cancels it; absolute
   maxima need a normal-world session.
7. **Expected zeros from the recorder's own gear.** A `--bas` transcript carries the worn
   inventory (`update_inv_* id=worn (94)`); defence bonuses are the cache obj's params
   (`stabdefence`, `magicdefence`, `rangedefence`, ...), levels `update_stat_v2`, prayer boosts
   their varbits. Defence roll = (level x prayer + 8) x (bonus + 64) (magic: 0.7 magic + 0.3
   defence); the npc's roll from the cache's `stat1..6` and attack params, x the content's
   accuracy scaling; P(hit) = 1 - (D + 2) / (2(A + 1)) if A > D, else A / (2(D + 1)). Sum the
   predicted zeros per model and compare (index 4292 Akkha, prayed magic: 17 recorded, 15.8 for
   max 22 / t 9, 11.9 for 40 / 15; prayed ranged: 20, 20.5, 17.9).
8. **Check the npc's levels are ours.** The cache's npc record states `stat1..6` (attack,
   defence, strength, hitpoints, ranged, magic); `_explicit/configs/server_fields.generated.npc`
   froze many npcs at the engine's seed (attack/defence/strength 1, ranged/magic 0), overriding
   the cache (fixed for all 3,784 on 2026-10-21; `tools/check_npc_cache_levels.py` must
   print 0 after any edit there). Print the defence roll
   (`::toabossdef` is the pattern): Zebak's 2240 = (1 + 9) x (160 + 64) was a Defence of 1.

Write the table per attack in the analysis doc: recorded n, zero %, max, base-equivalent max,
prayed distribution, ours from the test's tick log, verdict. A zero rate or a max that matches
only one band is not a match.

## 14. Tile hazards, random placements and the player's own reactions

A rule that matches the recording's shapes can still be wrong in WHEN and WHICH TILE.
Ezekiel's dodges (2026-10-21) and Dreadnoth's blood (2026-10-22) both passed shape audits.
- **Which tile a hazard judges.** For every hazard that hits a raider by tile (a band, a
  pool, a landing, a swing's reach), split the raiders who MOVED on its tick: hit by the tile
  they left, or the one they reached? `docs/rsprox-analysis/guthixian_archive/tools/
  dn_phase.py` is the template (start/end tile x hit, per mechanic). Then check ours judges
  the same: a room tick runs from a raider's queue, BEFORE every raider moves; an npc timer
  before that; a roll (`p_dodgewindow`) lands at the click, before both. Recorded: walks are
  judged on the start tile, rolls after the roll (Ezekiel's swords, 20/20 vs 6/6).
- **Something new this tick** (blood laid, a pool spawned) usually hurts from the NEXT tick:
  check a raider standing still when it appears (Dreadnoth: 48/48 hit at +1) and one stepping
  off that tick (47/47 never). Anything that adds it before the room's check (an npc's death
  in the npc phase) must defer the add.
- **Repeat cadence:** the first hit's offset from stepping on, the gap, and the hit the tick
  after stepping off (dn_bloodrule.py). Simulate ours against every recorded raider-tick.
- **Random placements and counts** (blood tiles, spawn tiles, how many): fit, don't eyeball.
  Count per event against the state before it (existing hazards, walls), compare models by
  likelihood and EM over the number of tries (dn_boxmodels.py, dn_jellyblood_fit.py), and look
  for what NEVER happens (no tile twice, never the middle, never over an existing one, never
  off the floor: 9/9, 0/63, 0/1162).
- **A death cancels what is in flight, selectively:** list every pending landing/shot at each
  recorded death and what still arrived (Dreadnoth: the heavy shot never lands, the light shot's
  hit still does, a landing due on the death tick does).
- **The raider's own reaction to a hit:** the defend seq on every hit the content deals
  (`anim(%varp6959_com_defendanim)`), unless an attack takes the slot; measure it per room
  (dn_block.py: Dreadnoth 2167/2167, Ezekiel 202/203, but two other Archive rooms mostly
  not). A spectated recording shows every raider's seqs: use `--all-players`.

## 15. When the wire matches and it still looks wrong: the client

The owner's visual complaint is real. If our packets now match the recording block for block
(the inventory above, every value), the remaining cause is in what the client does with them.
Do not close a visual complaint on a wire diff alone (Ezekiel's facing: wire matched since
2026-10-21, the client still faced him away, found 2026-10-22).
- **Read the client's own state first**: the live driver's npc rows (`t.npc.tiles(sym)` third
  return) carry `yaw`, `dst_yaw`, `face_x/z`, `ready_anim`, `walk_anim`, `pose_anim`,
  `body_model` per tick. Log them through the section (a `t.note` per tick) and compare with
  what the wire told it (Ezekiel: face at the cage north, `yaw 0 dst 0` for 180 ticks).
- **Then the deob for the client rule** (`~/Documents/git_repos/Deobfuscator/
  src_osrs239_rl1_12_33/deob`, never Client-TS): Ezekiel's coord face is held by the deob
  (`class105.method3531` clears only the instant flag), ours consumed it on an exact move's
  last cycle.
- **Photographs when needed** (live client, private everything): build the client with
  `make -C src OPT=1 EMBED_SERVER=1 PLATFORM_OBJ_BASE=build_<x> PLATFORM_TARGET=torirs_<x>`,
  compile the content into a private pack, and run the test through a wrapper that imports
  `tools/quest_gate/run.py` with `ensure_scripts` / `ensure_server_pack` stubbed (they rebuild
  the SHARED pack from a tree others are editing) under `QUEST_BINARY=` and
  `TORIRSSERVER_SCRIPTS=`. `t.shot` re-aims away from "occluders" (a cage's blocking locs):
  set `t.drive._shot_plan = function() return nil, nil, "fixed" end` and
  `t.drive.camera(yaw, pitch, zoom)` (a SMALLER zoom is closer); crop with PIL. The npc seed is
  the run name: rerun under the same name to compare two packs on one fight.

## Traps that have each cost a wrong fix

- One recording = one roll. Count across sessions and quote counts ("37/37").
- Recorded ids are from that session's revision. Name them in our cache (rev 241) first.
- A seasonal recorder (Leagues, Grid Master) changes damage, attack rate and party scaling.
- Instance tiles are template-translated. Measure player-to-npc distances on `--raw`.
- Do not invent lines, messages or sounds the recording lacks. Do not drop ones it has.
- A behaviour no recording shows is labelled *unverified* in the write-up, with its source (wiki/reference).
- **A tool's silence is not the wire's.** Before trusting "matched" from any timeline tool, check
  it prints the block kind at all (`npc_block_inventory.py`); `[bas]` and `[body_customisation]`
  were invisible to every audit of Ezekiel.
- **"Matched" on counts per window is not matched per life:** a value the recording varies per
  copy (models, stances, drop variants) needs the per-life table.
- **Never mutate shared source to test** (CLAUDE.md). Build private (`PLATFORM_OBJ_BASE=build_<x>`). If another session broke the shared pack, use the private packs recipe from memory.
