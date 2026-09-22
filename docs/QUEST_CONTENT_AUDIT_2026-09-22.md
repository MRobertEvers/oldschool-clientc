# Quest content audit — 2026-09-20..22 seam pass

**Question asked.** One seam call was wrong: an agent wanted Mort'ton rows in
`maps/multiway.csv` on the claim "Mort'ton is multicombat in OSRS". It is
single-way; the real answer was a test-only cheat. Were any of the *other*
content edits equally wrong — did any of them change real-game behaviour
(make a quest easier, remove a real gate, invent a mechanic, alter a dialogue
order OSRS has) instead of fixing a genuine port bug?

**Scope.** Every content change on `OSRS-Content` `lane-quest-driver` since
`48aa6cb5c710`, excluding the `selftest/quest_tests` evidence commits, plus
the four edits uncommitted on disk at the time of writing.

* `4420b02611` — "quests: WIP content seams for tier 1 (UNVERIFIED as a set)"
* `33fdbc8ee2` — "gertrude.rs2 puts the %fluffs ladder back in front"
* `033d83f61f` — "the swamp-paste cook row, Making History's carrier and ghost
  polarity, Throne of Miscellania's own completion write, the Royal Trouble
  gate, the oversized mes, and the four Prince Ali spawns"
* `ca329571b9` — "three npcs whose spawn row carries the multinpc BASE while
  the quest bound the CHILD"
* working tree — `twocats.rs2` + `atailoftwocats.constant`,
  `mend1_poison.rs2`, `quest_seaslug.rs2`
* also in range, not on the owner's list — `hans.rs2`/`hans.constant`,
  `tele_names.enum`/`tele_destinations.rs2`, and three test-only debugproc
  files

`maps/multiway.csv` was **never committed** and is **not** in the working tree.
Confirmed: `git log 48aa6cb5c710..HEAD -- osrs239-content/maps/multiway.csv`
is empty and `git status` lists four modified files, none of them it.

**Sources used, in order of authority.**

1. The 2004-era RS2 content this pack was ported from —
   `/Users/matthewevers/Documents/git_repos/LostCity_Content2` (to rev 254)
   and `/Users/matthewevers/Documents/git_repos/LostCity_Server/content`
   (to rev 289; has Throne of Miscellania, which Content2 does not).
   Quoted by absolute path below wherever it settled a row.
2. The wiki-pinned port audits in this repo, `docs/quests/<quest>.md`.
3. The osrs239 cache exports in the pack itself (`configs/all.npc`,
   `all.loc`, `all.obj`) — authoritative for *this cache's* shape, which is
   what several of the seams turn on.
4. My own OSRS knowledge, flagged as such and never used alone where 1–3
   could answer.

---

## 1. BEHAVIOUR CHANGE — the game does not do this

Three. One is already reverted; the other two are small and both have a known
correct value.

### B1. `gertrude.rs2` — A Tail of Two Cats' window tested before the `%fluffs` ladder — **ALREADY REVERTED**

*This is the one edit of the same class as the multiway.csv call, and it was
caught by the pass itself.*

`4420b02611` put

```
[opnpc1,gertrude]
if (%twocats_quest >= 20 & %twocats_quest <= 28) {
    ~gertrude_route_topics;
    return;
}
```

in front of the Gertrude's Cat ladder, with the banner reason *"That made
FINISHING Gertrude's Cat a prerequisite of A Tail's step 20, **which live OSRS
does not have**."*

**That claim is false.** A Tail of Two Cats requires Icthlarin's Little Helper,
which requires Gertrude's Cat. Live OSRS has exactly that prerequisite, through
the chain. The edit demoted a player's own in-progress Gertrude's Cat dialogue
to a "Something else" row and cost `test/quests/fluffs.lua` 12 rows
(53/53 → 41/16).

`33fdbc8ee2` reverted it, says so in as many words ("Ladder first, always"),
and re-proves Gertrude's Cat green (51/0, completion scroll photographed).
**Net across the whole range** the file gains only a `[label,gertrude_fluffs_talk]`
extraction and a defensive `if (%fluffs < ^fluffs_complete) @gertrude_fluffs_talk;`
at the head of `[label,gertrude_post_complete]` — no reachable behaviour change.

For the record, neither shape is 2004-authentic: LostCity's
`scripts/areas/area_varrock/scripts/gertrude.rs2` is a flat
`switch_int (%fluffs)` with **no default arm** and no other quest branching in
her handler at all. The `else` → `~gertrude_route_topics` door is this port's
own addition.

**Recommended action:** none — closed. Worth keeping as the pass's own worked
example of the failure mode.

### B2. `cooking_generic.dbrow` — the swamp-paste row's player-facing strings and `burnt` field are invented, and the authentic ones exist

