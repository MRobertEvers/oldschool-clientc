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
-- 186 verbs, one row each.  tools/quest_gate/verb_list.py --check reads the
-- `step("<name>", ...)` lines below and the QD.* definitions in
-- script/plugins/quest_driver/*.lua and refuses to agree when they differ, so
-- a verb added to the driver with no row here fails a make gate rather than
-- being quietly never called.  The count is asserted in the harness too, so
-- editing this file alone cannot drift either.
-- @verb-count 186
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
-- @seam-count 123
-- ---------------------------------------------------------------------------

local VERB_COUNT = 186
local SEAM_COUNT = 123
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
-- The held op these rows press: op 3, which no script claims, so the world's
-- honest answer to it is the engine's "Nothing interesting happens."  (Until
-- seam pass 20 op 1 was also off limits: the client flashed the backpack
-- cell's on_op hook with the OPHELD index, and index 1 there is rev-239's
-- shift-click-drop chain, so every op-1 press dropped the stack.  That is
-- fixed in src/app/app_minimenu.c and guarded by seam.held_op1_keeps_the_item.)
local INV_OP_UNCLAIMED = 3
-- seam.held_op1_keeps_the_item's subject: `ifop1=Dig`, op 5 Drop, and its
-- [opheld1] consumes nothing off a dig site.
local HELD_OP1_OBJ_SYMBOL = "spade"
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
local VARP_SYMBOL = "varp281_tutorial"      -- the fixture pins it (perm scope)
local VARP_VALUE = 1000             -- "tutorial finished" (docs/WORKTREE_SETUP.md)
local VARBIT_SYMBOL = "varb0_troll_freed_eadgar"
-- A varp this content pack allocates ABOVE the id the client's varp array can
-- address (pack/varp.alloc 6262 against an all.varp.compack topping out near
-- 5704), so the server never transmits it and both client-side reads answer
-- not_found for the whole run.  The seam row below is the only thing in this
-- harness that can read it at all; if a later cache widens the client's table
-- past 6262 that row says so in its own detail rather than passing quietly.
local HIGH_ID_VARP = "varp6262_rovingelves_quest"
-- Cook's Assistant, the one quest this content pack can be driven into every
-- state of from a cheat: `::setvar cookquest ^cook_started` stages it,
-- `::cookbmp_reward` completes it and puts the real reward scroll up, and its
-- journal row is the first in the quest list.  The stage numbers are the
-- quest's own constants (OSRS-Content/.../quest_cook/configs/quest_cook.constant:
-- ^cook_not_started 0, ^cook_started 1, ^cook_complete 2) written out, because
-- quest.bind's `constants` table takes integers: there is no "constant" kind
-- in DriveSymbolKind for a `^name` to resolve through.
local QUEST_VARP = "varp29_cookquest"
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

-- The bank rows' subject (seam bank_withdraw_and_deposit_verbs,
-- matthew-mbp-m4-b56-seam2).  Lumbridge castle's top-floor booth,
-- `aide_bankbooth` at 3208,3221 level 2, op 2 "Bank" (bank_booths.rs2; the
-- other copies on that row are `_closed` and `_multi`).  The fixture's bank
-- already holds ten slots (250000 coins among them -- measured,
-- build/quest_gate/s2bank_probe1 row 9), so the rows bank two objs it does
-- NOT hold, stocked by the setup-only `::bankgive` at the top of run():
-- sharks for the exchange, bones to fill the backpack for the full-pack
-- refusal.  Both are non-stackable, so every unit is a slot and "full" is 28.
local BANK_BOOTH_SYMBOL = "aide_bankbooth"
local BANK_BOOTH_OP = 2
local BANK_TILE_X = 3208
local BANK_TILE_Z = 3219
local BANK_TILE_LEVEL = 2
local BANK_OBJ_SYMBOL = "shark"
local BANK_OBJ_STOCK = 30
local BANK_OBJ_WITHDRAW = 12
local BANK_FILL_SYMBOL = "bones"
local BANK_FILL_STOCK = 40

-- player.cast's subject (seam cast_spell_on_npc, seam19): Wind Strike on a
-- Lumbridge goblin east of the river -- m50_50.spawn:102
-- `goblin_unarmed_melee_1 3244 3247`, three tiles from CAST_TILE.  Wind
-- Strike is Magic level 1 and one air + one mind rune (magic.rs2's own
-- refusal names the rune it lacks), so a fixture character casts it as he
-- stands.  The field is SINGLE-WAY and the goblins and the giant spider by the
-- river aggress, so every type that can claim the player there is made
-- ::passive first: one aggressor and the cast is refused "I'm already under
-- attack." (measured build/quest_gate/s19cast_c, a giantspider1 claim).
-- Fire Wave at the fixture's Magic 1 is the refusal row's subject: the
-- level check runs before the rune check (magic.rs2 check_spell_requirements).
local CAST_SPELL = "wind_strike"
local CAST_REFUSED_SPELL = "fire_wave"
local CAST_NPC_SYMBOL = "goblin_unarmed_melee_1"
local CAST_TILE_X = 3241
local CAST_TILE_Z = 3247
local CAST_PASSIVE = {
    "goblin_unarmed_melee_1", "goblin_unarmed_melee_2", "goblin_unarmed_melee_3",
    "goblin_unarmed_melee_4", "goblin_unarmed_melee_5", "giantspider1",
}

-- seam.attack_presses_the_watched_slot's goblin pair (seam21): ::spawn lands at
-- the player's tile +1,+1, so these stand tiles put N at 3241,3245 (two south
-- of CAST_TILE) and F at 3242,3250 (north-east of it).  Both are open field:
-- 3243,3247 is inside the goblin house and 3237,3247 is the river
-- (build/quest_gate/s21as_gob3, s21as_gob1: "I can't reach that!").
local ATTACK_PAIR_SPAWN_N_X = 3240
local ATTACK_PAIR_SPAWN_N_Z = 3244
local ATTACK_PAIR_SPAWN_F_X = 3241
local ATTACK_PAIR_SPAWN_F_Z = 3249
-- Where F lands, and where the row stands: one tile south-west of CAST_TILE so
-- player.cast's own goblin (spawned at 3242,3248) is not the nearest copy --
-- it was, from CAST_TILE, in build/quest_gate/s21as_conf1 row 154.
local ATTACK_PAIR_F_X = 3242
local ATTACK_PAIR_F_Z = 3250
local ATTACK_PAIR_STAND_X = 3240
local ATTACK_PAIR_STAND_Z = 3246

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

-- A state verb's `ok` must NAME WHAT IT READ (seam7, 2026-09-22).  var.await,
-- var.await_server, var.expect, inv.await, inv.await_all, inv.expect_absent,
-- msg.expect, msg.await and quest.bind all used to answer a bare ok, so a
-- quest row routed through t.expect landed PASS with an empty detail column
-- -- mourningsendparti shipped 21 of them and was reverted by the sampler for
-- it.  Each now answers the value and side, the count reached or the matched
-- line, and this is where that is held: an ok whose detail is not text, or
-- does not carry every fragment the world here makes knowable, is hollow.
local function names_reading(result, detail, prefix, fragments, wanted)
    local text = prefix .. describe(detail)
    if result ~= "ok" then
        return result, text
    end
    if not is_text(detail) then
        return "hollow", "answered ok with no detail -- " .. wanted .. " -- " .. text
    end
    for i = 1, #fragments do
        if not string.find(detail, fragments[i], 1, true) then
            return "hollow", "answered ok but the detail does not carry '" .. fragments[i]
                .. "' -- " .. wanted .. " -- " .. text
        end
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
        -- The world slot the tick log knows player.attack's npc by (the client
        -- slot is NPC_INFO's per-client name, not the server's), filled by
        -- ticklog.slot before the fight and read by ticklog.rows after it.
        local ticklog_world_slot = nil

        -- ------------------------------------------------------- the world

        -- Lumbridge, an npc to point at, bread in the backpack, bread on the
        -- ground, a skill with a reading.  All setup, no rows.
        setup_cheat("::tele lumbridge")
        settle(4)
        setup_cheat("::runes 25")
        setup_cheat("::dropobj " .. OBJ_SYMBOL .. " 1")
        setup_cheat("::xp " .. STAT_SYMBOL .. " 500")
        -- The bank rows' stock (BANK_* above).  HERE and nowhere later:
        -- `::bankgive` is setup-only, and QD.cheat refuses it once the
        -- quest.bind row has bound a quest (seam.bankgive_is_setup_only).
        setup_cheat("::bankgive " .. BANK_OBJ_SYMBOL .. " " .. BANK_OBJ_STOCK)
        setup_cheat("::bankgive " .. BANK_FILL_SYMBOL .. " " .. BANK_FILL_STOCK)
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

        step("tick", function()
            local fn = verb("tick")
            if not fn then return missing("tick") end
            local result, before = fn()
            if result ~= "ok" then
                return result, describe(before)
            end
            if math.type(before) ~= "integer" then
                return "hollow", "answered ok without an integer tick: " .. describe(before)
            end
            -- The SERVER's clock: two t.ticks later it must have moved on.
            settle(2)
            local _, after = fn()
            if not is_number(after) or after <= before then
                return "hollow", "srv->tick did not advance over t.ticks(2): " .. describe(before)
                    .. " -> " .. describe(after)
            end
            return "ok", "server tick " .. before .. " -> " .. after .. " over t.ticks(2)"
        end)

        step("ticklog.start", function()
            local fn = verb("ticklog", "start")
            if not fn then return missing("ticklog", "start") end
            local result, detail = fn()
            if result ~= "ok" then
                return result, describe(detail)
            end
            if not string.find(tostring(detail), "ticklog on at tick", 1, true) then
                return "hollow", "answered ok without the tick it started on: " .. describe(detail)
            end
            -- Idempotent: a second start keeps the rows and names the same tick.
            local again, again_detail = fn()
            local first_tick = string.match(tostring(detail), "at tick (%d+)")
            local again_tick = string.match(tostring(again_detail), "at tick (%d+)")
            if again ~= "ok" or first_tick ~= again_tick then
                return "hollow", "a second start moved the start tick: " .. describe(detail)
                    .. " then " .. describe(again_detail)
            end
            return "ok", tostring(detail) .. " [second start: same tick " .. first_tick .. "]"
        end)

        step("ticklog.mark", function()
            local fn = verb("ticklog", "mark")
            if not fn then return missing("ticklog", "mark") end
            local result, detail = fn("conformance")
            if result ~= "ok" then
                return result, describe(detail)
            end
            local rows = verb("ticklog", "rows")
            if not rows then return missing("ticklog", "rows") end
            local rows_result, marks = rows({ kind = "mark" })
            local found = false
            for _, row in ipairs(is_table(marks) and marks or {}) do
                found = found or row.label == "conformance"
            end
            if rows_result ~= "ok" or not found then
                return "hollow", "answered " .. describe(detail) .. " but no mark row reads "
                    .. "'conformance' (" .. describe(rows_result) .. ")"
            end
            return "ok", tostring(detail)
        end)

        step("shot", function()
            local fn = verb("shot")
            if not fn then return missing("shot") end
            local result, detail = fn("conformance-world")
            return answered(result, detail, "", is_text, "no file was named")
        end)

        -- ===== waves seam pass 7: shot_name_length (one seam row, no new verb) =====
        -- Counted as a SEAM row (seam_count), not a verb row: t.shot's answer word is
        -- "ok" whether or not the client cut the file name, so only the file name the
        -- answer carries can tell.  TEST-3 (CONTENT_BUGS.md): a name past 67 chars was
        -- written cut at 71 with no ".png"; torirs_plugin_drive_ui.c now writes
        -- <head49>~<fnv1a32 8 hex>~<tail8>.png (71 chars).  keep=true so the dedupe
        -- cannot answer "unchanged since ..." (the frame right after step("shot")
        -- is usually identical) and leave no file name to read.
        seam("seam.shot_name_length", function()
            local fn = verb("shot")
            if not fn then return missing("shot") end
            local long = "conformance.shot_name_length.a_ninety_character_name_the_writer_shortens_and_keeps_png"
            local result, detail = fn(long, true)
            if result ~= "ok" then
                return result, describe(detail)
            end
            local file = tostring(detail):match("([^/\\]+)$") or ""
            if #file > 71 or not file:match("%.png$") or not file:match("~%x%x%x%x%x%x%x%x~")
                or not file:match("_png%.png$") then
                return "hollow", "a " .. #long .. "-char name was written as " .. file
                    .. " (" .. #file .. " chars): cut, or not head~hash~tail.png"
            end
            return "ok", file .. " (" .. #file .. " chars, from a " .. #long .. "-char name)"
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
            return names_reading(result, detail, VARP_SYMBOL .. " == " .. VARP_VALUE .. " -> ",
                { VARP_SYMBOL .. " = " .. VARP_VALUE, "client == server" },
                "the verb names the value both sides agreed on")
        end)

        step("var.await", function()
            local fn = verb("var", "await")
            if not fn then return missing("var", "await") end
            local result, detail = fn(VARP_SYMBOL, VARP_VALUE, 3)
            return names_reading(result, detail, VARP_SYMBOL .. " == " .. VARP_VALUE .. " -> ",
                { VARP_SYMBOL .. " = " .. VARP_VALUE .. " (client" },
                "the await names the value it read and which side it read it on")
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
            return names_reading(result, detail, ABSENT_OBJ_SYMBOL .. " -> ",
                { ABSENT_OBJ_SYMBOL .. ": absent (count 0)" },
                "the assertion names the count it read")
        end)

        step("inv.await", function()
            local fn = verb("inv", "await")
            if not fn then return missing("inv", "await") end
            local result, detail = fn(OBJ_SYMBOL, 1, 3)
            return names_reading(result, detail, OBJ_SYMBOL .. " >= 1 -> ",
                { OBJ_SYMBOL .. " ", " (>= 1) after " },
                "the await names the count before and the count it reached")
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
            local named, named_text = names_reading(result, detail, OBJ_SYMBOL .. " >= 1 -> ",
                { "all held: " .. OBJ_SYMBOL .. "=" },
                "the await names every count it held")
            if named ~= "ok" then
                return named, named_text
            end
            local count = verb("inv", "count")
            local state, total = "missing", nil
            if count then state, total = count(OBJ_SYMBOL) end
            if state ~= "ok" or not is_number(total) or total < 1 then
                return "hollow", "await_all answered ok but inv.count(" .. OBJ_SYMBOL
                    .. ") reads " .. describe(total) .. " (" .. state .. ")"
            end
            return "ok", named_text .. "; inv.count agrees: " .. describe(total)
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
            return names_reading(result, detail, "contains '" .. DROP_MESSAGE_FRAGMENT .. "' -> ",
                { "matched: ", DROP_MESSAGE_FRAGMENT },
                "the verb names the line it matched")
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
            return names_reading(result, detail,
                "new line containing '" .. DROP_MESSAGE_FRAGMENT .. "' -> ",
                { "matched: ", DROP_MESSAGE_FRAGMENT },
                "the await names the line that arrived")
        end)

        -- -------------------------------- phase 3: pointing and acting

        step("drive.camera", function()
            local fn = verb("drive", "camera")
            if not fn then return missing("drive", "camera") end
            local result, detail = fn(0, 300, 400)
            return result, "yaw=0 pitch=300 zoom=400 -> " .. describe(detail)
        end)

        -- setup: stand within reach of the pointer rows' subject.  The Man
        -- WANDERS, and since seam15's npc wander parity (LostCity Npc.ts
        -- wanderMode, no per-tick go-home) the RNG stream that places him is a
        -- different one: at this row he stood inside Lumbridge castle at
        -- 3213,3225, eleven tiles from the fixture's 3222,3218, and no framing
        -- pose put him inside the viewport (build/seam_state/seam15/close/
        -- make_conformance3.log).  These rows prove projection, framing and
        -- the press -- not how far a pose can reach -- so the subject is
        -- walked to first, as every talk_to/press in a quest does.
        stage(function()
            local walk = verb("player", "walk_near")
            if walk and npc_target then
                walk(npc_target)
            end
        end)

        -- Where the pointer rows' subject stood when one of them could not
        -- frame it: the player's tile, the tile the driver aimed the camera
        -- at (QD.drive._target_tile), and every live copy.  The Man wanders,
        -- so a not_visible here is only diagnosable with the tiles in the row.
        local function subject_where()
            local parts = {}
            local tile_fn = verb("world", "tile")
            if tile_fn then
                local r, tile = tile_fn()
                parts[#parts + 1] = "player " .. describe(r) .. " " .. describe(tile)
            end
            local aim_fn = verb("drive", "_target_tile")
            if aim_fn and npc_target then
                local r, x, z = aim_fn(npc_target)
                parts[#parts + 1] = "aimed at " .. describe(r) .. " " .. describe(x) .. "," .. describe(z)
            end
            local tiles_fn = verb("npc", "tiles")
            if tiles_fn then
                local r, summary = tiles_fn(NPC_SYMBOL, 40)
                parts[#parts + 1] = describe(r) .. " " .. describe(summary)
            end
            return table.concat(parts, "; ")
        end

        step("drive.screen_position", function()
            local fn = verb("drive", "screen_position")
            if not fn then return missing("drive", "screen_position") end
            if not npc_target then
                return "no_subject", "player.by_symbol(npc, " .. NPC_SYMBOL .. ") built no target"
            end
            local result, detail = fn(npc_target)
            if result ~= "ok" then
                return result, describe(detail) .. " -- " .. subject_where()
            end
            return result, describe(detail)
        end)

        step("drive.click_minimenu", function()
            local fn = verb("drive", "click_minimenu")
            if not fn then return missing("drive", "click_minimenu") end
            if not npc_target then
                return "no_subject", "player.by_symbol(npc, " .. NPC_SYMBOL .. ") built no target"
            end
            local result, detail = fn(npc_target, 1)
            if result ~= "ok" then
                return result, "op slot 1 -> " .. describe(detail) .. " -- " .. subject_where()
            end
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
            -- SEAM walk_to_answers_ok_with_no_detail (matthew-mbp-m4-b62-seam1):
            -- an arrival answers `walk_to x,z: reached x,z,level from a,b in N
            -- tick(s)`, never (ok, nil) -- t.exec grades that hollow.
            local want_x, want_z = player_tile.x + 1, player_tile.z
            local result, detail = fn(want_x, want_z)
            return names_reading(result, detail, "one tile east -> ",
                { "walk_to " .. want_x .. "," .. want_z .. ": reached " .. want_x .. "," .. want_z .. ",",
                  " tick(s)" },
                "the tile the walk reached, read back, and its tick count")
        end)

        step("player.step_tick", function()
            local fn = verb("player", "step_tick")
            if not fn then return missing("player", "step_tick") end
            local tile = verb("world", "tile")
            local here_result, here = "missing", nil
            if tile then here_result, here = tile() end
            if here_result ~= "ok" or not is_table(here) then
                return "no_subject", "world.tile() answered no tile to step from"
            end
            local far, far_detail = fn(here.x + 2, here.z)
            if far ~= "refused" then
                return "hollow", "a tile two away answered " .. describe(far) .. " ("
                    .. describe(far_detail) .. "); step_tick takes an adjacent tile only"
            end
            local result, detail = fn(here.x - 1, here.z)
            if result ~= "ok" then
                return result, "one tile west of " .. here.x .. "," .. here.z .. " -> "
                    .. describe(detail)
            end
            local issued, resolved = string.match(tostring(detail),
                "issued at tick (%d+), resolved at tick (%d+)")
            if issued == nil or tonumber(resolved) <= tonumber(issued) then
                return "hollow", "answered ok without an issue and a later resolve tick: "
                    .. describe(detail)
            end
            return "ok", tostring(detail) .. " [two tiles away: " .. tostring(far_detail) .. "]"
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

        -- SEAM press_cannot_see_a_landed_prod (2026-09-22).  The other half of
        -- the silent press, and the half no verb's answer can show here.
        -- `npc_say` is a SAY mask on NPC_INFO and is deliberately NOT routed to
        -- the chatbox (SS_OP_NPC_SAY), so `api_drive.messages` never sees a
        -- word of it; for an `[opnpc<n>]` whose success path is `anim` +
        -- `npc_say` + `npc_walk`, and whose `npc_walk` is SILENT when the map
        -- refuses the destination, the word over the npc's head is the ONLY
        -- reading a client has that the press landed at all.  Before
        -- DriveNpcRow.overhead, 36 of the 55 presses in sheepherder's ledger
        -- answered `timeout ... nothing was said and no dialogue opened`, and
        -- every one of them had run.
        --
        -- Lumbridge still has no silent press in it (the row above says so),
        -- so what is graded here is the READER, in the two places it can
        -- rot silently:
        --   1. the C field itself.  A binary built without it answers `nil`,
        --      which every reader treats as "no reading" -- correct, and
        --      invisible: the quest rows would simply go back to timing out on
        --      success.  So the pool row must carry a STRING and a NUMBER.
        --   2. `QD.player._say_since`'s rise rule.  A herding loop presses the
        --      same npc over and over and the npc says the same word every
        --      time, so "it said it AGAIN" is not a text comparison:
        --      world_entity_set_chat resets the countdown to 150 on every
        --      message and world_cycle only ever decrements it, which makes a
        --      RISEN timer the one and only tell.  Grade it as a truth table,
        --      because a world in which a `man` says something twice on cue is
        --      not one this fixture has.
        seam("seam.press_reads_overhead", function()
            local say_since = t.player and t.player._say_since
            local tiles = verb("npc", "tiles")
            if not say_since or not tiles then
                return "unsupported", "player._say_since / npc.tiles are not on this driver"
            end

            local pool_result, pool_detail, rows = tiles(NPC_SYMBOL, 8)
            if pool_result ~= "ok" or type(rows) ~= "table" or type(rows[1]) ~= "table" then
                return "no_subject", "no " .. NPC_SYMBOL .. " in the pool to read a row from ("
                    .. describe(pool_result) .. " " .. describe(pool_detail) .. ")"
            end
            if type(rows[1].overhead) ~= "string" then
                return "refused", "the npc pool row carries overhead=" .. describe(rows[1].overhead)
                    .. " -- this client cannot read overhead text at all, so every press whose "
                    .. "only answer is a word over an npc's head goes back to reporting `timeout "
                    .. "... nothing was said` on a press that landed (rebuild for "
                    .. "DriveNpcRow.overhead)"
            end
            if type(rows[1].overhead_timer) ~= "number" then
                return "refused", "the npc pool row carries overhead_timer="
                    .. describe(rows[1].overhead_timer) .. " -- without the countdown, the same "
                    .. "words said a second time are unreadable"
            end

            -- The truth table.  Each line is a real shape from the sheep run:
            -- the first say, the same say decaying, the SECOND say, and a
            -- client that has no reading to give.
            local first = say_since({ say = "", say_timer = 0 }, { say = "BAAAAA!", say_timer = 150 })
            if first ~= "BAAAAA!" then
                return "refused", "a first say answered " .. describe(first)
            end
            local decaying = say_since({ say = "BAAAAA!", say_timer = 150 },
                { say = "BAAAAA!", say_timer = 60 })
            if decaying ~= nil then
                return "refused", "the SAME say still counting down answered " .. describe(decaying)
                    .. " -- a stale word read as a fresh answer credits the press before it with "
                    .. "the press after it"
            end
            local again = say_since({ say = "BAAAAA!", say_timer = 60 },
                { say = "BAAAAA!", say_timer = 150 })
            if again ~= "BAAAAA!" then
                return "refused", "the same words with a RISEN timer answered " .. describe(again)
                    .. " -- that is a second say and nothing else is, and without it every prod "
                    .. "after the first reads as silence"
            end
            local unread = say_since({ say = nil }, { say = nil })
            if unread ~= nil then
                return "refused", "a row with no overhead reading answered " .. describe(unread)
                    .. " -- `this client cannot read it` must never be reported as `it was silent`"
            end

            return "ok", "the pool row carries overhead=" .. describe(rows[1].overhead)
                .. " timer=" .. describe(rows[1].overhead_timer)
                .. "; _say_since reads a first say, ignores the same say decaying, reads it "
                .. "again on a risen timer, and answers nothing for a client with no reading"
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
            -- Op 3: a clean probe of the OPHELD dispatch path that no script
            -- claims, so the answer this row expects is the engine's "Nothing
            -- interesting happens." (no_script_probe above).  Op 1 used to
            -- drop the stack (the cell flash, fixed seam pass 20); that is
            -- seam.held_op1_keeps_the_item's subject, not this row's.
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

        -- SEAM choose_calls_a_new_screen_a_stale_reopen (2026-09-22).  A
        -- chatmenu that comes back byte-identical after a click decides
        -- NOTHING by itself, and grading it alone cost Shades of Mort'ton its
        -- whole shop leg (build/quest_gate/mortton ledger row 53, whose own
        -- screenshot shows Razmire's store open and stocked behind the menu
        -- the verb had just called `refused -- stale reopen`).
        --
        -- Two different things wear that face.  One is the server's own
        -- replay: `~p_choice2..5`'s `while (last_slot < 1 | last_slot > N)`
        -- loop re-calls `[proc,p_choice_open]` (chat.rs2:251-263), which
        -- re-sends the same header and the same rows, prints nothing and
        -- opens nothing -- a real refusal every caller must see.  The other
        -- is a row whose handler ends in a screen of its own: nothing in this
        -- pack closes the chatmenu on that path, so the menu still sitting
        -- under `~openshop`'s shopmain IS the menu that was clicked --
        -- unchanged, and already answered.
        --
        -- So the verdict asks what ELSE changed before it calls an unchanged
        -- menu a replay, and this row grades the three arms of that question
        -- against the LIVE cook menu without clicking it -- the same
        -- discipline seam.chat_page_ready uses, so `step("chat.choose")`
        -- below still finds the four rows it expects.  The first arm is the
        -- load-bearing one: it is the refusal that a future author, staring
        -- at a shop row going red, would be tempted to delete.
        seam("seam.choose_screen_outranks_replay", function()
            local verdict = t.chat and t.chat._choose_verdict
            local options = verb("chat", "options")
            local title_of = verb("chat", "options_title")
            if not verdict or not options or not title_of then
                return "unsupported", "chat._choose_verdict / chat.options / "
                    .. "chat.options_title are not on this driver"
            end
            local rows_result, rows = options()
            local title_result, title = title_of()
            if rows_result ~= "ok" or type(rows) ~= "table" or rows[1] == nil
                or title_result ~= "ok" then
                return "no_subject", "the options menu the rows above opened is no longer up ("
                    .. describe(rows_result) .. " / " .. describe(title_result) .. ")"
            end

            -- 1. The replay, and it must still be terminal: the same title,
            --    the same rows, no other screen and not a word printed.
            local replay, replay_detail = verdict(title, rows, nil, nil)
            if replay ~= "refused" or replay_detail ~= "stale reopen" then
                return "refused", "an unchanged menu with nothing else changed answered "
                    .. describe(replay) .. " " .. describe(replay_detail)
                    .. " -- the server's own replay is a real refusal and this is the only "
                    .. "thing that reads it"
            end

            -- 2. THE SEAM.  The identical menu, plus an interface that opened
            --    outside the chat modal while the click was served.
            local screened, screened_detail = verdict(title, rows, "shopmain (300) opened", nil)
            if screened ~= "ok" then
                return "refused", "an unchanged menu whose click OPENED A SCREEN answered "
                    .. describe(screened) .. " " .. describe(screened_detail)
                    .. " -- that is the landed click this seam is, and calling it refused takes "
                    .. "every shop, bank and cutscene row with it"
            end
            if not string.find(tostring(screened_detail), "shopmain (300)", 1, true) then
                return "hollow", "the ok does not name the interface that outranked the replay: "
                    .. describe(screened_detail)
            end

            -- 3. And a chat line the server printed does the same job, for a
            --    row whose handler answers in prose rather than a screen.
            local spoke = verdict(title, rows, nil, "You can't afford that.")
            if spoke ~= "ok" then
                return "refused", "an unchanged menu whose click made the server SPEAK answered "
                    .. describe(spoke)
            end

            -- 4. A menu that did change is `ok` on its own, screen or no.
            local moved = verdict(title .. " <no menu shows this>", rows, nil, nil)
            if moved ~= "ok" then
                return "refused", "a menu whose title differs answered " .. describe(moved)
            end

            return "ok", "on the live " .. describe(title) .. " menu (" .. tostring(#rows)
                .. " rows): unchanged + nothing else = refused 'stale reopen'; unchanged + a "
                .. "screen = ok naming it; unchanged + a server line = ok; changed = ok"
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
            local named, named_text = names_reading(result, detail,
                QUEST_VARP .. " == " .. QUEST_STARTED .. " -> ",
                { QUEST_VARP .. " = " .. QUEST_STARTED .. " (server" },
                "the await names the server value it read")
            if named ~= "ok" then
                return named, named_text
            end
            -- The await names its reading since seam7; the row still reads
            -- the server's own copy back, independently: an await that
            -- resolved without the value ever landing is exactly the hollow
            -- ok this harness is pointed at.
            local read = verb("var", "server")
            local state, value = "missing", nil
            if read then state, value = read(QUEST_VARP) end
            if state ~= "ok" or value ~= QUEST_STARTED then
                return "hollow", "await_server answered ok but var.server(" .. QUEST_VARP
                    .. ") reads " .. describe(value) .. " (" .. tostring(state) .. ")"
            end
            return "ok", named_text .. "; var.server agrees"
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
            -- bind touches no world, so beyond its own detail (seam7: it
            -- names the varp, the constants and the %qp baseline) the row
            -- reads what it left behind: the binding itself, and the %qp
            -- reading it must have taken NOW for quest.points to have a
            -- baseline to measure the award against later.
            local bound = is_table(t.quest) and t.quest._bound or nil
            if not is_table(bound) or bound.varp ~= QUEST_VARP then
                return "hollow", "bind answered ok but nothing was bound -- " .. describe(bound)
            end
            if not is_number(bound.qp_before) then
                return "hollow", "bind answered ok but took no %varp101_qp reading to measure the "
                    .. "award against -- " .. describe(bound.qp_before)
                    .. " (" .. tostring(bound.qp_before_result) .. ")"
            end
            return names_reading(result, detail, "",
                { "bound " .. QUEST_VARP .. " (", "qp_before=" .. tostring(bound.qp_before) },
                "bind names what it bound and the %varp101_qp baseline it took")
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

        -- SEAM goto_departure_stamp (b50-seam1).  A goto row used to name only
        -- its landing, so helper_coverage.py's hop_start took the hop's start
        -- from whatever row last read a tile, and any press or walk between
        -- hid where the teleport left from (993 of 1,735 green hops could not
        -- be judged; docs/quest_authoring/coverage-and-gate.md "The departure
        -- tile").  The verb now reads the tile once before its first ::goto
        -- and opens its ok detail with "at <landing> from <departure>".
        -- Graded: the departure it names is the tile world.tile answered just
        -- before the call.
        seam("seam.goto_departure_stamp", function()
            local fn = verb("player", "goto_tile")
            local tile_of = verb("world", "tile")
            if not fn then return missing("player", "goto_tile") end
            if not tile_of then return missing("world", "tile") end
            local read, before = tile_of()
            if read ~= "ok" or not is_table(before) then
                return read, "no departure tile to compare: " .. describe(before)
            end
            local result, detail = fn(GOTO_TILE_X, GOTO_TILE_Z, GOTO_TILE_LEVEL)
            local text = describe(detail)
            if result ~= "ok" then
                return result, "goto_tile -> " .. text
            end
            local from = before.x .. "," .. before.z .. "," .. before.level
            if not string.find(text, "from " .. from, 1, true) then
                return "hollow", "goto_tile's detail does not name the departure " .. from .. ": " .. text
            end
            return "ok", text
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
            local nearest = verb("npc", "nearest")
            if by_symbol and walk_near then
                local target, target_result = by_symbol("npc", NPC_SYMBOL)
                if target_result == "ok" and is_table(target) then
                    -- Walk to the copy player.attack will WATCH -- the one
                    -- npc.nearest answers -- not the pool's first Man, which
                    -- is what a bare by_symbol target walks to
                    -- (QD.drive._target_tile).  With two Men about, walking
                    -- to the first one left the player ten tiles from the
                    -- copy the row then read (seam15, after the npc wander
                    -- parity: player at 3215,3220 in the castle door, the
                    -- watched slot at 3225,3222 -- build/seam_state/seam15/
                    -- close/make_conformance6.log).  reach_element names the
                    -- copy for _target_tile, as seam13's selector does.
                    if nearest then
                        local near_result, near = nearest(NPC_SYMBOL, 0)
                        if near_result == "ok" and is_table(near) and near.element_id then
                            target.reach_element = near.element_id
                        end
                    end
                    walk_near(target, 20, 1)
                end
            end
            settle(2)
            -- And FACE that copy.  player.attack presses the bare symbol, and
            -- a bare npc press takes whichever copy projects closest to the
            -- viewport centre at the camera it finds (App_NpcScreenPosition);
            -- it yaws only when no copy is on screen.  With the camera left
            -- facing the castle by the rows above, that was a Man in the
            -- castle door while the row watched the one four tiles behind the
            -- camera: combat_trace had the player run to slot 578 at
            -- 3215,3220 and never swing (seam15, make_conformance8.log).  The
            -- press/watch split is a driver seam of its own; this row proves
            -- the attack verb on the copy it reads.
            local tile = verb("world", "tile")
            local yaw_towards = verb("drive", "_yaw_towards")
            local camera = verb("drive", "camera")
            if nearest and tile and yaw_towards and camera then
                local near_result, near = nearest(NPC_SYMBOL, 0)
                local here_result, here = tile()
                if near_result == "ok" and is_table(near) and here_result == "ok"
                    and is_table(here) and is_number(near.x) and is_number(here.x) then
                    local yaw = yaw_towards(near.x - here.x, near.z - here.z)
                    if yaw then
                        camera(yaw, 383, 400)
                        settle(1)
                    end
                end
            end
        end)

        -- The slot of the npc these two rows fight, read once before the
        -- attack: a SYMBOL is not a target (falador_gardener has three spawn
        -- rows, and Lumbridge has a courtyard full of Men), so "the one we
        -- fought is gone" can only be asked of the slot, never of the name.
        local combat_slot = nil

        step("ticklog.slot", function()
            local fn = verb("ticklog", "slot")
            if not fn then return missing("ticklog", "slot") end
            local nearest = verb("npc", "nearest")
            if not nearest then return missing("npc", "nearest") end
            local near_result, row = nearest(NPC_SYMBOL, 5)
            if near_result ~= "ok" or not is_table(row) then
                return "no_subject", NPC_SYMBOL .. " is not within five tiles ("
                    .. describe(near_result) .. ")"
            end
            local result, world = fn(row)
            if result ~= "ok" then
                return result, "client slot " .. describe(row.slot) .. " -> " .. describe(world)
            end
            if math.type(world) ~= "integer" or world < 0 then
                return "hollow", "answered ok without a world slot: " .. describe(world)
            end
            ticklog_world_slot = world
            return "ok", NPC_SYMBOL .. " client slot " .. row.slot .. " -> world slot " .. world
        end)

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
                local shot = verb("shot")
                if shot then
                    shot("player.attack-FAIL", true)
                end
                return result, text .. " -- watched slot " .. describe(combat_slot) .. "; "
                    .. subject_where()
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

        step("ticklog.rows", function()
            local fn = verb("ticklog", "rows")
            if not fn then return missing("ticklog", "rows") end
            if ticklog_world_slot == nil then
                return "no_subject", "ticklog.slot read no world slot for the fight"
            end
            local result, hits = fn({ kind = "hit_npc", slot = ticklog_world_slot })
            if result ~= "ok" then
                return result, describe(hits)
            end
            local _, deaths = fn({ kind = "npc_death", slot = ticklog_world_slot })
            local _, swings = fn({ kind = "npc_anim", slot = ticklog_world_slot })
            local _, bad = fn({ kind = "no_such_kind" })
            if #hits == 0 then
                return "hollow", "the fight npc.await_dead resolved left no hit_npc row on world "
                    .. "slot " .. ticklog_world_slot
            end
            if bad == nil or not string.find(tostring(bad), "no row kind", 1, true) then
                return "hollow", "an unknown kind was not refused: " .. describe(bad)
            end
            local ticks = {}
            for _, row in ipairs(hits) do
                ticks[#ticks + 1] = row.tick .. ":" .. row.damage
            end
            return "ok", #hits .. " hit_npc row(s) on world slot " .. ticklog_world_slot
                .. " (tick:damage " .. table.concat(ticks, " ") .. "), " .. #deaths
                .. " npc_death, " .. #swings .. " npc_anim"
        end)

        step("ticklog.gaps", function()
            local fn = verb("ticklog", "gaps")
            if not fn then return missing("ticklog", "gaps") end
            -- The one cadence every run has: a player_tile row every tick.
            local result, text, gaps = fn(nil, "player_tile")
            if result ~= "ok" then
                return result, describe(text)
            end
            if not is_table(gaps) or #gaps < 10 then
                return "hollow", "too few player_tile gaps to call a cadence: " .. describe(text)
            end
            for _, gap in ipairs(gaps) do
                if gap ~= 1 then
                    return "hollow", "player_tile is not one row per tick: " .. tostring(text)
                end
            end
            return "ok", "player_tile cadence " .. string.sub(tostring(text), -60)
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

        -- PLACEMENT: test/quests/_conformance.lua PLAN, directly AFTER
        -- step("npc.await_dead_engaged", ...) and BEFORE the "seam rows the
        -- 2026-09-20 seam pass landed" block (which starts from a fixed tile of
        -- its own, so the prayer tab these rows leave open costs it nothing --
        -- and the last stage below puts the inventory tab back anyway).
        -- Three verbs: prayer.set, prayer.read, prayer.points (verb count +3,
        -- no seam row).  Raid seam 1, prayer_set_read
        -- (docs/RAID_ORCHESTRATOR.md section 4 row 1); proved on a Lumbridge
        -- goblin in build/quest_gate/pr_seam_b (17/17).
        --
        -- ---------------------------------------------- the prayer verbs
        --
        -- Graded on the VARBIT, not on the verb's word: the server keeps no
        -- prayer mask, `~prayer_set` writes varb4118_prayer_protectfrommelee
        -- (skill_prayer/scripts/prayer.rs2) and that is what a protected hit
        -- reads (skill_combat/combat_stats.rs2 check_protect_prayer).  Protect
        -- from Melee needs Prayer 43 (prayers.dbrow), so the stage states it.
        stage(function()
            setup_cheat("::setlevel prayer 43")
            settle(2)
        end)

        step("prayer.set", function()
            local fn = verb("prayer", "set")
            if not fn then return missing("prayer", "set") end
            local server = verb("var", "server")
            if not server then return missing("var", "server") end
            local VARBIT = "varb4118_prayer_protectfrommelee"
            local on_result, on_detail = fn("protectfrommelee", true)
            local on_text = "on -> " .. describe(on_detail)
            if on_result ~= "ok" then
                return on_result, on_text
            end
            local lit_result, lit = server(VARBIT)
            if lit_result ~= "ok" or lit ~= 1 then
                return "hollow", "answered ok but " .. VARBIT .. " reads " .. describe(lit_result)
                    .. "/" .. describe(lit) .. " after it -- " .. on_text
            end
            if not string.find(tostring(on_detail), VARBIT .. " 0 -> 1", 1, true)
                or not string.find(tostring(on_detail), "prayerbook:prayer15", 1, true) then
                return "hollow", "answered ok without naming the button it pressed and the varbit "
                    .. "before and after -- " .. on_text
            end
            -- A second `on` must NOT press: a press on a lit prayer puts it out.
            local again_result, again_detail = fn("protectfrommelee", true)
            local again_lit_result, again_lit = server(VARBIT)
            if again_result ~= "ok" or again_lit_result ~= "ok" or again_lit ~= 1
                or not string.find(tostring(again_detail), "no press made", 1, true) then
                return "hollow", "a second set(on) answered " .. describe(again_result) .. " ("
                    .. describe(again_detail) .. ") and left " .. VARBIT .. " at "
                    .. describe(again_lit) .. " -- " .. on_text
            end
            local off_result, off_detail = fn("protectfrommelee", false)
            local off_text = "off -> " .. describe(off_detail)
            if off_result ~= "ok" then
                return off_result, on_text .. " | " .. off_text
            end
            local out_result, out = server(VARBIT)
            if out_result ~= "ok" or out ~= 0 then
                return "hollow", "set(off) answered ok but " .. VARBIT .. " reads "
                    .. describe(out_result) .. "/" .. describe(out) .. " -- " .. off_text
            end
            return "ok", on_text .. " | again -> " .. describe(again_detail) .. " | " .. off_text
        end)

        -- setup: one prayer lit for prayer.read to find (set is graded above).
        stage(function()
            local set = verb("prayer", "set")
            if set then
                set("protectfrommelee", true)
            end
        end)

        step("prayer.read", function()
            local fn = verb("prayer", "read")
            if not fn then return missing("prayer", "read") end
            local result, detail, set = fn()
            local text = "-> " .. describe(detail)
            if result ~= "ok" then
                return result, text
            end
            if not is_table(set) then
                return "hollow", "answered ok with no set table as its third return -- " .. text
            end
            local lit, count = {}, 0
            for name, on in pairs(set) do
                count = count + 1
                if on == true then lit[#lit + 1] = name end
            end
            if count ~= 29 or #lit ~= 1 or set.protectfrommelee ~= true then
                return "hollow", "with protectfrommelee lit the set held " .. count
                    .. " prayers and " .. #lit .. " lit (" .. table.concat(lit, ",") .. ") -- " .. text
            end
            -- The overhead has no reader: the detail must say so rather than claim one.
            if not string.find(tostring(detail), "overhead NOT read", 1, true) then
                return "hollow", "the detail does not say the overhead was not read -- " .. text
            end
            return "ok", text
        end)

        step("prayer.points", function()
            local fn = verb("prayer", "points")
            if not fn then return missing("prayer", "points") end
            local result, detail, reading = fn()
            return answered(result, reading, describe(detail) .. " ",
                field("base_level", equals(43)), "the reading's base_level is not the Prayer 43 stated above")
        end)

        -- PLACEMENT: test/quests/_conformance.lua PLAN, directly AFTER
        -- step("prayer.points", ...) and BEFORE the stage that puts the prayer
        -- out and the inventory tab back ("put the prayer out and the
        -- inventory tab back for the rows below").  At that point Prayer is 43
        -- (the prayer verbs' own stage) and Protect from Melee is LIT (the
        -- stage before prayer.read lit it).  Three verbs: prayer.set_on_tick,
        -- prayer.switch, prayer.flick (verb count +3, no seam row).  Waves seam
        -- pass 2, prayer_flick; proved in build/quest_gate/pf_a2 (77/77 PASS)
        -- and in the Inferno in build/quest_gate/pf_b1 (15/15 PASS).
        --
        -- Graded on the info table's TICKS (the server's own, t.tick()) and on
        -- the varbits, not on the verb's word.

        step("prayer.set_on_tick", function()
            local fn = verb("prayer", "set_on_tick")
            if not fn then return missing("prayer", "set_on_tick") end
            local tick_fn = verb("tick")
            if not tick_fn then return missing("tick") end
            local server = verb("var", "server")
            if not server then return missing("var", "server") end
            local _, now = tick_fn()
            local at = now + 2
            -- Protect from Melee is lit: put it out ON tick `at`.
            local result, detail, info = fn("protectfrommelee", false, at)
            local text = "-> " .. describe(detail)
            if result ~= "ok" then
                return result, text
            end
            if type(info) ~= "table" or info.issued ~= at or info.seen ~= at or info.in_force ~= at + 1 then
                return "hollow", "asked tick " .. at .. " but info issued/seen/in_force = "
                    .. describe(info and info.issued) .. "/" .. describe(info and info.seen) .. "/"
                    .. describe(info and info.in_force) .. " -- " .. text
            end
            -- t.var.server reads the CLIENT's record of a transmit=yes varbit,
            -- which can trail the server's own value by a few frames after the
            -- press (seen in build/quest_gate/pf_d1): give it two ticks.
            local ticks = verb("ticks")
            if not ticks then return missing("ticks") end
            ticks(2)
            local _, value = server("varb4118_prayer_protectfrommelee")
            if value ~= 0 then
                return "hollow", "answered ok but varb4118 reads " .. describe(value) .. " -- " .. text
            end
            -- A tick that has passed is never pressed.
            local _, later = tick_fn()
            local late_result, late_detail, late_info = fn("protectfrommelee", true, later - 1)
            local _, still = server("varb4118_prayer_protectfrommelee")
            if late_result ~= "timeout" or still ~= 0 or (late_info and late_info.issued ~= nil) then
                return "hollow", "a passed tick answered " .. describe(late_result) .. " and left varb4118 "
                    .. describe(still) .. " -- " .. describe(late_detail)
            end
            return "ok", text .. " | passed tick -> " .. describe(late_detail)
        end)

        step("prayer.switch", function()
            local fn = verb("prayer", "switch")
            if not fn then return missing("prayer", "switch") end
            local tick_fn = verb("tick")
            if not tick_fn then return missing("tick") end
            local _, now = tick_fn()
            -- Two protections in one tick: the later press wins
            -- (prayer.rs2 [proc,prayer_toggle] -> ~prayer_deactivate_conflicting).
            local result, detail, info = fn({ "protectfrommagic", "protectfrommissiles" }, { tick = now + 2 })
            local text = "-> " .. describe(detail)
            if result ~= "ok" then
                return result, text
            end
            if type(info) ~= "table" or info.issued ~= now + 2 or info.seen ~= now + 2
                or #info.presses ~= 2 or info.final.protectfrommagic ~= 0
                or info.final.protectfrommissiles ~= 1
                or info.displaced.protectfrommagic ~= "protectfrommissiles" then
                return "hollow", "two presses on tick " .. (now + 2) .. " did not read back as one tick with "
                    .. "magic displaced by missiles -- " .. text
            end
            return "ok", text
        end)

        -- setup: Protect from Missiles out again (the switch above lit it), so
        -- the flick starts from nothing lit.
        stage(function()
            local set = verb("prayer", "set")
            if set then
                set("protectfrommissiles", false)
            end
            settle(1)
        end)

        step("prayer.flick", function()
            local fn = verb("prayer", "flick")
            if not fn then return missing("prayer", "flick") end
            local tick_fn = verb("tick")
            if not tick_fn then return missing("tick") end
            local _, now = tick_fn()
            local at = now + 3
            local result, detail, info = fn("protectfrommelee", at)
            local text = "-> " .. describe(detail)
            if result ~= "ok" then
                return result, text
            end
            if type(info) ~= "table" or type(info.on) ~= "table" or type(info.off) ~= "table"
                or info.on.issued ~= at - 1 or info.off.issued ~= at or info.in_force ~= at then
                return "hollow", "a flick for tick " .. at .. " pressed ON on " .. describe(info and info.on and info.on.issued)
                    .. " and OFF on " .. describe(info and info.off and info.off.issued) .. " -- " .. text
            end
            -- One tick of Protect from Melee (drain 12) cannot reach 60: free.
            if info.points_before ~= info.points_after then
                return "hollow", "a one-tick flick cost points " .. describe(info.points_before) .. " -> "
                    .. describe(info.points_after) .. " -- " .. text
            end
            return "ok", text
        end)

        -- PRAYER DRAIN SKIPS THE ACTIVATION TICK AND KEEPS ITS COUNTER (waves seam3
        -- prayer_regen_and_drain).  No verb changed; one SEAM row.
        -- Protect from Melee (drain 12) in force for 5 npc phases is charged for
        -- 4 ticks -- "the game does not drain prayer for prayers on the tick they
        -- are activated" (wiki Prayer:528, docs/minigames/inferno/sources/wiki/
        -- wiki_Prayer.wikitext) -- and the off press leaves the drain counter where it
        -- was: it is reset only by "a rejuvenation pool, the Falador shield prayer
        -- recharge, or dying" (Prayer:35).  Before the seam: charged 5 ticks and the
        -- counter zeroed on the off press (build/quest_gate/prd_a_before vs prd_a_after).
        seam("seam.prayer_drain_activation_tick", function()
            local set_on_tick = verb("prayer", "set_on_tick")
            if not set_on_tick then return missing("prayer", "set_on_tick") end
            local points_fn = verb("prayer", "points")
            if not points_fn then return missing("prayer", "points") end
            local tick_fn = verb("tick")
            if not tick_fn then return missing("tick") end
            local server = verb("var", "server")
            if not server then return missing("var", "server") end
            local function reading()
                settle(2)
                local _, _, r = points_fn()
                local cr, counter = server("varp6296_prayer_drain_counter")
                if type(r) ~= "table" or cr ~= "ok" or type(counter) ~= "number" then
                    return nil, "points " .. describe(r and r.level) .. ", counter " .. describe(cr) .. " " .. describe(counter)
                end
                return { points = r.level, counter = counter }
            end
            local before, why = reading()
            if not before then return "hollow", "no reading before: " .. why end
            local _, now = tick_fn()
            local h0 = now + 3
            local r1, d1 = set_on_tick("protectfrommelee", true, h0 - 1)
            if r1 ~= "ok" then return r1, "on: " .. describe(d1) end
            local r2, d2 = set_on_tick("protectfrommelee", false, h0 + 4)
            if r2 ~= "ok" then return r2, "off: " .. describe(d2) end
            local after, why2 = reading()
            if not after then return "hollow", "no reading after: " .. why2 end
            local charged = (before.points - after.points) * 60 + (after.counter - before.counter)
            local text = string.format("Protect from Melee in force ticks %d..%d: points %d -> %d, counter %d -> %d, "
                .. "charged %d (wiki Prayer:528 + Prayer:35: 4 ticks x 12 = 48, counter kept)",
                h0, h0 + 4, before.points, after.points, before.counter, after.counter, charged)
            if charged ~= 48 then
                return "hollow", text
            end
            return "ok", text
        end)

        -- put the prayer out and the inventory tab back for the rows below
        stage(function()
            local set = verb("prayer", "set")
            if set then
                set("protectfrommelee", false)
            end
            local tab = verb("ui", "tab")
            if tab then
                tab("inventory")
            end
            settle(1)
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
            --
            -- AND EVERY POSE IS READ SETTLED (`_frame`'s fourth argument,
            -- seam17).  The default read answers the first frame that
            -- projects -- the new angles against the old eye, the orbit
            -- anchor still easing after walk_near's step -- so the pixel was
            -- a function of what ran BEFORE this row: two port-master spawns
            -- 300 tiles away shifted the world's random stream and pose 1
            -- read 422,382 where it had read 659,414, and the row went
            -- `covered` (parity1o's bisect, 153 -> 152).  Settled, pose 1 from
            -- 3217,3240 is 415,248 with the spawns and without them
            -- (build/quest_gate/_conf_s17cp_c, s17cp_probe3).
            local poses = is_table(t.drive) and t.drive._frame_poses or nil
            local limit = is_table(poses) and #poses or 1
            local budget = { left = cap }
            local hovered, account, pos = nil, "no pose framed it", nil
            local tried = {}
            local index = 1
            while index <= limit and not is_table(hovered) do
                local pos_result, framed = frame(seam_target, index, 4, true)
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

        -- Waves seam pass 2, supplies_by_dose: inv.doses and player.drink
        -- (verb count +2, no seam row); proved on a Lumbridge goblin in
        -- build/quest_gate/sbd_d (16/16 PASS) and inside the Inferno (wave 1
        -- practice, same run rows 14-16).
        --
        -- PLACED HERE, just before the shop block, by the pass's closer: its
        -- seam author's place (after the prayer rows) ran it on a FULL backpack
        -- (`0 of 28 backpack slot(s) free`, the ::give answered nothing), and
        -- the seam rows there need every cell they hold.  Here the stage may
        -- open with `::clearinv`, because the shop stage right after it opens
        -- with its own `::clearinv` and nothing between them reads the bag.
        --
        -- BACKPACK: after the clear the stage gives two cells
        -- (1doseprayerrestore and 4doseprayerrestore); after the rows they hold
        -- vial_empty and 3doseprayerrestore, which the shop stage clears.
        --
        -- Graded on the verb's own `info` readings (stats before/after, the
        -- slot's new item) and on the formula in prayer_potion.rs2
        -- (`stat_heal(prayer, 7, 25)`), not on the verb's word.  The owed
        -- restore is computed from the reading's own base level; a lit
        -- protection prayer may drain one point between the verb's before and
        -- after reads, hence the -1.
        stage(function()
            setup_cheat("::clearinv")
            settle(2)
            setup_cheat("::give 1doseprayerrestore")
            setup_cheat("::give 4doseprayerrestore")
            setup_cheat("::drain prayer 30 0")
            settle(2)
        end)

        step("inv.doses", function()
            local fn = verb("inv", "doses")
            if not fn then return missing("inv", "doses") end
            local result, detail, info = fn("prayer_potion")
            local text = "-> " .. describe(detail)
            if result ~= "ok" then
                return result, text
            end
            if not is_table(info) or info.doses ~= 5 or info.stem ~= "prayerrestore"
                or type(info.free) ~= "number" or info.free < 0 or info.free >= info.capacity then
                return "hollow", "a 1-dose and a 4-dose were given, so 5 doses of prayerrestore and "
                    .. "a free count below capacity are owed: " .. text
            end
            local unknown, unknown_detail = fn("nosuchpotion")
            if unknown ~= "no_row" then
                return "hollow", "an unknown family answered " .. describe(unknown) .. " ("
                    .. describe(unknown_detail) .. "), not no_row"
            end
            return "ok", text .. " [nosuchpotion -> no_row]"
        end)

        step("player.drink", function()
            local fn = verb("player", "drink")
            if not fn then return missing("player", "drink") end
            -- None carried: not_found, and nothing pressed.
            local none, none_detail = fn("saradomin_brew")
            if none ~= "not_found" then
                return "hollow", "saradomin_brew is not carried and answered " .. describe(none)
                    .. " (" .. describe(none_detail) .. ")"
            end
            -- Fewest doses first: the 1-dose, which leaves a vial in its slot.
            local result, detail, info = fn("prayer_potion")
            local text = "-> " .. describe(detail)
            if result ~= "ok" then
                return result, text
            end
            if not is_table(info) or info.item ~= "1doseprayerrestore" or info.after ~= "vial_empty"
                or info.doses_before - info.doses_after ~= 1 then
                return "hollow", "the 1-dose is the fewest and must be drunk first, leaving vial_empty: "
                    .. text
            end
            local prayer = info.stats and info.stats.prayer
            if not is_table(prayer) then
                return "hollow", "answered ok without a prayer reading: " .. text
            end
            local owed = math.min(prayer.base, prayer.before + 7 + (prayer.base * 25) // 100)
            if prayer.after < owed - 1 then
                return "hollow", "prayer " .. prayer.before .. " -> " .. prayer.after .. ", owed "
                    .. owed .. " (7 + 25% of " .. prayer.base .. ", prayer_potion.rs2): " .. text
            end
            -- Back to back, with then_attack = true and no fight here: the
            -- second press lands inside the first drink's p_delay(1) and is
            -- dropped by the server, so the verb must re-press; the drink
            -- happens, and the then_attack half fails with the drink still in
            -- `info`.  Which word it fails with depends on the harness's last
            -- fight (QD._combat_last): `no_row` with "no fight is engaged" when
            -- there was none (the seam's own scratch, sbd_e), else the
            -- re-attack's own answer at a subject that is long gone (the
            -- closer's run: no_row from player.attack).  Either way it is
            -- not ok, and info.attack carries the same word.
            local again, again_detail, again_info = fn("prayer_potion", { then_attack = true })
            local again_text = describe(again_detail)
            if again == "ok" or again == "timeout" or not is_table(again_info)
                or again_info.item ~= "4doseprayerrestore"
                or again_info.after ~= "3doseprayerrestore"
                or not is_table(again_info.attack) or again_info.attack.result ~= again
                or not string.find(tostring(again_detail), "then_attack", 1, true) then
                return "hollow", "a back-to-back drink with then_attack and no fight here must drink the "
                    .. "4-dose and answer the re-attack's failure: " .. describe(again) .. " " .. again_text
            end
            return "ok", text .. " [then back to back: " .. again_text .. "]"
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

        -- SEAM shop_opened_by_dialogue_has_no_stock_binding (2026-09-22).
        -- `shop.open` was the ONLY writer of the shop's inv binding and it
        -- gets there by pressing the npc's numbered shop op itself, so a shop
        -- reached any other way -- a dialogue row ("Can I see the building
        -- store please?"), the `::shop` cheat, a loc's own op -- sat open and
        -- stocked on screen and could not be bought from: Shades of Mort'ton
        -- lost three rows to `no_row -- this shop was not opened through
        -- shop.open`.
        --
        -- The recipe IS the seam, which is why it runs last: `::shop` opens
        -- the Lumbridge general store with nothing pressed
        -- (shop.rs2:167's `[debugproc,shop]`), so the driver holds no binding
        -- at all -- `shop.close` above cleared the one `shop.open` made.  The
        -- row reproduces the refusal, binds, and then buys, because a bind
        -- nothing spends is not evidence.
        --
        -- The settle after it is a RESTOCK, not a pause: shop.buy above took
        -- all five pots generalshop1 holds (lumbridge_general_store.inv:
        -- `stock1=pot_empty,5,10`, one back every 10 ticks), so the buy below
        -- had stock only when the global restock tick happened to fall inside
        -- the old 5-tick gap.  seam11 moved player.attack 4 ticks earlier and
        -- this row went red on stock 0 -- and, with the shop left open, took
        -- player.emote's tab down with it (s11close_conf vs s11close_nogate).
        stage(function()
            settle(2)
            setup_cheat("::shop")
            settle(12)
        end)

        step("shop.attach", function()
            local fn = verb("shop", "attach")
            local buy = verb("shop", "buy")
            local close = verb("shop", "close")
            local count_of = verb("inv", "count")
            if not fn then return missing("shop", "attach") end
            if not buy or not count_of then
                return "no_subject", "shop.attach is proved by a buy, and shop.buy/inv.count "
                    .. "are not on this driver"
            end

            -- 1. THE SEAM.  A shop is on screen and nothing can buy from it.
            local unbound = buy(SHOP_OBJ_SYMBOL, 1)
            if unbound ~= "no_row" then
                return "no_subject", "a buy against the ::shop screen answered "
                    .. describe(unbound) .. ", not the no_row this seam is -- either nothing "
                    .. "opened or a binding survived shop.close, and the row below would "
                    .. "prove nothing"
            end

            -- 2. The hazard attach carries that open does not: EVERY container
            --    this client was ever sent stays resident in its InvManager,
            --    so "the named inv is stocked" -- all open's wait ever proves
            --    -- is satisfied by the player's own backpack.  Measured: it
            --    reaches the binding step on stock alone.  What stops it is
            --    the grid on screen, which carries slot_count + 1 cells.
            local wrong, wrong_detail = fn("inv")
            if wrong == "ok" then
                return "refused", "attach(\"inv\") bound the player's BACKPACK as the shop -- "
                    .. "every later buy would then press grid cells off backpack slot numbers, "
                    .. "which is a wrong item three buys later rather than a failure here: "
                    .. describe(wrong_detail)
            end

            -- 3. The bind itself, and it has to say what it read of the
            --    SCREEN, not of the caller's claim.
            local result, detail = fn(SHOP_INV_SYMBOL)
            local text = SHOP_INV_SYMBOL .. " -> " .. describe(result) .. " " .. describe(detail)
            if result ~= "ok" then
                return result, text .. " (a buy first answered " .. describe(unbound)
                    .. ", attach(\"inv\") " .. describe(wrong) .. ")"
            end
            if not string.find(tostring(detail), "stocked slot", 1, true)
                or not string.find(tostring(detail), "cell", 1, true) then
                return "hollow", "an `ok` that does not say both what stock landed and what "
                    .. "the grid on screen carries -- those two numbers are the whole "
                    .. "difference between binding this shop and binding a container that "
                    .. "merely still exists: " .. text
            end

            -- 4. And the world, because the binding is only worth what it can
            --    spend.
            local held_before_result, held_before = count_of(SHOP_OBJ_SYMBOL)
            local bought, bought_detail = buy(SHOP_OBJ_SYMBOL, 1)
            local held_after_result, held_after = count_of(SHOP_OBJ_SYMBOL)
            if bought ~= "ok" then
                -- Close it anyway: an open shop takes the side panel, and every
                -- tab row after this one would fail on it instead of on itself.
                if close then
                    close()
                end
                return "refused", "attach answered ok and the buy behind it answered "
                    .. describe(bought) .. " " .. describe(bought_detail) .. " -- " .. text
            end
            if held_before_result ~= "ok" or held_after_result ~= "ok"
                or held_after - held_before ~= 1 then
                return "hollow", "the buy answered ok and the backpack went "
                    .. describe(held_before) .. " -> " .. describe(held_after) .. " -- " .. text
            end
            if close then
                close()
            end
            return "ok", text .. "; before it, a buy -> no_row and attach(\"inv\") -> "
                .. describe(wrong) .. "; after it, " .. SHOP_OBJ_SYMBOL .. " "
                .. tostring(held_before) .. " -> " .. tostring(held_after)
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

        -- --------- seam10 (2026-09-23): the emote tab, the reach retry's
        -- stand-on opt-in, the settle's teleport arm, the npc reach re-press
        --
        -- player.emote presses the rev239 emote tab's own cc_create cell
        -- (emote:contents, sub = emote.constant's index) and settles on the
        -- first chat line after it.  Its subject is the one content reaction a
        -- conformance character can be put in front of by setvars alone:
        -- Prince Brand, Throne of Miscellania's courting leg
        -- (misc_courting_emotes.rs2 [proc,misc_emote_performed_brand]: Clap at
        -- ^misc_affection_s1_step1 = 11 moves it to ^misc_affection_s1_step5 =
        -- 15, quest_misc.constant:19/23), standing at ^misc_brand_coord
        -- 2502,3852,1 (quest_misc.constant:47).  A clap nobody reacts to would
        -- answer `timeout` by design, so the row reads the content's varp, not
        -- the verb's word alone -- then the server's own refusal (cell 32, no
        -- anim in this pack) and an unknown name.
        step("player.emote", function()
            local emote = verb("player", "emote")
            local goto_tile = verb("player", "goto_tile")
            local server_var = verb("var", "server")
            if not emote then return missing("player", "emote") end
            if not goto_tile then return missing("player", "goto_tile") end
            if not server_var then return missing("var", "server") end
            setup_cheat("::setvar varb76_misc_toldking 1")                         -- setup
            setup_cheat("::setvar varb14607_misc_partner_multivar 1")                 -- setup: Brand
            setup_cheat("::setvar varb73_misc_affection ^misc_affection_s1_step1") -- setup
            settle(3)
            local before_result, before = server_var("varb73_misc_affection")
            if before_result ~= "ok" or before ~= 11 then
                return "no_subject", "::setvar varb73_misc_affection ^misc_affection_s1_step1 left "
                    .. describe(before_result) .. " " .. describe(before) .. ", not 11"
            end
            local arrived, where = goto_tile(2502, 3852, 1)
            if arrived ~= "ok" then
                return "no_subject", "goto_tile(2502,3852,1) (Prince Brand) -> "
                    .. describe(arrived) .. " " .. describe(where)
            end
            local clap_result, clap_detail = emote("clap")
            settle(2)
            local after_result, after = server_var("varb73_misc_affection")
            local locked_result, locked_detail = emote(32)
            local unknown_result, unknown_detail = emote("no_such_emote")
            if clap_result ~= "ok" then
                return clap_result, "emote(clap) beside Prince Brand: " .. describe(clap_detail)
            end
            if after_result ~= "ok" or after ~= 15 then
                return "hollow", "emote(clap) answered ok (" .. describe(clap_detail)
                    .. ") but misc_affection went 11 -> " .. describe(after)
                    .. ", not 15 -- the press did not reach [if_button,emote:contents]"
            end
            if locked_result ~= "refused" then
                return "hollow", "emote(32) must answer emote.rs2's own refusal as `refused`, got "
                    .. describe(locked_result) .. " " .. describe(locked_detail)
            end
            if unknown_result ~= "no_row" then
                return "hollow", "emote(no_such_emote) must be no_row, got "
                    .. describe(unknown_result) .. " " .. describe(unknown_detail)
            end
            return "ok", describe(clap_detail) .. "; misc_affection 11 -> 15; emote(32) -> refused ("
                .. describe(locked_detail) .. "); an unknown name -> no_row"
        end)

        -- SEAM reach_stand_on_opt_in (pointer.lua, inside _reach_retry).  The
        -- reach retry used to ::goto onto a loc's own square whenever every
        -- walkable neighbour had refused, unasked, and that hid a skipped river
        -- crossing (rovingelves row 39, reverted by sampler sonnet-b16) and
        -- fishingcompo's garlicpipe (build/quest_gate/seam10_reach_base row
        -- 11).  Now the own square is WALKED to and, when no route ends on
        -- it, the row fails `refused` with a detail starting `reach_failed:`;
        -- `{ stand_on_square = true }` is the only way back to the ::goto.
        --
        -- Driven with an injected press, like seam.reach_retry, and with the
        -- candidate list cut to the ONE own square of a Lumbridge tree -- the
        -- row is about the stand-on decision, not about which neighbour
        -- answers, and a tree's square is one no route ends on.  The press
        -- answers the reach sentence from anywhere but that square.
        seam("seam.reach_stand_on_opt_in", function()
            local retry = verb("player", "_reach_retry")
            local candidates_of = verb("player", "_reach_candidates")
            local goto_tile = verb("player", "goto_tile")
            local tile_of = verb("world", "tile")
            if not retry then return missing("player", "_reach_retry") end
            if not candidates_of then return missing("player", "_reach_candidates") end
            if not goto_tile then return missing("player", "goto_tile") end
            if not tile_of then return missing("world", "tile") end
            local arrived, where = goto_tile(3222, 3218, 0)
            if arrived ~= "ok" then
                return "no_subject", "goto_tile(3222,3218,0) (Lumbridge courtyard) -> "
                    .. describe(arrived) .. " " .. describe(where)
            end
            settle(2)
            local seam_target = seam_loc_target()
            if not seam_target then
                return "no_subject", "player.by_symbol(loc, " .. LOC_SYMBOL .. ") built no target"
            end
            local own = nil
            local all = candidates_of(seam_target)
            if is_table(all) then
                for index = 1, #all do
                    if all[index].own then
                        own = all[index]
                        break
                    end
                end
            end
            if not own then
                return "not_found", "no own square among " .. LOC_SYMBOL .. "'s approach tiles: "
                    .. describe(all)
            end
            local presses = 0
            local from_own = 0
            local function press()
                presses = presses + 1
                local at_result, at = tile_of()
                if at_result == "ok" and is_table(at) and at.x == own.x and at.z == own.z then
                    from_own = from_own + 1
                    return "ok", "the injected press landed from the loc's own square"
                end
                return "refused", "I can't reach that!"
            end
            t.player._reach_candidates = function() return { own } end
            local plain_result, plain_detail = retry(seam_target, "refused", "I can't reach that!", press)
            local plain_presses, plain_from_own = presses, from_own
            local opted_result, opted_detail = retry(seam_target, "refused", "I can't reach that!",
                press, { stand_on_square = true })
            t.player._reach_candidates = candidates_of
            goto_tile(3222, 3218, 0)
            local said = "default -> " .. describe(plain_result) .. " (" .. describe(plain_presses)
                .. " press(es)) " .. describe(plain_detail) .. " || opted in -> "
                .. describe(opted_result) .. " " .. describe(opted_detail)
            if plain_result ~= "refused" or plain_from_own ~= 0 then
                return "refused", "the default call stood on the loc's own square unasked -- " .. said
            end
            if string.sub(tostring(plain_detail), 1, 14) ~= "reach_failed: "
                or not string.find(tostring(plain_detail), "stand_on_square not set", 1, true) then
                return "hollow", "the default refusal does not start `reach_failed:` or does not "
                    .. "say the own square was left alone -- " .. said
            end
            if opted_result ~= "ok" or from_own ~= 1 then
                return "refused", "{ stand_on_square = true } did not stand on "
                    .. describe(own.x) .. "," .. describe(own.z) .. " and press from it -- " .. said
            end
            if not string.find(tostring(opted_detail), "stand_on_square opt-in", 1, true) then
                return "hollow", "the opted-in PASS does not name the opt-in, so the grader "
                    .. "cannot see the ::goto -- " .. said
            end
            return "ok", "own square " .. describe(own.x) .. "," .. describe(own.z) .. ": " .. said
        end)

        -- SEAM settle_teleport_landing (pointer.lua, the fifth arm of
        -- _settle_after_click).  A click whose whole answer is a p_teleport --
        -- a ladder, a staircase, a Temple of Light door -- mounts no page,
        -- prints no line and, from beside it, issues no route, so the settle
        -- ran out its budget on a click that visibly landed
        -- (parity_mend2_puzzle3d rows 62/64, build/quest_gate/seam10_reach_base
        -- row 7).  The subject is that ladder: the Temple of Light's north
        -- ladder top, the generic climb (no quest state), from beside it on
        -- level 2.  `ok` alone would pass on any arm, so the row also wants
        -- the `teleport:` detail and the lower plane.
        seam("seam.settle_teleport_landing", function()
            local goto_tile = verb("player", "goto_tile")
            local click_loc = verb("player", "click_loc")
            local tile_of = verb("world", "tile")
            if not goto_tile then return missing("player", "goto_tile") end
            if not click_loc then return missing("player", "click_loc") end
            if not tile_of then return missing("world", "tile") end
            local arrived, where = goto_tile(1898, 4666, 2)
            if arrived ~= "ok" then
                return "no_subject", "goto_tile(1898,4666,2) (Temple of Light, north ladder top) -> "
                    .. describe(arrived) .. " " .. describe(where)
            end
            settle(2)
            local result, detail = click_loc("mourning_temple_ladder_wall_top", 1)
            local after_result, after = tile_of()
            if result ~= "ok" then
                return result, "the ladder click: " .. describe(detail)
            end
            if string.sub(tostring(detail), 1, 9) ~= "teleport:" then
                return "hollow", "the ladder settled on another arm, so this row proves nothing "
                    .. "about the teleport one: " .. describe(detail)
            end
            if after_result ~= "ok" or not is_table(after) or after.level ~= 1 then
                return "hollow", "the settle said teleport but the player is at "
                    .. describe(after) .. ", not on level 1 -- " .. describe(detail)
            end
            return "ok", describe(detail)
        end)

        -- SEAM npc_reach_repress (pointer.lua, QD.player._npc_reach_retry).  A
        -- wandering npc's "I can't reach that!" is a fact about the MOMENT, and
        -- use_on re-presses it (up to twice, after the player stops) instead of
        -- failing the row -- sheepherder's poison2 went red without it once the
        -- teleport arm shifted its timing.  Injected presses: the re-press
        -- happens and is named, a loc target and a refusal that is not the
        -- reach sentence are passed through untouched.
        seam("seam.npc_reach_repress", function()
            local repress = verb("player", "_npc_reach_retry")
            if not repress then return missing("player", "_npc_reach_retry") end
            local npc_target = { kind = "npc", id = -1, symbol = NPC_SYMBOL }
            local presses = 0
            local function second_press_lands()
                presses = presses + 1
                if presses < 2 then
                    return "refused", "I can't reach that!"
                end
                return "ok", "the injected re-press landed"
            end
            local result, detail = repress(npc_target, "refused", "I can't reach that!", second_press_lands)
            local npc_presses = presses
            presses = 0
            local loc_result = repress({ kind = "loc", id = -1 }, "refused", "I can't reach that!",
                second_press_lands)
            local loc_presses = presses
            local other_result = repress(npc_target, "refused", "Nothing interesting happens.",
                second_press_lands)
            local other_presses = presses - loc_presses
            local said = "npc -> " .. describe(result) .. " after " .. describe(npc_presses)
                .. " press(es): " .. describe(detail) .. "; loc -> " .. describe(loc_result)
                .. " (" .. describe(loc_presses) .. "); other refusal -> "
                .. describe(other_result) .. " (" .. describe(other_presses) .. ")"
            if result ~= "ok" or npc_presses ~= 2 then
                return "refused", "the npc reach refusal was not re-pressed to a landing -- " .. said
            end
            if not string.find(tostring(detail), "npc reach retry", 1, true) then
                return "hollow", "the re-pressed row does not say it was re-pressed -- " .. said
            end
            if loc_result ~= "refused" or loc_presses ~= 0 or other_result ~= "refused"
                or other_presses ~= 0 then
                return "refused", "a loc target or a non-reach refusal was re-pressed -- " .. said
            end
            return "ok", said
        end)

        -- --------- seam: the photograph's camera, aimed at zero cost
        --
        -- SEAM driver-shot-camera-occluded-zero-cost (seam8, 2026-09-22).
        -- QD.shot photographed whatever pose the last press left, and in a
        -- basement or a walled room that pose is a wall face or the inside of
        -- a cave rock (mourningsendparti shots 47-90 were black void).  Seam
        -- 7 re-aimed every shot and waited two frames each way; the pictures
        -- came right and three green quests went red, because one run frame
        -- is one 20 ms logic cycle and the added frames moved every later
        -- press against the server tick.  The fix aims ONLY an occluded pose,
        -- inside the shot's own frames: aim in the pump that queues the
        -- capture, put the press pose back on the first poll that says the
        -- pixels were taken (pointer.lua's banner "THE PHOTOGRAPH'S CAMERA").
        --
        -- What is graded is the COST, not the picture (the picture is Read
        -- off the shot this row leaves): in the Lumbridge castle cellar, the
        -- press pose 0/128/600 is behind a wall, so the shot must aim; the
        -- pose must be put back BEFORE the shot answers; the live pose after
        -- must be the press pose exactly; and the shot must answer on the
        -- SAME poll as an unaimed control shot (1024/300/1200, clear in that
        -- cellar) -- the frame count a shot costs is unchanged by the aim.
        -- The player is left in the cellar: every row after this one is a
        -- scheduler control that reads no world.
        seam("seam.shot_camera_zero_cost", function()
            local goto_tile = verb("player", "goto_tile")
            local camera = verb("drive", "camera")
            local camera_pose = verb("drive", "_camera_pose")
            local plan = verb("drive", "_shot_plan")
            local shot = verb("shot")
            if not goto_tile then return missing("player", "goto_tile") end
            if not camera then return missing("drive", "camera") end
            if not camera_pose then return missing("drive", "_camera_pose") end
            if not plan then return missing("drive", "_shot_plan") end
            if not shot then return missing("shot") end
            local arrived, where = goto_tile(3210, 9620, 0)
            if arrived ~= "ok" then
                return "no_subject", "goto_tile(3210,9620,0) (Lumbridge castle cellar) -> "
                    .. tostring(arrived) .. " " .. describe(where)
            end
            settle(2)

            local function take(label, yaw, pitch, zoom)
                camera(yaw, pitch, zoom)
                settle(1)
                local pose_result, before = camera_pose()
                if pose_result ~= "ok" then
                    return nil, "_camera_pose -> " .. tostring(pose_result) .. " " .. describe(before)
                end
                local _, aim, why = plan()
                local shot_result, shot_detail = shot("seam.shot_camera." .. label)
                local last = t.drive._shot_last
                local _, after = camera_pose()
                return {
                    before = before, after = after, aim = aim, why = why,
                    result = shot_result, detail = shot_detail, last = last,
                }
            end
            local function pose_text(pose)
                if type(pose) ~= "table" then return describe(pose) end
                return pose.yaw .. "/" .. pose.pitch .. "/" .. pose.zoom
            end

            local aimed, aimed_error = take("occluded", 0, 128, 600)
            if not aimed then return "unsupported", aimed_error end
            local control, control_error = take("clear", 1024, 300, 1200)
            if not control then return "unsupported", control_error end
            local said = "occluded shot: plan '" .. tostring(aimed.why) .. "', "
                .. tostring(aimed.result) .. ", aimed=" .. tostring(aimed.last and aimed.last.aimed)
                .. " put back at poll " .. tostring(aimed.last and aimed.last.restored_poll)
                .. ", answered at poll " .. tostring(aimed.last and aimed.last.answered_poll)
                .. ", pose " .. pose_text(aimed.before) .. " -> " .. pose_text(aimed.after)
                .. "; clear control: plan '" .. tostring(control.why) .. "', "
                .. tostring(control.result) .. ", aimed=" .. tostring(control.last and control.last.aimed)
                .. ", answered at poll " .. tostring(control.last and control.last.answered_poll)

            if aimed.result ~= "ok" or control.result ~= "ok" then
                return "refused", "a shot did not land -- " .. said
            end
            if aimed.aim == nil or not (aimed.last and aimed.last.aimed) then
                return "hollow", "the press pose behind the cellar wall was not aimed -- " .. said
            end
            if control.aim ~= nil or (control.last and control.last.aimed) then
                return "hollow", "a clear pose was re-aimed: its picture is no longer the press's own -- " .. said
            end
            if aimed.last.restored_poll == nil or aimed.last.restored_poll >= aimed.last.answered_poll then
                return "refused", "the press pose was not put back before the shot answered -- " .. said
            end
            if aimed.last.answered_poll ~= control.last.answered_poll then
                return "refused", "the aimed shot answered on a different poll than the unaimed one "
                    .. "(the aim cost frames) -- " .. said
            end
            if pose_text(aimed.after) ~= pose_text(aimed.before) then
                return "refused", "the live pose after the shot is not the press pose -- " .. said
            end
            return "ok", said
        end)

        -- SEAM chat_shock_into_mesbox (src/painters/painters.c,
        -- painter_release_scenery).  Between a Rock froze the WHOLE client when
        -- Dondakan's ^chat_shock page ("...Now that's not a bad thought...")
        -- was dismissed into his ~mesbox (betweenarock row 45, author batch
        -- sonnet-b17, runs 1-4).  The page was never the cause: its
        -- %dwarfrock_gold_cannonball write re-spawns every multiloc on
        -- dwarfrock_main, and the dwarf rock beside him was ALREADY a runtime
        -- spawn from the stage write before it -- so the loc change released
        -- this cycle's DYNAMIC element by payload, after the paint's sort had
        -- put that payload in the static node.  The static node left the
        -- chain, the next reset handed the stranded node out again, and
        -- bucket_paint_world walked a cycle forever (seam11).  So the row
        -- stages exactly that: the stage write with the rock on screen, then
        -- the gold bar on Dondakan and the whole exchange walked to the end,
        -- ^chat_shock page and ~mesbox both dismissed, and the tick clock still
        -- running after it.  On the pre-fix painter this row never returns.
        -- The player is left in the dwarf cave: every row after this one is a
        -- scheduler control that reads no world.
        seam("seam.chat_shock_into_mesbox", function()
            local goto_tile = verb("player", "goto_tile")
            local by_symbol = verb("player", "by_symbol")
            local use_on = verb("player", "use_on")
            local play = verb("chat", "play")
            local await_server = verb("var", "await_server")
            local held = verb("inv", "await")
            local tab = verb("ui", "tab")
            if not goto_tile then return missing("player", "goto_tile") end
            if not tab then return missing("ui", "tab") end
            if not by_symbol then return missing("player", "by_symbol") end
            if not use_on then return missing("player", "use_on") end
            if not play then return missing("chat", "play") end
            if not await_server then return missing("var", "await_server") end
            if not held then return missing("inv", "await") end
            -- setup: the quest one stage short, a gold bar to show him.
            setup_cheat("::clearinv")
            setup_cheat("::complete quest_fishingcontest")
            setup_cheat("::setvar varb299_dwarfrock_quest 50")
            setup_cheat("::give gold_bar 1")
            local bar_result, bar_detail = held("gold_bar", 1, 10)
            if bar_result ~= "ok" then
                return "no_subject", "::give gold_bar 1 -> " .. describe(bar_result) .. " " .. describe(bar_detail)
            end
            local arrived, where = goto_tile(2824, 10168, 0)
            if arrived ~= "ok" then
                return "no_subject", "goto_tile(2824,10168,0) (Dondakan's rock) -> "
                    .. describe(arrived) .. " " .. describe(where)
            end
            settle(3)
            -- setup: the stage write with the rock painted -- it re-spawns as
            -- a runtime loc, which is what the next write has to release.
            setup_cheat("::setvar varb299_dwarfrock_quest 60")
            settle(3)
            -- setup: the backpack on screen before the arming.  The rows before
            -- this one leave another tab up (player.emote's), and use_on's own
            -- tab press does not wait for the cell to paint.
            tab("inventory")
            settle(2)
            local dondakan, target_result = by_symbol("npc", "dwarfrock_dondakan")
            if target_result ~= "ok" or type(dondakan) ~= "table" then
                return "no_subject", "player.by_symbol(npc, dwarfrock_dondakan) -> " .. describe(target_result)
            end
            local used, used_detail = use_on("gold_bar", dondakan)
            if used ~= "ok" then
                return "no_subject", "use_on(gold_bar, dwarfrock_dondakan) -> " .. describe(used) .. " "
                    .. describe(used_detail)
            end
            local played, played_detail = play({
                "player:Here, take a look at this.",
                "npc:Haha, what am I meant to do with that?",
                "player:The book said there's gold inside the rock.",
                "npc:Now that's not a bad thought",
                "mesbox:Dondakan agrees to try firing",
            })
            if played ~= "ok" then
                return played, "the exchange did not walk to its ~mesbox: " .. describe(played_detail)
            end
            local flag, flag_detail = await_server("varb301_dwarfrock_gold_cannonball", 1, 5)
            if flag ~= "ok" then
                return flag, "the ^chat_shock page and the ~mesbox were dismissed but "
                    .. "%varb301_dwarfrock_gold_cannonball did not read 1 -- " .. describe(flag_detail)
            end
            local ticked, tick_detail = await_server("varb299_dwarfrock_quest", 60, 3)
            if ticked ~= "ok" then
                return ticked, "the clock after the dismissal: " .. describe(tick_detail)
            end
            return "ok", describe(played_detail) .. " | " .. describe(flag_detail)
        end)

        -- SEAM pick_same_plane_copy (pointer.lua QD.drive._target_tile /
        -- _reach_candidates, torirs_plugin_drive_pointer.c's loc and obj
        -- projectors).  The client's pick keeps a scenery or ground-stack hit
        -- only on the player's own plane (torirs_pick.c), but the driver
        -- ranked a symbol's copies by x/z alone, and in the Temple of Light
        -- the nearest `mourning_temple_circle_stairs_top` from 1888,4642,1 is
        -- the level-2 copy at 1890,4641 (m29_72.jl2) -- five poses, a 99-probe
        -- hunt and every approach tile spent on stairs no press could reach
        -- (seam10_reverify row 75, build/quest_gate/s11_exp circle.near;
        -- seam11).  Graded on the tile the camera turns to and walk_near
        -- walks to: the level-1 copy at 1887,4638, never 1890,4641.
        seam("seam.pick_same_plane_copy", function()
            local goto_tile = verb("player", "goto_tile")
            local by_symbol = verb("player", "by_symbol")
            local target_tile = verb("drive", "_target_tile")
            local candidates_of = verb("player", "_reach_candidates")
            if not goto_tile then return missing("player", "goto_tile") end
            if not by_symbol then return missing("player", "by_symbol") end
            if not target_tile then return missing("drive", "_target_tile") end
            if not candidates_of then return missing("player", "_reach_candidates") end
            local arrived, where = goto_tile(1888, 4642, 1)
            if arrived ~= "ok" then
                return "no_subject", "goto_tile(1888,4642,1) (Temple of Light, level 1) -> "
                    .. describe(arrived) .. " " .. describe(where)
            end
            settle(2)
            local stairs, target_result = by_symbol("loc", "mourning_temple_circle_stairs_top")
            if target_result ~= "ok" or type(stairs) ~= "table" then
                return "no_subject", "player.by_symbol(loc, mourning_temple_circle_stairs_top) -> "
                    .. describe(target_result)
            end
            local tile_result, x, z = target_tile(stairs)
            if tile_result ~= "ok" then
                return tile_result, "_target_tile answered no tile: " .. describe(x)
            end
            if x == 1890 and z == 4641 then
                return "refused", "_target_tile chose the level-2 copy at 1890,4641 -- no press "
                    .. "from level 1 can ever pick it"
            end
            if x ~= 1887 or z ~= 4638 then
                return "hollow", "_target_tile answered " .. describe(x) .. "," .. describe(z)
                    .. ", neither the level-1 copy 1887,4638 nor the level-2 one"
            end
            local all = candidates_of(stairs)
            if is_table(all) then
                for index = 1, #all do
                    local c = all[index]
                    if math.abs(c.x - 1890) <= 1 and math.abs(c.z - 4641) <= 1
                        and (math.abs(c.x - 1887) > 1 or math.abs(c.z - 4638) > 1) then
                        return "refused", "_reach_candidates offers " .. describe(c.x) .. ","
                            .. describe(c.z) .. ", beside the level-2 copy only -- " .. describe(all)
                    end
                end
            end
            return "ok", "circle stairs from 1888,4642,1 -> the level-1 copy 1887,4638 ("
                .. (is_table(all) and #all or 0) .. " approach tile(s), none beside 1890,4641)"
        end)

        -- SEAM hunt_closes_menu (pointer.lua QD.drive._dismiss_menu,
        -- _hover_inside/_under_ui over api_drive.world_gate).  Two things read
        -- as "the world is not picking" in the Temple of Light's rooms: a
        -- covered press's minimenu, which owns the whole canvas until the
        -- pointer leaves it, and pixels under the chatbox, where the frame
        -- resets the pickset instead of stamping it (build/quest_gate/s11_gate:
        -- the grid all `menu` after one covered press; seam10_reverify row 19).
        -- The gate must say `ui` under the chatbox and `world` over the world,
        -- the hunt must treat the chatbox pixel as free, and the Temple's wall
        -- support -- whose first press is covered, so only the hunt can land
        -- it -- must be clicked (s11_exp4: covered with every candidate
        -- "under UI (menu)" before the dismiss; s11_ws: ok after).
        seam("seam.hunt_closes_menu", function()
            local goto_tile = verb("player", "goto_tile")
            local by_symbol = verb("player", "by_symbol")
            local gate = verb("drive", "_world_gate")
            local inside = verb("drive", "_hover_inside")
            local click_minimenu = verb("drive", "click_minimenu")
            if not goto_tile then return missing("player", "goto_tile") end
            if not by_symbol then return missing("player", "by_symbol") end
            if not gate then return missing("drive", "_world_gate") end
            if not inside then return missing("drive", "_hover_inside") end
            if not click_minimenu then return missing("drive", "click_minimenu") end
            local arrived, where = goto_tile(1901, 4611, 1)
            if arrived ~= "ok" then
                return "no_subject", "goto_tile(1901,4611,1) (Temple of Light, the wall support) -> "
                    .. describe(arrived) .. " " .. describe(where)
            end
            settle(2)
            local chat_world, chat_why = gate(100, 440)
            local mid_world, mid_why = gate(200, 200)
            if chat_world == nil then
                return "missing", "api_drive.world_gate is absent from this binary: " .. describe(chat_why)
            end
            if chat_world ~= false or string.sub(tostring(chat_why), 1, 2) ~= "ui" then
                return "refused", "the gate under the chatbox (100,440) answered "
                    .. describe(chat_world) .. " " .. describe(chat_why) .. ", not ui"
            end
            if mid_world ~= true then
                return "refused", "the gate over the world (200,200) answered "
                    .. describe(mid_world) .. " " .. describe(mid_why)
            end
            local free, free_why = inside(nil, 100, 440)
            if free ~= false or string.sub(tostring(free_why), 1, 2) ~= "ui" then
                return "refused", "_hover_inside would probe the chatbox pixel 100,440: "
                    .. describe(free) .. " " .. describe(free_why)
            end
            local support, target_result = by_symbol("loc", "mourning_temple_agility_hanging")
            if target_result ~= "ok" or type(support) ~= "table" then
                return "no_subject", "player.by_symbol(loc, mourning_temple_agility_hanging) -> "
                    .. describe(target_result)
            end
            local result, detail = click_minimenu(support, 1)
            if result ~= "ok" then
                return result, "the wall support: " .. describe(detail)
            end
            return "ok", "gate 100,440 -> " .. describe(chat_why) .. ", 200,200 -> "
                .. describe(mid_why) .. "; wall support " .. describe(detail)
        end)

        -- SEAM absent_loc_scan_bounded (pointer.lua's SCAN METER over
        -- _live_loc_id / _target_tile / world.loc_near, QD.drive._scan_*;
        -- seam15).  The quest-driver coroutine has 400000 Lua instructions per
        -- RESUME (torirs_plugin_lua.c PLUGIN_LUA_STEP_BUDGET), and a click on a
        -- loc ABSENT from a full 8,192-row scene walked the whole scenery pool
        -- nine times with no yield: the second such click in the Temple of
        -- Light ended the run `quest-driver:3489: instruction budget exhausted
        -- (400000)` (build/quest_gate/s15b_absent_before), as did parity1g's
        -- Mourning's End II replay at row 140.  Graded on three clicks on a
        -- loc no Temple square carries each answering not_found (a budget
        -- death ends the run at this row, which conformance.py reports as
        -- `abort`), and on the meter reading wrapped=true: api_drive.await is
        -- the meter's wrapper, so it sees every yield.
        seam("seam.absent_loc_scan_bounded", function()
            local goto_tile = verb("player", "goto_tile")
            local click_loc = verb("player", "click_loc")
            local meter = verb("drive", "_scan_meter")
            if not goto_tile then return missing("player", "goto_tile") end
            if not click_loc then return missing("player", "click_loc") end
            if not meter then return missing("drive", "_scan_meter") end
            local arrived, where = goto_tile(1876, 4620, 1)
            if arrived ~= "ok" then
                return "no_subject", "goto_tile(1876,4620,1) (Temple of Light) -> "
                    .. describe(arrived) .. " " .. describe(where)
            end
            settle(2)
            local answers = {}
            for i = 1, 3 do
                local result, detail = click_loc("fai_varrock_castle_door", 1)
                if result ~= "not_found" then
                    return (result == "ok") and "refused" or result,
                        "click " .. i .. " on a loc absent from the Temple answered "
                        .. describe(result) .. " " .. describe(detail)
                end
                answers[#answers + 1] = describe(result)
            end
            local reading = tostring(meter())
            if string.find(reading, "wrapped=true", 1, true) == nil then
                return "refused", "the scan meter is not on api_drive.await: " .. reading
            end
            return "ok", "3 clicks -> " .. table.concat(answers, ", ") .. "; meter " .. reading
        end)

        -- SEAM press_aims_named_npc_copy (pointer.lua QD.player._npc_copy /
        -- _click_npc_copy / QD.drive._aim_at_named_npc; seam13).  press and
        -- talk_to took only a SYMBOL, and among Sheep Herder's three live
        -- plaguesheep_1 copies (m40_52.spawn, two tiles apart in Brumty's
        -- barnyard) the copy pressed was whichever App_NpcScreenPosition
        -- ranked first: asked for each copy by slot, the press landed on the
        -- wrong one three times of three (build/quest_gate/seam13_aim_before),
        -- and batch sonnet-b20's herd loop kept pressing the copy wedged
        -- against a boulder.  Graded on every live copy pressed by `{ slot = n }`
        -- naming THAT slot in its detail ('aimed at slot N (' and 'pressed
        -- slot N ('), and on a selector that matches no copy answering `no_row`
        -- with the slot in the detail from both press and talk_to -- never a
        -- press on the ranked copy.  The quest is not started, so each landed
        -- prod answers only the content line "The sheep looks extremely ill".
        seam("seam.press_aims_named_npc_copy", function()
            local goto_tile = verb("player", "goto_tile")
            local press = verb("player", "press")
            local talk_to = verb("player", "talk_to")
            local tiles = verb("npc", "tiles")
            if not goto_tile then return missing("player", "goto_tile") end
            if not press then return missing("player", "press") end
            if not talk_to then return missing("player", "talk_to") end
            if not tiles then return missing("npc", "tiles") end
            local arrived, where = goto_tile(2612, 3342, 0)
            if arrived ~= "ok" then
                return "no_subject", "goto_tile(2612,3342) (Brumty's barnyard) -> "
                    .. describe(arrived) .. " " .. describe(where)
            end
            settle(3)
            local pool_result, pool_detail, rows = tiles("plaguesheep_1", 30)
            if pool_result ~= "ok" or type(rows) ~= "table" or #rows < 2 then
                return "no_subject", "need two or more plaguesheep_1 copies to aim between: "
                    .. describe(pool_result) .. " " .. describe(pool_detail)
            end
            local named = {}
            for i = 1, #rows do
                local slot = rows[i].slot
                local result, detail = press("plaguesheep_1", 1, 4, { slot = slot })
                if result ~= "ok" then
                    return result, "press({slot=" .. tostring(slot) .. "}): " .. describe(detail)
                end
                local text = tostring(detail)
                if string.find(text, "aimed at slot " .. tostring(slot) .. " (", 1, true) == nil
                    or string.find(text, "pressed slot " .. tostring(slot) .. " (", 1, true) == nil then
                    return "refused", "asked for slot " .. tostring(slot)
                        .. " and the detail names another copy: " .. text
                end
                named[#named + 1] = tostring(slot)
                settle(2)
            end
            local bogus, bogus_detail = press("plaguesheep_1", 1, 4, { slot = 9999 })
            if bogus ~= "no_row" or string.find(tostring(bogus_detail), "9999", 1, true) == nil then
                return "refused", "press({slot=9999}) must be no_row naming 9999: "
                    .. describe(bogus) .. " " .. describe(bogus_detail)
            end
            local talk_bogus, talk_detail = talk_to("plaguesheep_1", 1, { slot = 9999 })
            if talk_bogus ~= "no_row" or string.find(tostring(talk_detail), "9999", 1, true) == nil then
                return "refused", "talk_to({slot=9999}) must be no_row naming 9999: "
                    .. describe(talk_bogus) .. " " .. describe(talk_detail)
            end
            return "ok", "pressed slots " .. table.concat(named, ", ") .. " each by name; "
                .. describe(bogus_detail)
        end)

        -- SEAM npc_reaim_names_the_move (pointer.lua QD.drive._npc_reaim in
        -- click_minimenu; seam15).  Under the npc wander parity an npc steps
        -- between the frame its pixel is taken and the press: eadgar's
        -- talk_to(troll_eadgar) answered `covered ... none of 99 pixels
        -- hittested around the projected 349,264`, the menu offering only the
        -- Cave Exit and Walk here (build/seam_state/seam14/fixrun/eadgar row
        -- 18).  Now a covered press on an npc whose pool tile changed since
        -- the aim is re-aimed ONCE on the new tile, and the note names the
        -- move.  A real step cannot be timed from here, so the row STAGES one
        -- through the private helpers the press calls: the aim reads the
        -- sheep one tile east of where it stands, and the first press answers
        -- covered without pressing.  Graded on the press answering ok and the
        -- note carrying 'moved a,b -> c,d between aim and press; re-aimed'.
        -- The quest is not started, so the prod answers only "The sheep looks
        -- extremely ill".
        seam("seam.npc_reaim_names_the_move", function()
            local press = verb("player", "press")
            local aim_tile = verb("drive", "_npc_aim_tile")
            local press_row = verb("drive", "_press_row")
            local note = verb("note")
            if not press then return missing("player", "press") end
            if not aim_tile then return missing("drive", "_npc_aim_tile") end
            if not press_row then return missing("drive", "_press_row") end
            if not note then return missing("note") end
            local asks = 0
            local notes = {}
            t.drive._npc_aim_tile = function(target, element_id)
                asks = asks + 1
                local tile = aim_tile(target, element_id)
                if asks == 1 and tile ~= nil then
                    return { x = tile.x + 1, z = tile.z, element = tile.element }
                end
                return tile
            end
            t.drive._press_row = function(target, pos, action, deadline)
                if asks < 2 then
                    return "covered", "conformance: the staged covered press"
                end
                return press_row(target, pos, action, deadline)
            end
            t.note = function(text)
                notes[#notes + 1] = tostring(text)
                note(text)
            end
            local result, detail = press("plaguesheep_1", 1, 4)
            t.drive._npc_aim_tile = aim_tile
            t.drive._press_row = press_row
            t.note = note
            local said = table.concat(notes, " | ")
            if result ~= "ok" then
                return result, "press(plaguesheep_1) after a staged step: " .. describe(detail)
                    .. " [notes: " .. said .. "]"
            end
            if string.find(said, " between aim and press; re-aimed at ", 1, true) == nil
                or string.find(said, "moved ", 1, true) == nil then
                return "hollow", "the press landed but no note named the move: " .. said
                    .. " / " .. describe(detail)
            end
            return "ok", said .. " / " .. describe(detail)
        end)

        -- SEAM settle_modal_mount (pointer.lua QD.player._settle_after_click's
        -- modal arm, _mounted_groups/_modal_mount_is_new; seam12).  A click
        -- whose whole answer is a NON-chat interface -- opheld1 on
        -- dwarf_rock_schematic1 runs betweenarock_schematics.rs2's
        -- if_openmain_side(dwarf_rock_schematics, dwarf_rock_schematics_control)
        -- -- prints no line, pages no dialogue and walks no route, and the
        -- settle used to wait out its budget and answer `timeout` on a click
        -- that visibly landed (build/quest_gate/betweenarock row 73,
        -- schematic_puzzle_final2 row 2).  Graded on inv_op answering ok with
        -- `[modal dwarf_rock_schematics]` in its detail.  The schematic is left
        -- open for the next row, which reads its puzzle varps.
        seam("seam.settle_modal_mount", function()
            local inv_op = verb("player", "inv_op")
            local held = verb("inv", "await")
            local await_open = verb("ui", "await_open")
            if not inv_op then return missing("player", "inv_op") end
            if not held then return missing("inv", "await") end
            if not await_open then return missing("ui", "await_open") end
            -- setup: the quest at the assembly stage, the four fragments held.
            setup_cheat("::clearinv")
            setup_cheat("::setvar varb299_dwarfrock_quest ^dwarfrock_assembling_schematics")
            setup_cheat("::give dwarf_rock_schematic1 1")
            setup_cheat("::give dwarf_rock_base_schematic 1")
            setup_cheat("::give dwarf_rock_schematic2 1")
            setup_cheat("::give dwarf_rock_schematic3 1")
            local have, have_detail = held("dwarf_rock_schematic3", 1, 10)
            if have ~= "ok" then
                return "no_subject", "::give dwarf_rock_schematic3 1 -> " .. describe(have) .. " "
                    .. describe(have_detail)
            end
            settle(2)
            local result, detail = inv_op("dwarf_rock_schematic1", 1)
            if result ~= "ok" then
                return result, "inv_op(dwarf_rock_schematic1,1): " .. describe(detail)
            end
            if string.find(tostring(detail), "[modal dwarf_rock_schematics]", 1, true) == nil then
                return "hollow", "inv_op answered ok without naming the modal it opened: " .. describe(detail)
            end
            local opened, open_detail = await_open("dwarf_rock_schematics", 5)
            if opened ~= "ok" then
                return opened, "inv_op said modal but ui.await_open disagrees: " .. describe(open_detail)
            end
            return "ok", describe(detail)
        end)

        -- SEAM var_server_reads_varp_alloc (state.lua QD._var_server_varp over
        -- api_drive.var_content; seam12).  A varp this tree allocates above
        -- the cache's highest id (pack/varp.alloc: twocats_lamp_pick 7152,
        -- dwarfrock_puzzle_* 7153-7162) has no client copy --
        -- ToriRSServer_SendVarpSmall never sends it -- so var.server answered
        -- `not_found/nil` for the whole run although ::setvar resolved and
        -- wrote it (build/quest_gate/seam12_varp_before: 14 of 19 rows).
        -- Graded on reading back what ::setvar wrote, through var.server and
        -- var.await_server, and on a schematic puzzle varp reading a number
        -- while the previous row's interface is up.
        seam("seam.var_server_reads_varp_alloc", function()
            local server = verb("var", "server")
            local await_server = verb("var", "await_server")
            if not server then return missing("var", "server") end
            if not await_server then return missing("var", "await_server") end
            setup_cheat("::setvar varp7152_twocats_lamp_pick 3")
            settle(2)
            local result, value, source = server("varp7152_twocats_lamp_pick")
            if result ~= "ok" or value ~= 3 then
                return (result == "ok") and "refused" or result,
                    "var.server(twocats_lamp_pick) after ::setvar 3 -> " .. describe(result) .. "/"
                    .. describe(value) .. " " .. describe(source)
            end
            local awaited, await_detail = await_server("varp7152_twocats_lamp_pick", 3, 5)
            if awaited ~= "ok" then
                return awaited, "var.await_server(twocats_lamp_pick, 3): " .. describe(await_detail)
            end
            local puzzle_result, puzzle_value, puzzle_source = server("varp7153_dwarfrock_puzzle_dx1")
            if puzzle_result ~= "ok" or type(puzzle_value) ~= "number" then
                return (puzzle_result == "ok") and "hollow" or puzzle_result,
                    "var.server(dwarfrock_puzzle_dx1) -> " .. describe(puzzle_result) .. "/"
                    .. describe(puzzle_value)
            end
            return "ok", "twocats_lamp_pick=3 (" .. describe(source) .. "); " .. describe(await_detail)
                .. "; dwarfrock_puzzle_dx1=" .. describe(puzzle_value) .. " (" .. describe(puzzle_source) .. ")"
        end)

        -- SEAM varbit_server_reads_untransmitted_base (state.lua
        -- QD._var_server_varbit over api_drive.varbit_content; seam35).  A
        -- varbit is bits of a base varp, and the client holds only what the
        -- server SENT: mm_daero / mm_caranock sit on mm_gnomes (varp 372),
        -- which no content .varp declares transmit=yes, so var.server and
        -- var.await_server read a confident ok/0 for Monkey Madness's whole
        -- leg 1 (sonnet-b44; build/quest_gate/s35vb_before: 6 of 14 rows).
        -- Graded on reading back what ::setvar wrote into BOTH varbits (the
        -- second write must keep the first's bits), and on a varbit whose
        -- base IS transmitted (horrorquest on deephorror) staying on the
        -- client-record channel, so the fallback is never the default.
        seam("seam.varbit_server_reads_untransmitted_base", function()
            local server = verb("var", "server")
            local await_server = verb("var", "await_server")
            if not server then return missing("var", "server") end
            if not await_server then return missing("var", "await_server") end
            setup_cheat("::setvar varb123_mm_daero 3")
            setup_cheat("::setvar varb122_mm_caranock 2")
            setup_cheat("::setvar varb34_horrorquest 2")
            settle(2)
            local awaited, await_detail = await_server("varb123_mm_daero", 3, 5)
            if awaited ~= "ok" then
                return awaited, "var.await_server(mm_daero, 3) after ::setvar: " .. describe(await_detail)
            end
            local result, value, source = server("varb122_mm_caranock")
            if result ~= "ok" or value ~= 2
                or not string.find(tostring(source), "server content copy", 1, true) then
                return (result == "ok") and "refused" or result,
                    "var.server(mm_caranock) after ::setvar 2 -> " .. describe(result) .. "/"
                    .. describe(value) .. " " .. describe(source)
            end
            local control, control_value, control_source = server("varb34_horrorquest")
            if control ~= "ok" or control_value ~= 2 or control_source ~= "server" then
                return (control == "ok") and "refused" or control,
                    "a transmitted varbit left the client-record channel: var.server(horrorquest) -> "
                    .. describe(control) .. "/" .. describe(control_value) .. " " .. describe(control_source)
            end
            setup_cheat("::setvar varb123_mm_daero 0")
            setup_cheat("::setvar varb122_mm_caranock 0")
            setup_cheat("::setvar varb34_horrorquest 0")
            return "ok", describe(await_detail) .. "; mm_caranock=2 (" .. describe(source)
                .. "); horrorquest=2 (" .. describe(control_source) .. ")"
        end)

        -- ------------------------ phase 7c: a model component's pose
        --
        -- ui.model_pose / ui.await_model_pose (seam16 engine-if-model-angle).
        -- AFTER every Lumbridge seam row, not beside the objectbox rows of
        -- phase 5 where they first sat: their few ticks there moved the
        -- camera seam.press_pixel inherits, and it went `covered` twice
        -- running (the same binary under HEAD's harness pressed its tree).
        -- The objectbox's `item` is a type-6 MODEL component
        -- (interfaces/objectbox.if [item] type=6) and ::objbox puts an obj on
        -- it, so the reading is knowable: a pose on that component, and
        -- REFUSED on the box's `text` (type 4) -- a reader that answered a row
        -- of zeroes for a non-model would pass the first half and not this.
        -- The if_setangle half is pinned by the server selftest (IF_SETANGLE
        -- keeps xan/yan/zoom) and build/quest_gate/s16_angle_probe2.
        stage(function()
            local close = verb("chat", "close")
            if close then
                close()
            end
            setup_cheat("::objbox " .. SEAM_OBJ_SYMBOL .. " 250")   -- setup
            settle(3)
        end)

        local objbox_pose = nil
        step("ui.model_pose", function()
            local fn = verb("ui", "model_pose")
            if not fn then return missing("ui", "model_pose") end
            local result, detail, pose = fn(OBJECTBOX_INTERFACE .. ":item")
            if result ~= "ok" then
                return result, describe(detail)
            end
            if type(pose) ~= "table" or type(pose.zoom) ~= "number" or pose.zoom <= 0 then
                return "hollow", "answered ok but the pose carries no zoom -- " .. describe(detail)
            end
            objbox_pose = pose
            local text_result, text_detail = fn(OBJECTBOX_INTERFACE .. ":text")
            if text_result ~= "refused" then
                return "hollow", "objectbox:text is type 4, not a model, and answered "
                    .. describe(text_result) .. " -- " .. describe(text_detail)
            end
            return "ok", describe(detail) .. "; objectbox:text -> refused"
        end)

        step("ui.await_model_pose", function()
            local fn = verb("ui", "await_model_pose")
            if not fn then return missing("ui", "await_model_pose") end
            if objbox_pose == nil then
                return "no_subject", "ui.model_pose read no pose to await"
            end
            local result, detail = fn(OBJECTBOX_INTERFACE .. ":item",
                { xan = objbox_pose.xan, yan = objbox_pose.yan, zoom = objbox_pose.zoom }, 4)
            if result ~= "ok" then
                return result, describe(detail)
            end
            -- And a pose nobody set must time out, naming the last reading.
            local never, never_detail = fn(OBJECTBOX_INTERFACE .. ":item",
                { zoom = objbox_pose.zoom + 1 }, 2)
            if never ~= "timeout" then
                return "hollow", "a zoom nobody set (" .. (objbox_pose.zoom + 1) .. ") answered "
                    .. describe(never) .. " -- " .. describe(never_detail)
            end
            return "ok", describe(detail) .. "; zoom+1 -> timeout (" .. describe(never_detail) .. ")"
        end)

        -- ui.text / ui.expect_text (seam29 setup_wield_text_read_and_reach_honesty):
        -- the objectbox body read into the ledger; an unmounted component is not_found,
        -- and a string the box does not hold is not_found too.
        local objbox_text = nil
        step("ui.text", function()
            local fn = verb("ui", "text")
            if not fn then return missing("ui", "text") end
            local result, text = fn(OBJECTBOX_INTERFACE .. ":text")
            if result ~= "ok" then
                return result, describe(text)
            end
            if type(text) ~= "string" or text == "" then
                return "hollow", "objectbox:text answered ok with no text"
            end
            objbox_text = text
            local gone_result = fn("questjournal:title")
            if gone_result ~= "not_found" then
                return "hollow", "an unmounted component answered " .. describe(gone_result)
            end
            return "ok", "objectbox:text reads '" .. text .. "'; questjournal:title -> not_found"
        end)

        step("ui.expect_text", function()
            local fn = verb("ui", "expect_text")
            if not fn then return missing("ui", "expect_text") end
            if objbox_text == nil then
                return "no_subject", "ui.text read nothing to expect"
            end
            local word = string.match(objbox_text, "%S+")
            local result, detail = fn(OBJECTBOX_INTERFACE .. ":text", word, 1)
            if result ~= "ok" then
                return result, describe(detail)
            end
            local miss_result = fn(OBJECTBOX_INTERFACE .. ":text", "zz-not-on-this-box-zz", 1)
            if miss_result ~= "not_found" then
                return "hollow", "a string the box does not hold answered " .. describe(miss_result)
            end
            return "ok", describe(detail) .. "; a miss -> not_found"
        end)

        stage(function()
            local close = verb("chat", "close")
            if close then
                close()                                        -- setup: the box
            end
            settle(2)
        end)

        -- ------------------------------- phase 7d: a sea leg (sail.*)
        --
        -- seam16 engine-sailing-for-quests.  The last world rows, because they
        -- leave Lumbridge: Catherby, the player's own skiff at its berth, a
        -- courier task off the Catherby board (OSRS wiki "Courier tasks":
        -- choose it on the notice board, take the crate at the ledger table,
        -- load it into the boat's hold), then a real sea leg whose hull crosses
        -- into a map square content binds ([mapzone,0_43_52],
        -- skill_farming/scripts/farming_hops.rs2) -- which is what the arrival
        -- hook (torirs_server_vessel.c vessel_update_zones) exists to report.
        -- Proven first as build/quest_gate/s16b_sea_leg and s16b_port_task.
        stage(function()
            setup_cheat("::setlevel sailing 20")               -- setup
            setup_cheat("::setvar varb19258_sailing_boat_1_owned 1")     -- setup
            setup_cheat("::setvar varb19259_sailing_boat_1_type 1")      -- setup: a skiff
            setup_cheat("::setvar varb19260_sailing_boat_1_port 6")      -- setup: Catherby's dock_id
            setup_cheat("::setvar varb18554_sailing_last_personal_boat_boarded 1") -- setup
            setup_cheat("::setvar varb19279_sailing_boat_1_hotspot_6 1") -- setup: a cargo hold
            -- Catherby's shore, not the pier: a ::goto onto the pier strands
            -- the player (the seam's own finding).
            setup_cheat("::goto 2803 3430 0")                  -- setup
            settle(6)
        end)

        local CATHERBY_GANGPLANK = "sailing_gangplank_catherby"
        local SEAM_SHORE_OBJ = "cosmicrune"
        local CATHERBY_BOARD = "port_task_board_catherby"
        local CATHERBY_LEDGER = "dock_loading_bay_ledger_table_catherby"
        -- The Catherby board's first offer: port_task_catherby_courier_0,
        -- task_id 58, "Port Sarim flax delivery" (configs/all.dbrow), its
        -- cargo at Catherby and its destination Port Sarim.
        local FIRST_TASK_ID = 58

        step("sail.state", function()
            local fn = verb("sail", "state")
            if not fn then return missing("sail", "state") end
            local result, reading = fn()
            if result ~= "ok" then
                return result, describe(reading)
            end
            if type(reading) ~= "table" or reading.aboard ~= false
                or reading.player_x ~= 2803 or reading.player_z ~= 3430 then
                return "hollow", "answered ok but not the ashore reading at 2803,3430 -- "
                    .. describe(reading)
            end
            return "ok", "ashore at " .. reading.player_x .. "," .. reading.player_z
        end)

        step("sail.task_board", function()
            local fn = verb("sail", "task_board")
            if not fn then return missing("sail", "task_board") end
            local result, detail = fn(CATHERBY_BOARD)
            return names_reading(result, detail, "", { "open", "slots" },
                "the board it opened and the slots it read")
        end)

        step("sail.task_accept", function()
            local fn = verb("sail", "task_accept")
            if not fn then return missing("sail", "task_accept") end
            local result, detail = fn(0)
            return names_reading(result, detail, "", { "0:" .. FIRST_TASK_ID .. "/0/0" },
                "slot 0 holding task " .. FIRST_TASK_ID .. " with nothing taken")
        end)

        step("sail.tasks", function()
            local fn = verb("sail", "tasks")
            if not fn then return missing("sail", "tasks") end
            local result, slots, text = fn()
            if result ~= "ok" then
                return result, describe(text)
            end
            if type(slots) ~= "table" or type(slots[1]) ~= "table" or slots[1].id ~= FIRST_TASK_ID then
                return "hollow", "answered ok but slot 0 is not task " .. FIRST_TASK_ID .. " -- "
                    .. describe(text)
            end
            return "ok", describe(text)
        end)

        step("sail.cargo_take", function()
            local fn = verb("sail", "cargo_take")
            if not fn then return missing("sail", "cargo_take") end
            local result, detail = fn(CATHERBY_LEDGER, 3)
            return names_reading(result, detail, "", { "carrying", "0:" .. FIRST_TASK_ID .. "/1/0" },
                "the crate in hand and slot 0's taken count at 1")
        end)

        stage(function()
            local walk = verb("player", "walk_to")
            if walk then
                walk(2797, 3413, 30)                           -- setup: by the gangplank
            end
            -- seam.aboard_pool_radius_from_hull's subject: a ROOT ground obj
            -- on the shore beside the moored hull (::dropobj spawns it at the
            -- player's tile, general/scripts/misc/cheat_obj.rs2:15).
            setup_cheat("::dropobj " .. SEAM_SHORE_OBJ .. " 1") -- setup
            settle(2)
        end)

        step("sail.board", function()
            local fn = verb("sail", "board")
            if not fn then return missing("sail", "board") end
            local result, detail = fn(CATHERBY_GANGPLANK)
            return names_reading(result, detail, "", { "hull", "client=true" },
                "the hull boarded, live on the client")
        end)

        -- seam18 B.  ABOARD, the npc/obj pool reads measure radius and
        -- "nearest" from the rider's HULL-PROJECTED root tile
        -- (torirs_plugin_drive_ui.c drive_ui_search_origin ->
        -- app_wev_actor_root_fine).  A rider's grid position is view-LOCAL, so
        -- before the fix the origin was base + deck grid, a tile near the
        -- scene's corner, and every radius>0 read aboard missed what stood
        -- beside the hull: obj_near answered not_found, await_gone passed
        -- hollowly (build/quest_gate/seam18b_radius_before: 'obj.aboard.r15
        -- FAIL not_found beer'; _after 11/11).  The subject is the rune the
        -- stage above dropped on the shore by the gangplank.
        seam("seam.aboard_pool_radius_from_hull", function()
            local state = verb("sail", "state")
            local obj_near = verb("world", "obj_near")
            if not state then return missing("sail", "state") end
            if not obj_near then return missing("world", "obj_near") end
            local state_result, reading = state()
            if state_result ~= "ok" or type(reading) ~= "table" or reading.aboard ~= true then
                return "no_subject", "not aboard: " .. describe(reading)
            end
            local result, row = obj_near(SEAM_SHORE_OBJ, 15)
            if result ~= "ok" or type(row) ~= "table" then
                return result, "aboard (hull " .. describe(reading.hull_x) .. "," .. describe(reading.hull_z)
                    .. "): obj_near(" .. SEAM_SHORE_OBJ .. ", 15) -> " .. describe(row)
            end
            return "ok", "aboard (hull " .. describe(reading.hull_x) .. "," .. describe(reading.hull_z)
                .. "): the shore's " .. SEAM_SHORE_OBJ .. " at " .. describe(row.tile_x) .. "," .. describe(row.tile_z)
                .. " within 15 of the hull-projected rider"
        end)

        step("sail.cargo_load", function()
            local fn = verb("sail", "cargo_load")
            if not fn then return missing("sail", "cargo_load") end
            local result, detail = fn("cargo hold")
            return names_reading(result, detail, "", { "into the cargo hold" },
                "content's own load line")
        end)

        step("sail.helm", function()
            local fn = verb("sail", "helm")
            if not fn then return missing("sail", "helm") end
            local result, detail = fn("Helm")
            return names_reading(result, detail, "", { "helm=true" }, "the rider at the helm")
        end)

        step("sail.sails", function()
            local fn = verb("sail", "sails")
            if not fn then return missing("sail", "sails") end
            local result, detail = fn(true)
            return names_reading(result, detail, "", { "sails=true" }, "the sails reading set")
        end)

        -- The berth is in map square 0_43_53 (z 3392..3455).  Sail to 2791,3398
        -- -- still inside it -- and the hull, still heading south, crosses
        -- z 3391 into the bound 0_43_52 a few ticks later: the arrival
        -- await_arrival has to SEE happen, not one that happened before it.
        step("sail.sail_to", function()
            local fn = verb("sail", "sail_to")
            if not fn then return missing("sail", "sail_to") end
            local result, detail = fn(2791, 3398, 2)
            return names_reading(result, detail, "", { "heading press", "hull" },
                "the presses it made and where the hull reads")
        end)

        step("sail.await_arrival", function()
            local fn = verb("sail", "await_arrival")
            if not fn then return missing("sail", "await_arrival") end
            local result, detail = fn("[mapzone,0_43_52]", 40)
            return names_reading(result, detail, "", { "last=[mapzone,0_43_52]" },
                "the bound square the hull crossed into")
        end)

        stage(function()
            local sail_to = verb("sail", "sail_to")
            local sails = verb("sail", "sails")
            if sail_to then
                sail_to(2793, 3407, 1)                         -- setup: back to the berth
            end
            if sails then
                sails(false)                                   -- setup: furl
            end
        end)

        step("sail.disembark", function()
            local fn = verb("sail", "disembark")
            if not fn then return missing("sail", "disembark") end
            local result, detail = fn(CATHERBY_GANGPLANK)
            return names_reading(result, detail, "", { "ashore at" }, "the player ashore")
        end)

        -- Delivery is the honest negative here: the crate is in the hold, and
        -- this is its CARGO port, not its destination (Port Sarim).  The
        -- verb must not report a delivery that did not happen -- slot 0
        -- still holds task 58 with one crate taken and none delivered.  (A
        -- positive delivery needs quest_pandemonium's ledger op1 to fall
        -- through to port_tasks.rs2; recorded as open in seam16's close.)
        step("sail.cargo_deliver", function()
            local fn = verb("sail", "cargo_deliver")
            if not fn then return missing("sail", "cargo_deliver") end
            local result, detail = fn(CATHERBY_LEDGER, 6)
            if result == "timeout" and string.find(tostring(detail), "0:" .. FIRST_TASK_ID .. "/1/0", 1, true) then
                return "ok", "no delivery at the cargo port, slots unchanged: " .. describe(detail)
            end
            if result == "ok" then
                return "refused", "reported a delivery at the cargo port -- " .. describe(detail)
            end
            return result, describe(detail)
        end)

        -- ----------------------- phase 7c: a spell cast on an npc (seam19)
        --
        -- After the sail block, which ends ashore in Catherby, and before the
        -- scheduler's own controls: these rows move the player, and nothing
        -- after them but the session rows reads the world -- which compare a
        -- reading before a logout with one after it, wherever that is.  The
        -- closing stage puts the player back on `::tele lumbridge`'s landing.
        stage(function()
            setup_cheat("::tele lumbridge")
            settle(4)
            for i = 1, #CAST_PASSIVE do
                setup_cheat("::passive " .. CAST_PASSIVE[i])      -- setup
            end
            setup_cheat("::give airrune 5")                        -- setup
            setup_cheat("::give mindrune 5")                       -- setup
            local goto_tile = verb("player", "goto_tile")
            if goto_tile then
                goto_tile(CAST_TILE_X, CAST_TILE_Z, 0)
            end
            settle(2)
            -- The subject is SPAWNED beside the player (::spawn lands at the
            -- player's tile + 1), then made passive: the field's own goblins
            -- wander, and the nearest one can stand where the player cannot
            -- path -- the seam19 closer's conformance run pressed a goblin
            -- behind the house at CAST_TILE and read "I can't reach that!"
            -- (build/quest_gate/s19close_probe3), while the same rows run
            -- alone passed.  ::passive is per instance, so it follows the spawn.
            setup_cheat("::spawn " .. CAST_NPC_SYMBOL)             -- setup
            setup_cheat("::passive " .. CAST_NPC_SYMBOL)           -- setup
            settle(2)
        end)

        -- THE REFUSAL IS ITS OWN ANSWER.  A cast the server declines prints a
        -- sentence and nothing else: no runes spent, no XP, no projectile --
        -- so without reading the sentence the verb could only time out, and a
        -- timeout reads as "cast again".  Fire Wave at Magic 1 is declined by
        -- magic.rs2's level check word for word; the row requires `refused`,
        -- that sentence, and Magic XP unmoved.
        seam("seam.cast_names_its_refusal", function()
            local fn = verb("player", "cast")
            if not fn then return missing("player", "cast") end
            local skill = t.skill
            local read = is_table(skill) and type(skill.read) == "function" and skill.read or nil
            if not read then return missing("skill", "read") end
            local _, before = read("magic")
            local result, detail = fn(CAST_REFUSED_SPELL, CAST_NPC_SYMBOL, 10)
            local _, after = read("magic")
            local text = CAST_REFUSED_SPELL .. " -> " .. describe(detail)
            if result ~= "refused" then
                return result == "ok" and "hollow" or result,
                    "a Magic-1 Fire Wave answered " .. describe(result) .. ", not refused -- " .. text
            end
            if not string.find(tostring(detail), "Your Magic level is not high enough", 1, true) then
                return "hollow", "refused without naming the server's sentence -- " .. text
            end
            if not (is_table(before) and is_table(after)) or after.experience ~= before.experience then
                return "hollow", "refused, but Magic XP moved " .. describe(before) .. " -> "
                    .. describe(after) .. " -- " .. text
            end
            return "ok", text
        end)

        -- The cast itself, graded on what the SERVER did, never on the verb's
        -- word: one air rune and one mind rune gone from the backpack
        -- (~pvm_spell_cast's ~delete_spell_runes) and Magic XP up
        -- (~give_spell_xp) -- both paid whether the spell lands or splashes,
        -- which is why they are the proof of a cast and a splat is not (the
        -- verb's banner in spell.lua: a melee auto-retaliation splats too).
        step("player.cast", function()
            local fn = verb("player", "cast")
            if not fn then return missing("player", "cast") end
            local count = verb("inv", "count")
            local skill = t.skill
            local read = is_table(skill) and type(skill.read) == "function" and skill.read or nil
            if not (count and read) then
                return "no_subject", "t.inv.count / t.skill.read missing, so the cast cannot be graded"
            end
            local _, air_before = count("airrune")
            local _, mind_before = count("mindrune")
            local _, xp_before = read("magic")
            if type(air_before) ~= "number" or air_before < 1
                or type(mind_before) ~= "number" or mind_before < 1 then
                return "no_subject", "the stage left airrune " .. describe(air_before)
                    .. ", mindrune " .. describe(mind_before) .. " -- nothing to cast with"
            end
            local result, detail = fn(CAST_SPELL, CAST_NPC_SYMBOL, 15)
            local text = CAST_SPELL .. " on " .. CAST_NPC_SYMBOL .. " -> " .. describe(detail)
            if result ~= "ok" then
                return result, text
            end
            local _, air_after = count("airrune")
            local _, mind_after = count("mindrune")
            local _, xp_after = read("magic")
            if air_after ~= air_before - 1 or mind_after ~= mind_before - 1 then
                return "hollow", "answered ok but the runes read airrune " .. describe(air_before)
                    .. " -> " .. describe(air_after) .. ", mindrune " .. describe(mind_before)
                    .. " -> " .. describe(mind_after) .. " (one of each is the cast) -- " .. text
            end
            if not (is_table(xp_before) and is_table(xp_after))
                or not (xp_after.experience > xp_before.experience) then
                return "hollow", "answered ok but Magic XP did not rise -- " .. text
            end
            return "ok", text .. " [runes " .. air_before .. "->" .. air_after .. " air, "
                .. mind_before .. "->" .. mind_after .. " mind; magic xp "
                .. xp_before.experience .. "->" .. xp_after.experience .. "]"
        end)

        -- ONE COPY, PRESSED AND WATCHED (seam21, attack_press_and_watch_same_slot).
        -- player.attack pressed the copy App_NpcScreenPosition ranks nearest
        -- the VIEWPORT CENTRE and watched npc.nearest's slot; with two copies
        -- about those differ, and the row timed out on a fight it was not
        -- watching (Rum Deal's eleven fever spiders, Zogre's slash bash).
        -- The subject is a goblin PAIR placed so they DO differ: ::spawn lands
        -- at the player's tile +1,+1, so N stands two tiles south of the stand
        -- tile and F north-east of it, and a camera yawed at F at pitch 128
        -- puts N behind the eye.  Measured with the pre-seam21 press
        -- (build/quest_gate/s21as_gob7 and s21as_gob8, row old.press_vs_watch):
        -- "bare press landed on slot 141 ... watched nearest slot 132 (hp -1)
        -- -> DIFFERENT copy".  At pitch 383 the player sits at the viewport
        -- centre and the two never differ (s21as_gob2..gob6).
        stage(function()
            setup_cheat("::give " .. COMBAT_WEAPON .. " 1")
            -- Magic 99 for seam.cast_presses_the_named_copy (put back to 1
            -- after it), set HERE, before this stage's equip and the melee row
            -- below: the player's accuracy rolls are the %com_* varps
            -- [proc,player_combat_stat] computes (skill_combat/combat_stats.rs2),
            -- which a melee start reruns ([label,player_combat_start]) and
            -- ::setlevel does not (ToriRSServer_CombatSetLevel).  Set after the
            -- melee, eight Wind Strikes on the named goblin all splashed at
            -- "Magic 99" (build/quest_gate/s22cast_conf2..conf5, row 155).
            setup_cheat("::setlevel magic 99")                     -- setup
            settle(2)
            local equip = verb("player", "equip")
            if equip then
                equip(COMBAT_WEAPON)
            end
            local goto_tile = verb("player", "goto_tile")
            if goto_tile then
                goto_tile(ATTACK_PAIR_SPAWN_N_X, ATTACK_PAIR_SPAWN_N_Z, 0)
                setup_cheat("::spawn " .. CAST_NPC_SYMBOL)         -- setup
                goto_tile(ATTACK_PAIR_SPAWN_F_X, ATTACK_PAIR_SPAWN_F_Z, 0)
                setup_cheat("::spawn " .. CAST_NPC_SYMBOL)         -- setup
                setup_cheat("::passive " .. CAST_NPC_SYMBOL)       -- setup
                goto_tile(ATTACK_PAIR_STAND_X, ATTACK_PAIR_STAND_Z, 0)
            end
            settle(3)
        end)

        seam("seam.attack_presses_the_watched_slot", function()
            local fn = verb("player", "attack")
            local tiles = verb("npc", "tiles")
            local tile = verb("world", "tile")
            if not fn then return missing("player", "attack") end
            if not tiles then return missing("npc", "tiles") end
            if not tile then return missing("world", "tile") end
            local function copies()
                local r, _, found = tiles(CAST_NPC_SYMBOL, 0)
                return (r == "ok" and is_table(found)) and found or {}
            end
            local function slot_of(element, found)
                for i = 1, #found do
                    if found[i].element_id == element then return found[i].slot end
                end
                return nil
            end
            local found = copies()
            local _, here = tile()
            if #found < 2 or not is_table(here) then
                return "no_subject", "fewer than two " .. CAST_NPC_SYMBOL .. " copies ("
                    .. describe(#found) .. ") or no player tile"
            end
            local near = found[1]
            -- A copy on the OTHER side of the player from the nearest one,
            -- the one standing closest to where F landed: facing it puts the
            -- nearest behind the camera, and it is the open-field copy the
            -- {slot} press below names (the field's own goblins wander into
            -- the house east of here, where "I can't reach that!" answers --
            -- s21as_conf1 row 154, slot 6).
            local away = nil
            local best = nil
            local nx, nz = near.x - here.x, near.z - here.z
            for i = 2, #found do
                local row = found[i]
                if (row.x - here.x) * nx + (row.z - here.z) * nz < 0 then
                    local d = (row.x - ATTACK_PAIR_F_X) * (row.x - ATTACK_PAIR_F_X)
                        + (row.z - ATTACK_PAIR_F_Z) * (row.z - ATTACK_PAIR_F_Z)
                    if best == nil or d < best then
                        away = row
                        best = d
                    end
                end
            end
            away = away or found[2]
            local yaw = t.drive._yaw_towards(away.x - here.x, away.z - here.z)
            if yaw then
                t.drive.camera(yaw, 128, 400)
                settle(2)
            end
            local target = t.player.by_symbol("npc", CAST_NPC_SYMBOL)
            local _, projected = t.drive._projection(target)
            local ranked = is_table(projected) and slot_of(projected.element_id, copies()) or nil
            local condition = "nearest slot " .. describe(near.slot) .. ", the viewport centre ranks slot "
                .. describe(ranked) .. (ranked ~= near.slot and " (they DIFFER -- the pre-seam21 shape)"
                    or " (they agree this run)")
            local result, detail = fn(CAST_NPC_SYMBOL, COMBAT_ATTACK_OP, 20)
            local text = condition .. "; attack -> " .. describe(result) .. " " .. describe(detail)
            if result ~= "ok" then
                return result, text
            end
            if not string.find(tostring(detail), "pressed slot " .. tostring(near.slot) .. " ", 1, true)
                or not string.find(tostring(detail), "watching slot " .. tostring(near.slot) .. ":", 1, true) then
                return "hollow", "the detail does not name the nearest slot as both pressed and watched -- "
                    .. text
            end
            -- The fight on the nearest copy is finished before the next press,
            -- by the slot the press stamped: a second Attack while the first
            -- fight is live is a single-way question, not this row's.
            local dead = verb("npc", "await_dead_engaged")
            if dead then
                local dead_result, dead_detail = dead(40)
                text = text .. "; await_dead_engaged -> " .. describe(dead_result)
                if dead_result ~= "ok" then
                    return dead_result, text .. " " .. describe(dead_detail)
                end
            end
            -- And a NAMED copy: the one the camera was turned to, by slot.
            local other = nil
            found = copies()
            for i = 1, #found do
                if found[i].slot == away.slot and away.slot ~= near.slot then
                    other = found[i]
                end
            end
            if other == nil then
                return "ok", text .. " [slot " .. describe(away.slot)
                    .. " left the pool before the {slot} press]"
            end
            local result2, detail2 = fn(CAST_NPC_SYMBOL, COMBAT_ATTACK_OP, 20, { slot = other.slot })
            local text2 = "{slot=" .. describe(other.slot) .. "} -> " .. describe(result2) .. " "
                .. describe(detail2)
            local said = string.match(tostring(detail2), "the SERVER refused the swing: '[^']*'")
            if said then
                text2 = said .. " -- " .. text2
            end
            if result2 ~= "ok" then
                return result2, text .. " || " .. text2
            end
            if not string.find(tostring(detail2), "pressed slot " .. tostring(other.slot) .. " ", 1, true)
                or not string.find(tostring(detail2), "watching slot " .. tostring(other.slot) .. ":", 1, true) then
                return "hollow", "the named press does not name slot " .. describe(other.slot)
                    .. " as both pressed and watched -- " .. text2
            end
            return "ok", text .. " || " .. text2
        end)

        -- ONE COPY, CAST ON (seam22, cast_picks_one_copy).  player.cast took
        -- npc.nearest's slot for its settle and pressed a bare-id row, which
        -- lands on the copy App_NpcScreenPosition ranks nearest the VIEWPORT
        -- CENTRE, and took no selector at all: measured
        -- build/quest_gate/s22cast_before1 row cast.named_slot -- `{slot=72}`
        -- ignored, the verb read slot 125 and slot 72 was never hit.  The row
        -- casts on a FRESH pair's far copy, named by slot with the camera on
        -- the nearest copy, and grades on which copy the SERVER hit (every
        -- copy's hit cycle and bar before, and three ticks after each cast; a
        -- splash shows nothing, so up to eight casts) -- never on the verb's
        -- word.  Then the stamp's fight is waited out and its row must carry
        -- the re-CAST tag: a cast fight is re-engaged by casting, not by an
        -- Attack press (spell.lua's await_dead_engaged wrap).
        --
        -- The stage first waits out whatever fight the attack row above left
        -- live (single-way: a second copy is refused "I'm already under
        -- attack." while it holds the claim -- s22cast_after1 row 5), then
        -- spawns the pair afresh on the attack row's tiles.
        stage(function()
            local dead = verb("npc", "await_dead_engaged")
            if dead then
                dead(60)
            end
            setup_cheat("::give airrune 30")                       -- setup
            setup_cheat("::give mindrune 30")                      -- setup
            -- The phase 3 platebody is still worn (-30 magic attack), and it
            -- is a prerequisite of nothing here: off, so the casts below can
            -- land.  `not_found` when an earlier row left it off is fine.
            local unequip = verb("player", "unequip")
            if unequip then
                unequip(WEARABLE_OBJ_SYMBOL)
            end
            local goto_tile = verb("player", "goto_tile")
            if goto_tile then
                goto_tile(ATTACK_PAIR_SPAWN_N_X, ATTACK_PAIR_SPAWN_N_Z, 0)
                setup_cheat("::spawn " .. CAST_NPC_SYMBOL)         -- setup
                goto_tile(ATTACK_PAIR_SPAWN_F_X, ATTACK_PAIR_SPAWN_F_Z, 0)
                setup_cheat("::spawn " .. CAST_NPC_SYMBOL)         -- setup
                setup_cheat("::passive " .. CAST_NPC_SYMBOL)       -- setup
                goto_tile(ATTACK_PAIR_STAND_X, ATTACK_PAIR_STAND_Z, 0)
            end
            settle(3)
        end)

        seam("seam.cast_presses_the_named_copy", function()
            local fn = verb("player", "cast")
            local tiles = verb("npc", "tiles")
            local tile = verb("world", "tile")
            local dead = verb("npc", "await_dead_engaged")
            if not fn then return missing("player", "cast") end
            if not tiles then return missing("npc", "tiles") end
            if not tile then return missing("world", "tile") end
            if not dead then return missing("npc", "await_dead_engaged") end
            local function copies()
                local r, _, found = tiles(CAST_NPC_SYMBOL, 0)
                return (r == "ok" and is_table(found)) and found or {}
            end
            local function reading()
                local by_slot = {}
                local found = copies()
                for i = 1, #found do
                    by_slot[found[i].slot] = { found[i].hit_cycle, found[i].health_ratio }
                end
                return by_slot
            end
            -- A copy counts as hit when its hit cycle or bar moved -- or, for
            -- the NAMED copy only, when it left the pool: Wind Strike one-shots
            -- a goblin (s22cast_conf6 row 155, "hp no bar -> 0/30 ... newest
            -- splat 5", then no_row).  Any other copy leaving is a field goblin
            -- wandering out of the pool (s22cast_conf7: slot 118), not a hit.
            local function hit_since(before, named_slot)
                local now = reading()
                local hit = {}
                for slot, was in pairs(before) do
                    local is = now[slot]
                    if (is == nil and slot == named_slot)
                        or (is ~= nil and (is[1] > was[1] or is[2] ~= was[2])) then
                        hit[#hit + 1] = slot
                    end
                end
                table.sort(hit)
                return hit
            end
            local found = copies()
            local _, here = tile()
            if #found < 2 or not is_table(here) then
                return "no_subject", "fewer than two " .. CAST_NPC_SYMBOL .. " copies ("
                    .. describe(#found) .. ") or no player tile"
            end
            local near = found[1]
            -- The spawned far copy by its landing tile; else the copy nearest
            -- that tile that is not the nearest copy.
            local named = nil
            local best = nil
            for i = 2, #found do
                local row = found[i]
                local d = (row.x - ATTACK_PAIR_F_X) * (row.x - ATTACK_PAIR_F_X)
                    + (row.z - ATTACK_PAIR_F_Z) * (row.z - ATTACK_PAIR_F_Z)
                if best == nil or d < best then
                    named = row
                    best = d
                end
            end
            local yaw = t.drive._yaw_towards(near.x - here.x, near.z - here.z)
            if yaw then
                t.drive.camera(yaw, 128, 400)
                settle(2)
            end
            local condition = "named slot " .. describe(named.slot) .. " at " .. describe(named.x) .. ","
                .. describe(named.z) .. ", nearest slot " .. describe(near.slot)
                .. " (camera on the nearest)"
            local skill = t.skill
            local read = is_table(skill) and type(skill.read) == "function" and skill.read or nil
            if read then
                local _, magic = read("magic")
                condition = condition .. ", magic level " .. describe(is_table(magic) and magic.level)
            end
            local result, detail, hit
            local casts = 0
            local seen = {}
            for _ = 1, 8 do
                casts = casts + 1
                local before = reading()
                result, detail = fn(CAST_SPELL, CAST_NPC_SYMBOL, 15, nil, { slot = named.slot })
                settle(3)
                hit = hit_since(before, named.slot)
                -- What each cast read on its own copy (the harness's describe()
                -- truncates the verb's detail before this part).
                seen[#seen + 1] = describe(result) .. " "
                    .. (string.match(tostring(detail), "(watching slot .-) %-%- ") or "?")
                if result ~= "ok" or #hit > 0 then
                    break
                end
            end
            local text = condition .. "; " .. describe(casts) .. " cast(s) [" .. table.concat(seen, " | ")
                .. "], copies hit: " .. (#hit == 0 and "none" or table.concat(hit, ",")) .. "; cast -> "
                .. describe(result) .. " " .. describe(detail)
            if result ~= "ok" then
                return result, text
            end
            if not string.find(tostring(detail), "pressed slot " .. tostring(named.slot) .. " ", 1, true)
                or not string.find(tostring(detail), "watching slot " .. tostring(named.slot) .. ":", 1, true) then
                return "hollow", "the detail does not name the named slot as both pressed and watched -- "
                    .. text
            end
            if #hit ~= 1 or hit[1] ~= named.slot then
                return "hollow", "the server hit " .. (#hit == 0 and "no copy in eight casts"
                    or "another copy") .. " -- " .. text
            end
            local dead_result, dead_detail = dead(90, 12)
            text = text .. " || await_dead_engaged -> " .. describe(dead_result) .. " " .. describe(dead_detail)
            if dead_result ~= "ok" then
                return dead_result, text
            end
            if not string.find(tostring(dead_detail), "slot " .. tostring(named.slot) .. " ", 1, true)
                or not string.find(tostring(dead_detail), "[re-engagements re-CAST " .. CAST_SPELL, 1, true) then
                return "hollow", "the wait did not hold the named slot as a CAST fight -- " .. text
            end
            return "ok", text
        end)

        stage(function()
            setup_cheat("::setlevel magic 1")
            setup_cheat("::passive off")
            setup_cheat("::tele lumbridge")
            settle(4)
        end)

        -- AN OP-1 PRESS KEEPS THE ITEM.  inv_op's OPHELD press was followed by
        -- a "flash" of the backpack cell's own on_op hook, handed the OPHELD
        -- INDEX; on rev-239 that hook is clientscript 6014, whose op-1 branch
        -- is the shift-click-drop chain, so every inv_op(x, 1) ran the item's
        -- [opheld1] and then dropped it (Holy Grail's golden feather, the
        -- spade, both tier 1 books: build/seam_state/seam20/s20inv_before,
        -- `-> 0 left [STRAY DROP ...]`).  app_minimenu.c now flashes the
        -- cell's own op number.  The verb answers the same word either way --
        -- "Nothing interesting happens." off a dig site -- so the row is
        -- graded on the backpack five ticks later and on the absence of
        -- inv_op's STRAY DROP tag, never on the verb's result.
        stage(function()
            setup_cheat("::give " .. HELD_OP1_OBJ_SYMBOL)
            settle(2)
        end)
        seam("seam.held_op1_keeps_the_item", function()
            local fn = verb("player", "inv_op")
            local count = verb("inv", "count")
            if not fn then return missing("player", "inv_op") end
            if not count then return missing("inv", "count") end
            local before_result, before = count(HELD_OP1_OBJ_SYMBOL)
            if before_result ~= "ok" or type(before) ~= "number" or before < 1 then
                return "no_subject", "::give " .. HELD_OP1_OBJ_SYMBOL .. " left "
                    .. describe(before) .. " in the backpack (" .. describe(before_result) .. ")"
            end
            local result, detail = fn(HELD_OP1_OBJ_SYMBOL, 1)
            settle(5)
            local after_result, after = count(HELD_OP1_OBJ_SYMBOL)
            local text = "inv_op(" .. HELD_OP1_OBJ_SYMBOL .. ",1) -> " .. describe(result)
                .. " " .. describe(detail) .. "; count " .. describe(before) .. " -> "
                .. describe(after)
            if string.find(tostring(detail), "STRAY DROP", 1, true) then
                return "hollow", "the press put the item on the ground -- " .. text
            end
            if after_result ~= "ok" or after ~= before then
                return "hollow", "the backpack moved after an op that consumes nothing -- " .. text
            end
            return "ok", text
        end)

        -- ---------------------------------------------- seam23's four rows
        --
        -- THE FIRST use_on AFTER AN UNEQUIP ARMS.  player.unequip leaves the
        -- equipment tab up; use_on pressed the backpack tab and armed on the
        -- same frame, the cell was not painted yet, DrivePointer_InvArm
        -- answered REFUSED (not not_found) and _arm_held passed it straight
        -- out: `refused ... armed by this call (tab nil nil)` (The Tourist
        -- Trap's cell door key, build/quest_gate/s23b_base1).  use_on now
        -- waits for the backpack to PAINT and _arm_held retries a refusal
        -- (seam23, s23b_rows_fix2 3/3).  Graded on the verb's own word: the
        -- arming refusal is exactly what the old code answered.  The subject
        -- is a Hans SPAWNED beside the player: the castle's own Hans wanders
        -- (seam21, LostCity wander), and a shift in the world's random stream
        -- walked him out of the client's pool (seam24 closer: 'no npc 3105
        -- (hans) in the client's entity pool' on HEAD C and HEAD Lua alike,
        -- after which the armed pot turned the next row's drop into a use).
        stage(function()
            setup_cheat("::tele lumbridge")
            setup_cheat("::give bronze_scimitar")
            setup_cheat("::give pot_empty")
            setup_cheat("::spawn hans")
            settle(3)
        end)
        seam("seam.use_on_after_unequip", function()
            local equip = verb("player", "equip")
            local unequip = verb("player", "unequip")
            local use_on = verb("player", "use_on")
            local by_symbol = verb("player", "by_symbol")
            if not equip then return missing("player", "equip") end
            if not unequip then return missing("player", "unequip") end
            if not use_on then return missing("player", "use_on") end
            if not by_symbol then return missing("player", "by_symbol") end
            local equip_result, equip_detail = equip("bronze_scimitar")
            if equip_result ~= "ok" then
                return "no_subject", "equip bronze_scimitar -> " .. describe(equip_result) .. " "
                    .. describe(equip_detail)
            end
            local off_result, off_detail = unequip("bronze_scimitar")
            if off_result ~= "ok" then
                return "no_subject", "unequip bronze_scimitar -> " .. describe(off_result) .. " "
                    .. describe(off_detail)
            end
            local target, target_result = by_symbol("npc", "hans")
            if target_result ~= "ok" or type(target) ~= "table" then
                return "no_subject", "no hans to use the pot on (" .. describe(target_result) .. ")"
            end
            local result, detail = use_on("pot_empty", target)
            local text = "unequip -> " .. describe(off_detail) .. "; use_on pot_empty hans -> "
                .. describe(result) .. " " .. describe(detail)
            if string.find(tostring(detail), "armed by this call", 1, true) then
                return "hollow", "the first arming after the unequip was refused -- " .. text
            end
            return result, text
        end)

        -- A CAST ON A GROUND OBJ.  t.player.cast took npcs only, so Spirits of
        -- the Elid's Telekinetic Grab was hand-Taken and the sampler reverted
        -- the quest (sonnet-b27).  cast now takes use_on's {kind=, id=} world
        -- target and grades an obj cast on the obj ARRIVING in the backpack
        -- (seam23, build/quest_gate/s23cw_rows3).  Graded here on the backpack
        -- and the law rune, not on the verb's word.
        stage(function()
            setup_cheat("::setlevel magic 99")
            setup_cheat("::give lawrune 1")
            setup_cheat("::give bronze_dagger 1")
            settle(2)
        end)
        seam("seam.cast_on_ground_obj", function()
            local fn = verb("player", "cast")
            local goto_tile = verb("player", "goto_tile")
            local drop = verb("player", "drop")
            local walk_to = verb("player", "walk_to")
            local count = verb("inv", "count")
            if not fn then return missing("player", "cast") end
            if not goto_tile then return missing("player", "goto_tile") end
            if not drop then return missing("player", "drop") end
            if not walk_to then return missing("player", "walk_to") end
            if not count then return missing("inv", "count") end
            goto_tile(CAST_TILE_X, CAST_TILE_Z, 0)
            local drop_result, drop_detail = drop("bronze_dagger")
            if drop_result ~= "ok" then
                return "no_subject", "drop bronze_dagger -> " .. describe(drop_result) .. " "
                    .. describe(drop_detail)
            end
            walk_to(CAST_TILE_X + 4, CAST_TILE_Z + 2)
            local _, dagger_before = count("bronze_dagger")
            local _, law_before = count("lawrune")
            local result, detail = fn("telegrab", { kind = "obj", id = "bronze_dagger" })
            settle(1)
            local _, dagger_after = count("bronze_dagger")
            local _, law_after = count("lawrune")
            local text = "telegrab bronze_dagger from 4 tiles -> " .. describe(result) .. " "
                .. describe(detail) .. "; dagger " .. describe(dagger_before) .. " -> "
                .. describe(dagger_after) .. ", law " .. describe(law_before) .. " -> "
                .. describe(law_after)
            if result ~= "ok" then
                return result, text
            end
            if dagger_after ~= (dagger_before or 0) + 1 or law_after ~= (law_before or 0) - 1 then
                return "hollow", "cast answered ok but the backpack does not show the grab -- " .. text
            end
            return "ok", text
        end)

        -- raid seam1 client_npc_state_and_tile_hazards: world.spotanims, world.hazard_at,
        -- world.projectiles, npc.state, npc.state_text, npc.await_anim, on ordinary
        -- content (telegrab_impact 144 on a dropped dagger; a spawned Lumbridge man,
        -- attackrate 4, attack seq 422 human_unarmedpunch).

        local HAZARD_SPOT_TELEGRAB = 144
        local HAZARD_SPOT_PROJ = 91
        local HAZARD_SPOT_IMPACT = 92
        local HAZARD_SPOT_SPLASH = 85
        local STATE_NPC = "man"
        local STATE_SEQ_ATTACK = 422
        local STATE_ATTACKRATE = 4
        local state_slot = nil
        local state_levels = {}

        stage(function()
            setup_cheat("::give lawrune 1")                        -- setup
            setup_cheat("::give bronze_dagger 1")                  -- setup
            setup_cheat("::give airrune 5")                        -- setup
            setup_cheat("::give mindrune 5")                       -- setup
            settle(2)
        end)

        -- A MAP GRAPHIC ON A TILE (`spotanim_map`): the hazard half a raid
        -- needs (Xarpus acid, Maiden blood).  The client dropped the id; it
        -- now keeps it.  Telegrab's impact is placed on the dagger's tile.
        step("world.spotanims", function()
            local spotanims = verb("world", "spotanims")
            local cast = verb("player", "cast")
            local goto_tile = verb("player", "goto_tile")
            local drop = verb("player", "drop")
            local walk_to = verb("player", "walk_to")
            if not spotanims then return missing("world", "spotanims") end
            if not (cast and goto_tile and drop and walk_to) then
                return "no_subject", "player.cast/goto_tile/drop/walk_to missing"
            end
            goto_tile(CAST_TILE_X, CAST_TILE_Z, 0)
            -- A drop pressed on the teleport's own tick does not land (measured:
            -- `backpack 1 -> 1` right after `Teleported to ...`).
            settle(2)
            local drop_result, drop_detail = drop("bronze_dagger")
            if drop_result ~= "ok" then
                return "no_subject", "drop bronze_dagger -> " .. describe(drop_detail)
            end
            walk_to(CAST_TILE_X + 4, CAST_TILE_Z + 2)
            cast("telegrab", { kind = "obj", id = "bronze_dagger" }, 1)
            local found = nil
            local listing = "none"
            local waited = t.await({
                level = function()
                    local r, rows = spotanims(15)
                    if r ~= "ok" then
                        listing = describe(rows)
                        return false
                    end
                    for i = 1, #rows do
                        if rows[i].spotanim_id == HAZARD_SPOT_TELEGRAB then
                            found = rows[i]
                            return true
                        end
                    end
                    return false
                end,
                note = "telegrab_impact in world.spotanims",
            }, 6)
            if waited ~= "ok" or not found then
                return "timeout", "no spotanim " .. HAZARD_SPOT_TELEGRAB .. " within 6 ticks of the grab ("
                    .. listing .. ")"
            end
            if found.x ~= CAST_TILE_X or found.z ~= CAST_TILE_Z then
                return "hollow", string.format("spotanim %d at %d,%d, not the dagger's tile %d,%d",
                    found.spotanim_id, found.x, found.z, CAST_TILE_X, CAST_TILE_Z)
            end
            return "ok", string.format("spotanim %d at %d,%d level %d, active %s, %d cycle(s) left",
                found.spotanim_id, found.x, found.z, found.level, tostring(found.active), found.cycles_left)
        end)

        -- EVERYTHING ON ONE TILE.  The dagger came back to the backpack with
        -- the grab, so drop it again on CAST_TILE and read the tile: the obj
        -- must be there, and a neighbour answers with its own contents only.
        step("world.hazard_at", function()
            local hazard_at = verb("world", "hazard_at")
            local goto_tile = verb("player", "goto_tile")
            local drop = verb("player", "drop")
            if not hazard_at then return missing("world", "hazard_at") end
            if not (goto_tile and drop) then return "no_subject", "player.goto_tile/drop missing" end
            t.inv.await("bronze_dagger", 1, 6)
            -- The grab's own delay first: a drop pressed while the cast still
            -- holds the player is dropped by the server with no line (measured
            -- in the full harness: `backpack 2 -> 2, ground 0 -> 0` twice).
            settle(4)
            goto_tile(CAST_TILE_X, CAST_TILE_Z, 0)
            -- A drop pressed on the teleport's own tick does not land (measured:
            -- `backpack 1 -> 1` right after `Teleported to ...`).
            settle(2)
            local drop_result, drop_detail = drop("bronze_dagger")
            if drop_result ~= "ok" then
                -- The subject, not the verb under test: one more press after
                -- the player has stood still, named in the detail if it is used.
                settle(3)
                local first = describe(drop_detail)
                drop_result, drop_detail = drop("bronze_dagger")
                drop_detail = describe(drop_detail) .. " [second press; the first: " .. first .. "]"
            end
            if drop_result ~= "ok" then
                return "no_subject", "drop bronze_dagger -> " .. describe(drop_detail)
            end
            local r, hz = hazard_at(CAST_TILE_X, CAST_TILE_Z, 0)
            if r ~= "ok" then
                return r, describe(hz)
            end
            local has_obj = false
            for i = 1, #hz.objs do
                if hz.objs[i].count >= 1 then
                    has_obj = true
                end
            end
            if not has_obj then
                return "hollow", "the dropped dagger is not on its own tile: " .. tostring(hz.text)
            end
            local r2, empty = hazard_at(CAST_TILE_X + 1, CAST_TILE_Z - 2, 0)
            if r2 ~= "ok" or empty.spotanims == nil then
                return r2, describe(empty)
            end
            return "ok", tostring(hz.text) .. " | neighbour " .. tostring(empty.text)
        end)

        stage(function()
            local read = t.skill and t.skill.read
            if read then
                local _, attack = read("attack")
                local _, strength = read("strength")
                state_levels.attack = type(attack) == "table" and attack.level or nil
                state_levels.strength = type(strength) == "table" and strength.level or nil
            end
            -- The player's own punches keep the fight going (a man nobody
            -- hits back swings once and stops, npcst_after2); at Attack and
            -- Strength 1 they barely scratch his 7 hitpoints.
            setup_cheat("::setlevel attack 1")                     -- setup
            setup_cheat("::setlevel strength 1")                   -- setup
            setup_cheat("::spawn " .. STATE_NPC)                   -- setup
            settle(2)
        end)

        -- A PROJECTILE AIMED AT A TILE.  Wind Strike's travel graphic, homing
        -- on the man: target = his slot + 1, destination = his tile.
        step("world.projectiles", function()
            local projectiles = verb("world", "projectiles")
            local cast = verb("player", "cast")
            if not projectiles then return missing("world", "projectiles") end
            if not cast then return missing("player", "cast") end
            local state = verb("npc", "state")
            if not state then return missing("npc", "state") end
            local sr, man = state(STATE_NPC)
            if sr ~= "ok" then
                return "no_subject", "no " .. STATE_NPC .. " to cast on: " .. describe(man)
            end
            state_slot = man.slot
            cast("wind_strike", STATE_NPC, 1, 2, { slot = state_slot })
            local text = "none"
            local waited = t.await({
                level = function()
                    local r, rows = projectiles(0)
                    if r ~= "ok" then
                        text = describe(rows)
                        return false
                    end
                    for i = 1, #rows do
                        if rows[i].spotanim_id == HAZARD_SPOT_PROJ then
                            local r1, now = t.npc.state({ slot = state_slot })
                            text = string.format("projectile %d from %d,%d to %d,%d, target %d (npc slot %d),"
                                .. " %d cycle(s) left; %s slot %d at %s,%s", rows[i].spotanim_id,
                                rows[i].src_x, rows[i].src_z, rows[i].dst_x, rows[i].dst_z, rows[i].target,
                                rows[i].target_npc_slot, rows[i].cycles_left, STATE_NPC, state_slot,
                                r1 == "ok" and tostring(now.x) or "?", r1 == "ok" and tostring(now.z) or "?")
                            return r1 == "ok" and rows[i].target_npc_slot == state_slot
                                and rows[i].dst_x == now.x and rows[i].dst_z == now.z
                        end
                    end
                    return false
                end,
                note = "wind strike projectile aimed at the man",
            }, 6)
            if waited ~= "ok" then
                return "timeout", "no projectile " .. HAZARD_SPOT_PROJ .. " aimed at slot "
                    .. tostring(state_slot) .. " within 6 ticks: " .. text
            end
            return "ok", text
        end)

        -- WHAT THE NPC IS DOING: the impact graphic the server sent (92, or 85
        -- on a splash) with its tick, the face-lock on the player, and after
        -- npc.await_anim below the attack seq with the tick it arrived.
        step("npc.state", function()
            local state = verb("npc", "state")
            if not state then return missing("npc", "state") end
            if state_slot == nil then return "no_subject", "world.projectiles chose no man" end
            local text = "none"
            local waited = t.await({
                level = function()
                    local r, row = state({ slot = state_slot })
                    if r ~= "ok" then
                        text = describe(row)
                        return false
                    end
                    text = t.npc.state_text(row)
                    return (row.spotanim_sent_id == HAZARD_SPOT_IMPACT
                        or row.spotanim_sent_id == HAZARD_SPOT_SPLASH) and row.spotanim_tick >= 0
                end,
                note = "the man's impact graphic in npc.state",
            }, 8)
            if waited ~= "ok" then
                return "timeout", "no impact/splash spotanim on slot " .. tostring(state_slot) .. ": " .. text
            end
            return "ok", text
        end)

        -- t.npc.state_text(row): the one-line ledger reading of a state row.
        -- Graded on every state field being named in the line it returns, and
        -- (raid seam4 npc_state_size_and_stale_menu) on the man's footprint:
        -- every pool row carries `size`, 1 for a man (configs/all.npc states none).
        step("npc.state_text", function()
            local fn = verb("npc", "state_text")
            local state = verb("npc", "state")
            if not fn then return missing("npc", "state_text") end
            if not state then return missing("npc", "state") end
            if state_slot == nil then return "no_subject", "world.projectiles chose no man" end
            local r, row = state({ slot = state_slot })
            if r ~= "ok" then return "no_subject", "npc.state -> " .. describe(row) end
            local text = fn(row)
            for _, field in ipairs({ "size ", "anim ", "frame ", "spotanim ", "last seq ", "last spotanim ",
                "facing ", "last face square ", "hp " }) do
                if type(text) ~= "string" or not string.find(text, field, 1, true) then
                    return "hollow", "state_text does not name '" .. field .. "': " .. describe(text)
                end
            end
            if row.size ~= 1 then
                return "refused", "the man's footprint read " .. describe(row.size)
                    .. ", the cache says 1: " .. describe(text)
            end
            return "ok", text
        end)

        -- PLACEMENT: test/quests/_conformance.lua PLAN, directly AFTER
        -- step("npc.state_text", ...) (the raid seam1 block that spawned
        -- STATE_NPC "man" and hit it with a Wind Strike, so `state_slot` is
        -- his CLIENT slot and he has a fight).  Two verbs: world.los and
        -- npc.pack (verb count +2, no seam row).  Waves seam pass 2,
        -- los_and_pack; proved on ordinary ground in
        -- build/quest_gate/los_scratch_c (25/25 PASS) and in the Inferno in
        -- build/quest_gate/los_scratch_d.  Both need a binary with
        -- api_drive.server_los / server_npc_pack (in the shared test client
        -- since the seam pass's closer rebuilt it).
        --
        -- world.los is graded on two FIXED Lumbridge tile pairs whose
        -- collision the cache decides, not on where the harness stands:
        -- 3200,3233 -> 3201,3233 straddle a wall (flags 0x180c | 0x10080,
        -- WALL_EAST(_PROJ) / WALL_WEST(_PROJ)) and must read false;
        -- 3203,3233 -> 3209,3233 is open grass and must read true.

        step("world.los", function()
            local los = verb("world", "los")
            if not los then return missing("world", "los") end
            local r, d, seen, reading = los({ x = 3200, z = 3233 }, { x = 3201, z = 3233 })
            if r ~= "ok" then return r, describe(d) end
            if not reading.in_scene then
                return "no_subject", "the wall pair is outside the built scene: " .. describe(d)
            end
            if seen ~= false or reading.line_of_sight ~= false then
                return "refused", "a wall pair read as seen: " .. describe(d)
            end
            local r2, d2, seen2 = los({ x = 3203, z = 3233 }, { x = 3209, z = 3233 })
            if r2 ~= "ok" then return r2, describe(d2) end
            if seen2 ~= true then
                return "refused", "an open pair read as blocked: " .. describe(d2)
            end
            local r3, d3 = los({ x = 3203, z = 3233 }, { x = 3209, z = 3233 }, { routine = "nosuch" })
            if r3 ~= "refused" then
                return "refused", "an unknown routine was not refused: " .. describe(r3) .. " " .. describe(d3)
            end
            return "ok", describe(d) .. " | " .. describe(d2)
        end)

        -- t.npc.pack(radius): the man is in it, by his client slot, with his
        -- record's size, a target text, a server tick, and a sees_player that
        -- agrees with world.los("player", row) asked the same tick.
        step("npc.pack", function()
            local pack = verb("npc", "pack")
            local los = verb("world", "los")
            if not pack then return missing("npc", "pack") end
            if not los then return missing("world", "los") end
            if state_slot == nil then return "no_subject", "world.projectiles chose no man" end
            local r, d, rows, raw = pack(15)
            if r ~= "ok" then return r, describe(d) end
            local man = nil
            for i = 1, #rows do
                if rows[i].client_slot == state_slot then man = rows[i] end
            end
            if man == nil then
                return "hollow", "client slot " .. tostring(state_slot) .. " is not in the pack: " .. describe(d)
            end
            if man.symbol ~= STATE_NPC or man.size ~= 1 or type(man.sees_player) ~= "boolean"
                or type(man.target_text) ~= "string" or type(raw.tick) ~= "number" then
                return "refused", "the man's pack row is incomplete: " .. describe(d)
            end
            local lr, ld, lseen = los("player", man)
            if lr ~= "ok" or lseen ~= man.sees_player then
                return "refused", "world.los(player, man) " .. describe(lr) .. " " .. tostring(lseen)
                    .. " disagrees with sees_player " .. tostring(man.sees_player) .. ": " .. describe(ld)
            end
            return "ok", string.format("slot %d (client %d) %s target %s sees=%s gap %d | %s", man.slot,
                man.client_slot, tostring(man.symbol), man.target_text, tostring(man.sees_player),
                man.gap_player, describe(ld))
        end)

        -- ONE EDGE PER SWING, AT THE CACHE ATTACKRATE.  The first gap may carry
        -- a step (npcst_after4: 5 then 4s); the three after it must be exactly
        -- the man's attackrate, and npc.state's seq_tick must equal the last.
        step("npc.await_anim", function()
            local await_anim = verb("npc", "await_anim")
            if not await_anim then return missing("npc", "await_anim") end
            if state_slot == nil then return "no_subject", "world.projectiles chose no man" end
            -- The subject's fight, made two-sided: in the full harness the man
            -- the Wind Strike hit never retaliated (facing -1, no seq for 12
            -- ticks, three runs in a row), so the player punches him too --
            -- Attack/Strength 1 from the stage above, which barely scratches
            -- him -- and his swings are the retaliation to that.
            local attack = verb("player", "attack")
            local engaged = "not pressed"
            if attack then
                local attack_result, attack_detail = attack(STATE_NPC, 2, 8, { slot = state_slot })
                engaged = describe(attack_result) .. " " .. string.sub(describe(attack_detail), 1, 80)
            end
            local ticks = {}
            for i = 1, 5 do
                local r, d, tick = await_anim({ slot = state_slot }, STATE_SEQ_ATTACK, STATE_ATTACKRATE * 3)
                if r ~= "ok" then
                    return r, "swing " .. i .. ": " .. describe(d) .. " (seen " .. table.concat(ticks, ",")
                        .. "; player.attack " .. engaged .. ")"
                end
                ticks[#ticks + 1] = tick
            end
            local gaps = {}
            for i = 2, #ticks do
                gaps[#gaps + 1] = ticks[i] - ticks[i - 1]
            end
            local text = "seq " .. STATE_SEQ_ATTACK .. " on ticks " .. table.concat(ticks, ",") .. ", gaps "
                .. table.concat(gaps, ",") .. " (attackrate " .. STATE_ATTACKRATE .. ")"
            for i = 2, #gaps do
                if gaps[i] ~= STATE_ATTACKRATE then
                    return "hollow", "a gap after the first is not the attackrate -- " .. text
                end
            end
            local r, row = t.npc.state({ slot = state_slot })
            if r ~= "ok" or row.seq_id ~= STATE_SEQ_ATTACK or row.seq_tick ~= ticks[#ticks] then
                return "hollow", "npc.state disagrees with the last edge: "
                    .. (r == "ok" and t.npc.state_text(row) or describe(row)) .. " -- " .. text
            end
            return "ok", text .. "; " .. t.npc.state_text(row) .. " [player.attack " .. engaged .. "]"
        end)

        -- ===== waves seam pass 7: npc_record_reads (three verbs, no seam row) =====
        -- On the spawned Lumbridge man (STATE_NPC), whose attack seq
        -- STATE_SEQ_ATTACK 422 npc.await_anim just watched him play.  Needs a client built with
        -- src/plugin/torirs_plugin_drive_record.c (api.drive.npc_record /
        -- seq_length / npc_pose); on an older binary each verb answers
        -- `unsupported` naming the missing api function.
        -- Proved in a scratch harness (build/quest_gate/nrr_conform1) and on a
        -- goblin and wave 1's Jal-MejRah (build/quest_gate/nrr_scratch2, 31/31).

        -- THE RECORD, FROM BOTH SIDES: the man's cache record as the client
        -- resolved it (configs/all.npc:80725: name Man, model1 215) and the
        -- server's content block combat rolls with (attackrate 4, the cadence
        -- npc.await_anim just measured), on the live copy by slot.
        step("npc.record", function()
            local record = verb("npc", "record")
            if not record then return missing("npc", "record") end
            if state_slot == nil then return "no_subject", "world.projectiles chose no man" end
            local r, d, rec = record({ slot = state_slot })
            if r ~= "ok" then return r, describe(d) end
            if rec.slot ~= state_slot or rec.client.name ~= "Man" or rec.client.models[1] ~= 215
                or not rec.server.authored or rec.server.attackrate ~= STATE_ATTACKRATE
                or rec.server.attack_anim ~= STATE_SEQ_ATTACK then
                return "refused", "the man's record disagrees with all.npc:80725 / attackrate "
                    .. STATE_ATTACKRATE .. " / attack seq " .. STATE_SEQ_ATTACK .. ": " .. describe(d)
            end
            return "ok", d
        end)

        -- THE MOVEMENT TRACK npc.state's anim_id does not report: the ready or
        -- walk seq the client is stepping, named against the movement set the
        -- entity was given from the record npc.record just read.
        step("npc.pose", function()
            local pose_verb = verb("npc", "pose")
            if not pose_verb then return missing("npc", "pose") end
            if state_slot == nil then return "no_subject", "world.projectiles chose no man" end
            local _, _, rec = t.npc.record({ slot = state_slot })
            local text = "none"
            local waited = t.await({
                level = function()
                    local r, d, pose = pose_verb({ slot = state_slot })
                    text = describe(d)
                    return r == "ok" and pose.pose_seq >= 0
                        and (pose.pose_kind == "ready" or pose.pose_kind == "walk"
                            or pose.pose_kind == "ready_or_walk")
                        and rec ~= nil and pose.readyanim == rec.client.readyanim
                        and pose.walkanim == rec.client.walkanim
                end,
                note = "the man's movement track on his ready or walk seq",
            }, 8)
            if waited ~= "ok" then
                return "timeout", "no ready/walk movement track on slot " .. tostring(state_slot) .. ": " .. text
            end
            return "ok", text
        end)

        -- THE LENGTH THE CLIENT PLAYS: STATE_SEQ_ATTACK (human_unarmedpunch,
        -- configs/all.seq:5867: 5 frames, 38 client cycles), resolved because
        -- npc.await_anim watched him play it; by id and by symbol, one answer.
        step("seq.length", function()
            local length = verb("seq", "length")
            if not length then return missing("seq", "length") end
            local r, d, len = length(STATE_SEQ_ATTACK)
            if r ~= "ok" then return r, describe(d) end
            local r2, d2, len2 = length("human_unarmedpunch")
            if r2 ~= "ok" or len2.seq_id ~= STATE_SEQ_ATTACK then
                return "refused", "seq symbol human_unarmedpunch answered " .. describe(r2) .. ": " .. describe(d2)
            end
            local sum = 0
            for i = 1, #len.lengths do sum = sum + len.lengths[i] end
            if len.frames ~= 5 or len.cycles ~= 38 or sum ~= len.cycles or len.ticks ~= len.cycles / 30 then
                return "refused", "seq " .. STATE_SEQ_ATTACK .. " disagrees with all.seq:5867 (5 frames, 38 cycles): " .. describe(d)
            end
            return "ok", d
        end)
        -- ===== end npc_record_reads =====

        stage(function()
            setup_cheat("::kill " .. STATE_NPC)                    -- setup
            if state_levels.attack then
                setup_cheat("::setlevel attack " .. state_levels.attack)       -- setup
            end
            if state_levels.strength then
                setup_cheat("::setlevel strength " .. state_levels.strength)   -- setup
            end
            settle(12)   -- the single-way claim lapses before the next row
        end)

        -- RAID SEAM3 npc_facing_read: which square an npc was turned to, read two
        -- ways -- the client's t.npc.state face_x/face_z/face_tick and the server's
        -- npc_face tick-log row -- on Hans (every ~chatnpc page runs
        -- npc_facesquare(coord), interface_chat/scripts/chat.rs2:192); never a boss.
        -- Proved first by build/quest_gate/face_after4 (21/21) and face_conf1.
        stage(function()
            setup_cheat("::tele lumbridge")                        -- setup
            settle(3)
        end)

        local face_hans_slot = nil
        local face_hans_tile = nil

        -- WHICH SQUARE THE NPC WAS TURNED TO, AND ON WHICH TICK: t.npc.state's
        -- face_x/face_z/face_tick (the newest FACE_COORD op, kept past the
        -- turn that consumes the entity's pending square) and the server's
        -- own npc_face tick-log row for the same turn.  Graded on both naming
        -- the player's tile, the square npc_facesquare(coord) was given.
        seam("seam.npc_facing_read", function()
            local state = verb("npc", "state")
            local talk_to = verb("player", "talk_to")
            local walk_near = verb("player", "walk_near")
            local by_symbol = verb("player", "by_symbol")
            if not state then return missing("npc", "state") end
            if not (talk_to and walk_near and by_symbol) then
                return "no_subject", "player.talk_to/walk_near/by_symbol missing"
            end
            local target = by_symbol("npc", "hans")
            if target == nil then return "no_subject", "no hans to talk to" end
            walk_near(target)
            local sr, hans = state("hans")
            if sr ~= "ok" then return "no_subject", "npc.state hans -> " .. describe(hans) end
            if hans.face_x == nil then
                return "missing", "npc.state rows carry no face_x (binary before npc_facing_read)"
            end
            face_hans_slot = hans.slot
            local wr, wslot = t.ticklog.slot(hans)
            if wr ~= "ok" then return "no_subject", "ticklog.slot hans -> " .. describe(wslot) end
            local tr, td = talk_to("hans")
            if tr ~= "ok" then return "no_subject", "talk_to hans -> " .. describe(td) end
            local _, tile = t.world.tile()
            face_hans_tile = tile
            local text, seen = "none", nil
            local waited = t.await({
                level = function()
                    local r, row = state({ slot = face_hans_slot })
                    if r ~= "ok" then
                        text = describe(row)
                        return false
                    end
                    text = t.npc.state_text(row)
                    seen = row
                    return row.face_x == tile.x and row.face_z == tile.z and row.face_tick >= 0
                end,
                note = "hans face square = the player's tile",
            }, 4)
            if waited ~= "ok" then
                return "timeout", string.format("player at %d,%d; %s", tile.x, tile.z, text)
            end
            local rr, rows = t.ticklog.rows({ kind = "npc_face", slot = wslot })
            if rr ~= "ok" then return rr, "ticklog.rows npc_face -> " .. describe(rows) end
            local logged = nil
            for i = #rows, 1, -1 do
                if rows[i].x == tile.x and rows[i].z == tile.z then
                    logged = rows[i]
                    break
                end
            end
            if logged == nil then
                return "hollow", string.format("npc.state faced %d,%d but no npc_face row for world slot %s names it (%d row(s))",
                    seen.face_x, seen.face_z, tostring(wslot), #rows)
            end
            return "ok", string.format("player at %d,%d; %s; tick log: npc_face tick %d slot %d type %d -> %d,%d",
                tile.x, tile.z, text, logged.tick, logged.slot, logged.type, logged.x, logged.z)
        end)

        -- THE NEXT TURN, AS AN EDGE: Hans's own next page turns him again
        -- (a second facesquare to the same square), and npc.await_face
        -- answers its tick and tile.  The press that causes it is a verb that
        -- has already returned, so the state row read before it is passed as
        -- `since` (a turn newer than that reading also counts).
        step("npc.await_face", function()
            local await_face = verb("npc", "await_face")
            if not await_face then return missing("npc", "await_face") end
            if face_hans_slot == nil or face_hans_tile == nil then
                return "no_subject", "seam.npc_facing_read opened no dialogue with hans"
            end
            local drain = verb("chat", "drain")
            local choose = verb("chat", "choose")
            local continue_ = verb("chat", "continue_")
            if not (drain and choose and continue_) then
                return "no_subject", "chat.drain/choose/continue_ missing"
            end
            drain({ stop_at = "options" })
            local cr, cd = choose(1)
            if cr ~= "ok" then return "no_subject", "chat.choose 1 -> " .. describe(cd) end
            local br, before = t.npc.state({ slot = face_hans_slot })
            if br ~= "ok" then return "no_subject", "npc.state -> " .. describe(before) end
            continue_(true)
            local r, d, tick, x, z = await_face({ slot = face_hans_slot }, before, 5)
            drain({ stop_at = "none" })
            if r ~= "ok" then return r, describe(d) end
            if x ~= face_hans_tile.x or z ~= face_hans_tile.z or tick < before.face_tick then
                return "hollow", string.format("turned to %d,%d on tick %d, not the player's %d,%d after tick %d: %s",
                    x, z, tick, face_hans_tile.x, face_hans_tile.z, before.face_tick, describe(d))
            end
            return "ok", tostring(d)
        end)

        -- A GROUND OBJ ON A CENTREPIECE'S OWN TILE IS PRESSED WHERE IT IS DRAWN.
        -- The client lifts a stack on a raiseobject loc's tile onto the loc
        -- (App_WorldObjStackAdd: world_y = height - World_ObjRaiseGet; rscache
        -- defaults raiseobject to blocks_walk, so every table does it), and the
        -- driver's obj projector projected at the GROUND: the pixel was the
        -- table's body, the menu offered only the loc's rows, and click_obj got
        -- there only through five covered poses and the pixel hunt (30 ticks)
        -- -- or not at all: Legends' placed sapphire on the carved rock lasts 8
        -- ticks and was gone first (seam32 ground_obj_on_a_centrepiece_tile,
        -- build/quest_gate/s32_gem_before).  Graded on the pose-1 projection
        -- itself holding the stack in the pickset (the reading that separates
        -- the fix from the hunt that masks it), then on the Take landing.
        -- table4 at 3233,3208,0 is m50_50.jl2 "0 33 8: 604 10 3" (Lumbridge);
        -- 3231,3207 is inside the same room.
        stage(function()
            setup_cheat("::give sapphire 1")
            settle(2)
        end)
        seam("seam.click_obj_raised_stack", function()
            local goto_tile = verb("player", "goto_tile")
            local drop = verb("player", "drop")
            local by_symbol = verb("player", "by_symbol")
            local frame = verb("drive", "_frame")
            local probe = verb("drive", "_hover_probe")
            local click_obj = verb("player", "click_obj")
            local count = verb("inv", "count")
            if not goto_tile then return missing("player", "goto_tile") end
            if not drop then return missing("player", "drop") end
            if not by_symbol then return missing("player", "by_symbol") end
            if not frame then return missing("drive", "_frame") end
            if not probe then return missing("drive", "_hover_probe") end
            if not click_obj then return missing("player", "click_obj") end
            if not count then return missing("inv", "count") end
            goto_tile(3233, 3208, 0)
            local drop_result, drop_detail = drop("sapphire")
            if drop_result ~= "ok" then
                return "no_subject", "drop sapphire on table4's tile -> " .. describe(drop_result) .. " "
                    .. describe(drop_detail)
            end
            goto_tile(3231, 3207, 0)
            settle(2)
            local target, target_result = by_symbol("obj", "sapphire")
            if target_result ~= "ok" or type(target) ~= "table" then
                return "no_subject", "player.by_symbol(obj, sapphire) -> " .. describe(target_result)
            end
            local frame_result, pos = frame(target, 1, 4, true)
            if frame_result ~= "ok" or type(pos) ~= "table" then
                return "no_subject", "_frame pose 1 on the stack -> " .. describe(frame_result) .. " "
                    .. describe(pos)
            end
            local held = probe(pos.element_id, pos.x, pos.y, 4)
            local where = "projected " .. describe(pos.x) .. "," .. describe(pos.y)
                .. " holds the stack: " .. describe(held)
            if held ~= true then
                return "refused", where .. " -- the projection is not on the drawn (raised) stack"
            end
            local _, before = count("sapphire")
            local result, detail = click_obj("sapphire", 3)
            local _, after = count("sapphire")
            local text = where .. "; click_obj -> " .. describe(result) .. " " .. describe(detail)
                .. "; sapphire " .. describe(before) .. " -> " .. describe(after)
            if result ~= "ok" then
                return result, text
            end
            if after ~= (before or 0) + 1 then
                return "hollow", "click_obj answered ok but the backpack does not show the take -- " .. text
            end
            return "ok", text
        end)

        -- OBJ 0 IS AN ITEM TO THE DRIVER TOO.  seam32 moved the client's empty
        -- slot to -1 (INV_MANAGER_EMPTY_OBJ_ID), so api_drive.inv_slot answers
        -- obj 0 for Dwarf remains (mcannonremains, "0=mcannonremains" in
        -- all.obj.compack) -- but the driver's Lua still read `obj_id <= 0` as
        -- empty: t.inv.slot named the remains '' x0 and _inv_contents (the
        -- use_on / use_item_on_item diff) left them out (seam33
        -- obj_zero_in_the_driver_lua, build/quest_gate/s33_obj0_before).  Graded
        -- on both readers naming the remains, and on an empty slot still
        -- reading '' x0.  The remains are dropped afterwards to give the slot back.
        stage(function()
            setup_cheat("::give mcannonremains 1")
            settle(2)
        end)
        seam("seam.inv_slot_obj_zero", function()
            local slot = verb("inv", "slot")
            local count = verb("inv", "count")
            local contents = verb("player", "_inv_contents")
            local drop = verb("player", "drop")
            if not slot then return missing("inv", "slot") end
            if not count then return missing("inv", "count") end
            if not contents then return missing("player", "_inv_contents") end
            if not drop then return missing("player", "drop") end
            local held_result, held = count("mcannonremains")
            if held_result ~= "ok" or type(held) ~= "number" or held < 1 then
                return "no_subject", "::give mcannonremains left " .. describe(held) .. " in the backpack ("
                    .. describe(held_result) .. ")"
            end
            local remains_at, empty_at, empty_read = nil, nil, nil
            for index = 0, 27 do
                local r, cell = slot(index)
                if r == "ok" and type(cell) == "table" then
                    if remains_at == nil and cell.name == "mcannonremains" and cell.count == 1 then
                        remains_at = index
                    elseif empty_at == nil and cell.name == "" then
                        empty_at = index
                        empty_read = "'" .. tostring(cell.name) .. "' x" .. tostring(cell.count)
                    end
                end
            end
            local c_result, c = contents()
            local listed = (c_result == "ok" and type(c) == "table") and c.totals["mcannonremains"] or nil
            local text = "count " .. describe(held) .. "; t.inv.slot names it at "
                .. describe(remains_at) .. "; _inv_contents mcannonremains=" .. describe(listed)
                .. "; first empty slot " .. describe(empty_at) .. " reads " .. describe(empty_read)
            local drop_result, drop_detail = drop("mcannonremains")
            text = text .. "; drop -> " .. describe(drop_result) .. " " .. describe(drop_detail)
            if remains_at == nil then
                return "hollow", "t.inv.slot never named the obj-0 item -- " .. text
            end
            if listed ~= 1 then
                return "hollow", "_inv_contents left the obj-0 item out -- " .. text
            end
            if empty_at ~= nil and empty_read ~= "'' x0" then
                return "hollow", "an empty slot no longer reads '' x0 -- " .. text
            end
            return "ok", text
        end)

        -- A "*" ENTRY NAMES THE PAGE IT CONTINUED PAST, AND A PAGE FOLLOWED BY
        -- A p_delay IS WAITED OUT.  Professor Oddenstein's "Let's get this
        -- fixed then." is followed by mes / p_delay(2) / mes / p_delay(2) and
        -- no if_close (professor_oddenstein.rs2) before Ernest's page.  chat.play
        -- now awaits the ANSWER to each entry's click (a second 8-tick wait for
        -- a latch still up) and every "*" summarises the page it graded, so a
        -- reader can see which page each wildcard took (seam23,
        -- build/quest_gate/s23cr_odd1 4/4).  Hollow when the summary carries
        -- no page for the wildcard.
        stage(function()
            setup_cheat("::haunted")
            setup_cheat("::setvar varp32_haunted 2")
            setup_cheat("::give pressure_gauge")
            setup_cheat("::give oil_can")
            setup_cheat("::give rubber_tube")
            settle(2)
        end)
        seam("seam.chat_play_names_the_wildcard_page", function()
            local play = verb("chat", "play")
            local goto_tile = verb("player", "goto_tile")
            local talk_to = verb("player", "talk_to")
            if not play then return missing("chat", "play") end
            if not goto_tile then return missing("player", "goto_tile") end
            if not talk_to then return missing("player", "talk_to") end
            local goto_result, goto_detail = goto_tile(3110, 3367, 2)
            if goto_result ~= "ok" then
                return "no_subject", "goto Oddenstein's floor -> " .. describe(goto_result) .. " "
                    .. describe(goto_detail)
            end
            local talk_result, talk_detail = talk_to("professor_oddenstein", 1)
            if talk_result ~= "ok" then
                return "no_subject", "talk_to professor_oddenstein -> " .. describe(talk_result)
                    .. " " .. describe(talk_detail)
            end
            local result, detail = play({
                "npc:Have you found anything yet?",
                "player:I have everything!",
                "npc:Give 'em here then.",
                "*",
                "npc:It was dreadfully irritating being a chicken. How can I ever thank you?",
                "player:Well a cash reward is always nice...",
                "npc:Of course, of course.",
            })
            local text = "chat.play over the hand-in -> " .. describe(result) .. " " .. describe(detail)
            if result ~= "ok" then
                return result, text
            end
            if not string.find(tostring(detail), "*=npc 'Let's get this fixed", 1, true) then
                return "hollow", "the wildcard's summary does not name the page it continued past -- "
                    .. text
            end
            return "ok", text
        end)

        -- A CAST ON A LOC.  The same world target, the loc half: Charge Air
        -- Orb on the Obelisk of Water is refused in content's own sentence
        -- (charge_orb.rs2), then Charge Water Orb charges the orb and pays
        -- Magic XP (seam23, build/quest_gate/s23cw_rows3).  Graded on the
        -- sentence, the orb swap and the XP.
        stage(function()
            setup_cheat("::give stafforb 1")
            setup_cheat("::give waterrune 30")
            setup_cheat("::give cosmicrune 6")
            setup_cheat("::give airrune 30")
            settle(2)
        end)
        seam("seam.cast_on_loc", function()
            local fn = verb("player", "cast")
            local goto_tile = verb("player", "goto_tile")
            local count = verb("inv", "count")
            if not fn then return missing("player", "cast") end
            if not goto_tile then return missing("player", "goto_tile") end
            if not count then return missing("inv", "count") end
            local goto_result, goto_detail = goto_tile(2843, 3420, 0)
            if goto_result ~= "ok" then
                return "no_subject", "goto the Obelisk of Water -> " .. describe(goto_result) .. " "
                    .. describe(goto_detail)
            end
            local _, orb_before = count("stafforb")
            local refused, refused_detail = fn("charge_air_orb", { kind = "loc", id = "obelisk_water" })
            local _, orb_mid = count("stafforb")
            local text = "charge_air_orb -> " .. describe(refused) .. " " .. describe(refused_detail)
            if refused ~= "refused" or orb_mid ~= orb_before
                or not string.find(tostring(refused_detail), "This spell needs to be cast on an air obelisk.", 1, true) then
                return "hollow", "the wrong obelisk was not refused in content's words -- " .. text
            end
            local _, water_before = count("water_orb")
            local result, detail = fn("charge_water_orb", { kind = "loc", id = "obelisk_water" })
            settle(1)
            local _, water_after = count("water_orb")
            local _, orb_after = count("stafforb")
            text = text .. " || charge_water_orb -> " .. describe(result) .. " " .. describe(detail)
                .. "; water_orb " .. describe(water_before) .. " -> " .. describe(water_after)
                .. ", stafforb " .. describe(orb_mid) .. " -> " .. describe(orb_after)
            if result ~= "ok" then
                return result, text
            end
            if water_after ~= (water_before or 0) + 1 or orb_after ~= (orb_mid or 0) - 1 then
                return "hollow", "cast answered ok but no orb was charged -- " .. text
            end
            return "ok", text
        end)

        -- ONE GAME OF RUNE-DRAW, played for real (seam24 rune_draw_strategy).
        -- Ghosts Ahoy's bow branch is stated by cheats -- the quest at
        -- gathering_items, Ak-Haranu's bow handed over -- and Robin is asked
        -- for a game.  game.runedraw plays it from his options page to the
        -- settlement, every Draw/Hold from the pages it read.  Graded against
        -- the server: a win must move ahoy_robin_debt 0 -> 25, a loss must
        -- cost the 25-coin stake (he owes nothing yet), a level game neither.
        stage(function()
            setup_cheat("::ghostsahoy")
            setup_cheat("::give oak_longbow 1")
            setup_cheat("::give coins 100")
            setup_cheat("::setvar varb217_ahoy_questvar 4")
            setup_cheat("::setvar varb212_ahoy_subquest_bow 1")
            settle(2)
        end)
        step("game.runedraw", function()
            local fn = verb("game", "runedraw")
            local goto_tile = verb("player", "goto_tile")
            local talk_to = verb("player", "talk_to")
            local count = verb("inv", "count")
            local read_content = t.quest and t.quest._read_content
            if not fn then return missing("game", "runedraw") end
            if not goto_tile then return missing("player", "goto_tile") end
            if not talk_to then return missing("player", "talk_to") end
            if not count then return missing("inv", "count") end
            if type(read_content) ~= "function" then return missing("quest", "_read_content") end
            local goto_result, goto_detail = goto_tile(3672, 3491, 0)
            if goto_result ~= "ok" then
                return "no_subject", "goto Robin's pub -> " .. describe(goto_result) .. " " .. describe(goto_detail)
            end
            local talk_result, talk_detail = talk_to("ahoy_robin", 1)
            if talk_result ~= "ok" then
                return "no_subject", "talk_to ahoy_robin -> " .. describe(talk_result) .. " " .. describe(talk_detail)
            end
            local _, coins_before = count("coins")
            local result, game = fn()
            if result ~= "ok" then
                return result, describe(game)
            end
            settle(1)
            local _, coins_after = count("coins")
            local debt_result, debt = read_content("varp7172_ahoy_robin_debt")
            local text = describe(game.text) .. "; ahoy_robin_debt " .. describe(debt_result) .. " "
                .. describe(debt) .. ", coins " .. describe(coins_before) .. " -> " .. describe(coins_after)
            local want_debt, want_coins = 0, coins_before
            if game.outcome == "won" then
                want_debt = 25
            elseif game.outcome == "lost" then
                want_coins = (coins_before or 0) - 25
            end
            if debt_result ~= "ok" or debt ~= want_debt or coins_after ~= want_coins or game.draws < 1 then
                return "hollow", "the settlement the verb read is not the server's -- " .. text
            end
            return "ok", text
        end)

        -- A LOC ON A LINK_BELOW BRIDGE-DECK COLUMN, pressed first try.  The
        -- Tourist Trap's clifftop climbs sit at cache level 1 over a bridged
        -- column; drive_pointer_screen_position_loc handed that cache level to
        -- World_HeightAt, which reads a WIRE level, so the hunt projected the
        -- loc one plane up (382,99, 45 of 54 pixels off the viewport top:
        -- build/quest_gate/parity_desertrescue_d4).  It projects at
        -- World_TerrainWalkLevel now (seam23 landed it, seam24 re-landed it
        -- beside the sail poses; s24b_rows_fix 8/8, desertrescue leg_d 10/10).
        -- Graded on where the player ends up, not on click_loc's word: the
        -- climbs are stateless oploc1s (quest_desertrescue.rs2:205-208).
        stage(function()
            setup_cheat("::setlevel agility 99")
            settle(2)
        end)
        seam("seam.bridge_deck_loc_press", function()
            local goto_tile = verb("player", "goto_tile")
            local click_loc = verb("player", "click_loc")
            local tile_of = verb("world", "tile")
            if not goto_tile then return missing("player", "goto_tile") end
            if not click_loc then return missing("player", "click_loc") end
            if not tile_of then return missing("world", "tile") end
            local goto_result, goto_detail = goto_tile(3280, 3037, 0)
            if goto_result ~= "ok" then
                return "no_subject", "goto the clifftop foot -> " .. describe(goto_result) .. " "
                    .. describe(goto_detail)
            end
            settle(2)
            local function wait_x(test)
                for _ = 1, 8 do
                    local read, tile = tile_of()
                    if read == "ok" and is_table(tile) and test(tile.x) then
                        return tile
                    end
                    settle(1)
                end
                return nil
            end
            local up_result, up_detail = click_loc("tourtrap_qip_clifftop_climbup", 1)
            local text = "climbup -> " .. describe(up_result) .. " " .. describe(up_detail)
            if up_result ~= "ok" then
                return up_result, text
            end
            local top = wait_x(function(x) return x >= 3273 and x <= 3278 end)
            if not top then
                return "hollow", "climbup answered ok but the player never reached the second cliff -- " .. text
            end
            local down_result, down_detail = click_loc("tourtrap_qip_clifftop_climbdown", 1)
            text = text .. "; on the cliff at " .. top.x .. "," .. top.z .. "; climbdown -> "
                .. describe(down_result) .. " " .. describe(down_detail)
            if down_result ~= "ok" then
                return down_result, text
            end
            local below = wait_x(function(x) return x < 3273 end)
            if not below then
                return "hollow", "climbdown answered ok but the player is still on the cliff -- " .. text
            end
            return "ok", text .. "; below at " .. below.x .. "," .. below.z
        end)

        stage(function()
            setup_cheat("::tele lumbridge")
            settle(4)
        end)

        -- SEAM ghostsahoy_harbour_door_and_lobster (seam26): click_loc's
        -- `{ at = {x, z[, level]} }` LOC SELECTOR (pointer.lua
        -- QD.player._loc_copy).  Without it a symbol presses the copy nearest
        -- the player, and on Ghosts Ahoy's rock route that is the rock
        -- underfoot: eleven `ok` jumps that never left 3604,3550
        -- (build/seam_state/seam25/wreck, run s25wk_door1 rows 6-16).  The
        -- subject is Lumbridge's `tree`, planted 58 times around the castle:
        -- the row names a copy that is NOT the nearest (the second pool row,
        -- the pool comes nearest-first), presses Chop down on it, and the
        -- player must end beside THAT copy (a tree's footprint is 2x2, so
        -- within 2 of its SW tile) and nearer it than the nearest copy.  Then
        -- a tile with no copy must answer `no_row` and press nothing.
        -- Measured on the scratch twin (build/quest_gate/s26gh_sel1):
        -- `pressed the copy at 3196,3214,0`, player 3197,3216, nearest copy
        -- 3217,3241; the no-copy tile `no_row ... nothing pressed`.
        seam("seam.click_loc_names_the_copy", function()
            local click_loc = verb("player", "click_loc")
            local by_symbol = verb("player", "by_symbol")
            local pool_read = verb("drive", "_pool_read")
            local tile_of = verb("world", "tile")
            if not click_loc then return missing("player", "click_loc") end
            if not by_symbol then return missing("player", "by_symbol") end
            if not pool_read then return missing("drive", "_pool_read") end
            if not tile_of then return missing("world", "tile") end
            local tree, tree_result = by_symbol("loc", "tree")
            if tree_result ~= "ok" or not is_table(tree) then
                return "no_subject", "player.by_symbol(loc, tree) -> " .. describe(tree_result)
            end
            local here_result, here = tile_of()
            if here_result ~= "ok" or not is_table(here) then
                return "no_subject", "world.tile -> " .. describe(here_result)
            end
            local rows_result, rows = pool_read("locs", 0, 18)
            if rows_result ~= "ok" or not is_table(rows) then
                return "no_subject", "the loc pool -> " .. describe(rows_result)
            end
            local copies = {}
            for index = 1, #rows do
                local row = rows[index]
                if (row.loc_id == tree.id or row.resolved_loc_id == tree.id)
                    and row.level == here.level then
                    copies[#copies + 1] = row
                end
            end
            if #copies < 2 then
                return "no_subject", #copies .. " copy/copies of tree on this floor from "
                    .. here.x .. "," .. here.z .. " -- the row needs two"
            end
            local nearest, named = copies[1], copies[2]
            local result, detail = click_loc("tree", 1, { at = { named.x, named.z } })
            local text = "named " .. named.x .. "," .. named.z .. " (nearest " .. nearest.x .. ","
                .. nearest.z .. ") -> " .. describe(result) .. " " .. describe(detail)
            if result ~= "ok" then
                return result, text
            end
            settle(2)
            local after_result, after = tile_of()
            if after_result ~= "ok" or not is_table(after) then
                return "hollow", "no tile after the press -- " .. text
            end
            local to_named = math.max(math.abs(after.x - named.x), math.abs(after.z - named.z))
            local to_nearest = math.max(math.abs(after.x - nearest.x), math.abs(after.z - nearest.z))
            text = text .. "; player " .. after.x .. "," .. after.z .. ", " .. to_named
                .. " from the named copy, " .. to_nearest .. " from the nearest"
            if to_named > 2 or to_nearest <= to_named then
                return "refused", "the press did not land on the named copy -- " .. text
            end
            local none_result, none_detail = click_loc("tree", 1, { at = { here.x + 200, here.z + 200 } })
            if none_result ~= "no_row" then
                return "refused", "a tile with no copy answered " .. describe(none_result) .. " "
                    .. describe(none_detail) .. " -- a selector must never fall back; " .. text
            end
            return "ok", text .. "; no copy at " .. (here.x + 200) .. "," .. (here.z + 200)
                .. " -> no_row"
        end)

        stage(function()
            setup_cheat("::tele lumbridge")
            settle(4)
        end)

        -- SEAM towerladder_press (seam26): A LOC ONLY ON A LOWER FLOOR IS NO
        -- TARGET (pointer.lua QD.player._loc_other_floor).  The Watchtower's
        -- `towerladder` (2833) is the GROUND floor's Climb-up (maps/m39_48.jl2
        -- `0 48 39: 2833 10`); from the first floor (2544,3111,1) the client
        -- pick can never keep it, and click_loc used to hunt 119 ticks and
        -- answer "menu has no row for it" (build/quest_gate/s26tl_before row
        -- 2).  It must answer not_visible "other_floor", naming the loc that
        -- IS on this floor there, qip_watchtower_ladder_top (17122, `1 48 39:
        -- 17122 10`) -- measured s26tl_p2a row 2, 28 ticks.
        stage(function()
            setup_cheat("::goto 2544 3111 1")                  -- setup
            settle(6)
        end)

        seam("seam.click_loc_names_the_other_floor", function()
            local click_loc = verb("player", "click_loc")
            if not click_loc then return missing("player", "click_loc") end
            local result, detail = click_loc("towerladder", 1)
            local text = "click_loc(towerladder) from 2544,3111,1 -> " .. describe(result) .. " "
                .. describe(detail)
            if result ~= "not_visible" then
                return "refused", "a loc with no copy on this floor must answer not_visible -- " .. text
            end
            if not string.find(tostring(detail), "other_floor", 1, true)
                or not string.find(tostring(detail), "qip_watchtower_ladder_top", 1, true) then
                return "refused", "the answer must say other_floor and name the loc on this floor -- "
                    .. text
            end
            -- vm-b1-seam1 driver_other_floor_loc: the detail also says the
            -- game's client cannot press it either and names the level to
            -- reach (towerladder's copy is on level 0), so a triage does not
            -- file an other_floor answer as a driver seam.
            if not string.find(tostring(detail), "reach level 0 by the guide's route first", 1, true) then
                return "refused", "the answer must name the level to reach by the guide's route -- " .. text
            end
            return "ok", text
        end)

        -- A CAST ON A CARRIED ITEM.  No verb could cast on a backpack item, so
        -- Superheat Item / the alchemies / the enchants could not be driven
        -- (seam27).  t.player.cast now takes {kind="held", id=<obj symbol>}:
        -- spell_arm, then api_drive.inv_cast runs the cell's TGT_HELD row
        -- (OPHELDT).  Graded on the backpack, not the verb's word
        -- (build/quest_gate/s27held_b 7/7).
        stage(function()
            setup_cheat("::goto " .. CAST_TILE_X .. " " .. CAST_TILE_Z .. " 0")  -- setup
            setup_cheat("::setlevel magic 99")
            setup_cheat("::give firerune 3")
            setup_cheat("::give naturerune 1")
            setup_cheat("::give bronze_dagger 1")
            settle(2)
        end)
        seam("seam.cast_on_held_item", function()
            local fn = verb("player", "cast")
            local count = verb("inv", "count")
            if not fn then return missing("player", "cast") end
            if not count then return missing("inv", "count") end
            local _, dagger_before = count("bronze_dagger")
            local _, coins_before = count("coins")
            local _, nature_before = count("naturerune")
            local result, detail = fn("low_alchemy", { kind = "held", id = "bronze_dagger" })
            settle(1)
            local _, dagger_after = count("bronze_dagger")
            local _, coins_after = count("coins")
            local _, nature_after = count("naturerune")
            local text = "low_alchemy held bronze_dagger -> " .. describe(result) .. " "
                .. describe(detail) .. "; dagger " .. describe(dagger_before) .. " -> "
                .. describe(dagger_after) .. ", coins " .. describe(coins_before) .. " -> "
                .. describe(coins_after) .. ", nature " .. describe(nature_before) .. " -> "
                .. describe(nature_after)
            if result ~= "ok" then
                return result, text
            end
            if dagger_after ~= (dagger_before or 0) - 1 or (coins_after or 0) <= (coins_before or 0)
                or nature_after ~= (nature_before or 0) - 1 then
                return "hollow", "cast answered ok but the backpack does not show the alchemy -- " .. text
            end
            return "ok", text
        end)

        -- A CAST FIGHT RE-CASTS UNDER RETALIATION, AND AWAIT_DEAD EATS.
        -- With auto-retaliate on (this fixture's default, and Family Crest's)
        -- the player's melee retaliation puts 0 splats on Chronozon (defence
        -- 173) between casts; counting a new splat as progress kept the
        -- stall counter from ever reaching the re-cast (0 re-casts in 60
        -- ticks, build/quest_gate/s27re_before3).  A cast fight now stalls on
        -- the health reading alone.  And opts.eat = {item=, below=} eats
        -- through inv_op whenever the stated hitpoints fall under `below`
        -- (seam27; Family Crest's author had hand-written a cast-and-eat
        -- loop): Chronozon (strength 172) takes ~80 of 99 hitpoints in 60
        -- ticks, so this row DIES without it -- the first draft of this row,
        -- with no food, did (build/quest_gate/_conformance attempt-01 row
        -- 167).  Graded on >= 3 re-casts, >= 4 air runes spent and a shark
        -- eaten (s27re_after2: 9 re-casts, 6 sharks).  Chronozon regenerates
        -- without his four blast bits, so the row never grades a kill, and
        -- `::passive` breaks his fight off afterwards (he is left standing,
        -- harmless, for the rows after).
        stage(function()
            setup_cheat("::clearinv")                           -- setup
            setup_cheat("::setlevel magic 99")
            setup_cheat("::setlevel hitpoints 99")
            setup_cheat("::setlevel defence 99")
            setup_cheat("::give airrune 100")
            setup_cheat("::give mindrune 100")
            setup_cheat("::give shark 20")
            setup_cheat("::goto 3222 3219 0")                   -- setup
            settle(2)
            setup_cheat("::spawn chronozon")                    -- setup
            settle(2)
        end)
        seam("seam.await_dead_engaged_recasts_and_eats", function()
            local cast = verb("player", "cast")
            local fn = verb("npc", "await_dead_engaged")
            local count = verb("inv", "count")
            if not cast then return missing("player", "cast") end
            if not fn then return missing("npc", "await_dead_engaged") end
            if not count then return missing("inv", "count") end
            local _, air_before = count("airrune")
            local _, sharks_before = count("shark")
            local cast_result, cast_detail = cast("wind_strike", "chronozon", 14)
            if cast_result ~= "ok" then
                return "no_subject", "cast wind_strike on chronozon -> " .. describe(cast_result) .. " "
                    .. describe(cast_detail)
            end
            local result, detail = fn(60, 30, { eat = { item = "shark", below = 70 } })
            local _, air_after = count("airrune")
            local _, sharks_after = count("shark")
            local recasts = tonumber(string.match(tostring(detail),
                "re%-CAST wind_strike: (%d+) press%(es%) ok")) or 0
            local text = "await_dead_engaged(60, 30, {eat shark below 70}) after wind_strike -> "
                .. describe(result) .. " " .. describe(detail) .. "; re-casts ok " .. recasts
                .. ", airrune " .. describe(air_before) .. " -> " .. describe(air_after)
                .. ", shark " .. describe(sharks_before) .. " -> " .. describe(sharks_after)
            if result == "refused" then
                return result, text
            end
            if recasts < 3 or (air_before or 0) - (air_after or 0) < 4 then
                return "refused", "a cast fight under retaliation must re-cast on the stall -- " .. text
            end
            if not string.find(tostring(detail), "ate shark", 1, true)
                or (sharks_after or 0) >= (sharks_before or 0) then
                return "refused", "hitpoints fell under 70 and no shark was eaten -- " .. text
            end
            return "ok", text
        end)
        stage(function()
            setup_cheat("::passive chronozon")                  -- setup
            settle(3)
        end)

        -- THE AWAIT VERBS ANSWER A DETAIL.  gate.py fails a PASS row with an
        -- empty detail (seam27), and t.await / npc.await_present /
        -- npc.await_gone / ui.await_open used to answer a bare ok.  Graded
        -- on the words: t.await says "<note>: met after N tick(s)",
        -- await_present "<sym> present within R: slot S at x,z" (Chronozon,
        -- left standing by the row above) and await_gone "no <sym> within R".
        seam("seam.await_verbs_answer_a_detail", function()
            local await_fn = verb("await")
            local present = verb("npc", "await_present")
            local gone = verb("npc", "await_gone")
            if not await_fn then return missing("await") end
            if not present then return missing("npc", "await_present") end
            if not gone then return missing("npc", "await_gone") end
            local await_result, await_detail = await_fn({
                level = function() return true end,
                note = "conformance.await_detail",
            }, 2)
            local present_result, present_detail = present("chronozon", 15, 5)
            local gone_result, gone_detail = gone("ahoy_ghost_guard", 15, 5)
            local text = "t.await -> " .. describe(await_result) .. " " .. describe(await_detail)
                .. "; npc.await_present(chronozon, 15) -> " .. describe(present_result) .. " "
                .. describe(present_detail) .. "; npc.await_gone(ahoy_ghost_guard, 15) -> "
                .. describe(gone_result) .. " " .. describe(gone_detail)
            if await_result ~= "ok" or present_result ~= "ok" or gone_result ~= "ok" then
                return "refused", text
            end
            if not string.find(tostring(await_detail), "conformance.await_detail: met after", 1, true)
                or not string.find(tostring(present_detail), "chronozon present within 15: slot", 1, true)
                or not string.find(tostring(gone_detail), "no ahoy_ghost_guard within 15", 1, true) then
                return "hollow", "an await answered ok without its detail -- " .. text
            end
            return "ok", text
        end)

        -- A SPELL WITH NO TARGET (seam28 self_cast_and_moving_multinpc_press).
        -- t.player.cast(spell) with no target presses the spellbook cell's
        -- own op 1 (IF_BUTTON1 -> [if_button,magic_spellbook:<spell>]); before
        -- seam28 it died in by_symbol("npc", nil) (build/quest_gate/
        -- s28sc_before row 2).  Graded on the TILE: Varrock Teleport from
        -- Lumbridge must land in Varrock and say TELEPORTED; Ardougne Teleport
        -- with %elenaquest unset must answer refused quoting the gate
        -- (skill_magic/scripts/spells/teleport.rs2:16-23, LostCity
        -- teleport.rs2) and leave the player where he stood.
        stage(function()
            setup_cheat("::give lawrune 5")                     -- setup
            setup_cheat("::give firerune 5")
            setup_cheat("::goto 3222 3219 0")
            settle(3)
        end)
        seam("seam.cast_self_teleport", function()
            local cast = verb("player", "cast")
            local tile = verb("world", "tile")
            if not cast then return missing("player", "cast") end
            if not tile then return missing("world", "tile") end
            local result, detail = cast("varrock_teleport")
            local _, at = tile()
            local text = "cast varrock_teleport (no target) -> " .. describe(result) .. " "
                .. describe(detail) .. "; tile " .. (at and (at.x .. "," .. at.z .. "," .. at.level) or "?")
            if result ~= "ok" then
                return result, text
            end
            if not at or at.x < 3200 or at.x > 3230 or at.z < 3415 or at.z > 3440
                or not string.find(tostring(detail), "TELEPORTED", 1, true) then
                return "hollow", "the self-cast answered ok but the player is not in Varrock -- " .. text
            end
            return "ok", text
        end)
        seam("seam.cast_self_refused", function()
            local cast = verb("player", "cast")
            local tile = verb("world", "tile")
            if not cast then return missing("player", "cast") end
            if not tile then return missing("world", "tile") end
            local _, before = tile()
            local result, detail = cast("ardougne_teleport")
            local _, after = tile()
            local text = "cast ardougne_teleport before Plague City -> " .. describe(result) .. " "
                .. describe(detail) .. "; tile " .. (before and (before.x .. "," .. before.z) or "?")
                .. " -> " .. (after and (after.x .. "," .. after.z) or "?")
            if result ~= "refused" then
                return "refused", "the Plague City gate must refuse the cast -- " .. text
            end
            if not string.find(tostring(detail), "You must have completed Plague City to use this spell.", 1, true)
                or not before or not after or before.x ~= after.x or before.z ~= after.z then
                return "hollow", "refused without the gate's sentence, or the player moved -- " .. text
            end
            return "ok", text
        end)

        -- A TELEPORT OUT OF A DUNGEON, PRESSED WHILE A CHOP HOLDS THE PLAYER
        -- (seam33 spellbook_cast_never_runs).  Lost City's teleportAway: the
        -- Entrana dungeon floor, the Dramen tree's branch cut (leprechaun_tree
        -- .rs2 [oploc1,dramentree]: inv_add then p_delay(1)), and Lumbridge
        -- Teleport pressed the tick the branch lands.  The server refuses an
        -- [if_button] while the player is delayed, silently (LostCity
        -- Player.runScript; torirs_server_world.c
        -- if_button_refused_while_delayed), and the one-press verb read "the
        -- cast never ran" (build/quest_gate/zanaris row 35, s33cast_repro2
        -- row 4).  Graded on the TILE: the player must leave the dungeon for
        -- Lumbridge and the verb must say TELEPORTED.  The zanaris varp goes
        -- back to 0 after.
        stage(function()
            setup_cheat("::give bronze_axe 1")                  -- setup
            setup_cheat("::give lawrune 1")
            setup_cheat("::give airrune 3")
            setup_cheat("::give earthrune 1")
            setup_cheat("::setlevel woodcutting 36")
            setup_cheat("::setvar varp147_zanaris ^zanaris_spirit_defeated")
            setup_cheat("::goto 2862 9733 0")
            settle(3)
        end)
        seam("seam.cast_self_teleport_from_dungeon_after_delay", function()
            local cast = verb("player", "cast")
            local tile = verb("world", "tile")
            local click_loc = verb("player", "click_loc")
            local inv_await = verb("inv", "await")
            if not cast then return missing("player", "cast") end
            if not tile then return missing("world", "tile") end
            if not click_loc then return missing("player", "click_loc") end
            if not inv_await then return missing("inv", "await") end
            local _, before = tile()
            local chop_result, chop_detail = click_loc("dramentree", 1)
            local branch_result, branch_detail = inv_await("dramen_branch", 1, 15)
            local text = "chop dramentree -> " .. describe(chop_result) .. " " .. describe(chop_detail)
                .. "; branch " .. describe(branch_result) .. " " .. describe(branch_detail)
            if branch_result ~= "ok" then
                return "no_subject", "no branch was cut, so no p_delay to cast through -- " .. text
            end
            local result, detail = cast("lumbridge_teleport")
            local _, at = tile()
            text = text .. "; cast lumbridge_teleport (no target) from "
                .. (before and (before.x .. "," .. before.z .. "," .. before.level) or "?")
                .. " -> " .. describe(result) .. " " .. describe(detail) .. "; tile "
                .. (at and (at.x .. "," .. at.z .. "," .. at.level) or "?")
                .. "; presses " .. (string.match(tostring(detail), "%(press (%d+):") or "1")
            if result ~= "ok" then
                return result, text
            end
            if not before or before.z < 9000 then
                return "no_subject", "the cast did not start in the dungeon -- " .. text
            end
            if not at or at.level ~= 0 or at.x < 3200 or at.x > 3240 or at.z < 3200 or at.z > 3240
                or not string.find(tostring(detail), "TELEPORTED", 1, true) then
                return "hollow", "the self-cast answered ok but the player is not in Lumbridge -- " .. text
            end
            return "ok", text
        end)
        stage(function()
            setup_cheat("::setvar varp147_zanaris 0")                   -- teardown
            settle(1)
        end)

        -- A TRANSMOGGED PLAYER IS DRAWN AS THE NPC (seam34).  The 239
        -- appearance block's 0xffff entry carries an npc id; the client
        -- decoded it and then drew the player's own body anyway (Monkey
        -- Madness's greegree wearer stayed a human in a monkey's stance,
        -- build/quest_gate/s34tm_before2 transmog.monkey_drawn).  Reference:
        -- LostCity ClientPlayer.ts getTempModel2 / deob
        -- PlayerAppearance.getModel -- the npc type's model.  Graded on the
        -- pick set, i.e. on the triangles the frame drew for the player's
        -- element: the highest pixel above the player's projection that
        -- still holds it.  `::transmog` is the ladder twin of p_transmogrify
        -- (LostCity ape_atoll_dungeon.rs2:182 [debugproc,transmogrify]).
        stage(function()
            setup_cheat("::transmog off")                       -- setup
            setup_cheat("::goto 2604 3277 0")
            local camera = verb("drive", "camera")
            if camera then camera(0, 400, 350) end
            settle(3)
        end)
        seam("seam.transmog_draws_the_npc", function()
            local drive = t.drive
            if type(drive) ~= "table" or type(drive._projection) ~= "function" then
                return missing("drive", "_projection")
            end
            if type(drive._hover_probe) ~= "function" then
                return missing("drive", "_hover_probe")
            end
            local cheat = verb("cheat")
            if not cheat then return missing("cheat") end
            local function top_held()
                local result, pos = drive._projection({ kind = "player", id = -1 })
                if result ~= "ok" or pos == nil then
                    return nil, "projection " .. describe(result)
                end
                local top, misses = nil, 0
                for dy = 0, 180, 6 do
                    if drive._hover_probe(pos.element_id, pos.x, pos.y - dy, 4) == true then
                        top, misses = dy, 0
                    elseif top ~= nil then
                        misses = misses + 1
                        if misses >= 4 then break end
                    end
                end
                return top, "projection " .. pos.x .. "," .. pos.y .. " top held dy=" .. describe(top)
            end
            local human, human_text = top_held()
            local on_result = cheat("::transmog mm_transmogrification_normal_monkey")
            settle(3)
            local monkey, monkey_text = top_held()
            local off_result = cheat("::transmog off")
            settle(3)
            local back, back_text = top_held()
            local text = "human " .. human_text .. "; ::transmog mm_transmogrification_normal_monkey -> "
                .. describe(on_result) .. ", " .. monkey_text .. "; ::transmog off -> "
                .. describe(off_result) .. ", " .. back_text
            if on_result ~= "ok" or off_result ~= "ok" then
                return "refused", "the ladder did not answer -- " .. text
            end
            if human == nil or monkey == nil or back == nil then
                return "not_visible", "a silhouette was not read -- " .. text
            end
            if monkey * 2 > human then
                return "refused", "the transmogged player is still drawn at human height -- " .. text
            end
            if back * 2 <= human then
                return "refused", "::transmog off did not give the body back -- " .. text
            end
            return "ok", text
        end)
        stage(function()
            setup_cheat("::transmog off")                       -- teardown
            settle(1)
        end)

        -- AN UNDRAWN MULTINPC SHELL IS NOT A MISSED PRESS (seam28).  A
        -- multinpc table is indexed BY VALUE (all.npc multinpc1 is varbit
        -- value 0); at ^gobdip_grubfoot_hidden (3, catwalk_goblin's -1 rung)
        -- Grubfoot is not drawn, and talk_to must answer not_visible naming
        -- the shell -- not a pixel hunt's account (build/quest_gate/
        -- s28sm_proof1 gv.vis3.undrawn).  The varbit goes back to 0 after.
        stage(function()
            setup_cheat("::goto 2957 3510 0")                   -- setup
            settle(2)
            setup_cheat("::setvar varb13594_gobdip_grubfoot_vis ^gobdip_grubfoot_hidden")
            settle(3)
        end)
        seam("seam.talk_to_undrawn_multinpc", function()
            local fn = verb("player", "talk_to")
            if not fn then return missing("player", "talk_to") end
            local result, detail = fn("catwalk_goblin", 1)
            local text = "talk_to catwalk_goblin at gobdip_grubfoot_vis ^gobdip_grubfoot_hidden -> "
                .. describe(result) .. " " .. describe(detail)
            if result ~= "not_visible" then
                return "refused", "an undrawn shell must answer not_visible -- " .. text
            end
            if not string.find(tostring(detail), "resolved to NO child", 1, true) then
                return "hollow", "not_visible without naming the shell -- " .. text
            end
            return "ok", text
        end)
        stage(function()
            setup_cheat("::setvar varb13594_gobdip_grubfoot_vis 0")       -- setup
            settle(1)
        end)

        -- A PAGE BREAK READS AS A SPACE (seam30).  The server joins a
        -- dialogue page's hard rows with <br> (content ~chat_layout, seam29);
        -- _strip_tags used to delete it, so elena's mesbox read "You fall
        -- through......you land" and a chat.play fragment spanning the break
        -- could never match what the player sees.  The page text is the
        -- one build/quest_gate/seam29_pipe_break recorded, verbatim.
        seam("seam.strip_tags_br_is_a_space", function()
            local read = t.read
            local fn = type(read) == "table" and read._strip_tags or nil
            if type(fn) ~= "function" then return missing("read", "_strip_tags") end
            local raw = "You fall through...<br>...you land in the sewer.<br>Edmond follows you down the hole."
            local got = fn(raw)
            local want = "You fall through... ...you land in the sewer. Edmond follows you down the hole."
            local text = "_strip_tags(" .. raw .. ") -> '" .. tostring(got) .. "'"
            if got ~= want then
                return "refused", "expected '" .. want .. "' -- " .. text
            end
            local colour = fn("<col=0000ff>find the</col> <br> helmet")
            if colour ~= "find the helmet" then
                return "refused", "a break with spaces round it must read as ONE space -- got '"
                    .. tostring(colour) .. "'; " .. text
            end
            return "ok", text .. "; '<col=0000ff>find the</col> <br> helmet' -> '" .. colour .. "'"
        end)

        -- A REWARD LINE WITH A THOUSANDS SEPARATOR (seam30).  "10,500
        -- Magic XP" read as 500: "(%d+)" started after the comma.  No
        -- fixture quest awards a five-figure line, so the row hands the
        -- parse behind scroll.reward_xp the lines directly.
        seam("seam.reward_xp_thousands_separator", function()
            local scroll = t.scroll
            local fn = type(scroll) == "table" and scroll._parse_reward_xp or nil
            if type(fn) ~= "function" then return missing("scroll", "_parse_reward_xp") end
            local lines = { "1 Quest Point", "10,500 Magic XP", "1,234,567 Attack XP", "300 Cooking XP",
                "Amulet of accuracy" }
            local cases = { { "magic", 10500 }, { "attack", 1234567 }, { "cooking", 300 }, { "mining", nil } }
            local seen = {}
            for _, case in ipairs(cases) do
                local got = fn(lines, case[1])
                seen[#seen + 1] = case[1] .. "=" .. tostring(got)
                if got ~= case[2] then
                    return "refused", case[1] .. " read " .. tostring(got) .. ", expected "
                        .. tostring(case[2]) .. " -- " .. table.concat(seen, " ")
                end
            end
            return "ok", table.concat(seen, " ") .. " over {" .. table.concat(lines, " | ") .. "}"
        end)
        stage(function()
            setup_cheat("::setlevel hitpoints 10")
            setup_cheat("::setlevel magic 1")
            settle(2)
        end)

        stage(function()
            setup_cheat("::tele lumbridge")
            settle(4)
        end)

        -- THE LEG MARKER AND ITS CHECKPOINT (seam30 leg_checkpoints,
        -- docs/quest_authoring/relay.md "Checkpoints").  A relay file's
        -- `legs` table is driven by t.core_legs_drive: a `leg.<k>.<name>` row
        -- before each leg (tile, server-read stage, backpack), and after an
        -- all-PASS leg that has a successor, `::checkpoint k` -- the server's
        -- save serialiser in checkpoint mode -- whose reply is carried into
        -- the next leg's row.  Two tiny legs at the Lumbridge spawn, a quiet
        -- point, so the checkpoint must be WRITTEN.
        seam("seam.legs_marker_and_checkpoint", function()
            local drive = t.core_legs_drive
            if type(drive) ~= "function" then return missing("core_legs_drive") end
            local report = drive({ legs = {
                { name = "conf_a", run = function(tt)
                    tt.step("conformance.leg_a", "PASS", "leg a's own row")
                end },
                { name = "conf_b", run = function(tt) end },
            } }, { finish = false })
            local rows = type(report) == "table" and report.rows or {}
            local text = table.concat(rows, " || ")
            if #rows ~= 3 or not string.find(tostring(rows[1]), "leg.1.conf_a: tile=", 1, true)
                or not string.find(tostring(rows[2]), "leg.2.conf_b: tile=", 1, true)
                or not string.find(tostring(rows[3]), "leg.2.end: tile=", 1, true) then
                return "refused", "expected rows leg.1.conf_a, leg.2.conf_b and leg.2.end with a tile -- " .. text
            end
            if not string.find(tostring(rows[2]), "checkpoint 1 written: checkpoint 1 written at", 1, true) then
                return "refused", "the all-PASS leg 1 at a quiet point left no written checkpoint -- " .. text
            end
            -- seam31: the LAST leg that returns unfinished gets its checkpoint
            -- too, carried in its leg.<k>.end row.
            if not string.find(tostring(rows[3]), "checkpoint 2 written: checkpoint 2 written at", 1, true) then
                return "refused", "the all-PASS last leg left no written checkpoint -- " .. text
            end
            return "ok", text
        end)

        -- A CHECKPOINT IS REFUSED IN A DIALOGUE (seam30).  A checkpoint is
        -- per-player state only: a parked dialogue does not come back with
        -- it, so the server refuses `::checkpoint` while one is open and names
        -- it -- the reply the harness folds into the next leg row.
        stage(function()
            setup_cheat("::cook")                                -- setup
            settle(3)
        end)
        seam("seam.checkpoint_refuses_dialogue", function()
            local talk = verb("player", "talk_to")
            local drain = verb("chat", "drain")
            local cheat = verb("cheat")
            if not talk then return missing("player", "talk_to") end
            if not drain then return missing("chat", "drain") end
            if not cheat then return missing("cheat") end
            local talk_result, talk_detail = talk(COOK_SYMBOL)
            drain({ stop_at = "options" })
            local result = cheat("::checkpoint 2")
            local reply = t._legs_checkpoint_reply(2)
            local key = verb("key")
            if key then key("escape") end
            settle(2)
            local text = "talk_to cook -> " .. describe(talk_result) .. " " .. describe(talk_detail)
                .. "; ::checkpoint 2 -> " .. describe(result) .. " '" .. tostring(reply) .. "'"
            if result ~= "refused" or not string.find(tostring(reply), "refused: a dialogue is open", 1, true) then
                return "refused", "the checkpoint must be refused naming the open dialogue -- " .. text
            end
            return "ok", text
        end)
        stage(function()
            setup_cheat("::tele lumbridge")
            settle(4)
        end)

        -- A KILL WAIT CARRIES ITS PROGRESS TRAIL (seam31
        -- run_never_ends_silently).  await_dead / await_dead_engaged sample
        -- the fight every QD.COMBAT_PROGRESS_TICKS ticks, print each sample
        -- as a `QUEST progress` line (the one run.py's run.unfinished row
        -- reads back when a run stops inside the wait) and end their detail
        -- in "; progress t+10 hp .., ..", keeping the last
        -- QD.COMBAT_PROGRESS_KEEP.  Driven on the helpers themselves: a real
        -- fight long enough to drop samples would cost minutes of ticks.
        seam("seam.kill_wait_progress_trail", function()
            if type(t._combat_progress_new) ~= "function"
                or type(t._combat_progress_step) ~= "function"
                or type(t._combat_progress_text) ~= "function" then
                return "refused", "combat.lua has no _combat_progress_new/_step/_text"
            end
            if type(t.core_progress) ~= "function" or type(t.core_row_begin) ~= "function" then
                return "refused", "core.lua has no core_progress/core_row_begin"
            end
            local every = t.COMBAT_PROGRESS_TICKS
            local keep = t.COMBAT_PROGRESS_KEEP
            local progress = t._combat_progress_new("conformance kill wait")
            local empty = t._combat_progress_text(progress)
            -- under one period: no sample yet
            t._combat_progress_step(progress, every - 1, "30/30", 0, nil)
            local none_yet = #progress.trail
            local samples = keep + 2
            for i = 1, samples do
                t._combat_progress_step(progress, i * every, tostring(30 - i) .. "/30",
                    (i == 2) and 1 or 0, { eaten = (i >= 3) and 1 or 0 })
            end
            local text = t._combat_progress_text(progress)
            local detail = "every=" .. tostring(every) .. " keep=" .. tostring(keep)
                .. " before-first=" .. tostring(none_yet) .. " empty='" .. tostring(empty)
                .. "' after " .. tostring(samples) .. " samples: " .. text
            if empty ~= "" or none_yet ~= 0 then
                return "refused", "a wait with no sample must add nothing -- " .. detail
            end
            if #progress.trail ~= keep or progress.dropped ~= 2 then
                return "refused", "the trail must keep the last " .. tostring(keep)
                    .. " and count 2 dropped -- " .. detail
            end
            local last = "t+" .. tostring(samples * every) .. " hp " .. tostring(30 - samples) .. "/30 ate 1"
            if not string.find(text, "; progress (2 earlier dropped) t+" .. tostring(3 * every) .. " hp 27/30 ate 1", 1, true)
                or not string.find(text, last, 1, true) then
                return "refused", "the trail must read oldest-kept first and end at '" .. last .. "' -- " .. detail
            end
            return "ok", detail
        end)

        -- THE CAMERA IS READ, NOT SURVIVED (seam32 cutscene_verb_and_camera_read).
        -- No test read the camera, so a port that dropped a cutscene (Fight
        -- Arena's ogre pen: 17 LostCity camera ops, none in the port) stayed
        -- green.  Every CAM_* packet the client executes is stamped into
        -- app->cam_script (serial + a 64-deep ring, world tiles), read by
        -- t.world.camera() and t.cutscene.await.  Driven on the content
        -- debugproc ::cutscene <coord> [times] [hold] (cheat_cutscene.rs2:
        -- LostCity's Fire Warrior door cut, ikov_dungeon.rs2:183-185,217, at
        -- the tile named), so every keyframe the rows expect is written there.
        local function cutscene_literal(tile)
            return string.format("%d_%d_%d_%d_%d", tile.level, tile.x // 64, tile.z // 64, tile.x % 64, tile.z % 64)
        end

        step("world.camera", function()
            local fn = verb("world", "camera")
            if not fn then return missing("world", "camera") end
            local cam, why = fn()
            if type(cam) ~= "table" then
                return "refused", "t.world.camera() -> " .. tostring(cam) .. " " .. tostring(why)
            end
            return "ok", string.format("x=%d z=%d level=%d yaw=%d pitch=%d zoom=%d server_driven=%s serial=%d last_op=%s",
                cam.x, cam.z, cam.level, cam.yaw, cam.pitch, cam.zoom, tostring(cam.server_driven),
                cam.serial, tostring(cam.last_op))
        end)

        step("cutscene.mark", function()
            local fn = verb("cutscene", "mark")
            if not fn then return missing("cutscene", "mark") end
            local serial = fn()
            if type(serial) ~= "number" then
                return "refused", "t.cutscene.mark() -> " .. tostring(serial)
            end
            return "ok", "camera serial " .. serial
        end)

        -- t.cutscene.exempt (seam34 cutscene_site_on_an_optional_route):
        -- the row shape only -- the claim (a site OFF the guide's route, the
        -- reason naming a PASSed guide step) is gate.py's to grade, never
        -- this row's.  The verb writes its own `cutscene.exempt.<site>` row;
        -- this one checks the pair it answers.  Only the ok path: a refused
        -- call writes its own FAIL row (the refusals are proved in
        -- build/quest_gate/s34_exempt_verb).
        step("cutscene.exempt", function()
            local fn = verb("cutscene", "exempt")
            if not fn then return missing("cutscene", "exempt") end
            local result, detail = fn("conformance.rs2:1", "conformance: the row shape only; gate.py grades the claim")
            if result ~= "ok" or string.find(tostring(detail), "cutscene-exempt: conformance.rs2:1 ;; ", 1, true) ~= 1 then
                return "hollow", "t.cutscene.exempt -> " .. describe(result) .. " " .. describe(detail)
            end
            return "ok", detail
        end)

        step("cutscene.await", function()
            local fn = verb("cutscene", "await")
            if not fn then return missing("cutscene", "await") end
            local here = cutscene_literal(player_tile)
            local mark = t.cutscene.mark()
            t.cheat("::cutscene " .. here, false)
            return fn("conformance", { since = mark, timeout = 20, quiet = 15 })
        end)

        -- A free camera, then the same camera while ::cutscene holds it, then after the reset.
        seam("seam.world_camera_read", function()
            local free = t.world.camera()
            if type(free) ~= "table" or free.server_driven ~= false then
                return "refused", "free camera reads server_driven=" .. tostring(free and free.server_driven)
            end
            local here = cutscene_literal(player_tile)
            t.cheat("::cutscene " .. here, false)
            t.await({ level = function() return t.world.camera().server_driven end, note = "server_driven" }, 10)
            local mid = t.world.camera()
            local target = mid.last_target
            local aimed = target ~= nil and ((target.x == player_tile.x and target.z == player_tile.z)
                or (target.x == player_tile.x + 5 and target.z == player_tile.z + 2))
            t.await({ level = function() return not t.world.camera().server_driven end, note = "reset" }, 15)
            local after = t.world.camera()
            local text = string.format("free serial=%d; driven=%s last_target=%s,%s; after server_driven=%s last_op=%s",
                free.serial, tostring(mid.server_driven), tostring(target and target.x), tostring(target and target.z),
                tostring(after.server_driven), tostring(after.last_op))
            t.cutscene._claimed = after.serial   -- this sequence is not the next await's
            if mid.server_driven and aimed and after.server_driven == false and after.last_op == "reset" then
                return "ok", text
            end
            return "refused", text
        end)

        seam("seam.cutscene_await_records_keyframes", function()
            local here = cutscene_literal(player_tile)
            local mark = t.cutscene.mark()
            t.cheat("::cutscene " .. here, false)
            return t.cutscene.await("records", { since = mark, timeout = 20, quiet = 15, expect = {
                { op = "moveto", coord = here, height = 1000 },
                { op = "lookat", x = player_tile.x + 5, z = player_tile.z + 2, height = 50 },
                { op = "lookat", x = player_tile.x + 5, z = player_tile.z - 2 },
                { op = "reset" },
            } })
        end)

        seam("seam.cutscene_await_no_cutscene", function()
            local r, d = t.cutscene.await("nocam", { timeout = 5 })
            return r == "no_cutscene" and "ok" or "refused", "await answered " .. tostring(r) .. ": " .. tostring(d)
        end)

        seam("seam.cutscene_expect_missing_keyframe", function()
            local here = cutscene_literal(player_tile)
            local mark = t.cutscene.mark()
            t.cheat("::cutscene " .. here, false)
            local r, d = t.cutscene.await("missing", { since = mark, timeout = 20, quiet = 15, expect = {
                { op = "moveto", coord = here },
                { op = "lookat", x = player_tile.x + 9, z = player_tile.z + 9 },
            } })
            local named = string.find(tostring(d), "expected keyframe #2 (lookat " .. (player_tile.x + 9) .. ","
                .. (player_tile.z + 9), 1, true) ~= nil
            return (r == "not_found" and named) and "ok" or "refused", "await answered " .. tostring(r) .. ": " .. tostring(d)
        end)

        -- Fight Arena's shape: the second moveto lands in the tick of the first reset.
        seam("seam.cutscene_await_back_to_back", function()
            local here = cutscene_literal(player_tile)
            local full = {
                { op = "moveto", coord = here }, { op = "lookat", x = player_tile.x + 5, z = player_tile.z + 2 },
                { op = "lookat", x = player_tile.x + 5, z = player_tile.z - 2 }, { op = "reset" } }
            local mark = t.cutscene.mark()
            t.cheat("::cutscene " .. here .. " 2", false)
            local r1, d1 = t.cutscene.await("twice1", { since = mark, timeout = 20, quiet = 15, expect = full })
            -- The second sequence's moveto lands in the tick of the first reset, BEFORE this
            -- call. A quest reads it through the default start (the previous t.exec row's
            -- begin, clamped to the last claimed packet: arena's openCell.cutscene-2); this
            -- row makes no t.exec row between the two awaits, so it states that same start
            -- itself -- the serial the first read stopped at. What is proved is the stop:
            -- the first read must leave the second sequence unread and unclaimed.
            local r2, d2 = t.cutscene.await("twice2", { since = t.cutscene._claimed, timeout = 20,
                quiet = 15, expect = full })
            return (r1 == "ok" and r2 == "ok") and "ok" or "refused",
                "first " .. tostring(r1) .. ": " .. tostring(d1) .. " || second " .. tostring(r2) .. ": " .. tostring(d2)
        end)

        -- A WAIT OF REAL MINUTES IS FAST-FORWARDED, NOT SAT THROUGH (seam33
        -- test_clock_for_realtime_waits).  date_minutes / date_runeday were bare
        -- CLOCK_REALTIME reads, so Forgettable Tale's sixteen-minute kelda patch
        -- outlasted a run's frame budget; ::clockskip adds a per-world offset
        -- both opcodes read (ToriRSServer_WorldRealtimeMs) and t.clock.skip reads
        -- the new minute back through the client's date_minutes varp.  Graded on
        -- the varp moving by the skip (the verb's own +N check) and on the
        -- server's week bound refusing a 20000-minute skip.  One minute ahead
        -- costs nothing later: only cooldowns read the clock, and they shrink.
        step("clock.skip", function()
            local fn = verb("clock", "skip")
            if not fn then return missing("clock", "skip") end
            local result, detail = fn(1)
            if result ~= "ok" then
                return result, "t.clock.skip(1) -> " .. describe(detail)
            end
            if not string.find(tostring(detail), "(+1 skipped", 1, true) then
                return "hollow", "t.clock.skip(1) answered ok without naming the skip -- " .. describe(detail)
            end
            local refused_result, refused_detail = fn(20000)
            if refused_result ~= "refused" then
                return "hollow", "t.clock.skip(20000) (past the week bound) answered "
                    .. describe(refused_result) .. " " .. describe(refused_detail) .. "; skip(1): " .. detail
            end
            return "ok", detail .. "; skip(20000) refused: " .. describe(refused_detail)
        end)

        -- ::complete WRITES THE ROW IT NAMES, INCLUDING ONE WHOSE NAME IS ALSO A
        -- VARP (seam33 complete_cheat_arms).  `quest_wanted` is dbrow 156 AND
        -- varp 571 (Wanted!'s carrier), and `if ($row = quest_wanted)` compiled
        -- to `$row = 571` (sscompile resolves an untyped bare name by namespace
        -- sort order), so the arm never matched and `::complete quest_wanted`
        -- answered "::complete has no arm for that quest." -- Devious Minds' monk
        -- refused a player with every prerequisite staged
        -- (build/quest_gate/s33_cca_probe).  Temple of Ikov, Tourist Trap and
        -- Troll Stronghold had no arm at all, so Desert Treasure and Devious
        -- Minds staged them with ::setvar.  Graded per row on the progress var
        -- reaching the quest's own complete constant AND %varp101_qp rising by the row's
        -- quest:questpoints (the cheat pays it), from a var reset to 0 first so
        -- an earlier row's completion cannot pass it.  The vars go back to 0 after.
        -- parity3g added Underground Pass and Tree Gnome Village to the same row.
        -- One arm's reading: (true, reading) or (false, reading).
        local function complete_arm_reading(cheat, read, arm)
            setup_cheat("::setvar " .. arm.var .. " 0")
            settle(1)
            local _, qp_before = read("varp101_qp")
            local cheat_result = cheat("::complete " .. arm.row)
            settle(2)
            local value_result, value = read(arm.var)
            local _, qp_after = read("varp101_qp")
            local reading = arm.row .. ": " .. arm.var .. "=" .. describe(value)
                .. " (want " .. arm.complete .. "), qp " .. describe(qp_before) .. "->"
                .. describe(qp_after) .. " (want +" .. arm.points .. "), cheat " .. describe(cheat_result)
            if cheat_result ~= "ok" or value_result ~= "ok" or value ~= arm.complete
                or type(qp_before) ~= "number" or type(qp_after) ~= "number"
                or qp_after - qp_before ~= arm.points then
                return false, reading
            end
            return true, reading
        end
        seam("seam.complete_cheat_arms", function()
            local cheat = verb("cheat")
            local read = verb("var", "server")
            if not cheat then return missing("cheat") end
            if not read then return missing("var", "server") end
            local arms = {
                { row = "quest_wanted", var = "varb1051_wanted_main", complete = 11, points = 1 },
                { row = "quest_touristtrap", var = "varp197_desertrescue", complete = 30, points = 2 },
                { row = "quest_templeofikov", var = "varp26_ikov", complete = 80, points = 1 },
                { row = "quest_trollstronghold", var = "varp317_troll_quest", complete = 50, points = 1 },
                { row = "quest_undergroundpass", var = "varp161_upass", complete = 10, points = 5 },
                { row = "quest_treegnomevillage", var = "varp111_treequest", complete = 9, points = 2 },
            }
            local parts = {}
            for _, arm in ipairs(arms) do
                local landed, reading = complete_arm_reading(cheat, read, arm)
                if not landed then
                    return "refused", reading
                end
                parts[#parts + 1] = reading
            end
            local _, visible = read("varb10753_ikov_lucien_vis")
            if visible ~= 0 then
                return "refused", "ikov_lucien_vis=" .. describe(visible)
                    .. " after ::complete quest_templeofikov (the ending's ~ikov_lucien_sync gives 0)"
            end
            return "ok", table.concat(parts, "; ") .. "; ikov_lucien_vis=0"
        end)

        -- EVERY QUEST ROW WITH A COMPLETION SITE HAS A ::complete ARM (seam35
        -- complete_cheat_arms_for_every_quest_row).  These sixteen rows were
        -- passed to ~quest_complete_rewards by their own completion but
        -- `::complete` answered "has no arm for that quest.", so Legends',
        -- Mourning's End I's, Roving Elves' and Zogre's prerequisites were
        -- staged with ::setvar (rule (e)).  One row per arm: the var its
        -- completion writes reaches that constant and %qp rises by the row's
        -- quest:questpoints (build/seam_state/seam35/cca_before.ledger.tsv is the
        -- HEAD pack's answer, cca_after the fixed one).  Rows with no completion
        -- in content (the miniquests, Fairytale I/II, Rag and Bone Man II, ...)
        -- keep "no arm".
        -- The rows are literal (verb_list.py reads each name off its own `seam("` line).
        local seam35_vars = {}
        local function seam35_arm_row(row, var, complete, points)
            seam35_vars[#seam35_vars + 1] = var
            return function()
                local cheat = verb("cheat")
                local read = verb("var", "server")
                if not cheat then return missing("cheat") end
                if not read then return missing("var", "server") end
                local landed, reading = complete_arm_reading(cheat, read,
                    { row = row, var = var, complete = complete, points = points })
                return landed and "ok" or "refused", reading
            end
        end
        seam("seam.complete_cheat_arms.quest_bigchompybirdhunting", seam35_arm_row("quest_bigchompybirdhunting", "varp293_chompybird", 65, 2))
        seam("seam.complete_cheat_arms.quest_eadgarsruse", seam35_arm_row("quest_eadgarsruse", "varp335_eadgar_quest", 110, 1))
        seam("seam.complete_cheat_arms.quest_elementalworkshop1", seam35_arm_row("quest_elementalworkshop1", "varb2067_elemental_workshop_finished", 1, 1))
        seam("seam.complete_cheat_arms.quest_familycrest", seam35_arm_row("quest_familycrest", "varp148_crestquest", 11, 1))
        seam("seam.complete_cheat_arms.quest_fightarena", seam35_arm_row("quest_fightarena", "varp17_arenaquest", 14, 2))
        seam("seam.complete_cheat_arms.quest_horrorfromthedeep", seam35_arm_row("quest_horrorfromthedeep", "varb34_horrorquest", 10, 2))
        seam("seam.complete_cheat_arms.quest_insearchofthemyreque", seam35_arm_row("quest_insearchofthemyreque", "varp387_routequest", 105, 2))
        seam("seam.complete_cheat_arms.quest_lostcity", seam35_arm_row("quest_lostcity", "varp147_zanaris", 6, 3))
        seam("seam.complete_cheat_arms.quest_onesmallfavour", seam35_arm_row("quest_onesmallfavour", "varp416_onesmallfavour", 285, 2))
        seam("seam.complete_cheat_arms.quest_regicide", seam35_arm_row("quest_regicide", "varp328_regicide_quest", 15, 3))
        seam("seam.complete_cheat_arms.quest_scorpioncatcher", seam35_arm_row("quest_scorpioncatcher", "varp76_scorpcatcher", 6, 1))
        seam("seam.complete_cheat_arms.quest_seaslug", seam35_arm_row("quest_seaslug", "varp159_seaslugquest", 12, 1))
        seam("seam.complete_cheat_arms.quest_shadesofmortton", seam35_arm_row("quest_shadesofmortton", "varp339_morttonquest", 85, 3))
        seam("seam.complete_cheat_arms.quest_shilovillage", seam35_arm_row("quest_shilovillage", "varp116_zombiequeen", 15, 2))
        seam("seam.complete_cheat_arms.quest_tribaltotem", seam35_arm_row("quest_tribaltotem", "varp200_totemquest", 5, 1))
        seam("seam.complete_cheat_arms.quest_witchshouse", seam35_arm_row("quest_witchshouse", "varp226_ballquest", 7, 4))
        stage(function()
            setup_cheat("::setvar varb1051_wanted_main 0")               -- teardown
            setup_cheat("::setvar varp197_desertrescue 0")
            setup_cheat("::setvar varp26_ikov 0")
            setup_cheat("::setvar varp317_troll_quest 0")
            setup_cheat("::setvar varp161_upass 0")
            setup_cheat("::setvar varp111_treequest 0")
            for _, var in ipairs(seam35_vars) do
                setup_cheat("::setvar " .. var .. " 0")
            end
            for _, bit in ipairs({ "book", "key", "fire", "bellows", "bellows_switch", "switch" }) do
                setup_cheat("::setvar elemental_workshop_" .. bit .. " 0")   -- the EW1 arm's side bits
            end
            settle(1)
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

        -- ---------------------------- phase 9: leave and come back (session.*)
        --
        -- seam18 D.  LAST of the world rows: a relog RE-BOOTS the embedded
        -- server from the save (net_transport_embed.c), so nothing after it
        -- may need unsaved world state.  logout presses the logout tab's own
        -- button; login types the run's account through the title screen;
        -- the same tile and the same backpack prove the session came back.
        -- Proven first as build/quest_gate/s18d_relog7 (14/14).
        step("session.screen", function()
            local fn = verb("session", "screen")
            if not fn then return missing("session", "screen") end
            local result, name, number = fn()
            if result == "ok" and name ~= "game" then
                return "hollow", "in the world but the screen reads " .. describe(name) .. " (" .. describe(number) .. ")"
            end
            return result, describe(name) .. " (" .. describe(number) .. ")"
        end)

        -- The account: tools/quest_gate/conformance.py launches this harness
        -- as USER "qdconform", and its session directory is attempt-NN, so
        -- the verbs' default (the session dir's name, which IS the account
        -- for every run.py quest) would log a fresh character in.
        local SESSION_USER = "qdconform"
        local session_before_tile, session_before_runes

        local function session_reading()
            local tile = verb("world", "tile")
            local count = verb("inv", "count")
            if not (tile and count) then
                return nil, nil
            end
            local _, here = tile()
            local _, runes = count(OBJ_SYMBOL)
            return here, runes
        end

        -- The same tile and the same backpack after a login: the session that
        -- came back is this character's, loaded from the save its logout wrote.
        local function same_session(here, runes)
            if type(here) ~= "table" or type(session_before_tile) ~= "table"
                or here.x ~= session_before_tile.x or here.z ~= session_before_tile.z then
                return false, "in the world at " .. describe(here) .. ", left it at "
                    .. describe(session_before_tile)
            end
            if type(runes) ~= "number" or runes ~= session_before_runes then
                return false, "the backpack came back with " .. OBJ_SYMBOL .. " " .. describe(runes)
                    .. ", not " .. describe(session_before_runes)
            end
            return true, "same tile " .. here.x .. "," .. here.z .. ", " .. OBJ_SYMBOL .. " " .. runes
        end

        step("session.logout", function()
            local fn = verb("session", "logout")
            local screen = verb("session", "screen")
            if not fn then return missing("session", "logout") end
            if not screen then return missing("session", "screen") end
            session_before_tile, session_before_runes = session_reading()
            local result, detail = fn()
            if result ~= "ok" then
                return result, describe(detail)
            end
            local _, name = screen()
            if name ~= "title" then
                return "hollow", "answered ok but the screen reads " .. describe(name) .. " -- " .. describe(detail)
            end
            return "ok", describe(detail)
        end)

        step("session.login", function()
            local fn = verb("session", "login")
            if not fn then return missing("session", "login") end
            local result, detail = fn(SESSION_USER)
            if result ~= "ok" then
                return result, describe(detail)
            end
            local same, why = same_session(session_reading())
            if not same then
                return "hollow", why .. " -- " .. describe(detail)
            end
            return "ok", describe(detail) .. "; " .. why
        end)

        step("session.relog", function()
            local fn = verb("session", "relog")
            local cheat = verb("cheat")
            if not fn then return missing("session", "relog") end
            if not cheat then return missing("cheat") end
            session_before_tile, session_before_runes = session_reading()
            local result, detail = fn(SESSION_USER)
            if result ~= "ok" then
                return result, describe(detail)
            end
            local same, why = same_session(session_reading())
            if not same then
                return "hollow", why .. " -- " .. describe(detail)
            end
            -- The RE-BOOTED server must answer a script: a debugproc's reply.
            local answered_result, answered = cheat("::dropobj " .. OBJ_SYMBOL .. " 1")
            if answered_result ~= "ok" then
                return "hollow", "relogged (" .. why .. ") but ::dropobj answered "
                    .. describe(answered_result) .. " " .. describe(answered)
            end
            return "ok", describe(detail) .. "; " .. why .. "; the server answers ::dropobj"
        end)

        -- RENDER SKIP (seam34, owner request 2026-09-30).  run.py and
        -- conformance.py start every client with TORIRS_RENDER_SKIP=1, so a
        -- frame draws only when a screenshot, a pickset read or a pushed click
        -- needs it (src/app/app_render.c's render-skip banner).  The verbs are
        -- graded on frames MOVING the right way -- t.render._state's counters
        -- -- never on their own word, and each row leaves skip as it found it.
        -- HERE, after session.relog and before finish, on purpose: any row
        -- placed earlier spends ticks every later row then runs after, and
        -- seam.attack_presses_the_watched_slot reads the goblins' wander --
        -- moved into phase 0 these rows turned it red (the field's nearest
        -- copy became a ::spawned one), skip on and skip off alike.  The
        -- sailing rows' hull is still live here, but it stands outside the
        -- loaded scene, and only a hull inside it draws every frame.
        step("render.skip", function()
            local fn = verb("render", "skip")
            if not fn then return missing("render", "skip") end
            local _, before = t.render._state()
            local refused_result = fn("yes")
            local on_result, on_detail = fn(true)
            if on_result ~= "ok" then
                fn(before.skip)
                return on_result, "t.render.skip(true) -> " .. describe(on_detail)
            end
            local _, on_start = t.render._state()
            settle(3)
            local _, on_end = t.render._state()
            local off_result, off_detail = fn(false)
            local _, off_start = t.render._state()
            settle(3)
            local _, off_end = t.render._state()
            fn(before.skip)
            local skipped_on = on_end.skipped - on_start.skipped
            local drawn_off = off_end.drawn - off_start.drawn
            local reading = string.format("on: %s; 3 idle ticks skipped %d frame(s), drew %d (live hulls %d); "
                .. "off: %s; 3 idle ticks drew %d, skipped %d; skip('yes') -> %s",
                describe(on_detail), skipped_on, on_end.drawn - on_start.drawn, on_start.hulls,
                describe(off_detail), drawn_off, off_end.skipped - off_start.skipped, describe(refused_result))
            -- t.ticks(3) spans 61..90 frames (it starts mid-tick).  Idle with
            -- skip on, nothing owes a draw; with it off, an in-world frame
            -- always redraws.
            if off_result ~= "ok" or refused_result ~= "refused" or skipped_on < 55 or drawn_off < 55 then
                return "hollow", reading
            end
            return "ok", reading
        end)

        step("render.frame", function()
            local fn = verb("render", "frame")
            local skip = verb("render", "skip")
            if not fn then return missing("render", "frame") end
            if not skip then return missing("render", "skip") end
            local _, before = t.render._state()
            skip(true)
            settle(1)
            local result, detail = fn()
            skip(before.skip)
            local from, to = string.match(tostring(detail), "rendered (%d+) %-> (%d+)")
            if result ~= "ok" or not from or tonumber(to) <= tonumber(from) then
                return "hollow", "t.render.frame() with skip on -> " .. describe(result) .. " " .. describe(detail)
            end
            return "ok", detail
        end)

        -- A pick read after skipped frames must answer what it would with
        -- skip off -- the previous frame's stamp -- WITHOUT waiting a frame
        -- (a frame waited for shifts the whole run: five quests went red on
        -- that).  So the read draws the skipped frame late, then answers.
        seam("seam.render_skip_pick_read_catches_up", function()
            local skip = verb("render", "skip")
            if not skip then return missing("render", "skip") end
            local _, before = t.render._state()
            skip(true)
            local result, detail, facts = t.render._pick_catch_up_probe()
            skip(before.skip)
            if result ~= "ok" then
                return result, detail
            end
            if not facts.stamped or facts.skipped_idle < 55 or facts.caught_up ~= 1
                or not facts.valid or not facts.same_tick then
                return "refused", detail
            end
            return "ok", detail
        end)

        -- A screenshot under render skip draws its own frame, and the
        -- skipped frame before it is drawn late first (the overlays and
        -- mouseover text the capture shows were laid out from that frame).
        -- Graded on the file and on two frames drawn across the capture.
        seam("seam.render_skip_shot_draws_its_frame", function()
            local shot = verb("shot")
            local skip = verb("render", "skip")
            if not shot then return missing("shot") end
            if not skip then return missing("render", "skip") end
            local _, before = t.render._state()
            skip(true)
            settle(2)
            local _, idle = t.render._state()
            local result, detail = shot("render-skip-probe", true)
            local _, after = t.render._state()
            skip(before.skip)
            local reading = string.format("t.shot after 2 idle ticks -> %s %s; frames drawn across it %d",
                describe(result), describe(detail), after.rendered - idle.rendered)
            if result ~= "ok" or not string.find(tostring(detail), "render-skip-probe.png", 1, true)
                or after.rendered - idle.rendered < 2 then
                return "refused", reading
            end
            return "ok", reading
        end)

        -- A HELD OP THAT WIELDS ANSWERS ok AND SAYS WORN (seam34
        -- upass_wield_and_rock_bridges).  A Wield/Wear ([opheld2,_] ~equip,
        -- player/scripts/equip.rs2:304) that lands prints nothing, mounts
        -- nothing and routes nowhere, so inv_op's settle waited out its ten
        -- ticks and answered `timeout ... -> 0 left [settle_after_click]` for
        -- an item that was on the player (Underground Pass leg.4.wield,
        -- build/quest_gate/upass row 111; alone in s34wield_before2).  inv_op
        -- now reads the item's worn total before the press and resolves on it
        -- rising.  Graded on the verb's own word, the WORN tag naming the
        -- item, and the worn container read back independently.  The backpack
        -- is cleared first: this harness's is full by here.
        stage(function()
            setup_cheat("::clearinv")
            setup_cheat("::give bronze_scimitar")
            settle(2)
        end)
        seam("seam.inv_op_wield_reads_worn", function()
            local fn = verb("player", "inv_op")
            local count = verb("inv", "count")
            if not fn then return missing("player", "inv_op") end
            if not count then return missing("inv", "count") end
            local before_result, before = count("bronze_scimitar")
            if before_result ~= "ok" or type(before) ~= "number" or before < 1 then
                return "no_subject", "::give bronze_scimitar left " .. describe(before)
                    .. " in the backpack (" .. describe(before_result) .. ")"
            end
            local result, detail = fn("bronze_scimitar", 2)
            local worn_result, worn = t.ui._worn_count("bronze_scimitar")
            local text = "inv_op(bronze_scimitar,2) -> " .. describe(result) .. " " .. describe(detail)
                .. "; worn read " .. describe(worn_result) .. " " .. describe(worn)
            if result ~= "ok" then
                return result, text
            end
            if string.find(tostring(detail), "WORN bronze_scimitar", 1, true) == nil
                or worn_result ~= "ok" or worn ~= 1 then
                return "hollow", text
            end
            return "ok", text
        end)

        -- A CACHE-DECLARED onvarptransmit HOOK FIRES ON A GROUP'S FIRST OPEN.
        -- Task_InterfaceOpen armed a record's own transmit hooks
        -- (`onvarptransmit=` + `varptriggers=` in the .if) BEFORE it baked the
        -- group into the tree, so on a group's first open every hook held a
        -- node ref that resolved to nothing, and the var dispatch treats such a
        -- ref as a reclaimed component: never fired, compacted away. The panel
        -- painted once from its onloads and went deaf. Forgettable Tale's
        -- junction puzzle (interface 248; counters 1240/1241 on varp 525,
        -- switches 1244 on 524/525) kept "x 1 / x 1" and bare junctions while
        -- the server moved both varps (shots 194-196 byte-identical; seam34
        -- junction_interface_does_not_redraw). Graded on the counter TEXT the
        -- hook writes tracking the server's varbits across one Set Junction
        -- press, and on that text having CHANGED, so an onload-only paint
        -- cannot pass it. The press is undone (two more presses cycle the
        -- junction back to empty) and the quest vars are reset afterwards.
        seam("seam.cache_transmit_hook_first_open", function()
            local goto_tile = verb("player", "goto_tile")
            local click_loc = verb("player", "click_loc")
            local await_open = verb("ui", "await_open")
            local widget = verb("ui", "widget")
            local invoke = verb("ui", "invoke")
            local text = verb("ui", "text")
            local key = verb("key")
            local read = verb("var", "server")
            local tile_of = verb("world", "tile")
            if not goto_tile then return missing("player", "goto_tile") end
            if not click_loc then return missing("player", "click_loc") end
            if not await_open then return missing("ui", "await_open") end
            if not widget then return missing("ui", "widget") end
            if not invoke then return missing("ui", "invoke") end
            if not text then return missing("ui", "text") end
            if not key then return missing("key") end
            if not read then return missing("var", "server") end
            if not tile_of then return missing("world", "tile") end
            local _, home = tile_of()
            local function counters()
                local _, yellow = text("forget_puzzle1:yellow_counter")
                local _, green = text("forget_puzzle1:green_counter")
                local _, left = read("varb861_forget_num_left")
                local _, right = read("varb862_forget_num_right")
                return yellow, green, left, right
            end
            local function restore()
                key("escape")
                settle(1)
                setup_cheat("::setvar varb842_forget_if1 0")
                setup_cheat("::setvar varb861_forget_num_left 0")
                setup_cheat("::setvar varb862_forget_num_right 0")
                setup_cheat("::setvar varb822_forget_quest 0")
                if type(home) == "table" then
                    goto_tile(home.x, home.z, home.level or 0)
                end
            end
            -- forget_group() is 1 at forget_quest 100 (forget_puzzle.rs2), so the
            -- hub machinery opens forget_puzzle1; one stone of each colour.
            setup_cheat("::setvar varb822_forget_quest 100")
            setup_cheat("::setvar varb842_forget_if1 0")
            setup_cheat("::setvar varb861_forget_num_left 1")
            setup_cheat("::setvar varb862_forget_num_right 1")
            local goto_result = goto_tile(1861, 4954, 1)
            if goto_result ~= "ok" then
                restore()
                return "refused", "goto hub 1861,4954,1 -> " .. describe(goto_result)
            end
            local click_result, click_detail = click_loc("keldagrim_track_junction_control_box", 1)
            local open_result, open_detail = await_open("forget_puzzle1", 30)
            if open_result ~= "ok" then
                restore()
                return "refused", "machinery -> " .. describe(click_result) .. " " .. describe(click_detail)
                    .. "; forget_puzzle1 open -> " .. describe(open_result) .. " " .. describe(open_detail)
            end
            settle(2)
            local y0, g0, l0, r0 = counters()
            local _, switch = widget("forget_puzzle1:switch_a")
            invoke(switch, 0)
            settle(2)
            local y1, g1, l1, r1 = counters()
            local reading = "before yellow_counter=" .. describe(y0) .. " green_counter=" .. describe(g0)
                .. " (server left=" .. describe(l0) .. " right=" .. describe(r0) .. "); after one Set Junction"
                .. " yellow_counter=" .. describe(y1) .. " green_counter=" .. describe(g1)
                .. " (server left=" .. describe(l1) .. " right=" .. describe(r1) .. ")"
            -- Undo: empty -> green -> yellow -> empty is three presses in all.
            for _ = 1, 2 do
                invoke(switch, 0)
                settle(1)
            end
            local _, junction_after = read("varb842_forget_if1")
            restore()
            if type(l1) ~= "number" or type(r1) ~= "number" then
                return "refused", reading .. " -- the server varbits did not read"
            end
            if y1 ~= "x " .. l1 or g1 ~= "x " .. r1 then
                return "refused", reading .. " -- the counter text does not follow the varbits"
                    .. " (the cache onvarptransmit hook did not run)"
            end
            if y1 == y0 and g1 == g0 then
                return "refused", reading .. " -- the press changed nothing on the panel"
            end
            return "ok", reading .. "; junction back to " .. describe(junction_after)
        end)

        -- A CONTENT-DECLARED SHOP OPENS WITH ITS STOCK (seam35
        -- shop_with_content_declared_inv_opens_empty).  A shop inv declared in
        -- content (pack/inv.alloc + `size=`; the Ardougne silver stall is 2023,
        -- size 3) has no cache InvType, so the client's INV_SIZE answered 0 and
        -- shop_main_init (1074) built 0 grid cells: the stock was resident and
        -- `t.shop.buy` answered "cell 3 of shopmain:items is not mounted"
        -- (build/quest_gate/s35_silver_before).  INV_SIZE now falls back to the
        -- capacity UPDATE_INV_FULL gave the container (src/game/rs_cs2_host.c
        -- exec_inv_size).  Graded on the bar arriving in the backpack and the
        -- coins paid, after the third cell's text read.
        seam("seam.shop_content_inv_opens_stocked", function()
            local goto_tile = verb("player", "goto_tile")
            local open = verb("shop", "open")
            local buy = verb("shop", "buy")
            local close = verb("shop", "close")
            local count = verb("inv", "count")
            local await = verb("inv", "await")
            local text = verb("ui", "text")
            if not goto_tile then return missing("player", "goto_tile") end
            if not open then return missing("shop", "open") end
            if not buy then return missing("shop", "buy") end
            if not close then return missing("shop", "close") end
            if not count then return missing("inv", "count") end
            if not await then return missing("inv", "await") end
            if not text then return missing("ui", "text") end
            setup_cheat("::clearinv")
            setup_cheat("::give coins 5000")
            settle(2)
            local goto_result = goto_tile(2658, 3314, 0)
            if goto_result ~= "ok" then
                return "refused", "goto the Ardougne silver stall 2658,3314 -> " .. describe(goto_result)
            end
            local open_result, open_detail = open("silver_merchant_ardougne", 3, "ardougne_silver_stall_shop")
            if open_result ~= "ok" then
                return open_result, "shop.open(silver_merchant_ardougne) -> " .. describe(open_detail)
            end
            local cell_result, cell = text("shopmain:items", 3)
            local _, coins_before = count("coins")
            local buy_result, buy_detail = buy("silver_bar", 1)
            local landed = await("silver_bar", 1, 10)
            local _, coins_after = count("coins")
            close()
            setup_cheat("::clearinv")
            local reading = "open: " .. describe(open_detail) .. "; cell 3 " .. describe(cell_result) .. " "
                .. describe(cell) .. "; buy -> " .. describe(buy_result) .. " " .. describe(buy_detail)
                .. "; silver_bar await " .. describe(landed) .. ", coins " .. describe(coins_before)
                .. " -> " .. describe(coins_after)
            if buy_result ~= "ok" then
                return buy_result, reading
            end
            if landed ~= "ok" or type(coins_before) ~= "number" or type(coins_after) ~= "number"
                or coins_after >= coins_before then
                return "hollow", reading
            end
            return "ok", reading
        end)

        -- A COVERED ATTACK PRESS IS RE-TAKEN FROM A SETTLED CAMERA, AND A BOSS
        -- THAT TELEPORTS IS FOLLOWED (seam35 covered_press_and_timed_lift_without
        -- _drive_op).  Treus Dayth rises one row after a press that WALKED the
        -- player; the camera anchor is still easing, the first Attack press
        -- lands where he is not drawn, and the unsettled pose loop never held
        -- him (HEAD: 3 presses, 64 ticks, `not_found`/covered, the character
        -- dead -- build/quest_gate/s35rb_cov, s35rb_tel).  And npc_tele
        -- re-adds him under a NEW client slot, which await_dead_engaged graded
        -- a kill "corroborated by ABSENCE" while he stood alive.  ::god keeps
        -- the character standing (a death ends this run); the fight is real.
        local dayth_watch_slot = nil
        local function dayth_teardown()
            setup_cheat("::god 0")
            setup_cheat("::setvar varp382_hauntedmine 0")
            setup_cheat("::clearinv")
            setup_cheat("::tele lumbridge")
            settle(2)
        end
        stage(function()
            setup_cheat("::clearinv")
            setup_cheat("::setlevel ranged 99")
            setup_cheat("::setlevel hitpoints 99")
            setup_cheat("::setlevel defence 99")
            setup_cheat("::give magic_shortbow 1")
            setup_cheat("::give rune_arrow 400")
            setup_cheat("::god 1")
            settle(2)
        end)
        seam("seam.npc_cover_settled_recovery", function()
            local equip = verb("player", "equip")
            local goto_tile = verb("player", "goto_tile")
            local press = verb("player", "press")
            local attack = verb("player", "attack")
            local await = verb("msg", "await")
            local expect_line = verb("msg", "expect")
            local camera = verb("drive", "camera")
            if not equip then return missing("player", "equip") end
            if not goto_tile then return missing("player", "goto_tile") end
            if not press then return missing("player", "press") end
            if not attack then return missing("player", "attack") end
            if not await then return missing("msg", "await") end
            if not expect_line then return missing("msg", "expect") end
            if not camera then return missing("drive", "camera") end
            equip("magic_shortbow")
            equip("rune_arrow")
            local goto_result = goto_tile(2795, 4456, 0)
            if goto_result ~= "ok" then
                dayth_teardown()
                return "refused", "goto Dayth's key 2795,4456 -> " .. describe(goto_result)
            end
            camera(0, 383, 400)
            local press_result, press_detail = press("hauntedmine_boss_key", 1, 8)
            local rises = expect_line("Treus Dayth rises")
            if rises ~= "ok" then
                rises = await("Treus Dayth rises", 40)
            end
            if rises ~= "ok" then
                dayth_teardown()
                return "no_subject", "the key press did not raise Dayth: " .. describe(press_result) .. " "
                    .. describe(press_detail)
            end
            local result, detail = attack("hauntedmine_boss_ghost", 2, 20)
            dayth_watch_slot = tonumber(string.match(tostring(detail), "watching slot (%d+)"))
            local text = "attack(hauntedmine_boss_ghost) -> " .. describe(result) .. " " .. describe(detail)
            if result ~= "ok" and result ~= "timeout" then
                return result, text
            end
            if string.find(tostring(detail), "[Attack", 1, true) == nil
                or string.find(tostring(detail), "in 1 press(es)", 1, true) == nil then
                return "hollow", text
            end
            return "ok", text
        end)
        seam("seam.npc_tele_reslot_followed", function()
            local engaged = verb("npc", "await_dead_engaged")
            local server = verb("var", "server")
            if not engaged then return missing("npc", "await_dead_engaged") end
            if not server then return missing("var", "server") end
            if dayth_watch_slot == nil then
                dayth_teardown()
                return "no_subject", "seam.npc_cover_settled_recovery engaged no Dayth slot to watch"
            end
            local result, detail = engaged(60, 10)
            local _, stage_value = server("varp382_hauntedmine")
            local last = tonumber(string.match(tostring(detail), "slot (%d+) still alive")
                or string.match(tostring(detail), "slot (%d+) dead"))
            dayth_teardown()
            local text = "attack watched slot " .. describe(dayth_watch_slot) .. ", the wait ended on slot "
                .. describe(last) .. "; await_dead_engaged -> " .. describe(result) .. " (hauntedmine="
                .. describe(stage_value) .. ") " .. describe(detail)
            -- A teleport graded as a kill is the defect: `ok` while the quest
            -- stage is short of ^hmq_dayth_killed (9).
            if result == "ok" and (type(stage_value) ~= "number" or stage_value < 9) then
                return "refused", text
            end
            if last == nil or last == dayth_watch_slot then
                return "no_subject", text .. " -- Dayth never changed slot in the window"
            end
            return "ok", text
        end)

        -- A [MAPZONE] TRIGGER FIRES ON A SQUARE'S UPPER LEVEL (seam36
        -- mapzone_triggers_fire_on_every_level).  A [mapzone] subject is always
        -- level 0 (LostCity NetworkPlayer.ts:252 latches the square with
        -- CoordGrid.packCoord(0, ...); Player.ts:582 names it
        -- `[mapzone,0_x_z]`), so it fires on entering the 64x64 square on ANY
        -- level.  Underground Pass named its level-1 rooms `[mapzone,1_33_71]`
        -- / `[mapzone,1_33_72]`, a name no dispatch forms: the three demons and
        -- Iban's temple never spawned (build/seam_state/seam36
        -- s36_before.ledger.tsv: holthion=no_row).  Graded on Holthion standing
        -- in the demons' room a few ticks after stepping into 33_71 on level 1
        -- at upass stage 6 (^upass_spoken_nilhoof, >= ^upass_entered_main_area).
        seam("seam.mapzone_upper_level_square_fires", function()
            local goto_tile = verb("player", "goto_tile")
            local present = verb("npc", "await_present")
            local server = verb("var", "server")
            if not goto_tile then return missing("player", "goto_tile") end
            if not present then return missing("npc", "await_present") end
            if not server then return missing("var", "server") end
            setup_cheat("::setvar varp161_upass 6")
            settle(2)
            local _, stage_value = server("varp161_upass")
            local goto_result, goto_detail = goto_tile(2150, 4546, 1)
            local seen, seen_detail = "not_run", nil
            if goto_result == "ok" then
                seen, seen_detail = present("holthion", 40, 10)
            end
            setup_cheat("::setvar varp161_upass 0")
            setup_cheat("::tele lumbridge")
            settle(2)
            local text = "upass=" .. describe(stage_value) .. "; goto 2150,4546,1 -> " .. describe(goto_result)
                .. " " .. describe(goto_detail) .. "; holthion await_present -> " .. describe(seen) .. " "
                .. describe(seen_detail)
            if stage_value ~= 6 then
                return "no_subject", text .. " -- the stage did not take"
            end
            if goto_result ~= "ok" then
                return "refused", text
            end
            if seen ~= "ok" then
                return "refused", text .. " -- [mapzone,0_33_71] did not run ~upass_spawn_demons"
            end
            return "ok", text
        end)

        -- THE H.A.M. TRAPDOOR'S CLIMB-DOWN LANDS IN THE LAIR (seam37
        -- losttribe_trapdoor_maplink).  The lair is its own underground region
        -- (m49_150), not the plane below the trapdoor, and the maplink harvest
        -- dropped the trapdoor's row (it names the multiloc base 5492, which
        -- has no Climb-down op), so `~climb_ladder(-1)` answered "You can't go
        -- any further." and the lair could only be entered by a teleport.
        -- [oploc1,osf_trapdoor_open] now lands on ^lt_ham_trapdoor_in
        -- (3149,9652,0: RuneLite shortest-path transports.tsv:1064-1065,
        -- 2009scape HamHideoutPlugin) from any side.  Graded on the tile after
        -- Pick-Lock (op 5) and Climb-down, approached from the NORTH -- the side
        -- the harvest never listed -- and on the ladder back to 3165,3251,0.
        seam("seam.ham_trapdoor_climb_down_lands_in_lair", function()
            local goto_tile = verb("player", "goto_tile")
            local click_loc = verb("player", "click_loc")
            local await = verb("var", "await")
            local tile = verb("world", "tile")
            if not goto_tile then return missing("player", "goto_tile") end
            if not click_loc then return missing("player", "click_loc") end
            if not await then return missing("var", "await") end
            if not tile then return missing("world", "tile") end
            local goto_result, goto_detail = goto_tile(3166, 3253, 0)
            if goto_result ~= "ok" then
                return "no_subject", "goto 3166,3253,0 -> " .. describe(goto_result) .. " " .. describe(goto_detail)
            end
            local pick_result, pick_detail = click_loc("osf_trapdoor_closed", 5)
            local unlocked = await("varb235_ham_thief", 1, 6)
            local down_result, down_detail = click_loc("osf_trapdoor_open", 1)
            settle(4)
            local _, lair = tile()
            local up_result, up_detail = "not_run", nil
            local surface = nil
            if type(lair) == "table" and lair.x == 3149 and lair.z == 9652 then
                up_result, up_detail = click_loc("osf_ham_ladder", 1)
                settle(4)
                local _, after = tile()
                surface = after
            end
            setup_cheat("::setvar varb235_ham_thief 0")
            setup_cheat("::tele lumbridge")
            settle(2)
            local function at(where)
                if type(where) ~= "table" then return describe(where) end
                return describe(where.x) .. "," .. describe(where.z) .. "," .. describe(where.level)
            end
            local text = "pick-lock -> " .. describe(pick_result) .. " " .. describe(pick_detail)
                .. "; ham_thief await -> " .. describe(unlocked)
                .. "; Climb-down -> " .. describe(down_result) .. " " .. describe(down_detail)
                .. "; at " .. at(lair) .. " (want 3149,9652,0); ladder -> " .. describe(up_result)
                .. " " .. describe(up_detail) .. "; at " .. at(surface) .. " (want 3165,3251,0)"
            if unlocked ~= "ok" then
                return "no_subject", text .. " -- the Pick-Lock did not take"
            end
            if type(lair) ~= "table" or lair.x ~= 3149 or lair.z ~= 9652 or lair.level ~= 0 then
                return "refused", text .. " -- [oploc1,osf_trapdoor_open] did not land in the lair"
            end
            if type(surface) ~= "table" or surface.x ~= 3165 or surface.z ~= 3251 or surface.level ~= 0 then
                return "refused", text .. " -- [oploc1,osf_ham_ladder] did not return to the surface"
            end
            return "ok", text
        end)

        -- IBAN'S TEMPLE DOORS TAKE A REGICIDE PLAYER TO THE RUINED TEMPLE
        -- (matthew-mbp-m4-b48-seam1 regicide_temple_shortcut).  Regicide's
        -- route to the Well of Voyage goes through the doors of Iban's temple
        -- after Underground Pass is done; [label,open_iban_door] had only a
        -- "deferred" comment there, so a player with upass complete got "The
        -- temple is in ruins... You cannot enter." and the walk could only
        -- jump the door with a goto.  The branch is LostCity_Server
        -- quest_upass.rs2:576-585 (enter, before the Zamorak-robe check) and
        -- :629-631 (leave).  Graded on the tile after clicking the right leaf
        -- at 2143,4648,1 from the east (want 2014,4712,1, the ruined temple
        -- beside regicide_voyage_temple_well1), on the ruined copy at
        -- 2015,4712 putting the player back at 2145,4648,1, and on a player
        -- WITHOUT Regicide progress still being refused (stays east).
        seam("seam.iban_temple_door_regicide_shortcut", function()
            local goto_tile = verb("player", "goto_tile")
            local click_loc = verb("player", "click_loc")
            local server = verb("var", "server")
            local tile = verb("world", "tile")
            if not goto_tile then return missing("player", "goto_tile") end
            if not click_loc then return missing("player", "click_loc") end
            if not server then return missing("var", "server") end
            if not tile then return missing("world", "tile") end
            setup_cheat("::setvar varp161_upass ^upass_complete")
            setup_cheat("::setvar varp328_regicide_quest ^regicide_spoken_lathas")
            settle(2)
            local _, regicide_value = server("varp328_regicide_quest")
            local goto_result, goto_detail = goto_tile(2145, 4648, 1)
            local enter_result, enter_detail = "not_run", nil
            local leave_result, leave_detail = "not_run", nil
            local inside, outside = nil, nil
            if goto_result == "ok" then
                enter_result, enter_detail = click_loc("upass_templedoor_closed_right", 1, { at = { 2143, 4648 } })
                settle(4)
                local _, here = tile()
                inside = here
                if type(here) == "table" and here.x == 2014 and here.z == 4712 then
                    leave_result, leave_detail = click_loc("upass_templedoor_closed_right", 1, { at = { 2015, 4712 } })
                    settle(6)
                    local _, back = tile()
                    outside = back
                end
            end
            -- The control: no Regicide progress, so the branch must not run.
            local control_result, control_detail = "not_run", nil
            local control_at = nil
            setup_cheat("::setvar varp328_regicide_quest 0")
            settle(6)
            local control_goto = goto_tile(2145, 4648, 1)
            if control_goto == "ok" then
                control_result, control_detail = click_loc("upass_templedoor_closed_right", 1, { at = { 2143, 4648 } })
                settle(4)
                local _, here = tile()
                control_at = here
            end
            setup_cheat("::setvar varp161_upass 0")
            setup_cheat("::tele lumbridge")
            settle(2)
            local function at(where)
                if type(where) ~= "table" then return describe(where) end
                return describe(where.x) .. "," .. describe(where.z) .. "," .. describe(where.level)
            end
            local text = "regicide=" .. describe(regicide_value) .. "; goto 2145,4648,1 -> " .. describe(goto_result)
                .. " " .. describe(goto_detail) .. "; enter -> " .. describe(enter_result) .. " "
                .. describe(enter_detail) .. "; at " .. at(inside) .. " (want 2014,4712,1); leave -> "
                .. describe(leave_result) .. " " .. describe(leave_detail) .. "; at " .. at(outside)
                .. " (want 2145,4648,1); control (regicide 0) -> " .. describe(control_result) .. " "
                .. describe(control_detail) .. "; at " .. at(control_at) .. " (want x >= 2144)"
            if regicide_value ~= 2 then
                return "no_subject", text .. " -- the Regicide stage did not take"
            end
            if goto_result ~= "ok" then
                return "no_subject", text
            end
            if type(inside) ~= "table" or inside.x ~= 2014 or inside.z ~= 4712 or inside.level ~= 1 then
                return "refused", text .. " -- [label,open_iban_door] did not take the Regicide player in"
            end
            if type(outside) ~= "table" or outside.x ~= 2145 or outside.z ~= 4648 or outside.level ~= 1 then
                return "refused", text .. " -- the ruined temple's doors did not put the player back outside"
            end
            if type(control_at) ~= "table" or control_at.x < 2144 or control_at.level ~= 1 then
                return "refused", text .. " -- a player without Regicide progress went through"
            end
            return "ok", text
        end)

        -- AN IF1 BUTTON PRESS RUNS THE UNNUMBERED [if_button,...] TRIGGER
        -- (matthew-mbp-m4-b49-seam1 if_button_op_dispatch_for_if1).  A real
        -- click on an IF1 component (`if3=no`) sends the op-less IF_BUTTON and
        -- the server runs only [if_button,<com>] for it, as LostCity's
        -- IfButtonHandler.ts:31 does.  Ratcatchers bound its snake charm's
        -- notes as the IF3 op form [if_button1,ratcatcher_flute:*], so no
        -- press played the tune (seam1_flute_before: music_len stayed 0); the
        -- content now binds [if_button,...].  Graded on the first note of the
        -- tune (D) pressed with op 0 raising varb1421_ratcatch_music_len to 1.
        seam("seam.if1_button_unnumbered_trigger", function()
            local goto_tile = verb("player", "goto_tile")
            local inv_op = verb("player", "inv_op")
            local await_open = verb("ui", "await_open")
            local widget = verb("ui", "widget")
            local invoke = verb("ui", "invoke")
            local await_server = verb("var", "await_server")
            local server = verb("var", "server")
            local key = verb("key")
            if not goto_tile then return missing("player", "goto_tile") end
            if not inv_op then return missing("player", "inv_op") end
            if not await_open then return missing("ui", "await_open") end
            if not widget then return missing("ui", "widget") end
            if not invoke then return missing("ui", "invoke") end
            if not await_server then return missing("var", "await_server") end
            if not server then return missing("var", "server") end
            if not key then return missing("key") end
            setup_cheat("::clearinv")
            setup_cheat("::give snake_flute 1")
            setup_cheat("::give ratcatchers_music 1")
            setup_cheat("::setvar varb1404_ratcatch_var 100")
            settle(2)
            local function teardown()
                key("escape")
                setup_cheat("::setvar varb1421_ratcatch_music_len 0")
                setup_cheat("::setvar varb1404_ratcatch_var 0")
                setup_cheat("::clearinv")
                setup_cheat("::tele lumbridge")
                settle(2)
            end
            local goto_result, goto_detail = goto_tile(3018, 3234, 0)
            if goto_result ~= "ok" then
                teardown()
                return "no_subject", "goto Port Sarim 3018,3234,0 -> " .. describe(goto_result) .. " "
                    .. describe(goto_detail)
            end
            local op_result, op_detail = inv_op("snake_flute", 1)
            local open_result, open_detail = await_open("ratcatcher_flute", 10)
            local widget_result, note = widget("ratcatcher_flute:rc_flute_d")
            local press_result = "not_run"
            local landed = "not_run"
            if open_result == "ok" and widget_result == "ok" then
                press_result = invoke(note, 0)
                landed = await_server("varb1421_ratcatch_music_len", 1, 6)
            end
            local _, length = server("varb1421_ratcatch_music_len")
            teardown()
            local text = "snake_flute op1 -> " .. describe(op_result) .. " " .. describe(op_detail)
                .. "; flute open -> " .. describe(open_result) .. " " .. describe(open_detail)
                .. "; rc_flute_d widget -> " .. describe(widget_result) .. " " .. describe(note)
                .. "; invoke(op 0) -> " .. describe(press_result) .. "; music_len await 1 -> "
                .. describe(landed) .. " (read " .. describe(length) .. ")"
            if open_result ~= "ok" or widget_result ~= "ok" then
                return "no_subject", text .. " -- the snake charm did not open"
            end
            if landed ~= "ok" or length ~= 1 then
                return "refused", text .. " -- the op-0 press did not run [if_button,ratcatcher_flute:rc_flute_d]"
            end
            return "ok", text
        end)

        -- THE TROLLWEISS CAVE MOUTH AND CREVICE ARE MAPLINKS AGAIN
        -- (matthew-mbp-m4-b49-seam1 troll_love_arrg_and_sleds).
        -- curseofarrav.rs2 binds [oploc1,trollromance_caveentrance] and
        -- [oploc1,trollromance_snow_cavewall_crevis] by name for its own
        -- soft-skip; that name binding shadows [oploc1,_maplink_transition]
        -- (maplink.rs2:77), and outside Curse of Arrav it only said "A snowy
        -- cave.", so Troll Romance could not enter or leave the cave and the
        -- sled was never worn (tlseam_caves_before 12/6).  It now falls
        -- through to ~maplink_transition (maplink.dbrow 0_44_58_6_31..33 ->
        -- 0_43_159_51_11, 0_43_159_20_56 -> 0_43_60_26_29).  Graded on the
        -- tile after each click: in at 2803,10187,0 and out at 2778,3869,0.
        seam("seam.trollweiss_cave_maplink_not_shadowed", function()
            local goto_tile = verb("player", "goto_tile")
            local click_loc = verb("player", "click_loc")
            local tile = verb("world", "tile")
            if not goto_tile then return missing("player", "goto_tile") end
            if not click_loc then return missing("player", "click_loc") end
            if not tile then return missing("world", "tile") end
            local goto_result, goto_detail = goto_tile(2822, 3744, 0)
            if goto_result ~= "ok" then
                setup_cheat("::tele lumbridge")
                settle(2)
                return "no_subject", "goto 2822,3744,0 -> " .. describe(goto_result) .. " " .. describe(goto_detail)
            end
            local enter_result, enter_detail = click_loc("trollromance_caveentrance", 1)
            settle(4)
            local _, inside = tile()
            local leave_result, leave_detail = "not_run", nil
            local outside = nil
            if type(inside) == "table" and inside.x == 2803 and inside.z == 10187 then
                local back_result = goto_tile(2772, 10232, 0)
                if back_result == "ok" then
                    leave_result, leave_detail = click_loc("trollromance_snow_cavewall_crevis", 1)
                    settle(4)
                    local _, here = tile()
                    outside = here
                end
            end
            setup_cheat("::tele lumbridge")
            settle(2)
            local function at(where)
                if type(where) ~= "table" then return describe(where) end
                return describe(where.x) .. "," .. describe(where.z) .. "," .. describe(where.level)
            end
            local text = "cave mouth -> " .. describe(enter_result) .. " " .. describe(enter_detail) .. "; at "
                .. at(inside) .. " (want 2803,10187,0); crevice -> " .. describe(leave_result) .. " "
                .. describe(leave_detail) .. "; at " .. at(outside) .. " (want 2778,3869,0)"
            if type(inside) ~= "table" or inside.x ~= 2803 or inside.z ~= 10187 or inside.level ~= 0 then
                return "refused", text .. " -- the cave mouth did not take the maplink"
            end
            if type(outside) ~= "table" or outside.x ~= 2778 or outside.z ~= 3869 or outside.level ~= 0 then
                return "refused", text .. " -- the crevice did not take the maplink"
            end
            return "ok", text
        end)

        -- ZEMBO SELLS KARAMJAN RUM AT MUSA POINT (matthew-mbp-m4-b49-seam1
        -- tbwt_zembo_spawn_and_shop).  No square or script placed Zembo, so
        -- Tai Bwo Wannai Trio's getRum answered `no_row zembo` (tbwt ledger
        -- row 41).  quest_tbwt/ now carries his spawn (LostCity m45_49.jm2
        -- `0 45 7: zambo` = 2925,3143,0), his boozeshop stock (karamja.inv)
        -- and zambo.rs2's Talk-to/Trade.  Graded on op3 Trade opening the
        -- boozeshop and one karamja_rum landing in the backpack, paid for.
        seam("seam.zembo_boozeshop_sells_rum", function()
            local goto_tile = verb("player", "goto_tile")
            local open = verb("shop", "open")
            local buy = verb("shop", "buy")
            local close = verb("shop", "close")
            local count = verb("inv", "count")
            local await = verb("inv", "await")
            if not goto_tile then return missing("player", "goto_tile") end
            if not open then return missing("shop", "open") end
            if not buy then return missing("shop", "buy") end
            if not close then return missing("shop", "close") end
            if not count then return missing("inv", "count") end
            if not await then return missing("inv", "await") end
            setup_cheat("::clearinv")
            setup_cheat("::give coins 100")
            settle(2)
            local goto_result, goto_detail = goto_tile(2924, 3143, 0)
            if goto_result ~= "ok" then
                setup_cheat("::clearinv")
                setup_cheat("::tele lumbridge")
                settle(2)
                return "no_subject", "goto Musa Point 2924,3143,0 -> " .. describe(goto_result) .. " "
                    .. describe(goto_detail)
            end
            local open_result, open_detail = open("zembo", 3, "boozeshop")
            local buy_result, buy_detail = "not_run", nil
            local landed = "not_run"
            local _, coins_before = count("coins")
            if open_result == "ok" then
                buy_result, buy_detail = buy("karamja_rum", 1)
                landed = await("karamja_rum", 1, 10)
            end
            local _, coins_after = count("coins")
            close()
            setup_cheat("::clearinv")
            setup_cheat("::tele lumbridge")
            settle(2)
            local reading = "shop.open(zembo, 3, boozeshop) -> " .. describe(open_result) .. " "
                .. describe(open_detail) .. "; buy -> " .. describe(buy_result) .. " " .. describe(buy_detail)
                .. "; karamja_rum await " .. describe(landed) .. ", coins " .. describe(coins_before)
                .. " -> " .. describe(coins_after)
            if open_result ~= "ok" then
                return open_result, reading
            end
            if buy_result ~= "ok" then
                return buy_result, reading
            end
            if landed ~= "ok" or type(coins_before) ~= "number" or type(coins_after) ~= "number"
                or coins_after >= coins_before then
                return "hollow", reading
            end
            return "ok", reading
        end)

        -- DWARF CANNON'S TOOLKIT AND GRIM TALES' PIANO TAKE THE IF1 PRESS
        -- (matthew-mbp-m4-b49-seam2 if1_buttons_bound_as_if3).  Both
        -- interfaces are IF1 (`if3=no`), so a click sends the op-less
        -- IF_BUTTON and the server runs only [if_button,<com>] (LostCity
        -- IfButtonHandler.ts:31); the content bound them [if_button1,...], so
        -- mcannon was green only on an op-1 press no click makes and no piano
        -- key ever played (seam2_mcannon_before / seam2_piano_before).  Graded
        -- on the op-0 press landing: the hook selected
        -- (varb2237_mcannonmulti_tool3 = 1) and the piano's first key
        -- advancing varb3697_grim_pianotrack to 1.
        seam("seam.if1_toolkit_and_piano_op0", function()
            local goto_tile = verb("player", "goto_tile")
            local by_symbol = verb("player", "by_symbol")
            local use_on = verb("player", "use_on")
            local click_loc = verb("player", "click_loc")
            local await_open = verb("ui", "await_open")
            local widget = verb("ui", "widget")
            local invoke = verb("ui", "invoke")
            local await_server = verb("var", "await_server")
            local server = verb("var", "server")
            local key = verb("key")
            if not goto_tile then return missing("player", "goto_tile") end
            if not by_symbol then return missing("player", "by_symbol") end
            if not use_on then return missing("player", "use_on") end
            if not click_loc then return missing("player", "click_loc") end
            if not await_open then return missing("ui", "await_open") end
            if not widget then return missing("ui", "widget") end
            if not invoke then return missing("ui", "invoke") end
            if not await_server then return missing("var", "await_server") end
            if not server then return missing("var", "server") end
            if not key then return missing("key") end
            setup_cheat("::clearinv")
            setup_cheat("::give mcannontoolkit 1")
            setup_cheat("::setvar varp0_mcannon 6")
            setup_cheat("::setvar varb3694_grim_dwarfquest 20")
            settle(2)
            local function teardown()
                key("escape")
                setup_cheat("::setvar varb2237_mcannonmulti_tool3 0")
                setup_cheat("::setvar varp0_mcannon 0")
                setup_cheat("::setvar varb3697_grim_pianotrack 0")
                setup_cheat("::setvar varb3694_grim_dwarfquest 0")
                setup_cheat("::clearinv")
                setup_cheat("::tele lumbridge")
                settle(2)
            end
            local goto_result, goto_detail = goto_tile(2564, 3461, 0)
            if goto_result ~= "ok" then
                teardown()
                return "no_subject", "goto the cannon 2564,3461,0 -> " .. describe(goto_result) .. " "
                    .. describe(goto_detail)
            end
            local cannon = by_symbol("loc", "mcannon_cannon_multiloc")
            local use_result, use_detail = use_on("mcannontoolkit", cannon)
            local open_result = await_open("mcannon_interface", 10)
            local hook_result, hook = widget("mcannon_interface:mcannon_tool3")
            local hook_landed = "not_run"
            if open_result == "ok" and hook_result == "ok" then
                invoke(hook, 0)
                hook_landed = await_server("varb2237_mcannonmulti_tool3", 1, 6)
            end
            local _, tool3 = server("varb2237_mcannonmulti_tool3")
            key("escape")
            settle(2)
            local piano_goto = goto_tile(2903, 9874, 0)
            local play_result, play_detail = "not_run", nil
            local piano_open, key_result, piano_key = "not_run", "not_run", nil
            local key_landed = "not_run"
            if piano_goto == "ok" then
                play_result, play_detail = click_loc("grim_grandpiano", 1)
                piano_open = await_open("grim_piano", 10)
                key_result, piano_key = widget("grim_piano:ue")
                if piano_open == "ok" and key_result == "ok" then
                    invoke(piano_key, 0)
                    key_landed = await_server("varb3697_grim_pianotrack", 1, 6)
                end
            end
            local _, track = server("varb3697_grim_pianotrack")
            teardown()
            local text = "toolkit on cannon -> " .. describe(use_result) .. " " .. describe(use_detail)
                .. "; mcannon_interface open -> " .. describe(open_result) .. "; mcannon_tool3 invoke(op 0) -> tool3 await "
                .. describe(hook_landed) .. " (read " .. describe(tool3) .. "); piano goto -> " .. describe(piano_goto)
                .. "; grim_grandpiano op1 -> " .. describe(play_result) .. " " .. describe(play_detail)
                .. "; grim_piano open -> " .. describe(piano_open) .. "; ue invoke(op 0) -> pianotrack await "
                .. describe(key_landed) .. " (read " .. describe(track) .. ")"
            if open_result ~= "ok" or hook_result ~= "ok" or piano_open ~= "ok" or key_result ~= "ok" then
                return "no_subject", text .. " -- an interface did not open"
            end
            if hook_landed ~= "ok" or tool3 ~= 1 then
                return "refused", text .. " -- the op-0 press did not run [if_button,mcannon_interface:mcannon_tool3]"
            end
            if key_landed ~= "ok" or track ~= 1 then
                return "refused", text .. " -- the op-0 press did not run [if_button,grim_piano:ue]"
            end
            return "ok", text
        end)

        -- TAI BWO WANNAI TRIO'S VESSEL TAKES ONE KARAMBWANJI
        -- (matthew-mbp-m4-b49-seam2 tbwt_completion_rewards).  This cache's
        -- raw karambwanji is stackable and the load deleted the whole slot,
        -- so one vessel ate the stack; OSRS wiki "Raw karambwanji" (oldid
        -- 15184350) loads one.  Graded on both use orders: 5 -> 4 -> 3.
        seam("seam.tbwt_vessel_loads_one_karambwanji", function()
            local use_item_on_item = verb("player", "use_item_on_item")
            local count = verb("inv", "count")
            local await = verb("inv", "await")
            if not use_item_on_item then return missing("player", "use_item_on_item") end
            if not count then return missing("inv", "count") end
            if not await then return missing("inv", "await") end
            setup_cheat("::clearinv")
            setup_cheat("::give tbwt_raw_karambwanji 5")
            setup_cheat("::give tbwt_karambwan_vessel 2")
            settle(2)
            local first_result, first_detail = use_item_on_item("tbwt_raw_karambwanji", "tbwt_karambwan_vessel")
            local first_landed = await("tbwt_karambwan_vessel_loaded_with_karambwanji", 1, 10)
            local _, after_first = count("tbwt_raw_karambwanji")
            local second_result = use_item_on_item("tbwt_karambwan_vessel", "tbwt_raw_karambwanji")
            local second_landed = await("tbwt_karambwan_vessel_loaded_with_karambwanji", 2, 10)
            local _, after_second = count("tbwt_raw_karambwanji")
            setup_cheat("::clearinv")
            settle(2)
            local text = "karambwanji on vessel -> " .. describe(first_result) .. " " .. describe(first_detail)
                .. "; loaded await 1 -> " .. describe(first_landed) .. ", karambwanji 5 -> " .. describe(after_first)
                .. "; vessel on karambwanji -> " .. describe(second_result) .. "; loaded await 2 -> "
                .. describe(second_landed) .. ", karambwanji -> " .. describe(after_second)
            if first_landed ~= "ok" or second_landed ~= "ok" then
                return "no_subject", text .. " -- a vessel did not load"
            end
            if after_first ~= 4 or after_second ~= 3 then
                return "refused", text .. " -- a load did not take exactly one karambwanji (want 4 then 3)"
            end
            return "ok", text
        end)

        -- FILL ON AN EMPTY SACK MAKES POTATOES(10) (matthew-mbp-m4-b50-seam1
        -- enlightenedjourney_gather_sources).  The sacks' ifop1 Fill was unbound,
        -- so `sack_potato_10` -- Enlightened Journey's sack of potatoes -- had no
        -- source in the pack.  Wiki Empty sack (oldid 15183845): Fill puts 10
        -- vegetables in at once.  skill_farming/scripts/farming_sacks.rs2.
        -- Graded on 12 potatoes -> one Potatoes(10) and 2 potatoes left.
        seam("seam.vegetable_sack_fill", function()
            local inv_op = verb("player", "inv_op")
            local count = verb("inv", "count")
            local await = verb("inv", "await")
            if not inv_op then return missing("player", "inv_op") end
            if not count then return missing("inv", "count") end
            if not await then return missing("inv", "await") end
            setup_cheat("::clearinv")
            setup_cheat("::give sack_empty 1")
            setup_cheat("::give potato 12")
            settle(2)
            -- The press swaps the sack for another obj, so inv_op's own wait
            -- answers timeout; the new obj's count is the evidence.
            local op_result, op_detail = inv_op("sack_empty", 1)
            local landed = await("sack_potato_10", 1, 10)
            local _, sacks = count("sack_potato_10")
            local _, potatoes = count("potato")
            local _, empties = count("sack_empty")
            setup_cheat("::clearinv")
            settle(2)
            local text = "Fill on sack_empty -> " .. describe(op_result) .. " " .. describe(op_detail)
                .. "; sack_potato_10 await -> " .. describe(landed) .. " (read " .. describe(sacks)
                .. "), potato 12 -> " .. describe(potatoes) .. ", sack_empty 1 -> " .. describe(empties)
            if landed ~= "ok" or sacks ~= 1 then
                return "refused", text .. " -- Fill did not make Potatoes(10)"
            end
            if potatoes ~= 2 or empties ~= 0 then
                return "refused", text .. " -- Fill did not take exactly 10 potatoes and the empty sack"
            end
            return "ok", text
        end)

        -- A SECOND COPY DROPPED ON ITS TWIN'S TILE IS A DROP
        -- (matthew-mbp-m4-b52-seam1 drop_verb_grades_on_ground_count).
        -- player.drop graded on the ground count RISING, and the client keeps
        -- one ground row per (tile, obj id) whose count an OBJ_ADD overwrites
        -- (App_WorldObjStackAdd, src/app/app_world_rebuild.c:172), so the
        -- second of two non-stackable logs dropped where the player stands
        -- read `timeout ... backpack 1 -> 0, ground 1` though it left the
        -- backpack (legends b51 makeBowl.drop-spare-bar-2;
        -- build/quest_gate/dropseam_before row 3).  The client merge itself is
        -- gone since b53-seam1 (seam.two_copies_one_tile_both_takeable below):
        -- the second drop now reads `ground ... 1 -> 2 (2 row(s))`, and this
        -- row still holds the backpack grading.  Graded: both drops answer
        -- ok, the backpack goes 2 -> 1 -> 0, and the tile still shows logs.
        seam("seam.drop_second_copy_on_one_tile", function()
            local drop = verb("player", "drop")
            local count = verb("inv", "count")
            if not drop then return missing("player", "drop") end
            if not count then return missing("inv", "count") end
            setup_cheat("::clearinv")
            setup_cheat("::give logs 2")
            settle(2)
            local _, before = count("logs")
            local first_result, first_detail = drop("logs")
            local _, after_first = count("logs")
            settle(1)
            local second_result, second_detail = drop("logs")
            local _, after_second = count("logs")
            local text = "logs " .. describe(before) .. "; drop 1 -> " .. describe(first_result) .. " "
                .. describe(first_detail) .. " (backpack " .. describe(after_first) .. "); drop 2 -> "
                .. describe(second_result) .. " " .. describe(second_detail) .. " (backpack "
                .. describe(after_second) .. ")"
            if before ~= 2 then
                return "no_subject", text .. " -- ::give logs 2 did not put two logs in the backpack"
            end
            if after_first ~= 1 or after_second ~= 0 then
                return "no_subject", text .. " -- the world did not drop one log per press"
            end
            if first_result ~= "ok" or second_result ~= "ok" then
                return "refused", text .. " -- a real drop was graded as a failure"
            end
            if type(second_detail) ~= "string"
                or string.find(second_detail, "backpack 1 -> 0", 1, true) == nil
                or string.find(second_detail, "-> 0 (", 1, true) ~= nil then
                return "hollow", text .. " -- the second drop's ok did not name backpack 1 -> 0 with logs on the tile"
            end
            return "ok", text
        end)

        -- TWO COPIES OF ONE OBJ ON ONE TILE ARE TWO GROUND ROWS
        -- (matthew-mbp-m4-b53-seam1 drop_verb_followups_and_grip_lure).  The
        -- client used to find the tile's stack of an obj id and overwrite its
        -- count on every OBJ_ADD (App_WorldObjStackAdd,
        -- src/app/app_world_rebuild.c), so two logs dropped on one tile were
        -- ONE row, and the first Take's OBJ_DEL removed it while the server
        -- still held the second log: the tile drew nothing and no menu row
        -- could take it (build/quest_gate/b53s1_pick2_before: `total 1 in 1
        -- row(s)`, then `pick.two ... menu has no row for it`).  The tile is a
        -- list in both references: LostCity_JavaClient Client.java:8206-8228
        -- (OBJ_ADD pushes a new ClientObj, OBJ_DEL unlinks the first of the id)
        -- and the rev-239 deob (Statics.method1385 appends a TileItem,
        -- method6879 unlinks one).  Graded on the private pool reading (two
        -- rows, total 2), then on each Take landing one log and the second
        -- still being on the ground between them.  The drops are made on a
        -- tile of their own, 3241,3245 (open field two south of CAST_TILE, the
        -- seam21 goblin stand), so the logs the row above left on its tile are
        -- not counted.
        seam("seam.two_copies_one_tile_both_takeable", function()
            local goto_tile = verb("player", "goto_tile")
            local drop = verb("player", "drop")
            local by_symbol = verb("player", "by_symbol")
            local ground_on_tile = verb("player", "_ground_on_tile")
            local click_obj = verb("player", "click_obj")
            local obj_near = verb("world", "obj_near")
            local count = verb("inv", "count")
            if not goto_tile then return missing("player", "goto_tile") end
            if not drop then return missing("player", "drop") end
            if not by_symbol then return missing("player", "by_symbol") end
            if not ground_on_tile then return missing("player", "_ground_on_tile") end
            if not click_obj then return missing("player", "click_obj") end
            if not obj_near then return missing("world", "obj_near") end
            if not count then return missing("inv", "count") end
            setup_cheat("::clearinv")
            setup_cheat("::give logs 2")
            settle(2)
            goto_tile(CAST_TILE_X, CAST_TILE_Z - 2, 0)
            local target = by_symbol("obj", "logs")
            local obj_id = type(target) == "table" and target.id or nil
            if obj_id == nil then
                setup_cheat("::clearinv")
                return "no_subject", "player.by_symbol(obj, logs) named no obj id"
            end
            local first_result = drop("logs")
            settle(1)
            local second_result = drop("logs")
            settle(2)
            local total, rows = ground_on_tile(obj_id)
            local trail = { "drops -> " .. describe(first_result) .. "/" .. describe(second_result)
                .. ", ground on the tile: total " .. describe(total) .. " in " .. describe(rows) .. " row(s)" }
            local take_one = click_obj("logs", 3)
            settle(3)
            local _, held_one = count("logs")
            local left_result = obj_near("logs", 1)
            trail[#trail + 1] = "take 1 -> " .. describe(take_one) .. ", held " .. describe(held_one)
                .. ", ground -> " .. describe(left_result)
            local take_two = "not_run"
            if left_result == "ok" then
                take_two = click_obj("logs", 3)
                settle(3)
            end
            local _, held_two = count("logs")
            local gone_result = obj_near("logs", 1)
            trail[#trail + 1] = "take 2 -> " .. describe(take_two) .. ", held " .. describe(held_two)
                .. ", ground -> " .. describe(gone_result)
            setup_cheat("::clearinv")
            settle(1)
            local text = table.concat(trail, "; ")
            if first_result ~= "ok" or second_result ~= "ok" then
                return "no_subject", text .. " -- the two logs were not both dropped"
            end
            if rows ~= 2 or total ~= 2 then
                return "refused", text .. " -- two identical drops on one tile must read two ground rows"
            end
            if held_one ~= 1 or left_result ~= "ok" then
                return "refused", text .. " -- the first Take left no second log on the ground"
            end
            if held_two ~= 2 or gone_result == "ok" then
                return "refused", text .. " -- the second log could not be taken"
            end
            return "ok", text
        end)

        -- A DRAINED STAT STAYS DRAINED THROUGH AN XP GRANT
        -- (matthew-mbp-m4-b53-seam2 stat_drain_survives_xp_gain).
        -- ToriRSServer_CombatAddXp (src/torirsserver/torirs_server_combat.c)
        -- snapped any current level below its base straight back to the base
        -- on the next grant, so one hit cancelled every content drain (the Ice
        -- Path's cold, the Sourhog's spit, a stat_sub).  LostCity Player.ts:
        -- 1841-1851 (addXp) moves the current level with the base only while
        -- they are equal.  Graded: Attack 60 drained 90% reads 6/60, and a
        -- 10 xp `::xp` grant (stat_advance) leaves it 6/60 with the xp up.
        -- The row puts Attack back where it found it.
        seam("seam.drain_survives_xp_gain", function()
            local read = verb("skill", "read")
            if not read then return missing("skill", "read") end
            local function reading()
                local state, value = read("attack")
                if state ~= "ok" or type(value) ~= "table" then
                    return nil
                end
                return value
            end
            local start = reading()
            if start == nil then
                return "no_subject", "skill.read attack gave no reading"
            end
            setup_cheat("::setlevel attack 60")
            setup_cheat("::drain attack 0 90")
            settle(2)
            local drained = reading()
            setup_cheat("::xp attack 100")
            settle(2)
            local after = reading()
            setup_cheat("::setlevel attack " .. tostring(start.base_level))
            settle(2)
            local restored = reading()
            local function show(value)
                if value == nil then
                    return "nil"
                end
                return tostring(value.level) .. "/" .. tostring(value.base_level) .. " xp " .. tostring(value.experience)
            end
            local text = "attack " .. show(start) .. "; ::setlevel 60 + ::drain 90% -> " .. show(drained)
                .. "; ::xp attack 100 -> " .. show(after) .. "; put back -> " .. show(restored)
            if drained == nil or drained.level ~= 6 or drained.base_level ~= 60 then
                return "no_subject", text .. " -- the drain did not stage 6/60"
            end
            if after == nil or after.experience <= drained.experience then
                return "no_subject", text .. " -- the grant moved no xp"
            end
            if after.level ~= 6 then
                return "refused", text .. " -- the xp grant undid the drain"
            end
            return "ok", text
        end)

        -- PLACED AFTER seam.drain_survives_xp_gain (waves seam pass 1, driver_port).
        -- In the raid branch these three rows sit before the mapzone row.  There they
        -- add ~123 ticks between session.relog and seam.drain_survives_xp_gain, and
        -- that row then straddles a [timer,stat_restore] tick (player/scripts/
        -- stat_restore.rs2: one level back every 100 ticks from login), reading 7/60
        -- for its staged 6/60 (build/quest_gate/qdconform_port_run1: relog -> drain
        -- row = 397 ticks; v3's own order 274).  Here they cannot move its phase.

        -- ONE COPY ASKED, ONE COPY PRESSED (raid seam3 attack_exact_copy).  Two
        -- goblins spawned on ONE tile (::spawn twice from the same player tile
        -- lands both on player.x+1, player.z+1, torirs_server_world.c's spawn
        -- cheat) draw over each other, and a press names one of them by slot.
        -- Graded from the client: the asked copy is hit (a health bar, or gone
        -- inside the settle) and the wait holds THAT slot, while the other copy
        -- never carries a bar -- a client is sent a HEADBAR only once something
        -- has hit the npc, so health_ratio < 0 is "nothing ever hit it".
        -- Scratch proof with the server's tick log: s3ec_after1 "asked world
        -- slot 1080 ...: 8 hit_npc row(s) on it, 0 on the other copy (world
        -- 1079)".  The press-into-an-open-menu half (a press naming B landing
        -- inside the menu a covered press left over A SELECTED A's Attack row:
        -- s3ec_stale_before1 5 hit_npc rows on A, 0 on B) is OPEN: its fix cost
        -- three green quests a tick and the seam3 closer reverted it
        -- (raid_loop/CONTENT_BUGS.md); the repro stays a scratch
        -- (build/seam_state/matthew-mbp-m4-raid-b1-seam3/scratch/stale_menu.lua).
        seam("seam.attack_exact_copy_on_one_tile", function()
            local goto_tile = verb("player", "goto_tile")
            local attack = verb("player", "attack")
            local engaged = verb("npc", "await_dead_engaged")
            local tiles = verb("npc", "tiles")
            if not goto_tile then return missing("player", "goto_tile") end
            if not attack then return missing("player", "attack") end
            if not engaged then return missing("npc", "await_dead_engaged") end
            if not tiles then return missing("npc", "tiles") end
            local GOBLIN = "goblin_unarmed_melee_1"
            -- The stage before the Dayth rows set ranged 99: one arrow would
            -- kill a goblin inside the attack's own settle, and a fight that is
            -- several hits long is the subject here.  Put back on every exit.
            setup_cheat("::setlevel ranged 1")
            local function teardown()
                setup_cheat("::kill " .. GOBLIN .. " 10")
                setup_cheat("::kill " .. GOBLIN .. " 10")
                setup_cheat("::setlevel ranged 99")
                settle(2)
            end
            local goto_result = goto_tile(3229, 3233, 0)
            if goto_result ~= "ok" then
                teardown()
                return "no_subject", "goto 3229,3233 -> " .. describe(goto_result)
            end
            setup_cheat("::spawn " .. GOBLIN)
            settle(1)
            setup_cheat("::spawn " .. GOBLIN)
            settle(3)
            local _, _, rows = tiles(GOBLIN, 3)
            local copies = {}
            for _, row in ipairs(rows or {}) do
                if row.x == 3230 and row.z == 3234 then copies[#copies + 1] = row end
            end
            if #copies < 2 then
                teardown()
                return "no_subject", "two ::spawn " .. GOBLIN .. " did not put two copies on 3230,3234 ("
                    .. #copies .. ")"
            end
            table.sort(copies, function(x, y) return x.slot < y.slot end)
            local other, asked = copies[1], copies[2]
            local result, detail = attack(GOBLIN, 2, 20, { slot = asked.slot })
            local text = "two copies on 3230,3234: asked slot " .. asked.slot .. " (element "
                .. describe(asked.element_id) .. "), other slot " .. other.slot .. " (element "
                .. describe(other.element_id) .. "); attack -> " .. describe(result) .. " " .. describe(detail)
            if result ~= "ok" and result ~= "timeout" then
                teardown()
                return result, text
            end
            if string.find(tostring(detail), "pressed slot " .. asked.slot .. " ", 1, true) == nil then
                teardown()
                return "hollow", text .. " -- the detail does not name the asked slot as the one pressed"
            end
            local wait_result, wait_detail = engaged(60, 4)
            text = text .. "; await_dead_engaged -> " .. describe(wait_result) .. " " .. describe(wait_detail)
            -- The whole pool (radius 0): a spawned goblin wanders off its tile.
            local _, _, after = tiles(GOBLIN, 0)
            local other_now = nil
            for _, row in ipairs(after or {}) do
                if row.slot == other.slot then other_now = row end
            end
            teardown()
            if wait_result ~= "ok" or string.find(tostring(wait_detail), "slot " .. asked.slot .. " ", 1, true) == nil then
                return "refused", text .. " -- the wait did not finish the asked slot"
            end
            if other_now == nil then
                return "refused", text .. " -- the copy NOT asked for left the pool too"
            end
            if type(other_now.health_ratio) ~= "number" or other_now.health_ratio >= 0 then
                return "refused", text .. " -- the copy NOT asked for carries a health bar ("
                    .. describe(other_now.health_ratio) .. "/" .. describe(other_now.health_scale)
                    .. "): something hit the other copy"
            end
            return "ok", text .. " [other slot " .. other.slot .. " never hit: health_ratio "
                .. describe(other_now.health_ratio) .. "]"
        end)

        -- AN NPC'S FOOTPRINT, READ FROM THE PLAYER'S SIDE (raid seam4
        -- npc_state_size_and_stale_menu).  Before, a pool row carried no size,
        -- so Xarpus's 3 -> 5 (xarpus.size.p1_p2) had to be looked up by hand
        -- and a footprint guessed.  Graded on two ordinary npcs the cache
        -- sizes differently: goblin_unarmed_melee_1 (configs/all.npc states no
        -- size: 1) and cow (`size=2`), each read through t.npc.state and
        -- t.npc.nearest (the same pool row), and state_text naming it.
        -- Scratch proof: s4size_after1 (goblin 1, cow 2; the shared binary
        -- without the field, s4size_before1, reads nil); s4xsize_after1 Entry
        -- Xarpus: static form 3, fighting form (npc 10768) 5.  Note the
        -- triage's "cow (1)" is wrong: the cache states cow size=2, which makes
        -- the cow the ordinary size-2 npc this row wanted.
        seam("seam.npc_state_size", function()
            local state = verb("npc", "state")
            local nearest = verb("npc", "nearest")
            local state_text = verb("npc", "state_text")
            local goto_tile = verb("player", "goto_tile")
            if not state then return missing("npc", "state") end
            if not nearest then return missing("npc", "nearest") end
            if not state_text then return missing("npc", "state_text") end
            if not goto_tile then return missing("player", "goto_tile") end
            local GOBLIN = "goblin_unarmed_melee_1"
            local COW = "cow"
            local function teardown()
                setup_cheat("::kill " .. GOBLIN .. " 10")
                setup_cheat("::kill " .. COW .. " 10")
                settle(2)
            end
            local goto_result = goto_tile(3229, 3233, 0)
            if goto_result ~= "ok" then
                teardown()
                return "no_subject", "goto 3229,3233 -> " .. describe(goto_result)
            end
            setup_cheat("::spawn " .. GOBLIN)
            settle(1)
            goto_result = goto_tile(3225, 3233, 0)
            if goto_result ~= "ok" then
                teardown()
                return "no_subject", "goto 3225,3233 -> " .. describe(goto_result)
            end
            setup_cheat("::spawn " .. COW)
            settle(3)
            local text = {}
            for _, want in ipairs({ { GOBLIN, 1 }, { COW, 2 } }) do
                local symbol, size = want[1], want[2]
                local near_result, near = nearest(symbol, 8)
                if near_result ~= "ok" then
                    teardown()
                    return "no_subject", symbol .. ": npc.nearest -> " .. describe(near_result) .. " " .. describe(near)
                end
                local state_result, row = state(symbol, { slot = near.slot })
                if state_result ~= "ok" then
                    teardown()
                    return state_result, symbol .. ": npc.state -> " .. describe(row)
                end
                local line = state_text(row)
                text[#text + 1] = symbol .. ": nearest size " .. describe(near.size) .. ", " .. line
                if near.size ~= size or row.size ~= size then
                    teardown()
                    return "refused", table.concat(text, "; ") .. " -- the cache sizes " .. symbol .. " "
                        .. size
                end
                if not string.find(line, "size " .. size, 1, true) then
                    teardown()
                    return "hollow", table.concat(text, "; ") .. " -- state_text does not name 'size " .. size .. "'"
                end
            end
            teardown()
            return "ok", table.concat(text, "; ")
        end)

        -- ===================================================================
        -- raid seam5 attack_fast_path.
        --
        -- NO step row changes: t.player.attack, t.player.cast and
        -- t.npc.await_dead* keep their signatures, and every existing call in
        -- _conformance.lua passes the default ticks (or 20/40) and no
        -- opts.quick, so it takes the quest press exactly as before -- the
        -- rows step("player.attack"), step("npc.await_dead"),
        -- step("npc.await_dead_engaged"), step("player.cast"),
        -- seam("seam.attack_presses_the_watched_slot"),
        -- seam("seam.cast_presses_the_named_copy") and
        -- seam("seam.attack_exact_copy_on_one_tile") are unchanged (they ran
        -- PASS in the fixer's run of _conformance.lua with this snippet inserted,
        -- under conformance.py's own client environment,
        -- build/quest_gate/s5fp_conf_rows3).
        --
        -- (1) NEW SEAM ROW. PLACE: in _conformance.lua's PLAN directly AFTER
        -- seam("seam.npc_state_size", ...) (the raid seam4 row; it leaves the
        -- character in the Lumbridge goblin field at 3225,3233 with the
        -- stage's kit and ::god on, and kills its own npcs on every exit).
        -- SEAM row: SEAM_COUNT +1, @seam-count +1.
        --
        -- A PRESS THAT LANDS IN A TICK OR TWO, OR ANSWERS (raid seam5
        -- attack_fast_path).  t.player.attack / t.player.cast with ticks <= 2
        -- (or opts.quick = true) press through QD.drive._press_quick
        -- (pointer.lua, end of file): one aim, one press, on `covered`
        -- exactly one re-aim (the copy's new tile, a line hunt through the
        -- missed pixel, or a camera nudge) and one more press -- never the
        -- cover recovery, never walk_near, no hunt past one tick an aim --
        -- and npc.await_dead_engaged re-presses a fast fight the same way.
        -- Graded on three goblins spawned in the field, each fought BY SLOT:
        --   A  attack(ticks=1), the player walks off, and await_dead_engaged
        --      re-presses it: its detail says "N re-engagement(s) (fast path
        --      re-presses)" with N >= 1 (and the re-engagement note carries
        --      the press's own "fast path: ..." account);
        --   B  killed by repeated attack(ticks=1, {slot}): every press says
        --      "fast path:", answers ok/timeout/covered, and spends at most
        --      QUICK_MAX ticks; and one attack with the default ticks on the
        --      same copy does NOT say it (the quest press is untouched);
        --   C  one cast(wind_strike, ticks=1, {slot}) with exactly one air
        --      and one mind rune given for it (the cast spends them): "fast
        --      path:", and Magic XP paid inside 10 ticks (the copy can stand
        --      several tiles off, so the payment can trail the one-tick
        --      settle), then killed by attack(ticks=1).
        -- Scratch proof: build/quest_gate/s5fp_conf_rows3 (this row PASS
        -- beside the unchanged attack/cast/await rows), s5fp_gob_quick6
        -- (8/8), the Nylocas copies s5fp_copy_nylo_quick*/slow*.
        seam("seam.attack_fast_path", function()
            local goto_tile = verb("player", "goto_tile")
            local attack = verb("player", "attack")
            local cast = verb("player", "cast")
            local engaged = verb("npc", "await_dead_engaged")
            local tiles = verb("npc", "tiles")
            local walk_to = verb("player", "walk_to")
            local tile = verb("world", "tile")
            if not goto_tile then return missing("player", "goto_tile") end
            if not attack then return missing("player", "attack") end
            if not cast then return missing("player", "cast") end
            if not engaged then return missing("npc", "await_dead_engaged") end
            if not tiles then return missing("npc", "tiles") end
            if not walk_to then return missing("player", "walk_to") end
            if not tile then return missing("world", "tile") end
            local GOBLIN = "goblin_unarmed_melee_1"
            -- The most server ticks one fast press may spend: aim + press,
            -- the one re-aim (<= 1 tick of hunting) + press, the menu
            -- dismissals.  Measured max 3 (s5fp_copy_nylo_quick1, 72 presses
            -- in the Nylocas room; every goblin press 0-1).
            local QUICK_MAX = 4
            -- As seam.attack_exact_copy_on_one_tile: a fight several hits long.
            setup_cheat("::setlevel ranged 1")
            local function teardown()
                setup_cheat("::kill " .. GOBLIN .. " 12")
                setup_cheat("::kill " .. GOBLIN .. " 12")
                setup_cheat("::kill " .. GOBLIN .. " 12")
                setup_cheat("::setlevel ranged 99")
                settle(2)
            end
            local spots = { { 3229, 3233 }, { 3226, 3236 }, { 3232, 3236 } }
            for i = 1, #spots do
                local goto_result = goto_tile(spots[i][1], spots[i][2], 0)
                if goto_result ~= "ok" then
                    teardown()
                    return "no_subject", "goto " .. spots[i][1] .. "," .. spots[i][2] .. " -> "
                        .. describe(goto_result)
                end
                setup_cheat("::spawn " .. GOBLIN)
            end
            setup_cheat("::passive " .. GOBLIN)
            goto_tile(3229, 3235, 0)
            settle(3)
            local r0, _, rows = tiles(GOBLIN, 4)
            if r0 ~= "ok" or not is_table(rows) or #rows < 3 then
                teardown()
                return "no_subject", "fewer than three goblins within 4 (" .. describe(rows and #rows) .. ")"
            end
            local slots = { rows[1].slot, rows[2].slot, rows[3].slot }
            local function alive(slot)
                local _, _, now = tiles(GOBLIN, 0)
                for _, row in ipairs(now or {}) do
                    if row.slot == slot then return true end
                end
                return false
            end
            local function server_tick()
                local _, tick_now = t.tick()
                return tick_now or 0
            end
            local text = {}
            -- A: open fast, walk off, await_dead_engaged re-presses fast.
            local ra, da = attack(GOBLIN, COMBAT_ATTACK_OP, 1, { slot = slots[1] })
            if (ra ~= "ok" and ra ~= "timeout") or not string.find(tostring(da), "fast path:", 1, true) then
                teardown()
                return ra == "ok" and "hollow" or ra, "A: attack(ticks=1) -> " .. describe(ra) .. " " .. describe(da)
            end
            local _, here = tile()
            if is_table(here) then
                walk_to(here.x - 4, here.z, 8)
            end
            local re, de = engaged(80, 8)
            if re ~= "ok" then
                teardown()
                return re, "A: await_dead_engaged after walking off -> " .. describe(de)
            end
            if string.find(tostring(de), " 0 re-engagement(s)", 1, true)
                or not string.find(tostring(de), "re-engagement(s) (fast path re-presses)", 1, true) then
                teardown()
                return "hollow", "A: killed, but no fast re-engagement named: " .. describe(de)
            end
            text[#text + 1] = "A slot " .. slots[1] .. " re-pressed fast and killed"
            -- B: the quest press on the default ticks says nothing of a fast path.
            local rs, ds = attack(GOBLIN, COMBAT_ATTACK_OP, nil, { slot = slots[2] })
            if string.find(tostring(ds), "fast path:", 1, true) then
                teardown()
                return "refused", "B: attack with the default ticks took the fast path: " .. describe(ds)
            end
            text[#text + 1] = "B default-ticks press " .. describe(rs) .. " (quest press)"
            -- B: killed by fast presses by slot.
            local presses, worst, results = 0, 0, {}
            local start = server_tick()
            while alive(slots[2]) and server_tick() - start < 60 do
                local before = server_tick()
                local rb, db = attack(GOBLIN, COMBAT_ATTACK_OP, 1, { slot = slots[2] })
                local spent = server_tick() - before
                presses = presses + 1
                if spent > worst then worst = spent end
                results[#results + 1] = describe(rb) .. ":" .. spent
                if rb ~= "ok" and rb ~= "timeout" and rb ~= "covered" then
                    if not alive(slots[2]) then break end
                    teardown()
                    return rb, "B: press " .. presses .. " -> " .. describe(db)
                end
                if not string.find(tostring(db), "fast path:", 1, true) then
                    if not alive(slots[2]) then break end
                    teardown()
                    return "hollow", "B: press " .. presses .. " does not name the fast path: " .. describe(db)
                end
                if spent > QUICK_MAX then
                    teardown()
                    return "refused", "B: press " .. presses .. " spent " .. spent .. " ticks (max "
                        .. QUICK_MAX .. "): " .. describe(db)
                end
                t.ticks(2)
            end
            if alive(slots[2]) then
                teardown()
                return "timeout", "B: slot " .. slots[2] .. " alive after 60 ticks of fast presses ("
                    .. table.concat(results, " ") .. ")"
            end
            text[#text + 1] = "B slot " .. slots[2] .. " killed in " .. presses .. " fast press(es), worst "
                .. worst .. " tick(s) [" .. table.concat(results, " ") .. "]"
            -- C: one fast cast with exactly the runes it spends, then fast presses.
            setup_cheat("::give airrune 1")
            setup_cheat("::give mindrune 1")
            settle(2)
            -- Stand two tiles from C first, as a test would: a passive goblin
            -- wanders, and C stood 8 tiles off across the field when A and B
            -- were done (s5fp_conf_rows4..6), where the press landed and the
            -- server never cast -- reach and line of sight are the world's
            -- business, not the press's.
            local c_where = "?"
            do
                local _, _, now_rows = tiles(GOBLIN, 0)
                for _, row in ipairs(now_rows or {}) do
                    if row.slot == slots[3] then
                        goto_tile(row.x - 2, row.z, 0)
                        settle(2)
                    end
                end
                local _, _, again = tiles(GOBLIN, 0)
                local _, me = tile()
                for _, row in ipairs(again or {}) do
                    if row.slot == slots[3] and is_table(me) then
                        c_where = me.x .. "," .. me.z .. " with slot " .. slots[3] .. " at " .. row.x
                            .. "," .. row.z
                    end
                end
            end
            local skill = t.skill
            local read = is_table(skill) and type(skill.read) == "function" and skill.read or nil
            if not read then
                teardown()
                return missing("skill", "read")
            end
            local _, xp_before = read("magic")
            local before_cast = server_tick()
            local rc, dc = cast("wind_strike", GOBLIN, 1, COMBAT_ATTACK_OP, { slot = slots[3] })
            local cast_spent = server_tick() - before_cast
            if (rc ~= "ok" and rc ~= "timeout") or not string.find(tostring(dc), "fast path:", 1, true) then
                teardown()
                return rc == "ok" and "hollow" or rc, "C: cast(ticks=1) from " .. c_where .. " -> "
                    .. describe(rc) .. " " .. string.sub(tostring(dc), 1, 1800)
            end
            -- ticks=1 settles before a copy several tiles off is in range and
            -- paid for, so the PAYMENT is awaited here: Magic XP up (one air
            -- and one mind rune spent with it, ~pvm_spell_cast).
            local paid = t.await({
                level = function()
                    local _, xp_now = read("magic")
                    return is_table(xp_now) and is_table(xp_before)
                        and xp_now.experience > xp_before.experience
                end,
                note = "seam.attack_fast_path: the fast cast paid",
            }, 10)
            if paid ~= "ok" then
                teardown()
                return "timeout", "C: the fast cast from " .. c_where .. " pressed but no Magic XP inside"
                    .. " 10 ticks -- " .. string.sub(tostring(dc), 1, 1500)
            end
            text[#text + 1] = "C cast " .. describe(rc) .. " pressed in " .. cast_spent
                .. " tick(s) from " .. c_where .. ", Magic XP paid"
            start = server_tick()
            while alive(slots[3]) and server_tick() - start < 60 do
                attack(GOBLIN, COMBAT_ATTACK_OP, 1, { slot = slots[3] })
                t.ticks(2)
            end
            if alive(slots[3]) then
                teardown()
                return "timeout", "C: slot " .. slots[3] .. " alive after 60 ticks of fast presses"
            end
            text[#text + 1] = "C slot " .. slots[3] .. " killed"
            teardown()
            return "ok", table.concat(text, "; ")
        end)

        -- AN EAT IS TWO CLOCKS, NEVER A PARK (waves seam3 eat_delay_port, the
        -- raid's content 7936c59bf9 brought over).  Two SEAM rows for the port
        -- (consume_shared.rs2 + food.rs2 + every consumption script).
        -- (1) seam.eat_does_not_hold_queued_hit: a young dark wizard's spell lands on
        --     its own tick through an eat that LANDS on the cast tick or on the plain
        --     hit tick. The eat is aimed from the wizard's own cadence (press at
        --     land-1). The row it replaces pressed only after it read the cast, so the
        --     eat landed after the +1 hit and the row passed on the old content too
        --     (CONTENT_BUGS ENG-58). Proof, waves seam pass 5 (build/quest_gate/):
        --     sf_eat_row_head3 PASS "eat at cast+0 +1, cast+1 +1, cast+1 +1, cast+0 +1"
        --     on HEAD; sf_eat_row_old (HEAD content with the 38 eat-port files of
        --     content c93c574f20 put back to c93c574f20^, in a throwaway worktree,
        --     TORIRSSERVER_CONTENT) FAIL refused "eat at cast+0 -> +2, cast+1 -> +3,
        --     cast+1 -> +3, cast+0 -> +2 -- 4 of 4 eats moved the queued hit".
        --     Wiki Tick eating: "eating a piece of food between the time a monster's
        --     attack calculates its damage and when it hits the player".
        -- (2) seam.eat_delay_clocks: ::eatgate's one-tick answers (food after
        --     food refused, combo after food allowed, combo after combo refused,
        --     potion after food allowed, potion after potion refused, both
        --     refused through +2 = a 3-tick gap, a ready weapon +0, a running
        --     one +3 then +2), then an unarmed goblin fight with a shark eaten
        --     after every second swing: uneaten gaps 4, eaten gaps 7 (wiki
        --     Food/Fast foods: an eat while the weapon delay runs adds 3).
        --     Old content: no ::eatgate (no_row).
        seam("seam.eat_does_not_hold_queued_hit", function()
            local goto_tile = verb("player", "goto_tile")
            local attack = verb("player", "attack")
            local inv_op = verb("player", "inv_op")
            local by_symbol = verb("npc", "by_symbol")
            local tick = verb("tick")
            local count = verb("inv", "count")
            if not goto_tile then return missing("player", "goto_tile") end
            if not attack then return missing("player", "attack") end
            if not inv_op then return missing("player", "inv_op") end
            if not by_symbol then return missing("npc", "by_symbol") end
            if not tick then return missing("tick") end
            if not count then return missing("inv", "count") end
            if type(t.ticklog) ~= "table" or not t.ticklog.rows then return missing("ticklog", "rows") end
            local WIZ = "young_dark_wizard"
            local function now() local _, n = tick() return n or -1 end
            -- The stage before seam.attack_fast_path wields a magic shortbow (ranged 99): it
            -- kills the wizard in a cast or two and leaves no cadence to aim the eats from
            -- (closer's first harness run: one plain cast with a hit). Unarmed, as in the
            -- seam's own proof (sf_eat_row_head3); the bow goes back on at teardown.
            local BOW = "magic_shortbow"
            local equip = verb("player", "equip")
            local unequip = verb("player", "unequip")
            local bow_off = false
            local function teardown()
                setup_cheat("::kill " .. WIZ .. " 10")
                settle(2)
                if bow_off and equip then equip(BOW) end
            end
            if unequip and unequip(BOW) == "ok" then bow_off = true end
            setup_cheat("::setlevel attack 1")
            setup_cheat("::setlevel strength 1")
            setup_cheat("::setlevel defence 1")
            setup_cheat("::setlevel hitpoints 99")
            setup_cheat("::give shark 12")
            local goto_result = goto_tile(3242, 3248, 0)
            if goto_result ~= "ok" then teardown() return "no_subject", "goto 3242,3248 -> " .. describe(goto_result) end
            setup_cheat("::spawn " .. WIZ .. " 1")
            settle(3)
            t.ticklog.start()
            t.ticklog.mark("seam.eat_does_not_hold_queued_hit")
            local _, marks = t.ticklog.rows({ kind = "mark" })
            local since = marks[#marks].serial
            local ws, cast_seq, epoch = nil, nil, since
            -- every cast of the current wizard with its hit: { tick, hit }
            local function casts()
                local out = {}
                if ws == nil then return out end
                local _, an = t.ticklog.rows({ kind = "npc_anim", since = epoch, slot = ws })
                local _, hp = t.ticklog.rows({ kind = "hit_player", since = epoch, slot = ws })
                if cast_seq == nil and #hp > 0 then
                    for _, r in ipairs(an) do if r.tick <= hp[1].tick then cast_seq = r.seq end end
                end
                for _, r in ipairs(an) do
                    if cast_seq ~= nil and r.seq == cast_seq then out[#out + 1] = { tick = r.tick } end
                end
                -- a hit belongs to the newest cast before it (a splash writes no
                -- row, so the next cast's hit must not be paired with it)
                for k, c in ipairs(out) do
                    local limit = math.min(c.tick + 8, out[k + 1] and out[k + 1].tick or c.tick + 8)
                    for _, h in ipairs(hp) do
                        if c.hit == nil and h.tick > c.tick and h.tick <= limit then c.hit = h.tick end
                    end
                end
                return out
            end
            local function engage()
                local nr, nrow = by_symbol(WIZ)
                if nr ~= "ok" then
                    setup_cheat("::spawn " .. WIZ .. " 1")
                    settle(2)
                    nr, nrow = by_symbol(WIZ)
                    if nr ~= "ok" then return false end
                    local _, m2 = t.ticklog.rows({ kind = "npc_spawn", since = since })
                    epoch = (m2[#m2] and m2[#m2].serial) or epoch
                end
                ws = select(2, t.ticklog.slot(nrow))
                for _ = 1, 6 do if attack(WIZ, 2, 20) == "ok" then return true end settle(1) end
                return false
            end
            if not engage() then teardown() return "no_subject", "the young dark wizard was never engaged" end
            -- At least two plain casts that HIT: a splash writes no hit row, and in the
            -- harness's world a run of splashes is common, so wait for up to 120 ticks.
            local function hit_casts()
                local n = 0
                for _, c in ipairs(casts()) do if c.hit then n = n + 1 end end
                return n
            end
            local guard = 0
            while (#casts() < 3 or hit_casts() < 2) and guard < 120 do settle(1) guard = guard + 1 end
            local plain, eaten, held = {}, {}, {}
            for _, c in ipairs(casts()) do
                if c.hit then plain[#plain + 1] = c.hit - c.tick end
            end
            if #plain < 2 then teardown() return "no_subject", "fewer than two plain casts with a hit: " .. #plain end
            -- the queued hit's own offset: the smallest a plain cast shows (the young
            -- dark wizard's is +1; a held hit only ever lands later)
            local plain_off = plain[1]
            for _, d in ipairs(plain) do if d < plain_off then plain_off = d end end
            for i = 1, 6 do
                if #eaten >= 4 then break end
                local cs = casts()
                if #cs < 2 then if not engage() then break end settle(6) cs = casts() end
                if #cs >= 2 then
                    local gap = cs[#cs].tick - cs[#cs - 1].tick
                    if gap <= 0 then gap = 4 end
                    local A = cs[#cs].tick + gap
                    while A - 2 <= now() do A = A + gap end
                    local land_at = A + ((i % 2 == 1) and 0 or plain_off)
                    while now() < land_at - 1 do settle(1) end
                    if now() == land_at - 1 then
                        local _, n0 = count("shark")
                        local press = now()
                        inv_op("shark", 1)
                        settle(8)
                        local _, n1 = count("shark")
                        local got = nil
                        for _, c in ipairs(casts()) do if c.tick == A then got = c end end
                        local txt = "press " .. press .. " eat lands " .. land_at .. " cast " .. describe(got and got.tick or ("none at " .. A))
                            .. " hit " .. describe(got and got.hit)
                        if got and got.hit and n1 == n0 - 1 then
                            local d = got.hit - got.tick
                            txt = txt .. " (+" .. d .. ", eat at cast+" .. (land_at - got.tick) .. ")"
                            eaten[#eaten + 1] = txt
                            if d ~= plain_off then held[#held + 1] = txt end
                        end
                        attack(WIZ, 2, 2)
                    end
                end
            end
            teardown()
            local text = "cast seq " .. describe(cast_seq) .. "; plain +" .. table.concat(plain, ",+") .. " (own +" .. describe(plain_off) .. ")"
                .. "; eats aimed at the cast/hit tick: " .. table.concat(eaten, "; ")
            if #eaten < 2 then return "no_subject", text .. " -- fewer than two eats landed on a predicted cast" end
            if #held > 0 then
                return "refused", text .. " -- " .. #held .. " of " .. #eaten .. " eats moved the queued hit off the plain offset"
                    .. " (the eat's p_delay held the player's queue)"
            end
            return "ok", text
        end)

        seam("seam.eat_delay_clocks", function()
            local goto_tile = verb("player", "goto_tile")
            local attack = verb("player", "attack")
            local inv_op = verb("player", "inv_op")
            local by_symbol = verb("npc", "by_symbol")
            if not goto_tile then return missing("player", "goto_tile") end
            if not attack then return missing("player", "attack") end
            if not inv_op then return missing("player", "inv_op") end
            if not by_symbol then return missing("npc", "by_symbol") end
            if type(t.ticklog) ~= "table" or not t.ticklog.rows then return missing("ticklog", "rows") end
            if type(t.msg) ~= "table" or not t.msg.last then return missing("msg", "last") end
            local GOB = "goblin_unarmed_melee_1"
            -- The stage before seam.attack_fast_path wields a magic shortbow
            -- (ranged 99), which kills a 5-hp goblin with its first arrow and
            -- leaves no swing gap to measure (closer's first full run: 0 gaps
            -- in 166 ticks).  The gap under test is an UNARMED 4, so the bow
            -- comes off for this row and goes back on at its teardown.
            local BOW = "magic_shortbow"
            local equip = verb("player", "equip")
            local unequip = verb("player", "unequip")
            local bow_off = false
            local function line(prefix)
                local _, list = t.msg.last(20)
                for _, m in ipairs(list or {}) do
                    if string.sub(m.text, 1, #prefix) == prefix then return m.text end
                end
                return nil
            end
            local function teardown()
                setup_cheat("::kill " .. GOB .. " 10")
                settle(2)
                if bow_off and equip then equip(BOW) end
            end
            -- (a) the gate procs in one tick (consume_shared.rs2 ::eatgate)
            setup_cheat("::eatgate")
            local g1, g2 = line("eatgate:"), line("eatgate+:")
            local want1 = "food 1 food-after-food 0 combo-after-food 1 combo-after-combo 0 potion-after-food 1 potion-after-potion 0"
            local want2 = "food-refused-through +2 potion-refused-through +2 attack-ready +0 attack-running-2 +7"
            if g1 == nil or g2 == nil then
                return "refused", "no ::eatgate reply (" .. describe(g1) .. " / " .. describe(g2) .. "): consume_shared.rs2 is not in the pack"
            end
            if not string.find(g1, want1, 1, true) or not string.find(g2, want2, 1, true) then
                return "refused", g1 .. " | " .. g2 .. " -- want " .. want1 .. " | " .. want2
            end
            -- (b) an eat while the weapon delay runs adds 3: unarmed (4) swing
            -- gaps, measured hit_npc to hit_npc, with a shark eaten the tick a
            -- swing's hit lands on every second swing
            setup_cheat("::setlevel attack 1")
            setup_cheat("::setlevel strength 1")
            setup_cheat("::setlevel hitpoints 99")
            setup_cheat("::give shark 6")
            -- not_found when no bow is worn (the row run on its own): unarmed already
            if unequip and unequip(BOW) == "ok" then bow_off = true end
            local goto_result = goto_tile(3242, 3248, 0)
            if goto_result ~= "ok" then
                teardown()
                return "no_subject", "goto 3242,3248 -> " .. describe(goto_result)
            end
            setup_cheat("::spawn " .. GOB .. " 1")
            settle(3)
            t.ticklog.start()
            t.ticklog.mark("seam.eat_delay_clocks")
            local _, marks = t.ticklog.rows({ kind = "mark" })
            local since = marks[#marks].serial
            local gs, hcur, guard, engaged, last_hit, ate = nil, since, 0, false, nil, false
            local plain, eaten = {}, {}
            while guard < 160 and #eaten < 2 do
                local nr, nrow = by_symbol(GOB)
                if nr ~= "ok" then
                    setup_cheat("::spawn " .. GOB .. " 1")
                    settle(2)
                    gs, engaged, last_hit, ate = nil, false, nil, false
                else
                    if gs == nil then gs = select(2, t.ticklog.slot(nrow)) end
                    if not engaged then engaged = (attack(GOB, 2, 10) == "ok") end
                end
                settle(1)
                guard = guard + 1
                if gs ~= nil then
                    local _, hn = t.ticklog.rows({ kind = "hit_npc", since = hcur, slot = gs })
                    for i, h in ipairs(hn) do
                        hcur = math.max(hcur, h.serial)
                        if last_hit ~= nil then
                            if ate then eaten[#eaten + 1] = h.tick - last_hit else plain[#plain + 1] = h.tick - last_hit end
                        end
                        last_hit, ate = h.tick, false
                        if i == #hn and (#plain + #eaten) % 2 == 1 then
                            inv_op("shark", 1)
                            ate = true
                        end
                    end
                end
            end
            teardown()
            local text = "eatgate ok; unarmed swing gaps: no eat " .. table.concat(plain, ",")
                .. "; a shark eaten after the swing " .. table.concat(eaten, ",")
            if #plain < 2 or #eaten < 2 then
                return "no_subject", text .. " -- fewer than two gaps of each kind"
            end
            for _, d in ipairs(plain) do
                if d ~= 4 then return "refused", text .. " -- an uneaten unarmed gap of " .. d end
            end
            for _, d in ipairs(eaten) do
                if d ~= 7 then
                    return "refused", text .. " -- an eat while the weapon delay ran gave " .. d
                        .. ", not 4 + 3 (wiki Food/Fast foods)"
                end
            end
            return "ok", text
        end)

        -- A GROWN WILLOW GIVES BRANCHES TO SECATEURS (matthew-mbp-m4-b50-seam1
        -- enlightenedjourney_gather_sources).  farming_trees held only oak, so
        -- nothing in the pack gave out willow_branch.  Wiki Willow branch
        -- (oldid 15184331): branches grow once the tree's health is checked,
        -- one per 5 minutes, at most 6, cut with secateurs.  Patch values
        -- (RuneLite PatchImplementation TREE): 15 seedling .. 21 check-health,
        -- 22 chop, 192..197 chop with 1..6 branches.  The six 40-minute stages
        -- are the oak's growth ladder with the willow row's numbers (driven end
        -- to end in build/quest_gate/s1_ej_willow, ~3,000 ticks -- more than
        -- this harness's frame budget), so the row plants Auguste's sapling for
        -- real (that records the seed) and then STAGES the grown tree: state 21
        -- and the patch's check flag, which is what the sixth stage writes.
        -- Graded: no branch before any time passes, then one 30-minute
        -- t.clock.skip and the secateurs cut all 6 (counted from the deadline),
        -- leaving the bare chop state 22.  Last of the seam rows: it moves the
        -- world clock 30 minutes ahead.
        seam("seam.willow_tree_grows_branches", function()
            local goto_tile = verb("player", "goto_tile")
            local by_symbol = verb("player", "by_symbol")
            local click_loc = verb("player", "click_loc")
            local use_on = verb("player", "use_on")
            local skip = verb("clock", "skip")
            local await_server = verb("var", "await_server")
            local server = verb("var", "server")
            local await = verb("inv", "await")
            local count = verb("inv", "count")
            if not goto_tile then return missing("player", "goto_tile") end
            if not by_symbol then return missing("player", "by_symbol") end
            if not click_loc then return missing("player", "click_loc") end
            if not use_on then return missing("player", "use_on") end
            if not skip then return missing("clock", "skip") end
            if not await_server then return missing("var", "await_server") end
            if not server then return missing("var", "server") end
            if not await then return missing("inv", "await") end
            if not count then return missing("inv", "count") end
            local state_var = "varb701_varbit_701"
            setup_cheat("::clearinv")
            setup_cheat("::setlevel farming 30")
            setup_cheat("::give rake 1")
            setup_cheat("::give spade 1")
            setup_cheat("::give secateurs 1")
            setup_cheat("::give zep_plantpot_willow_sapling 1")
            settle(2)
            local function teardown()
                setup_cheat("::clearinv")
                setup_cheat("::setlevel farming 1")
                setup_cheat("::tele lumbridge")
                settle(2)
            end
            local goto_result, goto_detail = goto_tile(3002, 3376, 0)
            if goto_result ~= "ok" then
                teardown()
                return "no_subject", "goto the Falador park patch 3002,3376,0 -> " .. describe(goto_result)
                    .. " " .. describe(goto_detail)
            end
            local trail = {}
            local _, raw = server(state_var)
            trail[#trail + 1] = "patch " .. describe(raw)
            if raw ~= 3 then
                click_loc("farming_tree_patch_2", 1)
                trail[#trail + 1] = "rake -> " .. describe(await_server(state_var, 3, 80))
            end
            local patch = by_symbol("loc", "farming_tree_patch_2")
            local plant_result = use_on("zep_plantpot_willow_sapling", patch)
            local planted = await_server(state_var, 15, 10)
            trail[#trail + 1] = "plant -> " .. describe(plant_result) .. ", state 15 " .. describe(planted)
            local checked = "not_run"
            if planted == "ok" then
                -- What the sixth growth stage writes (farming_tree_set): the
                -- grown state, its client mirror varb4771 (the transmit var
                -- the patch's multiloc reads while you stand in square
                -- 0_46_52) and the patch's check-health flag.
                setup_cheat("::setvar " .. state_var .. " 21")
                setup_cheat("::setvar varb4771_farming_transmit_a 21")
                setup_cheat("::setvar varp5820_farming_falador_tree_check 1")
                -- The grown willow's model is first asked for now; let it land
                -- before the click hunts its triangles.
                settle(10)
                local check_result, check_detail = click_loc("farming_tree_patch_2", 1)
                checked = await_server(state_var, 22, 10)
                trail[#trail + 1] = "check-health click -> " .. describe(check_result) .. " " .. describe(check_detail)
            end
            trail[#trail + 1] = "staged 21, check-health -> state 22 " .. describe(checked)
            local early = nil
            local cut = "not_run"
            if checked == "ok" then
                use_on("secateurs", patch)
                settle(3)
                local _, early_count = count("willow_branch")
                early = early_count
                skip(30)
                use_on("secateurs", patch)
                cut = await("willow_branch", 6, 40)
            end
            trail[#trail + 1] = "secateurs at once -> willow_branch " .. describe(early)
            local _, branches = count("willow_branch")
            local _, after = server(state_var)
            trail[#trail + 1] = "30 minutes, secateurs -> willow_branch " .. describe(branches) .. " ("
                .. describe(cut) .. "), patch " .. describe(after)
            teardown()
            local text = table.concat(trail, "; ")
            if planted ~= "ok" or checked ~= "ok" then
                return "no_subject", text .. " -- the willow did not reach its checked chop state"
            end
            if early ~= 0 then
                return "refused", text .. " -- a branch was cut before any time passed"
            end
            if cut ~= "ok" or branches ~= 6 or after ~= 22 then
                return "refused", text .. " -- 30 minutes did not grow 6 branches that the secateurs cut back to state 22"
            end
            return "ok", text
        end)

        -- --------- seam bank_withdraw_and_deposit_verbs (b56-seam2): the bank
        --
        -- t.bank.open/withdraw/deposit/count/close drive the bank the way a
        -- player does: the booth's own Bank op through click_minimenu, then
        -- the bank interface's item cells pressed with their fixed sparse ops
        -- (bank.rs2, bank_deposit.rs2).  Every exchange row is graded on BOTH
        -- containers -- the backpack read through inv.count, the bank through
        -- bank.count -- because a backpack that grew while the bank did not is
        -- a conjured item, not a withdraw.  Last in the plan: it moves the
        -- player to the castle's top floor and empties the backpack, and
        -- nothing after it reads the world.  The stock is setup
        -- (`::bankgive` at the top of run()), exactly as a quest file's
        -- `setup` list stocks one.
        stage(function()
            setup_cheat("::clearinv")
            settle(2)
            local goto_tile = verb("player", "goto_tile")
            if goto_tile then
                goto_tile(BANK_TILE_X, BANK_TILE_Z, BANK_TILE_LEVEL)
            end
            settle(2)
        end)

        step("bank.open", function()
            local fn = verb("bank", "open")
            local withdraw = verb("bank", "withdraw")
            if not fn then return missing("bank", "open") end
            -- Before the press: every exchange verb refuses a bank that is
            -- not on screen, by name, rather than pressing a stale grid.
            local early = withdraw and withdraw(BANK_OBJ_SYMBOL, 1) or "closed"
            if early ~= "closed" then
                return "refused", "bank.withdraw with no bank open answered " .. describe(early)
                    .. ", not closed"
            end
            local result, detail = fn(BANK_BOOTH_SYMBOL, BANK_BOOTH_OP)
            local text = BANK_BOOTH_SYMBOL .. " op " .. BANK_BOOTH_OP .. " -> "
                .. describe(result) .. " " .. describe(detail)
            if result ~= "ok" then
                return result, text
            end
            if not string.find(tostring(detail), "opened bankmain", 1, true)
                or not string.find(tostring(detail), "item slot", 1, true) then
                return "hollow", "an `ok` that does not say the bank mounted and what its "
                    .. "container holds -- the frame and the container are two messages: " .. text
            end
            return "ok", text .. "; before it, a withdraw -> closed"
        end)

        step("bank.count", function()
            local fn = verb("bank", "count")
            if not fn then return missing("bank", "count") end
            -- The stock is the setup's own number, so the reading is knowable.
            local result, total = fn(BANK_OBJ_SYMBOL)
            return answered(result, total, BANK_OBJ_SYMBOL .. " in the bank: ",
                equals(BANK_OBJ_STOCK), "the setup banked " .. BANK_OBJ_STOCK)
        end)

        step("bank.withdraw", function()
            local fn = verb("bank", "withdraw")
            local count_of = verb("inv", "count")
            local bank_count = verb("bank", "count")
            if not fn then return missing("bank", "withdraw") end
            if not count_of or not bank_count then
                return "no_subject", "bank.withdraw is graded on inv.count and bank.count, and "
                    .. "one of them is not on this driver"
            end
            -- 1. The exchange: the backpack up by N AND the bank down by N.
            local _, held_before = count_of(BANK_OBJ_SYMBOL)
            local _, bank_before = bank_count(BANK_OBJ_SYMBOL)
            local result, detail = fn(BANK_OBJ_SYMBOL, BANK_OBJ_WITHDRAW)
            local _, held_after = count_of(BANK_OBJ_SYMBOL)
            local _, bank_after = bank_count(BANK_OBJ_SYMBOL)
            local text = BANK_OBJ_SYMBOL .. " x" .. BANK_OBJ_WITHDRAW .. " -> " .. describe(result)
                .. " " .. describe(detail)
            if result ~= "ok" then
                return result, text
            end
            if type(held_before) ~= "number" or type(held_after) ~= "number"
                or held_after - held_before ~= BANK_OBJ_WITHDRAW then
                return "hollow", "the backpack went " .. describe(held_before) .. " -> "
                    .. describe(held_after) .. ", not +" .. BANK_OBJ_WITHDRAW .. " -- " .. text
            end
            if type(bank_before) ~= "number" or type(bank_after) ~= "number"
                or bank_before - bank_after ~= BANK_OBJ_WITHDRAW then
                return "hollow", "the bank went " .. describe(bank_before) .. " -> "
                    .. describe(bank_after) .. ", not -" .. BANK_OBJ_WITHDRAW
                    .. ": a backpack that grew while the bank did not is a conjured item -- " .. text
            end
            -- 2. Fill the backpack from the bank (28 - N bones), then 3. the
            --    refusal: a withdraw into a full backpack moves nothing and
            --    says so by name, with the server's own sentence.
            local fill = 28 - held_after
            local filled, filled_detail = fn(BANK_FILL_SYMBOL, fill)
            if filled ~= "ok" then
                return "no_subject", "filling the backpack with " .. fill .. " " .. BANK_FILL_SYMBOL
                    .. " answered " .. describe(filled) .. " " .. describe(filled_detail)
                    .. " -- the full-pack refusal has no subject (" .. text .. ")"
            end
            local full, full_detail = fn(BANK_OBJ_SYMBOL, 1)
            local _, held_full = count_of(BANK_OBJ_SYMBOL)
            local _, bank_full = bank_count(BANK_OBJ_SYMBOL)
            if full ~= "refused" or not string.find(tostring(full_detail), "backpack full", 1, true) then
                return "refused", "a withdraw into a full backpack answered " .. describe(full)
                    .. " " .. describe(full_detail) .. ", not a refusal naming the full backpack -- "
                    .. text
            end
            if held_full ~= held_after or bank_full ~= bank_after then
                return "refused", "the refused withdraw still moved something: backpack "
                    .. describe(held_after) .. " -> " .. describe(held_full) .. ", bank "
                    .. describe(bank_after) .. " -> " .. describe(bank_full)
            end
            return "ok", text .. "; backpack " .. tostring(held_before) .. " -> "
                .. tostring(held_after) .. ", bank " .. tostring(bank_before) .. " -> "
                .. tostring(bank_after) .. "; filled with " .. fill .. " " .. BANK_FILL_SYMBOL
                .. ", then a withdraw -> refused: " .. describe(full_detail)
        end)

        step("bank.deposit", function()
            local fn = verb("bank", "deposit")
            local count_of = verb("inv", "count")
            local bank_count = verb("bank", "count")
            if not fn then return missing("bank", "deposit") end
            if not count_of or not bank_count then
                return "no_subject", "bank.deposit is graded on inv.count and bank.count, and "
                    .. "one of them is not on this driver"
            end
            -- 1. The mirror: Deposit-All of the filler, the backpack down by
            --    every one and the bank up by every one.
            local _, fill_held = count_of(BANK_FILL_SYMBOL)
            local _, fill_bank = bank_count(BANK_FILL_SYMBOL)
            local all, all_detail = fn(BANK_FILL_SYMBOL, "all")
            local _, fill_held_after = count_of(BANK_FILL_SYMBOL)
            local _, fill_bank_after = bank_count(BANK_FILL_SYMBOL)
            local text = BANK_FILL_SYMBOL .. " all -> " .. describe(all) .. " " .. describe(all_detail)
            if all ~= "ok" then
                return all, text
            end
            if fill_held_after ~= 0 or type(fill_held) ~= "number" or type(fill_bank) ~= "number"
                or fill_bank_after ~= fill_bank + fill_held then
                return "hollow", "Deposit-All left the backpack at " .. describe(fill_held_after)
                    .. " and the bank went " .. describe(fill_bank) .. " -> "
                    .. describe(fill_bank_after) .. " -- " .. text
            end
            -- 2. A counted deposit.
            local _, held_before = count_of(BANK_OBJ_SYMBOL)
            local _, bank_before = bank_count(BANK_OBJ_SYMBOL)
            local result, detail = fn(BANK_OBJ_SYMBOL, 5)
            local _, held_after = count_of(BANK_OBJ_SYMBOL)
            local _, bank_after = bank_count(BANK_OBJ_SYMBOL)
            text = text .. "; " .. BANK_OBJ_SYMBOL .. " x5 -> " .. describe(result) .. " "
                .. describe(detail)
            if result ~= "ok" then
                return result, text
            end
            if type(held_before) ~= "number" or type(held_after) ~= "number"
                or type(bank_before) ~= "number" or type(bank_after) ~= "number"
                or held_before - held_after ~= 5 or bank_after - bank_before ~= 5 then
                return "hollow", "backpack " .. describe(held_before) .. " -> " .. describe(held_after)
                    .. ", bank " .. describe(bank_before) .. " -> " .. describe(bank_after)
                    .. " -- not -5/+5: " .. text
            end
            -- 3. The refusal: nothing of it carried -> not_found, nothing pressed.
            local none, none_detail = fn(BANK_FILL_SYMBOL, 1)
            if none ~= "not_found" then
                return "refused", "a deposit of " .. BANK_FILL_SYMBOL .. " the backpack no longer "
                    .. "holds answered " .. describe(none) .. " " .. describe(none_detail)
                    .. ", not not_found -- " .. text
            end
            return "ok", text .. "; then a deposit of what is not carried -> not_found"
        end)

        step("bank.close", function()
            local fn = verb("bank", "close")
            local bank_count = verb("bank", "count")
            if not fn then return missing("bank", "close") end
            local result, detail = fn()
            local text = "close -> " .. describe(result) .. " " .. describe(detail)
            if result ~= "ok" then
                return result, text
            end
            -- The screen really gone: bank.count reads only an OPEN bank.
            if bank_count then
                local after = bank_count(BANK_OBJ_SYMBOL)
                if after ~= "closed" then
                    return "hollow", "after close, bank.count answers " .. describe(after)
                        .. " rather than closed -- the screen did not go: " .. text
                end
                text = text .. "; bank.count after it -> closed"
            end
            local again = fn()
            if again ~= "ok" then
                return "hollow", "a second close answers " .. describe(again) .. ", not ok -- " .. text
            end
            return "ok", text .. "; a second close -> ok"
        end)

        -- `::bankgive` stocks a bank and is SETUP ONLY: after a quest is
        -- bound it is a mid-run ::give with a detour through the bank (trap
        -- 16), and QD.cheat refuses it before anything is sent (core.lua).
        -- The quest.bind row bound one long before this point.
        seam("seam.bankgive_is_setup_only", function()
            local cheat = verb("cheat")
            if not cheat then return missing("cheat") end
            if type(t.quest) ~= "table" or t.quest._bound == nil then
                return "no_subject", "no quest is bound at this point (the quest.bind row did not "
                    .. "bind), so the after-setup rule has nothing to refuse"
            end
            local result, detail = cheat("::bankgive " .. BANK_OBJ_SYMBOL .. " 1", false)
            if result ~= "refused" or not string.find(tostring(detail), "SETUP", 1, true) then
                return "refused", "::bankgive after quest.bind answered " .. describe(result) .. " "
                    .. describe(detail) .. ", not the setup-only refusal"
            end
            return "ok", "::bankgive after quest.bind -> refused: " .. describe(detail)
        end)

        -- AN NPC THAT DRAWS NO FACE IS STILL PRESSED (matthew-mbp-m4-b58-seam1
        -- npc_press_answers_covered_in_a_cramped_room).  Biohazard's Chancy
        -- and Da Vinci in the Dancing Donkey Inn (gambler2 1106 at 3271,3388,
        -- artist2 1104 at 3272,3389; m51_52.spawn:12-13) are drawn with cache
        -- model 25362 alone: four vertices, a 128x128 quad 209 units up, both
        -- faces alpha 255, which ModelData.light hides (toridraw_lighting.c
        -- alpha -1 -> type 2 -> HIDDEN; public dump Joshua-F/osrs-dumps
        -- config/dump.npc says model1=model_25362 too).  The reference picks
        -- every npc by its projected box (Model.useAABBMouseCheck, NpcType:227),
        -- hidden vertices included, so the game's own client can right-click
        -- them; this client picked entities per-face and skips hidden faces,
        -- so every press answered `covered` ("none of 99 pixels hittested ...
        -- holds it", build/quest_gate/cr_probe1 rows 7-8) and biohazard.lua
        -- fell back to t.drive.op for both.  Graded on a REAL press of each:
        -- talk_to ok with no bypass note, and the npc's own [opnpc1] answering
        -- (errand_boys.rs2:206 "Chancy doesn't feel like talking.", :303 "...
        -- does not feel sufficiently moved to talk." at stage 0).  The goto is
        -- the row's starting point inside the inn (the tile biohazard.lua walks
        -- to), not a crossing.
        seam("seam.npc_drawing_no_face_is_pressed", function()
            local goto_tile = verb("player", "goto_tile")
            local talk_to = verb("player", "talk_to")
            local walk_to = verb("player", "walk_to")
            if not goto_tile then return missing("player", "goto_tile") end
            if not talk_to then return missing("player", "talk_to") end
            if not walk_to then return missing("player", "walk_to") end
            local goto_result, goto_detail = goto_tile(3270, 3388, 0)
            if goto_result ~= "ok" then
                return "no_subject", "goto the Dancing Donkey Inn 3270,3388 -> " .. describe(goto_result)
                    .. " " .. describe(goto_detail)
            end
            settle(2)
            local g_result, g_detail = talk_to("gambler2", 1)
            local text = "gambler2 -> " .. describe(g_result) .. " " .. describe(g_detail)
            if g_result ~= "ok" then
                return g_result, text
            end
            walk_to(3273, 3389, 10)
            local a_result, a_detail = talk_to("artist2", 1, { at = { 3272, 3389 } })
            text = text .. "; artist2 at 3272,3389 -> " .. describe(a_result) .. " " .. describe(a_detail)
            if a_result ~= "ok" then
                return a_result, text
            end
            if string.find(text, "drive.op", 1, true) ~= nil then
                return "hollow", text .. " -- a press went through the logged bypass"
            end
            if string.find(tostring(g_detail), "Chancy", 1, true) == nil
                or string.find(tostring(a_detail), "Da Vinci", 1, true) == nil then
                return "hollow", text .. " -- ok, but the npc's own [opnpc1] line never came back"
            end
            return "ok", text
        end)

        -- ONE DOOR, CROSSED ON FOOT (matthew-mbp-m4-b59-seam1
        -- level_aware_loc_read_and_door_helper).  t.player.pass_door walks to
        -- the near side, presses the CLOSED leaf by tile and level, walks
        -- through and grades the far tile, then (close = true) shuts the door
        -- behind and walks back to the far tile.  The subject is Miscellania
        -- castle's stair-room door castledoor 2506,3851,0 (hall 2506,3852 ->
        -- stair room 2506,3850; misc_shared_notes, misc_astrid run3), and the
        -- goto is the row's starting point in the hall, not a crossing.
        step("player.pass_door", function()
            local goto_tile = verb("player", "goto_tile")
            local fn = verb("player", "pass_door")
            if not goto_tile then return missing("player", "goto_tile") end
            if not fn then return missing("player", "pass_door") end
            local goto_result, goto_detail = goto_tile(2506, 3854, 0)
            if goto_result ~= "ok" then
                setup_cheat("::tele lumbridge")
                settle(2)
                return "no_subject", "goto the Miscellania castle hall 2506,3854,0 -> " .. describe(goto_result)
                    .. " " .. describe(goto_detail)
            end
            local result, detail = fn({ closed = "castledoor", open = "opencastledoor",
                at = { 2506, 3851, 0 }, near = { 2506, 3852 }, far = { 2506, 3850 }, close = true })
            setup_cheat("::tele lumbridge")
            settle(2)
            local text = tostring(detail)
            if result ~= "ok" then
                return result, text
            end
            if string.find(text, "pressed castledoor op1 at 2506,3851,0", 1, true) == nil
                or string.find(text, "closed leaf back on 2506,3851,0", 1, true) == nil then
                return "hollow", text .. " -- ok, but the detail names no press of the closed leaf or no shut"
            end
            return "ok", text
        end)

        -- A DOOR STACKED ON TWO FLOORS IS READ ON ITS OWN FLOOR (matthew-mbp-
        -- m4-b59-seam1 level_aware_loc_read_and_door_helper).  The loc pool is
        -- ordered by x/z distance only (DriveUi_Locs), so t.world.loc_near
        -- with no opts answers the first copy whatever its level: in
        -- Miscellania castle castledoor 2506,3851 stands on levels 0 AND 1,
        -- and with the level-0 door left open a level-1 player read the
        -- level-0 open leaf at 2506,3852,0 as "my door stands open" (misc
        -- run4: 10 of 69 open-door rows passed on the wrong floor).  Graded:
        -- loc_near's `level = "here"` filter answers not_found naming the
        -- skipped level-0 copy; pass_door on level 1 PRESSES the closed
        -- level-1 leaf (never "stands open") and crosses to 2506,3853,1; and
        -- back on level 0 the leaf left open is walked through, not pressed
        -- (pressing it would shut it), then shut.  The gotos are starting
        -- points (hall, landing, stair room), never a crossing of the door.
        seam("seam.stacked_door_read_on_its_own_floor", function()
            local goto_tile = verb("player", "goto_tile")
            local pass_door = verb("player", "pass_door")
            local loc_near = verb("world", "loc_near")
            if not goto_tile then return missing("player", "goto_tile") end
            if not pass_door then return missing("player", "pass_door") end
            if not loc_near then return missing("world", "loc_near") end
            local function leave(result, text)
                setup_cheat("::tele lumbridge")
                settle(2)
                return result, text
            end
            local goto_result, goto_detail = goto_tile(2506, 3854, 0)
            if goto_result ~= "ok" then
                return leave("no_subject", "goto the hall 2506,3854,0 -> " .. describe(goto_result) .. " "
                    .. describe(goto_detail))
            end
            local l0_result, l0_detail = pass_door({ closed = "castledoor", open = "opencastledoor",
                at = { 2506, 3851, 0 }, near = { 2506, 3852 }, far = { 2506, 3850 } })
            local text = "level 0 opened -> " .. describe(l0_result) .. " " .. tostring(l0_detail)
            if l0_result ~= "ok" then
                return leave("no_subject", text .. " -- the level-0 leaf was not left open")
            end
            goto_result, goto_detail = goto_tile(2506, 3850, 1)
            if goto_result ~= "ok" then
                return leave("no_subject", text .. "; goto the landing 2506,3850,1 -> " .. describe(goto_result))
            end
            local blind_result, blind_row = loc_near("opencastledoor", 3)
            local here_result, here_detail = loc_near("opencastledoor", 3, { level = "here" })
            text = text .. "; on level 1 loc_near(opencastledoor, 3) -> " .. describe(blind_result) .. " "
                .. (blind_result == "ok" and (blind_row.tile_x .. "," .. blind_row.tile_z .. "," .. blind_row.level)
                    or describe(blind_row))
                .. "; with {level=\"here\"} -> " .. describe(here_result) .. " " .. tostring(here_detail)
            if here_result == "ok" then
                return leave("refused", text .. " -- the level filter answered a copy on level 1 although none stands open there")
            end
            if string.find(tostring(here_detail), "2506,3852,0", 1, true) == nil then
                return leave("refused", text .. " -- the filtered not_found does not name the skipped level-0 leaf")
            end
            local l1_result, l1_detail = pass_door({ closed = "castledoor", open = "opencastledoor",
                at = { 2506, 3851, 1 }, near = { 2506, 3851 }, far = { 2506, 3853 }, close = true })
            text = text .. "; level 1 pass -> " .. describe(l1_result) .. " " .. tostring(l1_detail)
            if l1_result ~= "ok" then
                return leave(l1_result, text)
            end
            if string.find(tostring(l1_detail), "pressed castledoor op1 at 2506,3851,1", 1, true) == nil then
                return leave("refused", text .. " -- the level-1 door was not pressed (the level-0 leaf read as this door's)")
            end
            goto_result, goto_detail = goto_tile(2506, 3850, 0)
            if goto_result ~= "ok" then
                return leave("no_subject", text .. "; goto the stair room 2506,3850,0 -> " .. describe(goto_result))
            end
            local back_result, back_detail = pass_door({ closed = "castledoor", open = "opencastledoor",
                at = { 2506, 3851, 0 }, near = { 2506, 3850 }, far = { 2506, 3853 }, close = true })
            text = text .. "; level 0 back -> " .. describe(back_result) .. " " .. tostring(back_detail)
            if back_result ~= "ok" then
                return leave(back_result, text)
            end
            if string.find(tostring(back_detail), "stands open", 1, true) == nil then
                return leave("refused", text .. " -- the level-0 leaf left open was not read as standing open")
            end
            return leave("ok", text)
        end)

        -- SEAM door_revert_lost_when_player_is_away (matthew-mbp-m4-b59-seam1):
        -- UPDATE_ZONE_FULL_FOLLOWS RESETS THE ZONE'S LOCS, not just its obj
        -- stacks (src/game/rs_gameproto_exec.c zone_full_reset_locs; reference
        -- Client-TS Client.ts UPDATE_ZONE_FULL_FOLLOWS sets endTime = 0 on every
        -- locChanges entry in the zone).  The zone catch-up describes only what
        -- differs from the map, so a door whose 500-tick revert (doors.rs2
        -- ~door_open_active, loc_del(500) + loc_add(500)) fired while the client
        -- was not told about its zone came back with NEITHER leaf: the closed
        -- leaf still deleted, the open one removed by a stale LOC_DEL the server
        -- never retired (torirs_server_zone.c, a removed loc compared by angle).
        -- Subject: Miscellania's castle gate, castledoor 2510,3860,0 (open leaf
        -- opencastledoor 2511,3860,0).  Arrive at 2513,3862,0 (the teleport's
        -- rebuild centres the scene on zone 314,482), open it, go to
        -- 2545,3870,0 -- zone 318: outside the 7x7 zone window, and 7 tiles
        -- inside the rebuild margin, so the scene is NOT rebuilt (a rebuild
        -- re-reads the map and hides the bug: the first twin went to
        -- 2551,3895 from a scene centred one zone west and passed on the
        -- broken binary) -- wait the revert out, come back: the closed leaf
        -- must stand on 2510,3860,0 and no open leaf within 1.  Measured on the scratch twin: build/quest_gate/b59door_repro0
        -- row 7 (before: "closed=false open=false") / b59door_repro2 row 7
        -- (after: "closed=true open=false").  The same hole on a plane change
        -- (revert on the tick the stairs re-FULL the new plane) is
        -- b59door_plane4 / b59door_plane6 (build/seam_state/
        -- matthew-mbp-m4-b59-seam1/door_revert/repro_plane_stairs.lua).
        seam("seam.door_revert_reaches_a_returning_client", function()
            local goto_tile = verb("player", "goto_tile")
            local click_loc = verb("player", "click_loc")
            local by_symbol = verb("player", "by_symbol")
            local pool_read = verb("drive", "_pool_read")
            if not goto_tile then return missing("player", "goto_tile") end
            if not click_loc then return missing("player", "click_loc") end
            if not by_symbol then return missing("player", "by_symbol") end
            if not pool_read then return missing("drive", "_pool_read") end
            local closed, closed_result = by_symbol("loc", "castledoor")
            local open, open_result = by_symbol("loc", "opencastledoor")
            if closed_result ~= "ok" or not is_table(closed) or open_result ~= "ok" or not is_table(open) then
                return "no_subject", "by_symbol castledoor -> " .. describe(closed_result)
                    .. ", opencastledoor -> " .. describe(open_result)
            end
            -- Which leaves the CLIENT holds at the gate, on level 0.
            local function leaves()
                local rows_result, rows = pool_read("locs", 0, 18)
                if rows_result ~= "ok" or not is_table(rows) then
                    return nil, nil, "the loc pool -> " .. describe(rows_result)
                end
                local has_closed, has_open = false, false
                for index = 1, #rows do
                    local row = rows[index]
                    local id = row.resolved_loc_id or row.loc_id
                    if row.level == 0 and (row.loc_id == closed.id or id == closed.id)
                        and row.x == 2510 and row.z == 3860 then
                        has_closed = true
                    end
                    if row.level == 0 and (row.loc_id == open.id or id == open.id)
                        and math.abs(row.x - 2510) <= 1 and math.abs(row.z - 3860) <= 1 then
                        has_open = true
                    end
                end
                return has_closed, has_open, "closed leaf " .. tostring(has_closed)
                    .. ", open leaf " .. tostring(has_open)
            end
            local goto_result, goto_detail = goto_tile(2513, 3862, 0)
            if goto_result ~= "ok" then
                return "no_subject", "goto 2513,3862,0 -> " .. describe(goto_result) .. " " .. describe(goto_detail)
            end
            local press_result, press_detail = click_loc("castledoor", 1, { at = { 2510, 3860, 0 } })
            settle(2)
            local _, opened, opened_text = leaves()
            if not opened then
                return "no_subject", "Open castledoor 2510,3860,0 -> " .. describe(press_result) .. " "
                    .. describe(press_detail) .. "; " .. describe(opened_text) .. " -- the gate did not open"
            end
            local away_result, away_detail = goto_tile(2545, 3870, 0)
            if away_result ~= "ok" then
                return "no_subject", "goto 2545,3870,0 -> " .. describe(away_result) .. " " .. describe(away_detail)
            end
            settle(510)
            local back_result, back_detail = goto_tile(2513, 3862, 0)
            settle(4)
            local has_closed, has_open, text = leaves()
            text = "opened (" .. describe(opened_text) .. "), 35 tiles away for 510 ticks, back -> "
                .. describe(back_result) .. " " .. describe(back_detail) .. "; " .. describe(text)
            setup_cheat("::tele lumbridge")
            settle(4)
            if has_closed ~= true or has_open ~= false then
                return "refused", text .. " -- want the closed leaf on 2510,3860,0 and no open leaf:"
                    .. " the revert never reached the client"
            end
            return "ok", text
        end)

        -- THE CROSSING VERBS (matthew-mbp-m4-b60-seam0
        -- shared_crossing_helpers_for_gates_traps_and_walls): the four helpers
        -- every b56-b59 door-rule fixer hand-wrote, ported from hero.lua,
        -- hunt.lua, rovingelves.lua, mourningsendparti.lua and misc.lua.  The
        -- gotos below are each row's starting point, never a crossing.
        --
        -- A WALK-THROUGH WALL GATE, BOTH WAYS.  Taverley's east members' gate
        -- membergater 2935,3450,0 (gates.rs2 [label,member_fencegate_try]:
        -- a press p_teleports through and leaves no opened loc), the only way
        -- on foot between Taverley (x <= 2935) and the Ice Mountain side
        -- (sampler-findings.md "Sample matthew-mbp-m4-b59" (a)).  First the
        -- subject is proved: a walk from 2936,3450 to 2934,3450 does NOT get
        -- across (so a landing is the press's doing); then cross_gate in from
        -- the east (graded x <= 2935) and out from the west (graded x >= 2936,
        -- then walked on to 2937,3450 exactly).
        step("player.cross_gate", function()
            local goto_tile = verb("player", "goto_tile")
            local walk_to = verb("player", "walk_to")
            local tile = verb("world", "tile")
            local fn = verb("player", "cross_gate")
            if not goto_tile then return missing("player", "goto_tile") end
            if not walk_to then return missing("player", "walk_to") end
            if not tile then return missing("world", "tile") end
            if not fn then return missing("player", "cross_gate") end
            local function leave(result, text)
                setup_cheat("::tele lumbridge")
                settle(2)
                return result, text
            end
            local goto_result, goto_detail = goto_tile(2936, 3450, 0)
            if goto_result ~= "ok" then
                return leave("no_subject", "goto the gate's east side 2936,3450,0 -> " .. describe(goto_result)
                    .. " " .. describe(goto_detail))
            end
            local probe_result = walk_to(2934, 3450, 8)
            local probe_tile_result, probe = tile()
            local text = "walk_to 2934,3450 from 2936,3450 (no press) -> " .. describe(probe_result) .. " at "
                .. (probe_tile_result == "ok" and (probe.x .. "," .. probe.z .. "," .. probe.level)
                    or describe(probe_tile_result))
            if probe_tile_result ~= "ok" or not is_table(probe) or probe.x <= 2935 then
                return leave("no_subject", text .. " -- the walk got across without a press: not an only-way gate")
            end
            local in_result, in_detail = fn({ loc = "membergater", at = { 2935, 3450, 0 }, near = { 2936, 3450 },
                far_ok = function(at) return at.x <= 2935 end, far_desc = "inside Taverley, x <= 2935" })
            text = text .. "; in -> " .. describe(in_result) .. " " .. tostring(in_detail)
            if in_result ~= "ok" then
                return leave(in_result, text)
            end
            local out_result, out_detail = fn({ loc = "membergater", at = { 2935, 3450, 0 }, near = { 2934, 3450 },
                far_ok = function(at) return at.x >= 2936 end, far_desc = "out of Taverley, x >= 2936",
                far = { 2937, 3450 } })
            text = text .. "; out -> " .. describe(out_result) .. " " .. tostring(out_detail)
            if out_result ~= "ok" then
                return leave(out_result, text)
            end
            local press = "click_loc(membergater at 2935,3450,0, op1)"
            if string.find(tostring(in_detail), press, 1, true) == nil
                or string.find(tostring(out_detail), press, 1, true) == nil then
                return leave("hollow", text .. " -- ok, but a detail names no press of the gate")
            end
            local end_result, finish = tile()
            if end_result ~= "ok" or not is_table(finish) or finish.x ~= 2937 or finish.z ~= 3450 then
                return leave("hollow", text .. " -- ok, but world.tile reads " .. describe(finish))
            end
            return leave("ok", text)
        end)

        -- A GUARDED WALK-THROUGH SPEAKS BEFORE IT MOVES THE PLAYER
        -- (matthew-mbp-m4-b61-seam1
        -- cross_gate_needs_a_chat_option_for_a_guarded_walk_through).  Fight
        -- Arena's north fightarena_door1 2617,3171,0 (arena_locs.rs2
        -- [oploc1,fightarena_door1]): in the Khazard disguise at
        -- ^arena_obtained_armour, an arena_guard1 within 5 tiles (m40_49.spawn
        -- puts one on 2617,3172) says "Nice observation guard..." and the
        -- p_telejump into the prison corridor lands only after that page is
        -- continued; cross_gate without `chat` waited 12 ticks under the page
        -- and failed the crossing (b61 arena run 1; scratch b61gc_reproA).
        -- First the subject: the guard by the near tile, and a walk from
        -- 2617,3172 to 2617,3170 that does NOT get in.  Then in with
        -- chat = { "npc:Nice observation guard" }: graded on ok, on the detail
        -- carrying the guard's whole line and chat.play's ok, and on world.tile
        -- reading z <= 3171.  Then the two other answers on Taverley's members'
        -- gate, which never speaks: `chat` with no page is `refused` although
        -- the press landed, and `chat_optional` turns that into an ok that
        -- says "no page opened".
        seam("seam.cross_gate_plays_a_guards_page", function()
            local goto_tile = verb("player", "goto_tile")
            local walk_to = verb("player", "walk_to")
            local equip = verb("player", "equip")
            local unequip = verb("player", "unequip")
            local drop = verb("player", "drop")
            local nearest = verb("npc", "nearest")
            local tile = verb("world", "tile")
            local fn = verb("player", "cross_gate")
            if not goto_tile then return missing("player", "goto_tile") end
            if not walk_to then return missing("player", "walk_to") end
            if not equip then return missing("player", "equip") end
            if not unequip then return missing("player", "unequip") end
            if not drop then return missing("player", "drop") end
            if not nearest then return missing("npc", "nearest") end
            if not tile then return missing("world", "tile") end
            if not fn then return missing("player", "cross_gate") end
            local armour = { "khazard_helmet", "khazard_platemail" }
            local function leave(result, text)
                for _, item in ipairs(armour) do
                    unequip(item)
                    drop(item)
                end
                setup_cheat("::setvar varp17_arenaquest 0")
                setup_cheat("::tele lumbridge")
                settle(2)
                return result, text
            end
            local function tile_text()
                local result, at = tile()
                if result ~= "ok" or not is_table(at) then
                    return nil, describe(result)
                end
                return at, at.x .. "," .. at.z .. "," .. at.level
            end
            setup_cheat("::setvar varp17_arenaquest ^arena_obtained_armour")
            for _, item in ipairs(armour) do
                setup_cheat("::give " .. item .. " 1")
            end
            local goto_result, goto_detail = goto_tile(2617, 3174, 0)
            if goto_result ~= "ok" then
                return leave("no_subject", "goto outside the north door 2617,3174,0 -> " .. describe(goto_result)
                    .. " " .. describe(goto_detail))
            end
            for _, item in ipairs(armour) do
                local worn_result, worn_detail = equip(item)
                if worn_result ~= "ok" then
                    return leave("no_subject", "equip " .. item .. " -> " .. describe(worn_result) .. " "
                        .. describe(worn_detail) .. " -- no disguise, so the guard refuses")
                end
            end
            local probe_result = walk_to(2617, 3170, 8)
            local probe, probe_text = tile_text()
            local text = "disguised at ^arena_obtained_armour; walk_to 2617,3170 from 2617,3174 (no press) -> "
                .. describe(probe_result) .. " at " .. tostring(probe_text)
            if probe == nil or probe.z <= 3171 then
                return leave("no_subject", text .. " -- the walk got in without a press: not an only-way door")
            end
            walk_to(2617, 3172, 8)
            local guard_result, guard = "not_found", nil
            for _ = 1, 10 do
                guard_result, guard = nearest("arena_guard1", 5)
                if guard_result == "ok" and is_table(guard) then
                    break
                end
                settle(1)
            end
            if guard_result ~= "ok" or not is_table(guard) then
                return leave("no_subject", text .. "; no arena_guard1 within 5 of 2617,3172 in 10 ticks ("
                    .. describe(guard_result) .. ") -- the door would not speak")
            end
            text = text .. "; arena_guard1 at " .. tostring(guard.x) .. "," .. tostring(guard.z)
            local in_result, in_detail = fn({ loc = "fightarena_door1", at = { 2617, 3171, 0 }, near = { 2617, 3172 },
                far_ok = function(at) return at.z <= 3171 and at.x >= 2613 and at.x <= 2619 end,
                far_desc = "in the prison corridor, z <= 3171",
                chat = { "npc:Nice observation guard" } })
            text = text .. " | in -> " .. describe(in_result) .. " " .. tostring(in_detail)
            if in_result ~= "ok" then
                return leave(in_result, text)
            end
            local line = "the press opened npc 'Nice observation guard. You could have just asked to be let in"
                .. " like a normal person.'"
            if string.find(tostring(in_detail), line, 1, true) == nil
                or string.find(tostring(in_detail), "chat.play -> ok", 1, true) == nil then
                return leave("hollow", text .. " -- ok, but the detail carries no guard's line or no chat.play ok")
            end
            local inside, inside_text = tile_text()
            text = text .. "; world.tile " .. tostring(inside_text)
            if inside == nil or inside.z > 3171 or inside.level ~= 0 then
                return leave("hollow", text .. " -- ok, but world.tile is not in the corridor")
            end
            goto_result, goto_detail = goto_tile(2936, 3450, 0)
            if goto_result ~= "ok" then
                return leave("no_subject", text .. " | goto the members' gate's east side 2936,3450,0 -> "
                    .. describe(goto_result) .. " " .. describe(goto_detail))
            end
            local mute_result, mute_detail = fn({ loc = "membergater", at = { 2935, 3450, 0 }, near = { 2936, 3450 },
                far_ok = function(at) return at.x <= 2935 end, far_desc = "inside Taverley, x <= 2935",
                chat = { "npc:*" } })
            text = text .. " | silent gate, chat -> " .. describe(mute_result) .. " " .. tostring(mute_detail)
            if mute_result ~= "refused"
                or string.find(tostring(mute_detail), "no page opened", 1, true) == nil
                or string.find(tostring(mute_detail), "the press landed inside Taverley", 1, true) == nil then
                return leave("hollow", text .. " -- want refused: chat= named a page the press never opened")
            end
            local quiet_result, quiet_detail = fn({ loc = "membergater", at = { 2935, 3450, 0 }, near = { 2934, 3450 },
                far_ok = function(at) return at.x >= 2936 end, far_desc = "out of Taverley, x >= 2936",
                chat = { "npc:*" }, chat_optional = "conformance: the members' gate never speaks" })
            text = text .. " | silent gate, chat_optional -> " .. describe(quiet_result) .. " " .. tostring(quiet_detail)
            if quiet_result ~= "ok"
                or string.find(tostring(quiet_detail), "no page opened", 1, true) == nil
                or string.find(tostring(quiet_detail), "chat_optional: conformance", 1, true) == nil then
                return leave(quiet_result == "ok" and "hollow" or quiet_result,
                    text .. " -- want ok with 'no page opened' and the chat_optional reason")
            end
            local out, out_text = tile_text()
            if out == nil or out.x < 2936 then
                return leave("hollow", text .. " -- ok, but world.tile reads " .. tostring(out_text))
            end
            return leave("ok", text)
        end)

        -- The Isafdar rows' stage: Hitpoints 99 (the fixture's 10 is one
        -- slipped pitfall from dead) and Agility 70 -- the Agility the b59
        -- Isafdar tests stage, at which regicide_traps.rs2's
        -- stat_random(agility, 160, 300) Jump cannot slip, so the row is
        -- deterministic.  Put back by the teleport row's own exit.
        stage(function()
            setup_cheat("::setlevel hitpoints 99")
            setup_cheat("::setlevel agility 70")
            settle(2)
        end)

        -- A WAYPOINT ROUTE ACROSS SCENE REBUILDS.  rovingelves.lua's
        -- gate_to_camp walkToPitfall chain, verbatim: 26 waypoints from the
        -- Arandar Huge Gate's Isafdar side (2385,3333) to the pitfall's east
        -- source tile (2279,3262), hops of at most 10 tiles (2304,3302 ->
        -- 2304,3292 -> ... is ten apart), a route rovingelves' fixer flooded
        -- with every trap trigger tile blocked.  The end is 106 tiles west of
        -- the start, and a built scene is 104 tiles across, so no one scene
        -- holds both: the route crosses at least one walk-triggered rebuild.
        -- Graded on the exact end tile, on every hop's vitals hook having run,
        -- and on the detail naming all 26 hops.
        step("player.walk_route", function()
            local goto_tile = verb("player", "goto_tile")
            local tile = verb("world", "tile")
            local fn = verb("player", "walk_route")
            if not goto_tile then return missing("player", "goto_tile") end
            if not tile then return missing("world", "tile") end
            if not fn then return missing("player", "walk_route") end
            local function leave(result, text)
                setup_cheat("::tele lumbridge")
                settle(2)
                return result, text
            end
            local route = { { 2383, 3325 }, { 2376, 3322 }, { 2368, 3320 }, { 2359, 3319 }, { 2355, 3313 },
                { 2346, 3314 }, { 2343, 3321 }, { 2336, 3324 }, { 2331, 3319 }, { 2331, 3309 }, { 2323, 3307 },
                { 2316, 3310 }, { 2319, 3317 }, { 2317, 3325 }, { 2308, 3326 }, { 2303, 3321 }, { 2303, 3311 },
                { 2304, 3302 }, { 2304, 3292 }, { 2304, 3282 }, { 2304, 3272 }, { 2297, 3271 }, { 2290, 3274 },
                { 2284, 3270 }, { 2279, 3265 }, { 2279, 3262 } }
            local goto_result, goto_detail = goto_tile(2385, 3333, 0)
            if goto_result ~= "ok" then
                return leave("no_subject", "goto the Arandar gate's Isafdar side 2385,3333,0 -> "
                    .. describe(goto_result) .. " " .. describe(goto_detail))
            end
            local hooks = 0
            local result, detail = fn(route, { vitals = function()
                hooks = hooks + 1
                return nil
            end })
            local text = "2385,3333,0 -> 2279,3262,0 (106 tiles west: past a 104-tile scene) -> "
                .. describe(result) .. " " .. tostring(detail) .. "; vitals hook ran " .. hooks .. " time(s)"
            if result ~= "ok" then
                return leave(result, text)
            end
            local end_result, finish = tile()
            if end_result ~= "ok" or not is_table(finish) or finish.x ~= 2279 or finish.z ~= 3262
                or finish.level ~= 0 then
                return leave("hollow", text .. " -- ok, but world.tile reads " .. describe(finish))
            end
            if hooks ~= #route then
                return leave("hollow", text .. " -- want the hook once per hop (" .. #route .. ")")
            end
            return leave("ok", text)
        end)

        -- A TRAP CROSSED BY ITS OWN OP, LANDING READ.  Regicide's pitfall
        -- ring, westbound: regicide_pitfall_side 2278,3262,0 Jump from the
        -- source tile 2279,3262 onto 2275,3262 (maplink_agility's row;
        -- rovingelves.lua PITFALL_W).  The goto is the source tile itself --
        -- from Lumbridge, so the scene is built by the teleport.  Graded on
        -- the detail naming the press and the landing, on the server's own
        -- "You manage to cross safely." line the press caused, on world.tile
        -- reading 2275,3262,0, and on the vitals hook having run.
        step("player.cross_trap", function()
            local goto_tile = verb("player", "goto_tile")
            local tile = verb("world", "tile")
            local fn = verb("player", "cross_trap")
            if not goto_tile then return missing("player", "goto_tile") end
            if not tile then return missing("world", "tile") end
            if not fn then return missing("player", "cross_trap") end
            local function leave(result, text)
                setup_cheat("::tele lumbridge")
                settle(2)
                return result, text
            end
            local goto_result, goto_detail = goto_tile(2279, 3262, 0)
            if goto_result ~= "ok" then
                return leave("no_subject", "goto the pitfall's source tile 2279,3262,0 -> " .. describe(goto_result)
                    .. " " .. describe(goto_detail))
            end
            local hooks = 0
            local result, detail = fn({ loc = "regicide_pitfall_side", op_name = "Jump", at = { 2278, 3262, 0 },
                src = { 2279, 3262 }, dest = { 2275, 3262 }, vitals = function()
                    hooks = hooks + 1
                    return "hook " .. hooks
                end })
            local text = describe(result) .. " " .. tostring(detail)
            if result ~= "ok" then
                return leave(result, text)
            end
            if string.find(text, "click_loc(regicide_pitfall_side at 2278,3262,0, op1 Jump)", 1, true) == nil
                or string.find(text, "landed 2275,3262,0", 1, true) == nil then
                return leave("hollow", text .. " -- ok, but the detail names no press or no landing")
            end
            if string.find(text, "You manage to cross safely.", 1, true) == nil then
                return leave("hollow", text .. " -- ok, but the server's crossing line is not in the detail")
            end
            local end_result, finish = tile()
            if end_result ~= "ok" or not is_table(finish) or finish.x ~= 2275 or finish.z ~= 3262
                or finish.level ~= 0 then
                return leave("hollow", text .. " -- ok, but world.tile reads " .. describe(finish))
            end
            if hooks < 1 then
                return leave("hollow", text .. " -- the vitals hook never ran")
            end
            return leave("ok", text)
        end)

        -- SEAM cross_trap_cannot_name_a_loc_on_another_raw_level
        -- (matthew-mbp-m4-b61-seam1): A BRIDGE-DECK LOC NAMED BY loc_level.
        -- The Waterfall ledge's barrel_waterfall_quest stands at 2512,3463 on
        -- RAW level 1 of a bridge column (maps/m39_54.jl2; LostCity places it
        -- identically, LostCity_Content2/maps/m39_54.jm2) while the player
        -- stands on the ledge 2511,3463 on plane 0; its op1 is unconditional:
        -- "You climb in the barrel and start rocking." then
        -- p_teleport(^waterfall_fail_coord) = 2527,3413,0
        -- (quest_waterfall_locs.rs2 [oploc1,barrel_waterfall_quest]).  Before
        -- the seam, cross_trap's `at` level was both the copy's and the
        -- player's: at = {2512,3463,0} pressed nothing ("nearest copies:
        -- 2512,3463,1") and {..,1} refused the plane-0 player
        -- (build/quest_gate/b61s1_bridge_before rows 4-6).  Graded: loc_near
        -- {level="here"} is not_found and names the copy one raw level up;
        -- {level="here", deck=true} answers 2512,3463,1; cross_trap without
        -- loc_level presses nothing and leaves the player on the ledge; with
        -- loc_level = 1 it presses the raw-level-1 copy, the server's barrel
        -- line is in the detail, and world.tile reads 2527,3413,0.  The goto
        -- is the ledge itself, a starting point (rovingelves reaches it
        -- through the falls).
        seam("seam.bridge_deck_loc_named_by_loc_level", function()
            local goto_tile = verb("player", "goto_tile")
            local tile = verb("world", "tile")
            local loc_near = verb("world", "loc_near")
            local fn = verb("player", "cross_trap")
            if not goto_tile then return missing("player", "goto_tile") end
            if not tile then return missing("world", "tile") end
            if not loc_near then return missing("world", "loc_near") end
            if not fn then return missing("player", "cross_trap") end
            local function leave(result, text)
                setup_cheat("::tele lumbridge")
                settle(2)
                return result, text
            end
            local goto_result, goto_detail = goto_tile(2511, 3463, 0)
            if goto_result ~= "ok" then
                return leave("no_subject", "goto the ledge 2511,3463,0 -> " .. describe(goto_result) .. " "
                    .. describe(goto_detail))
            end
            settle(2)
            local here_result, here_detail = loc_near("barrel_waterfall_quest", 5, { level = "here" })
            local text = "loc_near(level=here) -> " .. describe(here_result) .. " " .. tostring(here_detail)
            if here_result == "ok" then
                return leave("refused", text .. " -- a plane-0 filter answered a raw-level-1 copy")
            end
            if string.find(tostring(here_detail), "the copy at 2512,3463,1 is one raw level up", 1, true) == nil then
                return leave("refused", text .. " -- the not_found does not name the deck copy one raw level up")
            end
            local deck_result, deck_row = loc_near("barrel_waterfall_quest", 5, { level = "here", deck = true })
            text = text .. "; {level=here, deck=true} -> " .. describe(deck_result) .. " "
                .. (deck_result == "ok" and (deck_row.tile_x .. "," .. deck_row.tile_z .. "," .. deck_row.level)
                    or describe(deck_row))
            if deck_result ~= "ok" or deck_row.tile_x ~= 2512 or deck_row.tile_z ~= 3463 or deck_row.level ~= 1 then
                return leave("refused", text .. " -- want the barrel at 2512,3463,1")
            end
            local plain_result, plain_detail = fn({ loc = "barrel_waterfall_quest", at = { 2512, 3463, 0 },
                src = { 2511, 3463 }, dest = { 2527, 3413 }, attempts = 1 })
            local still_result, still = tile()
            text = text .. "; cross_trap without loc_level -> " .. describe(plain_result) .. " (player "
                .. describe(still) .. ")"
            if plain_result == "ok" or still_result ~= "ok" or not is_table(still) or still.x ~= 2511
                or still.z ~= 3463 or still.level ~= 0 then
                return leave("refused", text .. " -- " .. tostring(plain_detail)
                    .. " -- a plane-0 `at` must name no raw-level-1 copy and press nothing")
            end
            local result, detail = fn({ loc = "barrel_waterfall_quest", op_name = "Get in",
                at = { 2512, 3463, 0 }, loc_level = 1, src = { 2511, 3463 }, dest = { 2527, 3413 }, attempts = 1 })
            text = text .. "; with loc_level=1 -> " .. describe(result) .. " " .. tostring(detail)
            if result ~= "ok" then
                return leave(result, text)
            end
            if string.find(text, "click_loc(barrel_waterfall_quest at 2512,3463,1 (raw level; the player on plane 0)",
                    1, true) == nil or string.find(text, "landed 2527,3413,0", 1, true) == nil then
                return leave("hollow", text .. " -- ok, but the detail names no raw-level-1 press or no landing")
            end
            if string.find(text, "You climb in the barrel and start rocking.", 1, true) == nil then
                return leave("hollow", text .. " -- ok, but the server's barrel line is not in the detail")
            end
            local end_result, finish = tile()
            if end_result ~= "ok" or not is_table(finish) or finish.x ~= 2527 or finish.z ~= 3413
                or finish.level ~= 0 then
                return leave("hollow", text .. " -- ok, but world.tile reads " .. describe(finish))
            end
            return leave("ok", text)
        end)

        -- A REAL TELEPORT CAST, RUNES READ.  Camelot Teleport pressed in the
        -- spellbook: magic_spells.dbrow [magic_spell_teleport_camelot] costs
        -- 5 air and 1 law, tele_coord 2757,3478,0, map_findsquare radius 2
        -- (teleport.rs2 [label,magic_teleport]; misc.lua camelotTeleport.*).
        -- Staged: an empty backpack holding exactly the cost, Magic 45.  The
        -- verb writes conformance.camelotTeleport.cast/.runes/.landed itself;
        -- this row forwards its answer and re-reads the pack (empty of both)
        -- and the tile.
        step("player.teleport_cast", function()
            local fn = verb("player", "teleport_cast")
            local count = verb("inv", "count")
            local tile = verb("world", "tile")
            if not fn then return missing("player", "teleport_cast") end
            if not count then return missing("inv", "count") end
            if not tile then return missing("world", "tile") end
            local function leave(result, text)
                setup_cheat("::tele lumbridge")
                setup_cheat("::setlevel magic 1")
                setup_cheat("::setlevel agility 1")
                setup_cheat("::setlevel hitpoints 10")
                settle(2)
                return result, text
            end
            setup_cheat("::clearinv")
            setup_cheat("::setlevel magic 45")
            setup_cheat("::give airrune 5")
            setup_cheat("::give lawrune 1")
            settle(2)
            local _, air = count("airrune")
            local _, law = count("lawrune")
            if air ~= 5 or law ~= 1 then
                return leave("no_subject", "the stage left airrune " .. describe(air) .. ", lawrune "
                    .. describe(law) .. " (want 5 and 1)")
            end
            local result, detail = fn("camelot_teleport", { 2757, 3478, 0 }, { name = "conformance.camelotTeleport",
                runes = { { "airrune", 5 }, { "lawrune", 1 } }, where = "Camelot" })
            local text = describe(result) .. " " .. tostring(detail)
            if result ~= "ok" then
                return leave(result, text)
            end
            local _, air_after = count("airrune")
            local _, law_after = count("lawrune")
            local end_result, finish = tile()
            text = text .. "; re-read: airrune " .. describe(air_after) .. ", lawrune " .. describe(law_after)
                .. ", at " .. (end_result == "ok" and is_table(finish)
                    and (finish.x .. "," .. finish.z .. "," .. finish.level) or describe(end_result))
            if air_after ~= 0 or law_after ~= 0 then
                return leave("hollow", text .. " -- ok, but the runes are still in the pack")
            end
            if end_result ~= "ok" or not is_table(finish) or finish.level ~= 0
                or math.abs(finish.x - 2757) > 2 or math.abs(finish.z - 3478) > 2 then
                return leave("hollow", text .. " -- ok, but the player is not within 2 of 2757,3478,0")
            end
            return leave("ok", text)
        end)

        -- b60 seam camera_detaches_from_player_after_walk_triggered_rebuild.
        -- A player who slipped at a Regicide pitfall walked on while his model,
        -- camera and minimap stayed at the pit: the slip played human_death,
        -- whose last frame holds 20,000 client cycles, and while a primary seq
        -- with postanim DELAYMOVE plays the client holds every walk the
        -- server sends (World_MoverHeldByAnim == Client-TS Client.ts routeMove).
        -- The content now ends the fall a tick later, as LostCity's spike pit
        -- does (upass_grid.rs2 upass_fail_grid). Agility 1 slips often; the
        -- row presses the south pitfall back and forth until a press slips
        -- and a later one lands, then walks 8 tiles and reads the eye: at yaw
        -- 0 / pitch 383 / zoom 600 it stands 5..9 tiles south of the player
        -- it follows.
        seam("seam.slip_fall_releases_the_walk", function()
            local goto_tile = verb("player", "goto_tile")
            local click_loc = verb("player", "click_loc")
            local walk_to = verb("player", "walk_to")
            local tile = verb("world", "tile")
            local camera = verb("world", "camera")
            local pose = verb("drive", "camera")
            local read = verb("skill", "read")
            if not goto_tile then return missing("player", "goto_tile") end
            if not click_loc then return missing("player", "click_loc") end
            if not walk_to then return missing("player", "walk_to") end
            if not tile then return missing("world", "tile") end
            if not camera then return missing("world", "camera") end
            if not pose then return missing("drive", "camera") end
            if not read then return missing("skill", "read") end
            local function here()
                local result, at = tile()
                if result ~= "ok" or not is_table(at) then
                    return nil
                end
                return at
            end
            local hp_result, hp = read("hitpoints")
            local agility_result, agility = read("agility")
            local hp_base = (hp_result == "ok" and is_table(hp)) and hp.base_level or nil
            local agility_base = (agility_result == "ok" and is_table(agility)) and agility.base_level or nil
            if hp_base == nil or agility_base == nil then
                return "no_subject", "skill.read hitpoints -> " .. describe(hp_result)
                    .. ", agility -> " .. describe(agility_result)
            end
            local function restore()
                setup_cheat("::setlevel hitpoints " .. hp_base)
                setup_cheat("::setlevel agility " .. agility_base)
                setup_cheat("::tele lumbridge")
                settle(4)
            end
            setup_cheat("::setlevel hitpoints 99")
            setup_cheat("::setlevel agility 1")
            local goto_result, goto_detail = goto_tile(2274, 3176, 0)
            if goto_result ~= "ok" then
                restore()
                return "no_subject", "goto 2274,3176,0 -> " .. describe(goto_result) .. " " .. describe(goto_detail)
            end
            local north = { at = { 2274, 3175, 0 }, from = { 2274, 3176 }, to = { 2274, 3172 }, away = { 2272, 3164 } }
            local south = { at = { 2274, 3173, 0 }, from = { 2274, 3172 }, to = { 2274, 3176 }, away = { 2275, 3184 } }
            local side = north
            local slips, presses = 0, 0
            local landed_after_slip = false
            while presses < 16 and not landed_after_slip do
                presses = presses + 1
                click_loc("regicide_pitfall_side", 1, { at = side.at })
                settle(6)
                local at = here()
                if at == nil then
                    restore()
                    return "no_subject", "world.tile unreadable after press " .. presses
                end
                if at.x == side.to[1] and at.z == side.to[2] then
                    if slips > 0 then
                        landed_after_slip = true
                    else
                        side = (side == north) and south or north
                    end
                elseif at.x == side.from[1] and at.z == side.from[2] then
                    slips = slips + 1
                else
                    restore()
                    return "no_subject", "press " .. presses .. " left the player at " .. at.x .. "," .. at.z
                        .. "," .. at.level .. " (neither side of the pitfall)"
                end
            end
            if not landed_after_slip then
                restore()
                return "no_subject", presses .. " presses, " .. slips .. " slip(s), no landing after a slip"
            end
            walk_to(side.away[1], side.away[2], 30)
            settle(2)
            pose(0, 383, 600)
            settle(2)
            local at = here()
            local cam = camera()
            restore()
            if at == nil or not is_table(cam) or cam.x == nil then
                return "no_subject", "tile/camera unreadable after the walk: camera -> " .. describe(cam)
            end
            local dx, dz = cam.x - at.x, cam.z - at.z
            local text = slips .. " slip(s) in " .. presses .. " presses, landed on " .. side.to[1] .. "," .. side.to[2]
                .. ", walked to " .. at.x .. "," .. at.z .. "; eye " .. cam.x .. "," .. cam.z .. " (d " .. dx .. ","
                .. dz .. ") yaw " .. tostring(cam.yaw) .. " pitch " .. tostring(cam.pitch)
            if at.x ~= side.away[1] or at.z ~= side.away[2] then
                return "refused", text .. " -- the walk did not arrive"
            end
            if math.abs(dx) > 1 or dz > -4 or dz < -10 then
                return "refused", text .. " -- want the eye 4..10 tiles south of the player: the camera"
                    .. " stayed with a model the slip's death pose held at the pit"
            end
            return "ok", text
        end)

        -- b60 seam1 spell_left_selected_after_a_fight_and_no_climb_verb, half
        -- one: A STAIRCASE CLIMBED BY ITS OWN OP, LEVEL AND LANDING READ.
        -- Crest, idesofmilk, vampire and fenkenstrain each hand-wrote a
        -- climb() for this.  Lumbridge castle's spiral staircase has no
        -- maplink row: ladders.rs2 [proc,climb] moves the player one plane on
        -- the tile it stands on, so the row stands on the src tile 3205,3228
        -- and lands on the same x,z a floor up (spiralstairsbottom_3 op1
        -- Climb-up at 3204,3229,0), then comes down by spiralstairsmiddle's
        -- op3 Climb-down at 3204,3229,1 (idesofmilk b60 rows 45 and 52).
        -- Graded on each answer naming the press and the landing, on
        -- world.tile after each, and on a press from the wrong floor being
        -- refused with nothing pressed.
        stage(function()
            setup_cheat("::goto 3207 3227 0")                   -- setup
            settle(3)
        end)
        step("player.climb", function()
            local tile = verb("world", "tile")
            local fn = verb("player", "climb")
            if not tile then return missing("world", "tile") end
            if not fn then return missing("player", "climb") end
            local function leave(result, text)
                setup_cheat("::tele lumbridge")
                settle(2)
                return result, text
            end
            local function at_text()
                local result, at = tile()
                if result ~= "ok" or not is_table(at) then
                    return nil, describe(result)
                end
                return at, at.x .. "," .. at.z .. "," .. at.level
            end
            local wrong_result, wrong_detail = fn({ loc = "spiralstairsmiddle", op = 3, op_name = "Climb-down",
                at = { 3204, 3229, 1 }, dest = { 3205, 3228, 0 } })
            local still, still_text = at_text()
            local text = "from level 0, the level-1 stair -> " .. describe(wrong_result) .. " " .. tostring(wrong_detail)
            if wrong_result ~= "refused" or string.find(tostring(wrong_detail), "not pressed", 1, true) == nil
                or still == nil or still.level ~= 0 then
                return leave("hollow", text .. " -- a press from the wrong floor must be refused unpressed; at "
                    .. tostring(still_text))
            end
            local up_result, up_detail = fn({ loc = "spiralstairsbottom_3", op = 1, op_name = "Climb-up",
                at = { 3204, 3229, 0 }, src = { 3205, 3228 }, dest = { 3205, 3228, 1 } })
            text = text .. " | up -> " .. describe(up_result) .. " " .. tostring(up_detail)
            if up_result ~= "ok" then
                return leave(up_result, text)
            end
            local up_at, up_text = at_text()
            text = text .. "; world.tile " .. tostring(up_text)
            if string.find(tostring(up_detail), "landed on level 1 at 3205,3228,1", 1, true) == nil
                or string.find(tostring(up_detail), "click_loc ->", 1, true) == nil then
                return leave("hollow", text .. " -- ok, but the detail names no press or no landing")
            end
            if up_at == nil or up_at.x ~= 3205 or up_at.z ~= 3228 or up_at.level ~= 1 then
                return leave("hollow", text .. " -- ok, but world.tile is not 3205,3228,1")
            end
            local down_result, down_detail = fn({ loc = "spiralstairsmiddle", op = 3, op_name = "Climb-down",
                at = { 3204, 3229, 1 }, dest = { 3205, 3228, 0 } })
            text = text .. " | down -> " .. describe(down_result) .. " " .. tostring(down_detail)
            if down_result ~= "ok" then
                return leave(down_result, text)
            end
            local down_at, down_text = at_text()
            text = text .. "; world.tile " .. tostring(down_text)
            if down_at == nil or down_at.x ~= 3205 or down_at.z ~= 3228 or down_at.level ~= 0 then
                return leave("hollow", text .. " -- ok, but world.tile is not 3205,3228,0")
            end
            return leave("ok", text)
        end)

        -- b62 seam1 climb_refuses_a_same_level_landing_into_another_map_frame:
        -- A CLIMB THAT CHANGES NO LEVEL.  Most of the underground is level 0
        -- in the map frame z+6400, so a manhole ladder between it and the
        -- street lands on the level it was pressed from, and climb raised
        -- "climb spec.dest is on the press's own level" (Demon Slayer's
        -- sewer, Plague City's basement, Family Crest's trapdoors and
        -- Vampire Slayer's crypt were each graded through cross_trap or a
        -- hand-written helper instead).  Varrock's sewer ladder,
        -- maplink.dbrow [maplink_0_50_154_37_2_up] 0_50_154_37_2 ->
        -- 0_50_54_36_2: 3237,9858,0 -> 3236,3458,0, keyed on the PLAYER's
        -- tile (maplink.rs2), so the press is taken standing on 3237,9858.
        -- Graded on the answer naming the frame change and the landing, on
        -- world.tile after it, and on a second call from the landing itself
        -- being refused unpressed (a same-level landing is graded by tile).
        stage(function()
            setup_cheat("::goto 3236 9858 0")                   -- setup
            settle(3)
        end)
        seam("seam.climb_lands_on_its_own_level_in_another_map_frame", function()
            local tile = verb("world", "tile")
            local fn = verb("player", "climb")
            if not tile then return missing("world", "tile") end
            if not fn then return missing("player", "climb") end
            local function leave(result, text)
                setup_cheat("::tele lumbridge")
                settle(2)
                return result, text
            end
            local function at_text()
                local result, at = tile()
                if result ~= "ok" or not is_table(at) then
                    return nil, describe(result)
                end
                return at, at.x .. "," .. at.z .. "," .. at.level
            end
            local start, start_text = at_text()
            if start == nil or start.level ~= 0 or start.z < 6400 then
                return leave("no_subject", "::goto 3236 9858 0 left the player at " .. tostring(start_text)
                    .. ", not in the sewer")
            end
            local up_result, up_detail = fn({ loc = "fai_varrock_manhole_ladder", op = 1, op_name = "Climb-up",
                at = { 3237, 9858, 0 }, src = { 3237, 9858 }, dest = { 3236, 3458, 0 } })
            local text = "from " .. tostring(start_text) .. ", up -> " .. describe(up_result) .. " " .. tostring(up_detail)
            if up_result ~= "ok" then
                return leave(up_result, text)
            end
            local up_at, up_text = at_text()
            text = text .. "; world.tile " .. tostring(up_text)
            if string.find(tostring(up_detail), "same level 0, map frame 1 -> 0", 1, true) == nil
                or string.find(tostring(up_detail), "landed on level 0 at 3236,3458,0", 1, true) == nil
                or string.find(tostring(up_detail), "click_loc ->", 1, true) == nil then
                return leave("hollow", text .. " -- ok, but the detail names no press, frame change or landing")
            end
            if up_at == nil or up_at.x ~= 3236 or up_at.z ~= 3458 or up_at.level ~= 0 then
                return leave("hollow", text .. " -- ok, but world.tile is not 3236,3458,0")
            end
            local again_result, again_detail = fn({ loc = "fai_varrock_manhole_ladder", op = 1, op_name = "Climb-up",
                at = { 3237, 9858, 0 }, dest = { 3236, 3458, 0 } })
            local still, still_text = at_text()
            text = text .. " | again from the landing -> " .. describe(again_result) .. " " .. tostring(again_detail)
            if again_result ~= "refused" or string.find(tostring(again_detail), "on the landing -- not pressed", 1, true) == nil
                or still == nil or still.x ~= 3236 or still.z ~= 3458 or still.level ~= 0 then
                return leave("hollow", text .. " -- a call from the landing itself must be refused unpressed; at "
                    .. tostring(still_text))
            end
            return leave("ok", text)
        end)

        -- b63 seam1 climb_has_no_chat_for_a_guarded_ladder: A GUARDED
        -- LADDER SPEAKS BEFORE IT MOVES THE PLAYER.  The Watchtower's
        -- towerladder 2544,3111,0 (quest_itwatchtower.rs2 [oploc1,towerladder])
        -- shows the tower guard's page "It is the wizards' helping hand - let
        -- 'em up." once the quest has started, and only after it is continued
        -- reaches if_close + ~climb_ladder(1); climb without `chat` waited its
        -- ticks under the page and the b62 itwatchtower test hand-graded the
        -- climb with click_loc.  Graded, from 2544,3112,0 at
        -- ^itwatchtower_started: up WITHOUT chat is not ok, still on level 0,
        -- and names the page up (then the page is played and the player is on
        -- level 1 -- the page held the climb); down the silent first-floor
        -- ladder top (qip_watchtower_ladder_top 2544,3111,1) with chat and
        -- chat_optional is ok with "no page opened"; up WITH chat is ok, its
        -- detail carrying the guard's whole line and chat.play's ok, and
        -- world.tile reads 2544,3112,1; down the ladder top with chat and no
        -- chat_optional is `refused` although the press landed.
        stage(function()
            setup_cheat("::setvar varp212_itwatchtower ^itwatchtower_started")   -- setup
            setup_cheat("::goto 2544 3112 0")
            settle(3)
        end)
        seam("seam.climb_plays_a_guards_page", function()
            local tile = verb("world", "tile")
            local play = verb("chat", "play")
            local fn = verb("player", "climb")
            if not tile then return missing("world", "tile") end
            if not play then return missing("chat", "play") end
            if not fn then return missing("player", "climb") end
            local function leave(result, text)
                setup_cheat("::setvar varp212_itwatchtower 0")
                setup_cheat("::tele lumbridge")
                settle(2)
                return result, text
            end
            local function at_text()
                local result, at = tile()
                if result ~= "ok" or not is_table(at) then
                    return nil, describe(result)
                end
                return at, at.x .. "," .. at.z .. "," .. at.level
            end
            local up = { loc = "towerladder", op = 1, op_name = "Climb-up",
                at = { 2544, 3111, 0 }, src = { 2544, 3112 }, dest = { 2544, 3112, 1 } }
            local function down(extra)
                local spec = { loc = "qip_watchtower_ladder_top", op = 1, op_name = "Climb-down",
                    at = { 2544, 3111, 1 }, src = { 2544, 3112 }, dest = { 2544, 3112, 0 } }
                for key, value in pairs(extra) do
                    spec[key] = value
                end
                return fn(spec)
            end
            local start, start_text = at_text()
            if start == nil or start.level ~= 0 or start.x ~= 2544 or start.z ~= 3112 then
                return leave("no_subject", "::goto 2544 3112 0 left the player at " .. tostring(start_text))
            end
            local bare_result, bare_detail = fn(up)
            local held, held_text = at_text()
            local text = "from " .. tostring(start_text) .. ", up without chat -> " .. describe(bare_result) .. " "
                .. tostring(bare_detail) .. "; world.tile " .. tostring(held_text)
            if bare_result == "ok" or held == nil or held.level ~= 0
                or string.find(tostring(bare_detail), "a page is up: npc 'It is the wizards' helping hand", 1, true) == nil
                or string.find(tostring(bare_detail), "pass spec.chat", 1, true) == nil then
                return leave("hollow", text .. " -- want not ok on level 0 naming the guard's page and spec.chat")
            end
            local page_result, page_detail = play({ "npc:It is the wizards' helping hand" })
            settle(4)
            local freed, freed_text = at_text()
            text = text .. " | the page played -> " .. describe(page_result) .. " " .. tostring(page_detail)
                .. "; world.tile " .. tostring(freed_text)
            if page_result ~= "ok" or freed == nil or freed.level ~= 1 then
                return leave("no_subject", text .. " -- the page did not release the climb to level 1")
            end
            local quiet_result, quiet_detail = down({ chat = { "npc:*" },
                chat_optional = "conformance: the ladder top never speaks" })
            text = text .. " | down, chat_optional -> " .. describe(quiet_result) .. " " .. tostring(quiet_detail)
            if quiet_result ~= "ok"
                or string.find(tostring(quiet_detail), "no page opened", 1, true) == nil
                or string.find(tostring(quiet_detail), "chat_optional: conformance", 1, true) == nil
                or string.find(tostring(quiet_detail), "landed on level 0 at 2544,3112,0", 1, true) == nil then
                return leave(quiet_result == "ok" and "hollow" or quiet_result,
                    text .. " -- want ok with 'no page opened', the chat_optional reason and the landing")
            end
            local guarded = { chat = { "npc:It is the wizards' helping hand" } }
            for key, value in pairs(up) do
                guarded[key] = value
            end
            local in_result, in_detail = fn(guarded)
            text = text .. " | up with chat -> " .. describe(in_result) .. " " .. tostring(in_detail)
            if in_result ~= "ok" then
                return leave(in_result, text)
            end
            if string.find(tostring(in_detail), "the press opened npc 'It is the wizards' helping hand - let 'em up.'",
                    1, true) == nil
                or string.find(tostring(in_detail), "chat.play -> ok", 1, true) == nil
                or string.find(tostring(in_detail), "landed on level 1 at 2544,3112,1", 1, true) == nil then
                return leave("hollow", text .. " -- ok, but the detail carries no guard's line, chat.play ok or landing")
            end
            local top, top_text = at_text()
            text = text .. "; world.tile " .. tostring(top_text)
            if top == nil or top.x ~= 2544 or top.z ~= 3112 or top.level ~= 1 then
                return leave("hollow", text .. " -- ok, but world.tile is not 2544,3112,1")
            end
            local mute_result, mute_detail = down({ chat = { "npc:*" } })
            text = text .. " | down, chat -> " .. describe(mute_result) .. " " .. tostring(mute_detail)
            if mute_result ~= "refused"
                or string.find(tostring(mute_detail), "no page opened", 1, true) == nil
                or string.find(tostring(mute_detail), "the press landed 2544,3112,0", 1, true) == nil then
                return leave("hollow", text .. " -- want refused: chat= named a page the press never opened")
            end
            return leave("ok", text)
        end)

        -- Half two: A SELECTION NOBODY SPENT IS CANCELLED.  The client drops
        -- an armed spell (app->targetsel) only at a menu row's doAction tail
        -- or a left click off any target; a re-cast whose presses all
        -- answered `covered` left Family Crest's fire blast armed after
        -- Chronozon and every later world press read `covered ... menu rows:
        -- <Cancel>` (crest b60 run 1 rows 138, 147).  The verb row arms Wind
        -- Strike and cancels it: graded on was_armed, on the menu it read
        -- having no Walk here before and Walk here after, and on a second
        -- call reading nothing armed.
        step("player.cancel_selection", function()
            local arm = verb("player", "_arm_spell")
            local fn = verb("player", "cancel_selection")
            if not arm then return missing("player", "_arm_spell") end
            if not fn then return missing("player", "cancel_selection") end
            local arm_result, arm_detail = arm("wind_strike")
            if arm_result ~= "ok" then
                return "no_subject", "arming wind_strike -> " .. describe(arm_result) .. " " .. describe(arm_detail)
            end
            local result, detail, was_armed = fn("conformance: wind_strike armed")
            local text = describe(result) .. " " .. tostring(detail) .. " was_armed=" .. tostring(was_armed)
            if result ~= "ok" then
                return result, text
            end
            if was_armed ~= true or string.find(tostring(detail), "(conformance: wind_strike armed)", 1, true) == nil
                or string.find(tostring(detail), "no Walk here", 1, true) == nil
                or string.find(tostring(detail), "now offers", 1, true) == nil
                or string.find(tostring(detail), "<Walk here>", 1, true) == nil then
                return "hollow", text .. " -- ok, but it did not read the armed menu and Walk here back"
            end
            local again_result, again_detail, again_armed = fn()
            text = text .. " | again -> " .. describe(again_result) .. " " .. tostring(again_detail)
                .. " was_armed=" .. tostring(again_armed)
            if again_result ~= "ok" or again_armed ~= false then
                return "hollow", text .. " -- the second call must read nothing armed"
            end
            return "ok", text
        end)

        -- THE CAST FIGHT ENDS WITH NOTHING ARMED.  Two places leave a spell
        -- armed and both now cancel it (spell.lua): npc.await_dead_engaged's
        -- re-cast wrap when the fight ends, and a cast press that missed on
        -- every try.  Staged: Fire Blast stamps a fight on a spawned Man, Wind
        -- Strike is armed after it (what a covered re-cast left), the Man is
        -- killed with attempts 0 so no re-cast spends the arming; the wait's
        -- detail must name the cancel and a cancel_selection after it must
        -- read nothing armed.  Then a press aimed at an element no npc
        -- carries (covered on every press) must name the cancel too.
        stage(function()
            setup_cheat("::clearinv")                           -- setup
            setup_cheat("::setlevel magic 99")
            setup_cheat("::setlevel hitpoints 99")
            setup_cheat("::setlevel defence 99")
            setup_cheat("::give airrune 100")
            setup_cheat("::give firerune 50")
            setup_cheat("::give deathrune 10")                  -- fire blast: 4 air, 5 fire, 1 death
            setup_cheat("::goto 3222 3219 0")
            settle(2)
            setup_cheat("::spawn man")                          -- setup
            settle(2)
        end)
        seam("seam.cast_fight_ends_with_nothing_armed", function()
            local cast = verb("player", "cast")
            local arm = verb("player", "_arm_spell")
            local wait = verb("npc", "await_dead_engaged")
            local cancel = verb("player", "cancel_selection")
            local missed = verb("player", "_cast_press_on_element")
            if not cast then return missing("player", "cast") end
            if not arm then return missing("player", "_arm_spell") end
            if not wait then return missing("npc", "await_dead_engaged") end
            if not cancel then return missing("player", "cancel_selection") end
            if not missed then return missing("player", "_cast_press_on_element") end
            local function leave(result, text)
                setup_cheat("::clearinv")
                setup_cheat("::setlevel magic 1")
                setup_cheat("::setlevel hitpoints 10")
                setup_cheat("::setlevel defence 1")
                setup_cheat("::tele lumbridge")
                settle(2)
                return result, text
            end
            local cast_result, cast_detail = cast("fire_blast", "man", 14)
            if cast_result ~= "ok" then
                return leave("no_subject", "cast fire_blast on man -> " .. describe(cast_result) .. " "
                    .. describe(cast_detail))
            end
            local arm_result, arm_detail = arm("wind_strike")
            if arm_result ~= "ok" then
                return leave("no_subject", "arming wind_strike -> " .. describe(arm_result) .. " " .. describe(arm_detail))
            end
            setup_cheat("::kill man")
            local result, detail = wait(20, 0)
            local text = "await_dead_engaged(20, 0) with wind_strike armed -> " .. describe(result) .. " "
                .. tostring(detail)
            if result == "refused" then
                return leave(result, text)
            end
            if string.find(tostring(detail), "fight over with a selection still armed -- cancel_selection: a selection WAS armed", 1, true) == nil then
                return leave("refused", text .. " -- the wait ended without cancelling the armed spell")
            end
            local after_result, after_detail, after_armed = cancel()
            text = text .. " | then cancel_selection -> " .. describe(after_result) .. " " .. tostring(after_detail)
            if after_result ~= "ok" or after_armed ~= false then
                return leave("refused", text .. " -- something is still armed after the wait")
            end
            setup_cheat("::spawn man")
            settle(2)
            local press_result, press_detail = missed("wind_strike", "man", 7777777)
            text = text .. " | missed press -> " .. describe(press_result) .. " " .. tostring(press_detail)
            if press_result == "ok" or string.find(tostring(press_detail), "selection WAS armed", 1, true) == nil then
                return leave("refused", text .. " -- a missed cast press must cancel the arming it made")
            end
            local last_result, last_detail, last_armed = cancel()
            text = text .. " | then cancel_selection -> " .. describe(last_result) .. " " .. tostring(last_detail)
            if last_result ~= "ok" or last_armed ~= false then
                return leave("refused", text .. " -- something is still armed after the missed press")
            end
            return leave("ok", text)
        end)

        -- b63 seam1 no_eating_inside_attack_and_re_engage_presses: AN ATTACK
        -- PRESS EATS.  t.player.attack takes opts.eat (the await verbs'
        -- table): it eats before the press, and a press made while the
        -- hitpoints still read under the line after that eat is the bounded
        -- fast press, never the pose-and-probe hunt that cannot stop to eat
        -- (Haunted Mine runs 12, 15 and 18 died inside it, hp 95 -> 0).
        -- Staged so the line is always crossed: 99/99 hitpoints and
        -- `below = 100` (an eat at full health is still an eat, food.rs2
        -- @eat_food has no hitpoints test).  Graded on the eat tag naming an
        -- eat "inside an attack press (before the press" and a press "made by
        -- the fast path because hp was under 100", on the backpack's sharks
        -- going down, and on the same press WITHOUT opts.eat naming neither.
        stage(function()
            setup_cheat("::clearinv")                           -- setup
            setup_cheat("::setlevel hitpoints 99")
            setup_cheat("::give shark 6")
            setup_cheat("::goto 3229 3233 0")                   -- setup: the goblin field (spawns 3230-3231,3234)
            settle(2)
            setup_cheat("::spawn goblin_unarmed_melee_1")       -- setup
            setup_cheat("::passive goblin_unarmed_melee_1")
            settle(3)
        end)
        seam("seam.attack_eats_inside_its_press", function()
            local attack = verb("player", "attack")
            local count = verb("inv", "count")
            local tiles = verb("npc", "tiles")
            if not attack then return missing("player", "attack") end
            if not count then return missing("inv", "count") end
            if not tiles then return missing("npc", "tiles") end
            local GOBLIN = "goblin_unarmed_melee_1"
            local function leave(result, text)
                setup_cheat("::kill " .. GOBLIN .. " 12")
                setup_cheat("::kill " .. GOBLIN .. " 12")
                setup_cheat("::clearinv")
                setup_cheat("::setlevel hitpoints 10")
                setup_cheat("::tele lumbridge")
                settle(2)
                return result, text
            end
            local r0, _, rows = tiles(GOBLIN, 4)
            if r0 ~= "ok" or not is_table(rows) or #rows < 1 then
                return leave("no_subject", "no goblin within 4 (" .. describe(rows and #rows) .. ")")
            end
            local _, sharks_before = count("shark")
            local plain_result, plain_detail = attack(GOBLIN, COMBAT_ATTACK_OP, 10, { slot = rows[1].slot })
            local _, sharks_plain = count("shark")
            local text = "without opts.eat, slot " .. tostring(rows[1].slot) .. " -> " .. describe(plain_result)
                .. " " .. tostring(plain_detail) .. "; shark " .. describe(sharks_before) .. " -> "
                .. describe(sharks_plain)
            if string.find(tostring(plain_detail), "inside an attack press", 1, true) ~= nil
                or sharks_plain ~= sharks_before then
                return leave("refused", text .. " -- a press with no eater ate")
            end
            -- The same copy: a second goblin would be single-way combat's
            -- "I'm already under attack." (the first press opened a fight).
            local eat_result, eat_detail = attack(GOBLIN, COMBAT_ATTACK_OP, 10,
                { slot = rows[1].slot, eat = { item = "shark", below = 100 } })
            local _, sharks_after = count("shark")
            text = text .. " | with opts.eat {shark, below 100}, slot " .. tostring(rows[1].slot) .. " -> "
                .. describe(eat_result) .. " " .. tostring(eat_detail) .. "; shark " .. describe(sharks_plain)
                .. " -> " .. describe(sharks_after)
            if eat_result ~= "ok" and eat_result ~= "timeout" then
                return leave(eat_result, text)
            end
            if string.find(tostring(eat_detail), "inside an attack press (before the press", 1, true) == nil
                or string.find(tostring(eat_detail), "made by the fast path because hp was under 100", 1, true) == nil
                or (sharks_after or 0) >= (sharks_plain or 0) then
                return leave("refused", text .. " -- want an eat before the press, the fast press under the"
                    .. " line, and fewer sharks")
            end
            return leave("ok", text)
        end)

        -- SEAM region_music_unlock_writes_the_music_variable_index_into_raw_varps
        -- (b61-seam1): DBTable 44's unlock pair is (music VARIABLE 1-27, bit),
        -- and the engine's region-music table carried the variable as if it
        -- were a varp id, so walking into Draynor Village (square 48,50,
        -- "Unknown Land" = variable 5 bit 5) OR'd bit 5 into %varp5_grail
        -- (spoken_crone 4 -> 36, the Grail whistle then went to the restored
        -- realm).  music.varp: variable 5 is [varp24_musicmulti_5].  Walked,
        -- not teleported: from square 49,50 over the open road west.
        stage(function()
            setup_cheat("::clearinv")                           -- setup
            setup_cheat("::goto 3150 3228 0")                   -- square 49,50 (Dream)
            settle(2)
            setup_cheat("::setvar varp5_grail 4")               -- setup: spoken_crone
            setup_cheat("::setvar varp24_musicmulti_5 0")       -- setup
            settle(2)
        end)
        seam("seam.region_music_unlock_writes_the_musicmulti_varp", function()
            local walk_route = verb("player", "walk_route")
            local server = verb("var", "server")
            if not walk_route then return missing("player", "walk_route") end
            if not server then return missing("var", "server") end
            local _, grail_before = server("varp5_grail")
            local _, music_before = server("varp24_musicmulti_5")
            if grail_before ~= 4 or music_before ~= 0 then
                return "no_subject", "staging read varp5_grail=" .. tostring(grail_before)
                    .. " varp24_musicmulti_5=" .. tostring(music_before) .. " (wanted 4 and 0)"
            end
            local walk_result, walk_detail = walk_route({ { 3141, 3228 }, { 3133, 3228 }, { 3125, 3228 } })
            settle(3)
            local _, grail_after = server("varp5_grail")
            local _, music_after = server("varp24_musicmulti_5")
            local text = "walk_route into square 48,50 -> " .. describe(walk_result) .. " "
                .. tostring(walk_detail) .. " | varp5_grail 4 -> " .. tostring(grail_after)
                .. ", varp24_musicmulti_5 0 -> " .. tostring(music_after)
            setup_cheat("::setvar varp5_grail 0")
            setup_cheat("::tele lumbridge")
            settle(2)
            if walk_result ~= "ok" then
                return "no_subject", text
            end
            if grail_after ~= 4 then
                return "refused", text .. " -- the region unlock wrote the music VARIABLE index as a varp id"
            end
            if type(music_after) ~= "number" or (music_after // 32) % 2 ~= 1 then
                return "refused", text .. " -- musicmulti_5 bit 5 (Unknown Land) was not set"
            end
            return "ok", text
        end)

        -- seam.npc_death_waits_for_its_queue (seam pass matthew-mbp-m4-b61-seam1):
        -- [ai_queue3,black_knight_titan] hands the death to queue_defeat_titan(npc_uid)
        -- (quest_grail/scripts/black_knight_titan.rs2). The engine must keep the
        -- titan until that queue has decided (LostCity: NpcOps.ts NPC_DEL is the
        -- only removal). Without Excalibur: the message, %varp5_grail 4 -> 7 and
        -- the SAME titan (pool slot) standing; with it: "Well done!".
        seam("seam.npc_death_waits_for_its_queue", function()
            local await_msg = verb("msg", "await")
            local read_var = verb("var", "server")
            local present = verb("npc", "await_present")
            if not await_msg then return missing("msg", "await") end
            if not read_var then return missing("var", "server") end
            if not present then return missing("npc", "await_present") end
            local unequip = verb("player", "unequip")
            local drop = verb("player", "drop")
            local function leave(result, text)
                if unequip then unequip("excalibur") end
                if drop then drop("excalibur") end
                setup_cheat("::setvar varp5_grail 0")
                setup_cheat("::tele lumbridge")
                settle(2)
                return result, text
            end
            local function titan_slot()
                local r, d = present("black_knight_titan", 15, 1)
                return tonumber(string.match(tostring(d), "slot (%d+)") or ""), tostring(r) .. " " .. tostring(d)
            end
            setup_cheat("::setlevel attack 99")
            setup_cheat("::give excalibur 1")
            setup_cheat("::setvar varp5_grail 4")
            setup_cheat("::~tele 0_43_73_37_50")
            settle(3)
            local slot0, before = titan_slot()
            if slot0 == nil then
                return leave("hollow", "no black_knight_titan within 15 of 2789,4722: " .. before)
            end
            setup_cheat("::kill black_knight_titan")
            local ar, ad = await_msg("Maybe you need something more to beat the titan?", 20)
            settle(3)
            local vr, vd = read_var("varp5_grail")
            local slotA, after = titan_slot()
            local text = "no Excalibur: msg " .. tostring(ar) .. "; varp5_grail " .. tostring(vr) .. " "
                .. tostring(vd) .. "; titan slot " .. tostring(slot0) .. " -> " .. after
            if ar ~= "ok" or vr ~= "ok" or tonumber(vd) ~= 7 or slotA ~= slot0 then
                return leave("hollow", text .. " -- queue_defeat_titan must find the SAME titan, say so and "
                    .. "downgrade spoken_crone(4) -> failed_defeat_titan(7)")
            end
            local chat_play = verb("chat", "play")
            if chat_play then
                chat_play({ "npc:Puny mortal...", "npc:I..." })
            end
            setup_cheat("::wield excalibur")
            settle(2)
            setup_cheat("::kill black_knight_titan")
            local br, bd = await_msg("Well done! You have defeated the Black Knight Titan!", 20)
            text = text .. " | Excalibur: msg " .. tostring(br) .. " " .. tostring(bd)
            if br ~= "ok" then
                return leave("hollow", text .. " -- with Excalibur worn the queue must run its win branch")
            end
            return leave("ok", text)
        end)

        -- Conformance rows for waves seam pass 2, wave_enter_state_pause
        -- (t.wave.state / enter / await_wave / await_clear / pause / resume).
        --
        -- PLACEMENT: LAST of every row that reads the world, just before
        -- `finish`, by the pass's closer, after two earlier places went red
        -- downstream.  After the prayer rows (its author's place) it left the
        -- client's npc pool holding the arena's npcs after the leave (wave.resume
        -- reads `pool 20: harpie x5, nibbler x15` out of the run; CONTENT_BUGS.md
        -- ENG-19), and seam.no_row_is_not_a_kill went red twice on a FULL pool it
        -- could not vouch for.  Just before phase 9's relog, every row passed
        -- except seam.drain_survives_xp_gain, which read the drain as 7/60
        -- twice in a row (the stat_restore tick moved into its window: ENG-2).
        -- Here nothing after it reads the world.
        -- The block enters a PRACTICE Inferno run through the content's own
        -- debugproc and leaves it through the arena's Cave exit (a practice run
        -- leaves on that click, inferno.rs2:190), then its closing stage travels the
        -- player back to the Lumbridge tile the rows after it expect. Measured shapes:
        -- build/quest_gate/ws2_wave_a (20/20 PASS) and ws2_wave_b5 (20/20 PASS).
        -- The logout-button pause is NOT exercised here: under today's content it
        -- ends the session (ws2_wave_b5 row 17), which the rows after this block
        -- cannot survive; ws2_wave_b5 is its proof.
        --
        -- Uses the harness's own helpers: verb, missing, describe, is_table, step,
        -- stage. The return of each step is the verdict pair.

        -- the state outside any run: a table, nothing active
        step("wave.state", function()
            local fn = verb("wave", "state")
            if not fn then return missing("wave", "state") end
            local result, detail, s = fn()
            local text = "-> " .. describe(detail)
            if result ~= "ok" then
                return result, text
            end
            if not is_table(s) or not is_table(s.pillars) or not is_table(s.pillars.w) then
                return "hollow", "answered ok with no state table (pillars w/s/e) as its third return -- " .. text
            end
            if s.active ~= false or s.game ~= "inferno" then
                return "hollow", "outside a run the state read active=" .. describe(s.active) .. " -- " .. text
            end
            return "ok", text
        end)

        -- setup: Protect from Missiles up before the run starts.  Wave 1 holds a
        -- bat (inferno_creature_harpie, ranged), and wave.pause walks to the
        -- Cave exit under its fire: the closer's second conformance run DIED
        -- there (attempt 1, 'Oh dear, you are dead!' read at tick 300, 40
        -- hitpoints), the retry skipped wave.pause and left the practice run
        -- active, and wave.resume and session.login went red behind it.  The
        -- bat's hit honours the protection prayer (inferno_ai.rs2:35 via
        -- ~check_protect_prayer; prayer_flick's pf_b_final 0/3 hits with it up).
        stage(function()
            local set = verb("prayer", "set")
            if set then
                set("protectfrommissiles", true)
            end
            local tab = verb("ui", "tab")
            if tab then
                tab("inventory")
            end
            settle(1)
        end)

        -- the content's debugproc entry, a practice run at wave 1
        step("wave.enter", function()
            local fn = verb("wave", "enter")
            if not fn then return missing("wave", "enter") end
            local unsupported = fn("colosseum", 1)
            if unsupported ~= "unsupported" then
                return "hollow", "colosseum answered " .. describe(unsupported) .. ", not unsupported"
            end
            local result, detail, s = fn("inferno", 1)
            local text = "-> " .. describe(detail)
            if result ~= "ok" then
                return result, text
            end
            if not is_table(s) or s.active ~= true or s.wave ~= 1 or s.practice ~= true
                or not (s.alive > 0) or s.pool ~= s.alive then
                return "hollow", "entered but the state is not an active practice wave 1 with pool = alive -- " .. text
            end
            if s.pillars.w.hp ~= 255 or s.pillars.s.hp ~= 255 or s.pillars.e.hp ~= 255 then
                return "hollow", "the three pillars are not at 255 -- " .. text
            end
            local again = fn("inferno", 3)
            if again ~= "refused" then
                return "hollow", "a second enter without opts.restart answered " .. describe(again) .. " -- " .. text
            end
            return "ok", text
        end)

        step("wave.await_wave", function()
            local fn = verb("wave", "await_wave")
            if not fn then return missing("wave", "await_wave") end
            local result, detail = fn(1, 3)
            return result, "-> " .. describe(detail)
        end)

        -- nothing is killed, so the wave cannot clear: the verb must time out
        -- and name the last state, never answer ok
        step("wave.await_clear", function()
            local fn = verb("wave", "await_clear")
            if not fn then return missing("wave", "await_clear") end
            local result, detail = fn(4)
            local text = "-> " .. describe(result) .. " " .. describe(detail)
            if result ~= "timeout" then
                return "hollow", "with nothing killed it answered " .. describe(result) .. " -- " .. text
            end
            if not string.find(tostring(detail), "ACTIVE wave 1", 1, true) then
                return "hollow", "the timeout does not name the last state -- " .. text
            end
            return "ok", text
        end)

        -- the content's request path on a PRACTICE run leaves (inferno.rs2:190):
        -- the verb must say the run ended without a pause
        step("wave.pause", function()
            local fn = verb("wave", "pause")
            if not fn then return missing("wave", "pause") end
            local result, detail = fn({ via = "exit" })
            local text = "-> " .. describe(result) .. " " .. describe(detail)
            if result ~= "refused" or not string.find(tostring(detail), "ENDED", 1, true) then
                return "hollow", "a practice run's Cave exit did not read as an ended run -- " .. text
            end
            return "ok", text
        end)

        -- THE ARENA POOL IS EMPTY ONCE THE CLIENT IS OFF THE ARENA (waves seam3
        -- npc_pool_after_leave, ENG-19 settled as a driver read, not a stale pool).
        -- No verb changed; one SEAM row.
        --
        -- PLACEMENT: in test/quests/_conformance.lua's PLAN, immediately AFTER
        -- step("wave.pause", ...) (the practice run's Cave exit, which ends the run)
        -- and BEFORE step("wave.resume", ...).  It re-enters and leaves once more, so
        -- wave.resume after it still reads "no run is paused".
        --
        -- What it pins (measured: build/quest_gate/npa_pool_d, rows c1-c3):
        --   * a read taken the instant t.wave.pause returns is on the SERVER's clock
        --     (the run's varp) while the pool is the CLIENT's: at +0 the client's own
        --     player tile still reads the arena (6430,81) and the pool still holds the
        --     wave (`pool 20: harpie x5, nibbler x15` with 20 staged).  That was
        --     ENG-19's reading; it is not a stale pool.
        --   * one server tick later the client is on the exit pad and the pool is 0
        --     (the row awaits the client's own tile off the arena, <= 3 ticks, then reads);
        --   * a second enter reads exactly one wave (pool == alive).
        seam("seam.wave_pool_after_leave", function()
            local state = verb("wave", "state")
            local enter = verb("wave", "enter")
            local pause = verb("wave", "pause")
            if not state then return missing("wave", "state") end
            if not enter then return missing("wave", "enter") end
            if not pause then return missing("wave", "pause") end
            -- wait (<= 3 ticks) for the CLIENT to be off the arena: the leave's own
            -- tick may not have reached the client when wave.pause returns on the
            -- server's varp (ENG-19's reading); then the pool must be empty
            local function off_arena()
                t.await({ level = function()
                    local _, _, s = state()
                    return is_table(s) and is_table(s.tile) and s.tile.x < 6000
                end, note = "client off the arena" }, 3)
                return state()
            end
            -- the leave was the wave.pause row just before this one
            local r1, d1, s1 = off_arena()
            if r1 ~= "ok" or not is_table(s1) then
                return "hollow", "wave.state answered " .. describe(r1) .. " " .. describe(d1)
            end
            if s1.active or s1.pool ~= 0 or not is_table(s1.tile) or s1.tile.x >= 6000 then
                return "refused", "once the client is off the arena the run is not over with an empty pool -- " .. describe(d1)
            end
            local re, de, se = enter("inferno", 1)
            if re ~= "ok" or not is_table(se) then
                return re, "re-enter -> " .. describe(de)
            end
            if se.pool ~= se.alive or not (se.alive > 0) then
                return "refused", "the second enter does not read exactly one wave (pool " .. tostring(se.pool)
                    .. ", alive " .. tostring(se.alive) .. ") -- " .. describe(de)
            end
            local rp, dp = pause({ via = "exit" })
            if rp ~= "refused" or not string.find(tostring(dp), "ENDED", 1, true) then
                return "hollow", "the second Cave exit did not end the practice run -- " .. describe(dp)
            end
            local r2, d2, s2 = off_arena()
            if r2 ~= "ok" or not is_table(s2) or s2.active or s2.pool ~= 0 or not is_table(s2.tile) or s2.tile.x >= 6000 then
                return "refused", "after the second leave the pool is not empty -- " .. describe(d2)
            end
            return "ok", string.format("leave 1: %s | enter 2: alive %d pool %d | leave 2: %s",
                tostring(d1), se.alive, se.pool, tostring(d2))
        end)

        step("wave.resume", function()
            local fn = verb("wave", "resume")
            if not fn then return missing("wave", "resume") end
            local result, detail = fn()
            local text = "-> " .. describe(result) .. " " .. describe(detail)
            if result ~= "refused" or not string.find(tostring(detail), "no run is paused", 1, true) then
                return "hollow", "with no paused run it did not refuse -- " .. text
            end
            return "ok", text
        end)

        -- THE INFERNO IS ENTERED BY CLICK (waves seam pass 5, inferno_entry_pause_death_file: ENG-6, INF-AV-001,
        -- ENG-37, ENTRY-4). No verb changed; one SEAM row.
        --
        -- PLACEMENT: in test/quests/_conformance.lua's PLAN, immediately AFTER step("wave.resume", ...) (the last
        -- wave.* row, which leaves no run) and BEFORE seam("seam.retaliate_no", ...).  It ends its own real run
        -- with wave.enter{restart} (::inferno leaves an active run first) and the practice run's Cave exit, so the rows
        -- after it see no run, as before.
        --
        -- What it pins (measured: build/quest_gate/s5ep_a6 rows A.*, s5ep_cf1):
        --   * TzHaar-Ket-Keh's Talk-to takes the fire cape and writes varb5646 = 2, the value the entrance's
        --     multiloc binds Jump-in to (cache_locs.txt:1602-1605); the old content wrote 1 and the entrance
        --     never offered Jump-in;
        --   * the entrance's Jump-in starts a REAL run (practice false) at local 30,36 (Blert 2270,5348);
        --   * exactly one "Wave: 1" line.
        -- Travel between Ket-Keh and the entrance is a labelled ::goto: the entrance's pocket is not walkable from
        -- Ket-Keh's (ENG-7, an engine/map finding).
        seam("seam.inferno_entry_by_click", function()
            local go = verb("player", "goto_tile")
            local talk = verb("player", "talk_to")
            local play = verb("chat", "play")
            local click = verb("player", "click_loc")
            local choose = verb("chat", "choose")
            local state = verb("wave", "state")
            local pause = verb("wave", "pause")
            local enter = verb("wave", "enter")
            if not go then return missing("player", "goto_tile") end
            if not talk then return missing("player", "talk_to") end
            if not play then return missing("chat", "play") end
            if not click then return missing("player", "click_loc") end
            if not choose then return missing("chat", "choose") end
            if not state then return missing("wave", "state") end
            if not pause then return missing("wave", "pause") end
            if not enter then return missing("wave", "enter") end
            t.cheat("::give tzhaar_cape_fire 1")
            t.cheat("::setvar varb5646_inferno_sacrificed_firecape 0")
            t.ticks(2)
            go(2495, 5112, 0)
            local tr, td = talk("inferno_master")
            if tr ~= "ok" then return tr, "talk_to inferno_master -> " .. describe(td) end
            local pr, pd = play({ "npc:the Inferno awaits", "choose:Sacrifice your fire cape." })
            if pr ~= "ok" then return pr, "the sacrifice choice -> " .. describe(pd) end
            t.ticks(2)
            local _, v = t.var.server("varb5646_inferno_sacrificed_firecape")
            local _, capes = t.inv.count("tzhaar_cape_fire")
            if v ~= 2 or capes ~= 0 then
                return "hollow", "after the sacrifice varb5646 " .. describe(v) .. " fire capes " .. describe(capes) .. ", not 2 and 0"
            end
            go(2495, 5131, 0)
            -- Count only the lines that arrive after this press: the wave.* rows before this one
            -- entered wave 1 themselves and their own "Wave: 1" lines are still in the chat ring.
            local floor = 0
            local _, before = t.msg.last(30)
            for _, l in ipairs(is_table(before) and before or {}) do
                if is_table(l) and type(l.serial) == "number" and l.serial > floor then floor = l.serial end
            end
            local cr, cd = click("inferno_entrance", 1)
            if cr ~= "ok" then return cr, "Jump-in press -> " .. describe(cd) end
            t.await({ level = function() return t.chat.kind() == "options" end, note = "jump-in options" }, 8)
            local jr, jd = choose("/^Jump into the Inferno/")
            if jr ~= "ok" then return jr, "the Jump-in row -> " .. describe(jd) end
            local wr = t.await({ level = function() local _, _, s = state() return s and s.active and s.wave == 1 and s.alive > 0 end, note = "wave 1" }, 40)
            local _, detail, s = state()
            local text = "-> " .. describe(detail)
            if wr ~= "ok" or not is_table(s) or s.practice ~= false then
                return "hollow", "the entrance did not start a REAL wave 1 -- " .. text
            end
            t.ticks(2)  -- the line reaches the client's chat ring a tick after the server's wave var
            local _, lines = t.msg.last(14)
            local waves = 0
            for _, l in ipairs(is_table(lines) and lines or {}) do
                local fresh = not is_table(l) or type(l.serial) ~= "number" or l.serial > floor
                if fresh and string.find(tostring(is_table(l) and l.text or l), "Wave: 1", 1, true) then waves = waves + 1 end
            end
            -- leave: wave.enter's ::inferno ends the real run and starts practice; the practice run's exit ends that
            -- (Protect from Missiles first: wave 1's bat fires on the walk to the exit, as the wave.pause row's stage says)
            local pray = verb("prayer", "set")
            if pray then pray("protectfrommissiles", true) end
            local nr, nd = enter("inferno", 1, { restart = true })
            if nr ~= "ok" then return "hollow", "could not replace the real run: " .. describe(nr) .. " " .. describe(nd) end
            local er, ed = pause({ via = "exit" })
            if er ~= "refused" or not string.find(tostring(ed), "ENDED", 1, true) then
                return "hollow", "could not end the run afterwards: " .. describe(er) .. " " .. describe(ed)
            end
            if waves ~= 1 then
                return "hollow", "'Wave: 1' printed " .. waves .. " time(s), not once -- " .. text
            end
            return "ok", "sacrifice by Talk-to (varb5646 2, cape taken); Jump-in started a real wave 1 at "
                .. s.tile.x .. "," .. s.tile.z .. "; one 'Wave: 1' line " .. text
        end)

        -- RETALIATE=NO REFUSES THE DEFAULT RETALIATION (waves seam pass 4
        -- retaliate_no, torirs_server_scripts.c rung_is_refused_retaliation).
        -- No verb changed; one SEAM row.
        -- What it proves: a `retaliate=no` npc with no `[ai_queue1,<type>]` binding of its
        -- own (maiden_blood_slug_hard, tob.npc) hit by a SPELL does not swing back. The
        -- spell goes through `~npc_retaliate` -> `npc_queue(1)`, whose `_` default is
        -- `npc_setmode(opplayer2)` (skill_combat/npc_combat.rs2:67); melee never reaches
        -- that rung (ENG-27), so a melee version of this row would pass on either binary.
        -- Measured: ret4_a_before2 c.retaliation hit_player 90:0 105:0; ret4_a_final none.
        -- Harness runs of this row: ret4_before_conf2 (HEAD C) and ret4_after_conf2 (seam C).
        seam("seam.retaliate_no", function()
            local go = verb("player", "goto_tile")
            local equip = verb("player", "equip")
            local cast = verb("player", "cast")
            local nearest = verb("npc", "nearest")
            local rows = verb("ticklog", "rows")
            local slotof = verb("ticklog", "slot")
            local start = verb("ticklog", "start")
            if not go then return missing("player", "goto_tile") end
            if not equip then return missing("player", "equip") end
            if not cast then return missing("player", "cast") end
            if not nearest then return missing("npc", "nearest") end
            if not rows then return missing("ticklog", "rows") end
            if not slotof then return missing("ticklog", "slot") end
            if not start then return missing("ticklog", "start") end
            local SLUG = "maiden_blood_slug_hard"
            -- bring-alongs and the subject (a setup ladder, as a quest's setup list)
            t.cheat("::setlevel magic 99")
            t.cheat("::give staff_of_air")
            t.cheat("::give airrune 20")
            t.cheat("::give mindrune 10")
            start()
            go(3226, 3216, 0)
            equip("staff_of_air")
            t.cheat("::spawn " .. SLUG)
            t.ticks(3)
            local rn, slug = nearest(SLUG, 6)
            if rn ~= "ok" or not is_table(slug) then
                return "hollow", "no " .. SLUG .. " after ::spawn -- " .. describe(rn)
            end
            local _, wslot = slotof(slug)
            local _, from = t.tick()
            for _ = 1, 2 do
                cast("wind_strike", SLUG, 8, 2, { slot = slug.slot })
                t.ticks(5)
            end
            t.ticks(8)
            -- `since` on rows is a serial, so the tick filter is done here
            -- a SPLASH writes no hit_npc row but still provokes (ret4_before_conf: wind
            -- strike projectile at 6, no hit_npc, the slug swung at 8), so the casts are
            -- counted from their projectiles (spotanim 91 = wind strike)
            local landed, swung = {}, {}
            local _, shots = rows({ kind = "projectile", spotanim = 91 })
            for i = 1, #(shots or {}) do
                if shots[i].tick >= from then landed[#landed + 1] = tostring(shots[i].tick) end
            end
            local _, back = rows({ kind = "hit_player" })
            for i = 1, #(back or {}) do
                if back[i].npc_slot == wslot and back[i].tick >= from then
                    swung[#swung + 1] = string.format("%d:%d", back[i].tick, back[i].damage)
                end
            end
            if #landed == 0 then
                return "hollow", "no wind strike was cast at the slug (world slot " .. tostring(wslot) .. ") since tick " .. tostring(from)
            end
            if #swung > 0 then
                return "refused", "the retaliate=no slug swung back: hit_player " .. table.concat(swung, " ")
                    .. " (casts fired " .. table.concat(landed, ",") .. ")"
            end
            return "ok", string.format("slug world slot %s cast at %s; hit_player from it: none",
                tostring(wslot), table.concat(landed, ","))
        end)

        -- prayer out, backpack tab back, and the player back on the Lumbridge
        -- landing (3222,3218), so `finish` ends the run where the rows before
        -- this block left it
        stage(function()
            local set = verb("prayer", "set")
            if set then
                set("protectfrommissiles", false)
            end
            local tab = verb("ui", "tab")
            if tab then
                tab("inventory")
            end
            local go = verb("player", "goto_tile")
            if go then
                go(3222, 3218, 0)
            end
        end)

        -- seam.prayer_drain_fresh_per_prayer sits HERE, at the end, and not
        -- beside seam.prayer_drain_activation_tick: twenty more ticks before
        -- the goblin rows moved the world's rolls, and the goblin player.cast
        -- hits retaliated onto the player on seam.attack_presses_the_watched_slot's
        -- N tile ("I'm already under attack."; waves seam pass 4 close, the
        -- same on HEAD's C and content).  Its own setup: Prayer 43 (Ultimate
        -- Strength needs 31), nothing lit (the stage above put Protect from
        -- Missiles out), the staff of air worn gives no prayer bonus.
        stage(function()
            setup_cheat("::setlevel prayer 43")                    -- setup
            local tab = verb("ui", "tab")
            if tab then
                tab("prayer")
            end
            settle(2)
        end)

        -- A PRAYER LIT OVER A DRAINING ONE IS FREE ON ITS OWN ACTIVATION TICK
        -- (waves seam4 prayer_land).  No verb changed; one SEAM row.
        -- Protect from Melee (12) in force 5 npc phases, Ultimate Strength (12)
        -- lit over it for the last 3: "the game does not drain prayer for
        -- prayers on the tick they are activated" (wiki Prayer:528) is per
        -- prayer, so 4 x 12 + 2 x 12 = 72.  Before the seam: 84 (Ultimate
        -- Strength charged on its activation tick; seam pass 3 measured 264
        -- against 228 for three flicks).  The long form is scratch_prl_a.lua's
        -- "over" row: build/quest_gate/s4prl_a_after measured 396 = wiki.
        seam("seam.prayer_drain_fresh_per_prayer", function()
            local set_on_tick = verb("prayer", "set_on_tick")
            if not set_on_tick then return missing("prayer", "set_on_tick") end
            local switch = verb("prayer", "switch")
            if not switch then return missing("prayer", "switch") end
            local points_fn = verb("prayer", "points")
            if not points_fn then return missing("prayer", "points") end
            local tick_fn = verb("tick")
            if not tick_fn then return missing("tick") end
            local server = verb("var", "server")
            if not server then return missing("var", "server") end
            local function reading()
                settle(2)
                local _, _, r = points_fn()
                local cr, counter = server("varp6296_prayer_drain_counter")
                if type(r) ~= "table" or cr ~= "ok" or type(counter) ~= "number" then
                    return nil, "points " .. describe(r and r.level) .. ", counter " .. describe(cr) .. " " .. describe(counter)
                end
                return { points = r.level, counter = counter }
            end
            local before, why = reading()
            if not before then return "hollow", "no reading before: " .. why end
            local _, now = tick_fn()
            local h0 = now + 3
            local r1, d1 = set_on_tick("protectfrommelee", true, h0 - 1)
            if r1 ~= "ok" then return r1, "melee on: " .. describe(d1) end
            local r2, d2 = set_on_tick("ultimatestrength", true, h0 + 1)
            if r2 ~= "ok" then return r2, "strength on: " .. describe(d2) end
            local r3, d3 = switch({ { "protectfrommelee", false }, { "ultimatestrength", false } }, { tick = h0 + 4 })
            if r3 ~= "ok" then return r3, "both off: " .. describe(d3) end
            local after, why2 = reading()
            if not after then return "hollow", "no reading after: " .. why2 end
            local charged = (before.points - after.points) * 60 + (after.counter - before.counter)
            local text = string.format("Protect from Melee in force ticks %d..%d, Ultimate Strength %d..%d: points %d -> %d, "
                .. "counter %d -> %d, charged %d (wiki Prayer:528 per prayer: 4 x 12 + 2 x 12 = 72)",
                h0, h0 + 4, h0 + 2, h0 + 4, before.points, after.points, before.counter, after.counter, charged)
            if charged ~= 72 then
                return "hollow", text
            end
            return "ok", text
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
