# Server cheats / quest selftests / instances

Repo researched: /Users/matthewevers/Documents/git_repos/3draster-quest-driver
(NOT /Users/matthewevers/Documents/git_repos/3draster — untouched.)

## A. Cheat dispatch path

Client (typed `::foo`):
- src/game/rs_chat.c:825-843 — chat input widens the allowed typed-char range
  to 126 only when the line already starts with `::` (lets `~`,`{`,`|`,`}`
  through), reference behaviour.
- src/app/app_frame.c:1836-1861 — on Enter, if input starts with `::`:
  special-cased `::clientdrop` (local disconnect sim), else the `::` is
  stripped and the remainder is sent via `net_out_client_cheat(...)`
  (src/net/net_out.c:1746) as packet **PKTOUT_NAME_CLIENT_CHEAT**.
  Also reachable via `App_SendCommand` (src/app/app_net.c:53-73), used by
  `TORIRS_SIM_CMD` (src/main.c:3128), `TORIRS_NET_CHEAT` boot cheats
  (src/app/app_tick.c:100-200+), and CmdBus EXEC_TEXT
  (src/app/app_frame.c:1827-1839). `app_client_cheat` (app_net.c:22-51)
  intercepts ONLY the purely client-local `lootkill` cheat before anything
  is sent to the server.

Server:
- Packet table: `{ PKTOUT_NAME_CLIENT_CHEAT, handle_cheat_packet }`
  (src/torirsserver/torirs_server_world.c:11072) → `handle_cheat_packet`
  (:9427-9436) → `handle_cheat` (:7751).
- `handle_cheat` (torirs_server_world.c:7751-9164):
  1. Reads the newline-terminated body (`::` already stripped by the
     client); if it starts with `~`, strips that server-side namespace
     escape too (:7773-7774).
  2. **Content is tried FIRST**: `ToriRSServer_ScriptsRunDebugproc(srv, text)`
     (:7803) looks up `[debugproc,<name>]` in the RuneScript tree. TRIGGER_RAN
     → return (content owned it). TRIGGER_FAILED → `say` an error and return.
     TRIGGER_NONE (no such debugproc) → falls through.
  3. **Only then** is the C `strncmp(text, "...", N)` ladder tried
     (:7819 onward), ending in two bare `sscanf` fallbacks with no strncmp
     guard at all (`item %d %d`, `tele %d %d`) and an `npc %d` fallback,
     then "Unknown command" (:9163).
  - The dispatch order is explicit in a comment (:7776-7785): "exactly as
    `[if_button]` is dispatched... a cheat writable without touching the
    engine... LostCity has no C-side cheat that a debugproc could not
    replace."

### Complete engine (C) ladder — every strncmp/sscanf literal in `handle_cheat`

