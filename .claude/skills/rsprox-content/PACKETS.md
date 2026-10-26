# RSProx transcript packets: field by field

Every packet kind that appears in the archive (176 local transcripts plus the index's rev-241
recordings, 2026-10-20), with what each field means, its units, how it changed across revisions,
and the script command that produces it here. Read this alongside a transcript. SKILL.md has the
workflow.

## Conventions

- **Layout.** `[n]` is a server tick. Lines indented 4 are packets in arrival order within that
  tick. In `player_info`/`npc_info`, an 8-indent line is one entity's movement or add/del, and
  12-indent lines under it are that entity's extended info.
- **Names.** Rev 230+ prints `name (id)`. Rev 227 prints bare ids. **Every id is from the
  recording's revision, not necessarily ours (241)**: look it up in
  `OSRS-Content/osrs-content/configs/all.<type>.compack` before using it. Synth and area-sound
  ids are named in `pack/4_soundeffects.pack`. Hitsplats are in `all.hitsplat.compack` (12
  block_me, 13 block_other, 16 damage_me, 17 damage_other, 18 shield_me). Healthbars are in
  `all.healthbar.compack` (0 standard_30, 17 standard_60, 20 standard_120).
- **Units.** A *cycle* is a client cycle, 20 ms, 30 per tick. Every `delay`, `starttime`,
  `endtime`, `start`/`end` and `cycles` field is in cycles, counted from the packet's arrival.
  **Angles** run 0..2047: 0 south, 512 west, 1024 north, 1536 east. **Coordinates** are
  `(x, z, level)` absolute, but instance tiles are translated to the template unless the
  transcript is `--raw`.
- **Our command** is the RuneScript command (`OSRS-Content/osrs-content/server/scripts`) that
  puts this packet on our wire. The signatures are in `src/serverscript/ssc_command_kinds.gen.h`.
  Our extra opcodes (11xxx) are documented in `src/serverscript/gen_opcode_meta.py` EXTRA_OPCODES.
  To see our own wire, set `TORIRSSERVER_TRACE_OUT=1` (or `=2` to include packet bodies).

## Client -> server (what the player did)

These are filed under the tick the proxy saw them, which **jitters by one**. Time everything from
the server's first answer, never from these.

| packet | fields | meaning | our trigger |
|---|---|---|---|
| `oploc1..5` (`_v1`/`_v2` = protocol versions) | `id, coord` | loc menu op N | `[oploc<N>,<loc or _category>]`; at range `[aploc<N>,...]` |
| `oploc6` | `id` | Examine | (client-side text) |
| `oploct` | loc + `com, sub, obj` | inventory item used on a loc | `[oplocu,...]` / spell `[oplocT,...]` |
| `opnpc1..5` | `npc=(index,id,coord)` | npc menu op N (op2 is usually Attack) | `[opnpc<N>,...]` / `[apnpc<N>,...]` |
| `opnpc6` | `id` | Examine npc | |
| `opnpct` | npc + `com` (spell) | spell or item on an npc | `[opnpct,...]` / `[apnpct,...]` |
| `opobj1..5`, `opobj6`, `opobjt` | `id, coord` | ground item op (3 = Take) | `[opobj<N>,...]` |
| `opplayer1..8`, `opplayert` | `player=(...)` | player op (Follow, Trade, Attack...) | `[opplayer<N>,...]` |
| `opworldentity1..6` | `worldentity=(index,id,coord,sizex,sizez)` | sailing boat (world entity) op | vessel scripts |
| `if_button1..10`, `if_button`, `if_buttonx` | `com=iface:comp (g:c), sub, obj, op` | interface component op. `sub` = grid slot; `obj` = item in it | `[if_button,...]` / `[opheld<N>,obj]` for inventory items |
| `if_buttont` | `selectcom/sub/obj` -> `targetcom/sub/obj` | use an item on an item | `[opheldu,obj]` |
| `if_subop` | `com, op, subop, sub, obj` | rev-241 sub-menu op (e.g. ring teleports) | `last_subop` |
| `resume_pausebutton` | `com, sub` | "Click here to continue", or a chat option (`chatmenu:options`, `sub` = option 1..5) | resumes `p_pausebutton` / `if_addresumebutton` |
| `resume_p_countdialog` / `_namedialog` / `_stringdialog` / `_objdialog` | `count` / `name` / `string` / `id` | answers to "Enter amount" / name / text / item-search prompts | `p_countdialog`, `p_namedialog`, `last_int` / `last_string` |

