# Quest authoring index: symptom -> where it is explained

One line per trap or fact, keyed by what you SEE (a ledger detail, a server sentence, a shape of the
run). `file: heading` names a file in this directory (`.md` omitted) and the start of a heading
in it. Not here? `grep -rn "<distinctive words>" docs/quest_authoring/`. A new fact goes into its
topic file with one line added here.

## Travel and finding things

- `screen_position`/`not_visible`/`no_row` from `talk_to`; `goto` does not parse; where an npc is -> start-and-travel: `goto_tile` before; traps-13-22: Trap 13
- `screen_position: <reason>`, `not_found` one tile from an npc -> traps-13-22: Trap 19
- where is this LOC placed (`loc_add`); a `loc_near -> not_found` sweep -> traps-23-33: Trap 29; gaps-world: A LOC's tile
- a `^*_coord` constant -> gaps-world: `^*_coord` decoding; A coord constant is where the player SHOULD go
- `on attempt 2 of 3 via ::goto` -> verbs-pointer: `t.player.goto_tile`
- a goto mid-fight runs back to the npc -> verbs-pointer: Since seam 15 a goto stops the player's action
- a teleport dialogue lands on your next click; every press `covered` -> start-and-travel: A dialogue or loc that
- `timeout settle_after_click` on a ladder/door; `ok teleport: ...` -> start-and-travel: Teleport locs
- `click_loc` `timeout` on a stile that landed -> start-and-travel: A short hop (stiles)
- `refused -- You can't go any further.` on a ladder -> start-and-travel: Why not `click_loc` the ladder
- ladders, stairs, trapdoors, basements (`z+6400`), a scene that fails to load -> start-and-travel: Floors and ladders
- a lever maze the hand-in never reads -> gaps-world: The `goto_tile` bypass
- `other_floor: ...`; a loc only on a lower floor -> verbs-pointer: One named copy
- `loc_near` level 2 on a bridge deck; goto to the wrong plane -> gaps-world: `t.world.loc_near` reports
- a shared object id's `WorldPoint` (`FAI_FALADOR_FURNACE`) is `not_found` -> gaps-world: Quest Helper's `WorldPoint`
- where a multi-floor room is (`Zone`/`WorldPoint`) -> gaps-dialogue: Rewards and steps
- `walk_near` wants a table -> start-and-travel: `walk_near` takes the `{kind=, id=}` table
- `by_symbol` says `ok` for an npc that is not there -> verbs-pointer: `t.player.by_symbol`
- a carpet ride moves you after a goto; `not in the client's entity pool` -> seam-facts: Seam 24: a postquest shop
- gray viewport, empty minimap after a far goto -> seam-facts: Seam pass 26, (b) (FIXED seam27; CONFLICT note at Seam 24)
- a jump/teleport loc answers before you land -> seam-facts: Seam pass 26, (c)
- a portal lands somewhere else (multiloc child) -> seam-facts: Seam pass 24, (b); Seam pass 25, (c)

## Pressing and clicking