The row itself is a genuine port fix (see table). Its *numbers* are right.
But three strings and one field were authored from a wiki paraphrase when the
2004 original is in the reference tree, byte-for-byte:

`/Users/matthewevers/Documents/git_repos/LostCity_Content2/scripts/skill_cooking/configs/cooking_source/cooking_generic.dbrow:505`

```
[cooking_generic_raw_swamp_paste]
data=uncooked,rawswamppaste
data=cooked,swamppaste
data=burnt,swamppaste
data=levelrequired,1
data=experience,20
data=successchance,500,500
data=cantcookmessage_range,You need to warm that over a fire.
data=cookmessage_fire,You warm the paste over the fire.
data=successmessage,It thickens into a sticky goo.
```

The seam wrote `burnt,null`, `successchance,1,1`, `cantcookmessage_range,You
need an open fire to cook that.` and left `cookmessage_fire`/`successmessage`
to `~attempt_cook`'s defaults, which render *"You cook the swamp paste on the
fire..."* / *"The swamp paste is now nicely cooked."*

Its banner also states a mechanic that is not the game's: *"a burn discards
the tar rather than handing back a burnt item, so there is no `burnt` obj to
name."* The original names one — `burnt=swamppaste`, i.e. a burn hands back an
ordinary swamp paste with no XP. Inert either way (the roll always passes), but
the reasoning is wrong and will be cited again.

**What a player notices:** three wrong sentences every time swamp paste is
made — on the Sea Slug boat repair, Cabin Fever, In Aid of the Myreque,
Dragon Slayer II and the Slug Menace.

**Recommended action:** replace the four lines with the LostCity values above
(`burnt,swamppaste`; `successchance,500,500`; the two `message` lines; the
authentic `cantcookmessage_range`) and delete the "discards the tar" sentence
from the banner.

### B3. `tearsofguthix.rs2` / `dttd_savezanik.rs2` — the chathead bind spawns a **world-visible** duplicate NPC

`[proc,tog_bind_speaker]` (new) and `[proc,dttd_zanik_say]` (new) do:

```
if (npc_find(coord, $type, 12, 0) = false) {
    npc_add(movecoord(coord, 1, 0, 0), $type, 200);
    npc_setmode(playerface);
}
```

There is no `npc_setowner`. A second Juna, and a Zanik, therefore stand in the
world beside the player for 200 ticks (two minutes), **visible to every other
player in the area**. Real OSRS has exactly one Juna in that cave and no
duplicate of her or of Zanik.

The live-content precedent the banner leans on does scope it: the Hazeel Cult
arrest scene (`quest_hazeelcult_locs.rs2:188-206`) calls `npc_setowner` after
every one of its three `npc_add`s. The precedent that does *not* —
`dttd_bmp.rs2`'s `[proc,dttdbmp_bind]` — is inside a `[debugproc,...]` harness
file, i.e. a test idiom, not content.

**What a player notices:** a duplicate Juna/Zanik popping into the cave beside
another player who is mid-dialogue, and lingering there.

**Recommended action:** add `npc_setowner;` after `npc_setmode(playerface);`
in `[proc,tog_bind_speaker]` (tearsofguthix.rs2) — `[proc,dttd_zanik_say]`
routes through it. Consider the same line in `dttd_bmp.rs2`'s copy.

---

## 2. UNSURE — what would settle each

### U1. `hans.rs2` / `hans.constant` — Hans is deleted 10 ticks after he flees

Commit `b6abee6696`. The "I have come to kill everyone in this castle!" branch
now arms `npc_queue(5, 0, ^hans_flee_ticks)` and `[ai_queue5,hans]` runs
`npc_setrespawn(25); npc_del;`. The banner's justification is a bare assertion:
*"OldSchool's Hans runs out of sight and is `not there` — the patrolling one
you meet later is a respawn."*

LostCity's 2004 Hans only sets `playerescape` and stops there — there is no
`npc_del`, no respawn arm. So this is a departure from the reference, on an
unsourced claim about live OSRS. It is harmless either way, but it is exactly
the shape of the multiway call: a mechanic added on "the game does this",
with no citation.

**Settles it:** the OSRS wiki's Hans page / a live observation of whether a
fled Hans despawns or just parks at the leash. If he only parks, delete the
queue and the two constants.

### U2. `twocats.rs2` — the potato growth timer is online-only

The 4 → 8 transition genuinely did not exist (`docs/quests/a_tail_of_two_cats.md:320`
says so: *"no timer writes 5-8"*), so state 40 was terminal and the seam is a
real fix. But the shape is not the game's: a `softtimer` at
`^twocats_potato_stage_ticks = 500` × 4 stages = 20 minutes **of logged-in
time**, where the wiki gives 15–35 minutes **across relog**
(`a_tail_of_two_cats.md:330` is already a P1 row for exactly this). The fixer
discloses the gap in the code and in its report.