| literal | line | meaning |
|---|---|---|
| `talk` | 7819 | `::talk <slot\|name> [op]` fires `[opnpc<op>]` on an npc without a right-click |
| `setting ` | 7886 | `::setting <varbit> <value>` mirrors an All Settings row write |
| `minimap ` | 7926 | `::minimap <0..5>` forces a native minimap state |
| `ifhide ` | 7935 | `::ifhide <uid> <0|1>` toggles a component's hidden flag |
| `layout ` | 7945 | `::layout <0|1|2>` Fixed/Resizable Classic/Modern, via a synthesized IF_BUTTON |
| `style` | 7993 | `::style <0-3>` sets attack style (accurate/aggressive/defensive/controlled) |
| `setlevel` | 8018 | `::setlevel <stat> <level>` sets a stat's level (base + xp to threshold) |
| `wield ` | 8047 | `::wield <objid>` runs the real OPHELD-equip path on a backpack item |
| `equipstats` | 8090 | `::equipstats` opens the equipment bonus screen |
| `run` | 8098 | `::run [0|1]` toggles the run-energy option |
| `god` | 8112 | `::god [0|1]` player invulnerability (gates CombatHitPlayer + stat-write drains); heals to full on enable |
| `bank` | 8137 | `::bank` opens the bank |
| `fight` | 8146 | `::fight [slot]` engages an npc (nearest attackable if no slot given) |
| `useon` | 8196 | `::useon <a> <b>` synthesizes OPHELDU on two named items (giving them first if absent) |
| `give` | 8299 | `::give <name> [count]` adds an item to the backpack by display/gameval name |
| `spawn` | 8366 | `::spawn <npc_name\|id> [count]` spawns npcs by name (capped at 20), `despawns_on_death=1` |
| `vesselgoto` | 8459 | `::vesselgoto <x> <z> [level]` — teleport under a name content doesn't own (`::tele` is claimed by `[debugproc,tele]`, see below) |
| `vesselwater` | 8491 | `::vesselwater [radius]` — ASCII-dump sailable tiles around the caller |
| `vesselspawnat` | 8545 | spawn a boat at an exact tile |
| `vesselspawn` | 8671 | find real ocean and build a sailing instance |
| `vesselsail` | 8809 | crew/sail state debug |
| `vesselboard` | 8851 | board the lowest live hull |
| `vesselop` | 8921 | vessel op debug |
| `vesselseq` | 8968 | vessel animation debug |
| `helm` | 9005 | take the helm |
| `sails` | 9045 | sail state |
| `speedup`/`speeddown` | 9064 | vessel speed debug |
| `reverse` | 9087 | vessel reverse debug |
| `vesselstep` | 9109 | `::vesselstep <dx> <dz>` walks the caller a few tiles (deck-rider testing) |
| `item %d %d` (sscanf, no strncmp) | 9133 | `::item <objid> [count]` — id-keyed twin of `::give` |
| `tele %d %d` (sscanf) | 9147 | `::tele <x> <z>` — **only reachable when content's `[debugproc,tele]` does NOT claim the word first** (see note below); absolute-tile teleport on the player's current level |
| `npc %d` (sscanf) | 9153 | `::npc <id>` — id-only ancestor of `::spawn`, spawns onto level 3 (a known long-standing quirk) |

