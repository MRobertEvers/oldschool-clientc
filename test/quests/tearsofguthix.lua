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
-- Route (b66 door-rule re-drive; every crossing by its own op):
--   * the first goto lands on 3169,3171,0, the open tile north of the
--     Lumbridge Swamp hole (`goblin_cave_entrance` 3169,3172 is solid);
--     its maplink row maplink_0_49_49_33_35 (ladders_stairs/configs/
--     maplink.dbrow) takes that tile to 3169,9571,0, so the descent is a
--     `t.player.climb` (map frame 0 -> 1). The hole's oploc1
--     (quest_anothersliceofham/scripts/slice_sergeants.rs2:62) ties the rope
--     on the first visit (rope 1 -> 0, varb279).
--   * the caves are walked: rope -> stepping stone a's bank 3204,9572 is
--     the static flood's only walk (112 tiles), and it passes two wall
--     beasts in one-tile corridors (swamp_wallbeast 3162,9574 and
--     3198,9572, areas/world/configs/m49_149.spawn:31,72;
--     tog_wallbeast_reveal.rs2 grabs within 1 tile). That is the real
--     hazard, so trout are staged and the walk eats, with a margin row.
--   * stones a and b by their own op (Jump-across, maplink_agility.dbrow
--     0_50_149_4_36 -> 8_36 and 21_20 -> 21_16) through `cross_trap`.
--   * the tunnel `tog_cave_down` (dttd_savezanik.rs2:37, p_teleport
--     ^tog_juna_stand 3250,9517,2) is a `climb` 0 -> 2.
--   * the chasm both ways on a light-creature (below); the return lands on
--     the north ledge ^tog_chasm_north 3228,9527, which the climbing rocks
--     `tog_climbing_rocks_up` (3240,9524-9525) wall off from Juna's side
--     (no walk joins them), so the rocks are climbed by their op1
--     (tearsofguthix_lantern.rs2 [oploc1]: p_teleport(movecoord(coord, 2,
--     0, 0)): from 3239,9525 onto 3241,9525) and Juna is walked to.
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
        "::setvar varp101_qp 43",      -- prerequisite quest points (^tog_qp_req), earned elsewhere -- not this quest's own reward
        "::setlevel firemaking 49",
        "::setlevel crafting 20",
        "::setlevel mining 20",
        "::give rope 1",               -- Quest Helper bring-along: the swamp caves hole (once per account: the hole's oploc1 refuses without it and ties it, slice_sergeants.rs2:70-86)
        "::give bronze_pickaxe 1",     -- Quest Helper bring-along: mining the stone
        "::give chisel 1",             -- Quest Helper bring-along: chisel the stone into a bowl
        "::give sapphire 1",           -- Quest Helper bring-along: sapphire lantern components
        "::give bullseye_lantern_unlit 1",
        "::give tinderbox 1",
        "::give trout 4",              -- food for the two wall beasts the swamp caves walk passes (route banner); no combat stat staged
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
            varp = "varb451_tog_juna_bowl",
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
        t.expect("setup.have_rope", t.inv.expect_has("rope", 1))
        t.expect("setup.have_trout", t.inv.expect_has("trout", 4))

        -- ------------------------------------------------- light the lantern
        -- On the surface, before the hole: the swamp caves are dark (the
        -- port's tunnels test for no light, losttribe.lua's note, but a
        -- player goes down with one lit). [opheldu,sapphire]/
        -- [opheldu,bullseye_lantern_unlit]: either click order swaps the
        -- lens for the sapphire.
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

        -- ------------------------------------------------- entrance (Option A)
        -- The first goto: Lumbridge -> the open tile north of the hole
        -- (static flood REACH closed-doors 153 from the fixture tile). The
        -- hole itself (3169,3172) is solid; maplink_0_49_49_33_35 keys this
        -- tile.
        t.exec("goto-swamp-hole", t.player.goto_tile, 3169, 3171, 0)
        t.exec("tog.enter_swamp", t.player.climb, { loc = "goblin_cave_entrance", op = 1, op_name = "Climb-down",
            at = { 3169, 3172, 0 }, src = { 3169, 3171 }, dest = { 3169, 9571, 0 } })
        local rope_result, rope_left = t.inv.count("rope")
        t.check("tog.rope_tied", rope_result == "ok" and rope_left == 0,
            "rope count after the descent " .. tostring(rope_left) .. " (" .. tostring(rope_result)
                .. "; the hole's first entry ties the rope: slice_sergeants.rs2:78-86)")

        -- The swamp caves. Rope -> stone a's bank is the only walk (bfs 112
        -- tiles: west, round the north loop, east, south), through two
        -- one-tile corridors beside wall beasts (3162,9574 and 3198,9572).
        -- Short hops by the beasts so the eater runs between them; the
        -- eater records the lowest hitpoints it reads for the margin row.
        local swamp = { low = nil, max = nil, ate = 0 }
        local function swamp_vitals()
            local hr, hs = t.skill.read("hitpoints")
            -- a reading's `level` is the current (stated) level, `base_level` the maximum (_setup.lua:54)
            if hr ~= "ok" or type(hs) ~= "table" or hs.level == nil or hs.base_level == nil then
                return
            end
            swamp.max = hs.base_level
            if swamp.low == nil or hs.level < swamp.low then
                swamp.low = hs.level
            end
            if hs.level < 7 then
                local er = t.player.inv_op("trout", 1)
                swamp.ate = swamp.ate + 1
                t.note("swamp.eat: hitpoints " .. tostring(hs.level) .. "/" .. tostring(hs.base_level)
                    .. " -> trout (" .. tostring(er) .. ")")
                t.ticks(2)
            end
        end
        t.exec("swampCaves.toStoneA", t.player.walk_route, {
            { 3169, 9571 }, { 3166, 9573 }, { 3163, 9573 }, { 3160, 9573 }, { 3152, 9573 }, { 3152, 9580 },
            { 3152, 9586 }, { 3151, 9590 }, { 3158, 9591 }, { 3166, 9591 }, { 3170, 9588 }, { 3176, 9586 },
            { 3184, 9586 }, { 3190, 9584 }, { 3194, 9581 }, { 3194, 9575 }, { 3196, 9572 }, { 3198, 9571 },
            { 3200, 9571 }, { 3203, 9571 }, { 3204, 9572 } },
            { level = 0, vitals = swamp_vitals })
        t.exec("tog.stone_a_jump", t.player.cross_trap, { loc = "swamp_cave_steppingstone_a", op = 1,
            op_name = "Jump-across", at = { 3206, 9572, 0 }, src = { 3204, 9572 }, dest = { 3208, 9572 },
            vitals = swamp_vitals })
        swamp_vitals()
        local trout_result, trout_left = t.inv.count("trout")
        t.check("swampCaves.margin",
            swamp.low ~= nil and swamp.max ~= nil and swamp.low * 4 >= swamp.max
                and trout_result == "ok" and trout_left ~= nil and trout_left >= 1,
            "past both wall beasts: lowest hitpoints read " .. tostring(swamp.low) .. "/" .. tostring(swamp.max)
                .. " (want >= a quarter), ate " .. tostring(swamp.ate) .. ", trout left " .. tostring(trout_left))

        t.exec("swampCaves.toStoneB", t.player.walk_route, {
            { 3208, 9572 }, { 3211, 9571 }, { 3218, 9571 }, { 3223, 9570 }, { 3223, 9563 }, { 3221, 9559 },
            { 3221, 9556 } }, { level = 0 })
        t.exec("tog.stone_b_jump", t.player.cross_trap, { loc = "swamp_cave_steppingstone_b", op = 1,
            op_name = "Jump-across", at = { 3221, 9554, 0 }, src = { 3221, 9556 }, dest = { 3221, 9552 } })

        -- On to the tunnel in the south-east corner, then down it to Juna.
        t.exec("swampCaves.toTunnel", t.player.walk_route, {
            { 3221, 9552 }, { 3226, 9552 }, { 3226, 9546 }, { 3226, 9542 } }, { level = 0 })
        t.exec("tog.descend_to_juna", t.player.climb, { loc = "tog_cave_down", op = 1, op_name = "Enter",
            at = { 3225, 9539, 0 }, src = { 3226, 9542 }, dest = { 3250, 9517, 2 } })

        -- ------------------------------------------------- greet Juna
        -- [oploc1,tog_juna] -> @tog_juna_talk (not_started branch,
        -- requirements met via setup's setvar/setlevel cheats).
        t.exec("tog.greet", t.player.click_loc, "tog_juna", 1)

        -- The whole first conversation, page by page, copied byte-for-byte
        -- from @tog_juna_talk's not-started branch. "Okay..." (p_choice3,
        -- option 1) falls through into the accept text; %tog_juna_bowl is
        -- set to need_bowl silently at the very end, so the list closes on
        -- "end".
        -- The camera packets fire as the "But first..." page is clicked
        -- away, i.e. at the END of the accept row, so the await's start is
        -- pinned here rather than at the bowl_dialog row (verbs-cutscene.md,
        -- t.cutscene.mark).
        local bowl_cutscene_mark = t.cutscene.mark()
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
        })
        -- tearsofguthix.rs2 cuts the camera to the stone cave for Juna's next
        -- two pages and glides it up (cam_moveto/cam_lookat/cam_moveto before
        -- "There is a cave...", cam_reset after "Mine some stone..."). The pages are played on
        -- their own row so their shots show the cutscene framing; the await
        -- then reads the whole sequence from the mark above.
        t.exec("tog.bowl.page1", t.chat.play, {
            "npc:There is a cave on the south side of the chasm that is similarly infused",
        })
        -- Hold on the second page while the glide runs, so the shots sample
        -- it (a reader takes a few seconds a page; the driver would click
        -- through in one tick).
        t.ticks(8)
        local glide_cam = t.world.camera()
        t.check("tog.bowl.glide", glide_cam ~= nil and glide_cam.server_driven == true,
            "camera 8 ticks into the glide: eye " .. tostring(glide_cam and glide_cam.x) .. "," .. tostring(glide_cam and glide_cam.z)
                .. " pitch " .. tostring(glide_cam and glide_cam.pitch) .. " last_op " .. tostring(glide_cam and glide_cam.last_op))
        t.exec("tog.bowl.page2", t.chat.play, {
            "npc:Mine some stone from that cave, make it into a bowl, and bring it to me",
            "end",
        })
        t.exec("tog.bowl.cutscene", t.cutscene.await, "tog.bowl", { since = bowl_cutscene_mark, expect = {
            { op = "moveto", coord = "2_50_148_37_31", height = 400 },   -- tearsofguthix.rs2, copied verbatim
            { op = "lookat", coord = "2_50_148_31_25", height = 0 },
            { op = "moveto", coord = "2_50_148_39_33", height = 700 },   -- the glide (1, 1)
            { op = "reset" },
        } })
        t.expect("quest.stage.need_bowl", t.quest.expect_stage("need_bowl"))

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
        -- ^tog_chasm_north = 3228,9527 -- the north ledge the attract
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

        -- The ride back lands on the north ledge, walled off from Juna's
        -- side by the climbing rocks (3240,9524-9525; no walk joins
        -- 3228,9527 and 3250,9517 at margin 600): walk to the rocks' west
        -- tile, climb them by their op1, then walk down to Juna.
        t.exec("ledge.toRocks", t.player.walk_route, {
            { 3228, 9527 }, { 3235, 9527 }, { 3236, 9525 }, { 3239, 9525 } }, { level = 2 })
        t.exec("ledge.climbRocks", t.player.cross_trap, { loc = "tog_climbing_rocks_up", op = 1, op_name = "Climb",
            at = { 3240, 9525, 2 }, src = { 3239, 9525 }, dest = { 3241, 9525 } })
        t.exec("walk-return-to-juna", t.player.walk_to, 3250, 9517)

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