The `[debugproc,twocats_growpotatoes]` beside it is the right shape and worth
keeping: it calls the *real* `[proc,twocats_potato_advance]` once per stage, so
the ledger row proves the growth logic and only the waiting is skipped. That is
the Mort'ton lesson applied correctly.

**Settles it:** an owner decision — accept the online-only approximation with
the P1 row left open, or build the persisted `date_minutes` deadline carrier
`farming_growth.rs2` already models. Do not leave the softtimer and *close*
the P1 row.

### U3. `pryingtimes_locs.rs2` — the two crates are world scenery placed by a conversation

`~pry_ensure_crates` `loc_add`s the sea crate at `%quest_pry >= ^pry_test_key`
and the bar crate at `>= ^pry_deliver`, called from `[label,pry_steve_talk]`.
In the real game both are map scenery that always exists. Here they blink into
being mid-conversation, and `loc_add` is **world** state, so one player's
conversation places them for everyone and a world restart removes them until
somebody talks to Steve again.

Nothing is made easier by it and nothing is gated on it, so this is a
port-bug fix with a caveat rather than a behaviour change — but it is a
divergence the owner should know about.

**Settles it:** whether this pack ever intends to add `maps/m*.jl2` `==== LOC ====`
rows (QUEST_AUTHORING trap 20 says there is no `.loc` placement mirror). If not,
consider placing them from a login/zone hook rather than a dialogue.

### U4. `reldo.rs2` — the two moved guest windows now sit above the Defender of Varrock branches

`ca329571b9` moved the Giant Dwarf and A Tail of Two Cats branches out of the
dead `[opnpc1,reldo_normal]` into the live `[opnpc1,reldo]` — correct, and the
reason is verified (see table). But they were placed **first**, above the
pre-existing `%dov` ladder. A player inside both an old window and a DoV window
now gets a different line than before.

Real Reldo answers with a `~p_choice*` menu carrying every live topic; this
port uses first-match ladders throughout, so the ordering is a port convention,
not a bug. Windows are narrow and no overlap was demonstrated.

**Settles it:** whether the owner wants guest topics folded into the
`~p_choice*` menu (matching the game) or is content with first-match. Same
question applies to `gertrude.rs2` and `holgart.rs2`.

### U5. `quest_seaslug.rs2` — the crane radius is 5 where the loc's real reach is 4

The old `coordz(coord) < coordz(movecoord(loc_coord, 0, 0, 3))` gate was a
**side** test wearing a proximity message, and it refused every tile a player
can stand on (measured: footprint x 2770-2773 / z 3287-3290 on server plane 1,
only walkable deck is z 3286 and one tile at z 3287). Replacing it is right.

But the fixer states plainly why it chose 5 and not 4: *"I sized the new radius
at 5 rather than the tight 4 specifically so that
`torirs_server_world_selftest.c`'s seaslug stanza stays green without my
editing an engine file this seam did not assign me."* That stanza teleports the
player to **2770,3292 level 1 — open sea, two tiles north of the footprint,
unreachable by any player** — a tile that only exists in the test because the
broken gate accepted nothing else.

