# Quest authoring

How to write `test/quests/<quest>.lua` -- the ONLY file you write, full stop (section 2; trap 7).
`test/quests/README.md` only points here; the live verb set with file:line is
`tools/quest_gate/verb_list.py`. Read this file for the *shape*, that one when a line here goes
stale.

This page is the CORE: everything you need before your first run, about 25 KB. The depth -- every
verb's full banner, the 33 traps, the definition of done, and the section 8 facts authors and seam
passes recorded -- lives in `docs/quest_authoring/`, reached through `docs/quest_authoring/INDEX.md`.
Section and trap numbers are the ones tests and commits already cite: sections 1-8 and traps 1-33
keep their numbers in the topic files.

## How to work: run early, look things up when a row fails

1. Claim the row, read `last_failure`, scaffold (or resume the committed file), resolve every
   `-- CHECK` marker against the quest's own `.rs2` (trap 17).
2. **Make the first run early.** Do not read the topic files first. Run, then run `gate.py`, then
   read the ledger and the failure block.
3. When a row fails, look up THAT failure: copy the distinctive words of the ledger detail, the
   server sentence or the result word into
   `grep -rn "<symptom text>" docs/quest_authoring/`, or find the symptom in
   `docs/quest_authoring/INDEX.md`. Read only the heading it names. Fix one thing, run again.
4. Read a whole topic file only when you are about to use a whole area for the first time (a sea
   leg: `verbs-sail-session.md`; a fight: `verbs-combat.md`; a shop: `verbs-inventory-shops.md`).

Reading everything up front is what ran four of seven authors out of context with zero runs. The
index is keyed by what you SEE, so a failure is one lookup away.

## The contract

- **The guide is the spec** (trap 32). The Quest Helper guide's step ladder is what the test drives,
  end to end. `python3 tools/quest_gate/helper_coverage.py <id>` must read FULL, or every gap must be
  declared with `.rs2` evidence (`-- GUIDE-GAP: <guideStepVar> <reason citing file.rs2:line>`).
- **Every guide step is driven by a real row**: a click, a talk, a real fight, a real use -- named
  after the guide step's variable (`talkToUnferth`, `drunkenAli-beer1`). A leg the port only narrates
  in a `mes()` is a CONTENT gap, never a PASS.
- **No cheats past the guide's own work** (trap 16). An item, kill, craft, search or fetch the quest's
  own `.rs2` makes you do is driven through clicks. `goto_tile` is a `::goto` teleport: plain travel
  only (a plain ladder or stair is travel, section 2; one whose trigger reads or writes a quest var
  is not), never past a door, gate, puzzle or loc the guide names. A step the guide does with a
  spell or tool is done with that spell or tool.
- **Setup cheats are for prerequisites only.** `::give`/`::setlevel`/`::setvar`/`::complete <other
  quest>` stage what Quest Helper lists as brought along or required. Never `::complete` your own
  quest (trap 8); `::complete` takes the DBROW name.
- **Var names carry their kind and id** in the bind, every `t.var.*` call and every `::setvar`
  (`varp29_cookquest`, `varb3185_anma_main`, `::setvar varp101_qp 43`), never the old bare name:
  `OSRS-Content/docs/VAR_NAMES.md` maps old to new, lint refuses a bare one (verbs-state-and-vars).
- **The boss is fought for real.** `boss_fight=yes` in `quest_inventory.tsv` is a guess: a boss with a
  plain op2 Attack and a stat block is fought (section 6). Arm the character in `setup`, carry food
  and EAT it (`opts.eat` on the kill wait), WEAR the weapon.
- **Outcomes** (queue statuses `todo green blocked content_bug`; the batch report adds `gave_up`):
  - `green` -- `gate.py <quest>` says green AND `lint_quest.py` (without `--allow-check`) is clean.
  - `blocked` -- a DRIVER seam: a verb cannot land a click on something that is really there. The
    file ends in a `t.blocked("<exact seam>")` row, then `return`. Anything else is rejected, not
    blocked.
  - `content_bug` -- the quest's own script misbehaves, or a symbol resolves but has zero live
    placements. The file ends in `t.blocked("content_bug: <finding>")` and a `return`, exactly like a
    blocked one; name the file:line.
  - `gave_up` -- the batch report word for stopping without a verdict (runs spent, or the context
    was compacted); the next author resumes your file and notebook.

