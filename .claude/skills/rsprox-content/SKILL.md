---
name: rsprox-content
description: Implement or correct OSRS content (RuneScript in OSRS-Content) from RSProx recordings — loc/npc/obj ops and ap triggers, ladders/stairs/maplinks, doors and loc adds/changes, projectiles, area sounds and synths, spotanims and anims, interface packets (open/close/hide/move/text/highlights/hint arrows), npc info (movement, facing, transforms, ops), dialogues, player facing/movement/teleports/exactmove. Trigger on "implement X from RSProx", "make this match the recording", "port this content from RSProx", or any content work where a recording shows the behaviour.
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
3. **Write the timeline down** in the analysis doc as `L+1 face; L+2 seq 827, busy=1; L+3
   teleport`, citing session and tick (`index 4286 [38]`).
4. **Find our side.** Grep `OSRS-Content/osrs239-content/server/scripts` for the trigger (by name,
   or by `_category` from the loc's `.loc` config). Check the shared procs first:
   - `~climb*` / `~maplink_try` in `ladders_stairs/scripts/`
   - door procs in `doors/scripts/`
   - `~chatnpc` / `~chatplayer` / `~mesbox` / `~objbox` / `~p_choice*` in `interface_chat/scripts/chat.rs2`
5. **Implement**, mapping each recorded line to its command with PACKETS.md.
6. **Verify on our wire, not in our heads.** Run a scriptrun test (`test/<area>/<name>.lua`,
   `t.ticklog.start()`), then compare in two ways:
   - **Event timing:** `tools/rsprox/ticklog_tells.py` and `tools/rsprox_tells.py --anchor ... --ours`.
   - **Packet-exact checks:** `TORIRSSERVER_TRACE_OUT=1` (2 adds the bodies) prints our every
     packet with the tick. Count `SERVER_TICK_END` to line ticks up.

   Remember the tick log has no message/interface rows (see the `rsprox` skill's §5).

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
- Script: `projanim_pl/npc/map(src, target, spot, startheight/4, endheight/4, starttime, endtime, angle, progress)`.
  Our wire multiplies the heights by 4. `npc_projanim_*` launches from the active npc.
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

- Npc: `npc_anim(seq, delay)` and `spotanim_npc(spot, height, delay)`; `_slot` variants for a non-zero `slot=`.
- Player: `anim(seq, delay)` and `spotanim_pl(...)`.
- Tile: `spotanim_map(spot, coord, height, delay)`.
- Heights are raw (92 chest, 124 head) and delays are in cycles.
- `id=-1` stops (`anim(null, 0)`).
- **Check each seq id's 239 name** (a recorded chathead seq can be an axe special in 239).
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

## 9. Npc info (analyse in detail)

For each npc type, build the **per-life record** (spawn to despawn, `tools/rsprox_nylocas.py`'s parser handles index reuse):

- **Spawn:** the `[add]` tick, `spawnangle` (our `npc_facedir`), `creationcycle`, and the first seq/spotanim (spawn anim on the add tick).
- **Movement kind per tick:**
  - `walk` / `run` / `crawl` carry steps.
  - `teleport` is a jump-clear shift that **the client walks** (glide; Bloat, Vasa, jugs).
  - `telejump` snaps.
  - Ours: `npc_walk` / modes give steps, `npc_tele` ≤2 tiles glides, and further moves remove and re-add.
  - Count tiles per tick to get the speed.
- **Facing:**
  - **Entity** (`face_pathingentity` / `[facing] player=..., fallbackangle=`) is the npc's target; it comes from modes (`npc_setmode(opplayer2)`, `playerfaceclose`) and clears on `entity=null` / `type=reset`.
  - **Square** (`face_coord` / `[facing] coord=`) is `npc_facesquare`.
  - **Angle** is `npc_facedir`.
  - Note **when** it re-faces (every attack? only on target change?).
  - Several bosses swing with the facing **held** into the swing (Tekton).
  - `playerface` drops past the npc's `maxrange` from spawn (default 7).
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

## Traps that have each cost a wrong fix

- One recording = one roll. Count across sessions and quote counts ("37/37").
- Recorded ids are from that session's revision. Name them in 239 first.
- A seasonal recorder (Leagues, Grid Master) changes damage, attack rate and party scaling.
- Instance tiles are template-translated. Measure player-to-npc distances on `--raw`.
- Do not invent lines, messages or sounds the recording lacks. Do not drop ones it has.
- A behaviour no recording shows is labelled *unverified* in the write-up, with its source (wiki/reference).
- **Never mutate shared source to test** (CLAUDE.md). Build private (`PLATFORM_OBJ_BASE=build_<x>`). If another session broke the shared pack, use the private packs recipe from memory.
