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
-- 109 verbs, one row each.  tools/quest_gate/verb_list.py --check reads the
-- `step("<name>", ...)` lines below and the QD.* definitions in
-- script/plugins/quest_driver/*.lua and refuses to agree when they differ, so
-- a verb added to the driver with no row here fails a make gate rather than
-- being quietly never called.  The count is asserted in the harness too, so
-- editing this file alone cannot drift either.
-- @verb-count 109
-- ---------------------------------------------------------------------------
--
-- SEAM ROWS -- `seam("seam.<name>", ...)`, counted separately.
--
-- A verb row proves a verb.  A seam row proves a BEHAVIOUR that lives under
-- the verbs, in the C the driver calls, where the verb above it answers the
-- same word whether the seam works or not.  The 2026-09-20 seam passes landed
-- five of those, and every one of them had already cost real quests before
-- anybody looked below the verb:
--
--   * use_on's far-side retry RE-ARMS the held item, and a re-arm of an
--     arming that survived sends nothing (api_drive.inv_arm ->
--     DrivePointer_InvArm).  Re-arming through inv_op instead encodes an
--     OPHELDU of the item on itself and leaves nothing armed, after which the
--     retry press lands an ordinary op row: `refused ... pressed 'Examine
--     @cya@Statuette in alcove'` (build/quest_gate/golem row 58).
--   * a press pixel is SEARCHED, not projected: the projection answers a
--     loc's ground centroid and the client's hittest is a per-triangle
--     containment test over the DRAWN model, so the pressed pixel sat 16-96
--     px below the model it was aiming at and the menu carried no row for it
--     -- `covered`, from every camera (build/quest_gate/cog row 23).
--   * a backpack press the CLIENT declines now names the condition it
--     declined on and is pressed again, instead of being reported as a server
--     that ignored the packet: `timeout ... -> 1 left` with no chat line, no
--     effect and not even content's own fallback message
--     (build/quest_gate/makinghistory row 19).
--   * a varp the client's array cannot address at all is read from the
--     embedded server's own copy, because BOTH client-side reads answer
--     not_found for such an id forever (build/quest_gate/rovingelves row 33).
--   * a loc's REACHABLE SIDE is not a fact the driver can read, so a press
--     the server refuses with "I can't reach that!" is re-taken from the
--     loc's other approach tiles -- and that refusal, which lands a tick
--     behind the map_flag the settle resolves on, is no longer graded a PASS
--     (build/quest_gate/seam_reach_p2, biohazard's watchtower).
--   * a chat page that has been CLICKED and not yet answered is still
--     mounted, with its own sentence, drawing "Please wait..." over its
--     continue prompt -- and chat.play graded it as the answer to that
--     click.  Two consecutive pages of the same kind (an npc page after an
--     npc page) made chat.kind() useless as a guard, so Ernest the Chicken
--     failed entry 1 on the PREVIOUS page's text and took five rows with it
--     (build/quest_gate/haunted row 55; fixed, 60/60 in
--     build/quest_gate/haunted_seamproof).
--   * a loc drawn as SEVERAL COPIES is pressed at the copy the standing
--     square serves, not at whichever one the projection's tie-break
--     happened to hand back: the ranking compares the player's tile ORIGIN
--     against a copy's footprint CENTROID, so a copy underfoot ties with
--     both of its neighbours and the scenery pool's order decides.  The
--     same square answered `You stash the garlic in the pipe.` on one run
--     and `I can't reach that!` on the next (build/quest_gate/seam_reach_p1
--     row 12 against build/quest_gate/useon_after row 29).
--   * a DEATH is an outcome.  Mort'ton's shade hunt was killed at four
--     kills, respawned in Lumbridge and went on clicking from ninety tiles
--     away for another thirty-six attempts, and the row it finally wrote
--     blamed the shade population (build/quest_gate/mortton row 19, whose
--     own screenshot is the Lumbridge castle courtyard).  The reading is a
--     chat line, not the hitpoints -- those are refilled within a tick or
--     two of the killing blow, which a verb blocked inside a twenty-tick
--     settle never sees.
--   * an NPC'S OWN SQUARE is stepped off before the press.  The step-off
--     ran for the loc half alone, and the scaffold puts the player inside a
--     stationary npc every time it walks a quest to that npc's own *.spawn
--     row -- `goto_tile` is a teleport.  Enter the Abyss read `covered ...
--     no frame hittested any of 3 pixels around the projected 382,250`,
--     the middle of the viewport, which is where the player's own model is
--     drawn (build/quest_gate/eta_before1 row 18).
--
-- The 2026-09-20 seam pass also fixed three ENGINE seams, and those are NOT
-- rows here because nothing the driver calls can reach them from Lumbridge:
-- a multiloc's trigger is looked up child-then-base (gate: the server
-- selftest and quest_golem's statuette alcove), `db_getfield` answers `null`
-- rather than 0 for a column a row does not state (the herblore brew is
-- members-gated and Lumbridge is not, so the gate is the selftest and
-- quest_mortton's serum), and sscompile types a bare name from the position
-- it sits in (gate: `make -C src test-ssc`, whose
-- test_comparison_and_case_operand_kind fails at HEAD).
--
-- They are ordinary PLAN entries -- same loop, same skip-and-re-run, same
-- `t.expect` grading, and conformance.py scores them beside the verbs -- but
-- they are NOT verbs, so they carry their own count and their names are all
-- prefixed `seam.`, which is how verb_list.py tells the two apart without a
-- hand-maintained list.
--
-- A seam row calls the driver's own PRIVATE helpers (`t.player._arm_held`,
-- `t.drive._hover_onto`, `t.quest._read_content`) on purpose: `t` IS the QD
-- table (core.lua's QD_ROOT), and the thing under test is the layer BELOW the
-- public verb -- calling the public verb would prove the wrong thing, because
-- the public verb is exactly what went on answering plausibly while the seam
-- under it was broken.
-- @seam-count 13
-- ---------------------------------------------------------------------------

local VERB_COUNT = 109
local SEAM_COUNT = 13
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
-- The held op these rows press, and it is NOT op 1: on rev-239 the backpack's
-- IF3 cell carries a client-side on_op hook at op index 1 that is the
-- shift-click-drop chain, so `inv_op(airrune, 1)` drops the stack whatever op
-- 1 is supposed to mean for the obj.  Op 3 carries no such binding, and --
-- being unbound -- is claimed by no script either, so the world's honest
-- answer to it is the engine's "Nothing interesting happens."  The long form
-- of that measurement is on the player.inv_op row below.
local INV_OP_UNCLAIMED = 3
-- Any tab that is not the backpack.  The seam row below needs the backpack's
-- cells NOT painted when it presses, which is the state `ui.tab("inventory")`
-- itself leaves behind for a frame -- see its own banner.
local TAB_AWAY_FROM_BACKPACK = "stats"
-- The seam rows' own backpack subject.  NOT OBJ_SYMBOL: those rows run
-- after player.drop, which empties the air runes, and putting them back is
-- not free -- `::runes` states twenty-two of the backpack's twenty-eight
-- slots, so re-adding one costs phase 7b the room its own `::give` pair
-- needs (measured 2026-09-20: `::give garlic left 0 in the backpack`).  A
-- mind rune comes from the same `::runes 25` the world phase opened with,
-- is never dropped, equipped or used by any row, and -- like the air rune
-- -- is claimed by no script, so the world's honest answer to a held op on
-- it is still the engine's "Nothing interesting happens."
local SEAM_OBJ_SYMBOL = "mindrune"
local VARP_SYMBOL = "tutorial"      -- the fixture pins it (perm scope)
local VARP_VALUE = 1000             -- "tutorial finished" (docs/WORKTREE_SETUP.md)
local VARBIT_SYMBOL = "troll_freed_eadgar"
-- A varp this content pack allocates ABOVE the id the client's varp array can
-- address (pack/varp.alloc 6262 against an all.varp.compack topping out near
-- 5704), so the server never transmits it and both client-side reads answer
-- not_found for the whole run.  The seam row below is the only thing in this
-- harness that can read it at all; if a later cache widens the client's table
-- past 6262 that row says so in its own detail rather than passing quietly.
local HIGH_ID_VARP = "rovingelves_quest"
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
-- The same room's occupant, and the subject of seam.npc_shared_tile: an npc
-- whose *.spawn row IS the tile above, and who does not wander off it.  That
-- is the shape the seam is about -- the scaffold walks a quest to an npc's own
-- spawn coordinate and `goto_tile` is a teleport, so the player lands INSIDE
-- him -- and a wanderer could not prove it, because a step-off it did not
-- make would read the same as one it did.
local STATIONARY_NPC_SYMBOL = "duke_of_lumbridge"
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

-- The shop rows' subject.  The Lumbridge general store, because it is the one
-- shop a fixture character can reach with no quest state at all: its keeper
-- stands at 3211,3247 level 0 six tiles from where `::tele lumbridge` lands,
-- op 3 on him is Trade, and `generalshop1` is the inv his own `~openshop`
-- call names (OSRS-Content/.../shop/*/configs/*.inv).  The inv symbol is not
-- optional and not a nicety: the client keeps a transmitted container under
-- its inv id with no record of which grid it was pushed to (struct
-- InvContainer, src/inv/inv_manager.h), so "which stock is this screen
-- showing" is a question only the caller can answer -- QD.shop's own banner
-- in ui.lua has the whole measurement.
--
-- Five empty pots, because the row has to see a REAL exchange in both
-- directions: `generalshop1` stocks them in quantity, they are stackable (so
-- one backpack slot, and phase 7b's `::give rune_scimitar` still has room),
-- and Buy-5 is one rung of the fixed ladder rather than a composition -- the
-- three-rung composition is proved on Mort'ton's swamp paste, where it is the
-- quest's own step (build/quest_gate/shop_mortton_audit).
local SHOP_NPC_SYMBOL = "generalshopkeeper1"
local SHOP_NPC_OP = 3
local SHOP_INV_SYMBOL = "generalshop1"
local SHOP_TILE_X = 3211
local SHOP_TILE_Z = 3247
local SHOP_OBJ_SYMBOL = "pot_empty"
local SHOP_OBJ_COUNT = 5
local SHOP_COINS = 5000

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
        local seam_count = 0

        -- One entry per verb.  `fn` returns (result, detail) -- or nil when it
        -- has already written its own row (t.step is the only one).
        local function step(name, fn)
            verb_count = verb_count + 1
            PLAN[#PLAN + 1] = { name = name, call = fn }
        end

        -- One entry per SEAM: same entry shape, same grading, counted apart
        -- from the verbs (the banner at the top of this file says why, and
        -- verb_list.py reads the `seam.` prefix to keep the two tables from
        -- drifting into each other).
        local function seam(name, fn)
            seam_count = seam_count + 1
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

        -- The seam rows' own world target, resolved when they run rather than
        -- held from the top: they are the last rows that touch the world and
        -- the Man every other pointer row points at is dead by then, so they
        -- point at a loc instead (trees are in every direction from the
        -- fixture's own tile).  player.by_symbol answers (target, result) --
        -- reversed from every other verb -- so this is the one place that
        -- reversal is spelled out.
        local function seam_loc_target()
            local by_symbol = verb("player", "by_symbol")
            if not by_symbol then
                return nil
            end
            local target, result = by_symbol("loc", LOC_SYMBOL)
            if result ~= "ok" or type(target) ~= "table" then
                return nil
            end
            return target
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

        -- SEAM silent_press_npc_step (2026-09-21) landed this one beside
        -- npc.nearest, and the difference between the two IS the row: nearest
        -- answers ONE copy and cannot be asked which one, `tiles` answers
        -- EVERY copy with the pool's own slot, tile and element id.  Sheep
        -- Herder is why -- `plaguesheep_1` has three spawn rows two tiles
        -- apart and the prod pushes whichever sheep the press named -- so a
        -- quest file that has to POSITION ITSELF relative to an npc had no
        -- reader at all before it.
        --
        -- Three returns, not two: (result, summary, rows).  A ledger detail
        -- is a string, and a list of tables renders through detail_text as
        -- `1=<table> 2=<table>`, so the summary is the string and the rows are
        -- the third value.  Graded here: the summary really names as many
        -- copies as the third value holds, every row carries the pool fields a
        -- caller does arithmetic on, and a symbol nothing spawns answers
        -- `no_row` rather than an empty ok.
        step("npc.tiles", function()
            local fn = verb("npc", "tiles")
            if not fn then return missing("npc", "tiles") end
            local result, summary, rows = fn(NPC_SYMBOL, NEAREST_RADIUS)
            local text = NPC_SYMBOL .. " r=" .. NEAREST_RADIUS .. " -> "
                .. describe(result) .. " " .. describe(summary)
            if result ~= "ok" then
                return result, text
            end
            if not is_table(rows) or #rows == 0 then
                return "hollow", "an `ok` with no rows behind it -- the third return is "
                    .. "the whole reason this verb is not npc.nearest: " .. text
            end
            if type(summary) ~= "string" then
                return "hollow", "the summary is " .. describe(summary)
                    .. ", not a string -- a ledger detail cannot carry a table"
            end
            if not string.find(summary, tostring(#rows) .. " copy(s)", 1, true) then
                return "hollow", "the summary does not agree with the rows it returned ("
                    .. tostring(#rows) .. "): " .. text
            end
            for i = 1, #rows do
                local row = rows[i]
                if not is_table(row) or not is_number(row.x) or not is_number(row.z)
                    or not is_number(row.slot) then
                    return "hollow", "row " .. i .. " is " .. describe(row)
                        .. " -- every row must carry the pool's slot and tile: " .. text
                end
            end
            -- And a symbol nothing here spawns is `no_row`, not an empty ok:
            -- the one answer a caller must be able to branch on.
            local absent = fn(ABSENT_NPC_SYMBOL, 5)
            if absent == "ok" then
                return "hollow", "a " .. ABSENT_NPC_SYMBOL .. " nothing spawns answered ok -- "
                    .. text
            end
            return "ok", text .. "; " .. ABSENT_NPC_SYMBOL .. " -> " .. describe(absent)
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

        -- SEAM talk_owes_a_page (2026-09-21) is graded HERE rather than as a
        -- seam row of its own, because it is a statement about this verb's
        -- own answer and this row already makes the only real talk in the
        -- file.  talk_to's `ok` must say what the DIALOGUE did -- "dialogue
        -- npc is up", or "no dialogue in N tick(s)" -- and not merely which
        -- arm of the settle resolved.  The three quests that went red the day
        -- the npc step-off landed all did it the same way: the settle
        -- answered on the route or on the content script's opening `mes`
        -- line, the page arrived a tick or four later, and the quest file's
        -- next line read a chat frame that was still empty (blackknight rows
        -- 28-30, murder rows 86-88, fluffs' crate hunt; build/quest_gate,
        -- 2026-09-21).  Delete the wait at the tail of QD.player.talk_to and
        -- this row goes red on the word, not on the timing.
        step("player.talk_to", function()
            local fn = verb("player", "talk_to")
            local kind_of = verb("chat", "kind")
            if not fn then return missing("player", "talk_to") end
            if not kind_of then return missing("chat", "kind") end
            local result, detail = fn(NPC_SYMBOL)
            local text = NPC_SYMBOL .. " -> " .. describe(detail)
            if result ~= "ok" then
                return result, text
            end
            local said_page = string.find(tostring(detail), "dialogue ", 1, true) ~= nil
                or string.find(tostring(detail), "no dialogue in ", 1, true) ~= nil
            if not said_page then
                return "hollow", "an `ok` that does not say whether a dialogue is up -- "
                    .. "a quest file's next line is always the conversation, and this "
                    .. "answer cannot tell it whether there is one: " .. text
            end
            local page_kind = kind_of()
            if page_kind == "none" then
                return "hollow", "the talk answered ok and left no page, and the detail "
                    .. "has to be the one that says so: " .. text
            end
            if not string.find(tostring(detail), "dialogue " .. tostring(page_kind), 1, true) then
                return "hollow", "the page is `" .. describe(page_kind) .. "` and the detail "
                    .. "does not name it -- the wait at the tail of talk_to is what puts it "
                    .. "there: " .. text
            end
            return "ok", text
        end)

        -- SEAM silent_press_npc_step (2026-09-21).  talk_to settles on the
        -- conversation; press settles on THE NPC MOVING, for an `[opnpc<n>]`
        -- whose whole body is `anim` + `npc_say` (overhead, never in the chat
        -- ring) + `npc_walk` and which therefore said nothing talk_to could
        -- wait for -- Sheep Herder's prod, diseased_sheep.rs2:117-121, where
        -- a SUCCESSFUL press timed out and every refusal settled.
        --
        -- Lumbridge has no silent press in it, so what this row grades is the
        -- half that is testable anywhere and is where the first draft of the
        -- verb went wrong: THE PRESS NAMES ITS OWN SUBJECT, and the world's
        -- own answer outranks any tile.  Op 1 on a Man opens a dialogue, so
        -- the detail must name the copy it pressed (the element id
        -- QD.drive._press_row now returns, not "the nearest npc of that id")
        -- AND say the page came up rather than crediting a neighbour's
        -- wander step -- which is exactly what the discarded draft did
        -- (build/quest_gate/seampress_a row 17: it credited a wander for a
        -- press content had refused in words).
        step("player.press", function()
            local fn = verb("player", "press")
            local close = verb("chat", "close")
            if not fn then return missing("player", "press") end
            -- CLOSE THE PAGE FIRST. player.talk_to's row above is graded on
            -- LEAVING a dialogue up, and a press issued into an open dialogue
            -- is swallowed: measured here, `timeout ... did not move in 8
            -- tick(s), and nothing was said and no dialogue opened`. The verb
            -- resolves on the page CHANGING, so it needs a known page to
            -- change from -- and `none` is the only known one.
            if close then
                close()
            end
            local result, detail = fn(NPC_SYMBOL, 1)
            local text = NPC_SYMBOL .. " op 1 -> " .. describe(result) .. " " .. describe(detail)
            if close then
                close()
            end
            if result ~= "ok" then
                return result, text
            end
            local said = tostring(detail)
            if not string.find(said, "pressed ", 1, true)
                or not string.find(said, "element ", 1, true) then
                return "hollow", "an `ok` that does not name the copy it pressed -- a press "
                    .. "that cannot say WHICH npc it named cannot tell a push from the two "
                    .. "sheep wandering beside it: " .. text
            end
            if not string.find(said, "dialogue ", 1, true)
                and not string.find(said, "content line ", 1, true) then
                return "hollow", "op 1 on a " .. NPC_SYMBOL .. " opens a conversation, and "
                    .. "this detail reports something else -- the world's own answer has to "
                    .. "outrank the tile every poll: " .. text
            end
            -- And a symbol nothing spawns never reaches a press at all: the
            -- verb hands back by_symbol's own result rather than pressing
            -- into the void.
            local absent = fn(ABSENT_NPC_SYMBOL, 1)
            if absent == "ok" then
                return "hollow", "pressing a " .. ABSENT_NPC_SYMBOL
                    .. " nothing spawns answered ok -- " .. text
            end
            return "ok", text .. "; " .. ABSENT_NPC_SYMBOL .. " -> " .. describe(absent)
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
            local result, detail = fn(OBJ_SYMBOL, INV_OP_UNCLAIMED)
            return no_script_probe(result, detail, "")
        end)

        step("player.equip", function()
            local fn = verb("player", "equip")
            if not fn then return missing("player", "equip") end
            local result, detail = fn(WEARABLE_OBJ_SYMBOL)
            return result, describe(detail)
        end)

        -- Straight after player.equip, on the same item, because that is
        -- the only state this verb has anything to say in: the worn tab's
        -- own "Remove" (op 1 on wornitems:slot<N>), asserted as BOTH halves
        -- -- worn fell and the backpack rose. An `ok` whose detail does not
        -- name the pair is the hollow this row exists to catch.
        step("player.unequip", function()
            local fn = verb("player", "unequip")
            if not fn then return missing("player", "unequip") end
            local result, detail = fn(WEARABLE_OBJ_SYMBOL)
            -- PUT IT BACK ON, and this is not tidiness: this harness's
            -- backpack is FULL (its own setup gives 25 of every rune and the
            -- message log carries "Your inventory is full."), so the slot
            -- this row hands back is a slot the rows below it are counting
            -- on.  Measured 2026-09-21: without this, the two `::give`s that
            -- stage player.use_item_on_item had one free slot between them,
            -- the second landed nothing, and that row failed `no_subject`
            -- for a verb it never called.  The reading above is already
            -- taken; this only restores the world.
            local back = verb("player", "equip")
            if back then
                back(WEARABLE_OBJ_SYMBOL)
            end
            if result == "ok" and (type(detail) ~= "string"
                or string.find(detail, "backpack", 1, true) == nil) then
                return "hollow", "answered ok without naming the worn/backpack move -- "
                    .. describe(detail)
            end
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

        -- ------------------------------- seam: a page nothing has answered
        --
        -- chat.play graded whatever was mounted with no readiness test at
        -- all.  chat.continue_ and chat.choose have always read the
        -- pause_pending latch (to refuse a double submit); chat.play never
        -- did, so a page the PREVIOUS entry had already clicked -- still
        -- mounted, still carrying its own sentence, drawing "Please wait..."
        -- while the server's remount was in flight -- was read as the answer
        -- to that click.  Ernest the Chicken, 2026-09-20: Oddenstein's
        -- "Let's get this fixed then." page and Ernest's thank-you page are
        -- BOTH kind `npc`, so chat.kind() could not tell them apart and the
        -- quest died on entry 1 with the previous page's text
        -- (build/quest_gate/haunted ledger row 55).
        --
        -- The verb above this cannot show it: chat.play answers "mismatch"
        -- with a quoted sentence whether the page was stale or genuinely
        -- wrong, which is exactly why five downstream rows were blamed on
        -- content.  So the row is taken on the helper: QD.chat._await_page_ready
        -- must HOLD on the page that is up and RELEASE on any other, and
        -- QD.chat._resume_outstanding must read the latch.  Nothing is
        -- clicked here -- the objectbox above is left exactly as
        -- chat.continue_ below expects to find it.
        seam("seam.chat_page_ready", function()
            local ready = t.chat and t.chat._await_page_ready
            local outstanding = t.chat and t.chat._resume_outstanding
            local identity = t.chat and t.chat._page_identity
            local kind_fn = verb("chat", "kind")
            if not ready or not outstanding or not identity or not kind_fn then
                return "unsupported", "chat._await_page_ready/_resume_outstanding/_page_identity "
                    .. "are not on this driver"
            end

            local kind = kind_fn()
            if kind ~= "objbox" then
                return "no_subject", "the objectbox the rows above opened is no longer up (kind="
                    .. describe(kind) .. ")"
            end
            local page = identity(kind)
            if type(page) ~= "string" then
                return "no_subject", "the objectbox presents no text to identify it by ("
                    .. describe(page) .. ")"
            end

            -- Nothing has been clicked, so the latch is clear and a caller
            -- with no page of its own to compare against is free to read.
            if outstanding() ~= false then
                return "refused", "a resume reads as outstanding on a page nothing has clicked"
            end
            local fresh = ready(nil, nil, 4)
            if fresh ~= "ok" then
                return "refused", "_await_page_ready(nil) on a quiet page answered " .. describe(fresh)
                    .. " -- the ordinary entry would pay ticks it does not owe"
            end

            -- THE ARM THIS SEAM IS.  Told that the page on screen is the one
            -- the last click was made ON, the wait must run out its own clock
            -- rather than let the caller grade it: a same-kind remount that
            -- has not landed yet reads as the page before it, and that is the
            -- whole defect.
            local held = ready(kind, page, 3)
            if held == "ok" then
                return "refused", "_await_page_ready(" .. kind .. ", <the page that IS up>) answered ok"
                    .. " -- the identity arm is not holding, so a same-kind remount is graded early"
            end

            -- And it is a comparison, not a constant `false`: an identity
            -- nothing on screen carries releases at once.
            local moved = ready(kind, page .. " <no page reads this>", 4)
            if moved ~= "ok" then
                return "refused", "_await_page_ready(" .. kind .. ", <an identity nothing shows>) answered "
                    .. describe(moved) .. " -- the arm never releases, which would stall every entry"
            end

            return "ok", "latch clear -> ready(nil)=ok; held on its own identity ("
                .. describe(held) .. ") and released on a different one"
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

        -- THE VAR WITH NO CLIENT HALF.  Two readings, and the row needs both.
        --
        -- ToriRSServer_SendVarpSmall refuses to encode a varp id the connected
        -- client's varp array cannot address, so a varp this content pack
        -- allocates ABOVE the cache's highest id is never transmitted and both
        -- client-side reads -- var.varp and var.server, which are the same
        -- array's two records -- answer not_found for the whole run, however
        -- complete the quest is.  api_drive.var_content reads the embedded
        -- server's own player varps instead (DriveState_VarpContent).
        --
        -- The DANGER in a reader like that is that it becomes the answer
        -- everywhere and quietly deletes the client/server desync check, so
        -- this row pins both ends of its licence:
        --
        --   * on a var the client CAN hold (cookquest, staged above) it must
        --     agree with var.server -- a channel that answered its own number
        --     here would be free to disagree with the world;
        --   * on a var the client canNOT hold (HIGH_ID_VARP) it must answer
        --     where both client reads said not_found -- which is the only
        --     case quest.lua ever reaches for it in.
        seam("seam.var_content", function()
            local read_content = verb("quest", "_read_content")
            local read_server = verb("var", "server")
            local read_client = verb("var", "varp")
            if not read_content then return missing("quest", "_read_content") end
            if not read_server then return missing("var", "server") end
            if not read_client then return missing("var", "varp") end

            local server_result, server_value = read_server(QUEST_VARP)
            local content_result, content_value = read_content(QUEST_VARP)
            local low = QUEST_VARP .. ": server=" .. describe(server_result) .. "/"
                .. describe(server_value) .. " content=" .. describe(content_result)
                .. "/" .. describe(content_value)
            if content_result ~= "ok" then
                return content_result, "the server's own copy could not be read at all -- " .. low
            end
            if server_result == "ok" and content_value ~= server_value then
                return "refused", "the server's own copy disagrees with the transmitted one, "
                    .. "so this channel is not reading the world -- " .. low
            end

            local client_result, client_value = read_client(HIGH_ID_VARP)
            local high_result, high_value = read_content(HIGH_ID_VARP)
            local high = HIGH_ID_VARP .. ": client=" .. describe(client_result) .. "/"
                .. describe(client_value) .. " content=" .. describe(high_result)
                .. "/" .. describe(high_value)
            if high_result ~= "ok" then
                return high_result, "the one var this channel exists for could not be read -- "
                    .. high .. " (" .. low .. ")"
            end
            if client_result == "ok" then
                return "hollow", "the client CAN address " .. HIGH_ID_VARP .. " in this pack, so "
                    .. "this row no longer covers the case it was written for -- pick a varp "
                    .. "above the compack's top: " .. high
            end
            return "ok", low .. "; " .. high
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

        -- SEAM goto_tile_fixed_budget (2026-09-21).  The public row above
        -- proves ONE goto onto ONE tile with nothing else moving the player,
        -- and that is exactly the case that always worked.  What was wrong
        -- was the BUDGET: one `::goto` and a hardcoded ten ticks with no
        -- retry arm, so a content teleport that landed behind the cheat --
        -- `p_delay(n); p_telejump(...)`, every boat and trapdoor in this pack
        -- -- took the player back and the verb reported a hard timeout on a
        -- tile it reaches perfectly well.  Sea Slug proved it inside ONE run,
        -- row 27 FAIL against row 61 PASS on the same tile with the same
        -- verb; the banner over QD.player.goto_tile in pointer.lua carries
        -- the measurement and build/seam_goto_budget/repro_boat_goto.lua
        -- reproduces it on demand.
        --
        -- Three things are graded here, none of them visible from the public
        -- row: the arrival predicate the loop turns on, that the loop really
        -- RE-ISSUES (and says how many times, with the server's own line for
        -- each attempt), and two gotos across a region change -- the shape
        -- the seam was found in.
        seam("seam.goto_tile_budget", function()
            local fn = verb("player", "goto_tile")
            local here = verb("player", "_goto_here")
            local tile_of = verb("world", "tile")
            if not fn then return missing("player", "goto_tile") end
            if not here then return missing("player", "_goto_here") end
            if not tile_of then return missing("world", "tile") end

            -- 1. THE ARRIVAL PREDICATE.  Every attempt is decided by it, so a
            -- predicate that answered true for anything would make the retry
            -- arm invisible and a predicate that answered false for
            -- everything would spend three teleports on every goto in the
            -- suite.  Chebyshev 1 on x/z, the plane EXACT.
            local read, tile = tile_of()
            if read ~= "ok" or not is_table(tile) then
                return read, "no tile to test the arrival predicate against: " .. describe(tile)
            end
            if not here(tile.x, tile.z, tile.level) then
                return "hollow", "player._goto_here says the player is not on the tile "
                    .. "world.tile just answered with (" .. describe(tile) .. ")"
            end
            if here(tile.x + 8, tile.z, tile.level) then
                return "hollow", "player._goto_here accepts a tile eight squares away, "
                    .. "so no attempt could ever miss"
            end
            if here(tile.x, tile.z, (tile.level + 1) % 4) then
                return "hollow", "player._goto_here accepts the wrong plane -- "
                    .. "one floor out is never the same room"
            end

            -- 2. THE RETRY ARM RUNS, AND SAYS SO.  A plane outside 0-3 is the
            -- one miss this harness can stage from Lumbridge with no race in
            -- it: torirs_server_world.c's ladder answers "::goto - level must
            -- be 0-3, not 4." and RAN, so the cheat is `ok` and the arrival
            -- can never be true.  Three attempts of one tick, and the detail
            -- has to account for all three -- that accounting is the whole
            -- fix, because without it a two-attempt landing is indistinguish-
            -- able from a one-attempt one in the ledger.
            local miss, miss_detail = fn(tile.x, tile.z, 4, 1, 3)
            if miss ~= "timeout" then
                return "hollow", "an unreachable plane answered " .. describe(miss)
                    .. " rather than timeout -- " .. describe(miss_detail)
            end
            local text = tostring(miss_detail)
            if not string.find(text, "3 attempt(s)", 1, true) then
                return "hollow", "the timeout does not say how many attempts ran: " .. text
            end
            local ran = 0
            for _ in string.gmatch(text, "attempt %d:") do
                ran = ran + 1
            end
            if ran ~= 3 then
                return "hollow", "the timeout accounts for " .. tostring(ran)
                    .. " attempt(s), not 3: " .. text
            end
            if not string.find(text, "level must be 0-3", 1, true) then
                return "hollow", "the timeout drops the server's own sentence, which is "
                    .. "the only thing that tells a typo from a race: " .. text
            end

            -- 3. TWO GOTOS ACROSS A REGION CHANGE, back to back, which is the
            -- shape row 27 died in: the second one is issued while the client
            -- is still finishing the first one's scene.  Draynor then Varrock
            -- -- two map squares apart each way from here and from each
            -- other, so each is a real rebuild and not a walk.
            local hops = {
                { 3093, 3244, 0, "Draynor Village market" },
                { 3213, 3428, 0, "Varrock square" },
            }
            local said = {}
            for index = 1, #hops do
                local hop = hops[index]
                local result, detail = fn(hop[1], hop[2], hop[3])
                said[#said + 1] = hop[4] .. " -> " .. describe(result)
                    .. " " .. describe(detail)
                if result ~= "ok" then
                    return result, "hop " .. tostring(index) .. " (" .. hop[4]
                        .. ") did not land: " .. table.concat(said, "; ")
                end
                local at_result, at = tile_of()
                if at_result ~= "ok" or not is_table(at) then
                    return at_result, "hop " .. tostring(index) .. " answered ok and "
                        .. "world.tile answered " .. describe(at_result)
                        .. " -- " .. table.concat(said, "; ")
                end
                if not here(hop[1], hop[2], hop[3]) then
                    return "hollow", "hop " .. tostring(index) .. " (" .. hop[4]
                        .. ") answered ok with the player at " .. describe(at.x) .. ","
                        .. describe(at.z) .. "," .. describe(at.level)
                        .. " -- " .. table.concat(said, "; ")
                end
            end
            return "ok", "arrival predicate exact; an unreachable plane accounts for "
                .. "3 attempt(s) with the server's line on each; " .. table.concat(said, "; ")
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
        --
        -- AND THEN WALK TO HIM, which is this block's stability and not a
        -- tidy-up.  `::tele lumbridge` lands the player at 3222,3218 every
        -- run, and the nearest `man` spawn row in this pack is
        -- server/scripts/areas/world/configs/m50_50.spawn:36 at 3216,3219 --
        -- SIX tiles away, permanently outside the five player.attack's row
        -- reads at.  Whether these three rows had a subject at all was
        -- therefore decided by which way a wandering npc had drifted on the
        -- tick this harness happened to reach, and nothing in the file said
        -- so: green 113/113 at 64ef6e9b2, red 111/114 at 68c5e8d9d with the
        -- whole combat block byte-identical between the two (`git diff
        -- 64ef6e9b2 HEAD -- test/quests/_conformance.lua` is the
        -- player.unequip row and the verb count, nothing else), the failure
        -- reading `[no_subject] man is not within five tiles (no_row)`
        -- (build/quest_gate/_conformance/attempt-01/ledger.tsv row 97).  One
        -- row added above moved every tick below it, which is all it took.
        --
        -- player.walk_near, not a second `::tele` or a goto_tile at the
        -- spawn coordinates: the Man is the thing that moves, so the walk has
        -- to track HIM (walk_near re-aims at the target's current tile every
        -- server tick) rather than at a tile he was once on.  It writes no
        -- row -- it is setup, exactly like the cheats above it -- and its own
        -- conformance row is a hundred rows higher up, already graded.
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
            local by_symbol = verb("player", "by_symbol")
            local walk_near = verb("player", "walk_near")
            if by_symbol and walk_near then
                local target, target_result = by_symbol("npc", NPC_SYMBOL)
                if target_result == "ok" and is_table(target) then
                    walk_near(target, 20, 1)
                end
            end
            settle(2)
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

        -- ----------------- the two verbs the death/engagement seam landed
        --
        -- Both come out of SEAM combat-hunt-kills-the-character (2026-09-20):
        -- Mort'ton's shade hunt ran 1,006 ticks and forty attack attempts,
        -- reported `holding 4/5 shade_bones1 ... no target to engage` and
        -- photographed the Lumbridge castle courtyard.  The character had been
        -- killed at four kills and every attempt after that was clicked from
        -- the respawn tile, because nothing in this driver could see a death
        -- and nothing could hold the npc an Attack had actually engaged.
        --
        -- Their position is the same measurement npc.await_dead's is: LAST of
        -- the world rows, because the fight above is their only subject.

        step("player.alive", function()
            local fn = verb("player", "alive")
            if not fn then return missing("player", "alive") end
            local result, detail = fn()
            local text = "-> " .. describe(detail)
            if result ~= "ok" then
                -- Not a soft answer: QD.player._death_fence ends a run the
                -- first time this reading goes the other way, so a verb that
                -- read `refused` on a living character would end every quest
                -- in the suite at its first click.
                return result, "the character this harness has been driving for "
                    .. "eighty rows reads as NOT ALIVE: " .. text
            end
            -- HOLLOW IS THE RISK HERE, not a wrong word.  An `ok` with nothing
            -- behind it is indistinguishable from a verb that never read the
            -- chat ring at all -- and the ring is the only place a death is
            -- written (the hitpoints are refilled by [proc,player_death_restore]
            -- within a tick or two of the killing blow, which is why they are
            -- not the reading).  So the row requires the reading itself.
            if not string.find(tostring(detail), "hitpoints", 1, true)
                or not string.find(tostring(detail), "no death line", 1, true) then
                return "hollow", "answered ok without naming the hitpoints it read or the "
                    .. "chat ring it read them beside, so nothing here was actually read: "
                    .. text
            end
            return "ok", text
        end)

        step("npc.await_dead_engaged", function()
            local fn = verb("npc", "await_dead_engaged")
            if not fn then return missing("npc", "await_dead_engaged") end
            if combat_slot == nil then
                return "no_subject", "player.attack read no npc slot, so nothing was engaged"
            end
            -- The fight is over -- npc.await_dead resolved it one row ago --
            -- and the SLOT is what this verb holds, so "the slot I was given is
            -- already dead" is the answer it owes.  That is not a weaker
            -- subject than a live fight: it is the one assertion a live fight
            -- cannot make, because it proves the stamp survived a verb that
            -- does not write one.
            local result, detail = fn(10)
            local text = "-> " .. describe(detail)
            if result == "no_row" and string.find(tostring(detail), "nothing is engaged", 1, true)
            then
                return "no_subject", "t.player.attack landed no Attack row, so no slot was "
                    .. "stamped for this verb to hold: " .. text
            end
            if result ~= "ok" then
                return result, "the slot player.attack engaged (" .. describe(combat_slot)
                    .. ") is dead and this verb did not read it as finished: " .. text
            end
            if not string.find(tostring(detail), "slot " .. tostring(combat_slot), 1, true) then
                return "hollow", "answered ok without naming the slot it held, which is the one "
                    .. "thing it has that npc.await_dead(symbol) has not: " .. text
            end
            -- AND THE STAMP IS SPENT.  A second wait with no new Attack press
            -- between them must not read the kill that already happened as its
            -- own: that is how a hunt loop counts one corpse twice and reports
            -- five kills it never made.
            local again, again_detail = fn(2)
            if again ~= "no_row" then
                return "hollow", "a second wait with no Attack press in between answered "
                    .. describe(again) .. " (" .. describe(again_detail) .. ") -- the engagement "
                    .. "is not consumed, so one kill can be waited out twice"
            end
            return "ok", text .. " [and a second wait with no new Attack answered no_row: "
                .. describe(again_detail) .. "]"
        end)

        -- ------------------ the seam rows the 2026-09-20 seam pass landed
        --
        -- LAST OF EVERYTHING THAT READS THE WORLD, after the fight, and the
        -- position is a measurement twice over.  A seam row presses, arms and
        -- moves the camera like any other click: between player.use_on and
        -- player.inv_op they cost `player.drop` its row on a backpack they
        -- had been pressing, and one block earlier they cost `player.attack`
        -- its fight -- 35 ticks of pressing is enough for the wandering Man
        -- the combat rows read to be somewhere else (measured against a HEAD
        -- build in a throwaway worktree: 101/101 and 244 ticks there, this
        -- harness 279 with the seam rows in front of the fight, and the same
        -- driver 101/101 again under HEAD's own harness).  Their subject is a
        -- LOC, not the Man, because the two rows above have just killed him.
        --
        -- Their subject is SEAM_OBJ_SYMBOL and not OBJ_SYMBOL, because
        -- player.drop just emptied the air runes -- and a `::give` to put them
        -- back is not free: `::runes` fills twenty-two of the backpack's
        -- twenty-eight slots, so re-adding one costs phase 7b below the room
        -- its own `::give garlic` needs (measured: `::give garlic left 0 in
        -- the backpack`).  A rune the drop did not touch is a subject that
        -- costs nothing to state.
        -- AND THEY START FROM A FIXED TILE, not from wherever the fight left
        -- the character.  `seam.press_pixel` picks the nearest `tree` and
        -- presses it, so the tile it starts from decides which tree it gets
        -- and at what angle -- and the fight above ends wherever the wandering
        -- Man it chased happened to be.  That is a row grading the rows above
        -- it, which this file's own section-8 note already names as the way
        -- press_pixel went red once before: the tile was 3222,3213 on one run
        -- and 3213,3224 on the next, eleven tiles apart, and the second one
        -- projected its tree at 207,79 with 54 of the 99 candidate pixels off
        -- the viewport entirely (`no candidate pixel holds tree from any
        -- pose`, 2026-09-22).  `::tele lumbridge` is the same landing the
        -- fight stage above opens with, so the seam block now starts from
        -- 3222,3218 every run, and a red press_pixel means the press, not the
        -- walk that happened to precede it.
        stage(function()
            local close = verb("chat", "close")
            if close then
                close()
            end
            setup_cheat("::tele lumbridge")
            settle(6)
        end)

        -- THE PRESS PIXEL, AND WHICH CAMERA POSE THE SEARCH FOR IT RUNS AT.
        --
        -- Re-graded 2026-09-20: the search itself landed in 62c051fb8 and
        -- could only run ONCE, at whatever pose the retry loop happened to
        -- end on, because an unbudgeted sweep of all five poses took
        -- Elemental Workshop I and Pirate's Treasure from green to red.  The
        -- poses a click has already framed are now RANKED by how many of the
        -- ladder's candidates land inside the world viewport at all, and the
        -- whole sweep shares ONE probe cap -- so it costs no more probing
        -- than the single hunt did.  Measured at cog's black spindle: the
        -- flattest pose holds it after 56 probes and the other four have 27
        -- to 81 of their candidates off-viewport and hold it never.
        seam("seam.press_pixel", function()
            local hunt = verb("drive", "_hover_onto")
            local frame = verb("drive", "_frame")
            local rank = verb("drive", "_hunt_order")
            local reach_of = verb("drive", "_hover_reach")
            if not hunt then return missing("drive", "_hover_onto") end
            if not frame then return missing("drive", "_frame") end
            if not rank then return missing("drive", "_hunt_order") end
            if not reach_of then return missing("drive", "_hover_reach") end
            local cap = is_table(t.drive) and t.drive._hover_budget or nil
            if type(cap) ~= "number" then
                return "missing", "t.drive._hover_budget is not a number -- the ranked sweep "
                    .. "is only affordable because every pose shares ONE probe cap"
            end

            -- THE RANKING, on synthetic poses, because the decision is
            -- arithmetic and must be graded without a camera in it.  The
            -- viewport is a plain rectangle (`_hover_inside` reads view_x /
            -- view_y / view_w / view_h and nothing else), and the ladder
            -- climbs UP to 192 px: a projection near the top of it has almost
            -- no legal candidate, which is cog's black spindle at the pose the
            -- retry loop ends on.
            local view = { view_x = 0, view_y = 0, view_w = 512, view_h = 334 }
            local middle = { x = 256, y = 200 }
            local under_the_chrome = { x = 256, y = 20 }
            local middle_reach = reach_of(view, middle)
            local edge_reach = reach_of(view, under_the_chrome)
            if type(middle_reach) ~= "number" or type(edge_reach) ~= "number"
                or middle_reach <= edge_reach then
                return "hollow", "_hover_reach does not separate a pose with room to climb "
                    .. "from one under the chrome: middle " .. describe(middle_reach)
                    .. ", top " .. describe(edge_reach)
            end
            -- Framed in the order click_minimenu frames them, with the BAD
            -- pose last -- which is the pose the pre-seam hunt was stuck with.
            local ranked = rank({ { index = 1, pos = middle }, { index = 2, pos = under_the_chrome } },
                view)
            if not is_table(ranked) or #ranked ~= 2 then
                return "hollow", "_hunt_order returned " .. describe(ranked)
                    .. " for two poses"
            end
            if ranked[1].index ~= 1 then
                return "refused", "the sweep would hunt at the pose the retry loop ENDED on "
                    .. "(" .. describe(ranked[1].index) .. ", reach " .. describe(ranked[1].reach)
                    .. ") ahead of the one with room to climb (1, reach "
                    .. describe(middle_reach) .. ") -- the ranking is what this seam is"
            end
            -- The tie rule, which is what keeps a target whose poses all reach
            -- equally being hunted exactly where it was hunted before this
            -- seam, with no camera move: the LAST pose framed leads.
            local tied = rank({ { index = 1, pos = middle }, { index = 2, pos = middle } }, view)
            if not is_table(tied) or #tied ~= 2 or tied[1].index ~= 2 then
                return "hollow", "a tie does not keep the pose the camera is already at first: "
                    .. describe(tied)
            end

            local seam_target = seam_loc_target()
            if not seam_target then
                return "no_subject", "player.by_symbol(loc, " .. LOC_SYMBOL .. ") built no target"
            end
            -- STAND NEXT TO IT FIRST -- the precondition every real caller of
            -- the hunt already has, and the one this row was reading off the
            -- rows above it.
            --
            -- `_hover_onto` is reached from QD.drive.click_minimenu, and every
            -- verb that reaches click_minimenu has walked to its target first
            -- (click_loc and use_on through walk_near's standoff, talk_to
            -- through the same gate).  This row called it with whatever tile
            -- the previous rows left the player on -- and the combat rows
            -- added above it leave him where the fight ended: measured
            -- 2026-09-20 and again 2026-09-21, `player.alive` reports
            -- 3222,3218 L0 and the nearest `tree` is 3217,3241
            -- (seam.reach_names_the_copy's own detail, same run), twenty-three
            -- tiles north.  At that range the loc projects at 400,84 -- the
            -- top edge of a 765x503 viewport, where the ladder's upward climb
            -- has almost no legal candidate left -- and the row failed
            -- `covered ... 45 pixels ... 54 off-viewport` on the DISTANCE, not
            -- on the seam (build/quest_gate/_conf_seam1/attempt-01 row 100).
            --
            -- So the tile is taken here instead of inherited: ::goto to the
            -- tree's own square (a teleport lands on the nearest tile the
            -- world accepts, so the player ends on it or beside it), then the
            -- loc standoff that click_loc itself takes, which steps off the
            -- square when the teleport landed on it.  Nothing about the hunt
            -- is relaxed; it is given the frame a press would have.
            local goto_tile = verb("player", "goto_tile")
            local walk_near = verb("player", "walk_near")
            local tile_of_target = verb("drive", "_target_tile")
            local tile_of = verb("world", "tile")
            if not goto_tile then return missing("player", "goto_tile") end
            if not walk_near then return missing("player", "walk_near") end
            if not tile_of_target then return missing("drive", "_target_tile") end
            if not tile_of then return missing("world", "tile") end
            local standoff = is_table(t.player) and t.player._loc_standoff or nil
            if type(standoff) ~= "number" then
                return "missing", "t.player._loc_standoff is not a number -- the hunt has to be "
                    .. "given the tile a press would be made from"
            end
            local here_result, here = tile_of()
            if here_result ~= "ok" or not is_table(here) then
                return here_result, "no tile reading before the approach"
            end
            local tree_result, tree_x, tree_z = tile_of_target(seam_target)
            if tree_result ~= "ok" then
                return "no_subject", "no tile for " .. LOC_SYMBOL .. " in the loc pool: "
                    .. describe(tree_result)
            end
            local approach_result, approach_detail = goto_tile(tree_x, tree_z, here.level)
            if approach_result ~= "ok" then
                return approach_result, "player.goto_tile(" .. describe(tree_x) .. ","
                    .. describe(tree_z) .. "," .. describe(here.level) .. ") did not reach "
                    .. LOC_SYMBOL .. "'s square: " .. describe(approach_detail)
            end
            local stepped_result, stepped_detail = walk_near(seam_target, nil, standoff)
            if stepped_result ~= "ok" then
                return stepped_result, "walk_near(" .. LOC_SYMBOL .. ", standoff "
                    .. describe(standoff) .. ") left the player on the target's own square: "
                    .. describe(stepped_detail)
            end
            local approached = select(2, tile_of())
            -- And the live half, the one the pre-seam row already proved: a
            -- pixel the target is DRAWN at, found by probing.  `_hover_onto`
            -- walks a ladder of candidates around the projected point and
            -- answers the first whose PICKSET HOLDS the element.  A table back
            -- from it is the whole C seam proved at once, because it only ever
            -- accepts a candidate that survived `_hover_probe`, and
            -- `_hover_probe` answers nil unless api_drive.pick_point
            -- (DrivePointer_PickPoint over World_PickSetStamp) says a rendered
            -- frame hittested at exactly that pixel.  Without that stamp a
            -- `held=false` is not a reading at all -- it is equally "the model
            -- is not drawn here" and "the frame has not caught up with my move
            -- yet" -- and the driver spent a batch of quests reporting the
            -- second as the first.
            --
            -- The pose is taken FIRST, through the same `_frame` a covered
            -- press retries with, rather than reading whatever projection the
            -- rows above left behind: measured 2026-09-20, the raw projection
            -- after the click_loc/click_obj/use_on rows put the Man at
            -- 593,198 -- a pixel the world never hittests -- and the row
            -- failed on where the camera happened to be pointing rather than
            -- on the seam.
            --
            -- Every pose shares ONE budget here, exactly as click_minimenu's
            -- sweep does, and the budget is read back afterwards: a
            -- `_hover_onto` that ignored its fourth argument would leave it
            -- untouched, and the whole reason the sweep may visit five poses
            -- is that together they cost one ladder.
            local poses = is_table(t.drive) and t.drive._frame_poses or nil
            local limit = is_table(poses) and #poses or 1
            local budget = { left = cap }
            local hovered, account, pos = nil, "no pose framed it", nil
            local tried = {}
            local index = 1
            while index <= limit and not is_table(hovered) do
                local pos_result, framed = frame(seam_target, index, 4)
                if pos_result == "ok" and is_table(framed) then
                    pos = framed
                    hovered, account = hunt(seam_target, framed, 4, budget)
                    tried[#tried + 1] = "pose " .. index .. " at " .. describe(framed.x)
                        .. "," .. describe(framed.y) .. ": " .. describe(account)
                else
                    tried[#tried + 1] = "pose " .. index .. ": " .. describe(pos_result)
                end
                index = index + 1
            end
            if not is_table(hovered) then
                return "covered", "no candidate pixel holds " .. LOC_SYMBOL
                    .. " from any pose -- " .. table.concat(tried, "; ")
            end
            if type(budget.left) ~= "number" or budget.left < 0 then
                return "refused", "the shared probe cap was overspent: " .. describe(budget.left)
                    .. " left of " .. describe(cap) .. " -- " .. table.concat(tried, "; ")
            end
            if budget.left >= cap then
                return "hollow", "the hunt charged NOTHING to the shared cap, so the fourth "
                    .. "argument is not threaded through _hover_onto and a five-pose sweep "
                    .. "would cost five ladders -- " .. table.concat(tried, "; ")
            end
            return "ok", "from " .. describe(is_table(approached) and approached.x)
                .. "," .. describe(is_table(approached) and approached.z) .. ", beside "
                .. LOC_SYMBOL .. " at " .. describe(tree_x) .. "," .. describe(tree_z)
                .. ": pressing " .. describe(hovered.x) .. "," .. describe(hovered.y)
                .. " instead of the projected " .. describe(pos.x) .. "," .. describe(pos.y)
                .. "; ranked a reach-" .. describe(middle_reach) .. " pose ahead of a reach-"
                .. describe(edge_reach) .. " one and spent " .. describe(cap - budget.left)
                .. " of " .. describe(cap) .. " probes -- " .. table.concat(tried, "; ")
        end)

        -- "I CAN'T REACH THAT!" IS A FACT ABOUT THE TILE, NOT ABOUT THE CLICK.
        --
        -- The driver never chose the tile it presses a loc from: walk_near's
        -- standoff and `_step_off_for_click` both answer "get off the target's
        -- own square", and neither asks which side the SERVER serves the
        -- interaction from.  The far-side walk under them fires on `covered`
        -- alone -- "the menu had no row for it" -- and a press the server
        -- refuses opens a menu with a perfectly good row in it, so that loop
        -- never ran.  biohazard's watchtower is served from the EAST and from
        -- nowhere else; its file recorded the refusal as a content bug and was
        -- blocked on it (build/quest_gate/seam_reach_p2, 2026-09-20).
        --
        -- Worse, the refusal did not even reach the row: the engine says it on
        -- the tick AFTER the route runs out, so `_settle_after_click`'s
        -- map_flag arm resolved first and the press was graded `ok (map_flag)`
        -- -- a green row for a press that did nothing.
        --
        -- Three things, and the row fails if any one of them goes:
        --   the SENTENCE is matched exactly, so content quoting it is not a
        --     reach refusal and an ordinary refusal does not start a walk;
        --   a press with no refusal behind it is NOT regraded, because a fence
        --     that invents refusals is worse than no fence;
        --   a refused press is RE-TAKEN from another approach tile of the same
        --     loc, and the row that comes back names the tile that worked.
        --
        -- The retry is driven with an injected press here rather than with a
        -- genuinely unreachable loc: the press is the caller's own closure
        -- (click_loc passes its click+settle, use_on its re-arm+press+settle),
        -- so injecting one grades the WALK and the candidate order -- which is
        -- the seam -- without needing a Lumbridge loc that refuses a side.
        seam("seam.reach_retry", function()
            local refusal_of = verb("player", "_reach_refusal")
            local verify = verb("player", "_reach_verify")
            local retry = verb("player", "_reach_retry")
            local candidates_of = verb("player", "_reach_candidates")
            local tile_of = verb("world", "tile")
            if not refusal_of then return missing("player", "_reach_refusal") end
            if not verify then return missing("player", "_reach_verify") end
            if not retry then return missing("player", "_reach_retry") end
            if not candidates_of then return missing("player", "_reach_candidates") end
            if not tile_of then return missing("world", "tile") end
            local seam_target = seam_loc_target()
            if not seam_target then
                return "no_subject", "player.by_symbol(loc, " .. LOC_SYMBOL .. ") built no target"
            end

            -- Exactly the sentence, trimmed -- never a line that quotes it.
            if refusal_of("I can't reach that!") ~= "I can't reach that!" then
                return "hollow", "the reach sentence itself is not recognised: "
                    .. describe(refusal_of("I can't reach that!"))
            end
            if refusal_of("The mourner says: I can't reach that! Go away.") ~= nil
                or refusal_of("Nothing interesting happens.") ~= nil then
                return "hollow", "a line that merely QUOTES the sentence, or an ordinary "
                    .. "refusal, would start a walk around the loc"
            end

            -- The fence does not invent a refusal.  A serial no message can be
            -- newer than is the cheapest way to state "nothing arrived behind
            -- this press", and it costs the same tick a real one does.
            local kept, kept_detail = verify("ok", "the press landed", 2147483000)
            if kept ~= "ok" then
                return "refused", "a clean press was regraded " .. describe(kept)
                    .. " (" .. describe(kept_detail) .. ") with no refusal behind it -- "
                    .. "every green loc row in the suite would go red"
            end

            -- The approach tiles: orthogonal neighbours of every copy of the
            -- loc, the copies' own squares LAST (a press from one of those has
            -- the player's own model in front of the pixel, so it is a last
            -- resort and not a neighbour).
            local candidates = candidates_of(seam_target)
            if not is_table(candidates) or #candidates == 0 then
                return "not_found", "no approach tile for " .. LOC_SYMBOL
                    .. ": " .. describe(candidates)
            end
            local saw_own = false
            for index = 1, #candidates do
                if candidates[index].own then
                    saw_own = true
                elseif saw_own then
                    return "hollow", "a neighbour tile is listed AFTER the loc's own square, "
                        .. "so the last resort would be walked to first: " .. describe(candidates)
                end
            end

            local before_result, before = tile_of()
            if before_result ~= "ok" or not is_table(before) then
                return before_result, "no tile to compare the retry against"
            end
            local pressed = 0
            local pressed_at = nil
            local result, detail, attempts = retry(
                seam_target, "refused", "I can't reach that!", function()
                    pressed = pressed + 1
                    local at_result, at = tile_of()
                    if at_result == "ok" and is_table(at) then
                        pressed_at = at
                    end
                    return "ok", "the injected press landed"
                end)
            if attempts ~= 1 or pressed ~= 1 then
                return "refused", "the refused press was re-taken " .. describe(attempts)
                    .. " time(s) from " .. describe(#candidates) .. " approach tile(s) ("
                    .. describe(pressed) .. " press(es) made) -- " .. describe(detail)
            end
            if result ~= "ok" then
                return result, "the retry press answered ok and the verb reported "
                    .. describe(result) .. ": " .. describe(detail)
            end
            if not is_table(pressed_at) or (pressed_at.x == before.x and pressed_at.z == before.z)
            then
                return "hollow", "the retry pressed from the SAME tile the refused press was "
                    .. "made from (" .. describe(before.x) .. "," .. describe(before.z)
                    .. ") -- it walked nowhere: " .. describe(detail)
            end
            if not string.find(tostring(detail), "approach tile", 1, true)
                or not string.find(tostring(detail), "I can't reach that!", 1, true) then
                return "hollow", "the row does not name the tile that worked or the refusal it "
                    .. "replaced, so an author cannot tell a retried press from a first one: "
                    .. describe(detail)
            end

            -- And an ordinary refusal is content answering on the merits:
            -- walking to another side cannot improve it, so nothing is pressed.
            local quiet = 0
            local other_result, _, other_attempts = retry(
                seam_target, "refused", "Nothing interesting happens.", function()
                    quiet = quiet + 1
                    return "ok", "this press should never have been made"
                end)
            if other_attempts ~= 0 or quiet ~= 0 or other_result ~= "refused" then
                return "refused", "a refusal that is NOT about the reach started a walk around "
                    .. "the loc (" .. describe(quiet) .. " press(es), "
                    .. describe(other_attempts) .. " attempt(s), " .. describe(other_result) .. ")"
            end
            return "ok", "the refused press was re-taken from " .. describe(pressed_at.x)
                .. "," .. describe(pressed_at.z) .. " of " .. describe(#candidates)
                .. " approach tile(s), the row names it, a clean press is not regraded, and an "
                .. "ordinary refusal presses nothing"
        end)

        -- THE RE-ARM, which is use_on's far-side retry with the walking taken
        -- out of it.  Four steps, and the third is the seam:
        --
        --   arm  -> "armed by this call"
        --   arm  -> "already armed, nothing sent"   <- inv_arm read app->objsel
        --                                             FIRST and did not spend
        --                                             the live arming on an
        --                                             OPHELDU of the item on
        --                                             itself
        --   press the wildcard row, exactly as the retry press does
        --   grade the row that was pressed: the HELD row, not an Examine
        --
        -- The old path fails this row at step two and again at step four, and
        -- those are the two halves of what golem and fishingcompo reported.  A
        -- binary older than DrivePointer_InvArm answers step two through the
        -- named fallback (`armed by the pre-inv_arm path`), which this row
        -- reads as the failure it is rather than as a pass.
        --
        -- It leaves nothing armed behind it: the press at step three is the
        -- OPHELDU, and encoding one is the only thing that clears app->objsel.
        seam("seam.use_on_rearm", function()
            local cell_of = verb("player", "_inv_cell")
            local arm = verb("player", "_arm_held")
            local press = verb("drive", "click_minimenu")
            local graded = verb("player", "_select_row_is_held")
            if not cell_of then return missing("player", "_inv_cell") end
            if not arm then return missing("player", "_arm_held") end
            if not press then return missing("drive", "click_minimenu") end
            if not graded then return missing("player", "_select_row_is_held") end
            local seam_target = seam_loc_target()
            if not seam_target then
                return "no_subject", "player.by_symbol(loc, " .. LOC_SYMBOL .. ") built no target"
            end
            local cell_result, cell = cell_of(SEAM_OBJ_SYMBOL)
            if cell_result ~= "ok" then
                return "no_subject", SEAM_OBJ_SYMBOL .. " has no backpack cell to arm ("
                    .. describe(cell_result) .. " " .. describe(cell) .. ")"
            end
            local first_result, first_detail = arm(SEAM_OBJ_SYMBOL, cell)
            if first_result ~= "ok" then
                return first_result, "the first arming failed -- " .. describe(first_detail)
            end
            local second_result, second_detail = arm(SEAM_OBJ_SYMBOL, cell)
            if second_result ~= "ok" then
                return second_result, "the SECOND arming failed, which is the seam: a live "
                    .. "selection must be recognised, not spent -- " .. describe(second_detail)
            end
            if not string.find(tostring(second_detail), "already armed, nothing sent", 1, true) then
                return "hollow", "the second arming answered ok but did not recognise the live "
                    .. "selection, so something WAS sent: " .. describe(second_detail)
                    .. " (first: " .. describe(first_detail) .. ")"
            end
            local press_result, click = press(seam_target, "select")
            if press_result ~= "ok" then
                return press_result, "armed twice, but the wildcard press answered "
                    .. describe(press_result) .. " -- " .. describe(click)
            end
            local held_ok, held_why = graded(seam_target, click)
            if not held_ok then
                return "refused", "the press landed, but not on the held-item row -- the arming "
                    .. "did not survive: " .. describe(held_why)
            end
            return "ok", "armed, re-armed without sending ("
                .. describe(second_detail) .. "), and the wildcard press took the held-item row"
        end)

        -- A USE WHOSE ONLY EFFECT IS IN THE BACKPACK IS STILL A USE THAT
        -- LANDED.
        --
        -- _settle_after_click's five arms are all edges the SCREEN shows -- a
        -- mounted chat sub, a chat line, a route, a changed page -- and a
        -- whole family of `[opnpcu]`/`[oplocu]` branches produce none of them.
        -- `[opnpcu,gertrudescat]`'s milk branch (quest_fluffs.rs2:222-231)
        -- animates, says "Mew!" OVERHEAD, swaps the bucket and writes the
        -- varp; the overhead say is not a chat line, the swap is not a page,
        -- and once the player stands BESIDE the cat rather than inside her
        -- there is no route either.  So the press that demonstrably landed
        -- answered `timeout` at the full deadline and Gertrude's Cat went
        -- 53/53 -> 41/16 (build/quest_gate/fluffs, 2026-09-21, rows 12 and
        -- 24, with `quest.stage.gave_milk PASS 3` one row below saying the
        -- milk HAD been drunk).
        --
        -- The rule is graded here rather than by pressing something, because
        -- the thing that can break is the DECISION and not the press: the
        -- first draft of this fix made the backpack a sixth settle ARM and
        -- Elemental Workshop lost its completion varbit to it (row 50
        -- `smithShield PASS 1 inv_changed` against the published `PASS 4
        -- chat_message`, row 51 `quest.varp_complete FAIL client=0 server=0`)
        -- -- a container delta is the EARLIEST thing a press produces and the
        -- weakest evidence that it finished.  So: read AFTER the timeout,
        -- never instead of an arm, and only ever turning a timeout into an ok.
        -- Both halves are checked, and nothing is pressed and nothing moves --
        -- the live reading is compared against a fabricated one.
        seam("seam.use_on_silent_effect", function()
            local decide = verb("player", "_use_on_silent_effect")
            local contents = verb("player", "_inv_contents")
            if not decide then return missing("player", "_use_on_silent_effect") end
            if not contents then return missing("player", "_inv_contents") end
            local now_result, now = contents()
            if now_result ~= "ok" or not is_table(now) then
                return "no_subject", "player._inv_contents() answered "
                    .. describe(now_result) .. " -- there is no backpack reading to compare"
            end
            -- A reading the world cannot match: one item this player does not
            -- carry.  The diff against the live backpack is therefore "lost
            -- <it>", exactly the shape a consumed item makes.
            local invented = "conformance_item_no_player_carries"
            local moved = { order = { invented }, totals = {} }
            moved.totals[invented] = 1
            local changed_result, changed_detail = decide(moved, "settle_after_click")
            if changed_result ~= "ok" then
                return changed_result, "a backpack that MOVED left the press graded "
                    .. describe(changed_result) .. " -- a use whose only effect is in a "
                    .. "container has no other evidence: " .. describe(changed_detail)
            end
            if not string.find(tostring(changed_detail), "backpack:", 1, true) then
                return "hollow", "the timeout was turned into an ok with no diff in the row, "
                    .. "so the ledger cannot say what the press moved: "
                    .. describe(changed_detail)
            end
            -- And the half that must NOT fire: nothing moved, so the caller's
            -- own timeout is handed straight back, word for word.
            local same_result, same_detail = decide(now, "settle_after_click")
            if same_result ~= "timeout" then
                return "hollow", "a backpack that did NOT move still graded "
                    .. describe(same_result) .. " -- this rule may only ever turn a timeout "
                    .. "into an ok, never invent one: " .. describe(same_detail)
            end
            if same_detail ~= "settle_after_click" then
                return "hollow", "the untouched timeout lost its own detail: "
                    .. describe(same_detail)
            end
            return "ok", "a moved backpack turns the timeout into `" .. describe(changed_detail)
                .. "`, an unmoved one is handed back as `" .. describe(same_detail) .. "`"
        end)

        -- A BACKPACK PRESS THE CLIENT DECLINES NAMES ITSELF, and is pressed
        -- again.  The tab is switched away first, because that is the state
        -- the seam was measured in and the one every quest meets: `ui.tab` is
        -- a button press that returns as soon as the click is TAKEN, and the
        -- sidebar's own CS2 paints the backpack's cells a frame later, so a
        -- press issued behind it finds no displayed node carrying the
        -- container's component id.
        --
        -- Before the seam that was reported as `timeout ... -> 1 left` with an
        -- empty detail -- indistinguishable, from a quest file, from a server
        -- that received the press and ignored it.  Two quests were filed
        -- BLOCKED against content over it.  What this row requires is the
        -- refusal SENTENCE and the retry that followed it, both in the detail,
        -- and the engine's own no-script answer at the end of it -- which is
        -- the proof the second press really did reach the server.
        seam("seam.inv_press_names_its_refusal", function()
            local fn = verb("player", "inv_op")
            local tab = verb("ui", "tab")
            if not fn then return missing("player", "inv_op") end
            if not tab then return missing("ui", "tab") end
            local tab_result = tab(TAB_AWAY_FROM_BACKPACK)
            settle(3)
            local result, detail = fn(SEAM_OBJ_SYMBOL, INV_OP_UNCLAIMED)
            local text = "tab " .. describe(tab_result) .. " -> " .. describe(detail)
            local graded, answer = no_script_probe(result, detail, "")
            if graded ~= "ok" then
                return graded, "the press did not reach the server -- " .. text
                    .. " (" .. describe(answer) .. ")"
            end
            if not string.find(tostring(detail), "pressed on attempt", 1, true) then
                return "hollow", "the press landed on the first attempt, so this row proved "
                    .. "nothing about the refusal path -- the backpack was already painted: "
                    .. text
            end
            if not string.find(tostring(detail), "no DISPLAYED node carries that component id",
                    1, true) then
                return "hollow", "it retried, but the first refusal was not named, so a quest "
                    .. "still cannot tell a declined press from an ignored one: " .. text
            end
            return "ok", "the first press was declined BY NAME and the second reached the "
                .. "server -- " .. text
        end)

        -- THE PRESS NAMES THE COPY THE SQUARE SERVES.
        --
        -- A loc is not one tile and it is not one COPY either: fishingcompo's
        -- `garlicpipe` is three wall decorations in a row, every one of them
        -- served by the server from its own square and from no neighbour at
        -- all (collision_test_wdecor has arms for the diagonal decors 6/7/8
        -- and none for 4/5, so the exact-tile shortcut is the whole reach
        -- set).  The retry already ended its candidate list with those
        -- squares -- and then let the PROJECTION choose which copy to press.
        -- drive_pointer_screen_position_loc ranks copies by the distance from
        -- the player's tile ORIGIN to a copy's footprint CENTROID, half a tile
        -- apart by construction, so the copy underfoot ties with both of its
        -- neighbours at 8192 and the scenery pool's order breaks the tie: the
        -- same square answered `You stash the garlic in the pipe.` on one run
        -- and `I can't reach that!` on the next with nothing about the world
        -- changed.
        --
        -- So the candidate carries its copy's element id and the press is
        -- aimed at it.  The verb above cannot show this -- use_on answers
        -- `refused I can't reach that!` whether it pressed the wrong copy or
        -- the right one from the wrong tile -- so the row is taken on the
        -- pairing and the rewrite, which are the two halves that were missing.
        -- Nothing is pressed and nothing moves: both are free reads.
        seam("seam.reach_names_the_copy", function()
            local candidates_of = verb("player", "_reach_candidates")
            local named_pos = verb("drive", "_named_copy_pos")
            local loc_near = verb("world", "loc_near")
            if not candidates_of then return missing("player", "_reach_candidates") end
            if not named_pos then return missing("drive", "_named_copy_pos") end
            if not loc_near then return missing("world", "loc_near") end
            local seam_target = seam_loc_target()
            if not seam_target then
                return "no_subject", "player.by_symbol(loc, " .. LOC_SYMBOL .. ") built no target"
            end

            -- The pool half.  An own-square candidate is only worth anything
            -- if the copy it came out of travelled with it, and the ONE place
            -- that pairing exists is the pool row _reach_candidates read.
            local pool_result, pool_row = loc_near(LOC_SYMBOL, 12)
            local candidates = candidates_of(seam_target)
            if not is_table(candidates) or #candidates == 0 then
                return "not_found", "no approach tile for " .. LOC_SYMBOL
                    .. ": " .. describe(candidates)
            end
            local named = nil
            local own = 0
            for index = 1, #candidates do
                local entry = candidates[index]
                if entry.own then
                    own = own + 1
                    if is_number(entry.element_id) then
                        named = named or entry
                    elseif pool_result == "ok" then
                        -- nil is legitimate for ONE candidate only: the
                        -- _target_tile fallback, which carries no pool row.
                        -- With the loc in the pool there is no such fallback.
                        return "refused", "the loc's own square " .. describe(entry.x) .. ","
                            .. describe(entry.z) .. " carries no element id while "
                            .. LOC_SYMBOL .. " IS in the pool (" .. describe(pool_row)
                            .. ") -- the press from it would let the projection choose"
                    end
                end
            end
            if own == 0 then
                return "no_subject", LOC_SYMBOL .. " contributed no own-square candidate ("
                    .. describe(#candidates) .. " approach tile(s))"
            end
            if not named then
                return "no_subject", "no copy of " .. LOC_SYMBOL .. " is in the loc pool, so "
                    .. "every own square came from the _target_tile fallback (" .. describe(pool_result) .. ")"
            end

            -- The rewrite, and it is a STRICT NO-OP for every target that
            -- names nothing -- which is every press in the suite but this
            -- retry's own.  Same table back, not an equal one: an ordinary
            -- press must not even be re-boxed.
            local pos = { x = 137, y = 241, element_id = -7 }
            seam_target.reach_element = nil
            if named_pos(seam_target, pos) ~= pos then
                seam_target.reach_element = nil
                return "refused", "_named_copy_pos rewrote a press that named no copy -- every "
                    .. "ordinary click in the suite would be aimed at something"
            end

            -- And it names the copy when one is named, keeping the pixel it
            -- was handed: WHICH square to stand on is the candidate list's
            -- decision and the projection's only job is to hand back a pixel.
            seam_target.reach_element = named.element_id
            local aimed = named_pos(seam_target, pos)
            seam_target.reach_element = nil
            if not is_table(aimed) or aimed == pos then
                return "refused", "_named_copy_pos did not name the copy the square serves: "
                    .. describe(aimed)
            end
            if aimed.element_id ~= named.element_id or aimed.x ~= pos.x or aimed.y ~= pos.y then
                return "refused", "the aimed press is not the same pixel carrying the named "
                    .. "element: asked " .. describe(named.element_id) .. " at "
                    .. describe(pos.x) .. "," .. describe(pos.y) .. ", got " .. describe(aimed)
            end
            return "ok", describe(own) .. " own square(s) of " .. LOC_SYMBOL
                .. ", each carrying its copy's element id (" .. describe(named.element_id)
                .. " at " .. describe(named.x) .. "," .. describe(named.z)
                .. "); the rewrite is a strict no-op when nothing is named and keeps the pixel "
                .. "when one is"
        end)

        -- A DEATH IS A SENTENCE, AND THE SENTENCE IS MATCHED EXACTLY.
        --
        -- QD.player._death_fence ENDS THE RUN, so the fence itself cannot be
        -- fired in here -- this harness would stop at the row that fired it.
        -- What can be proved, and is the fragile half, is the READING under
        -- it: the line is recognised with its colour codes on and its
        -- whitespace trimmed (rs_game_events.c:482's own rule, whose test pins
        -- "@red@Oh dear, you are dead!"), a line that merely QUOTES it is not
        -- a death, and a run nothing has killed latches nothing.
        --
        -- That last one is not a formality.  A `_death_record` that answered
        -- truthy on a living character would end EVERY quest in the suite at
        -- its first click settle, since combat.lua wraps _settle_after_click.
        seam("seam.death_line_is_read", function()
            local plain = verb("player", "_plain_line")
            local record = verb("player", "_death_record")
            if not plain then return missing("player", "_plain_line") end
            if not record then return missing("player", "_death_record") end
            local line = is_table(t.player) and t.player.DEATH_LINE or nil
            if not is_text(line) then
                return "hollow", "QD.player.DEATH_LINE is not a sentence: " .. describe(line)
            end

            if plain(line) ~= line then
                return "refused", "the plain sentence does not survive the strip: "
                    .. describe(plain(line))
            end
            if plain("@red@" .. line) ~= line then
                return "refused", "a COLOURED death line does not read as the death line ("
                    .. describe(plain("@red@" .. line)) .. ") -- the one sentence this driver "
                    .. "must never miss would be missed"
            end
            if plain("  " .. line .. "  ") ~= line then
                return "refused", "the strip does not trim, so a padded line reads as a "
                    .. "different sentence: " .. describe(plain("  " .. line .. "  "))
            end
            if plain("The mourner says: " .. line .. " Go away.") == line then
                return "refused", "a line that merely QUOTES the death sentence reads as a "
                    .. "death -- every run would end on content's own dialogue"
            end
            if plain(nil) ~= "" or plain(42) ~= "" then
                return "refused", "a non-string ring row raises or answers something other than "
                    .. "the empty string, and a raise in this sandbox ends the run"
            end

            -- The latch, on a character this harness has been driving for
            -- eighty rows and has not killed.
            local live = record()
            if live ~= nil then
                return "refused", "a death is recorded on a run that has killed nobody: "
                    .. describe(live) .. " -- the fence would end every quest in the suite at "
                    .. "its first click"
            end
            return "ok", "'" .. line .. "' is read coloured, padded and plain, is NOT read out "
                .. "of a line that quotes it, survives a non-string row, and nothing is latched "
                .. "on a living character"
        end)

        -- A KILL IS NEVER PROVED BY AN EMPTY POOL, AND A REFUSED SWING IS NOT
        -- AN OPENING -- the two arbitrations the 2026-09-21 combat seam pass
        -- landed, graded here because the verbs above them answer the same
        -- word whether either one works or not.
        --
        -- (1) `no_row` from the npc pool is the CLIENT's reading, and a slot
        -- leaves that pool for three reasons, only one of which is a death:
        -- the npc died, the npc ranked past DRIVE_UI_POOL_CAP's 64th nearest
        -- (Mort'ton's shade street holds more than 64), or THE PLAYER LEFT
        -- THE SCENE -- and the commonest way a player leaves a scene
        -- mid-fight is by dying.  Roving Elves spent its whole tail on the
        -- first of those being assumed: `killGuardian.await_dead PASS ...
        -- dead after 131 tick(s)` for a Moss Guardian that was alive at 2/30
        -- while the CHARACTER was the one who fell, then three FAIL rows
        -- blaming a seed nothing had dropped (build/quest_gate/rovingelves,
        -- three byte-identical runs; TORIRSSERVER_COMBAT_TRACE holds the
        -- interaction latch to tick 145 and TORIRSSERVER_HP_TRACE reads
        -- `tick=146 hp=0 dying=1`).  Two halves, and both are graded:
        -- `await_dead_engaged` separates the readings with the stamp's own
        -- `bar_seen` -- a health bar is only ever sent once something has HIT
        -- an npc, so gone-after-a-bar is a kill and gone with no bar ever
        -- sent is a target nothing ever fought -- and the death fence reads a
        -- STATED hitpoints of 0 as well as the chat line, because
        -- `[queue,player_death]` prints its sentence about twenty ticks after
        -- the killing blow and that gap is exactly where a lost fight lives.
        --
        -- (2) The engine's own single-way refusal has no packet and no log
        -- line: `ToriRSServer_CombatSinglewayRefuses` prints one sentence
        -- through content and `p_opnpc` takes its silent `return 1`, so
        -- `t.player.attack` answered `ok` for the press and `timeout` for the
        -- wait -- which its own banner calls the ordinary opening of a fight.
        -- Shades of Mort'ton believed it was fighting for twenty rounds and
        -- 1,643 ticks on that (build/quest_gate/mortton row 23).  The
        -- sentence is matched EXACTLY, never as a substring, because an npc
        -- quoting it is an npc talking.
        --
        -- All five statements are made without a fight in them: the pure
        -- matcher on its own sentences, the two death-text paths on
        -- synthetic records, the fence on the living character this harness
        -- has been driving for a hundred rows, and the bar arbitration on a
        -- SLOT NUMBER THE POOL DOES NOT HOLD -- which is the reading the
        -- whole seam turns on, reproduced without killing anything.
        seam("seam.no_row_is_not_a_kill", function()
            local refusal_line = verb("_combat_refusal_line")
            local row_by_slot = verb("_combat_row_by_slot")
            local death_seen = verb("player", "_death_seen")
            local death_text = verb("player", "_death_text")
            local ring_text = verb("player", "_death_text_from_ring")
            local await_engaged = verb("npc", "await_dead_engaged")
            if not refusal_line then return missing("_combat_refusal_line") end
            if not row_by_slot then return missing("_combat_row_by_slot") end
            if not death_seen then return missing("player", "_death_seen") end
            if not death_text then return missing("player", "_death_text") end
            if not ring_text then return missing("player", "_death_text_from_ring") end
            if not await_engaged then return missing("npc", "await_dead_engaged") end

            local death_line = is_table(t.player) and t.player.DEATH_LINE or nil
            if not is_text(death_line) then
                return "hollow", "QD.player.DEATH_LINE is not a sentence: " .. describe(death_line)
            end

            -- (a) THE ENGINE'S TWO REFUSAL SENTENCES, EXACTLY.
            local lines = t.ATTACK_REFUSAL_LINES
            if not is_table(lines) or #lines < 2 then
                return "hollow", "t.ATTACK_REFUSAL_LINES is not a list of the engine's own "
                    .. "refusal sentences: " .. describe(lines)
            end
            for i = 1, #lines do
                local sentence = lines[i]
                if not is_text(sentence) then
                    return "hollow", "t.ATTACK_REFUSAL_LINES[" .. tostring(i)
                        .. "] is not a sentence: " .. describe(sentence)
                end
                if refusal_line(sentence) ~= sentence then
                    return "refused", "the engine's own refusal '" .. sentence
                        .. "' does not read as one (" .. describe(refusal_line(sentence))
                        .. ") -- t.player.attack would answer `timeout` for a swing that was "
                        .. "never made, and the caller would wait longer for it"
                end
                if refusal_line("   " .. sentence .. "  ") ~= sentence then
                    return "refused", "a padded '" .. sentence .. "' does not read as a refusal: "
                        .. describe(refusal_line("   " .. sentence .. "  "))
                end
                if refusal_line("The guard says: " .. sentence .. " Move along.") ~= nil then
                    return "refused", "an npc QUOTING '" .. sentence .. "' reads as the ENGINE "
                        .. "refusing -- a line of dialogue would end fights the server never refused"
                end
            end
            if refusal_line(nil) ~= nil or refusal_line(42) ~= nil then
                return "refused", "a non-string chat row answers something other than nil, and a "
                    .. "raise in this sandbox ends the run"
            end

            -- (b) THE TWO DEATH RECORDS KEEP THEIR OWN SENTENCES.  combat.lua
            -- wraps state.lua's `_death_text` rather than editing it (it is
            -- the last part in DRIVE_SCRIPT_PARTS and state.lua belongs to
            -- another seam), so both paths have to be stated here or the wrap
            -- can swallow the ring's sentence with nothing going red.
            local mine = death_text({ text = "SENTINEL-DEATH-TEXT" })
            if mine ~= "SENTINEL-DEATH-TEXT" then
                return "refused", "a record latched by the HITPOINTS fence does not carry its own "
                    .. "sentence through t.player._death_text: " .. describe(mine)
            end
            local ring = death_text({ deaths = 1, wake = "", tick = 0,
                where = "nowhere", hitpoints = "0/10" })
            if not is_text(ring) or not string.find(ring, death_line, 1, true) then
                return "refused", "a record latched from the CHAT RING no longer gets state.lua's "
                    .. "own sentence back: " .. describe(ring)
            end

            -- (c) THE HITPOINTS ARM DOES NOT FIRE ON A LIVING CHARACTER.  The
            -- other side of (b): a fence that latches on a healthy read would
            -- end every quest in the suite at its first combat verb.
            local seen = death_seen()
            if seen ~= nil then
                return "refused", "the hitpoints fence latched a death on the character this "
                    .. "harness has been driving for a hundred rows: " .. describe(seen)
            end
            if t._death ~= nil then
                return "refused", "QD._death is latched with nothing dead: " .. describe(t._death)
            end

            -- (d) AN ABSENT SLOT IS A KILL ONLY AFTER A HEALTH BAR.
            --
            -- The stamp is synthetic and the slot is one the pool does not
            -- hold -- which is the whole reading this seam is about, and the
            -- only way to state it without a death or a 64-npc street.  The
            -- harness's own `npc.await_dead_engaged` row a hundred rows above
            -- has already consumed the real stamp; this saves and restores it
            -- anyway, because a seam row must leave the driver as it found it.
            local held = t._combat_last
            local slot = nil
            local absent = { 65535, 65534, 65533, 65532 }
            for i = 1, #absent do
                local result = row_by_slot(absent[i])
                if result == "no_row" then
                    slot = absent[i]
                    break
                end
            end
            if slot == nil then
                t._combat_last = held
                return "hollow", "no slot number could be found that the npc pool does not hold, "
                    .. "so the absent-slot arbitration cannot be stated"
            end

            local stamp = {
                symbol = "conformance_absent_slot",
                slot = slot,
                op = 2,
                npc_id = -1,
                name = "a slot the npc pool does not hold",
                health_before = "no bar",
                health = "no bar",
                bar_seen = false,
                tick = 0,
                consumed = false,
            }
            t._combat_last = stamp
            local no_bar_result, no_bar_detail = await_engaged(1)
            local consumed_without_a_bar = stamp.consumed

            stamp.bar_seen = true
            stamp.consumed = false
            t._combat_last = stamp
            local bar_result, bar_detail = await_engaged(1)
            local consumed_after_a_bar = stamp.consumed
            t._combat_last = held

            if no_bar_result ~= "no_row" then
                return "refused", "an absent slot that NOTHING EVER HIT -- no health bar was ever "
                    .. "sent for it -- is read as a kill: await_dead_engaged answered "
                    .. describe(no_bar_result) .. " / " .. describe(no_bar_detail)
                    .. " -- a hunt loop credits a corpse it never made"
            end
            if consumed_without_a_bar then
                return "refused", "the stamp was CONSUMED for a slot nothing ever hit, so the "
                    .. "caller's loop cannot press Attack again on the same engagement"
            end
            if bar_result ~= "ok" then
                return "refused", "an absent slot the server HAD sent a health bar for is no longer "
                    .. "read as a kill: await_dead_engaged answered " .. describe(bar_result)
                    .. " / " .. describe(bar_detail) .. " -- every real kill in the suite goes red"
            end
            if not consumed_after_a_bar then
                return "refused", "a resolved fight did not consume its stamp, so a second wait with "
                    .. "no new Attack in between would read the same kill again"
            end

            return "ok", "both engine refusals read exactly and never out of a line that quotes "
                .. "one; a fence-latched record carries its own sentence and a ring-latched one "
                .. "still carries state.lua's; nothing is latched on a living character; and slot "
                .. tostring(slot) .. ", absent from the npc pool, is a kill ONLY after a health bar "
                .. "(" .. describe(bar_result) .. ") and `no_row` without one ("
                .. describe(no_bar_result) .. ", stamp not consumed)"
        end)

        -- AN NPC'S OWN SQUARE IS STEPPED OFF BEFORE THE PRESS.
        --
        -- _step_off_for_click ran for the LOC half only, and the line that
        -- excluded the npc half said why: "an npc that shares the player's
        -- square is walking and will leave it".  True of a wanderer, false of
        -- everything the scaffold aims at -- a generated quest file walks to
        -- the npc's own `configs/*.spawn` row and `goto_tile` is `::goto`, a
        -- TELEPORT, so it puts the player INSIDE a stationary npc.  The press
        -- that follows is taken with the player's own model standing in the
        -- target's spot, and the eye orbits the PLAYER, so that model is
        -- between him and the target from every yaw the pose loop can reach.
        -- Enter the Abyss read `covered ... no frame hittested any of 3 pixels
        -- around the projected 382,250` -- the middle of the viewport, which
        -- is where the player is drawn.
        --
        -- The subject is the Duke: `duke_of_lumbridge 3212 3220 1` is his
        -- WHOLE *.spawn row (areas/world/configs/m50_50.spawn:168) -- no
        -- wander radius after the tile, so he is still standing on it -- and
        -- it is the tile player.goto_tile's own row teleports to.
        --
        -- THIS ROW MAKES THAT TELEPORT ITSELF rather than inheriting it.  The
        -- first cut read it off the goto_tile row forty rows up and answered
        -- `no_subject duke_of_lumbridge is not in the pool (no_row)`, because
        -- everything between them -- player.attack, npc.await_dead, and the
        -- reach/press seams that walk to a tree -- leaves the player on the
        -- ground floor of the courtyard, a plane and ninety tiles from the
        -- Duke's room (build/conformance_alone.log, 2026-09-20).
        --
        -- BOTH HALVES ARE TAKEN, because either one alone is believable for
        -- the wrong reason: the GATE (_step_off_for_click, handed an npc
        -- target on the player's own square, moves him off it) and then the
        -- PRESS (an ordinary t.player.talk_to, taken from inside him, answers
        -- `ok`) -- which is the sentence the seam's own reproduction could not
        -- get.  LAST of the world rows: it leaves the player upstairs in the
        -- castle, and it closes the Duke's dialogue behind it.
        seam("seam.npc_shared_tile", function()
            local standoff_for = verb("player", "_standoff_for_kind")
            local step_off = verb("player", "_step_off_for_click")
            local stand_on = verb("player", "_stand_on_square")
            local goto_tile = verb("player", "goto_tile")
            local by_symbol = verb("player", "by_symbol")
            local talk_to = verb("player", "talk_to")
            local nearest = verb("npc", "nearest")
            local await_present = verb("npc", "await_present")
            local tile_of = verb("world", "tile")
            local close_chat = verb("chat", "close")
            if not standoff_for then return missing("player", "_standoff_for_kind") end
            if not step_off then return missing("player", "_step_off_for_click") end
            if not stand_on then return missing("player", "_stand_on_square") end
            if not goto_tile then return missing("player", "goto_tile") end
            if not by_symbol then return missing("player", "by_symbol") end
            if not talk_to then return missing("player", "talk_to") end
            if not nearest then return missing("npc", "nearest") end
            if not tile_of then return missing("world", "tile") end

            -- The rule, per kind, before the behaviour: `obj` is nil on
            -- purpose (a ground stack is taken from ON TOP of it) and so is
            -- `player`, and a kind that answers a standoff it should not would
            -- walk the suite a tile per click.
            if standoff_for("npc") ~= 1 then
                return "refused", "an npc's standoff is " .. describe(standoff_for("npc"))
                    .. ", not 1 -- this gate fires at distance 0 and nowhere else"
            end
            if not is_number(standoff_for("loc")) then
                return "refused", "the loc half lost its standoff: " .. describe(standoff_for("loc"))
            end
            if standoff_for("obj") ~= nil or standoff_for("player") ~= nil then
                return "refused", "a ground stack or another player answers a standoff (obj="
                    .. describe(standoff_for("obj")) .. " player=" .. describe(standoff_for("player"))
                    .. ") -- an obj is picked up from on top of it"
            end

            -- The subject, fetched where he lives.
            local goto_result, goto_detail = goto_tile(GOTO_TILE_X, GOTO_TILE_Z, GOTO_TILE_LEVEL)
            if goto_result ~= "ok" then
                return goto_result, "player.goto_tile(" .. describe(GOTO_TILE_X) .. ","
                    .. describe(GOTO_TILE_Z) .. "," .. describe(GOTO_TILE_LEVEL) .. ") did not "
                    .. "reach " .. STATIONARY_NPC_SYMBOL .. "'s room: " .. describe(goto_detail)
            end
            -- THE PLANE FIRST, then the pool, and the pool is WAITED FOR.
            --
            -- This row failed `no_subject -- duke_of_lumbridge is not in the
            -- pool (no_row)` on one suite run and passed on the next two with
            -- nothing changed (2026-09-20/21), which is the signature of a
            -- read taken a tick early rather than of a wrong tile.  An
            -- absolute-tile teleport ACROSS A PLANE re-mounts the scene, and
            -- goto_tile's own arrival await is "the tile and the npc pool
            -- around it are visible" -- around the tile it asked for, not
            -- around the npc, and the rows above this one leave the player
            -- twenty tiles away on level 0, which is the longest jump any row
            -- in this file makes.  npc.await_present is the verb for "the pool
            -- has caught up" and it has its own conformance row above; a
            -- single `nearest` here was a poll of one.
            --
            -- The plane is read back first so the two failures cannot be
            -- confused: a teleport that landed on the ground floor is a fact
            -- about ::goto, and an empty pool on the RIGHT floor is a fact
            -- about the scene mount.
            local landed_result, landed = tile_of()
            if landed_result ~= "ok" or not is_table(landed) then
                return landed_result, "no tile reading after player.goto_tile"
            end
            if landed.level ~= GOTO_TILE_LEVEL then
                return "no_subject", "player.goto_tile(" .. describe(GOTO_TILE_X) .. ","
                    .. describe(GOTO_TILE_Z) .. "," .. describe(GOTO_TILE_LEVEL)
                    .. ") landed on level " .. describe(landed.level) .. " at "
                    .. describe(landed.x) .. "," .. describe(landed.z)
                    .. " -- the plane is the half of an absolute-tile teleport that is "
                    .. "never allowed to be one out"
            end
            if await_present then
                await_present(STATIONARY_NPC_SYMBOL, 4, 10)
            end
            local duke_state, duke = nearest(STATIONARY_NPC_SYMBOL, 4)
            if duke_state ~= "ok" or not is_table(duke) then
                return "no_subject", STATIONARY_NPC_SYMBOL .. " is not in the pool within 4 of "
                    .. describe(GOTO_TILE_X) .. "," .. describe(GOTO_TILE_Z) .. " L"
                    .. describe(GOTO_TILE_LEVEL) .. " after 10 tick(s) of npc.await_present, "
                    .. "with the player standing at " .. describe(landed.x) .. ","
                    .. describe(landed.z) .. " L" .. describe(landed.level) .. " ("
                    .. describe(duke_state) .. " " .. describe(duke)
                    .. ") -- his *.spawn row is not where this row thinks"
            end

            -- ON HIS SQUARE, exactly: goto_tile answers on Chebyshev 1 and one
            -- tile out is the case that already works.
            local function stand_inside()
                stand_on(duke.x, duke.z, duke.level)
                local tile_result, tile = tile_of()
                if tile_result ~= "ok" or not is_table(tile) then
                    return nil, describe(tile_result) .. " " .. describe(tile)
                end
                if tile.x ~= duke.x or tile.z ~= duke.z or tile.level ~= duke.level then
                    return nil, "stands at " .. describe(tile.x) .. "," .. describe(tile.z)
                        .. " L" .. describe(tile.level)
                end
                return tile, nil
            end

            local on_tile, not_inside = stand_inside()
            if not on_tile then
                return "no_subject", "::goto would not put the player on " .. STATIONARY_NPC_SYMBOL
                    .. "'s square (asked " .. describe(duke.x) .. "," .. describe(duke.z)
                    .. " L" .. describe(duke.level) .. ", " .. describe(not_inside) .. ")"
            end

            local target, target_result = by_symbol("npc", STATIONARY_NPC_SYMBOL)
            if target_result ~= "ok" or not is_table(target) then
                return target_result, "player.by_symbol(npc, " .. STATIONARY_NPC_SYMBOL
                    .. ") built no target"
            end
            local stepped, step_detail = step_off(target)
            local after_result, after = tile_of()
            if after_result ~= "ok" or not is_table(after) then
                return after_result, "no tile reading after the step-off"
            end
            if after.x == on_tile.x and after.z == on_tile.z then
                return "refused", "the player is STILL standing inside " .. STATIONARY_NPC_SYMBOL
                    .. " at " .. describe(after.x) .. "," .. describe(after.z)
                    .. " (_step_off_for_click -> " .. describe(stepped) .. " "
                    .. describe(step_detail) .. ") -- the press would hittest his own model"
            end

            -- AND THE PRESS LANDS FROM IN THERE.  Back onto his square, and
            -- nothing private is called this time: talk_to reaches the same
            -- gate on its own way to the pixel, so `ok` here is the whole
            -- behaviour the seam is for, not a helper answering about itself.
            local second, not_inside_again = stand_inside()
            if not second then
                return "no_subject", "the second ::goto did not land back inside "
                    .. STATIONARY_NPC_SYMBOL .. " (" .. describe(not_inside_again) .. ")"
            end
            local talk_result, talk_detail = talk_to(STATIONARY_NPC_SYMBOL)
            local talked_result, talked = tile_of()
            if close_chat then
                close_chat()
            end
            if talk_result ~= "ok" then
                return talk_result, "a talk_to taken from INSIDE " .. STATIONARY_NPC_SYMBOL
                    .. " at " .. describe(second.x) .. "," .. describe(second.z) .. " answered "
                    .. describe(talk_result) .. " -- " .. describe(talk_detail)
            end
            if talked_result ~= "ok" or not is_table(talked) then
                return talked_result, "no tile reading after the talk"
            end
            if talked.x == second.x and talked.z == second.z then
                return "refused", "the talk answered ok with the player never having left "
                    .. STATIONARY_NPC_SYMBOL .. "'s square " .. describe(talked.x) .. ","
                    .. describe(talked.z) .. " -- the press did not go through this gate, so "
                    .. "the row proves nothing about it"
            end
            return "ok", "stood on " .. STATIONARY_NPC_SYMBOL .. "'s own spawn square "
                .. describe(on_tile.x) .. "," .. describe(on_tile.z) .. " L"
                .. describe(on_tile.level) .. ": the gate moved the player to "
                .. describe(after.x) .. "," .. describe(after.z) .. " ("
                .. describe(stepped) .. "), and a talk_to taken from that square again answered "
                .. "ok from " .. describe(talked.x) .. "," .. describe(talked.z) .. " ("
                .. describe(talk_detail) .. "); npc standoff 1, obj and player none"
        end)

        -- ------------------------------- phase 9b: the shop
        --
        -- SEAM shop_purchase_verb (2026-09-21).  The first three verbs in this
        -- driver that open, read or press a shop screen, and before them a
        -- quest whose own script makes the player BUY something could not be
        -- written at all -- Shades of Mort'ton's temple leg needs timber,
        -- limestone bricks and swamp paste, `timberbeam` exists in exactly one
        -- place in this content pack (`razmire_builders_merchants.inv`, the
        -- store Razmire opens as the quest`s own reward), and trap 16 forbids
        -- `::give`ing a quest's own deliverable.
        --
        -- Here, against a shop that needs no quest state.  LAST of the rows
        -- that touch the world, and it has to be: the stage below opens with
        -- `::clearinv`, because a backpack with no free slot answers a buy
        -- with content's own inventory-space refusal and would red these rows
        -- for a reason that is not the verb -- and that `::clearinv` takes
        -- SEAM_OBJ_SYMBOL away from the seam rows above (measured: put this
        -- block before them and `seam.use_on_rearm` and
        -- `seam.inv_press_names_its_refusal` both go red with `mindrune: not
        -- in the backpack`).  Nothing below this block reads the world.
        -- The journey is real either way: the fight leaves the player in
        -- Lumbridge castle's courtyard and the store is inside it.
        stage(function()
            setup_cheat("::clearinv")
            settle(2)
            setup_cheat("::give coins " .. SHOP_COINS)
            settle(2)
            local goto_tile = verb("player", "goto_tile")
            if goto_tile then
                goto_tile(SHOP_TILE_X, SHOP_TILE_Z, 0)
            end
            settle(2)
        end)

        step("shop.open", function()
            local fn = verb("shop", "open")
            if not fn then return missing("shop", "open") end
            local result, detail = fn(SHOP_NPC_SYMBOL, SHOP_NPC_OP, SHOP_INV_SYMBOL)
            local text = SHOP_NPC_SYMBOL .. " op " .. SHOP_NPC_OP .. " / "
                .. SHOP_INV_SYMBOL .. " -> " .. describe(result) .. " " .. describe(detail)
            if result ~= "ok" then
                return result, text
            end
            -- An `ok` here has to mean the STOCK has landed, not merely that
            -- shopmain mounted: the container arrives in a second server
            -- message and a buy issued against an empty grid presses a cell
            -- that is not there yet.  The verb says how many stocked slots it
            -- found, and that sentence is the evidence.
            if not string.find(tostring(detail), "stocked slot", 1, true) then
                return "hollow", "an `ok` that does not say what stock landed -- the grid "
                    .. "arrives a message after the interface and a buy against an empty "
                    .. "one presses nothing: " .. text
            end
            return "ok", text
        end)

        step("shop.buy", function()
            local fn = verb("shop", "buy")
            local count_of = verb("inv", "count")
            if not fn then return missing("shop", "buy") end
            if not count_of then return missing("inv", "count") end
            local held_before_result, held_before = count_of(SHOP_OBJ_SYMBOL)
            local coins_before_result, coins_before = count_of("coins")
            local result, detail = fn(SHOP_OBJ_SYMBOL, SHOP_OBJ_COUNT)
            local text = SHOP_OBJ_SYMBOL .. " x" .. SHOP_OBJ_COUNT .. " -> "
                .. describe(result) .. " " .. describe(detail)
            if result ~= "ok" then
                return result, text
            end
            -- The detail is the verb's own arithmetic; this is the world's.
            -- A buy verb that answered ok without the backpack moving is the
            -- exact false green the ladder makes easy -- four fixed rungs, and
            -- a press on the wrong cell buys the next item along.
            local held_after_result, held_after = count_of(SHOP_OBJ_SYMBOL)
            local coins_after_result, coins_after = count_of("coins")
            if held_before_result ~= "ok" or held_after_result ~= "ok" then
                return "hollow", "inv.count could not read " .. SHOP_OBJ_SYMBOL
                    .. " (" .. describe(held_before_result) .. " / "
                    .. describe(held_after_result) .. ") -- " .. text
            end
            if held_after - held_before ~= SHOP_OBJ_COUNT then
                return "hollow", "the backpack gained " .. tostring(held_after - held_before)
                    .. " " .. SHOP_OBJ_SYMBOL .. ", not " .. SHOP_OBJ_COUNT .. " -- " .. text
            end
            if coins_before_result == "ok" and coins_after_result == "ok"
                and coins_after >= coins_before then
                return "hollow", "coins went " .. tostring(coins_before) .. " -> "
                    .. tostring(coins_after) .. ": nothing was paid, so nothing was bought "
                    .. "from a shop -- " .. text
            end
            return "ok", text .. "; backpack " .. tostring(held_before) .. " -> "
                .. tostring(held_after) .. ", coins " .. tostring(coins_before) .. " -> "
                .. tostring(coins_after)
        end)

        step("shop.close", function()
            local fn = verb("shop", "close")
            local buy = verb("shop", "buy")
            if not fn then return missing("shop", "close") end
            local result, detail = fn()
            local text = "close -> " .. describe(result) .. " " .. describe(detail)
            if result ~= "ok" then
                return result, text
            end
            -- Interface 300 has no close button (its nineteen components are
            -- the frame, the quantity bar, the grid and the scrollbar), so
            -- this is the ESC path -- and the only honest proof that it took
            -- is that the screen is really gone.  `shop.buy` with nothing
            -- open answers `no_row`; anything else means close returned on a
            -- shop that is still up and the next quest row would press into
            -- it.
            if buy then
                local after = buy(SHOP_OBJ_SYMBOL, 1)
                if after ~= "no_row" then
                    return "hollow", "after close, shop.buy answers " .. describe(after)
                        .. " rather than no_row -- the screen did not go: " .. text
                end
                text = text .. "; a buy after it -> no_row"
            end
            -- Idempotent, because a quest file closes a shop it may already
            -- have closed and that is not an error.
            local again = fn()
            if again ~= "ok" then
                return "hollow", "a second close answers " .. describe(again)
                    .. ", not ok -- " .. text
            end
            return "ok", text .. "; a second close -> ok"
        end)

        -- --------- seam: a mistyped ledger argument must not end the run
        --
        -- SEAM ledger_verdict_type_kills_run (2026-09-21).  `api_drive.ledger`
        -- reads `step` and `verdict` with luaL_checkstring and this sandbox
        -- has no pcall, so a raise there does not fail one row -- it ends the
        -- WHOLE RUN where it stands.  The shape that does it is the natural
        -- one, because `t.check`'s second argument IS a condition: Between a
        -- Rock wrote `t.step(name, <expr> == "ok", detail)` at its wall of
        -- flame and threw away the 93 PASS rows already on disk
        -- (build/quest_gate/betweenarock, 2026-09-21).
        --
        -- No verb's own answer can show this: what is graded is that the run
        -- IS STILL HERE.  So this row writes the two bad calls itself and then
        -- returns -- reaching the return at all is the evidence, and before
        -- the fix neither the return nor any row below it existed.
        --
        -- The two extra rows it leaves in the ledger are named
        -- `ledger_verdict.*` and are NOT plan rows (verb_list.py reads the
        -- harness's own `step(`/`seam(` declarations, so conformance.py never
        -- scores them).  One of them is a FAIL on purpose: a verdict that is
        -- not PASS/FAIL/BLOCKED and is not a condition -- a verb's own result
        -- word, which belongs to t.expect -- says nothing about the step
        -- passing, so it is graded FAIL and named, and a FAIL row in a green
        -- conformance ledger is what that half of the fix looks like.
        seam("seam.ledger_verdict_named", function()
            local record = verb("step")
            if not record then return missing("step") end
            -- A boolean: unambiguous, so the row is graded the way the file
            -- plainly meant it and the note says the call was still wrong.
            record("ledger_verdict.boolean_probe", true,
                "a boolean verdict must be READ as PASS and named, never raised")
            -- A result word: nothing here says the step passed.
            record("ledger_verdict.word_probe", "ok",
                "a verb's own result word is not a verdict -- graded FAIL and named")
            -- And a non-string step name, which raises one line EARLIER than
            -- the ledger call (record_with_shot's `step_name_uses[nil]`), so
            -- it is a second crash site and not the same one.
            record(4242, "PASS", "a non-string step name must be coerced and named")
            return "ok", "three mistyped ledger arguments written (boolean verdict, result "
                .. "word, numeric step name); this row exists, so none of them ended the run"
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

        if verb_count ~= VERB_COUNT or seam_count ~= SEAM_COUNT then
            local recorder = verb("step")
            if recorder then
                recorder("conformance-plan", "FAIL",
                    "the plan holds " .. verb_count .. " verbs and " .. seam_count
                    .. " seam row(s); this file declares " .. VERB_COUNT .. " and "
                    .. SEAM_COUNT .. " -- run tools/quest_gate/verb_list.py to see which")
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
