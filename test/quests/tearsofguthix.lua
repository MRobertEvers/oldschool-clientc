-- Tears of Guthix: rope into the Lumbridge Swamp Caves hole (Option A,
-- docs/quests/tears_of_guthix.md), cross the chasm's two stepping stones,
-- talk to Juna, light a sapphire lantern for real and attract a
-- light-creature across the chasm, mine + chisel a stone bowl, cross back
-- and hand it in.
--
-- Fixture start: fresh_lumbridge.ini stands the player at 3206,3233,0
-- (Lumbridge, beside Hans). No ::tearsofguthix/::toglantern debugproc --
-- those grant the quest's own deliverable (the stone, the bowl, the lit
-- lantern) and are a trap-16 cheat; the previously committed file used
-- ::tearsofguthix for the stone/chisel grant and was rejected for it
-- (queue last_failure, 2026-09-23): "light the sapphire lantern with a
-- tinderbox, use it on the chasm climbing rocks for a light-creature, mine
-- for real instead of ::tearsofguthix".
--
-- Entrance + stepping-stone crossing: the engine reach fix landed at
-- seam10 (collision_map.c) -- a floor-decoration stepping stone with
-- chasm on every side is now reachable from the bank across one gap
-- square, with no stand-on-square opt-in needed (doc trap 32). The exact
-- goto/click sequence below is copied from the scratch proof that measured
-- this 12/12 (build/seam_state/seam10/scratch_reach/tog_option_a.lua).
-- Wall beasts (`swamp_wallbeast` -> `swamp_wallbeast_combat`,
-- tog_wallbeast_reveal.rs2's `[ai_timer]` `huntall(npc_coord, 1, 0)`) only
-- reveal and grab within 1 tile -- every goto/walk tile below stays >=2
-- tiles off every swamp_wallbeast *.spawn row
-- (areas/world/configs/m49_149.spawn, m50_149.spawn), and every
-- goto_tile here is plain travel (rule (b)): it lands directly on a bank
-- or entrance tile, never skips a stepping-stone click or the cave-down
-- descent itself.
--
-- Lantern lighting: [opheldu,tinderbox] (skill_firemaking/scripts/
-- firemaking.rs2:36-41) now carries a tog_sapphire_lantern_unlit case
-- ("without this case either click order answered dm_default here
-- first"), so use_item_on_item("tinderbox","tog_sapphire_lantern_unlit")
-- lights it for real -- this used to be a content gap (parity1b's
-- notebook), fixed per docs/quests/tears_of_guthix.md's "Closer
-- follow-up" note and proved there (build/quest_gate/close_proof_1b).
--
-- Crossing: `[opnpc1,tog_light_creature_op]` / `[oplocu,
-- tog_climbing_rocks_down/up]` (tearsofguthix_lantern.rs2) are real,
-- already-implemented content. Locs carry no x/z in the compack (trap
-- 20/29), so their banks were decoded from the map text directly
-- (`grep -n ': 6672 \|: 6673 ' maps/m50_148.jl2`, ids from
-- all.loc.compack): `tog_climbing_rocks_down` (id 6672) sits at
-- 3239,9497-9499 -- the MINE (south) bank, beside `^tog_mine_stand`
-- (3229,9497) -- and `tog_climbing_rocks_up` (id 6673) sits at
-- 3240,9524-9525 -- the JUNA (north) bank, beside `^tog_chasm_north`
-- (3228,9527). (Run 1 got this backwards -- probing both symbols with
-- `t.world.loc_near` from Juna's stand answered `ok` for BOTH, because
-- that lookup is a scene-radius search, not a reachability test; clicking
-- the wrong-side `_down` symbol from Juna's stand auto-walked around the
-- whole chasm rather than refusing, landing the "crossing" backwards with
-- a z-delta that still satisfied a loose `>= 8` check.) So: `_up` first
-- (Juna's own side, crossing to the mine), `_down` for the return.
-- `~tog_attract_light_creature`'s own rule (`coordz(coord) >= 9516`)
-- confirms the direction: Juna's stand (z=9517) and `_up`'s bank (z~9524)
-- are both >=9516 -> lands at `^tog_chasm_south` (3229,9504, by the mine);
-- the mine bank and `_down` (z~9498) are both <9516 -> lands at
-- `^tog_chasm_north` (3228,9527, by Juna).
--
-- WHICH ROWS SHOOT. t.exec and t.check shoot the row they write; plain
-- t.expect/t.step rows do not.

return {
    id = "tearsofguthix",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setvar qp 43",              -- prerequisite quest points (^tog_qp_req), earned elsewhere -- not this quest's own reward
        "::setlevel firemaking 49",
        "::setlevel crafting 20",
        "::setlevel mining 20",
        "::give rope 1",               -- Quest Helper bring-along: the swamp caves hole (once-per-account; this port's maplink does not gate on it, but it is a documented bring-along item)
        "::give bronze_pickaxe 1",     -- Quest Helper bring-along: mining the stone
        "::give chisel 1",             -- Quest Helper bring-along: chisel the stone into a bowl
        "::give sapphire 1",           -- Quest Helper bring-along: sapphire lantern components
        "::give bullseye_lantern_unlit 1",
        "::give tinderbox 1",
        -- slayer_cave_crawler_1/3/4 (areas/world/configs/m49_149.spawn) are
        -- generic aggressive engine npcs along the Option A route, unrelated
        -- to the quest's own content (same idiom blackknight.lua uses for
        -- the fortress's own aggressive_black_knight patrol) -- taken out of
        -- the way rather than routed around blind.
        "::passive slayer_cave_crawler_1",
        "::passive slayer_cave_crawler_3",
        "::passive slayer_cave_crawler_4",
    },

    run = function(t)
        -- tog_juna_bowl is a VARBIT (all.varbit.compack id 451, no all.varp
        -- row of its own) -- quest.stage/expect_stage resolve that
        -- transparently.
        t.quest.bind({
            varp = "tog_juna_bowl",
            constants = {
                not_started = 0,
                need_bowl = 1,
                complete = 2,
            },
            display = "Tears of Guthix",
            points = 1,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        t.expect("setup.have_pickaxe", t.inv.expect_has("bronze_pickaxe", 1))
        t.expect("setup.have_chisel", t.inv.expect_has("chisel", 1))
        t.expect("setup.have_sapphire", t.inv.expect_has("sapphire", 1))
        t.expect("setup.have_lantern", t.inv.expect_has("bullseye_lantern_unlit", 1))
        t.expect("setup.have_tinderbox", t.inv.expect_has("tinderbox", 1))

        -- ------------------------------------------------- entrance (Option A)
        t.exec("goto-swamp-hole", t.player.goto_tile, 3169, 3172, 0)
        t.exec("tog.enter_swamp", t.player.click_loc, "goblin_cave_entrance", 1)
        t.ticks(3)
        local entered_result, entered_tile = t.world.tile()
        t.check("tog.entered_swamp_caves",
            entered_result == "ok" and entered_tile.level == 0 and entered_tile.z > 9000,
            "world.tile() -> " .. tostring(entered_result) .. " "
                .. tostring(entered_tile and entered_tile.x) .. ","
                .. tostring(entered_tile and entered_tile.z) .. ","
                .. tostring(entered_tile and entered_tile.level))

        -- west bank of stepping stone a -- plain travel, >=2 tiles off the
        -- swamp_wallbeast rows at 3198,9554 / 3198,9572 (m49_149.spawn)
        t.exec("goto-stone-a-bank", t.player.goto_tile, 3200, 9572, 0)
        t.exec("tog.stone_a_jump", t.player.click_loc, "swamp_cave_steppingstone_a", 1)
        t.ticks(2)
        local stone_a_result, stone_a_tile = t.world.tile()
        t.check("tog.stone_a_crossed",
            stone_a_result == "ok" and stone_a_tile.x == 3208 and stone_a_tile.z == 9572,
            "world.tile() -> " .. tostring(stone_a_result) .. " "
                .. tostring(stone_a_tile and stone_a_tile.x) .. "," .. tostring(stone_a_tile and stone_a_tile.z))

        -- real walk (no goto) to stone b's north bank
        local walk_b_result = t.player.walk_to(3221, 9556)
        local bank_b_result, bank_b_tile = t.world.tile()
        t.check("walk.stone_b_north_bank",
            walk_b_result == "ok" and bank_b_result == "ok" and bank_b_tile.x == 3221 and bank_b_tile.z == 9556,
            "walk_to -> " .. tostring(walk_b_result) .. " at "
                .. tostring(bank_b_tile and bank_b_tile.x) .. "," .. tostring(bank_b_tile and bank_b_tile.z))
        t.exec("tog.stone_b_jump", t.player.click_loc, "swamp_cave_steppingstone_b", 1)
        t.ticks(2)
        local stone_b_result, stone_b_tile = t.world.tile()
        t.check("tog.stone_b_crossed",
            stone_b_result == "ok" and stone_b_tile.x == 3221 and stone_b_tile.z == 9552,
            "world.tile() -> " .. tostring(stone_b_result) .. " "
                .. tostring(stone_b_tile and stone_b_tile.x) .. "," .. tostring(stone_b_tile and stone_b_tile.z))

        -- real walk on to the tunnel, then descend to the chasm / Juna
        local walk_down_result = t.player.walk_to(3226, 9542)
        local before_down_result = t.world.tile()
        t.check("walk.tog_cave_down",
            walk_down_result == "ok" and before_down_result == "ok",
            "walk_to -> " .. tostring(walk_down_result))
        t.exec("tog.descend_to_juna", t.player.click_loc, "tog_cave_down", 1)
        t.ticks(2)
        local juna_area_result, juna_area_tile = t.world.tile()
        t.check("tog.reached_juna_chasm",
            juna_area_result == "ok" and juna_area_tile.level == 2
                and math.abs(juna_area_tile.x - 3250) <= 2 and math.abs(juna_area_tile.z - 9517) <= 2,
            "world.tile() -> " .. tostring(juna_area_result) .. " "
                .. tostring(juna_area_tile and juna_area_tile.x) .. ","
                .. tostring(juna_area_tile and juna_area_tile.z) .. ","
                .. tostring(juna_area_tile and juna_area_tile.level))

        -- ------------------------------------------------- greet Juna
        -- [oploc1,tog_juna] -> @tog_juna_talk (not_started branch,
        -- requirements met via setup's setvar/setlevel cheats).
        t.exec("tog.greet", t.player.click_loc, "tog_juna", 1)

        -- The whole first conversation, page by page, copied byte-for-byte
        -- from @tog_juna_talk's not-started branch. "Okay..." (p_choice3,
        -- option 1) falls through into the accept text; %tog_juna_bowl is
        -- set to need_bowl silently at the very end, so the list closes on
        -- "end".
        t.exec("tog.accept_dialog", t.chat.play, {
            "npc:Tell me... a story...",
            "player:A story?",
            "npc:I have been waiting here three thousand years, guarding the Tears of Guthix.",
            "npc:An adventurer such as yourself must have many tales to tell.",
            "npc:Then you can drink of the power of balance, which will make you stronger",
            "choose:Okay...",
            "player:Okay...",
            "mesbox:You tell Juna some stories of your adventures.",
            "npc:Your stories have entertained me. I will let you into the cave for a short time.",
            "npc:But first you will need to make a bowl in which to collect the tears.",
            "npc:There is a cave on the south side of the chasm that is similarly infused",
            "npc:Mine some stone from that cave, make it into a bowl, and bring it to me",
            "end",
        })
        t.expect("quest.stage.need_bowl", t.quest.expect_stage("need_bowl"))

        -- ------------------------------------------------- light the lantern
        -- [opheldu,sapphire]/[opheldu,bullseye_lantern_unlit]: either
        -- click order swaps the lens for the sapphire.
        t.exec("tog.sapphire_swap", t.player.use_item_on_item, "sapphire", "bullseye_lantern_unlit")
        local swapped_result = t.inv.await("tog_sapphire_lantern_unlit", 1, 10)
        t.check("tog.lantern_sapphired", swapped_result == "ok",
            "inv.await(tog_sapphire_lantern_unlit,1) -> " .. tostring(swapped_result))

        -- [opheldu,tinderbox]'s tog_sapphire_lantern_unlit case (fixed --
        -- see banner) -> @tog_light_sapphire_lantern.
        t.exec("tog.light_lantern", t.player.use_item_on_item, "tinderbox", "tog_sapphire_lantern_unlit")
        local lit_result = t.inv.await("tog_sapphire_lantern_lit", 1, 10)
        t.check("tog.lantern_lit", lit_result == "ok",
            "inv.await(tog_sapphire_lantern_lit,1) -> " .. tostring(lit_result))

        -- ------------------------------------------------- attract a light-creature, cross
        local before_creature_result, before_creature_detail = t.npc.nearest("tog_light_creature_op", 15)
        t.check("pre.no_light_creature_yet", before_creature_result ~= "ok",
            "npc.nearest(tog_light_creature_op,15) -> " .. tostring(before_creature_result) .. " " .. tostring(before_creature_detail))

        -- Juna's own side -- tog_climbing_rocks_up (decoded 3240,9524-9525).
        local rocks_target = t.player.by_symbol("loc", "tog_climbing_rocks_up")
        t.exec("tog.use_lantern_on_tog_climbing_rocks_up", t.player.use_on, "tog_sapphire_lantern_lit", rocks_target)

        local creature_present_result = t.npc.await_present("tog_light_creature_op", 15, 10)
        t.check("tog.light_creature_present", creature_present_result == "ok",
            "npc.await_present(tog_light_creature_op) -> " .. tostring(creature_present_result))

        -- [opnpcu,tog_light_creature_op]: use the lit lantern ON the
        -- light-creature (the guide's own wording, helper_coverage's
        -- useLanternOnLightCreature step) -- the same
        -- ~tog_attract_light_creature proc [opnpc1] reaches too, but this
        -- is the documented trigger.
        local before_cross_result, before_cross_tile = t.world.tile()
        local creature_target = t.player.by_symbol("npc", "tog_light_creature_op")
        t.exec("tog.attract_light_creature", t.player.use_on, "tog_sapphire_lantern_lit", creature_target)
        t.ticks(3)
        local after_cross_result, after_cross_tile = t.world.tile()
        -- ^tog_chasm_south = 3229,9504 -- the mine bank the attract from
        -- Juna's side (z>=9516) must land on.
        t.check("tog.crossed_chasm",
            after_cross_result == "ok" and before_cross_result == "ok"
                and math.abs(after_cross_tile.x - 3229) <= 3 and math.abs(after_cross_tile.z - 9504) <= 3,
            "world.tile() before " .. tostring(before_cross_tile and before_cross_tile.x) .. "," .. tostring(before_cross_tile and before_cross_tile.z)
                .. " -> after " .. tostring(after_cross_tile and after_cross_tile.x) .. "," .. tostring(after_cross_tile and after_cross_tile.z)
                .. " (expect near tog_chasm_south 3229,9504)")

        -- ------------------------------------------------- mine the stone, craft the bowl
        local mine_result = t.player.click_loc("tog_blue_stone_rocks1", 1)
        if mine_result ~= "ok" then
            mine_result = t.player.click_loc("tog_blue_stone_rocks2", 1)
        end
        if mine_result ~= "ok" then
            mine_result = t.player.click_loc("tog_blue_stone_rocks3", 1)
        end
        t.step("tog.mine_stone", mine_result == "ok" and "PASS" or "FAIL",
            "click_loc(tog_blue_stone_rocks*,1) -> " .. tostring(mine_result))
        local stone_result = t.inv.await("tog_stone", 1, 10)
        t.check("tog.stone_mined", stone_result == "ok", "inv.await(tog_stone,1) -> " .. tostring(stone_result))

        t.exec("tog.make_bowl", t.player.use_item_on_item, "chisel", "tog_stone")
        local bowl_result = t.inv.await("tog_bowl", 1, 10)
        t.check("tog.bowl_crafted", bowl_result == "ok", "inv.await(tog_bowl,1) -> " .. tostring(bowl_result))

        -- ------------------------------------------------- cross back
        -- Mine side -- tog_climbing_rocks_down (decoded 3239,9497-9499,
        -- beside ^tog_mine_stand 3229,9497).
        local before_creature2_result = t.npc.nearest("tog_light_creature_op", 15)
        local rocks2_target = t.player.by_symbol("loc", "tog_climbing_rocks_down")
        t.exec("tog.use_lantern_return_tog_climbing_rocks_down", t.player.use_on, "tog_sapphire_lantern_lit", rocks2_target)

        local creature2_present_result = t.npc.await_present("tog_light_creature_op", 15, 10)
        t.check("tog.light_creature_present_return", creature2_present_result == "ok",
            "npc.await_present -> " .. tostring(creature2_present_result) .. " (before: " .. tostring(before_creature2_result) .. ")")

        local before_cross2_result, before_cross2_tile = t.world.tile()
        local creature2_target = t.player.by_symbol("npc", "tog_light_creature_op")
        t.exec("tog.attract_light_creature_return", t.player.use_on, "tog_sapphire_lantern_lit", creature2_target)
        t.ticks(3)
        local after_cross2_result, after_cross2_tile = t.world.tile()
        -- ^tog_chasm_north = 3228,9527 -- the Juna-side bank the attract
        -- from the mine side (z<9516) must land on.
        t.check("tog.crossed_chasm_return",
            after_cross2_result == "ok" and before_cross2_result == "ok"
                and math.abs(after_cross2_tile.x - 3228) <= 3 and math.abs(after_cross2_tile.z - 9527) <= 3,
            "world.tile() before " .. tostring(before_cross2_tile and before_cross2_tile.x) .. "," .. tostring(before_cross2_tile and before_cross2_tile.z)
                .. " -> after " .. tostring(after_cross2_tile and after_cross2_tile.x) .. "," .. tostring(after_cross2_tile and after_cross2_tile.z)
                .. " (expect near tog_chasm_north 3228,9527)")

        -- ------------------------------------------------- hand in the bowl
        -- Read the skill snapshot before the hand-in click that awards it.
        local craft_snap_result, craft_snap = t.skill.snapshot()
        t.step("reward.snapshot", craft_snap_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot() -> " .. tostring(craft_snap_result))

        -- The chasm round trip lands on the Juna-side bank, not on Juna's
        -- own tile -- plain travel back to her stand, same idiom as the
        -- entrance gotos above (rule (b): no click/gate is being skipped,
        -- the crossing itself was already driven for real above).
        t.exec("goto-return-to-juna", t.player.goto_tile, 3250, 9517, 2)

        -- Second click_loc on the same loc: %tog_juna_bowl is still
        -- need_bowl, but inv_total(inv, tog_bowl) > 0 now, so @tog_juna_talk
        -- takes the "I have a bowl" branch and commits completion.
        t.exec("tog.handin", t.player.click_loc, "tog_juna", 1)
        t.exec("tog.handin_dialog", t.chat.play, {
            "npc:Before you can collect the Tears of Guthix you must make a bowl",
            "player:I have a bowl.",
            "npc:I will keep your bowl for you, so that you may collect the tears many times",
            "npc:Now... tell me another story, and I will let you collect the tears for the first time.",
            "end",
        })

        -- Completion (inv_del the bowl, %tog_juna_bowl=complete,
        -- stat_advance, ~quest_complete_rewards) commits silently behind
        -- that last page and the reward scroll mounts asynchronously --
        -- give it real time before reading anything.
        t.ticks(3)

        t.quest.expect_complete()

        -- Reward: stat_advance(crafting, 10000) is 1000 Crafting XP
        -- (tenths) -- ~quest_complete_rewards(quest_tearsofguthix, "1000
        -- Crafting XP|Access to the Tears of Guthix minigame", coins), the
        -- literal amount the quest documents. No item/coin reward -- the
        -- minigame access is an unlock, not a testable delta.
        t.exec("reward.crafting_xp", t.skill.expect_gain, "crafting", 1000, craft_snap)

        t.finish(0)
    end,
}