- `covered` -> traps-13-22: Trap 21; gaps-dialogue: `covered`: step-off and other sides
- `none of 83 pixels hittested`, `hunted pose 1 (reach 99)` -> traps-13-22: Trap 21
- `the world is not picking`, `(gate at x,y: why)`, `yaw N framed nothing in 5 poses` -> traps-13-22: `the world is not picking`
- `I can't reach that!` -> start-and-travel: Doors; verbs-pointer: `t.player.click_loc`
- `reach_failed:`, `stood on with ::goto`, `stand_on_square` -> traps-23-33: `stand_on_square` needs; gaps-world: `coordz(...)`
- `target shares the player's tile and the step off it did not land` -> start-and-travel: `goto_tile` before
- `stepped off the target tile`, `pressed again from side N` -> gaps-dialogue: `covered`: step-off; gaps-world: `talk_to` steps off
- a row went PASS -> FAIL: bare `map_flag` `ok` regraded `refused` -> gaps-world: `click_loc` `ok` on a bare
- a press answered by NOTHING at all -> gaps-world: `talk_to` steps off; a press answered by NOTHING
- `talk_to` `covered` after the dialogue opened; `click_minimenu` ok, no page -> gaps-world: An `ok` from a CLICK verb
- one npc in a tight cluster fails every press -> gaps-world: `click_minimenu`'s hunt
- `drive.op bypass:`; `drive.op` `ok` and nothing happens -> verbs-pointer: `t.drive.op`; gaps-world: A `t.drive.op` that
- `I need to get closer to use that.` -> gaps-world: `coordz(...)
- `away from you`, `it said 'X' and did not move`, `may be the npc's own wander` -> verbs-pointer: Four outcomes
- `aimed at slot N`, `a selector never falls back to another copy` -> verbs-pointer: Naming the copy
- `moved a,b -> c,d between aim and press; re-aimed` -> verbs-pointer: `t.player.talk_to`
- a push drives the npc the wrong way -> verbs-pointer: A directional push
- Sheep Herder: prods `timeout`, the east fence, ranking copies -> traps-13-22: `press` has no hunt; gaps-world: Ranking multiple copies
- `pressed the copy at X,Z,L`; stepping stones -> verbs-pointer: One named copy; seam-facts: Seam pass 25, (f)
- `scan-meter: yield one frame`, `instruction budget exhausted` -> verbs-pointer: The 8,192-row; seam-facts: Seam pass 24, (e)
- `bad argument #2 to symbol (string expected, got table)` -> verbs-pointer: A symbol STRING, not a table
- `click_loc <sym> -> <other sym> (base|multiloc)`; an open door `not_found` -> traps-13-22: Trap 20
- `screen_position: no loc <id> (<symbol>)`; loc ids past ~6887 -> traps-13-22: Trap 20; Trap 22
- a Temple of Light door -> verbs-pointer: A symbol STRING
- a quest door narrates and never moves -> seam-facts: Seam pass 25, (a)
- a chop loc answers before the swing -> seam-facts: Seam pass 27, (l)
- a tile read after a `mes()`-first trigger is the OLD tile -> gaps-dialogue: A first effect that
- a top-down shot, `QUEST shot-aim` -> verbs-root-and-quest: The shot camera
- a seam row turned red when rows were added above it -> gaps-world: A seam row that

## Dialogue and chat

- `expected kind=npc, got player` -> traps-13-22: Trap 18
- `no_row` from `choose:` -> traps-01-12: Trap 3
- `mismatch` on an em dash; no `player:` echo -> gaps-dialogue: A `choose:` is not always followed
- `the dialogue closed after 2 page(s)` at a payout -> gaps-dialogue: A payout branch
- `expected kind=npc, got none` on a `mes()` line -> gaps-dialogue: Only the chat procs
- `refused -- stale reopen` -> traps-23-33: Trap 30; gaps-dialogue: Back-to-back choice menus
- `expected kind=<x>, got options` after a shop opened -> traps-23-33: Trap 30
- `chat.play` hangs on one page -> traps-23-33: Trap 30
- `a resume is already outstanding` -> traps-01-12: Trap 11; Trap 4; traps-13-22: Trap 22
- `not_visible: no dialogue is open` -> start-and-travel: The chat verbs; traps-23-33: Trap 26
- `orphaned page`, `dropping [...]` -> verbs-chat: `t.chat.play`; gaps-dialogue: Since seam24; seam-facts: Seam pass 23, (m)
- `no dialogue in 5 tick(s)`, `dialogue npc is up` -> traps-23-33: Trap 26
- a hung page from a loc/held/queue trigger -> traps-13-22: Trap 22; Which chat procs need an npc
- an `[ai_*]` script talks before binding a player -> seam-facts: Seam pass 21, (b)
- the scaffold's list takes the wrong branch; `-- CHECK choose` -> traps-13-22: Trap 17; gaps-world: `-- CHECK choose (page N)`
- the header reads `Someone` -> seam-facts: Seam pass 26, (a)
- an `~objbox` page -> verbs-chat: The entry forms; seam-facts: Seam pass 27, (n)
- a wiki `{{tbox}}` line -> traps-13-22: Trap 22; seam-facts: Seam pass 23, (s)
- `chat.close` `ok`, varp never moved -> gaps-dialogue: `t.chat.close()` does not RESUME
- `t.exec(name, t.chat.continue_)` is `bad verb/target` -> traps-01-12: Grind debugprocs
- a random dialogue loop; Robin's rune-draw -> verbs-chat: Any other unbounded; `t.game.runedraw`
- an overhead `npc_say` -> gaps-world: A fight is a wait; verbs-pointer: Four outcomes
- a book -> verbs-ui-and-npc: Readable books
- `msg.await` FAILs on a line already in chat -> gaps-dialogue: `t.msg.await` never sees

