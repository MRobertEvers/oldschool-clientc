# The "Activities" settings, as builtin plugins

The **All Settings** panel (interface `134`, `settings`) has an **Activities**
category, and until now not one row in it did anything. The rows are drawn --
the panel is built entirely by clientscripts and those work -- and every toggle
writes its varbit. Nothing then reads it. Turning "Highlight hovered tile" on
moves a checkbox and changes nothing on the screen.

This document is the list of what has to be implemented and the shape it is
implemented in.

## Where the list comes from

Nothing here is invented. The category is `struct_3620` (`param_744 =
"Activities"`), whose `param_745` names **`enum_4024`** -- 86 entries, in the
order the panel lays them out. Each entry is a setting struct:

| param | meaning |
|-------|---------|
| `param_1077` | the **setting id**, which every hub switches on |
| `param_1078` | row kind: 0 toggle, 2 dropdown, 4 slider, 5 section header, 6 button, 7 spacer, 9 colour |
| `param_1086` / `param_1087` | title (desktop / mobile) |
| `param_1096` / `param_1097` | description (desktop / mobile) |
| `param_1084` | **display INVERSION, not a default** — see below |
| `param_1230` | default colour |
| `param_1091` | the choice enum, for a dropdown |
| `param_1080` / `param_1081` | the setting this row depends on |
| `param_740` | mobile-only row -- **skipped below** |

The var behind each row was read out of the cache's own hubs rather than
guessed:

- **toggles** -- read `proc script6716(id)`, written `clientscript script3965(id)`
- **dropdowns** -- read `proc script3962(id)`, written `clientscript script3967(id, v, v2)`
- **sliders** -- read `proc script3964(id)`
- **colours** -- read `proc script4181(id)`, which returns `%varN - 1`
- **buttons** -- `clientscript script3969(id)`, whose switch has no case for
  either of ours; the only thing it does for them is `%varbit9657 = id`

Every one of those helpers opens with `if (~script100 = 0) return`, and
`~script100` is true here: `~script1445` returns 1 for `clienttype` 4, 5 **and
10**, and this client reports 10. So the toggles do reach their varbits. The
gap is entirely on the reading side.

To re-derive any of it:

```sh
3rd/rscache/tools/cs2/cs2 decompile --cache cache.osrs239 --rev osrs239 --out /tmp/cs2 6716 3965 3962 4181
```

## Status: 37 of 74

| | rows | |
|---|---:|---|
| **working** | 37 | 24 driven by the cache, 4 by builtins, 7 by the server, 2 by the client |
| **partial** | 4 | 164, and 173 / 176 / 179 |
| **not started** | 33 | classified below -- 7 + 14 + 12, and only 12 of those are open problems |

The count moved by one. What moved underneath it is larger: **five rows that a
builtin was faking are now driven by the cache's own scripts**, two builtins
are deleted, and the two subsystems that were blocking the whole of bucket A
are implemented.
See "Client triggers" and "Scripted entity overlays" below.

### Working

| id | setting | by |
|---:|---------|----|
| 112 | Tile highlighting | cache |
| 113 | Tile highlight colour | cache + client (picker) |
| 117 | Clear your highlighted tiles | client (button) |
| 172 | Highlight hovered tile | cache |
| 174 | Highlight hovered tile - Colour | cache |
| 175 | Highlight current tile | cache |
| 177 | Highlight current tile - Colour | cache |
| 178 | Highlight destination tile | cache |
| 180 | Highlight destination tile - Colour | cache |
| 189 | Bird nest notification | `nxt-bird-nest` |
| 190 | Highlight entities on mouse-over | cache |
| 248 | Cannon low on ammo notification | `nxt-cannon-ammo` |
| 249 | Cannon low on ammo amount | `nxt-cannon-ammo` |
| 250 | Cannon out of ammo notification | `nxt-cannon-ammo` |
| 165 | Highlight Agility shortcuts | cache |
| 210 | Highlight Agility shortcuts - Shortcut Requirements | cache |
| 258 | NPC highlight - Display name | cache |
| 259 | NPC highlight - Highlight tile | cache |
| 260 | NPC highlight - Highlight outline | cache |
| 261 | NPC highlight | cache |
| 262 | NPC highlight - Highlighting colour | cache |
| 263 | NPC highlight - Text colour | cache |
| 264 | Display all NPC names above their body | cache |
| 266 | NPC names text colour | cache |
| 267 | Clear your highlighted NPCs | client (button) |
| 269 | Blast Furnace highlights | cache |
| 367 | Highlight quest start points | cache |
| 368 | Filter quest start highlights based on requirements | cache |
| 374 | Beginner clue scroll warning | server (trail lane) |
| 375 | Easy clue scroll warning | server (trail lane) |
| 376 | Medium clue scroll warning | server (trail lane) |
| 377 | Hard clue scroll warning | server (trail lane) |
| 378 | Elite clue scroll warning | server (trail lane) |
| 379 | Master clue scroll warning | server (trail lane) |
| 416 | NPC highlight - Tagging | cache |
| 451 | STASH units take equipped items | server (trail lane) |
| 453 | Highlight poll booths | cache -- gated on an active poll, which this server never declares |

Every **colour** row above -- 113, 174, 177, 180, 262, 263, 266 -- is "cache"
only for the READ. The cache has no apply for one; the picker that writes it is
this client's, and is the same one for all of them. See "The third half".

### Partial

| id | setting | what is missing |
|---:|---------|-----------------|
| 164 | Highlight Agility obstacles | the objtype half populates; the obstacles need a per-course script |
| 173 | Highlight hovered tile - Always on top | flag reaches the renderer; needs a depth-tested ground primitive |
| 176 | Highlight current tile - Always on top | flag reaches the renderer; needs a depth-tested ground primitive |
| 179 | Highlight destination tile - Always on top | flag reaches the renderer; needs a depth-tested ground primitive |

### Not started, by what each needs

Derived by grepping every clientscript that reads each row's var and
looking at what it CALLS -- measured, not guessed.

**A. reachable now, not yet proven on screen** -- 7 rows

The two things that blocked these are implemented (see below): the cache's
scripts for them are found and run, and the overlays they build are drawn. What
is left per row is a scene that exercises it -- a fishing spot, a cannon, a clue
in hand -- which the mock server does not currently place.

| id | setting | what it needs to be seen |
|---:|---------|--------------------------|
| 120 | Fishing spot indicators | a fishing spot npc in the scene |
| 121 | Fishing spot indicators - Tools only | ditto, plus the tool in the inventory |
| 122 | Fishing spot indicators - Mouse over tooltip | ditto, plus a hover |
| 247 | Cannon hud | a placed cannon (`%var3551`) |
| 270 | Clue scroll helper - Overlay | a read clue (`%var3546`) |
| 271 | Clue scroll helper | ditto |
| 277 | Clue scroll helper - Entity highlights | ditto |

**B. the cache implements it; it needs the right context** -- 14 rows

Reclassified by measurement rather than by eye. For every row, the scripts that
READ its var were separated from the one that WRITES it and from the settings
hubs (`6716`, `3962`, `3964`, `4181`, `3965`, `3967`). A row with a real reader
is one the cache already implements; what it is missing is a situation.

| id | setting | its readers | what it is waiting for |
|---:|---------|-------------|------------------------|
| 81 | Last Man Standing fog colour | 1346 | being in LMS |
| 111 | Show normal health overlay | 4731 | interface 303, which only the server opens |
| 116 | Data orbs - Regeneration indicators | 4718 4723 6052 6060 | the orbs' own layout pass |
| 118 | Chambers of Xeric helper | 4663 | being in CoX |
| 187 | Ore respawn timer | 5482 | a server `RUNCLIENTSCRIPT` 5475 |
| 188 | Woodcutting respawn timer | 5483 | the same |
| 242 | Tears of Guthix timers | 6679 | the same |
| 243 | Hunter trap timers | 6680 | the same |
| 245 | Herbiboar helper | 4743 6853 | being at a herbiboar |
| 274 | Clue scroll helper - Menu highlights | 6477 6479 | a clue in hand |
| 276 | Clue scroll helper - Clue text | 6634..6644 | a clue in hand |
| 299 | Show enemy name on health overlay | 2103 | interface 303 |
| 300 | Compact boss health overlay | 2101 | interface 303 |
| 301 | Health overlay display type | 2103 | interface 303 |

