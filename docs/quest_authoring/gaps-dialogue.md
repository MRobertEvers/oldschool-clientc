# Section 8 gaps: dialogue, completion and the ledger

Section 8 ("Gaps reported by authors") is unnumbered; it is split by theme across the `gaps-*.md`
files, `running.md`, `seam-facts.md`, `sampler-findings.md` and `content-gaps.md`. A citation like
"section 8's payout-reopen" is resolved by `INDEX.md`.

## Completion, reopened dialogues, hollow oks and read timing (the long first gap)

*Origin: section 8 ("Gaps reported by authors").*

Completion is asynchronous: `t.ticks(3)` between the final `chat.play`/`drain` and
`quest.expect_complete()` is load-bearing, not padding -- a completion triggered by `use_on` queues
BEHIND its own mesbox chain (The Restless Ghost's `priest_coffin_use_skull` opens three pages before
`[queue,priest_quest_complete]` lands), so click through first and start the `chat.play` list at the
page already open.

### A `choose:` is not always followed by a `player:` page; copy text byte for byte

A `choose:` entry is not always followed by a `player:` echo page -- only where the branch opens
with `~chatplayer*`; read the branch in its `.rs2` and copy its text BYTE for byte (`chat.play`
matches with `string.find(..., plain=true)`, so a real em dash retyped as `--` is a `mismatch`).

### `t.settle()` does not cover an inventory sync; `loc_change` needs 2-3 ticks

`t.settle()` does not cover an inventory sync: a bare `t.inv.count` read right after a dialogue's
own `inv_add`/`inv_del` chat line can still read the OLD count (Prying Times' crowbar read 0 with
the grant message already on screen) -- poll with `t.inv.await(name, n, ticks)` instead -- including
on a RECORDING row written `t.check(name, true, "<what was read>")` around a bare `t.inv.count`,
which reads like a passive observation and is the same unsynced read: a dialogue's own `mes()` line
settles the click a packet AHEAD of the `inv_del`/`inv_add` queued behind it in the same proc
(Eadgar's Ruse, `dryThistle`, cost a full run to this), so the `inv.await` goes before the count
even when the row only reports -- and give a `loc_change` 2-3 ticks before a second `click_loc` can
see the new form.

### `covered`: step-off and other sides; a loc with no op string is a USE-ON target

`covered` is trap 21's answer -- the pressed pixel is not on the target's model -- and re-aiming the
camera cannot fix it on its own, because the eye orbits the PLAYER. `click_loc` and `use_on` now
handle the two cases they can: they step off the target's own tile before projecting (row detail
`stepped off the target tile x,z (a,b -> c,d)`) and, when the menu still carries no row and the
player is within a tile, walk to up to three other sides of it and press again
(`pressed again from side N of x,z (a,b -> c,d, asked e,f)`).

Measured on Mort'ton's `shades_experimentshelf`: from the east at one tile the press lands, from the
south at one tile it answers covered, on the loc's own tile it answers covered from all five poses.
So a `covered` that survives all of that means the STANDING TILE is wrong -- the row names the tile
each press was made from, so move the goto, not the camera -- or the loc publishes no such op, and
then the fix is NOT `configs/all.loc`: that file and `configs/all.npc` are cachepack EXPORTS, so an
`op1=` added there reaches the SERVER only (`ToriRSServer_SceneLocOpOverlay`) while the client
builds its minimenu from `cache.osrs239`, which this lane never re-bakes -- measured
byte-identically on Mourning's End's apple press, `build/quest_gate/seam_mourning_probe_before`
against `_probe_locop`: same three menu rows, same `drive.op -> no_row`.