## Items, held ops and shops

- `inv.count` reads the OLD count -> traps-23-33: Trap 24; Trap 25; gaps-dialogue: `t.settle()` does not
- `setup.::give ...` FAIL -> traps-23-33: Trap 23
- fourteen slots of tutorial kit -> start-and-travel: Inventory
- `Nothing interesting happens.` from `use_item_on_item` -> gaps-combat: `use_item_on_item` order
- `page none->mesbox '...' -- backpack unchanged`, `You need another ingredient` -> gaps-dialogue: A recipe that
- `pressed 'Examine <thing>', an ordinary op row` -> verbs-inventory-shops: `t.player.use_on`
- `[npc reach retry: ...]`, `[backpack: gained X ...]` -> verbs-inventory-shops: `t.player.use_on`; traps-23-33: Trap 24
- `armed by this call (tab nil nil)`; every later `use_on` refuses -> gaps-world: `t.player.use_on` waits
- `inv_op` `refused` vs `timeout`; `[pressed on attempt 2` -> verbs-inventory-shops: `t.player.inv_op`
- `[STRAY DROP: ...]`; op 1 dropped the item -> verbs-inventory-shops: `ifop1=`..`ifop5=`
- `inv_op` on a reward casket is `timeout` -> seam-facts: Seam pass 22, (j)
- a held op silently refused during a `p_delay`; `(press N)` -> verbs-inventory-shops: `t.player.equip`; seam-facts: Seam pass 26, (d)
- `not_found` on a worn item -> verbs-inventory-shops: `t.player.use_item_on_item`; `t.player.unequip`
- a cheat's worn item lands in the hat slot -> seam-facts: Seam pass 27, (h)
- `drop` FAILs on a second copy; drops nothing with a shop open -> seam-facts: Seam pass 27, (n); traps-01-12: Grind debugprocs
- `this shop was not opened through shop.open` -> verbs-inventory-shops: `t.shop.buy`
- `stocks 0 <item>`, `You don't have enough coins.` -> verbs-inventory-shops: Read the shop's row
- buy it or `::give` it? -> gaps-combat: Shops: only for an item
- a floor obj on a deck, `ground 0` -> seam-facts: Seam pass 18, (b)
- a private ground drop is not there yet -> gaps-combat: Three world facts
- an xpreward lamp; a make-X menu -> seam-facts: Seam pass 25, (e); verbs-ui-and-npc: Skill-multi menus

## Vars, stages and the journal

- a stage poll reads `0` forever -> gaps-world: A stage poll; gaps-combat: A varbit your quest writes
- `[server content]`, `no client copy`, `pack/varp.alloc` -> verbs-state-and-vars: `t.var.varp`; gaps-combat: Unaddressable varps
- `t.var.expect` answers `not_found` -> verbs-state-and-vars: `t.var.expect`
- `::setvar` refused on a carrier; `::setvar` of a varbit -> seam-facts: Seam pass 8, (b); verbs-root-and-quest: `t.cheat`
- a `_visible` bit hides the npc; `resolved to NO child` -> traps-23-33: Trap 28
- `%if1..%if6` -> traps-23-33: Trap 33; seam-facts: Seam pass 22, (h); Seam pass 23, (p)
- no server `.varp`; `t.quest._read_content` -> seam-facts: Seam pass 21, (d); Seam pass 22, (i)
- `journal_open` times out after early PASSes -> gaps-dialogue: `t.ui.journal_open` can degrade
- `timeout first_line=nil` after a dialogue -> gaps-world: `t.player.use_on` waits
- lint refuses a var name -> gaps-combat: What the ledger; `sscompile` contention