Three shapes, and none of them is a missing opcode:

- **An interface only the server opens.** 111 / 299 / 300 / 301 all live on
  interface 303 (`hpbar_hud`) and read `%hpbar_hud_npc`, the npc being fought.
  Nothing in the cache opens 303; in the reference the server does, with
  `IF_OPENSUB`.

  **The server half is written** (`src/torirsserver/torirs_server_hpbar.c`):
  once per tick, after the swing, the player's combat target becomes
  `%hpbar_hud_npc` / `%hpbar_hud_hp` / `%hpbar_hud_basehp` and the interface is
  opened into the gameframe's floater slot; a linger keeps it up across the gap
  between one npc dying and the next click, and switching setting 111 off
  closes it on the next tick. Every one of those names is the cache's own --
  including `hpbar_hud_standard_disabled`, which is the row's inversion
  *stated* rather than inferred.

  Verified on the wire with `TORIRS_HPBAR_DEBUG=1` and the `::fightcave`
  harness:

  ```
  hpbar: open iface 303 into 548:43 (live gameframe 548) for npc type 3116 (10/10)
  if-opensub: mount iface=303 under uid=0x0224001c (548<<16|28) type=1
  ```

  `hpbar_hud_boss` stays 0 deliberately. Which npcs are "certain bosses" is a
  per-encounter decision the reference leaves to the boss's own content, and
  inventing a list would light the wide bar for the wrong monsters.

  **The ordering is the whole trick, and the obvious order is the wrong one.**
  Clientscript 2099 is 303's onload and it paints nothing -- all it does is

  ```
  if_setonsubchange("script2100(...)", $component0);
  if_setonvartransmit("script2102(...){var1682, var1683}", $component6);
  ```

  so the first paint comes from a var TRANSMIT, not from the load. Writing the
  varps before the `IF_OPENSUB` -- which reads as the careful order, "data
  ready before the panel that shows it" -- is a change with nobody listening,
  and the panel stays blank until the npc's hitpoints happen to move. It opens
  first and feeds on the following tick, and that first feed is FORCED
  (`ToriRSServer_WorldMarkVarp`), because the varp writers dedupe on the
  current value and re-opening onto the same npc at the same hitpoints would
  otherwise send nothing at all.

  What is NOT closed: I have the open on the wire and the mount in the client
  (`if-opensub: mount iface=303 under uid=0x0224001c`), but no screenshot of
  the panel drawing. The `::fightcave` harness is slow and flaky -- the wave
  restarts before an npc spawns more often than not -- so the ordering fix
  above is reasoned from clientscript 2099 rather than observed.
- **A server-driven timer.** 187 / 188 / 242 / 243 all end at
  `[proc,script5482]`-shaped gates, and the overlay itself is 5471 (npc),
  5475 (loc) or 5478 (coord) -- "run a countdown on this thing for N ticks",
  which nothing in the cache calls because the SERVER calls it by
  `RUNCLIENTSCRIPT`. Four rows, one server feature.
- **A place or an item.** 81, 118, 245, 274, 276 are unverifiable rather than
  unimplemented: a fresh Lumbridge login does not reach LMS, CoX, a herbiboar
  or a clue scroll.

**C. no reader anywhere -- the client or the server owns it whole** -- 12 rows

| id | setting |
|---:|---------|
| 5 | Hitsplat tinting |
| 10 | Show boss health overlay |
| 163 | Agility helper |
| 182 | Iron loot restriction indicator |
| 183 | Iron loot restriction messages |
| 184 | Slayer helper |
| 268 | Blast Furnace helper |
| 272 | Clue scroll helper - Worldmap marker |
| 273 | Clue scroll helper - World arrows |
| 275 | Clue scroll helper - Infobox |
| 279 | Max hit hitsplats |
| 280 | Max hit hitsplats threshold |

These are the ones a builtin would have to implement from nothing, and most of
them need a fact this client is not told. 5 wants "whose damage was that", 279
wants "was that my maximum", 182/183 want the server's loot-ownership rule,
and 163 / 184 / 268 want per-course, per-task and per-minigame tables that are
game knowledge rather than cache data. Writing those from guesswork is how a
helper becomes confidently wrong, so each is named here rather than filled in.

## The reference client settles it

`~/Documents/git_repos/osclient_decompile/osclient-216-mac.c` is a Ghidra
decompilation of the OldSchool **C++** client -- the NXT engine, namespace
`jag::oldscape` -- with mangled symbol names intact. It still carries the
original source paths in its assert and log strings, so the opcode handlers are
findable by name:

```
projects/osclient/ScriptRunnerImpl_7200To7299.cpp
projects/osclient/highlighting/HighlightManager.cpp
```

The opcode switch is a plain `switch` on the opcode number, so a handler is one
grep away -- 7035 is `case 0x1b7b:`, 7200 is `case 0x1c20:`.

Everything below was guessed here first and is now read off that. Two of the
guesses were wrong, and both were wrong in a way nothing on screen would have
called out.

### HIGHLIGHT_*_SETUP, exactly

`HighlightManager::ConfigureChannel(category, channel, colour, thickness,
opacity, flags)`:

| CS2 arg | what it really is |
|---------|-------------------|
| group | channel, asserted `0..31` -- *"Highlight channel ID must be between 0 and %d (inclusive)"* |
| colour | RGB, run through `RunetekColour::RGBToHSL` on the way in |
| **style** | **outline THICKNESS in pixels** (0, 1 or 2) -- not a style enum |
| **opacity** | **fill alpha 0..255**, `jag::math::Clamp(v, 0, 255)` -- not a percent |
| flags | `EntityHighlightBitFlags` |

**`IsEnabled` is four predicates, not one:**

```
HasModelOutline = (flags & 1) && thickness != 0
HasTileOutline  = (flags & 2) && thickness != 0
HasModelFill    = (flags & 4) && opacity   != 0
HasTileFill     = (flags & 8) && opacity   != 0
IsEnabled       = any of the four
```

That is what makes the cache's two odd-looking families work, and testing
either field alone silently drops one of them: the six mouseover groups run at
**opacity 0** (an outline has no wash to be opaque) and clientscript 5198's
hovered tile runs at **thickness 0** (a wash with no border). This client had
guessed "any flag, and a colour", which called a group live on the strength of
bits 16 and 64 alone -- flags that say HOW to draw, not WHAT.

The opacity error was the visible one: treating 0..255 as a percent and scaling
by 255/100 made every wash in the game 2.55x too opaque.

### The membership ops

`highlight_tile_on(coord, group, flags)` -- the last argument is **not** flags.
It is a BOOLEAN, and the handler passes it as `param_4` to
`AddTileHighlight(coord, channel, bool, bool)`, where true means *convert this
world coord to a SOURCE coord first* (`Client::WorldCoordToSourceCoord`). The
tile markers pass 1 and the hovered tile passes 0, which is why a marker
survives an instance and a mouseover mark does not need to.

The `group` argument really is an INDEX, not a mask, even though the manager
stores masks: `AddNPCTypeHighlight` does `*slot = 1 << (channel & 0x1f)` after
asserting `channel < 0x20`.

## Client triggers -- the scripts nothing calls

The single largest thing this client was missing, and it is not an opcode.

Most of what a cache script does, this client already ran: panels open, hooks
fire, buttons work. What it could not reach at all was the family of scripts
that **nobody calls** -- the ones the client is supposed to find and run itself
when an npc walks on screen, when the scene builder places a loc, when the
right-click menu opens. In `cache.osrs239` that is **218 loc scripts and 23 npc
scripts**, and they are where the whole non-highlight half of this category
lives: the fishing spot indicators, the Agility shortcut markers, the cannon
hud, the clue scroll helper, the npc name plates.

The binding is not in a table. It is in the clientscript index's **group
names**:

```
hash       = subject * 256 + trigger          -- this npc type / this loc type
           = trigger - category * 256 - 0x300 -- any subject of this category
           = trigger - 0x200                  -- any subject at all
group name = the decimal string of that hash
name hash  = djb2 of that string
```

and the client walks the three narrowest-first (reference
`ClientScript::Get(ClientTriggerType2::ID, int, int)` ->
`ScriptTriggerHelpers::GetScriptHash` -> `FindTriggeredScript` ->
`HashToJs5GroupString`). See `src/game/rs_client_trigger.h`.

