-- The verb conformance harness: call EVERY verb the quest driver exposes on
-- `t`, exactly once, against a live world, and leave one ledger row per verb.
--
-- Why this file exists.  The driver was written, reviewed twice and passed
-- every gate in the tree while its verb layer did nothing, because the gate
-- was "it compiles" and no reviewer ever ran a verb.  This harness is the
-- replacement gate: `make -C src test-quest-conformance` is red until every
-- row here says PASS, so "fixed" means the ledger moved, not that the build
-- is green.  It is EXPECTED to be mostly failing the day it lands -- that
-- failing table is the baseline the repair phases are measured against.
--
-- Shape.  Every verb is one entry in PLAN, in dependency order: the world is
-- put into a known state first (an npc to point at, an item in the backpack,
-- an item on the ground, a dialogue to read), then the readers, then the
-- actions, then the dialogue pages, then the pure helpers.  An entry NEVER
-- aborts the run:
--   * a verb that is not a function answers "missing" and is not called;
--   * a verb whose subject could not be built (the npc did not resolve, so
--     there is no target to project) answers "no_subject" and is not called,
--     naming the dependency that failed;
--   * every other answer is the verb's own (result, detail), verbatim.
-- There is no pcall in this sandbox (torirs_plugin_lua.c:3925 removes it on
-- purpose), so "never aborts" is bought with type checks and valid argument
-- shapes, not with a catch.  A verb that raises anyway ends the run at that
-- row; the rows already written are on disk (drive_ledger_write appends one
-- row at a time) and tools/quest_gate/conformance.py reports every verb after
-- it as "abort" rather than silently scoring them.
--
-- Rules this file keeps (docs/ARCHITECT.md S2, test/quests/README.md):
--   * no numeric interface or component id, and no client op string: every
--     target is a content symbol, every option is an op NUMBER;
--   * no lane name anywhere;
--   * cheats used to STATE THE WORLD are setup, not rows -- exactly as a
--     normal quest test's `setup = { ... }` is.  The t.cheat row is its own
--     dedicated call, and every setup cheat is marked `-- setup` below.
--
-- A verb that answers "ok" and does nothing is the failure this file is
-- pointed at (QUEST_DRIVER_REMAINING.md phase E, mutation 4), and a row that
-- only forwards the verb's own result word cannot see it.  So every row whose
-- ANSWER is knowable from the world the setup cheats built checks the answer
-- through `answered(...)` and downgrades an empty ok to "hollow", which is not
-- ok and so lands as FAIL naming what was missing.  Measured, 2026-09-19, by
-- running this harness against a driver copy (in a throwaway script tree,
-- never this checkout) whose t.cheat, world.tile, npc.by_name and var.varp
-- were each cut down to `return "ok"`: the rows read
--   [ok] ::xp -> nil / [ok] {} / [ok] Man -> nil / [ok] tutorial -> 0
-- and PASSED before this, and now read
--   [hollow] ::xp answered ok but cooking experience stayed 0
--   [hollow] answered ok but the tile carried no x/z -- {}
--   [hollow] answered ok but the row is not the npc that was asked for
--   [hollow] answered ok but the fixture pins it at 1000 -- tutorial -> 0
-- A verb whose whole answer is `nil` (t.ticks, t.settle, drive.camera, the
-- expect_* assertions) still cannot be caught this way from Lua; those rows
-- say so by forwarding the result word and nothing more.
--
-- Two rows cannot grade themselves from inside Lua, and are re-graded by
-- tools/quest_gate/conformance.py against evidence outside the coroutine:
--   * "note" -- t.note folds its text into the NEXT row's detail, which is
--     this row.  The checker fails the row unless CONFORMANCE_NOTE_PROBE is
--     in the detail column.
--   * "t.finish" -- finish ENDS the run, so the row is written first and the
--     call is made after the loop.  The checker fails the row unless the
--     ledger's SUMMARY row says exit=0 and the process exited 0.
--
-- ---------------------------------------------------------------------------
-- 101 verbs, one row each.  tools/quest_gate/verb_list.py --check reads the
-- `step("<name>", ...)` lines below and the QD.* definitions in
-- script/plugins/quest_driver/*.lua and refuses to agree when they differ, so
-- a verb added to the driver with no row here fails a make gate rather than
-- being quietly never called.  The count is asserted in the harness too, so
-- editing this file alone cannot drift either.
-- @verb-count 101
-- ---------------------------------------------------------------------------

local VERB_COUNT = 101
local NOTE_PROBE = "CONFORMANCE_NOTE_PROBE"

-- Content symbols, never ids.  Each is the subject some verb needs, and each
-- exists in this content pack (OSRS-Content/osrs239-content/configs/*.compack).
-- Most cheats below are [debugproc]s, which is what this harness needed when
-- it was written: DriveCore_Cheat used to dispatch ONLY through
-- ToriRSServer_RunDebugprocForTest, so an engine-ladder cheat (::give,
-- ::spawn, ::setlevel) answered no_row and did nothing, and using one here
-- would have scored a t.cheat defect against whichever verb went without its
-- subject. Phase 1 (docs/QUEST_SUITE_KIT.md; commit "one cheat path for the
-- test client") closed that: DriveCore_Cheat now calls
-- ToriRSServer_RunCheatForTest, which tries content FIRST and then the
-- engine ladder, exactly as handle_cheat does for a logged-in player. So
-- `::setvar` below is a ladder cheat on purpose, and a cheat that matches
-- nothing at all still answers no_row -- which is still the only honest
-- answer for a subject that was never built.
local NPC_SYMBOL = "man"            -- Lumbridge's own; no debugproc spawns npcs
local NPC_DISPLAY_NAME = "Man"      -- npc.by_name matches the DISPLAY name
local COOK_SYMBOL = "cook"          -- the head the cook's dialogue shows
local LOC_SYMBOL = "tree"           -- Lumbridge has these in every direction
local OBJ_SYMBOL = "airrune"        -- ::runes puts 25 in the backpack
-- A fresh tutorial-graduate's own starting body slot (fresh_lumbridge.ini
-- carries no [inv] section, so this is the fresh-character default, the
-- same one inv.slot's own row already reads at slot 1) -- NOT OBJ_SYMBOL:
-- `[opheld2,_] ~equip(last_slot)` (player/scripts/equip.rs2) is a real
-- wildcard, but it still refuses an item with no worn slot, and air runes
-- have none. player.equip answering "refused" every run on a rune is not a
-- driver defect, it is the harness handing the verb a subject that can never
-- pass -- QD.player.equip's own banner already says as much ("must read as
-- refused, with the server's own sentence, not as success").
local WEARABLE_OBJ_SYMBOL = "bronze_platebody"
local ABSENT_OBJ_SYMBOL = "knife"   -- nothing here puts one in the backpack
local VARP_SYMBOL = "tutorial"      -- the fixture pins it (perm scope)
local VARP_VALUE = 1000             -- "tutorial finished" (docs/WORKTREE_SETUP.md)
local VARBIT_SYMBOL = "troll_freed_eadgar"
-- Cook's Assistant, the one quest this content pack can be driven into every
-- state of from a cheat: `::setvar cookquest ^cook_started` stages it,
-- `::cookbmp_reward` completes it and puts the real reward scroll up, and its
-- journal row is the first in the quest list.  The stage numbers are the
-- quest's own constants (OSRS-Content/.../quest_cook/configs/quest_cook.constant:
-- ^cook_not_started 0, ^cook_started 1, ^cook_complete 2) written out, because
-- quest.bind's `constants` table takes integers: there is no "constant" kind
-- in DriveSymbolKind for a `^name` to resolve through.
local QUEST_VARP = "cookquest"
local QUEST_NOT_STARTED = 0
local QUEST_STARTED = 1
local QUEST_COMPLETE = 2
local QUEST_DISPLAY = "Cook's Assistant"
local QUEST_POINTS = 1               -- quest:questpoints for this row
local QUEST_REWARD_XP = 300          -- "300 Cooking XP", quest_cook.rs2:158
-- ::xp is `stat_advance($stat, $amount)` and $amount is TENTHS of an xp point
-- on this build: measured 2026-09-19, `::xp cooking 500` moved
-- skill("cooking").experience by 50.
local XP_CHEAT_AMOUNT = 500
local XP_CHEAT_GAIN = 50
-- An npc symbol this pack defines (npc 1173) with no instance anywhere near
-- the Lumbridge courtyard -- the chicken coop is ~77 tiles north -- so
-- "this npc is not within five tiles" is a fact, not a race.
local ABSENT_NPC_SYMBOL = "chicken"
local STAT_SYMBOL = "cooking"
-- player.goto_tile's subject: the Duke of Lumbridge's room, upstairs in the
-- castle (OSRS-Content/.../areas/world/configs/m50_50.spawn:168,
-- `duke_of_lumbridge 3212 3220 1`).  An UPPER floor on purpose -- the plane
-- is the half of an absolute-tile teleport a tile read can catch being
-- wrong, because x/z are allowed to land one tile out (the world puts the
-- player on the nearest tile it accepts) and the plane is not: 3212,3220 on
-- level 0 is the castle's ground floor, a different room and a different
-- quest.
local GOTO_TILE_X = 3212
local GOTO_TILE_Z = 3220
local GOTO_TILE_LEVEL = 1
local INVENTORY_INTERFACE = "inventory"
local OBJECTBOX_INTERFACE = "objectbox"
local INVENTORY_ITEMS_COMPONENT = "inventory:items"
local OBJBOX_TEXT_FRAGMENT = "You get some"   -- interface_chat/scripts/chat.rs2:408
local DROP_MESSAGE_FRAGMENT = "Dropped"       -- ::dropobj's reply (cheat_obj.rs2:20)

-- player.use_item_on_item's pair, and what it makes.  Desert Treasure's
-- `[opheldu,garlic]` (quests/quest_deserttreasure/scripts/deserttreasure.rs2:505)
-- is `if (last_useitem = pestle_and_mortar)` with no quest gate on it at all:
-- it deletes the garlic, adds the powder and says one line.  Chosen over the
-- other recipes this pack has for three reasons -- it is reachable from a
-- fresh character, its whole effect is a BACKPACK SWAP the row can read back
-- (so a verb that answered `ok` having sent nothing cannot pass), and it
-- answers with a chat line rather than a `~mesbox`, so it leaves no dialogue
-- page standing for the rows after it to inherit.
local USE_ITEM_HELD = "pestle_and_mortar"   -- armed: phase 1, the "Use" row
local USE_ITEM_TARGET = "garlic"            -- clicked: phase 2, the OPHELDU
local USE_ITEM_MADE = "fd_crushed_garlic"   -- what the recipe leaves behind

-- The combat pair's subject: the SAME Man the pointer rows above point at
-- (`op1=Talk-to`, `op2=Attack`, `stat4=7` -- seven hitpoints and negative
-- defences, configs/all.npc), so Attack is op 2 here exactly as it is on the
-- npc player.attack's banner names.  Last in the run, because these two rows
-- kill him and `npc.await_present`/`player.talk_to`/`drive.*` above all need
-- him alive.
local COMBAT_ATTACK_OP = 2
-- Gear and levels are a PREREQUISITE, not the thing under test: the fight
-- itself is still driven by a real Attack click through click_minimenu.  A
-- level-3 fresh character with no weapon lands about one hit in thirty
-- (measured, build/quest_gate/q3probe2: thirty swings from one click, one
-- hit of 1), which would make this row a coin toss on the deadline rather
-- than a test of the verb.
local COMBAT_WEAPON = "rune_scimitar"
local COMBAT_LEVEL = 40

-- A detail column is a string or the ledger's luaL_optstring raises.  Tables
-- (a verb that answers with a row, a tile, an options list) are summarised
-- rather than dropped: what a verb ANSWERED WITH is half of what this harness
-- is for.
local function describe(value, depth)
    depth = depth or 0
    local kind = type(value)
    if value == nil then
        return "nil"
    elseif kind == "string" then
        if #value > 120 then
            return string.sub(value, 1, 120) .. "..."
        end
        return value
    elseif kind == "number" or kind == "boolean" then
        return tostring(value)
    elseif kind == "function" then
        return "<function>"
    elseif kind == "table" then
        if depth > 1 then
            return "<table>"
        end
        local parts = {}
        local count = 0
        for key, entry in pairs(value) do
            count = count + 1
            if count > 8 then
                parts[#parts + 1] = "..."
                break
            end
            parts[#parts + 1] = tostring(key) .. "=" .. describe(entry, depth + 1)
        end
        if count == 0 then
            return "{}"
        end
        return "{" .. table.concat(parts, " ") .. "}"
    end
    return "<" .. kind .. ">"
end

-- A verb that answers "ok" with nothing behind it is exactly the failure this
-- harness exists to catch (QUEST_DRIVER_REMAINING.md phase E, mutation 4:
-- "make a verb silently return ok without doing its work, and the conformance
-- harness must still go red").  A row that only forwards the verb's own result
-- word cannot catch it, so every row whose ANSWER is knowable checks the
-- answer here and downgrades an empty ok to "hollow", which is not ok and so
-- lands as a FAIL row naming what was missing.
local function is_table(value) return type(value) == "table" end
local function is_number(value) return type(value) == "number" end
local function is_text(value) return type(value) == "string" and value ~= "" end

local function field(name, test)
    return function(value)
        return type(value) == "table" and test(value[name])
    end
end

local function equals(wanted)
    return function(value) return value == wanted end
end

local function at_least(floor)
    return function(value) return type(value) == "number" and value >= floor end
end

-- (result, detail) in, (result, detail) out -- except that an "ok" whose
-- answer does not hold becomes "hollow", with what was expected in the row.
local function answered(result, detail, prefix, holds, wanted)
    local text = prefix .. describe(detail)
    if result == "ok" and not holds(detail) then
        return "hollow", "answered ok but " .. wanted .. " -- " .. text
    end
    return result, text
end

-- The DELIBERATE NO-OP PROBE, and the one answer that proves it.
--
-- Two rows below (player.use_on, player.inv_op) name an interaction no
-- content script claims: an air rune used on a Man, and op 3 on an air rune.
-- Nothing can make either of them do something -- that is the point. The rows
-- exist to prove the OPHELDU / OPHELD dispatch reaches the server at all, on
-- a pairing chosen because it cannot leave a side effect a later row would
-- inherit (the row below says why op 1 cannot be used).
--
-- The world's honest answer to such a click is the engine's own
-- "Nothing interesting happens." ([proc,nothing_interesting_message],
-- OSRS-Content/.../player/messages.rs2:116), said only when the interaction
-- REACHED its target and no script claimed it. Before the 2026-09-19 settle
-- fence both rows read `[ok] chat_message` -- which is the bug that pass
-- exists to kill: a refusal counted as a conversation. pointer.lua now
-- answers `refused` carrying that sentence, so this is where the row states
-- what it actually expects: THAT sentence, not any refusal and not any chat
-- line. Anything else -- a reachability failure, a timeout, a plain `ok` with
-- nothing behind it -- is forwarded unchanged and still fails the row.
local NO_SCRIPT_LINE = "Nothing interesting happens."
local function no_script_probe(result, detail, prefix)
    local text = prefix .. describe(detail)
    if result == "refused" and string.find(tostring(detail), NO_SCRIPT_LINE, 1, true) then
        return "ok", "the engine's own no-script refusal -- the dispatch reached "
            .. "the server and no script claimed it: " .. text
    end
    return result, text
end

return {
    id = "_conformance",
    fixture = "fresh_lumbridge.ini",
    -- Stated here for the runner's benefit; this harness re-issues them
    -- itself through t.cheat so it can also run standalone.
    setup = {},

    run = function(t)
        -- Rewritten by tools/quest_gate/conformance.py between attempts. There
        -- is no pcall here, so a verb that RAISES (rather than answering a
        -- result) ends the run where it stands. The runner attributes that
        -- error to the first verb with no row, records it as "error" with the
        -- message, adds it here, and re-runs -- so every verb still gets
        -- exactly one row, and an unrunnable verb costs its own row and no
        -- one else's. Empty on a clean first attempt.
        local SKIP = {} --@skip

        local PLAN = {}
        local verb_count = 0

        -- One entry per verb.  `fn` returns (result, detail) -- or nil when it
        -- has already written its own row (t.step is the only one).
        local function step(name, fn)
            verb_count = verb_count + 1
            PLAN[#PLAN + 1] = { name = name, call = fn }
        end

        -- A world-state entry: a cheat ladder that gives the verbs after it a
        -- subject.  It writes no row, is never skipped, and is exactly what a
        -- normal quest test's `setup = { ... }` list is.
        local function stage(fn)
            PLAN[#PLAN + 1] = { stage = fn }
        end

        -- Resolve a verb by its dotted path without ever indexing a nil.
        local function verb(namespace, name)
            if name == nil then
                local value = t[namespace]
                if type(value) == "function" then
                    return value
                end
                return nil
            end
            local group = t[namespace]
            if type(group) ~= "table" then
                return nil
            end
            local value = group[name]
            if type(value) == "function" then
                return value
            end
            return nil
        end

        local function missing(namespace, name)
            if name == nil then
                return "missing", "t." .. namespace .. " is not a function"
            end
            return "missing", "t." .. namespace .. "." .. name .. " is not a function"
        end

        -- A cheat that STATES THE WORLD.  Not a row: setup, exactly as a
        -- normal quest test's `setup = { ... }` list is.
        --
        -- `wait_for_reply` is passed through to t.cheat and is false in
        -- exactly one place: the stage that gives msg.await its subject.
        -- t.cheat waits for the cheat's own reply line by default, and a line
        -- that has already arrived is not a line msg.await can wait for.
        local function setup_cheat(text, wait_for_reply)
            local cheat = verb("cheat")
            if cheat then
                cheat(text, wait_for_reply)
            end
        end

        local function settle(ticks)
            local advance = verb("ticks")
            if advance then
                advance(ticks)
            end
        end

        -- Subjects the action verbs need, filled in by their own rows below so
        -- a later verb can say WHICH dependency was missing instead of
        -- crashing on a nil.
        local npc_target = nil
        local player_tile = nil
        local inventory_widget = nil
        local stat_snapshot = nil

        -- ------------------------------------------------------- the world

        -- Lumbridge, an npc to point at, bread in the backpack, bread on the
        -- ground, a skill with a reading.  All setup, no rows.
        setup_cheat("::tele lumbridge")
        settle(4)
        setup_cheat("::runes 25")
        setup_cheat("::dropobj " .. OBJ_SYMBOL .. " 1")
        setup_cheat("::xp " .. STAT_SYMBOL .. " 500")
        settle(6)

        -- --------------------------------------------- phase 0: the clock

        step("cheat", function()
            local fn = verb("cheat")
            if not fn then return missing("cheat") end
            -- ::xp is a [debugproc] whose whole observable effect is a stat
            -- reading, so the row reads that stat on both sides of the call.
            -- A cheat that answered ok and dispatched nothing has the same
            -- experience after as before, and that is the row's verdict.
            local read = verb("skill", "read")
            local before = nil
            if read then
                local state, reading = read(STAT_SYMBOL)
                if state == "ok" and type(reading) == "table" then
                    before = reading.experience
                end
            end
            local result, detail = fn("::xp " .. STAT_SYMBOL .. " 100")
            settle(3)
            if result ~= "ok" then
                return result, "::xp -> " .. describe(detail)
            end
            if before == nil then
                return "ok", "::xp -> " .. describe(detail)
                    .. " (no experience reading to confirm the effect with)"
            end
            local state, reading = read(STAT_SYMBOL)
            local after = type(reading) == "table" and reading.experience or nil
            if state ~= "ok" or not is_number(after) then
                return "ok", "::xp -> " .. describe(detail)
                    .. " (the stat read back " .. describe(reading) .. ")"
            end
            if after <= before then
                return "hollow", "::xp answered ok but " .. STAT_SYMBOL
                    .. " experience stayed " .. describe(before)
            end
            return "ok", "::xp -> " .. STAT_SYMBOL .. " experience "
                .. describe(before) .. " -> " .. describe(after)
        end)

        step("ticks", function()
            local fn = verb("ticks")
            if not fn then return missing("ticks") end
            local result, detail = fn(2)
            return result, "advance 2 server ticks -> " .. describe(detail)
        end)

        step("settle", function()
            local fn = verb("settle")
            if not fn then return missing("settle") end
            local result, detail = fn()
            return result, describe(detail)
        end)

        step("shot", function()
            local fn = verb("shot")
            if not fn then return missing("shot") end
            local result, detail = fn("conformance-world")
            return answered(result, detail, "", is_text, "no file was named")
        end)

        -- ------------------------------------- phase 1: naming the world

        step("world.tile", function()
            local fn = verb("world", "tile")
            if not fn then return missing("world", "tile") end
            local result, detail = fn()
            if result == "ok" and type(detail) == "table" then
                player_tile = detail
            end
            return answered(result, detail, "",
                function(value)
                    return is_table(value) and is_number(value.x) and is_number(value.z)
                end, "the tile carried no x/z")
        end)

        step("world.level", function()
            local fn = verb("world", "level")
            if not fn then return missing("world", "level") end
            local result, detail = fn()
            return answered(result, detail, "", is_number, "no level number came back")
        end)

        step("npc.by_name", function()
            local fn = verb("npc", "by_name")
            if not fn then return missing("npc", "by_name") end
            local result, detail = fn(NPC_DISPLAY_NAME)
            return answered(result, detail, NPC_DISPLAY_NAME .. " -> ",
                field("name", equals(NPC_DISPLAY_NAME)),
                "the row is not the npc that was asked for")
        end)

        step("npc.by_symbol", function()
            local fn = verb("npc", "by_symbol")
            if not fn then return missing("npc", "by_symbol") end
            local result, detail = fn(NPC_SYMBOL)
            return answered(result, detail, NPC_SYMBOL .. " -> ",
                field("npc_id", is_number), "the row carried no npc id")
        end)

        -- This row also carries world.tile's only behavioural check, because
        -- world.tile's own row can only look at the SHAPE of what came back
        -- and a reader that answered a constant would satisfy that.  Proved,
        -- not guessed: making QD.world.tile return a fixed {x=0,z=0,level=0}
        -- without asking the client left its own row PASS (mutation (d), the
        -- final gate's run).  `nearest` searched a radius around the player's
        -- real position and found this npc inside it, so a tile the two
        -- readers disagree about by more than that radius is one of them
        -- inventing an answer -- and it needs no literal coordinate, which
        -- ARCHITECT.md's naming rule would not allow here anyway.
        local NEAREST_RADIUS = 40
        step("npc.nearest", function()
            local fn = verb("npc", "nearest")
            if not fn then return missing("npc", "nearest") end
            local result, detail = fn(NPC_SYMBOL, NEAREST_RADIUS)
            local graded, text = answered(result, detail,
                NPC_SYMBOL .. " r=" .. NEAREST_RADIUS .. " -> ",
                field("npc_id", is_number), "the row carried no npc id")
            if graded ~= "ok" then
                return graded, text
            end
            if not is_table(player_tile) then
                return "hollow", "world.tile never produced a tile to cross-check -- " .. text
            end
            local dx = math.abs(detail.x - player_tile.x)
            local dz = math.abs(detail.z - player_tile.z)
            if dx > NEAREST_RADIUS or dz > NEAREST_RADIUS then
                return "hollow", "an npc found within " .. NEAREST_RADIUS
                    .. " tiles of the player sits " .. dx .. "," .. dz
                    .. " from the tile world.tile reported -- one of the two "
                    .. "readers is not reading -- " .. text
            end
            return "ok", text
        end)

        -- The two awaits over npc.nearest.  await_present has a real
        -- subject (a `man` walks the courtyard and npc.nearest just found
        -- one); await_gone's subject is the absence of a chicken, which is a
        -- standing fact here rather than a transition, so both rows read the
        -- world back through npc.nearest after the await answers -- an await
        -- that resolved on nothing would then be caught saying ok about a
        -- world that disagrees.  The EDGE (an npc that is here and then is
        -- not) is proved elsewhere, by test/quests/_cheats.lua's ::kill row
        -- and by hans.lua's hans.leaves: putting a kill in the middle of this
        -- harness would take a subject away from the rows after it.
        step("npc.await_present", function()
            local fn = verb("npc", "await_present")
            if not fn then return missing("npc", "await_present") end
            local result, detail = fn(NPC_SYMBOL, NEAREST_RADIUS, 6)
            if result ~= "ok" then
                return result, NPC_SYMBOL .. " within " .. NEAREST_RADIUS .. " -> " .. describe(detail)
            end
            local look = verb("npc", "nearest")
            local state = look and look(NPC_SYMBOL, NEAREST_RADIUS) or "missing"
            if state ~= "ok" then
                return "hollow", "await_present answered ok but npc.nearest(" .. NPC_SYMBOL
                    .. ", " .. NEAREST_RADIUS .. ") answers " .. state
            end
            return "ok", NPC_SYMBOL .. " within " .. NEAREST_RADIUS .. " -> ok"
        end)

        step("npc.await_gone", function()
            local fn = verb("npc", "await_gone")
            if not fn then return missing("npc", "await_gone") end
            local result, detail = fn(ABSENT_NPC_SYMBOL, 5, 6)
            if result ~= "ok" then
                return result, ABSENT_NPC_SYMBOL .. " within 5 -> " .. describe(detail)
            end
            local look = verb("npc", "nearest")
            local state = look and look(ABSENT_NPC_SYMBOL, 5) or "missing"
            if state == "ok" then
                return "hollow", "await_gone answered ok while npc.nearest still finds a "
                    .. ABSENT_NPC_SYMBOL .. " within 5 tiles"
            end
            return "ok", "no " .. ABSENT_NPC_SYMBOL .. " within 5 tiles (npc.nearest agrees: "
                .. state .. ")"
        end)

        step("world.loc_near", function()
            local fn = verb("world", "loc_near")
            if not fn then return missing("world", "loc_near") end
            local result, detail = fn(LOC_SYMBOL, 60)
            return answered(result, detail, LOC_SYMBOL .. " r=60 -> ",
                field("id", is_number), "the row carried no loc id")
        end)

        step("world.obj_near", function()
            local fn = verb("world", "obj_near")
            if not fn then return missing("world", "obj_near") end
            local result, detail = fn(OBJ_SYMBOL, 20)
            return answered(result, detail, OBJ_SYMBOL .. " r=20 -> ",
                field("id", is_number), "the row carried no obj id")
        end)

        step("player.by_symbol", function()
            local fn = verb("player", "by_symbol")
            if not fn then return missing("player", "by_symbol") end
            -- (target, "ok") on success; (nil, result, name) on failure.
            local target, result, name = fn("npc", NPC_SYMBOL)
            if type(target) == "table" then
                npc_target = target
                return "ok", "npc " .. NPC_SYMBOL .. " -> " .. describe(target)
            end
            return result or "no_row", "npc " .. NPC_SYMBOL .. " -> " .. describe(name)
        end)

        -- ------------------------------------------ phase 2: state reads

        step("var.varp", function()
            local fn = verb("var", "varp")
            if not fn then return missing("var", "varp") end
            local result, detail = fn(VARP_SYMBOL)
            -- The fixture pins this one, so the reading is knowable: a reader
            -- that answers ok with 0 (or with nothing) is not reading.
            return answered(result, detail, VARP_SYMBOL .. " -> ",
                equals(VARP_VALUE), "the fixture pins it at " .. VARP_VALUE)
        end)

        step("var.varbit", function()
            local fn = verb("var", "varbit")
            if not fn then return missing("var", "varbit") end
            local result, detail = fn(VARBIT_SYMBOL)
            return answered(result, detail, VARBIT_SYMBOL .. " -> ",
                is_number, "no varbit value came back")
        end)

        step("var.server", function()
            local fn = verb("var", "server")
            if not fn then return missing("var", "server") end
            local result, detail = fn(VARP_SYMBOL)
            -- The server's own copy of a varp the fixture pins: same value,
            -- read down the other side of the seam.
            return answered(result, detail, VARP_SYMBOL .. " -> ",
                equals(VARP_VALUE), "the fixture pins it at " .. VARP_VALUE)
        end)

        step("var.expect", function()
            local fn = verb("var", "expect")
            if not fn then return missing("var", "expect") end
            local result, detail = fn(VARP_SYMBOL, VARP_VALUE)
            return result, VARP_SYMBOL .. " == " .. VARP_VALUE .. " -> " .. describe(detail)
        end)

        step("var.await", function()
            local fn = verb("var", "await")
            if not fn then return missing("var", "await") end
            local result, detail = fn(VARP_SYMBOL, VARP_VALUE, 3)
            return result, VARP_SYMBOL .. " == " .. VARP_VALUE .. " -> " .. describe(detail)
        end)

        step("skill.read", function()
            local fn = verb("skill", "read")
            if not fn then return missing("skill", "read") end
            local result, detail = fn(STAT_SYMBOL)
            return answered(result, detail, STAT_SYMBOL .. " -> ",
                field("level", at_least(1)), "the reading carried no level")
        end)

        -- Every stat read once.  The snapshot is kept for skill.expect_gain
        -- below, which is the only way to grade a gain: the two verbs are one
        -- before/after pair with a cheat between them.
        step("skill.snapshot", function()
            local fn = verb("skill", "snapshot")
            if not fn then return missing("skill", "snapshot") end
            local result, detail = fn()
            if result == "ok" and is_table(detail) then
                stat_snapshot = detail
            end
            return answered(result, detail, "",
                field(STAT_SYMBOL, field("experience", is_number)),
                "a snapshot with no " .. STAT_SYMBOL .. " reading is not a snapshot")
        end)

        -- setup: the gain skill.expect_gain is about to be asked to find.
        stage(function()
            setup_cheat("::xp " .. STAT_SYMBOL .. " " .. XP_CHEAT_AMOUNT)
            settle(4)
        end)

        step("skill.expect_gain", function()
            local fn = verb("skill", "expect_gain")
            if not fn then return missing("skill", "expect_gain") end
            if not is_table(stat_snapshot) then
                return "no_subject", "skill.snapshot built no snapshot to measure against"
            end
            local result, detail = fn(STAT_SYMBOL, XP_CHEAT_GAIN, stat_snapshot)
            -- The verb's own detail names which unit matched, so a row that
            -- only forwarded "ok" would still be readable -- but the gain is
            -- knowable here (::xp ran between the snapshot and now), so the
            -- unit sentence is what is asserted.
            return answered(result, detail,
                STAT_SYMBOL .. " +" .. XP_CHEAT_GAIN .. " -> ", is_text,
                "the verb names the unit it matched in")
        end)

        step("inv.count", function()
            local fn = verb("inv", "count")
            if not fn then return missing("inv", "count") end
            local result, detail = fn(OBJ_SYMBOL)
            -- ::runes put 25 in the backpack, so 0 here is a reader that is
            -- not reading, not an empty backpack.
            return answered(result, detail, OBJ_SYMBOL .. " -> ",
                at_least(1), "::runes put " .. OBJ_SYMBOL .. " in the backpack")
        end)

        step("inv.has", function()
            local fn = verb("inv", "has")
            if not fn then return missing("inv", "has") end
            local result, detail = fn(OBJ_SYMBOL)
            return answered(result, detail, OBJ_SYMBOL .. " -> ",
                equals(true), "::runes put " .. OBJ_SYMBOL .. " in the backpack")
        end)

        step("inv.slot", function()
            local fn = verb("inv", "slot")
            if not fn then return missing("inv", "slot") end
            local result, detail = fn(1)
            return answered(result, detail, "slot 1 -> ",
                field("name", is_text), "the slot answered with no obj name")
        end)

        step("inv.expect_has", function()
            local fn = verb("inv", "expect_has")
            if not fn then return missing("inv", "expect_has") end
            local result, detail = fn(OBJ_SYMBOL, 1)
            return answered(result, detail, OBJ_SYMBOL .. " >= 1 -> ",
                at_least(1), "the assertion passed with no count behind it")
        end)

        step("inv.expect_absent", function()
            local fn = verb("inv", "expect_absent")
            if not fn then return missing("inv", "expect_absent") end
            local result, detail = fn(ABSENT_OBJ_SYMBOL)
            return result, ABSENT_OBJ_SYMBOL .. " -> " .. describe(detail)
        end)

        step("inv.await", function()
            local fn = verb("inv", "await")
            if not fn then return missing("inv", "await") end
            local result, detail = fn(OBJ_SYMBOL, 1, 3)
            return result, OBJ_SYMBOL .. " >= 1 -> " .. describe(detail)
        end)

        -- One await over a whole requirement table.  ::runes put 25 air runes
        -- in the backpack, so the table is satisfiable; the row reads the
        -- count back afterwards so an await that resolved on nothing cannot
        -- pass as ok.
        step("inv.await_all", function()
            local fn = verb("inv", "await_all")
            if not fn then return missing("inv", "await_all") end
            local result, detail = fn({ [OBJ_SYMBOL] = 1 }, 3)
            if result ~= "ok" then
                return result, OBJ_SYMBOL .. " >= 1 -> " .. describe(detail)
            end
            local count = verb("inv", "count")
            local state, total = "missing", nil
            if count then state, total = count(OBJ_SYMBOL) end
            if state ~= "ok" or not is_number(total) or total < 1 then
                return "hollow", "await_all answered ok but inv.count(" .. OBJ_SYMBOL
                    .. ") reads " .. describe(total) .. " (" .. state .. ")"
            end
            return "ok", OBJ_SYMBOL .. " >= 1 satisfied, inv.count agrees: " .. describe(total)
        end)

        step("msg.last", function()
            local fn = verb("msg", "last")
            if not fn then return missing("msg", "last") end
            local result, detail = fn(5)
            -- The setup cheats have already put lines in the chatbox, so an
            -- empty list is a reader that is not reading.
            return answered(result, detail, "last 5 -> ",
                field(1, field("text", is_text)),
                "the setup cheats already wrote lines to the chatbox")
        end)

        step("msg.expect", function()
            local fn = verb("msg", "expect")
            if not fn then return missing("msg", "expect") end
            local result, detail = fn(DROP_MESSAGE_FRAGMENT)
            return result, "contains '" .. DROP_MESSAGE_FRAGMENT .. "' -> " .. describe(detail)
        end)

        -- setup: msg.await is scoped to lines that arrive AFTER it registers,
        -- so the line it waits for is sent here -- dispatched into the server
        -- now, reaching the client a tick later, inside the await.
        --
        -- DISPATCHED WITHOUT t.cheat's own reply wait (the `false`), which is
        -- the whole reason that argument exists.  Phase 2 gave t.cheat a
        -- <=5-tick wait for the cheat's own reply line so a cheat's effect is
        -- visible before the next read; the cost is that the reply has
        -- already arrived by the time anything else can register an await for
        -- it, and this row went red the first run after that landed
        -- ([timeout] new line containing 'Dropped', measured 2026-09-19) --
        -- correctly, with nothing new left to wait for.  Here the waiting is
        -- the ROW's job, so the dispatch does not do it.
        stage(function()
            setup_cheat("::dropobj " .. OBJ_SYMBOL .. " 1", false)
        end)

        step("msg.await", function()
            local fn = verb("msg", "await")
            if not fn then return missing("msg", "await") end
            local result, detail = fn(DROP_MESSAGE_FRAGMENT, 6)
            return result, "new line containing '" .. DROP_MESSAGE_FRAGMENT .. "' -> " .. describe(detail)
        end)

        -- -------------------------------- phase 3: pointing and acting

        step("drive.camera", function()
            local fn = verb("drive", "camera")
            if not fn then return missing("drive", "camera") end
            local result, detail = fn(0, 300, 400)
            return result, "yaw=0 pitch=300 zoom=400 -> " .. describe(detail)
        end)

        step("drive.screen_position", function()
            local fn = verb("drive", "screen_position")
            if not fn then return missing("drive", "screen_position") end
            if not npc_target then
                return "no_subject", "player.by_symbol(npc, " .. NPC_SYMBOL .. ") built no target"
            end
            local result, detail = fn(npc_target)
            return result, describe(detail)
        end)

        step("drive.click_minimenu", function()
            local fn = verb("drive", "click_minimenu")
            if not fn then return missing("drive", "click_minimenu") end
            if not npc_target then
                return "no_subject", "player.by_symbol(npc, " .. NPC_SYMBOL .. ") built no target"
            end
            local result, detail = fn(npc_target, 1)
            return result, "op slot 1 -> " .. describe(detail)
        end)

        step("drive.op", function()
            local fn = verb("drive", "op")
            if not fn then return missing("drive", "op") end
            if not npc_target then
                return "no_subject", "player.by_symbol(npc, " .. NPC_SYMBOL .. ") built no target"
            end
            local result, detail = fn(npc_target, 1)
            return result, "LOGGED bypass, op 1 -> " .. describe(detail)
        end)

        step("player.idle", function()
            local fn = verb("player", "idle")
            if not fn then return missing("player", "idle") end
            local result, detail = fn()
            return result, describe(detail)
        end)

        -- Phase 3's first rows click "Talk-to" on a WANDERING man, which
        -- leaves the player chasing him -- and the three rows below assert
        -- against the player's OWN tile.  Measured 2026-09-19: player.walk_to
        -- timed out having moved two tiles WEST, toward the man, instead of
        -- the one tile east it asked for, on a run where the msg stage above
        -- stopped spending five ticks waiting for a cheat's reply and phase 3
        -- therefore started that much earlier.  player.idle's own row is not
        -- enough on its own: a wandering npc stands still between steps, and
        -- an idle reading taken in one of those gaps is true while the
        -- interaction behind it is very much alive.  So the dialogue is
        -- dismissed and the world is given six ticks to stop before the rows
        -- that are about the player's own feet.
        stage(function()
            local close = verb("chat", "close")
            if close then
                close()
            end
            settle(6)
        end)

        step("player.walk_to", function()
            local fn = verb("player", "walk_to")
            if not fn then return missing("player", "walk_to") end
            if not player_tile or type(player_tile.x) ~= "number" then
                return "no_subject", "world.tile() answered no tile to walk one step from"
            end
            local result, detail = fn(player_tile.x + 1, player_tile.z)
            return result, "one tile east -> " .. describe(detail)
        end)

        step("player.walk_near", function()
            local fn = verb("player", "walk_near")
            if not fn then return missing("player", "walk_near") end
            if not npc_target then
                return "no_subject", "player.by_symbol(npc, " .. NPC_SYMBOL .. ") built no target"
            end
            local result, detail = fn(npc_target)
            return result, describe(detail)
        end)

        step("player.talk_to", function()
            local fn = verb("player", "talk_to")
            if not fn then return missing("player", "talk_to") end
            local result, detail = fn(NPC_SYMBOL)
            return result, NPC_SYMBOL .. " -> " .. describe(detail)
        end)

        step("player.click_loc", function()
            local fn = verb("player", "click_loc")
            if not fn then return missing("player", "click_loc") end
            local result, detail = fn(LOC_SYMBOL)
            return result, LOC_SYMBOL .. " -> " .. describe(detail)
        end)

        step("player.click_obj", function()
            local fn = verb("player", "click_obj")
            if not fn then return missing("player", "click_obj") end
            local result, detail = fn(OBJ_SYMBOL)
            return result, OBJ_SYMBOL .. " -> " .. describe(detail)
        end)

        step("player.use_on", function()
            local fn = verb("player", "use_on")
            if not fn then return missing("player", "use_on") end
            -- A deliberate no-op pairing: no [opheldu] claims an air rune on
            -- a Man, so the world's honest answer is the engine's
            -- "Nothing interesting happens." -- see no_script_probe above.
            local result, detail = fn(OBJ_SYMBOL, npc_target)
            return no_script_probe(result, detail, OBJ_SYMBOL .. " on " .. NPC_SYMBOL .. " -> ")
        end)

        step("player.inv_op", function()
            local fn = verb("player", "inv_op")
            if not fn then return missing("player", "inv_op") end
            -- NOT op 1. On rev-239 the backpack's IF3 cell carries a
            -- CLIENT-SIDE on_op hook at op index 1 that is the shift-click-
            -- drop chain (script 6014 -- app_minimenu.c's own comment on
            -- app_inv_cell_op_flash, "the inventory slot builder puts the
            -- shift-click-drop handler there"): app_inv_cell_op_flash fires
            -- that hook for EVERY OPHELD1..5 dispatch, keyed on the SLOT,
            -- not the item, so `inv_op(airrune, 1)` silently drops the whole
            -- stack no matter what op 1 is "supposed" to mean for this obj.
            -- Measured: the stack goes 26 -> 0 within the same tick this
            -- verb's own settle resolves, and every verb after it that
            -- needs the item (player.equip, player.drop) then answers
            -- not_found. Op 3 carries no such client-side binding and is a
            -- clean probe of the OPHELD dispatch path -- and, being clean, one
            -- no script claims either, so the answer this row expects is the
            -- engine's "Nothing interesting happens." (no_script_probe above).
            local result, detail = fn(OBJ_SYMBOL, 3)
            return no_script_probe(result, detail, "")
        end)

        step("player.equip", function()
            local fn = verb("player", "equip")
            if not fn then return missing("player", "equip") end
            local result, detail = fn(WEARABLE_OBJ_SYMBOL)
            return result, describe(detail)
        end)

        step("player.drop", function()
            local fn = verb("player", "drop")
            if not fn then return missing("player", "drop") end
            local result, detail = fn(OBJ_SYMBOL)
            return result, describe(detail)
        end)

        -- ----------- phase 7b: the three verbs the phase 6 seams landed
        --
        -- Last of the acting rows, and after player.goto_tile, because the
        -- two combat rows KILL the Man every pointer and npc row above
        -- points at.  Nothing below this block reads the world.

        -- setup: the recipe's two items.  A `::give` pair, exactly as a quest
        -- test's own `setup` would state a prerequisite -- the thing under
        -- test is the OPHELDU click, not how the items were got.
        stage(function()
            local close = verb("chat", "close")
            if close then
                close()
            end
            setup_cheat("::give " .. USE_ITEM_HELD .. " 1")
            setup_cheat("::give " .. USE_ITEM_TARGET .. " 1")
            settle(4)
        end)

        step("player.use_item_on_item", function()
            local fn = verb("player", "use_item_on_item")
            if not fn then return missing("player", "use_item_on_item") end
            local count = verb("inv", "count")
            if not count then
                return "no_subject", "t.inv.count is not a function, so nothing can grade the swap"
            end
            local have_state, have = count(USE_ITEM_TARGET)
            if have_state ~= "ok" or not is_number(have) or have < 1 then
                return "no_subject", "::give " .. USE_ITEM_TARGET .. " left "
                    .. describe(have) .. " in the backpack (" .. tostring(have_state) .. ")"
            end
            local result, detail = fn(USE_ITEM_HELD, USE_ITEM_TARGET)
            local text = USE_ITEM_HELD .. " on " .. USE_ITEM_TARGET .. " -> " .. describe(detail)
            if result ~= "ok" then
                return result, text
            end
            -- THE SWAP IS THE EVIDENCE, not the verb's own word.  A verb that
            -- armed nothing, sent nothing and answered ok off a chat line
            -- that was already on screen still leaves the garlic in the
            -- backpack and no powder in it, and that is what this reads.
            local wait = verb("inv", "await")
            if wait then
                wait(USE_ITEM_MADE, 1, 5)
            end
            local made_state, made = count(USE_ITEM_MADE)
            local left_state, left = count(USE_ITEM_TARGET)
            if made_state ~= "ok" or not is_number(made) or made < 1 then
                return "hollow", "answered ok but " .. USE_ITEM_MADE .. " reads "
                    .. describe(made) .. " (" .. tostring(made_state) .. ") -- " .. text
            end
            if left_state == "ok" and is_number(left) and left >= have then
                return "hollow", "answered ok but " .. USE_ITEM_TARGET
                    .. " is still " .. describe(left) .. " in the backpack -- " .. text
            end
            return "ok", text .. " [" .. USE_ITEM_TARGET .. " " .. describe(have)
                .. "->" .. describe(left) .. ", " .. USE_ITEM_MADE .. " ->"
                .. describe(made) .. "]"
        end)

        -- ------------------------------------------------- phase 4: ui

        step("ui.widget", function()
            local fn = verb("ui", "widget")
            if not fn then return missing("ui", "widget") end
            local result, detail = fn(INVENTORY_ITEMS_COMPONENT)
            if result == "ok" then
                inventory_widget = detail
            end
            return answered(result, detail, INVENTORY_ITEMS_COMPONENT .. " -> ",
                function(value) return value ~= nil end,
                "no component came back to invoke")
        end)

        step("ui.invoke", function()
            local fn = verb("ui", "invoke")
            if not fn then return missing("ui", "invoke") end
            if inventory_widget == nil then
                return "no_subject", "ui.widget(" .. INVENTORY_ITEMS_COMPONENT .. ") resolved nothing to invoke"
            end
            local result, detail = fn(inventory_widget, 1)
            return result, "op 1 -> " .. describe(detail)
        end)

        step("ui.tab", function()
            local fn = verb("ui", "tab")
            if not fn then return missing("ui", "tab") end
            -- The name a quest author would write.  ui.tab has no name ->
            -- number table (ui.lua:84-96); passing a bare number instead would
            -- hide that behind the test's own knowledge.
            local result, detail = fn(INVENTORY_INTERFACE)
            return result, INVENTORY_INTERFACE .. " -> " .. describe(detail)
        end)

        step("ui.await_open", function()
            local fn = verb("ui", "await_open")
            if not fn then return missing("ui", "await_open") end
            local result, detail = fn(INVENTORY_INTERFACE, 4)
            return result, INVENTORY_INTERFACE .. " -> " .. describe(detail)
        end)

        step("key", function()
            local fn = verb("key")
            if not fn then return missing("key") end
            local result, detail = fn("enter")
            return result, "enter press+release -> " .. describe(detail)
        end)

        step("text", function()
            local fn = verb("text")
            if not fn then return missing("text") end
            local result, detail = fn("conformance")
            return result, describe(detail)
        end)

        -- ------------------------------- phase 5: the objectbox dialogue

        -- setup: let the player go idle before the dialogue phase.  Both
        -- ::objbox and the cook's quest-start run behind `p_finduid(uid)`, a
        -- PROTECTED access: a player still walking off the pointer phase's
        -- clicks silently gets no dialogue at all, which shows up as four
        -- chat verbs flipping between runs rather than as anything naming a
        -- busy player.
        stage(function()
            settle(6)
        end)

        step("ui.open", function()
            local fn = verb("ui", "open")
            if not fn then return missing("ui", "open") end
            local result, detail = fn(OBJECTBOX_INTERFACE, "::objbox " .. OBJ_SYMBOL .. " 250")
            return result, OBJECTBOX_INTERFACE .. " via ::objbox -> " .. describe(detail)
        end)

        step("ui.is_modal", function()
            local fn = verb("ui", "is_modal")
            if not fn then return missing("ui", "is_modal") end
            local result, detail = fn()
            -- ::objbox is up, and an objectbox IS modal: "false" here is the
            -- reader answering from nothing.
            return answered(result, detail, "", equals(true),
                "the objectbox opened by the row above is modal")
        end)

        step("chat.kind", function()
            local fn = verb("chat", "kind")
            if not fn then return missing("chat", "kind") end
            local kind = fn()
            if kind == "objbox" then
                return "ok", "objbox"
            end
            return "refused", "expected objbox with ::objbox up, got " .. describe(kind)
        end)

        step("chat.item", function()
            local fn = verb("chat", "item")
            if not fn then return missing("chat", "item") end
            local result, detail = fn()
            return answered(result, detail, "",
                function(value) return value ~= nil end,
                "the objectbox is showing an obj")
        end)

        step("chat.expect_item", function()
            local fn = verb("chat", "expect_item")
            if not fn then return missing("chat", "expect_item") end
            local result, detail = fn(OBJ_SYMBOL)
            return result, OBJ_SYMBOL .. " -> " .. describe(detail)
        end)

        step("chat.text", function()
            local fn = verb("chat", "text")
            if not fn then return missing("chat", "text") end
            local result, detail = fn()
            return answered(result, detail, "", is_text, "the page read back empty")
        end)

        step("chat.expect_text", function()
            local fn = verb("chat", "expect_text")
            if not fn then return missing("chat", "expect_text") end
            local result, detail = fn(OBJBOX_TEXT_FRAGMENT)
            return result, "'" .. OBJBOX_TEXT_FRAGMENT .. "' -> " .. describe(detail)
        end)

        step("chat.continue_", function()
            local fn = verb("chat", "continue_")
            if not fn then return missing("chat", "continue_") end
            local result, detail = fn()
            return result, describe(detail)
        end)

        step("ui.await_close", function()
            local fn = verb("ui", "await_close")
            if not fn then return missing("ui", "await_close") end
            local result, detail = fn(OBJECTBOX_INTERFACE, 6)
            return result, OBJECTBOX_INTERFACE .. " after chat.continue_ -> " .. describe(detail)
        end)

        -- ------------------------------ phase 6: a dialogue with choices

        -- setup: the cook's quest-start dialogue -- an npc page (a head and a
        -- name) that runs on into a chatmenu.
        --
        -- NOT ::cookbmp_choice: that debugproc jumps straight to
        -- @cooks_assistant_start, which opens with ~p_choice4(...) -- a bare
        -- options menu with no head or name behind it at all
        -- (quest_cook.rs2:41-42). chat.head/chat.name/chat.expect_head then
        -- have nothing to read and answered unsupported/timeout every run.
        -- ::cookbmp_talk drives the real Talk-to path instead (p_opnpc(1) ->
        -- [opnpc1,cook], quest_cook.rs2:11-20): for a fresh %cookquest it
        -- shows `~chatnpc_anim(^chat_sad, "What am I to do?")` -- a genuine
        -- chat_left/right head+name+text page -- FIRST, and only then falls
        -- into the same @cooks_assistant_start chatmenu this stage always
        -- meant to reach.
        stage(function()
            settle(4)
            setup_cheat("::cookbmp_talk")
            settle(4)
        end)

        step("chat.head", function()
            local fn = verb("chat", "head")
            if not fn then return missing("chat", "head") end
            local result, detail = fn()
            return answered(result, detail, "",
                function(value) return value ~= nil end,
                "no head came back from a chathead page")
        end)

        step("chat.name", function()
            local fn = verb("chat", "name")
            if not fn then return missing("chat", "name") end
            local result, detail = fn()
            return answered(result, detail, "", is_text,
                "a chathead page names its speaker")
        end)

        step("chat.expect_head", function()
            local fn = verb("chat", "expect_head")
            if not fn then return missing("chat", "expect_head") end
            local result, detail = fn(COOK_SYMBOL)
            return result, COOK_SYMBOL .. " -> " .. describe(detail)
        end)

        step("chat.drain", function()
            local fn = verb("chat", "drain")
            if not fn then return missing("chat", "drain") end
            local result, detail = fn({ stop_at = "options", max_pages = 12 })
            return result, "stop_at=options -> " .. describe(detail)
        end)

        step("chat.options", function()
            local fn = verb("chat", "options")
            if not fn then return missing("chat", "options") end
            local result, detail = fn()
            return answered(result, detail, "", field(1, is_text),
                "an options menu with no first row is not a menu")
        end)

        step("chat.options_title", function()
            local fn = verb("chat", "options_title")
            if not fn then return missing("chat", "options_title") end
            local result, detail = fn()
            return answered(result, detail, "", is_text, "the title came back empty")
        end)

        step("chat.choose", function()
            local fn = verb("chat", "choose")
            if not fn then return missing("chat", "choose") end
            local result, detail = fn(1)
            return result, "row 1 -> " .. describe(detail)
        end)

        step("chat.close", function()
            local fn = verb("chat", "close")
            if not fn then return missing("chat", "close") end
            local result, detail = fn()
            return result, describe(detail)
        end)

        -- setup: the same Talk-to page again, for the one verb that walks a
        -- whole conversation instead of a page.  %cookquest is still
        -- ^cook_not_started here -- phase 6 answered the p_choice4 but never
        -- the p_choice2 behind it, and only that second answer starts the
        -- quest (quest_cook.rs2, @cooks_assistant_whats_wrong) -- so
        -- [opnpc1,cook] opens on "What am I to do?" exactly as it did above.
        stage(function()
            settle(2)
            setup_cheat("::cookbmp_talk")
            settle(4)
        end)

        step("chat.play", function()
            local fn = verb("chat", "play")
            if not fn then return missing("chat", "play") end
            -- Three entry shapes in one call: a text-checked npc page, a bare
            -- options check, and the "/lua pattern/" row selector (only
            -- "What's wrong?" of the four rows contains "wrong").
            local result, detail = fn({ "npc:What am I to do", "options", "choose:/wrong/" })
            if result ~= "ok" then
                return result, describe(detail)
            end
            -- play answers a bare ok, so the row reads the page it left
            -- behind: choosing "What's wrong?" makes the PLAYER say it
            -- (~chatplayer_anim), so a play that clicked nothing -- or
            -- clicked the wrong row, which answers "No! Go away!" from the
            -- npc -- cannot reach a player page.
            local kind = verb("chat", "kind")
            local page = kind and kind() or "missing"
            if page ~= "player" then
                return "hollow", "play answered ok but the page it left is " .. describe(page)
                    .. ", not the player's own reply to the row it was told to choose"
            end
            return "ok", "npc page -> options -> /wrong/ -> the player's reply page"
        end)

        -- The cook is dismissed again, for the same reason phase 6 dismissed
        -- him: a quantity or a name prompt opened on top of a parked dialogue
        -- is arguing with a script that already owns the player.
        stage(function()
            local close = verb("chat", "close")
            if close then
                close()
            end
            settle(3)
        end)

        -- ------------------------- phase 6b: the two entry prompts
        --
        -- chat.close above is what makes these reachable: a quantity or a name
        -- prompt is a meslayer mode, and opening one on top of the cook's
        -- still-parked dialogue would be arguing with a script that already
        -- owns the player.  So the cook is dismissed first, and only then is
        -- each prompt opened on its own.
        --
        -- Every prompt this content pack opens by itself sits behind a bank, a
        -- shop, a clue challenge, a minigame, the friends list or a sailing
        -- dock -- all of them several interfaces away from this Lumbridge
        -- fixture, which is why three earlier passes left both verbs recording
        -- `unsupported` rather than reach one.  The two debugprocs below park
        -- on `p_countdialog` / `p_namedialog`, the exact commands those real
        -- openers park on, so each verb still answers a real meslayer through
        -- the real text field and a real enter key -- only the reason for
        -- opening the prompt is a test's.
        -- (OSRS-Content .../interface_chat/scripts/chat_test_prompts.rs2.)
        stage(function()
            settle(2)
            setup_cheat("::conform_test_countprompt")
            settle(3)
        end)

        -- Both rows below re-read the chat log afterwards, because a closed
        -- prompt on its own would also be what a verb that merely dismissed
        -- the box looked like.  Each debugproc echoes what it was actually
        -- handed (`mes("conform_test_countprompt: <tostring(last_int)>")`), so
        -- the echo is the proof that the typed number and the typed name
        -- travelled all the way to the server and came back as the script's
        -- own `last_int` / `last_string`.
        local function prompt_echoed(result, detail, echo)
            if result ~= "ok" then
                return result, describe(detail)
            end
            local expect = verb("msg", "expect")
            if not expect then return missing("msg", "expect") end
            -- The verb's own await ends when the meslayer mode leaves the
            -- prompt, which the client knows a tick before the resumed script's
            -- reply can have come back down the wire.
            settle(3)
            local seen, why = expect(echo)
            if seen ~= "ok" then
                return "hollow", "the prompt closed but the server never echoed '"
                    .. echo .. "' -- " .. describe(why)
            end
            return "ok", "answered, and the server echoed '" .. echo .. "'"
        end

        step("chat.count", function()
            local fn = verb("chat", "count")
            if not fn then return missing("chat", "count") end
            local result, detail = fn(1)
            return prompt_echoed(result, detail, "conform_test_countprompt: 1")
        end)

        stage(function()
            settle(3)
            setup_cheat("::conform_test_nameprompt")
            settle(3)
        end)

        step("chat.name_entry", function()
            local fn = verb("chat", "name_entry")
            if not fn then return missing("chat", "name_entry") end
            local result, detail = fn("conformance")
            return prompt_echoed(result, detail, "conform_test_nameprompt: conformance")
        end)

        -- ------------------------ phase 7: the scroll and the levelup box

        -- setup: a real questscroll on screen.  Nothing else in this harness
        -- ever opens one, so scroll.title/rewards/close answered
        -- not_visible/no_row every run and said nothing about the reader
        -- behind them.  ::cookbmp_reward sets %cookquest = ^cook_complete and
        -- calls ~quest_complete_rewards directly
        -- (OSRS-Content/osrs239-content/server/scripts/quests/quest_cook/
        -- scripts/quest_cook.rs2:297-300), which is the same completion scroll
        -- the quest itself puts up -- with no inventory or dialogue
        -- prerequisite of its own, so it works from whatever state phase 6
        -- left behind.
        stage(function()
            settle(2)
            setup_cheat("::cookbmp_reward")
            settle(4)
        end)

        step("scroll.title", function()
            local fn = verb("scroll", "title")
            if not fn then return missing("scroll", "title") end
            local result, detail = fn()
            -- scroll.title answers a TABLE, `{name, points}`
            -- (QUEST_DRIVER_PLAN.md S5: "scroll.title | () -> result,
            -- {name,points}"), not a bare string -- this row used to test the
            -- whole table with is_text, which no correct answer could ever
            -- satisfy. The quest's NAME is the thing only a real scroll can
            -- carry, so that is what is asserted.
            return answered(result, detail, "", field("name", is_text),
                "the quest name came back empty")
        end)

        step("scroll.rewards", function()
            local fn = verb("scroll", "rewards")
            if not fn then return missing("scroll", "rewards") end
            local result, detail = fn()
            -- `{lines, icon}` (QUEST_DRIVER_PLAN.md S5), so the first line is
            -- detail.lines[1] -- not detail[1], which is always nil and made
            -- this row unsatisfiable by construction.
            return answered(result, detail, "", field("lines", field(1, is_text)),
                "a reward scroll with no first line is not a scroll")
        end)

        step("scroll.reward_xp", function()
            local fn = verb("scroll", "reward_xp")
            if not fn then return missing("scroll", "reward_xp") end
            local result, detail = fn(STAT_SYMBOL)
            -- The number is knowable: this scroll's own reward string is
            -- "300 Cooking XP|..." (quest_cook.rs2:158), so a parser that
            -- answered ok with the wrong line -- or with the quest-point
            -- line's own number, which ~quest_points_reward prepends -- is
            -- caught here rather than being read as a pass.
            return answered(result, detail, STAT_SYMBOL .. " -> ",
                equals(QUEST_REWARD_XP),
                "this scroll awards " .. QUEST_REWARD_XP .. " " .. STAT_SYMBOL .. " xp")
        end)

        step("scroll.close", function()
            local fn = verb("scroll", "close")
            if not fn then return missing("scroll", "close") end
            local result, detail = fn()
            return result, describe(detail)
        end)

        -- `levelup_display` has no content opener anywhere in this pack: the
        -- server never puts the box up, so there is nothing on screen for
        -- either verb to read.  That is a decision, not a gap the driver can
        -- close -- docs/QUEST_DRIVER_REMAINING.md's "Out of scope, by
        -- decision" holds it until plan U16 lands the content, and its phase
        -- B2 says in so many words that "those two verbs record `unsupported`
        -- with that reason ... and the harness expects that".
        --
        -- So these two rows assert the REFUSAL rather than tolerating it: the
        -- documented word passes, and everything else -- most of all a verb
        -- that starts answering `ok` with no box on screen -- fails.  When U16
        -- lands the content, the row goes red on the first day the box opens,
        -- which is exactly when it should be rewritten to read it.
        local function refused_until_u16(result, detail)
            local text = describe(detail)
            if result == "unsupported" then
                return "ok", "refused as planned (plan U16, no content opener) -- " .. text
            end
            if result == "ok" then
                return "hollow", "answered ok with no levelup box on screen -- " .. text
            end
            return result, text
        end

        step("levelup.skill", function()
            local fn = verb("levelup", "skill")
            if not fn then return missing("levelup", "skill") end
            return refused_until_u16(fn())
        end)

        step("levelup.continue_", function()
            local fn = verb("levelup", "continue_")
            if not fn then return missing("levelup", "continue_") end
            return refused_until_u16(fn())
        end)

        -- ---------------- phase 9: the quest binding and the journal
        --
        -- Last, and after the scroll, for three reasons.  `::setvar cookquest`
        -- changes which branch [opnpc1,cook] takes, so it cannot run before
        -- phase 6's dialogue rows.  `::cookbmp_reward` has to put a SECOND
        -- scroll up for quest.expect_complete's own scroll row to read a
        -- title off, because phase 7 read the first one and then closed it.
        -- And player.teleport moves the player out of Lumbridge, which every
        -- npc/loc/obj subject above depends on and nothing below does.

        -- setup: the quest staged at ^cook_started.  A LADDER cheat, not a
        -- debugproc -- phase 1's one cheat path is what makes `::setvar`
        -- reachable from here at all, and this row set is the reason it
        -- exists.
        stage(function()
            setup_cheat("::setvar " .. QUEST_VARP .. " ^cook_started")
        end)

        step("var.await_server", function()
            local fn = verb("var", "await_server")
            if not fn then return missing("var", "await_server") end
            local result, detail = fn(QUEST_VARP, QUEST_STARTED, 10)
            if result ~= "ok" then
                return result, QUEST_VARP .. " == " .. QUEST_STARTED .. " -> " .. describe(detail)
            end
            -- The await answers a bare ok, so the row reads the server's own
            -- copy back: an await that resolved without the value ever
            -- landing is exactly the hollow ok this harness is pointed at.
            local read = verb("var", "server")
            local state, value = "missing", nil
            if read then state, value = read(QUEST_VARP) end
            if state ~= "ok" or value ~= QUEST_STARTED then
                return "hollow", "await_server answered ok but var.server(" .. QUEST_VARP
                    .. ") reads " .. describe(value) .. " (" .. tostring(state) .. ")"
            end
            return "ok", QUEST_VARP .. " reached " .. QUEST_STARTED
                .. " on the server within 10 ticks of ::setvar"
        end)

        step("quest.bind", function()
            local fn = verb("quest", "bind")
            if not fn then return missing("quest", "bind") end
            local result, detail = fn({
                varp = QUEST_VARP,
                constants = {
                    not_started = QUEST_NOT_STARTED,
                    started = QUEST_STARTED,
                    complete = QUEST_COMPLETE,
                },
                display = QUEST_DISPLAY,
                points = QUEST_POINTS,
            })
            if result ~= "ok" then
                return result, describe(detail)
            end
            -- bind touches no world and answers a bare ok, so the row reads
            -- what it left behind: the binding itself, and the %qp reading it
            -- must have taken NOW for quest.points to have a baseline to
            -- measure the award against later.
            local bound = is_table(t.quest) and t.quest._bound or nil
            if not is_table(bound) or bound.varp ~= QUEST_VARP then
                return "hollow", "bind answered ok but nothing was bound -- " .. describe(bound)
            end
            if not is_number(bound.qp_before) then
                return "hollow", "bind answered ok but took no %qp reading to measure the "
                    .. "award against -- " .. describe(bound.qp_before)
                    .. " (" .. tostring(bound.qp_before_result) .. ")"
            end
            return "ok", QUEST_VARP .. " bound; %qp at bind time = " .. describe(bound.qp_before)
        end)

        step("quest.stage", function()
            local fn = verb("quest", "stage")
            if not fn then return missing("quest", "stage") end
            local result, detail = fn()
            return answered(result, detail, QUEST_VARP .. " -> ",
                equals(QUEST_STARTED), "the ::setvar above staged ^cook_started")
        end)

        step("quest.expect_stage", function()
            local fn = verb("quest", "expect_stage")
            if not fn then return missing("quest", "expect_stage") end
            -- By NAME, through the bind's constants table -- the spelling a
            -- generated quest test uses.  A mismatch answers refused naming
            -- the side that disagreed, so an ok here is client AND server.
            local result, detail = fn("started")
            return answered(result, detail, '"started" -> ',
                equals(QUEST_STARTED), "the stage the ::setvar staged")
        end)

        step("ui.journal_open", function()
            local fn = verb("ui", "journal_open")
            if not fn then return missing("ui", "journal_open") end
            local result, detail = fn(QUEST_DISPLAY)
            -- The real click path: the quest tab, the quest-list strip icon,
            -- then op 2 on the row whose own rendered text is this name.  A
            -- verb that mounted nothing answers not_visible; one that mounted
            -- the page before its text landed answers a title and no lines,
            -- which is what the hollow check below refuses.
            return answered(result, detail, QUEST_DISPLAY .. " -> ",
                function(value)
                    return is_table(value) and is_text(value.title)
                        and is_number(value.line_count) and value.line_count >= 1
                end,
                "an opened journal carries a title and at least one line")
        end)

        step("ui.journal_read", function()
            local fn = verb("ui", "journal_read")
            if not fn then return missing("ui", "journal_read") end
            -- The journal ui.journal_open just opened, read again with no
            -- click of its own.
            local result, detail = fn()
            return answered(result, detail, "", field("title", is_text),
                "a mounted journal carries its quest's title")
        end)

        step("ui.journal_close", function()
            local fn = verb("ui", "journal_close")
            if not fn then return missing("ui", "journal_close") end
            local result, detail = fn()
            if result ~= "ok" then
                return result, describe(detail)
            end
            -- Closed means GONE: the reader is asked again, and a journal
            -- that still reads ok is a close that only said so.
            local read = verb("ui", "journal_read")
            local state, again = "missing", nil
            if read then state, again = read() end
            if state == "ok" then
                return "hollow", "journal_close answered ok but ui.journal_read still reads "
                    .. describe(again)
            end
            return "ok", describe(detail) .. "; ui.journal_read now answers " .. tostring(state)
        end)

        -- setup: the quest completed for real, scroll and all.  ::cookbmp_reward
        -- sets %cookquest = ^cook_complete and calls ~quest_complete_rewards,
        -- which awards the quest's points (~quest_award_points) and paints the
        -- completion scroll -- so all four of expect_complete's rows have a
        -- live subject, and the point award happened AFTER quest.bind took its
        -- %qp baseline.
        stage(function()
            setup_cheat("::cookbmp_reward")
            settle(6)
        end)

        step("quest.expect_complete", function()
            local fn = verb("quest", "expect_complete")
            if not fn then return missing("quest", "expect_complete") end
            -- This verb writes FOUR ledger rows of its own -- quest.varp_complete,
            -- quest.scroll_title, quest.points, quest.journal -- and those rows
            -- ARE the evidence; it answers ok only when every one of them
            -- passed, and `refused` naming the rows that did not.  So this row
            -- forwards, and the four rows above it in the ledger say why.
            local result, detail = fn()
            return result, describe(detail)
        end)

        -- LAST, because it leaves the player in another city.  It was
        -- written first in this phase and moved here: a teleport is a region
        -- load, and the journal rows that followed it then spent their whole
        -- budget waiting for a client busy building Varrock (the first
        -- ui.journal_open timed out at 15 ticks with no quest-list rows,
        -- while the same call later in the same run answered in 0).  The
        -- only row after it that reads the world is player.goto_tile, which
        -- moves the player once more and then reads back the tile it landed
        -- on -- its own subject, depending on nothing above.
        step("player.teleport", function()
            local fn = verb("player", "teleport")
            if not fn then return missing("player", "teleport") end
            local result, detail = fn("varrock")
            -- The landing tile IS the answer.  A teleport that did not move
            -- the player answers `timeout` naming the tile it never left (the
            -- verb refuses to call standing still a success), so the row only
            -- has to confirm that a tile came back with the ok.
            return answered(result, detail, "varrock -> ", is_text,
                "an ok teleport names the tile it landed on")
        end)

        -- player.goto_tile, next to the teleport and last for the same
        -- reason: it moves the player again, and nothing below reads the
        -- world.  It is also the pair's other half -- teleport(name) spends
        -- a NAME out of tele_destinations.rs2, goto_tile spends an absolute
        -- tile (the WorldPoint Quest Helper prints for every quest step),
        -- which is the one Lumbridge's fixture cannot reach on foot.
        step("player.goto_tile", function()
            local fn = verb("player", "goto_tile")
            if not fn then return missing("player", "goto_tile") end
            local result, detail = fn(GOTO_TILE_X, GOTO_TILE_Z, GOTO_TILE_LEVEL)
            local wanted = GOTO_TILE_X .. "," .. GOTO_TILE_Z .. "," .. GOTO_TILE_LEVEL
            if result ~= "ok" then
                return result, wanted .. " -> " .. describe(detail)
            end
            -- The verb's own detail is not the evidence; the TILE is.  A
            -- goto_tile that answered ok on its own say-so while the player
            -- never left Varrock is exactly the hollow ok this harness is
            -- pointed at, so the row reads world.tile back and grades the
            -- landing itself: Chebyshev 1 on x/z, the plane exact.
            local read = verb("world", "tile")
            local state, tile = "missing", nil
            if read then state, tile = read() end
            if state ~= "ok" or type(tile) ~= "table" then
                return "hollow", "answered ok but world.tile answered "
                    .. describe(state) .. " -- " .. wanted .. " -> " .. describe(detail)
            end
            local dx = math.abs((tile.x or -9999) - GOTO_TILE_X)
            local dz = math.abs((tile.z or -9999) - GOTO_TILE_Z)
            if math.max(dx, dz) > 1 or tile.level ~= GOTO_TILE_LEVEL then
                return "hollow", "answered ok but the player stands at "
                    .. describe(tile.x) .. "," .. describe(tile.z) .. ","
                    .. describe(tile.level) .. " -- " .. wanted .. " -> " .. describe(detail)
            end
            return "ok", wanted .. " -> " .. describe(detail)
        end)


        -- --------------- phase 7b: the fight the phase 6 seams landed
        --
        -- Last of the acting rows, and after player.goto_tile, because these
        -- two KILL the Man every pointer and npc row above points at.
        -- Nothing below this block reads the world.
        --
        -- setup: back to the Man, and armed for a fight that ends inside a
        -- deadline.  The teleport is the same one the world phase opened
        -- with; player.goto_tile left the player upstairs in the castle,
        -- where there is no npc to attack at all.
        stage(function()
            setup_cheat("::tele lumbridge")
            settle(6)
            setup_cheat("::setlevel attack " .. COMBAT_LEVEL)
            setup_cheat("::setlevel strength " .. COMBAT_LEVEL)
            setup_cheat("::setlevel hitpoints " .. COMBAT_LEVEL)
            setup_cheat("::give " .. COMBAT_WEAPON .. " 1")
            settle(4)
            local equip = verb("player", "equip")
            if equip then
                equip(COMBAT_WEAPON)
            end
            settle(4)
        end)

        -- The slot of the npc these two rows fight, read once before the
        -- attack: a SYMBOL is not a target (falador_gardener has three spawn
        -- rows, and Lumbridge has a courtyard full of Men), so "the one we
        -- fought is gone" can only be asked of the slot, never of the name.
        local combat_slot = nil

        step("player.attack", function()
            local fn = verb("player", "attack")
            if not fn then return missing("player", "attack") end
            local nearest = verb("npc", "nearest")
            if not nearest then
                return "no_subject", "t.npc.nearest is not a function, so no slot can be read"
            end
            local before_state, before = nearest(NPC_SYMBOL, 5)
            if before_state ~= "ok" or not is_table(before) then
                return "no_subject", NPC_SYMBOL .. " is not within five tiles ("
                    .. tostring(before_state) .. ")"
            end
            combat_slot = before.slot
            -- Twenty ticks, not the verb's ten: the click is issued straight
            -- after the equip above, and an Attack click made while another
            -- action is still in flight buys nothing for its first few ticks
            -- (player.attack's own banner, measured in build/quest_gate/
            -- q3proof).
            local result, detail = fn(NPC_SYMBOL, COMBAT_ATTACK_OP, 20)
            local text = NPC_SYMBOL .. " op" .. COMBAT_ATTACK_OP .. " -> " .. describe(detail)
            if result ~= "ok" then
                return result, text
            end
            -- THE BAR IS THE EVIDENCE.  A client is never told an npc's
            -- hitpoints -- the server sends a HEADBAR fill out of the
            -- healthbar type's own width, and it sends the first one only
            -- once something has hit the npc.  So `health_ratio` rising off
            -- -1 ("no bar has ever been sent for this npc") is the world
            -- saying the swing landed, and is a fact this row can read that
            -- the verb's own answer cannot fabricate.
            local after_state, after = nearest(NPC_SYMBOL, 5)
            if after_state ~= "ok" or not is_table(after) then
                -- Gone inside the attack's own settle is a one-shot kill,
                -- which is the strongest answer there is.
                return "ok", text .. " [the npc left the pool inside the settle]"
            end
            if not is_number(after.health_ratio) or after.health_ratio < 0 then
                return "hollow", "answered ok but the npc still carries no health bar, "
                    .. "so nothing has hit it -- " .. text
            end
            return "ok", text .. " [health bar " .. describe(after.health_ratio)
                .. "/" .. describe(after.health_scale) .. "]"
        end)

        step("npc.await_dead", function()
            local fn = verb("npc", "await_dead")
            if not fn then return missing("npc", "await_dead") end
            if combat_slot == nil then
                return "no_subject", "player.attack read no npc slot to finish off"
            end
            local result, detail = fn(NPC_SYMBOL, 60)
            local text = NPC_SYMBOL .. " -> " .. describe(detail)
            if result ~= "ok" then
                return result, text
            end
            -- The npc we FOUGHT, by slot.  `nearest` answering ok again is
            -- not a failure: Lumbridge has several Men and the next one is
            -- only ever a few tiles away -- it is the same slot coming back
            -- that would mean the verb called a living npc dead.
            local nearest = verb("npc", "nearest")
            local state, row = "missing", nil
            if nearest then state, row = nearest(NPC_SYMBOL, 5) end
            if state == "ok" and is_table(row) and row.slot == combat_slot
                and is_number(row.health_ratio) and row.health_ratio > 0 then
                return "hollow", "answered ok but slot " .. describe(combat_slot)
                    .. " is still in the pool at " .. describe(row.health_ratio)
                    .. "/" .. describe(row.health_scale) .. " -- " .. text
            end
            -- Not "the slot is gone": a corpse stays in the pool for a few
            -- ticks with its bar at 0, which is exactly what await_dead
            -- resolves on, so the row names the READING rather than claiming
            -- a disappearance it did not check for.
            return "ok", text .. " [slot " .. describe(combat_slot)
                .. " no longer reads as alive; nearest " .. NPC_SYMBOL .. " now "
                .. (state == "ok" and is_table(row)
                    and ("slot " .. describe(row.slot) .. " at "
                        .. describe(row.health_ratio) .. "/" .. describe(row.health_scale))
                    or tostring(state)) .. "]"
        end)

        -- ------------------------- phase 8: the scheduler's own controls

        step("await", function()
            local fn = verb("await")
            if not fn then return missing("await") end
            local result, detail = fn({
                level = function() return true end,
                note = "conformance.await",
            }, 2)
            return result, "a level predicate true at registration -> " .. describe(detail)
        end)

        step("ok", function()
            local fn = verb("ok")
            if not fn then return missing("ok") end
            local yes = fn("ok")
            local no = fn("timeout")
            if yes == true and no == false then
                return "ok", 'ok("ok")=true, ok("timeout")=false'
            end
            return "refused", 'ok("ok")=' .. describe(yes) .. ' ok("timeout")=' .. describe(no)
        end)

        step("fail", function()
            local fn = verb("fail")
            if not fn then return missing("fail") end
            local failed, detail = fn("timeout", "why")
            local not_failed = fn("ok")
            if failed == true and detail == "why" and not_failed == false then
                return "ok", 'fail("timeout","why")=(true,"why"), fail("ok")=false'
            end
            return "refused", "fail(timeout,why)=(" .. describe(failed) .. "," .. describe(detail)
                .. ") fail(ok)=" .. describe(not_failed)
        end)

        step("note", function()
            local fn = verb("note")
            if not fn then return missing("note") end
            fn(NOTE_PROBE)
            -- t.note folds into the NEXT row's detail, which is this one.
            -- tools/quest_gate/conformance.py fails this row unless the probe
            -- is in the detail column.
            return "ok", "a note was posted and must appear in this detail"
        end)

        -- t.exec and t.check both WRITE THEIR OWN ROW (and take their own
        -- screenshot) instead of answering a result for this harness to
        -- record, so each one is called with its own verb name as the row
        -- name and the step returns nil -- the same shape t.step's row below
        -- has used since this harness was written.  The verdict in the ledger
        -- is the one the verb itself decided, from a real answer: an auditor
        -- reading the row sees the wrapped verb's own detail in it.
        step("exec", function()
            local fn = verb("exec")
            if not fn then return missing("exec") end
            local wrapped = verb("var", "varp")
            if not wrapped then
                return "no_subject", "t.exec wraps a verb and var.varp is not one"
            end
            -- A verb with a knowable answer: the fixture pins `tutorial` at
            -- VARP_VALUE, so the row t.exec writes carries that number as its
            -- detail.  t.exec's own hollow rule (ok with a nil detail is FAIL)
            -- is what makes that detail load-bearing rather than decoration.
            fn("exec", wrapped, VARP_SYMBOL)
            return nil
        end)

        step("check", function()
            local fn = verb("check")
            if not fn then return missing("check") end
            local read = verb("var", "varp")
            if not read then
                return "no_subject", "t.check needs a reading to assert and var.varp is not a function"
            end
            local state, value = read(VARP_SYMBOL)
            fn("check", state == "ok" and value == VARP_VALUE,
                VARP_SYMBOL .. " -> " .. describe(value) .. " (" .. tostring(state)
                .. "), the fixture pins it at " .. VARP_VALUE)
            return nil
        end)

        step("step", function()
            local fn = verb("step")
            if not fn then return missing("step") end
            fn("step", "PASS", "this row was written by t.step itself, not by t.expect")
            return nil
        end)

        step("expect", function()
            local fn = verb("expect")
            if not fn then return missing("expect") end
            -- Every other row in this ledger was written by t.expect; a row
            -- named t.expect, in a ledger with a header, IS the evidence.
            return "ok", "wrote every other row in this ledger"
        end)

        step("finish", function()
            local fn = verb("finish")
            if not fn then return missing("finish") end
            -- finish ENDS the run, so it is called after the loop.  The
            -- ledger's SUMMARY row (exit=) and the process exit code are what
            -- tools/quest_gate/conformance.py re-grades this row against.
            return "ok", "called after this row; SUMMARY exit= and the process exit code are the evidence"
        end)

        step("blocked", function()
            local fn = verb("blocked")
            if not fn then return missing("blocked") end
            -- t.blocked writes a BLOCKED row and then calls t.finish(0), so
            -- it ENDS THE RUN exactly as t.finish does and is called after
            -- the loop for the same reason -- it is this harness's own
            -- terminator (see the tail below).  Like t.finish's row, this one
            -- is re-graded from outside the coroutine by
            -- tools/quest_gate/conformance.py, against the evidence Lua
            -- cannot read: a ledger row named `blocked` whose verdict really
            -- is BLOCKED, and a SUMMARY that counts it in its own `blocked=`
            -- bucket rather than as a failure.  THIS row is named `blocked`
            -- too (the controls sit on the root of `t`, so the verb is just
            -- `blocked`), so the ledger carries two rows under that name and
            -- only the second one -- the BLOCKED one t.blocked wrote itself
            -- -- is the evidence; conformance.py tracks it separately for
            -- exactly that reason.
            return "ok", "called after this row; the ledger's BLOCKED row and SUMMARY's "
                .. "blocked= bucket are the evidence"
        end)

        -- ------------------------------------------------------ the run

        if verb_count ~= VERB_COUNT then
            local recorder = verb("step")
            if recorder then
                recorder("conformance-plan", "FAIL",
                    "the plan holds " .. verb_count .. " verbs, the driver exposes "
                    .. VERB_COUNT .. " -- run tools/quest_gate/verb_list.py to see which")
            end
            local stop = verb("finish")
            if stop then
                stop(1)
            end
            return
        end

        local record = verb("expect")
        for i = 1, #PLAN do
            local entry = PLAN[i]
            if entry.stage then
                entry.stage()
            elseif not SKIP[entry.name] then
                local result, detail = entry.call()
                if result ~= nil and record then
                    -- The ledger's verdict column is only PASS/FAIL; the verb's
                    -- own result word (timeout, not_found, refused, covered,
                    -- no_row, not_visible, closed, unsupported) is the half that
                    -- says WHY, so it leads the detail.
                    record(entry.name, result, "[" .. tostring(result) .. "] " .. (detail or ""))
                end
            end
        end

        -- The terminator is t.blocked, not t.finish: it writes the BLOCKED
        -- row its own conformance row above promises and then calls
        -- t.finish(0) itself, so one call ends the run, proves both verbs,
        -- and still leaves the exit=0 SUMMARY that t.finish's row is graded
        -- against.  A driver without t.blocked finishes the old way.
        local blocked = verb("blocked")
        if blocked then
            blocked("t.blocked is this harness's own terminator -- the row it wrote and "
                .. "SUMMARY's blocked= bucket are what its conformance row is graded on")
        else
            local stop = verb("finish")
            if stop then
                stop(0)
            end
        end
    end,
}