## Fights

- `hp no bar -> no bar`, splats only on the player -> traps-23-33: Trap 31
- `I'm already under attack.` -> verbs-combat: `refused`, meaning two; gaps-combat: `::passive`; seam-facts: Seam pass 22, (g)
- attack `refused` `I can't reach that!` -> verbs-combat: Attack fights ONE copy
- attack `timeout` on a won fight -> gaps-combat: `t.player.attack`'s settle
- attack `no_row` for a just-added npc -> gaps-combat: Three world facts
- `player.died` ended the run -> verbs-combat: `t.player.alive()`
- `dead after N tick(s)` with the npc standing; `corroborated by` -> verbs-combat: A kill is never proved
- `await_dead_engaged` `no_row` -> verbs-combat: `t.npc.await_dead_engaged`
- `ok` while a neighbour still fights -> gaps-combat: A crowded spawn
- one blow, then nothing; `target=-1 interact=kind0/op0` -> gaps-combat: A fight that lands
- `OUT OF <food>`, `never needed to eat` -> verbs-combat: Eating
- `no_runes`; a cast `ok` that never landed; `re-CAST` -> verbs-combat: Casting; Cast fights re-cast
- `This spell only affects skeletons, ...` -> verbs-combat: Crumble Undead
- `every press's menu held only Cancel` -> verbs-combat: A spell on a ground obj or a loc
- `TELEPORTED to x,z,l`, `no teleport` -> verbs-combat: A spell with no target
- a boss dies in one hit -> seam-facts: Seam pass 24, (a)
- died to a prayer-bypass roll -> gaps-combat: Three world facts
- fever spiders; hitting through a door -> verbs-combat: Fever spiders
- a claim survives a loc teleport -> seam-facts: Seam pass 25, (f)
- `rune_platebody` cannot be worn -> sampler-findings: Sample sonnet-b32
- a wait that passed before seam28 now fails; an unhittable caster -> seam-facts: Seam 28

## Completion and rewards

- `quest.scroll_title` fails after a postquest shop -> seam-facts: Seam 24: a postquest shop
- `no painted journal within 20 ticks` -> gaps-combat: `quest.journal` can time out
- no `QUEST COMPLETE!` banner; no journal proc -> seam-facts: Seam 24; Seam pass 25, (d); gaps-dialogue: `t.ui.journal_open`
- `[scroll already photographed:` -> gaps-dialogue: Journal branches shadowed
- the `quest.scroll` shot has no scroll -> gaps-combat: The `quest.scroll` photograph
- last dialogue ran, varp never moved -> gaps-dialogue: `~quest_complete_rewards`
- completion not readable after the last page -> gaps-dialogue: Completion, reopened dialogues; `t.msg.await` never sees
- the reward differs from the wiki; split scroll lines -> gaps-dialogue: `~quest_complete_rewards`; `t.scroll.rewards()`
- which rewards need a row -> coverage-and-gate: A reward row; gaps-dialogue: Rewards and steps

## Gate, lint, coverage and the ledger