Verified against the cache before a line was written, by dumping the
identifiers out of index 12's reference table and solving the three forms
against them:

| form | example | resolves to |
|------|---------|-------------|
| subject, trigger 37 (loc placed) | 218 loc types | scripts 5110..5720 -- the Agility shortcut handlers |
| category, trigger 35 (npc appeared) | 17 npc categories | scripts 4528..4546 -- the fishing spot handlers |
| global, trigger 35 | -- | script 6693 -- the npc name plate |

The trigger ids come from the reference's own call sites: 35 npc appeared
(`Client::GetNPCPosNewVis`), 36 npc gone (`DestroyNpc`), 37 loc placed
(`OnLoadLocation`), 38 loc changed (`LocChangeUnchecked`), 41/42 player, 82
minimenu opened (`Minimenu::Open`).

**Why this is worth a test rather than a screenshot.** The failure mode is
silence: a formula that hashes the integer instead of its decimal string, or
uses 33 instead of 31, or gets the category bias wrong, answers -1 for every
trigger in the cache -- which is exactly what "not implemented" looked like for
as long as it was. `make -C src test-client-trigger` pins seven real
(hash, group) pairs read straight out of the cache.

### `_7200..7214` is `jag::oldscape::EntityOverlays` -- implemented

The bucket-A family, and it is not a mystery any more:

| op | reference call | anchor |
|----|----------------|--------|
| 7200 | `CreateOverlay()` + `ClientEntity::SetScriptedOverlay(slot, idx)` | the ACTIVE NPC |
| 7201 | `CreateStaticOverlay(slot, coord, LocLayer)` | the ACTIVE LOC |
| 7204 | `CreateStaticOverlay(slot, coord, type=4)` | a COORD argument |
| 7205 | `ClientEntity::GetScriptedOverlayIndex(slot)` | the ACTIVE NPC |
| 7209 | `GetStaticOverlayIndex(slot, coord, type)` | a COORD argument |

The common argument shape is `(slot, X, width, height, Y)`, with the coord
prepended for the 7204/7209 forms. `EntityOverlays::GetLayer(index)` hands back
a `shared_ptr<IfType>` -- **an ordinary interface component** -- and the handler
writes width to `+0x38`, height to `+0x3c` and X to `+0x70` before the script
decorates it with the usual `cc_*` ops. `StaticEntityOverlayType` is bounded by
`IsTypeValid(t) { return t < 5; }`, and `OverlayTypeFromLocLayer` is the
identity, so 0..3 are the four loc layers and 4 is "a bare coord".

`ScriptRunner::SetActiveNPC / SetActiveLoc / SetActiveObj / SetActiveTile /
SetActivePlayer` is the backing state for the `_67xx / _68xx / _69xx` getters,
which confirms the shape this client already implements: one "active subject",
set by a client-op dispatch and by the mouseover.

The `+0x70` field is `IfType::OverlayTypes` -- the BAND, which decides both
where the overlay sits and when it draws relative to the health bar. The scene
pass runs three sweeps against a three-point anchor
(`Client::GetAllOverlayPositions`):

| band | position | draw order |
|-----:|----------|------------|
| 0 | centred on the entity's mid-height point | under the bar |
| 1 | stacked UPWARD from the head | over the bar |
| 2 | stacked DOWNWARD from the feet | under the bar |

Only bands 1 and 2 advance their cursor, which is what lets two overlays in one
band stack instead of overprinting.

**How it is built here.** An overlay is a UITree LAYER parented to the
`entity_overlay` builtin plus a record saying what it hangs off
(`src/game/rs_entity_overlay.h`); `202`/`103`/`104` turn an overlay index into
that layer's component id and then do exactly what the panel-facing op of the
same name does, so `cc_setobject_nonum`, `cc_settext`, `cc_setonvartransmit`
and the rest need no special case. The App projects the anchor and moves the
layer each frame, because where it belongs is a fact about the camera.

Four things went wrong building it, all of them silent, and all four are now
pinned by tests:

- **The npc uid read -1.** `server_slot` is written by the caller *after* the
  spawn helper returns, so the trigger fired too early and every npc's overlay
  keyed on the same absent subject. The trigger moved to the entity-sync path.
- **The subject was written beside the queue call, not into it.**
  `RS_CS2_RunScript` queues; twenty-six npc-add scripts all ran during one
  settle and all saw npc twenty-six. The subject now rides its own one-shot
  task queued in front of the script.
- **Off camera was treated as gone.** An npc that walked behind the camera does
  not project, and reaping on that churned the whole table back to index 0
  every frame. "The subject is gone" and "it does not project" are two answers
  now.
- **A tree rebuild killed every layer, permanently.** Login's gameframe remount
  reclaims everything under the builtin -- including the `cc_setonvartransmit`
  hooks that are the only thing that would ever rebuild an overlay. Nothing in
  the cache re-fires a client trigger, so the client notices a layer that no
  longer resolves and re-raises them.

### Arity errors in this repo's own table

Three, all of the same kind -- a row inherited from the decompiler's vendored
signatures, which is exactly what the table's `known` field flags as unverified:

- **7205 / 7208** were `0 in, 1 out`. Both pop the slot. A GET that did not pop
  answered about whatever the script left below it.
- **202** was guessed as `CC_FINDROOT`, which pushes without popping. It is
  `OVERLAY_FIND`, and **57 call sites in this cache pass it one argument** -- so
  the guess also leaked an int per call. `203` was guessed as
  `CC_CHILDREN_FIND`; it is the overlay's `cc_find`, and nothing in either
  osrs239 or osrs230 calls it.

## Most of it is implemented in the cache, not here

Found after the four plugins below were written, and it changes the shape of
the rest.

The cache does not merely *store* these settings. It **acts** on them, through
the `HIGHLIGHT_*` opcode family (7000..7044) -- 125 clientscripts call one.
Those scripts read the varbit, read the colour row, and hand the client a
highlight GROUP: a colour, a style, an opacity, a flag word, and a membership
list of tiles / npcs / locs / objs / players.

```
[clientscript,script5198]                  // "Highlight hovered tile"
def_int $int2 = ~script5329(174);          //   ... its colour row
if (%varbit12977 = 1) {                    //   ... its own varbit
    if (%varbit12980 = 1) { $int3 = calc($int3 + 16); }   // "always on top"
    _7035(5, $int2, $int0, $int1, $int3);  // HIGHLIGHT_TILE_SETUP(group 5, ...)
} else {
    _7039(5);                              // HIGHLIGHT_TILE_CLEAR(5)
    _7035(5, -1, 0, 0, 0);
}
```

`CS2VM2_Op_Highlight` pops the arguments for all 45 of them and does nothing
with any of them. That is the real reason the category is dead: not that the
varbits are unread, but that the client throws away what the cache tells it.

**24 of the 74 rows name a `HIGHLIGHT_*` script**, and the seven colour rows
feed those same scripts through `~script5329(<setting id>)`. That is where the
Agility obstacles, quest start points, fishing spots, Blast Furnace and clue
scroll helper live -- the cache knows which locs and npcs those are, and this
client has no table for any of them.

Naming a script is not the same as that script doing anything, though: see
"What still does not populate" for what actually fires.

### What landed for it

`CS2VM2_Op_Highlight` already popped every argument correctly; only the host
end was missing. It now has one:

- **`src/game/rs_highlight.{h,c}`** -- the groups, as state. Eight kinds
  (npc / npctype / loc / loctype / obj / objtype / player / tile), each with
  its own 32 groups, each group a (colour, style, opacity, flags) and a
  membership list. It records what the scripts said and answers `GET`
  truthfully; it decides nothing about appearance and draws nothing.
- **`RS_CS2Host::highlight`** -- where it lives, because it is written from
  inside a running script.
- **`api->highlight_next`** -- the groups RESOLVED against live world state,
  one item per thing to draw. The resolution walks the npc, loc and
  ground-item pools, which is the engine's job and not a plugin's.
- **`nxt-highlight`** -- a hidden builtin, ~40 lines, that turns each item's
  flags into draw calls. It has no settings and no opinions: every appearance
  decision was made by a clientscript that read the user's own setting and
  colour, and the moment this plugin starts choosing a colour, All Settings has
  stopped being where that question is answered.

