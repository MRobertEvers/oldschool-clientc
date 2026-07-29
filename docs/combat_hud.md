# The combat HUD: hitsplats, skills, levels, facing, and the combat tab

Five things that were visibly wrong while fighting in the rev-230 mock, what
each one is actually made of, and where the gap was. Reference throughout is
**OpenRune** (`~/Documents/git_repos/OpenRune-Server`), whose server drives the
same OldSchool interfaces.

Four of the five had their root cause in the **client**, not the mock. That is
worth saying up front, because "the server isn't sending it" is the first guess
every time and it was right once out of five.

| | symptom | made of | gap |
|---|---|---|---|
| 1 | number, no splat | hitsplat config group 32 → sprite id | client looked for a sprite *archive* that does not exist at this revision |
| 2 | skills tab all zeros | CS2 `STAT`/`STAT_BASE`/`STAT_XP` | opcodes had a signature but no handler — the stub pushed 0 |
| 3 | no `(level-N)` on npcs | local player's combat level | server never sent `UPDATE_PID`, so the client did not know which entity was itself |
| 4 | facing never clears | `FACE_ENTITY` mask | it is a latch; the server never sent the clear |
| 5 | combat tab: only auto-retaliate | CS2 + weapon category + four varps | **partly open** — see §5 |

---

## 1. Hitsplats: the splat is a config record, not a sprite sheet

**How it works.** A hit is one bit of the entity info stream (`DAMAGE`), carrying
damage, type, health and max health. The client keeps four concurrent splats per
entity and draws each as *sprite + number*:

```
app_overlay_build_entity()   src/app.c
  → sprite at (screen_x-12, screen_y-12)
  → the damage number, twice: black at +1,+1 then white  (reference draws both)
```

Which sprite depends on the era, and the damage *type* means a different thing
in each:

- **dat1 / 2004-era**: one sprite archive named `hitmarks`, and the damage type
  is the frame index within it. `STATIC_SPRITE_HITMARKS` in
  `src/engine/static_sprites.c` binds it.
- **OldSchool**: no such archive. Each splat type is its own **config record** in
  group 32, and opcode 5 of that record is an ordinary sprite id. In
  `cache.osrs230` there are 78 of them; type 0 (damage) is sprite 2270, type 1
  (block) is 3521, and 25 records carry no sprite at all.

**The gap.** The client only knew the first form. `Dat2SpriteLoadByName: archive
'hitmark' not found in sprites table` was in every boot log, the static slot
stayed −1, and `if( hitmarks_scene > 0 )` skipped the sprite — so the number
drew alone. Nothing else went wrong, which is why it survived so long.

**The fix.** `src/game/rs_hitsplat.{c,h}` holds a type → sprite-id table;
`src/engine/dat2/task_dat2_hitsplat_load.c` fills it at boot from group 32
(whole-group and eager, like the varbit loader, because the consumer reads it
inside the per-frame overlay build where there is nowhere to yield). The overlay
prefers the config table and falls back to the archive, so the dat1 path is
untouched.

**The second half, which is the part that bites.**
`UITreeSceneBridge_EnsureSprite` binds a sprite that is *already resident*; it
does not load one. Knowing the id was not enough — nothing had ever asked for
sprite 2270, so it was not in memory and `EnsureSprite` still returned −1. The
load task therefore walks its own table afterwards and requests every sprite it
named. This is the same shape as the texture-wants registry: **an event-driven
loader only ever holds what something explicitly requested.**

Verify: `TORIRS_OVERLAY_DEBUG=1` prints the primitives per frame. A `kind=1`
(sprite) item beside the two `kind=2` (text) items is the fix working.

---

## 2. The skills tab: three opcodes with a signature and no handler

**How it works.** The tab (interface 320) is entirely CS2. Its scripts read
three host opcodes per skill:

```
3305 STAT       the boosted level  — what you can do right now
3306 STAT_BASE  the level the experience buys
3307 STAT_XP    the experience
```

and register an **`onStatTransmit`** listener so the panel repaints when a skill
changes. The client's own numbers come from `UPDATE_STAT`, which
`RS_GameProtoExec` writes into `RS_PlayerStats`.