- FAIL `hollow`, an empty PASS detail, `bad verb/target` -> traps-01-12: Trap 12; verbs-root-and-quest: `t.exec`
- `[bad ledger argument]`; a boolean in `t.step` -> gaps-dialogue: `t.step`'s second argument
- `no shot recorded` on a row that never shot -> gaps-combat: What the ledger
- duplicate MD5, `[frame unchanged]`, `unchanged since` -> traps-01-12: Trap 4; verbs-root-and-quest: `t.shot`
- a fingerprint match on a real frame -> coverage-and-gate: Fingerprints; sampler-findings: What `helper_coverage`, (d); seam-facts: Seam pass 26, (g)
- no `quest.*` row; stage row names -> traps-13-22: Trap 14; coverage-and-gate: Minimum shape
- 8 rows / 4 PNGs, 4 / 2 -> coverage-and-gate: Minimum shape; traps-13-22: Trap 15
- a FAIL right before `t.blocked()` -> gaps-combat: What the ledger
- one row per retry attempt -> gaps-combat: Retry loops
- CHEAT, UNMATCHED, CONTENT_GAP, FULL -> traps-23-33: Trap 32; coverage-and-gate: How a guide step is DRIVEN
- a refused or `UNVERIFIED` marker -> traps-23-33: Refused markers
- BRANCH-IN, PARTNER, NOT-A-STEP, OBSOLETE, ANY-OF, `EQUIVALENT` -> traps-23-33: Verified markers
- `declared crossing:`, `unclaimed stand-on` -> traps-23-33: What helper_coverage could
- `returnToX` UNMATCHED; two talks need two presses -> coverage-and-gate: How a guide step is DRIVEN
- a loop over step names grades UNMATCHED -> seam-facts: Seam pass 26, (e)
- a spell/tool step done by walking up; telegrab -> traps-23-33: Trap 32; What helper_coverage could not see
- a grind debugproc graded CHEAT -> traps-23-33: Sanctioned grind debugprocs
- a step with no target (`EmoteStep`, `PortTaskStep`) -> traps-23-33: Steps with no target
- mention-only credit; `MissCheeversStep` sub-steps -> sampler-findings: What `helper_coverage`
- `-- CHECK` refused; `-- CHECK gather` -> traps-13-22: Trap 17; running: `boss_fight=yes`
- MIXED verdict on a blocked run -> coverage-and-gate: `helper_coverage.py` reads FULL
- a FAIL above a full completion -> gaps-dialogue: A `t.check`/`t.expect` FAIL
- `helper_coverage.py --ledger`, `--lua <copy>` -> seam-facts: Seam pass 27, (p); traps-23-33: Refused markers

## Harness and runs

- a second `run.py` refuses -> running: The failure block
- `sscompile` takes minutes -> running: `--no-build` still pays; gaps-combat: `sscompile` contention
- `setup.::setlevel ...` FAIL -> running: `run.py --script` runs setup
- `SIGSEGV` (exit -11), a whole-client hang -> running: SIGSEGV, whole-client hangs
- run moved to the background; is it alive? -> running: A long run is moved to the background
- ~2,000 ticks, `max_frames`, `MAX_FRAMES_CEILING`, a long content wait -> gaps-combat: A run has about
- `after 0 repair(s)` -> traps-01-12: Grind debugprocs; gaps-combat: `::mortton_repairtemple`
- a rejected quest's file is gone -> gaps-dialogue: A rejected quest's file; running: Resuming
- a `git status` full of others' quest files -> running: `--no-build` still pays
- re-runs replay the same rolls -> seam-facts: Seam pass 24, (c); Seam 28; Seam pass 27, (i)
- `--no-publish`, `TORIRS_SCRIPT_DIR`; relog and MAP_BUILD_COMPLETE -> seam-facts: Seam pass 17; Seam pass 18, (d)
- a stalled script (`TORIRSSERVER_VERBOSE=1`) -> running: `boss_fight=yes`
- a stale `docs/quests/` walkthrough -> running: Resuming; gaps-world: Read `docs/quests/<quest>.md`

## Sea and session

- `t.world.tile()` is off the map aboard -> seam-facts: Seam pass 16, (a)
- `await_gone` passes hollowly aboard -> seam-facts: Seam pass 18, (a)
- running aground; the Catherby setup -> verbs-sail-session: `t.sail.*`
- `reply=173`; a relog lost spawned npcs -> verbs-sail-session: `t.session.logout`