## The file shape (section 1)

```lua
return {
    id = "cooks_assistant",
    fixture = "fresh_lumbridge.ini",
    -- Setup STAGES a quest, never finishes one (trap 8): `::cook` resets
    -- the varp, clears ingredients, puts the player beside the Cook.
    setup = { "::cook" },

    run = function(t)
        -- bind touches the world not at all: it records varp/constants/
        -- display/points for later, and reads %qp now for that delta.
        t.quest.bind({
            varp = "varp29_cookquest",
            constants = { not_started = 0, started = 1, complete = 2 },
            display = "Cook's Assistant",
            points = 1,
        })
        t.ticks(3)  -- a setup cheat's effect is not client-side yet
        t.expect("cook.reset", t.quest.expect_stage("not_started"))

        -- t.exec(name, verb, ...) writes row `name` from (result, detail)
        -- and SHOOTS it; t.expect does not -- photograph clicks, not reads.
        t.exec("cook.greet", t.player.talk_to, "cook")

        -- chat.play answers ("ok", "<N> page(s): ..."), a real detail, so
        -- it goes through t.exec like any other verb.
        t.exec("cook.accept", t.chat.play, {
            "npc:What am I to do",
            "choose:What's wrong?",
            "player:What's wrong",
            "npc:terrible mess",
            "npc:forgotten to buy",
            "choose:Yes, I'll help you.",
        })
        t.expect("cook.started", t.quest.expect_stage("started"))

        -- ... gather, talk to the Cook a SECOND time, dismiss the mesbox ...
        t.quest.expect_complete()   -- four rows, and it closes the scroll
        t.finish(0)
    end,
}
```

- `setup` runs before `run()`; every `::give` is counted into the backpack before the first row, and
  one that gives nothing is a FAIL row (trap 23). Start it with `::clearinv` (section 2).
- `t.quest.bind{varp=, constants={...}, display=, journal_title=, points=}` -- `constants.complete`
  is required; `varp=` may name a varp or a pure varbit. Name stage rows `quest.stage.<constant>`
  (trap 14).
- `t.quest.expect_complete()` writes `quest.varp_complete`, `quest.scroll_title` (with the
  `quest.scroll` photograph the gate requires), `quest.points`, `quest.journal`. Never `::complete`.
- `t.blocked(reason)` writes `BLOCKED`, shoots and ENDS the run; `return` right after it.
  `t.finish(code)` writes SUMMARY and ends the run too.