`TORIRS_HIGHLIGHT_DEBUG=1` prints every call with its arguments. When a
highlight does not appear the first question is always "did the script ask for
it", and from outside the VM there is no other way to answer it.

Measured against the real panel rather than reasoned about. Driving a live
headless client to All Settings > Activities and clicking "Highlight hovered
tile":

```sh
SDL_VIDEODRIVER=dummy TORIRS_HIGHLIGHT_DEBUG=1 \
  TORIRS_SIM_CLICK_AT="700,683,482;740,641,448;790,473,317" \
  TORIRS_MAX_FRAMES=860 ./run-live.sh manifests/manifest_osrs239.ini testc test
```

```
highlight: op 7035 (tile) 6 65280 2 50 90        <- on open: the tile MARKERS
highlight: op 7035 (tile) 5 12499566 0 70 10     <- the click: hovered tile
```

`65280` is `#00FF00`, setting 113's default; flags 90 is 2+8+16+64, the marker
group with its minimap bit. `12499566` is `#BEBA6E`, setting 174's default;
flags 10 is 2+8, tile outline and fill. Every constant in the table above, and
every argument slot in `rs_highlight.c`, is confirmed by those two lines.

### The other half: CLIENTOP_* (6700..6709)

Setting up a highlight group is not the same as putting anything in one. The
groups were being set up all along, off their varbits; the scripts that name
SUBJECTS get them from the client-op context, and that was a stub too:

```
[clientscript,script4762]                  // "Mark tile"
if (_7038(_6950, 6, 1) = true) {           //   _6950 = the clicked tile's coord
    highlight_tile_off(_6950, 6, 1);
} else {
    highlight_tile_on(_6950, 6, 1);
}
```

A cache script installs a right-click row with a slot, a label and a script —
`_6708(1, "Mark tile", 4762)`, `_6700(1, "Tag", 6688)` — and removes it with
the DEL form when the setting behind it is switched off. That has landed too:

- **`src/game/rs_clientop.{h,c}`** — the registry (five kinds × eight slots)
  and the dispatch context.
- **`CS2VM2_Op_ClientOpContext`** — the thirteen context getters, routed
  instead of falling to the stack stub.
- **`app_clientop_menu_build` / `app_clientop_run`** in app.c — the rows go on
  the minimenu for their subject kind, and picking one runs its script.

Two things are worth knowing about the dispatch:

**Shift is the client's gate, and the cache asked for it.** Nothing in the
installing scripts tests a key; setting 112's own description does —
"hold shift and right-click the ground to place highlights". Without the gate
every right click on open ground carries a "Mark tile" row above "Walk here".

**The context is scoped by script identity, not by a bracket.**
`RS_CS2_RunScript` queues a task, so the script runs during the frame's settle,
well below the click — a begin/run/clear around the call clears before the
script ever starts, and every context op reads -1. (It did, first time.) So the
context records which script it belongs to and only a root frame of that id may
read it. That is exact here: a client op's script is named by nothing else in
the cache.

`TORIRS_CLIENTOP_DEBUG=1` prints every install and every dispatch.

### The third half: a COLOUR row has no apply anywhere in the cache

Reading a colour row was always the easy half -- `~script5329(<setting id>)`
falls through to `settings_get_colour` (script_4181), a switch from setting id
to `calc(%var<n> - 1)`. WRITING one is not in the cache at all. The op the row
hangs off its swatch is two lines long:

```
[clientscript,settings_colour_input_click](int $int0, int $int1)
if (~settings_op_checker($int0, $int1) = 0) {
    return;
}
```

`~settings_op_checker` plays the panel's click sound and, for a row the player
may not change, prints that row's own refusal message. There is no third
statement. Nothing else in `cache.osrs239` writes `%var3108` -- or any of the
other 48 colour varps -- and interface 288 `colour_pallet` is an empty shell no
clientscript names. In the reference the picker is the ENGINE's, opened from
this op and writing the varp itself.

So "Tile highlight colour" showed the green default swatch, described what it
was for, and did nothing whatever when clicked. Same for every other colour row
on the page. What landed:

- **`RS_CS2Host_ScriptStarted`** (rs_cs2_host.c), called from
  `Task_CS2Run` with the script's arguments already in its locals and no
  opcode run yet. It is the only seam this client has on a script's
  ARGUMENTS, and arguments are the whole of what this one says: which row was
  clicked, and whether the row is enabled. It claims one script id and ignores
  every other.
- **The setting -> varp map, LEARNED rather than tabulated.** A varp read
  performed inside a frame of `settings_get_colour` names that setting's varp,
  and the row builder (clientscript 4182) calls the hub twice while laying the
  row out -- so every row on screen has answered the question before its swatch
  exists to be clicked. Nothing here carries a fifty-line table to keep in step
  with the cache by hand.
- **`app_settings_colour_tick`** in app.c: the picker itself, a
  `TORIRS_CHROME_W_COLORPICK` in the in-canvas chrome, opened beside the swatch
  that asked for it.

Committing to the varp is the whole apply -- the cache does everything after
that, exactly as it always did. Writing `%var3108` fires the var-transmit hooks
the row installed itself, so `settings_colour_input_update` re-fills the swatch
and clientscript 4763 re-runs `_7035(6, <colour>, 2, 50, 90)`. Nothing in the
client knows that the row it just wrote is about tile markers.

Two details are worth keeping:

**`colour + 1`.** The varp holds the colour plus one so that zero can mean
"never chosen", which is what makes the panel fall back to `param_1230`.
Storing the colour raw makes black indistinguishable from unset, and every
value one shade wrong.

**Default is committed verbatim; a pick is quantised.** The chrome's picker
works on the HSL16 axes the renderer draws in, so a picked colour is one of the
palette's 32768 entries -- deliberately, and visibly, since the hex field shows
the entry rather than what was asked for. `param_1230` is not: it is a colour
the cache authored, and "Default" that restored an approximation of it would
never quite get back to where the row started.

### The initialiser is the SERVER's, and it was not being sent

With all of that in place the category was still inert, and the reason was not
in the client at all. Clientscript **4743** is the cache's own initialiser for
the whole layer — it installs the client-op rows and sets up every highlight
group from its varbit. Nothing in the cache calls it. Its two entry points are
clientscript 876, which the reference server runs at login, and 5487, which
takes no arguments; this server sent neither, so a login ran no RUNCLIENTSCRIPT
at all.

`ToriRSServer` now sends **5487** at login, beside the camera-limit script 605
it already sent for the same kind of reason. 5487 rather than 876 because
5488's body is a strict subset of 876's, while 876 additionally wants the login
message's four arguments — a welcome line and a last-login stamp — that this
server does not have. Sending 876 with invented arguments would arm the same
layer and print a wrong welcome message beside it.

### End to end

```sh
SDL_VIDEODRIVER=dummy TORIRS_SIM_KEYHOLD=42 \
  TORIRS_SIM_CLICK_AT="700,250,250,1;720,232,281" \
  TORIRS_MAX_FRAMES=800 TORIRS_EXIT_BMP=/tmp/marked.bmp \
  ./run-live.sh manifests/manifest_osrs239.ini testc test
```

```
clientop: op 6708 set slot 1 script 4762 'Mark tile'      <- login: installed
clientop: tile slot 1 'Mark tile' -> script 4762 (coord=53054608)
highlight: op 7038 (tile) 53054608 6 1                    <- the script's GET
highlight: op 7036 (tile) 53054608 6 1                    <- ... and its ON
```

and the shot has a green tile on the ground, in setting 113's own colour, with
the wash group 6's flags call for. Shift + right-click offers "Mark tile";
picking it marks the tile; picking it again unmarks it.

`TORIRS_SIM_KEYHOLD="<keycode>[,...]"` is new, and exists for exactly this: the
click sims had no way to say "with shift down", and shift is not a decoration
on a right click — it is what makes a whole class of rows appear.

### What the client had to supply

The opcode layer alone drew almost nothing, because a group being SET UP is not
the same as anything being IN it. Five things were missing, and all five have
landed:

- **`CLIENTOP_*` (6700..6709)** and its thirteen context getters -- see above.
  Without them "Mark tile" and "Tag" did not exist and nothing could be put in
  a group by clicking.