**The gap, in two halves.**

- All three opcodes were marked `CS2_HANDLER_HOST` in the meta table and had a
  *known stack signature* — but no implementation anywhere. They fell through to
  `CS2VM2_Op_StackMetaStub`, which popped the skill id and pushed 0. The tab
  built perfectly and drew `0/0` for all 23 skills, `Total level: 0`. Because the
  signature was known there was no diagnostic: `TORIRS_CS2_SURVEY=1` reports
  *unknown* opcodes, and these were not unknown, just unimplemented.
- `CC_SETONSTATTRANSMIT` / `IF_SETONSTATTRANSMIT` were parsed and discarded, so
  even a correct value would only have been read once, at build time.

**The fix.** `CS2_OP_STAT`/`_BASE`/`_XP` pop the skill and issue
`CS2VM_HOST_REQUEST_STAT*`; `RS_CS2Host` answers from `RS_PlayerStats`
(`RS_CS2Host_SetStats` hands it over at init). A `stat_change_serial` /
`stat_transmit_dirty` pair mirrors the var and inv channels, bumped by
`RS_CS2Host_NotifyStatChanged` from the `UPDATE_STAT` handler.

That last part is the same bug as the inventory one: **a paint script that only
runs at build time paints the state the client started with.** Server-driven
values always arrive afterwards.

The health orb is fixed by the same change from the other end — see §2b.

### 2b. `UPDATE_STAT` carries two levels and only one has a consumer

The mock's `UPDATE_STAT` is `p1 stat, p1 base level, p4 xp, p1 boosted level`.
`RS_GameProtoExec` derives `base_level` from the xp itself and writes the
packet's level into `current_level` — so the **boosted** level is the field that
matters, and the base one on the wire is redundant. The parse override used to
take the base. The health orb reads `current_level[hitpoints]`, which is the
same number as the player's hitpoints, so it sat at full health all session.

---

## 3. NPC levels: the client did not know which entity was the player

**How it works.** `rs_minimenu_world.c` writes an npc row as
`name + combatColourCode(localPlayer, vislevel) + " (level-N)"`, and the colour
is the *difference* between the two levels. The reference gates the whole suffix
on there being a local player, and so does this:

```c
if( npc->combat_level > 0 && viewer_combat_level >= 0 )
```

`viewer_combat_level` comes from `World_PlayerGetByServerPid(world,
world->local_pid)`, and `local_pid` is set by exactly one thing: the
`UPDATE_PID` packet.

**The gap.** The mock never sent it. `local_pid` stayed −1, the lookup found no
player, and the suffix was suppressed for every npc — correctly, by a guard
doing its job. Other call sites paper over the same hole with a 2047 sentinel,
which is why nothing else looked broken.