## Content-side facts (content_bug reports, reviewers)

- a child-symbol `[opnpc1]` is dead code -> traps-13-22: Trap 19; The fix idiom
- `mes()` over 252/199 chars; `cannot be declared in a var-u8 length` -> traps-23-33: Trap 27
- `.loc_find` + `.loc_change` aborts -> traps-23-33: `.loc_find` does not exist
- `COMBAT_START_EXEMPT`, `APNPC2_TWIN_EXEMPT` -> traps-23-33: The sweep (seam20)
- `check-chatnpc-without-npc`; `check-npc-script-player-suspend` -> traps-13-22: Which chat procs; seam-facts: Seam pass 22, (k)
- `check-quest-journal-banner` -> seam-facts: Seam pass 25, (d)
- a loc with no op in the cache; `op1=` in `all.loc` -> gaps-dialogue: `covered`: step-off and other sides
- a map edit; a wall's `rot` -> traps-23-33: A map edit needs; Decoding a wall's `rot`
- a loc across water; `[aploc]` -> seam-facts: Seam pass 27, (k)
- `npc_del` then `npc_coord`; `*.struct`; `if_setangle`; `OBJ_SPAWN_EXCLUSIONS`; wiki 403 -> seam-facts: Seam pass 8, (c), (a); 16, (b); 27, (o); 24, (h)
- `map_findsquare` origin; `::spawn` on a parapet -> seam-facts: Seam pass 21, (e)
- a hub swallows another quest; merged relevance branches -> seam-facts: Seam pass 23, (l); gaps-dialogue: `~quest_complete_rewards`
- `last_item` vs `last_useitem` -> gaps-combat: `use_item_on_item` order; seam-facts: Seam pass 21, (c)
- named content gaps (The Feud, One Small Favour, Shadow of the Storm) -> content-gaps
- place facts (Watchtower, Tourist Trap, Death's Coffer, Witchaven, Ghosts Ahoy, Hazeel Cult) -> seam-facts: passes 23-28
- `::complete quest_druid` does nothing -> gaps-combat: `::complete` takes a DBROW name

## Citations: resolving a number or a name

- "trap N", "rule N", "point N" -> traps-01-12, traps-13-22, traps-23-33 (numbers unchanged)
- "section 1/2", "S2" -> start-and-travel; "section 3" -> verbs-*; "4" -> verbs-root-and-quest; "6" -> running; "7" -> coverage-and-gate
- "section 8" -> gaps-dialogue, gaps-world, gaps-combat, running, seam-facts, sampler-findings, content-gaps; its "first bullet" -> gaps-dialogue: Completion
- "payout-reopen", "luthas-payout/customs-pay", "mesbox/p_delay recipe" -> gaps-dialogue: A payout branch
- "the varp seam", "never-arriving-varp", "journal cross-check" -> gaps-world: A stage poll
- "section 8's player.attack note" -> gaps-combat: `t.player.attack`'s settle; "Ernest the Chicken's maze" -> gaps-world: The `goto_tile` bypass
- "a hunted press", "use_on's backpack tab press" -> traps-13-22: Trap 21; gaps-world: `t.player.use_on` waits
- "the budget note" -> gaps-combat: A run has about
- "rule (b)", owner rules (a)-(e) of 2026-09-23 -> `tools/quest_gate/author_batch.workflow.js`; here traps 16 and 32
- "seam pass N (x)" -> seam-facts (passes 8, 16-18, 21-28); a seam number in a verb or trap dates that behaviour
- samplers: b31, b32 -> sampler-findings; b12, b17 -> Trap 21; b13, b27 -> Trap 12; b16 -> gaps-world: `coordz`; b27, b28 -> Trap 32; b29 -> Trap 17; b33 -> Trap 4
- `docs/QUEST_AUTHORING.md:161` (pre-split line: `goto_tile` is a `::goto`) -> verbs-pointer: `t.player.goto_tile`
