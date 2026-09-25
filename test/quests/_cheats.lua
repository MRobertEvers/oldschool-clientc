-- The cheat ladder, reached through `t.cheat`. Not a quest -- the leading
-- underscore keeps it out of `run.py --all` (see test/quests/README.md).
--
-- WHY THIS FILE EXISTS
--
-- `t.cheat` used to call `ToriRSServer_RunDebugprocForTest`, which is only the
-- first half of what a cheat is: it looks the line up as a content
-- `[debugproc,<name>]` and stops there. Every command in the engine's own
-- strncmp ladder -- `::give`, `::setlevel`, `::spawn`, `::wield`, `::tele <x>
-- <z>` -- therefore answered `no_row` to a quest test and did nothing at all
-- (docs/QUEST_SERVER_CHEATS.md B, which measured it). A quest whose
-- `setup = { "::give bucket_of_milk" }` silently stated nothing went on to
-- fail four steps later at a dialogue option that never appeared, and the
-- ledger blamed the dialogue.
--
-- `ToriRSServer_RunCheatForTest` is now the whole dispatch -- content first,
-- then the ladder -- and this file is what says so out of a live client run
-- rather than out of a compiler. One row per command the quest suite is about
-- to depend on:
--
--     ::give      an item reaches the BACKPACK, not just the server struct
--     ::clearinv  the backpack is empty AT THE CLIENT afterwards -- every
--                 slot, not only the one the row above filled
--     ::setlevel  a stat level lands, and is still there ten ticks later
--     ::setvar    a named varp takes a ^constant, seen by the CLIENT's mirror
--     ::spawn     an npc appears in the client's own npc pool
--     ::kill      it dies through the ordinary death path and leaves the pool
--     ::passive   an aggressive npc TYPE stops starting fights, the claim it
--                 held is dropped, and the player may attack something else
--     ::tele      a content debugproc still works (the regression anchor: this
--                 one was always reachable, so it failing means the split
--                 broke dispatch rather than that a branch is missing)
--     ::nosuchcheat  answers `no_row`, because NOTHING understood it
--
-- HOW TO RUN IT
--
--     make -C src test-quest-cheats
--   or, against a private binary a C change was built into:
--     QUEST_BINARY=src/torirs_qd_<you> python3 tools/quest_gate/run.py \
--         --script test/quests/_cheats.lua --name cheats --no-build
--
-- WHAT IS DELIBERATELY NOT HERE
--
-- Screenshots at every step. Two shots of the same static Lumbridge frame have
-- the same MD5, and gate.py -- correctly -- calls that a driver that never
-- drove anything. This file takes exactly two, at the only two moments the
-- screen is guaranteed to differ: before anything, and after a teleport to
-- another city.
--
-- READING A READ BACK
--
-- Every read below is made through the CLIENT, and a cheat dispatched against
-- the embedded server reaches the client only after the packet round-trips
-- through a frame or two. So each cheat is followed by `t.ticks(n)` or a
-- bounded `t.await`, never by an immediate read -- the same reason
-- cooks_assistant.lua awaits its varp instead of sleeping a fixed count.

-- How many ticks a spawned npc's death takes to leave the client's pool.
-- The engine's own ledger (torirs_server_combat.c, npc_death_step's banner):
-- D hitpoints reach 0, D+1 QUEUED, D+1+a ARRIVE (a is 0..2), then death_delay
-- -- two ticks for an ordinary npc -- before `npc_del`. That is 4 to 6 server
-- ticks before the npc is gone at all, plus the NPC_INFO round trip that
-- removes it from the client's pool. Twelve is that with room, and still far
-- under anything that would pass while the npc stood there alive.
local KILL_DEADLINE_TICKS = 12

-- `::spawn` places its npc at the player's own tile + 1, so a radius of 2
-- names the thing this file just made and nothing else. Lumbridge has its own
-- `man` roster walking the streets outside; a wide radius here would let one
-- of those answer "present" for a spawn that never happened, and stop
-- `::kill` from ever reading as "gone".
local SPAWN_RADIUS = 2