**Not in the C ladder at all** (confirmed absent by exhaustive grep for
`strncmp(text,`/`sscanf(text,` in torirs_server_world.c): `die`, `kill`,
`killall`, `hit`, `heal`, `xp`, `pray`, `setvar`, `varp`, `varbit`, `quest`,
`stage`, `skip`, `invincible`. All of these (where they exist) are **content
debugprocs**, resolved via the content-first step above:
- `[debugproc,tele]`, `[debugproc,telefind]` —
  OSRS-Content/osrs239-content/server/scripts/general/scripts/misc/cheat_tele.rs2:51+
  — a `string` cheat (coord literal or curated name, see §E) that answers
  the packet BEFORE the C ladder's own `tele %d %d`/`vesselgoto` ever see the
  word "tele" (documented at torirs_server_world.c:8465-8469: "a capture
  harness driving `[net:boot] cheat=` cannot reach the C `tele` branch at
  all... the failure is silent").
- `[debugproc,xp]`, `[debugproc,xpdrop]`, `[debugproc,xpqueue]` —
  .../general/scripts/misc/cheat_xp.rs2:13+ — `::xp <stat> <amount>` calls
  `stat_advance`.
- `[debugproc,pray]` — .../skill_prayer/scripts/cheat_prayer.rs2:19 —
  `::pray <0-N>` toggles a prayer through the same `~prayer_toggle` the
  prayer book uses, auto-granting the required level.
- `[debugproc,die]` — .../player/death.rs2:419 — `::die` runs the *whole*
  death sequence via `~player_death_trigger` → `queue(player_death,...)`
  (not a shortcut).
- No generic `[debugproc,kill]`, `killall`, `hit`, `heal`, `setvar`,
  `varp`, `varbit`, `quest`, `stage`, `skip`, `invincible` exists anywhere
  in OSRS-Content (grepped `^[debugproc,<name>]` for each). Per-quest,
  narrowly-scoped skip/cheat debugprocs exist instead — see §D/§E.

## B. `ToriRSServer_RunDebugprocForTest` — confirmed: `t.cheat` cannot reach the engine ladder

- Definition: torirs_server_world.c:7741-7749.
  ```c
  int ToriRSServer_RunDebugprocForTest(struct ToriRSServer* srv, const char* line)
  {
      assert(srv); assert(line);
      return ToriRSServer_ScriptsRunDebugproc(srv, line);
  }
  ```
  Its own comment (:7732-7739) says it is exactly the content lookup
  `handle_cheat` makes at :7803, "kept" for a test that has `srv` in-process
  and no packet/socket to answer on.
- Sole caller: `DriveCore_Cheat` in src/plugin/torirs_plugin_drive.c:329-364,
  which backs the Lua verb `t.cheat` (registered as `{"cheat", lua_drive_cheat}`
  at torirs_plugin_drive.c:1008, `lua_drive_cheat` at :941). It strips a
  leading `::` (mirroring what a real client already removed) and calls
  `ToriRSServer_RunDebugprocForTest` directly — **there is no fallback to
  the C strncmp ladder anywhere in this path.**
- Confirmed: `t.cheat("::give scythe_of_vitur")`, `::setlevel`, `::spawn`,
  `::wield`, `::god`, `::fight`, `::useon`, `::run`, `::style`, `::bank`,
  `::layout`, `::minimap`, `::ifhide`, `::equipstats`, `::talk`, `::item`,
  `::tele <x> <z>` (bare sscanf form), `::npc <id>`, all the `::vessel*`
  commands — **none of these are reachable from `t.cheat`** because none of
  them is a `[debugproc,...]`; `ToriRSServer_ScriptsRunDebugproc` returns
  `TRIGGER_NONE` (mapped to `no_row`/`DRIVE_NO_ROW`) and the call stops there.
  `::tele`, `::xp`, `::pray`, `::die` (the content debugprocs) DO work
  through `t.cheat`.
- **Smallest hook to fix this**: add a new C entry point that runs the exact
  same two-step sequence `handle_cheat` runs (content-first, then the ladder),
  factored out of `handle_cheat`'s body so both paths share it — e.g.
  ```c
  int ToriRSServer_RunCheatForTest(struct ToriRSServer* srv, const char* line)
  {
      /* same as handle_cheat(): try [debugproc,line] first, then the
         strncmp/sscanf ladder, but return instead of writing packets/say() */
  }
  ```
  This requires splitting `handle_cheat`'s ladder body out of the
  packet-only concerns (it currently reads `payload`/`len` off the wire via
  `rsab_gjstr` and calls `say(srv, ...)`, which assumes `srv->active_player`
  and produces chat-line side effects — those are fine to keep for a test
  client too, since `t.cheat` already runs in-process against the same
  `srv`). Then register it as `{"cheat_ladder", lua_drive_cheat_ladder}`
  or make `t.cheat` itself call the combined function instead of
  `ToriRSServer_RunDebugprocForTest`. Either way, the return contract
  (`TRIGGER_RAN`/`FAILED`/`NONE`→`ok`/`refused`/`no_row`) already exists and
  can be reused as-is — the ladder branches currently `return;` with no
  verdict value, so the smallest structural change is giving the split-out
  ladder function an `enum ToriRSServerTriggerResult`-shaped return (RAN on
  any branch taken, NONE on falling through to "Unknown command").

## C. Server-side quest selftest harness

Structure (using src/torirsserver/test/quest_cook_selftest.u.h as the
template, 509 lines):
- Included from src/torirsserver/torirs_server_world_selftest.c via
  `#include "test/quest_cook_selftest.u.h"` (and 24 siblings, see list
  below, all `#include`d around torirs_server_world_selftest.c:3086-3105).
  `.u.h` = "unity header", textually spliced into one translation unit —
  these are NOT separately compiled files, they share statics/macros with
  the file that includes them.
- Each defines `static void selftest_quest_<name>(struct ToriRSServer* srv,
  struct ToriRSServerPlayer* player)`, which:
  1. Loads scripts if not already loaded (`ToriRSServer_ScriptsLoad`).
  2. Resolves every content symbol it needs by NAME via
     `ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_{NPC,OBJ,LOC,VARP,STAT,...}, "name")`
     and `SELFTEST_CHECK`s that they all resolved (cook: cook_type, cookquest
     varp, qp varp, cooking stat, milk/egg/flour/bucket/pot/grain objs,
     several locs — quest_cook_selftest.u.h:189-220).
  3. Resets world/player state directly (clears inventory, zeroes the quest
     varp, teleports via `ToriRSServer_WorldTeleport`) — quest_cook_selftest.u.h:222-230.
  4. Drives the ACTUAL trigger functions directly in C — no packet, no UI:
     `ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, cook_type, -1, cook_slot)`,
     `ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1/_OPLOCU, ...)`,
     ticking the world with `selftest_tick(srv)` (torirs_server_world_selftest.c:465-469,
     which acks the scene barrier then calls `ToriRSServer_WorldTick`) to
     drain SSVM_SUSPENDED scripts and resume dialogue pages
     (`cook_selftest_resume_pages`/`cook_selftest_drain_rewards`, both
     quest-file-local helpers).
  5. Asserts directly on `player->varps[cookquest]` (the raw server struct,
     no read indirection) via `SELFTEST_CHECK(cond, fmt, ...)`
     (macro at torirs_server_world_selftest.c:547-553: counts every check,
     logs "FAIL <msg>\n(<expr> at <file>:<line>)" to stderr on failure, and
     if `g_selftest_evidence` is open also TSV-logs every PASS).
  6. Emits a per-checkpoint human-readable stderr line on success, in a
     quest-local convention, e.g. cook: `"COOK PASS: %s trigger=%s
     observable=%s\n"` (quest_cook_selftest.u.h:250,261,278,...); most other
     quests use a shared local helper like `bio_pass`/`ghostsahoy_pass`/... that
     prints `"PASS <questname> <step> trigger=<trigger> <observable>\n"`
     (grep hit in animalmagnetism, anothersliceofham, biohazard, cog,
     demon, eaglepeak, entertheabyss, ghostsahoy, hazeelcult, imp,
     junglepotion, mcannon, deathplateau selftests, each near line 10-17 of
     its file). doric/druid/blackknight use their own spellings
     ("DORIC PASS", "quest_druid PASS", "BKF PASS").

How it's run:
- Entry point `int ToriRSServer_WorldSelftest(void)`
  (torirs_server_world_selftest.c:3110), invoked by
  `./src/<objdir>/torirsserver --selftest` (torirs_server_main.c:92-94,252).
  It heap-allocates one `struct ToriRSServer` (~5MB, static one-shot pointer,
  never freed — see the long comment at :3110-3132 on why not stack/BSS),
  memsets it fresh, and runs the ENTIRE selftest suite (sailing, ToB, GWD,
  every quest, tutorial island, etc.) as one linear sequence of stanzas in
  one process. Final line: `"ToriRSServer selftest: all checks passed\n"` or
  `"ToriRSServer selftest: %d failure(s)\n"`, exit code =
  `g_selftest_failures` (torirs_server_world_selftest.c:57649-57656).
- Make target: `make test-torirsserver` (src/makefile:5659-5660):
  ```
  test-torirsserver: torirsserver torirsserver-scripts torirsserver-servpack check-crystal-set-contract check-gauntlet-contract
      cd $(REPO_ROOT) && ./src/$(OBJ_DIR)/torirsserver --selftest
  ```
  (rebuilds the binary AND the compiled script pack AND the servpack first —
  a long comment at :5649-5657 explains this was fixed after a stale-script
  false-green: the C-side selftest tests `.rs2` behaviour only insofar as
  `script.dat` was rebuilt from today's source).
  Related: `make check-selftest-registration` runs
  `tools/check_selftest_registration.py`, which enumerates every
  `[proc,selftest_*]`/`[debugproc,*]` in content and fails the build if a
  stanza with assertions in it is never actually reached from C (written
  after a real incident: an unregistered stanza reported green and survived
  a mutation — tools/check_selftest_registration.py:1-33).

Quests with a C-side `.u.h` selftest (21, all included from
torirs_server_world_selftest.c, files in src/torirsserver/test/):
doric, imp, cook, druid, gobdip, blackknight, fluffs, cog, haunted, demon,
fishingcompo, hazeelcult, biohazard, junglepotion, mcannon, deathplateau,
animalmagnetism, anothersliceofham, entertheabyss, eaglepeak, ghostsahoy.
(Plus a non-quest sibling `tutorial_island_selftest.u.h`, and four sailing
siblings — `sailing_lifecycle/_multiplayer/_server/_stale_queues_selftest.u.h`
— which are not quests.) `selftest_quest_imp` is called separately, at the
very end of the suite (torirs_server_world_selftest.c:57635), the other 20
are called together at :36473-36494.

What a C selftest proves vs. the client-driven test (`make test-quests`,
tools/quest_gate/run.py + gate.py, driving `test/quests/<quest>.lua`
through the real embedded client):
- Only **2** quests currently have a client-driven Lua test:
  `test/quests/cooks_assistant.lua` and `test/quests/hans.lua` — a small
  fraction of the 21 with a C selftest.
- The C selftest calls trigger/proc functions directly in C
  (`ToriRSServer_ScriptsRunTrigger`, `ToriRSServer_ScriptsRunDebugproc`) and
  reads `player->varps[]` straight out of the live struct — it proves the
  **content logic** (script correctness, varp transitions, drop tables,
  reward math) runs correctly, with zero UI/rendering/click/packet path
  exercised.
- The client-driven test (`cooks_assistant.lua`) proves the **whole stack**:
  real dialogue navigation (`t.chat.drain/choose/continue_`), real clicks
  (`t.player.talk_to`), the reward-scroll UI (`t.scroll.title/rewards/close`),
  screenshots, and — importantly — **client/server sync races that the C
  selftest cannot see at all**, because the C selftest reads the server
  struct directly with no client in between. `cooks_assistant.lua`'s own
  comments say this explicitly (lines 173-181): "the reference server
  selftest (quest_cook_selftest.u.h) has no such gap because it reads the
  server's own player struct directly; this is a client-visibility race" —
  and works around it with a bounded `t.await` on `t.var.server(...)`
  rather than a fixed sleep, because a fixed-tick guess was observed to read
  pre-commit state live.

## D. Boss fights: npc death reaching content, and varp-by-name

Death pipeline (src/torirsserver/torirs_server_combat.c):
- A killing blow arms `npc->death_tick` and `npc->death_stage =
  TORIRSSERVER_DEATH_QUEUED` (combat.c:1382, :2601...).
- `npc_death_step` (combat.c:2582-2751) is a 4-tick state machine mirroring
  LostCity's `[proc,npc_death]`: QUEUED (stop/face-none) → ARRIVE (death
  sound + animation, once per life) → CORPSE (drop table + `npc_del`) →
  REAP. Comment at :2546-2581 spells out the exact tick ledger.
- In the CORPSE stage it calls `ToriRSServer_WorldNpcDied(srv, slot)`
  (combat.c:2697), which is where content is reached:
  ```c
  void ToriRSServer_WorldNpcDied(struct ToriRSServer* srv, int slot)
  {
      struct ToriRSServerNpc* npc = &srv->npcs[slot];
      ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_AI_QUEUE3, npc->type,
                                  ToriRSServer_NpcCategory(npc->type), slot);
      ToriRSServer_ScriptsRunProcOnNpc(srv, "[proc,slayer_on_npc_kill]", slot);
  }
  ```
  (torirs_server_world.c:11206-11226.) `[ai_queue3,<type>|<category>|_]`
  is an **exclusive** trigger — exact type first, then npc category, then
  the `_` catch-all — which is where drop tables AND quest-boss varp
  advancement both hang. A quest typically advances its varp inside its own
  `[ai_queue3,<boss_name>]` handler (e.g. `%questvarp = <next_stage>;`),
  the same trigger every drop table binds to.

Is there an engine cheat to kill an npc / set a varp by name?
- **No `::kill`/`::killall`/`::hit` anywhere** — confirmed by the exhaustive
  strncmp/sscanf ladder listing in §A and by grepping content for any
  `[debugproc,kill]`/`[debugproc,killall]`/`[debugproc,hit]` (no hits).
  The only place `ToriRSServer_WorldNpcDied` is called with a spawned test
  npc to *prove* a death-drop binding is the C selftest itself
  (torirs_server_world_selftest.c has ~20 call sites, e.g. :47395,:53372-53465,
  used for content-contract checks like the `rat_indoors` drop-table gap
  documented in quest_hetty.rs2:24-33) — never from a live-reachable cheat.
- **No `::setvar`/`::varp`/`::varbit` by name** exists either (confirmed:
  no such debugproc anywhere in OSRS-Content, no such strncmp branch in the
  C ladder). Instead, per-quest "skip"/"pass" debugprocs exist that write
  the specific varp(s) a quest needs directly, e.g.
  `[debugproc,biohazard_pass_mourner]`
  (OSRS-Content/osrs239-content/server/scripts/quests/quest_biohazard/scripts/quest_biohazard.rs2:85-90):
  ```
  [debugproc,biohazard_pass_mourner]
  if (%biohazard = ^biohazard_poisoned_stew) {
      if (~obj_gettotal(mournerkeytw) = 0) { ~biohazard_give(mournerkeytw); }
      mes("PASS biohazard boss=mournerstew2 CHEAT-SKIP");
  }
  ```
  driven from the biohazard selftest at
  src/torirsserver/test/quest_biohazard_selftest.u.h:498-505 as a
  "cheat-skip (no boss AI)". Similar per-quest skip debugprocs exist for
  hazeelcult (`boss=alomone CHEAT-SKIP`, quest_hazeelcult_selftest.u.h:491)
  and ghostsahoy (`boss=giant_lobster CHEAT-SKIP`, quest_ghostsahoy_selftest.u.h:504).
  These are all quest-scoped, hand-authored, and only settable through their
  own quest's script — there is no generic mechanism.
- **Where a `cheat_varp_from_name` would go**: alongside `cheat_obj_from_name`
  / `cheat_npc_from_name` (torirs_server_world.c:7498-7523), both thin
  wrappers over the shared 4-rung resolver `cheat_id_from_name`
  (:7412-7496 — numeric literal → exact gameval via
  `ToriRSServer_ContentSymbol(kind, wanted)` → underscored display-name exact
  match → unique substring). Varps have NO "display name" table (rungs 3/4
  need a `display_name(id)` callback and a `record_count`, which objs/npcs
  have and varps don't), but rung 1 (numeric) and, crucially, **rung 2
  already works for varps as-is**: `ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP,
  "cookquest")` is exactly how the cook selftest and combat code already
  resolve varps by name (quest_cook_selftest.u.h:190-191,
  torirs_server_vessel_lifecycle.u.h:95). So the smallest hook is:
  ```c
  int cheat_varp_from_name(const char* arg, char* suggest, size_t suggest_size)
  {
      return cheat_id_from_name(TORIRSSERVER_PACK_VARP, 0 /* no display-name rung */,
                                NULL, arg, suggest, suggest_size);
  }
  ```
  (with `cheat_id_from_name` tolerating a NULL `display_name`/0 `record_count`
  by skipping rungs 3, or trivially extended to substring-match over
  `ToriRSServer_ContentSymbolWalk(TORIRSSERVER_PACK_VARP, ...)` gamevals the
  same way rung 4 already does for objs/npcs), then a `setvar` branch in the
  C ladder:
  ```c
  if (strncmp(text, "setvar ", 7) == 0) {
      /* sscanf "setvar %63s %d", write player->varps[id] = value via
         ToriRSServer_WorldSetVarpOn (see :9179-9181 for the existing call shape) */
  }
  ```
- **`ToriRSServer_Ids()`/gameval table for varp names**: `ToriRSServer_Ids()`
  (torirs_server_ids.h:41+/:419-420) is a struct of specific, hand-declared
  engine-known symbol ids (interfaces, components, container ids, etc.) —
  NOT a generic varp-name table. The actual generic gameval table for varp
  names is the content-symbol pack `TORIRSSERVER_PACK_VARP`
  (torirs_server_content.h:65-73, `ToriRSServer_ContentSymbol` /
  `_ContentSymbolName` / `_ContentSymbolWalk`, torirs_server_content.h:144-193),
  already loaded from `pack/varp.pack` and already used throughout the
  engine and content by name (`"cookquest"`, `"qp"`, `"map_instance_handle"`,
  etc.) — this is the table a `cheat_varp_from_name` would walk.

## E. Instances

There is a full instance manager: src/torirsserver/torirs_server_mapinstance.h
(comment header :1-57). Six content-facing ops (alloc/setchunk/build/coord/
free/find, `ss_opcode.h` 11009..11014), a fixed pool
(`TORIRSSERVER_MAPINSTANCE_MAX 32`, :64), terrain/loc copied zone-by-zone
with rotation from a scanned pool of unused cache map squares
(`mapinstance_scan_pool`, x≥`MAPINSTANCE_SCAN_X0`, comment :38-44), a
per-instance owner/flag/128-int register file (`TORIRSSERVER_MAPINSTANCE_VARS`,
:84-104, used e.g. by the Theatre of Blood's 6 rooms), and a default 60s
"linger" auto-teardown once empty (`TORIRSSERVER_MAPINSTANCE_LINGER_DEFAULT`,
:110-124, `ToriRSServer_WorldMapInstanceFree`). This is engine-only —
LostCity ships no equivalent (:29-34).

Could a skip command simply set the post-boss varp and teleport out? — Yes
for the varp, but teleporting into/out of an instance needs care, evidenced
directly by tools/tele_extra_destinations.tsv (curated `::tele` name table,
consumed by `[debugproc,tele]`'s `~tele_resolve`, cheat_tele.rs2):
- Lines 37-44 (Fight Caves): "The cave interior is INSTANCED: fightcave.constant
  says every tile inside is a local offset into a private reservation and 'a
  coord that reaches the world without passing through ~fightcave_coord is a
  coord in the wrong cave'. There is therefore no `^fightcave_centre` or
  `^fightcave_entry` to name... `fight_caves -> ^fightcave_exit` (outside the
  cave; the interior is instanced)."
- Same rule stated again for Chambers of Xeric (lines 84-90, commented out
  entirely: "cox.constant declares only template-relative offsets, never an
  absolute staging or room coord") and for Zulrah (lines 62-65: "instanced
  and its configs declare no coord constant at all... no entry beats a
  guessed one").
- Header rule (lines 24-30): rows here take precedence over the automatic
  destination sources, bosses with no coord constant in the tree are
  **deliberately absent** rather than guessed ("a guessed coord is worse
  than no entry — it teleports you somewhere confidently wrong").
- `^respawn_coord` example (lines 34-35, 113-120): `home`/`respawn`/`lumbridge`
  all resolve to the single `^respawn_coord` constant content already
  declares, explicitly "the same one `::die` uses" — i.e. the curation
  discipline is: never invent a literal coord, always point at a constant
  the real content already stands a player on (entrances/exits/templates
  only for instanced content).
- Practical implication for a "skip" cheat: it can safely (a) write the
  post-boss varp directly (per-quest, as biohazard/hazeelcult/ghostsahoy's
  `..._pass_*`/CHEAT-SKIP debugprocs already do), and (b) teleport the
  player to the instance's known EXIT/entrance absolute coord (e.g.
  `^fightcave_exit`), exactly like `fight_caves` in the tsv — but it must
  NOT synthesize an interior coordinate, and for content whose only
  addressing is instance-relative (CoX, Zulrah) there is currently no
  absolute coord to land on at all; that content has no skip-by-teleport
  option today. `ToriRSServer_WorldTeleport` (torirs_server_world.c:9166-9202)
  already handles leaving a boat/instance correctly (clears
  `map_instance_handle` varp when the destination instance differs,
  :9176-9183), so the C teleport primitive itself is instance-aware — the
  gap is purely "no generic per-quest-boss skip cheat", not an engine
  limitation.

## F. Completed-quest stderr line, and `t.var.server`

- No single universal "QUEST ... PASS" line. Instead each quest selftest
  emits its OWN per-checkpoint stderr lines, all sharing the convention
  `PASS <questname> <step> trigger=<trigger> <observable>\n` (a local
  `*_pass()` helper defined near the top of most `quest_*_selftest.u.h`
  files, e.g. quest_biohazard_selftest.u.h:17, quest_ghostsahoy_selftest.u.h:14,
  quest_hazeelcult_selftest.u.h:17, quest_animalmagnetism_selftest.u.h:14,
  quest_cog_selftest.u.h:11 as `PASS clocktower ...`); a few spell it
  slightly differently (`"COOK PASS: %s ..."` in quest_cook_selftest.u.h:250 etc.,
  `"DORIC PASS %s\n"`, `"quest_druid PASS %s\n"`, `"BKF PASS %s\n"` for
  Black Knight's Fortress). Boss/AI-skip steps specifically read
  `PASS <quest> boss=<npc> CHEAT-SKIP` (biohazard :505, ghostsahoy :504,
  hazeelcult :491). At suite end, one final summary line:
  `"ToriRSServer selftest: all checks passed\n"` or
  `"ToriRSServer selftest: %d failure(s)\n"`
  (torirs_server_world_selftest.c:57649-57652), exit code =
  `g_selftest_failures`. Individual `SELFTEST_CHECK` failures print
  `"  FAIL %s\n       (%s at %s:%d)\n"` (selftest_check,
  torirs_server_world_selftest.c:183-185).

- `t.var.server(name)` (plugin-side quest-driver export):
  script/plugins/quest_driver/state.lua:81-90:
  ```lua
  function QD.var.server(name)
      local kind, id, fail_result, fail_name = QD._var_resolve(name)  -- tries varbit then varp by content symbol
      if not kind then return fail_result, fail_name end
      if kind == "varbit" then return api_drive.varbit_server(id) end
      return api_drive.var_server(id)
  end
  ```
  `api_drive.var_server`/`api_drive.varbit_server` are the C thunks
  `lua_drive_var_server`/`lua_drive_varbit_server`
  (src/plugin/torirs_plugin_drive_state.c:340-372), registered in
  `LUA_DRIVE_STATE_FNS` (:513-520) as `var_server`/`varbit_server`. They
  call `DriveState_VarpServer`/`DriveState_VarbitServer`
  (torirs_plugin_drive_state.c:92-146), which read
  **`app->varps.var_serv[varp_id]`** — the CLIENT's own mirrored record of
  the last server-confirmed value (kept in sync by
  `VarPManager_ApplySync/ApplySmall/ApplyLarge`), explicitly a *second,
  independent array* from `app->varps.var[]` (comment :103-106): "so this
  must be a second, independent read, never the same array GetVarp reads."
  It does **not** reach directly into the embedded server's
  `srv->active_player->varps[]` struct — it reads the client-side shadow of
  the server's last-sent value, which is why `QD.var.expect` (state.lua:113-136)
  compares client (`t.varp`/`t.varbit`) AND server-mirror
  (`t.var_server`/`t.varbit_server`) reads and calls it `refused` (not `ok`)
  on any mismatch between the two, or against the expected value.
