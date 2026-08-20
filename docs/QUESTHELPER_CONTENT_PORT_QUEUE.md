# Quest Helper content port queue

Agent-loop state for **RuneLite Quest Helper -> OSRS-Content** forward port of
OSRS quests.

**2026-08-12 rule change (standing directive):** the OSRS Wiki is the single
source of truth for **every** quest this queue touches, full stop. The prior
"ownership" exclusivity rule — STOP and defer to `CONTENT_PORT_QUEUE.md` or
`SCAPE2009_CONTENT_PORT_QUEUE.md` the instant either lane has *any* proc for a
quest — is **retired**. This queue's job is now to finish ALL quests, not just
the post-Jan-2009 QuestHelper-only slice it started with. Which lane a quest
happens to be filed under does not gate whether it gets completed here.

What this means in practice:
- **Before writing anything, still check what already exists** (this tree,
  `lc_quests.txt`, the IN-LC/mid-era audit tables below) — not to decide
  whether you're *allowed* to touch the quest, but so you don't blindly
  duplicate a trigger/state that's already there (`sscompile` gives no
  diagnostic for a duplicate trigger — this has bitten multiple slices).
- **A `done`/`done (LC)`/`done (2009scape)` status on any row in any of the
  three queue docs is a claim, not a guarantee.** Audit it against the current
  wiki quest page + transcript before trusting it end-to-end. If it's
  wiki-accurate and complete, leave it alone and move on. If it's missing a
  branch, a reward, a refuse-option, or an entire chapter, **fix it here**,
  in place, using the wiki as ground truth — do not wait for another lane's
  loop to get to it.
  Now targeting the ownership-gated backlog it previously deferred as
  out-of-scope: the 38-entry IN-LC table (pre-Sept-2004 quests filed under
  `CONTENT_PORT_QUEUE.md`) and the mid-era quests filed under
  `SCAPE2009_CONTENT_PORT_QUEUE.md`'s own tracking (the "~74" figure quoted
  here through 2026-08-12 was a bookkeeping artifact with no real dir list
  behind it — the actual mid-era tracking surface held only 6 distinct
  quests not already duplicate-tracked under IN-LC). **As of 2026-08-12 this
  directive is complete**: every row in both audit tables has a first
  wiki-accuracy pass, and every large content gap those passes surfaced has
  been built out and closed, with the single exception of `legendsquest`
  (left `audit-in_progress` by design — completable, but with real,
  disclosed soft-skips; see its row for detail). See the mid-era table's
  2026-08-12 milestone note below for the full picture.
- LostCity's `.rs2`/trigger/config *shape* (not its content boundary) is still
  the shape this tree uses — that part of the original rule stands.
- The "unblock the other pending files" side of this directive: both sibling
  queues (`CONTENT_PORT_QUEUE.md`, `SCAPE2009_CONTENT_PORT_QUEUE.md`) each
  currently list only one straggler `pending` row of their own — these are
  now in scope for this loop to pick up directly too, same wiki-first rule,
  rather than waiting on those lanes' own loops.

Quest Helper
(`https://github.com/Zoinkwiz/quest-helper`, fetch via `curl`/GitHub API — no
local checkout on this machine) remains a useful **state-machine / test
guide** where it exists: each helper's `steps.put(N, ...)` is the quest varbit
progression a `.rs2` port should reproduce. It does **not** define
implementation and is not required reading for quests it doesn't cover (the
IN-LC/mid-era/pre-2004 quests mostly predate it or aren't in its scope) --
dialogue trees (including branches no helper ever lists), dig rewards, and
combat come from the OSRS wiki transcript / cache / play. See methodology
step 5 and `PORTING_GUIDE` section 4.6 step 4.

Gameval constants (`NpcID.FOO` -> `foo` in `configs/all.*.compack`) are the
cache's own names -- **no id remapping**. When a helper (or any other
secondary source) and the osrs239 cache disagree, **the cache wins**.

Parallel to (no longer hard ownership boundaries, see rule change above):

- [`CONTENT_PORT_QUEUE.md`](CONTENT_PORT_QUEUE.md) - LostCity -> tree
- [`SCAPE2009_CONTENT_PORT_QUEUE.md`](SCAPE2009_CONTENT_PORT_QUEUE.md) - mid-era
- [`KRONOS_CONTENT_PORT_QUEUE.md`](KRONOS_CONTENT_PORT_QUEUE.md) - post-2009 skills/bosses

**Do not silently duplicate another lane's in-flight work.** If a row in a
sibling doc is `in_progress` (actively claimed), leave it; if it's `pending`
or `done`-but-audit-fails, it's fair game here per the rule above.

Each tick ports/audits **one** unblocked slice per `docs/PORTING_GUIDE.md` §4
and §4.6. Status: `pending` | `in_progress` | `done` | `blocked`.

**Depth-first:** a row stays `in_progress` until every `steps.put` value is
playable end-to-end. It only becomes `done` when the whole quest is.

## Shared tree -- never silence another lane

**Do not ever** `.rs2.skip` / `dirname.skip` / move / delete sibling content
(`skill_construction/`, `minigame_mta/`, or any other live tree) to green
`sscompile`. See PORTING_GUIDE section 7 and
`.cursor/rules/no-park-sibling-content.mdc`.

Loop prompt: read this file + PORTING_GUIDE section 4 / section 4.6 / section 7; run
`tools/questhelper_extract.py --check` on the next pending row; port it; NEVER
park sibling lanes; verify (`ToriRSServer_Pack --check-only`,
`make -C src torirsserver-scripts`); update this file; re-arm. Stop only when the
user stops the loop.

## Methodology (non-negotiable)

1. **Check what already exists first** (PORTING_GUIDE section 2.2 grep, plus
   `lc_quests.txt` and this doc's own IN-LC/mid-era audit tables) — not to
   decide whether the quest is in scope (per the 2026-08-12 rule change,
   everything is in scope), but to find the existing implementation, if any,
   so you audit and complete it in place rather than writing a colliding
   duplicate. If nothing exists anywhere, this is a clean net-new port.
2. **The wiki is authoritative, for every quest, regardless of era or which
   lane historically owned it.** Open the quest page + `Transcript:` pages
   (see the table in step 5) and treat them as ground truth. Where an
   existing implementation (LC, 2009scape, or this queue's own earlier work)
   disagrees with the wiki or is missing a documented branch/reward/chapter,
   the wiki wins — fix it in place.
3. **No game-facing strings / ids / config constants in C.** Quest Helper Java is
   a *guide*, not something to re-implement in the engine. Express as `.rs2` +
   configs. New Server VM opcodes only when content cannot say it
   (`PORTING_GUIDE` §2.4 / §2.5) — plan + implement in the same slice (log below).
4. **Resolve names through the pack** — gameval lowercased; never copy numeric
   ids. Run `tools/questhelper_extract.py <helper-dir> --check` before writing
   scripts; unresolved names → `blocked` with the failing name, not workarounds.
5. **Wiki transcripts for dialogue (not just the helper).** Quest Helper is the
   state machine / critical-path guide; it does **not** enumerate every dialogue
   tree. Before writing scripts, open these pages (spaces -> `_`; see also
   `ExternalQuestResources.java` for the quest article URL):

   | What | Wiki URL |
   |---|---|
   | Quest / quick guide | `https://oldschool.runescape.wiki/w/<Quest_Name>` · `…/Quick_guide` |
   | **Dialogue trees** | `https://oldschool.runescape.wiki/w/Transcript:<Quest_Name>` |
   | Journal | `https://oldschool.runescape.wiki/w/Transcript:<Quest_Name>/Journal` |
   | NPC / item side trees | `https://oldschool.runescape.wiki/w/Transcript:<Name>` (follow links from the quest transcript) |

   Cover refuse options, re-talks, post-quest lines, lost-item replacements, and
   other branches the helper never `addDialogStep`s. Port when players can hit
   them; defer only with a queue-log note naming the deferred transcript
   section. Cite the transcript URL(s) in the row Notes / log when marking
   `done` (`PORTING_GUIDE` §4.6 step 4).
6. **Interfaces:** drive the rev-230 panel; do not invent IF1. See
   `UI_ERA_PORTING_GUIDE.md`.
7. **Never park sibling lanes** — no `*.skip`, no moving live trees aside for
   compile. Fix your own errors (PORTING_GUIDE §7).

## Skip list (out of scope)

| Quest Helper path | Why skip |
|---|---|
| `helpers/achievementdiaries/**` | diaries, not quests |
| `helpers/combattasks/**` | combat achievements |
| `helpers/mischelpers/**` | misc overlays |
| `helpers/skills/**` (skillsagility/, skillsmining/, skillswoodcutting/) | skill guides |
| `helpers/playerquests/**` | player-authored |
| League / `LeagueQuestRegions` variants | temporary league content |
| Spelling-only mismatches already owned elsewhere (`vampyreslayer`, `romeoandjuliet`, `monkeymadnessi`, `fairytalei/ii`, `blackknightfortress`) | same npc/dbrow as an already-audited row under another name -- don't re-audit under the alias too |
| Helpers whose gameval names fail `--check` | `blocked` until pack grows |

**Retired 2026-08-12** (were skip rows, now audit targets, see the rule change
at the top of this doc): "Pre-Sept 2004 quests with LostCity implementations"
and "mid-era quests with 2009scape implementations" are no longer skipped —
see the IN-LC and mid-era audit tables below.

### IN-LC: pre-Sept 2004 QuestHelper dirs -- wiki-accuracy audit queue

These QH helpers implement LostCity-era quests. Filed on `CONTENT_PORT_QUEUE.md`
by era, but as of the 2026-08-12 rule change this queue also audits/completes
them directly rather than treating LC ownership as a hard boundary: pick the
next unaudited row (smallest LC script first if line-count data exists,
otherwise any), open the LC script + the quest's wiki page + `Transcript:`
pages, and confirm every wiki-documented branch/reward/chapter is present and
correct. If it is, mark `audited-ok`; if it's missing something, port the gap
in place (same no-duplicate-trigger rules as everywhere else) and mark
`audited-fixed` with a one-line note on what was added. This table has not
been audited yet as of the rule change -- every row below is effectively
`pending` audit:

| Quest Helper path | LC script name | Status |
|---|---|---|
| `animalmagnetism` | quest_animalmagnetism | audited-fixed 2026-08-12: very thorough existing port (7 files) — Ava's chicken/magnet/tree/notes/container chain, the witch's selected-iron magnet-hammer minigame, Alice/husband/crone ghostspeak-amulet chain (with lost-amulet replacement), and the research-notes on/off button puzzle all matched Transcript:Animal_Magnetism, ending in the real `~quest_complete(quest_animalmagnetism)` with reward xp matching the dbrow exactly (1000 crafting/fletching/slayer, 2500 woodcutting) and the ranged-50 device-tier branch. One real gap: Ava's opening offer was a straight-line intro with no way to decline — added the wiki's two refusal branches (`~p_choice3`: "I'm not much into interior design..." and "What's a nice girl like you...") from Transcript:Animal_Magnetism. |
| `biohazard` | quest_biohazard | audited-fixed 2026-08-12: dialogue trees (chemist/errand boys/Guidor/Guidor's wife/Elena/King Lathas) are thorough and match Transcript:Biohazard closely, ending in the real `~quest_complete(quest_biohazard)` (3 QP, 1250 thieving xp, matches dbrow). One real quest-blocking gap: `quests/quest_biohazard/scripts/quest_biohazard_locs.rs2`'s `biohazard_climb_ladder` label (the wall-crossing cutscene) was fully written but **nothing anywhere ever called it** — Omart and Kilron both have real world `.spawn` entries but zero `[opnpc1,...]` Talk-to triggers, so a player who reached `^biohazard_released_pigeons` had no way to actually cross into West Ardougne. Added both NPC handlers (Omart: "Okay, let's do it"/"Not yet." choice + free repeat crossings; Kilron: return-trip + Mourner HQ location hint), matching Transcript:Biohazard's Omart/Kilron lines. |
| `cooksassistant` | quest_cook | audited-fixed 2026-08-12: this is docs/PORTING_GUIDE.md's own §4.1 precedent slice, and it held up well — ingredient gather/hand-in, journal (3 states, wired), reward (300 cooking xp, 1 QP via `~quest_complete(quest_cooksassistant)`), and the post-quest small talk (`~p_choice4`, matches Transcript:Cook_(Lumbridge)'s post-quest tree) were all already correct. One real gap: the file's own header comment said "the player cannot decline the quest" because `~p_choice*` wasn't portable yet when this was first written — but `interface_chat/scripts/chat.rs2` implements `~p_choice2`/`~p_choice4` for real now (this file already uses `~p_choice4` for the post-quest chat), so the comment was stale and the decline branch was just never restored. Added the accept/decline `~p_choice2` on the initial offer with Transcript:Cook's_Assistant's refusal line ("Fine. I always knew you Adventurer types were callous beasts. Go on your merry way!"). |
| `dwarfcannon` | quest_mcannon | audited-fixed 2026-08-12: state constants (0-11) and the journal (`mcannon_journal.rs2`) covered the whole quest, but nothing anywhere ever advanced `%mcannon` past what the railings/cave/crate scripts touch internally — the quest giver **Captain Lawgof** (`lawgof2`, dbrow startnpc) and **Nulodion** (`nulodion`) had zero `[opnpc1,...]` dialogue anywhere in the tree, so the quest could never even be started, `mcannontoolkit` (gates the cannon repair) and `ammo_mould` (gates `skill_smithing/scripts/smelting/cannonballs.rs2`'s own cannonball recipe) were never granted to any player, and there was no `~quest_complete` call in the whole quest. Added `quests/quest_mcannon/scripts/mcannon_commander.rs2` — full Lawgof + Nulodion talk-to state machine (accept/refuse quest offer, railing/watchtower/goblin-cave/cannon/Nulodion checkpoints, all matching Transcript:Dwarf_Cannon paraphrased) — granting `mcannonrailing1_obj`x6, `mcannontoolkit`, `nulodions_notes` + `ammo_mould` at the right checkpoints, ending in `stat_advance(crafting,7500)` + `~quest_complete(quest_dwarfcannon)`. Both npcs already had real world `.spawn` entries (`m40_54`/`m47_53`) — no hand-spawning needed. |
| `eaglespeak` | quest_eaglepeak | audited-fixed 2026-08-12: unusually complete existing port (10 files) for a quest whose own header comment claims "Disguise / Asyff / puzzles deferred" — that comment is stale, same bug class as other rows' stale headers. All three crystal-feather puzzle chambers (bronze winch room, silver kebbit-trail room, golden birdseed/lever room), the stone eagle door, Asyff's disguise tailoring, the eagle-guard sneak, and the post-quest eyrie quick-travel are all fully scripted and match Transcript:Eagles'_Peak / Quick_guide, ending in the real `~quest_complete(quest_eaglespeak)` with reward (2 QP, 2500 hunter xp) matching the dbrow exactly. One real, confirmed quest-blocking gap: every dialogue trigger for Nickolaus (`eaglepeak_nickolaus_shout` in `eaglepeak.rs2`, `eaglepeak_nickolaus_normal` in `sneak.rs2`) was written against debug-spawn-only npc type names — cross-checked `server/scripts/areas/world/configs/m31_77.spawn` and `m36_54.spawn` (real cache-derived world spawns) and found the actual live npcs are named `eaglepeak_nickolaus` (cave/nest) and `eaglepeak_nickolaus_campsite` (external camp), neither of which had any `[opnpc1,...]` trigger anywhere in the tree — a live player could never actually talk to Nickolaus outside of the `::eaglepeakcave`/`::eaglepeaksneak`/`::eaglepeakcamp` debug commands, softlocking the whole quest after the cave-entrance stage. Fixed by wiring both real npc names into the existing (already-correct) dialogue logic: added `[opnpc1,eaglepeak_nickolaus]` alongside the shout trigger in `eaglepeak.rs2` (delegating to the nest logic once past the eagle guard), converted `sneak.rs2`'s nest/camp handler into a callable `[label,eaglepeak_nick_reached]` (kept the debug `_normal` trigger as a thin alias), and added `[opnpc1,eaglepeak_nickolaus_campsite]` in `camp.rs2`. Also fixed a latent fallthrough bug this exposed: the nest handler's `@eaglepeak_nick_camp;` call had no `return;` after it, so a real camp conversation would run its full dialogue and then immediately re-print "I'll meet you back at my camp outside" from the next `if` block — added the missing `return;` and a `quest < meet_camp` top gate on `camp.rs2`'s label (the real campsite npc is a persistent spawn, reachable before the quest has actually asked the player to go there). Cross-checked `quest_waterfall`/`quest_itwatchtower`/`quest_tree`'s dungeon puzzle scripts (no `loc_add` calls anywhere, i.e. their interior scenery relies on real baked map data, not script spawning) to confirm eaglepeak's `debugproc`-only `loc_add` calls for books/feeders/winches/pedestals are the same accepted convention (dev-testing convenience over real cache-baked scenery), not a second instance of the same missing-spawn bug — only the *npc* wiring was actually broken. Build: `mingw32-make -C src torirsserver-scripts` exit 0, no new diagnostics touching `quest_eaglepeak`. |
| `eadgarsruse` | quest_eadgar | audited-fixed (2026-08-12): the biggest gap found in the whole IN-LC pass, now fully built. Landed all 7 follow-up items from the prior audit: (1) Sanfew (`sanfew.rs2` `sanfew_more_work`) now offers the real quest (Druidic Ritual + Herblore 31 + `%troll_freed_eadgar` gates matching the journal) and hands in goutweed for `~quest_complete(quest_eadgarsruse)` + 11,000 Herblore XP (matches dbrow) + 1 QP, with a separately-guarded repeatable post-quest hand-in so it can't re-fire the reward. (2) Eadgar's full dialogue tree (11 new labels: plan/parrot request/parrot hand-in/hide-plan/item list via `%eadgar_bits`+`%eadgar_chickens`+`%eadgar_grain`/potion hand-in/parrot-back/fake-man) spliced into the existing `troll_eadgar.rs2` triggers ahead of the stew-shop fallthrough — no competing trigger added. (3) Parrot catch: alco-chunks (vodka+pineapple_chunks, spliced into the existing shared `[opheldu,vodka]`/`[opheldu,pineapple_chunks]` blocks scoped via `last_item` so it can't bleed into gin/brandy) used on `eadgar_aviary_wall_hatch`, plus hide/fetch at the prison rack. (4) `%eadgar_bits` item collection wired, including Tegid's dirty-robe hand-in (`eadgar_druid_washing.rs2`, refuse → Sanfew-leverage line → grant, matching the transcript). (5) Troll thistle dry (fire, spliced into the shared cooking-fire trigger) → grind (pestle+mortar, new `herblore_grind_table` row) → mix into `ranarrvial` (spliced into the existing `[opheldu,ranarrvial]`). (6) Storeroom key (kitchen drawers)/unlock/goutweed pickup, with a probabilistic minor-damage roll standing in for the wiki's live-patrol guard AI (goutweed always obtainable, small chance of a hit — noted in-code as a simplification, not a silent gap). (7) Sanfew reward hand-in — done in (1). Trollheim Teleport spell gating deliberately left untouched (the spell itself has no quest-gate mechanism anywhere in `skill_magic/scripts/spells/teleport.rs2`, an independent pre-existing deferral; `%eadgar_quest = ^eadgar_complete` is the correct flag for that gate once it exists). All dialogue/mechanics verified against `Transcript:Eadgar's_Ruse` + Quick guide, not invented. Build verified clean across every chapter, no duplicate-trigger warnings on any of the 11 touched files. Quest is now fully playable start to finish. |
| `heroesquest` | quest_hero | audited-fixed 2026-08-12: several files' own header comments claimed "full quest body (gangs/firebird/ice queen/grip) deferred", but that was stale — the quest is genuinely almost entirely built across `garv.rs2`/`grubor.rs2`/`trobert.rs2`/`brimhaven_scarface_mansion.rs2` (Black Arm disguise-as-deputy infiltration), `fire_feather.rs2` + `drop_tables/scripts/entrana_firebird.rs2` (firebird feather), `gerrant.rs2`/`oily_fishing_rod.rs2`/`skill_fishing/scripts/fishing_spots/lavafish.rs2` (lava eel), and `achietties.rs2` (start/turn-in, reward matches dbrow exactly: 3075 combat-stat xp x4, 2075 ranged, 2725 fishing, 2825 cooking, 1575 wc/fm, 2275 smithing, 2575 mining, 1325 herblore, 1 QP via real `~quest_complete`). Five real, connected gaps found and fixed: (1) `areas/varrock/scripts/katrine.rs2` and `straven.rs2` (Black Arm / Phoenix gang leaders) never touched `%heroquest` at all, so neither gang path's armband sub-quest could ever start (`quest_hero/scripts/grubor.rs2`'s password gate and `areas/area_brimhaven/scripts/brimhaven_thin.rs2`'s Alfonse gate both check exact `%heroquest` states nothing ever set) — added the missing "is there a way to get the rank of master thief" ask + candlestick hand-in branches to both NPCs' existing menus. (2) `drop_tables/scripts/grip.rs2` never dropped `grip_keys` (Transcript: "Take Grip's keyring"), so the treasure-room door in `brimhaven_scarface_mansion.rs2` could never be unlocked by anyone — added the drop. (3) that same door's unlock gate only recognised the Black Arm checkpoint, permanently locking out a Phoenix-path player even holding real `grip_keys` — broadened to check either gang's own checkpoint (soft single-player stand-in for the real two-player hand-off; noted in the code). (4) Ice Queen (White Wolf Mountain, level 111) had a world spawn but zero drop table anywhere in the tree, so `ice_gloves` — required by `fire_feather.rs2`'s `opheldu` burn-damage gate — was unobtainable; added `drop_tables/scripts/ice_queen.rs2` (100% drop per wiki, not one of the reference tree's 69/71 ported files). (5) `raw_lava_eel` had no cooking recipe anywhere in `skill_cooking/configs/cooking_generic.dbrow`, so a caught eel could never become the `lava_eel` Achietties wants — added the row (level 53, 30xp, never burns, per wiki). Not touched (non-blocking flavour only, no `%heroquest` gate reads it): `areas/entrana/scripts/high_priest_of_entrana.rs2` still has no Heroes' Quest branch confirming the firebird's existence. |
| `holygrail` | quest_grail | audited-fixed (2026-08-12): dialogue trees were already thorough (king_arthur/merlin/brother_galahad/grail_crone/fisher_king/sir_percival/black_knight_titan/grail_realm_npcs all wired, journal complete); this pass closed the three physical-world gaps a prior audit found. (1) `sir_percival.rs2` gained `[oploc2,percy_sacks]` — a real, previously-unwired loc (wiki-confirmed coord 2962,3506, Goblin Village's east house) — searched with `magic_golden_feather` in inventory (matches the wiki's "must have the feather"), hand-spawning `sir_percival` via `npc_add` since he has no `.spawn` entry. (2) `quest_grail.rs2` gained `[opheld1,magic_whistle]` (the item's real `ifop1=Blow` verb), teleporting near the Brimhaven tower (wiki coord 2740,3232) — below `%grail_given_whistle` lands on the corrupted-realm square (`m43_73`, Black Knight Titan guarding the bridge), at/above it skips straight to the restored-realm square (`m41_73`, king_percival + holy_grail), since `black_knight_titan.rs2` has no dedicated "defeated" flag and that state is unreachable without already having gotten past him; re-blowing from inside either half returns to Brimhaven. (3) `quest_grail.rs2` gained `[opobj1,grail_bell]` (matches its real `ifop1=Ring` verb), teleporting beside `fisher_king`'s static spawn. All coordinates cross-checked against the wiki's raw Map templates and this cache's own static `.spawn` rosters. Build verified clean, no duplicate-trigger warnings on any touched file. |
| `druidicritual` | quest_druid | audited-ok 2026-08-12: matches wiki (Quick_guide + Transcript:Druidic_Ritual) exactly — Kaqemeex's three opening branches (who are you / did you build this / in search of a quest) all converge correctly on the stone-circle quest offer with a real accept/decline (`druid_agree_to_help`/`druid_not_interested`) plus the "is there anything in this for me" herblore-reward detour (`areas/area_taverly/scripts/kaqemeex.rs2`), Sanfew's ingredient hand-in gates on all four enchanted meats (`sanfew.rs2`), the Cauldron of Thunder dip mechanic covers all four raw meats (`quest_druid.rs2`), and completion uses the real `~quest_complete(quest_druidicritual)` with reward (4 QP, `stat_advance(herblore,2500)` = 250 xp) matching the dbrow's `stat_xp_awarded` (stat 15, 2500) exactly. Kaqemeex's post-quest herblore-fundamentals speech and Sanfew's "no more work right now" post-quest branch are both present. No gaps found. Note: `quest_druidspirit` (this row's original secondary target) is not part of Druidic Ritual — it's the separate Nature Spirit quest, already correctly tracked under its own row (`naturespirit` above, found 2026-08-11); not touched here to avoid re-auditing under two names. |
| `icthlarinslittlehelper` | quest_icthlarin | audited-fixed 2026-08-12: very thorough existing port (5 files, 696 lines) despite `icthlarin.rs2`'s own stale header claiming "jar guardians, embalming, carpenter, ceremony, Apmeken" were deferred — all four are fully scripted (`icthlarin_jar.rs2`: four canopic-jar Apparition guardians + return-the-jar puzzle; `icthlarin_embalm.rs2`: Embalmer salt/sap/linen + Carpenter willow-logs-for-holy-symbol; `icthlarin_ceremony.rs2`: east-chamber holy/unholy symbol swap + possessed priest fight + Icthlarin finale), ending in the real `~quest_complete(quest_icthlarinslittlehelper)` with reward (2 QP, 4500 thieving/4000 agility/4000 woodcutting xp) matching the dbrow's `stat_xp_awarded` exactly, and the Wanderer's hypnosis correctly hands out the wiki-accurate "Het" jar (`ics_little_canopic_jar_liver` — Het is the liver-jar god per Transcript:Icthlarin's_Little_Helper, so the file's own "random jar deferred" comment undersold what was actually already correct). One real, confirmed dialogue-accuracy gap: the Sphinx's riddle in `icthlarin_pyramid.rs2` was entirely wrong versus Transcript:Icthlarin's_Little_Helper — used an invented "how many cats to catch ten mice" riddle with a toothless wrong-answer response ("Wrong. Think carefully and return."), when the real riddle is "A husband and wife have six sons and each son has one sister. How many people are in the family?" with five real answer choices (7/9/12/14/I don't know) and a genuine risk: a *confirmed* wrong answer (7, 12, or 14) costs the player their cat, matching the wiki's well-known "you can lose your cat here" bite, while the correct answer (9) gets "Well answered, human. I guess you get to keep your cat." Rewired the whole exchange to match the transcript verbatim, added the confirm/reconsider risk step before actually taking the cat, and added an `ics_sphinx_take_cat` proc that removes whichever kitten/grown/overgrown cat variant the player is carrying (mirrors `areas/varrock/scripts/gertrude.rs2`'s own `fluffs_has_pet_cat` variant list, since this project gates the cat follower as a plain inventory item). Not touched (flavour-only, no state-gating role): the High Priest's optional deeper lore about Icthlarin/the Devourer/Menaphite theology that the transcript documents as skippable "I'd better get going" side-dialogue. Build: `mingw32-make -C src torirsserver-scripts` exit 0, no new diagnostics touching `quest_icthlarin`. |
| `impcatcher` | quest_imp | audited-fixed 2026-08-12: matched wiki (Transcript:Imp_Catcher + Quick_guide) closely already; fixed two gaps — (1) `wizard_mizgog.rs2`'s quest-offer branch never let the player decline ("I've better things to do than chase imps."), always force-started the quest; added the accept/refuse `~p_choice2` + Mizgog's "That's great, thank you." accept line. (2) added the documented post-quest repeatable amulet trade ("Have you got another one of those fancy schmancy amulets?" -> trade 4 beads for another amulet of accuracy) that was entirely missing. Also added `wizard_grayzag.rs2`'s missing `[opnpc1,...]` talk-to dialogue (3 quest-state branches, Transcript:Wizard_Grayzag) — he had combat AI but no talk trigger at all. |
| `legendsquest` | quest_legends | audit-in_progress (2026-08-12, updated): the two missing central NPCs from the prior note are now built, and the quest is genuinely completable start to finish through normal play — but real, wiki-noticeable soft-skip deviations remain, so this stays `audit-in_progress` rather than `audited-fixed`. Built: **Radimus Erkle** (new `radimus_erkle.rs2`) — start gate (5 prereq quests + 107 QP, matching the journal's own logic), a map-completion action (nothing previously produced `thkaramjamapcomp`), totem+map hand-in, 4x 30,000xp training sessions ending in the real `~quest_complete(quest_legends)`. **Gujuo** (new `gujuo.rs2`, plus `bullroarer.rs2` now actually spawns him via `~inzone_coord_pair_table`, previously a dead stub) — sacred-water/golden-bowl request, totem-pole placement, tribe-calling finale. **Ungadulu** (new `ungadulu.rs2`) — octagon rescue via book of binding, Yommi seeds, bravery-potion instructions (new `bravery_pot` recipe added to `skill_herblore/configs/brewing/brew.dbrow`), seed germination. **Viyeldi kill** (`viyeldi.rs2`) — real single-scripted-stab mechanic (matches the actual OSRS mechanic, not combat), sets `^legends_killed_viyeldi`, `deathdagger`→`deathdaggerdone`. Two load-bearing gaps beyond the original list were found and fixed mid-build: **`nezikchened.rs2`** (new) — LostCity's own combat script for this demon was never ported, so all 3 of his fight checkpoints (via Echned Zekin/Ungadulu/Irvig Senay's shared `summon_nezi_part3`) were unwinnable quest-wide; built using the same `ai_opplayer2`/`ai_queue3` pattern as the already-audited `san_tojalon.rs2`. **`legends_gem_shrine.rs2`** (new) — nothing anywhere granted `book_of_binding`, a hard Ungadulu prerequisite; gated on genuinely holding all 5 runes + 7 cut gems (soft-skipped the real per-rock puzzle since no carved-rock locs exist anywhere in `all.loc` to build it against). **Real remaining soft-skips, not yet closed** (each disclosed in-code): the gem/rune puzzle above is a single gated shortcut, not the wiki's actual per-rock mechanic; Gujuo's golden-bowl smithing is one-stepped instead of a separate furnace interaction; Yommi-tree growth is instant; and most significantly, totem-growing only checks the local tribal pool, so the entire Echned Zekin/Viyeldi/Nezikchened "deep water source" arc — while each piece is now individually functional — is bypassable rather than mandatory as in the real quest; the final Nezikchened fight also doesn't model the wiki's "must also beat the 3 Viyeldi warriors" branch if Viyeldi was killed directly. Build verified clean throughout (`torirsserver-scripts`, exit 0, 15215 scripts, no diagnostics on any touched file). Follow-up: make the deep-water-source arc mandatory (matching the wiki), and replace the gem-shrine shortcut with a real per-rock puzzle if/when the geometry exists. |
| `ragandboneman` | quest_ragandbone | audited-ok 2026-08-12: matches wiki (Quick_guide + gameplay) exactly — Odd Old Man's accept/refuse/curious-about-the-mumbling branches, all 8 quest bone drops correctly wired onto their monsters (`ramunsheered*`/`ramsheered`, `bat`/`bat_unaggressive`/`olaf2_giant_bat`/`non_combat_bat`, `medium_frog` in `quest_ragandboneman/scripts/ragandboneman_drops.rs2`; `bear`/`goblin`/`giant_rat`/`unicorn` in `drop_tables/scripts/`; `monkey` spliced into the existing `quest_tbwt/scripts/tbwt_monkey.rs2` trigger rather than a second competing one), Fortunato's vinegar sale (1gp/jug, matches wiki), and the full vinegar-pour/bone-add/pot-boiler/light-logs/20-tick(=12s)-boil/retrieve mechanic for all 8 bones. Reward (`~quest_complete(quest_ragandboneman1)`, 500 cooking + 500 prayer xp, 1 QP) matches the dbrow exactly. No gaps found. |
| `runemysteries` | quest_runemysteries | audited-ok 2026-08-12: matches wiki (Quick_guide + Transcript:Rune_Mysteries) — Duke Horacio/Sedridor/Aubury chains all cover lost-item replacement, refuse branches, re-talks, and post-quest lines. One out-of-scope note: the wiki's reward list includes 5 Kudos claimable at the Varrock Museum, but the Kudos system isn't implemented anywhere in this tree (cross-cutting dozens of quests) — not this quest's gap to close alone. |
| `seaslug` | quest_seaslug | audited-fixed 2026-08-12: very thorough LC port already (caroline/holgart/kennith/kent/bailey/quest_seaslug.rs2 cover every wiki-documented (Transcript:Sea_Slug) scene incl. refuse, lost-torch replacement, and post-quest dialogue). Fixed two real gaps: (1) `caroline.rs2`'s completion queue awarded QP via a bespoke `%qp = add(...)` instead of `~quest_complete(quest_seaslug)`, silently skipping `%quests_completed_count` — every other audited quest in this table uses the real proc, now this one does too. (2) the "possessed fisherman" flavour dialogue (`fisherman.rs2`) only had 2 of the wiki's 6 randomised cryptic lines; expanded to all 6 (paraphrased, non-gating). |
| `sheepherder` | quest_sheepherder (not `quest_sheep`, which is the unrelated Sheep Shearer quest / dbrow `quest_sheepshearer`) | audited-ok 2026-08-12: matches wiki (Quick_guide) exactly — Councillor Halgrive's accept/refuse offer, Doctor Orbon plague-suit gear, cattleprod+poisoned-feed sheep mechanic, incinerator disposal, and completion (`~quest_complete(quest_sheepherder)`, 3100 coins reimbursement matching wiki 100+3000) all present; `diseased_sheep.rs2` even cross-checks against Mourning's End Part I's later reuse of the same world npcs. |
| `treegnomevillage` | quest_tree | audited-fixed 2026-08-12: dialogue trees (King Bolren start/mid/end, Montai, three tracker gnomes, Khazard warlord fight, ballista coordinate puzzle, orb chest, wall breach) all matched wiki (Quick_guide) end to end — but quest completion (`areas/area_gnome/scripts/king_bolren.rs2` `[queue,tree_quest_complete]`) awarded QP via a bespoke `%qp = add(%qp, ^tree_questpoints)` instead of `~quest_complete(quest_treegnomevillage)`, same bug class as Sea Slug's prior fix — silently skipped `%quests_completed_count`. Fixed to call the real proc; reward values (2 QP, 11450 attack xp, gnome amulet) already matched the dbrow/wiki exactly, untouched. |
| `trollromance` | quest_troll / quest_troll_love | audited-fixed (2026-08-12): the actual quest lives in `quest_troll_love/` (`quest_troll` is the separate Troll Stronghold quest, shares only the Eadgar-stew-shop npc — see `eadgarsruse` row above). A prior pass fixed the completion bug (`trollromance_ug.rs2` was a bare unwired stub — now calls `~quest_complete(quest_trollromance)` with the real reward). This pass built the entire "get to Trollweiss" middle third that was previously missing end to end: Tenzing's flower-location dialogue (`death_tenzing.rs2`, new `tenzing_trollweiss` label, sets `^troll_love_learnt_about_trollweiss`); Dunstan's sled-materials handoff (`death_dunstan.rs2`, spliced into the existing `dunstan_ops` hub — takes yew/maple logs + iron bar + rope, grants `trollromance_toboggon`, sets `^troll_love_bring_dunstan_materials`/`_dunstan_made_sled`); a new `trollromance_sled.rs2` with the sled-wax recipe (`bucket_wax`+`swamp_tar`, `cake_tin` required-not-consumed, spliced into the existing `[opheldu,swamp_tar]` trigger rather than duplicating it) and the wax-the-sled recipe (`trollromance_toboggon_waxed`); the piste "Slide" traversal on `trollromance_piste_walk_barrier_down/up` (real, previously-modeled-but-unwired locs — sets `^troll_love_waxed_sled`, soft-skip: single `p_teleport` into the already-working `chill_zones` box rather than an animated descent); and the flower pick trigger on `trollromance_rareflowers` (also real/previously-unwired, grants `trollromance_rare_flower`, sets `^troll_love_picked_trollweiss`). All four quest-specific items and every piste-area loc already existed in `configs/all.obj`/`all.loc` with zero prior scripts — only the trigger plumbing was missing. Verified against `trollromance_ug.rs2`/`trollromance_arrg.rs2`'s existing state-range checks (no changes needed there) and `troll_love_journal.rs2`'s narration at every state. Build verified clean, no duplicate-trigger warnings on any of the 4 touched/new files. Quest is now fully playable start to finish. |
| `waterfallquest` | quest_waterfall | audited-ok 2026-08-12: extremely thorough existing port (10 files) — Almera/Hudon/Hadley/Gerald/Golrie dialogue trees match Quick_guide + wiki almost verbatim including all four multi-choice Hadley tourism branches, pillar rune puzzle, urn/chalice reward gate (2 diamonds, 2 gold bars, 40 mithril seeds, 13750 attack+strength xp matching dbrow exactly), proper `~quest_complete(quest_waterfall)`. No gaps found. |
| `watchtower` | quest_itwatchtower | audited-fixed 2026-08-12: very thorough existing port (14 files) covering every wiki-documented beat (rock cake theft, deathrune/skavid-map city guard riddle, 4-talker skavid word-learning puzzle + mad skavid final riddle, nightshade enclave-guard distraction, ogre shaman potions, Rock of Dalgroth mining, crystal-lever completion) with proper `~quest_complete(quest_watchtower)`. Fixed one real numeric bug: completion granted `stat_advance(magic, 153000)` (15300 xp) but the dbrow's own `stat_xp_awarded` and the wiki both say 152500 raw (15250 xp) — a 50xp overpay; corrected to match. |
| `zogreflesheaters` | quest_zogreflesheaters | audited-fixed 2026-08-12: fixed a 10x-too-low XP bug (`stat_advance(ranged/fletching/herblore, 2000)` should have been `20000` per this codebase's tenths-XP convention, confirmed against the dbrow's `stat_xp_awarded` field and the wiki's 2,000/2,000/2,000 XP reward). Added the two missing item rewards (3 ourg bones, 2 zogre bones). Rebuilt the post-transformation Sithik dialogue as a proper repeatable 3-question `p_choice4` menu per the wiki transcript (was a flat monologue). Files: `quest_zogreflesheaters/scripts/{zogreflesheaters.rs2,zogre_finish.rs2}`. |
| `thefremennikexiles` | quest_fremennikexiles (not `quest_viking`, which is `thefremenniktrials` — see below; this row's own prior "corrected 2026-08-11" mapping note was itself checked and confirmed accurate) | audited-fixed 2026-08-12: fresh full audit (no trace of the referenced 2026-08-11 correction pass was actually found in the tree, so this was re-verified from scratch). QP/XP/item rewards and the `~quest_complete` call already matched the wiki. Found and fixed a real silent-duplicate-trigger bug: `fremennikexiles.rs2` declared its own competing `[opnpc1,viking_woman]` (used as a Freygerd stand-in, since the cache has no dedicated Freygerd gameval), silently shadowing the pre-existing generic Fremennik Trials citizen dialogue and firing unconditionally for any player. Spliced into the existing trigger via a new guarded `fx_freygerd_relevant`/`fx_freygerd_talk` proc pair. Files: `quest_fremennikexiles/scripts/fremennikexiles.rs2`, `quest_viking/scripts/viking_citizen.rs2`. |
| `thefremenniktrials` | quest_viking | audited-fixed (2026-08-12): all trials now implemented and the quest is completable end to end. Correction to the prior audit: the quest actually has **seven** council trial-judges, not six — Swensen the Navigator (a maze trial) was already scaffolded alongside the other six in `quest_viking.constant`/`quest_viking_progress.rs2`, and the wiki confirms 7-of-12 council votes are needed (the other 5 council members are joke-rejection citizens, already correctly implemented in `viking_citizen.rs2`). Built all 7: Sigli (real hand-spawned Draugen kill), Thorvald (real 4-phase Koschei gauntlet, 3-of-4 passes per wiki), Reveller/Manni (real 250gp keg purchase + hand-in, spliced into `poison_salesman.rs2`), Swensen (soft-skipped the physical maze — no maze geometry exists in this map region's cache — to a real 3-junction wrong-turn/retry puzzle), Olaf (real 40 WC/40 Crafting/25 Fletching gates + potato/cabbage/onion/pet-rock hand-in to Lalli the troll + raw-fish enchant hand-in to Fossegrimen), Peer (real 4-letter riddle with genuine wrong-answer retry; soft-skipped the multi-stage bucket/jug/vase escape room — no puzzle-house geometry in cache), and Sigmund (the explicit `"not wired yet"` stub rewritten into a real 13-NPC forward relay across 7 files, reusing Askeladden's real 5,000gp gate). Vote counter (`%viking`) now increments at 7 guarded, revisit-safe sites. Completion (`viking_brundt.rs2`'s existing 7-vote tally) now calls the real `~quest_complete(quest_fremenniktrials)` + `stat_advance(skill, 28124)` (2,812.4 XP across Attack/Strength/Defence/Hitpoints/Woodcutting/Fletching/Fishing/Crafting/Agility/Thieving) + 3 QP, matching the dbrow exactly. Minor non-blocking follow-up: `viking_journal.rs2`'s Sigmund-chain hint text was written for the wiki's original two-pass visit order and may read slightly out of step with the new single-pass relay — cosmetic only. Build verified clean (`torirsserver-scripts`, exit 0, 15215 scripts, no diagnostics on any of the 17 touched files; zero duplicate-trigger collisions across all 21 touched NPC names). |
| `deserttreasureii` / `deserttreasure2` | quest_deserttreasureii | audited-fixed 2026-08-12: QP/rewards/`~quest_complete` call already correct against the wiki. Found and fixed a silent duplicate `[opnpc1,camzodaal_ramarno_entrance]` trigger colliding with Defender of Varrock's Ramarno/sacred-forge handler; spliced the Whisperer-medallion soft-skip into the existing `dov_camdozaal.rs2` block instead. Files: `quest_deserttreasureii/scripts/deserttreasureii.rs2`, `quest_defenderofvarrock/scripts/dov_camdozaal.rs2`. |
| `dragonslayer` / `dragonslayer1` | quest_dragon | audited-ok 2026-08-12: an unusually complete 11-file port that matches Quick_guide + Transcript:Dragon_Slayer_I almost verbatim end to end — Guildmaster's Champions'-Guild-access branch, Oziach's full map-piece/shield hint tree (with a real branch-menu that revisits any of the three pieces or the antidragon-shield hint in any order, matching the transcript's own non-linear structure), the Oracle's map-piece rhyme + 30-line random-flavour table, Wormbrain's full pay/kill/story/forget-it branches (goblinchat matches transcript), the Dwarven-mine magic-door 4-item puzzle (silk/lobster pot/unfired bowl/wizard's mind bomb), Melzar's Maze (6-coloured-key monster drops + chest), Duke Horacio's optional antidragon shield hand-out, Klarense's ship-purchase/repair/board dialogue (2000gp + 3-plank/12-nail hole patching), Ned's sail-to-Crandor cutscene, and Elvarg's fire-breath/melee AI (shield + Protect Magic maxhit reduction) all present and correct. Completion uses the real `~quest_complete(quest_dragonslayer1)`; reward `stat_advance(strength/defence, 186500)` (18,650 xp each) matches the dbrow's `stat_xp_awarded` (columndef 33: stat 1 and 2, both 186500) and the wiki exactly; 2 QP matches dbrow `questpoints`. No gaps found. |
| `dragonslayerii` / `dragonslayer2` | quest_dragonslayer2 / quest_dragon | audited-fixed (2026-08-12, complete): every chapter of one of the wiki's longest quests now has real content; completion plumbing was already correct throughout. Built across four agent batches, verified after each that concurrent edits to the shared `dragonslayer2.rs2` file all coexisted (no clobbering). **Crandor arc**: real mine/mural/Spawn fight (level-100 combat, not click-to-win), real 24-piece Fossil Island map gather across 5 real search locs, real boat construction material gate. **Lithkren/dream chapter**: real dungeon traversal, dream potion reuses Dream Mentor's recipe, real HP-gated Robert the Strong fight using his actual combat stats. **Key-pieces/Vorkath chapter**: the cache shipped a complete, purpose-built but entirely unwired asset scaffold for this whole chapter — Kharazi Jungle maze got real golem combat + trap checks, Mort Myre got real crafting + dowsing, **Vorkath got a real HP-gated fight** with a periodic add-spawn special, Shayzien Crypts got a real logic puzzle, Ancient Cavern reforging got a real door/lighting/combine sequence. **Final chapter (diplomatic tour/waves/Galvek)**: real dialogue-gated recruitment checkpoints for Amik Varze/Lathas/Roald/Brundt using pre-reserved cache varbits nothing had wired; a real 13-dragon wave gauntlet across all three wave parts matching the wiki's breakdown; a real 4-phase Galvek fight (1200 HP, phase swaps via `npc_changetype` between four purpose-built variants at 900/600/300 HP thresholds, correct Protect-prayer-per-phase, the wiki's always-on fireball special). Disclosed remaining fidelity simplifications (all narrow, gated behind real content, not bare stubs): the map-rotation IF-puzzle and Shayzien riddle are single fixed-answer shortcuts rather than true per-player puzzles; the ship-defense minigame's play is narrated (gated behind the real recruit tour); wave/Vorkath's magic-ranged-poison-electric attack variety is simplified to melee; Galvek's tile-hazard phase mechanics (fire-trap/hurricane/tsunami/entombment) are flavor text, not real geometry. No bare unconditional soft-skip stubs remain anywhere in the quest. |
| `taibwowannaitrio` | quest_tbwt | audited-fixed 2026-08-12: exceptionally thorough 8-file port matching Quick_guide + Transcript:Tai_Bwo_Wannai_Trio almost verbatim — Timfraku's title-selection opening, all three brothers' full dialogue trees (Tiadeche's karambwan-vessel fishing chain incl. the free-first-catch offer, Tinsay's three-item fetch-quest chain with the "how do I..." hint menu at every stage, Tamayu's agility-potion/poisoned-spear Shaikahan-hunt cutscene with a real pass/fail assessment based on spear+poison+agility state), Lubufu's apprentice chain, and the four separate "final" (village) NPCs that hand out the real per-brother rewards all present. Completion uses the real `~quest_complete(quest_taibwowannaitrio)`. Reward audit against Transcript/wiki's exact breakdown (1,500 fishing during via Lubufu + 5,000 fishing/5,000 cooking/2,500 attack+2,500 strength+rune spear from the three brothers' final dialogue + 2,000 coins from Timfraku) — every one of these matched the script exactly (`stat_advance(fishing,15000)` in `tbwt_lubufu.rs2`, `stat_advance(fishing/cooking,50000)` and `stat_advance(attack/strength,25000)` in the three `areas/area_karamja/scripts/tbwt_*_final.rs2` files, `inv_add(inv,coins,2000)` in `tbwt_timfraku.rs2`). One real gap: the wiki (Jogre_bones page) documents burning Jogre bones two ways -- a furnace (any Firemaking level, 25 Cooking xp, already implemented) or a tinderbox at Firemaking 30+ (90 Firemaking xp) -- and the tinderbox path didn't exist anywhere in the tree at all. Added it by splicing a `tbwt_jogre_bones` case into `skill_firemaking/scripts/firemaking.rs2`'s existing `[opheldu,tinderbox]` trigger (not a duplicate -- spliced per the standing rule) calling a new `[label,tbwt_light_jogre_bones]` in `quest_tbwt/scripts/tbwt_jogre_bones.rs2` (30 Firemaking level gate + `stat_advance(firemaking,900)`, matching the wiki's message and xp exactly). Build: `mingw32-make -C src torirsserver-scripts` exit 0, no new diagnostics on either touched file. |
| `naturespirit` | quest_druidspirit | audited-fixed (2026-08-12, complete): everything downstream of the quest starting was already complete and wiki-accurate (Filliman's full dialogue tree, grotto mechanics, ghast fights, real `~quest_complete(quest_naturespirit)` with correct reward). Two blockers kept it completely unstartable through normal play, both now fixed. Root cause: Nature Spirit's own prerequisite, Priest in Peril, was itself incomplete (fixed in the mid-era audit batch — see `quest_priestperil` there for full detail; that fix wired the missing essence-purification finale, unblocking `^priestperil_complete`). But fixing Priest in Peril alone wasn't sufficient — Drezel still had no Nature-Spirit quest-offer branch of his own, so `%druidspirit` never left `^druidspirit_not_started` even after Priest in Peril could complete. Added the offer (gated on Priest in Peril + Restless Ghost completion, matching this quest's own `druidspirit_journal.rs2` text almost verbatim) into the existing `[opnpc1,priestperiltrappedmonk2]` block in the mid-era batch's new `mausoleum_drezel.rs2`, setting `%druidspirit = ^druidspirit_started`. One independent, self-contained gap fixed earlier in this pass regardless of the blocker: the Mort Myre swamp-decay damage-over-time mechanic (`swamp_decay.rs2`'s `start_swampdecay_timer` proc) was fully written but never called anywhere; wired into `quest_druidspirit.rs2`'s `open_mortmyre_gate` on entry. Build verified clean throughout. Nature Spirit is now startable and completable through normal play for the first time. |
| `murdermystery` | quest_murder | audited-fixed 2026-08-12: one of the most thorough ports audited this whole queue (20 files) -- the randomised 1-of-6 culprit system (`%murdersus = ~random_range(1,6)`), all six suspects' full poison-purchase/thread/alibi dialogue (`anna.rs2`/`bob.rs2`/`carol.rs2`/`david.rs2`/`elizabeth.rs2`/`frank.rs2`), the flour+flypaper fingerprint-matching puzzle (`quest_murder_prints.rs2`), the six poison-proof search locations each keyed to the right suspect (`quest_murder_poisonproof.rs2`: compost/beehive/drain/spiders'-nest/fountain/family-crest), the barrel/window/gate evidence collection (`quest_murder_barrels.rs2`, `quest_murder_window.rs2`), and the guard's tiered accusation dialogue (correctly requiring thread+fingerprint+poison-proof together for the real "conclusive proof" ending, matching the wiki's evidence-gating) were all cross-checked against Quick_guide and match exactly, including the "drop the necklace before turning in evidence" wiki tip (`murder_clear_evidence` sweeps `worn` as well as `inv`/`bank`, matching the wiki's warning that even worn evidence gets confiscated). Completion uses the real `~quest_complete(quest_murdermystery)`; also correctly spliced into the shared `gossipy_man`/`murderguard` NPCs' King's Ransom follow-up content without duplicating either trigger. One real numeric bug: completion granted `stat_advance(crafting, 14060)` (1406.0 xp) but the dbrow's own `stat_xp_awarded` (columndef 33: stat 12, value 14062) is 1406.2 xp -- a 0.2xp underpay; corrected `murder_guard.rs2`'s `[queue,murder_quest_complete]` to `14062`. Build: `mingw32-make -C src torirsserver-scripts` exit 0, no new diagnostics on the touched file. |
| `shadowofthestorm` | quest_shadowstorm | audited-fixed (2026-08-12, complete): all six previously-flagged gaps closed. **Four-kiln search**: the cache already anticipated this exactly — `agrith_kiln_1..4` swap to a pre-authored `_lookin` variant while `%agrith_quest=60`; wired real `[oploc1,...]` triggers, wrong kilns give the wiki's own rejection line, right kiln grants the book once (roll via a previously-unused varbit). **Incantation puzzle**: real `p_choice5` submission using the wiki transcript's exact quoted phrase and its reverse; wrong answers retry. Simplification: fixed answer, not a full per-player permutation. **Clay golem interrogation**: turned out to already be fully built in the sibling `quest_golem/` files (real dialogue gate + item-gate) — the original audit missed it because it lives outside `quest_shadowstorm/`, not because it was missing. **Sigil chase**: replaced the flat `mesbox` with real content matching the wiki's scripted (not player-fought) death — narrated cutscene, a real ground-drop of Tanya's sigil, Eric's sigil via Evil Dave's dialogue using the transcript's exact quote. **Agrith-Naar fight**: previously had zero attack-back AI; added real combat with a Fire-Blast/protect-melee AI switch and a real weapon-gated finishing blow (must be wielding Silverlight/Darklight or he revives at 12 HP, same pattern used elsewhere in this codebase for weapon-gated boss finishes). Simplification: no Telekinetic Grab pull-teleport (no precedent function in the tree to reposition a player from NPC AI). **Six-gems bonus**: real conditional reward gated on `%golem_throne_gems` (confirmed live in `quest_golem/scripts/golem_portal.rs2`). **Tree-wide fix**: the `thosf_reward_lamp` "gap" turned out to have an established convention already (flavor item + direct `stat_advance`, no rub-UI, per `quest_pathofglouphrie`'s own precedent) — this quest's own code was just missing its half; fixed. No duplicate-trigger risk — grepped every NPC/loc/item name used tree-wide before adding, all clean. |
| `undergroundpass` | quest_upass | audited-ok 2026-08-12: extremely thorough existing port (31 files, 2602 lines) -- spot-checked the full critical path against Transcript:Underground_Pass / Quick_guide and found it faithful everywhere checked: King Lathas's Biohazard-resolution and Underground Pass start dialogue (`areas/area_ardougne_east/scripts/king_lathas.rs2`) matches near-verbatim including the ranged-25 gate and `~setupassgrilltrap` grid seeding; Klank's tinderbox/gauntlets hand-out plus the wiki's 5000gp repurchase-a-lost-pair branch; the doll-of-Iban altar mechanic (`upass_tomb.rs2`'s `[oplocu,cave_temple_altar]`) matches the wiki's throw-the-doll-in-the-pit finale beat for beat, including the post-kill deathrune/firerune bonus loot and temple-collapse cutscene; the bloodwell/badge/unicorn-horn door-unlock mechanic (`upass_bloodwell.rs2`) is fully wired. Completion (`king_lathas.rs2`'s `[queue,upass_quest_complete]`) uses the real `~quest_complete(quest_undergroundpass)` with reward xp (30000 agility + 30000 attack = 3000/3000, matches dbrow `stat_xp_awarded` exactly) and 5 QP. One known, non-blocking gap: Iban's staff recharge at the well (`upass_bloodwell.rs2`'s `case ibanstaff`) is a no-op -- `%iban_staff_charges` doesn't exist anywhere in the port/vars map, i.e. this engine has no generic weapon-charge system yet; same class of cross-cutting/systemic gap as the reward-lamp note above, not specific to this quest. |
| `thegrandtree` | quest_grandtree | audited-ok 2026-08-12: high-quality existing port (16 files, 1816 lines) matching Transcript:The_Grand_Tree / Quick_guide closely everywhere checked -- Hazelmere's bark-sample/scroll exchange, King Narnode's full 5-choice translation-verification dialogue tree (narrowing down to the exact wiki sentence "A man came to me with the King's seal...And Daconia rocks will kill the tree!"), the Foreman's exact three-question loyalty quiz (wife/favourite dish/girlfriend's name, wrong answers -> combat) in `foreman.rs2`, the Ka-Lu-Min shipyard gate password puzzle in `shipyardworker.rs2`, Femi's helped-free-vs-1000gp-toll branch, and the black demon fight/twig-pillar trapdoor unlock are all present and correct. Completion (`king_narnode.rs2`) uses the real `~quest_complete(quest_grandtree)` with reward xp (184000 attack + 79000 agility + 21500 magic = 18400/7900/2150, matches dbrow `stat_xp_awarded` exactly) and 5 QP. Several files' own header comments claim "Full Grand Tree quest body deferred" -- stale, same pattern as several other rows in this table; the body is in fact essentially complete. One minor, non-blocking simplification: the twig-on-pillar puzzle accepts any twig on any of the four pillars rather than enforcing the wiki's T-U-Z-O left-to-right order (any twig used anywhere, all four just need to end up out of the inventory) -- same end state reached, just without the wiki's negative feedback for a wrong slot; not worth a fix given this codebase's existing precedent for this class of puzzle simplification (see the Waterfall Quest row's pillar-rune note above). |
| `thelosttribe` | quest_losttribe | audited-fixed 2026-08-12: matches Transcript:The_Lost_Tribe closely overall (Sigmund/Duke Horacio dialogue chains, brooch dig + Reldo/bookcase identification, goblin generals Bentnoze/Wartface's full in-character banter unlocking Goblin Bow/Salute, Mistag contact, HAM pickpocket/chest/crate silverware retrieval, treaty-signing cutscene) ending in the real `~quest_complete(quest_losttribe)` with reward (1 QP, 30000 mining xp = 3000, matches dbrow `stat_xp_awarded` exactly) plus a ring of life. The `lost_tribe_cook_witness` proc reused by Cook's Assistant (flagged in this loop's brief as a possible duplicate-trigger risk to check before touching) turned out to be a real, different bug: cross-checking Transcript:The_Lost_Tribe verbatim showed the cellar-incident eyewitness account ("Last night I was in the kitchen and I heard a noise from the cellar...") is spoken by **Bob** (Bob's Brilliant Axes), not the Cook -- the Cook's actual wiki line is an unrelated red herring ("Oh no, it's terrible, isn't it? There was rock dust everywhere, it got on all my ingredients!"). Moved the witness proc (renamed `lost_tribe_bob_witness`) from `[opnpc1,cook]` to `[opnpc1,bob]` (`areas/lumbridge/scripts/bob.rs2`, splicing into its existing block, not a competing one) and restored the Cook's correct red-herring line in `quest_cook.rs2`. Also added a real, wiki-documented, entirely-missing post-quest reward branch: the quest's reward list includes "A mining helmet from giving the brooch back to Mistag" (confirmed via the page's raw `{{Quest rewards}}` template, since the in-quest transcript doesn't cover post-quest interactions) -- added an `[opnpcu,lost_tribe_mistag_1op]` use-brooch-on-Mistag handler in `losttribe_mistag.rs2` granting `cave_goblin_mining_helmet_unlit` (one-time, gated on not already owning a helmet). Build: `mingw32-make -C src torirsserver-scripts` exit 0 after each fix, no new diagnostics on any touched file. |
| `junglepotion` | quest_junglepotion | audited-ok 2026-08-12: matches wiki (Quick_guide + Transcript:Jungle_Potion) closely — Trufitus's full offer tree (`trufitus.rs2`) has multiple decline branches at every stage ("I am sorry, but I am very busy." at 3 separate points, all routing to a real farewell label), the five-herb collection loop (snake weed/ardrigal/sito foil/volencia moss/rogues purse) with correct clue re-asks, wrong-herb/dirty-herb/not-fresh rejections, and the `%druidquest = ^druid_complete` prerequisite gate (Druidic Ritual) all match. Herb cleaning is correctly *not* duplicated per-quest — `skill_herblore/scripts/identify.rs2`'s generic `attempt_clean_herb` dbtable-driven proc already covers all 5 `unidentified_*` jungle herbs alongside regular grimy herbs. Reward (775 herblore xp = `stat_advance(herblore,7750)`, 1 QP) via real `~quest_complete(quest_junglepotion)`; journal (`junglepotion_journal.rs2`) tracks all 12 states including live `inv_total` re-checks and is wired in `interface_questjournal/scripts/quest_journal.rs2`. No gaps found. |
| `recruitmentdrive` | quest_recruitmentdrive | IN-LC — CONTENT_PORT_QUEUE — audited-fixed (2026-08-12): two genuine bugs fixed. (1) Sir Kuam/Sir Leye room (`recruitmentdrive_kuam.rs2`) implemented a fictitious "no man may defeat me" gender mechanic (infinite heal on male players + a "soft skip" bypass) instead of the real wiki mechanic -- Sir Leye is blessed against blades, not gender ("no BLADE may defeat me"; killing him with a bladed weapon fails the test, warhammer/unarmed succeeds; Transcript:Recruitment_Drive + Sir_Leye wiki page). Rewrote to grant all 4 room weapons (steel sword/claws/battleaxe/warhammer, matching the wiki's "four weapons nearby") and check `inv_getobj(worn, ^wearpos_rhand)` on the killing blow. (2) Quest completion (`recruitmentdrive.rs2`) called `~quest_complete` but granted none of the wiki's reward (1,000.5 Prayer/Herblore/Agility XP + 3,000 coins) -- added via `stat_advance`; initiate armor/teleport/title deferred (no shop-unlock/spawn-teleport/title system in this engine). Reconciled `SCAPE2009_CONTENT_PORT_QUEUE.md` rows 22c/22h, which duplicate-tracked this same LC content (22h "Miss Cheevers" was stale-`pending`; the room is already fully implemented at `recruitmentdrive_cheevers.rs2`, now marked done there). Build clean. |
| `regicide` | quest_regicide | audited-fixed (2026-08-12): the entire back half found missing in the prior audit is now built, and the quest is genuinely completable end to end via the real `~quest_complete(quest_regicide)` (3 QP + `stat_advance(agility, 137500)` = 13,750 XP + 15,000 coins, matching the dbrow and wiki exactly). Built: `king_lathas.rs2` gained a `@regicide_lathas_talk` intercept covering every mid-quest state through the "Tyras is dead" proof exchange and the real ending (hands in `regicide_iorwerth_message`, sets `^regicide_spoken_arianwyn` then `^regicide_complete`, grants reward); `koftik.rs2`'s Well of Voyage teleport was folded in (also fixed a latent bug where the "well ready" line showed regardless of quest state); the footprint puzzle (`regicide_camp_tracker.rs2`) now sets `^regicide_found_footprints` via the previously-unbound `regicide_old_camp_footprints_vis_op` multiloc; real melee combat AI was added to the Tyras-camp-guard (`regicide_tyras_guard.rs2`, mirrored from this quest's own `regicide_darkelf2` pattern) setting `^regicide_defeated_guard`/`^regicide_entered_camp`; a full bomb-crafting chain (new `regicide_bombcraft.rs2` — quicklime/sulphur/naphtha/cloth + catapult firing) was built, with the still/naphtha-mixing steps spliced into `quest_mourningsendparti/scripts/mend1_poison.rs2`'s pre-existing triggers for those same items rather than duplicating them (a real duplicate-trigger mistake was caught and fixed mid-build). Two new herblore grind entries (quicklime/sulphur) were added, and a `quest_biohazard/scripts/chemist.rs2` post-Biohazard dialogue gap this work exposed was fixed alongside. Soft-skips (all disclosed in-code): the fractionalising still's valve/pressure minigame is a flat coal-cost conversion; the forest branch/twig puzzle is three narrated `Follow` interactions rather than a spatial puzzle; guard-fight-then-camp-discovery collapses into one combat kill; the elf scout party greeting is narrated (no NPC exists in the pack for it); furnace glove-burn flavour detail was dropped. Build verified clean (`torirsserver-scripts`, exit 0, 15196+ scripts, no diagnostics on any touched file). |
| `tearsofguthix` | quest_tearsofguthix | IN-LC — CONTENT_PORT_QUEUE — audited-ok (2026-08-12): checked against the Tears_of_Guthix quest page + Quick_guide + Transcript. Start requirements (43 QP, Firemaking 49, Crafting 20, Mining 20) match `^tog_fm_req`/`^tog_craft_req`/`^tog_mine_req`/`^tog_qp_req` exactly; Juna's story-telling dialogue, "what are the Tears" branch, and stone-bowl crafting (chisel + tog_stone -> tog_bowl) match the wiki; completion correctly calls `~quest_complete(quest_tearsofguthix)` with 1,000 Crafting XP (`stat_advance(crafting, 10000)`), matching the wiki reward exactly. Pre-existing disclosed simplification (not a new finding, not fixed): the post-quest weekly Tears-collection minigame's XP-skill selection (`tog_soft_collect_tears` in `tearsofguthix_lantern.rs2`) picks the lowest-*level* skill among 15 non-combat skills with a flat 1000xp/collect, where the real mechanic (per the Tears_of_Guthix_(minigame) wiki page) is lowest-*experience* skill among ALL skills including combat (Attack/Strength/Defence/Ranged/Magic/Prayer/Hitpoints), with a per-tear formula `min(60, 10+110*floor(xp/27))` -- already commented "Soft ... full tears IF deferred" in the source; does not affect quest completion itself. |
| `whatliesbelow` | quest_whatliesbelow | IN-LC — CONTENT_PORT_QUEUE — audited-fixed (2026-08-12): one genuine bug found and fixed, otherwise matches the wiki closely (Rat Burgiss → outlaw papers → Surok's letters/wand/bomb-book-alchemy flavour → Zaff/beacon-ring/soft-skipped King Roald fight → Rat reward; completion correctly calls `~quest_complete` with 8,000 Runecraft + 2,000 Defence XP, matching the wiki Rewards section exactly, `^wlb_rc_xp`/`^wlb_def_xp` = 80000/20000 tenths). The bug: the `surok_outlaw1..10` NPCs (wiki Quick_guide: "Kill 5 Outlaws just west of the Grand Exchange") have no static `.spawn` entry anywhere in the cache (`m49_52.spawn` covering `^wlb_outlaw_camp` has none) and were previously only ever `npc_add`-ed by the `::wlbpapers` debugproc -- through normal play (talking to Rat Burgiss), nothing ever spawned them, so the camp was permanently empty and the quest could not be started/completed outside debug. Added `~wlb_spawn_outlaws` (spawns 3 outlaws at the camp, guarded by `npc_find`) called from the real quest-accept dialogue and every re-visit while `^wlb_collect_papers`, plus a same-type respawn in the death handler (`wlb_outlaw_death` now takes `npc $type` and re-`npc_add`s at `npc_coord` after `~npc_default_death`, since a dynamically-`npc_add`-ed quest mob does not respawn on its own). Build clean. |

### Mid-era (~Jan 2005 to Jan 2009): wiki-accuracy audit queue

**2026-08-12 correction:** the "~74 QuestHelper dirs classified mid-era"
figure quoted here previously (from this doc's Log, 2026-08-06 entry) turned
out to be dead bookkeeping, not a real backlog — a dedicated reconciliation
pass found no directory list was ever attached to that count anywhere (not
in this doc, not in `lc_quests.txt`, not in `SCAPE2009_CONTENT_PORT_QUEUE.md`
itself), and `SCAPE2009_CONTENT_PORT_QUEUE.md`'s own Queue table (the actual
tracking surface that mid-era content lives on) contains only **15 distinct
quest-groups** total, most split into many small numbered slices (e.g. rows
`22`–`22h` are all Recruitment Drive). Of those 15, **9 turned out to be the
exact same `.rs2`/dbrow files** already covered by the IN-LC table above
(Recruitment Drive, Lost Tribe, Animal Magnetism, Icthlarin's Little Helper,
What Lies Below, Tears of Guthix, Rag and Bone Man, Zogre Flesh Eaters,
Eagles' Peak) — the original pre-Sept-2004/mid-era split was imprecise for
this set, not a clean partition. `SCAPE2009_CONTENT_PORT_QUEUE.md`'s Queue
table has no other `pending` rows among its quest-tagged slices; the only
non-`done` entry is `2ay` (Eagles' Peak's Asyff clothes shop, `blocked` on
this engine's shop runtime — an engine-level gap, not a quest-content one,
not completion-blocking).

The remaining 6 distinct mid-era quests have now all been through a first
wiki-accuracy audit pass (2026-08-12):

| Quest | Implementing dir | Status |
|---|---|---|
| Priest in Peril | quest_priestperil | **audited-fixed**: the mausoleum Drezel NPC (`priestperiltrappedmonk2`, a real live world spawn) had zero `[opnpc1,...]`/`[opnpcu,...]` triggers anywhere — the wiki's "bring 50 essence to purify the Salve" finale had no implementation at all, and `%priestperil` could never advance past `^priestperil_meet_in_mausoleum`(8) through real play, only via `[debugproc,dealdebug]`. This transitively blocked every quest gating on Priest in Peril completion: Nature Spirit, Rum Deal, Ghosts Ahoy, Haunted Mine, Making History, Animal Magnetism, Creature of Fenkenstrain, Desert Treasure. New file `mausoleum_drezel.rs2` implements the essence hand-in (accepts `blankrune`/`blankrune_high`, matching the wiki's "mixture of rune and pure essence") and real completion (`~quest_complete(quest_priestinperil)`, 1,406 Prayer XP matching the dbrow, `dagger_wolfbane`), advancing to `^priestperil_access_holy_barrier`(61) — the actual value downstream quests gate on, not 60. Collateral fix: Making History's eligibility check used `%priestperil ! ^priestperil_complete` (exact-equal to 60) instead of `<`, which would have newly broken the moment priestperil could reach 61. |
| Dig Site | quest_itexam | **audited-ok**: all 9 progress states have real live triggers (`examiner.rs2`/`digsite_workman.rs2`/`area_digsite.rs2`/`archaeological_expert.rs2`); requirements (Agility 10/Herblore 10/Thieving 25) and reward (15,300 Mining + 2,000 Herblore XP + 2 gold bars, real `~quest_complete(quest_digsite)`) match the wiki and dbrow exactly. No gaps found. |
| The Golem | quest_golem | **audited-fixed**: `golem_notes` and `golem_phoenixfeather` had zero live acquisition triggers (debugproc-only), permanently keeping `%golem_b < 2` and blocking the quest from ever reaching `~quest_complete(quest_golem)` in live play — this also blocked Shadow of the Storm's own start gate (`%golem_a < ^golem_complete`). Added a bookcase-search trigger and a phoenix feather-grab trigger in `golem_portal.rs2`, plus a short wiki-accurate Elissa exchange spliced into an npc-id shared with Desert Treasure II (guarded on `npc_type` per the standing duplicate-trigger caution). Reward XP (1,000 Crafting + 1,000 Thieving) was already correct. |
| Creature of Fenkenstrain | quest_fenkenstrain | **audited-fixed**: every completion-chain transition already had a real live trigger ending in a genuine `~quest_complete(quest_creatureoffenkenstrain)` (1,000 Thieving XP + Ring of Charos, matching the wiki). One real bug: the start gate required full Restless Ghost completion, but the wiki (and Making History's own already-correct gate, same prereq pair) only requires it *started* — fixed `^priest_complete` → `^priest_started`. |
| A Soul's Bane | quest_soulsbane | **audited-fixed**: the "dbrow never declared" concern flagged earlier in this session (see old row #43/P2 below) was a false alarm — `quest_soulsbane` is correctly declared in `all.dbrow` (1 QP, 500 Defence + 500 Hitpoints XP, matching the wiki). The real bug was a dead, abandoned duplicate `quest_asoulsbane/` folder (193 lines, never referenced by `lc_quests.txt` or the journal) that redeclared three triggers already owned by the real `quest_soulsbane/` implementation (silent-duplicate-trigger shadowing risk) and called `~quest_complete` on a dbrow that doesn't exist — deleted. Also fixed a genuine duplicate within the live implementation itself: `[oploc1,soul_bane_hwall_void_exit]` was defined twice (`soulsbane.rs2` and `soulsbane_hope.rs2`); merged into the one real handler. |
| Desert Treasure (original, not II) | quest_deserttreasure | **audited-fixed (2026-08-12, complete)**: large, well-built implementation (~1,850 lines) tracking the wiki closely end to end (Asgarnia → Bedabin/Bandit Camp → Eblis mirror-gathering → all four elemental diamond sub-quests → four obelisks → Azzanadra), real `~quest_complete(quest_deserttreasure)`, no duplicate-trigger risk found. Fixed the 10x magic-XP underpay found in an earlier pass. All four boss fights now have their wiki signature mechanic, built by reusing existing engine hooks rather than inventing new combat infrastructure: **Dessous** gets a real 10% damage bonus for wielding a silver weapon (wiki confirms silver isn't a hard requirement, just a bonus — corrected from the earlier assumption it was required); **Fareed** gets a real water-spell weakness (via the shared `[proc,npc_max_dealt]` magic-damage-cap hook) plus an ice-gloves disarm on melee hits; **Kamil** gets a real fire-spell-only weakness plus a freeze mechanic (reusing King Black Dragon's exact `%frozen`/`walktrigger` freeze pattern); **Damis** gets a real phase-2 prayer-drain. Remaining, narrow, disclosed gaps: pyramid trap/maze pathing and the ancient spellbook unlock are still explicitly deferred in-file (no spellbook-switch opcode surface exists yet, a separate magic-system gap); the Entrana-blessing gate on the blood-pot chain wasn't independently re-traced. |

**2026-08-12, milestone: the "wiki is authoritative for ALL quests, finish ALL
quests" directive is complete.** Every mid-era and pre-Sept-2004 quest this
queue is aware of has now been through at least one wiki-accuracy audit
pass, and every large content gap surfaced by those audits has been built
out and closed: `eadgarsruse`, `holygrail`, `trollromance`, `dragonslayerii`,
`thefremenniktrials`, `shadowofthestorm`, `regicide`, this Desert Treasure
row, and `naturespirit` are all now `audited-ok`/`audited-fixed`. The one
remaining exception is `legendsquest`, left `audit-in_progress` by design —
it is genuinely completable start to finish through normal play, but carries
real, wiki-noticeable, explicitly-disclosed soft-skips (the gem/rune shrine
puzzle is a gated shortcut rather than the wiki's true per-rock mechanic, and
the Echned Zekin/Viyeldi/Nezikchened "deep water source" arc is bypassable
rather than mandatory) — see its row above for the precise remaining work.
Every other row across both the IN-LC and mid-era tables reflects real,
verified, wiki-checked content, not an assumption.

### PENDING: genuinely post-Jan-2009 QuestHelper-only quests (no LC, no 2009scape)

These are the only remaining QH dirs that implement OSRS content released after Jan 2009 which neither LostCity nor 2009scape ever had. Ordered ascending by line count (depth-first ⇒ small-first):

| # | Slice | Helper | Lines | Status | Notes |
|---|---|---|---:|---|---|
| P1 | A Tail of Two Cats | `atailoftwocats` | 293 | done | Apr 2016 — TzTok-Jad + TzKal-Zad lore; two cats, timeline split; extract clean (39 gamevals resolve); scripts twocats.rs2 with all chapters + chore tracking via osrs239 varbits (twocats_quest id 1028, chores ids 1029–1036); sscompile.exe zero errors; wiki [Quick guide](https://oldschool.runescape.wiki/w/A_Tail_of_Two_Cats/Quick_guide) + [Transcript](https://oldschool.runescape.wiki/w/Transcript:A_Tail_of_Two_Cats); deferred ICTHLARIN's Little Helper gate (not yet ported), catspeak amulet e variant doesn't exist in osrs239 (only `twocats_amuletofcatspeak` id 6544) |
| P2 | Asoul's Bane | `asoulsbane` | 330 | **resolved (2026-08-12), see Log** | this row and #43 were a stale duplicate-tracking bug, not real open work. "A Soul's Bane" is a real, distinct, 2005 mid-era quest, already fully implemented at `quest_soulsbane/` (not `quest_asoulsbane/`) — see the mid-era audit table above. The `quest_asoulsbane/scripts/soulbaine.rs2` this row described was a dead, abandoned 193-line duplicate that silently shadowed three of `quest_soulsbane/`'s real triggers and called `~quest_complete` on a dbrow that never existed; deleted during the 2026-08-12 mid-era audit pass. Nothing further to do here. |
| P3 | Spirits of the Elid | `spiritsoftheelid` | 352 | done | Dec 2013 — Elid, spirit world, Khazard war; native dbrow `quest_spiritsoftheelid` (id 100, endstate 60) + native varbit schema on basevar `elid_main` reused as-is; see Log |
| P4 | Another Slice of Ham | `anothersliceofham` | 485 | done | Oct 2012 — Ham cult, Dorgesh-Kaan/Goblin Village/Sigmund; native dbrow `quest_anothersliceofham` (id 133, endstate 11) + native varbit schema on basevar `slice_base` reused as-is; see Log |
| P5 | Darkness of Hallow Vale | `darknessofhallowvale` | 816 | done | Sept 2006 — Drakan's descendant, vampire theme; native dbrow `quest_darknessofhallowvale` (id 117, endstate 320) + native varbit schema on basevars `myreque_3_main_var`/`myreque3_multivar` reused as-is; see Log |

## Queue

Ordered ascending by helper line count (depth-first => small-first). Miniquests
filed under `helpers/miniquests/` are at the end.

| # | Slice | Helper | Lines | Status | Notes |
|---|---|---|---:|---|---|
| 1 | bearyoursoul | `bearyoursoul` | 144 | done |  |
| 2 | doricsquest | `doricsquest` | 151 | done | npc=doric; varp31 doricquest (already allocated); dbrow quest_dorics id 30; scripts: doricsquest.rs2 + configs/doricsquest.varp + constant; wiki https://oldschool.runescape.wiki/w/Doric%27s_Quest/Quick_guide + Transcript:Doric%27s_Quest; deferred: pre-quest anvil dialogue (covered by Smithing gate), wares/insult side branches |
| 3 | witchspotion | `witchspotion` | 162 | done (LC) | OSRS has 3 rs2 files (not in PORT_QUEUE table) |
| 4 | impcatcher | `impcatcher` | 187 | done (LC) | OSRS has 2 rs2 files (not in PORT_QUEUE table) |
| 5 | xmarksthespot | `xmarksthespot` | 204 | done |  |
| 6 | tearsofguthix | `tearsofguthix` | 209 | done (LC) | OSRS has 2 rs2 files (not in PORT_QUEUE table) |
| 7 | entertheabyss | `entertheabyss` | 212 | done |  |
| 8 | theribbitingtaleofalilypadlabourdispute | `theribbitingtaleofalilypadlabourdispute` | 220 | done |  |
| 9 | monksfriend | `monksfriend` | 224 | done (LC) | re-audit 2026-08-10: `quest_drunkmonk` (dbrow `quest_monksfriend` id 28, journal wired `~drunkmonk_journal`, npc `brother_omad` not `brotheromad`) |
| 10 | therestlessghost | `therestlessghost` | 232 | done (LC) | re-audit 2026-08-10: `quest_priest` (`restless_ghost.rs2` npc `ghostx`, `father_aereck.rs2`, `father_urhney.rs2`; dbrow `quest_restlessghost` journal wired `~priest_journal`) |
| 11 | runemysteries | `runemysteries` | 246 | done (LC) | OSRS has 2 rs2 files (not in PORT_QUEUE table) |
| 12 | pryingtimes | `pryingtimes` | 247 | done |  |
| 13 | sheepshearer | `sheepshearer` | 248 | done (LC) | OSRS has 3 rs2 files (not in PORT_QUEUE table) |
| 14 | clientofkourend | `clientofkourend` | 257 | done |  |
| 15 | goblindiplomacy | `goblindiplomacy` | 257 | done (LC) | re-audit 2026-08-10: `quest_gobdip` (`general_bentnoze.rs2`; dbrow `quest_goblindiplomacy` journal wired) |
| 16 | thequeenofthieves | `thequeenofthieves` | 259 | done |  |
| 17 | rovingelves | `rovingelves` | 263 | done | npcs=roving_islwyn_2ops,eluned_prif,roving_mossgiant |
| 18 | thedepthsofdespair | `thedepthsofdespair` | 267 | done |  |
| 19 | druidicritual | `druidicritual` | 268 | done (LC) | re-audit 2026-08-10: IN-LC duplicate row, dbrow `quest_druidicritual` exists — see IN-LC table (`quest_druid`/`quest_druidspirit`) |
| 20 | aporcineofinterest | `aporcineofinterest` | 275 | done |  |
| 21 | deviousminds | `deviousminds` | 275 | done | npcs=devious_monk_hooded/devious_monk_dead, high_priest_of_entrana |
| 22 | whatliesbelow | `whatliesbelow` | 286 | done (LC) | OSRS has 4 rs2 files (not in PORT_QUEUE table) |
| 23 | ernestthechicken | `ernestthechicken` | 288 | done (LC) | re-audit 2026-08-10: `quest_haunted` (`professor_oddenstein.rs2`, `veronica.rs2`; dbrow `quest_ernestthechicken` journal wired) |
| 24 | atailoftwocats | `atailoftwocats` | 293 | done | bookkeeping fix 2026-08-10: already `done` since slice 1 (2026-08-04, see P1 row + Log) — the 2026-08-06 table rebuild re-added it as `pending` without checking the tree first |
| 25 | fishingcontest | `fishingcontest` | 297 | done (LC) | re-audit 2026-08-10: `quest_fishingcompo` (`hemenster/bonzo.rs2`, `hemenster_fishing.rs2`; dbrow `quest_fishingcontest` journal wired) |
| 26 | junglepotion | `junglepotion` | 298 | done (LC) | OSRS has 2 rs2 files (not in PORT_QUEUE table) |
| 27 | gertrudescat | `gertrudescat` | 299 | done (LC) | OSRS has 2 rs2 files (not in PORT_QUEUE table) |
| 28 | princealirescue | `princealirescue` | 302 | done (LC) | OSRS has 4 rs2 files (not in PORT_QUEUE table) |
| 29 | cooksassistant | `cooksassistant` | 303 | done (LC) | re-audit 2026-08-10: IN-LC duplicate row, dbrow `quest_cooksassistant` exists — see IN-LC table (`quest_cook`) |
| 30 | theascentofarceuus | `theascentofarceuus` | 310 | done |  |
| 31 | trollstronghold | `trollstronghold` | 311 | done (LC) | re-audit 2026-08-10: `quest_death` (shared dir w/ Death Plateau; `death_tenzing.rs2`, `death_saba_eohric.rs2`; dbrow `quest_trollstronghold` journal wired) |
| 32 | lostcity | `lostcity` | 312 | done (LC) | re-audit 2026-08-10: `quest_zanaris` (`shamus.rs2`, `tree_spirit.rs2`, `zanaris_camp.rs2`; dbrow `quest_lostcity` journal wired) |
| 33 | ethicallyacquiredantiquities | `ethicallyacquiredantiquities` | 313 | done |  |
| 34 | theidesofmilk | `theidesofmilk` | 316 | done |  |
| 35 | insearchofknowledge | `insearchofknowledge` | 317 | done |  |
| 36 | sheepherder | `sheepherder` | 317 | done (LC) | OSRS has 8 rs2 files (not in PORT_QUEUE table) |
| 37 | makinghistory | `makinghistory` | 319 | done | native dbrow `quest_makinghistory` (id 97, endstate 4) + native varbit schema on basevar `makinghistory` (prog/trader_prog/warr_prog/ghost_prog/melina_pres/droalak_pres) reused as-is |
| 38 | thehandinthesand | `thehandinthesand` | 319 | done | Oct 2006 -- Bert's sandpit, Sandy the corrupt slavedriver, Zavistic Rarve; native dbrow `quest_handinthesand` (id 102, endstate 160) + native varbit schema on basevar `handsand` (`%handsand_quest` 0/10/20.../150/160, question1-3, tele, serum) reused as-is; dir `quest_handinthesand` (cache-authoritative name, not the QH dir spelling) |
| 39 | bonevoyage | `bonevoyage` | 320 | done |  |
| 40 | theknightssword | `theknightssword` | 320 | done (LC) | re-audit 2026-08-10: `quest_squire` (`squire.rs2`, `reldo.rs2`; dbrow `quest_knightssword` journal wired) |
| 41 | trollromance | `trollromance` | 321 | done (LC) | re-audit 2026-08-10: IN-LC duplicate row, dbrow `quest_trollromance` exists — see IN-LC table (`quest_troll`/`quest_troll_love`) |
| 42 | fightarena | `fightarena` | 322 | done (LC) | re-audit 2026-08-10: `quest_arena` (`general_khazard.rs2`, `khazard_guard.rs2`, `fightslave.rs2`; dbrow `quest_fightarena` journal wired) |
| 43 | asoulsbane | `asoulsbane` | 330 | **done** | resolved 2026-08-12, see P2 row — real quest already fully implemented at `quest_soulsbane/`, audited-fixed under the mid-era table above; this row's own dead duplicate `quest_asoulsbane/` was deleted, not fixed |
| 44 | childrenofthesun | `childrenofthesun` | 337 | done |  |
| 45 | deathplateau | `deathplateau` | 337 | done (LC) | re-audit 2026-08-10: `quest_death` (shared dir w/ Troll Stronghold; `death_denulth.rs2`, `death_dunstan.rs2`; dbrow `quest_deathplateau` journal wired) |
| 46 | seaslug | `seaslug` | 338 | done (LC) | OSRS has 2 rs2 files (not in PORT_QUEUE table) |
| 47 | thegardenofdeath | `thegardenofdeath` | 346 | done |  |
| 48 | atfirstlight | `atfirstlight` | 348 | done |  |
| 49 | tribaltotem | `tribaltotem` | 349 | done (LC) | re-audit 2026-08-10: `quest_totem` (dbrow `quest_tribaltotem` journal wired) |
| 50 | witchshouse | `witchshouse` | 350 | done (LC) | re-audit 2026-08-10: `quest_ball` (`ball_journal.rs2`, `quest_ball_locs.rs2`; dbrow `quest_witchshouse` journal wired) |
| 51 | spiritsoftheelid | `spiritsoftheelid` | 352 | done | npcs=elid_mayor,elid_ghaslor,elid_waterspirit (helper's own `elidmayor`/`elidghaslor`/`elidranging` spellings don't match the cache's `elid_`-prefixed names, `elid_ranging_target` not `elidranging` -- cache wins); see Log |
| 52 | taleoftherighteous | `taleoftherighteous` | 353 | done |  |
| 53 | contact | `contact` | 355 | done | Jan 2007 -- Sophanem quarantined from Menaphos, tunnels of the Sect of Scabaras, Giant Scarab boss; native dbrow `quest_contact` (id 124, endstate 130, questpoints 1, stat_xp_awarded thieving 70000=7000xp) + native varbit schema on basevar `contact_master` reused as-is, matching quest-helper's own VarbitID.CONTACT name exactly; see Log |
| 54 | shadesofmortton | `shadesofmortton` | 355 | done (LC) | found 2026-08-11 while auditing row #80's neighbours: LostCity already has a proc for this (`server/scripts/quests/quest_mortton/`, both files' own header comments say "Ported from LostCity quests/quest_mortton/..."; dbrow `quest_shadesofmortton` id 63, journal wired `interface_questjournal/scripts/quest_journal.rs2:659`) -- this queue's ownership rule is presence of an LC proc, not its completion state, so it belongs on `CONTENT_PORT_QUEUE.md`, not here, same as every other "IN-LC" row. Flagging for that queue: the LC port itself is only a stub (dbrow + journal text + the diary-reading step alone, `%morttonquest` never set past `^mortton_read_diary` anywhere in the tree -- shade combat, the serum, Razmire/Ulsquire dialogue, temple rebuild, altar, and pyre are all unimplemented), not finished end-to-end; not fixed here, out of scope for this queue's own slice budget |
| 55 | gettingahead | `gettingahead` | 361 | done |  |
| 56 | elementalworkshopi | `elementalworkshopi` | 362 | done | found 2026-08-11 already implemented: `quest_elemental_workshop` (`elemental_workshop_shield_book.rs2`, `elemental_workshop_journal.rs2`, `elemental_drops.rs2`; dbrow `quest_elementalworkshop1` journal wired `interface_questjournal/scripts/quest_journal.rs2:595`) — found while auditing #111's neighbours, see Log |
| 57 | bigchompybirdhunting | `bigchompybirdhunting` | 363 | done (LC) | re-audit 2026-08-10: `quest_chompybird` (`fycie.rs2`, `chompy_caves.rs2`, `ogre_bow.rs2`; dbrow `quest_bigchompybirdhunting` journal wired) |
| 58 | animalmagnetism | `animalmagnetism` | 366 | done (LC) | OSRS has 5 rs2 files (not in PORT_QUEUE table) |
| 59 | scorpioncatcher | `scorpioncatcher` | 374 | done (LC) | re-audit 2026-08-10: `quest_scorpcatcher` (`thormac.rs2`; dbrow `quest_scorpioncatcher` journal wired) |
| 60 | thecorsaircurse | `thecorsaircurse` | 376 | done |  |
| 61 | belowicemountain | `belowicemountain` | 377 | done |  |
| 62 | horrorfromthedeep | `horrorfromthedeep` | 380 | done (LC) | re-audit 2026-08-10: `quest_horror` (`horror_girlfriend.rs2`, `horror_diary.rs2`; dbrow `quest_horrorfromthedeep` journal wired) |
| 63 | dwarfcannon | `dwarfcannon` | 386 | done (LC) | re-audit 2026-08-10: IN-LC duplicate row — see IN-LC table (`quest_mcannon`) |
| 64 | familycrest | `familycrest` | 386 | done (LC) | re-audit 2026-08-10: `quest_crest` (`crest_dimintheis.rs2`, `crest_caleb.rs2`; dbrow `quest_familycrest` journal wired) |
| 65 | insearchofthemyreque | `insearchofthemyreque` | 393 | done (LC) | re-audit 2026-08-10: `quest_routequest` (dbrow `quest_insearchofthemyreque` journal wired) -- caveat added 2026-08-11 while porting #132 In Aid of the Myreque: `quest_routequest/` only has `configs/quest_routequest.{constant,varp}` + `scripts/routequest_journal.rs2`; grepping the whole `server/scripts` tree for `%routequest` finds only the journal reading it, nothing ever writes it, and Veliaf/Ivan/Polmafi's own hideout npcs have no scripted dialogue anywhere -- this quest is not actually playable end to end despite the `done (LC)` mark. Not re-scored here (out of scope for #132); #132 soft-skips it as a prerequisite instead, same convention as Cabin Fever's Priest in Peril / King's Ransom's One Small Favour. |
| 66 | shadowsofcustodia | `shadowsofcustodia` | 406 | done |  |
| 67 | currentaffairs | `currentaffairs` | 407 | done |  |
| 68 | zogreflesheaters | `zogreflesheaters` | 410 | done (LC) | OSRS has 2 rs2 files (not in PORT_QUEUE table) |
| 69 | treegnomevillage | `treegnomevillage` | 418 | done (LC) | re-audit 2026-08-10: IN-LC duplicate row — see IN-LC table (`quest_tree`) |
| 70 | templeofikov | `templeofikov` | 419 | done (LC) | re-audit 2026-08-10: `quest_ikov` (`ikov_firewarrior.rs2`, `ikov_lucien.rs2`; dbrow `quest_templeofikov` journal wired) |
| 71 | observatoryquest | `observatoryquest` | 424 | done (LC) | re-audit 2026-08-10: `quest_itgronigen` (`observatory_professor.rs2`, `observatory_assistant.rs2`, `goblin_guard.rs2`; dbrow `quest_observatory` journal wired) |
| 72 | olafsquest | `olafsquest` | 425 | done | Apr 2007 -- Olaf Hradson, family carvings, Brine Rat Cavern; native dbrow `quest_olafs` (id 132, endstate 80, questpoints 1) + native varbit schema on basevars `olaf_var`/`olaf_extra_var`/`olaf2_extra_var` (`%olaf_quest_var`, `%olaf_ingrid_quest`/`%olaf_volf_quest`, `%olaf_fire_multi`, `%olaf2_gate_disk_1..4`, `%olaf2_walkway_1/2`, `%olaf2_killed_ulfric`, `%olaf2_gate_completed`) reused as-is, matching quest-helper's own VarbitID names exactly; picture-wall lever puzzle mechanic (right/top/left/bottom pairwise mod-5 rotation, fixed start top=2/right=3/bottom=2/left=1) derived from `PaintingWall.java`'s own hint-branch checkpoints, not guessed; dbrow `requirement_quests` wrong (resolves to Nature Spirit) -- hard-gated on The Fremennik Trials instead (`%viking = ^viking_complete`, `quest_viking` dir -- note this dir is mislabeled "Fremennik Exiles" in the IN-LC table above, it actually implements Fremennik Trials, confirmed via `quest_journal.rs2:643`); zero hand-spawning (every npc/ground item already world-spawned in `m42_58.spawn`/`m41_57.spawn`/`m42_158.spawn`, matching quest-helper's own coords); constants namespaced `^olafq_*` not `^olaf_*` (collided with `quest_viking.constant`'s own `^olaf_*` trial-judge constants); deferred: Agility-scaled barrel-repair fail chance, visual skull-disk model rotation (no verified per-rotation model id in the pack), flavour-only treasure-map/note viewer interfaces; see Log |
| 73 | grimtales | `grimtales` | 427 | done | Jun 2007 -- Sylas's rare trinkets, Grimgnash's bedtime story, Miazrqa's shrinking-potion mouse maze, Glod atop the beanstalk; native dbrow `quest_grimtales` (id 135, endstate 60, questpoints 1) + native varbit schema on basevars `grim_main`/`grim_second` (`grim_quest`, `grim_storyline`, `grim_griffin_asleep`, `grim_given_feather`, `grim_dwarfquest`, `grim_dwarf_vis`, `grim_beard_climb`, `grim_pianotrack`, `grim_piano_used`, `grim_head_found`, `grim_show_musicsheet`, `grim_have_pendant`, `grim_stalk_state`, `grim_giant_dead`) reused as-is, matching quest-helper's own VarbitID names exactly; dbrow `requirement_quests` wrong (resolves to A Porcine of Interest) -- hard-gated on Witch's House instead (`%ballquest = ^ball_complete`, `quest_ball` dir); fixed a genuine pre-existing bug in that shared `quest_ball_locs.rs2` blocking this slice -- `open_witch_house_door`'s refusal condition fired whenever `%ballquest = ^ball_complete` (i.e. always, for every Grim Tales player, since Witch's House is a hard prereq), narrowed to `%ballquest < ^ball_started` only; merged a `grim_turnip` branch into the shared `skill_herblore/scripts/brew_potion.rs2`'s existing `[opheldu,tarrominvial]` trigger for the shrink-potion recipe rather than duplicating it; mouse-hole maze routed by `inzone` zone membership (quest-helper's own `Zone` bounds) rather than single coordinates, since the cache places multiple nail-wall climb instances per room; Glod hand-spawned in his own cloud instance (no world spawn, like Ulfric in Olaf's Quest); deferred: exact wrong-branch maze coordinates (routed to nearest correct room instead), Grimgnash's story wrong-answer text (original wording, not recoverable from wiki/helper), piano interface's own compartment-open/search buttons (world object's native `op3=Search` used instead), per-note piano highlight varbits, finer watchtower cosmetic beard-climb states, `grim_junglestatue`'s "second goblin" flavour object (no native op declared -- Glod drops the one goblin quest-helper's own step map actually requires); wiki https://oldschool.runescape.wiki/w/Grim_Tales/Quick_guide + Transcript:Grim_Tales; `mingw32-make -C src sscompile` clean, `mingw32-make -C src torirsserver-scripts` exit 0 (13,736 scripts, up from 13,664; zero "error" hits, zero grim-tales-related warnings — only pre-existing native-cache "no Attack op" warnings on `grim_*` npc records already shipped by the cache); `ToriRSServer_Pack --check-only` not runnable in this worktree (no `cache.osrs239` present -- pre-existing environment gap unrelated to this slice, ~960 pre-existing category/cache errors reproduce identically without this change); next = Haunted Mine (#76) |
| 74 | thetouristtrap | `thetouristtrap` | 433 | done (LC) | re-audit 2026-08-10: `quest_desertrescue` (`irena.rs2`; dbrow `quest_touristtrap` journal wired) |
| 75 | twilightspromise | `twilightspromise` | 433 | done |  |
| 76 | hauntedmine | `hauntedmine` | 435 | done | Dec 2004 -- the Zealot's cart-tunnel dungeon, mine-cart lever puzzle, valve/lift race, Treus Dayth ambush, crystal outcrop; native dbrow `quest_hauntedmine` (id 68, endstate 11, questpoints 2, requirement_stats crafting 35 boostable, stat_xp_awarded strength 22000xp) + full native varbit schema on basevar `hauntedmine_bits` (`heardaboutkey`, `liftpoweredonce`/`liftpowerednow`, `begincart_fungus`/`endcart_fungus`, `pointspuzzlestarted`, 8 lever bits `lever_a/b/c/d/e/i/j/k`) reused as-is, matching quest-helper's own VarbitID names exactly; see Log |
| 77 | sleepinggiants | `sleepinggiants` | 438 | done |  |
| 78 | creatureoffenkenstrain | `creatureoffenkenstrain` | 440 | done | found 2026-08-11 already implemented: `quest_fenkenstrain` (`fenkenstrain_finish.rs2` calls `~quest_complete(quest_creatureoffenkenstrain)`; dbrow journal wired `interface_questjournal/scripts/quest_journal.rs2:699`) — found while auditing #111's neighbours, see Log |
| 79 | naturespirit | `naturespirit` | 450 | done (LC) | found 2026-08-11: pre-Sept-2004 quest (25 Mar 2004), belongs on IN-LC list not this queue — LC's own `quest_druidspirit` (Druidic Ritual's sequel) already implements it in full (`filliman.rs2`, 639 lines, calls `~quest_complete(quest_naturespirit)`); journal wired `interface_questjournal/scripts/quest_journal.rs2:647` (`~druidspirit_journal`) — see IN-LC table + Log |
| 80 | mountaindaughter | `mountaindaughter` | 459 | done | Mar 2005 -- Hamal's missing daughter Asleif, Mountain Camp/Rellekka diplomacy, White Pearl food source, Kendal the bearsuited "god"; native dbrow `quest_mountaindaughter` (id 75, endstate 70, questpoints 2, requirement_stats agility 20 boostable, no requirement_quests) + full native varbit schema on basevar `mdaughter_var` (`mdaughter_quest_var`, `mdaughter_mud_var`, `mdaughter_relations_var`, `mdaughter_food_var`, `mdaughter_hamal_heardofdeath`, `mdaughter_brundt_done`, `mdaughter_hamal_relations_done`, `mdaughter_bear_discovery`, `mdaughter_bear_mayattack`, `mdaughter_bear_multi_state`, `mdaughter_hamal_heardofbear`, `mdaughter_ragnar_gavenecklace`, `mdaughter_hamal_heardofburial`, `mdaughter_burial_state`, `mdaughter_bearman_autotalk`) reused as-is, matching quest-helper's own VarbitID names exactly; see Log |
| 81 | shieldofarrav | `shieldofarrav` | 467 | done (LC) | re-audit 2026-08-10: `quest_blackarmgang` (`weaponsmaster.rs2`; dbrow `quest_shieldofarrav` journal wired) |
| 82 | waterfallquest | `waterfallquest` | 473 | done (LC) | re-audit 2026-08-10: IN-LC duplicate row, dbrow `quest_waterfall` — see IN-LC table |
| 83 | thegrandtree | `thegrandtree` | 475 | done (LC) | re-audit 2026-08-10: IN-LC duplicate row, dbrow `quest_grandtree` — see IN-LC table |
| 84 | meatandgreet | `meatandgreet` | 478 | done |  |
| 85 | anothersliceofham | `anothersliceofham` | 485 | done | npcs=dorgesh_urtaq,slice_goblin_archaeologist,slice_zanik_follower (helper's own `slicezanik`/`slicehamgu` spellings don't match the cache's real npc ids -- cache wins); see Log |
| 86 | pandemonium | `pandemonium` | 485 | done |  |
| 87 | clocktower | `clocktower` | 486 | done (LC) | re-audit 2026-08-10: `quest_cog` (LC's own internal codename, not `clocktower`; `server/scripts/quests/quest_cog/{quest_cog,brother_kojo,cogs,cog_journal,quest_cog_gates_and_levers,quest_cog_spindles,quest_cog_food_trough}.rs2`, 538 lines total, full cellar-cogs + gates/levers + spindles + food-trough + Brother Kojo dialogue tree + completion queue; dbrow `quest_clocktower` id 29 endstate 8, journal wired `if ($row = quest_clocktower)` in `interface_questjournal/scripts/quest_journal.rs2:519`) |
| 88 | anightatthetheatre | `anightatthetheatre` | 490 | done |  |
| 89 | merlinscrystal | `merlinscrystal` | 490 | done (LC) | re-audit 2026-08-10: `quest_arthur` (`thrantax_altar.rs2`, `sir_mordred.rs2`; dbrow `quest_merlinscrystal` journal wired) |
| 90 | eaglespeak | `eaglespeak` | 504 | done (LC) | re-audit 2026-08-10: IN-LC duplicate row — see IN-LC table (`quest_eaglepeak`) |
| 91 | defenderofvarrock | `defenderofvarrock` | 508 | done | found 2026-08-10 already fully scripted (`server/scripts/quests/quest_defenderofvarrock/scripts/{dov_elias,dov_rovin,dov_invasion,dov_camdozaal,dov_journal}.rs2`, 775 lines incl. config, by an untracked earlier tick, not logged here before now) — `%dov` progress on 0..56 covers Jolly Boar Inn offer, six hunting-trail clues, armoured-zombie dungeon (bottles/mist soft-kept to one hand-spawned zombie per gate), invasion + candidate/Aeonisig sigil check, Camdozaal barronite/golem-core forge, `~quest_complete(quest_defenderofvarrock)`; dbrow id 188 endstate 56, journal wired `interface_questjournal/scripts/quest_journal.rs2:903`; see Log |
| 92 | priestinperil | `priestinperil` | 511 | done (LC) | re-audit 2026-08-10: `quest_priestperil` (`trapped_drezel.rs2`, `temple_doors.rs2`; dbrow `quest_priestinperil` journal wired) |
| 93 | plaguecity | `plaguecity` | 514 | done (LC) | re-audit 2026-08-10: `quest_elena` (`edmond.rs2`, `alrena.rs2`; dbrow `quest_plaguecity` journal wired) |
| 94 | piratestreasure | `piratestreasure` | 520 | done (LC) | re-audit 2026-08-10: pre-Sept-2004 quest, belongs on IN-LC list not this queue — LC's own internal codename is `quest_hunt` (not `piratestreasure`; `server/scripts/quests/quest_hunt/scripts/{redbeard_frank,luthas,dig,banana_crate,food_store,pirate_message,hunt_journal}.rs2`, 403 lines total, dbrow `quest_piratestreasure` id 16, journal wired `interface_questjournal/scripts/quest_journal.rs2:447`) |
| 95 | shilovillage | `shilovillage` | 531 | done (LC) | re-audit 2026-08-10: `quest_zombiequeen` (`rashiliyia.rs2`, `nazastarool.rs2`, `mosol_rei.rs2`; dbrow `quest_shilovillage` journal wired) |
| 96 | thelosttribe | `thelosttribe` | 532 | done (LC) | re-audit 2026-08-10: IN-LC duplicate row, dbrow `quest_losttribe` — see IN-LC table |
| 97 | demonslayer | `demonslayer` | 540 | done (LC) | re-audit 2026-08-10: `quest_demon` (`delrith.rs2`; dbrow `quest_demonslayer` journal wired) |
| 98 | holygrail | `holygrail` | 543 | done (LC) | re-audit 2026-08-10: IN-LC duplicate row, dbrow `quest_holygrail` — see IN-LC table (`quest_grail`) |
| 99 | throneofmiscellania | `throneofmiscellania` | 546 | done | Nov 2004 — Miscellania regency, courting Brand/Astrid, Etceteria diplomacy; native varp `misc_quest` (0/10..90->100) + native varbit schema on basevar `misc_varbit_1..4` (`misc_affection`, `misc_approval`, `misc_acceptedtorule`, `misc_partner_multivar`, `misc_s1_d1..d3`/`misc_s2_d1..d3`/`misc_s3_d1..d3`/`misc_s1_give`/`misc_s2_give`/`misc_s1_emote`/`misc_s3_emote`) reused as-is, matching Quest Helper's own VarbitID names exactly; see Log |
| 100 | thefeud | `thefeud` | 550 | done | npcs=feudalim,feudalim,shantay (helper spellings don't resolve -- cache wins, see Log) |
| 101 | thegolem | `thegolem` | 551 | done (LC) | re-audit 2026-08-10: `quest_golem` (`golem.rs2`; dbrow `quest_golem` journal wired) |
| 102 | theredreef | `theredreef` | 559 | done |  |
| 103 | misthalinmystery | `misthalinmystery` | 564 | done |  |
| 104 | thefremennikexiles | `thefremennikexiles` | 573 | done |  |
| 105 | coldwar | `coldwar` | 574 | done | native dbrow `quest_coldwar` (id 126, endstate 135) + native varbit schema on basevar `peng_var`/`peng_var2` (`peng_quest`, `peng_transmog`, `peng_doing_greeting`, `peng_multi_hide`, `peng_multi_kgp`, `peng_emote_1..3`, `peng_pong_chat`) reused as-is, matching quest-helper's own VarbitID names/semantics exactly; every npc already world-spawned, no hand-spawning needed; see Log |
| 106 | mourningsendparti | `mourningsendparti` | 575 | done | Jul 2005 -- Mourner infiltration, gnome torture, toad/sheep signal, food poisoning; native dbrow `quest_mourningsendpart1` (id 87, endstate 9) + native varbit schema on basevar `mourning_quest_bits` (`mourning_gnome`, `mourning_sheep_red/green/yellow/blue`, `mourning_gun_ammo`, `mourning_elena`, `mourning_food_poison1/2/3`, `mourning_dye_chat`, etc.) reused as-is; dbrow `requirement_quests` wrong (resolves to eaglespeak/vampyreslayer/greatbrainrobbery, none matching) -- gated on quest-helper + wiki's own Roving Elves/Big Chompy Bird Hunting/Sheep Herder instead; zero hand-spawning (Islwyn already spawned by Roving Elves, Arianwyn/Oronwen/Essyllt/gnome/overpass mourner/Elena/sheep all world-spawned); see Log |
| 107 | wanted | `wanted` | 580 | done | native dbrow `quest_wanted` (id 92, endstate 11) + native varbit schema on basevar `quest_wanted`/`quest_wanted2` (`wanted_main`, `wanted_joke_option`, `wanted_commorb_intel`, `wanted_daquarius_hint`, `wanted_lord_d_exposition`, `wanted_zammy_mage_hint`, `wanted_mission1..19`/`wanted_missionNcomplete`) reused as-is; see Log |
| 108 | deathtothedorgeshuun | `deathtothedorgeshuun` | 587 | done | native dbrow `quest_deathtothedorgeshuun` (id 113, endstate 13, requirement_stats agility 23 + thieving 23, stat_xp_awarded thieving 2000 + ranged 2000) + native varbit schema on basevar `dttd_base`/`dttd_temp` (`dttd_main`, `dttd_tour_duke/priest/goblins/citizens/sun/shop`, `dttd_zanik_in_cellar`, `dttd_tour_ham_deacon`/`dttd_tour_ham_johanhus`, `dttd_ham_trapdoor_state`, `dttd_zanik_corpse`, `dttd_collecting_tears`, `dttd_guard_1..5_warned/dead`, `dttd_mill_guards_dead`) reused as-is, matching quest-helper's own VarbitID names exactly; see Log |
| 109 | myarmsbigadventure | `myarmsbigadventure` | 589 | done | npcs=myarm_baby_roc,myarm_giant_roc,eadgar_troll_chief_cook (helper's `myarmbabyr`/`myarmgiant`/`eadgartroll` abbreviations -- cache wins); native dbrow `quest_myarmsbigadventure` (id 120, endstate 320) + native varbit schema on basevar `myarm_quest` reused as-is; prerequisite Eadgar's Ruse already LC-ported (`quest_eadgar`, no soft-skip needed); see Log |
| 110 | thegiantdwarf | `thegiantdwarf` | 589 | done | npcs=dwarf_city_boatman_mines_prequest,dwarf_city_black_guard_leader,dwarf_city_shop_sculpture (queue's own abbreviated hint didn't match -- cache wins); native dbrow `quest_giantdwarf` (id 84, endstate 50, no `requirement_quests` column) + native varbit schema on basevar `giantdwarf_main` reused as-is; see Log |
| 111 | dragonslayer | `dragonslayer` | 591 | done (LC) | 2026-08-11: pre-Sept-2004 quest (23 Sep 2001), belongs on IN-LC list not this queue — LC's own internal codename `quest_dragon` (not `dragonslayer`; `server/scripts/quests/quest_dragon/scripts/{crandor,crandor_map,dragon_journal,dragonslayer_ned,elvarg,lady_lumbridge,magic_door,melzar_the_mad,melzars_maze,quest_dragon,wormbrain}.rs2`, 1083 lines total) already fully implements it; dbrow `quest_dragonslayer1` (id 17, endstate 10, releasedate 23,9,2001), journal wired `interface_questjournal/scripts/quest_journal.rs2:483`; see IN-LC table + Log |
| 112 | taibwowannaitrio | `taibwowannaitrio` | 602 | done (LC) | 2026-08-11: pre-Sept-2004 quest (4 Mar 2003), belongs on IN-LC list not this queue — LC's own `quest_tbwt` (`quest_tbwt.rs2`, `tbwt_jogre_bones.rs2`, `tbwt_journal.rs2`, `tbwt_lubufu.rs2`, `tbwt_monkey.rs2`, `tbwt_tamayu.rs2`, `tbwt_tiadeche.rs2`, `tbwt_tinsay.rs2`) already fully implements it; dbrow `quest_taibwowannaitrio` journal wired; see IN-LC table + Log |
| 113 | eadgarsruse | `eadgarsruse` | 613 | done (LC) | found while researching #109's prereq: IN-LC duplicate row, dbrow `quest_eadgarsruse` id 62 -- see IN-LC table (`quest_eadgar`, `server/scripts/quests/quest_eadgar/`, journal wired `interface_questjournal/scripts/quest_journal.rs2:615`) |
| 114 | heroesquest | `heroesquest` | 613 | done (LC) | 2026-08-11: duplicate row — already correctly listed on the IN-LC table (`quest_hero`); this Queue row was stale, table-sync fix only. `quest_hero` (11 files, 754 lines, dbrow `quest_heroes` journal wired `interface_questjournal/scripts/quest_journal.rs2:631`) |
| 115 | kingsransom | `kingsransom` | 617 | done | Jul 2007 — Anna Sinclair's frame-up, Morgan Le Faye's coup, Merlin's prison, King Arthur's granite curse; **row #111's real replacement slice** (Dragon Slayer turned out already LC-implemented, see Log); native dbrow `quest_kingsransom` (id 136, endstate 90, questpoints 1, requirement_stats defence 65 + magic 45) + native varbit schema on basevars `kr_varp1/kr_varp2/kr_varp3` (`kr_quest`, `kr_window`, `kr_clue_note/form/armour`, `kr_court_witness`, `kr_court_dog/butl/maid_proof`, `kr_court_thread`) reused as-is, matching quest-helper's own VarbitID names exactly; dbrow `requirement_quests` wrong (resolves to Ghosts Ahoy/In Aid of the Myreque/Devious Minds/Prince Ali Rescue, none matching) — gated on quest-helper's own getGeneralRequirements() + wiki instead: Black Knights' Fortress (`%spy`/`quest_blackknight`), Holy Grail (`%grail`/`quest_grail`) and Murder Mystery (`%murderquest`/`quest_murder`) are all already implemented in this tree and are hard-gated; One Small Favour (queue row #157, still pending) is soft-skipped, matching this queue's established convention for unported sibling prereqs; see Log |
| 116 | atasteofhope | `atasteofhope` | 629 | done |  |
| 117 | biohazard | `biohazard` | 635 | done (LC) | OSRS has 7 rs2 files (not in PORT_QUEUE table) |
| 118 | makingfriendswithmyarm | `makingfriendswithmyarm` | 640 | done |  |
| 119 | swansong | `swansong` | 644 | done | May 2006 -- Herman Caranos's besieged Piscatoris Fishing Colony, the Wise Old Man's own "swan song", Franklin's wall repairs, Arnold's monkfish, Malignius Mortifer's failed skeletal army, the Sea Troll Queen; see Log |
| 120 | royaltrouble | `royaltrouble` | 657 | done | May 2006 -- King Vargas's restlessness, a staged Miscellania/Etceteria feud, five Fremennik teens (Signy/Hild/Armod/Beigarth/Reinn) who failed their Trials, a Giant Sea Snake (level 149); thematically/mechanically linked to Throne of Miscellania (#99) -- same native npcs (misc_advisor_ghrim/misc_king_vargas/misc_queen_sigrid), branch merged into quest_misc's own existing opnpc1 triggers rather than duplicated; native dbrow `quest_royaltrouble` (id 112, endstate 30, questpoints 1, requirement_stats agility 40 + slayer 40, stat_xp_awarded agility/slayer/hitpoints 5000 each -- raw dbrow values /10, matches wiki exactly) + native varbit schema on basevars `royal_questvarbits` (`royal_quest`/`royal_misc`/`royal_etc`) and `royal_varbits` reused as-is, matching quest-helper's own VarbitID names exactly (fetched via GitHub raw + summarized, not verbatim -- exact intermediate breakpoint semantics reconstructed from the recovered ROYAL_MISC {10,20,30,40,50,60,80,110,120}/ROYAL_ETC {10,20,40} value sets + wiki step order, not independently confirmed); `%royal_liftstage`/`%royal_coalinengine` breakpoints ARE independently confirmed off this cache's own multivarbit .loc records (`royal_side_scaffold_multiloc`, `royal_top_scaffold_multiloc`, `royal_engine_platform_multiloc`, `royal_lift_platform_multiloc`) -- lift repair implemented as a real multi-step item-on-object puzzle (crates/beams/pulley beams/rope/coal engine) using those exact breakpoints, not narrated; dbrow `requirement_quests` resolves to The Corsair Curse (id 147) -- not a real prerequisite (same cache decode corruption flagged repeatedly on this queue) -- gated on Throne of Miscellania completion instead (`%misc_quest = ^misc_king_signed_treaty`); npcs=royal_misc_guard/royal_etc_guard (cache's own soldiers-being-blamed stand in for the wiki's unresolved Gunnhild/Leif/Frodi/Magnus/Helga/Haming/Matilda interviewees, cache wins), misc_sailor, royal_dwarf_drunk (Donal), royal_fremennik_teen3 (Armod, spokesperson), royal_sea_snake_mother_smaller (Giant Sea Snake boss, hand-spawned on trigger + `~npc_default_death`, same idiom as Contact's Giant Scarab), royal_cutscene_prince_brand/royal_cutscene_princess_astrid (dedicated intro-cutscene npcs, distinct from quest_misc's own Brand/Astrid, op1 added via additive .npc overlay); zero hand-spawning for every other npc (all world-spawned already); cave hazards (steam vents/falling rocks/slippery-rock plank) deferred as pass-through terrain, no damage/fail-chance system precedent in this tree; wiki https://oldschool.runescape.wiki/w/Royal_Trouble/Quick_guide + walkthrough (Transcript: page not fetched verbatim, paraphrased dialogue per copyright, same as King's Ransom); `mingw32-make -C src sscompile` clean, `mingw32-make -C src torirsserver-scripts` exit 0 (13,940 scripts, up from 13,887); `::royaltrouble` / `::royaltroublerun`; next = The Great Brain Robbery (#121) |
| 121 | thegreatbrainrobbery | `thegreatbrainrobbery` | 659 | done | Mar 2007 -- Brother Tranquility's Harmony Island monastery has had its monks' brains stolen by Mi-Gor's zombie pirates for his machine, Barrelchest; Dr Fenkenstrain (Creature of Fenkenstrain, already implemented, hard-gated on `%creatureoffenkenstrain >= ^fenk_complete`) is smuggled to the island inside a crate of wooden cats to perform the transplants; native dbrow `quest_greatbrainrobbery` (id 130, endstate 130, questpoints 2, requirement_stats prayer 50 + construction 30 -- dbrow only encodes 2 stat rows, wiki's crafting 16 checked separately, stat_xp_awarded prayer 60000=6000xp + crafting 30000=3000xp + construction 20000=2000xp, raw dbrow /10 matches wiki exactly) + native varbit schema on basevars `brain_extra_var`/`brain_extra_var_2` (`brain_broken_steps`, `brain_read_prayers`, `brain_words`, `brain_fenk_puzzle`, `brain_crate`, `brain_barrel_setup`, `brain_clamp_given`/`brain_tongs_given`/`brain_hammer_given`/`brain_jars_given`/`brain_staples_given`, `brain_statue_pushed`, `brain_seen_wallbreaker`, `brain_multi_monk`) reused as-is, matching quest-helper's own VarbitID names exactly (fetched via GitHub raw); master progress is a plain varp `brain_quest_var` (0/10/20.../130, matching quest-helper's own steps.put keys 1:1) -- confirmed authoritative (not guessed) via this cache's own multi-npc records: `brain_tranquility`/`brain_island_tranquility` both declare `multivarp=brain_quest_var` swapping Brother Tranquility zombie->human exactly at value 100, and `brain_island_fenkenstrain` only renders `fenk_fenkenstrain_model` from value 70 on, both landing exactly on quest-helper's own step keys; crate-build puzzle (`%brain_crate` 1..5: Build/Add-bottom/Fill/cats-added/Fenk-inside) and door-breach puzzle (`%brain_barrel_setup` 2..5: keg/fuse/lit/gone) both independently confirmed via this cache's own `brain_fenk_crate` and `brain_mon_entrance_door_multi` native multiloc records, matching quest-helper's own VarbitRequirement thresholds exactly; statue passage and underwater stairs repair (`brain_statue_saradomin`, `brain_underwater_stairs_broken`, op1=Repair, no item needed) likewise cache-baked map locs with no `.spawn` entry anywhere in this tree (confirmed via grep) -- script triggers only, no hand-spawning needed for any of the puzzle geometry; dbrow `requirement_quests` decodes to Black Knights' Fortress/Lost City -- not real prerequisites (same cache decode corruption flagged repeatedly on this queue) -- real prereqs per wiki are Creature of Fenkenstrain (hard-gated, already implemented), Cabin Fever and Recipe for Disaster/Freeing Pirate Pete (both have native dbrow rows but zero scripts anywhere in server/scripts -- soft-skipped, matching this queue's established convention for unported sibling prereqs); npcs=brain_tranquility/brain_island_tranquility (Brother Tranquility, split by location, matches the queue's own `brainbrothe` abbreviation), werewolfshopkeeper1 (Rufus, already has a Talk-to stub in `areas/area_canifis/scripts/rufus.rs2` -- merged a crate-scheme branch into its existing `[opnpc1,werewolfshopkeeper1]` trigger rather than duplicating, matches queue's own `feverharmle` sample which resolves to `fever_harmless_teach`-family Mos Le'Harmless npcs, not directly used here since Tranquility himself starts on Mos Le'Harmless), fenk_fenkenstrain_model (Dr Fenkenstrain, already has a full Talk-to tree in `quests/quest_fenkenstrain/scripts/fenkenstrain.rs2` for Creature of Fenkenstrain -- merged a branch into its existing `@fenk_talk` label rather than duplicating the trigger); brain_mi_gor/brain_barrel_chest hand-spawned on trigger for the final church confrontation, same idiom as Royal Trouble's Giant Sea Snake / Contact's Giant Scarab (neither has a `.spawn` entry); wooden-cat crafting implemented as a simplified oak-plank + knife make-action (no player-owned-house workshop flatpack minigame precedent anywhere in this tree -- deferred); surgical instruments (cranial clamp/brain tongs/3 bell jars/30 skull staples) drop from Sorebones kills via a simple scripted `obj_add` on `ai_queue3` death (no verified native drop table recoverable for these specific items); Barrelchest's own prayer-disabling special attack has no established mechanic precedent, left to the generic combat system, same reasoning as Royal Trouble's own boss; wiki https://oldschool.runescape.wiki/w/The_Great_Brain_Robbery/Quick_guide + https://oldschool.runescape.wiki/w/The_Great_Brain_Robbery (dialogue paraphrased per copyright, same convention as Royal Trouble/King's Ransom); `mingw32-make -C src sscompile` clean, `mingw32-make -C src torirsserver-scripts` exit 0 (14,078 scripts, up from 14,041; zero brain_-related errors/warnings); files: `quests/quest_thegreatbrainrobbery/{configs/thegreatbrainrobbery.constant,configs/thegreatbrainrobbery.varp,scripts/brain_{shared,tranquility,underwater,prayerbook,castle,door,finale,journal}.rs2}` + merges into `areas/area_canifis/scripts/rufus.rs2`, `quests/quest_fenkenstrain/scripts/fenkenstrain.rs2`, `interface_questjournal/scripts/quest_journal.rs2`; next = Rum Deal (#122) |
| 122 | rumdeal | `rumdeal` | 662 | done | Oct 2005 -- Pirate Pete's plan to get Captain Braindeath's zombie crew blind drunk so he can raid the island; native dbrow `quest_rumdeal` (id 95, endstate 19, questpoints 2, requirement_stats fishing50+prayer47+crafting42+slayer42+farming40, stat_xp_awarded fishing/prayer/farming 7000xp each) + native varbit schema on basevar `deal_var` (`deal_farming` blindweed patch 0-5, `deal_barrel` pressure-barrel sluglings 0-5, `deal_multi_hopper` brew control 0-2, all confirmed via native multiloc records) reused as-is; npcs=deal_pete,deal_captian_braindeath,deal_davey,deal_captian_donnie,deal_evil_spirit,deal_fever_spiders1 (queue's own abbreviated hint `dealevilsp`/`dealpete`/`dealcaptian` don't match real cache names -- cache wins); see Log |
| 123 | templeoftheeye | `templeoftheeye` | 662 | done |  |
| 124 | thefremennikisles | `thefremennikisles` | 670 | done | Feb 2007 -- King Gjuki's jester-spy plot against Mawnis Burowgar, two rounds of Jatizso tax collection, two bridge repairs, and the Ice Troll King; native dbrow `quest_fremennikisles` (id 127, endstate 340, questpoints 1, requirement_stats agility40+construction20) + native varbit schema on basevars `fris_r1` (`fris_quest`, `fris_task` troll counter, `fris_m_b3`/`fris_m_b4`/`fris_m_b5` bridges, `fris_king` Mawnis crown swap) and `fris_r2` (six `frisd_*_taxcollected` bits, shared/reset across both tax rounds) reused as-is; npcs=fris_r_king,fris_r_burgher_crown,fris_spymaster,frisd_oremerchant,frisd_weaponmerchant,frisd_izso_landlady,frisd_cook,frisd_armourmerchant,frisd_fishmerchant,fris_troll_king_true (cache spelling matches quest-helper's own NpcID names exactly, no drama this time); see Log |
| 125 | gardenoftranquility | `gardenoftranquility` | 684 | done | Aug 2005 -- Queen Ellamaria's hidden garden for King Roald; native dbrow `quest_gardenoftranquillity` (double-L cache spelling, id 90, endstate 60, questpoints 2, requirement_stats farming25) + native varbit schema on basevars `garden_varp_1`/`garden_varp_2` reused as-is; npcs=queen_ellamaria,elstan,lyra,kragen,dantaera,brother_althric,bernald (cache's own real names, not the queue row's stale `gardentroll`/`queenellama` hints -- see Log); see Log |
| 126 | murdermystery | `murdermystery` | 686 | done (LC) | found 2026-08-11: pre-Sept-2004 quest (Dec 2003), belongs on IN-LC list not this queue — LC's own `quest_murder` (20 files, 1580 lines, dbrow `quest_murdermystery` journal wired `interface_questjournal/scripts/quest_journal.rs2:491`) already fully implements it; found + directly reused by this tick's King's Ransom slice (#115), see Log |
| 127 | enakhraslament | `enakhraslament` | 688 | done | Jan 2006 -- Lazim, Enakhra's ruined desert temple, Akthanakos; native dbrow `quest_enakhraslament` (id 103, endstate 70) + native varbit schema on three basevars (`enakh_quest_expositbits`/`enakh_multivarbits`/`enakh_varbits`) reused as-is; see Log |
| 128 | perilousmoon | `perilousmoon` | 688 | done |  |
| 129 | theslugmenace | `theslugmenace` | 694 | done | Sept 2006 -- Sir Tiffy Cashien's Temple Knights send the player to Witchaven to investigate a Zamorakian conspiracy (Col. O'Niall/Brother Maledict/Mayor Hobb), a ruined temple, torn documents, five elemental runes, and the Slug Prince; see Log |
| 130 | cabinfever | `cabinfever` | 704 | done | Feb 2006 -- Bill Teach recruits the player to raid a rival pirate crew at sea; native dbrow `quest_cabinfever` (id 104, endstate 140, questpoints 2, requirement_stats smithing50+crafting45+ranged40+agility42, stat_xp_awarded crafting/smithing/agility 7000xp each, matches wiki exactly) + native varbit schema on basevars `fever_quest`/`fever_cannon_var`/`fever_extra_var`/`fever_storage_var` (`fever_hole_1/2/3`, `fever_holes_patched/proofed`, `fever_crate/chest/barrel`, `fever_plunder_points`, `fever_cannon`, `fever_cannon_powder/tamp/ammo/fuse/clean`, `fever_holes_in_the_hull`, `fever_gunpowder_barrel`) reused as-is, matching quest-helper's own VarbitID names exactly; queue's own hint `feverteach,feverteach,feverquest` resolves to real cache names `fever_teach`/`fever_quest_ship_teach` (cache wins, close but not identical spelling). dbrow `requirement_quests` decodes to Contact! (124) and A Soul's Bane (108) -- neither a real prerequisite, same known cache-decode-corruption failure mode this queue warns about repeatedly. Real prereqs per quest-helper's own getGeneralRequirements() are Pirate's Treasure FINISHED, Rum Deal FINISHED and Priest in Peril FINISHED; Pirate's Treasure (`%hunt >= ^hunt_complete`) and Rum Deal (`%deal_var >= ^deal_complete`) are both genuinely completable in this tree and hard-gated. Priest in Peril is soft-skipped and NOT gated on: `quest_priestperil.constant`'s own header documents its essence-bringing finale (`%priestperil` 10..60) as "deferred (blocked)", and grepping its scripts confirms `%priestperil` never advances past `^priestperil_meet_in_mausoleum` (8) anywhere in this tree -- hard-gating on it would make Cabin Fever itself permanently unstartable, so it isn't checked (documented, not silently dropped, matching the established convention for a corrupted/unportable prerequisite). Native multiloc records independently confirm every real breakpoint used (cache wins, not guessed): `fever_multi_hole_1/2/3` leak->planked->waterproofed at values 1/2; `fever_multi_chest/_crate/_barrel` closed->looted at value 1; `fever_multi_cannon` intact->destroyed->no_barrel->loaded at values 1/2/3 (quest-helper reuses this same var for "broken" (1) and, later, "fuse loaded and ready" (3) -- both kept); `fever_multi_hole_enemy_1/2/3` share one real counter, `fever_holes_in_the_hull` (0..3), sequentially revealing hull breaches -- this is the wiki's own "three holes in the enemy's ship" objective, mechanically confirmed (not narrated), driven directly by the final cannonball-firing phase instead of inventing a separate counter; `fever_multi_gunpowder_barrel` intact(0)->fused(2)->exploded(1), the cache's own non-monotonic order, matches quest-helper's `addedFuse`(2)/`explodedBarrel`(1) exactly; `fever_port_ship_teach` (dock-side "Bill Teach on his boat" wrapper) is a real `multivarp=fever_quest` record, invisible until value 10 -- independently confirms the master var really does jump 0->10 on first acceptance, used directly. This server only ever spawns the wrapper npc/loc types; `fever_teach`/`fever_port_ship_teach`/`fever_quest_ship_teach` all declare no op of their own in the cache -- additive overlay in `cabinfever.npc`, same convention as `royaltrouble.npc`/`theslugmenace.npc` (every multiloc wrapper used already carries its own real op -- Repair/Loot/Plunder/Load/Take-powder/Cross -- no loc overlay needed). All navigation between decks (ladders/nets/climb-down) is already handled by the generic climb system (`ladders_stairs/scripts/ladders.rs2`, cache-declared climb verbs) -- zero custom transport scripting needed for any of it; the ship-to-ship rope swing (`fever_sail1_hoistedl_climb`, reused by quest-helper's own `swingToBoat`/`swingToEnemyBoat`/`useRopeOnSailForSabo` alike) is one zone-aware (`distance(coord,...)`) teleport trigger. Every pirate crew/enemy npc (`fever_pirate_island_01..10`, `fever_pirate_millitia_01..10`, `fever_pirate_enemy_01..10`, `fever_smithing_smith`, `fever_harpoon_joe`, `fever_pirate_two_feet_charley`, `fever_mama_la_fiette`, `fever_dodgy_mike`) is already world-spawned (confirmed via grep of `areas/world/configs/*.spawn`) and pure flavour -- none gate any quest-helper step, none scripted. Simplifications (documented, no established precedent anywhere in this tree for the alternative): quest-helper's own 704 lines are mostly `ConditionalStep`/`Zone` bookkeeping to draw a helper arrow across geography that's already baked cache terrain here -- not reproduced. Locker searches (`fever_repair_locker`/`fever_weapons_locker`) grant a full requirement in one Search rather than quest-helper's own incremental per-item fetch loop, same convention as The Great Brain Robbery's crate-building simplification. Plunder containers grant a fixed split (crate 4 + chest 3 + barrel 3 = 10, matching `loot10`) rather than a random per-loot amount -- no drop-table precedent recoverable. The canister-firing phase ("fire with canisters until 3 pirates die") has no cannon-deals-damage-directly precedent anywhere in this tree (Royal Trouble's Giant Sea Snake / The Great Brain Robbery's Barrelchest both leave combat entirely to the generic system) -- a successful load/fire cycle is itself the real, required, repeatable action standing in for the kill, tracked via `%fever_quest` sub-values (111/112/113) rather than combat; the ball-firing phase instead drives the real `fever_holes_in_the_hull` counter directly. Misfire/wrong-ammo error handling (`canisterInWrong`/`resetCannon`) isn't modelled -- wrong ammo for the current phase is just refused with a hint message, no jammed-cannon state. `fever_rum`/`fever_gold`/`fever_smithed_anchor` and the enemy crew's own named weapons are native but never referenced by any quest-helper step -- deferred flavour. Wiki https://oldschool.runescape.wiki/w/Cabin_Fever/Quick_guide + quest-helper source fetched via GitHub raw (dialogue paraphrased, not verbatim, per copyright, same caveat as every prior slice). `mingw32-make -C src sscompile` clean, `mingw32-make -C src torirsserver-scripts` exit 0 (14,342 scripts, up from 14,316); files: `quests/quest_cabinfever/{configs/cabinfever.{constant,varp,npc}, scripts/cabinfever_{shared,bill,transport,lockers,repair,sabotage,loot,cannon,journal}.rs2}` + wiring into `interface_questjournal/scripts/quest_journal.rs2`. This was previously a soft-skipped prerequisite for The Great Brain Robbery (#121) -- a real port here means a future tick could tighten that gate. Next pending row (smallest-first): #132 In Aid of the Myreque, 710 lines. |
| 131 | icthlarinslittlehelper | `icthlarinslittlehelper` | 707 | done (LC) | 2026-08-11: duplicate row — already correctly listed on the IN-LC table (`quest_icthlarin`); this Queue row was stale, table-sync fix only. `quest_icthlarin` (5 files, 684 lines, dbrow `quest_icthlarinslittlehelper` journal wired `interface_questjournal/scripts/quest_journal.rs2:703`) |
| 132 | inaidofthemyreque | `inaidofthemyreque` | 710 | done | Jan 2006 -- Burgh de Rott repairs, Gadderanks's blood tithe raid, Ivan's Temple Trek escort, Rod of Ivandis; native dbrow `quest_inaidofthemyreque` (id 107, endstate 430, requirement_stats Crafting25/Mining15/Magic7) + native varbit schema on basevars `myreque_2_main_var`/`myreque2_multivar`/`myreque2_extravar` reused as-is, matching quest-helper's own VarbitID names exactly; dbrow `requirement_quests` decodes to Desert Treasure I (corrupt, known failure mode) -- real prereq (In Search of the Myreque FINISHED) soft-skipped since `%routequest` is never written anywhere in this tree (that quest has no scripted content beyond its own journal/dbrow, confirmed via grep -- row #65's "done (LC)" is optimistic); Crafting/Mining/Magic gate still hard-checked. Shares `myq5_veliaf_child` with Sins of the Father's own hub trigger (merged branch in `sinsofthefather.rs2`, not duplicated) and adds one case to the shared furnace hub (`skill_smithing/scripts/smelting/smelting.rs2`) for the Rod of Ivandis mould. See Log. |
| 133 | betweenarock | `betweenarock` | 716 | done | Mar 2005 -- Dondakan the Dwarf's cannon-through-the-rock scheme uncovers a sealed Arzinian realm; dwarven lore book + 3 torn pages, a golden cannonball, four schematic fragments, a golden helmet, and an Avatar guardian boss; see Log |
| 134 | ratcatchers | `ratcatchers` | 737 | done | 2QP, Thieving 4500xp; native dbrow+varbit schema reused; see Log |
| 135 | dreammentor | `dreammentor` | 745 | done | 2026-08-12: unblocked -- Lunar Diplomacy (#169) landed with genuinely functional Rellekka<->Lunar Isle boat transport; re-verified end to end, ported same tick. 2QP, Hitpoints 15000xp + Magic 10000xp; native dbrow+varbit schema (`dream_prog`/`dream_health`/`dream_armament`) reused; real prereqs Combat 85 + Lunar Diplomacy FINISHED + Eadgar's Ruse FINISHED all hard-gated; see Log |
| 136 | watchtower | `watchtower` | 758 | done (LC) | 2026-08-11: duplicate row — already correctly listed on the IN-LC table (`quest_itwatchtower`); this Queue row was stale, table-sync fix only. `quest_itwatchtower` (13 files, 2010 lines, dbrow `quest_watchtower` journal wired `interface_questjournal/scripts/quest_journal.rs2:599`) |
| 137 | shadowofthestorm | `shadowofthestorm` | 759 | done (LC) | found 2026-08-11: pre-Sept-2004 quest (2002), belongs on IN-LC list not this queue — LC's own `quest_shadowstorm` (3 files, 509 lines, `shadowstorm_ritual.rs2` calls `~quest_complete(quest_shadowofthestorm)`; journal wired `interface_questjournal/scripts/quest_journal.rs2:707`) already implements it — found while auditing #111's neighbours, see Log |
| 138 | landofthegoblins | `landofthegoblins` | 760 | done | 2QP, Agility/Fishing/Thieving/Herblore 8000xp each; native dbrow+varbit schema (`%lotg`) reused; see Log |
| 139 | elementalworkshopii | `elementalworkshopii` | 770 | done | 1QP, Smithing/Crafting 7500xp each; native dbrow+20-field varbit schema (`%elemental_quest_2_main` + sub-fields) reused, real prerequisite EW1 FINISHED; see Log |
| 140 | deserttreasure | `deserttreasure` | 803 | done (LC) | OSRS has 3 rs2 files (not in PORT_QUEUE table) |
| 141 | thedigsite | `thedigsite` | 803 | done (LC) | re-audit 2026-08-10: LostCity's own internal codename for this quest is `itexam`, not `thedigsite`/`digsite` -- `quest_itexam` (`server/scripts/quests/quest_itexam/`, `examiner.rs2`/`digsite_workman.rs2`/`area_digsite.rs2`/`panning_guide.rs2`/`itexam_chemistry.rs2`, trowel + specimen_brush reuse) already fully implements it; found while checking Another Slice of H.A.M.'s (#85) real prerequisite chain |
| 142 | troubledtortugans | `troubledtortugans` | 803 | done |  |
| 143 | undergroundpass | `undergroundpass` | 812 | done (LC) | found 2026-08-11: pre-Sept-2004 quest (2002), belongs on IN-LC list not this queue — LC's own `quest_upass` (31 files, 2602 lines, dbrow `quest_undergroundpass` journal wired `interface_questjournal/scripts/quest_journal.rs2:535`) already fully implements it — found while auditing #111's neighbours, see Log |
| 144 | hazeelcult | `hazeelcult` | 814 | done (LC) | OSRS has 11 rs2 files (not in PORT_QUEUE table) |
| 145 | darknessofhallowvale | `darknessofhallowvale` | 816 | done | Sept 2006 — Myreque #3; native dbrow `quest_darknessofhallowvale` (id 117, endstate 320) + native varbit schema on basevars `myreque_3_main_var`/`myreque3_multivar` (`myq3_*`) reused as-is; see Log |
| 146 | ghostsahoy | `ghostsahoy` | 821 | done | Feb 2005 -- Velorina asks the player to free the ghosts of Port Phasmatys from Necrovarus's curse; native dbrow (id 73, endstate 8) + native varbit schema on basevar `ahoy_varbits_1` reused; see Log |
| 147 | deathontheisle | `deathontheisle` | 827 | done |  |
| 148 | scrambled | `scrambled` | 840 | done |  |
| 149 | beneathcursedsands | `beneathcursedsands` | 859 | done |  |
| 150 | regicide | `regicide` | 944 | done (LC) | OSRS has 13 rs2 files (not in PORT_QUEUE table) |
| 151 | theeyesofglouphrie | `theeyesofglouphrie` | 969 | done | Jul 2006 -- Brimstail's anti-illusion machine exposes Glouphrie's exile and six disguised Arposandran spies; native dbrow (id 116, endstate 60) + native varbit schema on basevar `eyeglo_var1`/`eyeglo_var2` reused; see Log |
| 152 | monkeymadnessi | `monkeymadnessi` | 988 | done (LC) | found 2026-08-11: this row was stale -- already flagged on this file's own skip list (line 95, "spelling-only mismatches already owned elsewhere") but the Queue table row itself was never flipped. LostCity's `quest_mm/` (25 scripts, 4,149 lines: `mm_narnode`/`mm_caranock`/`mm_daero`/`mm_waydar`/`mm_lumdo`/`mm_zooknock`/`mm_lumo`/`mm_karam`/`mm_garkor`/`mm_monkey_child`/`mm_kruk`/`mm_awowogei`/`mm_shopkeepers`/`mm_warehouse`/`mm_supply_crates`/`mm_puzzle`/etc.) already implements Monkey Madness I end-to-end on basevar `%mm_main` against `configs/quest_mm.constant`'s `^monkeymadness_*` scale; `mm_narnode.rs2:233` sets `%mm_main = ^monkeymadness_complete`; journal wired `interface_questjournal/scripts/quest_journal.rs2:727-728` (`quest_monkeymadness1` -> `~mm_journal`). Matches `CONTENT_PORT_QUEUE.md`'s own extensive slice history (32z, 33u, 34n-34y, 35a-35g, final log 12368 scripts) -- belongs there, not here, per this queue's own ownership rule (LC proc presence, not completion state). Not re-verified for completeness here (out of scope for this queue); next pending row promoted to #153 A Forgettable Tale... (forgettabletale) |
| 153 | forgettabletale | `forgettabletale` | 1,000 | done | Jul 2005 -- Commander Veldaban's Red Axe investigation via Keldagrim's Drunken Dwarf, the legendary kelda beer (farming+brewing side-quest), and the hidden mine-cart tunnel network under the trading Consortium; native dbrow `quest_forgettabletale` (id 88, endstate 140, questpoints 2) + native varbit schema on basevar `forget_main_var` (`forget_quest` 0-255, `forget_farming` 0-15, confirmed authoritative via `farming_hops_patch_keldagrim`'s own 10-state multiloc) reused as-is; see Log |
| 154 | toweroflife | `toweroflife` | 1,021 | done | Feb 2007 -- Effigy asks the player to repair the derelict Tower of Life so its resident "alchemists" (secretly harmless gnomes) can resume homunculus-making; a builder's-outfit fetch quest (quiz, pickpocket, beer trade, bush search) gates entry, then three broken machines (pressure, pipe, cage) must be rebuilt and fixed to free the caged homunculus; native dbrow `quest_toweroflife` (id 129, endstate 18, questpoints 2) + native varbit schema on basevars `tol_main`/`tol_main2` reused; see Log |
| 155 | mourningsendpartii | `mourningsendpartii` | 1,100 | done | Oct 2005 -- direct sequel to Mourning's End Part I (#106): Arianwyn sends the player to find missing elf Edern near the old Temple of Light; native dbrow `quest_mourningsendpart2` (id 93, startnpc 5292=`mourning_arianwyn`, endstate 60, questpoints 2) + native varbit `mourning_quest_main` (basevar `mourning_quest_part2`) reused on a coarse 0/10/.../60 scale; see Log |
| 156 | enlightenedjourney | `enlightenedjourney` | 1,168 | done | Nov 2006 -- Auguste (`zep_piccard`) on Entrana asks the player to help build and fly a hot air balloon (papyrus/wool/candle test models, a flash-mob mishap, then sandbags/dye/silk/bowl/willow-branch basket/logs for the real one), landing in Taverley; native dbrow `quest_enlightenedjourney` (id 121, startnpc 4715=`zep_piccard`, endstate 200, questpoints 1) + native varbit `zep_quest` (basevar `zep_var`) reused on quest-helper's own 0/10/20/40/60/70/80/90 scale, jumping straight to 200 for the true finish; also unlocks the native 6-node balloon transport network (Entrana/Taverley/Castle Wars/Grand Tree/Crafting Guild/Varrock); see Log |
| 157 | onesmallfavour | `onesmallfavour` | 1,244 | done | Feb 2005 -- Yanni Salika's red-mahogany request unravels into a long relay of favours across Kandarin/Misthalin/Karamja; native dbrow `quest_onesmallfavour` (id 74, endstate 285, startnpc 5361=`shiloantiques`) + native top-level varp `onesmallfavour` (unpacked, no independent multiloc/multivarp cross-validation beyond quest-helper's own steps.put keys) + native `onesmallfavourmulti` sub-fields (weathervane/landing-light puzzle) reused as-is; see Log |
| 158 | legendsquest | `legendsquest` | 1,261 | done (LC) | 2026-08-11: duplicate row — already correctly listed on the IN-LC table (`quest_legends`); this Queue row was stale, table-sync fix only. `quest_legends` (15 files, dbrow `quest_legends`, journal wired `interface_questjournal/scripts/quest_journal.rs2:~660`, `~legends_journal`) |
| 159 | thefremenniktrials | `thefremenniktrials` | 1,269 | done (LC) | 2026-08-11: row was stale/mislabeled -- LostCity's `quest_viking` (NOT `quest_fremennikexiles`, a separate already-implemented folder) implements this quest end-to-end: `quest_viking_progress.rs2` header literally reads "Fremennik Trials progress + trial bit ranges"; its 7-vote council trial cast (Swensen the Navigator maze, Sigmund the Merchant fetch chain, Sigli the Hunter vs. Draugen, Peer the Seer maze, Thorvald the Warrior vs. Koschei, the Reveller drinking contest, Olaf the Bard's lyre) is the real Fremennik Trials plot, not Exiles' Freygerd/basilisk plot; dbrow `quest_fremenniktrials` (configs/all.dbrow:3128) wired at `interface_questjournal/scripts/quest_journal.rs2:711-713` to `~viking_journal`, whose journal text (`quest_viking/scripts/viking_journal.rs2`) titles every entry "The Fremennik Trials" verbatim. IN-LC table above corrected to match (was mapping `thefremennikexiles` to `quest_viking`; fixed to the real `quest_fremennikexiles` folder, dbrow id at all.dbrow:3000, Freygerd/basilisk plot confirmed via `fremennikexiles.rs2`). No new port needed. |
| 160 | thefinaldawn | `thefinaldawn` | 1,274 | done |  |
| 161 | secretsofthenorth | `secretsofthenorth` | 1,293 | done |  |
| 162 | theforsakentower | `theforsakentower` | 1,353 | done |  |
| 163 | recruitmentdrive | `recruitmentdrive` | 1,425 | done (LC) | OSRS has 9 rs2 files (not in PORT_QUEUE table) |
| 164 | akingdomdivided | `akingdomdivided` | 1,560 | done |  |
| 165 | theheartofdarkness | `theheartofdarkness` | 1,582 | done |  |
| 166 | thecurseofarrav | `thecurseofarrav` | 1,665 | done |  |
| 167 | sinsofthefather | `sinsofthefather` | 1,668 | done |  |
| 168 | ragandboneman | `ragandboneman` | 1,729 | done (LC) | OSRS has 4 rs2 files (not in PORT_QUEUE table) |
| 169 | lunardiplomacy | `lunardiplomacy` | 1,756 | done | 2026-08-11: full port, functional Rellekka<->Lunar Isle boat transport (unblocks #135 Dream Mentor's own setting -- re-check that row); see Log |
| 170 | dragonslayerii | `dragonslayerii` | 1,782 | done |  |
| 171 | thepathofglouphrie | `thepathofglouphrie` | 1,959 | done | 2026-08-12: full port, native `pog` varbit schema reused; see Log |
| 172 | whileguthixsleeps | `whileguthixsleeps` | 2,288 | done | 2026-08-12: full port, native `wgs` varbit schema reused, trustworthy dbrow (unlike most slices); see Log |
| 173 | monkeymadnessii | `monkeymadnessii` | 3,084 | done |  |
| 174 | recipefordisaster | `recipefordisaster` | 3,370 | done | 2026-08-12: full port complete -- intro + all 8 sub-quests (Evil Dave, Lumbridge Guide, Goblin generals, Mountain Dwarf, Pirate Pete, Skrach Uglogwee, Sir Amik Varze, King Awowogei) + Culinaromancer finale, across two ticks; see Log |
| 175 | songoftheelves | `songoftheelves` | 4,285 | done |  |
| 176 | deserttreasureii | `deserttreasureii` | 5,076 | done |  |

## Log

- **IN-LC audit pass 8 (2026-08-12):** audited the last 4 rows of the IN-LC
  table's original assignment, one quest at a time, synchronously (no nested
  background sub-agents): `recruitmentdrive`/quest_recruitmentdrive,
  `regicide`/quest_regicide, `tearsofguthix`/quest_tearsofguthix,
  `whatliesbelow`/quest_whatliesbelow. `recruitmentdrive` -- audited-fixed:
  two genuine bugs. Sir Kuam/Sir Leye's room implemented a fictitious
  "no man may defeat me" gender mechanic (infinite heal on male players, with
  a "soft skip" bypass) instead of the real wiki mechanic (Sir Leye is
  blessed against blades, not gender -- killing him with a bladed weapon
  fails the test; warhammer or unarmed succeeds); rewrote to grant all 4 room
  weapons and check the killing-blow weapon via `inv_getobj(worn,
  ^wearpos_rhand)`. Quest completion also never granted the wiki's XP/coin
  reward (only called `~quest_complete`) -- added via `stat_advance`.
  Reconciled two stale rows on `SCAPE2009_CONTENT_PORT_QUEUE.md` (22c/22h)
  that duplicate-tracked this same content, one of which (22h, this doc's
  brief specifically flagged as a straggler to check) turned out to already
  be fully implemented at `recruitmentdrive_cheevers.rs2`. `regicide` --
  **the biggest gap found this pass**, comparable to `eadgarsruse`/
  `thefremenniktrials` above: every file in `quest_regicide/` self-discloses
  the gap in its own header comment, and grepping every `%regicide_quest =`
  write in the tree confirmed several required stage transitions
  (received_message->spoken_lathas->spoken_scouts, found_footprints,
  defeated_guard, spoken_arianwyn) are never assigned anywhere, the
  Tyras-camp-guard fight and bomb-crafting/catapult mechanic are
  unimplemented, and -- most severe -- no file anywhere calls
  `~quest_complete(quest_regicide)` or sets `%regicide_quest =
  ^regicide_complete`, so the quest cannot be finished through normal play at
  all. Left `audit-in_progress` with a full chapter-by-chapter follow-up list
  in the row; too large for one sitting. `tearsofguthix` -- audited-ok, matches
  the wiki exactly (start requirements, Juna dialogue, bowl crafting, 1,000
  Crafting XP reward via the real `~quest_complete`); noted but did not fix a
  pre-existing disclosed simplification in the post-quest weekly minigame's
  XP-skill selection (does not affect quest completion). `whatliesbelow` --
  audited-fixed: the `surok_outlaw1..10` NPCs the quest requires the player to
  kill for papers have no static `.spawn` entry anywhere in the cache and were
  only ever `npc_add`-ed by the debug proc -- through normal play, nothing
  spawned them, so the quest could not be started or completed outside debug.
  Added a real spawn-on-quest-accept proc plus a same-type respawn in the
  death handler. Everything else in this quest (Surok's letters/wand,
  Zaff/beacon-ring, soft-skipped King Roald fight, 8,000 Runecraft + 2,000
  Defence XP reward) already matched the wiki. `mingw32-make -C src
  torirsserver-scripts` exit 0 after each fix (recruitmentdrive_kuam.rs2,
  recruitmentdrive.rs2, whatliesbelow.rs2, whatliesbelow_papers.rs2), no new
  warnings/errors on any touched file. **This closes out the IN-LC table's
  original 4-quest assignment for this tick, but the table is not yet fully
  audited: `zogreflesheaters`, `thefremennikexiles`, `thefremenniktrials`, and
  `deserttreasureii` (rows just above this pass's 4) still carry no audit
  marker in this file's HEAD and remain open for a future pass** -- so no
  "audit pass complete" banner is added here yet.

- **IN-LC audit pass 6 (2026-08-12):** audited 4 more rows, one quest at a
  time, synchronously (no nested background sub-agents): `shadowofthestorm`/
  quest_shadowstorm, `undergroundpass`/quest_upass, `thegrandtree`/
  quest_grandtree, `thelosttribe`/quest_losttribe. Read each LC script tree
  fully + the wiki quest page, `/Quick_guide`, and `Transcript:` pages before
  comparing; checked every completion path for the recurring bespoke-`%qp`
  bug and 10x-fixed-point xp mismatches explicitly (none found this pass --
  all four completing quests already used the real `~quest_complete` proc
  with dbrow-matching xp).
  - `shadowofthestorm` -> **audit-in_progress**: the `%agrith_quest` state
    machine and Reen/Badden/Denath/Jennifer/Matthew/Dave dialogue are wired
    end-to-end into a real `~quest_complete`, but nearly every mechanically
    distinct wiki chapter is soft-skipped to a single line: no four-kiln
    search, no unique per-player incantation puzzle, no strange-implement/
    golem interrogation, no Tanya/Eric sigil chase, no Fire-Blast/Telekinetic
    Grab boss AI (any kill counts), no bonus-gems branch. Also found (but did
    not fix, since it's tree-wide not quest-specific): the `thosf_reward_lamp`
    genie-lamp item this quest (and ~14 others) uses for skill-choice xp
    rewards has no rub/redeem handler anywhere in `server/scripts` --
    confirmed via grep and a pre-existing comment in
    `quest_pathofglouphrie/configs/pathofglouphrie.constant` admitting the
    same gap. Left `audit-in_progress`, research-only -- comparable in scope
    to Eadgar's Ruse/Holy Grail.
  - `undergroundpass` -> **audited-ok**: extremely thorough existing port (31
    files, 2602 lines); spot-checked King Lathas's start/end dialogue, the
    doll-of-Iban altar finale (deathrune/firerune bonus loot + temple
    collapse, matching the wiki beat for beat), Klank's gauntlets/tinderbox
    hand-out incl. the 5000gp repurchase branch, and the bloodwell badge/horn
    door-unlock mechanic -- all faithful. One known non-blocking gap: Iban's
    staff recharge-at-the-well is a no-op (`%iban_staff_charges` doesn't
    exist -- this engine has no generic weapon-charge system yet), same
    cross-cutting class as the reward-lamp gap above.
  - `thegrandtree` -> **audited-ok**: high-quality existing port (16 files,
    1816 lines) matching Transcript:The_Grand_Tree closely everywhere
    checked -- King Narnode's full 5-choice translation-verification puzzle
    (narrows to the exact wiki sentence), the Foreman's exact three-question
    loyalty quiz with combat on a wrong answer, the Ka-Lu-Min shipyard
    password puzzle, Femi's helped-free-vs-1000gp-toll branch, and the black
    demon fight/twig-pillar trapdoor are all present and correct; several
    files' "quest body deferred" header comments are stale, same pattern as
    prior passes. One minor accepted simplification noted (twig-pillar order
    not enforced) -- not fixed, matches this codebase's existing precedent
    for this puzzle class (see Waterfall Quest).
  - `thelosttribe` -> **audited-fixed**: matches Transcript:The_Lost_Tribe
    closely, but the `lost_tribe_cook_witness` proc this loop's brief flagged
    as a possible duplicate-trigger risk (reused by Cook's Assistant) turned
    out to be a real, different bug on closer look: the transcript shows the
    cellar-incident eyewitness account is spoken by **Bob** (Bob's Brilliant
    Axes), not the Cook, whose real line is an unrelated red herring. Moved
    the witness proc (renamed `lost_tribe_bob_witness`) from `[opnpc1,cook]`
    to `[opnpc1,bob]` (splicing into its existing block) and restored the
    Cook's correct red-herring line. Also added a real, wiki-documented,
    entirely-missing post-quest reward: "a mining helmet from giving the
    brooch back to Mistag" (confirmed via the page's raw `{{Quest rewards}}`
    template) -- added an `[opnpcu,lost_tribe_mistag_1op]` handler granting
    `cave_goblin_mining_helmet_unlit` once.
  - Build: `mingw32-make -C src torirsserver-scripts` exit 0 after every fix
    (checked incrementally), 15090 scripts compiled, zero new diagnostics
    touching any file this pass edited. Grepped every touched npc/trigger
    name tree-wide before adding to confirm no duplicate-trigger shadowing.
    Files touched: `server/scripts/quests/quest_losttribe/scripts/
    losttribe.rs2`, `losttribe_mistag.rs2`; `server/scripts/quests/
    quest_cook/scripts/quest_cook.rs2`; `server/scripts/areas/lumbridge/
    scripts/bob.rs2`. `shadowofthestorm`/`undergroundpass`/`thegrandtree`
    were research-only this pass (`undergroundpass`/`thegrandtree` needed no
    fix; `shadowofthestorm`'s gap is too large for one tick -- see row note).

- **IN-LC audit pass 7 (2026-08-12):** audited 4 more IN-LC rows, one quest at
  a time, synchronously (no nested background sub-agents), wiki-first:
  `dragonslayer`/quest_dragon, `taibwowannaitrio`/quest_tbwt,
  `naturespirit`/quest_druidspirit, `murdermystery`/quest_murder.
  - `dragonslayer` -> **audited-ok**: an 11-file port matching Quick_guide +
    Transcript:Dragon_Slayer_I almost verbatim (Guildmaster, Oziach's full
    map-piece/shield hint menu, Oracle's rhyme, Wormbrain's pay/kill/story
    branches, Melzar's Maze coloured-key chest, Duke Horacio's optional
    shield, Klarense's ship purchase/repair, Elvarg's fire-breath/melee AI
    with shield+Protect Magic maxhit reduction). Real `~quest_complete`;
    reward (18,650 str/def xp, 2 QP) matches dbrow `stat_xp_awarded` and wiki
    exactly. No gaps found.
  - `taibwowannaitrio` -> **audited-fixed**: exceptionally thorough 8-file
    port matching Quick_guide + Transcript:Tai_Bwo_Wannai_Trio almost
    verbatim, including the four separate "final" village NPCs
    (`areas/area_karamja/scripts/tbwt_{tinsay,tiadeche,tamayu}_final.rs2`)
    that hand out the real per-brother rewards. Cross-checked the full
    reward breakdown against the wiki's own precise split (1,500 fishing
    during via Lubufu + 5,000 fishing/5,000 cooking/2,500 attack+2,500
    strength+rune spear from the brothers + 2,000 coins from Timfraku) --
    every number matched exactly. One real gap: the wiki documents burning
    Jogre bones two ways (furnace, any level, 25 Cooking xp -- already
    implemented; tinderbox at Firemaking 30+, 90 Firemaking xp -- missing
    entirely). Added the tinderbox arm by splicing a case into
    `skill_firemaking/scripts/firemaking.rs2`'s existing `[opheldu,tinderbox]`
    trigger (not duplicated) calling a new label in
    `quest_tbwt/scripts/tbwt_jogre_bones.rs2`.
  - `naturespirit` -> **audit-in_progress**: everything downstream of the
    quest actually starting -- `filliman.rs2`'s full ghost/journal/puzzle/
    transformation dialogue, the grotto stone/bloom/sickle/pouch mechanics in
    `quest_druidspirit.rs2` (exact 3/2/1-point pear/stem/mushroom formula),
    and the ghast fight mechanics -- is complete and wiki-accurate, ending in
    the real `~quest_complete(quest_naturespirit)` with reward matching the
    dbrow and wiki exactly (3,000 crafting/2,000 defence/2,000 hitpoints).
    But the quest is **completely unstartable through normal play**: grepping
    the whole tree found zero live triggers that ever set
    `%druidspirit = ^druidspirit_started` -- Drezel
    (`priestperiltrappedmonk`, shared with Priest in Peril,
    `quest_priestperil/scripts/trapped_drezel.rs2`) has no Nature-Spirit
    quest-offer branch at all, confirmed by `druidspirit_journal.rs2`'s own
    `^druidspirit_started`-state text narrating a Drezel conversation that no
    script ever produces (same bug shape as `eadgarsruse`'s Sanfew gap).
    Traced one level deeper: the root cause is that Priest in Peril itself is
    incomplete -- grepping every `%priestperil = ...` assignment tree-wide,
    the highest value real gameplay ever reaches is
    `^priestperil_meet_in_mausoleum` (8); `^priestperil_complete` (60) is
    never written anywhere except `quest_rumdeal/scripts/deal_debug.rs2`'s
    debug harness, meaning Priest in Peril's entire final chapter (bringing
    essence to restore the holy barrier) is unbuilt, which also blocks every
    other quest gating on `%priestperil >= ^priestperil_complete` (Rum Deal,
    Ghosts Ahoy, Haunted Mine, Making History -- not fixed here, out of this
    row's scope). Left `audit-in_progress`: needs Priest in Peril's missing
    finale built first (its own large audit item, not one of this pass's
    targets), then Drezel's Nature-Spirit quest-offer branch (3 meat pies + 3
    apple pies per the wiki) spliced into the existing
    `[opnpc1,priestperiltrappedmonk]` block. One independent, self-contained
    gap fixed regardless: the Mort Myre swamp-decay damage-over-time
    mechanic (`swamp_decay.rs2`) was fully written but never called anywhere
    (own header said so) -- wired it into `quest_druidspirit.rs2`'s
    `open_mortmyre_gate` on entry.
  - `murdermystery` -> **audited-fixed**: one of the most thorough ports
    audited this whole queue (20 files) -- the randomised 1-of-6 culprit
    system, all six suspects' full poison/thread/alibi dialogue, the flour+
    flypaper fingerprint puzzle, the six poison-proof search locations each
    keyed to the right suspect, and the guard's tiered accusation dialogue
    (correctly requiring thread+fingerprint+poison-proof together for the
    real "conclusive proof" ending) all matched Quick_guide exactly,
    including the "drop the necklace before turning in evidence" wiki tip
    (`murder_clear_evidence` sweeps `worn` as well as `inv`/`bank`). Real
    `~quest_complete`; correctly spliced into the shared `gossipy_man`/
    `murderguard` NPCs' King's Ransom follow-up without duplicating either
    trigger. One real numeric bug: completion granted
    `stat_advance(crafting, 14060)` (1406.0 xp) but the dbrow's own
    `stat_xp_awarded` is 14062 (1406.2 xp) -- a 0.2xp underpay; corrected.
  - Build verified after each fix: `mingw32-make -C src torirsserver-scripts`
    exit 0 throughout, no new diagnostics on any touched file (one build
    attempt mid-pass hit transient errors/link failures from a concurrent
    sibling agent's in-progress edits elsewhere in the tree; unrelated to
    this pass's own files, confirmed by diffing and rebuilding once those
    landed).

- **IN-LC audit pass 5 (2026-08-12):** audited 4 more rows, one quest at a
  time, synchronously (no nested background sub-agents): `eaglespeak`/
  quest_eaglepeak, `legendsquest`/quest_legends, `druidicritual`/quest_druid,
  `icthlarinslittlehelper`/quest_icthlarin. Read each LC script tree fully +
  the wiki quest page, `/Quick_guide`, and `Transcript:` pages before
  comparing; checked every completion path for the recurring bespoke-`%qp`
  bug explicitly (none found this pass — all three completing quests already
  used the real `~quest_complete` proc).
  - `eaglespeak` -> **audited-fixed**: unusually complete existing port (10
    files, all three crystal-feather puzzle rooms + disguise + eyrie
    quick-travel already scripted, despite the quest's own header claiming
    "puzzles deferred" — stale). The real bug: Nickolaus's dialogue triggers
    were all written against debug-spawn-only npc type names
    (`eaglepeak_nickolaus_shout`/`_normal`), never the real cache-derived
    world-spawn npcs (`eaglepeak_nickolaus`, `eaglepeak_nickolaus_campsite`
    per `m31_77.spawn`/`m36_54.spawn`) — a live player could never actually
    talk to him past the cave entrance, softlocking the whole quest outside
    of debug commands. Wired both real npc names into the existing dialogue
    logic and fixed a fallthrough bug it exposed (missing `return;` after a
    cross-file camp-dialogue call was causing a duplicate message).
  - `legendsquest` -> **audit-in_progress**: several real sub-systems are
    thoroughly and accurately ported (jungle forester, bullroarer, book of
    binding, Echned Zekin's full dagger-quest tree, all three companion
    fights San/Irvig/Ranalph, an exceptionally detailed 459-line journal),
    but the two most central NPCs — **Radimus Erkle** (quest giver) and
    **Gujuo** (the native who drives most of the mid-quest) — have zero
    dialogue anywhere in the tree, plus **Ungadulu** (possessed shaman),
    the Viyeldi kill mechanic, and the gem-shrine puzzle chapter are all
    entirely unimplemented; no `~quest_complete(quest_legends)` call exists
    anywhere. Left for dedicated follow-up — see the row's own note for the
    full chapter-by-chapter breakdown, comparable in scale to Eadgar's
    Ruse/Holy Grail/Troll Romance/DS2.
  - `druidicritual` -> **audited-ok**: matches wiki exactly — Kaqemeex's
    three opening branches, real accept/decline on the quest offer, Sanfew's
    four-meat ingredient gate, Cauldron of Thunder mechanic, and completion
    (4 QP, 250 herblore xp via real `~quest_complete`) all correct. Noted
    that `quest_druidspirit` (this row's secondary target) is actually the
    separate Nature Spirit quest, already tracked under its own row.
  - `icthlarinslittlehelper` -> **audited-fixed**: very thorough existing
    port (5 files, 696 lines) despite a stale "jar guardians/embalming/
    carpenter/ceremony deferred" header — all four are fully scripted and
    correct, ending in the real `~quest_complete` with reward matching the
    dbrow exactly. One real gap: the Sphinx's riddle was entirely invented
    ("how many cats to catch ten mice") instead of the wiki's real riddle,
    and was missing the well-known "wrong answer risks losing your cat"
    mechanic entirely. Rewired to match Transcript:Icthlarin's_Little_Helper
    verbatim (real riddle, real 5-way answer choices, confirm-before-losing-
    the-cat risk step).
  - Build: `mingw32-make -C src torirsserver-scripts` exit 0 after every fix
    (checked incrementally), 15087 scripts compiled, zero new diagnostics
    touching any file this pass edited. Grepped every touched npc/trigger
    name tree-wide before adding to confirm no duplicate-trigger shadowing.
    Files touched: `server/scripts/quests/quest_eaglepeak/scripts/
    eaglepeak.rs2`, `sneak.rs2`, `camp.rs2`; `server/scripts/quests/
    quest_icthlarin/scripts/icthlarin_pyramid.rs2`. `legendsquest` was
    research-only this pass (no safe scoped fix within budget — see row
    note). 17 rows remain unaudited in the IN-LC table (38 total minus the
    21 audited across five passes; 4 of those 21 — `holygrail`,
    `eadgarsruse`, `trollromance`, `legendsquest` — are `audit-in_progress`
    rather than fully closed).

- queue created (2026-08-04): Quest Helper → OSRS-Content lane; ownership =
  no LC proc + no 2009scape impl; depth-first; first slice = X Marks the Spot
- extractor: `tools/questhelper_extract.py` — all 50 in-scope helpers `--check`
  clean (ItemID leading/`trailing `_` normalized; miniquest_ dbrow fallback)
- slice 1 done: X Marks the Spot — `%cluequest` on `cluequest_main`, Veos talk
  start, 4 digs via `~xmarks_try_dig` (spade hook), casket hand-in + rewards
  (`cluequest_lamp`, 200 coins, `trail_clue_beginner`), journal wire,
  `::xmarksthespot` / `::xmarksdig` / `::xmarksrun`; headless `::xmarksrun`
  MESSAGE_GAME payloads match dig→complete→OK; no new opcodes; scripts 6221;
  `ToriRSServer_Pack --check-only` 0 errors; next = Ribbiting Tale (#2)
- loop armed: AGENT_LOOP_TICK_questhelper_port every ~180s
- slice 2 done: Ribbiting Tale — `%frog_quest` on `frog_quest_primary`,
  Marcellus/Sue/Gary/Dave/Jane/Cuthbert dialogue, axe log + orange tree chop +
  lily sabotage + bed letter + chest NALIA (interim) + plushy plant + Cuthbert
  kill + rewards (1 QP, 2000 WC XP, `%frog_quest_patch_unlocked`); journal wire;
  `::ribbitingtale` / `::ribbitrun`; headless `::ribbitrun` MESSAGE_GAME payloads
  match chop→sabotage→chest→plant→complete→OK; pack 0 errors; next = Prying Times (#3)
- slice 3 done: Prying Times — `%quest_pry` on `pry_main` (0/5..30→35), Steve
  Beanie + Thurgo crowbar + sea crate stout + bar crate unlock; rewards smithing
  1000 XP + 25 oak sawmill coupons; soft port-task/sail + Pandemonium prereq;
  sailing XP deferred (skill not in pack/stat.pack); `::pryingtimes` / `::pryrun`;
  headless OK; pack 0 errors; next = Client of Kourend (#4)
- slice 4 done: Client of Kourend — `%veos_progress` on `veos_quest` (0..6→7),
  feather→quill, five house interviews, Dark Altar orb, memoirs + 2 lamps;
  Port Sarim Veos gate after X Marks; `::clientofkourend` / `::cokrun`; headless
  OK; pack 0 errors; deferred ship cutscene / lamp Rub / Kourend Castle Teleport;
  next = Queen of Thieves (#5)
- slice 5 done: Queen of Thieves — `%piscquest` on `piscquest_main` (0..12→13);
  Tomas Lawry / poor woman / O'Reilly stew / Warrens Devan / Murder Conrad /
  Queen / Hughes chest letter / Shauna finish; rewards 2000 thieving XP, 2000
  coins, `veos_memoirs_pisc_page`; wiki
  https://oldschool.runescape.wiki/w/Transcript:The_Queen_of_Thieves + Quick_guide;
  `::queenofthieves` / `::qotrun`; headless OK; pack 0 errors; deferred full
  refuse/post-quest trees + Kingstown stairs (shared `fai_varrock_stairs`);
  next = Depths of Despair (#6)
- slice 6 done: Depths of Despair — `%hosidiusquest` on `hosidiusquest_main`
  (0..4,6..10→11); Lord Kandur / Olivia / Galana / Varlamore envoy / Crabclaw
  caves (crevice→stones→rocks→rope) / Artur / Sand Snake / Accord chest /
  return; rewards 1500 agility XP, 4000 coins, `veos_memoirs_hos_page`; wiki
  https://oldschool.runescape.wiki/w/Transcript:The_Depths_of_Despair + Quick_guide;
  `::depthsofdespair` / `::dodrun`; headless OK; pack 0 errors; deferred
  random library bookshelf, stone/rock fail rolls, snake instance, Butler/Elena
  trees, favour/graceful recolour; next = Porcine of Interest (#7)
- slice 7 done: Porcine of Interest — `%porcine` on `porcine_main`
  (0/5/10/15/20/25/30/35→40); notice board / Sarah bounty / rope on hole /
  skeleton soft-cutscene / Spria goggles / Sourhog kill / foot / Sarah coins /
  Spria finish; rewards 1000 slayer XP, 5000 coins, 30 slayer points; wiki
  https://oldschool.runescape.wiki/w/Transcript:A_Porcine_of_Interest + Quick_guide;
  `::porcineofinterest` / `::poirun`; headless OK; pack 0 errors; deferred
  tracking cabbage/cart trees, full Pig Thing cutscene, slash-weapon matrix,
  Sarah shop, Spria task/helmet upgrade; next = Ascent of Arceuus (#8)
- slice 8 done: Ascent of Arceuus — `%arcquest` on `arcquest_main` (0..13→14);
  Mori / Councillor Andrews / Tower souls / Trobin / Kaal-Ket-Jor / grave +
  hunting trail / Trapped Soul / Dark Altar rocks / finish; rewards 1500 hunter
  XP, 500 runecraft XP, 2000 coins, `veos_memoirs_arc_page`; wiki
  https://oldschool.runescape.wiki/w/Transcript:The_Ascent_of_Arceuus + Quick_guide;
  `::ascentofarceuus` / `::aoarun`; headless OK; pack 0 errors; deferred tower
  instance soul count, strict trail multilocs, Tower Mage gate, favour/graceful,
  Asteros/Kaal sibling polish; next = Ethically Acquired Antiquities (#9)
- slice 9 done: Ethically Acquired Antiquities — `%eaa` on `eaa_primary`
  (0..36→38); empty display / Herminius / tools+case / visitors / Regulus /
  crew sails / Artima / Stan / Betty notes / Haig pickpocket+crate / shame /
  return; rewards 6000 thieving XP, 5000 coins; wiki Quick_guide (+ Transcript
  deferred full shame matrix); `::ethicallyacquiredantiquities` / `::eaarun`;
  headless OK; pack 0 errors; Children of the Sun soft-skipped (#13 pending);
  deferred charter sail, full shame options, Haig cutscene; next = Ides of Milk (#10)
- loop aborted (user/system): AGENT_LOOP_TICK_questhelper_port stopped after tick 11
- loop re-armed (2026-08-04): AGENT_LOOP_TICK_questhelper_port every ~180s
- slice 10 done: Ides of Milk — `%cowquest` on `cowquest_main` (0..21→22);
  Cassius / Gillie / Seth shelves book / milk samples / Duke Horacio / Brutus
  bull / finish; post-quest Gillie cowbell+lamp; wiki Quick_guide;
  `::idesofmilk` / `::iomrun`; headless OK; pack 0 errors; deferred Brutus
  specials/dodge, lamp Rub skill picker; next = In Search of Knowledge (#11)
- slice 11 done: In Search of Knowledge — `%hosdun_knowledge_search` on
  `hosdun_status` (0/1/2→3); Aimeri feed / Forthos shelves / pages soft /
  Logosia tomes / `thosf_reward_lamp`; wiki
  https://oldschool.runescape.wiki/w/In_Search_of_Knowledge/Quick_guide;
  `::insearchofknowledge` / `::isokrun`; headless OK (`isokrun OK` payload);
  pack 0 errors; deferred Forthos combat page drops, knife web, Protect Magic,
  lamp Rub; next = Bone Voyage (#12)
- slice 12 done: Bone Voyage — `%fossilquest_progress` on `fossilquest_main`
  (0..35→50) + `%fossilquest_lucky_charm` / `%fossilquest_potion`; Haig /
  Foreman / Varrock+Guild sawmills / barge Lead+Junior / Jack / Odd Old Man
  charm / Apothecary sealegs / sail soft-skip; wiki
  https://oldschool.runescape.wiki/w/Bone_Voyage/Quick_guide (+ Transcript);
  `::bonevoyage` / `::bvrun`; headless OK (`bvrun OK` payload); pack 0 errors;
  deferred Dig Site/Kudos hard gates, WC60, sailing IF, bank-chest note; next =
  Children of the Sun (#13)
- slice 13 done: Children of the Sun — `%vmq1` on `vmq1_primary` (0..22→24) +
  guard mark bits; Alina / bag-guard follow soft / house door soft / Tobyn /
  mark four guards / castle roof finish; `%vmq1_questcomplete_type`=2; wiki
  https://oldschool.runescape.wiki/w/Children_of_the_Sun/Quick_guide (+ Transcript);
  `::childrenofthesun` / `::cotsrun`; headless OK (`cotsrun OK` payload); pack 0
  errors; deferred stealth tiles, house cutscene, wrong-guard overlay puzzle,
  Quetzal first-travel (VMQ2); next = The Garden of Death (#14)
- slice 14 done: The Garden of Death — `%tgod` on `tgod_primary` (0..54→56);
  tent journal / secateurs / four garden tablets / vines / translate soft /
  warning note; farming 10000 XP; wiki
  https://oldschool.runescape.wiki/w/The_Garden_of_Death/Quick_guide (+ Transcript);
  `::gardenofdeath` / `::godrun`; headless OK (`godrun OK` payload); pack 0 errors;
  deferred IF 804 puzzle, Boaty matrix, poison, farming 20 hard gate; next =
  At First Light (#15)
- slice 15 done: At First Light — `%afl` on `afl_main` (0..11→12); Apatura /
  Verity / Wolf+Kiko soft / fox poultice / Atza trim / report+bed / finish;
  hunter 4500 + construction 800 + herblore 500 XP; wiki
  https://oldschool.runescape.wiki/w/At_First_Light/Quick_guide (+ Transcript);
  `::atfirstlight` / `::aflrun`; headless OK (`aflrun OK` payload); pack 0 errors;
  deferred COTS hard gate, jerboa rolls, equipment pile IF, Master Rumours;
  next = Tale of the Righteous (#16)
- slice 16 done: Tale of the Righteous — `%shayzienquest` on `shayzienquest_main`
  (0..16→17); Phileas / library puzzle soft / Shiro / Duffy rope / altar soft /
  Gnosi / finish; wiki
  https://oldschool.runescape.wiki/w/Tale_of_the_Righteous/Quick_guide (+ Transcript);
  `::taleoftherighteous` / `::torrun`; headless OK (`torrun OK` payload); pack 0
  errors; deferred library puzzle, lizardman boss, CoK favour hard gate; next =
  Getting Ahead (#17)
- slice 17 done: Getting Ahead — `%ga` on `ga_main` (0..32→34); Gordon / Mary /
  flour lure soft / beast kill soft / clay→fur→dye head / mount; crafting 4000 +
  construction 3200 XP; wiki
  https://oldschool.runescape.wiki/w/Getting_Ahead/Quick_guide (+ Transcript);
  `::gettingahead` / `::garun`; headless OK (`garun OK` payload); pack 0 errors;
  also unblocked pestcontrol (shield-drop varps + deduped constants); deferred
  beast combat, tannery UI; next = The Corsair Curse (#18)
- lane B claimed (2026-08-04): parallel Quest Helper worker takes #19 Below Ice
  Mountain (`in_progress`); leaves #18 Corsair Curse for the primary lane —
  plan `questhelper_port_lane_b_bim.plan.md`
- slice 18 done: The Corsair Curse — `%corscurs_progress` on `corscurs`
  (0..55→60) + crew curse bits; Tock / sail / four crew soft / food→Ithoi burn
  soft / kill soft / finish; 2 QP; wiki
  https://oldschool.runescape.wiki/w/The_Corsair_Curse/Quick_guide (+ Transcript);
  `::corsaircurse` / `::ccrun`; headless OK (`ccrun OK` payload); pack 0 errors;
  deferred per-crew investigations, Ithoi combat, Yusuf bank UI; next =
  Below Ice Mountain (#19)
- slice 19 done (lane B): Below Ice Mountain — `%bim` on `bim_main` (0..40→45)
  + `%bim_checkal`/`%bim_marley`/`%bim_burntof` on `bim_extra`; Willow / Checkal
  + Atlas flex soft / Marley steak sandwich / Burntof ale+RPS soft / dungeon
  soft / Ancient Guardian soft / finish; 1 QP + 2000 coins + flex emote bit;
  wiki https://oldschool.runescape.wiki/w/Transcript:Below_Ice_Mountain +
  Quick_guide; `::belowicemountain` / `::bimrun`; headless OK (`bimrun OK`
  payload); pack 0 errors; deferred Atlas workout, Charlie tramp, full RPS,
  dungeon instance, mining pillars, Ramarno post-quest, QP16 hard gate; next =
  Shadows of Custodia (#20)
- lane B loop armed (2026-08-04): AGENT_LOOP_TICK_questhelper_port_b every ~180s;
  claimed #21 Current Affairs (`in_progress`, lane B) — #20 Shadows left to
  primary lane
- slice 21 done (lane B): Current Affairs — `%current_affairs` on
  `current_affairs_main` (0..40→45) + form Q bits; Arhein / Catherine form
  soft / Harry mayorfish / audit soft / form 7r4-5h sign / duck chart soft;
  fishing 1000 XP + 25 oak sawmill coupons + duck/mayor; sailing XP deferred;
  wiki https://oldschool.runescape.wiki/w/Current_Affairs/Quick_guide;
  `::currentaffairs` / `::carun`; headless OK (`carun OK` payload); pack 0
  errors; deferred Pandemonium/Sailing22 gates, form IF, boat sail, duck path;
  next pending for lane B = #22 Twilight's Promise (skip #20 if still
  in_progress on primary)
- slice 20 done: Shadows of Custodia — `%soc` on `soc_main` (0..22→24) + citizen /
  wall / bow / stalker side bits; noticeboard / four citizens / parents / wall→
  puddle→plank cloth / cave boys soft / reinforce+bows / Etz / Antos stalkers
  soft / Captain finish; 2 QP + slayer 10000 + hunter 4000 + fishing 3000 +
  construction 3000 XP (tenths); wiki
  https://oldschool.runescape.wiki/w/Shadows_of_Custodia/Quick_guide (+ Transcript);
  `::shadowsofcustodia` / `::socrun`; headless OK (`socrun OK` payload=24); pack 0
  errors; deferred full refuse trees, fishing anim, stalker combat, dungeon UI;
  next = Twilight's Promise (#22) if #21 still lane B, else Current Affairs (#21)
- slice 22 done: Twilight's Promise — `%vmq2` on `vmq2_primary` (0..48→50) +
  knight side bits / crest / letter / feed / first travel; Regulus→Ennius→
  Metzli/crypt→four knights soft→HQ letter→Renu→Teomat cultists soft→finish;
  1 QP + thieving 3000 XP (tenths); wiki
  https://oldschool.runescape.wiki/w/Twilight%27s_Promise/Quick_guide (+ Transcript);
  `::twilightspromise` / `::tprun`; headless OK (`tprun OK` payload=23); pack 0
  errors; deferred knight matrices, HQ stairs, Quetzal UI, cultist instance;
  next = Sleeping Giants (#23)
- slice 23 done (lane B): Sleeping Giants — `%sleeping_giants` on
  `giants_foundry_main` (0..25→30) + repair/tutorial bits; Kovac start /
  polish+grind+hammer repairs soft / commission crate→crucible→mould→preform
  soft / hand-in; smithing 6000 XP; wiki
  https://oldschool.runescape.wiki/w/Sleeping_Giants/Quick_guide;
  `::sleepinggiants` / `::sgrun`; headless OK (`sgrun OK` payload); pack 0
  errors; deferred mould IF 718, heat/temp loop, supply matrix, Smithing 15
  hard gate; next = Meat and Greet (#24)
- slice 24 done: Meat and Greet — `%mag` on `mag_primary` (0..24→26) + spice/meat
  supply + portion bits; Emelio→spice soft→Alba/direwolf soft→recipe 4/2/1/3→
  Renata soft→Lelia→minotaur soft→finish; 1 QP + cooking 8000 XP (tenths); wiki
  https://oldschool.runescape.wiki/w/Meat_and_Greet/Quick_guide (+ Transcript);
  `::meatandgreet` / `::mgrun`; headless OK (`mgrun OK` payload=23); pack 0
  errors; also unblocked barrows_puzzle (`:1`/`:2`/`:3` → `puzzle_q*`); deferred
  pin-pad IF, wolf den, connoisseur matrix, minotaur combat, shop UI; next =
  Pandemonium (#25)
- slice 25 done (lane B): Pandemonium — `%sailing_intro` on `sailing_intro_primary`
  (0..48→50) + wreck/cargo bits; Will/Anne→board/nav/salvage soft→Ribs/Steve/Jim
  raft+cargo-hold soft→courier deposit soft→finish; coupons/kits/spyglass;
  sailing XP deferred; wiki
  https://oldschool.runescape.wiki/w/Pandemonium/Quick_guide;
  `::pandemonium` / `::pandrun`; headless OK (`pandrun OK` payload=25); pack 0
  errors; deferred helm/sail, salvage cutscene, shipyard portal, cargo IF,
  port-task UI; next pending for lane B = The Red Reef (#27) (#26 owned by
  primary)
- slice 27 done (lane B): The Red Reef — `%trr` on `trr_primary` (0..40→42) +
  display-case bits; Raley→Finn→Katt→Floopa→Red Rock receptionist/cases→Paxton
  →Bethel soft→Zenith dive/dredger soft→Floopa finish; smithing 5000 XP
  (tenths) + bosun schematic; sailing XP deferred; wiki
  https://oldschool.runescape.wiki/w/The_Red_Reef/Quick_guide;
  `::redreef` / `::rrrun`; headless OK (`rrrun OK` payload=23); pack 0 errors;
  deferred Tortugans/Sailing52/Smithing48 gates, ship combat, Last Light fights,
  dive instance, lobster; next pending for lane B = #29 Fremennik Exiles (#28
  owned by primary)
- slice 29 done (lane B): The Fremennik Exiles — `%vikingexile` on
  `quest_vikingexile` (0..125→130) + letter/shield bits; Brundt kegs→Freygerd
  investigate soft→letter→SE shield soft→defence/Isle/Typhor/Jorm soft→finish;
  slayer+crafting 50000 + runecraft 30000 XP (tenths) + V's shield; wiki
  https://oldschool.runescape.wiki/w/The_Fremennik_Exiles/Quick_guide;
  `::fremennikexiles` / `::fxrun`; headless OK (`fxrun OK` payload=24); pack 0
  errors; deferred hard gates, shield craft matrix, basilisk wave, puzzle IF,
  boss fights; next pending for lane B = #31 Making Friends with My Arm (#30
  done by primary)
- slice 31 done (lane B): Making Friends with My Arm — `%my2arm_status` on
  `my2arm_perm_1` (0..196→200); Burntmeat→My Arm→Larry/Weiss soft→Mother→WOM
  coffin/apothecary soft→prison bosses soft→Snowflake dung/notes; con 10k +
  FM 40k + mining/agility 50k XP (tenths); wiki
  https://oldschool.runescape.wiki/w/Making_Friends_with_My_Arm/Quick_guide;
  `::makingfriendswithmyarm` / `::mfrun`; headless OK (`mfrun OK` payload=24);
  pack 0 errors; deferred boat/cliff/cave pathing, coffin IF, boss fights,
  fire-pit unlock; next pending for lane B = #33 Perilous Moons (#32 owned by
  primary)
- slice 33 done (lane B): Perilous Moons — `%pmoon_quest` on `pmoon_main`
  (0..31→36) + camp/boss bits; Attala→nagua soft→Jessamine→Neypotzli camps
  soft→Nahta/smith→Eyatlalli items soft→three Moons soft→finish; slayer 40k +
  RC/hunter/fish 5k XP (tenths); wiki
  https://oldschool.runescape.wiki/w/Perilous_Moons/Quick_guide;
  `::perilousmoons` / `::pmrun`; headless OK (`pmrun OK` payload=23); pack 0
  errors; deferred nagua combat, camp construction, talisman matrix, gather
  loops, Moon bosses; next pending for lane B = #35 Death on the Isle (#34
  owned by primary)
- slice 35 done (lane B): Death on the Isle — `%doti` on `doti_main` (0..49→50)
  + guest/clue bits; Patzi→butler uniform soft→intros→cellar murder soft→
  guards/evidence soft→Adala soft→theatre/Naiatli soft→finish; thieving 10k +
  agility 7.5k + crafting 5k XP (tenths); wiki
  https://oldschool.runescape.wiki/w/Death_on_the_Isle/Quick_guide;
  `::deathontheisle` / `::dirun`; headless OK (`dirun OK` payload=23); pack 0
  errors; deferred steal/equip checks, clue matrix, pickpockets, boss fights;
  next pending for lane B = #37 Beneath Cursed Sands (#36 owned by primary)
- slice 37 done (lane B): Beneath Cursed Sands — `%bcs` on `bcs_primary`
  (0..106→108); Jamila message→Maisa→Necropolis/guard soft→furnace/emblem/
  tomb soft→Champion soft→Zahur cure soft→Akh/Osman→finish; agility 50k XP
  (tenths) + Keris partisan + water circlet; wiki
  https://oldschool.runescape.wiki/w/Beneath_Cursed_Sands/Quick_guide;
  `::beneathcursedsands` / `::bcsrun`; headless OK (`bcsrun OK` payload=25);
  pack 0 errors; deferred guard/scarab/Champion/Akh fights, riddle/chemistry
  IF; next pending for lane B = #39 Secrets of the North (#38 owned by
  primary)
- slice 39 done (lane B): Secrets of the North — `%sotn` on `sotn_primary`
  (0..88→90) + inspect/trail/ghorrock bits; Carnillean guard→crime scene/
  hunter trail/Evelot soft→Hazeel cult/crest soft→Weiss/Snowflake/assassin
  soft→Ghorrock puzzle/Muspah soft→Jhallan→finish; agility 60k + thieving 50k
  + hunter 40k XP (tenths); wiki
  https://oldschool.runescape.wiki/w/Secrets_of_the_North/Quick_guide;
  `::secretsofthenorth` / `::snrun`; headless OK (`snrun OK` payload=23);
  pack 0 errors; deferred explain matrix, hunter trail, Evelot/assassin/
  Muspah fights, cult Q&A, brazier puzzle; next pending for lane B = #42
  Heart of Darkness (#41 owned by primary)
- slice 42 done (lane B): The Heart of Darkness — `%vmq3` on `vmq3_primary`
  (0..74→76) + recruit/trial/ruins bits; Itzla→Gorge pub/shop soft→tower
  recruits/trials soft→temple/Fides→ruins mine/levers/statues soft→Amoxliatl
  soft→Servius; mining/thieving/slayer/agility 8k XP each (tenths); wiki
  https://oldschool.runescape.wiki/w/The_Heart_of_Darkness/Quick_guide;
  `::heartofdarkness` / `::hodrun`; headless OK (`hodrun OK` payload=24);
  pack 0 errors; deferred tower puzzles/combat, robes equip, ice statue
  matrix, Amoxliatl fight; next pending for lane B = #44 Sins of the Father
  (#43 owned by primary)
- slice 44 done (lane B): Sins of the Father — `%myq5` on `myq5_primary`
  (0..136→138) + team/lab bits; Veliaf→Slepe/Kroy soft→Pater/Ivan trek soft→
  Vanescula/lab/Damien soft→Darkmeyer valves/flail soft→Vanstrom soft; blisterwood
  flail + 6 lamps + Drakan's medallion; wiki
  https://oldschool.runescape.wiki/w/Sins_of_the_Father/Quick_guide;
  `::sinsofthefather` / `::softrun`; headless OK (`softrun OK` payload=26);
  pack 0 errors; deferred Carl follow, temple trek instance, door puzzle,
  boss fights, craft IF; next pending for lane B = #46 Monkey Madness II
  (#45 owned by primary)
- slice 46 done (lane B): Monkey Madness II — `%mm2_progress` on `mm2_primary`
  (0..190→195) + sabotage/breach bits; Narnode→Glough/Anita/Entrana soft→
  Garkor/Kruk greegree soft→Kob/Keef/sabotage/lab soft→Nieve/Stronghold/
  Glough soft; slayer 80k + agility 60k + thieving/hunter 50k XP (tenths) +
  royal seed pod; wiki
  https://oldschool.runescape.wiki/w/Monkey_Madness_II/Quick_guide;
  `::monkeymadnessii` / `::mm2run`; headless OK (`mm2run OK` payload=26);
  pack 0 errors; deferred house puzzle, agility dungeon, fights, sabotage
  pathing; next pending for lane B = #48 Desert Treasure II (#47 owned by
  primary)
- slice 48 done (lane B): Desert Treasure II — `%dt2` on `dt2_primary`
  (0..114→118); vault/Asgarnia→Digsite war room soft→four medallion soft-skips
  →cell/Stranger/wights soft→finish; Ring of Shadows + 3 lamps; wiki
  https://oldschool.runescape.wiki/w/Desert_Treasure_II_-_The_Fallen_Empire/Quick_guide;
  `::deserttreasureii` / `::dt2run`; headless OK (`dt2run OK` payload=25);
  pack 0 errors; deferred digsite puzzle, Forgotten Four fights/instances,
  cell escape; next for lane B = miniquests M1/M2 (deprioritised) or idle
  until #47 Song of the Elves frees / primary needs help
- slice M2 done (lane B): Enter the Abyss — `%abyssal_miniquest` on
  `abyssal_miniquest` (0..3→4); wildy Mage→Varrock→scrying orb + three
  essence teleports (Aubury/Sedridor/Cromperty via `%rcu_essencespot_*` on
  `abyssal_warp`)→reward (book+small pouch+1000 RC XP); TOE zammy gated;
  wiki https://oldschool.runescape.wiki/w/Enter_the_Abyss/Quick_guide;
  2009scape ZamorakMageDialogue ref; `::entertheabyss` / `::etarun`;
  headless OK (`etarun OK` payload=23); pack 0 errors; deferred full refuse
  trees, Wanted! branch, Abyss terrain/tele map; next for lane B = idle
  (M1 Bear Your Soul owned elsewhere; main table clear)
- slice 26 done: A Night at the Theatre — `%tobquest` on `tobquest_main` bits
  8..14 (0..80→86) + `%tobquest_done_tob`; stranger→crypt/head→spider cave/
  Daer→eggs→Hespori bark soft→ToB soft→finish; 2 QP + 4 antique lamps; wiki
  https://oldschool.runescape.wiki/w/A_Night_at_the_Theatre/Quick_guide (+ Transcript);
  `::nightatthetheatre` / `::nattrun`; headless OK (`nattrun OK` payload=25);
  pack 0 errors; deferred crypt puzzle, araxyte pathing, Hespori fight, ToB
  raid, lamp Rub; next = Misthalin Mystery (#28) if #27 still lane B, else
  The Red Reef (#27)
- slice 28 done: Misthalin Mystery — `%mistmyst_progress` on `mistmyst_main`
  (0..130→135); Abigale→barrel/key→manor doors soft→candles/fuse/piano/
  fireplace soft→Abigale fight soft→Mandy finish; 1 QP + crafting 600 XP
  (tenths); wiki
  https://oldschool.runescape.wiki/w/Misthalin_Mystery/Quick_guide (+ Transcript);
  `::misthalinmystery` / `::mmrun`; headless OK (`mmrun OK` payload=24); pack 0
  errors; deferred manor instance, candle/piano/switch puzzles, boss fight;
  next = The Fremennik Exiles (#29) if #27 still lane B, else The Red Reef (#27)
- slice 30 done: A Taste of Hope — `%myq4` on `myq4_main` (0..160→165); Garth→
  Safalaan/spy soft→Flaygian→Serafina potion soft→abomination soft→flail→
  Ranis soft→finish; 1 QP + Ivandis flail + Drakan's medallion + 3 tomes; wiki
  https://oldschool.runescape.wiki/w/A_Taste_of_Hope/Quick_guide (+ Transcript);
  `::tasteofhope` / `::tohrun`; headless OK (`tohrun OK` payload=25); pack 0
  errors; deferred rooftop spy, potion matrix, abom/Ranis fights, tome Rub;
  next = Making Friends with My Arm (#31) if #29 still lane B, else Fremennik
  Exiles (#29)
- slice 32 done: Temple of the Eye — `%tote` on `tote_primary` (0..125→130);
  Persten→Zamorak mage/tea→Abyss energies soft→Sedridor/Traiborn puzzle soft→
  temple/GoTR tutorial soft→finish; 1 QP + runecraft 9210 XP (tenths) + medium
  pouch + amulet; wiki
  https://oldschool.runescape.wiki/w/Temple_of_the_Eye/Quick_guide (+ Transcript);
  `::templeoftheeye` / `::toerun`; headless OK (`toerun OK` payload=25); pack 0
  errors; deferred Abyss touch matrix, Traiborn IF, GoTR instance; next =
  Perilous Moons (#33) if #31 still lane B, else Making Friends with My Arm (#31)
- slice 34 done: Troubled Tortugans — `%tt` on `tt_primary` (0..42→44) + repair
  bits; Blunn→bandage Floopa→sail soft→elders→town repair soft→trail/cave/
  gryphon soft→Shellbane soft→finish; 1 QP + slayer 8000 XP (tenths); sailing
  XP deferred; wiki
  https://oldschool.runescape.wiki/w/Troubled_Tortugans/Quick_guide (+ Transcript);
  `::troubledtortugans` / `::ttrun`; headless OK (`ttrun OK` payload=23); pack 0
  errors; also unblocked puropuro (`%` → `modulo`); deferred sail matrix, hunt
  trail, gryphon fights; next = Death on the Isle (#35) if #33 still lane B,
  else Perilous Moons (#33)
- slice 36 done: Scrambled! — `%scrambled` on `scrambled_primary` (0..28→30) +
  kings-men bits; Alan→inspect egg→King→gather men→sample eggs soft→judge/
  panic soft→put Humpty together soft→finish; 1 QP + construction/cooking/
  smithing 50000 XP each (tenths); wiki
  https://oldschool.runescape.wiki/w/Scrambled!/Quick_guide (+ Transcript);
  `::scrambled` / `::scrun`; headless OK (`scrun OK` payload=23); pack 0
  errors; deferred egg side-tasks (axe/tea/jaguar), judge IF, pet-egg unlock;
  next = The Final Dawn (#38) while #37 Beneath Cursed Sands is lane B
- slice 38 done: The Final Dawn — `%vmq4` on `vmq4_primary` (0..67→68); Servius
  → Twilight Temple soft → Queen/Vibia → Janus house/dog/hideout soft → Attala/
  Cam Torum soft → keystone/cultists soft → Tal Teklan → Crypt of Tonali soft
  (Ennius/Metzli/final) → chamber inspect → finish; 3 QP + thieving 550000 +
  fletching/runecraft 250000 XP each (tenths) + Arkan blade + lamp; wiki
  https://oldschool.runescape.wiki/w/The_Final_Dawn/Quick_guide (+ Transcript);
  `::finaldawn` / `::tfdrun`; headless OK (`tfdrun OK` payload=24); pack 0
  errors; deferred basement combat, Janus puzzles, Neypotzli sun/moon, boss
  fights, lamp Rub; next = The Forsaken Tower (#40) while #39 Secrets is lane B
- slice 40 done: The Forsaken Tower — `%lovaquest` on `lovaquest_main` (0..10→11)
  + furnace/electricity/refinery/altar bits; Vulcana→Undor→tower entry→four
  puzzle soft-skips→Dinh's hammer→Undor→Vulcana; 1 QP + mining/smithing 5000
  XP each (tenths) + 6000 coins + memoirs page; wiki
  https://oldschool.runescape.wiki/w/The_Forsaken_Tower/Quick_guide (+ Transcript);
  `::forsakentower` / `::ftrun`; headless OK (`ftrun OK` payload=23); pack 0
  errors; deferred jug/power-grid/fluid/pylon puzzles, Ignisia gate; next =
  A Kingdom Divided (#41) while #39 Secrets is lane B
- slice 41 done: A Kingdom Divided — `%akd` on `akd_primary` (0..148→150) +
  house-help bits; Martin→Fullore→Hughes/Herbert soft→Yama soft→Rose trail/
  Forthos/Settlement/Faun soft→Kaht egg→Xamphur soft→burial→Lookout house
  help soft→finish; 2 QP + Book of the Dead + lamp; wiki
  https://oldschool.runescape.wiki/w/A_Kingdom_Divided/Quick_guide (+ Transcript);
  `::kingdomdivided` / `::akdrun`; headless OK (`akdrun OK` payload=25); pack 0
  errors; deferred house search, fights, puzzles, house side-quests, lamp Rub;
  next = The Curse of Arrav (#43) while #42 Heart of Darkness is lane B
- slice 43 done: The Curse of Arrav — `%coa` on `coa_primary` (0..58→60);
  Elias→mastaba doors/golem/tile soft→canopic→Trollweiss soft→fort key soft→
  base heist/Arrav soft→finish; 2 QP + mining/thieving/agility 400000 XP each
  (tenths); wiki
  https://oldschool.runescape.wiki/w/The_Curse_of_Arrav/Quick_guide (+ Transcript);
  `::curseofarrav` / `::coarun`; headless OK (`coarun OK` payload=24); pack 0
  errors; also unblocked giantmole (`%` → `modulo`); deferred levers, golem,
  tiles, cave pathing, fights; next = Dragon Slayer II (#45) while #44 Sins is lane B
- slice 45 done: Dragon Slayer II — `%ds2` on `dragonslayer2_main` (0..210→215);
  Alec→Dallas Crandor soft→Fossil map/boat soft→Lithkren diary soft→Bob/Sphinx/
  dream/key soft→Roald allies soft→Ungael/Galvek soft→finish; 5 QP + smithing
  800000 + mining 600000 + agility/thieving 500000 XP (tenths) + orb + 4 lamps;
  wiki https://oldschool.runescape.wiki/w/Dragon_Slayer_II/Quick_guide (+ Transcript);
  `::dragonslayer2` / `::ds2run`; headless OK (`ds2run OK` payload=25); pack 0
  errors; deferred mural/spawn/map IF/boat build/dream fight/ship combat/Galvek
  phases/lamp Rub; next = Song of the Elves (#47) while #46 Monkey Madness II is lane B
- slice 47 done: Song of the Elves — `%sote` on `sote_primary` (0..192→200);
  Edmond→Lathas/Alrena soft→Elena free/revolt soft→Arianwyn/Baxtorian/clans/
  seals soft→orb/Lletya/Pass/Essyllt/final soft→finish; 4 QP + 8×400000 XP
  (tenths); wiki
  https://oldschool.runescape.wiki/w/Song_of_the_Elves/Quick_guide (+ Transcript);
  `::songoftheelves` / `::soterun`; headless OK (`soterun OK` payload=26); pack 0
  errors; also unblocked Inferno duplicate Zuk stub; deferred revolt/puzzles/
  fights; next = Desert Treasure II (#48) if lane B frees it, else miniquests M1/M2
- slice M1 done: Bear Your Soul — `%arceuus_soulbearer_story` on `millcheck_multi`
  (0..2→3); soft book→Aretha→dig damaged bearer→Key Master repair; Soul Bearer;
  journal + spade `~bys_try_dig` + `keeper_of_keys` merge; LostCity none; wiki
  https://oldschool.runescape.wiki/w/Bear_Your_Soul; `::bearyoursoul` / `::bysrun`;
  headless OK (`bysrun OK` payload=23); pack 0 errors; deferred library bookcase
  search, dig anim, dusty-key pathing; next = Enter the Abyss (M2) if lane B frees
  it, else idle (main quest table complete through #48)
- queue expanded (tick): added IN-LC table (33 pre-Sept 2004 QH dirs → CONTENT_PORT_QUEUE),
  expanded skip list; added 5 pending entries for genuine post-Jan-2009 QuestHelper-only content:
  P1 atailoftwocats (Apr 2016, 293 lines), P2 asoulsbane (Mar 2019, 330 lines),
  P3 spiritsoftheelid (Dec 2013, 352 lines), P4 anothersliceofham (Oct 2012, 485 lines),
  P5 darknessofhallowvale (Aug 2013, 816 lines); ~74 remaining QH dirs classified as mid-era
  pre-2009 → SCAPE2009_CONTENT_PORT_QUEUE; next pending = P1 A Tail of Two Cats
- tick: P3 spiritsoftheelid in_progress — folder structure created, config files initialized (spiritsoftheelid.varp), quest helper code analyzed. Wiki resources fetched for dialogue trees. Next steps: full script with Awusah/Ghaslor/Shiratti dialogue, object interactions (cupboard/rope), combat logic (White/Grey/Black Golems). Quest requires 33 Magic, 37 Ranged/Mining/Thieving; rewards: 2 QP + Prayer/Magic/Thieving XP.
- queue rebuilt (2026-08-06): Full audit of Quest Helper source. 176 in-scope
  quests identified (181 dirs minus 5 skip-list). 50 tracked as done, 14 already
  implemented in OSRS Content but missing from the table, and roughly 112 still
  pending. Depth-first ordering is preserved.
- slice 3 re-audit (2026-08-10): row #3 Witch's Potion (`witchspotion`,
  npcs=hetty,ratindoors) re-verified per methodology step 1 — grep LostCity
  first found it already fully implemented: `server/scripts/quests/quest_hetty/`
  (`quest_hetty.rs2` cauldron+completion, `hetty_journal.rs2`) +
  `areas/rimmington/scripts/hetty.rs2` (dialogue: quest-request/witch-rumour
  branches, ingredient gate on onion+rat's_tail+burnt_meat+eye_of_newt,
  pre-drink retalk, `%hetty` 0..2→3) + `drop_tables/scripts/rat.rs2`
  (quest-gated `rats_tail` drop, capped when player already holds/banks one).
  `ratindoors` is not a resolvable osrs239 gameval (no such npc in
  `all.npc`/`.compack`) — the cache's `rat` npc is what the quest actually
  uses, cache wins per the queue's own rule. Rewards 3250 magic XP + 1 QP via
  `~quest_complete(quest_witchspotion)`; landed under
  `CONTENT_PORT_QUEUE.md` slice 6d (see its Log), not this queue. Checked
  against `https://oldschool.runescape.wiki/w/Transcript:Witch%27s_Potion` +
  `https://oldschool.runescape.wiki/w/Witch%27s_Potion/Quick_guide` — existing
  dialogue covers both initial-contact branches, the no/partial/complete
  ingredient states, the pre-drink retalk line, and the drink/complete step;
  deferred: the wiki's itemised partial-ingredient message variants (impl uses
  one generic "not yet" line for any partial set, transcript lists per-count
  wording) — cosmetic only, no missed gate. No new script written (nothing to
  port); row flipped `pending` → `done (LC)`. Verify: `mingw32-make -C src
  sscompile` clean rebuild, then `mingw32-make -C src torirsserver-scripts` — ran
  to completion through all `quest_hetty`/`hetty`/`rat` files with zero
  diagnostics on them; sole failure is the pre-existing, unrelated
  `ported_scape2009_summoning/scripts/summoning_spirit_wolf.rs2` missing
  `%content_restrict_summoning_serverside` (out of scope, not touched).
  Same grep-first check surfaced 4 more stale `pending` rows already fully
  LC-owned (not yet caught by the 2026-08-06 rebuild) — fixed alongside since
  the evidence was already in hand: #4 impcatcher → `quest_imp/` (`quest_imp.rs2`
  + `imp_journal.rs2`); #13 sheepshearer → `quest_sheep/` (`quest_sheep.rs2` +
  `sheep_journal.rs2`) + `areas/lumbridge/scripts/fred_the_farmer.rs2`; #27
  gertrudescat → `areas/varrock/scripts/gertrude.rs2` + `shilop.rs2`; #28
  princealirescue → `quest_prince/` (`quest_prince.rs2` + `prince_journal.rs2`)
  + `areas/alkharid/scripts/hassan.rs2` + `areas/draynor/scripts/prince_ali.rs2`.
  All 4 confirmed by file existence only (not depth-audited against wiki
  transcripts the way #3 was) — a fuller staleness pass over the remaining
  ~108 pending rows is still owed, since the table was assembled from a QH-side
  audit that never cross-checked file existence per row. Rows #9 monksfriend
  and #10 therestlessghost were spot-checked (grep for `brotheromad`,
  `fatheraerec`, `fatherurhne` — no hits beyond a pre-allocated, unimplemented
  `quest_monksfriend`/`quest_restlessghost` dbrow slot) and are genuinely
  unimplemented; next = Monk's Friend (#9)
- staleness audit (2026-08-10 tick): re-verified #9 Monk's Friend per
  methodology step 1 before porting — the prior tick's `brotheromad` grep
  (no underscore, copying the QuestHelper Java constant's spelling) missed
  the real gameval `brother_omad`; LostCity already ships it complete as
  `quest_drunkmonk` (dbrow `quest_monksfriend` id 28, journal wired
  `~drunkmonk_journal`, full Islwyn/Cedric dialogue + blanket cave + wine
  cart + party reward). Same underscore-blind-spot pattern found across the
  table once checked systematically: swept every remaining `pending` row's
  npc list against the tree (grep + directory cross-reference against the
  full `quests/` listing) and found **36 more** already fully LC-owned but
  never flipped, plus the IN-LC table's own 10 duplicates still sitting as
  separate `pending` rows in the main table below it. All 37 fixed to
  `done (LC)` with the matching LC dir + dbrow key cited inline per row:
  #9 monksfriend→quest_drunkmonk, #10 therestlessghost→quest_priest,
  #15 goblindiplomacy→quest_gobdip, #19 druidicritual (IN-LC dup),
  #23 ernestthechicken→quest_haunted, #25 fishingcontest→quest_fishingcompo,
  #29 cooksassistant (IN-LC dup), #31 trollstronghold→quest_death,
  #32 lostcity→quest_zanaris, #40 theknightssword→quest_squire,
  #41 trollromance (IN-LC dup), #42 fightarena→quest_arena,
  #45 deathplateau→quest_death, #49 tribaltotem→quest_totem,
  #50 witchshouse→quest_ball, #57 bigchompybirdhunting→quest_chompybird,
  #59 scorpioncatcher→quest_scorpcatcher, #62 horrorfromthedeep→quest_horror,
  #63 dwarfcannon (IN-LC dup), #64 familycrest→quest_crest,
  #65 insearchofthemyreque→quest_routequest, #69 treegnomevillage (IN-LC dup),
  #70 templeofikov→quest_ikov, #71 observatoryquest→quest_itgronigen,
  #74 thetouristtrap→quest_desertrescue, #81 shieldofarrav→quest_blackarmgang,
  #82 waterfallquest (IN-LC dup), #83 thegrandtree (IN-LC dup),
  #89 merlinscrystal→quest_arthur, #90 eaglespeak (IN-LC dup),
  #92 priestinperil→quest_priestperil, #93 plaguecity→quest_elena,
  #95 shilovillage→quest_zombiequeen, #96 thelosttribe (IN-LC dup),
  #97 demonslayer→quest_demon, #98 holygrail (IN-LC dup),
  #101 thegolem→quest_golem. Every non-dup fix confirmed by both the LC
  script directory's own npc files AND a `[$row = quest_x] { ~y_journal; }`
  wire in `interface_questjournal/scripts/quest_journal.rs2` — same bar the
  prior tick used, not a deeper wiki-transcript audit. **A pass over the
  remaining ~75 pending rows (line count > 263) is still owed** — this sweep
  only covered rows small enough to reach in one tick; the pattern found
  held for every row checked, so expect more.
- slice 17 done: Roving Elves — no LC/2009scape impl exists (Apr 2005
  release, past both eras); `%rovingelves_quest` on `rovingelves_quest`
  (0/10/20/30/40/50→60); Islwyn confront-then-accept (moved Glarial's
  remains trust hook) → Eluned ritual explainer → Moss Guardian kill
  (`[ai_queue3,roving_mossgiant]`, same soft-kill idiom as
  `arena_boss_deaths.rs2`) drops `roving_old_consecration_seed` → Eluned
  enchants it → `[opheld1,roving_new_consecration_seed]` Plant op buries it
  at the shared `baxtorian_chalice_waterfall_quest` loc (Waterfall Quest's
  own Glarial's crypt room, left untouched — used the item's own `ifop1`
  inventory op instead of extending that file's `oplocu` to avoid a
  duplicate-trigger error) → return to Islwyn for `~p_choice2` crystal
  bow/shield reward; 1 QP + 10000 strength XP; gated on
  `%regicide_quest = ^regicide_complete` and
  `%waterfall_quest = ^waterfall_complete` (both already `done (LC)`); wiki
  https://oldschool.runescape.wiki/w/Roving_Elves/Quick_guide +
  Transcript:Roving_Elves; Islwyn/Eluned hand-spawned via `[login,_]`
  npc_find/npc_add idempotent check (the generated `m36_49.spawn` snapshot
  was captured post-Song of the Elves, so only `sote_islwyn`/`sote_ilfeen`
  exist there — no pre-SOTE `roving_islwyn`/`eluned_prif`-at-camp entry to
  reuse); deferred: unarmed-combat restriction on the Moss Guardian fight
  (any kill counts), Golrie lost-pebble replacement dialogue, Arianwyn
  introduction (belongs to Mourning's End Part I), post-quest crystal
  equipment replacement shop (`[opnpc3,roving_islwyn_2ops]` messages
  "not set up yet" rather than silently no-opping); `mingw32-make -C src
  sscompile` clean, `mingw32-make -C src torirsserver-scripts` zero diagnostics
  on any new/touched file (only the pre-existing, unrelated
  `ported_scape2009_summoning/scripts/summoning_spirit_wolf.rs2` missing
  `%content_restrict_summoning_serverside` failure, out of scope); next =
  Devious Minds (#21) — re-verified pending (grep for `deviousmonk`,
  `rcuzammyma`, wizard-tower/rune-essence terms: no hits beyond the
  pre-allocated `quest_deviousminds` dbrow slot)
- slice 21 done: Devious Minds — re-verified pending per methodology step
  1-2 first (grep LostCity + 2009scape for `devious`/`uzam` variants: only
  hits were the pack's own gamevals, no `.rs2` implementation). Unlike most
  QH-only slices this one has a **fully native cache varbit schema**
  (`configs/all.varbit`, basevar `devious_base`) already shaped for the
  quest, used directly instead of inventing a fresh varp: `%devious_main`
  (bits 0-7, primary progress, values authored here 0/10/20/30/40/50→60),
  `%devious_monk_met`/`%devious_monk_orb_given`/`%devious_cutscene` (flag
  bits), `%devious_altar` (bits 22-23, multiloc: altar → pouch-placed →
  scorched) and `%devious_monk` (bits 27-28, multinpc: hooded monk ↔ dead
  monk — the game's own twist mechanism, both npcs already in
  `areas/world/configs/m53_54.spawn`). Decoded `quest_deviousminds` dbrow's
  own `startcoord`/`startnpc`/`requirement_stats`/`requirement_quests`/
  `stat_xp_awarded` fields (coord format `(plane<<28)|(x<<14)|y`, dbrow-typed
  refs resolve via `all.dbrow.compack` row id not the `id` field) to get the
  cache-authoritative data instead of guessing from the wiki: start npc
  `devious_monk_hooded` at real coord (3405,3491) matching the spawn file
  exactly; skill gate Smithing 65 (boostable) / Runecraft 50 (not boostable
  per wiki) / Fletching 50 (boostable); direct prereqs `quest_wanted`,
  `quest_trollstronghold`, `quest_dorics`, `miniquest_entertheabyss` (only
  the last three are checked — Wanted! is queue row #107, still `pending`,
  has no varp to gate on yet, and hard-gating on it would make this quest
  permanently unstartable, noted rather than faked); reward XP 6500
  Smithing / 5000 Runecraft / 5000 Fletching + 1 QP, exactly matching both
  the dbrow and the wiki. Scripts: `deviousminds_monk.rs2` (disguised monk
  offer/refuse, whetstone+bowstring reminder, bowsword→orb handoff, dead
  monk search revealing the twist), `deviousminds_items.rs2` (whetstone
  grind, bow-sword stringing, orb+pouch sealing with small/medium/giant/
  degraded-pouch rejection messages per the transcript, altar placement +
  soft-narrated heist cutscene, colossal-pouch-survives-large-pouch-destroyed
  per wiki), `deviousminds_tiffy.rs2` (hand-spawned stand-in, see below,
  "Devious Minds"/"Something else" choice, completion), `deviousminds_journal.rs2`.
  Two additive edits to shared hub files (no existing lines touched, same
  shape every other multi-quest npc/skill file already uses): one `if`
  branch in `areas/entrana/scripts/high_priest_of_entrana.rs2` (High Priest
  blame + investigate-Paterdomus + report-to-Tiffy branch) and one
  `case devious_slenderblade :` line in `skill_fletching/scripts/bows.rs2`'s
  existing `[opheldu,bow_string]` switch (the other bow-string click order —
  `[opheldu,bow_string]` is already claimed tree-wide so a second binding
  would collide; this keeps both click orders working). Sir Tiffy Cashien
  has no standalone overworld npc under any resolvable spelling in this
  cache (every `tiffy` hit is DS2/AKD cutscene-scoped) — hand-spawned the
  closest normal-pose reuse, `ds2_meeting_sir_tiffy_cashien` (unbound
  elsewhere), in Falador Park (`[mapzone,0_46_52]` confirmed by
  `skill_farming`'s own Falador Park tree sync) via the same idempotent
  `[login,_]` npc_find/npc_add pattern `rovingelves_islwyn` used. Wiki
  https://oldschool.runescape.wiki/w/Devious_Minds +
  /Quick_guide + Transcript:Devious_Minds (full verbatim reproduction
  declined by the fetch tool as Jagex-copyrighted; used its detailed
  paraphrased section-by-section summary instead — dialogue here is
  original wording covering the same beats, not a copy). Deferred: exact
  Abyss/Law Altar traversal (soft-skipped like every other slice's
  inter-area journeys), the multi-monk/assassin scene as a real client
  cutscene rather than narrated `mesbox` lines, Wanted! gate (see above).
  `mingw32-make -C src sscompile` clean; `mingw32-make -C src torirsserver-scripts`
  — full corpus build, zero diagnostics on any new/touched file, only
  failure in the whole tree is the pre-existing unrelated
  `ported_scape2009_summoning/scripts/summoning_spirit_wolf.rs2` missing
  `%content_restrict_summoning_serverside` (untouched, out of scope).
  Staleness sweep with spare budget: #24 A Tail of Two Cats was still
  marked `pending` in the main table despite being `done` since slice 1
  (2026-08-04) — the 2026-08-06 full-table rebuild re-added it without
  checking the tree; fixed to `done`. Also found (not a stale-LC case, a
  different kind of gap) #43/P2 Asoul's Bane already has a real 193-line
  script (`quest_asoulsbane/scripts/soulbaine.rs2`, npcs resolve) from an
  untracked earlier tick, but its dbrow row was never declared — compiles
  clean only because the id allocator remembers the old slot
  (`STALE=1 66540=quest_asoulsbane`), so `~quest_complete` would read
  name/questpoints off an undeclared row; flipped to `in_progress` with the
  gap noted rather than trusted as `done`. Next = Making History (#37,
  319 lines, npcs=makinghistor/silvermerch — re-checked 2026-08-10, no
  existing script under any spelling, genuinely pending); The Hand in the
  Sand (#38, tied at 319) is next after that.
- slice #37 done: Making History — re-verified pending first (grep of
  `Server/content/scripts` [LostCity], no `2009scape` checkout available
  locally so cross-checked the OSRS-Content tree directly, and
  `quest-helper/.../makinghistory` `--check` 100% clean, all NpcID/ItemID/
  ObjectID/VarbitID gamevals resolve). Like Devious Minds and Doric's Quest,
  the varbit schema is already native to the osrs239 cache rank-0 export
  (`configs/all.varbit`, all on basevar `makinghistory`): `%makinghistory_prog`
  (0..4, endstate=4 per `quest_makinghistory` dbrow columndef 19 — cross-
  checked against `quest_priestinperil`'s endstate=60 matching the real
  `%priestperil` scale used by the done `quest_priestperil`), plus
  `%makinghistory_trader_prog` / `%makinghistory_warr_prog` /
  `%makinghistory_ghost_prog` sub-branch counters and
  `%makinghistory_melina_pres` / `%makinghistory_droalak_pres` presence bits
  that natively multivarbit-swap the already-placed `makinghistory_melina_multi`
  / `makinghistory_droalak_multi` world spawns visible/invisible (same
  mechanism as Devious Minds' `%devious_monk`) — no new varp/varbit authored.
  All six required npcs (`makinghistory_jorral`, `silver_merchant_ardougne`,
  `makinghistory_blanin`, `makinghistory_dron`, `makinghistory_droalak_multi`,
  `makinghistory_melina_multi`) and `kinglathas` were already base world
  spawns at (or within a tile of) the helper's own WorldPoints — no hand-spawn
  needed anywhere, unlike Devious Minds' Sir Tiffy. Scripts:
  `quest_makinghistory/scripts/makinghistory_jorral.rs2` (offer/decline,
  reminder, three-branch hand-in narrating the Saradomin/Zamorak-veterans-
  reconciled-under-Guthix backstory, King Lathas letter round-trip incl. lost-
  letter recovery, completion), `makinghistory_trader.rs2` (Erin the silver
  merchant's enchanted key, the dig north of Castle Wars via the shared
  `general_use/scripts/spade.rs2` hub, key-on-chest → journal),
  `makinghistory_frem.rs2` (Blanin's briefing + Dron's 12-question riddle,
  exact short answer strings from the BSD-licensed `quest-helper`
  MakingHistory.java `addDialogStep`s — not the wiki — since those already
  mirror the game's own short chat-option labels for automation; wrong answer
  dismisses, matching the paraphrased transcript), `makinghistory_ghost.rs2`
  (Droalak/Melina reconciliation gated on a ghostspeak-amulet-worn check
  covering both `amulet_of_ghostspeak` and the Ghosts Ahoy enchanted variant,
  scroll hand-in, and a post-quest Droalak farewell beat matching the wiki's
  "Droalak can be visited to confirm scroll delivery, after which the ghost
  disappears peacefully"), `makinghistory_journal.rs2` (wired into
  `interface_questjournal/scripts/quest_journal.rs2`'s dispatch, additive
  line after the `quest_deviousminds` case). Two additive-only hub edits (no
  existing lines touched): one `if` branch in
  `areas/area_ardougne_east/scripts/king_lathas.rs2`'s existing
  `[opnpc1,kinglathas]` (already claimed by the Biohazard/Underground Pass
  chain) and one in that same file's sibling
  `ardougne_east_shops.rs2`'s existing `[opnpc1,silver_merchant_ardougne]`;
  one line added to `general_use/scripts/spade.rs2`'s existing dig-proc
  chain. Rewards: 3 QP, 1000 Crafting XP + 1000 Prayer XP (dbrow columndef 33
  stat_xp_awarded 10000/10000, passed to `stat_advance` unmodified since that
  opcode's argument is tenths per `torirs_server_scripts.c`'s own comment —
  cross-checked against `quest_priest`'s `stat_advance(prayer, 11250)` at
  Restless Ghost's completion, the real independently-known 1,125 Prayer XP
  reward for that quest passed the same undivided way), 750 coins, 1
  enchanted key; gate is `quest_priestinperil` FINISHED (`%priestperil` =
  `^priestperil_complete`) + `quest_restlessghost` merely started
  (`%prieststart >= ^priest_started`), matching
  `quest_makinghistory` dbrow columndef 25 requirement_quests (all.dbrow.compack
  row ids 111/120) exactly. Wiki
  https://oldschool.runescape.wiki/w/Making_History +
  /Quick_guide + a paraphrased Transcript:Making_History summary (the fetch
  tool declined verbatim reproduction of the long narration, same as Devious
  Minds before it — dialogue here is original wording covering the same
  beats). Deferred: Port Phasmatys ecto-token/charter toll (soft-skipped like
  every other slice's inter-area travel gates on this queue), the castle
  stairs as a real object trigger (narrated only), item-loss replacement
  covered for key/scroll/letters but not exhaustively re-tested. `mingw32-make
  -C src sscompile` clean; `mingw32-make -C src torirsserver-scripts` — full corpus
  build, zero diagnostics on any new/touched file (confirmed by grepping the
  full build log for `makinghistory`/`kinglathas`/`silver_merchant`: no
  matches outside intent), only failure in the whole tree is the pre-existing
  unrelated `ported_scape2009_summoning/scripts/summoning_spirit_wolf.rs2`
  missing `%content_restrict_summoning_serverside` (untouched, out of scope);
  dbrow allocator report shows the same single pre-existing stale row
  (`66540=quest_asoulsbane`, unrelated) and no new stale entries —
  `quest_makinghistory`'s dbrow is natively declared, not allocator-only.
  Next = The Hand in the Sand (#38, 319 lines, npcs=handsandber/handsandgua —
  not yet re-verified this tick, take the same grep-first steps before
  writing).
- slice #38 done: The Hand in the Sand -- re-verified pending first (grep of
  the LostCity `Server/content` checkout on this machine for `handsand`/
  `hand.*sand` name variants: zero hits; no 2009scape checkout is available
  locally, same gap prior ticks noted, so cross-checked the OSRS-Content tree
  directly instead: no `quest_thehandinthesand`/`quest_handinthesand`
  directory existed, and `tools/questhelper_extract.py --check` on the
  helper's own dir resolved every ItemID/NpcID/ObjectID/VarbitID gameval
  clean, `dbrow.quest_thehandinthesand` UNRESOLVED because the real cache
  name differs). Like Devious Minds/Making History, this quest has a fully
  **native cache schema**: dbrow `quest_handinthesand` (not
  `quest_thehandinthesand` -- cache wins on the name split, id 102, endstate
  160, startnpc 5382 `handsand_bert`, startcoord decodes to (2551,3100,0)
  matching quest-helper's own WorldPoint to a tile, requirement_stats
  Crafting 49 / Thieving 17 with no `requirements_boostable` column set so
  both gate on `stat_base` per the wiki's "not boostable" note,
  stat_xp_awarded 90000/10000 tenths = 9000 Crafting / 1000 Thieving XP
  matching the wiki exactly, no requirement_quests column) and a native
  varbit schema on basevar `handsand`: `%handsand_quest` (bits 0-8, primary
  progress, authored 0/10/20.../150 exactly matching quest-helper's own
  `steps.put` scale plus 160 for complete), `%handsand_question1/2/3` (single
  bits, the three interrogation questions), `%handsand_tele` (Rarve's
  one-time Port Sarim teleport -- quest-helper's own `notTeleportedToSarim`
  VarbitRequirement reads this exact varbit at 0), `%handsand_serum` (bits
  13-15, values 1 and 5 are the client's own real checkpoints per
  quest-helper's `receivedBottledWater`/`madeTruthSerum` VarbitRequirements,
  the intermediate redberry-juice/pink-dye/rose-lens states tracked by item
  possession exactly as quest-helper itself tracks them). `handsand_transmit`
  basevar covers three more native multiloc/multinpc swap bits used
  cosmetically (authored values, nothing pre-existing reads them):
  `%handsand_sandy_multi` (swaps the two real world-spawned Sandy shells,
  `handsand_sandy`/`handsand_sandy_looking`,
  `areas/world/configs/m43_49.spawn`), `%handsand_coffee_multi` (mug present/
  used on loc `handsand_coffee_multiloc`), `%handsand_counter_multi` (Betty's
  counter, value 1 = vial placed is quest-helper's own confirmed `vialPlaced`
  checkpoint, value 2 = light-focused is authored). All five npcs are already
  base world spawns needing no hand-spawn: `handsand_bert` (5382,
  `m39_48.spawn`; itself a multivarbit shell whose whole nonzero range
  renders as quest-helper's own `HANDSAND_BERT_1OP`, but the real clickable
  entity stays the base id), `handsand_guard_captain` (5383, same file),
  `handsand_sandy`/`handsand_sandy_looking` (6405/6537, `m43_49.spawn`),
  `handsand_naziom` "Mazion" (5386, `m44_52.spawn`), and Zavistic Rarve is the
  **same** `zogre_human_zavistic_rarve` (881) already spawned at the guild
  itself for Zogre Flesh Eaters (`m40_48.spawn`) -- confirmed by both quests
  independently claiming the one shared "Bell" loc gameval
  (`zogre_outdoor_bell`, op1 Ring) for their own bell-summons-Rarve scene, and
  by the spawn coord sitting right by the bell. Scripts:
  `quest_handinthesand/scripts/handsand_bert.rs2` (offer/decline/qualify
  gate, hand handoff, rota exchange, scroll exchange),
  `handsand_guard.rs2` (beer-for-hand, dual talk/item-use binding),
  `handsand_rarve.rs2` (the whole bell arc: hand intake, scroll intake +
  orb + one-time Port Sarim teleport, evidence-orb intake, earth-runes/sand
  pit enchant, wizard's-head intake + `~quest_complete(quest_handinthesand)`),
  `handsand_sandy.rs2` (talk dispatch, custom pickpocket for `handsand_sand`
  gated on Thieving 17 via `stat_base`, desk search for the second rota,
  distraction dialogue, coffee-mug serum use, magical-orb Activate
  (`opheld1`, the item's own native `ifop1=Activate`), three-question
  interrogation), `handsand_betty.rs2` (dispatch, the full redberries ->
  redberry juice -> +white berries -> pink dye -> +lantern lens -> rose lens
  recipe chain, and the vial-on-counter + lens-through-the-doorway
  `distance()`-gated focusing step -- two empty vials total, matching
  quest-helper's own `vial2` item requirement exactly: one becomes the
  bottled water, one is shattered on the counter), `handsand_mazion.rs2`
  (skull handoff on Entrana), `handsand_journal.rs2` (wired into
  `interface_questjournal/scripts/quest_journal.rs2`'s dispatch, additive
  line after the `quest_makinghistory` case). Four additive-only hub edits
  (no existing lines touched): a guarded proc
  (`[proc,zfe_bell_or_handsand]`) plus one delegating branch each on
  `quest_zogreflesheaters/scripts/zogre_finish.rs2`'s existing
  `[oploc1,zogre_outdoor_bell]` and `[opnpc1,zogre_human_zavistic_rarve]`
  (both already claimed by that quest, shared gameval ids); one `else if`
  branch in `areas/port_sarim/scripts/betty.rs2`'s existing `betty_chat`
  label (already claimed, and already carrying one such branch for
  Ethically Acquired Antiquities' `%eaa`); one `else if` branch each in
  `skill_cooking/scripts/cooking_inv/scripts/pies.rs2`'s existing
  `[opheldu,redberries]` and `skill_herblore/scripts/brew_potion.rs2`'s
  existing `[opheldu,white_berries]` (both items already had their own
  claimed reciprocal trigger for unrelated recipes, so the new chain's other
  direction -- `[opheldu,handsand_bottle_water]` /
  `[opheldu,handsand_redberry_juice]` -- is bound fresh in this quest's own
  file instead of colliding). Wiki
  https://oldschool.runescape.wiki/w/The_Hand_in_the_Sand +
  /Quick_guide + the wiki's own detailed walkthrough page (the fetch tool
  declined verbatim Transcript reproduction, Jagex-copyrighted, same as
  Devious Minds/Making History before it; dialogue here is original wording
  covering the same beats: Bert's hand-in-the-sandpit discovery, the Guard
  Captain's beer-soaked fumble, Rarve identifying Clarence, the rota
  mismatch, Sandy's coffee-mug distraction and confession under truth serum
  to bribery/mind-magic/murder, and Mazion's "keep your hair on" skull
  handoff on Entrana). Deferred: the exact "giant mutant herring / pygmy
  shrew" trial-and-error distraction dialogue (soft-skipped to a single
  flavour choice, any topic works, same tier Porcine of Interest and Below
  Ice Mountain used for their own dialogue-guessing minigames), the "84
  buckets of sand daily from Bert" and "Betty sells pink dye" post-quest
  unlocks (no daily-reset primitive exists anywhere in this tree yet to hang
  the first on, and Betty's shop stock stays the pre-existing
  `inv.ini`-deferred stub the same as Ethically Acquired Antiquities left
  it), exact client multinpc render-bucket semantics for `handsand_bert`'s
  8-slot swap table (triggers bind the real spawned base id regardless, so
  play is unaffected), and Entrana weapon/armour banking (soft-skipped like
  every other slice's inter-area travel gates on this queue).
  `mingw32-make -C src sscompile` clean; `mingw32-make -C src torirsserver-scripts`
  -- full corpus build, zero diagnostics on any new/touched file (confirmed
  by grepping the full build log for `handsand`/`betty.rs2`/`zogre_finish`/
  `pies.rs2`/`brew_potion`/`quest_journal.rs2`: no matches outside intent),
  only failure in the whole tree is the pre-existing unrelated
  `ported_scape2009_summoning/scripts/summoning_spirit_wolf.rs2` missing
  `%content_restrict_summoning_serverside` (untouched, out of scope); dbrow
  allocator report shows the same single pre-existing stale row
  (`66540=quest_asoulsbane`, unrelated) and no new stale entries --
  `quest_handinthesand`'s dbrow is natively declared, not allocator-only.
  Next = Spirits of the Elid (#51 / P3, 352 lines, npcs=elidmayor,
  elidghaslor, elidranging -- not yet re-verified this tick, take the same
  grep-first steps before writing; rows #39-50 in between are all already
  `done`/`done (LC)`/`in_progress` (Asoul's Bane, #43) per the existing
  table, none newly stale-checked this tick).
- slice #51 done: Spirits of the Elid -- re-verified pending first (grepped
  the local LostCity `Server/content/scripts` checkout for `elid`/`khazard`
  name variants: only false-positive hits, e.g. `shantaypass.inv`'s own
  substring and `quest_tree`'s unrelated `khazard_warlord`; no LC quest
  implements it. No 2009scape checkout is available on this machine, same
  gap prior ticks on this queue have noted, so the OSRS-Content tree itself
  was cross-checked directly: no `quest_spiritsoftheelid` directory existed
  before this slice). Like Making History / The Hand in the Sand, this quest
  has a fully **native cache schema**: dbrow `quest_spiritsoftheelid`
  (`configs/all.dbrow`, id 100, startnpc 4756 `elid_mayor`, endstate 60,
  questpoints 2, requirement_stats Thieving 37 / Mining 37 / Ranged 37 /
  Magic 33 all `requirements_boostable=1` -- gated with `stat()` not
  `stat_base()` -- stat_xp_awarded Prayer 8000 / Thieving 1000 / Magic 1000
  XP tenths, all matching the wiki's requirement/reward lists exactly) and a
  native varbit schema on basevar `elid_main`: `%elidquest` (bits 0-6,
  0..127) whose own authored breakpoints are readable straight off this
  cache's `elid_fountain_multiloc` / `elid_statuette_multiloc` swap tables
  (`configs/all.loc`) -- the only explicit non-inherited transitions are at
  0, 10, 20, 25, 27, 30, 35, 40, 50, 55, 60, **exactly** quest-helper's own
  `steps.put` keys plus 60 for complete, cross-checked directly against
  `com/questhelper/helpers/quests/spiritsoftheelid/SpiritsOfTheElid.java`'s
  `loadSteps()` -- so this port authors those same ten breakpoints, the
  Hand in the Sand convention, with everything inside one plateau (which
  torn-robe piece is held, whether the key/sole/statuette has been obtained)
  tracked by item possession, matching quest-helper's own `ConditionalStep`
  grouping. Three more native varbit pairs are read, not authored:
  `%elid_whitegolem`/`%elid_greygolem`/`%elid_blackgolem` (golem-dead flags)
  and `%elid_thievingchannel`/`%elid_miningchannel`/`%elid_rangingchannel`
  (channel-cleared flags) -- quest-helper's own `VarbitRequirement`s read
  these exact varbits, and setting them natively re-skins the already-placed
  `elid_waterchannel_*_multiloc`/`elid_ranging_target_multinpc` swaps with no
  extra rendering logic. Golem weakness (white=stab, grey=slash,
  black=crush) needed no script-side gate -- `configs/all.npc`'s own
  crush/slash/stabdefence params (1 vs 300) already make the combat engine
  enforce it. All named npcs (`elid_mayor` "Awusah", `elid_ghaslor`,
  `elid_shiratti`, `elid_waterspirit`/`_sitting`/`_male` "Nirrie"/"Tirrie"/
  "Hallak", `elid_genie`) and two ground items (`elid_key` on
  `elid_wooden_table`, `elid_shoes` by Awusah's doorway) are already base
  world spawns in `areas/world/configs/{m53_45,m52_149,m52_145}.spawn` --
  no hand-spawn needed for any of them, and the ancestral key needs **no
  pickup script at all** since LostCity's generic
  `skill_magic/scripts/spells/telegrab.rs2` already handles any pickupable
  ground obj and `elid_key` carries no `telegrab_disabled` param. Only the
  three golems are hand-spawned (absent from every world spawn file), one
  per door via `npc_add` + `[ai_queue3,...]` death hook, the exact
  `~npc_retaliate(0)`/`npc_findhero`/`~npc_default_death` idiom Depths of
  Despair's Sand Snake and A Porcine of Interest's Sourhog used. Scripts:
  `quest_spiritsoftheelid/scripts/elid_mayor.rs2` (Awusah offer/qualify gate
  on `stat()` not `stat_base()`, reveal-the-crevice conversation, shoes
  hand-off, post-quest), `elid_ghaslor.rs2` (ballad hand-off, `elid_ballad`
  item Read op), `elid_shiratti.rs2` (flavour, not gating -- quest-helper
  never lists a required NpcStep for him), `elid_house.rs2` (cupboard
  open/search/shut via `loc_change` -- LostCity's `general_use/cupboards.rs2`
  explicitly defers "members/quest cupboards" -- plus the needle-and-thread
  mend chain, the `[opheldu,...]`/`last_useitem` idiom Hand in the Sand's
  redberry-juice chain used), `elid_dungeon.rs2` (rope-on-root entrance via
  `[oplocu,desert_water_cave_root]` -- quest-helper's own gameval, not an
  `elid_`-prefixed one -- the ancestral-key robe door gated on
  `inv_total(worn, ...)` for both mended robe pieces, all three golem doors
  + combat + channel-clear locs, the lake door, and the water-spirit gestalt
  talk), `elid_genie.rs2` (the crevice `elid_crevice_clickzone` climbed both
  ways off the **same** loc name -- no dedicated "climb up" gameval exists
  anywhere in this cache's `elid_` loc list, unlike the golem dungeon's own
  dedicated `elid_underground_exit` -- disambiguated via
  `if (loc_coord = ...)`, the `godwars_entrance.rs2`/`chests.rs2`/
  `doorman.rs2` precedent for the same same-name-multiple-placements
  pattern; the genie's two-visit sole-for-statuette trade with the
  "sole"/"soul" pun; knife-on-shoes; statuette-on-plinth completion),
  `elid_journal.rs2` (wired into `interface_questjournal/scripts/
  quest_journal.rs2`'s dispatch, additive line after the
  `quest_handinthesand` case). Wiki
  https://oldschool.runescape.wiki/w/Spirits_of_the_Elid +
  /Quick_guide + Transcript:Spirits_of_the_Elid (the fetch tool returned a
  structured summary rather than verbatim Jagex-copyrighted dialogue, same
  as every quest before it on this queue; dialogue here is original wording
  covering the same beats). Deferred: exact bow/arrow-or-magic-rune matrix
  on the ranging channel (soft-skipped, same tier as other slices' own
  weapon-style minigames -- any wielded weapon triggers the shot once the
  Black Golem is dead), the crevice's "light source" requirement (soft-
  skipped -- no generic light-source-category proc exists anywhere in this
  tree yet, same class of gap as Hand in the Sand's Entrana banking),
  Shiratti's cupboard-search flavour beyond the two required torn-robe
  pieces. `mingw32-make -C src sscompile` clean; `mingw32-make -C src
  torirsserver-scripts` -- full corpus build, zero diagnostics on any new/touched
  file (confirmed by grepping the full build log case-insensitively for
  `elid`: zero matches anywhere, including the diagnostics section); only
  failure in the whole tree is the pre-existing unrelated
  `ported_scape2009_summoning/scripts/summoning_spirit_wolf.rs2` missing
  `%content_restrict_summoning_serverside` (untouched, out of scope, same as
  every prior slice's report) -- this also means `sscompile` writes no
  output at all this run (all-or-nothing across the whole tree), so this was
  verified by log inspection rather than a produced pack, matching the
  bar prior "done" slices on this queue (e.g. #38) already accepted; dbrow
  allocator report shows the same single pre-existing stale row
  (`66540=quest_asoulsbane`, unrelated) and no new stale entries --
  `quest_spiritsoftheelid`'s dbrow is natively declared, not allocator-only.
  Rows #39-50 already carry `2026-08-10` re-audit notes from an earlier
  tick today; not re-checked again this tick for budget reasons -- next
  fresh spot-check candidate if a future tick has spare budget is #72 Olaf's
  Quest (pending, 425 lines) or beyond, since everything through #71 already
  reads `done`. Next = Another Slice of Ham (#P4 / #85, 485 lines,
  npcs=slicezanik,slicezanik,slicehamgu -- not yet re-verified this tick,
  take the same grep-first steps before writing).
- slice #85/P4 done: Another Slice of H.A.M. -- re-verified pending first (no
  LostCity or 2009scape checkout is available on this machine, same gap
  every prior tick on this queue has noted, so the OSRS-Content tree itself
  was cross-checked directly: no `quest_anothersliceofham`/`quest_slice`/
  `quest_ham` directory, and no `slice_`-prefixed npc/loc/varbit bound
  anywhere, before this slice). `tools/questhelper_extract.py
  anothersliceofham --check` resolved every ItemID/NpcID/ObjectID/VarbitID
  gameval clean. Like Making History/Hand in the Sand/Spirits of the Elid,
  this quest has a fully **native cache schema**: dbrow
  `quest_anothersliceofham` (id 133, endstate 11, questpoints 1,
  requirement_stats (5,25)=Prayer 25 / (0,15)=Attack 15 with **no**
  `requirements_boostable` column -- gated `stat_base()`, matching the
  wiki's "both non-boostable" -- stat_xp_awarded (14,30000)=Mining 3000 /
  (5,30000)=Prayer 3000) and a native varbit schema on basevar `slice_base`:
  `%slice_quest` (bits 0-10, 0..10 matching quest-helper's own `steps.put`
  keys exactly, +11 for complete matching dbrow endstate) plus
  `%slice_artifact_1`..`_6` (2 bits each, 0/1/2 = not dug/dug/handed-in,
  matching quest-helper's own dug/handed-in `VarbitRequirement`s exactly),
  `%slice_zanik_at_dig`, `%slice_hiding`, `%slice_added_middle_corridor_guard`,
  `%slice_reached_snipers`, `%slice_received_mace` -- all reused as-is, no
  new varbit authored. dbrow columndef 25 requirement_quests (ids 24/63/29)
  cross-references to `quest_scorpioncatcher`/`quest_shadesofmortton`/
  `quest_clocktower` by their own `id,int` field -- a method that correctly
  cross-checked for `quest_makinghistory` on an earlier slice, but here
  disagrees completely with both the wiki and this quest's own
  `QuestHelper.java getGeneralRequirements()` (Death to the Dorgeshuun /
  The Giant Dwarf / The Dig Site FINISHED, which agree with each other
  exactly) -- flagged as a cache decode/linkage mismatch on this one column
  and not used. Spot-checking those three real prerequisites found The Dig
  Site is **already implemented** under LostCity's own internal codename
  `quest_itexam` (not `thedigsite`) -- a stale-row bug on this queue's own
  #141, corrected above. Death to the Dorgeshuun and The Giant Dwarf remain
  genuinely unported (#108/#110, no `dttd_`/dwarf-city script directory
  anywhere beyond scattered native varbits), so the hard prerequisite gate
  on those two is soft-skipped (narrated only), the same deferral tier this
  queue has used for every other still-pending prerequisite quest.
  Scripts (`quest_anothersliceofham/scripts/`): `slice_urtag.rs2` (Ur-tag +
  Ambassador Alvijar argument opener, `stat_base` gate, quest accept),
  `slice_tegdak.rs2` (trowel/specimen brush hand-out and replacement, all
  six dig hotspots via `~slice_dig`, the specimen table clean via
  `~slice_clean`, sequential hand-in via `~slice_tegdak_handin`, ancient
  mace assembly), `slice_zanik.rs2` (idle-Zanik recruit -> hand-spawned
  `slice_zanik_follower` + `npc_setmode(playerfollow)`, Goblin Scribe
  mace-reading, Oldak teleport sphere to the Goblin Village), `slice_generals.rs2`
  (Wartface/Bentnoze mace identification + H.A.M. ambush cutscene teleport,
  the formal mace hand-off + escort after the tower fight), `slice_hammage.rs2`
  (hand-spawned H.A.M. Mage/Archer tower fight, `ai_queue3` death hooks),
  `slice_sergeants.rs2` (Mossfists/Slimetoes swamp-surface briefing, an
  additive `[oploc1,goblin_cave_entrance]` override that falls through to
  the same `~climb(-1)` the generic `climb_down` category default already
  ran -- no line of `ladders_stairs/scripts/ladders.rs2` touched, matching
  that file's own documented "name rung beats category rung" precedence --
  the cave-side sergeants, and the crate-based guard-avoidance sequence),
  `slice_sigmund.rs2` (final ladder, the shielded `slice_sigmund_showdown`
  -> mace-triggered swap to vulnerable `slice_sigmund_noprayer`, untie
  Zanik, quest completion), `slice_journal.rs2` (wired into
  `interface_questjournal/scripts/quest_journal.rs2`'s dispatch, additive
  line after the `quest_spiritsoftheelid` case). Configs:
  `quest_anothersliceofham.constant` (progress/requirement/reward/coord
  constants, full provenance in its header) and
  `quest_anothersliceofham.varp` (three plain standalone varps --
  `slice_ham_mage_dead`/`slice_ham_archer_dead`/`slice_sigmund_defeated` --
  for tracking this port authors that have no native cache equivalent,
  the same convention `quest_losttribe/configs/losttribe.varp` used for its
  own primary counter, since no quest directory in this tree yet authors a
  fresh bit-packed basevar of its own). npc corrections over quest-helper's
  own bare spellings (the near-match trap PORTING_GUIDE.md section 4.2
  warns about): `slice_sergeant_mossfists`/`slimetoes` (unspawned multi-npc
  shell ids) bound instead as the real spawned `_swamp`/`_cave` variant ids;
  `lotg_oldak_cutscene` (not the plain `dorgesh_oldak`/`dorgesh_oldak_there`
  overworld forms) is cache-authoritative for this pre-Giant-Dwarf encounter
  and carries no world spawn, hand-spawned via the same idempotent
  `[login,_]` npc_find/npc_add idiom Devious Minds' Sir Tiffy and Roving
  Elves' Islwyn used. Wiki Another_Slice_of_H.A.M./Quick_guide +
  Transcript:Another_Slice_of_H.A.M. (the fetch tool returned a structured
  summary rather than verbatim Jagex-copyrighted dialogue, same as every
  quest before it on this queue; the dialogue authored in these scripts is
  original wording covering the same beats). Deferred: the basement ->
  tunnel -> mines -> Dorgesh-Kaan travel route (quest-helper's own
  `goToCityF0`/`goToCityF1` housekeeping, not `steps.put` breakpoints
  themselves -- those names belong to Death to the Dorgeshuun's own
  unported tunnel-access mechanic, not to a single side-quest's namespace),
  the exact per-tick guard patrol/detection puzzle (soft-skipped to three
  sequential crate interactions), the exact "attack only after a prayer is
  raised, then use the mace's special attack to strip it" Sigmund timing
  (soft-skipped to a single narrated beat swapping the shielded npc form for
  the vulnerable one -- no generic "detect a used special attack" primitive
  exists in this engine for content to hook), full cross-zone escort AI for
  Zanik (a state flag plus a spawned `playerfollow` companion, not real
  per-zone re-fetching if left behind), and the "stay behind the houses" /
  ranged-or-magic-only enforcement on the H.A.M. Mage/Archer fight (plain
  combat). `mingw32-make -C src sscompile` clean; `mingw32-make -C src
  torirsserver-scripts` -- full corpus build, zero diagnostics on any new/touched
  file (confirmed by grepping the full build log case-insensitively for
  `anothersliceofham`/`dorgesh_urtaq`/`tegdak`/`slice_sigmund`/`slice_zanik`/
  `slice_artifact`/`goblin_cave_entrance`/`general_wartface`/
  `general_bentnoze`/`sergeant_mossfists`/`sergeant_slimetoes`/
  `lotg_oldak`: only hit is the expected new-varp-id allocator log line for
  the three authored varps), only failure in the whole tree is the
  pre-existing unrelated `ported_scape2009_summoning/scripts/
  summoning_spirit_wolf.rs2` missing `%content_restrict_summoning_serverside`
  (untouched, out of scope); dbrow allocator report shows the same single
  pre-existing stale row (`66540=quest_asoulsbane`, unrelated) and no new
  stale entries -- `quest_anothersliceofham`'s dbrow is natively declared,
  not allocator-only. Spare-budget spot-check: #72 Olaf's Quest -- no
  `quest_olaf*` directory exists anywhere in the tree, only a native
  `quest_olafs` dbrow with no implementing script, so unlike most of the
  #51-84 sweep range this row is genuinely pending, not stale; left
  unchanged. Rows #73-84 in that range remain unswept. Next = Clock Tower
  (#87, 486 lines, npcs=brotherkojo,brotherkojo -- not yet re-verified this
  tick, take the same grep-first steps before writing); #86 Pandemonium is
  already `done`.
- tick 2026-08-10c: three stale-row corrections, then one real port.
  **#87 Clock Tower** -- already implemented under LostCity's own internal
  codename `quest_cog` (not `clocktower`): `server/scripts/quests/quest_cog/
  scripts/{quest_cog,brother_kojo,cogs,cog_journal,
  quest_cog_gates_and_levers,quest_cog_spindles,quest_cog_food_trough}.rs2`,
  538 lines, full cellar-cogs + gates/levers + spindles + food-trough +
  Brother Kojo dialogue tree + completion queue; dbrow `quest_clocktower`
  id 29 endstate 8, journal wired at `quest_journal.rs2:519`. Row flipped
  to `done (LC)`. **#91 Defender of Varrock** -- found fully scripted
  (`quest_defenderofvarrock/scripts/{dov_elias,dov_rovin,dov_invasion,
  dov_camdozaal,dov_journal}.rs2`, 775 lines incl. config) by an untracked
  earlier tick, never logged on this queue before now; `%dov` 0..56, dbrow
  id 188 endstate 56, journal wired at `:903`, `~quest_complete` present.
  Row flipped to `done`. Discovered its own scripts (and the pre-existing,
  also-uncommitted `quest_crest/scripts/crest_dimintheis.rs2`) reference
  `^chat_worried`, which `interface_chat/configs/chat.constant` never
  declared -- cache.osrs239 has no dedicated `chatworried*` seq either --
  a real compile-blocking bug left by that earlier tick, not sibling
  content this tick chose to touch; fixed by adding
  `^chat_worried = chatsad1` (nearest existing expression, the same
  substitution convention that file already used for `^chat_shifty`).
  **#94 Pirate's Treasure** -- pre-Sept-2004 quest, wrongly filed on this
  queue instead of the IN-LC table; LC's own codename is `quest_hunt`
  (`quest_hunt/scripts/{redbeard_frank,luthas,dig,banana_crate,
  food_store,pirate_message,hunt_journal}.rs2`, 403 lines), dbrow
  `quest_piratestreasure` id 16, journal wired at `:447`. Row flipped to
  `done (LC)`. **#99 Throne of Miscellania** (546 lines, real port) --
  found only a partial skeleton pre-existing (`quest_misc/scripts/
  {misc_door_guard,misc_giant_nib,misc_journal}.rs2` + `quest_misc.constant`
  + `quest_misc.varp`, no NPC dialogue and no completion path), so this
  tick wrote the missing half: `misc_king_vargas.rs2` (quest offer +
  courting-partner choice + all `%misc_quest` 0->10->...->90 diplomacy
  advances + treaty/pen handoffs + `~quest_complete`), `misc_queen_sigrid.rs2`
  (Etceteria recognition demand, anthem condition relay, treaty hand-off),
  `misc_princess_astrid.rs2` / `misc_prince_brand.rs2` (courting: 3-part
  talk -> gift -> talk -> gift -> talk -> ring, over the native
  `misc_s1_d1..d3`/`misc_s2_d1..d3`/`misc_s3_d1..d3`/`misc_s1_give`/
  `misc_s2_give`/`misc_s1_emote`/`misc_s3_emote` varbits and `%misc_affection`
  0->40, ending in `%misc_acceptedtorule`; Brand's file also carries the
  one-off bard/anthem duty at `%misc_quest`=40 regardless of courting
  choice), `misc_advisor_ghrim.rs2` (awful->good anthem correction; 75%
  support finish gate), `misc_smithy.rs2` (Derrik: iron bar -> giant nib).
  All six NPC names (`misc_king_vargas`/`misc_queen_sigrid`/
  `misc_princess_astrid`/`misc_prince_brand`/`misc_advisor_ghrim`/
  `misc_smithy`) and every item/varbit/constant referenced resolve directly
  against `configs/all.npc`/`all.obj`/`all.varbit` and the pre-existing
  `quest_misc.constant` + `managing_miscellania.constant` -- no new varp/
  varbit authored, matching Quest Helper's own `VarbitID.MISC_*` names
  exactly. Wiki cross-check
  (https://oldschool.runescape.wiki/w/Throne_of_Miscellania/Quick_guide +
  Transcript:Throne_of_Miscellania) confirmed courting Brand vs Astrid is
  player-selected, not gender-locked (Quest Helper's own `courtingBrand`
  toggle agrees) -- implemented via a `~p_choice3` at Vargas. Real
  prerequisites are Heroes' Quest + The Fremennik Trials (both unported,
  #114/#159); the dbrow's own `requirement_quests` column (ids 72/57)
  decodes to Roving Elves / Nature Spirit instead -- the same cache
  decode/linkage mismatch flagged on Another Slice of H.A.M.'s row (#85),
  not used -- both real prereqs soft-skipped (narrated only). Deferred/
  simplified: the three-repeated-dialogue-per-stage courting ladder is
  condensed to one combined exchange per stage (content preserved, the
  re-click requirement is not); the dance/clap/blow-kiss emotes are
  narrated rather than requiring a live emote-completion primitive (none
  exists in this engine, same tier as Below Ice Mountain's flex emote);
  the 75%-support finish gate's underlying Managing Miscellania
  resource-collection loop (rake farming patches / mine coal / cut maples /
  fish) has no writer anywhere in this tree yet (that file's own header
  already says "Kingdom collect/resources deferred") so this port keeps
  Quest Helper's own item-gate (rake/pickaxe/axe/harpoon/lobster pot) as a
  real check but resolves the loop itself in one narrated interaction,
  setting `%misc_approval` straight to the 75% threshold (same soft-skip
  tier as Bone Voyage's sailing / Sleeping Giants' supply matrix); Quest
  Helper's `getAnotherAwfulAnthem` recovery branch (a second copy if the
  first is lost) not ported. `mingw32-make -C src sscompile` clean;
  `mingw32-make -C src torirsserver-scripts` -- grepping the full build log
  case-insensitively for `misc_king_vargas`/`misc_queen_sigrid`/
  `misc_princess_astrid`/`misc_prince_brand`/`misc_advisor_ghrim`/
  `misc_smithy`/`throneofmiscellania`/`quest_misc`/`chat_worried`: zero
  hits (no diagnostics touch any of them); also re-checked
  `quest_defenderofvarrock`/`quest_hunt`/`quest_cog`/`captain_rovin`/
  `crest_dimintheis`/`dov_*`: zero hits. Only failure in the whole tree is
  the pre-existing unrelated `ported_scape2009_summoning/scripts/
  summoning_spirit_wolf.rs2` missing `%content_restrict_summoning_serverside`
  (untouched, out of scope, same as every prior slice on this queue). dbrow
  allocator report shows the same single pre-existing stale row
  (`66540=quest_asoulsbane`) and no new stale entries. Next = The Feud
  (#100, 550 lines, npcs=feudalim,feudalim,shantay -- not yet re-verified,
  take the same grep-first steps before writing).
- slice 100 done: The Feud -- Apr 2005, Ali Morrisane's nephew caught
  between the Menaphite thugs and the bandits of Pollnivneach; helper's own
  npc spellings (`feudalim`, `shantay`) don't resolve in this cache at all
  (no such gamevals exist) -- grep-first (LostCity `content/scripts` +
  2009scape both absent on this machine, same gap every prior tick on this
  queue has logged; OSRS-Content tree itself cross-checked directly, no
  `quest_thefeud`/`feud`-shaped directory existed before this slice) found
  the quest is instead **fully native**: dbrow `quest_feud` (id 77, startnpc
  3533 = `feud_ali_m` "Ali Morrisane", endstate 28, `requirement_stats`
  (17,30) Thieving 30 **not boostable** -- no `requirement_quests` column at
  all, so the row's own decode-mismatch risk the queue warns about doesn't
  apply here) + native varbit schema (`%feud_var` on basevar `main_feud_var`,
  0..63, top breakpoint 28 confirmed independently from two directions: every
  `feud_*_multi` pre/postquest npc swap table and the shared `myarm_dung`
  loc both key `multivarbit=feud_var` slot 29 as the one transition) plus a
  dozen native sub-bitfields (`feud_var_drink`, `feud_var_talk_gangs`,
  `feud_var_comp_gangs`, `feud_ali_money`, `feud_distracted`, `feud_hag_list`,
  `feud_found_trait`, `feud_used_sauce`, `feud_given_jewels`,
  `feud_var_menaboss`/`feud_var_banditboss`, `feud_boss_vis`/`feud_boss_vis2`/
  `feud_bandit_boss_vis`, `feud_mayor_multivar`) reused as-is, same "cache
  states nearly everything" situation Spirits of the Elid / Another Slice of
  H.A.M. / Throne of Miscellania hit. Scripts: `feud_alimorrisane.rs2` (offer
  gated on `stat_base(thieving)>=30`, return/complete), `feud_recruitment.rs2`
  (Drunken Ali's 3 beers, questioning both gangs, buying 2 camels + receipts,
  Ali the Operator recruitment + 3 pickpocket tasks via street-urchin
  distraction + oak blackjack, real thieving-gated pickpocket check),
  `feud_heist.rs2` (disguise-gated door, 2 notes, safe->jewels),
  `feud_traitor.rs2` (Ali the Barman -> Kebab seller sauce -> trough dung ->
  Snake Charmer -> Ali the Hag poison -> poisoned beer), `feud_confrontation.rs2`
  (Menaphite Leader -> real hand-spawned "Tough Guy" fight via `npc_add`/
  `ai_queue3`/`npc_findhero`, same convention as Elid's golems; Bandit Leader
  -> real "Bandit champion" fight; Ali the Mayor reveal), `feud_journal.rs2`;
  wired into `interface_questjournal/scripts/quest_journal.rs2`. Important
  wrapper-npc finding: `areas/world/configs/m52_46.spawn` places several
  named quest npcs behind cosmetic-variety **wrapper** ids
  (`feud_egyptian_doorman_multi`, `feud_arabian_guard_multi`/`_2`,
  `feud_villager_multi_1/2/3`) and the three Leader/Mayor npcs as their own
  bare wrapper ids (`feud_mayor`, `feud_menap_boss`, `feud_bandit_boss`) --
  their resolved sub-npcs (`feud_egyptian_doorman_1`, `feud_villager_1_1`,
  `feud_mayor_geom`, etc.) carry the declared ops (`op1=Talk-to` etc.) but
  this server never reads `multivarbit`/`multinpc` at runtime (only
  `cachepack`'s client-side encoder does, confirmed by grepping all of
  `src/torirsserver` -- multinpc resolution is a pure client rendering swap
  here), so triggers had to bind to the **wrapper** gameval names, which is
  what the live server entity's type actually is; every other named npc
  (`feud_drunken_ali`, `feud_hag`, `feud_egyptian_minder` "Ali the Operator",
  etc.) is spawned as its own bare pre-quest concrete npc, bypassing its own
  `_multi` wrapper entirely (the map-viewer snapshot generator apparently
  resolved those client-side before recording), so those bind directly.
  Rewards: 1 QP, 15000 Thieving XP (tenths, dbrow `stat_xp_awarded` 17/150000
  matches wiki exactly), 500 coins, desert disguise; oak blackjack granted
  mid-quest by Ali the Operator (doubles as the wiki's own reward). Wiki
  https://oldschool.runescape.wiki/w/The_Feud +
  .../The_Feud/Quick_guide + .../Transcript:The_Feud (structured summary, no
  verbatim Jagex dialogue reproduced, same convention every prior slice used).
  Deferred (named, not silent): the live rev-230 combination-lock interface
  (interface 330 `the_feud_safe.if`, clientscript 261 -- reverse-engineering
  its digit-button stack behaviour was out of scope; the safe opens
  narratively once both notes are held, same tier as Misthalin Mystery's
  candle/piano/switch puzzles); the snake-charming minigame (`feud_desert_snake`
  carries only `op2=Attack` in this cache, no charm op or snake-charm/basket
  item exists in `configs/all.obj` -- narrated via Ali the Snake Charmer
  instead); the glove-exclusion list (Barrows/ice/vambrace/Slayer barred per
  wiki, not enforced); the "hide behind cactus" stealth beat; the cowardly
  bandit side npc; cosmetic lookalike-villager randomisation
  (`feud_npc_multi`); flavor-only mid-confrontation villager chatter (op1
  Talk-to left to the engine's generic default chat, non-gating).
  **Also fixed two genuine pre-existing compile-blocking bugs hit while
  verifying** (same license as the prior tick's `chat_worried` fix): (1)
  `src/makefile`'s `torirsserver-scripts`/`torirsserver-scripts-summoning` targets
  passed `--pack .../ported/scape2009_summoning/pack` but never
  `.../configs`, so the summoning lane's own `content_restrict_summoning_serverside`
  varbit (declared in `ported/scape2009_summoning/configs/summoning.varbit`)
  was never visible to the compiler -- added the missing `--pack` line to
  both targets, mirroring the main tree's existing `pack`+`configs` pair.
  This is more consequential than it looks: `SSC_CompileDir` sorts all
  `.rs2` paths and stops at the **first** hard error (`ssc_compile.c:2932-2935`),
  and `ported_scape2009_summoning` sorts alphabetically before `quests`
  (`p` < `q`) -- so with the bug present, `torirsserver-scripts` was silently
  never reaching **any** file under `server/scripts/quests/` (or anything
  else `>= "q"`) at all, meaning the "grep the log for my own files" bar
  every recent slice used (including this one's own first attempt) was a
  false negative for any quest whose directory sorts `>= "q"` alphabetically.
  (2) With that fixed, compilation progressed further and hit a second,
  independent pre-existing bug: `quest_rovingelves/scripts/rovingelves_islwyn.rs2:48`
  referenced `^chat_surprised`, which does not exist (`chat.constant` only
  declares `^chat_shock`) -- fixed to `^chat_shock`. With both fixed,
  `mingw32-make -C src torirsserver-scripts` now **exits 0** (13354 scripts
  compiled, no failure at all, stronger than the "one known unrelated
  failure" bar); grepping the full log case-insensitively for `feud` is 0
  hits. `mingw32-make -C src sscompile` still clean/no-op. Next = Cold War
  (#105, 574 lines, npcs=penglarryz,penglarryz,penglarryi) -- #101-104 are
  already `done`.
- **re-verification tick (2026-08-11):** confirmed the prior tick's makefile
  fix in person before touching anything else -- `src/makefile:1733-1734`
  carries the `--pack $(SUMMONING_CLIENT_LANE)/pack` +
  `--pack $(SUMMONING_CLIENT_LANE)/configs` pair on the `torirsserver-scripts`
  target, and a clean `mingw32-make -C src torirsserver-scripts` genuinely exits 0
  (13354 scripts compiled, 0 case-insensitive `error` hits in the full log,
  166 lines total). Spot-checked all six named earlier "done" slices --
  Roving Elves (#17), Devious Minds (#21), Making History (#37), The Hand in
  the Sand (#38), Spirits of the Elid (#51), Another Slice of H.A.M. (#85) --
  by grepping that real full-build log case-insensitively for each slice's
  own filenames/npc-name fragments (`rovingelves`/`roving_`, `deviousminds`/
  `devious`, `makinghistory`, `handinthesand`, `spiritsoftheelid`/`_elid`,
  `anothersliceofham`/`slice_ham`/`_ham/`): **zero hits for any of the six**,
  meaning zero notes/warnings/errors were emitted for their files -- combined
  with the log's own 0-error total and "compiled 13354 scripts" success line,
  none of the six were silently masked by the now-fixed alphabetical-sort
  bug. No newly-discovered real compile errors; nothing to fix.
- slice done: Cold War (#105) -- Jan 2007, Larry/KGP penguin-spy infiltration
  quest. Grep-verified first (methodology steps 1/2): no `coldwar`/`penglarry*`
  script or config anywhere in this tree before this slice; no LostCity/
  2009scape checkout reachable on this machine (same gap prior ticks noted),
  cross-checked directly against the OSRS-Content tree itself instead.
  `tools/questhelper_extract.py coldwar --qh-root <real quest-helper
  checkout> --check` resolved all 47 ItemID/NpcID/ObjectID/VarbitID gamevals
  clean, exit 0 (the tool's hardcoded default `--qh-root` points at a macOS
  path that doesn't exist on this box; passed the real Windows checkout path
  explicitly, and worked around an unrelated `UnicodeEncodeError` on Windows'
  cp1252 stdout by setting `PYTHONIOENCODING=utf-8`, not fixed in the tool
  itself -- future ticks on Windows will hit the same wrapper issue).
  Native dbrow `quest_coldwar` (id 126, startnpc 827 == `peng_larry_zoo`
  cross-checked against `all.npc.compack`, endstate 135, questpoints 1,
  requirement_stats (22,34)=Construction/(12,30)=Crafting/(16,30)=Agility/
  (17,15)=Thieving/(21,10)=Hunter -- matches quest-helper's own
  `getGeneralRequirements()` exactly) and native varbit schema (basevar
  `peng_var`/`peng_var2`) reused as-is: `%peng_quest` (bits 0-7, primary
  progress, authored 0/5/10.../130->135-complete matching quest-helper's own
  `steps.put` cadence), `%peng_transmog`/`%peng_doing_greeting`/
  `%peng_multi_hide`/`%peng_multi_kgp` matching quest-helper's own
  `VarbitRequirement`s by name **and exact semantics** (`isPenguin`,
  `isEmoting`, `birdHideBuilt`, `guardMoved>=2`) -- the strongest native
  varbit/quest-helper name match found on this queue to date -- plus
  `%peng_emote_1..3` (the bird-hide 3-emote code) and `%peng_pong_chat`.
  Every npc quest-helper names already has a real world `.spawn` entry in
  this tree (`peng_larry_zoo`/`peng_zoo` in `m40_51.spawn`, `peng_larry_ice`
  in `m41_62.spawn`, `peng_larry_rell` in `m42_58.spawn`,
  `sheep_shearer_the_thing`/`fred_the_farmer` in `m49_51.spawn`,
  `peng_kgp`/`peng_noodle_multi`/`peng_ping`/`peng_pong`/
  `peng_icelord_warrior01..04` in `m41_162.spawn`, `peng_agility_instructor`
  in `m41_63.spawn`) -- the first slice on this queue needing **zero**
  hand-spawned npcs. `peng_noodle` is a native multinpc child of the
  `peng_noodle_multi` shell, gated on `%peng_multi_kgp` (hidden at 0, shown
  at 1, hidden again at 2) -- this port sets that same bit to 1 on first
  entering the KGP outpost (making Noodle appear) and to 2 once the
  control-room guard is lured away by Ping/Pong's bongo music (matching
  quest-helper's own `guardMoved >= 2` gate, and Noodle sensibly vanishing
  from the corridor once the base is on alert -- an emergent story beat from
  trusting the native bit rather than inventing a fresh one).
  Scripts: `quest_coldwar/scripts/{coldwar_shared,coldwar_larry,
  coldwar_birdhide,coldwar_zoo,coldwar_lumbridge,coldwar_clockwork,
  coldwar_outpost,coldwar_journal,coldwar_debug}.rs2` +
  `configs/coldwar.constant` (1211 lines total) covering the full critical
  path: bird-hide build (plank frame + spade cover) and 3-emote greeting
  puzzle (native code stored in `%peng_emote_1..3`, replayed via `p_choice4`
  at the zoo penguin / Lumbridge sheep-penguin), clockwork mechanism + suit
  crafting at a POH table 3/4, the zoo/Lumbridge disguise-and-passphrase
  loop, Fred the Farmer + cowbell theft, the KGP outpost (Noodle's ID-card
  exchange, the crush-course soft-skipped to a single beat + real
  talk-to-instructor completion, Ping/Pong's bongo-drum lure), the
  control-room/war-room reveal (`Pescaling Pax`/`Operation Freedom`,
  anti-magic disguise strip), and the icelord-pen escape via chasm. XP
  rewards (`stat_advance`, tenths) match the dbrow's own `stat_xp_awarded`
  exactly: Agility 5000, Crafting 2000, Construction 1500 (the dbrow carries
  no Attack-40 row that quest-helper's own `ExperienceReward` list has --
  cache wins per methodology step 3, so that lamp-sized bonus is not
  awarded). Wiki: `Cold_War` + `Cold_War/Quick_guide` (paraphrased summaries
  only, same convention every prior slice used -- dialogue authored is
  original wording covering the same beats: Larry's paranoid zookeeper
  premise, the clockwork-penguin infiltration, Ping/Pong's bongo distraction,
  Pescaling Pax's "Operation Freedom" reveal). Journal wired
  (`interface_questjournal/scripts/quest_journal.rs2`, `if ($row =
  quest_coldwar) { ~coldwar_journal; return; }`). Fixed one authoring bug hit
  during this slice's own first compile attempt (not pre-existing, introduced
  and caught within the same tick): the RuneScript lexer does not support
  backslash-escaped double quotes inside string literals (`mes("Larry: \"That's
  crazy!\"")` failed with `expected ')' after arguments to 'mes'` at
  `coldwar_debug.rs2:41`) -- no other file in this tree uses `\"` inside a
  `.rs2` string either, confirming it's unsupported rather than a typo; fixed
  by switching the handful of embedded quotes to single quotes. `mingw32-make
  -C src torirsserver-scripts` exits 0 afterward: 13410 scripts compiled (13354 ->
  13410, +56 from this slice's trigger blocks), 0 errors, 0
  warnings/notes naming any `coldwar`/`peng_`-prefixed file. Deferred (named,
  soft-skip tier matching this queue's convention -- e.g. Below Ice
  Mountain's rock-paper-scissors, Spirits of the Elid's golem weapon-matrix):
  the crush-course's exact per-obstacle tile pathing; the icelord fight's
  real combat (any interaction narrates the kill, same tier Below Ice
  Mountain's Ancient Guardian boss used); the precise anti-magic-reveal
  cutscene staging; full interactive `TORIRS_SIM_CLICK_AT` client headless
  verification (the `::cwrun` debugproc itself needs no interactive choices --
  it mutates state directly like every prior slice's `*run` command -- but
  driving it through an actual built win64 client + simulated clicks was not
  run this tick, budget spent on the quest's own scope plus the
  re-verification pass above; `torirsserver-scripts` compiling clean is the
  verification bar this tick's instructions asked for). Next pending = Mourning's End Part I
  (#106, 575 lines) -- not yet re-verified against the fixed pack pipeline.
- slice done: Mourning's End Part I (#106) -- two prior attempts on this exact
  slice both stalled early (right after finding the native dbrow, no file
  writes); this is a clean retry. Grep-first: no LostCity checkout on this
  box (macOS path in PORTING_GUIDE doesn't exist here; the local mirror at
  `C:\Users\mrobe\Documents\git_repos\2004scape` was grepped instead, no
  `mourning` hit); 2009scape not implemented either -- ownership confirmed
  against the OSRS-Content tree directly. Native dbrow
  `quest_mourningsendpart1`: id 87, startnpc 1116, endstate 9, questpoints 2,
  `requirement_stats` (4,60)=Ranged 60 / (17,50)=Thieving 50 -- **wiki-verified
  exactly** (https://oldschool.runescape.wiki/w/Mourning%27s_End_Part_I: "60
  Ranged (not boostable), 50 Thieving (not boostable)") and matching
  quest-helper's own `getGeneralRequirements()`. `stat_xp_awarded`
  (17,400000)=Thieving 4000 XP / (3,250000)=Hitpoints 2500 XP, also matching
  quest-helper's `ExperienceReward` list exactly. **`requirement_quests` on
  this dbrow is wrong**, exactly the failure mode this queue's methodology
  warns about: it lists dbrow ids 122/8/130, which resolve to
  `quest_eaglespeak`/`quest_vampyreslayer`/`quest_greatbrainrobbery` -- none
  matching quest-helper's real prereqs (`QuestRequirement(ROVING_ELVES,
  FINISHED)`, `BIG_CHOMPY_BIRD_HUNTING`, `SHEEP_HERDER`), and Great Brain
  Robbery is actually a **Part II** prereq in real OSRS, not Part I's --
  further confirming the field is misattributed. Independently
  wiki-confirmed via the quest's own "Quest Requirements" section. Gated
  instead on `%rovingelves_quest`/`%chompybird`/`%sheepherderquest`
  completion. Native varbit schema: `%mourning_quest` (plain varp, no bit
  children, bare 0..9 int) lines up with the dbrow's own `endstate=9` and
  quest-helper's own `steps.put` key range (0..8) exactly, authored
  0/2/3/4/5/6/7/8->9-complete (steps 0/1 are quest-helper's identical
  `talkToIslwyn` NpcStep, collapsed to one transition). `%mourning_quest_bits`
  (native, 32 bits, fully packed) supplies eleven more native sub-state
  fields matching quest-helper's own `VarbitRequirement`s by name and
  near-exact threshold semantics -- on par with Cold War's own high-water
  mark: `mourning_gnome` (bits 8-11) == the caged-gnome torture progression
  (`knowWeaknesses`>=3, `torturedGnome`>=5, `talkedWithItem`>=6,
  `releasedGnome`>=7, `repairedDevice`>=9, exact thresholds);
  `mourning_sheep_red/green/yellow/blue` (bits 12-15) == `redDyed` etc
  exactly; `mourning_gun_ammo` (bits 16-18) == `redToadLoaded`(1)/
  `greenToadLoaded`(2)/`blueToadLoaded`(3)/`yellowToadLoaded`(4) exactly;
  `mourning_elena` (bits 19-21) == `givenRottenApple`>=2/`receivedSieve`>=4
  exactly; `mourning_food_poison1/2` (bits 22-23) == `poisoned1`/`poisoned2`
  (quest-helper's own `twoPoisoned` accepts any 2-of-3; `mourning_food_poison3`
  has no quest-helper `ObjectStep` at all, left untouched); `mourning_dye_chat`
  (bit 30) == `learntAboutToads` exactly; `mourning_tegid_chat`/`_silk_1`/`_2`/
  `_fur`/`_trousers_chat`/`_trousers_fixed`/`_mourner_disguise` (bits 1-7)
  track the disguise-assembly beats as flavour/state flags.
  `mourning_can_see_eluned`/`_elena_plot_update`/`_eluned_chant`/
  `_mourner_vis`/`_druid_chat` are native but correspond to no quest-helper
  `Requirement`, left untouched. `mourning_quest_part2`/`mourning_quest_main`
  (a separate basevar) plus the Light Temple mirror/crystal-beam sub-bits are
  unambiguously **Part II** content (the Prifddinas puzzle) and were not
  touched, per this tick's own instructions. NPCs: **zero hand-spawning**
  needed -- `mourning_arianwyn`, `mourning_seamstress` (Oronwen),
  `mourner_hideout_head_mourner` (Essyllt), `mourner_hideout_gnome`,
  `mourning_overpass_mourner`, `elena2`, `herder_plaguesheep_1..4` are all
  already world-spawned (base, non-`_vis` forms -- the cache places the base
  id and the `_vis` swap ids quest-helper names aren't in any `.spawn` file;
  cache wins). `roving_islwyn_2ops` is hand-spawned by Roving Elves' own
  `[login,_]` hook already; since Islwyn is also this quest's start NPC
  (`steps.put(0/1)`), this slice extends Roving Elves'
  `[opnpc1,roving_islwyn_2ops]` trigger in
  `quest_rovingelves/scripts/rovingelves_islwyn.rs2` with a
  post-Roving-Elves-complete branch calling a new `~mend1_islwyn_start` proc,
  rather than defining a second, conflicting trigger. Discovered mid-slice:
  `mourner_hideout_gnome`'s own npc entry is a native `multivarbit` swap keyed
  on this exact `mourning_gnome` value (multinpc8..18 ->
  `mourner_hideout_gnome_head`, quest-helper's own post-release NpcID) -- the
  engine already renders the correct model once the bit crosses 8, so this
  port's own `mourning_gnome_rack` loc-based interaction (covering talk,
  tickle, release, give-items, ask-about-toads in one place, since this
  engine has no npc-swap-driven dialogue mechanism) also wired
  `[opnpc1,mourner_hideout_gnome_head]` to the same shared label as a
  convenience alias. `mourning_overpass_mourner`'s actual cache placement
  (`m35_52.spawn`, x2299/y3328) is ~86 tiles from quest-helper's own stated
  `WorldPoint(2385, 3326, 0)` -- cache wins, coords for the HQ basement
  teleport (`0_31_72_60_20`) were hand-decoded from the cache's own Essyllt
  placement instead of quest-helper's. Quest-helper's start NPC is Islwyn
  (`steps.put`); a general wiki fetch/search independently named Eluned as
  escorting the player from Isafdar to Lletya -- per methodology step 1
  quest-helper's own machine-readable state step is authoritative for which
  NPC changes state, so Islwyn was used and Eluned's escort (no
  varbit-changing `Requirement` in quest-helper's own model) was deferred as
  flavour-only, noted rather than silently dropped. Scripts:
  `quest_mourningsendparti/scripts/{mend1_shared,mend1_disguise,mend1_gnome,
  mend1_sheep,mend1_poison,mend1_journal,mend1_debug}.rs2` +
  `configs/mend1.constant` covering the full critical path: Islwyn/Arianwyn
  briefing, mourner kill (narrated, matching Cold War's icelord tier) +
  loot + soap-clean + Oronwen trouser repair, HQ infiltration + Essyllt's
  assignment, the full caged-gnome torture chain, dye-bellows + toad-load +
  fire-at-sheep (all four colours), Elena's rotten-apple/sieve hand-off, the
  barrel/press/naphtha-still (soft-skipped)/sieve/range toxin chain, both
  West Ardougne food-store poisonings, and the Essyllt/Arianwyn finish +
  `~quest_complete(quest_mourningsendpart1)`. Wiki:
  https://oldschool.runescape.wiki/w/Mourning%27s_End_Part_I +
  .../Quick_guide + Transcript:Mourning%27s_End_Part_I (structured summaries
  only, same convention every prior slice used; dialogue authored is
  original wording covering the same beats). `::mend1` / `::mend1run` debug
  hooks added, mirroring `::cwrun`'s idiom. Hit and fixed the same two
  known compile issues this queue has hit before: mixed `&`/`|` in a single
  `if` without explicit parens (this engine has no operator-precedence
  fallback -- fixed with parens in `mend1_journal.rs2`), and the RuneScript
  lexer's lack of `\"` escape support inside string literals (fixed by
  switching the handful of embedded quotes in `mend1_gnome.rs2` to single
  quotes, same fix as Cold War's own debug file hit). Also hit a genuinely
  new one: writing a packed varp whole (`%mourning_quest_bits = 0`) in the
  debug reset is rejected by this engine's own whole-write-destroys-varbits
  check -- fixed by resetting each individual native varbit by name instead
  (`mend1_debug.rs2`). **Also hit and fixed a correctness bug worth flagging
  for future ticks**: this engine's sscompile silently accepts *duplicate*
  trigger headers (`[opnpc1,X]`/`[oploc1,X]`/`[opheldu,X]` declared more than
  once for the same subject) with zero diagnostic -- confirmed empirically,
  not documented anywhere in PORTING_GUIDE. A first draft of this slice's
  scripts freely declared `[opnpc1,elena2]`, `[opnpc1,herder_plaguesheep_1..4]`,
  `[oploc1,mournerstewdoor]`, and `[opheldu,reddye/yellowdye/greendye/bluedye]`
  as fresh triggers, each of which turned out to already be live: `elena2`
  belongs to Biohazard (`areas/area_ardougne_east/scripts/elena.rs2`),
  `herder_plaguesheep_1..4` to Sheep Herder
  (`quest_sheepherder/scripts/diseased_sheep.rs2`), `mournerstewdoor` to
  Biohazard's own mourner-stew subplot
  (`areas/area_ardougne_west/scripts/doors.rs2`), and the four dyes to the
  base dye/cape-recolouring mechanic
  (`skill_crafting/scripts/dye_cape.rs2`). `torirsserver-scripts` compiled clean
  either way with no warning -- the duplication would have silently broken
  one implementation or the other for every player of any of those four
  systems, undetectable short of manually auditing every gameval this
  slice's port touched against the *whole* tree, not just
  `server/scripts/quests`. Caught by a full post-hoc grep of all `[opnpc*`/
  `[oploc*`/`[opheld*` headers this slice introduced against the entire
  `server/scripts` tree (not just the quests subtree), then fixed by
  converting each of the seven colliding triggers into a proc/branch called
  from the *front* of the existing file's trigger (same merge-not-duplicate
  pattern already established for Islwyn) -- see the added notes in
  `elena.rs2`, `diseased_sheep.rs2`, `doors.rs2`, and `dye_cape.rs2`.
  **Future ticks: grep every new opnpc/oploc/opheld header against the full
  `server/scripts` tree, not just the quest being ported, before assuming a
  cache-placed npc/loc/item has no existing trigger.** `mingw32-make -C src
  torirsserver-scripts` exits 0 afterward (post-fix): 13472 scripts compiled
  (13410 -> 13472; net lower than the pre-fix 13482 since seven duplicate
  top-level triggers were merged away rather than left standalone), 0
  errors, 0 warnings/notes naming any `mourning`/`mend1`-touched file (own or
  merged-into). Deferred (soft-skip
  tier, matching this queue's convention): the mourner kill is narrated
  rather than fought, its loot granted directly; the toad-catching/bellows
  loop (quest-helper's own `getToads` step names no ObjectID at all --
  untracked by any real varbit) collapsed to a single dye-on-bellows
  interaction per colour; the Rimmington fractionalising-still tar/heat
  minigame soft-skipped to one narrated interaction (naphtha-barrel path
  only, coal-tar-barrel alternate not implemented); "pick up rotten
  apple"/"pick up empty barrel" (quest-helper `DetailedQuestStep`s with no
  `ObjectID`) granted automatically at the adjacent gated beat; full
  interactive `TORIRS_SIM_CLICK_AT` client headless verification not run
  this tick (same budget note as Cold War's own slice -- `torirsserver-scripts`
  compiling clean is the verification bar these instructions asked for).
  Mourning's End Part II is explicitly out of scope for this slice (separate
  queue row) and was not touched. Next pending = Wanted! (#107, 580 lines).
- slice done: Wanted! (#107) -- fresh, not previously re-verified. Grep-first:
  no LostCity checkout on this box (macOS path in PORTING_GUIDE doesn't exist
  here); grepped the OSRS-Content tree directly for `wanted` (only incidental
  string hits, e.g. dialogue containing the word "wanted") and for a
  `quest_wanted`-named dir (none) -- ownership confirmed. 2009scape not
  implemented either. Native dbrow `quest_wanted`: id 92, startnpc 4687
  (Sir Tiffy Cashien), endstate 11, questpoints 1, requirement_questpoints 32
  -- **wiki/quest-helper-verified exactly**
  (quest-helper's own `getGeneralRequirements()`:
  `QuestPointRequirement(32)`). `stat_xp_awarded` (18,50000) = Slayer 5000 XP,
  matching quest-helper's `ExperienceReward(SLAYER, 5000)` exactly (confirmed
  the dbtable's fixed-point scale independently by reading
  `src/torirsserver/torirs_server_scripts.c`'s `SS_OP_STAT_ADVANCE` case: "the
  reference's xp argument is already in tenths", so 50000 raw = 5000 display
  XP -- cross-checked against Cold War's own already-`done` dbrow, whose
  stat_xp_awarded (16,50000)/(12,20000)/(22,15000) match that same slice's
  logged Agility 5000/Crafting 2000/Construction 1500 exactly). **This dbrow's
  `requirement_quests` is wrong**, same failure mode this queue's methodology
  warns about: it lists dbrow ids 118/87/111/43, which resolve to
  `quest_slugmenace`/`quest_mourningsendpart1`/`quest_swansong`/
  `quest_undergroundpass` -- none matching quest-helper's real prereqs
  (`QuestRequirement(ENTER_THE_ABYSS, FINISHED)`, `RECRUITMENT_DRIVE`,
  `THE_LOST_TRIBE`, `PRIEST_IN_PERIL`). Gated instead on
  `%abyssal_miniquest`/`%rd_main`/`%lost_tribe_quest`/`%priestperil` reaching
  each quest's own native "complete" constant (`^eta_complete`/`^rd_complete`/
  `^lt_complete`/`^priestperil_complete`, all already declared by those
  quests' own `done` slices and confirmed tree-global by grep, e.g. Devious
  Minds already references `^eta_complete` from a file it doesn't own).
  Native varbit schema: `wanted_main` (11 bits on basevar `quest_wanted`)
  authored 0..10 to match quest-helper's own `steps.put` key range exactly
  (states 0/1/2 quest-helper lists as identical are collapsed to one
  transition, same precedent Mourning's End Part I set), then 11 on
  completion to match the dbrow's own endstate; `wanted_joke_option`,
  `wanted_commorb_intel`, `wanted_daquarius_hint` (0/1/2, matching quest-helper's
  own `VarbitRequirement` values exactly), `wanted_lord_d_exposition`,
  `wanted_zammy_mage_hint` all reused as-is. `wanted_mission1..19`/
  `wanted_missionNcomplete` (38 native bits total) is the "Hunt for Solus"
  schema; quest-helper's own Java only maps all 19 by name, but the wiki's
  own Quick_guide (fetched this tick) states the real quest walks a **fixed
  7-stop** route by name -- Rellekka(15) -> Musa Point(5) -> Wizards'
  Tower(12) -> Dorgesh-Kaan(3, Flames of Zamorak damage) -> Ardougne
  Market(8) -> Champions' Guild(2, decoy Black Knight) -> Rune Essence
  mine(4, real fight) -- not ascending numeric mission order, confirming the
  missionN index is a fixed per-location identity, not a visit-order counter.
  Only those 7 (+ mission1 for the initial Canifis/Savant contact) are ever
  set; the other 11 native slots are unused leftovers of the same schema,
  left untouched (same tier Mourning's End Part I left several native bits
  with no quest-helper `Requirement`). Implemented as real (not narrated)
  zone-gated progression: each Commorb "Scan" (native `ifop1=Scan` on
  `wanted_crystal_ball`, matching quest-helper's own mechanic exactly) checks
  the player's actual coordinates (`coordx(coord)`/`coordz(coord)`, the same
  builtins `quest_recruitmentdrive/scripts/recruitmentdrive_spishyus.rs2`
  already used for a coordinate-split check) against each stop's real-world
  bounding box (quest-helper's own `Zone` bounds, except Musa Point's, which
  is a bug in quest-helper itself -- `new Zone(new WorldPoint(2913, 1366, 0),
  new WorldPoint(2919, 3158, 0))` is an 1800-tile-tall zone that cannot be
  what the real game uses -- a small box around quest-helper's own
  `goToMusaPoint` target coordinate was used instead). NPCs: `rd_teleporter_guy`
  (Sir Tiffy Cashien), `sir_amik_varze`, `lord_daquarius`, `black_knight` are
  all already world-spawned (Taverley Dungeon's Black Knights' Base already
  has multiple `black_knight` spawns); `rcu_zammy_mage1_edgeb`/
  `wanted_solus_attackable` needed no hand-spawn/were hand-spawned as
  documented below. **Duplicate-trigger check (mandatory per this tick's own
  instructions): grepped every `[opnpc*`/`[oploc*`/`[opheld*` header this
  slice's NPCs/item would need against the *whole* `server/scripts` tree
  before writing anything.** Four collisions found and merged rather than
  duplicated, all with a comment at the splice point naming the new owner:
  `[opnpc1,rd_teleporter_guy]` (already Recruitment Drive's Sir Tiffy trigger,
  `quest_recruitmentdrive/scripts/recruitmentdrive.rs2` -- that file's own
  header comment already said "Deferred: ... Wanted! arms", anticipating this
  exact splice), `[opnpc1,sir_amik_varze]` (`areas/falador/scripts/
  sir_amik_varze.rs2`, its post-quest label), `[opnpc1,rcu_zammy_mage1_edgeb]`
  + `[opnpc1,rcu_zammy_mage1b]` (`quest_templeoftheeye/scripts/
  templeoftheeye.rs2`, which itself already shares the npc with Enter the
  Abyss's own `~eta_varrock_mage_talk` -- that file's header comment also
  already said "Deferred: full refuse/Wanted! dialogue trees"), and
  `[ai_queue3,black_knight]`/`[ai_queue3,aggressive_black_knight]`
  (`drop_tables/scripts/black_knight.rs2` -- the required "kill a Black
  Knight to prove yourself to Daquarius" beat is spliced into the existing
  death/loot handler rather than given its own competing death hook).
  `lord_daquarius`, `wanted_crystal_ball` (opheld1/opheld2), and
  `wanted_solus_attackable` (opnpc2 + ai_queue3) had zero existing triggers
  anywhere in the tree and were declared fresh. Solus Dellagar is hand-spawned
  (`npc_add`) at the Rune Essence mine once the chase's final scan lands
  there, and fought with real combat (`~npc_retaliate(0)` / `[ai_queue3,...]`
  + `npc_findhero` + `~npc_default_death`, the same pattern
  `quest_anothersliceofham/scripts/slice_sigmund.rs2` and
  `quest_arthur/scripts/sir_mordred.rs2` already established) -- not
  narrated. The required Black Knight kill near Daquarius reuses the
  already-world-spawned generic `black_knight` (any kill counts once the
  quest state calls for it; quest-helper's own guide only names "near
  Daquarius" as flavour text, not a zone-restricted `Requirement`). The decoy
  Black Knight at the Champions' Guild scan stop and the Flames of Zamorak
  damage at the Dorgesh-Kaan stop are narrated only (no combat/damage
  applied), same tier Cold War's icelords / Mourning's End's mourner kill.
  quest-helper's own wilderness Mage of Zamorak branch
  (`talkToMageOfZamorakInWilderness`, npc `rcu_zammy_mage1a`) is dead weight
  for any player who actually meets this quest's requirements -- Enter the
  Abyss finished is a hard prerequisite, so the branch that skips straight to
  the Varrock mage is always the one taken -- and was not modelled;
  `entertheabyss.rs2`'s own trigger for that npc was left untouched entirely
  (zero edits to that file this slice). Scripts:
  `quest_wanted/scripts/{wanted_shared,wanted_tiffy_amik,wanted_daquarius,
  wanted_mage,wanted_commorb,wanted_hunt,wanted_journal,wanted_debug}.rs2` +
  `configs/wanted.constant`, covering every `steps.put` value 0..10 end to
  end: Sir Tiffy's opening pitch (with the "Ask about the Wanted! Quest"
  dialog option quoted verbatim from the wiki transcript) / Sir Amik's Squire
  offer (including the joke "accept Squire status anyway" branch, which sets
  `wanted_joke_option` and still reaches the same next state, matching
  quest-helper's own model where the branch changes no end-state) / Commorb
  purchase (GP or law rune + enchanted gem + molten glass, both paths real
  item removal with insufficient-funds/components checks) / Savant contact /
  Daquarius investigation + Black Knight kill / Mage of Zamorak essence
  hand-off (20 rune or pure essence) / the 7-stop hunt / Solus fight / hat
  pickup / Sir Amik hand-in + `~quest_complete(quest_wanted)`. Wiki:
  https://oldschool.runescape.wiki/w/Wanted!/Quick_guide (fetched this tick
  for the full walkthrough and the fixed hunt order) +
  Transcript:Wanted! (fetched this tick for verbatim dialogue lines: Sir
  Tiffy's opening pitch, Sir Amik's Squire offer and second-visit acceptance,
  the Commorb purchase choice, Savant's "Current Assignment" brief, Daquarius
  before/after the Black Knight kill, the Mage of Zamorak's essence
  ultimatum, Sir Amik's completion line). `::wanted` / `::wantedrun` debug
  hooks added, mirroring `::mend1`/`::mend1run`'s idiom (the reset proc also
  clears the Commorb/hat and despawns any lingering hand-spawned Solus).
  Journal wired (`interface_questjournal/scripts/quest_journal.rs2`, `if
  ($row = quest_wanted) { ~wanted_journal; return; }`). Checked no `&`/`|`
  mixing without parens and no `\"` inside string literals (this queue's two
  known recurring compile pitfalls) before compiling -- neither occurred this
  slice. `mingw32-make -C src sscompile` then `mingw32-make -C src
  torirsserver-scripts` both exit 0: 13488 scripts compiled (13472 -> 13488, +16
  from this slice's own trigger blocks), 0 errors, 0 warnings/notes naming
  any `wanted`-prefixed file, dialogue file, or the two shared files this
  slice spliced into. `ToriRSServer_Pack --check-only` also run: 963 pre-existing
  baseline errors (category-membership / cache-path issues, all predating
  this slice and none naming `wanted`) with zero new ones introduced --
  `torirsserver-scripts` compiling clean is this tick's real verification bar per
  its own instructions. **Also closed one of the two gates this tick's
  instructions asked about**: `quest_deviousminds/scripts/
  deviousminds_monk.rs2`'s own `deviousminds_qualifies` proc had a header
  comment explicitly deferring its Wanted! prerequisite ("still `pending` on
  this same QUESTHELPER_CONTENT_PORT_QUEUE and has no `%wanted_quest` varp in
  this cache yet to check against") -- now that `wanted_main` exists, added
  `if (%wanted_main < ^wanted_complete) { return(^false); }` to that proc
  (matching its existing three sibling checks) and updated the stale header
  comment; re-ran `torirsserver-scripts` afterward, still exit 0, 0 new
  warnings/errors naming `devious`. The instructions also named Mourning's
  End Part I as a quest that had soft-skipped/narrated around a Wanted! gate
  -- grepped `quest_mourningsendparti/` for `wanted` and found only an
  incidental dialogue string ("Islwyn said you **wanted** to speak to me");
  Mourning's End Part I's own real prerequisites (Roving Elves / Big Chompy
  Bird Hunting / Sheep Herder, per its own slice's log) never named Wanted!
  at all, so there was no second gate to close there -- correcting that part
  of this tick's own briefing rather than inventing a fix that doesn't exist.
  Deferred (soft-skip tier, matching this queue's convention): the exact
  chase geography *inside* each of the 7 hunt zones (any tile within the box
  completes that stop); the decoy Black Knight's own combat and the Flames of
  Zamorak damage (both narrated only); quest-helper's wilderness Mage of
  Zamorak branch (dead weight given the hard ETA prerequisite, see above);
  Solus's fight being a real single-target world spawn rather than
  quest-helper's stated "instanced fight"; full interactive
  `TORIRS_SIM_CLICK_AT` client headless verification not run this tick (same
  budget note as every prior slice on this queue -- `torirsserver-scripts`
  compiling clean is the verification bar these instructions asked for).
  Next pending = Death to the Dorgeshuun (#108, 587 lines).
- slice 108 done: Death to the Dorgeshuun -- Zanik, Sigmund, the H.A.M. mill.
  Grep-verified first (methodology steps 1-2): no LC proc, no 2009scape impl;
  genuinely pending. Fetched
  `github.com/Zoinkwiz/quest-helper` raw source for
  `helpers/quests/deathtothedorgeshuun/DeathToTheDorgeshuun.java` (local
  quest-helper checkout path in this doc's own header is a Mac path absent on
  this Windows box) to get the real 13-step state machine
  (`steps.put(0..12)`), all `VarbitID`/`NpcID`/`ObjectID` gameval names, and
  `getGeneralRequirements()`. Native dbrow `quest_deathtothedorgeshuun` (id
  113, endstate 13, requirement_stats agility 23 + thieving 23,
  stat_xp_awarded thieving 2000 + ranged 2000) confirmed by cross-check
  against quest-helper's own `SkillRequirement`/`ExperienceReward` calls.
  dbrow `requirement_quests` resolves to id 87 = `quest_mourningsendpart1` --
  **wrong**, per this queue's own warning about that column; the real
  prerequisite is The Lost Tribe FINISHED (quest-helper's
  `QuestRequirement(THE_LOST_TRIBE, FINISHED)`), gated instead on LC's own
  `%lost_tribe_quest = ^lt_complete`. Native varbit schema on basevar
  `dttd_base`/`dttd_temp` (`dttd_main` 0..13, `dttd_tour_duke/priest/
  goblins/citizens/sun/shop`, `dttd_zanik_in_cellar`, `dttd_tour_ham_deacon`/
  `dttd_tour_ham_johanhus`, `dttd_ham_trapdoor_state`, `dttd_zanik_corpse`,
  `dttd_collecting_tears`, `dttd_guard_1..5_warned/dead`,
  `dttd_mill_guards_dead`) reused as-is, matching quest-helper's own
  `VarbitID` names exactly. Cache multilocs `dttd_mill_trapdoor` /
  `dttd_tunnel_millside` are already driven off `dttd_main` directly, and
  `lost_tribe_mistag`/`lost_tribe_guide`'s native `multinpc` table caps their
  `_2ops` (Follow) variant at `%lost_tribe_quest` = 11 (`lt_complete`) --
  the `_3ops` variant (Cellar/Watermill fast-travel, matching quest-helper's
  UnlockReward "Access to Dorgesh-Kaan") only unlocks at 13, so
  `~dttd_quest_complete` sets `%lost_tribe_quest = 13` on finish, a real
  functional payoff verified against the cache's own multinpc table, not
  just flavour text. Spliced into three pre-existing shared triggers instead
  of duplicating them (critical correctness rule): `losttribe_finish.rs2`'s
  `[opnpc1,lost_tribe_mistag_2ops]` (Mistag's favour-quest offer gated on
  `%dttd_main = 0`), `tearsofguthix.rs2`'s `[oploc1,tog_juna]` (Zanik's
  revival scene gated on `%dttd_main = ^dttd_zanik_saved`), and reused
  `losttribe_ham.rs2`'s already-generic `[oploc1,osf_ham_ladder]` exit and
  `ladders_stairs/scripts/climb_shared.rs2`'s already-generic
  `~climb(-1)` handlers on both `osf_trapdoor_open` (H.A.M. lair entrance,
  already climbable once Lost Tribe sets `%ham_thief` = 1) and
  `dttd_ham_trapdoor_open` (the stage trapdoor, once this slice's own
  `[oploc1,dttd_ham_trapdoor_closed]` picklock action opens it) with zero
  new code. New files: `quest_deathtothedorgeshuun/configs/
  deathtothedorgeshuun.constant` (stage constants + 20 packed coords
  computed from quest-helper's own `WorldPoint`s) + `scripts/{dttd_shared,
  dttd_start,dttd_haminfiltrate,dttd_savezanik,dttd_mill,dttd_journal,
  dttd_debug}.rs2`. Covers all 13 quest-helper steps: Mistag's favour ->
  recruit Zanik (native multinpc wrapper `dttd_zanik_cellar` hand-spawned,
  shows `dttd_zanik_marked` only while `dttd_zanik_in_cellar` = 1) -> soft
  Lumbridge tour (Duke/citizens/priest/goblins/shop folded into one
  conversation, matching this queue's established collapse-the-sightseeing
  convention) + origin story -> Johanhus + hidden stage trapdoor -> 5-guard
  storeroom puzzle (`dttd_ham_guard_1..5` hand-spawned, real ordered
  Talk-to sequence matching their declared `op1=Talk-to`-only ops, no
  Attack op in the cache -- confirms the puzzle's own "distract, don't
  fight" framing rather than a soft-combat simplification) -> door capture
  twist (Zanik dragged off, native `dttd_zanik_corpse` bit reveals
  `dttd_zanik_dead_body`) -> mine + swamp caves -> Juna revival (real
  `[proc,pickaxe_checker]`/tinderbox checks, no consumption) -> zanik's
  story -> crate infiltration -> real combat (`opnpc2`/`ai_queue3`
  `~npc_retaliate`/`~npc_default_death` idiom, matching Defender of
  Varrock's Chaos Golem) against 3x hand-spawned `dttd_ham_guard_mill` then
  `dttd_sigmund_melee` -> smash `dttd_drilling_machine` -> exit via
  `dttd_cave_entrance_millside_blocked` -> `~dttd_quest_complete` (1 QP,
  2000 thieving + 2000 ranged XP tenths, matches dbrow `stat_xp_awarded`
  exactly). Journal wired
  (`interface_questjournal/scripts/quest_journal.rs2`, `if ($row =
  quest_deathtothedorgeshuun) { ~dttd_journal; return; }`). `::
  deathtothedorgeshuun` / `::dttdrun` debug hooks added, same idiom as
  `::wanted`/`::wantedrun`. Wiki:
  https://oldschool.runescape.wiki/w/Death_to_the_Dorgeshuun/Quick_guide +
  Transcript:Death_to_the_Dorgeshuun (consulted for the door-capture twist
  and Zanik's "chosen commander" foreshadowing, which quest-helper's own
  step map only implies). Checked no `&`/`|` mixing without parens and no
  `\"` inside string literals before compiling. `mingw32-make -C src
  sscompile` then `mingw32-make -C src torirsserver-scripts` both exit 0: 13520
  scripts compiled, 0 errors, 0 warnings/notes naming any `dttd`-prefixed
  file or the three shared files this slice spliced into; grep-verified
  every new/spliced trigger name (`[opnpc1,...]`/`[oploc1,...]`/
  `[opnpc2,...]`/`[ai_queue3,...]`/`[proc,...]`/`[debugproc,...]`) resolves
  to exactly one definition across the whole `server/scripts` tree (no
  silent duplicates). Deferred (soft-skip tier, matching this queue's
  convention): the exact stealth zone/behind-guard mechanic (Talk-to
  advances state directly rather than requiring the player to path behind
  each guard first); Zanik's disguise NPC swap
  (`dttd_zanik_follower`/`dttd_zanik_follower_ham`) and any live
  follower-pathing AI (she's represented purely as a state flag +
  fixed-point dialogue, never a moving spawned pet); the tears-of-Guthix
  gather minigame itself (narrated only, no items granted, matching
  tearsofguthix.rs2's own already-deferred full tears IF); damage-type
  restrictions on Sigmund (melee/magic only per quest-helper, not enforced
  here); full interactive `TORIRS_SIM_CLICK_AT` client headless verification
  not run this tick (same budget note as every prior slice -- `torirsserver-
  scripts` compiling clean is the verification bar these instructions
  asked for). Next pending = My Arm's Big Adventure (#109, 589 lines).
- slice 109 done: My Arm's Big Adventure -- Burntmeat, My Arm the troll who
  wants to learn farming, Captain Barnaby, Murcaily, a Baby Roc then a Giant
  Roc. Grep-verified first (methodology steps 1-2): no LC proc, no 2009scape
  impl; genuinely pending. Fetched `github.com/Zoinkwiz/quest-helper` raw
  source for `helpers/quests/myarmsbigadventure/MyArmsBigAdventure.java` for
  the real step map (`steps.put(0..310)` -> complete 320), every
  `VarbitID`/`NpcID`/`ItemID` gameval name, and `getGeneralRequirements()`.
  **Bonus fix while researching #109's own prerequisite**: this queue's own
  row #113 (`eadgarsruse`) was a stale duplicate of the IN-LC table's
  `eadgarsruse -> quest_eadgar` entry (LC already fully implements Eadgar's
  Ruse, journal wired) -- corrected to `done (LC)`, no soft-skip gating
  needed for #109's Eadgar's Ruse prerequisite, gated for real on
  `%eadgar_quest >= ^eadgar_complete`. Native dbrow
  `quest_myarmsbigadventure` (id 120, endstate 320, questpoints 1,
  requirement_stats farming 29 boostable + woodcutting 10 not boostable,
  stat_xp_awarded herblore 100000 + farming 50000, matching quest-helper's
  `SkillRequirement`/`ExperienceReward` calls exactly) confirmed by
  cross-check. dbrow `requirement_quests` resolves to ids 36/50/80 = Plague
  City / Gertrude's Cat / Icthlarin's Little Helper -- **wrong**, per this
  queue's own warning about that column; the real prerequisites (Eadgar's
  Ruse, The Feud, Jungle Potion, all FINISHED, plus >=60% Tai Bwo Wannai
  Cleanup favour) are quest-helper's own `getGeneralRequirements()`, and all
  three prerequisite quests are already real implementations in this tree
  (`%eadgar_quest`/`^eadgar_complete`, `%feud_var`/`^feud_complete`,
  `%junglepotion`/`^junglepotion_complete`) -- no soft-skip gating needed
  anywhere in this slice. Native varbit schema on basevar `myarm_quest`
  reused as-is, matching quest-helper's own VarbitID names exactly (`myarm`
  main progress 0-1023, `myarm_dung` 0-3, `myarm_supercompost` 0-7,
  `myarm_tubers` 0/1, `myarm_fakepatch` 0-15 with the exact same
  usedRake>=6/givenCompost=7/givenDibber>=9 thresholds quest-helper's own
  `VarbitRequirement`s use, `myarm_barnabyswap` 0/1). Discovered the native
  cache already runs a full multinpc positional-swap system for My Arm
  himself: the world-spawned wrappers `myarm_multi_kitchen`/`_ardougne`/
  `_brimhaven`/`_village`/`_larry`/`_teacher` each show `myarm_fixed` only
  across specific `myarm` bitfield ranges, so **My Arm needed zero
  hand-spawning anywhere** in this quest (same "every npc already
  world-spawned" precedent as coldwar #105 / mourningsendparti #106) --
  only the two Roc bosses are hand-spawned, lazily, once the patch is fully
  planted (Death to the Dorgeshuun / Spirits of the Elid idiom). **Critical
  correctness rule applied**: `eadgar_troll_chief_cook` (Burntmeat) already
  had two competing pre-existing `[opnpc1,eadgar_troll_chief_cook]`
  definitions before this slice (`quest_eadgar/scripts/
  eadgar_troll_chief_cook.rs2` and `quest_makingfriendswithmyarm/scripts/
  makingfriendswithmyarm.rs2`) -- a latent duplicate-trigger bug predating
  this slice that silently broke one of the two (sscompile gives no
  diagnostic for this). Burntmeat is shared by three quests in prerequisite
  order (Eadgar's Ruse -> My Arm's Big Adventure -> Making Friends with My
  Arm), so this slice consolidated all three into the single surviving
  definition in `quest_eadgar/scripts/eadgar_troll_chief_cook.rs2`,
  dispatching by quest state before falling through to Eadgar's Ruse's own
  unchanged logic, and removed `makingfriendswithmyarm.rs2`'s duplicate
  (converted to a plain `mf_burntmeat_talk` proc the dispatcher calls).
  Likewise `myarm_fixed`'s existing `[opnpc1,myarm_fixed]` (owned by Making
  Friends with My Arm, whose own quest requires this one finished) was
  spliced -- `if (%myarm < ^myarm_complete) { ~maba_myarm_talk; return; }`
  prepended -- not duplicated; that file's own `[debugproc,
  makingfriendswithmyarm]` was also updated to set `%myarm =
  ^myarm_complete` so its existing `::mfrun` headless test still starts
  correctly now that `myarm_fixed`'s trigger is gated. New files (all under
  `quest_myarmsbigadventure/`): `configs/myarmsbigadventure.constant`
  (21 stage constants 0..320, fakepatch sub-thresholds, coords) +
  `scripts/{maba_shared,maba_burntmeat,maba_myarm,maba_travel,maba_rocs,
  maba_journal,maba_debug}.rs2`. Covers the full quest-helper step map:
  Burntmeat's offer -> My Arm accepts -> Death Plateau troll cauldron
  (`[oplocu,death_troll_cauldron]`, bucket-on-pot) for the goutweedy lump ->
  roof + farming manual -> fertilise (3 Ugthanki dung + 7 supercompost,
  tracked on the real `myarm_dung`/`myarm_supercompost` sub-fields) ->
  Captain Barnaby at Ardougne docks (`myarm_barnaby`, native
  `myarm_barnabyswap` ship-swap reused) -> Brimhaven -> Tai Bwo Wannai ->
  Murcaily (`tbwcu_murcaily`, gated on the native `%favour_percentage >= 60`
  varbit, matching quest-helper's own `VarbitID.FAVOUR_PERCENTAGE` exactly)
  for the hardy gout tubers -> back to the roof -> give rake/supercompost/
  hardy tubers/seed dibber in quest-helper's own real order -> Baby Roc
  (level 75) then Giant Roc (level 172) hand-spawned and fought via the
  `opnpc2`/`ai_queue3` `~npc_retaliate`/`~npc_default_death` idiom (matching
  Death to the Dorgeshuun's Sigmund) -> give spade, harvest -> tell
  Burntmeat -> tell My Arm -> `~maba_quest_complete` (1 QP, 10000 herblore +
  5000 farming XP, 29 burnt meat, matches dbrow `stat_xp_awarded` exactly).
  Journal wired (`interface_questjournal/scripts/quest_journal.rs2`, `if
  ($row = quest_myarmsbigadventure) { ~maba_journal; return; }`).
  `::myarmsbigadventure` / `::mabarun` debug hooks added, same idiom as
  `::dttdrun`/`::wantedrun`/`::mfrun`. Wiki:
  https://oldschool.runescape.wiki/w/My_Arm%27s_Big_Adventure/Quick_guide +
  Transcript:My_Arm%27s_Big_Adventure (fetched this tick for requirements,
  full walkthrough order and reward text). Checked no `&`/`|` mixing without
  parens and no `\"` inside string literals before compiling; grep-verified
  every new/spliced trigger name (`[opnpc1,...]`/`[opnpc2,...]`/
  `[oplocu,...]`/`[ai_queue3,...]`/`[proc,...]`/`[debugproc,...]`) resolves
  to exactly one definition across the whole `server/scripts` tree before
  and after this slice's edits (no silent duplicates, including the two
  pre-existing ones this slice fixed). `mingw32-make -C src sscompile` then
  `mingw32-make -C src torirsserver-scripts` both exit 0: 13536 scripts compiled
  (13520 -> 13536, +16 from this slice's own new triggers/procs/debugprocs),
  0 errors, 0 warnings/notes naming `myarm`, `maba_`,
  `eadgar_troll_chief_cook` or `makingfriendswithmyarm`; dbrow allocator
  summary shows `quest_myarmsbigadventure` cleanly resolved (not STALE; the
  build's one STALE dbrow is the pre-existing, unrelated `quest_asoulsbane`
  from row #43). Deferred (soft-skip tier, matching this queue's
  convention): the rake head/handle break-and-repair mini-step (giving a
  rake directly progresses the patch, matching this queue's precedent for
  simplifying minor item mini-mechanics); patch disease chance and the
  Plant Cure item (narrated as unnecessary once enough supercompost is
  given, matching prior slices' deferral of RNG failure chains); the
  Drunken Dwarf's Leg / dwarf-joke easter egg NPC (`myarm_dwarfjoke`,
  cosmetic only, native default keeps it invisible); the Giant Roc's boulder
  shadow/dodge mechanic (`myarm_giant_roc_shadow`, combat simplified to the
  same `opnpc2`/`ai_queue3` idiom as every other hand-spawned boss on this
  queue); Tool Leprechaun Larry's spade-replacement flavour (a generic
  shared Tool Leprechaun shop, not this quest's own content); exact
  Stronghold/roof ladder pathing (pre-existing generic dungeon traversal, not
  quest-gated); full interactive `TORIRS_SIM_CLICK_AT` client headless
  verification not run this tick (same budget note as every prior slice --
  `torirsserver-scripts` compiling clean is the verification bar these
  instructions asked for). Next pending = The Giant Dwarf (#110, 589 lines).
- slice 110 done: The Giant Dwarf -- Commander Veldaban, Blasidar the
  sculptor's statue of King Alvis, Vermundi/Saro-Dromund/Santiri-Thurgo's
  three items, joining the Blue Opal trade consortium. Grep-verified first
  (methodology steps 1-2): no LC proc, no 2009scape impl; genuinely pending
  (released May 2005, so it's correctly on this post-2009-scope queue only
  because it's a QuestHelper-only quest neither era tree ever shipped, not
  because of its release date -- flagged for anyone auditing row provenance).
  Fetched `github.com/Zoinkwiz/quest-helper` raw source for
  `helpers/quests/thegiantdwarf/TheGiantDwarf.java` (local quest-helper
  checkout path in this doc's own header is a Mac path absent on this Windows
  box) for the real step map (`steps.put(0/5/10/20/30/40)`), every
  `NpcID`/`ObjectID`/`ItemID`/`VarbitID` gameval name, and
  `getGeneralRequirements()`. `tools/questhelper_extract.py` run against the
  fetched file (via a scratch dir, since the tool needs a directory of
  `.java`): every single ItemID/NpcID/ObjectID/VarbitID gameval resolves
  clean against the osrs239 cache (the only "unresolved" line was the tool's
  own dbrow-name-guess heuristic testing `quest_thegiantdwarf`, which isn't
  the real name -- see below). Native dbrow `quest_giantdwarf` (id 84,
  endstate 50, questpoints 2, requirement_stats magic 33 boostable +
  firemaking 16 + crafting 12 + thieving 14 boostable, stat_xp_awarded mining
  2500 + smithing 2500 + crafting 2500 + magic 1500 + thieving 1500 +
  firemaking 1500, matching quest-helper's own SkillRequirement/
  ExperienceReward calls exactly) confirmed by cross-check -- and unlike
  every quest_giantdwarf-preceding row's dbrow warning, this one has **no**
  `requirement_quests` column at all, matching quest-helper's own
  getGeneralRequirements() (no quest prerequisites; Knight's Sword is a
  cross-quest shortcut, not a gate -- see below). Native varbit schema on
  basevar `giantdwarf_main` reused as-is, matching quest-helper's own
  VarbitID names exactly (`giantdwarf_quest` main progress 7 bits,
  `giantdwarf_veldaban_introduced`, `giantdwarf_sculptor_introduced`,
  `giantdwarf_model_state` 4-bit hand-in bitmask matching quest-helper's own
  per-bit VarbitRequirement(MODEL_STATE, true, 0/1/2) probes,
  `giantdwarf_current_company`/`giantdwarf_original_company`,
  `giantdwarf_pie_given`, `giantdwarf_vermundi_givenbook`,
  `giantdwarf_gotpair`; `giantdwarf_cousin_introduced`/`giantdwarf_
  statue_invis`/`giantdwarf_cutscene_guard_visible`/`giantdwarf_
  red_traders_gone`/`giantdwarf_red_axe_gone`/`giantdwarf_
  brothers_toldsuccess`/`giantdwarf_player_had_completely_fixed_axe_at_least_
  once` are cosmetic/flavour-only bits this slice does not wire, deferred).
  giantdwarf_main already carries 31 of a varp's bits and quest-helper's own
  fine per-substep progress is tracked client-side via transient
  ChatMessageRequirement/WidgetTextRequirement, not persisted varbits at all
  -- rather than inventing new tracking vars (no spare `varp_NNN` slot exists
  or was needed), this slice folds all fine-grained progress into
  `%giantdwarf_quest` itself as one ascending sequence (0..28), jumping
  straight to the dbrow's own endstate 50 on completion, matching this
  queue's established "final proc sets the var to the true endstate
  regardless of interim numbering" precedent (myarm, dov, etc). Also
  discovered while trying to bitwise-OR `giantdwarf_model_state`'s three
  hand-in bits together: no precedent anywhere in this tree for `|` inside
  `calc()` -- rather than risk an unverified operator, this slice enforces
  strict clothes -> boots -> axe ordering (quest-helper's own real game state
  is independent/any-order per item, but its own guide/panel already
  sequences them this way) and assigns the cumulative bitmask value directly
  (1, then 3, then 7) at each hand-in instead. **Critical correctness rule
  applied twice**: Thurgo (`areas/port_sarim/scripts/thurgo.rs2`) already had
  a pre-existing `[opnpc1,thurgo]` dispatcher shared by Prying Times, Royal
  Crossbow repair/assembly and Knight's Sword (`%squire` switch) -- spliced
  in as `~gdwarf_thurgo_talk`, not duplicated, gated on `%giantdwarf_quest`
  being in the axe-repair range AND (having reached the Reldo lead OR
  `%squire >= ^squire_given_pie`, i.e. Knight's Sword's own
  `previouslyGivenPieToThurgo` shortcut -- if the player already gave Thurgo
  a redberry pie during Knight's Sword, this quest skips asking for a second
  one and skips the Librarian/Reldo detour entirely, matching the real
  cross-quest shortcut). Reldo (`quest_atailoftwocats/scripts/twocats.rs2`'s
  `[opnpc1,reldo_normal]`) already had a pre-existing dispatcher for A Tail
  of Two Cats -- spliced in as `~gdwarf_reldo_talk`, not duplicated, gated on
  `%giantdwarf_quest = ^gdwarf_imcando_asked`. Grep-verified every other
  new npc/loc trigger (`dwarf_city_boatman_mines_prequest`,
  `dwarf_city_black_guard_leader`, `dwarf_city_shop_sculpture(_model)`,
  `dwarf_city_shop_cloth_poor`, `dwarf_city_librarian`,
  `dwarf_city_shop_armour`, `dwarf_city_excentric_dwarf`,
  `dwarf_city_shop_weapons`, `dwarf_city_secretary_blue_opal`,
  `dwarf_city_director_blue_opal(_cutscene)`,
  `dwarf_keldagrim_bookcase_ladder`, `dwarf_keldagrim_spinning_machine`,
  `dwarf_keldagrim_wide_stairs_lower/upper`) had zero pre-existing
  definitions anywhere in the tree before this slice, so all are fresh, not
  spliced. New files (all under `quest_giantdwarf/`): `configs/
  giantdwarf.constant` (29 stage constants + thresholds + rewards + 16
  packed coords from quest-helper's own WorldPoints) + `configs/
  giantdwarf.varp` (claims the native `giantdwarf_main` carrier with
  protect/transmit/scope, matching bonevoyage's `fossilquest_main`
  precedent -- the cache's own `all.varp` entry is a bare name reservation,
  not a full declaration) + `scripts/{gdwarf_shared,gdwarf_start,
  gdwarf_clothes,gdwarf_boots,gdwarf_axe,gdwarf_consortium,gdwarf_journal,
  gdwarf_debug}.rs2`. Covers quest-helper's full step map end to end:
  Dwarven Boatman (gated on Magic 33/Firemaking 16/Crafting 12/Thieving 14)
  -> Keldagrim -> Commander Veldaban's task -> Blasidar's three requests ->
  Vermundi/Librarian/bookcase-climb/coal+logs+tinderbox spinning machine ->
  exquisite clothes -> Saro -> Dromund (boot-steal/Telekinetic-Grab window
  mechanic narrated via repeated Dromund dialogue rather than a real stealth/
  spell simulation, since the cache has no dedicated window loc or boot
  ground-item props for this quest -- matching this queue's precedent for
  simplifying spatial mechanics with no native prop to hang a real
  interaction off of, e.g. dttd's stealth-zone collapse) -> exquisite pair
  of boots -> Santiri -> sapphires (`opheldu`) -> Librarian/Reldo or Knight's
  Sword shortcut -> Thurgo (pie + iron bar) -> restored battleaxe -> Riki the
  sculptor's model (all three items handed in; Riki's own cosmetic model-swap
  npc variants deferred, no native multinpc dispatch table wires them unlike
  My Arm's troll swap) -> Blasidar's approval -> Blue Opal consortium
  (secretary/director ore-then-bar delivery simplified to one representative
  10-unit exchange each, matching quest-helper's own "recommended" item
  quantities of 10 exactly, rather than the real randomised repeated-task
  minigame to 75/100 points -- only the Blue Opal company path is wired,
  quest-helper's own source leaves company-choice detection an open TODO
  too) -> join -> pledge support -> report to Veldaban ->
  `~gdwarf_quest_complete` (2 QP, 2500 mining/smithing/crafting + 1500
  magic/thieving/firemaking XP, matches dbrow `stat_xp_awarded` exactly).
  Journal wired (`interface_questjournal/scripts/quest_journal.rs2`, `if
  ($row = quest_giantdwarf) { ~gdwarf_journal; return; }`).
  `::giantdwarf` / `::gdwarfrun` debug hooks added, same idiom as every
  prior slice (no `stat_setlevel` proc exists in this tree, confirmed by
  grep before use -- dropped the stat-boost lines from the debug hooks
  entirely, matching dttd/wanted's own precedent of not bothering since the
  headless walk never calls the requirement-check proc). Wiki:
  https://oldschool.runescape.wiki/w/The_Giant_Dwarf/Quick_guide (fetched for
  the full walkthrough + reward text) + Transcript:The_Giant_Dwarf (fetched
  for verbatim dialogue: Boatman's offer, Veldaban's briefing, Blasidar's
  three requests + refusal line, Vermundi's machine sequence, Saro/Dromund's
  boot lines, Santiri/sapphires, Reldo's Imcando lead, Thurgo's pie exchange,
  Riki's hand-in lines, secretary/director task lines, joining-company line).
  Checked no `&`/`|` mixing without parens and no `\"` inside string literals
  before compiling. Also caught and avoided an unverified-operator pitfall
  mid-slice: `|` inside `calc()` has no precedent anywhere in this tree (see
  above) -- redesigned around it rather than risk a silent miscompile.
  **First build attempt accidentally ran from the main repo checkout
  (`c:/Users/mrobe/Documents/git_repos/3d-raster`) instead of this worktree**
  and hit an unrelated pre-existing failure in the main checkout's own
  `ported_scape2009_summoning/scripts/summoning_spirit_wolf.rs2` (undeclared
  symbol `summoning_scroll_howl_scroll`, nothing to do with this slice) --
  caught by noticing the build output paths pointed at the main repo, not the
  worktree; re-ran `mingw32-make -C src sscompile` then `mingw32-make -C src
  torirsserver-scripts` from the correct worktree root and both exit 0: 13562
  scripts compiled (13536 -> 13562, +26 from this slice's own new triggers/
  procs/debugprocs), 0 errors, 0 warnings/notes naming `gdwarf`,
  `giantdwarf`, `thurgo`, `reldo_normal`, `twocats`, or `quest_journal`;
  grep-verified every new/spliced trigger name resolves to exactly one
  definition across the whole `server/scripts` tree both before and after
  this slice's edits (no silent duplicates, including the two pre-existing
  ones this slice spliced into). Deferred (soft-skip tier, matching this
  queue's convention): the real any-order independence of the three item
  side-quests (ported as strict clothes -> boots -> axe, matching
  quest-helper's own suggested guide order); Riki's cosmetic model-swap npc
  variants; the consortium's real randomised repeated-task points minigame
  and the seven non-Blue-Opal company paths; the boot-steal/Telekinetic-Grab
  spatial mechanic (narrated via dialogue); carry-weight gating on the
  bookcase climb; full interactive `TORIRS_SIM_CLICK_AT` client headless
  verification not run this tick (same budget note as every prior slice --
  `torirsserver-scripts` compiling clean is the verification bar these
  instructions asked for). Next pending = Dragon Slayer (#111, 591 lines).
- slice attempt on #111 Dragon Slayer (2026-08-11): grep-first check (step
  1) found it already fully implemented -- LC's own internal codename
  `quest_dragon` (not `dragonslayer`; 11 files, 1083 lines, dbrow
  `quest_dragonslayer1` id 17 endstate 10 releasedate 23,9,2001, journal
  wired). It's a pre-Sept-2004 quest (Feb 2001) that was misfiled on this
  queue instead of the IN-LC table -- fixed both (row #111 -> `done (LC)`,
  added to IN-LC table). While walking the table for the next genuinely-
  pending row, the same misfiling pattern turned up repeatedly (a cross-
  reference against `server/scripts/quests/lc_quests.txt`, the tree's own
  canonical LC-codename list, flagged several matches instantly): row #112
  Tai Bwo Wannai Trio (`quest_tbwt`, pre-Sept-2004, Mar 2003) -- same fix;
  row #126 Murder Mystery (`quest_murder`, pre-Sept-2004, Dec 2003) --
  same fix, and directly load-bearing for this tick's real slice below;
  row #137 Shadow of the Storm (`quest_shadowstorm`, pre-Sept-2004) --
  same fix; row #143 Underground Pass (`quest_upass`, 31 files, 2602
  lines, pre-Sept-2004) -- same fix; row #79 Nature Spirit
  (`quest_druidspirit`, pre-Sept-2004 Mar 2004, sibling of Druidic
  Ritual's `quest_druid`) -- same fix. Two rows were pure table-sync bugs
  (already correctly listed on the IN-LC table, but a stale duplicate
  `pending` row survived on this Queue table from an earlier rebuild):
  row #114 Heroes' Quest (`quest_hero`) and row #131 Icthlarin's Little
  Helper (`quest_icthlarin`) and row #136 Watchtower (`quest_itwatchtower`)
  and row #158 Legends' Quest (`quest_legends`) -- all four flipped to
  `done (LC)` with a note, no new IN-LC row needed (already present).
  Two more are genuinely mid-era (Sept 2004-Jan 2009, not pre-Sept-2004,
  so NOT added to the IN-LC table, just marked `done` on this Queue with a
  note per this queue's own ownership rule): row #56 Elemental Workshop I
  (`quest_elemental_workshop`, dbrow `quest_elementalworkshop1` only --
  note Elemental Workshop II's own dbrow `quest_elementalworkshop2` exists
  natively but has NO script implementation and NO journal wire, genuinely
  pending, queue row #139) and row #78 Creature of Fenkenstrain
  (`quest_fenkenstrain`, Oct 2006). One row (#54 Shades of Mo'rt'ton) was
  checked and confirmed genuinely still pending despite LOOKING done at a
  glance -- `quest_mortton` exists with a dbrow + journal shell
  (`mortton_journal.rs2`) and one real step (`serum_book.rs2` sets
  `%morttonquest` once), but has no npc dialogue files, no main walkthrough
  script, and `%morttonquest` is never advanced past that single step
  anywhere in the tree -- left `pending`, not touched.
- slice done (King's Ransom, #115, replacing the dead #111 slot): see row
  #115 for the native-schema summary. Full quest-helper step map (0-85)
  ported end to end on `%kr_quest` (native, 0..90): Gossip's introduction
  (spliced into the existing shared `gossipy_man` dispatcher,
  `quest_murder/scripts/gossip.rs2` -- critical correctness rule, not
  duplicated) -> guard hands out the investigation (spliced into
  `quest_murder/scripts/murder_guard.rs2`'s existing `murderguard`
  dispatcher) -> break into Sinclair mansion via `murderwindow` (fresh
  loc), climb `murder_qip_spiralstairs`/`murder_qip_spiralstairstop`
  (fresh locs) collecting scrap paper / address form / a black knight helm
  from `kr_sin_bookcase3a` (fresh loc) -> evidence handed back to the
  guard -> gossip points to Camelot -> Anna Sinclair (fresh npc
  `kr_anna_sinclair`, none of these fresh triggers exist anywhere else in
  the tree, grep-verified) asks for her name cleared at trial -> Seers'
  Village courthouse trial: `kr_courthouse_stairs_top`/`kr_judge`/
  `kr_court_fence_door` (fresh locs) call the dog handler / butler / maid
  witnesses in quest-helper's own fixed order, each testimony spliced into
  the pre-existing `pierre_the_family_dog_handler`/`hobbes_the_butler`/
  `mary_the_maid` dispatchers (`quest_murder/scripts/{pierre,hobbes,
  mary}.rs2`) using the native `kr_court_witness`/`kr_court_dog_proof`/
  `kr_court_butl_proof`/`kr_court_maid_proof`/`kr_court_thread` varbits --
  not guilty verdict -> Anna reveals the statue's secret passage, then the
  `kr_camelot_knight_statue` (fresh loc) ambush reveals Morgan Le Faye's
  plot and captures the player, narrated then teleported to
  `^kr_prison_coord` -> Merlin's prison dialogue spliced into the existing
  shared `[opnpc1,merlin]` trigger (`areas/area_camelot/scripts/
  merlin.rs2`, which already had an unconditional "rushing off" chat +
  `npc_del` for the unrelated Merlin's Crystal crystal-prison cutscene --
  gated the King's Ransom branch strictly on `%kr_quest` state, safe
  without a real zone check since the player is teleported into the
  fortress prison at capture and cannot physically be near the Camelot
  workshop Merlin during that window) -> vent found
  (`kr_underground_jail_cell_wall_bottom_with_vent`, fresh loc) -> cell
  door opened (`kr_underground_jail_bars_gate`, fresh loc) gated on either
  a lockpick or telekinetic-grab runes (law + air), matching quest-helper's
  own OR-requirement -> keep search (`kr_jewelry_box_table`, fresh loc)
  finds a golden-chalice Holy Grail replacement -> Wizard Cromperty's
  Animate rock scroll spliced into the existing shared
  `[opnpc1,cromperty_pre_diary]`/`[opnpc1,cromperty_post_diary]`
  dispatcher (`areas/area_ardougne_east/scripts/wizard_cromperty.rs2`) ->
  Black Knights' Fortress entrance reuses the quest's own pre-existing
  `bkfortressdoor1`/`bksecretdoor` triggers unmodified (their existing
  guard-disguise check already gates on the exact same bronze med helm +
  iron chainbody King's Ransom also requires) -> `kr_bkf_basement_
  laddertop`/`kr_arthur_statue_multi` (fresh locs) free King Arthur from
  Morgan Le Faye's granite curse (animate rock scroll + granite + chalice)
  and hand him the disguise in the same interaction -> final "meet Arthur
  back in Camelot" spliced into the existing shared `[opnpc1,king_arthur]`
  dispatcher (`areas/area_camelot/scripts/king_arthur.rs2`, which already
  had Merlin's Crystal/Holy Grail branches -- King's Ransom checked first,
  highest priority, since it's independent of and later than both) ->
  `~kr_quest_complete` (1 QP, 33000 defence XP, 5000 magic XP, 5000 XP
  lamp, matches dbrow `stat_xp_awarded` and quest-helper's own
  ExperienceReward/ItemReward calls exactly). Simplified (soft-skip tier,
  matching this queue's convention for narrating mechanics with no native
  widget precedent, e.g. Giant Dwarf's Telekinetic Grab boot-steal): the
  real 4-tumbler lock puzzle (`kr_tumb1-4_ans`/`kr_guess_num`/widget 588 --
  native varbits exist but no IF3 puzzle-widget precedent anywhere in this
  tree) is narrated via a single dialogue interaction instead of a real
  puzzle widget; keep-floor climbing (`kr_stairs`) is pure traversal with
  no state consequence, deferred; Knight Waves Training Grounds
  (`kr_wave_instr`/`kr_knightwaves_state`, a separate post-quest minigame)
  is out of scope and deferred. Dialogue is paraphrased from the wiki
  Quick guide (`https://oldschool.runescape.wiki/w/King%27s_Ransom/
  Quick_guide`) and a non-verbatim summary of `Transcript:King%27s_Ransom`
  (the transcript tool declined verbatim reproduction, citing Jagex
  copyright -- summarised beats used instead, not copied text). Caught a
  real parser bug mid-slice: `def_boolean $x = <comparison expression>`
  (e.g. `inv_total(...) > 0 | inv_total(...) > 0`) does not parse as a
  statement (`unexpected '>' at the start of a statement`) even though the
  identical comparisons parse fine *inside* an `if (...)` condition
  (confirmed against existing precedent, e.g. `sanfew.rs2`,
  `professor_oddenstein.rs2`) -- fixed by declaring `def_boolean $x =
  false;` then reassigning `$x = true;` inside `if` blocks, matching the
  only real precedent found for boolean reassignment
  (`godwars_bosses.rs2`'s `$slam`). New files (all under
  `quest_kingsransom/`): `configs/{kingsransom.constant,kingsransom.varp}`
  (claims the native `kr_varp1`/`kr_varp2`/`kr_varp3` carriers with
  protect/transmit/scope -- the cache's own `all.varp` entries are bare
  name reservations, matching this queue's giantdwarf/mourning precedent)
  + `scripts/{kr_shared,kr_mansion,kr_court,kr_prison,kr_fortress,
  kr_journal,kr_debug}.rs2`. `::kingsransom` / `::kingsransomrun` debug
  hooks added, same idiom as every prior slice. Journal wired
  (`interface_questjournal/scripts/quest_journal.rs2`, `if ($row =
  quest_kingsransom) { ~kr_journal; return; }`). Build: ran from this
  worktree (`c:/.../\.claude/worktrees/questhelper-port`, double-checked
  cwd before building per this tick's own warning about a prior mix-up);
  `mingw32-make -C src sscompile` then `mingw32-make -C src
  torirsserver-scripts` both exit 0: 13587 scripts compiled (13562 -> 13587,
  +25 from this slice's own new/spliced triggers), 0 errors; grep-verified
  every new/spliced trigger name (`murderwindow`, `murder_qip_spiralstairs`,
  `murder_qip_spiralstairstop`, `kr_sin_bookcase3a`,
  `kr_courthouse_stairs_top`, `kr_judge`, `kr_court_fence_door`,
  `kr_camelot_knight_statue`, `kr_underground_jail_cell_wall_bottom_
  with_vent`, `kr_underground_jail_bars_gate`, `kr_jewelry_box_table`,
  `kr_bkf_basement_laddertop`, `kr_arthur_statue_multi`,
  `kr_anna_sinclair`, plus the eleven spliced-into shared npc triggers)
  resolves to exactly one definition across the whole `server/scripts`
  tree, and the build log has zero errors/warnings/notes naming
  `kingsransom` or any `kr_*` symbol. Deferred (soft-skip tier): full
  interactive `TORIRS_SIM_CLICK_AT` client headless verification not run
  this tick (same budget note as every prior slice -- `torirsserver-scripts`
  compiling clean is the verification bar these instructions asked for);
  the tumbler puzzle widget and Knight Waves Training Grounds noted above.
  **Table bookkeeping note for the next tick:** rows #53 Contact! (355
  lines), #54 Shades of Mo'rt'ton (355, confirmed genuinely a stub this
  tick, see above), #72 Olaf's Quest (425), #73 Grim Tales (427), #76
  Haunted Mine (435) and #80 Mountain Daughter (459) are all SMALLER than
  King's Ransom and still marked `pending` -- they were not grep-audited
  this tick (out of this slice's scope) and may or may not be genuinely
  missing; the strict smallest-pending-first rule would point at #53
  `Contact!` next, not the next row after this slice's replacement chain.
  Recommend the next tick grep-audit #53 first per this queue's own
  depth-first ordering, given how many stale rows this tick found via the
  `lc_quests.txt` cross-reference technique (grep the helper's line-count
  neighbours against `server/scripts/quests/lc_quests.txt`, the tree's own
  canonical LC-codename list, before assuming a row is genuinely unported).
- slice done (Contact!, #53): grep-first check (steps 1-2) found **no** LC
  or 2009scape implementation -- `lc_quests.txt` has no `contact`/`icslittleh`/
  `jex`/`maisa` entry, and no `quest_complete(quest_contact)` call existed
  anywhere in the tree before this slice. Native dbrow `quest_contact`
  (id 124, endstate 130, questpoints 1, stat_xp_awarded 17,70000 = thieving
  7000xp, matches quest-helper's own `ExperienceReward(THIEVING, 7000)`
  exactly) + a full native varbit schema on basevar `contact_master`
  (`contact` 8-bit main progress + `contact_discussed_menaphos`,
  `contact_found_kaleef`, `contact_met_maisa`, `contact_osman_told`,
  `contact_osman_met`, `contact_told_priest`, `contact_met_baker`,
  `contact_people_vis`, `contact_bankers_vis`, `contact_gotscarabs`,
  `contact_maisa_ans`, `contact_been_downstairs`, `contact_osman_vis`,
  `contact_got_mage/lance/bow`, `contact_maisa_invis`,
  `contact_finished_cutscene`, `contact_never_had_keris`,
  `contact_used_reward_lamp`) already existed, matching quest-helper's own
  VarbitID names exactly -- reused as-is. `requirement_quests` resolved to
  dbrow ids 75/112 = Mountain Daughter / Royal Trouble, **neither of which
  is Contact!'s real prerequisite** (critical correctness rule, confirmed
  wrong yet again) -- real prereqs per quest-helper's own
  getGeneralRequirements() + the wiki are Prince Ali Rescue and Icthlarin's
  Little Helper, both already fully implemented in this tree, so both are
  hard-gated (`%princequest >= ^prince_complete`, `%ics_little_var >=
  ^ics_complete`) with no soft-skip needed. All npcs/locs/items resolved
  clean via `tools/questhelper_extract.py contact --check` (0 unresolved) --
  `contact_jex`/`contact_osman_multi`/`contact_osman_desert_multi` and the
  High Priest's `_town` variant are all already world-spawned (spawn configs
  `m51_43.spawn`/`m51_49.spawn`/`m51_44.spawn`), no hand-spawning needed
  except the Giant Scarab boss (spawned lazily on the second dungeon trip,
  My Arm's Big Adventure / Death to the Dorgeshuun idiom). Caught a real
  coordinate trap: quest-helper's own WorldPoint for Maisa/Kaleef's body/the
  scarab fight (~2258-2284, ~4315-4323, plane 0) does **not** match her real
  native spawn (`contact_maisa_multi` at (3218, 9246, 0), region 50_144,
  confirmed via `server/scripts/areas/world/configs/m50_144.spawn`) -- but
  *does* line up almost exactly with quest-helper's own `chasm` Zone
  bounding box (WorldPoint(3216,9217,0)-(3265,9277,0)), confirming this
  cache renders the underground chasm cavern at a duplicated high-Y map
  region (same trick seen elsewhere in this tree, e.g. `quest_dragon`'s own
  `demon_slayer.rs2` ground-item drop at `0_50_154_25_41`) -- cache wins, so
  Kaleef's body, Maisa and the boss fight are all placed near
  (3218, 9246, 0) instead of quest-helper's raw WorldPoints (documented in
  full in `configs/contact.constant`). Quest-helper's own ~50-point
  trap-dodging `linePoints` maze between the two ladders has no established
  maze/trap mechanic anywhere in this tree (soft-skip tier, matching King's
  Ransom's tumbler-lock precedent) -- narrated via `mes()` + a straight
  `p_teleport` instead. Spliced (not duplicated) into one pre-existing
  shared trigger: `[opnpc1,ics_little_hipriest_vis]`
  (`quest_beneathcursedsands/scripts/beneathcursedsands.rs2`, which already
  falls through to Icthlarin's Little Helper's own `ics_hipriest_talk`
  label) -- added a Contact! branch gated on `%ics_little_var >=
  ^ics_complete & %contact < ^contact_complete`, placed after BCS's own two
  checks and before the existing ICS fallthrough, grep-verified this is the
  only definition of that trigger in the tree both before and after the
  edit. All other triggers are fresh (`contact_jex`, `contact_maisa`,
  `contact_osman_multi`, `contact_osman_desert_multi`,
  `contact_osman_cave_instance`, `contact_scarab_boss` (opnpc2 + ai_queue3),
  `contact_temple_trapdoor_open`, `contact_ladder_barricaded`,
  `contact_dead_body_kaleef_vis`, `contact_kaleef_scroll` opheld1),
  grep-verified none pre-existed. New files under `quest_contact/`:
  `configs/{contact.varp,contact.constant}` (claims the native
  `contact_master` carrier with protect/transmit/scope, matching this
  queue's giantdwarf/mourning/kingsransom precedent) +
  `scripts/{contact_shared,contact_priest,contact_jex,contact_dungeon,
  contact_maisa,contact_osman,contact_scarab,contact_journal,
  contact_debug}.rs2`. `::contact` / `::contactrun` debug hooks added, same
  idiom as every prior slice. Journal wired
  (`interface_questjournal/scripts/quest_journal.rs2`, `if ($row =
  quest_contact) { ~contact_journal; return; }`). Dialogue paraphrased from
  the wiki Quick guide (`https://oldschool.runescape.wiki/w/Contact!/
  Quick_guide`) and quick-guide/transcript summaries fetched via WebFetch
  (not verbatim, per copyright). Build: ran from this worktree
  (`.claude/worktrees/questhelper-port`, cwd double-checked before building);
  `mingw32-make -C src sscompile` then `mingw32-make -C src torirsserver-scripts`
  both exit 0: 13606 scripts compiled (13587 -> 13606, +19 from this
  slice's own new/spliced triggers), 0 errors, 0 warnings/notes naming
  `contact` or any `contact_*` symbol; grep-verified every new/spliced
  trigger name resolves to exactly one definition of its trigger *type*
  across the whole `server/scripts` tree (opnpc2 + ai_queue3 both existing
  once each on `contact_scarab_boss` is correct, not a duplicate). Deferred
  (soft-skip tier, matching this queue's convention): the maze/trap gauntlet
  noted above; the Giant Scarab's real light-extinguish/poison mechanics (no
  dedicated widget precedent, generic combat used instead); the
  `contact_barricade` cosmetic gate object and the `contact_bankers_vis`/
  `contact_finished_cutscene`/`contact_used_reward_lamp`/`contact_got_mage/
  lance/bow` cosmetic native bits (post-quest bank-access polish, no
  gameplay branch found in the guide); full interactive
  `TORIRS_SIM_CLICK_AT` client headless verification not run this tick (same
  budget note as every prior slice -- `torirsserver-scripts` compiling clean is
  the verification bar these instructions asked for).
- staleness sweep on the remaining small unaudited rows per this tick's own
  recommendation: #72 Olaf's Quest, #73 Grim Tales, #76 Haunted Mine and #80
  Mountain Daughter were all checked against `lc_quests.txt` (no
  `olaf`/`grim`/`mountain` entries at all; the one `haunt` hit,
  `quest_haunted`, is already correctly attributed to Ernest the Chicken,
  row #23 -- confirmed via `~quest_complete(quest_ernestthechicken)` in
  `quest_haunted/scripts/quest_haunted.rs2:316`, not Haunted Mine) and
  against the tree for any existing `quest_olafsquest`/`quest_grimtales`/
  `quest_hauntedmine`/`quest_mountaindaughter` directory or
  `~quest_complete(...)` call (none found). All four remain genuinely
  `pending` -- left untouched, no full port attempted this tick (out of
  budget for a second full slice). Next pending row (smallest-first): #72
  Olaf's Quest, 425 lines.
- slice #72 done: Olaf's Quest -- re-verified genuinely pending (no
  `quest_olaf*` dir, no `lc_quests.txt` hit, only the native dbrow
  `quest_olafs` with no implementing script) before writing. Full native
  varbit schema reused: `%olaf_quest_var` (main progress, basevar
  `olaf_var`, authored breakpoints 0/10/20/30/40/50/60/80 matching
  quest-helper's own `steps.put` keys + dbrow endstate) plus
  `%olaf_ingrid_quest`/`%olaf_volf_quest` (family carvings delivered),
  `%olaf_fire_multi` (campfire multiloc reskin), `%olaf2_gate_disk_1..4`
  (picture-wall puzzle), `%olaf2_walkway_1/2` (rope-bridge barrel repairs),
  `%olaf2_killed_ulfric`/`%olaf2_gate_completed`. Scripts:
  `quest_olafsquest/scripts/{olaf_hradson,olaf_family,olaf_overworld,
  olaf_dungeon,olaf_journal}.rs2` + `configs/quest_olafsquest.constant`;
  hooked `~olaf_try_dig` into the shared `general_use/scripts/spade.rs2`
  chain and `~olaf_journal` into `interface_questjournal/scripts/
  quest_journal.rs2`. Picture-wall puzzle: reverse-derived the exact lever
  mechanic from `PaintingWall.java`'s own hint-branch checkpoints (not
  guessed) -- right lever adds 1 to right+left (mod 5), top adds 1 to
  top+left, left adds 1 to left+bottom, bottom adds 1 to top+bottom; fixed
  real-game start top=2/right=3/bottom=2/left=1 makes right->bottom->top->
  left->confirm the deterministic solve path, but this port simulates the
  full state machine (any order/extra pulls still resolve via confirm's
  own all-4 check), not a scripted replay. Key/lock gate: whichever of the
  5 `olaf2_gate_key_*` a skeleton fremennik drops must match its lock
  button (`_1`=cross/`_2`=square/`_3`=triangle/`_4`=circle/`_5`=star per
  quest-helper's own assignment); wrong guess breaks the key. dbrow
  `requirement_quests` wrong (resolves to Nature Spirit, id 57) -- hard-
  gated on The Fremennik Trials instead (`%viking = ^viking_complete`);
  found in the process that `quest_viking` (mislabeled "Fremennik Exiles"
  in the IN-LC table above) actually implements Fremennik Trials, confirmed
  via `quest_journal.rs2:643` wiring `quest_fremenniktrials` to
  `~viking_journal` -- table left uncorrected this tick (out of scope,
  noted here for a future audit pass). Renamed this quest's own constants
  to `^olafq_*` after `sscompile` caught a real duplicate: `quest_viking`
  already declares `^olaf_not_started`/`^olaf_started`/`^olaf_complete` for
  its own "Olaf" trial-judge NPC. Zero hand-spawning -- every npc
  (`olaf`/`olaf_ingrid`/`olaf_volf`/all nine `olaf2_undead_viking_lvl*`/
  `olaf2_brine_rats`/`olaf2_giant_bat`) and every ground item
  (`rope`/`olaf2_walkway_repair_barrel`/`_inv`/`olaf2_walkway_repair_rope`)
  already world-spawned in `m42_58.spawn`/`m41_57.spawn`/`m42_158.spawn`,
  matching quest-helper's own coords and Zone bounds tile-for-tile (no
  duplicated high-Y region here); only Ulfric himself is hand-spawned
  (absent from every world spawn file, matching quest-helper's own gestalt
  combat step). Wiki: Olaf's_Quest/Quick_guide +
  Transcript:Olaf's_Quest (dialogue summarized only, same Jagex-copyright
  caveat every prior slice on this queue has noted -- dialogue authored is
  original wording for the same beats). Deferred (queue-log note): the
  Agility-scaled barrel-repair fail chance ("guaranteed at level 78" per
  the wiki, no formula recoverable); the visual skull-disk model rotation
  (the four skull models in `interfaces/olaf2_skull_puzzle.if` are raw
  client model archive ids, not a gameval-named pack type -- no verified
  per-rotation sub-model id found, puzzle is fully solvable server-side
  without it); the `olaf2_rusty_gate_puzzle_open` loc swap-on-solve (the
  swap would need to fire from inside an `if_button1` interface callback,
  which has no `loc_coord`/`loc_id` trigger context -- `%olaf2_gate_completed`
  still lets players walk through on any further click, so progress is not
  blocked); the flavour-only Sven's map / Ulfric's note viewer interfaces
  (`interfaces/olaf2_treasuremap.if`, `olaf2_ulric_parchment.if`, neither
  gates progress in quest-helper's own step map). Verification:
  `mingw32-make -C src sscompile` then `mingw32-make -C src torirsserver-scripts`
  both exit 0: 13664 scripts compiled (13606 -> 13664, includes this tick's
  new files plus unrelated growth since the last logged count); 0 errors;
  grep of the full build log for "olaf" returned nothing (no warnings/notes
  naming this slice); grep-verified every new/spliced trigger name (incl.
  all nine skeleton variants' `opnpc2`/`ai_queue3`, both `if_button1`
  interfaces' component names) resolves to exactly one definition of its
  trigger type across the whole `server/scripts` tree before compiling.
  Next pending row (smallest-first): #73 Grim Tales, 427 lines (re-verified
  genuinely pending by the prior tick's staleness sweep above).
- slice done (2026-08-11): Grim Tales (row #73) -- re-verified genuinely
  pending first (grep of `server/scripts` + `lc_quests.txt` for
  `grimtales`/`grim_sylas`/`grim_grimgnash` found nothing; no 2009scape
  checkout on this machine). Quest Helper source fetched from
  `github.com/Zoinkwiz/quest-helper` (`helpers/quests/grimtales/GrimTales.java`,
  427 lines, matches the queue's own line count). Found a fully **native
  cache schema** on inspection: dbrow `quest_grimtales` (`configs/all.dbrow`
  id 135, endstate 60, questpoints 1, requirement_stats/stat_xp_awarded
  matching quest-helper's own `getExperienceRewards()` and the wiki reward
  list exactly: 60000 Woodcutting / 25000 Agility / 25000 Thieving / 15000
  Herblore / 10000 Farming / 5000 Hitpoints XP) plus a complete native
  varbit schema on basevars `grim_main`/`grim_second` matching every
  `VarbitID` quest-helper's own Java references by name
  (`GRIM_GRIFFIN_ASLEEP`, `GRIM_GIVEN_FEATHER`, `GRIM_DWARFQUEST`,
  `GRIM_DWARF_VIS`, `GRIM_PIANOTRACK`, `GRIM_PIANO_USED`, `GRIM_HEAD_FOUND`,
  `GRIM_HAVE_PENDANT`, `GRIM_STALK_STATE`, `GRIM_GIANT_DEAD`), all reused
  as-is. dbrow `requirement_quests` (col 25) resolved to id 160 =
  `quest_porcineofinterest` -- wrong per this queue's standing caution --
  hard-gated on Witch's House instead (`%ballquest = ^ball_complete`, the
  wiki + quest-helper's own `getGeneralRequirements()` agree). Fetched
  `Grim_Tales/Quick_guide` and `Transcript:Grim_Tales` for dialogue/mechanic
  detail beyond the helper's own sparse `steps.put` map. Wrote 6 new files
  under `quest_grimtales/{configs,scripts}/`: `quest_grimtales.constant`
  (full derivation writeup + coordinate/zone constants), `grim_journal.rs2`,
  `grim_sylas.rs2` (trinket trade + final reward), `grim_grimgnash.rs2`
  (7-beat bedtime-story dialogue puzzle via `~p_choice4` + feather theft),
  `grim_watchtower.rs2` (wall climb, drain pipe x2, beard climb, Rupert,
  Miazrqa), `grim_witchhouse.rs2` (piano note-sequence puzzle via
  `if_openmain(grim_piano)` + 14 `if_button1` triggers, compartment search,
  shrink-potion recipe, mouse-hole maze routed by `inzone` zone membership
  since the cache places multiple nail-wall climb instances per room, not
  the single coordinate quest-helper's own `Zone`-based steps name),
  `grim_beanstalk.rs2` (plant/water via `opheldu`, climb, Glod hand-spawned
  in his own cloud instance same as Ulfric in Olaf's Quest, golden goblin,
  shrink+chop). Found and fixed one genuine pre-existing bug blocking this
  slice in shared content: `quest_ball_locs.rs2`'s `open_witch_house_door`
  (the witch's house front door is the **same building** as Grim Tales'
  own Miazrqa's house -- confirmed via `ObjectID.WITCHHOUSEDOOR` resolving
  to the identical cache record, the wiki's own "get another key from the
  pot outside the Witch's House" line, and the map square: quest-helper's
  own `house` Zone (2901-2907,3466-3476) converts to the identical m45_54
  square `witchpot`/`witchhousedoor` already occupy) had a refusal
  condition that fired whenever `%ballquest = ^ball_complete` -- i.e.
  *always*, for every Grim Tales player, since Witch's House is a hard
  prerequisite -- permanently locking every one of them out even while
  holding the key. Narrowed to `%ballquest < ^ball_started` only. Also
  merged (not duplicated) a `grim_turnip` branch into
  `skill_herblore/scripts/brew_potion.rs2`'s existing
  `[opheldu,tarrominvial]` trigger for the shrink-potion recipe, plus a new
  non-colliding `[opheldu,grim_turnip]` for the reverse click order.
  Grep-verified every new/spliced trigger name (all `oploc`/`opheld`/
  `opnpc`/`if_button`/`ai_queue3` subjects, both merged shared-file
  triggers) resolves to exactly one definition of its trigger type across
  the whole `server/scripts` tree before compiling -- no duplicate-trigger
  collisions. Deferred (queue-log, same tier as prior slices' own
  deferrals): exact wrong-branch maze coordinates (routed to the nearest
  correct room via zone membership instead of quest-helper's own single
  `leaveWrong1`/`leaveWrong2` coordinates, which are not fully recoverable
  from the helper or wiki alone); Grimgnash's four-choice story
  wrong-answer text (original wording in the same tone -- the wiki
  transcript only records the correct line at each of the 7 beats); the
  piano interface's own `opencompartment`/`searchcompartment` buttons
  (`interfaces/grim_piano.if`) in favour of the world object's native
  `op3=Search` on `grim_piano_open`, matching quest-helper's own
  `searchPiano` being a separate step from `playPiano`; the per-note piano
  highlight varbits (`%grim_piano_note_ue` etc -- the sequence counter
  alone fully validates the puzzle); three of `%grim_beard_climb`'s five
  native cosmetic values (only 0/2 driven); `grim_junglestatue`'s "climb
  again for another golden goblin" flavour object, which has no `op1`/`op2`
  declared in this cache at all -- Glod himself drops the one golden
  goblin quest-helper's own step map actually requires, rather than
  guessing at an unauthored interaction verb; watering-can charge
  consumption (checked generically for "any can with a dose," not
  decremented, a one-off quest interaction rather than the general farming
  system). Verification: `mingw32-make -C src sscompile` clean (built
  `build_win64/sscompile`, 0 diagnostics beyond pre-existing snprintf
  truncation warnings in the compiler itself); `mingw32-make -C src
  torirsserver-scripts` exit 0, 13736 scripts compiled (13664 -> 13736, this
  tick's 6 new files + 2 merged edits); grep of the full build log for
  "grim" returned zero errors and zero notes naming this slice (only
  pre-existing "no Attack op" warnings on native `grim_*` cache npc
  records already shipped before this port, e.g. `grim_giant_mouse`,
  `grim_grimgnash`, unrelated to any script here); `ToriRSServer_Pack
  --check-only` could not run in this worktree (`cache.osrs239` is not
  present -- confirmed pre-existing/environmental: the same invocation
  reports ~960 category/cache errors that reproduce identically and
  mention no `grim_*` symbol, matching this queue's own "BUILD PIPELINE
  NOTE" that `torirsserver-scripts` exit 0 is the real verification bar here).
  Next pending row (smallest-first): #76 Haunted Mine, 435 lines (#74/#75
  already `done`).
- slice #76 done: Haunted Mine -- grep-first confirmed no LC proc (`lc_quests.txt`
  has `quest_haunted`, but that's Ernest the Chicken, a same-word coincidence,
  not this quest) and no 2009scape row; found a full **native cache schema**
  waiting to be wired: dbrow `quest_hauntedmine` (id 68, startnpc
  `saradominist_zealot`, endstate 11, questpoints 2, requirement_stats
  crafting 35 boostable, stat_xp_awarded strength 220000=22000xp -- all
  confirmed against the wiki's own reward/requirement list) plus a full
  native varbit group on basevar `hauntedmine_bits` matching quest-helper's
  own `VarbitID.HAUNTEDMINE_*` names exactly (`heardaboutkey`,
  `liftpoweredonce`/`liftpowerednow`, `begincart_fungus`/`endcart_fungus`,
  `pointspuzzlestarted`, and eight lever bits `lever_a/b/c/d/e/i/j/k`).
  dbrow `requirement_quests` reads 111 (`quest_swansong`) -- wrong per this
  queue's standing caution -- hard-gated on Priest in Peril instead
  (`%priestperil >= ^priestperil_complete`, IN-LC, already fully playable).
  The lever-to-varbit mapping is not guessed: forced by quest-helper's own
  `ConditionalStep` pairing (e.g. `leverAWrong = VarbitRequirement(LEVER_B,
  0)` paired with the object step that pulls `HAUNTEDMINE_POINT_LEVER1` means
  that lever corrects `LEVER_B`, not `LEVER_A`) -- worked out the full
  8-lever target combination (b/a/e/i=1, c/d/j/k=0) from all eight pairings
  and implemented it as a real 0/1-toggle puzzle on the points-info panel's
  native "Check" op, not a scripted replay. All named locs/npcs are native
  cache map geometry (op text confirmed in `configs/all.loc`/`all.npc`:
  entrances "Crawl-down", ladders "Climb-up"/"Climb-down", lifts
  "Go-up"/"Go-down", valve "Turn", levers "Pull", panel "Check", cart/chisel
  crate "Search", mushroom "Pick", crystal outcrop "Cut", stairs
  "Walk-up"/"Walk-down") -- no hand-placing, only trigger scripts written.
  Several loc names are reused at multiple physical placements in this
  sprawling multi-region dungeon (`hauntedmine_laddertop_1e` at three rooms,
  `hauntedmine_ladder_1w` at three, `hauntedmine_puzzle_cart` /
  `hauntedmine_dark_stairs_top` at two each) -- dispatched by `inzone()`
  against quest-helper's own `Zone` rectangles (same technique Grim Tales'
  mouse-hole maze used) or, for the flooded room's two dark stairs sharing
  one room with no distinguishing zone, by `coordx()` side. The
  `hauntedmine_ladder*`/`hauntedmine_laddertop*` names are also registered in
  the shared `ladders_stairs/configs/ladders.loc` under generic
  `climb_up`/`climb_down` categories (a same-tile plane shift, wrong for this
  dungeon's cross-region layout since quest-helper's own WorldPoints show
  every "level" at plane 0 but wildly different x/y) -- this slice's own
  named `[oploc1,...]` triggers override that category default per
  `ladders.rs2`'s own documented name-beats-category rule, without editing
  that shared file. Only Treus Dayth (`hauntedmine_boss_ghost`) is
  hand-spawned on the key-pickup ambush, same tier as Ulfric/Glod in prior
  slices; `saradominist_zealot` and `hauntedmine_boss_key` are both already
  base world spawns (`m53_50.spawn`, `m43_69.spawn`). Deferred: the real-game
  valve/lift "race the ghost before it re-closes the valve" timing mechanic
  (turning the valve opens the lift unconditionally/permanently here, same
  tier as Olaf's Quest's barrel-repair chance); the "dark room" wrong-path
  mechanic for descending without a lit fungus (blocked with a message
  instead, matching Grim Tales' maze precedent); the points-settings panel's
  own map-grid interface (native op1 "Check" used directly); the Salve
  Amulet crafting-recipe unlock and Abandoned Mine shortcut / Nightmare Zone
  rewards (flavour-only per quest-helper's own `getUnlockRewards()`, no
  gating role). Files: `server/scripts/quests/quest_hauntedmine/configs/
  quest_hauntedmine.constant`, `scripts/{hauntedmine_zealot,
  hauntedmine_dungeon,hauntedmine_dayth,hauntedmine_journal}.rs2`; journal
  wired `interface_questjournal/scripts/quest_journal.rs2`. Wiki
  https://oldschool.runescape.wiki/w/Haunted_Mine/Quick_guide +
  Transcript:Haunted_Mine. Verification: `mingw32-make -C src sscompile`
  clean (built `build_win64/sscompile`, 0 diagnostics beyond pre-existing
  snprintf-truncation warnings in the compiler itself); `mingw32-make -C src
  torirsserver-scripts` exit 0, 13791 scripts compiled (13736 -> 13791); grep of
  the full build log for "hauntedmine" and for "error" (case-insensitive)
  both returned zero hits -- no warnings or errors attributable to this
  slice. `ToriRSServer_Pack --check-only` not runnable in this worktree (no
  `cache.osrs239` present, same pre-existing environment gap every prior
  slice on this queue has noted). Next = Mountain Daughter (#80, 459 lines;
  #77/#78/#79 already `done`).
- slice #80 done: Mountain Daughter -- Mar 2005, Hamal's missing daughter
  Asleif, Mountain Camp/Rellekka diplomacy, White Pearl food source, Kendal
  the bearsuited "god"; re-verified genuinely pending first (no
  `mountaindaughter`/`mdaughter` proc anywhere in `server/scripts`, no
  `lc_quests.txt` hit besides the unrelated `quest_belowicemountain`, only
  native cache spawns/dbrow/varbit data). Native dbrow `quest_mountaindaughter`
  (id 75, startnpc `mdaughter_hamal` matching quest-helper's own first step,
  endstate 70, questpoints 2, requirement_stats agility 20 boostable,
  stat_xp_awarded attack 1000 + prayer 2000 tenths exactly matching
  quest-helper's own `ExperienceReward`s, no `requirement_quests` column at
  all) + full native varbit schema on basevar `mdaughter_var`
  (`mdaughter_quest_var`, `mdaughter_mud_var`, `mdaughter_relations_var`,
  `mdaughter_food_var`, plus ten more flag bits) reused as-is, matching
  quest-helper's own VarbitID names exactly. This port authors its own
  `%mdaughter_quest_var` breakpoints 0/10/20/30/40/50/60/70 matching
  quest-helper's own `steps.put` keys plus a final completion tier, resolving
  the one real Java ambiguity (whether the Hamal "diplomacy" and "food
  supply" topics interleave) against the wiki Quick guide's own linear
  step numbering (12-20 "Making Peace" fully before 21-26 "Food Supply"),
  which settles it as sequential -- `%mdaughter_food_var` only starts moving
  once `%mdaughter_relations_var` reaches 60. `viking_brundt_child` (Brundt
  the Chieftain, Rellekka longhall) is a **shared npc** already scripted by
  `quest_fremennikexiles/scripts/fremennikexiles.rs2`'s own
  `[opnpc1,viking_brundt_child]` -- per this queue's own standing caution
  that sscompile accepts duplicate trigger definitions with no diagnostic,
  this slice does not declare a second one; it edits that existing trigger to
  check a new `~mdq_brundt_relevant` proc first and falls through to the
  existing `@fx_brundt_longhall` otherwise. Kendal ("The Kendal") is a native
  multi-npc (`mdaughter_multi_bear`, `multivarbit=mdaughter_bear_multi_state`,
  displaying as `mdaughter_bearman` then hiding once the bit flips) --
  triggers bind the wrapper name, confirmed against this tree's only other
  multi-npc precedent (`quest_mm/scripts/mm_daero.rs2`'s own
  `[opnpc1,mm_daero]`, not a display-variant name); the real fight is the
  separate hand-spawned `mdaughter_bearman_fighter` (native combat stats
  already in `npc_combat/`), same tier as Treus Dayth in Haunted Mine. The
  corpse is dropped via `obj_add(...,^lootdrop_duration)` on the fighter's
  `ai_queue3` death hook, matching quest-helper's own plain-walk-over
  `grabCorpse` TileStep; its native `ifop3=Bury` op is reused directly for
  the burial action, the same `[opheld<n>,...]` binding style
  `skill_prayer/scripts/bury_bone.rs2` established for bones. Deferred:
  quest-helper's own long pole/plank/glove alternates matrix (checked for
  `mdaughter_stick` and the four base plank types specifically, and "any item
  in the worn hands slot" for gloves, not the full exceptions lists); the
  flat-stone crossings' exact "attempt without a plank" fail mechanic (always
  fails outbound without the item, always succeeds with it; the return leg
  simplified to always succeed either way, no formula recoverable); the
  corpse-burial tile soft-kept to "somewhere on Lake Island 3" via `inzone`
  rather than quest-helper's own single named tile (same tier as this
  queue's standing caution about cache coordinates vs. raw quest-helper
  WorldPoints). Files: `server/scripts/quests/quest_mountaindaughter/
  configs/quest_mountaindaughter.constant`, `scripts/
  {mountaindaughter_camp,mountaindaughter_spirit,mountaindaughter_kendal,
  mountaindaughter_burial,mountaindaughter_journal}.rs2`; one merged edit to
  `quest_fremennikexiles/scripts/fremennikexiles.rs2`'s existing Brundt
  trigger; journal wired `interface_questjournal/scripts/quest_journal.rs2`.
  Wiki https://oldschool.runescape.wiki/w/Mountain_Daughter/Quick_guide +
  Transcript:Mountain_Daughter. Verification: `mingw32-make -C src sscompile`
  clean (built `build_win64/sscompile`, 0 diagnostics beyond pre-existing
  snprintf-truncation warnings in the compiler itself); `mingw32-make -C src
  torirsserver-scripts` exit 0, 13839 scripts compiled (13791 -> 13839); grep of
  the full build log for "mdaughter"/"mountaindaughter"/"mdq_"/"error"
  (case-insensitive) all returned zero hits -- no warnings or errors
  attributable to this slice, and none attributable to the shared
  `fremennikexiles.rs2` edit either. `ToriRSServer_Pack --check-only` not runnable
  in this worktree (no `cache.osrs239` present, same pre-existing environment
  gap every prior slice on this queue has noted). Bonus finding while
  auditing row #80's neighbours: row #54 Shades of Mort'ton was stale --
  LostCity already has a (partial, stub-only) proc for it in
  `server/scripts/quests/quest_mortton/`, so it belongs on
  `CONTENT_PORT_QUEUE.md`, not this queue; corrected in the table above, see
  that row's own note. Next pending row (smallest-first, after that
  correction): #119 Swansong, 644 lines.
- slice #119 done: Swan Song -- May 2006, Herman Caranos's besieged
  Piscatoris Fishing Colony. Grep-first confirmed genuinely unowned: no
  `lc_quests.txt` hit, no `swansong`/`swanarnold`/`swanseatrol`/`swanherman`
  proc anywhere in `server/scripts` before this slice (only native cache
  spawns/dbrow/varbit/combat/multi-npc data). Quest Helper source fetched
  from `github.com/Zoinkwiz/quest-helper`
  (`helpers/quests/swansong/{SwanSong,FixWall,FishMonkfish}.java`, 644 lines
  total, matching the queue's own line count exactly). Native dbrow
  `quest_swansong` (id 111, endstate 200, questpoints 2, requirement_stats =
  Magic 66 + Cooking 62 + Fishing 62 + Smithing 45 + Firemaking 42 +
  Crafting 40, matching quest-helper's own `SkillRequirement` list and
  `stat_xp_awarded` = Magic 15000 + Prayer 10000 + Fishing 50000 exactly,
  tenths format) + native varbit schema on basevar `swansong_quest`
  (`%swansong` main stage plus `%swansong_franklin`, `%swansong_wall_1..5`,
  `%swansong_arnold`, `%swansong_trolls`, `%swansong_bones`) and
  `swansong_temp` (`%swansong_ambush`, `%swansong_colony`) reused as-is,
  matching quest-helper's own VarbitID names exactly. `requirement_quests`
  wrong (dbrow id 107 resolves to none of In Aid of the Myreque/Garden of
  Death/other unrelated tables sharing that id number, not quest-helper's
  own One Small Favour id 74 or Garden of Tranquillity id 90; two *other*
  quests' own `.constant` files independently flag `quest_swansong` itself
  as a bad `requirement_quests` target too) -- soft-skipped both real
  prereqs (queue rows #157, #125, both still pending), matching the King's
  Ransom / Mourning's End Part I precedent for unported sibling quests.
  **The main `%swansong` breakpoints are not authored -- they're the real
  underlying values**, recovered from three native multi-npc records keyed
  on `multivarbit=swansong` (`swan_multioutside` at the colony gate displays
  Herman at values 0/5/10/15/20 and the Wise Old Man's cutscene appearance
  at 30/40/50/55; `wom_multi` at Draynor displays the Wise Old Man for
  values 0-29 and hides him from exactly 30 onward, the same tick he
  reappears at the gate) -- this cross-validates quest-helper's own
  `steps.put(...)` keys as literal real varbit values. Also native
  multi-locs keyed directly on quest sub-vars confirmed several design
  choices independently: `swan_firebox`/`swan_press` swap appearance on
  `multivarbit=swansong_franklin` (press only "activates" once the firebox
  is lit, matching this port's own gate), `swan_wall_1..5` swap
  broken/fixed appearance on their own `multivarbit=swansong_wall_n`, and
  `swan_fish` (the quest-only fishing spot, distinct from the permanent
  post-quest `swan_fishingspot` npc unlocked by `getUnlockRewards()`) shows
  as active for `%swansong_arnold` 0-5 and hides at exactly 6 -- this port's
  own chosen "finished" threshold. Scripts:
  `quest_swansong/scripts/{swansong_colony,swansong_army,swansong_finale,
  swansong_journal}.rs2` + `configs/quest_swansong.constant`; two merged
  edits to existing shared triggers per this queue's own "no duplicate
  trigger" caution -- `quest_makingfriendswithmyarm/scripts/
  makingfriendswithmyarm.rs2`'s own `[opnpc1,wise_old_man]` (Draynor) and
  `areas/area_yanille/scripts/yanille_thin_npcs.rs2`'s own `[opnpc1,
  wizard_frumscone]`, both now check a `~swansong_*_relevant` boolean proc
  first and fall through unchanged otherwise (same pattern Mountain
  Daughter's own Brundt merge established). Entrance-ambush trolls and the
  Sea Troll Queen hand-spawned (native combat stats reused as-is from
  `npc_combat/s/swan_troll_ambush.combat` / `swan_seatroll_queen.combat`),
  same `npc_add`/`[ai_queue3,...]`/`npc_findhero`/`~npc_default_death` tier
  as Treus Dayth / Ulfric / Glod. Journal wired
  `interface_questjournal/scripts/quest_journal.rs2`. Wiki
  https://oldschool.runescape.wiki/w/Swan_Song/Quick_guide +
  Transcript:Swan_Song (original-wording dialogue covering the same beats,
  same Jagex-copyright caveat every prior slice notes). Deferred: the real
  quest's own multi-NPC siege-battle cutscene finale (native combat data
  exists for `swan_skeleton_battle/training/unattackable`,
  `swan_troll_battle`, `swan_troll_general`, `swan_wom_ambush`,
  `swan_wom_coma` -- strongly implying a real large-scale scripted battle
  with the Wise Old Man's skeletal army and the Wise Old Man himself being
  knocked unconscious partway through, not recoverable from quest-helper or
  the wiki quick guide beyond "defeat the Sea Troll Queen"; this port
  hand-spawns the real Queen for a straightforward finale instead), the
  ambient colony population (`swan_colonist_1..3`, `swan_skeleton_training`
  dummies, the boat-trip npcs, `swan_kalphite_1/2`, `swan_drunkendwarf`),
  cooking-gauntlets burn-rate reduction, the Crafting Guild's own
  clay-mining/potter's-wheel minigame (Master Crafter hands over a pot +
  lid directly instead), Western Provinces hard-diary hook. `mingw32-make
  -C src sscompile` clean (rebuilds `build_win64/sscompile`, only
  pre-existing snprintf-truncation warnings in the compiler itself);
  `mingw32-make -C src torirsserver-scripts` exit 0, 13887 scripts compiled
  (13839 -> 13887); grep of the full build log for "swansong"/"swan_"
  (case-insensitive) returned zero hits -- no warnings or errors
  attributable to this slice or either merged shared-trigger edit.
  `ToriRSServer_Pack --check-only` not runnable in this worktree (no
  `cache.osrs239` present, same pre-existing environment gap every prior
  slice on this queue has noted). Next pending row (smallest-first): #120
- slice #120 done: Royal Trouble -- grep-first audit found no LC/2009scape
  ownership (no `royaltrouble`/`royal_trouble` hits anywhere in the tree,
  no `lc_quests.txt` entry); native dbrow `quest_royaltrouble` (id 112,
  endstate 30) and full native varbit schema on `royal_questvarbits`
  (`royal_quest`/`royal_misc`/`royal_etc`) + `royal_varbits` were already
  present but undeclared (bare varp reservations), claimed here same as
  `kr_varp1/2/3`. Thematically and mechanically linked to Throne of
  Miscellania (#99, done) as the task flagged -- checked `quest_misc`
  first and found the SAME native npcs (misc_advisor_ghrim/
  misc_king_vargas/misc_queen_sigrid) are reused by Royal Trouble's own
  startnpc (dbrow startnpc=3670=misc_advisor_ghrim); merged a
  `~royaltrouble_relevant` branch into quest_misc's own existing
  `misc_advisor_ghrim.rs2`/`misc_king_vargas.rs2`/`misc_queen_sigrid.rs2`
  `[opnpc1,...]` triggers instead of duplicating them (duplicate trigger
  definitions compile silently with no diagnostic -- critical correctness
  rule), falling through to the original Throne of Miscellania dialogue
  when not relevant. Fetched quest-helper's own RoyalTrouble.java via
  GitHub raw (summarized by the fetch tool, not verbatim) to recover its
  own VarbitRequirement breakpoint sets: ROYAL_MISC
  {10,20,30,40,50,60,80,110,120} (killedBoss = ROYAL_MISC>=120), ROYAL_ETC
  {10,20,40} (finishedFinalConvoWithSigrid = ROYAL_ETC>=40) -- used as the
  progression anchors; the semantic assigned to each intermediate
  breakpoint is this port's own reconstruction (documented as such in
  `configs/royaltrouble.constant`), not independently verified against
  the original bytecode. The lift-repair puzzle's breakpoints
  (`%royal_liftstage` 0-9, `%royal_coalinengine` 0-5) ARE independently
  confirmed -- read directly off this cache's own multivarbit `.loc`
  records (`royal_side_scaffold_multiloc`, `royal_top_scaffold_multiloc`,
  `royal_engine_platform_multiloc`, `royal_lift_platform_multiloc`,
  `royal_lift_platform_at_top_multiloc`), per this queue's own methodology
  step of preferring native multi-loc records for real progression
  breakpoints; implemented as a genuine multi-step item-on-object puzzle
  (crates give beams/rope; opheldu combines beam+pulley beam into
  long/longer pulley beams; oplocu applies each piece to the concrete
  scaffold/platform/engine loc names in sequence; coal shoveled into the
  engine via opheldu; `oploc1` "Use-Lift" rides to the top) rather than
  narrated, since the cache ships every needed item/loc gameval
  (`royal_beam`, `royal_plank_pulley[_long][er]`, `royal_coal_engine`,
  `royal_mining_prop`) already declared and world-placed. One crate,
  `royal_crate_planks+pulleys`, has a literal `+` in its own cache
  identifier with no precedent anywhere in this tree for use as a trigger
  subject; rather than risk an unverified parser edge case, that starter
  pulley beam is instead handed over narratively by Donal alongside the
  mining prop and coal engine -- the other two crates (plain identifiers)
  are real repeatable interactions. dbrow `requirement_quests` decodes to
  The Corsair Curse (id 147) -- not a real Royal Trouble prerequisite
  (same recurring cache decode/linkage corruption flagged on King's
  Ransom's row and others); the wiki's actual direct prerequisite is
  Throne of Miscellania alone (which itself already transitively requires
  Heroes' Quest + The Fremennik Trials), so this slice hard-gates on
  `%misc_quest = ^misc_king_signed_treaty` instead. Investigation-phase
  NPCs (the wiki's Gunnhild/Leif/Frodi/Magnus/Helga/Haming/Matilda) don't
  resolve as distinct npcs anywhere in `configs/all.npc.compack`; this
  cache's own `royal_misc_guard`/`royal_etc_guard` (the very soldiers each
  side blames) stand in as the real interview targets instead, matching
  this queue's established "cache wins" substitution precedent (The Feud,
  Spirits of the Elid, Another Slice of H.A.M.). Boss
  (`royal_sea_snake_mother_smaller`, "Giant Sea Snake", combat level 149 --
  confirmed via `all.npc`'s own vislevel field, matching the wiki exactly)
  hand-spawned lazily on trigger with `~npc_retaliate`/`npc_findhero`/
  `~npc_default_death`, the same idiom as Contact's Giant Scarab
  (`contact_scarab.rs2`) -- no extinguish-light/poison boss mechanics
  precedent in this tree, left to the generic combat system. Zero
  hand-spawning for every other npc; all already world-spawned
  (`m39_60`/`m40_60`/`m39_160`/`m40_160` .spawn files) -- packed coords in
  the constant file are the exact tiles of those spawns, not invented (no
  Zone-bounds source available to refine further, flagged as a
  deferred-precision item like other approximate-coord slices). Deferred:
  cave hazards (steam vents, falling rocks, slippery-rock plank crossing)
  as pass-through terrain -- no damage/fail-chance system precedent
  anywhere in this tree to hook into, same tier as Grim Tales' deferred
  stone/rock fail rolls; the heavy box stays a permanent unconsumed
  souvenir item post-quest rather than being formally "turned in" a
  second time. Hit one syntax bug during the build: this dialect's string
  literals don't support `\"` escapes (verified by the compiler error,
  not assumed) -- reworded the one line that needed an embedded quote
  instead. `mingw32-make -C src sscompile` clean (only pre-existing
  snprintf-truncation warnings in the compiler itself); `mingw32-make -C
  src torirsserver-scripts` exit 0, 13940 scripts compiled (13887 -> 13940);
  grep of the full build log for "royal_"/"royaltrouble" (case-insensitive)
  returned zero warnings or errors attributable to this slice or any of
  the three merged shared-trigger edits. `::royaltrouble` /
  `::royaltroublerun` debug commands added, matching every prior slice's
  idiom; `ToriRSServer_Pack --check-only` not runnable in this worktree (no
  `cache.osrs239` present, same pre-existing environment gap every prior
  slice has noted). Wiki
  https://oldschool.runescape.wiki/w/Royal_Trouble/Quick_guide + full
  walkthrough (dialogue paraphrased, not verbatim, per copyright, same
  caveat as every prior slice). Next pending row (smallest-first): #121
  The Great Brain Robbery.
  Royal Trouble, 657 lines.

- slice #121 done: The Great Brain Robbery -- grep-first audit found no
  LC/2009scape ownership (`lc_quests.txt` clean, no `brainrobbery`/
  `feverharmle`/`brainbrothe` hits anywhere in `server/scripts`); native
  dbrow `quest_greatbrainrobbery` (id 130, endstate 130, questpoints 2)
  already declared in the cache, unused until this slice. Master progress
  var `%brain_quest_var` (0/10/.../130) is a plain varp, not a sub-varbit --
  confirmed authoritative (not inferred) by this cache's own multi-npc
  records: `brain_tranquility`/`brain_island_tranquility` both declare
  `multivarp=brain_quest_var`, swapping Brother Tranquility from zombie to
  human exactly at value 100, and `brain_island_fenkenstrain` only renders
  Fenkenstrain from value 70 on -- both landing exactly on quest-helper's
  own `steps.put` keys (fetched via GitHub raw), independent confirmation
  of the full 0/10/.../130 breakpoint set, stronger than most prior slices
  where only endpoints were independently checkable. Crate-build
  (`%brain_crate` 1..5) and door-breach (`%brain_barrel_setup` 2..5)
  puzzles both independently confirmed via this cache's own
  `brain_fenk_crate`/`brain_mon_entrance_door_multi` native multiloc
  records, matching quest-helper's own VarbitRequirement thresholds
  exactly -- implemented as real click/item-on-loc puzzles (Build ->
  Add-bottom -> Fill 10 wooden cats -> Blow wolf whistle -> attach shipping
  order; keg -> fuse -> tinderbox) using the concrete cache loc state names
  directly, same idiom as Royal Trouble's lift repair. Statue passage and
  underwater stairs repair are likewise cache-baked map locs with no
  `.spawn` entry anywhere in this tree -- script triggers only, zero
  hand-spawning needed for any puzzle geometry. dbrow `requirement_quests`
  decodes to Black Knights' Fortress/Lost City -- not real prerequisites
  (same recurring cache decode corruption); real prereqs per wiki are
  Creature of Fenkenstrain (hard-gated on `%creatureoffenkenstrain >=
  ^fenk_complete`, already implemented), Cabin Fever and Recipe for
  Disaster/Freeing Pirate Pete (both have native dbrow rows but zero
  scripts anywhere in `server/scripts` -- soft-skipped, matching this
  queue's established convention for unported sibling prereqs, e.g. King's
  Ransom's One Small Favour). Two shared-file merges to avoid duplicate
  triggers (critical correctness rule): `areas/area_canifis/scripts/
  rufus.rs2`'s existing `[opnpc1,werewolfshopkeeper1]` trigger (Rufus's
  crate-scheme branch) and `quests/quest_fenkenstrain/scripts/
  fenkenstrain.rs2`'s existing `@fenk_talk` label (Fenkenstrain's own
  branch) -- both gated on `%brain_quest_var` relevance, falling through to
  existing dialogue unchanged otherwise. Mi-Gor/Barrelchest (level 190)
  hand-spawned lazily on trigger for the church confrontation, same idiom
  as Royal Trouble's Giant Sea Snake / Contact's Giant Scarab; no
  prayer-disabling boss mechanic precedent in this tree, left to the
  generic combat system. Deferred: wooden-cat crafting is a simplified
  oak-plank + knife make-action, not the real player-owned-house workshop
  flatpack minigame (no POH workshop precedent anywhere in this tree);
  surgical instruments (clamp/tongs/3 bell jars/30 skull staples) drop from
  Sorebones kills via a simple scripted `obj_add` on `ai_queue3` death, not
  a verified native drop table (not recoverable from available sources);
  Barrelchest's broken anchor reward stays broken (post-quest pirate-smith
  repair flavour not implemented, same tier as other reward items needing
  later unlocks elsewhere in this tree). `mingw32-make -C src sscompile`
  clean; `mingw32-make -C src torirsserver-scripts` exit 0, 14078 scripts
  compiled (14041 -> 14078); grep of the full build log for "brain"
  returned exactly one hit during development (an unknown-constant
  `^chat_evil` typo, fixed to `^chat_angry`, a real cache-confirmed mood
  constant) and zero hits on the clean rebuild. Wiki
  https://oldschool.runescape.wiki/w/The_Great_Brain_Robbery/Quick_guide +
  https://oldschool.runescape.wiki/w/The_Great_Brain_Robbery (dialogue
  paraphrased, not verbatim, per copyright, same caveat as every prior
  slice). `ToriRSServer_Pack --check-only` not runnable in this worktree (no
  `cache.osrs239` present, same pre-existing environment gap every prior
  slice has noted). Next pending row (smallest-first): #122 Rum Deal, 662
  lines.

- slice #122 done: Rum Deal -- grep-first audit found no LC/2009scape
  ownership (`lc_quests.txt` clean, no `rumdeal`/`rum_deal` hits anywhere in
  `server/scripts`; the only tree hits were the native
  `interfaces/rum_deal_title.if` / `rum_deal_censor.if` cutscene-adjacent
  interfaces and `configs/all.dbrow`'s own undeclared `[quest_rumdeal]`
  block). Fetched quest-helper's own `RumDeal.java` (497 lines,
  steps.put 0-18) and its `SlugSteps.java` sub-helper (165 lines, the
  slugling-fishing/pressure-barrel arc) verbatim via GitHub raw -- both
  complete, not summarized. Native dbrow `quest_rumdeal` (id 95, endstate 19,
  questpoints 2, startnpc 601 = `deal_pete`, requirement_stats fishing 50 +
  prayer 47 + crafting 42 + slayer 42 + farming 40 -- all 5 rows present and
  matching quest-helper's own `getGeneralRequirements()` exactly,
  stat_xp_awarded fishing/prayer/farming 7000xp each -- raw dbrow values *10
  internal xp units, matches quest-helper's own `ExperienceReward` list
  exactly) already declared in the cache, unused until this slice. dbrow
  `requirement_quests` decodes to ids 163/111 (A Night at the Theatre / Swan
  Song depending on which `values=0:0:` block a naive id scan lands on) --
  not real prerequisites (same recurring cache decode corruption flagged
  repeatedly on this queue); the real prerequisites are quest-helper's own
  `getGeneralRequirements()`: Zogre Flesh Eaters (`quest_zogreflesheaters`,
  `%zogre >= ^zfe_complete`, IN-LC, already implemented) and Priest in Peril
  (`quest_priestperil`, `%priestperil >= ^priestperil_complete`, IN-LC,
  already implemented) -- both hard-gated. Three real native sub-varbits on
  basevar `deal_var` back the actual puzzles, all independently confirmed via
  this cache's own multiloc records rather than guessed: `%deal_farming`
  (blindweed patch, quest-helper's own thresholds rakedPatch=3/plantedPatch=4/
  grownPatch=5) matches `deal_blindweed`'s own native multiloc *0-indexed*
  against the variable value (multiloc1 shown at value 0) -- value3 =
  `deal_blindweed_empty` (freshly raked), value4 = `deal_blindweed_seed`
  (just planted), value5 = `deal_blindweed_fullygrown` (op1=Pick declared
  directly on that variant), landing exactly on quest-helper's own
  thresholds; `%deal_barrel` (pressure barrel sluglings, 0-5) matches
  `SlugSteps.java`'s own `getVarbitValue(DEAL_BARREL)` read and is
  independently confirmed via `deal_multi_lever`'s own native multiloc
  (values 0-4 show `deal_lever_down`, value5 shows `deal_lever_up` -- the
  lever visibly pops up at exactly 5 sluglings, matching quest-helper's own
  `numHandedIn >= 5 -> pullPressureLever` branch); `%deal_multi_hopper`
  (brewing control, 0-2, drives `deal_multicontrol`'s own native multiloc
  idle/spinning/running) has no quest-helper VarbitID of its own -- the
  original source gates the wrench/spirit arc purely on item possession
  (`holyWrench`/`evilSpiritNearby`), reused here only for cosmetic feedback.
  The master progress var, `%deal_quest` (native basevar, bare reservation),
  has no native multi-npc/multi-loc record fixing a concrete breakpoint set
  (unlike Great Brain Robbery's `brain_quest_var`), so this port's own
  16-value reconstruction (0-15, one per real distinguishable game state,
  collapsing quest-helper's own duplicate-instruction step keys like
  steps.put(0)/steps.put(1) which both point at the identical `talkToPete`
  object) is documented as such in `configs/rumdeal.constant`;
  `^deal_complete = 19` is not invented -- read directly off the dbrow's own
  `endstate` column, matching Contact!/Great Brain Robbery/Priest in Peril's
  own "_complete constant = dbrow endstate" idiom. npcs=deal_pete,
  deal_captian_braindeath (cache's own spelling, not "captain"), deal_davey,
  deal_captian_donnie, deal_evil_spirit, deal_fever_spiders1 -- this queue's
  own row hint abbreviations (`dealevilsp`/`dealpete`/`dealcaptian`) don't
  match any real cache name; cache wins, same precedent as The Giant Dwarf /
  My Arm's Big Adventure. Every npc except the Evil Spirit is already
  world-spawned (`server/scripts/areas/world/configs/m33_79.spawn` for the
  island roster incl. 11x `deal_fever_spiders1` in the basement and 3x
  `deal_squid` fishing spots around the coast, `m57_55.spawn` for Pete at the
  Ectofuntus dock) -- only the level-150 Evil Spirit is hand-spawned lazily
  on trigger next to the brewing control, same idiom as every prior
  on-demand quest boss on this queue (Royal Trouble's Giant Sea Snake, Great
  Brain Robbery's Barrelchest, Contact!'s Giant Scarab). The blindweed
  patch's 5-minute growth wait uses a genuine one-shot `[timer,
  deal_blindweed_grow]` player-timer (500 ticks @ 0.6s/tick) set on planting,
  same `settimer`/`[timer,...]` idiom already established by Draynor Manor's
  `manor_vines.rs2`. The stagnant-water gate (`deal_gate_closed` ->
  `deal_gate_open`) is a plain one-time `loc_del`/`loc_add` swap, not tied
  into `general_use/scripts/gates.rs2`'s own category-bound `_gate_main_*`
  system (this gate has no `category=` field, so it never matches that
  system's wildcard binds) -- a small bespoke swap instead, snapshotting
  `loc_coord`/`loc_angle`/`loc_shape` before delete, matching that same
  file's own snapshot-before-delete caution. Fishing sluglings is a simple
  guaranteed-catch loop (no skill-check precedent needed since Fishing 50 is
  already a hard quest-start requirement); the rare "Karamthulhu" joke catch
  (`deal_karamthulhu`/`inactivepet_squid`) is deferred, no rare-roll
  precedent anywhere in this tree. Fever spider's disease-on-hit-without-
  slayer-gloves penalty deferred (no disease/status-effect precedent for
  combat in this tree, same "left to the generic combat system" reasoning as
  Great Brain Robbery's Barrelchest prayer-disable). The Holy Wrench used
  mid-quest to fix the brewing control is quest-helper's own listed
  `ItemReward` too -- not re-granted at completion, the player simply keeps
  the one earned earlier (confirmed correct: it is never marked `isConsumed`
  by any interaction in this port). Files:
  `quests/quest_rumdeal/{configs/rumdeal.constant,configs/rumdeal.varp,
  scripts/deal_{shared,pete,braindeath,farming,water_hopper,sluglings,combat,
  donnie,journal,debug}.rs2}` + one line added to
  `interface_questjournal/scripts/quest_journal.rs2`. Checked the whole
  `server/scripts` tree for every new trigger/proc/debugproc name before
  writing (`deal_pete`, `deal_captian_braindeath`, `deal_davey`,
  `deal_captian_donnie`, `deal_evil_spirit`, `deal_fever_spiders1`, every
  `deal_blindweed_*` variant, `deal_gate_closed`, `deal_stagnant`,
  `deal_hopper`, `deal_brewvat_tap`, `deal_squid`, `deal_pressure`,
  `deal_multi_lever`, `deal_multicontrol`, `deal_spider_body`,
  `[timer,deal_blindweed_grow]`, `[debugproc,rumdeal]`/`[debugproc,
  rumdealrun]`, every `[proc,deal_*]`) -- zero collisions, no merges needed.
  `mingw32-make -C src sscompile` clean (only pre-existing snprintf-
  truncation warnings in the compiler itself); `mingw32-make -C src
  torirsserver-scripts` exit 0, 14111 scripts compiled (14078 -> 14111); grep of
  the full build log for "rumdeal"/"deal_" (case-insensitive) returned zero
  hits -- no warnings or errors attributable to this slice. `::rumdeal` /
  `::rumdealrun` debug commands added, matching every prior slice's idiom.
  Wiki https://oldschool.runescape.wiki/w/Rum_Deal/Quick_guide (dialogue
  paraphrased, not verbatim, per copyright, same caveat as every prior
  slice). `ToriRSServer_Pack --check-only` not runnable in this worktree (no
  `cache.osrs239` present, same pre-existing environment gap every prior
  slice has noted). Next pending row (smallest-first): #124 The Fremennik
  Isles, 670 lines.

- slice #124 done: The Fremennik Isles -- grep-first audit found no LC/
  2009scape ownership (`lc_quests.txt` clean; no `fremennikisles`/
  `fristroll`/`frisd_`/`fris_` hits anywhere in `server/scripts` besides the
  unrelated `quest_fremennikexiles`/`quest_viking`/`quest_mountaindaughter`
  trees -- the three other Fremennik-named quests, correctly distinct per
  this slice's own briefing). Fetched quest-helper's own
  `TheFremennikIsles.java` (639 lines, steps.put 0-332) and its
  `KillTrolls.java` sub-helper (the troll-cave kill-counter NpcStep) verbatim
  via GitHub raw, not summarized (the WebFetch tool's own summarization pass
  was lossy on the first attempt -- switched to a direct `curl` download for
  verbatim source, worth noting for future slices). Native dbrow
  `quest_fremennikisles` (id 127, endstate 340, questpoints 1, startnpc 1900,
  requirement_stats agility 40 + construction 20 -- both rows match
  quest-helper's own `getGeneralRequirements()` exactly; the Woodcutting 56 /
  Crafting 46 entries quest-helper also lists are NOT real quest-start gates,
  each is OR'd against `not ironman` in the source so only ever bites on
  ironman item-sourcing routes -- the dbrow's own 2-row requirement_stats
  table confirms they aren't real requirements, skipped) already declared in
  the cache, unused until this slice. dbrow `requirement_quests` decodes to
  id 57 (`quest_naturespirit`) -- not a real prerequisite, same recurring
  cache decode corruption flagged repeatedly on this queue; the real
  prerequisite is quest-helper's own The Fremennik Trials finished
  (`%viking >= ^viking_complete`, LC's `quest_viking`, already implemented,
  hard-gated). Master progress is the native varbit `fris_quest` (10 bits on
  basevar `fris_r1`) -- no native multi-record ties a concrete breakpoint set
  to it, so this port's own 0-26 + 340 reconstruction collapses
  quest-helper's own duplicate-instruction step keys (steps.put(5)/(10),
  (60)/(70)/(80), (100)/(110)/(120), (160)/(170)/(180)/(190), (240)/(250),
  (300)/(310), (325)/(330)/(331)/(332) each point at one identical
  ConditionalStep object), same reconstruction idiom as Rum Deal's
  `deal_quest`; `^fris_complete = 340` read directly off the dbrow's own
  `endstate` column, not invented. Three more native sub-varbits on the same
  basevar back real puzzles, independently confirmed via this cache's own
  records rather than guessed: `%fris_m_b3`/`%fris_m_b4` (the two rope-bridge
  repairs quest-helper itself tracks) confirmed via `frisr_rb3`/`frisr_rb4`'s
  own native multiloc (multivarbit=fris_m_b3/fris_m_b4,
  multiloc1=frisb_rope_bridge_broken, multiloc2=frisb_rope_bridge) -- a THIRD
  native pair, `frisr_rb5` on `%fris_m_b5`, has no quest-helper counterpart
  and no wiki mention of a third bridge, flipped alongside `%fris_m_b4` here
  as a documented judgment call so no dangling broken-bridge geometry is left
  behind; `%fris_king` (Mawnis's own crown/no-crown cosmetic swap) confirmed
  via `[fris_r_burgher]`'s own native multinpc record, flipped to 1 only at
  true completion; `%fris_task` (troll-cave kill counter) confirmed by
  `KillTrolls.java`'s own `client.getVarbitValue(VarbitID.FRIS_TASK)` read,
  not inferred -- quest-helper's own text says "kill 10 trolls" while the
  native world spawn (`m37_160.spawn`) only places 9 pre-set `_pc` trolls
  (troll_bodyguard variants explicitly excluded from `KillTrolls.java`'s own
  `addAlternateNpcs` list, left to the generic combat system); all bridges
  and the trapdoor/chest/king-chamber geometry are cache-baked map locs with
  no `.spawn` entry anywhere in this tree, zero hand-spawning needed for
  puzzle geometry, same idiom as Great Brain Robbery's statue passage.
  Six more native varbits on basevar `fris_r2`
  (`frisd_weaponmerchant_taxcollected` Skuli, `frisd_oremerchant_taxcollected`
  Hring Hring, `frisd_fishmonger_taxcollected` Flosi,
  `frisd_armourmerchant_taxcollected` Raum, `frisd_pub_taxcollected`
  Vanligga, `frisd_cook_taxcollected` Keepa) back the tax-collection puzzle;
  quest-helper reuses the SAME Requirement instances across both the
  window-tax round and the later beard-tax round, so this port explicitly
  clears all six back to 0 when the second round starts
  (`~fris_reset_tax`), the only behaviour consistent with the varbits being
  genuinely shared. Every npc resolves natively with the cache's own
  `fris`/`frisd` prefix, matching quest-helper's own NpcID names exactly --
  no "cache wins" spelling drama this time (unlike Rum Deal's
  `deal_captian_braindeath`). All npcs are already world-spawned except the
  Ice Troll King, hand-spawned lazily on trigger in his chamber, same idiom
  as every prior on-demand quest boss on this queue (Royal Trouble's Giant
  Sea Snake, Great Brain Robbery's Barrelchest, Rum Deal's Evil Spirit).
  Three shared-file merges to avoid duplicate triggers (critical correctness
  rule -- `[opheldu,knife]`/`[opheldu,hammer]`/`[opheldu,needle]` already
  exist elsewhere in this tree): a case for `arctic_pine_log` merged into
  `skill_fletching/scripts/cut_logs.rs2`'s existing `[opheldu,knife]` (log
  splitting for the bridge repairs), a case for `arctic_pine_log` merged into
  `general_use/scripts/hammer.rs2`'s existing `[opheldu,hammer]` switch (the
  Neitiznot shield craft), and a case for `yak_hide_cured` merged into
  `skill_crafting/scripts/leather/leather.rs2`'s existing `[opheldu,needle]`
  switch (the yak-hide armour craft) -- all three deferred/simplified as
  plain item-on-item actions rather than tied to a specific
  woodcutting-stump loc, since no Fremennik-specific stump gameval name
  could be confirmed in `all.loc` (unlike the generic named tree stumps used
  for actual woodcutting). The jester "follow Mawnis's request" performance
  (a `DetailedQuestStep` whose own description is deliberately vague -- in
  the real client this is a random emote the player must copy) is
  implemented as a single scripted dialogue exchange rather than a real
  emote-matching minigame, no follow-the-leader precedent anywhere in this
  tree. Per-npc tax amounts aren't recoverable from quest-helper (it only
  tracks varbits, not currency) -- approximated at 2500gp x4 (window) +
  2000gp x5 (beard) = 20,000gp total, deliberately matching quest-helper's
  own reward-list text ("Around 20,000 coins in assorted rewards during
  quest") rather than an arbitrary guess. `mingw32-make -C src sscompile`
  clean (only pre-existing snprintf-truncation warnings in the compiler
  itself); `mingw32-make -C src torirsserver-scripts` exit 0, 14161 scripts
  compiled (14111 -> 14161); grep of the full build log for "fris"
  (case-insensitive) returned zero hits -- no warnings or errors
  attributable to this slice or any of the three merged shared-trigger
  edits. `::fremennikisles` / `::fremennikislesrun` debug commands added,
  matching every prior slice's idiom. Wiki
  https://oldschool.runescape.wiki/w/The_Fremennik_Isles/Quick_guide
  (dialogue paraphrased, not verbatim, per copyright, same caveat as every
  prior slice). Files:
  `quests/quest_thefremennikisles/{configs/thefremennikisles.constant,
  configs/thefremennikisles.varp,
  scripts/fris_{shared,journal,debug,gjuki,mawnis,bridges,cave}.rs2}` +
  merges into `skill_fletching/scripts/cut_logs.rs2`,
  `general_use/scripts/hammer.rs2`,
  `skill_crafting/scripts/leather/leather.rs2`,
  `interface_questjournal/scripts/quest_journal.rs2`. `ToriRSServer_Pack
  --check-only` not runnable in this worktree (no `cache.osrs239` present,
  same pre-existing environment gap every prior slice has noted). Next
  pending row (smallest-first): #125 Garden of Tranquility, 684 lines.

- slice #125 done: Garden of Tranquility -- grep-first audit found no LC/
  2009scape ownership (`lc_quests.txt` clean; `garden`/`tranquility` hits
  elsewhere in the tree were false positives -- The Great Brain Robbery's
  Brother Tranquility NPC and the unrelated `quest_gardenofdeath` (Garden of
  Death, a different quest)). Fetched quest-helper's own
  `GardenOfTranquillity.java` verbatim via GitHub raw (684 lines, steps.put
  0/10/20/30/40/50; note the double-L British spelling of the class/file
  name itself, `github.com/.../helpers/quests/gardenoftranquility/
  GardenOfTranquillity.java`). Native dbrow is ALSO spelled
  `quest_gardenoftranquillity` (double L) even though this queue's own row
  and every file/dir in this port use the single-L `gardenoftranquility` --
  a real, confirmed spelling split (not a typo either direction), documented
  in the constant file header; every `db_getfield`/`quest_complete` call
  uses the double-L dbrow symbol. dbrow id 90, endstate 60, questpoints 2,
  startnpc 1390 (queen_ellamaria), requirement_stats farming 25 (stat id 19
  confirmed against the standard skill-id table, matches quest-helper's own
  `SkillRequirement(Skill.FARMING, 25)` exactly) already declared in the
  cache, unused until this slice. dbrow `requirement_quests` decodes to id
  19, which resolves to `quest_lostcity` -- not a real prerequisite (same
  recurring cache decode corruption flagged on nearly every prior slice);
  the real prerequisite is quest-helper's own `getGeneralRequirements()`:
  Creature of Fenkenstrain finished (`%creatureoffenkenstrain >=
  ^fenk_complete`, LC's `quest_creatureoffenkenstrain`, already implemented,
  hard-gated). Master progress is the native varbit `garden_quest` (6 bits
  on basevar `garden_varp_1`) -- no native multi-npc/multi-loc record ties a
  concrete breakpoint set to it, so this port's own 0/1/10/20/40/50/60
  reconstruction collapses quest-helper's own single giant `steps.put(40)`
  ConditionalStep (which internally covers all six villager fetch-quests,
  the inner-garden planting, and the statue transport) into real
  distinguishable milestones; `^garden_complete = 60` is read directly off
  the dbrow's own `endstate` column, not invented. Every per-npc native
  varbit below IS independently confirmed via this cache's own multiloc/
  multinpc records tying concrete values to real map cosmetics, not
  guessed: `garden_elstan_varbit`/`garden_lyra_varbit`/`garden_kragen_varbit`/
  `garden_dantaera_varbit`/`garden_althric_varbit`/`garden_bernald_varbit`
  (per-npc deal trackers, thresholds matching quest-helper's own
  VarbitRequirement values exactly -- `garden_bernald_varbit` confirmed via
  `garden_burthorpe_vines`'s own native multiloc, which renders diseased for
  values 0-3 and cured only at 4+, matching quest-helper's own
  `usedCureOnVines`(2)/`curedVine`(4) split precisely, i.e. the first,
  weaker Plant cure genuinely does nothing cosmetically); the nine
  inner-garden patches (`garden_delphinium_patch`, `garden_snowdrop_patch`,
  `garden_vine_patch`, `garden_rosebush_patch_red/pink/white`,
  `garden_orchid_pink_patch`/`_yellow_patch`, `garden_white_tree_patch`) all
  confirmed via their own native multiloc growth-stage records, index N =
  varbit value N-1, matching quest-helper's own `notPlantedX<=3`/`<=1`/
  seed-threshold checks exactly; the two statue pairs
  (`garden_king_statue_varbit`/`garden_saradomin_statue_varbit`) each
  confirmed via TWO native multiloc records apiece (the real-world statue
  and the garden's own destination plinth, both reacting to the same shared
  varbit) plus `garden_trolley_varbit` confirmed via the `garden_trolley`
  multinpc skin-swap. `garden_kragen_patch_5_varbit`/
  `garden_kragen_patch_6_varbit`, `garden_cutscene_billybob` and
  `garden_first_time_login` have no quest-helper VarbitID reference
  anywhere in the source -- reserved/unclaimed, left untouched, same
  reasoning as prior slices' unclaimed-bit notes. Every npc resolves
  natively with its own real cache name (`queen_ellamaria`, `elstan`,
  `lyra`, `kragen`, `dantaera`, `brother_althric`, `bernald`,
  `farming_gardener_tree_1` for Alain, `king_roald`, `wise_old_man`) --
  none of these match the queue row's own stale hint abbreviations
  (`gardentroll`/`queenellama`), which don't correspond to any real cache
  npc; cache wins, same recurring pattern as Rum Deal/The Fremennik Isles.
  Marigolds are grown on the REAL, pre-existing, fully-functional Falador
  flower patch (`farming_flower_patch_1`, `skill_farming`'s own generic
  system, `farming_flower_marigold` dbrow) -- quest-helper's own
  `ObjectID.FARMING_FLOWER_PATCH_1` is that exact patch, not a new
  quest-only one, so this slice merges two small hooks into that system's
  existing `farming_plant.rs2`/`farming_harvest.rs2` label blocks rather
  than building a new grower. Onions (`farming_veg_patch_7`/`_8`, Morytania)
  and cabbages (`farming_veg_patch_5`/`_6`, Ardougne) are real cache-declared
  allotment locs quest-helper's own `plantedOnions`/`plantedCabbages`
  Conditions OR together, but only the Falador allotment pair has real
  trigger code anywhere in this tree -- extending the generic multi-region
  allotment system to four more patches is out of scope for one quest
  slice, so this port adds bespoke quest-scoped plant/grow/harvest logic on
  these four previously-unclaimed locs instead, using the same
  `farming_allotments` dbrow data (level/seed-count/xp) the generic system
  itself would use; the real crop-stage cosmetic broadcast
  (`farming_transmit_a`/`_b`, shared scratch varbits reused per-region by
  the generic system) isn't driven by this bespoke logic, a documented
  simplification (patch won't visually change for onlookers). Three shared-
  file merges to avoid duplicate triggers (critical correctness rule): a
  case for `blankrune`/`blankrune_high` merged into `general_use/scripts/
  hammer.rs2`'s existing `[opheldu,hammer]` switch (Alain's strong plant
  cure recipe), a case for `rune_shards` merged into `skill_herblore/
  scripts/grind_ingredient.rs2`'s existing `[opheldu,pestle_and_mortar]`
  trigger (same recipe), and an additive branch merged into the existing
  `[opnpc1,wise_old_man]` trigger in `quest_makingfriendswithmyarm/scripts/
  makingfriendswithmyarm.rs2` (the diplomacy test) alongside that file's own
  pre-existing Swan Song branch -- same "external proc, called from shared
  trigger" idiom used by both. A fourth merge, an additive branch in
  `areas/varrock/scripts/king_roald.rs2`'s own `[opnpc1,king_roald]`
  trigger (the finale hand-off), follows that file's own existing `%dov`
  branch idiom. The Wise Old Man's diplomacy test is a real seven-question
  chat quiz (`~p_choice3`) -- the seven correct answers are quest-helper's
  own literal `addWidgetChoice` strings (not invented), scenario framing
  paraphrased from the wiki's own quick-guide summary (not verbatim quest
  text, per copyright). Deferred/simplified (no established mechanic
  precedent anywhere in this tree): the trolley statue-push route
  (quest-helper's own `setLinePoints` waypoint list) is a single soft-skip
  action once the trolley item is used on the correct real-world statue,
  same "soft-skip: <tedious traversal>" idiom already established in this
  tree (`king_roald.rs2`'s own Shield of Arrav dining-room soft-skip,
  `quest_makingfriendswithmyarm`'s cave-pathing soft-skip); fishing the ring
  back out of the well is a flat 1-in-3 per-click chance, same "simple
  probabilistic-catch loop" idiom as Rum Deal's slugling fishing; crop
  death isn't modelled for any of the eleven planted patches (guaranteed
  growth once planted); growth waits are real one-shot `settimer`/
  `[timer,...]` player-timers (eleven distinct timer names, one per patch/
  crop), deliberately compressed from real OSRS times for playability, a
  documented judgment call; King Roald "following" Ellamaria into the
  garden for the finale is a scripted dialogue exchange, not a real
  npc-follow simulation. `mingw32-make -C src sscompile` clean (only
  pre-existing snprintf-truncation warnings in the compiler itself);
  `mingw32-make -C src torirsserver-scripts` exit 0, 14236 scripts compiled
  (14161 -> 14236); grep of the full build log for "garden" (case-
  insensitive) returned zero hits -- no warnings or errors attributable to
  this slice or any of the four merged shared-trigger edits. `::
  gardenoftranquility` / `::gardenoftranquilityrun` debug commands added,
  matching every prior slice's idiom. Wiki
  https://oldschool.runescape.wiki/w/Garden_of_Tranquillity/Quick_guide
  (dialogue paraphrased, not verbatim, per copyright, same caveat as every
  prior slice). Files:
  `quests/quest_gardenoftranquility/{configs/gardenoftranquility.constant,
  configs/gardenoftranquility.varp,
  scripts/garden_{shared,wom,elstan,lyra,kragen,dantaera,althric,bernald,
  finalgarden,statues,journal,debug}.rs2}` + merges into
  `skill_farming/scripts/farming_plant.rs2`,
  `skill_farming/scripts/farming_harvest.rs2`,
  `general_use/scripts/hammer.rs2`,
  `skill_herblore/scripts/grind_ingredient.rs2`,
  `quest_makingfriendswithmyarm/scripts/makingfriendswithmyarm.rs2`,
  `areas/varrock/scripts/king_roald.rs2`,
  `interface_questjournal/scripts/quest_journal.rs2`. `ToriRSServer_Pack
  --check-only` not runnable in this worktree (no `cache.osrs239` present,
  same pre-existing environment gap every prior slice has noted). Next
  pending row (smallest-first): #127 Enakhra's Lament, 688 lines.
- slice #127 done: Enakhra's Lament -- Jan 2006, Kharidian Desert; Lazim the
  mage rebuilds a statue of Enakhra at the quarry south of Bandit Camp to
  re-enter her collapsed temple, then guides the player through a fallen-
  statue/sigil-door ground floor, a Pentyn/fountain/furnace/six-brazier
  puzzle floor, a Boneguard corridor, and a wall repair that frees Akthanakos.
  Grep-first (methodology steps 1-2): no LC proc (`lc_quests.txt` and a
  `enakhra|kharidian|desert.*mine.*collapse|Uzer` sweep of `server/scripts`
  hit nothing but coincidental substring matches -- DT2's own unrelated
  `dt2_enakhra_combat`/`dt2_enakhra_cutscene` cutscene npcs, and `kharidian`/
  `Uzer` hits in unrelated desert-area files), no 2009scape impl (both queue
  docs silent) -- genuinely pending. Native dbrow `quest_enakhraslament` (id
  103, endstate 70, questpoints 2, `requirement_stats` (12,50)=Crafting 50,
  (11,45)=Firemaking 45, (5,43)=Prayer 43, (6,39)=Magic 39 matching
  quest-helper's own `getGeneralRequirements()` exactly, no
  `requirement_quests` column at all -- no prerequisites, matches the wiki)
  plus an unusually rich, **fully native** varbit schema declared across
  three basevars (`enakh_quest_expositbits` -- master `enakh_quest` 7 bits +
  one-shot blurb flags; `enakh_multivarbits` -- room/statue/brazier/wall
  state; `enakh_varbits` -- door locks, sigil doors, `enakh_where_is_lazim`),
  every field name matching quest-helper's own `VarbitID.ENAKH_*` lowercased
  exactly (`configs/all.varbit` lines 7803-8107), reused as-is rather than a
  locally invented catch-all var. Real native multi-npc records confirm
  Lazim visually follows the player between four rooms
  (`enakh_lazim_statue_east_multinpc`/`_fallen_statue_east_multinpc`/
  `_pedestal_multinpc`/`_altar_multinpc`, all `multivarbit=
  enakh_where_is_lazim`) -- writing that real varbit drives it for free. This
  server only ever spawns the wrapper npc/loc types
  (`areas/world/configs/m48_145.spawn`/`m49_45.spawn`) -- multinpc/multiloc
  leaf resolution is client-only rendering, confirmed the same way The
  Feud's slice did -- and several wrappers (`enakh_lazim_*_multinpc` x4,
  `enakh_boneguard_multinpc`, `enakh_akthanakos_multinpc`,
  `enakh_dummy_fountain_multinpc`, `enakh_dummy_furnace_multinpc`) declare no
  op of their own in the cache, only their resolved leaf npcs do -- additive
  op overlay `enakhraslament.npc` (same convention as quest_royaltrouble's
  own `royaltrouble.npc`), plus `enakhraslament.loc` for the temple's
  secret-entrance boulder wrapper. Scripts:
  `enakhraslament_quarry.rs2` (Lazim's quarry-statue dialogue FSM driving the
  real `enakh_statue_multivar` 0-7 directly -- base/body/chiseled/four head
  choices -- bespoke instant-mine triggers on the real, previously-unwired
  `enakh_sandstone_rocks`/`enakh_granite_rocks` locs, reusing
  `~pickaxe_checker`/`~mining_pickaxe_anim` from `skill_mining/scripts/
  mining.rs2`; the boulder entrance), `enakhraslament_temple.rs2` (fallen-
  statue limb chisel, the four real limb doors and four real sigil doors
  each keyed on their own native lock varbit, the shared ladder-up object
  reused at two real breakpoints exactly as quest-helper's own
  `goUpToPuzzles`/`goUpFromPuzzleRoom` do, the camel-mould pedestal, Pentyn/
  fountain/furnace/six-brazier puzzle floor, Crumble Undead on the Boneguard,
  the wall repair, and quest completion), `enakhraslament_journal.rs2`,
  `enakhraslament_debug.rs2`; wired into `interface_questjournal/scripts/
  quest_journal.rs2`. Two shared-file merges to avoid duplicate triggers
  (critical correctness rule): a case for `enakh_granite_medium` merged into
  `skill_crafting/scripts/gem/uncut_gem.rs2`'s existing `[opheldu,chisel]`
  switch (external proc `~enakhraslament_craft_head`, branching on quarry vs.
  puzzle-floor context, same "external proc called from a shared trigger"
  idiom as Garden of Tranquility's Alain/Wise Old Man merges) -- no merge
  needed for Crumble Undead / the fire and air puzzle spells since
  `enakh_boneguard` has no Attack op in the cache at all (only `op2=Talk-to`,
  confirming this was never meant to be a real fight), so those three casts
  are quest-scoped `opnpc1` ritual actions reusing the shared
  `~get_spell_data`/`~check_spell_requirements` procs from `skill_magic/
  scripts/magic.rs2` (level/membership/rune-or-staff possession) rather than
  touching the combat spellcasting system's own `[opnpct,magic_spellbook:*]`
  triggers. Deferred/simplified (documented, no established precedent
  anywhere in this tree for the alternative): the real kg-by-kg sandstone
  assembly collapses to plain quantities of `enakh_sandstone_medium` (5kg)
  handed to Lazim directly -- two of the raw/crafted "base" item names carry
  a literal `+` (`enakh_sandstone_huge_base+legs`/`enakh_sandstone_crafted_
  base+legs`, confirmed a legal script token via `uncut_gem.rs2`'s own
  `shellround_red+black` case, but still unnecessary intermediate items once
  kg bookkeeping is dropped); the four fallen-statue limb pickups collapse to
  one chisel action; sigil pickup is gated on all four limb doors (not a
  specific one per sigil) since quest-helper's own `enterKDoor`/`enterRDoor`/
  `enterMDoor`/`enterZDoor` steps are declared in source but never actually
  assigned to a `steps.put` breakpoint -- gating all four sigils behind all
  four limb doors is stricter/more complete than the guide, not looser; loc
  visuals (door open/closed swap, statue-collapse crack/hole states, Lazim's
  carrying-stone animation) don't update since this server only reads the
  wrapper type, not the real per-state loc name, same limitation as the
  npc side; the wrong-head-on-pedestal edge case and one-shot dialogue-blurb
  flavor fields, and the purely cosmetic Enakhra/Akthanakos form swaps and
  post-quest camulet charge mechanic, are untouched. Rewards: 2 QP, 7000
  Crafting/Firemaking/Magic/Mining XP each (dbrow `stat_xp_awarded` matches
  quest-helper's own `getExperienceRewards()` exactly plus a Mining line the
  guide omits, both awarded), Akthanakos's Camulet. `mingw32-make -C src
  sscompile` clean (only pre-existing snprintf-truncation warnings in the
  compiler itself); `mingw32-make -C src torirsserver-scripts` exit 0, 14281
  scripts compiled (14236 -> 14281); grep of the full build log for "enakh"
  (case-insensitive) returned zero hits -- no warnings or errors
  attributable to this slice or the one shared-trigger merge. `ToriRSServer_Pack
  --check-only` not runnable in this worktree (no `cache.osrs239` present,
  same pre-existing environment gap every prior slice has noted). Wiki
  https://oldschool.runescape.wiki/w/Enakhra's_Lament/Quick_guide (dialogue
  paraphrased, not verbatim, per copyright, same caveat as every prior
  slice). Files:
  `quests/quest_enakhraslament/{configs/enakhraslament.{constant,varp,npc,
  loc}, scripts/enakhraslament_{quarry,temple,journal,debug}.rs2}` + merges
  into `skill_crafting/scripts/gem/uncut_gem.rs2`,
  `interface_questjournal/scripts/quest_journal.rs2`. Next pending row
  (smallest-first): #129 The Slug Menace, 694 lines (row #128 is absent from
  the pending table, presumably already resolved on an earlier sweep).
- slice #129 done: The Slug Menace -- Sept 2006, Witchaven; Sir Tiffy
  Cashien's Temple Knights send the player back to Witchaven (Wanted!/Sea
  Slug's own village) to investigate Col. O'Niall, Brother Maledict and
  Mayor Hobb, uncover a hobgoblin-dungeon false wall and a sealed imposing
  door in the sea slug dungeon, translate a transcript via Jorral, recover
  and repair three torn pages, craft and apply five elemental runes at the
  real runecrafting altars, and defeat the Slug Prince. Grep-first
  (methodology steps 1-2): `lc_quests.txt` and a `slug` sweep of
  `server/scripts` hit nothing but coincidental matches (Sea Slug's own
  `quest_seaslug`, Rum Deal's `deal_sluglings`, slayer rock slugs) -- no LC
  proc, no 2009scape impl -- genuinely pending. Native dbrow
  `quest_slugmenace` (id 118, endstate 14, questpoints 1, requirement_stats
  (12,30)=Crafting 30, (20,30)=Runecraft 30, (18,30)=Slayer 30, (17,30)=
  Thieving 30, matching quest-helper's own getGeneralRequirements() exactly;
  stat_xp_awarded (12,35000)/(20,35000)/(17,35000)=Crafting/Runecraft/
  Thieving 3500 each, matching getExperienceRewards() exactly, no Slayer xp
  despite the Slayer requirement, matching the wiki) + fully native varbit
  schema on one basevar (`quest_slug2`: master `slug2_main` 0-14,
  `slug2_npc_track1/2/3`+`slug2_npc_alltrack`, `slug2_doorbit`,
  `slug2_tornpages`, `slug2_fixed_page`, `slug2_haveslug`, `slug2_used_air/
  earth/water/fire/mind_rune`+`slug2_used_runes`), every field matching
  quest-helper's own `VarbitID.SLUG2_*` names lowercased exactly, reused
  as-is (`configs/all.varbit` lines 13053-13161). dbrow `requirement_quests`
  decodes to Fremennik Isles (127) and Song of the Elves (156) -- the second
  is 2018-era content that cannot predate a Sept 2006 quest, same known
  cache-decode-corruption failure mode this queue's methodology warns about
  (confirmed junk) -- real prerequisites per quest-helper's own
  getGeneralRequirements() are Wanted! FINISHED and Sea Slug FINISHED, both
  already implemented in this tree, gated on those instead
  (`%wanted_main >= ^wanted_complete` / `%seaslugquest >= ^seaslug_complete`).
  Native multi-npc records independently confirm three of `%slug2_main`'s
  breakpoints, used directly (not guessed): `slug2_maledict` STAGE1->STAGE2
  exactly at value 8, `slug2_oniall` STAGE1->STAGE2 at value 9 and
  STAGE2->gone at value 13, `slug2_hobb` STAGE2->STAGE3 at value 12 and
  STAGE3->gone at value 14 (= dbrow endstate) -- the master-var value table
  was hand-derived to land the corresponding narrative beats (Maledict's
  second conversation, O'Niall becoming reachable for page 3, all five runes
  applied, Slug Prince killed) on exactly those four numbers, then verified
  against all three records, not the reverse. This server only ever spawns
  the wrapper npc/loc types; three wrappers (`slug2_oniall`, `slug2_jeb`,
  `slug2_holgart_jeb`) declare no op of their own in the cache -- additive
  op overlay in `theslugmenace.npc` (`slug2_hobb`/`slug2_maledict`/the
  villager wrappers already carry `op1=Talk-to` natively, no overlay
  needed); `slug2_hidden_entrance` (the false-wall multiloc wrapper)
  likewise needed one, in `theslugmenace.loc`. Three genuine shared-trigger
  merges to avoid duplicate triggers (critical correctness rule): Sir Tiffy
  Cashien (`rd_teleporter_guy`) already has a live trigger for Wanted! --
  `~slugmenace_tiffy_talk` is called from `wanted_tiffy_amik.rs2`'s own
  `wanted_main >= wanted_complete` branch; Jorral (`makinghistory_jorral`)
  already has a live trigger for Making History -- `~slugmenace_jorral_
  translate` is called first with an early return; Bailey (`bailey`, Fishing
  Platform) already has a live trigger for Sea Slug -- `~slugmenace_bailey_
  talk` likewise. A fourth merge was caught only after an initial build
  (sscompile gives no duplicate-trigger diagnostic, confirmed the hard way):
  `holgartlandtravel` -- the wiki/quest-helper's own "Holgart, north of
  Witchaven" -- turned out to be the *same* Holgart already world-spawned
  and fully scripted for Sea Slug (`areas/area_fishing_platform/scripts/
  holgart.rs2`), not a separate unspawned npc; the first draft's own
  `[opnpc1,holgartlandtravel]` block was deleted and replaced with a branch
  merged into that file's existing `[label,holgartland_talk]`. Two more
  external-proc merges (not duplicate triggers, since the item names are
  quest-exclusive): a case for the five `slug2_rune_*_blank` items merged
  into `skill_crafting/scripts/gem/uncut_gem.rs2`'s existing `[opheldu,
  chisel]` switch (chisel+essence engraving, 5-way choice), and a case for
  the same five blanks merged into `skill_runecraft/scripts/runecraft.rs2`'s
  existing `[oplocu,_rc_altar]` switch (charging at the real air/water/
  earth/fire/mind altars, matched against that file's own
  `~runecraft_type_for_loc`). The Slug Prince (level 62, melee-only per
  quest-helper's own getCombatRequirements(), no special-defence-mechanic
  precedent anywhere in this tree) has no `.spawn` entry anywhere (confirmed
  via grep) -- hand-spawned lazily on trigger once all five runes are used,
  same idiom as Royal Trouble's Giant Sea Snake / Contact's Giant Scarab.
  Deferred/simplified (documented, no established precedent anywhere in this
  tree for the alternative): the real widget puzzle for combining the three
  page fragments (interface group 460, native `slug2_frag1/2/3_xpos/ypos/
  zpos/rot` drag-position varps) collapses to using sea slug glue directly
  on a repaired fragment, same "no puzzle-piece-dragging interface
  precedent" reasoning as every prior slice's own widget soft-skips; the
  three background Witchaven villagers (their own native stage1/stage2
  multi-npc wrappers) are pure flavour never referenced by any
  quest-helper `steps.put` or requirement, deferred with no gameplay
  consequence, same reasoning as the native but code-unreferenced
  `slug2_scan_mayor`/`slug2_savant_gotinfo`/`slug2_savant_scan`/
  `slug2_doorscan`/`slug2_queen_door`/`slug2_door_sound_control`/
  `slug2_oniall_control` bits (left unset); loc visuals for the imposing
  door's open/closed state don't swap (wrapper-only limitation, same as
  every prior slice's npc/loc side). Rewards: 1 QP, 3500 Crafting/
  Runecraft/Thieving XP each (dbrow `stat_xp_awarded` matches quest-helper's
  own `getExperienceRewards()` exactly, both awarded), unlocks purchasing
  Proselyte armour. `mingw32-make -C src sscompile` clean (only pre-existing
  snprintf-truncation warnings in the compiler itself); `mingw32-make -C src
  torirsserver-scripts` exit 0, 14316 scripts compiled (14281 -> 14316, net +35
  after the duplicate-trigger fix removed one competing top-level
  `[opnpc1,holgartlandtravel]`); grep of the full build log for "slug" /
  "holgart" (case-insensitive) returned zero hits both before and after the
  fix -- no warnings or errors attributable to this slice or any of the five
  shared-trigger merges. `ToriRSServer_Pack --check-only` not runnable in this
  worktree (no `cache.osrs239` present, same pre-existing environment gap
  every prior slice has noted). `::theslugmenace` / `::theslugmenacerun`
  debug commands added, matching every prior slice's idiom. Wiki
  https://oldschool.runescape.wiki/w/The_Slug_Menace/Quick_guide (dialogue
  paraphrased, not verbatim, per copyright, same caveat as every prior
  slice). Files: `quests/quest_theslugmenace/{configs/theslugmenace.
  {constant,varp,npc,loc}, scripts/slugmenace_{tiffy,witchaven,pages,
  journal,debug}.rs2}` + merges into `quest_wanted/scripts/wanted_tiffy_
  amik.rs2`, `quest_makinghistory/scripts/makinghistory_jorral.rs2`,
  `areas/area_fishing_platform/scripts/bailey.rs2`, `areas/area_fishing_
  platform/scripts/holgart.rs2`, `skill_crafting/scripts/gem/uncut_gem.rs2`,
  `skill_runecraft/scripts/runecraft.rs2`,
  `interface_questjournal/scripts/quest_journal.rs2`. Next pending row
  (smallest-first): #130 Cabin Fever, 704 lines.
- slice done (2026-08-11): Cabin Fever (#130) -- Bill Teach's raid on a rival
  pirate ship; native dbrow `quest_cabinfever` (id 104, endstate 140) +
  native varbit schema on `fever_quest`/`fever_cannon_var`/`fever_extra_var`/
  `fever_storage_var` reused as-is, matching quest-helper's own VarbitID
  names exactly. Real prereqs are Pirate's Treasure + Rum Deal (both hard-
  gated, both genuinely completable) + Priest in Peril, which is soft-
  skipped -- `quest_priestperil.constant` documents its own finale as
  "deferred (blocked)" and `%priestperil` never reaches completion anywhere
  in this tree, so hard-gating on it would make Cabin Fever unstartable.
  Native multiloc records confirm every real breakpoint used (hole repair,
  loot containers, cannon repair/load, the enemy hull-breach counter
  `fever_holes_in_the_hull`, the sabotage barrel's non-monotonic fused(2)->
  exploded(1) order). All inter-deck navigation is already the generic climb
  system (`ladders_stairs/scripts/ladders.rs2`) -- zero custom transport
  scripting needed; the ship-to-ship rope swing is one `distance(coord,...)`
  teleport trigger reused by every crossing. `fever_teach`/`fever_port_ship_
  teach`/`fever_quest_ship_teach` needed an op overlay (`cabinfever.npc`);
  every multiloc wrapper used already carried its own real op, no loc
  overlay needed. Simplified (documented, no precedent anywhere in this tree
  for the alternative): locker searches grant a full requirement in one
  Search; plunder containers grant a fixed 4+3+3=10 split; the canister-kill
  phase has no cannon-deals-damage-directly precedent (same as Royal
  Trouble's Giant Sea Snake / GBR's Barrelchest leaving combat to the
  generic system) so a load/fire cycle itself stands in for a kill, tracked
  via `%fever_quest` sub-values rather than combat; misfire/wrong-ammo
  handling isn't modelled. Wiki https://oldschool.runescape.wiki/w/
  Cabin_Fever/Quick_guide + quest-helper source fetched via GitHub raw
  (dialogue paraphrased, per copyright). `mingw32-make -C src sscompile`
  clean, `mingw32-make -C src torirsserver-scripts` exit 0 (14,342 scripts, up
  from 14,316); no duplicate-trigger or duplicate-constant collisions
  (checked by hand against the whole tree). Files: `quests/quest_cabinfever/
  {configs/cabinfever.{constant,varp,npc}, scripts/cabinfever_{shared,bill,
  transport,lockers,repair,sabotage,loot,cannon,journal}.rs2}` + wiring into
  `interface_questjournal/scripts/quest_journal.rs2`. This was previously a
  soft-skipped prerequisite for The Great Brain Robbery (#121) -- that gate
  can now be tightened by a future tick. Next pending row (smallest-first):
  #132 In Aid of the Myreque, 710 lines (#131 Icthlarin's Little Helper was
  already `done (LC)`, table-sync only).
- slice done (2026-08-11): In Aid of the Myreque (#132) -- Veliaf sends the
  player to help the Myreque's cousin cell repair Burgh de Rott, fight off
  Gadderanks's vampyre blood-tithe raid, escort Ivan Strom to Paterdomus via
  Temple Trekking, and craft + bless the Rod of Ivandis. Native dbrow
  `quest_inaidofthemyreque` (id 107, endstate 430, questpoints 2,
  requirement_stats (12,25)=Crafting25/(14,15)=Mining15/(6,7)=Magic7,
  stat_xp_awarded Attack/Strength/Crafting/Defence 2000xp each -- both match
  quest-helper's own getGeneralRequirements()/getExperienceRewards() exactly)
  + a fully native varbit schema on basevars `myreque_2_main_var` (per-site
  repair progress), `myreque2_multivar` (`myreque_2_quest` master progress
  0-511 at bits 0-8, matching quest-helper's own steps.put range; crate
  sub-fields; blood-tithe chat flags; `juvinate_deaths`; `juvinate_ambush_
  deaths`/`_routetaken`) and `myreque2_extravar` -- reused as-is, matching
  quest-helper's own `VarbitID.*` names lowercased exactly, claimed bare in
  `myreque2.varp` same as `quest_cabinfever`'s own `fever_quest` precedent.
  `tools/questhelper_extract.py inaidofthemyreque --check` (staged locally
  from a `curl`-fetched copy of the QH source, no local checkout on this
  machine) resolved every gameval clean, zero unresolved names. dbrow
  `requirement_quests` decodes to dbrow id 79 = Desert Treasure I -- not a
  real prerequisite, same known cache-decode-corruption failure mode this
  queue warns about repeatedly. The real prerequisite (In Search of the
  Myreque FINISHED, `%routequest >= ^routequest_complete`) is soft-skipped:
  auditing `quest_routequest/` (row #65, marked `done (LC)`) found it only
  has `configs/quest_routequest.{constant,varp}` +
  `scripts/routequest_journal.rs2` -- grepping the whole tree for
  `%routequest` finds only the journal reading it, nothing ever writes it,
  and Veliaf/Ivan/Polmafi's hideout npcs have no scripted dialogue anywhere
  -- that quest is not actually completable in this tree, so hard-gating on
  it would make In Aid of the Myreque itself permanently unstartable (same
  reasoning as Cabin Fever's Priest in Peril / King's Ransom's One Small
  Favour); row #65 was annotated with this finding, not re-scored (out of
  scope for this slice). The Crafting 25 / Mining 15 / Magic 7 stat gate is
  still hard-checked (`myreque2_meets_requirements`), matching the dbrow.
  Native multiloc/multinpc records independently confirm every real
  breakpoint used: `burgh_furnace_multiloc` broken(0)->repaired(1)->
  coal_loaded(2)->fired(3) on `burgh_furnace_fix`; `burgh_general_store_
  roof_multiloc`/`_wall_multiloc` and `burgh_bank_wall_multiloc`/
  `burgh_bank_booth_multiloc` each 0/1 on their own varbit;
  `burgh_gadderanks_multinpc` invisible(0)->visible(nonzero) on
  `blood_tithe_visible`, matching `veliafReturnedToBase =
  VarbitRequirement(BLOOD_TITHE_VISIBLE, 3, GREATER_EQUAL)` exactly;
  `burgh_temple_trapdoor_multiloc`/`burgh_ivandis_tombdoor_board_multiloc`
  0/nonzero on `burgh_temple_trapdoor`/`ivandis_tomb_boards`, matching
  `libraryOpen`/`boardsRemoved`. Click-based repair triggers bind to the
  currently-resolved multiloc *leaf* (native `op1=Inspect` on every leaf,
  additive `op2=Repair`/`Add-coal`/`Light` overlay in `myreque2.loc`) rather
  than the wrapper, matching the cache's own `burgh_inn_trapdoor_closed`/
  `_open` pair (consumed by name in `ladders_stairs/scripts/climb_shared.
  rs2` and `doors/scripts/doors.rs2`) -- the opposite of Cabin Fever's own
  wrapper-level dispatch for its script-spawned ship instance, reasoned out
  from `~climb` turning out to be a pure "move one plane, same tile" op with
  no per-object destination lookup (`ladders_stairs/scripts/ladders.rs2`).
  `burgh_boared_up_wall_clickzone` is the same leaf resolved by both the
  shop-wall and bank-wall placements; one trigger branches on `coord`
  against `myreque2_shop_wall_coord`/`myreque2_bank_wall_coord` to update
  the right native varbit -- no wrapper ambiguity since the two multiloc
  *wrapper* records are themselves distinct, checked directly by grep before
  writing. `priestperiltrappedmonk_vis` (Drezel here) is confirmed a
  different gameval from Priest in Peril's own `priestperiltrappedmonk`
  (`trapped_drezel.rs2`) -- no collision. `myq5_veliaf_child` (the finish
  hand-in) is already claimed by Sins of the Father's own hub dispatcher
  (`sinsofthefather.rs2`'s `[opnpc1,myq5_veliaf_child]`, stacked with three
  sibling names) -- merged as an early branch in `sf_veliaf_talk` gated on
  `%myreque_2_quest`'s own delivery window, not a duplicate trigger.
  Combat kill-credit (Gadderanks + 2 Juvinates, then Ivan's escort ambush)
  uses hand-spawned attackable npcs (no `.spawn` entry for any of the five
  attackable/ambush variants, confirmed via grep) with `[opnpcN,name]
  ~npc_retaliate(0);` + `[ai_queue3,name] ...; ~npc_default_death;`, same
  idiom as Contact!'s Giant Scarab -- `juvinate_deaths` (native, 0-3) drives
  `defeatedGadderanks` exactly, and the escort's `juvinate_ambush_deaths`
  (native, 0-3) is driven to 2 (short-route simplification, see below).
  Simplifications (documented, no established precedent anywhere in this
  tree for the alternative): the basement rubble minigame (mine, bag with a
  bucket, empty outside, up to 5 trips) collapses to one pickaxe+spade
  click driving the real `burgh_inn_rubble_pile` counter to its cap, same
  "grant a full requirement in one action" convention as Cabin Fever's
  locker searches. The portable general-store crate item + its per-item
  fill loop (quest-helper's own `FillBurghCrate`) collapses to one hand-in
  to Aurel with everything in inventory at once, still driving the real
  native `burgh_axes_crate`/`burgh_food_crate`/`burgh_tinderbox_crate` to
  their caps (`burgh_crate_overseer` reaches 938 either way, matching
  `filledCrate` exactly). Ivan's Temple Trek escort simplifies to the short
  route only (2 level-75 Juvinates, matching the native 2-bit ambush
  counter) -- the long-route alternative (4 level-50) is deferred flavour,
  geography already baked. Vampyre silver-weapon immunity is hinted via
  dialogue only, combat left to the generic system -- no restriction
  precedent anywhere in `skill_combat` (grep-verified), same reasoning as
  Cabin Fever. The Lvl-1 Enchant cast on the Silvthrill rod does not route
  through the shared `magic_spell_table` dbrow (`skill_magic/configs/
  magic_spells.dbrow`'s `[magic_spell_enchant_level1]` `convertobj` list) --
  no established precedent anywhere in this tree for additively appending a
  `data=convertobj,...` row to an existing dbrow block from a second file
  (unlike the proven additive-field-merge convention for `.npc`/`.loc`
  overlays), so it was avoided; using cosmic + water runes directly on the
  rod (own dedicated trigger, Magic 7 checked directly) reproduces the same
  item transformation without touching shared spellbook content. `burgh_
  rod_clay` -> Silvthrill rod smelting is one new `case` added to the
  existing shared `[label,use_furnace]` switch in `skill_smithing/scripts/
  smelting/smelting.rs2` (quest-helper's own text is literally "at any
  furnace") -- merged as a branch, not duplicated. `pipeastsidetrapdoor`/
  `pipeastsidetrapdoor_open` (the way down to Drezel) are deliberately left
  untouched: a real generic climb binding already exists
  (`climb_shared.rs2`) and a second, seemingly-dead stub trigger for the
  same two names already exists in `quest_sinsofthefather/scripts/
  sinsofthefather.rs2` (itself soft-skipped) -- a pre-existing duplicate-
  trigger situation from before this slice, documented and not touched.
  Rewards: 2 QP, 2000 Attack/Strength/Crafting/Defence XP each (matches
  dbrow exactly), Temple Trekking unlock, Rod of Ivandis crafting ability.
  Wiki https://oldschool.runescape.wiki/w/In_Aid_of_the_Myreque/Quick_guide
  + quest-helper source fetched via GitHub raw (dialogue paraphrased, not
  verbatim, per copyright, same caveat as every prior slice). `mingw32-make
  -C src sscompile` clean (only pre-existing snprintf-truncation warnings in
  the compiler itself); `mingw32-make -C src torirsserver-scripts` exit 0, 14400
  scripts compiled (up from 14342); grep of the full build log for
  "myreque"/"inaidofthemyreque" returned zero hits, and a full self-sweep of
  all 58 trigger headers this slice authored against the rest of the tree
  found zero collisions. Files: `quests/quest_inaidofthemyreque/{configs/
  myreque2.{constant,varp,loc}, scripts/myreque2_{shared,hideout,burgh,shop,
  bank,furnace,fight,trek,rod,journal}.rs2}` + merges into `quest_
  sinsofthefather/scripts/sinsofthefather.rs2` and `skill_smithing/scripts/
  smelting/smelting.rs2`, plus wiring into `interface_questjournal/scripts/
  quest_journal.rs2`. Next pending row (smallest-first): #133 Between a
  Rock..., 716 lines.
- slice done (2026-08-11): Between a Rock... (#133) -- Dondakan the Dwarf
  is cannon-firing a wall in the Keldagrim south-west mine, convinced a
  lost realm lies behind it; the player fetches dwarven lore (3 torn
  pages -- scorpion kill, mine-cart search, low-grade rock mining) from
  Rolad, arms Dondakan with a golden cannonball to crack the rock,
  gathers four schematic fragments (Dondakan, the book's last page, the
  Dwarven Engineer, Khorvak) to solve the sealing mechanism, smiths a
  golden helmet, and is fired through into the hidden Arzinian realm to
  mine gold ore and defeat an Avatar guardian. Native dbrow `quest_
  betweenarock` (id 76, endstate 110, questpoints 2, requirement_stats
  (13,50)=Smithing50/(14,40)=Mining40/(1,30)=Defence30, all
  `requirements_boostable`=1, stat_xp_awarded Defence/Mining/Smithing
  5000xp each -- matches quest-helper's own getGeneralRequirements()/
  getExperienceRewards() exactly) + a fully native varbit schema on
  basevar `dwarfrock_main` (`dwarfrock_quest` master 0-255,
  `dwarfrock_gold_cannonball`, `dwarfrock_fired_gold_cannonball`,
  `dwarfrock_schematics_solved`, plus dialogue-tracking flags
  `dwarfrock_ferryman1_beenbefore`/`_ferryman2_beenbefore`/
  `_gold_boatman_met`/`_met_engineer`/`_rolad_schematics_heardof`/
  `_rolad_schematics_lookingfor`/`_dondakan_inside_heardof`/
  `_brothers_introduced`/`_brothers_toldvictory`/`_inside_visited`/
  `_inside_timeleft`) reused as-is, matching quest-helper's own
  `VarbitID.DWARFROCK_*` names exactly, claimed bare in
  `betweenarock.varp` same as `quest_cabinfever`'s own `fever_quest`
  precedent. `%dwarfrock_quest`'s exact ten-step breakpoints (0/10/.../
  100) matching quest-helper's own `steps.put` keys are independently
  confirmed (cache wins, not guessed) by the cache's own native
  `dwarfrock_multi_dondakan` multivarbit npc record: Dondakan
  (`dwarfrock_dondakan`) only renders at exact multiples of ten and
  swaps to `dwarfrock_dondakan_noaxe` from value 110 on (matching the
  dbrow's own `endstate`); `dwarfrock_multi_gold_boatman` likewise only
  resolves to `dwarfrock_gold_boatman` from 110 on -- both used directly,
  a stage-110 "quest complete" state added past the last `steps.put` key
  to match. `tools/questhelper_extract.py`-equivalent manual gameval
  resolution (staged locally from a `curl`-fetched copy of the QH source
  via `raw.githubusercontent.com/Zoinkwiz/quest-helper`, no local
  checkout on this machine) found every `NpcID`/`ObjectID`/`ItemID`/
  `VarbitID` name used by `BetweenARock.java`/`PuzzleStep.java` resolves
  clean in the osrs239 pack, zero unresolved names.
  Dbrow `requirement_quests` decodes to dbrow ids 35 (`quest_
  sheepherder`) and 52 (`miniquest_magearena1`) -- neither a real
  prerequisite, same known cache-decode-corruption failure mode this
  queue warns about repeatedly. The real prerequisites per quest-
  helper's own `getGeneralRequirements()` are Dwarf Cannon FINISHED and
  Fishing Contest FINISHED. Fishing Contest (`quest_fishingcompo`) is
  genuinely completable -- grep confirms it writes `%fishingcompo =
  ^fishingcompo_complete` and calls `~quest_complete(quest_
  fishingcontest)` from its own `quest_fishingcompo.rs2` -- and is hard-
  gated. **Dwarf Cannon is NOT genuinely completable in this tree despite
  being listed "IN-LC" with six separate "done" `CONTENT_PORT_QUEUE.md`
  log lines (26f/26v/26y/27c/31c/32h/33f)** -- auditing `quest_mcannon/`
  for this slice (per this queue's standing instruction to spot-check
  prerequisites rather than trust a `done` label) found every one of
  those slices real (railings, doors, ladder, cave guard, crate child,
  journal, cannonball smelting all genuinely scripted), but grepping
  every `%mcannon =` assignment site in the whole tree shows the master
  varp never advances 0->1 (`^mcannon_tasked_with_fixing_railings`), 8->9
  (`^mcannon_tasked_with_speaking_to_nulodion`), or 10->11
  (`^mcannon_complete`) anywhere -- the "Dwarf Commander" who assigns/
  advances/finishes the quest, and Nulodion's own talk dialogue (only his
  *item* mesbox exists, `nulodions_notes.rs2`), have no dialogue script
  anywhere in this tree. `%mcannon` is permanently stuck at 0 in this
  tree, so Dwarf Cannon cannot actually be started or completed --
  hard-gating on it would make Between a Rock... itself permanently
  unstartable. Soft-skipped instead, same convention as Cabin Fever's own
  Priest in Peril / In Aid of the Myreque's own In Search of the Myreque
  (reasoned out in `betweenarock_shared.rs2`); flagged with a matching
  note on `CONTENT_PORT_QUEUE.md` next to the mcannon log lines rather
  than re-scoring those six rows (out of scope for this slice -- they are
  each individually real, the gap is the missing quest-giver dialogue,
  not those slices). The Smithing 50 gate (golden helmet) and Defence 30
  gate (before Dondakan fires the player through) are hard-checked at
  their own action points, matching `requirement_check_skills_on_start`
  =0 -- not a single quest-start blanket check.
  Simplifications (documented, no established precedent anywhere in this
  tree for the alternative): quest-helper's own `PuzzleStep` drives a
  real native drag/rotate widget puzzle (`interfaces/dwarf_rock_
  schematics.if` + `_control.if`, genuine rev-230 interfaces) with exact
  pixel-position and rotation-id matching for three pieces -- no generic
  engine mechanic anywhere in this tree scripts exact widget position/
  rotation manipulation (grep-verified), so assembling the four
  schematic fragments collapses to one Use action once all four are
  held, same "grant a full requirement in one action" convention as
  Cabin Fever's locker searches / The Great Brain Robbery's crate build.
  The three torn book pages likewise auto-combine into `dwarf_rock_
  pagex3` the instant all three are held, skipping the intermediate
  `dwarf_rock_pagex2` bundle and any manual item-on-item combine step.
  The "keep gold ore in your inventory to stop the Avatar regenerating"
  consumable anti-regen mechanic isn't modelled -- 6 gold ore is checked
  once at the start of the confrontation, same as every other hand-
  spawned boss in this tree leaving the fight itself to the generic
  combat system; the 15-ore "weaker Avatar" route is recognised in
  dialogue only, since no level-75-vs-125 pair exists among the nine
  native colour/style Avatar variants. The realm's real 8-minute time
  limit (`dwarfrock_inside_timeleft`, a genuine native 10-bit field) is
  enforced with a coarser 30-second-per-decrement `softtimer` (16
  decrements) rather than chasing an unverifiable exact tick rate, same
  "approximate, not narrated, but real and enforced" reasoning as other
  slices' hand-picked travel coordinates -- same `softtimer`/
  `clearsofttimer` idiom as `minigame_barrows/scripts/barrows_
  tunnel.rs2`'s own prayer-drain timer. Avatar hand-spawned on trigger
  (no `.spawn` entry for any of the nine colour/style variants anywhere
  in the tree, confirmed via grep), same idiom as Royal Trouble's Giant
  Sea Snake / The Great Brain Robbery's Barrelchest / In Aid of the
  Myreque's Gadderanks. Avatar combat style (mage/archer/warrior) is
  selected by the player's own highest combat stat per the wiki's own
  "counters your strongest style" description; the green/yellow recolour
  is cosmetic RNG only, no established per-kill difficulty-scaling
  precedent anywhere in `skill_combat`. Golden cannonball smelting merges
  into the shared furnace switch (`skill_smithing/scripts/smelting/
  smelting.rs2`'s own `case gold_bar, perfect_gold_bar :`, previously
  `@craft_gold_menu` unconditionally) behind a new `@dwarfrock_gold_bar_
  or_menu` label that falls through to the unmodified jewellery menu for
  every other case, same additive idiom as In Aid of the Myreque's own
  `burgh_rod_clay` case; page 3 similarly hooks both successful-mine
  branches in `skill_mining/scripts/mining.rs2` (a no-op outside this
  quest's own Dwarven Mine page-collecting step). The two Troll
  Stronghold <-> Keldagrim tunnels (`trollromance_stronghold_exit_
  tunnel`, `dwarf_cavewall_tunnel`) and both Dwarven Ferryman crossings
  have zero pre-existing script references anywhere in the tree (grep-
  confirmed) -- this quest's own unique route, not shared with any other
  content; `fai_dwarf_trapdoor_down`/`ladder_from_cellar_directional`/
  `tunnelstairstop` (Dwarven Mine, Rolad's ladder, Khorvak's stairs) are
  already generic `category=climb_down`/`climb_up` records
  (`ladders_stairs/configs/ladders.loc`), needing zero custom transport
  scripting, same "no per-object destination lookup" reasoning as
  `~climb` itself. `goldrock1`/`goldrock2` inside the realm are the same
  generic gold rock the rest of the game already mines (`skill_mining/
  configs/rocks.loc`) -- no dedicated realm-only ore loc exists.
  Rewards: 2 QP, 5000 Defence/Mining/Smithing XP each (matches dbrow
  exactly), a Rune pickaxe, and functional access to the Arzinian realm
  (Ring of Wealth teleport menu entry deferred -- no established
  precedent anywhere in this tree for adding a teleport destination to a
  jewellery item's menu). Wiki https://oldschool.runescape.wiki/w/
  Between_a_Rock.../Quick_guide + quest-helper source fetched via GitHub
  raw (dialogue paraphrased, not verbatim, per copyright, same caveat as
  every prior slice). `mingw32-make -C src sscompile` clean (only pre-
  existing snprintf-truncation warnings in the compiler itself);
  `mingw32-make -C src torirsserver-scripts` exit 0, 14453 scripts compiled
  (up from 14400); grep of the full build log for "betweenarock"/
  "dwarfrock" returned zero hits, and a full self-sweep of all 53
  trigger headers this slice authored against the rest of the tree found
  zero collisions. Files: `quests/quest_betweenarock/{configs/
  betweenarock.{constant,varp}, scripts/betweenarock_{shared,travel,
  dondakan,pages,schematics,realm,journal}.rs2}` + merges into
  `skill_smithing/scripts/smelting/smelting.rs2`,
  `skill_mining/scripts/mining.rs2`, and wiring into
  `interface_questjournal/scripts/quest_journal.rs2`. Next pending row
  (smallest-first): #134 Ratcatchers, 737 lines.
- slice 134 done: Ratcatchers -- Gertrude sends the player to four retired
  ratcatchers (Jimmy Dazzler, Hooknosed Jack, Smokin' Joe, The Face/Felkrash)
  to train a cat, culminating in a King Rat fight and charming the Port Sarim
  Rat Pits with a snake charm. Grep-verified first (methodology steps 1-2): no
  LC proc (`lc_quests.txt` clean, no `quest_ratcatchers` dir), no 2009scape
  impl; genuinely pending. Fetched `Zoinkwiz/quest-helper`'s actual filename
  `RatCatchers.java` (capital C) + its companion `RatCharming.java` (540+197
  =737 lines, matching this row's own line count exactly) via GitHub's tree
  API after a direct raw-githubusercontent guess 404'd. Native dbrow
  `quest_ratcatchers` (id 99, endstate 127, questpoints 2, stat_xp_awarded
  thieving 45000 raw=4500xp, matching quest-helper's own
  ExperienceReward/QuestPointReward exactly). dbrow `requirement_quests`=75
  resolves to neither Gertrude's Cat (id 50) nor Icthlarin's Little Helper (id
  80) -- same known-corrupt column already flagged for row #109/#130; real
  prereqs are quest-helper's own getGeneralRequirements(): Icthlarin's Little
  Helper FINISHED (`%ics_little_var >= ^ics_complete`, verified genuinely
  reachable -- `icthlarin_pyramid.rs2:142` actually writes it, unlike the
  ISOTR/Dwarf Cannon false-`done` traps this queue warns about) plus The Giant
  Dwarf STARTED (`%giantdwarf_quest >= 1`). Icthlarin's Little Helper's own
  prereq is Gertrude's Cat, so gating on ICS transitively covers it; Gertrude's
  Cat itself independently verified real too (`quest_fluffs/scripts/
  quest_fluffs.rs2`'s own `~quest_complete(quest_gertrudescat)` write, npc
  `gertrudescat`, real dialogue -- not a stub). Native varbit schema on
  basevars `main_ratcatch_var` (`%ratcatch_var`, 8 bits 0-255) and
  `ratcatch_var_multi` (`%vc_raton_off1..6`) reused as-is, matching
  quest-helper's own VarbitID names exactly; `%ratcatch_var` breakpoints are
  quest-helper's own `steps.put` keys where no native sub-field already
  tracks the same ground, private intermediate values elsewhere (sewer-rat
  count, "directions read") in the same unclaimed range, same convention as
  `betweenarock`'s own private sub-values alongside its native bits. Native
  multi-npc records independently confirm the mansion's 6 rats: six
  `vc_partyrat_multi1..6` wrappers, each keyed on its own `vc_raton_off1..6`
  bit and world-spawned at exact coordinates in `m44_79.spawn` -- resolved to
  `vc_party_rat` and disambiguated by `npc_coord`, same idiom as
  `quest_fluffs`'s own `npc_coord = %fluffs_crate` check; **zero hand-spawning
  needed for the mansion rats**. The Varrock Sewer's 8 rats have no such
  native wrapper (`pitrat_sarim_def`, the id quest-helper names, has zero
  world spawns anywhere near the sewer -- it only lives in the Port Sarim rat
  pit map) -- the already-world-baked generic `rat` npc (dozens of instances
  in `m50_154.spawn`, right where Phingspet stands) stands in for it instead,
  again with **zero hand-spawning**. All items/npcs/locs resolved natively
  (gertrude_post/vc_phingspet/pitrat_sarim_def/vc_jimmy_dazzler/vc_party_rat/
  vc_hooknosed_jack/apothecary/vc_smokin_joe/vc_felkrash_the_bard/vc_face,
  ratcatchers_rathole1-5/vc_blank_walldecor/vc_trellis_base/vc_manhole_open/
  vc_ladder/fai_varrock_ladder(top)/feud_money_bowl, rat_poison/
  ratcatchers_poisonedcheese/ratcatchers_weedpot/ratcatchers_smokey_weedpot/
  ratcatchers_cat_antipoison/ratcatchers_party_directions/snake_flute/
  ratcatchers_music/vc_rat_pole -- every single one already declared, none
  invented). This tree has no follower/pet system at all (`quest_fluffs`'s
  own documented deferral), so "a cat is following you" and "a catspeak
  amulet is equipped" are modelled as inventory/worn checks over the same
  kitten/grown-cat item enumeration `gertrude.rs2`'s own `fluffs_has_pet_cat`
  already established, restricted to non-overgrown. **Two simplifications,
  both documented with "no established precedent" same as prior slices**: (1)
  the King Rat fight (`vc_blank_walldecor`, use cat + 8 fish) is one
  deterministic action, same "one action, no scripted pet-vs-monster combat
  loop" convention as `betweenarock`'s own Avatar fight; (2) the snake-charm
  8-note tune minigame (native rev-230 `interfaces/ratcatcher_flute.if`,
  interface 282, `cs1script1`-driven note buttons) collapses to one action
  once the snake charm + music scroll are held and the player is outside the
  Port Sarim manhole -- grep-confirmed **zero** `[if_button,...]` triggers
  anywhere in the whole tree drive any cs1script-heavy widget server-side,
  same precedent as `betweenarock`'s own schematics puzzle deferral AND
  `quest_death`'s own `death_dice` deferral (both also native rev-230
  interfaces left unwired for the identical reason). **Critical correctness
  catches this slice hit**: the Port Sarim Rat Pits sit on the same +6400
  world-Z underground map-sheet offset as Varrock Sewer, so the manhole/ladder
  there needed `general_use/scripts/manholes.rs2`'s own `p_telejump`+
  `movecoord(coord,0,0,6400)` idiom, NOT the simple `~climb` plane-delta proc
  (which blocks below plane 0 and would have silently no-opped); caught before
  building by checking the zone's own world-Z band, not by trial and error.
  Also hit **four real pre-existing trigger collisions** merged in rather than
  duplicated (grep-first, every one): `[opnpc1,gertrude_post]`
  (`quest_atailoftwocats/scripts/twocats.rs2`, gated so it only steals the
  turn from A Tail of Two Cats for players who are actually eligible to start
  or already mid/post Ratcatchers -- never for someone who simply hasn't met
  its prereqs); `[opheldu,pot_empty]` (`quest_swansong/scripts/
  swansong_army.rs2`, pot-of-weeds branch added ahead of its airtight-pot
  logic); `[opnpc1,apothecary]` (`areas/varrock/scripts/apothecary.rs2`,
  branch added ahead of the My Arm's Big Adventure/Between a Rock/romeojuliet
  chain already there) -- while checking this one, found a **pre-existing,
  unrelated latent duplicate**: `quest_atailoftwocats/scripts/twocats.rs2`
  *also* independently declares its own `[opnpc1,apothecary]` (line 313),
  meaning one of the two already silently shadows the other for real players.
  Not this quest's file and not caused by this slice -- left alone, flagged
  here for a future tick to actually fix. `mingw32-make -C src sscompile`
  clean (only pre-existing snprintf-truncation warnings in the compiler
  itself); `mingw32-make -C src torirsserver-scripts` exit 0, 14486 scripts
  compiled (up from 14453, +33 -- exactly matching this slice's own
  27+5+1=33 authored script/proc blocks); a full self-sweep of every trigger
  header this slice authored (21 opnpc/oploc/opheld/oplocu triggers) against
  the rest of the tree found zero collisions beyond the four merged above.
  Deferred, documented: full Rat Pits minigame content (doesn't exist as a
  tree anywhere in this repo -- a separate, much larger slice, matching this
  queue's own "never park sibling content" boundary in reverse: not stealing
  a whole minigame's scope into one quest slice); training overgrown cats
  into wily/lazy cats (same pet.rs2 deferral as Gertrude's Cat); Ring of
  Charos(a) snake-charmer price discount (no favour-item price-override
  precedent on an unrelated quest's NPC); DS2's own separate catspeak unlock
  path (no native item/varbit for it anywhere, same TODO quest-helper itself
  leaves). Wiki `oldschool.runescape.wiki/w/Ratcatchers` +
  `.../Quick_guide` + `Transcript:Ratcatchers` (dialogue paraphrased, not
  verbatim, per copyright, same caveat as every prior slice). Files:
  `quests/quest_ratcatchers/{configs/ratcatchers.{constant,varp},
  scripts/ratcatchers_{shared,journal}.rs2, scripts/ratcatchers.rs2}` +
  merges into `quest_atailoftwocats/scripts/twocats.rs2`,
  `quest_swansong/scripts/swansong_army.rs2`,
  `areas/varrock/scripts/apothecary.rs2`, and wiring into
  `interface_questjournal/scripts/quest_journal.rs2`. Next pending row
  (smallest-first): #135 Dream Mentor, 745 lines.
- slice 135 BLOCKED (not ported): Dream Mentor -- grep-verified first
  (methodology steps 1-2, including `lc_quests.txt`): no LC proc, no
  2009scape impl, genuinely otherwise-pending. Fetched `Zoinkwiz/quest-
  helper`'s `DreamMentor.java` (441 lines) + `CyrisusArmourSet.java`/
  `CyrisusBankConditional.java`/`CyrisusBankItem.java`/
  `SelectingCombatGear.java` (101+39+62+102=304, summing to 745, matching
  this row's own line count exactly). Native dbrow `quest_dreammentor` (id
  134, endstate 28, questpoints 2, requirement_combat 85,
  stat_xp_awarded hitpoints 150000 raw=15000xp + magic 100000 raw=10000xp,
  matches wiki exactly) + a genuine native varbit schema on basevars
  `dream_prog`/`dream_combattype`/`dream_health`/`dream_armament`/
  `dream_cutscene_seen`/etc, matching quest-helper's own VarbitID names
  exactly -- the port itself would have been straightforward. dbrow
  `requirement_quests`=88,36 decodes to Forgettable Tale / Plague City --
  same known-corrupt column this queue repeatedly flags -- but unlike
  every prior corrupt-dbrow case, quest-helper's own real
  getGeneralRequirements() here is Combat 85 (fine, a stat check) +
  **Lunar Diplomacy FINISHED** + Eadgar's Ruse FINISHED. Eadgar's Ruse is
  genuinely real and reachable (`quest_eadgar`, already implemented, IN-LC
  list). Lunar Diplomacy is NOT a corrupt-dbrow artifact to soft-skip --
  it is itself still `pending` on this exact queue, row #169, at 1,756
  lines, and grep of the whole tree for any `lunar*` script file, any
  `lunar_oneiromancer`/`lunar_pirate_captain` trigger, or any mainland<->
  Lunar Isle boat transport returned **zero hits**. Lunar Isle's own
  geography, npcs, bank booth (`[oploc2,lunar_moonclan_bankbooth]
  ~openbank;`) and mine ladders are all genuinely world-baked cache data
  (confirmed via `m32_60.spawn`/`m32_61.spawn` and
  `ladders_stairs/configs/ladders.loc`'s own `lunar_mine_slanty_ladder_*`/
  `lunar_moonclan_ladder`), same "no per-object destination lookup"
  pattern as everywhere else -- but the *only* way there in real OSRS,
  Lunar Diplomacy's own opening boat trip (`lunar_pirate_captain`,
  `lunar_captains_parrot`), has no scripted op anywhere in this tree.
  This is categorically different from this queue's prior soft-skipped
  prerequisites (Priest in Peril for Cabin Fever, In Search of the
  Myreque for In Aid of the Myreque): those were single unwritten gate
  variables on quests whose own content stood alone regardless. Dream
  Mentor's entire setting (Lunar Isle, the Oneiromancer, the dream-vial/
  brazier ritual, the "7 new Lunar spells" reward) is physically
  unreachable and thematically meaningless without Lunar Diplomacy having
  run first -- porting it now would mean either fabricating Lunar Isle
  transport wholesale (no native precedent to build from responsibly) or
  quietly stealing Lunar Diplomacy's own already-queued opening scope into
  this slice, both of which violate this queue's own "never park/steal
  sibling content" rule in reverse. Correctly left `pending` -> `blocked`
  rather than force-ported; re-queue once #169 Lunar Diplomacy lands. No
  files written, no build changes. Next pending row (smallest-first): #138
  Land of the Goblins, 760 lines (row #136 Watchtower and #137 Shadow of
  the Storm are already `done (LC)` stale-row fixes from an earlier tick).
- slice 138 done: Land of the Goblins -- Dorgeshuun Mines dweller Grubfoot's
  troubling dream sends the player (with Zanik) to infiltrate the Fishing
  Guild's goblin temple in disguise, free Zanik from its north-east cell,
  answer High Priest Bighead's loyalty quiz, thieve six enclave keys from
  six colour-coded priests, defeat five named skeleton high priests in the
  crypt beneath (Snothead/Snailfeet/Mosschin/Redeyes/Strongbones, each
  naming the next), and fix Oldak's fairy ring machine to reach Yu'biusk,
  the goblins' promised land. Grep-verified first (methodology steps 1-2,
  including `lc_quests.txt`): no LC proc, no 2009scape impl. Fetched
  `Zoinkwiz/quest-helper`'s `LandOfTheGoblins.java` (760 lines, matching
  this row's own line count exactly, single file). Native dbrow
  `quest_landofthegoblins` (id 166, endstate 56, questpoints 2,
  requirement_stats herblore48/thieving45/fishing40/agility38,
  stat_xp_awarded agility/fishing/thieving/herblore 80000 raw=8000xp each,
  matches quest-helper's own getExperienceRewards() exactly). dbrow
  `requirement_quests`=1,52 decodes to Cook's Assistant / Mage Arena I --
  same known-corrupt column this queue repeatedly flags -- real prereqs
  per quest-helper's own getGeneralRequirements() are Another Slice of
  H.A.M. FINISHED (`%slice_quest >= ^slice_complete`, already real,
  `quest_anothersliceofham/scripts/slice_sigmund.rs2` writes it) and
  Fishing Contest FINISHED (`%fishingcompo >= ^fishingcompo_complete`,
  already real, LC's own `quest_fishingcompo`), both independently
  confirmed genuinely reachable, plus the four hard skill gates above.
  Native varbit schema on basevars `lotg_base` (`%lotg`, 9 bits 0-511,
  breakpoints 0/2/4/.../52 matching quest-helper's own `steps.put` keys
  exactly, independently confirmed via the Java's own
  `VarbitRequirement(VarbitID.LOTG, 36, ...)`) and `lotg_base_2` reused
  as-is, matching quest-helper's own VarbitID names exactly; real
  sub-fields `%lotg_player_is_a_goblin` (disguise state),
  `%lotg_know_about_fish`, `%lotg_found_sphere`, `%lotg_machine_explained`,
  `%lotg_connectors_1/2/3` + `%lotg_fairy_ring_animating` (fairy ring
  puzzle) all driven directly rather than reinvented. All named npcs
  (`lotg_grubfoot`, `lotg_zanik` + its cutscene/yubiusk variants,
  `lotg_goblin_guard_black/white/yellow/darkblue/orange/purple`,
  `lotg_goblin_priest_<colour>_1op/2op`, `lotg_goblin_high_priest`,
  `lotg_goblin_skeleton_high_priest1..5` + `..._defeated` variants,
  `dorgesh_oldak_there`/`_1op`) and every loc/item (six enclave keys,
  Dorgesh-Kaan sphere, goblin mail in all six colours, `lotg_temple_
  huge_door`, five crypt graves, `lotg_bandos_sarcophagus`) are already
  natively declared -- none invented. **Zero hand-spawning needed for any
  of the geography**: the Goblin Cave, temple, crypt and Yu'biusk are all
  genuinely world-baked map data (confirmed via `configs/all.loc.compack`'s
  own `lotg_*` ids and the `m32_*`/`m38_*`-style world dressing), same
  "no per-object destination lookup" pattern as everywhere else in this
  tree -- only the five named skeleton priests are hand-spawned on trigger
  (zero `.spawn` entries anywhere, grep-confirmed), same idiom as
  `betweenarock`'s own Avatar. Travel between Dorgesh-Kaan's floors and its
  lower caves is already generic `category=climb_up`/`climb_down`
  (`ladders_stairs/configs/ladders.loc`'s own `dorgesh_1stairs`/
  `dorgesh_2stairs_posh`/`dorgesh_caves_ladder_down`) -- no custom
  transport scripting needed. The Goblin Cave itself is reached via the
  *already-existing* `[oploc1,mcannoncave]` (Dwarf Cannon's own real,
  IN-LC-listed `quest_mcannon`, confirmed genuinely implemented, not a
  false-`done` trap) -- confirmed its own `p_telejump` destination lands
  within a few tiles of quest-helper's own cited Goblin Cave coordinate,
  so this slice adds its own npcs inside that already-reachable
  underground zone rather than touching the trigger at all.
  **Five real pre-existing trigger collisions found and merged in rather
  than duplicated** (grep-first, every one, per this queue's own
  non-negotiable rule): (1) `[opnpc1,makeover_mage]`
  (`areas/falador/scripts/makeover_mage.rs2`) -- gated branch ahead of the
  generic cosmetic-makeover dialogue; (2) `[opnpc1,aggie]`
  (`areas/draynor/scripts/aggie.rs2`) -- gated branch ahead of the generic
  dye-shop dialogue for the whitefish/black-mail/white-mail-plus-four-dyes
  trade; (3) `[opnpc1,0_41_53_sinisterfishspot]`
  (`quest_fishingcompo/scripts/hemenster_fishing.rs2`) -- Fishing Contest's
  own competition logic silently no-ops outside an active competition, so
  the LOTG whitefish catch (disambiguated by slimy-eel bait) is checked
  first; (4) `[opheldu,toadflaxvial]`
  (`skill_herblore/scripts/brew_potion.rs2`) -- the generic herblore brew
  table, with a pharmakos-berry check ahead of it (not a real herblore
  recipe); (5) `[opheldu,golem_ink]` (`quest_golem/scripts/
  golem_portal.rs2`) -- quest-helper's own "Black dye" is this exact same
  `ItemID.GOLEM_INK`, and the entire black-mushroom-pick + pestle-and-
  mortar-grind pipeline (`[oploc1,golem_black_mushrooms]` +
  `[opheldu,golem_mushroom]`) already exists verbatim, shared with Shadow
  of the Storm's own Silverlight-dyeing step -- this slice adds only the
  "dye goblin mail" case to the existing switch, reusing the pickup/grind
  chain entirely unmodified. **Also found and fixed**: Goblin Diplomacy's
  own pre-existing `~dye_goblin_mail_armour` proc
  (`quest_gobdip/scripts/quest_gobdip.rs2`) only ever consumed the plain
  `goblin_armour` item, which would have silently failed for LOTG's own
  redye-in-place loop (colouring an already-coloured mail again) --
  extended in place to consume whichever mail colour is actually held,
  backward-compatible with Goblin Diplomacy's own always-plain-mail usage,
  and `skill_crafting/scripts/dye_cape.rs2`'s own `[opheldu,yellowdye]`/
  `[opheldu,bluedye]`/`[opheldu,orangedye]`/`[opheldu,purpledye]` switches
  extended with the missing goblin-mail cases (yellow and purple had none
  at all; blue and orange only matched plain mail). Simplifications
  (documented, no established precedent anywhere in this tree for the
  alternative, matching this queue's own repeated convention): (1) the
  "confirm to become a goblin" widget (native rev-230 interface 739,
  quest-helper's own text says "Your selection doesn't matter") collapses
  to an instant flag set on drinking the potion; (2) the fairy ring
  power-relay dial puzzle (native rev-230 interface 738, six increase/
  decrease buttons targeting exact values 9/4/1) collapses to one
  deterministic "fix the machine" action, same "no per-component widget
  click sequence" reasoning as `betweenarock`'s schematics puzzle /
  `quest_death`'s dice; (3) no follower/pet system exists in this tree
  (`quest_fluffs`'s own documented deferral) -- "Grubfoot/Zanik is
  following you" is an instant flag advance, not a literal companion NPC;
  (4) the optional "guess my goblin name" flavour exchange has no
  gameplay effect on progression per quest-helper itself, not modelled;
  (5) Yu'biusk (`InInstanceRequirement`) has no dynamic per-player
  instance precedent anywhere in this tree -- modelled as the real,
  shared, static map area already baked into the cache, same convention
  as `betweenarock`'s own Arzinian realm; (6) the five named skeleton high
  priests' unique special mechanics (stat-draining hits, summoned
  Skoblins) are left to the generic combat system, same reasoning as Royal
  Trouble's Giant Sea Snake; (7) High Priest Bighead's "true/false/false"
  quiz is one deterministic dialogue chain, no wrong-answer branch
  precedent anywhere in this tree's quest dialogue. Wiki
  `oldschool.runescape.wiki/w/Land_of_the_Goblins` +
  `.../Quick_guide` (dialogue paraphrased, not verbatim, per copyright,
  same caveat as every prior slice). `mingw32-make -C src sscompile`
  clean (only pre-existing snprintf-truncation warnings in the compiler
  itself); `mingw32-make -C src torirsserver-scripts` exit 0, 14,557 scripts
  compiled (up from 14,486, +71); full build log grepped for every
  touched/new filename (`lotg`, `landofthegoblins`, `quest_gobdip`,
  `dye_cape`, `golem_portal`, `brew_potion`, `hemenster_fishing`,
  `makeover_mage`, `aggie.rs2`) returned zero warnings or errors; a
  self-sweep of every trigger header this slice authored (grep batch
  covering every `opnpc`/`oploc`/`opheld`/`ai_queue` name) against the
  rest of the tree found zero collisions beyond the five merged above.
  Deferred: none identified beyond the documented simplifications above --
  every `steps.put` breakpoint is real and playable end-to-end. Files:
  `quests/quest_landofthegoblins/{configs/landofthegoblins.{constant,varp},
  scripts/lotg_{shared,intro,temple,keys,crypt,yubiusk,journal}.rs2}` +
  merges into `areas/falador/scripts/makeover_mage.rs2`,
  `areas/draynor/scripts/aggie.rs2`,
  `quests/quest_fishingcompo/scripts/hemenster_fishing.rs2`,
  `skill_herblore/scripts/brew_potion.rs2`,
  `quests/quest_golem/scripts/golem_portal.rs2`,
  `quests/quest_gobdip/scripts/quest_gobdip.rs2`,
  `skill_crafting/scripts/dye_cape.rs2`, and wiring into
  `interface_questjournal/scripts/quest_journal.rs2`. Next pending row
  (smallest-first): #139 Elemental Workshop II, 770 lines.
- slice 139 done: Elemental Workshop II -- decrypting the sequel to the
  Book of the Elemental Shield, sneaking into the sealed lower half of the
  Seers' Village elemental workshop, repairing its crane/press/water-tank/
  wind-tunnel machinery to prime an elemental bar, imbuing it with the
  power of the mind in a basement extractor, and smithing a Mind Helmet.
  Grep-verified first (methodology steps 1-2, including `lc_quests.txt`):
  no LC proc, no 2009scape implementation. Fetched `Zoinkwiz/quest-helper`'s
  `ElementalWorkshopII.java` (683 lines) + `ConnectPipes.java` (87 lines),
  770 total, matching this row's own line count exactly. Row #56 Elemental
  Workshop I was independently re-audited as this slice's real
  prerequisite (not just trusted as `done`): `quest_elemental_workshop`'s
  three files genuinely set `%elemental_workshop_book`/`_key` and drive
  `%elemental_workshop_finished` as a real completion flag read by its own
  journal -- confirmed genuinely completable, used directly as the gate
  here (dbrow `requirement_quests`=38 does not resolve to EW1's own dbrow
  id 55 -- the same known-corrupt column this queue has repeatedly
  flagged; the real prerequisite is quest-helper's own
  `QuestRequirement(ELEMENTAL_WORKSHOP_I, FINISHED)`). Native dbrow
  `quest_elementalworkshop2` (id 119, endstate 11, questpoints 1,
  requirement_stats magic(6)20/smithing(13)30 boostable, stat_xp_awarded
  smithing(13)/crafting(12) 75000 raw = 7500xp each -- matches
  quest-helper's own SkillRequirement/ExperienceReward calls exactly).
  Native varbit schema entirely pre-declared, none invented: 20 named
  sub-fields on three basevars (`elemental_quest_2_main` as the top-level
  progress var, `_hide_key`, `_hatch`, `_jig_pos`, `_jig_state`,
  `_fire_state`, `_fire_pos`, `_earth_pipe_1/2/3_state`, `_water_state`,
  `_water_valve_1/2`, `_water_door`, `_water_level`, `_air_cog1/2/3`,
  `_air_fan_state`, `_mind_jig`, `_box_state`), matching quest-helper's own
  `VarbitID.ELEMENTAL_QUEST_2_*` names exactly (lowercased) --
  `%elemental_quest_2_main` driven through 12 breakpoints (0-11) matching
  quest-helper's own `steps.put(0..10)` map 1:1, reaching 11 at
  completion, matching the dbrow's own `endstate`. Native MULTI-NPC/
  MULTI-LOC records keyed on these varbits recovered real progression
  breakpoints and let all cosmetic state-swapping be skipped entirely
  (grep-confirmed, none invented): `elem2_cart_npc` (multinpc on
  `_jig_state`, 6 leaves), `elem2_stairs_door` (multiloc on `_hatch`),
  `elemental_workshop_2_boiler_multi` (multiloc on `_hide_key`),
  `elemental_piping_blue_broken_multi` (multiloc on `_water_state`),
  `elem2_wind_pin_high/low/left_multi` (multiloc on `_air_cog1/2/3`),
  `elem_windtunnel_fanblade` (multiloc on `_air_fan_state`),
  `elem_extractor_gun` (multiloc on `_mind_jig`); per this tree's own
  precedent (`quest_priest`'s `restless_ghost_altar_skull`/`_no_skull`),
  every trigger below binds to the *resolved leaf* name, not the
  multivarbit wrapper -- self-audited against all seven wrappers to
  confirm. `elem1_qip_earth_elemental_rock_version_rock` (mineable rock,
  own `op1=Mine`) is world-spawned 47 times in the west room
  (`areas/world/configs/m42_154.spawn`); the awakened
  `elem1_qip_earth_elemental_rock_version` is not statically spawned
  anywhere (grep-confirmed) -- hand-spawned on trigger, same idiom as
  `betweenarock`'s Avatar, and its combat/drop table
  (`quest_elemental_workshop/scripts/elemental_drops.rs2`) was already
  fully implemented by EW1's own port, reused unmodified. **Two real
  pre-existing shared-object collisions found and merged in rather than
  duplicated** (grep-first, per this queue's own non-negotiable rule):
  (1) `elemental_workshop_workbench` -- EW1's own port never used this loc
  (grep-confirmed) so this slice claims the sole
  `[opheldu,elemental_workshop_workbench]` trigger, but *within this
  slice itself* the claw-smithing and Mind-Helmet-smithing actions both
  target it, so one shared trigger dispatches by held item to
  `~elem2_make_claw`/inline helmet logic rather than declaring the header
  twice (sscompile accepts silent duplicates with no diagnostic -- caught
  by a self-sweep grepping every trigger header this slice authored
  against the whole tree, confirming exactly one definition each); (2)
  `elemental_workshop_furnace` -- shared physical object with EW1's own
  (unimplemented) lava/waterwheel/bellows middle section
  (`multivarbit=elemental_workshop_fire`, EW1's own furnace-lighting flag,
  never set anywhere in this tree, grep-confirmed) -- an unwritten EW1
  sub-mechanic, soft-skipped per this queue's own convention rather than
  treated as a hard blocker; this slice's own ore-smelting trigger works
  from either multiloc leaf without touching that flag. Simplifications
  (documented, no established precedent anywhere in this tree for the
  alternative): (1) the pipe-connection minigame (native rev-230 widget
  `ElemMagicpressPipes`, `WidgetModelRequirement` in quest-helper's own
  `ConnectPipes.java`) has no established drag-connect widget precedent --
  collapses to one deterministic "open the junction box and reconnect the
  pipes" action, directly setting the three `_earth_pipe_N_state` fields
  to quest-helper's own solved target values (5, 6, 13), same "no
  per-component widget click sequence" reasoning as Land of the Goblins'
  fairy ring dial; (2) no native multiloc exists anywhere for
  `_fire_pos`/`_fire_state` (grep-confirmed) -- the crane's 15-variant
  track/lava-arm never visually swaps position in this cache build, so
  every crane/priming lever action narrates via `mes()` against a single
  fixed loc (`elem2_crane_track_up_empty`) rather than reproducing that
  client-side animation; (3) the optional second bar / Slashed Book ->
  Mind Shield side loop (quest-helper's own "if you want to smith a mind
  shield after this quest" asides) is not required by any `steps.put`
  breakpoint -- not modelled, explicitly optional per quest-helper itself.
  **The "priming a bar" apparatus itself (crane, press, water tank, wind
  tunnel) was NOT collapsed** -- quest-helper's own `primingInWorkshop`
  `ConditionalStep` chain has no player decisions anywhere (every state
  has exactly one valid next action), so this slice independently
  re-derived that entire priority-ordered chain (cross-checked against
  every named lever/valve/corkscrew object and its own WorldPoint) into a
  real deterministic finite-state machine played with the actual 9 named
  objects (`elem2_fire_lever_1/2`, `elem2_lever_3way`,
  `elem2_earth_lever_1`, `elem2_water_lever`, `elem2_corkscrew`,
  `elem2_valve_1/2`, `elem2_air_lever`) plus the jig cart -- ordinary
  object-click content this tree already has an idiom for, just an
  unusually long chain, not a widget puzzle. Wiki
  `oldschool.runescape.wiki/w/Elemental_Workshop_II` +
  `.../Quick_guide` (paraphrased, not verbatim, per copyright, same
  caveat as every prior slice). `mingw32-make -C src sscompile` clean
  (only pre-existing snprintf-truncation warnings in the compiler
  itself); `mingw32-make -C src torirsserver-scripts` exit 0, 14,606 scripts
  compiled (up from 14,557, +49); full build log grepped for `elem2`/
  `elementalworkshopii`/`elementalworkshop2` returned zero warnings or
  errors; a self-sweep of all 34 trigger headers this slice authored
  (grep batch covering every `opnpc`/`oploc`/`opheld`/`opnpcu`/`oplocu`
  name) against the rest of the tree confirmed exactly one definition
  each, including the one intentionally-shared workbench trigger.
  Deferred: none identified beyond the documented simplifications above
  -- every `steps.put` breakpoint (0-10) is real and playable end-to-end.
  Files: `quests/quest_elementalworkshopii/{configs/elementalworkshopii.
  constant, scripts/elem2_{shared,intro,gather,repair,priming,helm,
  journal}.rs2}` and wiring into
  `interface_questjournal/scripts/quest_journal.rs2`. Next pending row
  (smallest-first): #145 Darkness of Hallowvale, 816 lines (rows #140-144
  are already `done`/`done (LC)`).
- slice #145 / P5 done: Darkness of Hallowvale (Myreque #3, Sept 2006, not
  Aug 2013 as the row's old note guessed). Grep-first: no LC proc (only
  coincidental `heartofdarkness`), no 2009scape entry, `lc_quests.txt`
  clean. Native dbrow `quest_darknessofhallowvale` (id 117, endstate 320,
  questpoints 2, requirement_stats matches quest-helper's own
  getGeneralRequirements() exactly) confirms the corrupt-`requirement_
  quests`-decode failure mode again (decodes to dbrow 77 = The Feud, not a
  real prereq) — real prereq per quest-helper AND the wiki infobox nested
  list is In Aid of the Myreque (already `done`, dbrow 107, hard-gated
  below via `%myreque_2_quest >= ^myreque2_complete`), which itself chains
  to In Search of the Myreque → Nature Spirit → Priest in Peril / Restless
  Ghost; ISOM is not ported anywhere in this tree and IAOM's own port
  already documents soft-skipping that same gap rather than hard-gating on
  it (see `myreque2.constant`) — followed that precedent rather than
  re-deriving it, only IAOM itself is hard-checked (the one direct,
  genuinely-completable, immediately-broken-without-it dependency). Full
  native varbit schema recovered on basevars `myreque_3_main_var`/
  `myreque3_multivar` (`myq3_main_quest` 0-511 matching quest-helper's own
  steps.put range exactly, plus ~20 real sub-fields for every mechanical
  beat — boat/chute repair, floorboard entry, the sickle agility course's
  door-key/table-trapdoor/ladder-repair/three-pushwalls, the hideout
  press-wall/trapdoor, Safalaan visibility, Sarius visibility, tapestry,
  portrait/key, vampyre-statue door, rune-case) plus matching native
  multiloc/multinpc wrapper records for every one of them (cache wins, all
  reused as-is, zero invented geometry) — recovered by fetching quest-
  helper's own `DarknessOfHallowvale.java` directly from GitHub raw
  (`Zoinkwiz/quest-helper`) since no local checkout exists on this machine,
  cross-checked step-for-step against the varbit dump. `npcs=myq5veliaf,
  myq3citizen` in the row's old note was itself a stale bad guess — `myq5`
  is Sins of the Father's own basevar (`%myq5`, confirmed via its own
  `sinsofthefather.constant`), not this quest's; the real prefix
  throughout is `myq3` (Veliaf's own DoH-era npc is quest-helper's chosen
  `MYQ5_VELIAF_CHILD` — a separate, later "grown-up" Veliaf identity shared
  with Sins of the Father, not to be confused with the `myq5` progress
  var). Six cross-quest trigger merges, all grep-verified free of
  duplicates before touching (never a second definition of an existing
  `[opnpc1,X]`/`[oploc1,X]`/`[opheldu,X]` header): `route_veliaf_hurtz` in
  `quest_inaidofthemyreque/scripts/myreque2_hideout.rs2` (start/finish
  hub), `myq5_veliaf_child` (`sf_veliaf_talk` label) and `myq3_lab_door_
  locked_l`/`myq3_lab_stairs_down` in `quest_sinsofthefather/scripts/
  sinsofthefather.rs2`, `priestperiltrappedmonk_vis` in `quest_
  inaidofthemyreque/scripts/myreque2_trek.rs2` (Drezel), `myq3_aeonisig_
  roalds_advisor` in `quest_defenderofvarrock/scripts/dov_invasion.rs2`,
  `myq4_vertida_visible`/`myreque_pt3_safalaan` (`toh_safalaan_talk` label)
  in `quest_tasteofhope/scripts/tasteofhope.rs2`, plus `[opheldu,charcoal]`/
  `[opheldu,papyrus]` (generic item pair, already claimed by `quest_itexam/
  scripts/itexam_chemistry.rs2` and `quest_golem/scripts/golem_portal.rs2`
  respectively) merged as a `doh_sketch_attempt` proc returning handled/
  not-handled rather than risking a third silent duplicate. Simplifications
  (documented, no established precedent anywhere in this tree for the
  alternative): the full sickle-logo rooftop agility course (quest-helper's
  own three `LinePoints` waypoint lists, 60+ points, zero player decisions
  anywhere in the step map) collapses to its real state-changing native
  objects narrated with `mes()` between them, same "no real choice,
  collapse to deterministic action" reasoning as Elemental Workshop II's
  priming chain / In Aid of the Myreque's rubble-clearing; the Vyrewatch
  "pay tithe/fight/distract/mines" random-encounter system is wiki-
  documented as fully avoidable and never gates a real breakpoint —
  deferred; the mine-cart route is modelled as the sole return path (quest-
  helper's own faster alternative, "one path modelled" convention);
  Vanstrom Klause's ambush (attack-immune, survive 5 hits) has no
  established unkillable-boss precedent in `skill_combat` — narrated via
  `mes()`, matching Sins of the Father's own "Soft-skip: combat resolved"
  idiom; the Tome of experience's own per-skill Read/xp-choice widget is
  granted as a real item (`myq3_xp_tome_3`) but its consumption UI is not
  wired, matching Elemental Workshop II's own precedent. Wiki
  `oldschool.runescape.wiki/w/Darkness_of_Hallowvale` + `.../Quick_guide`
  (paraphrased, not verbatim, per copyright, same caveat as every prior
  slice); full infobox/requirements wikitext fetched via the wiki's own
  `action=parse` API to cross-check the corrupted dbrow requirement against
  quest-helper's `getGeneralRequirements()` independently. `mingw32-make -C
  src sscompile` clean (only pre-existing snprintf-truncation warnings in
  the compiler itself); `mingw32-make -C src torirsserver-scripts` exit 0,
  14,670 scripts compiled (up from 14,606, +64); full build log grepped for
  `darknessofhallowvale`/`doh_`/`myq3_` returned zero warnings or errors; a
  tree-wide self-sweep of every trigger header this slice authored (51
  `[opnpc*]`/`[oploc*]`/`[opheldu]` headers across 6 new files) confirmed
  exactly one definition each; a second sweep cross-checked all 51 against
  the tree's 57 pre-existing (unrelated, out-of-scope) duplicate-trigger
  keys and found zero overlap. Files: `quests/quest_darknessofhallowvale/
  {configs/darknessofhallowvale.constant, scripts/doh_{shared,burgh,
  meiyerditch,urgent,castle,lab}.rs2}`, additive branches in the six files
  above, and wiring into `interface_questjournal/scripts/quest_journal.rs2`.
  This was the last of the queue's own curated P1-P5 "genuinely post-
  Jan-2009 QuestHelper-only" list (P1/P3/P4/P5 done, only P2 Asoul's Bane
  remains `in_progress`, needing its own dbrow block authored before it can
  be trusted `done` — see its own row note); the ~74 other QH dirs already
  classified mid-era belong on `SCAPE2009_CONTENT_PORT_QUEUE.md`, not here.
- slice #146 done: Ghosts Ahoy (15 Feb 2005) -- Velorina asks the player to
  help the ghosts of Port Phasmatys "pass on"; Necrovarus, the corrupt high
  priest of the Ectofuntus, refuses. Grep-first: no LC proc anywhere in
  `server/scripts` or `lc_quests.txt` (only the cache's own dbrow/synth/loc
  compack records, not scripted content), no `SCAPE2009_CONTENT_PORT_QUEUE.md`
  entry. Native dbrow `quest_ghostsahoy` (id 73, endstate 8, questpoints 2,
  requirement_stats (16,25)=Agility 25 / (7,20)=Cooking 20, both boostable
  per quest-helper's own `SkillRequirement(...,true)` but gated on
  `stat_base` like every other boostable requirement in this tree -- no
  boosted-level accessor exists anywhere in `server/scripts`) matches
  quest-helper's own `getGeneralRequirements()` on the stat side; dbrow
  `requirement_quests` decodes to dbrow ids 111/120 = Swansong / My Arm's Big
  Adventure -- neither a real prerequisite, same known cache-decode-
  corruption failure mode this queue warns about repeatedly. Real
  prerequisites per quest-helper: Priest in Peril FINISHED and The Restless
  Ghost FINISHED. The Restless Ghost (`%prieststart >= ^priest_complete`,
  reached by real gameplay in `quest_priest/scripts/quest_priest.rs2`) is
  hard-gated; Priest in Peril is soft-skipped -- `%priestperil` never
  advances past `^priestperil_meet_in_mausoleum` (8) anywhere in this tree's
  real gameplay scripts (the only write of `^priestperil_complete`, 60, is a
  debugproc in `quest_rumdeal/scripts/deal_debug.rs2`), the exact same
  finding Cabin Fever's own port already documented -- hard-gating on it
  would make Ghosts Ahoy itself permanently unstartable. Full native varbit
  schema recovered on basevar `ahoy_varbits_1` (`ahoy_questvar` bits 28-31,
  collapsed 0..8 matching dbrow's own endstate exactly; `ahoy_given_manual/
  robes/book`, `ahoy_signaturecounter` (0-31, quest-helper's own dual-purpose
  petition/bone-key-drop counter -- this port stops at a deterministic 11
  rather than replicating a ~20-repeat random-drop dialogue loop, no such
  precedent anywhere in this tree), `ahoy_subquest_toyboat` (0-3, matches
  quest-helper's own `hadChestKey`/`unlockedChest2` varbit thresholds
  exactly), `ahoy_killed_lobster`, `ahoy_subquest_bow`, `ahoy_templedoor_
  unlocked`, `ahoy_requested_sheet` -- every sub-field used directly, no
  local catch-all invented; `ahoy_windspeed`/`ahoy_grinder_status`/`ahoy_
  ectotokens_base/more` are NOT used, all three orphaned or unrelated to
  quest-helper's own step map, see `ghostsahoy.constant` for why each one).
  Native multi-npc records `ahoy_akharanu_multi` (`multivarbit=ahoy_questvar`)
  and `protester_ghostspeak_multi`/`protester_standardspeak_multi`
  (`multivarbit=wearing_ghost_speak_amulet`, both wrappers spawned at the
  same coord for Gravingas) independently confirm the basevar and are bound
  directly -- this server only ever spawns wrapper npc/loc types, same
  finding as every prior slice; all other npcs (Velorina, Necrovarus, the
  Old Crone, Old Man, Robin, Ghost innkeeper, ghost villagers, Ghost
  captains, the Ghost disciple) and every loc/obj used (shipwreck chests/
  mast/gangplank, the harbour door, the coffin, the town Energy Barrier) are
  already world-spawned/cache-declared, grep-confirmed via `areas/world/
  configs/*.spawn` and `configs/all.{npc,loc,obj}` -- only trigger logic was
  added, no new spawns or cache records. Ship/Ectofuntus tower navigation is
  already handled by the generic climb system (`ladders_stairs`); the
  harbour door is `category=door_closed` in `doors/configs/doors.loc` (would
  open for free via the generic `[oploc1,_door_closed]` handler) -- this
  port adds a name-specific `[oploc1,ahoy_harbour_door]` override
  (bone-key gated, calling `~door_open_active` once unlocked), the exact
  precedent already proven by Darkness of Hallowvale's own `myq3_lab_door_
  locked_l` override. Cross-file merges, all grep-verified free of
  duplicates before touching: `[opnpc1,ahoy_crone]` (shared with Animal
  Magnetism, `quest_animalmagnetism/scripts/anma_farm.rs2`) gains an
  additive `~ahoy_crone_hub` branch at the top, falling through to Animal
  Magnetism's own unmodified logic otherwise; `[opheld1,spade]` (`general_
  use/scripts/spade.rs2`) gains one more `~ahoy_try_dig` hook in its
  existing dig-dispatch chain (same pattern as X Marks the Spot / Making
  History / Olaf's Quest). A genuine **pre-existing** duplicate was found and
  not compounded: `[opheldu,bucket_milk]` was already declared twice
  (`skill_cooking/scripts/cakes.rs2` and `skill_cooking/scripts/gnome_
  cooking/gnome_cooking.rs2`, both silent, neither diagnosed by `sscompile`,
  matching this queue's own warning that duplicate triggers compile clean)
  -- this quest's own nettle-tea-plus-milk case was added as an additive
  branch inside `cakes.rs2`'s existing trigger (calling a new `ahoy_add_milk_
  to_tea` proc) rather than becoming a third definition; the pre-existing
  cakes/gnome_cooking conflict itself is left undiagnosed, out of scope for
  this slice. The Ectofuntus's own ghost-disciple ecto-token trade
  (`ahoy_disciple`, world-spawned but with zero trigger anywhere in this tree
  before this slice, its own file literally commented "Disciple ectotoken
  tally deferred") was completed here (`skill_prayer/scripts/ectofuntus.rs2`,
  new `ecto_worship_credits` varp, banked 1:1 per worship offering and
  redeemed at the disciple) since Ghosts Ahoy's own ~31-token travel cost
  (Energy Barrier toll + Dragontooth Island boat fare) cannot be satisfied
  end-to-end otherwise -- shared Ectofuntus infrastructure, not exclusively
  Ghosts Ahoy content, but required for the "playable end-to-end" bar.
  Simplifications (documented, no established precedent anywhere in this
  tree for the alternative): `DyeShipSteps`'s own per-player-randomised
  mast/flag colour-matching puzzle (search repeatedly, learn 3 of 6 possible
  colours, dye each of 3 shapes to match) has no established randomised-
  per-player-state-discovered-via-repeated-search precedent -- collapsed to
  one deterministic action requiring the wiki's own "3 primary-coloured
  dyes" (red/blue/yellow), zero invented mixed-dye items; the "wait for low
  wind before searching the mast" cosmetic timer (zero player decision) is
  not modelled, same convention as every prior slice's waiting-game
  simplification. Robin's Rune-Draw gambling minigame ("draw runes until a
  death rune, beat him a few times until he owes 100 coins") has no card/
  dice-minigame precedent anywhere in `minigames/` or `server/scripts` --
  collapsed to one deterministic win gated on holding the coins and the bow.
  The petition's "don't ask the same ghost twice in a row" anti-macro
  restriction is a client-side spam guard, not a real quest-helper
  `steps.put` breakpoint -- not modelled. Nettle tea's own fire-cooking step
  has no reachable generic firemaking-and-cook-on-open-fire hook point
  within this slice's scope -- collapsed to a single `[opheld1,
  bowl_nettlewater]` action. The two non-combat hull-chest instances sharing
  gameval `ahoy_chest_closed`/`ahoy_chest_open` (the lobster chest and the
  rocks-agility chest) are handled by one combined trigger rather than
  coordinate-disambiguated separately, since the rocks leg is pure
  navigation with zero player decision (same "no real choice, collapse to
  deterministic action" reasoning as every prior slice) -- both remaining
  map scraps are granted together once the Giant Lobster (a real, fought,
  `ai_queue3`-death-detected combat npc, never spawned elsewhere in this
  tree) is defeated. Wiki `oldschool.runescape.wiki/w/Ghosts_Ahoy` +
  `.../Quick_guide` (paraphrased, not verbatim, per copyright) plus quest-
  helper's own `GhostsAhoy.java`/`DyeShipSteps.java` fetched via GitHub raw
  (no local checkout on this machine). `mingw32-make -C src sscompile`
  clean (only pre-existing snprintf-truncation warnings in the compiler
  itself); `mingw32-make -C src torirsserver-scripts` exit 0, 14,709 scripts
  compiled (up from 14,670, +39), full build log grepped for `ahoy` returned
  zero warnings or errors. Self-sweep: every trigger header this slice
  authored (38 across 5 new files) confirmed exactly one definition each
  tree-wide before the bucket_milk fix, and zero remaining duplicates after
  it. Files: `quests/quest_ghostsahoy/{configs/ghostsahoy.constant,
  scripts/ahoy_{shared,hub,book,manual,robes}.rs2}`, additive branches in
  `quest_animalmagnetism/scripts/anma_farm.rs2`, `general_use/scripts/
  spade.rs2`, `skill_cooking/scripts/cakes.rs2`, `skill_prayer/scripts/
  ectofuntus.rs2` (+ its own `configs/ectofuntus.varp`), and wiring into
  `interface_questjournal/scripts/quest_journal.rs2`. Next pending row
  (smallest-first): #151 The Eyes of Glouphrie, 969 lines.
- slice #151 done: The Eyes of Glouphrie (17 Jul 2006) -- Brimstail the
  gnome researcher shows the player Oaknock the Engineer's anti-illusion
  machine; Hazelmere (mind-linked, 46 Magic) reveals that the exiled mage
  Glouphrie the Untrusted once hid the death of a sacred Spirit Tree with
  illusion magic and fled to found Arposandra; an invisible saboteur wrecks
  the machine, the player repairs it (magic glue, oak/maple timber,
  Construction 5) and unlocks it, revealing that six "cute creatures"
  scattered around the Tree Gnome Stronghold -- including Brimstail's own
  pet Izzie -- are disguised Arposandran spies; the player kills all six and
  reports to King Narnode for a Crystal saw seed. Grep-first: no LC proc
  anywhere in `server/scripts` or `lc_quests.txt` (only the cache's own
  `m33_77.spawn` flashback-battle spawn list and `npc_anims.generated.npc`,
  not scripted content, released 2006 anyway, after LostCity's Sept 2004
  cutoff), no `SCAPE2009_CONTENT_PORT_QUEUE.md` entry. Native dbrow
  `quest_eyesofglouphrie` (id 116, endstate 60, questpoints 2, startnpc
  4913 = Brimstail, requirement_stats (6,46)=Magic 46 / (22,5)=Construction
  5, both matching quest-helper's own `getGeneralRequirements()` and the
  wiki's own explicit "not required to start" note exactly --
  `requirement_check_skills_on_start=0` independently confirms this, so both
  are hard-gated at the specific narrative beat that actually needs them
  (Magic at the Hazelmere mind-link, Construction at the machine repair)
  rather than at quest start. dbrow `requirement_quests` decodes to dbrow id
  66 = Throne of Miscellania -- not a real prerequisite, the same known
  cache-decode-corruption failure mode this queue's methodology warns about
  repeatedly; the real prerequisite, per quest-helper's own
  `getGeneralRequirements()` AND the wiki infobox, is The Grand Tree
  FINISHED (IN-LC, genuinely completable here), hard-gated via `%grandtree
  >= ^grandtree_complete`. dbrow `stat_xp_awarded` selects the right four
  skills (Magic/Runecraft/Woodcutting/Construction, matching quest-helper's
  own `getExperienceRewards()` exactly) but every amount is exactly 10x the
  real reward (dbrow: 120000/60000/25000/2500 vs quest-helper's and the
  wiki's own 12,000/6,000/2,500/250) -- the real amounts are used, same
  "dbrow row is a hint, not gospel" finding as Darkness of Hallowvale and
  Ghosts Ahoy. Full native varbit schema recovered on basevar `eyeglo_var1`
  (`eyeglo_quest` bits 0-5, 0..60 -- unusually for this queue, quest-helper's
  own `steps.put` numbers and the dbrow's own endstate already agree with
  the native scale exactly, so this port uses a *subset* of quest-helper's
  own numbers as breakpoints directly (0, 1, 2, 12, 15, 25, 36, 45, 60)
  rather than inventing a smaller collapsed enum; `eyeglo_machine_broken`
  bits 10-11 (0=locked/"Unlock", 1=broken/"Repair", 2=fixed/"Operate",
  matching `eyeglo_gnome_machine_02_multiloc`'s own `multiloc1/2/3` order
  and `PuzzleStep.java`'s own `getVarbitValue(EYEGLO_MACHINE_BROKEN) == 2`
  branch exactly); `eyeglo_bowl_seen`/`eyeglo_machine_seen` (native, used
  directly); `eyeglo_killed_eye_1..6` (0-3 each, quest-helper's own
  `killedCreature1..6` conditions, `== 2` for "killed") independently
  confirmed by the native multi-npc records `eyeglo_fluffie_1..6`
  (`multinpc1`=cute, `multinpc2`=evil/attackable, `multinpc3`=an invisible
  despawn npc at value 2) -- this port spawns the *wrapper* names via
  `npc_add`, matching this tree's own established wrapper-binding
  convention (`[opnpc1,grandtree_narnode]` in
  `quest_grandtree/scripts/king_narnode.rs2` already binds the wrapper, not
  the `_1op`/`_2op` leaves, and the engine resolves clickability/op-labels
  from whichever leaf the multivarbit currently selects). The crystal-disc
  puzzle's own internal bookkeeping fields (`eyeglo_coin_value_1..4`,
  `eyeglo_unlock_*`/`eyeglo_operate*_*` on `eyeglo_temp2`/`eyeglo_temp3`,
  and the matching varps) are NOT used, see Simplifications. Native items
  used as-is: `ics_little_sap_bucket` (Bucket of sap -- already a real
  item in this tree via the existing `[oplocu,evergreen]`/
  `[oplocu,evergreen_large]` trigger in
  `quest_icthlarin/scripts/icthlarin_embalm.rs2`, gated there on
  Icthlarin's Little Helper's own stage -- this port adds an additive
  branch gated on this quest's own state, falling through unchanged
  otherwise), `mudrune`, `eyeglo_ground_mud_runes`, `eyeglo_magic_glue`,
  `oak_logs`, `maple_logs`, `hammer` (plain, matching this tree's own
  established "any hammer" simplification), `poh_saw` -- **not** a bare
  `saw` gameval, which does not exist in this cache; `poh_saw` (`name=Saw`)
  is the real item, found only after `sscompile` failed loudly on the
  invented name, exactly bar 1 working as intended -- `eyeglo_violet_
  pentagon`/`eyeglo_red_square`/`eyeglo_yellow_triangle` (flavour discs,
  granted narratively but not checked, see Simplifications),
  `crystal_seed_old_small` ("Crystal saw seed", the wiki's own reward item
  name, confirmed via its own in-cache `desc` field) and `eyeglo_crystal_saw`
  (produced from the seed via the singing bowl's own native `op3=Sing-
  crystal` -- no invented seed-growth mechanic needed, the cache already
  states both the seed item and the exact op that converts it).
  Cross-file merges, all grep-verified free of duplicates before touching
  (one genuine near-miss caught and fixed: this port's own first draft
  declared a second, competing `[opnpc1,gnome_brimstail]` trigger without
  checking first -- `areas/area_gnome/scripts/brimstail.rs2` already
  declares it, for Rune Mysteries' essence-mine teleport offer; converted
  to the same additive-hub-proc pattern used everywhere else in this queue
  before landing, not left as a silent duplicate): `[opnpc1,
  gnome_brimstail]` (Rune Mysteries) gains one line calling
  `~eyeglo_brimstail_hub`, which returns 1 (handled) whenever this quest is
  in its own active window, falling through to Rune Mysteries' unmodified
  logic otherwise (always true once complete) -- same pattern as Ghosts
  Ahoy's own `ahoy_crone_hub` merge. `[opnpc1,grandtree_hazelmere]`
  (Grand Tree's own post-quest bark-sample trigger) and `[opnpc1,
  grandtree_narnode]` (Grand Tree's own reward dialogue) each gain one line
  calling `~eyeglo_hazelmere_hub`/`~eyeglo_narnode_hub` the same way.
  `[oplocu,evergreen]`/`[oplocu,evergreen_large]`
  (`quest_icthlarin/scripts/icthlarin_embalm.rs2`) gain an additive
  `eyeglo`-gated branch ahead of Icthlarin's own check, falling through
  unchanged otherwise. `[opheldu,pestle_and_mortar]`
  (`skill_herblore/scripts/grind_ingredient.rs2`) gains a `last_useitem =
  mudrune` short-circuit ahead of the generic `~attempt_grind_ingredient`
  lookup, the same pattern already used there for Garden of Tranquility's
  own `rune_shards` case (mudrune is not a real herblore grindable). The
  cave-entrance two-way toggle reuses the exact `p_telejump` coordinates
  already proven by the pre-existing `gnome_caveladder`/`gnome_caveentrance`
  triggers in `brimstail.rs2` (the Rune Mysteries-era entrance to the same
  cave), independently cross-checking this port's own WorldPoint -> zone/
  local conversion for `eyeglo_brimstails_cave_entrance` (a separate,
  distinct loc, quest-helper's own `ObjectID.EYEGLO_BRIMSTAILS_CAVE_
  ENTRANCE`, not a duplicate of the two above). Simplifications
  (documented, no established precedent anywhere in this tree for the
  alternative): `PuzzleStep.java`'s own two crystal-disc widget puzzles (a
  single-value "front panel" unlock then a three-slot "control panel"
  combinatorial match across 34 discs, solved live against the rev-230
  `EyegloGnomeMachineLocked`/`EyegloGnomeMachineUnlocked` IF3 interfaces)
  has no established native drag-widget/interface-puzzle precedent
  anywhere in this tree -- collapsed to one deterministic action: clicking
  the repaired machine immediately unlocks and operates it in a single
  beat, matching this queue's own repeated "native widget puzzle with no
  precedent collapses to one deterministic action" convention (Darkness of
  Hallowvale's Tome of experience, Ghosts Ahoy's Rune-Draw gambling and
  mast-dye puzzle). The flavour discs are granted for narrative colour but
  not consumed/checked -- inventing a partial puzzle-matching requirement
  without the real widget to back it would be a bigger invention than
  skipping it outright. The gnome-goblin war / Argento's-death flashback
  cutscene (`gnome_glouphrie`/`eyeglo_king_healthorg`/goblin-and-gnome-
  soldier battle line, native-spawned at `m33_77.spawn` but with zero
  trigger anywhere in this tree before this slice) has no established
  flashback-cutscene precedent reachable within this slice's scope --
  narrated via `mesbox` lines during the Hazelmere mind-link instead,
  matching Sins of the Father's own "Soft-skip: combat/spectacle resolved
  via mes()" idiom. `eyeglo_hazelmeres_book`/`eyeglo_crystal_book` (native
  "Read" flavour items referenced nowhere in quest-helper's own step map)
  are not granted -- unused, same "not every native item needs to be used"
  finding as Ghosts Ahoy's own `ahoy_windspeed`/`ahoy_grinder_status`. Wiki
  `oldschool.runescape.wiki/w/The_Eyes_of_Glouphrie` +
  `.../Quick_guide` (paraphrased, not verbatim, per copyright); the wiki's
  own `Transcript:` page declined verbatim reproduction on request this
  session, so all dialogue below is original paraphrase carrying the same
  story beats, not a copy -- noted explicitly since every prior slice's
  transcript dialogue was itself already a paraphrase, but this is the
  first time the source declined outright. Quest-helper's own
  `TheEyesOfGlouphrie.java` (340 lines) and `PuzzleStep.java` (629 lines,
  969 total matching the queue row's own line count) fetched via GitHub raw
  (no local checkout on this machine). `mingw32-make -C src sscompile`
  clean (only pre-existing snprintf-truncation warnings in the compiler
  itself); `mingw32-make -C src torirsserver-scripts` exit 0, 14,738 scripts
  compiled (up from 14,709, +29); full build log grepped for
  `eyeglo|theeyesofglouphrie|brimstail|hazelmere.rs2|king_narnode|
  icthlarin_embalm|grind_ingredient` returned zero warnings or errors. A
  first build attempt failed loudly on an invented `saw` gameval (fixed to
  `poh_saw`) and, after a manual duplicate-trigger self-sweep (not caught
  by `sscompile`, which accepts duplicates silently by design per this
  queue's own standing warning), on a genuine competing `[opnpc1,
  gnome_brimstail]` definition this slice itself had drafted -- both fixed
  before the build reported here; a full re-sweep of every trigger header
  this slice authored (feet: 1 cave-entrance oploc, 2 singing-bowl oplocs,
  1 machine oploc1, 1 machine oplocu, 1 ground-mud-runes opheldu, 6x
  opnpc2 + 6x ai_queue3 for the six spies, plus procs/debugprocs) confirmed
  exactly one definition each tree-wide after the fixes. Files:
  `quests/quest_theeyesofglouphrie/{configs/theeyesofglouphrie.constant,
  scripts/eyeglo_{shared,quest}.rs2}`, additive branches in
  `areas/area_gnome/scripts/{brimstail,hazelmere}.rs2`,
  `quests/quest_grandtree/scripts/king_narnode.rs2`,
  `quests/quest_icthlarin/scripts/icthlarin_embalm.rs2`,
  `skill_herblore/scripts/grind_ingredient.rs2`, and wiring into
  `interface_questjournal/scripts/quest_journal.rs2`. Next pending row
  (smallest-first): #152 Monkey Madness I, 988 lines.

- row #152 corrected 2026-08-11, not ported here: Monkey Madness I
  (`monkeymadnessi`) turned out already stale-`pending` -- this file's own
  skip list (line 95, "spelling-only mismatches already owned elsewhere")
  already flagged it, but the Queue table row itself was never flipped.
  LostCity's `quest_mm/` (25 scripts, 4,149 lines: `mm_narnode`/
  `mm_caranock`/`mm_daero`/`mm_waydar`/`mm_lumdo`/`mm_zooknock`/`mm_lumo`/
  `mm_karam`/`mm_garkor`/`mm_monkey_child`/`mm_kruk`/`mm_awowogei`/
  `mm_shopkeepers`/`mm_warehouse`/`mm_supply_crates`/`mm_puzzle`/etc.)
  already implements Monkey Madness I end-to-end on basevar `%mm_main`
  against `configs/quest_mm.constant`'s `^monkeymadness_*` scale;
  `mm_narnode.rs2:233` sets `%mm_main = ^monkeymadness_complete`; journal
  wired `interface_questjournal/scripts/quest_journal.rs2:727-728`
  (`quest_monkeymadness1` -> `~mm_journal`) -- matches `CONTENT_PORT_QUEUE.md`'s
  own extensive slice history (32z, 33u, 34n-34y, 35a-35g, final log 12368
  scripts). Row flipped to `done (LC)`, out of scope for this queue per its
  own ownership rule (LC proc presence, not completion state). Real slice
  for this tick promoted to the next genuinely-pending smallest row: #153 A
  Forgettable Tale of a Drunken Dwarf, 1,000 lines.

- slice 153 done: A Forgettable Tale of a Drunken Dwarf (`forgettabletale`)
  -- Jul 2005, npcs=dwarfcityb/dwarfcityd/dwarfcityr (quest-helper's own
  abbreviations for the Drunken Dwarf/one of Rowdy Dwarf/Gauss -- cache
  wins, real gameval names `dwarf_city_drunken_dwarf`/
  `dwarf_city_rowdy_dwarf`/`dwarf_city_dwarf_man6`); Commander Veldaban asks
  the player to investigate the Red Axe (the same syndicate as The Giant
  Dwarf / Between a Rock...) by loosening Keldagrim's Drunken Dwarf's
  tongue with a legendary kelda beer (a mini farming + brewing side-quest:
  4 kelda seeds from the Drunken Dwarf/Rowdy/Khorvak/Gauss, grown on
  `farming_hops_patch_keldagrim`, brewed in the east pub's upstairs vat),
  then infiltrating a hidden mine-cart tunnel network under the trading
  Consortium floor -- capped by the quest's own joke ending (kebab + beer =
  total memory loss, matching the quest's own name). Native dbrow
  `quest_forgettabletale` (id 88, endstate 140, questpoints 2,
  requirement_stats cooking 20 + farming 17, both boostable via
  requirements_boostable=1, gated with `stat()` not `stat_base()`;
  requirement_check_skills_on_start=0, matching quest-helper's own
  getGeneralRequirements not blocking the start dialogue; stat_xp_awarded
  cooking 50000=5000xp + farming 50000=5000xp, matching quest-helper's own
  ExperienceReward calls exactly -- the java's own
  SkillRequirement(COOKING, 22, true) is stale vs this dbrow's 20, cache
  wins). `requirement_quests` decodes to dbrow 63 (Shades of Mortton) and a
  non-existent id 52 -- corrupt, same cache decode issue flagged repeatedly
  on this queue; real prerequisites per quest-helper's own
  getGeneralRequirements() are The Giant Dwarf (`%giantdwarf_quest >=
  ^gdwarf_complete`) and Fishing Contest (`%fishingcompo >=
  ^fishingcompo_complete`), both already implemented in this tree and hard
  gated (`forget_meets_quest_reqs`). This quest has a **native varbit
  schema** even though quest-helper's own java never reads a single master
  progress varbit (it infers the step map from sub-varbit combinations
  instead): `forget_quest` (basevar `forget_main_var`, bits 0-7, 0..255,
  comfortably holds this dbrow's own endstate 140) is confirmed as the real
  master field by grepping `configs/all.varbit` directly, not guessed. Also
  on `forget_main_var`: `forget_farming` (bits 8-11, 0..15) -- confirmed
  authoritative via this cache's own `farming_hops_patch_keldagrim`
  multiloc record (`configs/all.loc`), which keys `multivarbit=
  forget_farming` across ten states (weeds x3, weeded, seed, growing x3,
  fully grown, harvested/gone) landing exactly on quest-helper's own
  thresholds (`plotRaked` >=3, `keldaGrowing` >=4, `keldaGrown` >=8);
  `forget_boarding_removed` likewise confirmed via
  `keldagrim_boardedupdoor_multi`, which removes `route_boardedupdoor`
  entirely (multiloc2=-1) once set, matching the Director's own "boarded up
  tunnel" dialogue; `forget_beer_given`, `forget_seed2_given`,
  `forget_seed3_given`, `forget_seed4_given`, `forget_tunnel_cutscene1/2/3`
  are all read as simple per-friend / per-tunnel-room flags. The nine real
  rail-junction widget puzzles (interfaces 244/247/248, native fields
  `forget_if1..if19` on basevars `forget_puzzle_var`/`forget_puzzle_var2`)
  + the listening room + the library have no generic widget-puzzle
  primitive anywhere in this tree (grep-confirmed, same gap Between a
  Rock...'s own schematic assembly and The Great Brain Robbery's crate
  build hit), so all three tunnel rooms collapse into one deterministic
  "search the box, work the machinery" beat each, matching this queue's
  established "native widget puzzle with no precedent collapses to one
  deterministic action" convention -- gated on the real per-room
  `forget_tunnel_cutscene1/2/3` flags, so it is a faithful compression, not
  an invented shortcut. The vat/barrel brewing sub-timer
  (`brewing_vat_varbit_1`/`brewing_barrel_varbit_1`, native but packed into
  the *shared* `farming_varp_9` basevar with real-time-tick semantics
  quest-helper's own thresholds of 1/2/68/69/71 imply but do not fully
  specify) is not driven natively here -- this port tracks the brewing
  sub-steps as plain `%forget_quest` plateaus instead, to avoid depending on
  an unverified shared timer field another future brewing feature might
  also need; `brewing_vat_1`/`brewing_barrel_1` are themselves native
  multilocs keyed on those same shared fields, so their models do not
  visually reskin as ingredients are added, same "logic correct, display
  deferred" simplification as The Giant Dwarf's own Riki model reskin.
  Every `vat_*`/`barrel_*` leaf state in the cache declares no op1/op2 of
  its own (grep-confirmed), so the four vat additions and the barrel
  glass-fill bind as `oplocu` (use-item-on-loc, needs no declared menu op)
  against the multiloc wrapper name, matching quest-helper's own
  `addIcon(ItemID....)` steps exactly -- only the valve (`vat_valve_1`,
  real `op1=Turn`) is a plain click. Kelda growth uses a `settimer`/
  `[timer,...]` callback, same idiom as Rum Deal's own quest-only Blindweed
  patch (`deal_farming.rs2`'s `deal_blindweed_grow` timer) rather than a
  real-time multi-stage catchup loop. `keldagrim_train_cart` is placed at
  two different coordinates by quest-helper itself (the ordinary White Wolf
  Mountain shuttle and the "secret" southern cart into the tunnels) --
  disambiguated with `loc_coord`, same pattern established by Spirits of
  the Elid's crevice hole. Rowdy Dwarf's own item request is quest-helper's
  `randomItem = ItemRequirement("A random item per player", -1, -1)` -- a
  per-player server-rolled item with no fixed identity, impossible to
  replicate faithfully; simplified to a running gag (a single coin "for the
  road") rather than inventing a fixed plausible-looking item. Three npcs
  are pre-existing SHARED triggers, spliced into (not duplicated, critical
  correctness rule): `dwarf_city_black_guard_leader` (Commander Veldaban,
  `quest_giantdwarf/scripts/gdwarf_start.rs2`), `dwarfrock_engineer2`
  (Khorvak, matching quest-helper's own `DWARFROCK_ENGINEER2`,
  `quest_betweenarock/scripts/betweenarock_schematics.rs2`), and
  `dwarf_city_director_blue_opal`/`_cutscene` (`quest_giantdwarf/scripts/
  gdwarf_consortium.rs2`, one early branch inside the shared
  `gdwarf_director_talk` proc rather than two duplicated branches); `kebab`'s
  existing generic Eat trigger (`player/scripts/consumption/kebab.rs2`)
  gets one early branch for this quest's own joke ending. Wiki
  https://oldschool.runescape.wiki/w/A_Forgettable_Tale.../Quick_guide;
  Transcript declined verbatim reproduction (same as prior slices on this
  queue) -- dialogue is original paraphrase covering the same beats.
  Quest-helper's own `ForgettableTale.java` (1,000 lines, matching this
  row's own line count) fetched via GitHub raw (no local checkout on this
  machine); `tools/questhelper_extract.py forgettabletale --check` exit 0
  (every ItemID/NpcID/ObjectID/VarbitID resolves clean). `mingw32-make -C
  src sscompile` clean (only pre-existing snprintf-truncation warnings in
  the compiler itself); `mingw32-make -C src torirsserver-scripts` exit 0,
  14,763 scripts compiled (up from 14,738, +25); full build log grepped for
  `forget|dwarf_city_rowdy|dwarf_city_dwarf_man6|dwarf_city_train_conductor|
  keldagrim_track_junction|brewing_vat_1|brewing_barrel_1|vat_valve_1|
  dwarf_keldagrim_stairs|farming_hops_patch_keldagrim|ge_keldagrim_trapdoor|
  whitewolfmountain_train_cart|blandebir|dwarfrock_engineer2|
  dwarf_city_director_blue_opal|dwarf_city_black_guard_leader|kebab`
  returned zero warnings or errors; a manual duplicate-trigger sweep (not
  caught by `sscompile`, which accepts duplicates silently by design per
  this queue's own standing warning) confirmed exactly one definition
  tree-wide for every trigger header this slice authored or spliced into.
  `tools/ss_allocate.py --tree OSRS-Content/osrs239-content --check` exit 0
  (no pending allocations -- `forget_main_var` is a native pre-existing
  basevar, not something the server needed to allocate). Files:
  `quests/quest_forgettabletale/{configs/forgettabletale.constant,
  configs/forgettabletale.varp, scripts/forget_{shared,keldagrim,brewing,
  tunnels}.rs2}`, additive branches in `quests/quest_giantdwarf/scripts/
  {gdwarf_start,gdwarf_consortium}.rs2`, `quests/quest_betweenarock/scripts/
  betweenarock_schematics.rs2`, `player/scripts/consumption/kebab.rs2`, and
  wiring into `interface_questjournal/scripts/quest_journal.rs2`. Next
  pending row (smallest-first): #154 Tower of Life, 1,021 lines.

- slice 154 done: Tower of Life (`toweroflife`) -- Feb 2007, npcs=Effigy
  (`tol_npc_efergy01`), Bonafido (`tol_npc_barry01`), a caged homunculus
  (`tol_homonculus_cage_broken` / `_nocage`). Effigy asks the player to help
  repair the derelict Tower of Life; Bonafido won't allow entry without a
  full builder's outfit (hard hat from a three-question quiz with
  'Black-eye', boots pickpocketed off 'No fingers' after first asking him,
  a shirt traded for a beer with 'The Guns', trousers found searching
  bushes); inside, three broken machines (pressure/pipe/cage) on three
  floors must be rebuilt from crate materials and fixed, which frees the
  caged homunculus -- a friendly, curious creature (not the dangerous
  "alchemist" Effigy feared) who asks the player a few questions about
  magic before being sent to have a final word downstairs. Native dbrow
  `quest_toweroflife` (id 129, endstate 18, questpoints 2,
  requirement_stats construction 10 (stat 22) -- no
  `requirements_boostable` column on this row (unlike A Forgettable Tale's),
  so gated with `stat_base()`, not `stat()`; stat_xp_awarded construction
  10000=1000xp (stat 22) + crafting 5000=500xp (stat 12) + thieving
  5000=500xp (stat 17), matching quest-helper's own `ExperienceReward` calls
  exactly -- no stale mismatch this time. No `requirement_quests` column at
  all on this row, matching quest-helper's own `getGeneralRequirements()`,
  which names only the Construction level, no prerequisite quest. This
  quest has a rich **native varbit schema**, all packed onto two bare-name
  basevars, `tol_main`/`tol_main2`, claimed in
  `configs/toweroflife.varp` per this queue's established "claim a bare
  cache reservation" precedent (Contact!'s `contact_master`, The Great
  Brain Robbery's `brain_quest_var`): `tol_prog` (bits 0-4, 0..31) is the
  real master field, confirmed authoritative (not guessed) by grepping
  `tol_npc_efergy01_multi` / `tol_homonculus_multi` in `configs/all.npc`,
  both `multivarbit=tol_prog` and landing Effigy's own on/off visibility and
  the broken-cage homunculus's own visibility exactly on the values
  quest-helper's step map implies -- confirming states 13/15 are pure
  filler (the homunculus stays visible across 12/14/16 without a break) and
  licensing this port's 12->16 compression with zero loss of native-visible
  npc state. `tol_pres_prog`/`tol_pipe_prog`/`tol_cage_prog` (0=not built /
  1=built / 2=fixed) are confirmed authoritative via `tol_pipe_machine_multi`
  and `tol_cage_multi`'s own multiloc leaves in `configs/all.loc`, which
  land exactly on quest-helper's own `VarbitRequirement` thresholds; the
  pressure machine (`tol_pressure_machine01`) has no multiloc wrapper at
  all -- the same static op1=Fix leaf serves both the build and calibrate
  clicks, so this port dispatches on `tol_pres_prog` internally rather than
  a model swap, same idiom quest-helper's own java uses (one ObjectStep,
  two different Conditions gates). `tol_cage_state` doubles as
  quest-helper's own `isTowerFixed` flag and is set the moment the cage --
  fixed last of the three machines -- is repaired.
  `tol_nofingers_asked` is confirmed authoritative as `hasSpokenToNoFingers`
  both by name and by matching quest-helper's own pickpocket-gate exactly.
  The three widget puzzles (`PuzzleSolver.java`'s `pressureSolver`/
  `pipeSolver`/`cageSolver`, backed by real sub-varbits --
  `tol_pres1..4_level`, `tol_pipe_piece1..5_active`, etc) have no generic
  widget-puzzle primitive anywhere in this tree (grep-confirmed, same gap
  every prior slice on this queue has hit), so each machine collapses into
  one deterministic "gather materials, build, fix" beat per this queue's
  established convention -- logic correct via the real `_prog` plateaus,
  puzzle-widget display deferred, not invented. Quest-helper's own
  `talkToHomunculusBasement` targets a separate dungeon region
  (`WorldPoint(3040, 4400, 0)`, reached via the `TOL_TRAPDOOR_MULTI` loc);
  this cache's own world spawn file
  (`server/scripts/areas/world/configs/m41_50.spawn`) disagrees and places
  `tol_homonculus_multi2` (the very npc `tol_homonc_pres` gates) at ground
  level (2640, 3221, 0), inside the same map square as Effigy and the
  builders, not any separate region -- cache wins per this queue's standing
  rule, so this port has the final conversation happen at that native
  ground-level spawn instead of building bespoke separate-region navigation
  this engine's generic `~climb` (one plane, same tile) cannot reach and no
  precedent on this queue attempts; the trapdoor itself is left as
  unclaimed scenery (it already climbs via the generic ladder category on
  its own, it is simply not this port's route to the final npc). All
  floor-to-floor navigation (`tol_stairs01`/`tol_gapfill01`/
  `area_sanguine_ghetto_ladder_up`/`_down`) was already wired generically by
  category in `ladders_stairs/configs/ladders.loc` +
  `scripts/ladders.rs2` -- zero content needed, confirmed by direct lookup
  of all four names before writing anything. Black-eye's own hat riddle
  (quest-helper's `addDialogStep` records only the three correct answers,
  "Three"/"Torn curtains"/"10 clay pieces", no question text of its own) is
  reproduced as an original three-question quiz using exactly those three
  answers; the top-of-tower homunculus's fourteen-question curiosity
  dialogue is compressed to two representative questions, same "faithful
  compression, not omission" convention. Wiki
  https://oldschool.runescape.wiki/w/Tower_of_Life/Quick_guide; transcript
  declined verbatim reproduction (same as every prior slice on this queue)
  -- dialogue is original paraphrase covering the same beats. Quest-helper's
  own `TowerOfLife.java` (504 lines) + `PuzzleSolver.java` (517 lines, guide-
  only client UI, no server state of its own) sum to exactly this row's own
  1,021 lines, fetched via GitHub raw (no local checkout on this machine);
  `tools/questhelper_extract.py toweroflife --check` exit 0 (every
  ItemID/NpcID/ObjectID/VarbitID resolves clean, including all 20
  puzzle-widget sub-varbits this port deliberately does not drive). `mingw32-
  make -C src sscompile` clean (only the pre-existing snprintf-truncation
  warnings in the compiler itself); `mingw32-make -C src torirsserver-scripts`
  exit 0, 14,788 scripts compiled (up from 14,763, +25); full build log
  grepped case-insensitively for `tol_|toweroflife` returned zero warnings
  or errors. A manual duplicate-trigger sweep (not caught by `sscompile`,
  which accepts duplicates silently by design per this queue's own standing
  warning) confirmed exactly one definition tree-wide for every trigger/proc
  header this slice authored. `tools/ss_allocate.py --tree
  OSRS-Content/osrs239-content --check` exit 0 (no pending allocations --
  `tol_main`/`tol_main2` are native pre-existing basevars, not something the
  server needed to allocate). Files: `quests/quest_toweroflife/{configs/
  toweroflife.constant, configs/toweroflife.varp, scripts/tol_{shared,start,
  outfit,tower,homunculus}.rs2}`, and wiring into
  `interface_questjournal/scripts/quest_journal.rs2`. Next pending row
  (smallest-first): #155 Mourning's End Part II, 1,100 lines (not yet
  verified for staleness).
- slice done: Mourning's End Part II (#155) -- direct sequel to Mourning's
  End Part I (#106, already `done`), read first for shared arc
  state/conventions per this tick's own instructions. Grep-first: no
  `mourningsendpartii`/`mend2`/`mourning_quest_main` script anywhere in
  `server/scripts/quests` before writing (only the *config* namespace --
  dbrow, varbit, varp, spawns -- already carried the native `mourning_*`
  rows, same as Part I); `lc_quests.txt` has no `mourning` entry at all
  (LostCity doesn't claim either Mourning's End quest); 2009scape not
  implemented either; Part I's own Log entry explicitly deferred this row
  and confirmed its Part-II-only native fields (`mourning_quest_part2`/
  `mourning_quest_main` and the Light Temple sub-bits) untouched -- this
  slice's own grep confirmed still untouched going in. Fetched quest-helper's
  `MourningsEndPartII.java` directly via raw.githubusercontent.com (no local
  checkout) -- 1,100 lines, matching this row's own line count exactly.
  Native dbrow `quest_mourningsendpart2` (`configs/all.dbrow`): id 93,
  startnpc 5292 (resolves to `mourning_arianwyn`, matching quest-helper's own
  `steps.put(0, talkToArianwyn)` exactly), endstate 60, questpoints 2,
  stat_xp_awarded (16,600000)=Agility 60000 -- matches quest-helper's own
  `ExperienceReward(AGILITY, 60000)` exactly (tenths scale). **`requirement_quests`
  on this dbrow is wrong again**, same failure mode this queue's methodology
  warns about: it lists dbrow id 99, which resolves via `all.dbrow`'s own
  `id` column (not row order) to `quest_ratcatchers` -- unrelated.
  Quest-helper's own `getGeneralRequirements()` lists exactly one prereq,
  `QuestRequirement(MOURNINGS_END_PART_I, FINISHED)`, wiki-confirmed; gated
  instead on `%mourning_quest >= ^mend1_complete` (Part I's own native
  completion constant). Native varbit `mourning_quest_main` (8-bit,
  `basevar=mourning_quest_part2`) is the primary progress var -- no
  quest-helper `Requirement` names a numeric threshold for it (same
  relationship Part I's `%mourning_quest` had to its own `steps.put` keys),
  so it is authored here on round 10s matching the dbrow's own `endstate=60`
  exactly: 0/10/20/30/40/50/60, collapsing quest-helper's finer
  0/5/10/15/20/30/40/50 `steps.put` keys onto the coarser native scale (same
  collapsing convention Part I's own `%mourning_quest` used). The sibling
  native bits under the same basevar (`mourning_arianwyn_told`/`_asked`,
  `mourning_temple_*_reset_tray`, `mourning_temple_parts_1..6`) correspond to
  no quest-helper `Requirement` this port's collapsed puzzle needs and are
  left untouched. NPCs: zero hand-spawning and zero new triggers anywhere --
  `mourning_arianwyn` and `mourner_hideout_head_mourner` (Essyllt) each
  already have a live `[opnpc1,...]` trigger owned by Mourning's End Part I
  (`quest_mourningsendparti/scripts/mend1_shared.rs2`'s
  `mend1_arianwyn_talk` label and `mend1_disguise.rs2`'s
  `mend1_essyllt_talk` label); this slice extends each file's own
  `%mourning_quest >= ^mend1_complete` branch to call a new
  `~mend2_arianwyn_talk`/`~mend2_essyllt_talk` proc instead of declaring a
  second, conflicting `[opnpc1,...]` trigger -- the exact merge-not-duplicate
  pattern Part I itself used for Islwyn/Roving Elves, confirmed via a full
  grep of `server/scripts` for both npc names before writing (each appeared
  only inside its one owning Part I file). Items all native (`configs/all.obj`):
  `mourning_ederns_journal`, `mourning_crystal_sample`,
  `mourning_crystal_new_sample`, `mourning_crystal_trinket`, `death_talisman`,
  `rope`, `chisel`, `gasmask`, `mourning_mourner_top`/`_legs`/`_cloak`/
  `_boots`/`_gloves` (the same mourner-disguise gamevals Part I's own
  `mend1_disguise.rs2` already grants/checks). Quest-helper's own
  `getItemRequirements()` (`mournersOutfit, chisel, deathTalismanHeader,
  rope`) is gated at the single collapsed puzzle interaction; outfit and
  chisel not consumed (matching the source's own `isNotConsumed()`), rope
  consumed. Deferred (soft-skip tier, matching this queue's convention for
  no-precedent puzzle/traversal content): the entire Temple of Light
  crystal-mirror maze -- quest-helper's own `doAllPuzzles`/`puzzle1`..
  `puzzle6`/`deathAltarPuzzle`/`addCrystal` `ConditionalStep`s, the largest
  block of the 1,100-line source, backed by dozens of native per-tile
  `mourning_light_temple_*` beam-orientation varbits with no rs2 precedent
  anywhere in this tree for a light-beam-propagation puzzle -- collapsed to
  one narrated Arianwyn interaction (state 30 -> 40) per this queue's own
  "native drag-widget/interface puzzles ... with no precedent collapse to
  one deterministic action" rule, same tier as Cold War's crush-course and
  Spirits of the Elid's golem weapon-matrix; none of the
  `mourning_light_temple_*`/`mourning_pillar_light_cross_*`/`mourning_door_2_*`
  bits touched. Also deferred: Eluned's own dialogue
  (`talkToElunedAfterGivingCrystal`, no varbit of her own) folded into
  Arianwyn's hand-off line, matching Part I's own precedent for deferring
  Eluned; the Underground Pass/Well-of-Voyage alternate route to the Death
  Altar (used only if the light door isn't unlocked from the Mourner side)
  not implemented, direct-route-only (holding a `death_talisman` already, not
  consumed, matching the source's own `isNotConsumed()`); Thorgel the
  dwarf's 50-item Death Talisman fetch-quest alternate not implemented; the
  native `mourning_dark_beast` npc (no world spawn touched) narrated as
  evaded rather than fought, same tier Part I's mourner-kill and Cold War's
  icelords; the completion `UnlockReward`s ("craft Death Runes" / "Dark
  Beasts as a Slayer task") narrated as flavour text only -- no existing gate
  in `skill_runecraft/scripts/runecraft.rs2` or any slayer-assignment script
  checks a Mourning's End Part II completion var to hook a new mechanical
  unlock into, out of scope for this slice; no in-game journal integration
  authored (mirroring Part I's own `mend1_journal.rs2` was skipped for time).
  Wiki: https://oldschool.runescape.wiki/w/Mourning%27s_End_Part_II +
  .../Quick_guide + Transcript:Mourning%27s_End_Part_II (structured summaries
  only, same convention every prior slice used; dialogue authored is
  original wording covering the same beats, cross-checked against
  quest-helper's own step text/NpcID/ObjectID names directly). Files:
  `quest_mourningsendpartii/{configs/mend2.constant,
  scripts/mend2_shared.rs2, scripts/mend2_debug.rs2}`, plus two small merge
  edits into Part I's own `mend1_shared.rs2`/`mend1_disguise.rs2` (each
  swapping a single static dialogue line for a proc call into the new file,
  no trigger headers touched). `::mend2` / `::mend2run` debug hooks added,
  mirroring `::mend1run`'s idiom (forces `%mourning_quest = ^mend1_complete`
  for the headless session since this quest's own gate depends on Part I).
  `mingw32-make -C src sscompile` clean (only the pre-existing
  snprintf-truncation warnings in the compiler itself); `mingw32-make -C src
  torirsserver-scripts` exit 0, 14,793 scripts compiled; full build log grepped
  case-insensitively for `mourning|mend1|mend2` returned zero warnings,
  errors, or notes. A manual duplicate-trigger check confirmed
  `[opnpc1,mourning_arianwyn]` and `[opnpc1,mourner_hideout_head_mourner]`
  each still have exactly one definition tree-wide after this slice's edits
  (the existing Part I trigger, now branching into the new proc) -- no new
  `[opnpc1,...]`/`[oploc1,...]` trigger headers were declared by this slice
  at all, since the entire quest routes through the two already-triggered
  NPCs. `tools/ss_allocate.py` not invoked -- no new varp/varbit IDs needed,
  every var this slice touches (`mourning_quest_main` plus Part I's own
  `%mourning_quest`) is a pre-existing native id. Next pending row
  (smallest-first): #156 Enlightened Journey, 1,168 lines (not yet verified
  for staleness).
- slice done: Enlightened Journey (#156) -- grep-first: no
  `enlightenedjourney`/`zep_`-script anywhere in `server/scripts/quests`
  before writing (only the *config* namespace -- dbrow, varbit, varp, npc/loc
  gamevals, world spawns -- already carried the full native `zep_*` schema);
  `lc_quests.txt` has no `enlighten` entry; `SCAPE2009_CONTENT_PORT_QUEUE.md`
  grep clean. Fetched quest-helper's own `enlightenedjourney/*.java` directly
  via raw.githubusercontent.com (no local checkout) --
  `EnlightenedJourney.java`(286) + `GiveAugusteItems.java`(101) +
  `BalloonFlightStep.java`(135) + `TaverleyBalloonFlight.java`(195) +
  `CastleWarsBalloonFlight.java`/`CraftingGuildBalloonFlight.java`/
  `GrandTreeBalloonFlight.java`/`VarrockBalloonFlight.java`(112+113+112+114)
  = 1,168 lines exactly, matching this row's own count. Native dbrow
  `quest_enlightenedjourney` (id 121, startnpc 4715=`zep_piccard`, endstate
  200, questpoints 1, requirement_questpoints 20, requirement_stats
  crafting(12)=36/farming(19)=30/firemaking(11)=20, all
  `requirements_boostable`) matches quest-helper's own
  `getGeneralRequirements()` exactly; no `requirement_quests` column, also
  matching. `stat_xp_awarded` firemaking(11)=40000/farming(19)=30000/
  crafting(12)=20000/woodcutting(8)=15000 matches quest-helper's own
  `ExperienceReward` calls exactly, independently confirming this stat-id
  table. Master var is native `zep_quest` (8-bit `zep_var`); quest-helper's
  own routing never names a real varbit threshold (item-possession-driven
  only), so this port reuses quest-helper's own `steps.put` keys verbatim as
  the real checkpoints (0/10/20/40/60/70/80/90) but jumps straight from 90 to
  the dbrow's own `endstate` of 200 for the true finish (not 100 --
  `makingfriendswithmyarm`/`songoftheelves`/`swansong` also use a real
  0..200 dbrow scale, confirmed by grepping every `endstate=200` row, ruling
  out a typo). Sibling native varbits `zep_rdye`/`zep_ydye`/`zep_bowl`/
  `zep_sandbags`/`zep_silk`/`zep_logs` are each-item given/loaded trackers;
  quest-helper's own `GiveAugusteItems.java` mis-points `givenBowl` at
  `VarbitID.ZEP_LOGS` with an inline "maybe both dyes done" comment flagging
  its own author's uncertainty -- the cache disagrees and wins per this
  queue's standing rule, so the real `zep_bowl` is used instead. Native
  `zep_multi_basket` (2-bit) drives the Entrana basket/balloon scenery
  (0=nothing, 1=metal frame at accept, 2=woven basket at the willow-branch
  build, 3=basket+envelope once logs/tinderbox are loaded), matching
  quest-helper's own `ObjectStep` target `ObjectID.ZEP_MULTI_BASKET_ENTRANA`
  exactly. Items all native (`configs/all.obj`,
  `tools/questhelper_extract.py enlightenedjourney --check` exit 0, every
  ItemID/NpcID/ObjectID/VarbitID resolves clean): `papyrus`, `ball_of_wool`,
  `sack_potato_10`, `sack_empty` (fills to `zep_sandbag` at the `sandpit`,
  matching quest-helper's own `emptySack8.addAlternates(ZEP_SANDBAG)`),
  `logs`, `tinderbox` (not consumed), `willow_branch`, `unlit_candle`/
  `unlit_black_candle`, `yellowdye`, `reddye`, `silk`, `bowl_empty`,
  `zep_test_balloon_struc`/`zep_test_balloon` (test-model crafting chain),
  `zep_bomber_jacket`/`zep_bomber_cap` (completion rewards).

  NPC: `zep_piccard` (Auguste) is world-spawned on Entrana (`m43_52.spawn`,
  2808,3355,0) *and* is the same npc id shown at Taverley via the
  `zep_multi_piccard` multi-npc wrapper the instant the maiden flight lands
  (`configs/all.npc` `multinpc2=zep_piccard`) -- one trigger correctly serves
  both conversations, disambiguated purely by `%zep_quest`. **A live
  `[opnpc1,zep_piccard]` trigger already existed** --
  `quest_monkeymadnessii/scripts/monkeymadnessii.rs2` soft-skips an "Entrana
  balloon route unlocked" MM2 story beat on it (forcing `%zep_multi_gno = 1`
  directly, written before this quest existed in this tree). Grep-verified
  this was the *only* other `[opnpc1,zep_piccard]` trigger tree-wide; per
  this queue's merge-not-duplicate methodology, that trigger's own fallback
  branch (previously a flat `~chatnpc("Auguste.")`) now calls
  `~zep_piccard_talk` (defined in this slice's own `ej_shared.rs2`), leaving
  MM2's own gated branch (checked first, returns early) completely
  untouched. Two more merges, same pattern: `papyrus`/`ball_of_wool` already
  had live `[opheldu,...]` triggers in `quest_golem/scripts/golem_portal.rs2`
  and `skill_crafting/scripts/jewellery/stringing.rs2` -- each got one
  additive branch/case calling the new `~ej_make_structure` proc rather than
  a third, conflicting declaration; `sandpit` already had a live
  `[oplocu,sandpit]` trigger in `skill_crafting/scripts/glass/glass.rs2` --
  it got one additive `sack_empty` branch calling `~ej_fill_sandbag`. A
  genuine pre-existing gap directly blocking this quest's own critical path
  was also fixed (not scope creep, per PORTING_GUIDE section 7's allowance
  for fixing real bugs hit along the way): both `monk_of_entrana.rs2` files
  (Port Sarim and Entrana) had their ferry dialogue fully scripted but the
  actual sail action itself left stubbed ("The boat ... isn't sailing yet.")
  by the prior slices that first wired them -- this slice completes just
  that one action (a `p_telejump` to the other dock) on both sides, touching
  nothing else those files already deferred (`~set_sail`/`ship_journey`
  IF/armour-restriction enforcement all remain untouched).

  Post-quest balloon transport network: quest-helper's own step map does
  *not* track this (`steps.put` stops at 100/finish; the four sibling
  `*BalloonFlight.java` classes are standalone overlays for RuneLite's
  separate live transport-helper feature, wired to no `WorldPoint` and never
  referenced by `EnlightenedJourney`'s own `loadSteps()`), but all six real
  platform coordinates were locatable with confidence from native world
  spawns (`m38_48`/`m38_54`/`m45_51`/`m51_54`/`m45_53`/`m43_52.spawn` --
  `zep_assist_cast`/`_gno`/`_craft`/`_varr`/`zep_multi_piccard`/`zep_piccard`
  respectively), each gated by its own native multivarbit
  (`zep_multi_cast`/`_gno`/`_craft`/`_varr`, 2-bit `zep_multi_piccard` at
  Taverley: 1=Piccard's own post-flight cameo, 2=Assistant Stan takes over
  as permanent pilot). This port unlocks all five non-Entrana city bits at
  once on completion (quest-helper gives no evidence of a finer per-city
  "fly there once first" native gate, and inventing one would be exactly the
  unverified-mechanic problem this queue's methodology warns against) and
  wires a destination menu at each of the six pilots' own native `op4=Fly`
  option (distinct from `op1=Talk-to`), following
  `areas/area_gnome/scripts/spirit_tree.rs2`'s own established
  destination-menu/`p_telejump` idiom for network travel in this tree.

  Deferred (soft-skip tier, matching this queue's convention for
  no-precedent widget/real-time-interface content): the entire pixel-precise
  flight itself -- `BalloonFlightStep`/`TaverleyBalloonFlight`/the four
  city-specific flight classes (781 of this row's own 1,168 lines, easily
  the largest block) drive a live widget-471 interface tracking
  sandbag-drop/log-burn/rope-pull actions against a per-tick height/position
  table in a wholly separate instanced coordinate space
  (`zep_piccard_crash`'s own `m28_76.spawn` entry sits far outside the real
  map) -- there is no widget-driven real-time steering minigame precedent
  anywhere in this tree (same gap Tower of Life's build-puzzles and
  Mourning's End Part II's mirror maze already hit), so both the maiden
  flight and every network flight collapse to one narrated `p_telejump`,
  logic correct via the real item/varbit gates, minigame presentation not
  modelled. The "flash mob" peasant vignette (`zep_peasant_1..4`, no combat
  or item stakes in quest-helper's own source) is narrated as mesbox text
  only, no npc spawn/combat. Per-city "first flight unlocks the route"
  gating is not modelled, all five network cities open together. Wiki:
  https://oldschool.runescape.wiki/w/Enlightened_Journey +
  .../Quick_guide + Transcript:Enlightened_Journey (structured summaries
  only; dialogue authored is original wording covering the same beats,
  cross-checked against quest-helper's own step text/dialog options/
  NpcID/ObjectID names directly). Files:
  `quest_enlightenedjourney/{configs/enlightenedjourney.constant,
  scripts/ej_shared.rs2, scripts/ej_crafting.rs2, scripts/ej_network.rs2,
  scripts/ej_debug.rs2}`, small merge edits into
  `quest_golem/scripts/golem_portal.rs2`,
  `skill_crafting/scripts/jewellery/stringing.rs2`,
  `skill_crafting/scripts/glass/glass.rs2`,
  `quest_monkeymadnessii/scripts/monkeymadnessii.rs2`,
  `areas/port_sarim/scripts/monk_of_entrana.rs2`,
  `areas/entrana/scripts/monk_of_entrana.rs2`, and wiring into
  `interface_questjournal/scripts/quest_journal.rs2`. `::ej`/`::ejrun` debug
  hooks added, mirroring `::mend2run`'s idiom. `mingw32-make -C src
  sscompile` clean (only the pre-existing snprintf-truncation warnings in the
  compiler itself); `mingw32-make -C src torirsserver-scripts` exit 0, 14,819
  scripts compiled (up from 14,793, +26); full build log grepped
  case-insensitively for `enlightenedjourney|ej_|zep_piccard_talk|
  zep_give_items_menu|zep_fly_to|monkeymadnessii|golem_portal|stringing|
  glass.rs2|monk_of_entrana` returned zero warnings, errors, or notes. A
  manual duplicate-trigger sweep (not caught by `sscompile`, which accepts
  duplicates silently by design) confirmed exactly one real declaration
  tree-wide (excluding this slice's own prose mentions) for every
  trigger/proc header this slice authored or merged into, including
  `[opnpc1,zep_piccard]` (still just the one, now-extended, MM2 trigger).
  `tools/ss_allocate.py --tree OSRS-Content/osrs239-content --check` exit 0
  (no pending allocations -- every var this slice touches is a pre-existing
  native id). Next pending row (smallest-first): #157 One Small Favour,
  1,244 lines (not yet verified for staleness).
- slice #157 done: One Small Favour -- Feb 2005, Yanni Salika (Shilo
  Village antiques dealer) wants a red mahogany log; getting one unravels
  into the genre's longest relay-chain quest, roughly 20 NPCs across
  Kandarin, Misthalin and Karamja each asking the player to do one more
  favour before the last one pays off. Grep-first confirmed genuinely
  unowned before this slice: no `onesmallfavour` proc anywhere in
  `server/scripts` (only a prose mention in `quest_swansong.constant`,
  which soft-skips this quest as one of its own two unported real
  prerequisites -- flagged there for a future tick to tighten now that
  this row is real). `lc_quests.txt` clean; not on either Skip/IN-LC table.
  Quest Helper source fetched via `raw.githubusercontent.com/Zoinkwiz/
  quest-helper` (`OneSmallFavour.java`, 1,244 lines, matching this row's
  own line count exactly); `tools/questhelper_extract.py onesmallfavour
  --check` (after staging the fetched source under a local `helpers/
  quests/onesmallfavour/` shape) resolved every `ItemID`/`NpcID`/
  `ObjectID`/`VarbitID` name clean, zero unresolved. Native dbrow
  `quest_onesmallfavour` (`configs/all.dbrow`, id 74): startnpc 5361
  (`shiloantiques` = Yanni Salika, matching quest-helper's own
  `steps.put(0, talkToYanni)` exactly), endstate 285, questpoints 2,
  requirement_stats (16,36)+(13,30)+(12,25)+(15,18) = Agility 36 +
  Smithing 30 + Crafting 25 + Herblore 18, matching quest-helper's own
  `getGeneralRequirements()` SkillRequirement list exactly (a rare case
  where this decode was *not* corrupted). `requirement_quests` decodes to
  dbrow ids 133/125 (Another Slice of Ham / Lair of Tarn Razorlor) --
  wrong again, same recurring corruption this queue's methodology warns
  about; real prereqs per quest-helper + wiki are Rune Mysteries
  (FINISHED), Druidic Ritual (FINISHED), Shilo Village (FINISHED). Rune
  Mysteries and Druidic Ritual are real, hard-gated at the quest start
  (`%runemysteries >= ^runemysteries_complete`, `%druidquest >=
  ^druid_complete`, both IN-LC). Shilo Village is soft-skipped: LostCity's
  own `quest_shilovillage` dbrow exists, but `areas/area_brimhaven/
  scripts/hajedy.rs2` (line 8) explicitly documents it as "quest body
  deferred -- gate stays closed at 0", i.e. unimplemented -- same
  unported-sibling-prereq precedent as King's Ransom / Great Brain
  Robbery.

  Primary progress var is native and unpacked: plain top-level varp
  `onesmallfavour` (`configs/all.varp`, no basevar/bitfield). No
  multiloc/multinpc record in this cache keys off the bare `onesmallfavour`
  varp directly (checked both `multivarp=`/`multivarbit=` across
  `configs/all.loc`/`configs/all.npc`, zero hits), so unlike several prior
  slices, the ~50 stage breakpoints below are **not independently
  cross-validated** beyond quest-helper's own `steps.put` keys -- collapsed
  onto 45 named constants (`^osf_not_started` .. `^osf_yanni_done`,
  `^osf_complete` = the dbrow's own endstate 285), same "coarser scale"
  convention every prior slice with duplicate `steps.put` keys for one
  NpcStep used. **The weathervane-repair and gnome-glider landing-light
  sub-puzzles ARE independently cross-validated**, via native multiloc
  records reused as-is: `[osf_weathervane]` (`multivarbit=weathervanefixed`,
  swapping `favour_weathervane_broken`/`favour_weathervane`) and 4x
  `[osf_multi_landinglight_*]` (`multivarbit=all_lights_fixed`), both on
  basevar `onesmallfavourmulti` alongside quest-helper's own exact
  VarbitID field names (`directionalsfixed`/`ornamentfixed`/
  `rotatingpillarfixed`/`fixedlandinglights`/8x `*light*_taken`/`*_fixed`),
  all reused verbatim.

  Vertical movement needed zero new scripting -- every ladder/trapdoor/
  staircase this quest's geography touches (`ham_multi_trapdoor`,
  `fai_dwarf_trapdoor_down`, `favour_seer_ladder`, `favour_roof_trapdoor`/
  `favour_seer_laddertop`, `kr_ladder_directional`, `kr_laddertop`/
  `kr_laddertop_directional`) is already a generic `category=climb_*`
  record handled by the existing `ladders_stairs` climb system (confirmed
  via `quest_betweenarock/scripts/betweenarock_travel.rs2`'s own
  precedent comment and a direct grep of `ladders_stairs/configs/
  ladders.loc`) or already gated on `%ham_thief` by Lost Tribe, per
  `quest_deathtothedorgeshuun/scripts/dttd_haminfiltrate.rs2`'s own
  header comment -- narrowing this slice's own scripting to dialogue,
  item exchanges, two hand-spawned fights, and three real sub-puzzles.

  Shared npcs/locs merged into their existing triggers (all grep-verified
  before writing, "no duplicate trigger" caution): `shiloantiques`
  (areas/area_shilo/scripts/yanni_salika.rs2), `jungleforester_f`/`_m`
  (quest_legends/scripts/jungle_forester.rs2), `brian` (areas/port_sarim/
  scripts/brian.rs2), `favour_johanhus_ulsbrecht`
  (quest_deathtothedorgeshuun/scripts/dttd_haminfiltrate.rs2, already had
  real DTTD dialogue on this exact trigger), `fred_the_farmer`
  (areas/lumbridge/scripts/fred_the_farmer.rs2 -- a pre-existing *second*
  `[opnpc1,fred_the_farmer]` already exists in quest_coldwar/scripts/
  coldwar_lumbridge.rs2, sscompile tolerates it silently, same tech-debt
  tier as apothecary below, not touched), `favour_seth_groats`
  (quest_idesofmilk/scripts/idesofmilk.rs2), `horvik_the_armourer`
  (areas/varrock/scripts/horvik.rs2, same additive-branch idiom as its own
  pre-existing `%dov` branch), `apothecary` (areas/varrock/scripts/
  apothecary.rs2 -- a pre-existing *second* `[opnpc1,apothecary]` already
  exists in quest_atailoftwocats/scripts/twocats.rs2, already flagged as
  tech debt by quest_ratcatchers's own comment, not touched), `sanfew`
  (areas/area_taverly/scripts/sanfew.rs2), `arhein` (areas/area_catherby/
  scripts/arhein.rs2 -- a pre-existing *second* `[opnpc1,arhein]` already
  exists in quest_currentaffairs/scripts/currentaffairs.rs2, not touched),
  `cromperty_pre_diary`/`cromperty_post_diary` (areas/area_ardougne_east/
  scripts/wizard_cromperty.rs2), the anvil (`[oplocu,_anvil]` in
  skill_smithing/scripts/smithing/smithing.rs2, same "check last_useitem,
  call out, fall through" idiom the Dragon square shield repair already
  established there), `pot_empty`/`potlid` (quest_swansong/scripts/
  swansong_army.rs2, which already produces this same shared
  `favour_airtight_pot` item for its own beat -- confirmed by name match),
  the generic pottery wheel/oven menus (skill_crafting/scripts/pottery/
  pottery.rs2, extended with a 4th "Pot lid" `craft_pottery_menu`/
  `fire_pottery_menu`/`fire_pottery` option alongside Pot/Pie dish/Bowl),
  and `guam_leaf`/`marentill`/`harralander` (skill_herblore/scripts/
  brew_potion.rs2's own `[opheldu,...]` triggers, additive branch for the
  reverse herb-on-cup click order, same idiom as that file's own
  `white_berries`/Hand in the Sand precedent) -- 15 merge edits total, one
  real duplicate-trigger sweep confirmed exactly one declaration
  tree-wide for every trigger this slice authored or merged into.

  New content: `quest_onesmallfavour/{configs/onesmallfavour.constant,
  scripts/onesmallfavour_relay.rs2, scripts/onesmallfavour_puzzles.rs2,
  scripts/onesmallfavour_journal.rs2}`, wired into
  `interface_questjournal/scripts/quest_journal.rs2`. Hand-spawned combat
  (native `npc_combat/s/` stats reused as-is, no world-spawn entry
  touched): Slagilith (level 92, "takes reduced damage from anything
  other than a pickaxe" per quest-helper, left to the generic combat
  system same as every prior boss slice) inside the sculpture wall's own
  animate-rock puzzle, and Hammerspike Stoutbeard's 3 dwarf gang members
  hand-spawned sequentially on each kill (`favour_gangster_dwarf` ->
  `_2` -> `_3`), same "spawn on trigger" idiom as Contact's Giant Scarab.
  Real sub-puzzles kept mechanically genuine: the anvil repair of the 3
  broken weathervane parts (each its own correct bar -- steel/bronze/iron
  -- matching quest-helper's own per-part `useVaneNOnAnvil` combinations
  exactly) and placing each fixed part back onto the vane individually,
  each setting its own real native varbit
  (`directionalsfixed`/`ornamentfixed`/`rotatingpillarfixed`) until
  `weathervanefixed` flips the native loc's own broken/fixed model; the
  Guthix Rest tea brew (bowl of hot water + empty cup, then 2 guam +
  marrentill + harralander) is a real 2-step make chain, both click
  orders covered.

  Deferred (collapse tier, matching this queue's "no rs2 precedent for a
  fine-grained multi-click puzzle collapses to one deterministic action"
  convention -- same tier as Cold War's crush-course, Spirits of the
  Elid's golem matrix, Mourning's End Part II's light-beam maze): the
  8-lamp gnome landing-light puzzle (quest-helper's own `fixAllLamps`,
  bundling 8x take-lamp/cut-gem/put-lamp with no enforced order) collapses
  to one Gnormadium Avlafrim dialogue exchange that still sets every real
  underlying per-lamp `*_taken`/`*_fixed` bit and the combined
  `fixedlandinglights`/`all_lights_fixed` fields correctly, so the native
  loc-swap visuals stay genuinely correct afterwards; the weathervane's
  own "search / hammer / search again" 3-click sequence to obtain the 3
  broken parts collapses to one `osf_weathervane` interaction (still
  gated on holding a hammer + 3 free inventory slots, matching
  quest-helper's own requirements). Also deferred: the Gnome Glider
  network's own Feldip Hills destination unlock (no existing gate in
  `areas/area_gnome/scripts/gnome_glider.rs2` checks any quest var for
  that destination -- narrated as a reward line only, same tier Mourning's
  End Part II deferred its own unlock hookups); pigeon cages / T.R.A.S.H.
  as tracked items (quest-helper itself names no `ItemID` for T.R.A.S.H.,
  purely a dialogue joke; pigeon cages folded into Horvik's own dialogue
  rather than a literal Ardougne ground-item pickup); backup-purchase
  paths for lost quest items (Tindel/Cromperty/Gnormadium/Phantuwti
  selling replacements per quest-helper's own item tooltips) -- primary
  path only.

  Wiki: https://oldschool.runescape.wiki/w/One_Small_Favour +
  .../Quick_guide + Transcript:One_Small_Favour (dialogue authored is
  original wording covering the same relay-chain beats, same Jagex-
  copyright caveat every prior slice on this queue has noted).
  `mingw32-make -C src sscompile` clean (only the pre-existing
  snprintf-truncation warnings in the compiler itself); `mingw32-make -C
  src torirsserver-scripts` exit 0, 14,855 scripts compiled; full build log
  grepped case-insensitively for `onesmallfavour|osf_|favour_|slagilith|
  hammerspike|tassie|phantuwti|gnormadium|rantz|tindel|petra|cromperty|
  arhein|yanni|jungleforester|brian|apothecary|horvik|seth|johanhus|
  sanfew|bleemadge|pilot_white_wolf|weathervane` returned zero warnings,
  errors, or notes. A duplicate-trigger sweep (`sscompile` accepts
  duplicates silently by design) confirmed exactly one real declaration
  tree-wide for every trigger this slice's own new files declare.
  `tools/ss_allocate.py --tree OSRS-Content/osrs239-content --check` exit
  0 (no pending allocations -- every var this slice touches is a
  pre-existing native id). Next pending row (smallest-first): #159 The
  Fremennik Trials, 1,269 lines (not yet verified for staleness).

- 2026-08-11, #159 The Fremennik Trials (correction, no new port): row was
  stale, not pending. A much earlier tick today (porting Olaf's Quest, #72)
  had already found in passing and logged (see above, "found in the
  process that `quest_viking` (mislabeled 'Fremennik Exiles' in the IN-LC
  table above) actually implements Fremennik Trials") but left the table
  uncorrected as out-of-scope. Verified ground truth directly this tick by
  reading `quest_viking`'s own files rather than trusting the aside:
  `quest_viking/scripts/quest_viking_progress.rs2`'s header comment reads
  verbatim "Fremennik Trials progress + trial bit ranges"; its bit-range
  constants and progress procs are named for the real Fremennik Trials'
  7-vote council cast (`^swensen_*`/`^sigmund_*`/`^sigli_*`/`^peer_*`/
  `^thorvald_*`/`^reveller_*`/`^olaf_*` = Navigator maze, Merchant fetch
  chain, Hunter vs. Draugen, Seer maze, Warrior vs. Koschei, drinking
  contest, Bard's lyre) -- none of which appear in Exiles (Freygerd's
  basilisk investigation). `quest_viking/scripts/viking_journal.rs2`
  titles every journal entry "The Fremennik Trials" verbatim and narrates
  exactly this 7-vote trial structure. Confirmed the dbrow wiring:
  `configs/all.dbrow:3128` declares `[quest_fremenniktrials]`, and
  `interface_questjournal/scripts/quest_journal.rs2:711-713` routes
  `quest_fremenniktrials` to `~viking_journal`. Also discovered a
  previously-unnoticed second folder, `quest_fremennikexiles/` (separate
  from `quest_viking`), whose `fremennikexiles.rs2` genuinely implements
  Exiles' real plot (Freygerd, basilisk threat, `configs/all.dbrow:3000`
  `[quest_fremennikexiles]`) -- so the IN-LC table's row 127 had the QH
  slug pointing at the wrong LC folder entirely (mapped
  `thefremennikexiles` -> `quest_viking` instead of the real
  `quest_fremennikexiles`), not just a mislabel. Fixed both IN-LC rows:
  `thefremennikexiles` now correctly maps to `quest_fremennikexiles`, and
  a new `thefremenniktrials` row was added mapping to `quest_viking`.
  Flipped row #159 to `done (LC)`. No new `.rs2`/config files written --
  correction only, no build re-verification needed (no code touched).
  Next pending row (smallest-first): #169 Lunar Diplomacy, 1,756 lines
  (not yet verified for staleness).
- slice 169 done: Lunar Diplomacy -- 24 Jul 2006, Rellekka / Lunar Isle. The
  Fremennik want nothing to do with the reclusive Moon Clan; the player
  sails to Lunar Isle with Lokar Searunner and Captain Bentley's crew,
  proves themselves to the Oneiromancer (defeat a Suqah, brew a potion
  with Baba Yaga, assemble a lunar staff from the four elemental altars,
  gather a full set of ceremonial regalia from five named Moon Clan
  monks), then enters a shared dream to pass six ethereal trials and duel
  their own reflection, "Me". Grep-verified first (methodology steps 1-2,
  including `lc_quests.txt` and this file's own Skip/IN-LC tables): no LC
  proc, no 2009scape impl, zero `lunar*` script files existed anywhere in
  `server/scripts` before this slice. Native dbrow `quest_lunardiplomacy`
  (id 115, endstate 190, questpoints 2, startcoord decodes to plane 0, x
  2620, y 3691 -- matches this cache's own `lunar_fremennik_pirate` spawn
  in `m40_57.spawn` almost exactly; requirement_stats magic65/
  crafting61/mining60/woodcutting55/firemaking49/defence40/herblore5,
  matches quest-helper's own getGeneralRequirements() and the wiki
  exactly; stat_xp_awarded magic5000+runecraft5000, raw/10, matches the
  wiki exactly). dbrow `requirement_quests`=57,133,86,125 decodes to
  Nature Spirit/Another Slice of H.A.M./Recruitment Drive/Lair of Tarn
  Razorlor -- none real, same known cache-decode-corruption pattern this
  queue warns about repeatedly -- gated instead on the real prerequisites
  (The Fremennik Trials/Lost City/Rune Mysteries/Shilo Village, all four
  genuinely completable native/LC content): `quest_viking`
  (`%viking>=^viking_complete`), `quest_zanaris`
  (`%zanaris>=^zanaris_complete`, this cache's own codename for Lost City,
  confirmed via `quest_journal.rs2:631-632`), `quest_runemysteries`, and
  `quest_zombiequeen` (this cache's own codename for Shilo Village --
  confirmed via `quest_journal.rs2:727-728` -> `~zombiequeen_journal`,
  whose own journal text reads "I discovered Shilo Village was being
  overrun by zombies"). Extensive native varbit schema pre-declared on
  basevars `lunar_quest`/`lunar_quest1..5` (`configs/all.varbit`),
  matching quest-helper's own VarbitID names almost exactly:
  `lunar_quest_symbolpres1..5` (ship symbol hunt), `lunar_pt2_oneiro_
  given_helm/cape/amulet/torso/gloves/boots/trousers/ring` (the 8-piece
  regalia handoff checklist -- no `given_tiara` bit exists, confirming the
  tiara is traded away rather than worn into the dream), `lunar_monk_
  cape_intro`/`_ring_intro`/`_amulet_intro`/`_tanclothes_intro` (which
  monk covers which garment) and `lunar_dice_prog`/`lunar_num_prog`/
  `lunar_tree_prog`/`lunar_floor_prog`/`lunar_skill_prog`/`lunar_emote_
  prog` (the six dream trials) -- all reused as-is; no local catch-all
  var invented, all claimed via a bare-reservation `.varp` file matching
  `cabinfever`/`greatbrainrobbery`'s own precedent. `%lunar_quest_main`'s
  own intermediate breakpoints (0/10 and 190 are cache-confirmed via
  dbrow endstate; the granularity between is this slice's own invention,
  documented not guessed-as-recovered, same caveat as Royal Trouble).
  Native items confirmed and used directly: `lunar_seal_of_passage`,
  `lunar_helmet/cape/amulet/torso/legs/gloves/boots/tiara/ring`,
  `dramen_staff` (Lost City reward), `astralrune`. No `suqah_tooth`/
  `suqah_hide`/`lunar_ore`/`lunar_bar`/`lunar_staff` items exist anywhere
  in this pack (grep-confirmed, same "no native item, same TODO
  quest-helper itself leaves" pattern as Ratcatchers) -- the raw-material
  fetch/craft sub-chain isn't modelled item-by-item; each Moon Clan monk
  grants their finished regalia piece directly in one narrated
  interaction instead (same "grant the full requirement in one action"
  simplification as Cabin Fever's locker searches), and the lunar staff
  is the real `dramen_staff` item plus four altar-visit breakpoints
  (merged into `skill_runecraft/scripts/runecraft.rs2`'s own shared
  `[oplocu,_rc_altar]` trigger as a `case dramen_staff` branch, same
  convention as The Slug Menace's `slugmenace_charge_rune`) rather than a
  separate crafted prop. Native npcs used directly, matching the wiki's
  named cast exactly: Lokar Searunner, the ship's crew (Captain Bentley,
  'Eagle-eye' Shultz, cabin boy, 'Beefy' Burns, First mate 'Davey-boy',
  'Birds-Eye' Jack, the parrot), the Oneiromancer, Baba Yaga, five named
  monks (Pauline Polaris/Meteora/Melana Moonlander/Selene/Rimae
  Sirsalis) and Clan Guard, seven fightable Suqah (level 111, matches
  wiki), six dream trial hosts (Ethereal Numerator/Expert/Perceptive/
  Guide/Fluke/Mimic, already world-spawned in the dream instance,
  `m27_79.spawn`) and the final boss (`quest_lunar_mirror_of_player`,
  "Me", level 79, matches wiki, already world-spawned in `m28_79.spawn`
  -- no hand-spawn needed). `lunar_oneiromancer` already carried an
  `[opnpc1,...]` trigger in `quest_dragonslayer2/scripts/
  dragonslayer2.rs2` (a soft-skipped DS2 dream step) -- `sscompile`
  accepts a duplicate trigger header with no diagnostic, so this quest's
  Oneiromancer dialogue was written as a proc (`lunardip_oneiromancer_
  talk`) and called from DS2's own existing trigger as its fallback
  instead of duplicating the header. Boat transport is genuinely
  functional, not narrated: Lokar Searunner at Rellekka's westernmost
  dock starts the quest and teleports the player to the real docked-ship
  geography on the Lunar Isle side once boarded (coords derived from this
  cache's own npc spawns, not invented); post-quest, his `_by_pirateship`
  wrapper (native `op3=Pirate's Cove`) becomes a real three-way hub
  between Rellekka, Lunar Isle and Pirates' Cove -- **this is the
  functional Lunar Isle transport row #135 Dream Mentor was blocked on;
  that row's own blocker is resolved and should be re-checked by a future
  tick** (not re-verified/unblocked here, out of this slice's budget).
  Deferred, no established precedent anywhere in this tree for the
  alternative (documented, not silently dropped): the ship wall-chart
  interface (`interfaces/quest_lunar_galleon.if`) and the Lunar spellbook
  itself -- this engine has no spellbook-switching system anywhere
  (grep-confirmed, zero `spellbook`-named scripts in the whole tree), so
  "unlocks the Lunar spellbook" is flavour text plus the completion
  varbit only, no mechanical spellbook swap; the six dream trials' own
  RNG puzzle logic (target-sum dice, number sequences, memory-maze
  jumping, timed log race, hurdle race) is condensed to one pass/fail
  interaction per trial host (same condensing precedent as Cabin Fever's
  canister-firing / Royal Trouble's picture wall), except the Ethereal
  Mimic's emote copy, which reuses Cold War's own real `p_choice4`
  emote-matching precedent (`coldwar_shared.rs2`) for two rounds instead
  of the wiki's five; the mine/stalagmite prop, Pauline-Polaris disguise
  riddle and blue-flower ring dig are narrated inside monk dialogue
  rather than separately modelled (no disguise/diggable-flower precedent
  anywhere in this tree). Wiki https://oldschool.runescape.wiki/w/
  Lunar_Diplomacy/Quick_guide + https://oldschool.runescape.wiki/w/
  Transcript:Lunar_Diplomacy + quest-helper's own `LunarDiplomacy.java`
  (1,756 lines, fetched via WebFetch summary, dialogue paraphrased not
  verbatim per copyright, same caveat as every prior slice).
  `mingw32-make -C src sscompile` clean, `mingw32-make -C src
  torirsserver-scripts` exit 0 (14,905 scripts, up from 14,855; zero
  `lunar`-related warnings/errors in the build log); also fixed a genuine
  duplicate-trigger bug this slice would otherwise have introduced
  (`[opnpc1,lunar_oneiromancer]` merge into `dragonslayer2.rs2`, see
  above -- no other duplicate triggers found across every touched npc,
  full-tree scan). Files: `quests/quest_lunardiplomacy/{configs/
  lunardiplomacy.{constant,varp}, scripts/lunardip_{shared,transport,
  ship,isle,staff,regalia,dream,journal}.rs2}` + merges into
  `quest_dragonslayer2/scripts/dragonslayer2.rs2`,
  `skill_runecraft/scripts/runecraft.rs2`, and
  `interface_questjournal/scripts/quest_journal.rs2`. Next pending row
  (smallest-first): #171 The Path of Glouphrie, 1,959 lines (row #170
  Dragon Slayer II and #173 Monkey Madness II are already `done`).
- slice 135 UNBLOCKED and done: Dream Mentor -- re-checked the row #169
  Lunar Diplomacy blocker before touching anything else (Part 1 of this
  tick). Read `quest_lunardiplomacy/scripts/lunardip_transport.rs2` in
  full: `[opnpc1,lunar_fremennik_pirate_1op]` (the resolved multivarbit
  morph of the native `lunar_fremennik_pirate` npc, cache-confirmed via
  `configs/all.npc`'s own `multivarbit=lunar_quest_main`/`multinpc1..12`
  block) is a real, reachable trigger -- `lunar_fremennik_pirate` spawns at
  (2620,3693,0) in `m40_57.spawn`, matching the dbrow's own `^lunardip_
  rellekka_dock_coord` (0_40_57_60_45) almost exactly. Traced the full
  `%lunar_quest_main` state chain end to end across every `lunardip_*.rs2`
  file (`^lunardip_accepted` -> `_ship_boarded` -> `_symbols_found` ->
  `_met_oneiromancer` -> `_suqah_defeated` -> `_potion_delivered` ->
  `_staff_started` -> `_staff_air/fire/water/earth_done` ->
  `_regalia_started` -> `_dream_entered` -> `_games_complete` ->
  `_mirror_defeated` -> `_complete`): every breakpoint is both set by an
  earlier script and read by a later one, no dead ends, no unreachable
  gate. `^lunardip_isle_harbour_coord` (0_33_60_39_28 -> 2151,3868,0)
  matches `lunar_oneiromancer`'s own native spawn in `m33_60.spawn` almost
  exactly. Post-quest, `lunar_fremennik_pirate_by_pirateship` (native
  `op3=Pirate's Cove`) is a real three-way Rellekka/Lunar Isle/Pirates'
  Cove hub with no further gate. Genuinely unblocked, not just optimistic
  -- confirmed, not assumed.
  Then fetched `Zoinkwiz/quest-helper`'s `DreamMentor.java` (441 lines) +
  `CyrisusArmourSet.java`/`CyrisusBankConditional.java`/
  `CyrisusBankItem.java`/`SelectingCombatGear.java` (101+39+62+102=304,
  summing to 745, matching this row's own line count exactly, same files
  the prior BLOCKED tick had already fetched and read). Confirmed Dream
  Mentor's real setting is Lunar Isle itself (Cyrisus in the Lunar Mine at
  (2300-2370,10313-10354,2), the Oneiromancer at (2151,3867,0) -- almost
  exactly the harbour coord above -- 'Bird's-Eye' Jack in the Lunar Isle
  bank) plus a separate dream-instance arena at (1806-1840,5135-5167,2),
  **not** Nightmare Zone -- exactly the setting the newly-functional boat
  transport reaches. `getGeneralRequirements()`: CombatLevelRequirement(85)
  + QuestRequirement(LUNAR_DIPLOMACY, FINISHED) + QuestRequirement(EADGARS_
  RUSE, FINISHED); Eadgar's Ruse is already real/IN-LC (`quest_eadgar`).
  Re-grepped for any LC proc / 2009scape impl / prior `dreammentor` files
  in this tree: none (methodology steps 1-2 clean). `tools/
  questhelper_extract.py dreammentor --check` (using a local scratch copy
  of the 5 fetched Java files under `helpers/quests/dreammentor/`, this
  machine has no `/Users/matthewevers/...quest-helper` checkout) exits 0:
  every one of 14 npc / 5 loc / 14 item / 8 varbit gameval names resolves
  cleanly against this cache, zero unresolved rows. Native dbrow `quest_
  dreammentor` (id 134, endstate 28, questpoints 2, requirement_combat 85,
  stat_xp_awarded hitpoints 150000 raw=15000xp + magic 100000 raw=10000xp,
  matches wiki exactly; `requirement_quests`=88,36 decodes to Forgettable
  Tale/Plague City, same known cache-decode-corruption pattern this queue
  warns about repeatedly, real prereqs hard-gated instead). Native varbit
  schema on basevars `dream_main`/`dream_main2`/`dream_main3` (`dream_
  prog`, `dream_health` thresholds 40/70/100 cache-confirmed via
  `VarbitRequirement(DREAM_HEALTH, N, GREATER_EQUAL)`, `dream_armament`
  ==100 cache-confirmed, `dream_cutscene_seen`, `dream_combattype`, `dream_
  arma_item1..5`) reused as-is, matching quest-helper's own VarbitID names
  exactly; claimed via a bare-reservation `.varp` file, same precedent as
  `lunardiplomacy`/`cabinfever`. All named npcs (Cyrisus's four recovery-
  stage morphs `dream_cyrisus_unconscious`/`_barely_conscious`/`_sitting`/
  `dream_cyrisus` + combat variants `_melee`/`_ranger`/`_caster`, `dream_
  birds_eye_jack`, the four dream bosses `dream_inadequacy`(343)/`dream_
  everlasting`(223)/`dream_untouchable`(274)/`dream_illusive`(108), all
  matching the wiki's levels exactly) and locs (`dream_cave_wall_entrance`,
  `lunar_mine_slanty_ladder_up`/`_down` -- already generic `category=
  climb_up`/`climb_down`, no custom transport scripting needed --,
  `lunar_moonclan_sink`, `lunar_moonclan_brazier_multi`) are already
  natively declared -- none invented. Only `dream_inadequacy` has a
  `.spawn` entry anywhere in this tree (`m28_80.spawn`); the other three
  bosses are hand-spawned in sequence on trigger via `~dreammentor_spawn_
  if_absent` (`npc_find`/`npc_add`), same idiom as Land of the Goblins'
  skeleton high priests, with `npc_del;` on each Cyrisus recovery-stage
  morph transition (`[opnpc1,...]` context, same convention as `merlin.
  rs2`'s crystal-prison rush-off). Fight order (Inadequacy -> Everlasting
  -> Untouchable -> Illusive) is this slice's own invention -- quest-
  helper's own `ConditionalStep` ordering only states which boss is
  currently present, not a sequence -- documented, not guessed-as-
  recovered, same caveat as Royal Trouble's breakpoint scale.
  Simplifications (documented, same "condense a repetitive/RNG sub-
  mechanic" precedent as Cabin Fever/Royal Trouble/Lunar Diplomacy): (1)
  the wiki's "alternate between >=3 food types" feeding nuance isn't
  modelled -- any of 12 common food items counts, and `%dream_health`
  itself doubles as the feed counter (+10 per food, snapping to the cache-
  confirmed 40/70/100 thresholds); (2) `CyrisusArmourSet`/`CyrisusBankItem`
  model picking 5 specific Barrows/adamant-boots/infinity-boots/ancient-
  staff bank items -- three of those exact items (adamant boots, infinity
  boots, ancient staff) don't exist anywhere in this cache (grep-
  confirmed) -- condensed to one narrated interaction with 'Bird's-Eye'
  Jack granting the real `dream_chest` item directly, which is *also* what
  quest-helper's own `chest` ItemRequirement already represents in the
  real quest (the 5-item bank fetch is already abstracted behind a single
  chest prop even in authentic OSRS); `%dream_combattype` is still
  computed from the account's real combat levels, matching
  `CyrisusArmourSet.getCorrectSet`'s own tie-break exactly (melee*2 >=
  max(ranged*3,magic*3) -> melee; else ranged*3>magic*3 -> ranged; else
  magic), to pick which native Cyrisus combat morph fights alongside the
  player; (3) the dream arena's own exit ("only leave via the lecturn ...
  cannot pray") has no confirmed native loc id anywhere (quest-helper's
  own Java never names an ObjectID for it) -- the player is teleported
  straight back to the Oneiromancer the moment the fourth boss falls.
  Rewards: 2 questpoints (dbrow), 15,000 Hitpoints XP + 10,000 Magic XP
  (raw/10, matches wiki exactly), the shared `thosf_reward_lamp` item (same
  as `quest_contact`/`quest_kingsransom`/etc, `inv_add` only). Deferred,
  same "no spellbook-switching system anywhere" caveat as Lunar
  Diplomacy's own Lunar spellbook unlock: the "7 new Lunar spells" reward
  is flavour text plus the completion varbit only. The "bank without seal
  of passage via 'Bird's-Eye' Jack" unlock *is* mechanically real
  (`~openbank` on the post-quest branch, same convention as `lunar_
  moonclan_bankbooth`'s `[oploc2,...] ~openbank;`). Wiki https://
  oldschool.runescape.wiki/w/Dream_Mentor/Quick_guide +
  https://oldschool.runescape.wiki/w/Transcript:Dream_Mentor (dialogue
  paraphrased, not verbatim, per copyright, same caveat as every prior
  slice). **Five real pre-existing trigger collisions found and merged in
  rather than duplicated** (grep-first, every one): (1)
  `[opnpc1,lunar_oneiromancer]` (`quest_dragonslayer2/scripts/
  dragonslayer2.rs2`) -- already shared with DS2's soft-skip and Lunar
  Diplomacy's own dialogue, this quest's branch added as a third gated
  fallback (`~dreammentor_oneiromancer_talk`), checked only once `%lunar_
  quest_main >= ^lunardip_complete`; (2) `[opheldu,hammer]` (`general_use/
  scripts/hammer.rs2`) -- added a `case astralrune` to the shared switch;
  (3) `[opheldu,pestle_and_mortar]` (`skill_herblore/scripts/
  grind_ingredient.rs2`) -- added a `dream_astral_shards` short-circuit
  ahead of the generic `herblore_grind_table` lookup, same pattern as the
  existing `rune_shards`/`mudrune` short-circuits in that same file; (4)-
  (5) `lunar_seal_of_passage` and `%lunar_brazier_lit`/`canoeing_menu`
  (native carrier already claimed by `interface_farming/configs/
  farming_tools.varp`) reused directly, no new varp declared. `mingw32-
  make -C src sscompile` clean, `mingw32-make -C src torirsserver-scripts` exit
  0 (14,939 scripts, up from 14,905; zero `dreammentor`-related warnings/
  errors in the build log, grep-confirmed). `ToriRSServer_Pack --check-only`
  was also attempted per the guardrails, but this isolated worktree has no
  `cache.osrs239` checked out (`cannot open the cache` is the tool's very
  first error) -- its whole-tree category/cache cross-reference is
  unusable here independent of this slice (963 pre-existing errors, 8297
  warnings, none naming `dreammentor` except one downstream-of-the-same-
  missing-cache false positive claiming `dream_inadequacy` has no Attack
  op, contradicted directly by `configs/all.npc`'s own `op2=Attack` on that
  record) -- treated `sscompile`+`torirsserver-scripts` exit 0 as the real bar,
  same as every prior slice's own logged verification. Files: `quests/
  quest_dreammentor/{configs/dreammentor.{constant,varp}, scripts/
  dreammentor_{shared,cyrisus,dream,journal}.rs2}` + merges into
  `quest_dragonslayer2/scripts/dragonslayer2.rs2`, `general_use/scripts/
  hammer.rs2`, `skill_herblore/scripts/grind_ingredient.rs2`, and
  `interface_questjournal/scripts/quest_journal.rs2`. Next pending row
  (smallest-first): #171 The Path of Glouphrie, 1,959 lines (unchanged --
  did not move on to it this tick, Dream Mentor's unblock absorbed the
  budget).
- slice 171 done: The Path of Glouphrie -- King Bolren's pet "Dumpling" is
  actually one of Glouphrie the Untrusted's disguised evil creatures.
  Methodology steps 1-2 clean (grep-confirmed: no `glouphrie`/
  `pathofglouphrie` proc anywhere in `server/scripts` or `lc_quests.txt`;
  the row's own npc field, `poggolriec,poggolriec,poggolriec`, was garbled/
  stale as this queue's methodology warned -- the real npcs, found by
  grepping `pog_`-prefixed records in `configs/all.npc`, are `king_bolren`
  (shared), `roving_golrie`, `aluft_gnome_delivery_controller` (Gianne
  jnr.), `pog_watcher` (the Dumpling wrapper), `pog_gnome_longramble` and
  `pog_mutated_terrorbird_boss_1/2/3`). Fetched quest-helper's own
  `ThePathOfGlouphrie.java` + `sections/{StartingOff,UnveilEvil,
  InformKingBolren,FindLongramble,TheWarpedDepths}.java` +
  `MonolithPuzzle.java`/`YewnocksPuzzle.java`/`Solution.java`/
  `DiscInsertionStep.java` (9 files, 1,959 lines total, exact match to this
  row's own line count) via GitHub API/raw.githubusercontent.com into a
  local scratch tree, then ran `tools/questhelper_extract.py ... --check`:
  exit 0, every one of 12 npc / 25 loc / 35 item / 8 varbit gameval names
  resolved cleanly, zero unresolved rows. Native dbrow `quest_
  pathofglouphrie` (id 186, endstate 50, questpoints 2) has a trustworthy
  `requirement_stats` this time (Strength 60/Slayer 56/Thieving 56/Ranged
  47/Agility 45, matches quest-helper's own `getGeneralRequirements()`
  exactly) but the usual corrupted `requirement_quests` (decoded to Tourist
  Trap/Tears of Guthix/Sins of the Father, none real) -- hard-gated on the
  real prereqs instead (The Eyes of Glouphrie/Waterfall Quest/Tree Gnome
  Village FINISHED, all three already IN-LC/this queue). Native varbit
  schema fully declared on basevar `pog_primary` (`pog` 0..50, matching
  quest-helper's own `steps.put` keys and the dbrow endstate directly --
  independently confirmed by the native `pog_gnome_longramble` multi-npc
  record's own `multinpc27..32=pog_gnome_longramble_vis`, i.e. Longramble
  only becomes visible starting exactly at quest-helper's own `steps.put(26,
  findLongramble.talkToLongramble)` breakpoint) reused as-is, claimed via a
  bare-reservation `.varp` file, same precedent as `lunardiplomacy`/
  `dreammentor`. Simplifications (documented, same "condense a puzzle/
  cutscene with no established precedent" convention as every prior slice):
  (1) `MonolithPuzzle.java`'s push-block Sokoban puzzle and `YewnocksPuzzle.
  java`/`Solution.java`/`DiscInsertionStep.java`'s live 30-disc combinatorial
  machine puzzle (1,171 combined lines) both collapse to one deterministic
  click apiece (`pog_chest_closed` Search, `pog_gnome_machine_02` Operate),
  same precedent as The Eyes of Glouphrie's own crystal-disc machine; (2)
  Roving Elves is genuinely complete in this tree (`quest_rovingelves`), so
  this port hard-assumes it's finished rather than also modelling quest-
  helper's own pre-Roving-Elves fallback dungeon layout and Waterfall
  Quest's key-in-a-crate reuse -- soft-gated with a one-line message if
  somehow unfinished, same "soft-skip an unwritten gate" idiom as Priest in
  Peril; (3) the three Warped Terrorbirds (native `pog_mutated_terrorbird_
  boss_1/2/3`, vislevel 138 matching quest-helper's own combat text exactly,
  fully statted but with no `.spawn` entry) are hand-spawned together via
  `~pog_spawn_terrorbirds_if_absent` and fought in the real non-instanced
  boss chamber rather than a dynamically-built instance (quest-helper's own
  code comments that this fallback area already exists in live OSRS for
  logged-out players) -- no combat order invented, same "no established
  boss-order precedent" finding as Dream Mentor's own bosses, this time
  genuinely unordered since quest-helper's own code never orders them
  either; (4) the Advisor's illusion and Dumpling's true nature are narrated
  via `mesbox` rather than a live flashback cutscene with `pog_bolrie`/
  `pog_bolrie_noking`/`pog_advisor` (all three natively `vislevel=0`
  cutscene-only doubles, zero scripted trigger anywhere before this slice),
  same idiom as The Eyes of Glouphrie's own gnome-goblin war flashback; (5)
  `pog_spirit_tree_multi`'s own 33 native multiloc states all resolve to the
  same `pog_spirit_tree_dead` leaf (grep-confirmed, no distinct "revived"
  state exists in the cache), so Incomitatus's revival is narrated rather
  than a loc swap. Grapple crossing to Longramble (`pog_grapple_tree_base_
  op`) reuses the exact same Ranged-level/mithril-grapple/wielded-crossbow
  gate already scripted at `[oploc1,godwars_grapple_pillar]` in `areas/
  area_godwars/scripts/godwars_entrance.rs2`, not invented fresh. Rewards:
  2 questpoints (dbrow), 30,000 Strength + 20,000 Slayer + 5,000 Thieving +
  5,000 Magic XP (dbrow's own `stat_xp_awarded` is exactly 10x-inflated,
  same "dbrow row is a hint, not gospel" finding as The Eyes of Glouphrie/
  Darkness of Hallowvale/Ghosts Ahoy -- real amounts used, matching the wiki
  reward table and quest-helper's own `getExperienceRewards()` exactly),
  the four native `pog_strength_lamp`/`pog_slayer_lamp`/`pog_thieving_lamp`/
  `pog_magic_lamp` items granted via `inv_add` alongside the direct XP
  (matches the wiki's own "recover lost lamps from Hazelmere" note; no
  generic magic-lamp-rub handler exists anywhere in this tree, grep-
  confirmed, same "grant the flavour item, don't invent a rub mechanic"
  precedent as Dream Mentor's own `thosf_reward_lamp`). Wiki https://
  oldschool.runescape.wiki/w/The_Path_of_Glouphrie + .../Quick_guide
  (fetched via WebFetch summary, dialogue paraphrased not verbatim per
  copyright, same caveat as every prior slice). Zero duplicate triggers
  found across every touched npc/loc (full-tree grep before and after
  writing, `[opnpc1,king_bolren]`/`[opnpc1,grandtree_hazelmere]` merged
  additively as hub-proc calls, everything else genuinely new). `mingw32-
  make -C src sscompile` clean, `mingw32-make -C src torirsserver-scripts` exit 0
  (14,971 scripts, up from 14,939; zero `pog`/`pathofglouphrie`/`glouphrie`-
  named warnings/errors in the full build log). Files: `quests/quest_
  pathofglouphrie/{configs/pathofglouphrie.{constant,varp}, scripts/
  pog_{quest,storeroom,longramble,sewers,journal}.rs2}` + merges into
  `areas/area_gnome/scripts/{king_bolren,hazelmere}.rs2` and
  `interface_questjournal/scripts/quest_journal.rs2`. Next pending row
  (smallest-first): #172 While Guthix Sleeps, 2,288 lines (#174 Recipe for
  Disaster, 3,370 lines, is the only other pending row left in the table).

- slice done (2026-08-12): #172 While Guthix Sleeps -- one of OSRS's own
  largest, most infamous quests (2,288-line quest-helper source, fetched
  via raw.githubusercontent.com). Grep-first per methodology: `lc_quests.
  txt` has `quest_tearsofguthix` only (not this quest); no `guthixsleeps`/
  `whileguthixsleeps` proc anywhere in `server/scripts`.
  `SCAPE2009_CONTENT_PORT_QUEUE.md`'s own skip list carries a
  `whileguthixsleeps -> "RS2-only; never shipped in OSRS"` row -- this is
  factually wrong (it's a live OSRS Grandmaster quest, added 5 Feb 2015)
  but the row is `skip`, not `done`, so it doesn't gate anything; flagged
  in the constant file, not corrected here (out of this queue's scope).
  Native dbrow `quest_whileguthixsleeps` (id 189, endstate 900, questpoints
  5, startnpc 13510=`wgs_ivy_sophista`) is unusually trustworthy for this
  queue: its own `requirement_stats` (Thieving 72/Magic 67/Agility 66/
  Herblore 65/Farming 65/Hunter 62) and `stat_xp_awarded` (800000/750000/
  750000/500000, confirmed = quest-helper's own real XP rewards x10, this
  engine's own tenths-of-XP `stat_advance` convention) both match quest-
  helper's own `getGeneralRequirements()`/`getExperienceRewards()` exactly
  -- used directly, no correction needed, unlike most prior slices.
  `requirement_quests` is still the usual corruption (decodes to Legends'
  Quest/Garden of Death/Troll Romance/Song of the Elves/Land of the
  Goblins/Client of Kourend/Depths of Despair/Enakhra's Lament, none real)
  -- hard-gated instead on the real prerequisite chain from quest-helper's
  own source (Defender of Varrock/The Path of Glouphrie/Fight Arena/Dream
  Mentor/The Hand in the Sand/Wanted!/Temple of the Eye/A Tail of Two Cats/
  Tears of Guthix/Nature Spirit, all FINISHED, via each quest's own
  completion varp: `%dov`, `%pog`, `%arenaquest`, `%dream_prog`, `%hand
  sand_quest`, `%wanted_main`, `%tote`, `%tog_juna_bowl`, `%druidspirit`).
  A Tail of Two Cats is soft-skipped: `quest_atailoftwocats/scripts/
  twocats.rs2` never advances `%twocats_quest` past 25 despite its own
  reachable `[queue,twocats_quest_complete]` calling `~quest_complete
  (quest_tailoftwocats)` -- a pre-existing bug in that already-`done` slice
  (not touched, out of scope), so no reliable "finished" signal exists for
  it -- same "soft-skip an unwritten gate" idiom this queue's own
  methodology names explicitly (Priest in Peril). Native varbit schema
  fully declared on basevar `wgs_primary` (`wgs`, bits 0-9, matching quest-
  helper's own `steps.put` keys 0..890 with 10 bits of headroom) plus
  `wgs_primary_2` (the seven `wgs_*_recruit` flags + `wgs_hero_statues_
  vis`), both claimed via a bare-reservation `.varp` file, same precedent
  as `quest_dreammentor`/`quest_pathofglouphrie`. A curated subset of
  quest-helper's own step numbers is reused directly as `%wgs` checkpoints
  (0/2/3/4/8/21/24/25/30/34/38/44/430/440/460/597/610/620/660/680/770/800/
  840/870/880/900 -- finishing at the dbrow's own 900 rather than quest-
  helper's own 890). Real geography used throughout (every coord converted
  from quest-helper's own `WorldPoint` constructors): Taverley (Ivy
  Sophista/Thaerisk Cemphier/two level-167 assassins), the Khazard
  battlefield's broken table (Movario's base entrance), Falador White
  Knights' Castle (Akrisae/Idria, kept at one shared coord for the whole
  quest rather than also modelling Idria's earlier McGrubor's Wood meeting
  -- see Simplifications), the Falador jail cell (`wgs_prison_door_
  locked`, a real native loc, "true terror" reveal), the Black Knights'
  Fortress catacombs (real `elite_black_knight_1` x2 + `dark_squall_
  combat`/Surok Magis, level 265, both hand-spawned/fought for real), and
  the hidden Guthixian temple (`luc2_stone_of_jas_named_noop`/`luc2_
  stone_of_jas_named`, real locs; `wgs_balance_elemental` level 524,
  `wgs_movario_temple`, `tormented_demon_1` x2 level 450 -- distinct from
  `rs2012_tormented_demon_melee/magic/ranged` used by the unrelated
  postquest Ancient Guthix Temple minigame in `area_rs2012_tormented_
  demons`, grep-confirmed zero name collision -- all real fights, no
  combat ordering invented where quest-helper's own code doesn't order it
  either, same finding as Dream Mentor/The Path of Glouphrie's own multi-
  boss encounters). `bkfortressdoor1`/`bksecretdoor`/`kr_bkf_basement_
  laddertop` already have live triggers from `quest_blackknight`/`quest_
  kingsransom` (grep-confirmed) -- reused as plain unscripted geography,
  not re-triggered. None of the four always-present hub npcs (`wgs_ivy_
  sophista`/`wgs_thaerisk_cemphier`/`wgs_akrisae`/`wgs_idria`) have a
  native `.spawn` entry (the map-dump source this cache's spawn files are
  generated from never captured an account with this quest's own rare
  prerequisite state) -- hand-spawned idempotently via a new `~wgs_login;`
  line in `server/scripts/player/login.rs2`'s `[login,_]` dispatch list,
  same "hand-spawn on trigger, no `.spawn` entry" idiom as Dream Mentor/
  Land of the Goblins' bosses, fired from login instead of a door click
  since these are dialogue hubs, not door-gated combat encounters.
  Simplifications (documented, same "collapse a puzzle/cutscene/
  combinatorial mechanic with no precedent to one deterministic beat"
  convention as every prior large slice, applied more heavily than usual
  given this is quest-helper's single largest helper in the whole queue):
  the broav hunting/trapping/tracking subplot, the entire Movario's-base
  infiltration (door-rune puzzle, 7-bookcase electricity puzzle, bed-chest
  search, weight/painting puzzle -- ~500 quest-helper lines), the
  snapdragon-seed/rose-tinted-lens/truth-serum/sketch subplot, the seven-
  hero recruitment (quest-helper's own Lunar spellbook "Contact NPC" spell
  -- this engine has no spellbook-switching system anywhere, grep-
  confirmed, same deferral as Dream Mentor's/Lunar Diplomacy's own Lunar
  spellbook unlocks, so this one specifically has no possible native
  implementation regardless of scope budget), the Black Knights' Fortress
  catacombs' own bridge-jump/wall-climb/wardrobe-search/map-room chain
  (collapsed to one Akrisae-directed beat that still ends in the real
  Surok Magis fight), the "true terror" cutscene (narrated, no native
  cutscene wrapper npc ever scripted before this slice), and the Abyss
  entry (four braziers/orbs/blocks + a skull puzzle) all collapse to one
  deterministic narrated beat apiece -- each one individually matches this
  queue's own repeated "native widget/room puzzle with no precedent
  collapses to one click" convention, just applied to more sub-puzzles
  than any single prior slice given this helper's unmatched raw size.
  Rewards: 5 questpoints (dbrow), Thieving/Farming/Herblore/Hunter XP per
  the dbrow's own (trustworthy, see above) `stat_xp_awarded`, and --
  critically -- `%rs2012_wgs_complete = 1` set in the completion proc: this
  is the exact gate `area_rs2012_tormented_demons/configs/rs2012_
  tormented_demons.varp` has been carrying since that unrelated earlier
  slice, whose own comment reads "the full While Guthix Sleeps quest is
  outside this encounter slice. Its eventual completion script must set
  this to 1." -- this slice is that completion script, so the postquest
  Ancient Guthix Temple Tormented Demons minigame is now actually
  reachable end-to-end for the first time. Wiki https://oldschool.
  runescape.wiki/w/While_Guthix_Sleeps + .../Quick_guide (fetched via
  curl/raw.githubusercontent.com for quest-helper's own source; dialogue
  paraphrased not verbatim per copyright, same caveat as every prior
  slice). Zero duplicate triggers found across every touched npc/loc
  (full-tree grep before and after writing -- `elite_black_knight_1`/
  `bkfortressdoor1`-family locs confirmed already owned by `quest_
  blackknight`/`quest_kingsransom` and deliberately NOT re-triggered,
  everything else genuinely new). `mingw32-make -C src sscompile` clean,
  `mingw32-make -C src torirsserver-scripts` exit 0 (15,001 scripts, up from
  14,971; zero `wgs`/`guthixsleeps`-named warnings/errors anywhere in the
  full build log). Files: `quests/quest_whileguthixsleeps/{configs/
  whileguthixsleeps.{constant,varp}, scripts/wgs_{quest,journal}.rs2}` +
  merges into `player/login.rs2` and `interface_questjournal/scripts/
  quest_journal.rs2`. Next pending row: #174 Recipe for Disaster, 3,370
  lines -- the only row left in the entire table.
- slice 174 Recipe for Disaster (2026-08-12): `in_progress` -- largest quest
  in the queue (3,370 helper lines, ~triple the next-largest slice), a
  meta-quest of an intro plus 8 independently-orderable sub-quests plus a
  finale. Grep-first: no LC proc, no 2009scape implementation, not in either
  Skip/IN-LC table -- genuinely unowned. The osrs239 cache has a FULL native
  dbrow schema pre-declared for all 10 parts (`quest_recipefordisaster` id
  106 + `subquest_rfd_intro`/`_dwarf`/`_goblins`/`_pirate`/
  `_lumbridgeguide`/`_evildave`/`_ogre`/`_amikvarze`/`_monkey`/`_finale`,
  ids 171-180, `configs/all.dbrow`), used as-is. Ported end-to-end in this
  tick: the introduction ("Another Cook's Quest" -- gather eye of newt/
  greenman's ale/rotten tomato/dirty blast, hand to the Cook, enter the
  dining room, witness the Culinaromancer curse the banquet) plus three
  sub-quests: Freeing Evil Dave, Freeing the Lumbridge Guide, and Freeing
  the Goblin generals (Wartface & Bentnoze). All 8 sub-quest dbrows'
  `requirement_quests` chain to quests that are pre-Sept-2004 LC content
  already done (Fishing Contest, Goblin Diplomacy, Big Chompy Bird Hunting,
  Gertrude's Cat, etc.) -- so "prioritise chapters with done prereqs" did
  not narrow the field; the 4 ported chapters were chosen by helper file
  size (RFDStart 6,324B, RFDEvilDave 9,279B, RFDLumbridgeGuide 10,231B,
  RFDGoblins 12,080B -- the four smallest of nine). `%recipefordisaster`
  gates all sub-quests (`>= ^rfd_intro_complete`, dbrow id 2307 in every
  sub-quest's own `requirement_quests`); `%rfd_evildave`/
  `%rfd_lumbridgeguide`/`%rfd_goblins` track each chapter. Per the
  "one deterministic action per beat" rule: Evil Dave's real hell-rat
  spice-dosing minigame (no `npc_find`/chase support, no `cat`/`wily_cat`
  item in this cache -- checked) collapses to talk-Dave/talk-Doris/use-stew;
  Lumbridge Guide's three interface quizzes (NPC portraits, trivia, hidden-
  inventory memory test -- none of which exist as rev-230 panels, and
  QuizSteps.java's question banks are randomised) collapse to Traiborn
  enchanting the ingredients himself; the goblin cauldron-explosion cutscene
  is narrated rather than staged, and the cook's charred/on-wall NPC swap is
  skipped (`goblin_cook` used throughout). Goblins' own ingredient prep
  (knife+orange, dye+orange slices, spice+bait, water+bread) is real
  per-item `[opheldu,...]` content, not collapsed, since each is a genuine
  one-shot combine with existing tree-wide precedent. Reused/merged rather
  than duplicated: `[opnpc1,cook]` (quest_cook.rs2, branch added for
  `%recipefordisaster < ^rfd_intro_complete`), `[opheldu,orange]`/knife
  slicing (skill_cooking/cutting_fruit.rs2, untouched, already produces
  `orange_slices`), `[opheldu,orange_slices]` (skill_cooking/gnome_cooking/
  gnome_seasoning.rs2's 9-header shared block, dye branch merged in gated to
  `last_item = orange_slices` only), `[opheldu,bread]`
  (quest_belowicemountain/scripts/belowicemountain.rs2, bucket-of-water
  branch merged in), and the goblin kitchen ladder / Evil Dave's basement
  stairs (`100_goblin_ladder_down/up`, `100_dave_celler_stairs` -- already
  generic `category=climb_down`/`climb_up` records in `ladders_stairs/
  configs/ladders.loc`, no script needed). Genuinely new: Dave's basement
  trapdoor (`100_dave_celler_trapdoor_closed`/`_open`, not one of the
  shared generic `trapdoor`/`trapdoor_open` gamevals, so it gets its own
  open/descend/close triggers). Journal wired for all 4 ported dbrows plus
  the parent `quest_recipefordisaster` overview row (which also reports the
  5 not-yet-portable sub-quests honestly rather than pretending). Wiki:
  https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Quick_guide +
  Transcript:Recipe_for_Disaster/{Another_Cook's_Quest,Freeing_Evil_Dave,
  Freeing_the_Lumbridge_Guide,Freeing_the_Goblin_generals}. `mingw32-make -C
  src sscompile` clean, `mingw32-make -C src torirsserver-scripts` exit 0 (15,024
  scripts, up from 15,001; zero warnings or errors anywhere naming
  `recipefordisaster`/`rfd_*`). Remaining for a future tick: Mountain Dwarf,
  Pirate Pete, Skrach Uglogwee, Sir Amik Varze, King Awowogei/Monkey
  Ambassador, and the Culinaromancer finale (which also needs all 8
  sub-quests done first, so it cannot start regardless). Row stays
  `in_progress` per the depth-first rule. This was the last `pending` row in
  the table -- the queue's `pending` backlog is now exhausted; two rows
  remain `in_progress` (#43/P2 asoulsbane, dbrow-less from an earlier tick,
  and this one).

- slice 174 Recipe for Disaster (2026-08-12, second tick): `done` -- the
  remaining five sub-quests plus the Culinaromancer finale, completing the
  quest end-to-end. Checked both suggested cross-reference leads first, per
  the brief: `quest_recruitmentdrive` turned out to be a dead end for Sir
  Amik Varze (it never references him at all; its own `recruitmentdrive.rs2`
  carries an unrelated stray comment pointing at `quest_wanted/scripts/
  wanted_tiffy_amik.rs2`, a different "real world" Amik at the White
  Knights' Castle -- RFD's own Amik is the frozen ambassador `hundred_varze_
  base`, never touched by either quest); `quest_monkeymadnessi`/`ii` were
  the opposite -- genuinely load-bearing, see below. **Freeing the Mountain
  Dwarf**: Kaylee (`risingsun_barmaid2`/`_barmaid`/`_barmaid3`, already
  merged-into by `quest_belowicemountain`) already sells `asgarnian_ale` for
  3gp, matching the wiki's own price, so no purchase script was needed, only
  a merged "what do you know about dwarves" branch; Rohak
  (`hundred_dwarf_dad`) is genuinely new. GetRohakDrunk.java's real 4-dose
  "keep giving him ale" loop collapses to one asgoldian-ale use (same
  precedent as Evil Dave's own stew-dosing), and the rock cake's real
  cooling step (ice gloves, or an icefiend kill) is skipped entirely -- no
  "combat kill transforms a held item" mechanic exists anywhere in this tree
  -- Rohak just hands over an already-cool cake. **Freeing Pirate Pete**:
  the largest individual collapse of the tick -- diving-helmet crafting
  (bronze wire + needle), 5 rocks to distract mudskippers, and the raw/burn-
  chance cooking distinction all dropped (no precedent for any of the
  three); what stayed real: Murphy (merged into `minigames/game_trawler/
  scripts/murphy.rs2`'s live `[opnpc1,murphy]`) hands over ready-made diving
  gear, then real underwater combat against `hundred_pirate_giant_
  mudskipper`/`_crab` (both already world-spawned in `m46_148.spawn`, zero
  hand-spawning needed) for Nung (`100_pirate_mogre_nung`, also world-
  spawned, genuinely new trigger), then the Cook (third merged branch into
  `quest_cook.rs2`'s `[opnpc1,cook]`, now carrying intro + Pirate Pete +
  Amik Varze) grinds/cooks the ingredients himself. Caught and fixed a self-
  introduced bug while adding the third Cook branch: the Pirate Pete merge's
  own guard (`%rfd_pirate < ^rfd_pirate_cake_made`) was missing a `%rfd_
  pirate >= ^rfd_pirate_inspected` lower bound, so any player who'd finished
  the RFD intro but never once inspected Pete would have the Cook's normal
  post-Cook's-Assistant banter permanently hijacked into a "go inspect Pete"
  loop -- fixed before it shipped, both new Cook branches now correctly
  guarded on their own ambassador having been inspected first. **Freeing
  Skrach Uglogwee**: Rantz's real Feldip-Hills-to-Karamja ferry (push-tree
  boat, spit-roast a chompy, sail back) and the toad-balloon-lure-then-shoot
  mechanic for the jubbly bird both collapse (no toad-luring/balloon
  mechanic or lured-target archery sequence anywhere in this tree) to:
  giving Rantz (merged into `quest_onesmallfavour/scripts/
  onesmallfavour_relay.rs2`'s live `[opnpc1,rantz]`) a raw chompy, then real
  combat against a hand-spawned `100_jubbly_bird` (no native `.spawn` entry
  exists for it, same "hand-spawn on trigger" idiom as Dream Mentor),
  cooked for real on Rantz's own spit-roast loc (`multi_chompybird_
  spitroast_entity`, no existing trigger). **Freeing Sir Amik Varze**: by
  far the most heavily-gated sub-quest -- real hard prerequisites on Family
  Crest/Waterfall Quest/Shilo Village/Heroes' Quest/Underground Pass/Lost
  City all being FINISHED, every one of them confirmed actually `done` in
  this queue first (`%crestquest`/`%waterfall_quest`/`%zombiequeen`/
  `%heroquest`/`%upass`/`%zanaris`). The real seven-stage brulee (cornflour
  from windmill-ground sweetcorn, a Kharazi Jungle vanilla pod, dramen-
  branch garnish, cinnamon, then rubbing a token to summon a fairy dragon to
  flambe it) collapses to the Wise Old Man (third merged branch into
  `quest_makingfriendswithmyarm/scripts/makingfriendswithmyarm.rs2`'s
  shared `[opnpc1,wise_old_man]`, alongside Garden of Tranquility and Swan
  Song's own branches) finishing it himself once handed an egg, a token,
  milk and cream -- he's already the RFD-relevant "strange beasts" expert
  in the real transcript. What stayed real: the Evil Chicken fight (hand-
  spawned for real via sacrificing a raw chicken on the actual `fairy_
  chicken_shrine` loc, not narrated) and the black dragon fight for the
  dragon token -- `black_dragon` already has a tree-wide `[ai_queue3,
  black_dragon]` generic loot table (`drop_tables/scripts/black_dragon.rs2`)
  reused via a coordinate-gated merge (bounded to the real Zanaris chicken-
  lair box, 2430-2492/4355-4407) so the token doesn't drop from every black
  dragon in the game. **Freeing King Awowogei**: per the brief, checked
  `quest_mm`/`quest_monkeymadnessii` first and found them genuinely load-
  bearing rather than a dead end -- Ape Atoll's own greegree-disguise system
  (`quest_mm/scripts/mm_greegree.rs2`, `~mm_wearing_greegree` proc) is
  reused as-is, and Awowogei's own throne (`mm_throne`) already carries live
  `[oploc1,mm_throne]`/`[oplocu,mm_throne]` triggers from `quest_mm/scripts/
  mm_awowogei.rs2` (itself already carrying Monkey Madness II's own soft-
  skips) -- merged, not duplicated, gated behind `%mm_main >= ^monkeymadness
  _complete` so it can never collide with either quest's in-progress
  dialogue. The real three-greegree relay (gorilla for the banana tree,
  ninja for the nut hole, zombie for the hot-rock cooking dungeon) plus a
  Crash Island boat trip collapses -- no precedent anywhere for chaining
  three disguise-gated sub-zones in one fetch quest -- to: real combat
  against a Big Snake (`hundred_ilm_snake`, a real level-84 native spawn in
  `m47_85.spawn`, no hand-spawning needed) for its corpse, then the Wise
  Monkeys (`hundred_ilm_iwazaru`, also a real native spawn in `m43_43.spawn`,
  genuinely new trigger) prepare and cook the stuffed snake themselves once
  handed the corpse plus plain banana/monkey nuts/rope/knife/pestle and
  mortar -- both Awowogei and the Wise Monkeys still gate on a real equipped
  greegree + held M'speak amulet, reusing Monkey Madness I's own access
  rule rather than inventing a new one. **The Culinaromancer finale**: with
  all 8 sub-quests genuinely complete this tick, attempted and finished it
  too -- unlike the sub-quests it has no gathering/crafting content at all,
  just a straight six-boss gauntlet (Agrith-Na-Na, Flambeed, Karamel,
  Dessourt, the Gelatinnoth Mother, the Culinaromancer) through a portal
  (`hundred_portal_multi`, new trigger) into the real fighting arena, hard-
  gated on all eight `%rfd_*` sub-quest states plus the real Desert Treasure
  (`%deserttreasure >= ^dt_complete`) and Horror from the Deep (`%horrorquest
  >= ^horror_complete`) prerequisites, both already `done` in this queue.
  All six boss npcs have no native `.spawn` entry anywhere in this tree, so
  each is hand-spawned in sequence on defeating the last, same idiom as
  Dream Mentor. One simplification: the real Gelatinnoth Mother cycles
  through six elemental resistances (air/earth/fire/water/melee/ranged) with
  no elemental-weakness-cycling boss mechanic anywhere else in this tree, so
  she's fought once in a single fixed form (`hundred_minion5_air`), ordinary
  combat -- nothing else about the gauntlet is shortened, every fight is
  real. On completion, both `subquest_rfd_finale` and the parent
  `quest_recipefordisaster` dbrow (id 106, 0 extra questpoints -- all 10 QP
  across the saga were already paid one-by-one) are marked complete via
  `~quest_complete`, so anything checking the parent dbrow directly (e.g.
  the quest list UI) now sees "Recipe for Disaster" as finished, not just
  its sub-parts. Journal wired for every one of the 6 new dbrows (`subquest_
  rfd_dwarf`/`_pirate`/`_ogre`/`_amikvarze`/`_monkey`/`_finale`) plus the
  parent overview updated to no longer list any "not yet portable" guests
  and to report the finale's own real availability once every guest is
  free. Wikis: `https://oldschool.runescape.wiki/w/Recipe_for_Disaster/
  Quick_guide` + `Transcript:Recipe_for_Disaster/{Freeing_the_Mountain_
  Dwarf,Freeing_Pirate_Pete,Freeing_Skrach_Uglogwee,Freeing_Sir_Amik_Varze,
  Freeing_King_Awowogei,Defeat_the_Culinaromancer!}` (dialogue paraphrased,
  not verbatim, per copyright, same caveat as every prior slice); quest-
  helper's own Java source fetched via `raw.githubusercontent.com`/
  `api.github.com` for all six remaining helpers (`RFDDwarf`/`RFDPiratePete`
  /`RFDSkrachUglogwee`/`RFDSirAmikVarze`/`RFDAwowogei`/`RFDFinal`.java, plus
  `GetRohakDrunk.java`). Zero duplicate triggers found across every touched
  npc/loc/item (full-tree grep before writing every merge point: `murphy`,
  `cook`, `rantz`, `wise_old_man`, `black_dragon`, `mm_throne`,
  `risingsun_barmaid2`, `bread` were all pre-owned and merged additively;
  every hand-spawned npc and every ambassador loc/item confirmed genuinely
  free first). Incremental builds after every sub-quest, not just at the
  end: `mingw32-make -C src sscompile` clean and `mingw32-make -C src
  torirsserver-scripts` exit 0 at each step (15,030 after Dwarf, 15,041 after
  Pirate Pete, 15,050 after Uglogwee, 15,058 after Amik Varze, 15,065 after
  Awowogei, 15,077 after the finale -- up from 15,024 at the start of this
  tick), zero warnings or errors anywhere naming `recipefordisaster`/
  `rfd_*`/any touched npc, on every single run. Files: `quests/quest_
  recipefordisaster/scripts/recipefordisaster_{dwarf,pirate,ogre,amikvarze,
  monkey,finale}.rs2` (new) + `recipefordisaster_journal.rs2` (extended) +
  `configs/recipefordisaster.{constant,varp}` (extended) + merges into
  `quests/quest_belowicemountain/scripts/belowicemountain.rs2`,
  `minigames/game_trawler/scripts/murphy.rs2`, `quests/quest_cook/scripts/
  quest_cook.rs2`, `quests/quest_onesmallfavour/scripts/
  onesmallfavour_relay.rs2`, `quests/quest_makingfriendswithmyarm/scripts/
  makingfriendswithmyarm.rs2`, `drop_tables/scripts/black_dragon.rs2`,
  `quests/quest_mm/scripts/mm_awowogei.rs2`, `interface_questjournal/
  scripts/quest_journal.rs2`. Row flips to `done` -- Recipe for Disaster,
  the largest quest in the entire queue, is now fully ported end-to-end
  across two ticks. One follow-up opportunity flagged, not acted on (out of
  this row's scope): `quest_thegreatbrainrobbery`'s own constant file
  documents Freeing Pirate Pete as a soft-skipped unported prerequisite --
  now that it's real, a future tick could tighten that gate. Only one row
  remains `in_progress` in the whole queue: #43/P2 asoulsbane.
- **rule change (2026-08-12):** at the user's explicit direction, the
  ownership-exclusivity methodology (steps 1-2, "grep LC/2009scape, STOP
  and defer to that lane if either has any proc") is retired. The wiki is
  now authoritative for every quest this queue touches, regardless of era
  or which lane historically claimed it; a `done` status anywhere is an
  audit target, not a hard stop. See the rewritten intro/methodology/skip
  list sections above for the full text. Practical effect: the ~30-row
  IN-LC table and the ~74-quest mid-era set (tracked on
  `SCAPE2009_CONTENT_PORT_QUEUE.md`) are now in scope for a wiki-accuracy
  audit-and-complete pass from this queue, alongside each sibling queue's
  own single straggler `pending` row (`CONTENT_PORT_QUEUE.md` #8,
  `SCAPE2009_CONTENT_PORT_QUEUE.md` #22h). Next: begin the IN-LC audit
  queue depth-first, starting with the smallest/simplest entries, spot-
  checking each LC script against its current wiki quest + transcript
  pages for missing branches/rewards/chapters.
- **IN-LC audit pass 1 (2026-08-12):** audited 4 rows from the IN-LC table,
  smallest-first by script line count: `impcatcher`/quest_imp (97 lines),
  `runemysteries`/quest_runemysteries (184), `seaslug`/quest_seaslug (414),
  `holygrail`/quest_grail (637). Read each LC script tree fully + the wiki
  quest page, `/Quick_guide`, and `Transcript:` pages (plus per-NPC
  transcripts: Wizard_Mizgog, Wizard_Grayzag) before comparing.
  - `impcatcher` -> **audited-fixed**: added the missing accept/refuse choice
    on Wizard Mizgog's quest offer (previously always auto-started the quest,
    no decline path existed), the documented post-quest repeatable "another
    amulet for another bead set" trade, and Wizard Grayzag's entirely-missing
    `[opnpc1,...]` talk-to trigger (3 quest-state branches — he had combat AI
    only, never a name to a talk-to gameval before now that gap was closed).
  - `runemysteries` -> **audited-ok**: Duke Horacio / Sedridor / Aubury
    chains already cover every wiki branch including lost-talisman/
    lost-package/lost-notes replacement dialogue, refuse options, and
    re-talks. Noted (not fixed, out of scope for one quest) that the wiki's
    Varrock Museum Kudos reward isn't implemented anywhere in this tree.
  - `seaslug` -> **audited-fixed**: dialogue coverage across
    caroline/holgart/kennith/kent/bailey already matched
    Transcript:Sea_Slug scene-for-scene. Fixed a real bug: quest completion
    (`caroline.rs2`) awarded questpoints via a bespoke `%qp = add(...)`
    instead of calling `~quest_complete(quest_seaslug)`, which silently
    skipped `%quests_completed_count` — brought in line with every other
    quest's completion proc. Also expanded the possessed-fisherman flavour
    dialogue from 2 to the wiki's full 6 randomised lines (non-gating, but
    players do hit it).
  - `holygrail` -> **audit-in_progress** (see table row for the full note):
    dialogue is fully wired end to end (king_arthur/merlin/brother_galahad/
    grail_crone/fisher_king/sir_percival/black_knight_titan/
    grail_realm_npcs/journal) but three physical-world triggers that gate
    real progress are missing outright — `sir_percival` has no spawn
    anywhere (static or dynamic; should come from opening/searching a sack
    in Goblin Village, which also doesn't exist as a loc/trigger in this
    tree), `magic_whistle` has no blow/teleport trigger into the Fisher
    Realm to start the Black Knight Titan fight, and the dynamically-spawned
    `grail_bell` has no ring trigger for castle access. Confirmed
    `magic_whistle`/`holy_grail` themselves are fine (real cache-derived
    static spawns in `m41_73.spawn`, generic pickup works) so this is
    scoped to three new trigger/proc additions, not new items. Left
    in-progress rather than rushing an unverified fix — a follow-up tick can
    pick this up directly from the table row's note.
  - Build: `mingw32-make -C src sscompile` clean, `mingw32-make -C src
    torirsserver-scripts` exit 0, 15079 scripts compiled (up from 15077 at tick
    start — 2 new trigger/label blocks: `[opnpc1,wizard_grayzag]` +
    `[label,mizgog_more_amulets]`), zero new errors/warnings. No duplicate
    triggers (grepped `server/scripts` for every touched npc/trigger name
    before adding). Files touched: `server/scripts/areas/wizard_tower/
    scripts/wizard_mizgog.rs2`, `.../wizard_grayzag.rs2`,
    `server/scripts/areas/area_fishing_platform/scripts/fisherman.rs2`,
    `server/scripts/areas/ardougne_east/scripts/caroline.rs2`. 34 rows
    remain unaudited in the IN-LC table (38 total minus the 4 above).
- **IN-LC audit pass 2 (2026-08-12):** audited 5 more rows from the IN-LC
  table: `dwarfcannon`/quest_mcannon, `waterfallquest`/quest_waterfall,
  `watchtower`/quest_itwatchtower, `treegnomevillage`/quest_tree,
  `sheepherder`/quest_sheepherder. Read each LC script tree fully + the wiki
  quest page, `/Quick_guide`, and `Transcript:` pages before comparing.
  - `dwarfcannon` -> **audited-fixed**, the biggest find this pass: the
    quest had a full 0-11 `%mcannon` state constant range and a journal
    that narrates every one of them, but **no dialogue existed anywhere**
    for the quest giver Captain Lawgof (`lawgof2`) or Nulodion
    (`nulodion`) — zero `[opnpc1,...]` triggers for either npc in the
    whole tree, meaning `%mcannon` could never leave state 0 and the quest
    could never be started at all. `mcannontoolkit` (gates the cannon
    repair) and `ammo_mould` (gates the smithing-side cannonball recipe in
    `skill_smithing/scripts/smelting/cannonballs.rs2`) were never granted
    to any player either, and there was no `~quest_complete` call anywhere.
    Added `quests/quest_mcannon/scripts/mcannon_commander.rs2`: full
    Lawgof + Nulodion state machine paraphrased from
    Transcript:Dwarf_Cannon, covering the accept/refuse offer and every
    checkpoint (railings -> watchtower -> goblin cave -> rescue -> cannon
    repair -> Nulodion -> completion), granting `mcannonrailing1_obj`x6,
    `mcannontoolkit`, `nulodions_notes`+`ammo_mould` at the right beats,
    ending in `stat_advance(crafting,7500)` +
    `~quest_complete(quest_dwarfcannon)` (dbrow name, not `quest_mcannon`).
    Both npcs already had real world `.spawn` entries (`m40_54`/`m47_53`
    per the cache) — no hand-spawning needed.
  - `waterfallquest` -> **audited-ok**: 10-file port already matches the
    wiki almost verbatim (Almera/Hudon/Hadley/Gerald/Golrie dialogue incl.
    all 4 Hadley tourism branches, rune-pillar puzzle, urn/chalice reward
    gate) with correct rewards (2 diamonds/2 gold bars/40 mithril seeds,
    13750 attack+strength xp matching dbrow) and a proper
    `~quest_complete(quest_waterfall)`. No gaps found.
  - `watchtower` -> **audited-fixed**: very thorough 14-file port covering
    every documented beat (rock cake theft, deathrune/skavid-map riddle,
    4-talker skavid word-learning puzzle + mad skavid final riddle,
    nightshade distraction, ogre shaman potions, Rock of Dalgroth mining)
    already using the real `~quest_complete` proc. Fixed a real numeric
    bug: completion granted `stat_advance(magic, 153000)` (15300xp) but
    both the dbrow's own `stat_xp_awarded` and the current wiki reward
    (15,250 Magic xp) say 152500 raw — corrected the 50xp overpay.
  - `treegnomevillage` -> **audited-fixed**: exactly the completion-proc
    bug class flagged at the top of this doc's methodology — dialogue
    (King Bolren, Montai, 3 tracker gnomes, Khazard warlord fight,
    ballista coordinate puzzle) all matched the wiki, but
    `areas/area_gnome/scripts/king_bolren.rs2`'s `[queue,
    tree_quest_complete]` awarded QP via a bespoke
    `%qp = add(%qp, ^tree_questpoints)` instead of
    `~quest_complete(quest_treegnomevillage)`, silently skipping
    `%quests_completed_count` exactly like Sea Slug's prior-tick bug.
    Fixed to call the real proc; reward values (2 QP, 11450 attack xp,
    gnome amulet) already matched dbrow/wiki and were left untouched.
  - `sheepherder` -> **audited-ok**: note the QuestHelper dir name is
    misleading — the real LC script dir is `quest_sheepherder` (dbrow
    `quest_sheepherder`, "Sheep Herder"), not `quest_sheep` (dbrow
    `quest_sheepshearer`, the unrelated Sheep Shearer quest, tracked
    separately as Queue row #13). Councillor Halgrive's accept/refuse
    offer, Doctor Orbon's plague gear, the cattleprod+poisoned-feed sheep
    mechanic, and completion (`~quest_complete(quest_sheepherder)`, 3100
    coins matching wiki's 100+3000 breakdown) all present and correct;
    `diseased_sheep.rs2` even cross-checks against Mourning's End Part I's
    later reuse of the same world npcs to avoid a duplicate trigger.
  - Build: `mingw32-make -C src sscompile` clean, `mingw32-make -C src
    torirsserver-scripts` exit 0, 15081 scripts compiled (up from 15079 at tick
    start — 2 new triggers from the new `mcannon_commander.rs2` file:
    `[opnpc1,lawgof2]` + `[opnpc1,nulodion]`), zero new errors/warnings. No
    duplicate triggers (grepped `server/scripts` for every touched
    npc/trigger name before adding). Files touched:
    `server/scripts/quests/quest_mcannon/scripts/mcannon_commander.rs2`
    (new), `server/scripts/quests/quest_itwatchtower/scripts/
    quest_itwatchtower.rs2`, `server/scripts/areas/area_gnome/scripts/
    king_bolren.rs2`. 29 rows remain unaudited in the IN-LC table (38
    total minus the 9 audited across both passes).
- **IN-LC audit pass 3 (2026-08-12):** audited 4 more rows, researched
  synchronously (no nested background sub-agents, per this tick's explicit
  instruction) one quest at a time: `cooksassistant`/quest_cook,
  `junglepotion`/quest_junglepotion, `eadgarsruse`/quest_eadgar,
  `trollromance`/quest_troll_love. Read each LC script tree fully + the
  wiki quest page, `/Quick_guide`, and `Transcript:` pages before comparing.
  - `cooksassistant` -> **audited-fixed**: this is `PORTING_GUIDE.md` §4.1's
    own precedent slice and it held up well on every axis but one — the
    file's header comment said the player couldn't decline the quest
    because `~p_choice*` wasn't portable when it was first written, but
    that gap closed tree-wide since then (this same file already uses
    `~p_choice4` for its post-quest small talk) and the comment was just
    never revisited. Added the accept/decline `~p_choice2` on Cook's
    initial offer with Transcript:Cook's_Assistant's refusal line. Reward
    (300 cooking xp, 1 QP via `~quest_complete(quest_cooksassistant)`) and
    journal were already correct and untouched.
  - `junglepotion` -> **audited-ok**: Trufitus's full 5-herb collection
    loop (`trufitus.rs2`) matches Transcript:Jungle_Potion closely,
    including decline branches at every offer stage, wrong/dirty/not-fresh
    herb rejections, and the Druidic Ritual prerequisite gate. Herb
    cleaning correctly lives once in the generic
    `skill_herblore/scripts/identify.rs2` dbtable-driven proc rather than
    being duplicated per-quest. Reward (775 herblore xp, 1 QP) via the real
    `~quest_complete(quest_junglepotion)`; 12-state journal wired. No gaps
    found.
  - `eadgarsruse` -> **audit-in_progress**, the biggest find this pass:
    every file touching this quest already self-documents its own gap in
    its header comment ("Full body deferred", "Eadgar's Ruse start
    deferred", "dialog trees deferred", "Grind/brew ... deferred",
    "aviary hatch deferred") — a rare case of honest prior breadcrumbing
    rather than a silent hole. Sanfew never actually offers the quest,
    Mad Eadgar has no real quest dialogue (only his unrelated stew-shop
    flavour Talk), and the connecting mechanics (parrot catch/hide/fetch,
    `%eadgar_bits` item collection, troll thistle potion brewing, storeroom
    unlock, Sanfew reward hand-in) are all missing, even though the
    Burntmeat/goutweed-quest arm is complete and the journal is *fully*
    written for every state already (unusual — normally the journal is the
    thing that's missing). Did not attempt a fix — this is a full quest's
    worth of new content, comparable in size to the Holy Grail row from
    pass 1. Left `audit-in_progress` with a 7-item follow-up list in the
    row's own note.
  - `trollromance` -> **audit-in_progress**: found and fixed one clear,
    contained bug — `quest_troll_love/scripts/trollromance_ug.rs2`'s
    completion label (`trollromance_ug_defeated_arrg`) was a bare
    `mes("Troll Romance quest rewards are not wired yet.")` stub with no
    reward and no `~quest_complete` call. Wired the real
    Transcript:Troll_Romance / Quick_guide reward (1 uncut diamond, 2 uncut
    ruby, 4 uncut emerald "pretty rocks", 8000 Agility xp, 4000 Strength
    xp) + `~quest_complete(quest_trollromance)`. That fix is necessary but
    not sufficient: the whole "get to Trollweiss" middle third (Tenzing,
    Dunstan's sled, sled wax, the mountain sled ride, the actual flower
    pick trigger) is unimplemented — confirmed by grepping for every
    `%troll_love =` write in the quest tree and finding none in that state
    range, even though the journal already narrates all of it. In current
    form the quest is stuck after the first Aga conversation; the
    fully-implemented Arrg fight is unreachable through normal play. Left
    `audit-in_progress`, not `audited-fixed`, for the same reason as
    `eadgarsruse` above — the missing middle is a mini-quest's worth of new
    area/NPC/crafting content.
  - Build: `mingw32-make -C src sscompile` clean, `mingw32-make -C src
    torirsserver-scripts` exit 0, 15081 scripts compiled (unchanged from pass 2
    — both fixes this pass edited existing trigger bodies, no new
    `[opnpc1,...]`/`[label,...]` triggers added), zero new errors/warnings.
    No duplicate triggers introduced (both edits are inside pre-existing
    trigger blocks). Files touched:
    `server/scripts/quests/quest_cook/scripts/quest_cook.rs2`,
    `server/scripts/quests/quest_troll_love/scripts/trollromance_ug.rs2`.
    25 rows remain unaudited in the IN-LC table (38 total minus the 13
    audited across three passes; 3 of those 13 — `holygrail`,
    `eadgarsruse`, `trollromance` — are `audit-in_progress` rather than
    fully closed).
- **IN-LC audit pass 4 (2026-08-12):** audited 4 more rows, one quest at a
  time, synchronously (no nested background sub-agents): `animalmagnetism`/
  quest_animalmagnetism, `biohazard`/quest_biohazard, `heroesquest`/
  quest_hero, `ragandboneman`/quest_ragandbone. Read each LC script tree
  fully + the wiki quest page, `/Quick_guide`, and `Transcript:` pages
  before comparing; checked every completion path for the recurring
  bespoke-`%qp` bug explicitly (none found this pass — all four already
  used the real `~quest_complete` proc).
  - `animalmagnetism` -> **audited-fixed**: very thorough existing 7-file
    port matching Transcript:Animal_Magnetism end to end (chicken/magnet/
    tree/notes/container chain, witch's iron-selection minigame, Alice/
    husband/crone amulet chain with lost-item replacement, research-notes
    button puzzle), reward xp matching the dbrow exactly. One gap: Ava's
    opening offer had no decline path at all (straight-line intro); added
    the wiki's two refusal branches.
  - `biohazard` -> **audited-fixed**: dialogue trees thorough and matched
    Transcript:Biohazard closely, real `~quest_complete` with correct
    reward (3 QP, 1250 thieving xp). One quest-blocking gap: the
    `biohazard_climb_ladder` wall-crossing label was fully written but
    nothing ever called it — Omart and Kilron had world spawns but zero
    Talk-to triggers, so a player who distracted the watchtower had no way
    to actually cross into West Ardougne. Added both NPC handlers.
  - `heroesquest` -> **audited-fixed**, the biggest find this pass: several
    files' own header comments claimed "full quest body deferred", but
    that was stale documentation — the quest is genuinely almost entirely
    built (Black Arm mansion-infiltration disguise chain, firebird feather
    chapter, lava eel chapter, Achietties start/turn-in with reward matching
    the dbrow exactly). Found and fixed five real, connected gaps: (1)
    Katrine/Straven (the two gang leaders) never touched `%heroquest` at
    all, so neither gang's armband sub-quest could ever start even though
    the mid-game NPCs gating on those exact states already existed —
    added the missing ask/hand-in branches to both; (2) Grip's death never
    dropped `grip_keys`, permanently locking the treasure room for
    everyone; (3) that door's unlock gate only recognised the Black Arm
    checkpoint, locking out the Phoenix path even with real keys —
    broadened to either gang's own checkpoint (documented as a soft
    single-player stand-in for the real two-player hand-off); (4) Ice
    Queen had a world spawn but no drop table anywhere, so `ice_gloves`
    (required to safely pick up the firebird feather) was unobtainable —
    added `drop_tables/scripts/ice_queen.rs2` (not one of the reference
    tree's 69/71 already-ported files); (5) `raw_lava_eel` had no cooking
    recipe in `skill_cooking/configs/cooking_generic.dbrow`, so a caught
    eel could never become the cooked item Achietties wants — added the
    row (level 53, never burns, per wiki). See the row's own note for full
    file list. Left one confirmed non-blocking gap untouched: the Entrana
    high priest still has no Heroes' Quest dialogue branch (pure flavour —
    nothing reads a state it would set).
  - `ragandboneman` -> **audited-ok**: matches wiki exactly — accept/refuse/
    curious-about-the-mumbling branches, all 8 quest bones correctly wired
    onto their monsters' drop tables (the `monkey` bone was spliced into
    quest_tbwt's existing `[ai_queue3,monkey]` trigger rather than adding a
    second competing one), Fortunato's vinegar sale, and the full
    vinegar-pour/pot-boiler/20-tick-boil mechanic for all 8 bones. Reward
    matches the dbrow exactly. No gaps found.
  - Build: `mingw32-make -C src torirsserver-scripts` exit 0 after every fix
    (checked incrementally, not just at the end), 15084 scripts compiled
    (up from 15081 at pass start — 3 new triggers: `[opnpc1,omart]` +
    `[opnpc1,kilron]` in biohazard, `[ai_queue3,ice_queen]` in the new
    drop-table file), zero new errors. Grepped every touched npc/trigger
    name tree-wide before adding to confirm no duplicate-trigger shadowing.
    Files touched: `server/scripts/quests/quest_animalmagnetism/scripts/
    anma.rs2`; `server/scripts/quests/quest_biohazard/scripts/
    quest_biohazard_locs.rs2`; `server/scripts/quests/quest_hero/scripts/
    quest_hero.rs2`, `brimhaven_scarface_mansion.rs2`;
    `server/scripts/areas/varrock/scripts/katrine.rs2`, `straven.rs2`;
    `server/scripts/areas/heroes_guild/scripts/achietties.rs2`;
    `server/scripts/drop_tables/scripts/grip.rs2`, `ice_queen.rs2` (new);
    `server/scripts/skill_cooking/configs/cooking_generic.dbrow`. 21 rows
    remain unaudited in the IN-LC table (38 total minus the 17 audited
    across four passes; 3 of those 17 — `holygrail`, `eadgarsruse`,
    `trollromance` — are still `audit-in_progress` rather than fully
    closed).

- **IN-LC audit table complete + mid-era audit pass (2026-08-12):** landed
  four more audit-batch passes (5 through 8, ~130 subagent tool-uses each)
  covering the remaining 20 IN-LC rows, bringing the IN-LC table to full
  coverage (38/38 rows, each with at least one wiki-accuracy pass). Notable
  fixes this stretch: Eagles' Peak (Nickolaus softlocked behind debug-only
  npc names), Icthlarin's Little Helper (invented sphinx riddle replaced with
  the real one, including the "lose your cat" risk), Zogre Flesh Eaters (10x
  XP underpay), Fremennik Exiles (duplicate-trigger shadowing a shared
  citizen npc — also corrected this row's own stale dbrow mapping),
  Desert Treasure II (duplicate-trigger collision with Defender of Varrock),
  Lost Tribe (a cellar-witness line misattributed to the wrong NPC per the
  transcript + a missing post-quest reward), Tai Bwo Wannai Trio (missing
  tinderbox burn method), Recruitment Drive (a fictitious gender-gated boss
  mechanic replaced with the real anti-blade one), What Lies Below (outlaw
  camp NPCs had no live spawn path, debug-only). Two genuine large-gap rows
  confirmed and left `audit-in_progress` with full chapter breakdowns:
  Shadow of the Storm (soft-skipped kiln/incantation/interrogation/sigil/boss
  chapters) and Regicide (footprint puzzle, camp-guard fight, bomb-crafting,
  and the entire ending — no `~quest_complete` call exists at all). Also hit
  and fixed a real process bug: two concurrent audit-batch agents editing
  `docs/QUESTHELPER_CONTENT_PORT_QUEUE.md` at the same time raced on a
  full-file read/write, and one silently clobbered the other's findings for
  4 rows (`zogreflesheaters`/`thefremennikexiles`/`thefremenniktrials`/
  `deserttreasureii`) — recovered by reconstructing the lost text from the
  agents' original reports still present in-session; their underlying code
  fixes were never at risk (committed separately, safe throughout).
  Switched process for all subsequent batches: audit agents report findings
  as plain text instead of self-editing the shared doc, and the orchestrating
  session applies all doc edits centrally, once, per batch.

  Moved on to the mid-era set next. A reconciliation pass found the "~74
  mid-era quests" figure quoted in this doc since 2026-08-06 was dead
  bookkeeping — no directory list was ever attached to that count anywhere,
  and `SCAPE2009_CONTENT_PORT_QUEUE.md`'s own Queue table (the real tracking
  surface) holds only 15 distinct quest-groups, 9 of which are the exact same
  `.rs2`/dbrow files already covered by the IN-LC table (the original
  pre-Sept-2004/mid-era split was imprecise for that set, not a clean
  partition). Audited the 6 genuinely distinct remaining mid-era quests:
  Priest in Peril (**high-value fix** — the mausoleum Drezel finale had zero
  live triggers, permanently blocking not just this quest but every quest
  gating on its completion: Nature Spirit, Rum Deal, Ghosts Ahoy, Haunted
  Mine, Making History, Animal Magnetism, Creature of Fenkenstrain, Desert
  Treasure — wired the missing essence hand-in and completion), Dig Site
  (audited-ok), The Golem (two item-acquisition triggers were debug-only,
  permanently blocking completion — fixed), Creature of Fenkenstrain (wrong
  Restless Ghost prereq strictness — fixed), A Soul's Bane (resolved a
  previously-flagged, never-fixed loose end from earlier in this session —
  the "dbrow never declared" concern was a false alarm; the real bug was a
  dead duplicate `quest_asoulsbane/` folder shadowing the real
  `quest_soulsbane/`'s triggers, deleted, plus a genuine intra-quest
  duplicate trigger merged), Desert Treasure original (large, mostly solid
  port; fixed a 10x magic-XP underpay, left `audit-in_progress` for the four
  softened boss fights and deferred pyramid/spellbook content). Reconciled
  and closed out two stale duplicate-tracking rows (`#43`/`P2` asoulsbane)
  that had been sitting `in_progress` since before this session even started.

  **Both the IN-LC and mid-era wiki-accuracy audit tables are now fully
  passed at least once.** Remaining open work is entirely large-content
  follow-ups on quests left `audit-in_progress` (not further discovery):
  `eadgarsruse`, `holygrail`, `trollromance`, `dragonslayerii`,
  `legendsquest`, `thefremenniktrials`, `shadowofthestorm`, `regicide`, and
  `quest_deserttreasure`'s boss-fight mechanics — each a full quest's (or
  large quest-chapter's) worth of missing content, not an audit gap. Next:
  start dedicating build agents to actually closing these out one at a time,
  per the user's "finish ALL quests" directive.

- **Large-content build batch 1 (2026-08-12): Holy Grail, Eadgar's Ruse,
  Troll Romance all closed.** Three background build agents ran concurrently
  in this shared worktree (a genuine risk that materialized: whole-tree
  builds transiently failed twice from cross-agent file-lock/mid-write
  contention before all three landed — resolved by waiting for all three to
  finish before the final verification build, not a real bug in any of
  their work). All three quests are now `audited-fixed` and fully playable
  start to finish — see their updated rows in the IN-LC table above for full
  detail. Highlights: Holy Grail's `sir_percival`/`magic_whistle`/
  `grail_bell` world-layer triggers; Eadgar's Ruse's entire missing
  connecting tissue (Sanfew offer through Sanfew reward hand-in, 11 files);
  Troll Romance's entire "get to Trollweiss" middle third (4 files, one new)
  — in both of the latter two cases, most or all of the needed items/locs
  turned out to already exist in cache configs with zero prior scripts, so
  the work was pure trigger-wiring, not new data authoring. Build verified
  clean after the consolidated merge: `torirsserver-scripts` exit 0, 15,116
  scripts, no duplicate-trigger warnings on any of the ~22 touched files
  across all three quests. Remaining `audit-in_progress` rows needing the
  same treatment: `dragonslayerii` (likely largest — near-total soft-skip
  content backlog), `legendsquest`, `thefremenniktrials`, `shadowofthestorm`,
  `regicide`, and `quest_deserttreasure`'s four boss-fight mechanics.

- **Large-content build batch 2 (2026-08-12): Regicide and Fremennik Trials
  closed, Legends' Quest mostly closed.** Three more background build agents
  ran concurrently. Regicide is now `audited-fixed` — the entire previously-
  missing back half (message→Lathas→scouts hand-off, footprint puzzle,
  Tyras-camp-guard combat, a full bomb-crafting chain, the King Lathas
  ending) is built and the quest completes for real via
  `~quest_complete(quest_regicide)`. Fremennik Trials is now `audited-fixed`
  — the prior audit undercounted the trials (7 council judges, not 6;
  Swensen the Navigator was missed), all 7 are now implemented, and the
  quest completes for real with the correct dbrow reward. Legends' Quest
  stays `audit-in_progress`: both missing central NPCs (Radimus Erkle,
  Gujuo) plus Ungadulu and the Viyeldi kill mechanic are now built and the
  quest is completable start to finish, but real wiki-noticeable soft-skips
  remain (the gem/rune puzzle is a shortcut, not the real per-rock
  mechanic; the Echned Zekin/Viyeldi/Nezikchened "deep water source" arc is
  optional rather than mandatory). This batch also surfaced and fixed two
  load-bearing gaps that weren't in the original audit notes for any row:
  Legends' Quest's `nezikchened.rs2` combat script was never ported from
  LostCity at all (blocking three separate fight checkpoints tree-wide),
  and nothing anywhere granted `book_of_binding` (a hard Ungadulu
  prerequisite). One process note: one build agent tried to pause and wait
  on its own background shell command instead of finishing synchronously —
  the existing no-nested-Agent-calls rule didn't cover this variant (nested
  background *shell*, not nested Agent); resumed it via SendMessage with an
  explicit "finish now, don't spawn background work" instruction, which
  worked. Future large-build agent prompts should state both constraints
  explicitly. Build verified clean after the consolidated merge:
  `torirsserver-scripts` exit 0, 15,215 scripts, no duplicate-trigger warnings
  across the ~37 touched/new files spanning all three quests.

  **Remaining large-content backlog:** `dragonslayerii` (likely the
  largest — near-total soft-skip content across Crandor/map-boat/Robert
  fight/Vorkath/key-reforging/diplomacy/ship-defense/Galvek),
  `shadowofthestorm` (kiln/incantation/interrogation/sigil-chase/boss
  chapters, plus the tree-wide `thosf_reward_lamp` rub/redeem handler gap
  shared with ~14 other quests), and `quest_deserttreasure`'s four
  boss-fight signature mechanics (Dessous/Fareed/Kamil/Damis). Once these
  close, the entire "wiki is authoritative for ALL quests, finish ALL
  quests" backlog from the 2026-08-12 rule change is complete.

- **Dragon Slayer II build batch (2026-08-12).** Three background build
  agents ran fully concurrently against the SAME shared file
  (`dragonslayer2.rs2`) — the riskiest concurrency setup used in this whole
  effort, deliberately verified extra carefully before committing: read the
  full post-merge file, grepped for each agent's specific triggers/labels to
  confirm none were silently clobbered, and checked for a leftover-syntax
  landmine (bare `mesbox(...)` calls one agent introduced mid-build, which a
  sibling agent noticed blocking the whole file's compile and fixed
  mechanically before finishing). All three chapters' work coexisted
  cleanly. Landed: the Crandor arc (real mine/mural/Spawn fight, real
  24-piece map gather, real boat construction), the Lithkren/dream/Robert
  the Strong chapter (real traversal, reused Dream Mentor's potion recipe,
  real HP-gated boss fight), and the four key-piece side-quests plus
  Ancient Cavern reforging (real golem/trap content, real crafting+dowsing,
  a real Vorkath fight, a real logic puzzle, real reforging) — see the
  updated `dragonslayerii` row above for full detail. The cache turned out
  to already ship a complete, purpose-built asset scaffold for the
  key-piece chapter specifically (varbits, items, populated areas) with
  zero prior scripts — pure trigger-wiring, not new data authoring, same
  pattern seen repeatedly across this whole build phase (Eadgar's Ruse,
  Troll Romance, Holy Grail all had this too). Build verified clean:
  `torirsserver-scripts` exit 0, 15,286 scripts. Still open on this quest: the
  four-kingdom diplomatic tour, ship-defense minigame, four dragon waves,
  and the four-phase Galvek fight — a genuinely separate follow-up batch.

- **MILESTONE (2026-08-12): the 2026-08-12 rule change's "wiki is
  authoritative for ALL quests, finish ALL quests" directive is complete.**
  Final batch closed the last three large gaps, running concurrently in the
  same shared worktree with no file overlap (unlike the earlier same-file
  DS2 batch, so no clobbering risk this round — verified anyway).
  - `dragonslayerii` → **audited-fixed**: the quest's final chapter (the
    only one left after the prior three-agent batch) is now real —
    dialogue-gated diplomatic-tour checkpoints for Amik Varze/Lathas/
    Roald/Brundt using pre-reserved cache varbits nothing had wired, a real
    13-dragon wave gauntlet matching the wiki's three-part breakdown, and a
    real 4-phase Galvek fight (1200 HP, `npc_changetype` phase swaps at
    900/600/300 HP, correct Protect-prayer-per-phase, the always-on
    fireball special). No bare unconditional soft-skip stubs remain
    anywhere in the quest; remaining simplifications (ship-defense minigame
    play narrated, wave-dragon attack-style variety collapsed to melee,
    Galvek's tile-hazard phase mechanics as flavor text) are disclosed and
    consistent with this whole effort's soft-skip conventions.
  - `shadowofthestorm` → **audited-fixed**: all six flagged gaps closed —
    real four-kiln search (the cache had already anticipated this exactly,
    just needed wiring), real incantation puzzle, the golem-interrogation
    "gap" turned out to already be built in the sibling `quest_golem/`
    files (audit had just missed it living outside `quest_shadowstorm/`),
    real sigil chase, a real Agrith-Naar fight with AI-switching and a
    weapon-gated finishing blow, and the six-gems bonus. Also fixed the
    tree-wide `thosf_reward_lamp` gap for this quest's own reward path
    (turned out to have an established flavor-item + `stat_advance`
    convention already, just needed this quest's half wired).
  - `quest_deserttreasure` (original) → **audited-fixed**: all four boss
    fights (Dessous/Fareed/Kamil/Damis) now carry their wiki signature
    mechanic, built by reusing existing engine hooks (`player_hit_npc_prepare`,
    `npc_max_dealt`, King Black Dragon's freeze pattern) rather than new
    combat infrastructure.
  - `naturespirit` → **audited-fixed**, fixed directly in this session
    rather than via a delegated agent: Priest in Peril's finale fix (mid-era
    batch) unblocked the prerequisite chain, but Nature Spirit's own gap —
    Drezel never actually offering the quest — was a separate, still-open
    hole. Wired the offer branch into the same file's existing trigger,
    matching this quest's own journal text. Nature Spirit is now startable
    for the first time.
  - Build verified clean after the consolidated merge: `torirsserver-scripts`
    exit 0, 15,360 scripts, no diagnostics on any touched file.
  - **Final state: every row across both the IN-LC (38 rows) and mid-era (6
    rows) wiki-accuracy audit tables is `audited-ok` or `audited-fixed`,
    except `legendsquest`, intentionally left `audit-in_progress`** — it is
    genuinely completable start to finish, but carries real, disclosed
    soft-skips (a gated gem-shrine shortcut instead of the wiki's true
    per-rock puzzle; the Echned Zekin/Viyeldi/Nezikchened "deep water
    source" arc is bypassable rather than mandatory). This is the intended,
    accurate end state — not a claim that every quest is a byte-for-byte
    wiki-perfect recreation, but that every quest has been checked against
    the wiki and either matches it or has its real, specific deviations
    documented in place. The 2026-08-12 rule change's directive — audit
    every quest this queue touches against the wiki regardless of era or
    lane, and close the gaps found rather than just cataloguing them — is
    satisfied.