return {
    id = "_cheats",
    fixture = "fresh_lumbridge.ini",
    setup = {},

    run = function(t)
        local failures = 0

        -- Record a row and keep score. `t.step` writes the ledger row; this
        -- only adds the exit code, so a red run exits non-zero and
        -- `make test-quest-cheats` is red with it.
        local function record(name, passed, detail)
            t.step(name, passed and "PASS" or "FAIL", detail)
            if not passed then
                failures = failures + 1
            end
        end

        -- The login banner (music unlock, welcome lines, the "::style" hint)
        -- keeps arriving for a few ticks after login, and a cheat dispatched
        -- into the middle of it is answered correctly but read back against a
        -- client that is still catching up. Settle first, once.
        t.settle()
        t.ticks(3)
        t.shot("cheats-start")

        -- ------------------------------------------------ nothing understood it
        -- First, because it touches nothing: `::nosuchcheat` is neither a
        -- `[debugproc]` nor a ladder branch, so the dispatch falls all the way
        -- through and must answer NONE -> `no_row`. If this ever answers `ok`
        -- the ladder has grown a branch that matches on shape (the three bare
        -- `sscanf` fallbacks at its end are exactly that hazard).
        local unknown_result, unknown_detail = t.cheat("::nosuchcheat")
        record("cheats.unknown", unknown_result == "no_row",
            "::nosuchcheat -> " .. tostring(unknown_result)
                .. " (" .. tostring(unknown_detail) .. "), want no_row")

        -- ---------------------------------------------------------- ::give
        -- The delta, not the absolute count: a fixture is allowed to carry
        -- eggs, and this row is about what the CHEAT did.
        local before_result, before_count = t.inv.count("egg")
        local give_result = t.cheat("::give egg")
        t.ticks(3)
        local after_result, after_count = t.inv.count("egg")
        local gained = nil
        if before_result == "ok" and after_result == "ok" then
            gained = after_count - before_count
        end
        record("cheats.give",
            give_result == "ok" and gained == 1,
            "::give egg -> " .. tostring(give_result)
                .. "; backpack egg " .. tostring(before_count) .. " -> "
                .. tostring(after_count) .. " (delta " .. tostring(gained) .. ", want 1)"
                .. " reads=" .. tostring(before_result) .. "/" .. tostring(after_result))

        -- ------------------------------------------------------ ::clearinv
        -- Straight after ::give, so the backpack certainly holds something
        -- this file put there AND the fourteen the fixture's tutorial
        -- character boots with -- the pile a quest needing twenty free slots
        -- (Sheep Shearer's twenty balls of wool) has to get rid of.
        --
        -- Two halves, because either alone would pass while the cheat was
        -- half-broken: the egg is gone (the one item this file KNOWS was
        -- there), and every slot of the backpack is empty (a clear that only
        -- reached the slot ::give wrote would pass the first half).
        --
        -- The slot scan is 0..27, not 1..28: DriveState_InvSlot indexes from
        -- zero (torirs_plugin_drive_state.c:186 answers not_found for a slot
        -- outside 0..slot_count-1), so a 1-based sweep would skip the first
        -- cell -- the one an empty backpack's next ::give lands in. Slot 28
        -- is read too, and must answer `not_found`: that is the capacity edge
        -- saying the scan covered the whole container rather than stopping
        -- short of it.
        local clear_result = t.cheat("::clearinv")
        t.ticks(3)
        local egg_result, egg_count = t.inv.count("egg")
        local occupied = ""
        local slot_reads_ok = true
        for slot = 0, 27 do
            local slot_result, cell = t.inv.slot(slot)
            if slot_result == "ok" then
                if cell.name ~= "" or cell.count ~= 0 then
                    occupied = occupied .. slot .. "=" .. tostring(cell.name)
                        .. "x" .. tostring(cell.count) .. " "
                end
            elseif slot_result ~= "not_found" then
                slot_reads_ok = false
                occupied = occupied .. slot .. "=<" .. tostring(slot_result) .. "> "
            end
        end
        local past_end = t.inv.slot(28)
        -- The ladder's own sentence, for the detail: "Cleared N item(s)."
        -- names how many cells it emptied, and a reader comparing that count
        -- to the scan above can see the two agree.
        local said = "?"
        local lines_result, lines = t.msg.last(1)
        if lines_result == "ok" and type(lines) == "table" and lines[1] then
            said = tostring(lines[1].text)
        end
        record("cheats.clearinv",
            clear_result == "ok" and egg_result == "ok" and egg_count == 0
                and occupied == "" and slot_reads_ok and past_end == "not_found",
            "::clearinv -> " .. tostring(clear_result)
                .. "; egg " .. tostring(egg_result) .. "=" .. tostring(egg_count)
                .. " (want 0); slots 0..27 still holding: "
                .. (occupied == "" and "<none>" or occupied)
                .. "; slot 28 -> " .. tostring(past_end) .. " (want not_found)"
                .. "; server said '" .. said .. "'")

        -- ------------------------------------------------------ ::setlevel
        -- 40 rather than a small number so it cannot be mistaken for a level
        -- the fixture already carries, and cooking because that is the stat
        -- the first real quest in this suite sets.
        local setlevel_result = t.cheat("::setlevel cooking 40")
        t.ticks(3)
        local cook_result, cooking = t.skill.read("cooking")
        record("cheats.setlevel",
            setlevel_result == "ok" and cook_result == "ok" and cooking.level == 40,
            "::setlevel cooking 40 -> " .. tostring(setlevel_result)
                .. "; skill read " .. tostring(cook_result)
                .. " level=" .. tostring(cook_result == "ok" and cooking.level or cooking)
                .. " base=" .. tostring(cook_result == "ok" and cooking.base_level or "-")
                .. " xp=" .. tostring(cook_result == "ok" and cooking.experience or "-"))

        -- `::setlevel` writes the XP threshold for the level, not a temporary
        -- boost, so the level must still be 40 after the world has ticked on.
        -- A boost would have started draining by now; this is the difference
        -- between a stat a quest can rely on and one that expires mid-test.
        t.ticks(10)
        local kept_result, kept = t.skill.read("cooking")
        record("cheats.setlevel_permanent",
            kept_result == "ok" and kept.level == 40 and kept.base_level == 40,
            "ten ticks later: " .. tostring(kept_result)
                .. " level=" .. tostring(kept_result == "ok" and kept.level or kept)
                .. " base=" .. tostring(kept_result == "ok" and kept.base_level or "-")
                .. " (both want 40)")

        -- --------------------------------------------------------- ::setvar
        -- `^cook_started`, never `1`: the stage numbering belongs to
        -- quest_cook.constant, and a test that spells the number is pinning
        -- today's value of somebody else's variable.
        local setvar_result = t.cheat("::setvar cookquest ^cook_started")
        t.ticks(3)
        local server_result, server_value = t.var.server("cookquest")
        local client_result, client_value = t.var.varp("cookquest")
        record("cheats.setvar",
            setvar_result == "ok" and server_result == "ok" and server_value == 1,
            "::setvar cookquest ^cook_started -> " .. tostring(setvar_result)
                .. "; server mirror " .. tostring(server_result) .. "=" .. tostring(server_value)
                .. ", client " .. tostring(client_result) .. "=" .. tostring(client_value)
                .. " (^cook_started is 1, quest_cook.constant)")

        -- A name nothing declares must be refused, not written somewhere
        -- arbitrary. This is the half of `::setvar` a quest suite depends on
        -- most: a misspelled variable in a 179-quest suite has to be loud.
        local badvar_result, badvar_detail = t.cheat("::setvar no_such_variable_here 1")
        record("cheats.setvar_unknown_name", badvar_result == "refused",
            "::setvar no_such_variable_here 1 -> " .. tostring(badvar_result)
                .. " (" .. tostring(badvar_detail) .. "), want refused")

        -- ----------------------------------------------------------- ::spawn
        -- The kill below needs something it certainly made itself. Recorded
        -- either way: if Lumbridge already has a `man` inside SPAWN_RADIUS the
        -- two rows after this cannot separate him from the spawn, and the
        -- detail is what says so to whoever reads a red one.
        local prior_man = t.npc.nearest("man", SPAWN_RADIUS)
        local spawn_result = t.cheat("::spawn man")
        local present_result, present_detail = t.await({
            level = function()
                return t.npc.nearest("man", SPAWN_RADIUS) == "ok"
            end,
            note = "cheats.spawn",
        }, 5)
        record("cheats.spawn",
            spawn_result == "ok" and present_result == "ok",
            "::spawn man -> " .. tostring(spawn_result)
                .. "; within " .. SPAWN_RADIUS .. " tiles: " .. tostring(present_result)
                .. " (" .. tostring(present_detail) .. ")"
                .. "; before the spawn the same read said " .. tostring(prior_man))

        -- ------------------------------------------------------------ ::kill
        -- Lethal damage through `ToriRSServer_CombatHitNpc`, so the npc dies
        -- the ordinary way: QUEUED -> ARRIVE -> CORPSE, and it is at CORPSE
        -- that `[ai_queue3,<type>]` fires -- the trigger every drop table and
        -- every quest boss's varp advance hangs on. An npc that vanished
        -- without reaching it would prove nothing a quest wants to know, which
        -- is why this waits for the pool to empty rather than for a chat line.
        local kill_result = t.cheat("::kill man")
        local gone_result, gone_detail = t.await({
            level = function()
                return t.npc.nearest("man", SPAWN_RADIUS) ~= "ok"
            end,
            note = "cheats.kill",
        }, KILL_DEADLINE_TICKS)
        record("cheats.kill",
            kill_result == "ok" and gone_result == "ok",
            "::kill man -> " .. tostring(kill_result)
                .. "; gone within " .. KILL_DEADLINE_TICKS .. " ticks: "
                .. tostring(gone_result) .. " (" .. tostring(gone_detail) .. ")")

        -- The row that actually proves the DEATH PATH rather than the
        -- disappearance.
        --
        -- `[ai_queue3,man]` (drop_tables/scripts/man.rs2:11) is only reached
        -- from `ToriRSServer_WorldNpcDied`, which `npc_death_step` calls at the
        -- CORPSE stage and nowhere else. The first line of `man_drop_table`
        -- puts `npc_param(death_drop)` on the ground unconditionally, and
        -- `death_drop` defaults to `bones` (skill_combat/configs/
        -- npc_combat.param:43-46). So bones on the floor is the observable end
        -- of the whole chain: an `::kill` that deleted the npc instead -- or
        -- that only emptied its hitpoints -- leaves nothing here, and that is
        -- exactly the difference between a cheat a quest boss test can use and
        -- one that proves nothing. Every quest boss advances its varp from the
        -- same trigger.
        local bones_result, bones_detail = t.await({
            level = function()
                return t.world.obj_near("bones", SPAWN_RADIUS + 2) == "ok"
            end,
            note = "cheats.kill_reached_ai_queue3",
        }, KILL_DEADLINE_TICKS)
        record("cheats.kill_reached_ai_queue3", bones_result == "ok",
            "[ai_queue3,man] -> man_drop_table -> obj_add(npc_coord, bones):"
                .. " bones within " .. (SPAWN_RADIUS + 2) .. " tiles -> "
                .. tostring(bones_result) .. " (" .. tostring(bones_detail) .. ")")

        -- A name no npc carries must be refused rather than killing whatever
        -- happens to be nearest -- the failure mode that would make `::kill`
        -- unusable in a test is it quietly killing the wrong thing.
        local badkill_result, badkill_detail = t.cheat("::kill no_such_npc_here")
        record("cheats.kill_unknown_name", badkill_result == "refused",
            "::kill no_such_npc_here -> " .. tostring(badkill_result)
                .. " (" .. tostring(badkill_detail) .. "), want refused")

        -- --------------------------------------------------------- ::passive
        -- `::passive <npc_symbol>` -- an npc TYPE stops STARTING fights for
        -- the rest of the session. A test affordance, and the one the quest
        -- suite asked for by name: Shades of Mort'ton's five-shade hunt is on
        -- a street four aggressive Afflicted types wander, Mort'ton is
        -- single-way, and one Afflicted that aggresses claims the player for
        -- eight ticks past her last swing -- so every Attack on a Loar Shadow
        -- in that window is refused by the engine's own rule and the hunt
        -- landed two kills of five in twenty rounds
        -- (build/quest_gate/mortton/ledger.tsv row 23).
        --
        -- The rows below are that, in miniature and in Lumbridge, which is
        -- single-way too: an Afflicted spawned beside the player engages her,
        -- an Attack on a SECOND npc is refused with the engine's own sentence,
        -- `::passive` breaks her off, and the same press then lands.
        --
        -- A level-34 Afflicted takes a fixture character (10 hitpoints) down
        -- in a few swings, and a dead player's every click answers `refused`
        -- for reasons that have nothing to do with this cheat.
        t.cheat("::setlevel hitpoints 50")
        t.ticks(2)

        -- The second target -- the stand-in for a Loar Shadow. What is read is
        -- whether the press is ACCEPTED, never whether it kills anything.
        t.cheat("::spawn man")
        t.ticks(2)

        local afflicted_spawn_result = t.cheat("::spawn mort_afflicted_woman")
        local engaged_result, engaged_detail = t.await({
            level = function()
                local result, reading = t.skill.read("hitpoints")
                return result == "ok" and reading.level < 50
            end,
            note = "cheats.passive_setup",
        }, 40)
        local engaged_hp_result, engaged_hp = t.skill.read("hitpoints")
        record("cheats.passive_setup",
            afflicted_spawn_result == "ok" and engaged_result == "ok",
            "::spawn mort_afflicted_woman -> " .. tostring(afflicted_spawn_result)
                .. "; she swung within 40 ticks: " .. tostring(engaged_result)
                .. " (" .. tostring(engaged_detail) .. "); hitpoints 50 -> "
                .. tostring(engaged_hp_result == "ok" and engaged_hp.level or engaged_hp_result)
                .. " -- the CONTROL: without it the two rows below prove nothing")

        -- The seam itself, live: claimed by her, refused on everything else.
        local claimed_result, claimed_detail = t.player.attack("man", 2, 10)
        record("cheats.passive_claimed_before", claimed_result == "refused",
            "t.player.attack(man) while an aggressive Afflicted is on us -> "
                .. tostring(claimed_result) .. " (" .. tostring(claimed_detail)
                .. "), want refused")

        local passive_result, passive_detail = t.cheat("::passive mort_afflicted_woman")
        local passive_said = "?"
        local passive_lines_result, passive_lines = t.msg.last(1)
        if passive_lines_result == "ok" and type(passive_lines) == "table" and passive_lines[1] then
            passive_said = tostring(passive_lines[1].text)
        end
        record("cheats.passive", passive_result == "ok",
            "::passive mort_afflicted_woman -> " .. tostring(passive_result)
                .. " (" .. tostring(passive_detail) .. "); server said '"
                .. passive_said .. "'")

        -- Read BEFORE the second Attack press, while the player is in no fight
        -- at all -- `::passive` broke hers off and cleared his interaction with
        -- it, and the `man` beside him is not aggressive. So every point of
        -- damage in this window would be hers, and "hitpoints did not move" has
        -- exactly one meaning. Twenty ticks is five of her four-tick swings.
        local hold_result, hold = t.skill.read("hitpoints")
        t.ticks(20)
        local held_result, held = t.skill.read("hitpoints")
        record("cheats.passive_stops_the_fight",
            hold_result == "ok" and held_result == "ok" and held.level >= hold.level,
            "hitpoints over 20 ticks beside a passive Afflicted, with no fight"
                .. " of our own: "
                .. tostring(hold_result == "ok" and hold.level or hold_result) .. " -> "
                .. tostring(held_result == "ok" and held.level or held_result)
                .. " (must not fall at all -- nothing else here can hit us)")

        -- And the claim is gone with her, which is the half the quest needed:
        -- the press the engine refused four rows up now lands.
        --
        -- Forty ticks, not the verb's ten.  Since seam15's npc wander parity
        -- (LostCity Npc.ts wanderMode) the Man this presses wanders into
        -- Lumbridge castle, a few tiles from a player standing OUTSIDE its
        -- north wall: the approach is a run round to the east door and back
        -- in, twenty-odd tiles (TORIRSSERVER_MOVE_TRACE, build/quest_gate/
        -- s15close_cheats_trace: 3207,3233 -> 3221,3220 -> 3207,3227 over
        -- ticks 66-85, interaction armed on slot 566 the whole way), and it
        -- timed out at 10 and at 20.  The row proves the claim is gone -- the
        -- press is accepted and the fight happens -- not how fast a walk-up is.
        local freed_result, freed_detail = t.player.attack("man", 2, 40)
        record("cheats.passive_frees_the_player", freed_result == "ok",
            "t.player.attack(man) after ::passive -> " .. tostring(freed_result)
                .. " (" .. tostring(freed_detail) .. "), want ok")

        -- The restore half. The player is now in a fight with the `man`, so an
        -- Afflicted made aggressive again may legitimately decline to engage --
        -- single-way, from her end this time -- and waiting for a second
        -- engagement would be a row that fails for the right reason. What is
        -- read instead is the STATE both combat guards consult, through the
        -- cheat's own bare listing.
        local off_result, off_detail = t.cheat("::passive off mort_afflicted_woman")
        t.ticks(2)
        local list_result = t.cheat("::passive")
        local list_said = "?"
        local list_lines_result, list_lines = t.msg.last(1)
        if list_lines_result == "ok" and type(list_lines) == "table" and list_lines[1] then
            list_said = tostring(list_lines[1].text)
        end
        record("cheats.passive_off",
            off_result == "ok" and list_result == "ok"
                and string.find(list_said, "nothing is passive", 1, true) ~= nil,
            "::passive off mort_afflicted_woman -> " .. tostring(off_result)
                .. " (" .. tostring(off_detail) .. "); ::passive -> "
                .. tostring(list_result) .. ", server said '" .. list_said .. "'")

        -- A name no npc carries must be refused rather than held as a type
        -- nothing will ever match: a misspelled `::passive` in a quest's setup
        -- has to be loud, exactly as a misspelled `::setvar` is.
        local badpassive_result, badpassive_detail = t.cheat("::passive no_such_npc_here")
        record("cheats.passive_unknown_name", badpassive_result == "refused",
            "::passive no_such_npc_here -> " .. tostring(badpassive_result)
                .. " (" .. tostring(badpassive_detail) .. "), want refused")

        -- ------------------------------------------------------------ ::tele
        -- LAST, because it moves the player out of the world every row above
        -- reads. `::tele` is a CONTENT debugproc (cheat_tele.rs2) and was
        -- always reachable from `t.cheat`, so this row is the anchor: if it
        -- goes red while the ladder rows are green, the content-first half of
        -- the dispatch was broken by the split rather than a branch being
        -- missing.
        local tile_result, before_tile = t.world.tile()
        local tele_result = t.cheat("::tele varrock")
        local moved_result, moved_detail = t.await({
            level = function()
                local result, tile = t.world.tile()
                return result == "ok" and tile_result == "ok"
                    and (tile.x ~= before_tile.x or tile.z ~= before_tile.z)
            end,
            note = "cheats.tele",
        }, 10)
        local after_tile_result, after_tile = t.world.tile()
        record("cheats.tele",
            tele_result == "ok" and moved_result == "ok",
            "::tele varrock -> " .. tostring(tele_result)
                .. "; tile " .. tostring(tile_result == "ok" and (before_tile.x .. "," .. before_tile.z) or tile_result)
                .. " -> " .. tostring(after_tile_result == "ok" and (after_tile.x .. "," .. after_tile.z) or after_tile_result)
                .. " (" .. tostring(moved_result) .. "/" .. tostring(moved_detail) .. ")")

        t.ticks(3)
        t.shot("cheats-teleported")

        t.finish(failures > 0 and 1 or 0)
    end,
}