**The fix (server).** `UPDATE_PID` is now in the rev-230 table (opcode 127,
assigned; lc254's `p2 index, p1 members` payload) and goes out at the top of the
login burst — before the stats and containers, because the first `PLAYER_INFO`
is what makes the value matter.

Now: `Attack Man (level-2)`, coloured by difference, and Hans — combat level 0 —
correctly still has no suffix.

---

## 4. Facing: `FACE_ENTITY` is a latch

**How it works.** The mask names an entity to turn toward, and the client keeps
the entity turned that way until told otherwise. There is no timeout and no
implicit clear. `65535` on the wire is "face nothing".

**The gap.** The mock set the mask when a fight started and never cleared it.
Every path that drops a target — the target dies, the player dies, the player
walks away, the player does something else — left the entity staring at where
the fight used to be.

**The fix (server).** `mock230_combat_stop_player` / `_stop_npc` do both halves
together: drop `combat_target` *and* raise `FACE_ENTITY` with −1. Every drop
site now goes through them, including the ones that are not about combat at all:

- `MOVE_*` — walking somewhere is a new interaction, which is what "clicking
  away" means
- `OPNPC` — re-established immediately if the op turns out to be Attack
- `OPOBJ`, `OPLOC` — picking something up, opening a door

Keeping the two halves in one function is the point: they were separable before,
and that is exactly how one of them got forgotten.

---

## 5. The combat tab — **partly open**

**How it works.** Interface 593 is two independent pieces:

- **Auto-retaliate** — a plain button whose label is driven by varp 172
  (`option_nodef`).
- **The weapon panel** — the weapon name (593:2), a subtitle (593:4) and four
  style buttons (593:5, 9, 13, 17). *All of them ship `hidden=1` with empty text
  and `graphic=-1`*, so a CS2 script has to unhide and fill every one. Which
  layout it builds depends on the equipped weapon's **category** — a field on
  the obj's cache record (bronze scimitar 21, abyssal whip 150) — looked up in
  the `combat_interface_weapon_category` dbtable (table 78). OpenRune generates
  a Kotlin row class for that same table, which is how its ids were confirmed.

**What was done.**

- *Server*: the four varps OpenRune's `AttackTab` writes now reach the client —
  `com_mode` (attack style), `option_nodef` (auto-retaliate), `sa_energy`
  (special energy, tenths of a percent), `sa_attack`. They are **content, not
  engine**: declared `transmit=yes` in
  `content/scripts/player/configs/player_controls.varp` and written by
  `[login,_]`, the way LostCity does it. `sa_energy` is what forced a
  `VARP_LARGE` encoder — `VARP_SMALL` carries a signed byte and a full bar is
  1000. Confirmed landing: the special-attack orb reads 100, and exactly four
  varp packets go out (three small, one wide) with the mock's own undeclared
  counters correctly absent.
- *Client*: `ToriRS_Objtype` had **no category field at all**; the adaptor
  dropped it on the floor. It is now carried through from the cache record,
  which is a precondition for any weapon-category lookup.

**What is still missing.** The weapon panel does not build. Established:

- the tab opens, lays out, and its auto-retaliate half works;
- the varps it reads are correct and arriving;
- the panel's components are cache-hidden, so *something* must run to show them.

Not established: which script that is, or why it does not fill them.
`TORIRS_DUMP_HOOKS_EXIT` reports zero runtime hooks for the *entire* tree —
including the inventory, which demonstrably works — so that dump is reading the
wrong state and is not usable as evidence either way. The next step is to
disassemble 593's `onLoad` with `3rd/rscache/tools/cs2 decompile` and read what
it asks for, rather than to guess again.

Worth knowing before that: the panel being empty is **not** the unarmed layout.
The reference's unarmed case still shows a weapon name and Punch/Kick/Block, and
this shows neither — so the script is failing earlier than the weapon lookup,
not falling through to a default. (Compare the dragon-claws note in
`exporter-owns-generated-configs`: a *missing category* produces the unarmed
interface, which is a different and more visible failure than this one.)

---

## Verifying

```
make -C src && make -C src mock230
./src/build/mock230 43595 &

# stats tab (sidebar icon 2)
SDL_VIDEODRIVER=dummy TORIRS_MAX_FRAMES=900 TORIRS_EXIT_BMP=/tmp/stats.bmp \
  TORIRS_SIM_CLICK_AT="600,534,187" \
  ./src/torirs --manifest manifest_osrs230.ini --user testc --pass test

# a fight, with the overlay primitives dumped
SDL_VIDEODRIVER=dummy TORIRS_MAX_FRAMES=2300 TORIRS_OVERLAY_DEBUG=1 \
  TORIRS_NET_CHEAT="tele 3263 3232" \
  ./src/torirs --manifest manifest_osrs230.ini --user testc --pass test

# a film strip through the fight, to catch a splat mid-flight
TORIRS_BMP_SERIES="/tmp/strip,2150,12,20"
```

`TORIRS_NET_CHEAT` runs `::` commands right after login; `::style <0-3>` and
`::setlevel <stat> <level>` move the combat inputs, and `::tele 3263 3232` puts
you next to the goblins, who are aggressive to a level-3 character and not to a
stronger one (OldSchool's own rule: aggression stops above twice the monster's
combat level).