So one tile of the accepted radius exists to satisfy a test that encodes the
bug. That is the shape CLAUDE.md warns about ("do not write tests that pin
silent-failure behaviour").

**Settles it:** move the selftest teleport to `2772,3286` level 1 (proved
walkable) and tighten the gate to `distance(coord, loc_coord) > 4`, which is
the exact Chebyshev reach of any tile touching a 4×4 from its south-west
corner.

### U6. `quest_haunted.rs2` — the non-wall door mirror is not clamped

`p_teleport(movecoord($loc_coord, $dx, 0, $dz))` with
`$dx = loc.x - player.x`. From an orthogonally adjacent tile this is the
correct mirror. From a two-tile stand-off it lands the player **two tiles past
the gate**. `p_oploc` should make that unreachable, but the branch does not say
so.

The open/shut gate is intact — `[label,ernest_open_maze_door]`'s
`testbit(%ernestdoors, $bit)` / *"The door is firmly shut."* runs before
`~ernest_walk_through_door` is ever called, so the lever puzzle is not
weakened. That is the part that mattered and it is fine.

**Settles it:** clamp `$dx`/`$dz` to ±1, or assert adjacency.

### U7. Making History — both ghosts now vanish for good at the end

The polarity inversion itself is correct and verified from the cache (see
table). What it *enables* is the port's own pre-existing narration: after the
fix, `%makinghistory_droalak_pres`/`_melina_pres` are set at the two "fades
away" lines, so both ghosts leave Port Phasmatys permanently. The quest's own
header admits *"No LostCity or 2009scape implementation"*, and neither
reference tree has one to check against.

**Settles it:** the wiki's Droalak and Melina pages — do they state a
post-quest location? If the ghosts remain in OSRS, the two `= ^true` writes
should go and only the `= ^false` re-assert at Jorral's offer should stay.

### U8. Mourning's End Part I — the whole leg the two seams touch is a reconstruction

`mend1.constant`'s own header says it: no LostCity script, no 2009scape
implementation, *"dialogue authored below is original wording covering the same
beats"*, and there is **no** `docs/quests/mournings_end_part_i.md` in this repo
to pin it. The caged gnome, the toad-signal device, the dyed sheep and the
apple-press toxin are this pack's reconstruction of the quest.

Both seams on it (`mend1_gnome.rs2`'s gate + stage write, `mend1_poison.rs2`'s
`[oplocu,...]` triggers) are internally consistent with the quest's own journal
and constants — I verified each against `mend1_journal.rs2` and `mend1.constant`
and they line up exactly. So they are correct *fixes to this reconstruction*.
Whether the reconstruction matches OSRS is a larger question this diff cannot
answer.

**Settles it:** a wiki-pinned `docs/quests/mournings_end_part_i.md` written the
way the other 69 were. Note the fixer already filed the quest's next real
blocker: `[oploc1,carnilleanrange]` names a loc that is not placed anywhere in
the world (six anchors probed at radius 60, all `not_found`).

---

## 3. The full table

Verdicts: **FIX** = PORT-BUG FIX (restores what the game does) ·
**CHANGE** = BEHAVIOUR CHANGE · **UNSURE** · **INFRA** = test/debug only,
no game behaviour.

| File | Commit | Change | What OSRS / the reference does | Source & confidence | Verdict | Recommended action |
| --- | --- | --- | --- | --- | --- | --- |
| `areas/varrock/scripts/gertrude.rs2` | `4420b02611` | A Tail's window (`%twocats_quest` 20..28) tested **before** the `%fluffs` ladder, on "live OSRS does not have [Gertrude's Cat as a prerequisite]" | It does, through the chain: A Tail → Icthlarin's Little Helper → Gertrude's Cat. `[gertrude]` is `multivarp=fluffs`, multinpc7 = `gertrude_post` at `%fluffs` 6, and twocats.rs2's own trigger is `[opnpc1,gertrude_post]` | `configs/all.npc`; commit `33fdbc8ee2`'s own analysis; OSRS quest chain (high) | **CHANGE — already reverted** | None. Closed by `33fdbc8ee2`. Keep as the pass's worked example |
| `areas/varrock/scripts/gertrude.rs2` | `33fdbc8ee2` | Reverts the above; extracts `[label,gertrude_fluffs_talk]`; adds a `%fluffs < ^fluffs_complete` redirect at the head of `[label,gertrude_post_complete]` | LostCity's Gertrude is a flat `switch_int(%fluffs)` with no default arm; the `else` → topics door is this port's own | `LostCity_Content2/scripts/areas/area_varrock/scripts/gertrude.rs2` (high) | **FIX** (net: no reachable change) | None |
| `areas/world/configs/m40_51.spawn` | `4420b02611` | Ground OBJ ×3 at 2618,3323/3324/3325 changed `pigeoncage` → `pigeons` | **Exact match.** `LostCity_Content2/maps/m40_51.jm2` `==== OBJ ====` has `0 58 59: 424 / 0 58 60: 424 / 0 58 61: 424`, and `pack/obj.pack` `424=pigeons`. `pigeoncage` (425) has **no** ground spawn anywhere. `pigeons` = "It's full of pigeons", `iop1=Open`; `[opheld1,pigeons]` swaps it for the empty `pigeoncage` | LostCity map + obj pack + `quest_biohazard.rs2:63` (**highest**) | **FIX** | None. This restores the authentic rows byte-for-byte |
| `quest_betweenarock/scripts/betweenarock_dondakan.rs2` | `4420b02611` | Helmet check reads `worn` as well as `inv` | Equipping moves the only copy out of `inv`; the `worn` guard below and `betweenarock_realm.rs2:40/:65` both demand it equipped, so the `inv`-only test swallowed the equipped case and `^dwarfrock_in_the_realm` had no writer | Internal consistency, three call sites (high) | **FIX** | None |
| `quest_currentaffairs/configs/currentaffairs.spawn` (new) | `4420b02611` | Places `current_affairs_councillor` at 2825,3454,0 | The repo's own wiki-pinned audit prescribes exactly this: `docs/quests/current_affairs.md:450` — *"P0 \| Councillor Catherine has no server spawn \| Organic route stops immediately at state 5 \| Add/verify generated spawn at (2825,3454,0)"*. Matches `^ca_councillor_coord = 0_44_53_9_62` | `docs/quests/current_affairs.md` (high) | **FIX** | None |
| `quest_deathtothedorgeshuun/scripts/dttd_savezanik.rs2` | `4420b02611` | Juna/Zanik lines routed through `~tog_juna` / new `[proc,dttd_zanik_say]` so the chathead has a bound npc. Dialogue text unchanged | `~chatnpc_specific` reads `npc_coord`/`npc_facesquare` after mounting the page; from a loc trigger there is no active npc and the script aborts with the page on screen and no resume armed | `interface_chat/scripts/chat.rs2:108-118` (high) | **FIX**, with **CHANGE** in the bind — see **B3** | Add `npc_setowner` (B3) |
| `quest_haunted/scripts/quest_haunted.rs2` | `4420b02611` | Non-wall maze doors (`shape != wall_*`) step the player across the gate's tile instead of calling `~door_open`, which `error()`s on shape 10 | Ernest the Chicken's Draynor Manor maze. The lever gate is untouched: `testbit(%ernestdoors, $bit)` / "The door is firmly shut." runs first | `doors/scripts/door_procs.rs2:49`; read the whole trigger chain (high) | **FIX** (mirror unclamped — see **U6**) | Clamp `$dx`/`$dz` to ±1 |
| `quest_hero/scripts/brimhaven_scarface_mansion.rs2` | `4420b02611` | Candle-chest stage write gated on the Black Arm band (`>= ^hero_blackarm_gangmember_spoken`) instead of the route-blind `%heroquest < 12` | LostCity's chest is **not** gated and writes 12 unconditionally — **but** its treasure door refuses a Phoenix player outright (`%heroquest < ^hero_blackarm_id_papers_given` → "The key doesn't fit."), so the write never fires on that route. **This port already softened that door** (its own comment: *"let either gang's own solo checkpoint unlock the door"*) to match solo OSRS, which is what exposed the write. Phoenix at 5 was being bumped to 12, past Straven's `^hero_phoenix_obtained_armband` gate, and Katrine refuses a Phoenix player — quest unfinishable | `LostCity_Content2/.../brimhaven_scarface_mansion.rs2:74-93` + `straven.rs2:97` + `katrine.rs2:15`; measured in `seam_hero_before/ledger.tsv` (**high**) | **FIX** | None. Departs from LostCity deliberately and correctly, because the door already did |
| `quest_mourningsendparti/configs/mend1.varp` (new) | `4420b02611` | Declares `[mourning_quest]` `transmit=yes protect=no scope=perm` | `all.varp`'s entry is a bare name reservation with an empty body; an undeclared/no-transmit varp is server-only, so the stage never reached the client (`::mend1run` printed complete=9 while `var.varp` read 0). LostCity declares its quest carriers the same way (`quest_misc.varp` → `transmit=yes scope=perm`) | `LostCity_Server/content/.../quest_misc.varp`; `carrier_before` ledger (high) | **FIX** | None |
| `quest_mourningsendparti/scripts/mend1_gnome.rs2` | `4420b02611` | Cage entry gate `^mend1_gnome_task`(5) → `^mend1_assignment`(4); adds `%mourning_quest = ^mend1_gnome_task` at the weakness discovery | Internally proved: `mend1_journal.rs2:16` at stage 4 reads *"Essyllt gave me a key and a broken device — I should get the caged gnome talking"*, and `:20` at stage 5 reads *"I'm working on the caged gnome"*. Nothing but a debugproc wrote 5, so stages 4→5 had no rung. No stage is skipped | `mend1.constant:180-188` + `mend1_journal.rs2` (high, **for this reconstruction** — see **U8**) | **FIX** | None; see U8 for the leg's fidelity |
| `quest_mourningsendpartii/configs/mend2.varp` (new) | `4420b02611` | Declares `[mourning_quest_part2]` transmitting | Same class: the carrier holds `mourning_quest_main` (bits 0-7) and ~10 more varbits, every one reading 0 on the client | `carrier_before` ledger (high) | **FIX** | None |
| `quest_pryingtimes/scripts/pryingtimes.rs2` + `pryingtimes_locs.rs2` | `4420b02611` | `[proc,pry_ensure_crates]` `loc_add`s the sea crate (`>= ^pry_test_key`) and the bar crate (`>= ^pry_deliver`), called on entry and at the two stage writes | Neither crate is placed anywhere; the coords are the quest's own constants. Real scenery is map data | Pack has no `.loc` placement mirror (trap 20) (high) | **FIX**, with a caveat — see **U3** | Decide map rows vs a login hook |
| `quest_tearsofguthix/scripts/tearsofguthix.rs2` | `4420b02611` | `[proc,tog_bind_speaker]` binds/spawns the chathead npc before `~chatnpc_specific` | Same abort as DttD. Juna is reached through `[oploc1,tog_juna]` (a loc, no active npc), and the one static `tog_juna_dummy` stands on the wrong plane | `chat.rs2:108-118`; `m50_148.spawn` (high) | **FIX**, with **CHANGE** in the bind — see **B3** | Add `npc_setowner` |
| `pack/dbrow.alloc` + `skill_cooking/configs/cooking_generic.dbrow` | `033d83f61f` | New `[cooking_generic_swamp_paste]` row: level 1, exp 20, fire-only | **Numbers exact.** LostCity's `[cooking_generic_raw_swamp_paste]`: `levelrequired,1`, `experience,20`, `cantcookmessage_range` set (fire-only). Recipe confirmed: `swamp_tar` + `pot_flour` → `rawswamppaste` → cook on a fire. Without the row `db_find` returned null and `swamppaste` could not exist | `LostCity_Content2/.../cooking_generic.dbrow:505` + `quest_seaslug.rs2:11` (**highest**) | **FIX** + **CHANGE** in 4 fields — see **B2** | Take the authentic `burnt`, `successchance` and three message strings |
| `quest_makinghistory/configs/quest_makinghistory.varp` (new) | `033d83f61f` | Declares `[makinghistory]` transmitting | Same carrier class as mend1/mend2 and LostCity's own quest varps. Every bit of this quest's state is a varbit on it | `all.varp:1221` empty body (high) | **FIX** | None |
| `quest_makinghistory/scripts/makinghistory_{ghost,jorral}.rs2` + `.constant` | `033d83f61f` | `_pres` bit polarity inverted at three writes; `.constant` renamed to FADED | Verified from the cache: `[makinghistory_droalak_multi]`/`[makinghistory_melina_multi]` are `multinpc1=<ghost>, multinpc2=-1`, and **multinpc1 is value 0**. So 0 shows the ghost and 1 hides it — the bit means "faded", not "present". Written the old way, Jorral's offer hid Droalak the instant the carrier started transmitting | `configs/all.npc:168418/168429`; `m57_54.spawn:13,35`; multinpc value-indexing rule (**high**) | **FIX** | None for the polarity; see **U7** for whether the ghosts should vanish at all |
| `quest_misc/scripts/misc_king_vargas.rs2` | `033d83f61f` | Adds `%misc_quest = ^misc_complete;` at `[label,vargas_finish_quest]` | **Confirmed by the reference.** `LostCity_Server/content/.../king_vargas.rs2:172-180` does exactly this at the crowning — `%misc_quest = ^misc_complete;` (100, `general/configs/quest.constant:43`) plus the coffers seed. Without it every `= ^misc_complete` reader here (`vargas_post_quest`, `misc_door_guard.rs2:51`, `misc_journal.rs2:134`'s "Stage 100" branch, the approval dialogue) was dead code | `LostCity_Server/content/.../king_vargas.rs2` (**highest**) | **FIX** | Optional: the reference guards it `if (%misc_quest = 90)`. Low risk here since the label is only reached past the support/treaty checks |
| `quest_royaltrouble/{configs/royaltrouble.constant, scripts/royal_shared.rs2, royal_journal.rs2, royal_debug.rs2}` | `033d83f61f` | `~royaltrouble_relevant` and the journal gate `^misc_king_signed_treaty`(90) → `^misc_complete`(100) | Royal Trouble's real prerequisite is Throne of Miscellania **complete**, and ToM's completion value is 100 (proved by the row above). At 90 the branch sat above quest_misc's own switch in both trigger files and hijacked King Vargas and Advisor Ghrim ten stages early, so `%misc_quest` could never reach 100 | Same reference + OSRS requirement (high) | **FIX** | None |
| `quest_mourningsendpartii/scripts/mend2_shared.rs2` | `033d83f61f` | Three `mes()` strings split; one was 300 bytes | A `var-u8` MESSAGE_GAME cannot carry >252 bytes — the length byte wraps and takes the session. Two more were over the client's 200-char display cap and rendered cut mid-word. Text substance unchanged | Wire format; measured (high) | **FIX** | None. Cosmetic only: prose the game would show as one line now shows as two |
| `quest_prince/configs/quest_prince.spawn` (new) | `033d83f61f` | Places `hassan` 3302,3163 · `joe` 3123,3245 · `prince_ali_prison` 3123,3242 · `lady_keli` 3128,3244 | **All four exact.** `LostCity_Content2/maps/m51_49.jm2` `0 38 27: 923` → hassan (3302,3163); `maps/m48_50.jm2` `0 51 45: 916` → joe (3123,3245), `0 51 42: 920` → prince_ali_prison (3123,3242), `0 56 44: 919` → lady_keli (3128,3244). The same file's `0 22 44: 924` → osman (3286,3180) is where this pack already put `contact_osman_multi`, confirming the tile source. No duplicate rows exist in this tree | `LostCity_Content2/maps/*.jm2` + `pack/npc.pack` (**highest**) | **FIX** | None |
| `areas/alkharid/scripts/osman.rs2` + `quest_contact/scripts/contact_osman.rs2` | `ca329571b9` | `[opnpc1,osman]`'s body extracted to `[label,osman_talk]`; Contact!'s wrapper hands the turn back to it below `^contact_met_maisa`, replacing the literal "Osman has business to attend to." | Verified: `m51_49.spawn:26` places `contact_osman_multi`, and **no spawn row anywhere carries `osman`**. The npc op lookup keys on the spawned type, so Prince Ali Rescue's whole Osman ladder was dead code and `%princequest` could not leave `^prince_not_started`. In OSRS Osman gives the Prince Ali instructions and only later belongs to Contact! | `grep` over every `.spawn` in the tree (**high**) | **FIX** | Delete `test/quests/prince.lua`'s `osman.gated_by_contact` row — it pins the port's placeholder refusal (CLAUDE.md: do not keep tests that pin silent-failure behaviour) |
| `areas/varrock/scripts/reldo.rs2` + `quest_atailoftwocats/scripts/twocats.rs2` | `ca329571b9` | Giant Dwarf and A Tail windows moved whole from the dead `[opnpc1,reldo_normal]` into the live `[opnpc1,reldo]`; `[proc,twocats_reldo_talk]` added | Verified: `m50_54.spawn:13` places the base `reldo`; `[reldo]` is `multivarbit=twocats_reldo` with children `reldo_normal`/`reldo_withbook`, so the child is not even stable across A Tail's own step. Branches moved, not rewritten | `grep` over every `.spawn`; `configs/all.npc` (**high**) | **FIX** | See **U4** on ordering |
| `quest_theslugmenace/scripts/slugmenace_pages.rs2` | `ca329571b9` | `[opnpc1/opnpc3,slug2_holgart_jeb]` hand `%slug2_npc_track1 = 0` to `@holgartplatform_talk`; Jeb keeps the turn at 1 | Verified: `m43_51.spawn:25` places the wrapper `slug2_holgart_jeb` (`multivarbit=slug2_npc_track1`, multinpc1 = `holgartplatform`, multinpc2 = `slug2_jeb_stage2`), and no row carries `holgartplatform`, so Sea Slug's platform leg dead-ended on the literal "Mind the slugs." **The new `[opnpc3]` is not a new behaviour:** `holgart.rs2:17-18` already routes both op1 and op3 to the same `@holgartplatform_talk` | `configs/all.npc:171096`; `holgart.rs2` (**high**) | **FIX** | None |
| `quest_atailoftwocats/scripts/twocats.rs2` | *uncommitted* | Rake+plant merged into one `[oplocu,twocats_patch]` on `last_useitem`; `[oploc2,twocats_bed]` → `[oploc1,...]`; `[opnpc2,twocats_unferth*]` → `[opnpcu,...]`; the "three rakes" planting guard replaced with a dibber/seed check | **The repo's wiki-pinned note says all three in as many words** (`docs/quests/a_tail_of_two_cats.md:320-324`): *"Rake is registered as `oploc2` although patch has no op2"*; *"Click cache op1 `Make` on unmade bed \| Registered as `oploc2`; handler cannot be reached from the cache menu"*; *"Registered as nonexistent NPC op2 on long-hair only, while real action is item-on-NPC"*. Cache agrees: no `twocats_patch` child publishes an op, `[twocats_bed_unmade]` carries `op1=Make` only, every `twocats_unferth*` carries `op1=Talk-to` only. Correctly fixed in the script, **not** by inventing op strings in the cachepack export | `docs/quests/a_tail_of_two_cats.md` + `configs/all.loc`/`all.npc` (**high**) | **FIX** | Rewrite `test/quests/atailoftwocats.lua` (the committed file still names the old trigger ops); `build/atailoftwocats_seam_copy.lua` is ready to lift. Note the port still enforces a chore ORDER the wiki says is free — pre-existing, not this edit |
| `quest_atailoftwocats/scripts/twocats.rs2` + `configs/atailoftwocats.constant` | *uncommitted* | `[proc,twocats_potato_advance]` + `[softtimer,twocats_potato_grow]` step tidygarden 4→8 at 500 ticks/stage; `^twocats_potato_stage_ticks = 500`; re-plant guard; `[debugproc,twocats_growpotatoes]` | The mechanic is real (`a_tail_of_two_cats.md:320`: *"progress potato stages 4-8 over 15-35 minutes across relog … no timer writes 5-8"*) and state 40 was terminal without it. The implementation is not the game's — online-only, no relog carry | `docs/quests/a_tail_of_two_cats.md` (high for the defect, see **U2** for the shape) | **FIX** (shape **UNSURE**) | Keep the debugproc — it runs the real advance body, which is the correct fast-forward idiom. Decide U2 |
| `quest_mourningsendparti/scripts/mend1_poison.rs2` | *uncommitted* | `[oplocu,...]` triggers added beside the existing `[oploc1,...]` on the apple press and both food stores, keyed on `last_useitem`; bodies extracted to labels | Quest Helper's own step text is *"Use the rotten apples on the apple press"* / *"Use the toxic powder on the food store"*, and none of the three locs carries an op string in the cache. **The config fix was tried first and measured inert** (`all.loc` is a cachepack export; the client builds its menu from the frozen `cache.osrs239`, so an added `op1=` changed nothing — before/after probes byte-identical). The `%mourning_quest != ^mend1_poison_task` gates are preserved on both stores | `MourningsEndPartI.java:440/456/459`; `seam_mourning_probe_before` vs `_probe_locop` (**high**) | **FIX** | None. Worth landing the doc correction the fixer proposes: QUEST_AUTHORING §8 currently sends authors to `all.loc`, which cannot work in this lane |
| `quest_seaslug/scripts/quest_seaslug.rs2` | *uncommitted* | Crane gate `coordz(coord) < coordz(movecoord(loc_coord,0,0,3))` → `distance(coord, loc_coord) > 5` | The old test was a **side** test (the doors/ladders idiom) wearing a proximity message, and refused every tile a player can stand on — measured per-tile against the map data. A proximity test is right | Geometry measured from `maps/m43_51.jl2:142` + jm2 collision (**high**) | **FIX** | Tighten to 4 and fix the selftest's open-sea tile — see **U5** |
| `areas/lumbridge/scripts/hans.rs2` + `configs/hans.constant` (new) | `b6abee6696` | Fleeing Hans is `npc_del`'d after 10 ticks and respawns after 25 | LostCity's 2004 Hans only sets `playerescape` — no delete, no respawn arm. The claim about live OSRS is unsourced | `LostCity_Content2` / `LostCity_Server` `hans.rs2` (high that it departs; unknown whether OSRS agrees) | **UNSURE** — see **U1** | Cite it or drop it |
| `general/configs/tele_names.enum` + `general/scripts/misc/tele_destinations.rs2` | `1804eed897` | `::tele` destination table regenerated; Lumbridge is the respawn tile; a handful of destinations added/removed | Debug teleport table. No player-reachable behaviour | Read the diff (high) | **INFRA** | None |
| `areas/lumbridge/scripts/hans_test_constants.rs2`, `interface_chat/scripts/chat_test_prompts.rs2`, `quest_cook/scripts/quest_cook_test_ingredients.rs2` (new) | `c697fc387d`, `c4a40bbb77`, `b6abee6696` | Three `[debugproc,...]`-only files: report Hans' constants, open a real count/name prompt, put the three Cook's Assistant ingredients in the backpack | Debugprocs are unreachable without a cheat. Each is the correct shape — the prompt one opens a **real** `p_countdialog`/`p_namedialog` rather than simulating it, and the Cook one deliberately avoids the existing `::cookbmp_handin` bypass so the test drives a real click | Read all three (high) | **INFRA** | None. This is the Mort'ton lesson applied correctly: the cheat goes in a debugproc, not in the world |

---

## 4. Summary

* **28 content edits reviewed** across four seam commits, four uncommitted
  files and five in-range extras.
* **One edit was wrong in the same way the multiway.csv call was wrong** — the
  `gertrude.rs2` reordering in `4420b02611`, justified by a false claim about
  live OSRS. **The pass caught it itself** and reverted it in `33fdbc8ee2`
  with the correct reading written down. Net effect on the tree: none.
* **Two small behaviour changes stand** and both have a known correct value:
  the swamp-paste row's invented strings (**B2** — the authentic ones are in
  LostCity) and the world-visible chathead bind (**B3** — one missing
  `npc_setowner`).
* **Everything else is a genuine port bug.** Four of them are confirmed
  *byte-for-byte* against the 2004 reference: the pigeon-cage ground spawns,
  all four Prince Ali npc tiles, Throne of Miscellania's missing completion
  write, and the swamp-paste row's level/XP/fire-only.
* **No edit removed a real gate or made a quest easier.** The only two gates
  that were loosened were provably unsatisfiable before (the Sea Slug crane's
  side-test-as-proximity-test; Mourning's End's cage rung with no writer), and
  one edit *added* a gate that was missing (the Heroes' Quest candle chest).
* **Eight rows are UNSURE**, all of them "the fix is right, the shape or the
  fidelity of the surrounding content is unverified" rather than "this may be
  wrong". **U1** (Hans) and **U5** (crane radius sized to keep a bug-shaped
  selftest green) are the two worth acting on first.