- **`MINIMENU_*` (7100..7110)** -- clientscript 5350 is the cache's own
  "Highlight entities on mouse-over": it asks `_7100` what kind of thing the
  pointer is on, confirms with the matching FIND op, and highlights it. While
  TYPE answered 0 the script returned on its first branch. The App publishes a
  per-frame mouseover snapshot into `host->clientop`, and the same snapshot is
  what the `_67xx / _68xx / _69xx` getters fall back to outside a client op --
  those ops are the CURRENT TARGET, not only a client op's subject.
- **`NC_PARAM` (6513) and `LC_PARAM` (6514)** -- 5350 picks which highlight
  group a subject belongs in by reading `param_2312` off its type. Neither
  opcode existed, and neither could: the loc and npc converters dropped the
  param table on the reasoning that "the client is not a script host". It is
  one. Both types now carry the whole table, as objtypes always did.
- **`_3330`** -- the walk destination, `coord`'s sibling. Unrouted it answered
  0, which is a real tile, so clientscript 5210's `if (_3330 ! null)` was always
  true and the destination-tile highlight marked the corner of the map.
- **The three tile refreshers.** 5197 / 5204 / 5210 mark the hovered, current
  and destination tile. Each takes no arguments and reads its subject from an
  opcode (`_6950`, `coord`, `_3330`), and nothing in the cache calls them --
  the reference client re-runs each one when its subject changes, and so does
  this one now, on the edge.

The mouseover highlighter is deliberately NOT driven that way: the cache
re-arms clientscript 4726 on a gameframe component timer and calls 5350 itself.
Measured -- with the client's own edge-triggered call removed, 5350 still ran
89 times over the same window. A second driver for an idempotent script is only
waste.

### What is still not populated, and why

Two reasons now, both measured rather than assumed. There used to be a third,
and it was the biggest:

- ~~**A per-instance hook this client does not raise.** Clientscript 8320 puts
  a poll booth in its group and is called by the reference when a loc comes
  into view; nothing in the cache calls it.~~ **Fixed.** It is not a hook this
  repo had to invent -- it is the cache's own client trigger 37, and 8320 is
  bound to it by group name. It runs now, and so do 240 others, and
  `nxt-poll-booths` is deleted -- see the plugin roster.
- **In-game context.** The Agility obstacle list fills from a per-course
  script, the clue-helper groups from holding a clue. Those are unverified
  rather than broken -- a fresh Lumbridge login does not reach them. What can
  be reached from one was checked: turning setting 367 on adds quest-start npcs
  to npctype group 13, and setting 164 adds its two ground-item types to
  objtype group 9.
- **A server fact.** 453 is gated on `%varbit4337` ("there is an active poll"),
  which this server never writes.

## The shape: hidden builtin plugins

Each feature is a **C plugin**, statically registered in
`src/plugin/torirs_plugin_registry.c`, and marked `.hidden = true` so the
Plugin settings roster does not list it.

The reason for `hidden` rather than just "don't register it in `plugins.ini`":
these are not opt-in extras with a switch of their own. Their switch is the
cache's, in All Settings, where the user already expects to find it. A second
switch in the plugin roster would be a second source of truth for the same
feature, and the two would disagree the first time someone used either one.
So the plugin is *always enabled* and reads the varbit itself; the roster row
would only ever be a way to break it.

`hidden` also keeps the roster honest about what it is: a list of things
*added* to the client. A builtin has no business in it.

### What that costs in API

`ToriRS_PluginDef` grows one flag, and the api table grows the reads a builtin
needs to see its own switch:

```c
struct ToriRS_PluginDef {
    ...
    /** Not listed in the Plugin settings roster. For a builtin whose switch
     *  lives somewhere else -- the cache's own All Settings panel. */
    bool hidden;
};

struct ToriRS_PluginApi {
    ...
    /** The client's live varbit / varp value. READ ONLY: a varp is the
     *  server's, and a plugin that wrote one would be telling the client
     *  something the server never said. */
    int (*varbit)(struct ToriRS_PluginCtx* ctx, int varbit_id);
    int (*varp)(struct ToriRS_PluginCtx* ctx, int varp_id);
    /** A colour row's value, as 0xRRGGBB, or `fallback` when unset. The
     *  cache stores these as `varp - 1`, so 0 means "never chosen". */
    uint32_t (*setting_color)(struct ToriRS_PluginCtx* ctx, int varp_id, uint32_t fallback);
    /** The nearest entity under the pointer -- the one the client's own
     *  left-click would act on. Not hover_tile, which answers with the ground
     *  and is filled even over open grass. */
    int (*hover_entity)(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginHoverEntity* out);
    /** Where an overhead hangs above an element, in the projector's units --
     *  the anchor the client's own health bars and chat heads use. */
    int (*element_height)(struct ToriRS_PluginCtx* ctx, int element_id);
    /** Locs in the loaded scene -- the door, the tree, the rock. Scene-scoped:
     *  a loc has no server-side identity a client can hold on to, so its tile
     *  is its identity across a rebuild. */
    int (*loc_next)(struct ToriRS_PluginCtx* ctx, int iter, struct ToriRS_PluginLocSnap* out);
};
```

and one event, for the rows that are momentary rather than stateful:

```c
/** An All Settings row was used. Payload: EvSetting. The only way to see a
 *  BUTTON row (117 "Clear your highlighted tiles"), which has no var of its
 *  own -- the panel's only trace of it is `%varbit9657 = <setting id>`. */
TORIRS_PLUGIN_EV_SETTING
```

That event needed an engine seam too, because the write happens inside a
running clientscript and nothing there may touch the plugin bus. All four apply
hubs open with `%varbit9657 = <setting id>`, so `RS_CS2Host` queues the id at
that write (`RS_CS2Host_TakeSettingsAction`) and `App_RunOnce` drains it into
`PluginHost_Setting` once a frame. A queue rather than a latch on the varbit
itself: the var layer drops an equal write, so the varbit cannot say "pressed
twice", and a button that only worked on alternate presses is worse than one
that does not work at all.

`TORIRS_PLUGIN_ABI` is bumped, from 7 to 8, for all of it. The Lua adapter
carries the same additions (`api.varbit`, `api.varp`, `api.setting_color`,
`api.hover_entity`, `on_setting`), because a contract only one language can
reach is not the contract this layer claims to have.

## The list

`var` is what the client actually reads.

**`sense` is the trap.** `param_1084` looks like a default and is not: the row
builder (clientscript 3846) reads it into a boolean and, when it is set, draws
the checkbox as `1 - varbit`. So a row marked **inverted** below is ON when its
varbit reads **0**, and the driving scripts agree — clientscript 6681 installs
the tile-marker client op when `%varbit12342 = 0`, and 8319 lights the poll
booths when `%varbit9538 = 0`.

30 of the 54 desktop toggles are inverted and 24 are not, with no pattern to
them. Reading an inverted row the plain way gives a feature that is on exactly
when the user asked for it to be off, which looks like it works until someone
switches it off — this document said "default" here for a while and one plugin
was written against it. `nxt_activities.h` carries `NXT_ON()` and
`NXT_ON_INVERTED()` so no plugin has to decide again.

For a colour row `sense` is `param_1230`, the swatch the panel shows before
anyone picks one.

Mobile-only rows (`param_740`: 410, 409, 212) are omitted: this client is
`clienttype` 10 and never builds them.


### General