- An excerpt, not a whole quest (every line above ran and PASSed, but a file this short fails
  section 7's minimum shape: a green file needs 8 rows / 4 PNGs, a `BLOCKED` one 4 / 2). In a real
  file name each row after the guide step it drives (`talkToUnferth`, section 7), name stage rows
  `quest.stage.<constant>` (trap 14), put `t.exec("goto-...", t.player.goto_tile, x, z, level)` before
  every far step, and take `skill.snapshot()` before the hand-in for the reward rows. Read the
  complete green files next: `test/quests/cooks_assistant.lua`, then `hans.lua` (no quest varp).

## The commands (section 6)

```sh
python3 tools/quest_gate/queue.py next --tier 1 --claim <you>   # claims a row
python3 tools/quest_gate/queue.py show <test_id>                # quest_dir, helper, last_failure
python3 tools/quest_gate/new_quest.py <test_id>                 # scaffold (refuses to clobber)
python3 tools/quest_gate/lint_quest.py --allow-check test/quests/<test_id>.lua
python3 tools/quest_gate/run.py <quest> --no-build              # always --no-build for Lua edits
python3 tools/quest_gate/gate.py <quest>                        # the verdict, not run.py's exit
python3 tools/quest_gate/helper_coverage.py <quest>             # guide step ladder vs your rows
python3 tools/quest_gate/ladder.py <quest> --leg K               # the guide as a table, leg K (relay.md)
python3 tools/quest_gate/fail.py <quest>                        # first failing row + neighbours, <=3 KB
TORIRSSERVER_VERBOSE=1 python3 tools/quest_gate/run.py <quest> --no-build   # a stalled script
python3 tools/quest_gate/run.py <quest> --no-build --detach; python3 tools/quest_gate/run.py --wait <quest>  # a run over ~8 min: relay.md
```

Artefacts: `build/quest_gate/<quest>/` (`ledger.tsv`, `shots/`, `client.log`), replaced every run. A
non-green run prints a `---- failures ----` block -- start there, then read the WHOLE ledger (a FAIL
does not stop the run). A client that stops ticking is killed after about 90 s with `run.unfinished`
`run stalled: no client tick for N s` (relay.md, "Runs longer than the shell cap"). Resuming: if the file exists and the row is `todo` with a `last_failure`,
continue from it; never regenerate over it. Detail: `docs/quest_authoring/running.md`.

## The verb table

`(result, detail)` unless noted. Results are the fixed set `ok timeout not_found refused covered
no_row not_visible closed unsupported` (plus `chat.play`'s `mismatch` and the `hollow` FAIL). Full
banners: the topic file named in each group heading.

### Root of `t` -- `verbs-root-and-quest.md`

- `t.exec(name, verb, ...)` -> the verb's own pair; FAIL `hollow` on `ok` with nil detail, `bad verb/target` on a nil first arg. Shoots.
- `t.check(name, cond_or_result, detail)` -> PASS iff `true`/`"ok"`; shoots. Write the reading into `detail`.
- `t.expect(name, result, detail)` -> PASS iff `ok`; no shot. Not an escape from the hollow rule (trap 12).
- `t.step(name, "PASS"|"FAIL"|"BLOCKED", detail)` -- the second argument is the VERDICT WORD.
- `t.blocked(reason)` / `t.finish(code)` -- terminal; `return` right after.
- `t.cheat(text, wait_for_reply=true)` -> `ok refused no_row`. Hollow: call directly, record with `t.check`.
- `t.ticks(n)` / `t.settle()` -> `ok timeout`. SERVER ticks (trap 1); hollow.
- `t.await({level=fn, note=text}, ticks)` -> `ok timeout`; give it a `note`, the `ok` detail is `<note>: met after N tick(s)`.
- `t.shot(name)` -> `ok refused timeout`; only for a bare narrative shot. `t.note(text)` folds into the next row.
- `t.key(name)` / `t.text(str)` -> `ok`/error. `t.key("escape")` closes a modal.
- `t.render.skip(on)` -> `ok refused`; `t.render.frame()` -> `ok timeout`. Render skip is ON in every run and changes no frame of it (shots, clicks and pick reads draw what they need); `--render-every-frame` is the A/B (running: Render skip).

### `quest`, `scroll`, `levelup` -- `verbs-root-and-quest.md`

- `t.quest.bind{...}` -> `ok refused`. `t.quest.stage()` -> `(result, value)`, varp-or-varbit.
- `t.quest.expect_stage(name_or_value)` -> `ok refused`. `t.quest.expect_complete()` -> `ok refused`, four rows.
- `t.scroll.title()` / `rewards()` / `reward_xp(skill)` / `close()` -> read the reward scroll; `not_visible` when none is up.
- `t.levelup.skill()` / `continue_()` -> `unsupported` always in this pack.

### `chat` -- `verbs-chat.md`

- `t.chat.play({entries})` -> `ok mismatch unsupported timeout`. Entries: `npc:` `player:` `mesbox:` `options` `choose:<row|/pattern/>` `count:` `name:` `end` `*`. No `objbox:` entry.
- `t.chat.continue_()` -> `ok unsupported refused not_visible closed timeout`; under `t.exec` pass `true` as filler.
- `t.chat.choose(selector)` -> `ok no_row refused`. `t.chat.drain{stop_at=, max_pages=}` -> `(ok, kind) timeout`.
- `t.chat.kind()` -> bare string (`npc player mesbox objbox options count name other_input none`).
- `t.chat.text()` / `head()` / `name()` / `item()`; `t.chat.expect_text(s)` / `expect_head` / `expect_item` -> `ok not_found`.
- `t.chat.options()` / `options_title()`; `t.chat.count(n)` / `name_entry(text)`; `t.chat.close()` (does not resume a script).
- `t.game.runedraw(policy)` -> Robin's rune-draw game (Ghosts Ahoy), one game per call.

### `var`, `inv`, `msg`, `skill` -- `verbs-state-and-vars.md`

- `t.var.varp(name)` / `varbit(name)` / `server(name)` -> `(ok, value[, source])`.
- `t.var.await(name, value, ticks)` / `await_server(...)` -> `ok timeout`; `t.var.expect(name, value)` -> `ok refused`.
- `t.inv.count(name)` -> `(ok, total)`; `t.inv.has` / `t.inv.slot(i)`; `t.inv.expect_has(name, n)` / `expect_absent(name)`.
- `t.inv.await(name, n, ticks)` / `await_all({name=n}, ticks)` -> `ok timeout`. Use these after a click, not a bare count (trap 24).
- `t.msg.last(n)`; `t.msg.expect(s)` -> a line already in the ring; `t.msg.await(s, ticks)` -> only lines NEWER than the call.
- `t.skill.read(name)`, `t.skill.snapshot()`, `t.skill.expect_gain(name, xp, snapshot)` -> `ok refused no_row`.
- `t.clock.skip(minutes)` -> `ok refused no_row timeout`; the fast-forward for a wait of REAL minutes (`date_minutes`: a crop, a brew); await the quest's own effect next (gaps-world).

### `ui`, `npc` lookups -- `verbs-ui-and-npc.md`

- `t.ui.open(interface, cheat)`; `t.ui.await_open(interface, ticks)` -> `ok timeout no_row`; `await_close` answers a bare `ok` (hollow).
- `t.ui.widget(sym, sub)` -> `(ok, id)`; `t.ui.invoke(widget, op)` (hollow; `op=0` for an IF1 button, trap 33); `t.ui.tab(name)`; `t.ui.is_modal()`.
- `t.ui.model_pose(sym, sub)` / `await_model_pose(...)` -> a type-6 model component's angles.
- `t.ui.text(component|list, sub)` -> `(ok, text)` `not_found` `not_visible`; `t.ui.expect_text(component, want, ticks=5)` asserts a page's text (seam29).
- `t.ui.journal_open(display)` / `journal_read()` / `journal_close()` -> the quest journal.
- `t.npc.by_name` / `by_symbol` / `nearest(sym, r)` -> `(ok, row) not_found no_row`; `t.npc.tiles(sym, r)` -> every copy, three returns.
- `t.npc.await_present(sym, r, ticks)` / `await_gone(...)` -> `ok timeout`, detail on `ok`.

### Combat -- `verbs-combat.md`

- `t.player.attack(npc, op=2, ticks, opts)` -> `ok timeout refused`. `refused` can be single-way combat or no route.
- `t.npc.await_dead_engaged(ticks, attempts, opts)` -> `ok timeout no_row refused`; the kill wait for every hunt. `opts.eat = {item=, below=}` eats.
- `t.npc.await_dead(npc, ticks, radius, attempts, opts)` -> `ok timeout not_found`; a slot leaving the pool, corroborated.
- `t.player.cast(spell, target, ticks, ...)` -> `ok refused no_runes timeout no_row not_visible unsupported`; npc, obj/loc, held item or no target.
- `t.player.alive()` -> `ok refused`; takes no argument (use `t.expect`). A death writes `player.died` and ENDS the run.

### Held items and shops -- `verbs-inventory-shops.md`

- `t.player.use_on(item, target, opts)` -> `ok unsupported refused covered`; `target` is a `{kind,id}` TABLE.
- `t.player.use_item_on_item(a, b)` -> tries `[opheldu,b]` (the clicked item) first, then `[opheldu,a]`; grep both orders.
- `t.player.inv_op(item, op)` -> `ok`/error; a numbered held op (`refused` = never sent, `timeout` = sent, no answer).
- `t.player.equip(item)` / `unequip(item)` / `drop(item)` / `emote(name)`.
- `t.shop.open(npc, op=3, shop_inv)`, `t.shop.attach(shop_inv)`, `t.shop.buy(item, n)`, `t.shop.close()`.

### Travel and clicks -- `verbs-pointer.md`

- `t.player.goto_tile(x, z, level=0)` -> `(ok, "x,z,level") timeout no_row`; the `::goto` travel cheat, re-issues up to 3 times.
- `t.player.teleport(name)` -> a named destination in `tele_destinations.rs2`.
- `t.player.walk_to(x, z, ticks)` (hollow on success) / `walk_near(target, ticks)` / `idle()`.
- `t.player.talk_to(npc, op=1, opts)` -> waits up to 5 ticks for its page; `opts` `{slot=n}` / `{at={x,z}}`.
- `t.player.press(npc, op=1, ticks, opts)` -> a silent npc op settled on the npc MOVING; four outcomes.
- `t.player.click_loc(loc_symbol_string, op=1, opts)` -> walks other approach tiles on `I can't reach that!`; `opts.at` names a copy.
- `t.player.click_obj(obj, op=3)` -> waits for the backpack count to rise; write the count yourself.
- `t.player.by_symbol(kind, name)` -> `(target, "ok")` -- reversed order; resolves only, never a presence check.
- `t.world.tile()` / `level()`; `t.world.loc_near(sym, r)` / `obj_near(sym, r)` -> `(ok, {...}) not_found`.
- `t.drive.screen_position(target)`, `t.drive.click_minimenu(target, option)`, `t.drive.camera(yaw, pitch, zoom)`.
- `t.drive.op(target, option)` -> the logged bypass, never the default and never evidence of reach.

### Cutscenes -- `verbs-cutscene.md`

- `t.cutscene.await(name, {expect=, timeout=100, quiet=30, shots=})` -> `ok no_cutscene unfinished not_found`; the row right after the one that starts it, named `<step>.cutscene`. Detail `cutscene: <n> keyframes, ... reset=yes -- #1 t=.. moveto x,z ...`; `expect` entries (`{op="moveto", coord="0_38_154_44_13", height=2000}`, copied from the `.rs2`) must appear in order. A quest whose content calls `cam_moveto`/`cam_lookat` is RED under `gate.py` (`cutscene_row_required`) until its rows cover every site.
- `t.world.camera()` -> a bare table `{x, z, level, yaw, pitch, zoom, server_driven, serial, last_op, last_target}`; `t.cutscene.mark()` -> the serial, for `opts.since`.
- `t.cutscene.exempt(site, reason)` -> `ok refused`, its own row, called directly: a site OFF the guide's route only, `reason` naming the guide step driven instead; `gate.py` accepts or refuses it (seam34).

### Sea and session -- `verbs-sail-session.md`

- `t.sail.state()`, `board`, `helm`, `sails`, `sail_to(x, z)`, `await_arrival`, `disembark`, the port-task verbs.
- `t.session.logout()` / `login()` / `relog()` / `screen()`; no target, so `t.check(...)`. A relog re-boots the embedded server.

## The rules (traps 1-33, section 5)

One or two sentences each; the trap files (`traps-01-12.md`, `traps-13-22.md`, `traps-23-33.md`)
have the evidence and the exceptions.

1. Every tick count is SERVER ticks; `t.ticks(10)` is 200ms for the world to answer one thing.
2. Targets are content symbols from `OSRS-Content/osrs239-content/configs/all.*.compack`, never ids or display names.
3. `choose:` takes a 1-based index, the EXACT row text, or a `/lua pattern/`; a wrong spelling is `no_row`.
4. Two byte-identical shots in `shots/` fail the gate; a repeated page chain is clicked through with `t.ticks(2)` + `continue_`.
5. No `pcall`: a guard that raises ends the run; `t.exec` stringifies a table detail.
6. A fixture's `[varps]` may only carry `scope=perm` vars.
7. Edit only your quest file: never `script/plugins/`, `src/`, `tools/`, `OSRS-Content/` or other `test/` files.
8. Never `::complete` your own quest.
9. Pass `--no-build` for Lua-only iteration.
10. The evidence is the `ledger.tsv` row and the failure block, not `client.log`.
11. Never `shots=false` on `chat.drain`; its shot is a frame pump.
12. A verb that answers `ok` with no detail cannot go through `t.exec`, and `gate.py` fails EVERY PASS row with an empty detail: read the value back and write it.
13. Find an npc's tile from its `*.spawn` row under `OSRS-Content/osrs239-content/server/scripts/` (grep the whole tree).
14. Name stage rows `quest.stage.<constant>`; the gate matches the literal `quest.` prefix.
15. Never pad the shot count; a shot follows a click that changed something.
16. Never cheat the quest's own work; `::give`/`::kill`/`::setvar` are for setup and brought-along prerequisites.
17. Resolve every `-- CHECK` before the first run; rebuild guarded `chat.play` lists from the `.rs2` branch the stage reaches.
18. A branch almost always opens with the PLAYER's line; spell pages in the order the `.rs2` opens them.
19. A spawn row carries a multinpc's BASE symbol; the fix is content in the base symbol's own trigger, reported as a content seam.
20. A loc resolves `exact`/`base`/`multiloc`; doors are `next_loc_stage` pairs; a symbol can be placed twice.
21. `covered` means the pixel is not on the target's triangles, not that something is in front; the row names the hunt.
22. A `~mesbox`/`~chatnpc*` page SUSPENDS its script: dismiss it before asserting what follows it.
23. A setup `::give` is in the backpack before `run()`'s first row; one that gives nothing is a FAIL row.
24. A click verb's `ok` is the server's sentence, not the container update: `t.inv.await`/`t.var.await_server` or `t.ticks(1)` next.
25. A dialogue's effects are readable a tick later; `chat.play` waits it out, `chat.drain` does not.
26. `talk_to` waits up to 5 ticks for its page; a refused press can still read `ok` and fail one row later.
27. A `mes()` string over 252 characters desyncs the session; over 199 renders cut.
28. A `multinpc<N>` table is indexed by VALUE (`multinpc1` is value 0); a `-1` rung is hidden.
29. A `loc_near` sweep is not evidence a loc is unplaced; grep `maps/*.jl2` (underground is `z + 6400`).
30. A `choose:` whose row opens a screen ends that `chat.play` list; bind a shop with `t.shop.attach`.
31. An `[opnpc2,<npc>]` binding without `@player_combat_start` makes the npc unhittable (`hp no bar -> no bar`).
32. The guide is the spec: `helper_coverage.py` FULL, or declared gaps; GUIDE-GAP and the verified markers.
33. An IF1 graphic button takes `t.ui.invoke(widget, 0)`; a `buttontype=3` close icon needs `t.key("escape")`.

## How to look something up

- `docs/quest_authoring/INDEX.md` -- one line per symptom (the exact ledger message, server sentence
  or shape you see), grouped by area, naming the file and heading that explains it.
- `grep -rn "<symptom text>" docs/quest_authoring/` -- paste the distinctive words of the detail
  (`reach_failed`, `stale reopen`, `hp no bar`, `I'm already under attack.`).
- A citation in a test comment or commit ("trap 31", "section 8's payout-reopen", "seam pass 21 (d)",
  "sonnet-b31 (c)") is resolved by the index's "Citations" group.
- Topic files: `start-and-travel.md` (sections 1-2), `verbs-*.md` (section 3), `traps-*.md`
  (section 5), `running.md` (section 6), `coverage-and-gate.md` (section 7), and section 8 in
  `gaps-dialogue.md`, `gaps-world.md`, `gaps-combat.md`,
  `seam-facts.md`, `sampler-findings.md`, `content-gaps.md`.

**Keeping this page small:** the core stays under 25 KB; a new fact goes into the matching topic file
under `docs/quest_authoring/` with one line added to `INDEX.md`.