A loc with NO op string in the cache (`brokeclockpole_red`, `mourning_orchard_applebarrel_empty`,
`mourning_sack_full`) is usually a USE-ON target rather than a broken op number: check Quest
Helper's own step text for "Use the X on the Y", and report it as a content seam wanting
`[oplocu,<loc>]` beside the `[oploc1,<loc>]` (the pairing `corsaircurse.rs2:286` and
`mcannon_broken_cannon.rs2:15` already use -- and on a multiloc it binds to the BASE, because the
trigger lookup is child-then-base while the OP validation is child-only).

`op3=hidden` in a `.loc` block stays the one legitimate authored op: it is server-side by design.
And it can still answer `covered` right after a `goto_tile` teleport on a target that worked twice
already: `t.ticks(2)` first.

### A payout branch that `if_close`s and reopens (`hunt.lua`'s worked example)

A payout branch that calls `if_close` right after its npc page, grants with a plain `mes()`, waits a
`p_delay`, then opens a FRESH `~p_choice*` cannot be driven with one continuous `chat.play` list --
it dies on the reopened `options` entry with `the dialogue closed after 2 page(s)`, every run,
because that close is REAL and the reopen is a separate dialogue (`luthas.rs2`'s crate payout:
`if_close` + `mes("Luthas hands you 30 coins.")` + `p_delay(3)` + `~p_choice4`;
`customs_officer.rs2`'s `customs_pay` is the same shape with `p_delay(2)` + `p_telejump` +
`~mesbox`).

End the first list on `"end"` (kind `none`), `t.await` a level predicate polling `t.chat.kind()` for
the reopened kind, then play the reopened dialogue as its own row -- `hunt.lua`'s
`hunt.luthas_payout_reopen` / `hunt.customs_pay_reopen` rows are the worked example. Read it too
early and the shot you publish is that branch's own `Please wait...` transition frame.

Death Plateau's Harold has the same shape twice (the ale and the blurberry special): an objbox,
then `if_close` + `p_delay`, then the next page. End the `chat.play` list at the objbox, run
`t.ticks(...)`, and start a second list at the next page.

### A page behind `mes()` + `p_delay` after a loc click (Waterfall's raft, Golrie's junk)

*Origin: sampler sonnet-b40 (waterfall review).*

`click_loc` and `use_on` settle on the first edge their click makes, which is often the script's
first `mes()` line. When the script narrates with `mes()` + `p_delay(N)` before its first dialogue
page, a `chat.play` on the next row finds nothing open and answers `not_visible: no dialogue is
open`. The click did land, so do not "fix the click" as start-and-travel's chat-verbs note suggests.
Two examples:

- Waterfall's raft (`[oploc1,lograft_waterfall_quest]`) runs four `mes` + `p_delay(2)` and a
  `p_teleport` before Hudon's first page.
- Golrie's search (`golrie.rs2`) runs `if_close` + `mes` + `p_delay(2)` twice between his two
  pages.

Before the `chat.play`, `t.await` a level predicate on `t.chat.kind() ~= "none"`. Give it a deadline
that covers the script's summed `p_delay`s. `waterfall.lua` does this for `boardRaft-chat`,
`talkToGolrie-rest` and `talkToGolrie-farewell`:

```lua
t.await({ level = function() return t.chat.kind() ~= "none" end, note = "raft lands and Hudon's page opens" }, 20)
```

### `ok` is not proof: `click_obj`, `inv.await(name, 0)`, `[oplocu]` hand-ins, auto-shots

> CONFLICT (kept both): the `click_obj` nil-detail sentence below predates seam27. Trap 12 (seam27)
> says its `ok` now reads `click_obj: met after N tick(s)`. The later date wins; write the counts you
> read back either way.

`ok` is not proof that the thing your row is NAMED after happened: `t.player.click_obj` answers `ok`
with a nil detail -- a hollow verb alongside trap 12's `t.cheat`, so call it directly and write the
counts you read back; `t.inv.await(name, 0, ticks)` never waits at all (`total >= 0` is always true)
-- poll `count == 0` with `t.await` when you mean "wait for it to be consumed". Read the quest's own
triggers before picking a verb: an `[oplocu,<loc>]` hand-in is
`t.player.use_on(item, t.player.by_symbol("loc", sym))`, never a second `click_loc`, and a loc that
wires only ONE direction of a climb (Monk's Friend's blanket ladder descends and has no return trip)
takes a `goto_tile` back, same as any far step.