| id | row | var | sense / choices | setting | what it has to do |
|---:|-----|-----|-----------------|---------|-------------------|
| 112 | toggle | `%varbit12342` | **inverted** | **Tile highlighting** | When enabled, hold shift and right-click the ground to place highlights. |
| 113 | colour | `%var3108` | #00FF00 | **Tile highlight colour** | What colour your marked tiles will show up as. |
| 117 | button | — | — | **Clear your highlighted tiles** |  |
| 190 | toggle | `%varbit13088` | plain | **Highlight entities on mouse-over** | Adds a highlight to entities when you hover the mouse cursor over them. |
| 172 | toggle | `%varbit12977` | plain | **Highlight hovered tile** | When enabled, highlights the tile under the mouse. |
| 173 | toggle | `%varbit12980` | plain | **Highlight hovered tile - Always on top** | When enabled, shows the hovered tile highlight over objects in the world. |
| 174 | colour | `%var3155` | #BEBA6E | **Highlight hovered tile - Colour** | Colour to highlight the tile under the mouse. |
| 175 | toggle | `%varbit12978` | plain | **Highlight current tile** | When enabled, highlights the tile you are currently on. |
| 176 | toggle | `%varbit12981` | plain | **Highlight current tile - Always on top** | When enabled, shows the current tile highlight over objects in the world. |
| 177 | colour | `%var3156` | #9A9733 | **Highlight current tile - Colour** | Colour to highlight the tile you are currently on. |
| 178 | toggle | `%varbit12979` | plain | **Highlight destination tile** | When enabled, highlights the tile you are moving to. |
| 179 | toggle | `%varbit12982` | plain | **Highlight destination tile - Always on top** | When enabled, shows the destination tile highlight over objects in the world. |
| 180 | colour | `%var3157` | #A9A753 | **Highlight destination tile - Colour** | Colour to highlight the tile you are moving to. |
| 261 | toggle | `%varbit14168` | plain | **NPC highlight** | When enabled, tagged NPCs will be highlighted. |
| 258 | dropdown | `%varbit14169` | Off / On - Normal / On - Bold | **NPC highlight - Display name** | When enabled, display the NPC's name above its body. |
| 259 | dropdown | `%varbit14171` | Off / Outline only / Outline and fill | **NPC highlight - Highlight tile** | When enabled, highlight the tiles the NPC is on. |
| 260 | toggle | `%varbit14170` | plain | **NPC highlight - Highlight outline** | When enabled, highlight the NPC. |
| 262 | colour | `%var3540` | #05F8F8 | **NPC highlight - Highlighting colour** | Colour to highlight the NPC's. |
| 263 | colour | `%var3541` | #05F8F8 | **NPC highlight - Text colour** | Colour of the text shown above the highlighted NPC. |
| 416 | toggle | `%varbit11518` | plain | **NPC highlight - Tagging** | When enabled, hold shift and right-click an NPC to tag it for highlighting. |
| 267 | button | — | — | **Clear your highlighted NPCs** |  |
| 264 | dropdown | `%varbit14178` | Off / On - Normal / On - Bold | **Display all NPC names above their body** | When enabled, display the name of every NPC above its body. |
| 266 | colour | `%var3542` | #05F8F8 | **NPC names text colour** | Colour of the text shown above all NPCs. |
| 453 | toggle | `%varbit9538` | **inverted** | **Highlight poll booths** | When enabled, poll booths will be highlighted in the game world when the game is inviting you to vote. |

### Skills

| id | row | var | sense / choices | setting | what it has to do |
|---:|-----|-----|-----------------|---------|-------------------|
| 163 | toggle | `%varbit12379` | **inverted** | **Agility helper** | When enabled, the Agility helper will be shown when on Agility courses. |
| 164 | toggle | `%varbit12380` | plain | **Highlight Agility obstacles** | When enabled, Agility obstacles will be highlighted in the game world, for the course that you are training on. |
| 165 | dropdown | `%varbit13135` | Off / On / On (Req based) | **Highlight Agility shortcuts** | When enabled, Agility shortcuts will be highlighted in the game world. |
| 210 | toggle | `%varbit13136` | **inverted** | **Highlight Agility shortcuts - Shortcut Requirements** | When enabled on members' worlds, Agility shortcuts you can't use will have an overlay displaying the requirements to use them. |
| 187 | dropdown | `%varbit13085` | On / On carrying a pickaxe / Off | **Ore respawn timer** | When enabled, a respawn timer will be shown when you mine a rock for ore. You can set this to only be shown if you have a pickaxe. |
| 188 | dropdown | `%varbit13086` | On / On carrying an axe / Off | **Woodcutting respawn timer** | When enabled, a respawn timer will be shown when you chopdown a tree. You can set this to only be shown if you have an axe. |
| 189 | toggle | `%varbit13087` | **inverted** | **Bird nest notification** | When enabled, a notification will be displayed if you obtain a bird nest drop while cutting down trees. |
| 120 | toggle | `%varbit12349` | **inverted** | **Fishing spot indicators** | When enabled, fishing spots will be highlighted and the fish the spot gives will be shown above the fishing spot. You can hover over the fish shown to see every fish that can be caught at the spot. |
| 121 | toggle | `%varbit12350` | **inverted** | **Fishing spot indicators - Tools only** | When enabled, fishing spot indicators will only be shown if you have a tool that can be used at the fishing spot. When disabled, show fishing spot indicators regardless of tools. |
| 122 | toggle | `%varbit12351` | **inverted** | **Fishing spot indicators - Mouse over tooltip** | When enabled, hovering over a fishing spot indicator will show all the fish the spot has to offer. |
| 184 | toggle | `%varbit13082` | **inverted** | **Slayer helper** | When enabled, the Slayer helper will be shown while training slayer. |
| 243 | toggle | `%varbit14165` | **inverted** | **Hunter trap timers** | When enabled, shows the time until your trap will be dismantled. Also shows if your trap has caught something or not. |
| 245 | toggle | `%varbit14172` | **inverted** | **Herbiboar helper** | When enabled, highlight starting locations, footprints and inspect locations for herbiboar hunting. |

### Combat

| id | row | var | sense / choices | setting | what it has to do |
|---:|-----|-----|-----------------|---------|-------------------|
| 116 | toggle | `%varbit12346` | **inverted** | **Data orbs - Regeneration indicators** | When enabled, adds a regeneration timer around your HP and special attack orbs on your minimap. |
| 5 | toggle | `%varbit10236` | **inverted** | **Hitsplat tinting** | When enabled, hitsplats caused by damage that you did not deal are tinted. |
| 279 | toggle | `%varbit14196` | **inverted** | **Max hit hitsplats** | When enabled, your max hit hitsplats will look different from non max hits. (Excludes fixed damage, damage from effects such as recoil, retribution, vengeance, corruption, poison and venom, as well as damage from thralls.) |
| 280 | slider | `%varbit14195` | — | **Max hit hitsplats threshold** | Max hits below this threshold will not show max hit hitsplats. |
| 182 | toggle | `%varbit13039` | **inverted** | **Iron loot restriction indicator** | When enabled, Ironmen will occasionally see indicator icons to warn them if they're attacking a creature that's restricted from dropping loot to them. |
| 183 | toggle | `%varbit13040` | **inverted** | **Iron loot restriction messages** | When enabled, Ironmen will occasionally see chatbox messages warning them if they're attacking a creature that's restricted from dropping loot to them. |
| 10 | toggle | `%varbit12389` | **inverted** | **Show boss health overlay** | When enabled, fighting certain bosses will display a larger health overlay. |
| 111 | toggle | `%varbit12390` | **inverted** | **Show normal health overlay** | When enabled, fighting any enemies will display a small health overlay. |
| 299 | toggle | `%varbit14706` | **inverted** | **Show enemy name on health overlay** | When enabled, the enemy name will be displayed on the health overlay. |
| 301 | dropdown | `%varbit14708` | Exact Value / Percentage | **Health overlay display type** | Determines how values are displayed on the health overlay. |
| 300 | toggle | `%varbit14707` | plain | **Compact boss health overlay** | When enabled, the boss health overlay will be more compact. |
| 118 | toggle | `%varbit12347` | **inverted** | **Chambers of Xeric helper** | When enabled, the Chambers of Xeric helper will be shown when in a Chambers of Xeric raid. |
| 247 | toggle | `%varbit14174` | **inverted** | **Cannon hud** | When enabled, shows how many cannonballs you have left in your cannon. |
| 248 | toggle | `%varbit14175` | plain | **Cannon low on ammo notification** | When enabled, a notification will be sent when you are low on cannonballs. |
| 249 | slider | `%varbit14176` | — | **Cannon low on ammo amount** | A notification is sent when your cannon reaches this quantity of remaining cannonballs. |
| 250 | toggle | `%varbit14177` | plain | **Cannon out of ammo notification** | When enabled, a notification will be sent when your cannon has run out ammo. |

### Quests

| id | row | var | sense / choices | setting | what it has to do |
|---:|-----|-----|-----------------|---------|-------------------|
| 367 | dropdown | `%varbit9619` | Off / On for last quest viewed / On | **Highlight quest start points** | When enabled, quest start points for unstarted quests will be highlighted. Quest start points can be NPCs or scenery. |
| 368 | toggle | `%varbit9622` | plain | **Filter quest start highlights based on requirements** | When enabled, only quests that you meet all the requirements for will have their quest start point highlighted. |

