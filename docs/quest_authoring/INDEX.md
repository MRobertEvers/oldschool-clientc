# Quest authoring index: symptom -> where it is explained

One line per trap or fact, keyed by what you SEE (a ledger detail, a server sentence, a shape of the
run). `file: heading` names a file in this directory (`.md` omitted) and the start of a heading
in it. Not here? `grep -rn "<distinctive words>" docs/quest_authoring/`. A new fact goes into its
topic file with one line added here.

## Travel and finding things

- "I can't reach that!" right after a fence squeeze; pulled back to the fence after a goto -> gaps-world: A fence squeeze pulls you back
- a trap door landing never reads `z > 6400` (Mourner HQ basement 2044,4628) -> gaps-world: The Mourner HQ basement is an instance region
- "Nothing interesting happens." on a wall into a boss lair (FIXED seam31); the Crandor hole refuses from one side -> gaps-world: Crandor
- a guard tower's ladder `not_found` as `mcannonladder` on the ground floor -> gaps-world: Dwarf Cannon: the tower's two ladders
- Ikov: `walk_to` times out at x=2644 going east from the lever; thrown back west on the lava bridge -> gaps-world: Temple of Ikov
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
- a loc with only Examine; no op to go back down a hole (`goDownHole`) -> gaps-world: A loc with only Examine
- `the cast never ran` right after a `p_delay` step (a chop, a door); `refused: player is delayed`; `(press N: ...)` in a cast detail; `zanarismagicdoor` -> gaps-world: Leaving the Entrana dungeon
- `walk_to ... stalled at ... -- locs with an op beside the stop: <loc>`; a walk that stops before a rock bridge, stepping stone or log -> verbs-pointer: A walk stops at an obstacle (seam34); seam-facts: Seam pass 34 (b)
- a ship trip lands in the sea (2831,3334,0); `useGangPlank` has no menu row; the Entrana monks' ferry -> seam-facts: Seam pass 34 (g) (FIXED); docs/quests/ladders/deviousminds.notes.md

## Pressing and clicking