Walk clicks (`move_gameclick`) **are not printed** in these transcripts. Movement shows only as the
server's `player_info` steps.

## Player info (`[player_info]`)

The recorder's own player only, unless the transcript was made with `--all-players`.

| line | fields | meaning | our command |
|---|---|---|---|
| `[idle] player=(index,name,coord)` | | no movement this tick; ext-info follows | |
| `[walk]` / `[run]` / `[crawl]` `... newcoord=` | | **The label is the player's move-speed state** (0 crawl, 1 walk, 2 run), not the distance. A `run` line can move one tile (the last odd step of a run). | `p_walk`, a click route. `p_run`/`p_temprun` set the speed. |
| `[teleport] ... newcoord=` | | move speed 127: placed, not walked. A plane change is always this. | `p_telejump` (always), `p_teleport` (snaps unless the move is ≤2 tiles on one level, which **glides**: sent as a walk or run step) |
| `[add]` / `[del]` | | another player enters or leaves view | |
| `[sequence] id=..., delay=` | seq; delay in cycles | the player's animation. `id=-1` stops it. | `anim(seq, delay)`; `anim(null, 0)` stops |
| `[spotanim] slot=, id=, delay=, height=` | slot 0..255 (absent = 0); height in raw units (92 chest, 124 head) | a graphic on the player | `spotanim_pl(spot, height, delay)`, `spotanim_pl_slot(spot, slot, height, delay)` |
| `[face_angle] angle=[, instant=true]` (rev ≤237) | 0..2047 | turn to a fixed angle. A face on a LOC also arrives as this in ≤237. | `facesquare(coord)` (the server computes the angle) |
| `[face_pathingentity] npc=/player=/entity=null` (≤237) | | keep facing an entity / stop | interaction engine: talk-to, attack, follow |
| `[facing] ...` (rev 238+, one packet for all of the above) | `coord=(..)[, sizex=, sizez=]` faces a tile or a loc's footprint; `angle=[, instant=true]`; `npc=/player=(...), fallbackangle=`; `type=reset` | as the three ≤237 kinds | as above; a player's `angle=` is `p_faceangle(angle, instant)` (rev 241) |
| `[exactmove] to1=, delay1=, to2=, delay2=, angle=` | to1/to2 absolute tiles; delays in cycles; angle 0..2047 | forced movement (agility, knockback). Slides from to1 (reached at delay1) to to2 (at delay2), facing `angle`. The player's real tile is set separately (a teleport line) | `p_exactmove(start, end, startcyc, endcyc, dir)` + `p_teleport` |
| `[hit] id=, value=` | hitsplat type, amount | splat | combat / `hitmark` / `hitmark_bar` |
| `[headbar] id=, fill=` | healthbar type; fill out of the bar's width (30 for id 0) | hp bar. Ours: `1 + left*(w-1)/span`, 0 when dead | automatic on damage |
| `[say] text=` | | overhead text | `say(text)` |
| `[tinting] start=, end=, hue=, saturation=, lightness=, weight=` | cycles; hsl 0..255 (-1/255 leaves a channel alone); weight 0 clears | colour overlay (freeze, poison) | `p_tint(start, end, h, s, l, weight)` |
| `[transparency] start=, end=, starttransparency=, endtransparency=, usestarttransparency=` | cycles from arrival (signed), alpha 0 opaque..255 gone | the body fades in/out (rev 241, player flag 0x10000) | `p_transparency(start, end, from, to, usestart)`; npcs `npc_transparency(start, end, from, to)` |
| `[appearance]` + `[details] name, combatlevel, gender` / `[bas]` / `[model]` | | worn kit; BAS = the stance/walk anims. **BAS and equipment are filtered out** unless transcribed with `--bas`. | equipment, `p_transmogrify`, `walkanim` etc. |
| `[player_reset] value=` | | clears the client's per-player animation state | |
| `[name_change]`, `[freeze] delay, duration` | | rare | |