### Minigames

| id | row | var | sense / choices | setting | what it has to do |
|---:|-----|-----|-----------------|---------|-------------------|
| 81 | dropdown | `%varbit11865` | Default / Green / Yellow / Red / Blue / White | **Last Man Standing fog colour** | Displays the chosen colour of the fog inside of Last Man Standing. |
| 242 | toggle | `%varbit14167` | **inverted** | **Tears of Guthix timers** | When enabled, shows timers on the Weeping walls. |
| 268 | toggle | `%varbit14180` | **inverted** | **Blast Furnace helper** | When enabled, the Blast Furnace helper will be shown when in the Blast Furnace. |
| 269 | toggle | `%varbit14181` | **inverted** | **Blast Furnace highlights** | When enabled, certain pieces of scenery at the Blast Furnace will be highlighted in the game world. |

### Treasure trails

| id | row | var | sense / choices | setting | what it has to do |
|---:|-----|-----|-----------------|---------|-------------------|
| 374 | toggle | `%varbit10693` | **inverted** | **Beginner clue scroll warning** | When enabled, you will get a message if you do not receive a clue as a drop because you already own one. |
| 375 | toggle | `%varbit10694` | **inverted** | **Easy clue scroll warning** | When enabled, you will get a message if you do not receive a clue as a drop because you already own one. |
| 376 | toggle | `%varbit10695` | **inverted** | **Medium clue scroll warning** | When enabled, you will get a message if you do not receive a clue as a drop because you already own one. |
| 377 | toggle | `%varbit10723` | **inverted** | **Hard clue scroll warning** | When enabled, you will get a message if you do not receive a clue as a drop because you already own one. |
| 378 | toggle | `%varbit10724` | **inverted** | **Elite clue scroll warning** | When enabled, you will get a message if you do not receive a clue as a drop because you already own one. |
| 379 | toggle | `%varbit10725` | **inverted** | **Master clue scroll warning** | When enabled, you will get a message if you do not receive a clue as a drop because you already own one. |
| 451 | toggle | `%varbit17938` | plain | **STASH units take equipped items** | When active, when putting items back into a STASH UNIT, equipped items will be taken. Your inventory takes priority. |
| 271 | toggle | `%varbit14182` | plain | **Clue scroll helper** | When enabled, reading a clue scroll will display extra information to help solve it. |
| 275 | toggle | `%varbit14187` | plain | **Clue scroll helper - Infobox** | When enabled, information about a clue will be shown in an infobox. |
| 276 | toggle | `%varbit14188` | plain | **Clue scroll helper - Clue text** | When enabled, shows the clue text in the infobox. |
| 270 | toggle | `%varbit14189` | plain | **Clue scroll helper - Overlay** | When enabled, shows overlays above any relevant entities. |
| 277 | toggle | `%varbit14184` | plain | **Clue scroll helper - Entity highlights** | When enabled, highlights any relevant entities. |
| 273 | toggle | `%varbit14185` | plain | **Clue scroll helper - World arrows** | When enabled, an arrow will be shown in the world to indicate where the clue step is. |
| 272 | toggle | `%varbit14183` | plain | **Clue scroll helper - Worldmap marker** | When enabled, a marker will be shown on the world map to indicate where the clue step is. |
| 274 | toggle | `%varbit14186` | plain | **Clue scroll helper - Menu highlights** | When enabled, relevant buttons will be highlighted in interfaces. |

## The plugins

One plugin per *feature*, not per row: a row that only qualifies another one
("- Always on top", "- Colour", "- Tools only") belongs to the same plugin as
the row it qualifies, because it is read in the same place and means nothing
apart from it.

Every one of them is `.hidden = true`, always enabled, and reads its own
switch. `src/plugin/plugins/nxt_*.c`, registered in
`src/plugin/torirs_plugin_registry.c`.

| # | plugin | rows | status |
|--:|--------|------|--------|
|  0 | `nxt-highlight` | every row the cache drives | done |
|  1 | `nxt-bird-nest` | 189 | done |
|  2 | `nxt-cannon-ammo` | 248 249 250 | done |
| .. | (the rest) | see "Status" above | not started |


Plugin 0 is the renderer for everything the cache drives itself; 1 and 2 are
per-setting builtins for rows the cache does not drive at all.

Seven more rows are the SERVER's, in the treasure-trail content lane: the six
clue-scroll warnings (374-379) and STASH's equipped-item rule (451). Those
could not be the client's -- the drop it did not give you, and the item it took
off your back, both happen there and the client never learns either happened.
The cache names those varbits for us too: `option_trail_reminder_beginner` and
its five siblings, and `option_hidey_holes_equipped`.

**Six builtins have been deleted**, and that is the point rather than a
retreat. `nxt-tile-markers` went when `CLIENTOP_*` landed and the cache started
installing its own "Mark tile" row; `nxt-tile-indicator` when the three tile
refreshers started running; `nxt-entity-hover` when `MINIMENU_*` let
clientscript 5350 do its job; `nxt-npc-highlight` when the cache's own "Tag"
client op appeared beside it in the menu -- there were briefly two "Tag Duck"
rows, which is exactly the double-draw this was always going to become.

`nxt-npc-names` and `nxt-poll-booths` went last, when the world-anchored
component family and the client triggers landed together.

The poll booths are the clearer of the two, because the cache's answer is
strictly better than the builtin's. The builtin matched booths BY NAME, on the
reasoning that no id list stays complete across revisions -- but the cache
carries loc CATEGORY 761, which is exactly the thirty-four votable booths and
nothing else. The two records it leaves out,
`clanwars_tournament_pollbooth_blue` and `pollbooth_green_noop`, are a prop and
a dead booth, and the name match highlighted both. One behaviour went with it,
deliberately: the builtin lit booths unconditionally, and clientscript 8319
gates on "there is an active poll", so the row is inert against a server that
never declares one. That is the truthful state; a booth that lights up forever
teaches the user to ignore it.


It was the remainder of `nxt-npc-highlight`: the name over an npc is the one
part the cache does not express as a highlight flag, because it builds a
component for it instead (clientscript 6698). The cache's version is better in
a way the fake could not be -- it uses the row's own font, `fontmetrics_495`
for Normal and `496` for Bold, where the builtin had one font and struck the
glyphs twice a pixel apart.

Each was deleted in the change that replaced it. Not before, or those rows go
dark; not after, or they draw twice.

### "- Always on top" (173, 176, 179) is read and not honoured

The row means "draw the marker over the scenery in front of it". `draw_tile`
lands in this client's overlay layer, which is composited after the scene and
is therefore *always* on top -- so the ON state is what you get either way and
the OFF state cannot be produced at all. Honouring it needs a depth-tested
ground primitive; ToriDraw's z-buffer scratch is per MODEL
(`TORIDRAW_SCENE_MODEL_ZBUFFER`) and the overlay layer has none. That is a
renderer change, not a plugin one.

The varbit is still read, and deliberately not acted on. Reading it keeps the
dependency visible where the fix will go. Acting on it -- hiding the marker
when "always on top" is off -- would be worse than doing nothing, because it is
not what the row says, and a user turning it off would lose the marker
entirely.

### 453's "you have not voted" half is the server's

The row's full sentence is "poll booths will be highlighted when there is an
active poll you have not voted in". Clientscript 8319 reads `%varbit4337` for
the first half; this server never writes it, and nothing carries the second at
all. So the highlight is unconditional while the setting is on, and the gate is
written down as a server feature that does not exist rather than faked -- a
booth that lit up forever would teach the user to ignore it.

453 is also an **inverted** row: the feature is on when `%varbit9538` reads 0.
This plugin read it the plain way at first, which highlighted every booth in
the game for anyone who had switched the setting off.

The booths are found by NAME rather than by an id list, and that is worth
knowing before the next loc-based plugin is written: this cache holds dozens of
separate poll booth locs, all called "Poll booth", and the set is not stable
between revisions. An id table would be right for one cache and quietly
incomplete for every other.

### What each one still needs

The builtins that are done needed `varbit`, `varp`, `setting_color`,
`hover_entity`, `element_height`, `loc_next`, `highlight_next` and
`EV_SETTING`; those have landed, as has the whole `CLIENTOP_*` layer.