The auto-shot photographs the FRAME, not the assertion: a `reward.*`/`inv.*` row gets whatever panel
happens to be mounted (after `expect_complete`, the quest list), so the reading belongs in the
detail and the row's name must not claim a picture the driver cannot take.

### A rejected quest's file; the eager before-page snapshot (2026-09-20)

A rejected quest's `.lua` may not be the file on disk -- a revert removes it -- resume from the
`git show <sha>:test/quests/<id>.lua` its queue row names, not from what `git ls-files` tracks.
`_settle_after_click` now snapshots the before-page EAGERLY for every click verb, the same as
`talk_to` always did (2026-09-20) -- a mesbox an earlier row left open is no longer swallowed as the
NEXT `click_loc`/`click_obj`'s own settle event, so the old habit of calling `t.chat.close()` before
every click just to guard against that is gone; call it directly, with `t.check`, only when
dismissing a mesbox IS the row's own point.

### Rewards and steps are what THIS pack wired; where Quest Helper lives on this machine

Section 7's "every reward Quest Helper lists" means every reward THIS pack grants: the arguments of
the quest's own `~quest_complete_rewards(row, "<rewards>", icon)` call plus the
`stat_advance`/`inv_add` beside it (The Restless Ghost's `stat_advance(prayer, 11250)` is the
literal 1125 its scroll advertises; Scorpion Catcher passes an EMPTY rewards string and no xp at
all, so `quest.points` is the only reward row there is).

Same for steps -- a Helper step is real only where this pack wired it to the quest varp, so trace
the `.rs2` before driving a jailer, a key or an agility shortcut. And `helpers/quests/` is not in
this checkout but it IS on this machine -- `tools/questhelper_extract.py`'s `DEFAULT_QH` is
`/Users/matthewevers/Documents/git_repos/quest-helper/src/main/java/com/questhelper/helpers/quests`,
the tree `new_quest.py` already reads, so `new_quest.py`'s banner names a path a later reader can
open after all.

Open the quest's own `*.java` whenever the pack cannot answer where something physically IS: its
`Zone`/`WorldPoint` constants are the only place a multi-floor building's layout is written down in
either tree (`thegolem/TheGolem.java`'s `throneRoom = Zone(2709,4879,2)-(2731,4919,2)` names the
PLANE the throne sits on, and a kitchen npc can stand a whole floor above the storeroom door beside
him -- which is what a `click_loc` hunt that fails at long range is usually telling you). The
content pack's own `.rs2`/`.spawn`/`.loc`/`.dbrow` files stay the source a scaffold guess is CHECKED
against.

### A recipe that answers with `~mesbox`; the Herblore `db_getfield` engine defect (fixed 2026-09-20)

> CONFLICT (kept both): the end of this passage says "never reach for `inv_op(..., 1)` to complete a
> use by hand: op 1 on rev-239's backpack is the shift-click-drop chain and it drops the item on the
> floor." `t.player.inv_op` (`verbs-inventory-shops.md`) records FIXED (seam20): op 1 keeps the item,
> and `[STRAY DROP: ...]` marks a binary built before the fix. The later fix wins on the drop; the
> rest of the advice (continue the page, then `inv.expect_has`) is unchanged.

A recipe that answers with `~mesbox` PAUSES the content script, so `use_item_on_item` settles on the
PAGE and the item is only made once the page is dismissed -- a detail reading
`page none->mesbox '...' -- backpack unchanged` is the verb being right. That exact shape is also
how an ENGINE defect reads from a quest file, and it cost two independent runs of Shades of Mort'ton
a `content_bug` verdict: every Herblore brew in this pack answered
`page none->mesbox 'You need another ingredient to make this potion.'` because `db_getfield` pushed
0, not `null`, for a column a row does not state, and 103 of the 104 brew rows state no third
ingredient.

