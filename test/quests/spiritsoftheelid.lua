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
        -- op2=Search -> ~elid_search_cupboard).
        t.exec("goto-openCupboard", t.player.goto_tile, 3420, 2930, 0)
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

        -- The ancestral key: Telekinetic Grab from beside Shiratti's table,
        -- as content's own line says ("You should Telekinetic Grab the
        -- ancestral key from the table", elid_house.rs2:117) -- seam23
        -- cast_on_ground_obj_and_loc (t.player.cast on a {kind="obj"} target).
        -- ANY-OF: telegrabKey telegrabKey.cast elid_house.rs2:117 routes the pickup through a Telekinetic Grab cast on the ground obj, not a table trigger
        -- (the guide's own target elid_wooden_table has no [op*]/[ap*]
        -- trigger in this port at all -- quest_spiritsoftheelid.constant:84-92
        -- says the pickup needs no quest script whatsoever). Driven for real
        -- below with t.player.cast on elid_key.
        t.exec("goto-telegrabKey", t.player.goto_tile, 3432, 2929, 0)
        local key_before_result, key_before = t.inv.count("elid_key")
        t.exec("telegrabKey.cast", t.player.cast, "telegrab", { kind = "obj", id = "elid_key" })
        local key_after_result, key_after = t.inv.count("elid_key")
        t.check("telegrabKey", key_after_result == "ok" and (key_after or 0) > (key_before or 0),
            string.format("cast telegrab on elid_key; elid_key %s(%s) -> %s(%s)",
                tostring(key_before), tostring(key_before_result),
                tostring(key_after), tostring(key_after_result)))

        -- Wear the robes -- the robe door checks inv_total(worn, ...) on both.
        t.exec("equipRobeTop", t.player.equip, "elid_robetop")
        t.exec("equipRobeBottom", t.player.equip, "elid_robebottoms")
        t.expect("quest.stage.robes_key", t.quest.expect_stage("robes_key"))

        -- ============================================ The Golems, gather ==
        -- Bow and arrows spawn along the river near the cave entrance
        -- (m52_48.spawn) -- quest-helper's own bow/arrows ItemRequirement
        -- tooltips say "obtainable during quest", so gathered here rather
        -- than ::given (trap (c)).
        t.exec("goto-getShortbow", t.player.goto_tile, 3389, 3123, 0)
        local bow_before_result, bow_before = t.inv.count("shortbow")
        local bow_take_result = t.player.click_obj("shortbow", 3)
        local bow_after_result, bow_after = t.inv.count("shortbow")
        t.check("takeShortbow", bow_take_result == "ok" and bow_after_result == "ok"
            and (bow_after or 0) > (bow_before or 0),
            string.format("click_obj(shortbow,3) -> %s; shortbow %s(%s) -> %s(%s)",
                tostring(bow_take_result), tostring(bow_before), tostring(bow_before_result),
                tostring(bow_after), tostring(bow_after_result)))

        t.exec("goto-getArrows", t.player.goto_tile, 3391, 3087, 0)
        local arrow_before_result, arrow_before = t.inv.count("bronze_arrow")
        local arrow_take_result = t.player.click_obj("bronze_arrow", 3)
        local arrow_after_result, arrow_after = t.inv.count("bronze_arrow")
        t.check("takeArrows", arrow_take_result == "ok" and arrow_after_result == "ok"
            and (arrow_after or 0) > (arrow_before or 0),
            string.format("click_obj(bronze_arrow,3) -> %s; bronze_arrow %s(%s) -> %s(%s)",
                tostring(arrow_take_result), tostring(arrow_before), tostring(arrow_before_result),
                tostring(arrow_after), tostring(arrow_after_result)))

        -- Rope onto the root: [oplocu,desert_water_cave_root] -- use_on, not
        -- click_loc (the scaffold's guess was wrong: this is an item-used-on-loc
        -- trigger, not a numbered op).
        t.exec("goto-enterCave", t.player.goto_tile, 3370, 3132, 0)
        local cave_root = t.player.by_symbol("loc", "desert_water_cave_root")
        local enterCave_result, enterCave_detail = t.player.use_on("rope", cave_root)
        t.check("enterCave", enterCave_result == "ok", "use_on(rope,desert_water_cave_root) -> "
            .. tostring(enterCave_result) .. " (" .. tostring(enterCave_detail) .. ")")
        t.exec("enterCave.msg", t.msg.expect, "climb down into the dungeon")
        t.exec("expect_stage.cave_entered", t.quest.expect_stage, "cave_entered")

        -- Ancestral key on the robe door: [oploc1,elid_underground_robe_door]
        -- (numbered op1), robes worn + key held checked inline.
        t.exec("goto-useAncestralKey", t.player.goto_tile, 3353, 9544, 0)
        t.exec("useAncestralKey", t.player.click_loc, "elid_underground_robe_door", 1)
        t.exec("useAncestralKey.msg", t.msg.expect, "unlock the door with the ancestral key")
        t.expect("quest.stage.golems", t.quest.expect_stage("golems"))

        -- Content bug (elid_journal.rs2:28, reported not routed-around by
        -- editing content): the "mend the robes" journal branch is
        -- `inv_total(inv, elid_robetop) = 0 | inv_total(inv, elid_robebottoms)
        -- = 0` with NO %elidquest bound, unlike every other branch in that
        -- if/else-if chain -- once the robes are WORN (not carried), this
        -- branch reads true forever after and permanently shadows every
        -- later stage branch, including the final "QUEST COMPLETE!" one.
        -- The robe door only checks worn state AT THE CLICK, nothing later
        -- requires staying in them, so unequip right after (ordinary gear
        -- management, not a guide step either way) keeps elid_robetop/
        -- elid_robebottoms in the backpack for the journal's own sake.
        t.exec("unequipRobeTop", t.player.unequip, "elid_robetop")
        t.exec("unequipRobeBottom", t.player.unequip, "elid_robebottoms")

        -- ---- White Golem (south door, stab-weak, thieving channel) -------
        t.exec("equipDagger", t.player.equip, "bronze_dagger")
        t.exec("goto-openStabDoor", t.player.goto_tile, 3365, 9542, 0)
        t.exec("openStabDoor", t.player.click_loc, "elid_whitegolem_door", 1)
        t.exec("attackWhiteGolem", t.player.attack, "elid_golem_white", 2, 20)
        t.exec("whiteGolem.dead", t.npc.await_dead_engaged, 80, 8)
        t.exec("goto-clearChannel", t.player.goto_tile, 3365, 9538, 0)
        t.exec("clearChannel", t.player.click_loc, "elid_water_channel_spiketrap", 1)
        t.exec("clearChannel.msg", t.msg.expect, "disarm the spike trap")

        -- ---- Grey Golem (east door, slash-weak, mining channel) ----------
        t.exec("equipScimitar", t.player.equip, "bronze_scimitar")
        t.exec("goto-openSlashDoor", t.player.goto_tile, 3374, 9547, 0)
        t.exec("openSlashDoor", t.player.click_loc, "elid_greygolem_door", 1)
        t.exec("attackGreyGolem", t.player.attack, "elid_golem_grey", 2, 20)
        t.exec("greyGolem.dead", t.npc.await_dead_engaged, 80, 8)
        t.exec("goto-clearChannel2", t.player.goto_tile, 3378, 9547, 0)
        t.exec("clearChannel2", t.player.click_loc, "elid_water_channel_blocked_rocks", 1)
        t.exec("clearChannel2.msg", t.msg.expect, "mine through the blocking rocks")

        -- ---- Black Golem (north-east door, crush-weak) + ranging channel -
        t.exec("equipMace", t.player.equip, "bronze_mace")
        t.exec("goto-openCrushDoor", t.player.goto_tile, 3372, 9556, 0)
        t.exec("openCrushDoor", t.player.click_loc, "elid_blackgolem_door", 1)
        t.exec("attackBlackGolem", t.player.attack, "elid_golem_black", 2, 20)
        t.exec("blackGolem.dead", t.npc.await_dead_engaged, 80, 8)

        -- elid_ranging_target has no npc_stats (op2=Shoot, no combat block) --
        -- t.player.attack would wait forever on a health bar this npc never
        -- gets. It answers a plain mes() with no page and no npc movement, so
        -- t.player.press (content-line outcome) is the verb, not attack.
        t.exec("equipShortbow", t.player.equip, "shortbow")
        t.exec("equipArrows", t.player.equip, "bronze_arrow")
        t.exec("goto-clearChannel3", t.player.goto_tile, 3376, 9557, 0)
        local ranging_press_result, ranging_press_detail = t.player.press("elid_ranging_target", 2, 10)
        if ranging_press_result == "no_row" then
            -- multinpc wrapper symbol fallback (trap 19's shape): the spawn
            -- row is elid_ranging_target_multinpc (m52_149.spawn), and the
            -- live pool may carry that symbol rather than the child's.
            ranging_press_result, ranging_press_detail = t.player.press("elid_ranging_target_multinpc", 2, 10)
        end
        t.check("clearChannel3", ranging_press_result == "ok"
            and tostring(ranging_press_detail):find("blockage gives way") ~= nil,
            "press(elid_ranging_target,2) -> " .. tostring(ranging_press_result)
            .. " (" .. tostring(ranging_press_detail) .. ")")
        t.expect("quest.stage.golems_still", t.quest.expect_stage("golems")) -- unchanged until the lake door

        -- Lake door: all three channels clear.
        t.exec("goto-openFarNorthDoor", t.player.goto_tile, 3354, 9558, 0)
        t.exec("openFarNorthDoor", t.player.click_loc, "elid_underground_lake_door", 1)
        t.exec("openFarNorthDoor.msg", t.msg.expect, "water level drops and the door swings open")

        -- River spirits: @elid_spirits_talk (stage golems .. < spirits_done).
        t.exec("goto-speakToSpirits", t.player.goto_tile, 3367, 9586, 0)
        t.exec("speakToSpirits", t.player.talk_to, "elid_waterspirit", 1)
        t.exec("speakToSpirits-dialog", t.chat.play, {
            "player:I come as an emissary from the people of Nardah.",
            "npc:We are Nirrie, Tirrie and Hallak",
            "player:Is there anything they can do",
            "npc:Long ago, Nardah cast out the statuette",
            "npc:Recover the statuette, restore it",
        })
        t.expect("quest.stage.spirits_done", t.quest.expect_stage("spirits_done"))

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

        -- Take Awusah's shoes, a plain ground item by his doorway.
        -- Step off elid_shoes' own tile (m53_45.spawn: "elid_shoes 3439 2913
        -- 0") before clicking it -- standing on a ground item's own tile
        -- reads `covered` (measured: click_obj hunted 32 ticks and failed).
        t.exec("goto-takeShoes", t.player.goto_tile, 3438, 2913, 0)
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

        -- Crevice west of Nardah: [oploc1,elid_crevice_clickzone] (numbered
        -- op), gated on rope + a lit light source.
        t.exec("goto-enterCrevice", t.player.goto_tile, 3373, 2905, 0)
        t.exec("enterCrevice", t.player.click_loc, "elid_crevice_clickzone", 1)
        t.exec("enterCrevice.msg", t.msg.expect, "climb down into the crevice")

        -- Genie, first deal: agree to trade the sole for the statuette.
        t.exec("goto-talkToGenie", t.player.goto_tile, 3371, 9320, 0)
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

        -- No quest-helper step covers leaving the crevice (creviceSteps'
        -- ladder ends at talkToGenieAgain / useStatuette -- there is no
        -- climbUp QuestStep) -- it is plain travel back to a room the
        -- player already reached for real on the way down, so goto_tile
        -- straight to the shrine plinth below is not a skipped guide leg.

        -- ============================================== Hand-in + reward ==
        local snap_result, snap = t.skill.snapshot()
        t.step("reward.snapshot", snap_result == "ok" and "PASS" or "FAIL", tostring(snap_result))

        -- Statuette on the plinth: [oplocu,elid_statuette_base] -- use_on,
        -- not click_loc (another item-used-on-loc trigger the scaffold
        -- guessed wrong).
        t.exec("goto-useStatuette", t.player.goto_tile, 3427, 2930, 0)
        local plinth = t.player.by_symbol("loc", "elid_statuette_multiloc")
        local useStatuette_result, useStatuette_detail = t.player.use_on("elid_statuette", plinth)
        t.check("useStatuette", useStatuette_result == "ok", "use_on(elid_statuette,elid_statuette_multiloc) -> "
            .. tostring(useStatuette_result) .. " (" .. tostring(useStatuette_detail) .. ")")

        t.ticks(3) -- completion's own reward scroll / journal text mounts asynchronously (section 8 idiom)
        t.quest.expect_complete()

        t.exec("reward.prayer_xp", t.skill.expect_gain, "prayer", 8000, snap)
        t.exec("reward.thieving_xp", t.skill.expect_gain, "thieving", 1000, snap)
        t.exec("reward.magic_xp", t.skill.expect_gain, "magic", 1000, snap)

        t.finish(0)
        return
    end,
}