- `player.attack: first press covered (...) -> settled camera: ...`; a boss raised one row after a press that walked you answers `covered` -> verbs-combat: A covered Attack press, a boss that teleports, a timed walk (seam35); seam-facts: Seam pass 35 (e)
- `click_loc: the walk outlasted the 20-tick settle; followed it N more tick(s)`; a timed lift missed while walking -> verbs-pointer: A press whose walk outlasts the 20-tick settle (seam35)
- `map_flag: no dialogue in 5 tick(s)` on a freed/respawned multinpc form (Desert Treasure's troll parents) -> seam-facts: Seam pass 33 (e); traps-13-22: trap 19
- a search that sometimes gives nothing (a `stat_random` roll, a cave-in mesbox); passes on one `--name`, fails on another -> seam-facts: Seam pass 33 (g); running: `--script` runs are deterministic
- `t.inv.slot` reads `''` for Dwarf remains (obj id 0) -> seam-facts: Seam pass 33 (b) (FIXED)
- an interface does not redraw while open; client varp == server varp but `t.ui.text` unchanged; `[frame unchanged]` rows on a puzzle panel -> seam-facts: Seam pass 34 (c) (FIXED)
- a greegree wearer (any `p_transmogrify`) still drawn as a human in a monkey stance; `::transmog` -> gaps-world: Monkey Madness (FIXED seam34); seam-facts: Seam pass 34 (f)
- `click_obj` op 5 answers covered/no row on a knife; picking one obj from a drop pile -> verbs-pointer: `t.player.click_obj`
- `click_obj`: the menu shows only the table's/rock's rows (`pickset held=false`) for an item lying ON it; `hunted ... hovered +0,-64` -> verbs-pointer: `t.player.click_obj` (raised stacks, seam32)
- `talk_to` `refused -- the client said 'I can't reach that!'` -> traps-23-33: Trap 26 (FIXED seam29)
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

- a `|` drawn inside a line (`find the|helmet`); `chat.play` meets an extra page (`npc:together.`) -> verbs-chat: A long line is more than one page
- a Talk-to missing on a multinpc shell drawn as a `*_noop` form (Fight Arena's Sammy) -> gaps-world: A multinpc shell whose every visible child
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
- `not_visible: no dialogue is open` on the `chat.play` after a `click_loc`/`use_on` whose script runs `mes` + `p_delay` first (a raft, a search) -> gaps-dialogue: A page behind `mes()` + `p_delay`
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
- a `|` glyph mid-line in a dialogue shot; an entry spanning a line break fails -> gaps-dialogue: A `|` in a page's text (FIXED seam29)
- page count before a menu unknown; an answer-any-dialogue row; a `conv()` helper to unroll -> verbs-chat: A page count you do not know
- a `chat.play` fragment across a `|`/`<br>` break; `'through......you land'` -> verbs-chat: fragments see each break as ONE space (seam30)
- a `~mesbox` in Mort Myre gone a tick later, `no dialogue is open` -> seam-facts: Seam pass 30 (g)

## Items, held ops and shops

- `click_obj` `timeout` on a pickup that landed; `expect_has` misses an obj with id 0 (FIXED seam32; `t.inv.slot` still names it `''`) -> verbs-pointer: `click_obj` answers `timeout`; seam-facts: Seam pass 32 (c)
- a bucket on a sink/pump answers "Nothing interesting happens." -> gaps-world: A sink or water pump (FIXED seam29)
- a book page, journal line or dial letter proved only by a PNG -> verbs-ui-and-npc: `t.ui.text` / `t.ui.expect_text`
- `inv.count` reads the OLD count -> traps-23-33: Trap 24; Trap 25; gaps-dialogue: `t.settle()` does not
- `setup.::give ...` FAIL -> traps-23-33: Trap 23
- fourteen slots of tutorial kit -> start-and-travel: Inventory
- `Nothing interesting happens.` from `use_item_on_item` -> gaps-combat: `use_item_on_item` order
- `page none->mesbox '...' -- backpack unchanged`, `You need another ingredient` -> gaps-dialogue: A recipe that
- `pressed 'Examine <thing>', an ordinary op row` -> verbs-inventory-shops: `t.player.use_on`
- `[npc reach retry: ...]`, `[backpack: gained X ...]` -> verbs-inventory-shops: `t.player.use_on`; traps-23-33: Trap 24
- `armed by this call (tab nil nil)`; every later `use_on` refuses -> gaps-world: `t.player.use_on` waits
- `inv_op` `refused` vs `timeout`; `[pressed on attempt 2` -> verbs-inventory-shops: `t.player.inv_op`
- `inv_op ... -> 0 left [settle_after_click]` on a Wield/Wear; `[WORN <item>: worn 0 -> 1, wear slot N]` -> verbs-inventory-shops: A Wield/Wear through `inv_op` (FIXED seam34)
- a shop that opens EMPTY (`cell 3 of shopmain:items is not mounted` after `holds K stocked slot(s) of K`; Ardougne silver stall, Zaff, Pie Shop) -> verbs-inventory-shops: `cell N of shopmain:items is not mounted` (FIXED seam35); seam-facts: Seam pass 35 (a)
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

- `Which qp? varb456_tog_qp_before_return, ...` on a `setup.::setvar` row; `not_found/cookquest`; `stage() -> not_found ... (no varp and no varbit of that name)`; lint `the bare var name` -> verbs-state-and-vars: Var names carry their kind and id (seam37); seam-facts: Seam pass 37 (a)
- `var.await_server <varbit> ... read: 0` while the dialogue moved; `(varbit, server content copy; base varp N is never transmitted)`; `-- no client copy` on `var.expect` -> verbs-state-and-vars: A quest varbit reads 0 (FIXED seam35); seam-facts: Seam pass 35 (d)
- `::complete has no arm for that quest.` for a quest with a completion (Regicide, Family Crest, Big Chompy...) -> QUEST_SERVER_CHEATS.md `::complete` (seam35 arms table); seam-facts: Seam pass 35 (f)
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

- `slot N left the pool ... came back as slot M ... -- followed it`; a teleporting boss graded dead `corroborated by ABSENCE` while alive -> verbs-combat: A covered Attack press, a boss that teleports, a timed walk (seam35)
- a Haunted Mine `t.drive.op` for Dayth's Attack or the lift -> gaps-world: A timed lift or a `covered` boss press (both real presses since seam35)
- `; progress t+10 hp .., ..` at the end of an await_dead detail -> verbs-combat: `t.npc.await_dead(npc, ticks=60`
- a quest boss's first bar reading is empty (`0/60` after one hit, Elvarg; FIXED seam31) -> gaps-combat: Elvarg dies to the first hit
- Dad's surrender page closes before the loop reads it -> gaps-combat: Troll Stronghold: Dad's surrender
- player hits missing on an npc for several ticks mid-attack (Melzar, trolls, KBD) -> seam-facts: Seam pass 29 (b)
- every `::spawn`/`npc_add` of one npc on one tile drops the same loot -> seam-facts: Seam pass 29 (a)
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
- a `::spawn` drop hunt sees too few drops (Imp Catcher beads) -> sampler-findings: Sample sonnet-b34, (b)
- battle mage `hp no bar -> no bar`; an `[opnpc2]` ending in `p_opnpc(2)` -> traps-23-33: A binding that re-enters itself
- a LostCity `npc_getmode = opplayer2` test never true mid-fight -> seam-facts: Seam pass 30 (c)

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
- a second branch replayed after completion with `::<quest>reset` -> sampler-findings: Sample sonnet-b34, (a)
- `reward.*` shot shows the Quest List tab; `reward tab ... not selected`; `10,500 Magic XP` read as 500 -> verbs-root-and-quest: Reward rows photograph the tab; `t.scroll.reward_xp`

## Long quests

- an await on a crop, brew or cooldown times out; a wait of real minutes; `date_minutes` -> gaps-world: A step that waits real minutes
- my context is filling up; which guide steps are left; never read the guide Java -> relay: The ladder (`ladder.py <id>`)
- quest has more than 30 steps; handed one leg; where the previous author stopped -> relay: Legs; relay: Working one leg (`ladder.py <id> --leg K`)
- which row failed; the ledger is too long to read -> relay: After a run (`fail.py <id>`, `--all`)
- replaying legs 1..K-1 every run; `--from-leg`/`--only-leg`; `checkpoint k NOT written`/`refused: a dialogue is open`; `STALE ... (legs_hash)`; `a checkpoint run is for authoring` -> relay: Checkpoints
- "You're a bit too busy" on a `--from-leg` run; a clock-stamped varp after a checkpoint -> relay: The clock: map_clock comes back
- `checkpoint k refused: the player is in combat` long after the fight; `STALE` after a seam or pack rebuild; setup gives overflow the 28-slot backpack -> relay: Still "in combat" after the fight; Running one leg; The backpack overflows
- a run over ~8 min killed by the shell cap; `still running: last row ...`; fail.py `IN PROGRESS` / exit 3 -> relay: Runs longer than the shell cap
- `run stalled: no client tick for N s`; `run stalled at boot`; `timed_out` column `stall`; `has no heartbeat ... stall detector off` -> relay: Runs longer than the shell cap (seam32)
- `not published <id> (the run did not reach t.quest.expect_complete` -> relay: Honesty

## Gate, lint, coverage and the ledger

- `leaves the <step> side ... without crossing the ladder/stair/trapdoor`; a sub-step graded CHEAT or ALTERNATIVE -> coverage-and-gate: A promoted sub-step
- `t.check(name, true, ...)` after a `t.ui.invoke` or a pickup -> sampler-findings: Sample sonnet-b36
- FAIL `hollow`, an empty PASS detail, `bad verb/target` -> traps-01-12: Trap 12; verbs-root-and-quest: `t.exec`
- `[bad ledger argument]`; a boolean in `t.step` -> gaps-dialogue: `t.step`'s second argument
- `no shot recorded` on a row that never shot -> gaps-combat: What the ledger
- duplicate MD5, `[frame unchanged]`, `unchanged since` -> traps-01-12: Trap 4; verbs-root-and-quest: `t.shot`
- a fingerprint match on a real frame (a cutscene fade, a dark corner) -> coverage-and-gate: Fingerprints (FIXED seam35: both canvas probes); seam-facts: Seam pass 35 (b)
- `matches the title_screen fingerprint` (rows driven while logged out, e.g. after a refused relog); `gate.py --probe` -> coverage-and-gate: Fingerprints; seam-facts: Seam pass 35 (b)
- `presses opN '<op>' on <loc>, a gating op: a '<verb>' step needs the travel op`; Pick-Lock then `goto_tile` past a trapdoor -> coverage-and-gate: "presses op5 'pick-lock' ..." (seam35); seam-facts: Seam pass 35 (c)
- `lands at x,z,l (<zone>) from x,z,l (<zone>) ... without pressing the <gate> <step> names`; a ladder kind `<Kind><<composite>[<condition>]`; a step no guide panel lists -> coverage-and-gate: A branch-only step (seam36); seam-facts: Seam pass 36 (b)
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
- sonnet-b41: `teleportAway` UNMATCHED with its row PASS -> coverage-and-gate: A step named `teleportAway`; next mesbox missing after an objbox -> verbs-chat: The next script's mesbox; `is_modal() == true` never holds -> verbs-ui-and-npc: `t.ui.is_modal() == true`; stage row reads the old value after a goto out of a zone -> gaps-world: A stage a zone exit writes

## Harness and runs

- `run.unfinished FAIL run ended without finishing`; exit 0 with no SUMMARY; `at the frame budget` -> running: A run that ended unfinished
- `QUEST row-begin` / `QUEST progress` lines in client.log; where a slow run spends its ticks -> running: A run that ended unfinished
- a `--script` rerun blocks on the same random roll every time -> running: `--script` runs are deterministic
- `setup.::wield <item> FAIL`; a `::wield` that printed Usage -> seam-facts: Seam pass 29 (e)
- prove content against HEAD without editing the shared tree -> seam-facts: Seam pass 29 (c)
- a second `run.py` refuses -> running: The failure block
- `sscompile` takes minutes -> running: `--no-build` still pays; gaps-combat: `sscompile` contention
- `setup.::setlevel ...` FAIL -> running: `run.py --script` runs setup
- `SIGSEGV` (exit -11), a whole-client hang -> running: SIGSEGV, whole-client hangs
- run moved to the background; is it alive? -> running: A long run is moved to the background
- `render-skip: N frame(s) drawn` in client.log; `TORIRS_RENDER_SKIP`, `--render-every-frame`, `::renderskip`, `t.render.skip`/`frame`; does skip change a run (no -- same frames, state drawn late) -> running: Render skip
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

- a sled ride that walks; `P_TEMPRUN is not implemented`; an anim that plays over a protected stance; a monkey body with a human chathead; `TORIRSSERVER_ANIM_TRACE` -> seam-facts: Seam pass 35 (g)
- `::run 0` leaves the run orb on (it sets run ENERGY) -> QUEST_SERVER_CHEATS.md `run` row; seam-facts: Seam pass 35 (g)
- `COORD requires an active entity` on `.huntnext`; `the active loc is gone` after `loc_del`; `not implemented` obj_name / inv_dropitem; `cannot resolve param value ^...` -> seam-facts: Seam pass 31, (a) (FIXED)
- a double door gone for the session after one swing (`loc_del` + `loc_add` on the same tile) -> seam-facts: Seam pass 31, (c) (FIXED: Seam pass 32 (e))
- `LOC_CHANGE requires an active entity` on a `.loc_*` op; `.loc_find` changing the wrong leaf; `loc_name`/`loc_param` aborting after `loc_del` -> seam-facts: Seam pass 32 (e) (FIXED)
- Oziach knocking `%dragon_oracle` back; a cooldown stamp misread after a server restart; `zq_rash_timer` perm -> seam-facts: Seam pass 32 (f)
- fails under one `--name`, passes under another; druidspirit `I'm already under attack.` at killGhasts -> seam-facts: Seam pass 32 (g)
- Zanaris Door man / market door / exit ladder missing -> gaps-world: Zanaris has no Door man (seam32)
- `::setvar varp6733_priestperil_mausoleum` in a setup; the golden-key gate after `::complete quest_priestinperil` -> seam-facts: Seam pass 31, (b) (druidspirit dropped its setvar, seam32)
- a guide step with no content branch (guild master's map questions, one-click magic door; FIXED seam31) -> content-gaps: Dragon Slayer
- `multinpc_shells.csv` rung labels (`0=` is multinpc1, `N+=`) -> traps-23-33: Trap 28
- a garbled note text ("bncket of nnilk") that is the real game's -> seam-facts: Seam pass 29 (f)
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
- `::complete has no arm for that quest.`; `::complete quest_wanted` leaves `wanted_main` 0 (arms for touristtrap, templeofikov, trollstronghold, wanted exist since seam33) -> gaps-combat: `::complete has no arm for that quest.`; QUEST_SERVER_CHEATS.md: `::complete <quest row>`
- a `$row = <name>` compare that never matches; a name that is both a dbrow and a varp (`quest_wanted`) -> seam-facts: Seam pass 33 (d)
- a quest stage moves BACK after re-asking an npc (Oracle, Oziach) -> seam-facts: Seam pass 33 (f); Seam pass 32 (f)
- "The trapdoor opens..." / "Lab stairs and trapdoors sit locked." then `goto_tile`; reward shots show the Quest List; "10,500" xp reads 500; `ogre_bow` missing after completion; Harold's door or objbox gap -> sampler-findings: Sample sonnet-b35; gaps-world: Paterdomus, Death Plateau; verbs-root-and-quest: `t.scroll.reward_xp`; gaps-combat: Feldip; gaps-dialogue: A payout branch
- sonnet-b42: `cutscene_row_required` on a route you skipped (Shilo's table raft); `by_symbol` misses a door after one use (`thzq_tombrooml2/3`); `walk_to` `timeout` under spider attacks or before a rock bridge; `checkpoint k refused: ... in combat` in the room the next leg starts in; `if_click` nil past `switch_s`; backpack full of weeds or a second weeds `drop` FAIL; Desert Treasure's ring/signet never given; no Roald cutscene; no pre-quest boatman; Consortium ores by `::give` -> verbs-cutscene: `cutscene_row_required` names a site; gaps-world: A loc that changes symbol; Underground Pass; relay: in a room the next leg; verbs-ui-and-npc: `if_click` on `nil`; verbs-inventory-shops: A rake fills; content-gaps: Rewards that are scroll text only; coverage-and-gate: A setup `::give` of The Giant Dwarf's
- sonnet-b43: `attempt to index a string value` on `t.skill.snapshot()` or a nil `.xp`; `walk_to` never reaches Underground Pass's witch, cat or demons (`bridgecollapsed1/2`); `t.drive.op` for a timed lift or a `covered` boss press (Haunted Mine); `::god 1` in setup; a boss bar `0/30` after one hit (Desert Treasure) -> verbs-state-and-vars: `attempt to index a string value`; gaps-world: Underground Pass: `walk_to` cannot reach, A timed lift or a `covered` boss press; sampler-findings: Sample sonnet-b43
- sonnet-b44: `Usage: ::complete quest_cooksassistant` from a scaffolded `::complete quest_gobdip`/`quest_tree`, or a `::setvar varp111_treequest`/`varp161_upass` prerequisite; a quest varbit (`mm_daero`) stuck at 0 in `var.await_server`; a nil `.current`; aground on every heading after a mooring; "Nothing interesting happens." on an agility log/leaf; a walk two tiles short of `upass_mud`; a pick-lock then `goto_tile` into a lair -> gaps-combat: The scaffold's `::complete <folder>`; verbs-state-and-vars: A quest varbit reads 0, `attempt to index a string value`; verbs-sail-session: Aground on every heading; start-and-travel: An agility crossing; gaps-world: Underground Pass: the mud pile; sampler-findings: Sample sonnet-b44
- sonnet-b45: `goto_tile` across a gate the guide shows only in a ConditionalStep branch (Marim, `enterGate`) while helper_coverage reads FULL (FIXED seam36: CHEAT); a detail `table: 0x...` -> sampler-findings: Sample sonnet-b45; a full relay run ends `at the frame budget` though every `--from-leg` passes -> relay: The full run ends at the frame budget; a kill's bones are not in the pack -> verbs-combat: A kill's bones are on the floor
- matthew-mbp-m4-b47: `Usage: ::complete` from `quest_death`, or a `::setvar varp161_upass` prerequisite; FULL while a `goto_tile` jumps the Underground Pass grid; a trap row PASS while the server said "...and fail"; Koschei's third-form wait fights on into the fourth; `menu has no row for it` on a field crop; a candle lantern that will not relight; an IF1 button bound `[if_button1,...]` never fires -> gaps-combat: The scaffold's `::complete <folder>`; coverage-and-gate: The ladder lists a route back to front; sampler-findings: Sample matthew-mbp-m4-b47; verbs-combat: A boss whose death spawns its next form; verbs-pointer: Field crops pick on op 2; content-gaps: The Lost Tribe; traps-23-33: Trap 33
- matthew-mbp-m4-b48: a `goto_tile` fixed in one leg still runs in a later leg's second walk; a `t.check(name, true, ...)` summary row (`navigateMaze`) over a `goto_tile` across a maze; "The temple is in ruins..." then `goto_tile` to the well room -> sampler-findings: Sample matthew-mbp-m4-b48 (the door FIXED b48-seam1)
- sonnet-b46: a `--from-leg` run moved to the background at 120 s -> running: A 3-5 minute `--from-leg` run; `walk_to` never reaches Underground Pass level 1's cage or temple -> gaps-world: `walk_to` never arrives on an upper level; `choose:How can I kill Dessous?` times out, or Damis answers "I'm already under attack." -> content-gaps: Desert Treasure: Malak
- a multinpc shell never changes form on the client (sote_tertiary) -> seam-facts: Seam pass 30 (b)
- `COORD requires an active entity` after `.huntnext`; `the active loc is gone`; an npc death script aborting after `p_delay` -> seam-facts: Seam pass 30 (d), (e) (the `.huntnext` / `loc_del` gaps FIXED: Seam pass 31 (a))
- Paterdomus trapdoor/gates/holy barrier locked after `::complete quest_priestinperil` -> seam-facts: Seam pass 30 (f); the bit-20 `::setvar` FIXED: Seam pass 31 (b)
- a jug/bowl/vial at a sink or a bucket at the Edgeville well: `Nothing interesting happens.` -> gaps-world: A sink or water pump
- Underground Pass demons or Iban never spawn (`holthion=no_row`); a `[mapzone,1_...]` / `[mapzone,<level>_...]` header that never fires -> gaps-world: Underground Pass: the demons and Iban's temple (FIXED seam36); seam-facts: Seam pass 36 (a)
- "You can't go any further." on a TRAPDOOR into a separate underground region (the H.A.M. lair); a multiloc climb with no maplink row; a lair only a `goto_tile` reaches -> seam-facts: Seam pass 37 (b) (H.A.M. FIXED seam37)
- "Nothing interesting happens." on a Regicide dense forest (`regicide_cross_over2`), stranded at 2237,3149, the forest guard `I can't reach that!`; a crossing row reading `teleport: A -> B (a jump no walk makes ...)` -> seam-facts: Seam pass 37 (c) (FIXED seam37)
- player locked for good after a hit (Iban's bolt); `dropping [ai_timer,...], which suspended while [...] waits`; an npc death script whose `p_delay` loses `npc_coord` (Kalrag) -> seam-facts: Seam pass 37 (d); gaps-world: Underground Pass: the finale (FIXED seam37)
- a LostCity npc symbol that finds nobody (`caveguide5`, Koftik in the post-Iban pocket); Talk-to `I can't reach that!` on Koftik -> seam-facts: Seam pass 37 (e); gaps-world: Underground Pass: the finale
- "The temple is in ruins... / ...You cannot enter." on Iban's temple doors (`upass_templedoor_closed_*`) in Regicide; `goto_tile 2010,4709,1` to the Well of Voyage; `click_loc` on the temple door `not_found` from Iban's door landing -> gaps-world: Underground Pass: Iban's temple door; seam-facts: Seam pass matthew-mbp-m4-b48-seam1 (a) (FIXED b48-seam1)

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
- "seam pass N (x)" -> seam-facts (passes 8, 16-18, 21-37); a seam number in a verb or trap dates that behaviour
- samplers: b31, b32, b34, b35 -> sampler-findings; b12, b17 -> Trap 21; b13, b27 -> Trap 12; b16 -> gaps-world: `coordz`; b27, b28 -> Trap 32; b29 -> Trap 17; b33 -> Trap 4
- `docs/QUEST_AUTHORING.md:161` (pre-split line: `goto_tile` is a `::goto`) -> verbs-pointer: `t.player.goto_tile`
- `the dialogue closed after N page(s)` right after a cutscene; a page a cutscene ends in -> verbs-cutscene: A cutscene between two dialogue pages
- `cutscene_row_required`; `cutscene:` rows; `no_cutscene`/`unfinished`; `expected keyframe #N ... not found`; `checkpoint k NOT written: the camera is server-driven`; a quest whose content scripts `cam_moveto`/`cam_lookat` (`cutscene_sweep.py`, DROPPED/PARTIAL fail `check-quest-cutscenes`) -> verbs-cutscene (`t.cutscene.await`, `t.world.camera`); a fade with no camera op -> verbs-ui-and-npc: Fade overlays
- `cutscene_row_required` names a site on a route the guide never takes; `cutscene_exempt_refused:`; `t.cutscene.exempt`; `gate.py --cutscene-as` -> verbs-cutscene: `cutscene_row_required` names a site on a route you did not take (seam34)
- `check-quest-cutscenes` red on mm PARTIAL / troll_love DROPPED; `lostcity_tree`; `cutscene_sweep.py --sites` -> verbs-cutscene: the sweep paragraph (seam34); seam-facts: Seam pass 34 (e)