Fixed 2026-09-20, so a potion step is drivable at all now -- but the lesson stands: a verb naming
the SERVER's own sentence is the verb being right, and the next place to look is the trigger, then
the engine, not the click. Put `chat.play`/`continue_` on the next row and the `inv.expect_has`
after THAT, and never reach for `inv_op(..., 1)` to complete a use by hand: op 1 on rev-239's
backpack is the shift-click-drop chain and it drops the item on the floor.

### `t.msg.await` never sees a line that already landed; prefer `var.await_server` before `expect_complete`

`t.msg.await(text, ticks)` matches only lines whose serial is NEWER than a snapshot it takes at the
moment you call it (`state.lua`), so it can never see a line that has ALREADY landed -- and the verb
on the row above usually made it land: `_settle_after_click`'s second arm resolves on any new chat
line, so `inv_op`, `click_loc` and `use_on` have typically already waited for the very message you
are about to await. Right after one of those, `t.msg.expect(text)` -- any line still in the ring --
is the correct verb, and `t.msg.await` is for a line that arrives LATER, behind a `p_delay` or a
queued script; Roving Elves' `plantSeed` row read a false FAIL for a whole run on exactly this, its
two `mes()` lines already in the ring before the await took its floor.

Prefer `t.var.await_server(<completion varp>, <complete value>, 10)` to a flat `t.ticks(N)` ahead of
`quest.expect_complete()`: a sleep tuned to one hand-in path silently stops being enough when a
later fix in the same file makes a second path reachable with less padding ahead of it.

## Back-to-back choice menus (`stale reopen`) and scripts whose first effect is a `mes()` line; `player.delayed` (seam24)

*Origin: section 8 ("Gaps reported by authors").*