**The unblock was never an api, and it is done.** It was the two things named
under "What still does not populate" -- a "this loc came into view" hook and a
"this npc appeared" one. They turned out not to be hooks this repo had to
invent: they are the cache's own **client triggers**, and honouring them turned
241 unreachable scripts into running ones in a single change. Every group that
was set up and empty is now populated by the cache itself. See "Client
triggers" above.

What that leaves for the remaining rows is not plumbing but content: a fishing
spot to stand next to, a cannon to place, a clue to read.

The rows the cache does NOT drive at all are blocked on api the layer does not
have, and the gaps are worth naming because several share one:

- **`inv_next` / `inv_count`** -- read a container. 6 ("Tools only" is "am I
  carrying a rod"), 19 (cannonballs), 24 (do I already hold this clue), 25.
- **a loc despawn/respawn edge** -- 8 times a rock or a stump from the moment
  it changes. `EV_LOC_CHANGE` alongside the obj events.
- **hitsplat and health-bar interception** -- 14, 15, 17. These are not overlay
  drawing: they change what the *entity* draws, which the plugin layer has no
  seam onto at all. Most likely these stay engine-side and read the varbit
  directly rather than becoming plugins.
- **a notification primitive** -- 9, 11, 19, 22 want "say this in the chatbox
  and optionally flash the window". `api->notify`.
- **cache config reads** -- `objtype`/`loctype`/`npctype` params by id. 6, 7,
  20 and 26 all need to recognise things by more than a hardcoded id list.
  Note that the highlight route sidesteps this entirely for the rows it
  covers: the cache already knows which loc is an Agility obstacle and says
  so, which is why honouring the opcodes is worth more than the same rows'
  worth of hand-written plugins would be.

### Rows that are not the client's to implement

- **451 STASH units take equipped items** is a *server* behaviour: the client
  only stores the preference. It is listed for completeness; the row's job on
  this side is done the moment the varbit round-trips.
- **81 Last Man Standing fog colour** needs an LMS fog effect to exist first.

## Verifying

```sh
make -C src test-clientop        # the CLIENTOP_* registry and its context ops
make -C src test-highlight       # the cache's HIGHLIGHT_* family, as state
make -C src test-client-trigger  # trigger hashes vs real cache identifiers
make -C src test-nxt-plugins     # the builtins against a fake engine
make -C src test-plugin-host     # the host, including the hidden flag
make -C src test-uitree          # includes the scripted-overlay draw path
```

`test-clientop` and `test-highlight` drive real calls, copied out of the
decompiled clientscripts with their arguments intact. The failure it guards is not the bookkeeping but
which argument slot holds the group: three of the eight kinds put it somewhere
different -- `highlight_tile_on(coord, group, flags)`,
`highlight_loc_on(type, coord, group, flags)`,
`highlight_npctype_on(type, group)` -- and reading the wrong one gives a
plausible group number every time. A test written from the table it is testing
would agree with any of those.


`test-uitree`'s two scripted-overlay cases are the only proof that an overlay
reaches the screen. A screenshot cannot be that proof: the cache builds these
only when a setting is on, so "nothing there" is the same picture whether the
feature is off, the trigger never fired, or the layer drew somewhere nobody can
see. The cases pin all three separately -- the child reaches the emit list, it
is hoisted with the world rather than left where the tree lists it (which is
over the inventory), and it is clipped to the viewport.

`test-nxt-plugins` is where a broken builtin actually shows. These plugins have
no roster row, no config page and no log line: a broken one draws nothing,
which looks exactly like a setting somebody left switched off -- which is also
what the whole category looked like before any of this existed. So the tests
drive each row's varbit from off to on and check that the drawing appears, that
it appears in the row's own colour rather than a hardcoded one, and that the
colour rows' `+1` offset is dropped (getting that wrong is a colour one unit
out, which no screenshot can show).

For the client itself:

```sh
SDL_VIDEODRIVER=dummy TORIRS_MAX_FRAMES=60 \
    src/torirs --manifest manifests/manifest_osrs239.ini --offline
```

The boot line names them -- `nxt-highlight, nxt-bird-nest, nxt-cannon-ammo`.

For the cache-driven half, the two debug channels are the tools:

```sh
SDL_VIDEODRIVER=dummy TORIRS_HIGHLIGHT_DEBUG=1 TORIRS_CLIENTOP_DEBUG=1 \
  TORIRS_SIM_KEYHOLD=42 TORIRS_SIM_CLICK_AT="700,250,250,1;720,232,281" \
  TORIRS_MAX_FRAMES=800 ./run-live.sh manifests/manifest_osrs239.ini testc test
```

`TORIRS_HIGHLIGHT_DEBUG` prints every highlight call with its arguments,
`TORIRS_CLIENTOP_DEBUG` every client-op install and dispatch plus the mouseover
target as it changes. When a highlight does not appear the first question is
always "did the script ask for it", and from outside the VM there is no other
way to answer it.

`TORIRS_SIM_KEYHOLD="<keycode>[,...]"` holds keys for the whole run (42 is
shift). The click sims had no way to say "with shift down", and shift is not a
decoration on a right click -- it is what makes the client-op rows appear at
all.

`TORIRS_TRIGGER_DEBUG=1` prints every client trigger raised with its subject,
its category and the script it resolved to (or -1). `TORIRS_OVERLAY_SCRIPT_DEBUG=1`
prints every scripted-overlay op with its arguments, then one line per live
overlay per frame with its anchor, band, box and child count, and a line
whenever one is reaped.

### Turning a setting on without the panel

```sh
TORIRS_SIM_VARBIT="<frame>,<id>,<value>[;...]"
```

Every row in this category is a varbit, and **nothing in the cache writes one**
-- the panel's own row does, through a path that needs a real click on a real
mounted panel. That made all seventy-four rows unverifiable from a headless run
without a click script each. This writes one directly, the way the panel's write
is written (optimistic), and prints the base varp and the value it reads back --
so "the write did not land" and "the write landed and nothing acted on it" are
distinguishable, which they were not.

**Prefer the cache's own writer where there is one.** Most rows have a
clientscript that flips the varbit, reachable through the settings hubs, and
running that does the write AND the apply in one:

```sh
TORIRS_SIM_RUNSCRIPT="<frame>,3965,<setting id>"          # a toggle
TORIRS_SIM_RUNSCRIPT="<frame>,3967,<setting id>,<value>,0"  # a dropdown
```

`3965`'s switch reaches e.g. `~script4578` for row 10 and `~script4579` for
111; `3967`'s reaches `~script6471` for 264. A handful of dropdowns have no
writer script at all -- row 165's `%varbit13135` is one -- because the panel's
own widget writes them; those are what `TORIRS_SIM_VARBIT` is for.

Two caveats, both learned the hard way:

- **Not before the varbit config loads.** Too early and the write is a silent
  no-op; the readback in the log line is what says so (`base varp -1`).
- **The write is not the whole apply.** The panel writes the varbit *and* runs
  the row's apply, and some rows need both. Setting 165 is the example: the
  write alone re-runs the per-loc script and the shortcuts join highlight group
  12, but group 12's *style* is set by clientscript 5325, which only the apply
  path re-runs -- so the group stays inert and nothing outlines. Pair the write
  with `TORIRS_SIM_RUNSCRIPT="<frame>,5488"`, the settings re-init, and the
  outline appears.

```sh
SDL_VIDEODRIVER=dummy TORIRS_HIGHLIGHT_DEBUG=1 \
  TORIRS_SIM_VARBIT="450,13135,1" TORIRS_SIM_RUNSCRIPT="470,5488" \
  TORIRS_MAX_FRAMES=900 TORIRS_EXIT_BMP=/tmp/agility.bmp \
  ./run-live.sh manifests/manifest_osrs239.ini testc test
```

Row 210 ("Shortcut Requirements") is the same run with `13135` set to 2, and
its two branches were checked separately rather than assumed. Both shortcuts in
range go into group 12 (green) with the test account's levels; adding
`TORIRS_NET_CHEAT="setlevel 16 1"` drops Agility to 1 and 19032 and 19036 move
to group 13 (red) while 7527 -- which has no Agility requirement -- stays green.
A helper that colours everything the same colour is the failure this catches,
and it looks correct in a screenshot.