## Npc info (`[npc_info_small_v4..6]` / `[npc_info_large_*]`)

"small" vs "large" is only the view radius the client asked for, not the content.

| line | fields | meaning | our command |
|---|---|---|---|
| `[add] npc=(index,id,coord), creationcycle=, spawnangle=, jump=` | spawnangle in words (rev 234+) or `angle=`; creationcycle = the client cycle the npc was created | npc enters view (or spawns) | `npc_add(coord, npc, duration)`; facing on spawn: `npc_facedir(coord)` (persistent 8-way) |
| `[del] npc=...` | | leaves view or despawns | `npc_del` / despawn |
| `[idle]` | | no movement; ext-info follows | |
| `[walk] ... step=` | one step | 1 tile at walk speed | `npc_walk(coord)`, hunt/chase modes |
| `[run] ... step1=, step2=` | two steps | 2 tiles in one tick | npc run speed |
| `[crawl] ... step=` | one step at half speed | slow movers (crawl BAS) | `npc_setmovespeed` |
| `[teleport] ... newcoord=` | coord shift, no steps, jump=false | **the client walks it there** (queued as a step when within 8 tiles), so a one-tile `teleport` every tick is a smooth glide (Bloat, Vasa, Zebak's jugs). It is NOT a snap. | `npc_tele(coord)` ≤2 tiles on one floor: ours glides too, but on our wire it is a walk/run step (RSProx would print `[walk]`/`[run]`); same look, different label |
| `[telejump] ... newcoord=` | coord shift with jump=true | instant snap | `npc_tele(coord)` >2 tiles or across floors: ours sends a remove + re-add (`[del]` then `[add]`), which the client also draws as a snap |
| `[sequence] id=, delay=` | seq, cycles | npc anim; `-1` stops | `npc_anim(seq, delay)` |
| `[spotanim] slot=, id=, delay=, height=` | as player | graphic on npc | `spotanim_npc(spot, height, delay)`, `spotanim_npc_slot` |
| `[face_angle]` / `[face_coord] coord=` / `[face_pathingentity]` (≤237); `[facing]` (238+) | as player; `face_coord` is the npc facing a tile | turn | `npc_facesquare(coord)`; entity facing from modes (`npc_setmode(opplayer2)`, `playerfaceclose`), cleared by `entity=null` / `type=reset` |
| `[transformation] id=` | new npc type | changes type in place (forms, states) | `npc_changetype(npc, duration)` |
| `[level_change] level=` | combat level shown | sent on every transform | engine (sent with `npc_changetype`) |
| `[enabled_ops] opflags=0b11111` | 5 bits, op1 = lowest | which ops are clickable. Jagex hides all (`0b00000`) when a death starts, and re-sends `0b11111` on transform. | `npc_setops(mask)` |
| `[hit]`, `[headbar]` | as player | | `npc_hitmark`, `hitmark_bar`, combat |
| `[say] text=` | | overhead text | `npc_say(text)` |
| `[exact_move] to1=, delay1=, to2=, delay2=, angle=` | as player | forced slide (knockback, roll, jump) | `npc_exactmove(start, end, c1, c2, dir)` |
| `[tinting]` | as player | | `npc_tint(...)` |
| `[bas]` | turn/walk/run/ready seqs | npc's movement anims overridden | |
| `[body_customisation]` + `[model] id=` / `[recolour]` | | body model or recolour override | `npc_bodymodel(model)` |
| `[headicon_customisation]` + `[headicon] headiconslot, id, spriteindex` | sprite group + index | overhead icon (prayer/boss phase) | `npc_headicon(slot, group, index)` |
| `[head_customisation]` | | chathead override | |

Npc-scoped "specific" packets go only to one player: `[npc_anim_specific] npc=(index), anim=`,
`[npc_spotanim_specific] npc=(index), slot=, spotanim=`, `[npc_headicon_specific] npc=(index),
slot, graphic, graphicindex` (65535 = none). Per-player graphics on a player use
`[player_spotanim_specific]` and `[player_anim_specific]`.

## Zone packets (the world around the player)

All are sent to every player who can see the zone, except `*_specific`, which go only to the
recorder. **A level change, region change or teleport re-sends the whole zone** (every
`loc_add_change`, `obj_add_specific` and `loc_del` in view). Do not read that burst as content
events. `rsprox_interact.py` collapses it.

| packet | fields | meaning | our command |
|---|---|---|---|
| `loc_add_change_v1/_v2` | `id, coord, shape, rotation[, opflags=0bNNNN]` | a loc appears, or the one in that slot (same coord and shape layer) changes. `opflags` = ops shown (rev 241, sailing). | `loc_add(coord, loc, rotation, shape, duration)`; `loc_change(loc, duration)` on the active loc; `loc_add_revert(...)`; ops: `loc_add_op` |
| `loc_del` | `coord, shape, rotation` | the loc in that slot goes | `loc_del(duration)` on the active loc (duration = ticks until it comes back; ≤0 = gone for good) |
| `loc_anim` | `coord, shape, rotation, anim=seq` | loc plays an animation (a tree falling, a chest opening) | `loc_anim(seq)` on the active loc |
| `loc_anim_specific` | `id, coord, shape, rotation` | per-player loc anim | |
| `loc_merge` | `id, coord, shape, rotation, start, end, minx..maxz` | the player's model merges with a loc for `start..end` cycles (agility obstacles, hiding) | `p_locmerge(start, end, se, nw)` |
| `map_anim` | `id=spotanim, [delay=], height=, coord` | graphic on a tile; height raw | `spotanim_map(spot, coord, height, delay)` |
| `map_anim_specific` | same | per-player tile graphic | `spotanim_map_specific(...)` |
| `map_projanim_v1/_v2`, `projanim_specific_v3` | `id, starttime, endtime, angle, progress, startheight, endheight` + `[source] coord[, npc=/player=]` + `[target] coord[, npc=/player=]` | a projectile. starttime = cycles before it appears; endtime = cycles until it lands (from the packet, not from starttime); **heights: a v1 packet (rev up to ~233) prints our script units (the byte the client multiplies by 4); a v2 / specific_v3 packet (rev 234+) prints raw heights, 4x ours -- divide by 4**; angle = launch slope; progress = start offset along the path; `[source]` = launch tile (an npc's centre for size>1); `[target] npc=/player=` = homing, coord only = a tile | `projanim_pl/_npc/_map(src, target, spot, startheight, endheight, starttime, endtime, angle, progress)` (heights in our units per the rule left); `npc_projanim_*` launches from the active npc |
| `scriptedproj_add` (rev 241) | `id, slot, starttime, endtime, curvescripth, curvescripta, curvescriptt` + `[source] coord, offsetx, offsetz, height[, npc=]` + `[target] coord, offsetx, offsetz, height[, player=]` | a projectile with no arc: every cycle the client runs the three curve clientscripts on the progress (0..65535) and places it at start + side*H*128 + up*A*128 + along*T*distance (deob class56.method1439). **Heights are raw and used as printed (no /4)**; `slot` is a per-shot counter the server assigns | `npc_scriptedproj_pl(src, player_uid, spot, startheight, endheight, starttime, endtime, curve_h, curve_a, curve_t)` from the active npc; other revisions get a flat `projanim` instead |
| `sound_area` | `id, loops, [delay=], range, [size=], coord` | a sound heard by everyone within `range` tiles, with falloff; `size` = full-volume radius; delay in cycles. **Only players inside the range receive it**, so the recorder hears only nearby ones. `id=-1` exists (an npc with no sound). | `sound_area(synth, delay, coord, range)`; `sound_area_size(..., size)` |
| `obj_add` / `obj_add_specific` | `id, count, [reveal=, despawn=, ownership=self/other/group,] neverturnpublic, coord` | ground item appears. reveal/despawn in ticks; ownership = who sees it first | `obj_add(coord, obj, count, duration)`, `obj_add_private` |
| `obj_del` / `obj_del_specific` | `id, count, coord` | ground item gone (taken or despawned) | `obj_del` / take |
| `obj_count` / `obj_count_specific` | `id, oldcount, newcount, coord` | a ground stack changes | merging drops |
| `obj_enabled_ops` | `id, opflags, coord` | ground item ops shown | |

## Sounds and music (to the recorder only)

| packet | fields | meaning | our command |
|---|---|---|---|
| `synth_sound` | `id[, loops=, delay=]` | a sound at full volume for this player only (UI clicks, own actions: equip 2229, door 62, gate 66/67, tree_fall 2734) | `sound_synth(synth, loops, delay)` |
| `midi_song_v2` | `id, fadeoutdelay, fadeoutspeed, fadeindelay, fadeinspeed` | music track. `0/0/0/0` = cut; `0/60/60/0` = the normal crossfade | `midi_song(track)` sends 0/60/60/0; `midi_song_envelope(id, od, os, id, is)` for any other |
| `midi_song_withsecondary` | `primaryid, secondaryid, fades` | a track with a second layer | |
| `midi_jingle` | `id, length=ms` | jingle (level up, quest done) | `midi_jingle(jingle)` (length from our table) |
| `midi_swap` | fades | swap to the secondary layer | |
| `ambientsound_start` / `_stop` | `id, fade` | area ambience bed | `ambientsound(...)` |

## Interfaces and client state

| packet | fields | meaning | our command |
|---|---|---|---|
| `if_opentop` | `[previousid,] id` | the gameframe (`toplevel_display` 165 → `toplevel_pre_eoc` 164 / `toplevel_osrs_stretch` 161 by client mode) | `if_opentop` |
| `if_opensub` | `com=parent:slot, id=iface, type=modal/overlay` | open an interface into a frame slot. modal = closes on walk; overlay = stays | `if_opensub(com, iface, type)`; `if_openmain`, `if_openchat`, `if_openoverlay`, `if_openside` |
| `if_closesub` | `com, id` | close what is in the slot | `if_closesub(com)`, `if_close` |
| `if_movesub` | `sourcecom, destcom, [id]` | move open subs to another frame (resize/mode switch) | `if_movesub` |
| `if_settext` | `com, text` | set text. `<br>` = line break, `<col=rrggbb>` = colour | `if_settext(com, text)` |
| `if_sethide` | `com, hide` | show/hide a component | `if_sethide(com, bool)` |
| `if_setevents_v1/_v2` | `com, start, end, events=[OP1, PAUSEBUTTON, IF_SCRIPT_TRIGGER, ...]` | which clicks a component (or grid slots start..end; -1/-1 = not a grid) sends | `if_setevents(com, from, to, events)` |
| `if_setanim` | `com, id=seq` | animate a model component (chatheads: `chat<mood><rows>`) | `if_setanim(com, seq)` |
| `if_setnpchead` / `if_setplayerhead` | `com[, id=npc]` | chathead model | `if_setnpchead(com, npc)`, `if_setplayerhead(com)` |
| `if_setobject` | `com, id=obj, zoomorcount` | item model in a component (objbox) | `if_setobject(com, obj, zoom)` |
| `if_setmodel(_v1/_v2)` | `com, id=model` | raw model | `if_setmodel(com, model)` |
| `if_setposition` | `com, x, y` | move a component | `if_setposition` |
| `if_setangle` | `com, anglex, angley, zoom` | model view angle | `if_setangle` |
| `if_setcolour` | `com, colour` (RGB555) | recolour (the hp hud spams this; ignore) | `if_setcolour` |
| `if_setscrollpos` | | scroll | `if_setscrollpos` |
| `runclientscript` | `id=script, types=[...], values=[...]` | run a cache clientscript with args. Much of the UI is done this way: `if_settextalign` 600 (dialogue line height), `chatbox_multi_init` 58 (chat options), `mesoverlay` 1974 (tutorial instruction box), `ui_highlight_update` 8478 / `ui_highlight_clear` 8484 / `inv_highlight_item_on` 8461 (glow highlights; a dbrow style arg), `tutorial_overlay_hint` 2584, `toplevel_sidebutton_switch` 915 (select a side tab), `toplevel_mainmodal_open` 2524, `fade_overlay` 948 (screen fades), `camera_do_zoom` | `runclientscript*(^clientscript_name)(args...)` / `runclientscriptvararg` |
| `if_runscript` / `if_script_trigger` | `com, sub, script=crc, payload` | client → server: a component's script fired | `[if_button,...]` with IF_SCRIPT_TRIGGER events |
| `hint_arrow` | `npc=(...)` / `player=(...)` / `coord=, height=, position="Center"/"North"...` / `type="reset"` / `worldentity=` | the yellow hint arrow | `hint_npc` / `hint_pl` (the active npc / player, no args), `hint_coord(position, coord, height)`, `hint_stop` |
| `minimap_toggle` | `state=0` normal, `2` hidden/no-click | minimap state (cutscenes, canoe ride) | `minimap_toggle(state)` |
| `hidelocops` / `hidenpcops` / `hideobjops` | `hide` | hide every op of that kind (cutscenes) | `hidelocops(bool)`, `hidenpcops`, `hideobjops` |
| `set_player_op` | `id, op="Follow"/null` | the right-click ops on other players | `set_player_op(string, int, int)` |
| `set_interaction_mode` / `reset_interaction_mode` | | tile/entity click modes (sailing) | |
| `cam_moveto(_v1.._v3)` / `cam_lookat(_v1.._v3)` | `coord, height, rate, rate2[, heightrelative]` | camera eye / look target. Per client cycle the camera moves a flat `rate` plus `rate2`/1000 of what is left; **`rate2` >= 100 places it at once** (every cutscene's first placement is 232/232), small values (5..20) are a slow pan (src/app/app_camera.c `app_cinema_ease`, reference cinemaCamera). `coord` is world-absolute: in an instance it is the copy's tile, and the transcript prints it translated to the template | `cam_moveto(coord, height, rate, rate2)`, `cam_lookat(...)` (heights as printed) |
| `cam_moveto_cycles(_v3)`, `cam_rotateto_coordinate_v3` | coords, `cycles`, `easing=linear/ease_out_sine`, `ignoreterrain` | timed/eased camera moves | `cam_moveto_cycles(coord, height, cycles, ignore_terrain, easing)`, `cam_rotateto_coordinate(coord, height, cycles, easing)` |
| `cam_lookat_cycles`, `cam_lookat_eased_coord`, `cam_moveto_arc(_v3)`, `cam_rotateby`, `cam_rotateto` | as above | timed/eased moves | (no command yet; add an opcode) |
| `cam_shake` | `axis, random, amplitude, rate` | sent once per axis (0..4); a cutscene lock clears all five with zeros | `cam_shake(axis, random, amplitude, rate)` |
| `cam_reset` | | camera back to the player. A cutscene's unlock sends it TWICE (before and after the side panels) | `cam_reset` |
| `cam_unlock` | `unlock=true/false` | the client's camera pitch limits widened (true: 0..520) or put back (false: the profile's 128..383); deob `field758`. Sent `false` once at every cutscene's unlock, before the first `cam_reset` (4286 10/10, 4336 7/7) | `cam_unlock(boolean)` (script opcode 11285) |
| `cam_skybox model=` / `zbuf` | | rare | |
| `message_game` | `type=, message="..."` | chat-box line. Types: `gamemessage`, `dialogue` (the `Name|text` echo of a dialogue page), `mesbox` (echo of a mesbox), `spam` (filterable), `engine`, `friendschatnotification`, `clanmessage`; numbers for the unnamed ones (108 welcome, 117 "Did you know") | `mes(text)`, `mes_spam(text)`; dialogue echoes come from the chat procs |
| `message_public` | `type, message` | a player's public chat | |
| `logout_withreason`, `logout_transfer`, `reconnect`, `update_reboot_timer gamecycles=`, `clear_entities` | | session | |

## Vars, inventories, stats

| packet | fields | meaning | our command |
|---|---|---|---|
| `varp_small` / `varp_large` | `id=name (n), old, new` | a varp changed (`old` is RSProx's own tracking) | `%varp = ...` (needs `transmit=yes`) |
| `varbit` | `id, old, new` | a varbit changed (RSProx derives it from the varp) | `%varbit = ...` |
| `varp_reset` | | all varps zeroed (login) | |
| `update_inv_full` / `update_inv_partial` + `[objs] - slot=, id=, count=` | `com=-1:64209, id=inv (93)` | inventory contents (93 inv, 94 worn, 95 bank) | `inv_add`/`inv_del`/... + `inv_transmit` |
| `update_inv_stoptransmit` | `id` | stop sending an inv (bank closed) | `inv_stoptransmit` |
| `update_stat_v2` | `stat, level, experience` | a skill changed | `stat_*` |
| `update_stockmarket_slot*`, `update_tradingpost*` | | Grand Exchange | |

Clock varps to ignore: `map_clock` (3079, +1 every tick, rev 241) and the `date_*` varbits
(12391/12392). `busy` (varbit 12393) is 1 while a dialogue or a delayed action holds the player.
`chatmodal_unclamp` (10670) is set on the first dialogue page. `current_hint_arrow` (14192) and
`flashside` (3756, the flashing side tab) are tutorial state.

## A packet with no command: adding one

`cam_unlock` (2026-10-23) is the worked example; follow its trail (`grep -rn "CAM_UNLOCK\|cam_unlock" src`).
1. **What the client does with it**: the deob's handler (`grep -n "(<opcode>, <size>)" deob/class243.java`
   for the packet object, then its `== var3.field1550` branch in client.java) and every read of the
   field it sets. Copy that behaviour into the client, not a guess.
2. **Client**: append `PKT_NAME_*` to `src/net/rev/pktnames.h` (never insert); map the opcode in
   each revision's `packetin.h` row (they are listed with `PKT_NAME_NONE` already); a `struct Pkt*` in
   `revpacket.h`; the plain parser in `gameproto_parse.c`; a `bridge_*` in `net/proto/proto_in_adapt.c` when
   `3rd/rsprot/packets/<name>.c` scrambles bytes per revision (241 often does: `U1_ALT3`); the exec in
   `rs_gameproto_exec.c`; a line in `scriptrun_core.c`'s packet log.
3. **Server**: an encoder `ToriRSServer_Send*` in `torirs_server_encode.c` (`OP_* = PKT_NAME_*`),
   writing through a `ToriRSServerWirePayload` hook when the layout differs by revision (`wire.c`
   for 239, `wire241.c` with the rsprot codec); the name in both revisions' transcribed lists and in
   `wire.c`'s name switch; the declaration in `torirs_server.h`.
4. **Script command**: an `EXTRA_OPCODES` row in `src/serverscript/gen_opcode_meta.py` (next free id
   above every other row, uncommitted ones included) and its typed signature in
   `gen_command_kinds.py`; regenerate (`python3 src/serverscript/gen_opcode_meta.py`,
   `python3 src/serverscript/gen_command_kinds.py`); the `case SS_OP_*` in
   `torirs_server_scripts.c`; `cd src && python3 torirsserver/gen_opcode_coverage.py`.
5. Build privately (`make -C src OPT=1 EMBED_SERVER=1 PLATFORM_OBJ_BASE=build_<x>
   PLATFORM_TARGET=torirs_<x> torirs_<x> sscompile`), then use it in content and check the wire.