A quest test that reads a page immediately after a click into a content script that opens `mes()` +
`p_delay(N)` + `~mesbox` is racing the `p_delay`; `fluffs.lua`'s crate loop is the case in point,
and trap 26's wait is what makes it deterministic rather than lucky. The same reopen boundary bites
with NO `p_delay` and no mesbox in sight: two back-to-back `~p_choice*` menus in one branch make
`chat.play` answer `refused -- stale reopen` (`chat.lua`'s identical title+rows classifier), because
the second menu paints the same interface and the play loop cannot tell it from the one it just
answered.

Section 8's mesbox/p_delay recipe is the fix for a plain choice-to-choice chain too: end the list,
`t.await` the reopened kind, then play a SECOND list.

### A first effect that is a bare `mes()` (Mourning's End II wall support)

The mirror shape is a trigger whose FIRST effect is a bare `mes()` line and whose real effect lands
many ticks later: Mourning's End II's wall support (`mend2_puzzle1.rs2`
`[oploc1,mourning_temple_agility_hanging]`) is `mes` -> `p_delay(1)` -> `~agility_exactmove` ->
`~agility_success` roll -> `p_teleport` (or the fall's `p_telejump`), and that first line satisfies
the click settle's chat-line arm, so `click_loc`/`use_on` answer `ok` before the player has moved. A
`t.world.tile()` read right after is PRE-action state, not a stall -- `t.await` a tile in either
landing set (far side or the fall tile) or the closing `mes()` line before grading it.

### Since seam24: `player.delayed`

Since seam24 the server honours LostCity's `player.delayed`: a click made while ANOTHER script of
the player's sits in its `p_delay` never starts a trigger on top of it -- a world op
(npc/loc/obj/player, and a move) LATCHES and fires once the delay ends, a held-item op, inventory
button or interface-script button is REFUSED (`torirs_server_world.c` `player_delayed`) -- so an
item pressed during a door's or lever's delay does nothing: wait the delay out. That closed the
orphaned-page shape (`dropping [proc,chatnpc_anim], which suspended while [proc,...] waits`). A
`::goto` is NOT refused and still moves the player under a parked script.

## Only the chat procs open a PAGE; a bare `mes()` is a log line

*Origin: section 8 ("Gaps reported by authors").*

Only a `~chatnpc*` / `~chatplayer*` / `~chatnpc_specific*` / `~mesbox` / `~objbox` / `~p_choice*`
call opens a PAGE. A bare `mes()` is a chat-LOG line and never a `chat.play` entry, however it is
spelled and however many modal pages surround it -- Enter the Abyss' `~eta_charge_orb` and
Biohazard's `elena.rs2` both bury `mes()` lines between `~chatnpc_anim` pages, and a list that
spells one dies with `the dialogue closed after N page(s)` or `expected kind=npc, got none`.

Read a log line back with `t.msg.expect` (already landed) or `t.msg.await` (still to come), never
with a page entry. The same branch is where the reopen rule bites twice: section 8's
`if_close`/reopen worked example (`hunt.lua`) shows ONE boundary per branch, and `elena.rs2`'s
`found_distillator` has TWO back to back -- one after the vial description, a second immediately
after the item grant -- so count the `if_close`s in the branch you are driving and give each
reopened dialogue its own row, rather than assuming the shape is once-per-branch.

## A `t.check`/`t.expect` FAIL does not stop the run

*Origin: section 8 ("Gaps reported by authors").*

A `t.check`/`t.expect` FAIL does not stop the run. Only a raised Lua error, `t.blocked` and
`t.finish` do -- so a run can reach a full, honest completion (all four `quest.expect_complete` rows
PASS) with a FAIL still sitting ten rows above it, and `run.py`'s printed "last FAIL/BLOCKED row" is
not where the run stopped. Read the whole ledger, not the failure block alone, before deciding what
a red run actually reached.

## `~quest_complete_rewards` does not write the completion varp; merged relevance branches; the reward branch's own guard

*Origin: section 8 ("Gaps reported by authors").*

`~quest_complete_rewards` awards the quest points and paints the scroll and NOTHING else: every
quest writes its own completion varp itself, on the line beside the queue call. Throne of
Miscellania had ten `= ^misc_complete` readers across the tree and no writer at all, so its last
stage was unreachable while every one of them was dead code. When a quest reaches its final dialogue
and the varp will not move, read the completion label itself before blaming a gate.

The neighbouring rule: where one quest's relevance branch is merged into ANOTHER quest's `[opnpc1]`
trigger (Royal Trouble does this on five of Throne of Miscellania's npcs), that branch sits ABOVE
the host's own switch and swallows every stage at or past its threshold -- so it must gate on the
host's COMPLETE value, never an intermediate stage. Read the completion branch's OWN guard before
writing a reward row, because an item granted earlier in the same run can flip which one it hands
out: A Tail of Two Cats' `[queue,twocats_quest_complete]` is
`if(doctors_hat=0){grant doctors} else if(nurses_hat=0){grant nurses}`, and its Apothecary step
already granted a doctor's hat, so the completion actually grants a NURSES hat -- a
`reward.doctors_hat` row written from the wiki's "doctor's or nurse's hat" would fail on a correct
run.

### A brief names skill XP that the content pays as a lamp; "1 Quest Point" twice on the scroll

*Origin: reviewer and sampler matthew-mbp-m4-b53, round 3 (Contact!).*

An orchestrator brief asked for "7000 Strength + 7000 Magic literal". Contact! pays 7,000 Thieving
XP and a Combat lamp with two wishes of 7,000 XP in a combat skill (wiki Combat_lamp oldid
15185818; Quest Helper lists the same reward as two 7,000 XP combat lamps). The Strength and Magic
in the brief were an earlier test's lamp picks. They are not documented reward amounts. Take the
reward rows from the wiki and Quest Helper, not from a brief. Assert the fixed XP with
`skill.expect_gain`. For a lamp, rub it, pick a skill, and assert the documented amount gained in
the skill you picked. Name the picked skill in the row (`lamp.strength_xp_7000`).

`~quest_complete_rewards` writes the quest point line itself. Contact!'s reward string started with
`1 Quest Point|` (`contact_shared.rs2:22`), so its scroll showed "1 Quest Point" twice. No other
quest in the tree passes it. Check the scroll shot for a doubled first line. FIXED by seam pass
matthew-mbp-m4-b53-seam4 (OSRS-Content 4fa2748185): the scroll shows one quest-point line, and
"and bank" is visible again (`b53s4_scroll_qp_after`, 9/0).

### `t.scroll.rewards()` returns wrapped lines

`t.scroll.rewards()` hands back the scroll's lines as the SCROLL wrapped them, not as the quest
wrote them: Throne of Miscellania's third reward arrives as two entries ("Ring of wealth teleport
to" / "Miscellania"), so a `find()` for the whole phrase fails on a scroll that is perfectly
correct. Match the un-wrapped prefix, or check each line on its own.

## `t.chat.close()` does not RESUME a script

*Origin: section 8 ("Gaps reported by authors").*

`t.chat.close()` is idempotent, but it does not RESUME a script: a `~mesbox` that suspends its
branch (trap 22) needs `t.chat.continue_(true)`, and `close()` answers `ok` while the lines below
the mesbox -- the `npc_del`, the varp write -- never run (Prince Ali Rescue's `[opnpcu,lady_keli]`:
the rope tie read `ok` and `%princequest` sat at `prince_guard_drunk` until the continue landed).
And `t.exec(name, t.chat.continue_)` with no argument is FAIL `bad verb/target` (t.exec reads a nil
first argument as a missing target) -- pass `true`.

## Journal branches shadowed by a worn item; `expect_complete()` is all four rows or none; `[scroll already photographed:`

*Origin: section 8 ("Gaps reported by authors").*

A journal branch that reads `inv_total(inv, <wearable>)` with no stage bound shadows every later
branch once the item is WORN, and `quest.journal` never sees the completion banner
(`elid_journal.rs2:28`): report it as a content bug, unequip after the last check that needs it
worn. `quest.expect_complete()` is all four of its rows or none: it writes `quest.journal`
UNCONDITIONALLY (`quest.lua`), so on a quest whose journal channel cannot answer, a bare
`expect_complete()` followed by a hand-rolled retry still ships a committed FAIL row, and no later
PASS can undo it.

Either the journal answers, or you hand-roll
`quest.varp_complete`/`quest.scroll_title`/`quest.points` plus the reward rows with `t.check` (the
recipe above; `rovingelves.lua`, `mourningsendpartii.lua`) -- never both. Hand-rolling
`quest.scroll_title` means reproducing one LITERAL string: `gate.py`'s completion-scroll rule
(`gate.py`:512) accepts a shotless row only when its detail contains
`[scroll already photographed:`, and `t.check`'s own `[frame unchanged]` marker does not satisfy it.

Take the picture yourself with `t.shot("quest.scroll")` and, when it answers `unchanged`, append
`" [scroll already photographed: " .. detail .. "]"` to the row's detail exactly the way `quest.lua`
builds it.

## `t.ui.journal_open` can degrade partway through a run; quests with no journal proc

*Origin: section 8 ("Gaps reported by authors").*

`t.ui.journal_open(display)`can degrade PARTWAY THROUGH a run, not only after completion: on
Mourning's End Part I it PASSed three stage rows (`not_started`, `spoken_islwyn`, `spoken_eluned`)
and then timed out at 20 ticks on every later one, after the character had been underground
(Glarial's Tomb, `z+6400`) and back. Early PASSes are not proof the channel holds -- move those
stage rows to `t.quest.expect_stage`, the server-content channel `quest.varp_complete` already
proves live.

The harder shape is a quest with no `~<abbr>_journal` proc AT ALL, where `quest.journal` can never
pass rather than degrading partway: `atailoftwocats` is one (grep `journal` in `twocats.rs2` is
empty), alongside the transmit-starved `makinghistory` and `mourningsendpartii`. Grep for the proc
before planning on `expect_complete()`, and hand-roll the other three rows when it is absent.

## `t.step`'s second argument is the VERDICT WORD

*Origin: section 8 ("Gaps reported by authors").*

`t.step`'s SECOND ARGUMENT IS THE VERDICT WORD (`"PASS"`/`"FAIL"`/`"BLOCKED"`), written as
`cond and "PASS" or "FAIL"`. `t.check` and `t.expect` are the ones that take the condition or the
verb's own result, and writing a boolean into `t.step` used to end the WHOLE RUN --
`api_drive.ledger` reads that column with `luaL_checkstring`, there is no `pcall`, and Between a
Rock threw away 93 PASS rows at its wall of flame for one mistyped argument.

`core.lua` now reads it as the PASS/FAIL it plainly meant and puts `[bad ledger argument] ...` in
the row's detail (a result word or a nil is graded FAIL, a non-string step name is coerced and
named), and `lint_quest.py` REFUSES the shape -- so the failure today is a refused file, not a dead
run. Do not rely on the floor: the linter is the rule.

## A `|` in a page's text: a manual line break that the client draws as a glyph

FIXED seam29: `~chat_layout` (chat.rs2) turns every `|` into a real row break and pages text longer
than the box; the page text now carries `<br>` where the `|` was, and the stripped text chat.play
matches has NO separator there (`'here,but'`), so an entry must stop inside one row. See
verbs-chat: A long line is more than one page. The rest of this section is the pre-seam29 behaviour.

*Origin: sample sonnet-b34 (2026-09-29): elena, grandtree, hazeelcult.*

Ported LostCity content writes a manual line break as `|` inside `~chatnpc`/`~chatplayer`/`~mesbox`
text (`"You fall through...|...you land in the sewer."`). The client does not break the line
there. It draws the `|` inline as a small glyph between the two halves, so shots of the page show
`here,|but` run together (Plague City shots 88, 90, 93, 207, 251 and 255, and most of The Grand
Tree's). Treat that as a known rendering gap and do not send a quest back for it.

For matching, `t.chat.play` compares each entry against the page text by plain substring
(`chat.lua`, `stripped:find(arg, 1, true)`), and the `|` is still in that text. So an entry
that spans the break must spell the `|` (`"npc:Yes she was staying here,|but"`), and one that stops
before it (`"npc:Yes she was staying here"`) matches as usual. The ledger detail prints the `|`.
Copy the entry from there.

## A gap where an em dash should be: the dialogue font has no glyph for U+2014

*Origin: sample matthew-mbp-m4-b53 round 2 (b); fixed for The Queen of Thieves in seam pass
matthew-mbp-m4-b53-seam3.*

A real em dash (U+2014) inside `~chatnpc`/`~chatplayer`/`~mesbox` text draws as a blank gap: Devan's
line read "see you now    tent at the end of the tunnels" (queenofthieves shot 048). `chat.play`
fragments that stop short of the dash still match, so only a shot shows it. Content writes `' - '`
for a dash, as LostCity dialogue does (LostCity_Content2 `quest_hero/scripts/npcs/grip.rs2:40`,
`quest_grail/scripts/sir_percival.rs2:42`). OPEN: about 174 other port-authored chat lines in
OSRS-Content still carry U+2014 (for example `quest_ethicallyacquiredantiquities`); a test whose
`chat.play` entry spans one copies the dash byte for byte until the content is swept.
