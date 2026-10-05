-- Spirits of the Elid (spiritsoftheelid) -- brief docs/quests/spirits_of_the_elid.md,
-- content quest_spiritsoftheelid (OSRS-Content/osrs239-content/server/scripts/quests/
-- quest_spiritsoftheelid/). Progress varp %elidquest, constants from
-- configs/quest_spiritsoftheelid.constant (0..60, ten steps.put plateaus + complete).
--
-- Rewritten from the new_quest.py scaffold: the scaffold guessed several verbs
-- wrong against the live .rs2 (read directly, see per-row citations below):
--   * enterCave / useStatuette are `[oplocu,...]` (item-used-on-loc) triggers,
--     not numbered ops -- use_on(rope,...) / use_on(elid_statuette,...), never click_loc.
--   * telegrabKey: content's own elid_house.rs2:117 line says "You should
--     Telekinetic Grab the ancestral key from the table" -- driven with
--     t.player.cast('telegrab', {kind='obj', id='elid_key'}) (seam23
--     cast_on_ground_obj_and_loc), never a hand Take.
--   * the three golems are NOT a `::skipboss` stub -- quest_inventory.tsv's
--     boss_fight/boss_npcs flag is stale for this quest: each golem is an
--     ordinary op2=Attack npc (configs/all.npc, stat1-4 present) whose weakness
--     is pure defence-table (stabdefence/slashdefence/crushdefence = 1 vs 300
--     for the other two), exactly like hero.lua's Ice Queen -- driven with
--     t.player.attack + t.npc.await_dead_engaged per golem, real fights.
--   * the ranging-channel target has no npc_stats (op2=Shoot, no stat1-4) --
--     t.player.attack would time out forever waiting on a health bar that is
--     never sent. It answers a plain mes() with no page and no npc movement,
--     which is exactly what t.player.press's fourth outcome (content line,
--     "it said '...' and did not move") is for.
-- Weapon styles (configs/all.npc + skill_combat/configs/combat.dbrow default
-- %com_mode=0 per category): bronze_dagger (category 25, weapon_stab_sword,
-- mode0=stab) for the White Golem (stabdefence=1); bronze_scimitar (category
-- 21, weapon_slash_sword, mode0=slash) for the Grey Golem (slashdefence=1);
-- bronze_mace (category 39, weapon_spiked, mode0=crush) for the Black Golem
-- (crushdefence=1) -- same "equip and attack, no combat-tab switch" pattern
-- hero.lua's rune_mace uses against the Ice Queen. Shortbow+bronze_arrow
-- (weapon_bow_table, mode0=ranged_style) satisfy elid_ranging_attack_gate's
-- `%damagetype = ^ranged_style | ^magic_style` check -- gathered from the
-- river spawns (m52_48.spawn) per quest-helper's own bow/arrows
-- ItemRequirement tooltips ("obtainable during quest east/south of the cave
-- entrance"), never ::give (trap (c)/rule 16).
--
-- Door rule (b60 re-drive, docs/QUEST_ORCHESTRATOR.md standing rules,
-- owner 2026-10-03): no goto lands in or leaves a closed space.
--   * Lumbridge -> Nardah crosses the Shantay Pass, the only way into the
--     Kharidian Desert on foot (reach.py 3304,3125 -> 3304,3108 UNREACHABLE
--     at margins 30/80/160): the pass is bought from Shantay and the
--     doorway clicked (shantay_pass.rs2 [oploc1,shantay_pass_henge_doorway],
--     [queue,shantay_pass_enter] lands 3304,3115). Every other hop is
--     overland travel between open tiles (reach.py REACH, doors closed).
--   * The shrine house north of the fountain (cupboard, table, plinth) is
--     entered and left on foot through its arch, icthalarins_door_arch
--     3426-3427,2923: blockwalk=0 and op1=Open in all.loc, but no [oploc]
--     handler anywhere in the pack and no doors/configs/doors.loc stage, so
--     there is nothing to press; the crossing is graded on the tiles.
--   * The key on Shiratti's table sits behind elid_temple_railing
--     (blockrange=0): the cast is made from the shrine floor across the
--     railing, never from inside the railed pocket.
--   * The cave root (desert_water_cave_root 3369-3371,3132) is BLOCKED: its
--     only approach tiles 3369-3370,3130-3131 are a 4-tile pocket the map
--     closes with river water (maps/m52_48.jm2 f1 at 41-43,57-58; comp.py
--     floods 4 tiles, no door, no op loc), so a rope from the bank answers
--     "I can't reach that!" from every approach. The run stops there with
--     t.blocked; the code below it is the honest route for when the content
--     is fixed (see the t.blocked text for the two content gaps).

return {
    id = "spiritsoftheelid",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- fourteen tutorial slots, so a requirement fits
        -- Bring-along items quest-helper's getItemRequirements() lists with
        -- NO "obtainable during quest" tooltip (airRune, lawRune, needle,
        -- thread, knife, rope, pickaxe) or whose ItemRequirement is a pure
        -- style/collection placeholder with a concrete displayItemId this
        -- port's combat math resolves to the right damagetype (crushWep,
        -- stabWep, slashWep) or a real single item (lightSource):
        "::give airrune 1",
        "::give lawrune 1",
        "::give needle 1",
        "::give thread 2", -- exactly one per mend (elid_house.rs2 inv_del(inv,thread,1) x2)
        "::give knife 1",
        "::give rope 1", -- never consumed (elid_dungeon.rs2/elid_genie.rs2 read inv_total only)
        "::give lit_candle 1", -- light source: elid_has_light reads inv/worn state directly
        "::give bronze_pickaxe 1", -- ~pickaxe_checker: any held/worn pickaxe
        "::give bronze_dagger 1", -- stab -- White Golem
        "::give bronze_scimitar 1", -- slash -- Grey Golem
        "::give bronze_mace 1", -- crush -- Black Golem
        -- Recommended (quest-helper getItemRecommended: waterskins, Shantay
        -- passes / coins, food): 5 coins buy the Shantay pass
        -- (shantay.rs2), waterskins hold off desert_heat.rs2's timer, and
        -- lobsters are the golem fights' food.
        "::give coins 5",
        "::give water_skin4 3",
        "::give lobster 6",
        -- Requirement levels, all boostable (elid_qualifies uses stat()):
        "::setlevel magic 33",
        "::setlevel ranged 37",
        "::setlevel mining 37",
        "::setlevel thieving 37",
        -- Combat survivability for three level-75 golems (max hit 4 each,
        -- QUEST_HELPER_COVERAGE precedent: over-gearing hp/att/str/def is
        -- noted, not a quest leg) -- not part of any guide step.
        "::setlevel hitpoints 60",
        "::setlevel defence 50",
        "::setlevel attack 40",
        "::setlevel strength 40",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb1444_elidquest",
            constants = {
                not_started = 0,
                started = 5,
                ghaslor_done = 10,
                robes_key = 20,
                cave_entered = 25,
                golems = 27,
                spirits_done = 30,
                awusah_return = 35,
                shoes_phase = 40,
                genie_deal = 50,
                statuette_phase = 55,
                complete = 60,
            },
            row = "quest_spiritsoftheelid",
            display = "Spirits of the Elid",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.expect("quest.reset", t.quest.expect_stage("not_started"))

        local function tile_text(r, tl)
            if r ~= "ok" or type(tl) ~= "table" then
                return "tile read " .. tostring(r)
            end
            return tl.x .. "," .. tl.z .. "," .. tl.level
        end

        -- A walk graded on the exact tile it reached.
        local function hop(name, x, z, ticks)
            local wr, wd = t.player.walk_to(x, z, ticks)
            local rr, rt = t.world.tile()
            t.check(name, rr == "ok" and rt.x == x and rt.z == z,
                "walk_to " .. x .. "," .. z .. " -> " .. tostring(wr) .. " (" .. tostring(wd) .. "); at "
                    .. tile_text(rr, rt))
        end

        -- The shrine house's arch (3426-3427,2923): nothing to press (no
        -- handler, blockwalk=0), so the row is the walk through it, graded on
        -- the side the player stood on before and the exact tile after.
        local ARCH_Z = 2923
        local function through_arch(name, x, z, going_in)
            local br, bt = t.world.tile()
            local lr, arch = t.world.loc_near("icthalarins_door_arch", 8, { at = { 3426, ARCH_Z, 0 } })
            local wr, wd = t.player.walk_to(x, z, 20)
            local rr, rt = t.world.tile()
            local before_ok, after_ok
            if going_in then
                before_ok = br == "ok" and bt.z < ARCH_Z
                after_ok = rr == "ok" and rt.z > ARCH_Z
            else
                before_ok = br == "ok" and bt.z > ARCH_Z
                after_ok = rr == "ok" and rt.z < ARCH_Z
            end
            t.check(name, lr == "ok" and before_ok and after_ok and rt.x == x and rt.z == z,
                (going_in and "into" or "out of") .. " the shrine house through icthalarins_door_arch ("
                    .. tostring(lr) .. " at " .. tostring(arch and arch.tile_x) .. "," .. tostring(arch and arch.tile_z)
                    .. "): before " .. tile_text(br, bt) .. ", walk_to " .. x .. "," .. z .. " -> " .. tostring(wr)
                    .. " (" .. tostring(wd) .. "), after " .. tile_text(rr, rt))
        end

        -- A fight's margin: the eat detail's "lowest hp x/y" against a quarter
        -- of y, AND lobsters left (never OR).
        local function margin_row(name, fight, dead_detail)
            local low, base = tostring(dead_detail):match("lowest hp (%d+)/(%d+)")
            low, base = tonumber(low), tonumber(base)
            local fr, food = t.inv.count("lobster")
            t.check(name, low ~= nil and base ~= nil and low * 4 >= base and fr == "ok" and (food or 0) >= 1,
                fight .. ": lowest hp " .. tostring(low) .. "/" .. tostring(base) .. ", lobsters staged 6, left "
                    .. tostring(food) .. " (" .. tostring(fr) .. ") (margin: lowest hp >= a quarter of max AND food left)")
        end
        local EAT = { eat = { item = "lobster", below = 30 } }

        -- ======================================= Lumbridge -> the desert ==
        -- The Shantay Pass: buy a pass, then the doorway (a pass check and a
        -- queued teleport through the gate, shantay_pass.rs2).
        t.exec("goto-buyShantayPass", t.player.goto_tile, 3304, 3123, 0)
        t.exec("buyShantayPass", t.player.talk_to, "shantay", 1)
        t.exec("buyShantayPass-dialog", t.chat.play, {
            "npc:Hello effendi, I am Shantay.",
            "npc:I see you're new. Please read",
            "choose:I want to buy a shantay pass for 5 gold coins.",
            "player:I want to buy a shantay pass for",
            "mesbox:You purchase a Shantay Pass.",
        })
        local pass_r, pass_n = t.inv.count("shantay_pass")
        local coin_r, coin_n = t.inv.count("coins")
        t.check("buyShantayPass-verify", pass_r == "ok" and pass_n == 1 and coin_r == "ok" and coin_n == 0,
            "shantay_pass " .. tostring(pass_n) .. " (" .. tostring(pass_r) .. "), coins " .. tostring(coin_n)
                .. " (" .. tostring(coin_r) .. ") -- 5 coins staged, the pass costs 5")
        -- Stand north of the doorway (z 3116) so the pass-check branch runs
        -- (a player at or south of it is only pushed north).
        hop("walk-shantayGate", 3304, 3118, 10)
        t.exec("shantayGate", t.player.click_loc, "shantay_pass_henge_doorway", 1)
        t.exec("shantayGate-dialog", t.chat.play, {
            "mesbox:There is a large poster on the wall",
            "mesbox:The Desert is a VERY Dangerous place",
            "mesbox:That seems pretty scary!",
            "choose:Yeah, that poster doesn't scare me!",
            "npc:Can I see your Shantay Desert Pass",
            "mesbox:You hand over a Shantay Pass.",
            "player:Sure, here you go!",
            "npc:Here, have a disclaimer",
        })
        local gate_landed = t.await({
            level = function()
                local r, tl = t.world.tile()
                return r == "ok" and tl.z < 3116
            end,
            note = "south of the Shantay doorway (z < 3116)",
        }, 10)
        local gate_tr, gate_tile = t.world.tile()
        local gate_pr, gate_pass = t.inv.count("shantay_pass")
        t.check("shantayGate.landed", gate_landed == "ok" and gate_tr == "ok" and gate_tile.z < 3116
            and gate_pr == "ok" and gate_pass == 0,
            "after the doorway: " .. tile_text(gate_tr, gate_tile) .. " (the queued gate teleport lands 3304,3115), "
                .. "shantay_pass " .. tostring(gate_pass) .. " (handed over)")

        -- ================================================= Starting off ==
        t.exec("goto-speakToAwusah", t.player.goto_tile, 3442, 2912, 0)
        -- elid_mayor.rs2 [opnpc1,elid_mayor] -> @elid_mayor_offer at stage 0.
        t.exec("speakToAwusah", t.player.talk_to, "elid_mayor", 1)
        t.exec("speakToAwusah-dialog", t.chat.play, {
            "npc:Oh, adventurer... please, tell",
            "choose:What's wrong?",
            "player:What's wrong?",
            "npc:Our fountain and shrine have r",
            "choose:Any idea how you got this curse?",
            "player:Any idea how you got this curs",
            "npc:I honestly don't know. Ghaslor",
            -- elid_qualifies is true (levels set in setup), so the "Truthfully,
            -- whoever looks into this..." rejection branch is never entered.
            "npc:Would you be willing to look i",
            "choose:Yes.",
            "player:Yes. I'll have a look around a",
            "npc:Thank you! Please, speak to Gh",
        })
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        t.exec("goto-speakToGhaslor", t.player.goto_tile, 3441, 2933, 0)
        -- elid_ghaslor.rs2 [opnpc1,elid_ghaslor] -> @elid_ghaslor_ballad at stage 5.
        t.exec("speakToGhaslor", t.player.talk_to, "elid_ghaslor", 1)
        t.exec("speakToGhaslor-dialog", t.chat.play, {
            "player:Awusah said you might know mor",
            "npc:Ah... yes. It goes back genera",
            "player:River spirits? What are they?",
            "npc:Spirits of the river itself, o",
            -- inv_freespace(inv) < 1 assumed false: setup cleared the pack.
            "npc:You'll need the shrine robes t",
        })
        t.expect("quest.stage.ghaslor_done", t.quest.expect_stage("ghaslor_done"))
        t.exec("gotBallad", t.inv.expect_has, "elid_ballad", 1)

        -- Open then search the cupboard (elid_house.rs2): op1=Open on the
        -- closed loc, then op2=Search on the loc it becomes (all.loc:
        -- elid_cupboard_closed_withrobes op1=Open -> elid_cupboard_open_withrobes
        -- op2=Search -> ~elid_search_cupboard). The cupboard is in the shrine
        -- house: goto the open street south of its arch, walk in.
        t.exec("goto-shrineArch", t.player.goto_tile, 3426, 2921, 0)
        through_arch("enterShrine", 3426, 2925, true)
        t.exec("openCupboard", t.player.click_loc, "elid_cupboard_closed_withrobes", 1)
        -- elid_house.rs2's [oploc1,elid_cupboard_closed_withrobes] suspends on
        -- p_arrivedelay + anim(human_openchest) + p_delay(0) before loc_change
        -- -- the click's own settle can resolve before that chain lands (and
        -- before the "You open the cupboard." mes()), so AWAIT the message
        -- rather than a bare t.msg.expect, then search the (now different)
        -- loc symbol.
        t.exec("openCupboard.msg", t.msg.await, "You open the cupboard.", 5)
        t.exec("searchCupboard", t.player.click_loc, "elid_cupboard_open_withrobes", 2)
        t.exec("gotTornRobes", t.inv.await_all, { elid_robetop_torn = 1, elid_robebottoms_torn = 1 }, 10)

        -- Mend both torn robes: [opheldu,elid_robetop_torn]/[elid_robebottoms_torn]
        -- fire on "needle used on <torn robe>" (last_useitem = needle), so the
        -- robe is the TARGET (item_b) and the needle is armed (item_a).
        t.exec("mendTop", t.player.use_item_on_item, "needle", "elid_robetop_torn")
        t.exec("mendBottom", t.player.use_item_on_item, "needle", "elid_robebottoms_torn")
        t.exec("gotMendedRobes", t.inv.await_all, { elid_robetop = 1, elid_robebottoms = 1 }, 10)
        local torn_top_r, torn_top = t.inv.count("elid_robetop_torn")
        local torn_bot_r, torn_bot = t.inv.count("elid_robebottoms_torn")
        local thread_r, thread_n = t.inv.count("thread")
        t.check("mendRobes.consumed", torn_top_r == "ok" and torn_top == 0 and torn_bot_r == "ok" and torn_bot == 0
            and thread_r == "ok" and thread_n == 0,
            "after both mends: elid_robetop_torn " .. tostring(torn_top) .. ", elid_robebottoms_torn " .. tostring(torn_bot)
                .. ", thread " .. tostring(thread_n) .. " (2 staged, one per mend)")

        -- The ancestral key: Telekinetic Grab from the shrine floor, across
        -- elid_temple_railing (x 3430, blockrange=0) -- the table (3432,2928)
        -- stands in a pocket the railing closes, so the player never steps
        -- behind it. Content's own line: "You should Telekinetic Grab the
        -- ancestral key from the table" (elid_house.rs2:117) -- seam23
        -- cast_on_ground_obj_and_loc (t.player.cast on a {kind="obj"} target).
        -- ANY-OF: telegrabKey telegrabKey.cast elid_house.rs2:117 routes the pickup through a Telekinetic Grab cast on the ground obj, not a table trigger
        -- (the guide's own target elid_wooden_table has no [op*]/[ap*]
        -- trigger in this port at all -- quest_spiritsoftheelid.constant:84-92
        -- says the pickup needs no quest script whatsoever).
        hop("walk-telegrabKey", 3429, 2928, 15)
        local key_before_result, key_before = t.inv.count("elid_key")
        local air_before_r, air_before = t.inv.count("airrune")
        local law_before_r, law_before = t.inv.count("lawrune")
        t.exec("telegrabKey.cast", t.player.cast, "telegrab", { kind = "obj", id = "elid_key" })
        local key_after_result, key_after = t.inv.count("elid_key")
        local air_after_r, air_after = t.inv.count("airrune")
        local law_after_r, law_after = t.inv.count("lawrune")
        local grab_tr, grab_tile = t.world.tile()
        t.check("telegrabKey", key_after_result == "ok" and (key_after or 0) > (key_before or 0)
            and air_after_r == "ok" and air_before_r == "ok" and air_before - air_after == 1
            and law_after_r == "ok" and law_before_r == "ok" and law_before - law_after == 1
            and grab_tr == "ok" and grab_tile.x <= 3430,
            string.format("cast telegrab on elid_key from %s (x <= 3430, the railing's near side); elid_key %s(%s) -> %s(%s); "
                .. "airrune %s -> %s, lawrune %s -> %s (one of each)",
                tile_text(grab_tr, grab_tile), tostring(key_before), tostring(key_before_result),
                tostring(key_after), tostring(key_after_result), tostring(air_before), tostring(air_after),
                tostring(law_before), tostring(law_after)))

        -- Wear the robes -- the robe door checks inv_total(worn, ...) on both.
        t.exec("equipRobeTop", t.player.equip, "elid_robetop")
        t.exec("equipRobeBottom", t.player.equip, "elid_robebottoms")
        t.expect("quest.stage.robes_key", t.quest.expect_stage("robes_key"))

        hop("walk-leaveShrine.inside", 3426, 2925, 15)
        through_arch("leaveShrine", 3426, 2921, false)

        -- ============================================ The Golems, gather ==
        -- Bow and arrows spawn along the river near the cave entrance
        -- (m52_48.spawn) -- quest-helper's own bow/arrows ItemRequirement
        -- tooltips say "obtainable during quest", so gathered here rather
        -- than ::given (trap (c)).
        -- Arrows first (the south spawn), then walk north up the river's east
        -- bank past the bow to the foot of the waterfall.
        t.exec("goto-getArrows", t.player.goto_tile, 3391, 3087, 0)
        local arrow_before_result, arrow_before = t.inv.count("bronze_arrow")
        local arrow_take_result = t.player.click_obj("bronze_arrow", 3)
        local arrow_after_result, arrow_after = t.inv.count("bronze_arrow")
        t.check("takeArrows", arrow_take_result == "ok" and arrow_after_result == "ok"
            and (arrow_after or 0) > (arrow_before or 0),
            string.format("click_obj(bronze_arrow,3) -> %s; bronze_arrow %s(%s) -> %s(%s)",
                tostring(arrow_take_result), tostring(arrow_before), tostring(arrow_before_result),
                tostring(arrow_after), tostring(arrow_after_result)))

        t.exec("walk-getShortbow", t.player.walk_route, { { 3391, 3087 }, { 3391, 3095 }, { 3389, 3101 },
            { 3389, 3109 }, { 3389, 3115 }, { 3389, 3121 } })
        -- (stops a tile short of the bow's own tile, m52_48.spawn 3389,3123:
        -- standing on a ground item reads not_visible, run 2)
        local bow_before_result, bow_before = t.inv.count("shortbow")
        local bow_take_result = t.player.click_obj("shortbow", 3)
        local bow_after_result, bow_after = t.inv.count("shortbow")
        t.check("takeShortbow", bow_take_result == "ok" and bow_after_result == "ok"
            and (bow_after or 0) > (bow_before or 0),
            string.format("click_obj(shortbow,3) -> %s; shortbow %s(%s) -> %s(%s)",
                tostring(bow_take_result), tostring(bow_before), tostring(bow_before_result),
                tostring(bow_after), tostring(bow_after_result)))

        -- Rope onto the root: [oplocu,desert_water_cave_root] -- use_on, not
        -- click_loc (an item-used-on-loc trigger, not a numbered op). The
        -- player walks up to the foot of the waterfall (3372,3129, the
        -- nearest tile any walk reaches) and uses the rope.
        local bow_tr, bow_tile = t.world.tile()
        local route_start = (bow_tr == "ok") and { bow_tile.x, bow_tile.z } or { 3389, 3123 }
        t.exec("walk-enterCave", t.player.walk_route, { route_start, { 3381, 3123 }, { 3374, 3124 }, { 3372, 3129 } })
        local cave_root = t.player.by_symbol("loc", "desert_water_cave_root")
        local enterCave_result, enterCave_detail = t.player.use_on("rope", cave_root)
        local cave_tr, cave_tile = t.world.tile()
        local entered = cave_tr == "ok" and cave_tile.z > 9000
        if not entered then
            t.check("enterCave.unreachable", enterCave_result == "refused" and cave_tr == "ok"
                and cave_tile.z < 9000 and tostring(enterCave_detail):find("can't reach", 1, true) ~= nil,
                "use_on(rope,desert_water_cave_root) from the river bank -> " .. tostring(enterCave_result)
                    .. " (" .. tostring(enterCave_detail) .. "); still on the surface at " .. tile_text(cave_tr, cave_tile))
            t.blocked("content_bug: the Water Ravine Dungeon cannot be entered, crossed or left on foot -- "
                .. "desert_water_cave_root is out of reach, and elid_underground_robe_door, elid_whitegolem_door, "
                .. "elid_greygolem_door, elid_blackgolem_door and elid_underground_lake_door never open. "
                .. "(1) desert_water_cave_root (3369-3371,3132, width 3, all.loc:57455) is approached only from "
                .. "3369-3370,3130-3131, a 4-tile pocket the map closes with river water (OSRS-Content/osrs239-content/"
                .. "maps/m52_48.jm2 f1 at 41-43,57-58; comp.py 3369 3131 floods 4 tiles, no door, no op loc); a rope "
                .. "from the bank answers \"I can't reach that!\" from every approach tile (this row; probe "
                .. "build/quest_gate/spiritsoftheelid_probe1 row 2), and ^elid_cave_exit_coord 0_52_48_42_59 "
                .. "(quest_spiritsoftheelid.constant:142, used by elid_dungeon.rs2:32) lands the player back in that "
                .. "pocket with no walk out (probe1 rows 24-26). (2) elid_dungeon.rs2:36 [oploc1,elid_underground_robe_door] "
                .. "and :229 [oploc1,elid_underground_lake_door] shadow doors/scripts/doors.rs2:89 [oploc1,_door_closed] "
                .. "~door_open_active (doors/configs/doors.loc:836-850 give both a door_closed category and an _open "
                .. "stage) and never call it -- the robe door says 'step through' and moves nobody (probe1 rows 11-15: "
                .. "the leaf stays on 3353,9544, a walk to 3353,9546 stalls) -- and the golem doors "
                .. "(elid_dungeon.rs2:57/105/173) have no doors.loc stage at all. Needed: an approach the bank reaches "
                .. "(an [aplocu] at range, or a walkable ledge) with an exit coord outside the pocket, and the five "
                .. "dungeon doors opening through ~door_open_active after their checks.")
            return
        end
        t.check("enterCave", enterCave_result == "ok" and entered, "use_on(rope,desert_water_cave_root) -> "
            .. tostring(enterCave_result) .. " (" .. tostring(enterCave_detail) .. "); at " .. tile_text(cave_tr, cave_tile))
        t.exec("enterCave.msg", t.msg.expect, "climb down into the dungeon")
        t.exec("expect_stage.cave_entered", t.quest.expect_stage, "cave_entered")

        -- ---- Below: the dungeon on foot, for when the content is fixed ----
        -- Ancestral key on the robe door: [oploc1,elid_underground_robe_door]
        -- (numbered op1), robes worn + key held checked inline. The door
        -- (wall on the north edge of 3353,9544) is pressed on every crossing.
        local function robe_door(name, inbound)
            if inbound then
                t.exec(name, t.player.pass_door, { closed = "elid_underground_robe_door",
                    open = "elid_underground_robe_door_open", at = { 3353, 9544, 0 },
                    near = { 3353, 9544 }, far = { 3353, 9545 } })
            else
                t.exec(name, t.player.pass_door, { closed = "elid_underground_robe_door",
                    open = "elid_underground_robe_door_open", at = { 3353, 9544, 0 },
                    near = { 3353, 9545 }, far = { 3353, 9544 } })
            end
        end
        robe_door("useAncestralKey", true)
        t.exec("useAncestralKey.msg", t.msg.expect, "unlock the door with the ancestral key")
        t.expect("quest.stage.golems", t.quest.expect_stage("golems"))

        -- Content bug (elid_journal.rs2:28, reported not routed-around by
        -- editing content): the "mend the robes" journal branch is
        -- `inv_total(inv, elid_robetop) = 0 | inv_total(inv, elid_robebottoms)
        -- = 0` with NO %elidquest bound -- once the robes are WORN this
        -- branch shadows every later stage branch. Unequip after the door;
        -- re-equip before pressing it again on the way out.
        t.exec("unequipRobeTop", t.player.unequip, "elid_robetop")
        t.exec("unequipRobeBottom", t.player.unequip, "elid_robebottoms")

        -- One golem: through its door, the fight, the channel, back out.
        local function golem_room(g)
            t.exec("equip-" .. g.weapon, t.player.equip, g.weapon)
            hop("walk-" .. g.door_row, g.near[1], g.near[2], 40)
            t.exec(g.door_row, t.player.pass_door, { closed = g.door, at = g.at, near = g.near, far = g.far })
            t.exec(g.door_row .. ".msg", t.msg.expect, g.lumbers)
            t.exec(g.attack_row, t.player.attack, g.npc, 2, 20)
            local dr, dd = t.npc.await_dead_engaged(120, 8, EAT)
            t.check(g.dead_row, dr == "ok", tostring(dr) .. " " .. tostring(dd))
            margin_row(g.dead_row .. ".margin", g.fight, dd)
            g.channel()
            t.exec(g.leave_row, t.player.pass_door, { closed = g.door, at = g.at, near = g.far, far = g.near })
        end

        golem_room({
            weapon = "bronze_dagger", door = "elid_whitegolem_door", at = { 3365, 9542, 0 },
            near = { 3365, 9543 }, far = { 3365, 9541 }, door_row = "openStabDoor",
            lumbers = "White Golem lumbers out", npc = "elid_golem_white", attack_row = "attackWhiteGolem",
            dead_row = "whiteGolem.dead", fight = "White Golem (level 75, stab-weak)", leave_row = "leaveStabRoom",
            channel = function()
                t.exec("clearChannel", t.player.click_loc, "elid_water_channel_spiketrap", 1)
                t.exec("clearChannel.msg", t.msg.expect, "disarm the spike trap")
            end,
        })
        golem_room({
            weapon = "bronze_scimitar", door = "elid_greygolem_door", at = { 3374, 9547, 0 },
            near = { 3373, 9547 }, far = { 3375, 9547 }, door_row = "openSlashDoor",
            lumbers = "Grey Golem lumbers out", npc = "elid_golem_grey", attack_row = "attackGreyGolem",
            dead_row = "greyGolem.dead", fight = "Grey Golem (level 75, slash-weak)", leave_row = "leaveSlashRoom",
            channel = function()
                t.exec("clearChannel2", t.player.click_loc, "elid_water_channel_blocked_rocks", 1)
                t.exec("clearChannel2.msg", t.msg.expect, "mine through the blocking rocks")
            end,
        })
        golem_room({
            weapon = "bronze_mace", door = "elid_blackgolem_door", at = { 3372, 9556, 0 },
            near = { 3371, 9556 }, far = { 3373, 9556 }, door_row = "openCrushDoor",
            lumbers = "Black Golem lumbers out", npc = "elid_golem_black", attack_row = "attackBlackGolem",
            dead_row = "blackGolem.dead", fight = "Black Golem (level 75, crush-weak)", leave_row = "leaveCrushRoom",
            channel = function()
                -- elid_ranging_target has no npc_stats (op2=Shoot, no combat
                -- block): t.player.press (content-line outcome), not attack.
                t.exec("equipShortbow", t.player.equip, "shortbow")
                t.exec("equipArrows", t.player.equip, "bronze_arrow")
                hop("walk-clearChannel3", 3375, 9557, 10)
                local rr, rd = t.player.press("elid_ranging_target", 2, 10)
                if rr == "no_row" then
                    rr, rd = t.player.press("elid_ranging_target_multinpc", 2, 10)
                end
                t.check("clearChannel3", rr == "ok" and tostring(rd):find("blockage gives way") ~= nil,
                    "press(elid_ranging_target,2) -> " .. tostring(rr) .. " (" .. tostring(rd) .. ")")
                hop("walk-leaveCrushRoom", 3373, 9556, 10)
            end,
        })
        t.expect("quest.stage.golems_still", t.quest.expect_stage("golems")) -- unchanged until the lake door

        -- Lake door (wall on the north edge of 3354,9558): all three channels clear.
        hop("walk-openFarNorthDoor", 3354, 9558, 40)
        t.exec("openFarNorthDoor", t.player.pass_door, { closed = "elid_underground_lake_door",
            open = "elid_underground_lake_door_open", at = { 3354, 9558, 0 }, near = { 3354, 9558 }, far = { 3354, 9559 } })
        t.exec("openFarNorthDoor.msg", t.msg.expect, "water level drops and the door swings open")

        -- River spirits: @elid_spirits_talk (stage golems .. < spirits_done).
        t.exec("walk-speakToSpirits", t.player.walk_route, { { 3354, 9559 }, { 3358, 9566 }, { 3362, 9574 },
            { 3365, 9582 }, { 3367, 9586 } })
        t.exec("speakToSpirits", t.player.talk_to, "elid_waterspirit", 1)
        t.exec("speakToSpirits-dialog", t.chat.play, {
            "player:I come as an emissary from the people of Nardah.",
            "npc:We are Nirrie, Tirrie and Hallak",
            "player:Is there anything they can do",
            "npc:Long ago, Nardah cast out the statuette",
            "npc:Recover the statuette, restore it",
        })
        t.expect("quest.stage.spirits_done", t.quest.expect_stage("spirits_done"))

        -- Out the way the player came: the lake door, the robe door (robes
        -- worn again for its check), the cave exit.
        t.exec("walk-leaveLake", t.player.walk_route, { { 3367, 9586 }, { 3365, 9582 }, { 3362, 9574 },
            { 3358, 9566 }, { 3354, 9559 } })
        t.exec("leaveLake", t.player.pass_door, { closed = "elid_underground_lake_door",
            open = "elid_underground_lake_door_open", at = { 3354, 9558, 0 }, near = { 3354, 9559 }, far = { 3354, 9558 } })
        t.exec("equipRobeTop.out", t.player.equip, "elid_robetop")
        t.exec("equipRobeBottom.out", t.player.equip, "elid_robebottoms")
        hop("walk-leaveRobeDoor", 3353, 9545, 40)
        robe_door("leaveRobeDoor", false)
        t.exec("unequipRobeTop.out", t.player.unequip, "elid_robetop")
        t.exec("unequipRobeBottom.out", t.player.unequip, "elid_robebottoms")
        t.exec("leaveCave", t.player.click_loc, "elid_underground_exit", 1)
        t.exec("leaveCave.msg", t.msg.await, "climb back up out of the dungeon", 5)
        hop("walk-leaveCave.bank", 3372, 3129, 15)

        -- ==================================================== The Genie ==
        -- elid_mayor_reveal: at stage spirits_done, sets awusah_return then
        -- immediately shoes_phase in the same call (elid_mayor.rs2).
        t.exec("goto-speakToAwusah2", t.player.goto_tile, 3442, 2912, 0)
        t.exec("speakToAwusah2", t.player.talk_to, "elid_mayor", 1)
        t.exec("speakToAwusah2-dialog", t.chat.play, {
            "player:The river spirits told me the fountain went dry",
            "npc:The old statuette?",
            "npc:If you could recover it somehow",
            "npc:Oh -- and take these old shoes",
        })
        t.exec("speakToAwusah2.msg", t.msg.expect, "shoes are sitting by the doorway")
        t.expect("quest.stage.shoes_phase", t.quest.expect_stage("shoes_phase"))

        -- Take Awusah's shoes, a plain ground item by his doorway
        -- (m53_45.spawn: "elid_shoes 3439 2913 0"), from the open floor beside
        -- them (3438,2913 is a potted fern).
        hop("walk-takeShoes", 3440, 2913, 10)
        local shoes_before_result, shoes_before = t.inv.count("elid_shoes")
        local shoes_take_result = t.player.click_obj("elid_shoes", 3)
        local shoes_after_result, shoes_after = t.inv.count("elid_shoes")
        t.check("takeShoes", shoes_take_result == "ok" and shoes_after_result == "ok"
            and (shoes_after or 0) > (shoes_before or 0),
            string.format("click_obj(elid_shoes,3) -> %s; elid_shoes %s(%s) -> %s(%s)",
                tostring(shoes_take_result), tostring(shoes_before), tostring(shoes_before_result),
                tostring(shoes_after), tostring(shoes_after_result)))

        -- Cut the sole with the knife: [opheldu,elid_shoes], knife armed.
        t.exec("cutShoes", t.player.use_item_on_item, "knife", "elid_shoes")
        t.exec("gotSole", t.inv.expect_has, "elid_sole", 1)
        local shoes_left_r, shoes_left = t.inv.count("elid_shoes")
        t.check("cutShoes.consumed", shoes_left_r == "ok" and shoes_left == 0,
            "elid_shoes after the cut: " .. tostring(shoes_left) .. " (" .. tostring(shoes_left_r) .. ")")

        -- Crevice west of Nardah: [oploc1,elid_crevice_clickzone] (numbered
        -- op), gated on rope + a lit light source.
        t.exec("goto-enterCrevice", t.player.goto_tile, 3372, 2905, 0)
        t.exec("enterCrevice", t.player.click_loc, "elid_crevice_clickzone", 1)
        t.exec("enterCrevice.msg", t.msg.expect, "climb down into the crevice")

        -- Genie, first deal: agree to trade the sole for the statuette. The
        -- climb lands in the genie's room (^elid_crevice_arrival_coord 3370,9320).
        t.exec("talkToGenie", t.player.talk_to, "elid_genie", 1)
        t.exec("talkToGenie-dialog", t.chat.play, {
            "npc:Well, well. A visitor.",
            "choose:I'm after a statue that was thrown down here.",
            "player:I'm after a statue that was thrown down here.",
            "npc:Ohh, that old thing?",
            "choose:Maybe I can make a deal for it?",
            "player:Maybe I can make a deal for it?",
            "npc:A deal, yes!",
            "player:You want me to kill Awusah?!",
            "npc:Ha! No, no",
            "choose:Ok, I agree to the deal.",
            "player:Ok, I agree to the deal.",
            "npc:Splendid!",
        })
        t.expect("quest.stage.genie_deal", t.quest.expect_stage("genie_deal"))

        -- Genie, second visit with the sole: @elid_genie_take_sole.
        t.exec("talkToGenieAgain", t.player.talk_to, "elid_genie", 1)
        t.exec("talkToGenieAgain-dialog", t.chat.play, {
            "npc:Ahh, delicious.",
        })
        t.exec("gotStatuette", t.inv.expect_has, "elid_statuette", 1)
        t.expect("quest.stage.statuette_phase", t.quest.expect_stage("statuette_phase"))

        -- Out of the crevice on foot: the genie's door (doors.loc:852, a
        -- door_closed stage) and the climbing rope at 3373,9305
        -- (probe build/quest_gate/spiritsoftheelid_probe2: lands 3375,2904).
        t.exec("leaveGenie", t.player.pass_door, { closed = "elid_genie_door", open = "elid_genie_door_open",
            at = { 3371, 9312, 0 }, near = { 3371, 9313 }, far = { 3371, 9310 } })
        t.exec("climbRope", t.player.click_loc, "elid_climbing_rope", 1)
        local rope_landed = t.await({
            level = function()
                local r, tl = t.world.tile()
                return r == "ok" and tl.z < 9000
            end,
            note = "back on the surface west of Nardah",
        }, 10)
        local rope_tr, rope_tile = t.world.tile()
        t.check("climbRope.landed", rope_landed == "ok" and rope_tr == "ok" and rope_tile.z < 9000
            and math.abs(rope_tile.x - 3375) <= 2 and math.abs(rope_tile.z - 2904) <= 2,
            "after elid_climbing_rope: " .. tile_text(rope_tr, rope_tile) .. " (probe2 landed 3375,2904)")

        -- ============================================== Hand-in + reward ==
        local snap_result, snap = t.skill.snapshot()
        t.step("reward.snapshot", snap_result == "ok" and "PASS" or "FAIL", tostring(snap_result))

        -- Statuette on the plinth: [oplocu,elid_statuette_base] -- use_on,
        -- not click_loc. The plinth is in the shrine house: in by the arch.
        t.exec("goto-useStatuette", t.player.goto_tile, 3426, 2921, 0)
        through_arch("enterShrine2", 3426, 2925, true)
        local plinth = t.player.by_symbol("loc", "elid_statuette_multiloc")
        local useStatuette_result, useStatuette_detail = t.player.use_on("elid_statuette", plinth)
        local statuette_r, statuette_n = t.inv.count("elid_statuette")
        t.check("useStatuette", useStatuette_result == "ok" and statuette_r == "ok" and statuette_n == 0,
            "use_on(elid_statuette,elid_statuette_multiloc) -> " .. tostring(useStatuette_result) .. " ("
                .. tostring(useStatuette_detail) .. "); elid_statuette left in the pack: " .. tostring(statuette_n))

        t.ticks(3) -- completion's own reward scroll / journal text mounts asynchronously (section 8 idiom)
        t.quest.expect_complete()

        t.exec("reward.prayer_xp", t.skill.expect_gain, "prayer", 8000, snap)
        t.exec("reward.thieving_xp", t.skill.expect_gain, "thieving", 1000, snap)
        t.exec("reward.magic_xp", t.skill.expect_gain, "magic", 1000, snap)

        t.finish(0)
        return
    end,
}
