-- Nature Spirit, driven end to end from the Quest Helper ladder (naturespirit/NatureSpirit.java).
-- Route facts: quest_druidspirit/scripts/{quest_druidspirit,filliman,druidspirit_drezel,ghast,swamp_decay}.rs2.
-- Brought along (guide getItemRequirements): amulet of ghostspeak and a silver sickle. The
-- rest -- pies, mirror, journal, bloom scroll, mushroom, pouch, blessed sickle -- is obtained in play.
--
-- WALLS (door rule, owner 2026-10-03/05; re-driven in b68). Every goto departs from and lands on an
-- open tile outside; every crossing is pressed by its verb on every visit (static tools, --root b63):
--   * the fixture stands in Lumbridge (3206,3233); the only walk east is through the Varrock members'
--     gate fai_varrock_member_gatel 3319,3468 (3206,3233 -> 3318,3468 REACH closed-doors len=389), by
--     pass_door, then 3321,3468 -> 3405,3506 beside the Paterdomus trapdoor (REACH len=122);
--   * the trapdoor (open, climb), the two mausoleum gates and the holy barrier (p_telejump out at
--     3423,3485, mausoleum_interactions.rs2:26-30) by their verbs; the east trapdoor pipeastsidetrapdoor
--     3422,3485 (p_telejump 3440,9887, mausoleum_interactions.rs2:50-59) by climb;
--   * Mort Myre's north gate mortmyre_metalgateclosed_l 3444,3458 (a walk-through p_teleport,
--     quest_druidspirit.rs2:13-48) by cross_gate, in and out (3423,3485 -> 3444,3460 REACH len=46;
--     3444,3458 -> 3422,3484 REACH len=48);
--   * the Grotto stands on an ISLAND (comp.py: 68 tiles, z 3331-3342) whose only way on or off is the
--     bridge druidjump_loc 'Jump' (ground decor on raw level 1 at 3440-3441 x 3329/3331,
--     quest_druidspirit.rs2:50-74). Every trip is the jump by cross_gate; on the island the player
--     walks. A failed Agility roll still lands on the far bank (north 3438,3332, south 3438,3327, 2-7
--     damage), so the verdict is the bank (far_ok), not one tile. Swamp hops to and from the south bank
--     3441,3328 are open swamp (from 3444,3457 len=186, 3440,3348 len=49, 3414,3360 len=65);
--   * the Grotto door grotto_door_druidicspirit 3440,3337 from stage 60 p_teleports to 3442,9734
--     (frame 1), and underground_rootwall_door 3442,9733 back to 3440,3337: both by climb.
return {
    id = "druidspirit",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::give amulet_of_ghostspeak 1",
        "::give silver_sickle 1",
        "::give lobster 12",
        "::give dagger_wolfbane 1",
        "::complete quest_restlessghost",
        -- ::complete also sets Priest in Peril's golden-key gate bit (bit 20 of
        -- %priestperil_mausoleum, gates.rs2:15; quest_cheat.rs2:972, seam31).
        "::complete quest_priestinperil",
        "::setlevel crafting 18",
        "::setlevel prayer 40",
        "::setlevel attack 40",
        "::setlevel strength 40",
        "::setlevel defence 30",
        "::setlevel hitpoints 45",
    },

    run = function(t)
        t.quest.bind({
            varp = "varp307_druidspirit",
            constants = {
                not_started = 0, started = 5, entered_swamp = 10, failed_talk = 15,
                spoken_filliman = 20, shown_mirror = 25, given_journal = 30, received_spell = 35,
                blessed = 40, casted_spell = 45, picked_fungi = 50, spoken_filliman2 = 55,
                performed_ritual = 60, entered_grotto = 65, full_transform = 70, blessed_sickle = 75,
                casted_sickle_bloom = 80, picked_sickle = 85, added_pouch = 90,
                killed_ghast1 = 95, killed_ghast2 = 100, killed_ghast3 = 105, complete = 110,
            },
            row = "quest_naturespirit",
            display = "Nature Spirit",
            points = 2,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        t.exec("amulet.equip", t.player.equip, "amulet_of_ghostspeak")

        -- The crossings, each one row graded on the tiles (see WALLS above).
        local function holy_barrier(step)
            t.exec(step, t.player.cross_gate, { loc = "pip_underground_wall_side_withportal",
                at = { 3440, 9886, 0 }, near = { 3440, 9887 },
                far_ok = function(tile) return tile.x == 3423 and tile.z == 3485 end,
                far_desc = "east of the Salve at 3423,3485 (mausoleum_interactions.rs2:28 p_telejump(0_53_54_31_29))" })
        end
        -- Mort Myre's north gate: in from 3444,3459 (lands 3444,3457), out from 3444,3457 (lands on the gate
        -- tile 3444,3458: [label,open_mortmyre_gate] p_teleport($door)).
        local function swamp_in(step)
            t.exec("goto-" .. step, t.player.goto_tile, 3444, 3460, 0)
            t.exec(step, t.player.cross_gate, { loc = "mortmyre_metalgateclosed_l", at = { 3444, 3458, 0 },
                near = { 3444, 3459 }, far_ok = function(tile) return tile.z <= 3457 end,
                far_desc = "inside Mort Myre, z <= 3457" })
        end
        local function swamp_out(step)
            t.exec("goto-" .. step, t.player.goto_tile, 3444, 3456, 0)
            t.exec(step, t.player.cross_gate, { loc = "mortmyre_metalgateclosed_l", at = { 3444, 3458, 0 },
                near = { 3444, 3457 }, far_ok = function(tile) return tile.z >= 3458 end,
                far_desc = "out of Mort Myre, z >= 3458" })
        end
        -- A failed jump costs 2-7 hitpoints (quest_druidspirit.rs2:70): eat a lobster between trips when low.
        local function patch_up(step)
            -- a jump that lands holds the player 3 ticks (~agility_exactmove p_delay(3)): an eat pressed
            -- inside that hold is dropped, so let it run out first
            t.ticks(3)
            local hp_result, hp = t.skill.read("hitpoints")
            local have_result, have = t.inv.count("lobster")
            if hp_result == "ok" and type(hp) == "table" and hp.level ~= nil and hp.level < 25
                and have_result == "ok" and (have or 0) > 0 then
                -- graded on the lobster leaving the pack (an eat's press can answer timeout settle_after_click)
                local eat_result, eat_detail = t.player.inv_op("lobster", 1)
                t.await({
                    level = function()
                        local r, n = t.inv.count("lobster")
                        return r == "ok" and n ~= nil and n < have
                    end,
                    note = step .. ": the lobster is eaten",
                }, 4)
                local after_result, after = t.inv.count("lobster")
                t.check(step .. ".eat", after_result == "ok" and after == have - 1,
                    "hp " .. hp.level .. " < 25: lobsters " .. have .. " -> " .. tostring(after) .. " (want one eaten); press -> "
                        .. tostring(eat_result) .. " " .. tostring(eat_detail))
            end
        end
        -- The bridge to the Grotto's island, from the south bank (3441,3328, open swamp) and back.
        local function island_in(step)
            t.exec("goto-" .. step, t.player.goto_tile, 3441, 3328, 0)
            t.exec(step, t.player.cross_gate, { loc = "druidjump_loc", at = { 3441, 3329, 0 }, loc_level = 1,
                near = { 3441, 3328 }, far_ok = function(tile) return tile.z >= 3331 and tile.z <= 3342 end,
                far_desc = "on the Grotto's island, z 3331-3342 (the jump lands 3440,3331; a failed one 3438,3332)" })
            patch_up(step)
        end
        local function island_out(step)
            t.exec(step, t.player.cross_gate, { loc = "druidjump_loc", at = { 3440, 3331, 0 }, loc_level = 1,
                near = { 3440, 3332 }, far_ok = function(tile) return tile.z <= 3329 end,
                far_desc = "on the swamp's south bank, z <= 3329 (the jump lands 3441,3329; a failed one 3438,3327)" })
            patch_up(step)
        end

        -- goDownToDrezel / talkToDrezel: the members' gate, the temple trapdoor, the two mausoleum gates, Drezel
        t.exec("goto-goDownToDrezel.varrockGate", t.player.goto_tile, 3318, 3468, 0)
        t.exec("goDownToDrezel.varrockGate", t.player.pass_door, { closed = "fai_varrock_member_gatel",
            open = "fai_varrock_member_gatel_open", at = { 3319, 3468, 0 }, near = { 3318, 3468 }, far = { 3321, 3468 } })
        t.exec("goto-goDownToDrezel", t.player.goto_tile, 3405, 3506, 0)
        t.exec("goDownToDrezel.openTrapdoor", t.player.click_loc, "trapdoor", 1, { at = { 3405, 3507, 0 } })
        t.await({
            level = function()
                return t.world.loc_near("trapdoor_open", 3, { at = { 3405, 3507, 0 } }) == "ok"
            end,
            note = "goDownToDrezel: the trapdoor opens",
        }, 6)
        local tdo_r, tdo = t.world.loc_near("trapdoor_open", 3, { at = { 3405, 3507, 0 } })
        local tdc_r = t.world.loc_near("trapdoor", 3, { at = { 3405, 3507, 0 } })
        t.check("goDownToDrezel.trapdoorOpen", tdo_r == "ok" and tdc_r ~= "ok",
            "trapdoor_open on 3405,3507,0 -> " .. tostring(tdo_r) .. " "
                .. (tdo_r == "ok" and (tdo.tile_x .. "," .. tdo.tile_z .. "," .. tdo.level) or tostring(tdo))
                .. "; closed trapdoor there -> " .. tostring(tdc_r) .. " (want the open leaf and no closed one)")
        t.exec("goDownToDrezel", t.player.climb, { loc = "trapdoor_open", op = 1, op_name = "Climb-down",
            at = { 3405, 3507, 0 }, src = { 3405, 3506 }, dest = { 3405, 9906, 0 }, slack = 1 })
        t.exec("goDownToDrezel.gate1", t.player.cross_gate, { loc = "pip_underground_door1", at = { 3405, 9895, 0 },
            near = { 3405, 9896 }, far_ok = function(tile) return tile.z > 6400 and tile.z <= 9894 end,
            far_desc = "south of the golden-key gate, z <= 9894", ticks = 30 })
        t.exec("goDownToDrezel.gate2", t.player.cross_gate, { loc = "pip_underground_door2", at = { 3431, 9897, 0 },
            near = { 3430, 9897 }, far_ok = function(tile) return tile.z > 6400 and tile.x >= 3432 end,
            far_desc = "Drezel's side of the second gate, x >= 3432", ticks = 60 })
        -- Priest in Peril's farewell advice (LostCity drezel.rs2:138-147): 60 -> 61, the barrier opens
        t.exec("talkToDrezel-advice", t.player.talk_to, "priestperiltrappedmonk2", 1)
        t.exec("talkToDrezel-advice-dialog", t.chat.play, {
            "player:So can I pass through that barrier now?",
            "npc:Ah, ",
            "npc:Morytania is an evil land",
            "npc:You should take some basic precautions",
            "npc:In many ways Werewolves",
            "npc:and it is a holy relic",
            "npc:wolf form is incredibly powerful",
            "player:Okay, I will keep it equipped",
        })
        t.exec("talkToDrezel-advice-var", t.var.await_server, "varp302_priestperil", 61, 8)
        t.exec("talkToDrezel", t.player.talk_to, "priestperiltrappedmonk2", 1)
        t.exec("talkToDrezel-dialog", t.chat.play, {
            "npc:Greetings again adventurer",
            "choose:Is there anything else interesting to do around here?",
            "player:Is there anything else interesting",
            "npc:not a great deal",
            "choose:Well, what is it, I may be able to help?",
            "player:Well, what is it",
            "npc:There's a man called Filliman",
            "choose:Who is this Filliman?",
            "player:Who is this Filliman?",
            "npc:Filliman Tarlock is his full name",
            "npc:Most people that come this way",
            "choose:Yes, I'll go and look for him.",
            "player:Yes, I'll go and look for him.",
            "npc:That's great, but it is very dangerous",
            "choose:Yes, I'm sure.",
            "player:Yes, I'm sure.",
            "npc:That's great! Many thanks!",
            "npc:Just run from them",
        })
        t.exec("talkToDrezel-pies", t.inv.await_all, { meat_pie = 3, apple_pie = 3 }, 8)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))
        t.exec("talkToDrezel-box", t.chat.expect_text, "The cleric hands you some food")
        t.exec("talkToDrezel-dialog2", t.chat.play, {
            "*",
            "npc:Please take this food to Filliman",
            "player:I'll do my very best",
        })
        t.chat.close()

        -- leaveDrezel: through the holy barrier, out east of the Salve
        holy_barrier("leaveDrezel")
        t.exec("leaveDrezel-msg", t.msg.expect, "You pass through the holy barrier")
        -- enterSwamp: the north gate of Mort Myre, 5 -> 10
        swamp_in("enterSwamp")
        t.exec("enterSwamp-msg", t.msg.expect, "gloomy atmosphere of Mort Myre", 8)
        t.expect("quest.stage.entered_swamp", t.quest.expect_stage("entered_swamp"))

        -- tryToEnterGrotto / talkToFilliman: with the amulet worn Filliman answers, 10 -> 20
        island_in("tryToEnterGrotto.bridge")
        t.exec("walk-tryToEnterGrotto", t.player.walk_to, 3440, 3334)
        t.exec("tryToEnterGrotto", t.player.click_loc, "grotto_door_druidicspirit", 1)
        t.exec("talkToFilliman", t.chat.play, {
            "mesbox:A shifting apparition appears in front of you.",
            "player:Hello?",
            "npc:Oh, I understand you!",
            "choose:How long have you been a ghost?",
            "player:How long have you been a ghost?",
            "npc:What?! Don't be preposterous",
            "player:But it's true",
            "npc:Don't be silly, I can see you",
            "choose:Ok, thanks.",
            "player:Ok, thanks.",
        })
        t.chat.close()
        t.expect("quest.stage.spoken_filliman", t.quest.expect_stage("spoken_filliman"))

        -- takeWashingBowl / takeMirror: the mirror is under the washing bowl
        t.exec("walk-takeWashingBowl", t.player.walk_to, 3437, 3336)
        local bowl_result, bowl_detail = t.player.click_obj("bowl_empty_filliman", 3)
        t.ticks(2)
        t.exec("takeWashingBowl-inv", t.inv.await, "bowl_empty_filliman", 1, 8)
        t.step("takeWashingBowl", bowl_result == "ok" and "PASS" or "FAIL", "click_obj bowl_empty_filliman -> " .. tostring(bowl_result) .. " " .. tostring(bowl_detail))
        t.exec("takeWashingBowl-msg", t.msg.expect, "small mirror under the washing bowl")
        local mirror_result, mirror_detail = t.player.click_obj("mirror", 3)
        t.ticks(1)
        t.step("takeMirror", mirror_result == "ok" and "PASS" or "FAIL", "click_obj mirror -> " .. tostring(mirror_result) .. " " .. tostring(mirror_detail))
        t.exec("takeMirror-inv", t.inv.await, "mirror", 1, 8)

        -- useMirrorOnFilliman: 20 -> 25
        t.exec("walk-useMirrorOnFilliman", t.player.walk_to, 3440, 3334)
        t.exec("useMirrorOnFilliman-door", t.player.click_loc, "grotto_door_druidicspirit", 1)
        t.chat.close()
        local fil = t.player.by_symbol("npc", "filliman_tarlock_spirit")
        t.exec("useMirrorOnFilliman", t.player.use_on, "mirror", fil)
        t.exec("useMirrorOnFilliman-dialog", t.chat.play, {
            "mesbox:You use the mirror on the spirit",
            "player:Here take a look at this",
            "mesbox:The spirit of Filliman reaches forwards",
            "npc:Well, that is the most peculiar thing",
            "npc:visage apparent",
            "player:That's because you're dead!",
            "npc:I think you might be right my friend",
            "npc:It must be a sign",
        })
        t.chat.close()
        t.expect("quest.stage.shown_mirror", t.quest.expect_stage("shown_mirror"))

        -- searchGrotto: the journal in the knot hole (the tree is 3x3 at 3439-3441,3338-3340: searched from
        -- the open tile 3438,3339 beside it), then useJournalOnFilliman: 25 -> 30 -> 35
        t.exec("walk-searchGrotto", t.player.walk_to, 3438, 3339)
        t.exec("searchGrotto", t.player.click_loc, "grotto_druidicspirit", 2)
        t.exec("searchGrotto-text", t.chat.expect_text, "Tarlock")
        t.chat.close()
        t.exec("searchGrotto-inv", t.inv.await, "filliman_journal", 1, 8)
        t.exec("walk-useJournalOnFilliman", t.player.walk_to, 3440, 3334)
        t.exec("useJournalOnFilliman-door", t.player.click_loc, "grotto_door_druidicspirit", 1)
        t.chat.close()
        fil = t.player.by_symbol("npc", "filliman_tarlock_spirit")
        t.exec("useJournalOnFilliman", t.player.use_on, "filliman_journal", fil)
        t.exec("useJournalOnFilliman-dialog", t.chat.play, {
            "mesbox:You give the journal to Filliman Tarlock.",
            "player:Here, I found this",
            "npc:My journal!",
            "mesbox:The spirit starts leafing through the journal",
            "npc:It's all coming back to me now",
        })
        t.expect("quest.stage.given_journal", t.quest.expect_stage("given_journal"))
        t.exec("useJournalOnFilliman-help", t.chat.play, {
            "choose:How can I help?",
            "player:How can I help?",
            "npc:Will you help me to become a nature spirit?",
            "player:I might be interested",
            "npc:Well, the book says",
            "player:Well, that does seem a bit vague.",
            "npc:Hmm, it does and I could understand",
            "mesbox:The druid produces a small sheet of papyrus",
            "npc:This spell needs to be cast in the swamp",
            "player:Blessed, what does that do?",
            "npc:It is required if you're to cast this druid spell",
        })
        t.chat.close()
        t.expect("quest.stage.received_spell", t.quest.expect_stage("received_spell"))
        t.exec("useJournalOnFilliman-spell", t.inv.await, "bloom_spell", 1, 8)

        -- goBackDownToDrezel / talkToDrezelForBlessing: off the island, out of the swamp's north gate, the
        -- east trapdoor, 35 -> 40
        island_out("goBackDownToDrezel.bridge")
        swamp_out("goBackDownToDrezel.swampGate")
        t.exec("goBackDownToDrezel-leaveMsg", t.msg.expect, "You skip gladly out of murky Mort Myre")
        t.exec("goto-goBackDownToDrezel", t.player.goto_tile, 3422, 3484, 0)
        t.exec("goBackDownToDrezel.openTrapdoor", t.player.click_loc, "pipeastsidetrapdoor", 1, { at = { 3422, 3485, 0 } })
        t.await({
            level = function()
                return t.world.loc_near("pipeastsidetrapdoor_open", 3, { at = { 3422, 3485, 0 } }) == "ok"
            end,
            note = "goBackDownToDrezel: the east trapdoor opens",
        }, 6)
        local edo_r = t.world.loc_near("pipeastsidetrapdoor_open", 3, { at = { 3422, 3485, 0 } })
        local edc_r = t.world.loc_near("pipeastsidetrapdoor", 3, { at = { 3422, 3485, 0 } })
        t.check("goBackDownToDrezel.trapdoorOpen", edo_r == "ok" and edc_r ~= "ok",
            "pipeastsidetrapdoor_open on 3422,3485,0 -> " .. tostring(edo_r) .. "; closed trapdoor there -> "
                .. tostring(edc_r) .. " (want the open leaf and no closed one)")
        t.exec("goBackDownToDrezel", t.player.climb, { loc = "pipeastsidetrapdoor_open", op = 1, op_name = "Climb-down",
            at = { 3422, 3485, 0 }, src = { 3422, 3484 }, dest = { 3440, 9887, 0 }, slack = 1 })
        -- the climb lands the tick the scene is rebuilt: Drezel enters the client's pool a few ticks later
        t.exec("talkToDrezelForBlessing.present", t.npc.await_present, "priestperiltrappedmonk2", 15, 12)
        t.exec("talkToDrezelForBlessing", t.player.talk_to, "priestperiltrappedmonk2", 1)
        t.exec("talkToDrezelForBlessing-dialog", t.chat.play, {
            "player:Hello again! I'm helping Filliman",
            "npc:But you haven't sneezed!",
            "player:You're so funny!",
            "player:But can you bless me?",
            "npc:Very well my friend, prepare yourself",
        })
        t.exec("talkToDrezelForBlessing-var", t.var.await_server, "varp307_druidspirit", 40, 12)
        t.exec("talkToDrezelForBlessing-dialog2", t.chat.play, {
            "npc:There you go my friend, you're now blessed",
            "player:Many thanks!",
        })
        t.chat.close()
        t.expect("quest.stage.blessed", t.quest.expect_stage("blessed"))

        -- back to the swamp the way Drezel's room lets out: the holy barrier, then the north gate
        holy_barrier("leaveDrezel2")
        swamp_in("reenterSwamp")

        -- castSpellAndGetMushroom: cast the scroll beside the rotting log, pick the fungus, 40 -> 45 -> 50
        t.exec("goto-castSpellAndGetMushroom", t.player.goto_tile, 3440, 3348, 0)
        t.ticks(2)
        t.exec("castSpellAndGetMushroom", t.player.inv_op, "bloom_spell", 1)
        t.exec("castSpellAndGetMushroom-msg", t.msg.expect, "You cast the spell in the swamp")
        t.expect("quest.stage.casted_spell", t.quest.expect_stage("casted_spell"))
        t.exec("castSpellAndGetMushroom-used", t.inv.await, "used_bloom_spell", 1, 8)
        t.ticks(3)
        t.exec("castSpellAndGetMushroom-pick", t.player.click_loc, "log_druidicspirit2", 2)
        t.exec("castSpellAndGetMushroom-inv", t.inv.await, "mortmyremushroom", 1, 8)
        t.expect("quest.stage.picked_fungi", t.quest.expect_stage("picked_fungi"))

        -- show the fungus (50 -> 55) and take a second bloom scroll
        island_in("showFungus.bridge")
        t.exec("walk-showFungus", t.player.walk_to, 3440, 3334)
        t.exec("showFungus-door", t.player.click_loc, "grotto_door_druidicspirit", 1)
        t.exec("showFungus-dialog", t.chat.play, {
            "npc:Did you manage to get something from nature?",
            "mesbox:You show the fungus to Filliman.",
            "player:Yes, I have a fungus here that I picked.",
            "npc:Wonderful, the mushroom represents",
            "choose:What are the things that are needed?",
            "player:What are the things that are needed again?",
            "npc:The three things are",
            "player:Ok, and 'something from nature'",
            "npc:Yes, that's correct",
            "player:Do you have any ideas",
            "npc:I'm sorry my friend",
            "choose:What should I do when I have those things?",
            "player:What should we do when we have those things?",
            "npc:Ah yes, I looked this up",
            "player:Can we just place the components on any rock?",
            "npc:Well, the only thing the journal says",
            "choose:Could I have another bloom scroll please?",
            "player:Could I have another bloom scroll please?",
            "npc:Sure, but please look after this one.",
            "*",
        })
        t.exec("showFungus-scroll", t.inv.await, "bloom_spell", 1, 8)
        t.chat.close()
        t.expect("quest.stage.spoken_filliman2", t.quest.expect_stage("spoken_filliman2"))

        -- useMushroom / useSpellCard: the two stones outside the grotto
        t.exec("walk-useMushroom", t.player.walk_to, 3440, 3334)
        t.exec("useMushroom", t.player.use_on, "mortmyremushroom", t.player.by_symbol("loc", "stonedisc_ds_nature"))
        t.exec("useMushroom-msg", t.msg.expect, "The stone seems to absorb the fungus.")
        t.exec("useMushroom-bit", t.var.await_server, "varp6200_druidspirit_bits", 1, 10)
        t.exec("useSpellCard", t.player.use_on, "used_bloom_spell", t.player.by_symbol("loc", "stonedisc_ds_spirit"))
        t.exec("useSpellCard-msg", t.msg.expect, "The stone seems to absorb the used spell scroll.")
        t.exec("useSpellCard-bit", t.var.await_server, "varp6200_druidspirit_bits", 3, 10)

        -- tellFillimanToCast / standOnOrange: the ritual, 55 -> 60
        -- Filliman's spirit lives 100 ticks from the grotto door that summoned him (npc_add(...,
        -- filliman_tarlock_spirit, 100), quest_druidspirit.rs2:114, LostCity "100t osrs"); the door is
        -- the summon, so open it again before the ritual rather than count on the mirror-leg copy
        -- (seam32: takeMirror went 26 -> 1 tick and that copy expired at tick ~190, one row short).
        t.exec("tellFillimanToCast-door", t.player.click_loc, "grotto_door_druidicspirit", 1)
        t.chat.close()
        t.exec("walk-standOnOrange", t.player.walk_to, 3440, 3335)
        t.ticks(2)
        local tile_result, tile = t.world.tile()
        t.check("standOnOrange", tile_result == "ok" and tile.x == 3440 and tile.z == 3335, "standing on the orange stone at " .. tostring(tile and tile.x) .. "," .. tostring(tile and tile.z))
        t.exec("tellFillimanToCast", t.player.talk_to, "filliman_tarlock_spirit", 1)
        t.exec("tellFillimanToCast-dialog", t.chat.play, {
            "npc:Hello again! I don't suppose you've found out",
            "choose:I think I've solved the puzzle!",
            "player:I think I've solved the puzzle!",
            "npc:Oh really.. Have you placed all the items",
            "end",
        })
        t.expect("tellFillimanToCast-reopen", t.await({
            level = function() return t.chat.kind() == "npc" end,
            note = "ritual reopen",
        }, 15))
        t.exec("tellFillimanToCast-dialog2", t.chat.play, {
            "npc:Aha, everything seems to be in place!",
        })
        t.chat.close()
        t.expect("quest.stage.performed_ritual", t.quest.expect_stage("performed_ritual"))

        -- enterGrotto: 60 -> 65 (the door p_teleports into the grotto, 3442,9734 in frame 1)
        t.exec("enterGrotto", t.player.climb, { loc = "grotto_door_druidicspirit", op = 1, op_name = "Enter",
            at = { 3440, 3337, 0 }, dest = { 3442, 9734, 0 } })
        t.ticks(1)
        t.expect("quest.stage.entered_grotto", t.quest.expect_stage("entered_grotto"))

        -- talkToFillimanInGrotto: the altar, Filliman transforms, 65 -> 70
        t.exec("talkToFillimanInGrotto", t.player.click_loc, "druidic_spirit_grotto", 1)
        t.exec("talkToFillimanInGrotto-dialog", t.chat.play, {
            "npc:Well, hello there again, I was just enjoying the grotto.",
            "npc:I must complete the transformation now.",
            "end",
        })
        t.expect("talkToFillimanInGrotto-reopen", t.await({
            level = function() return t.chat.kind() == "npc" end,
            note = "transform reopen",
        }, 25))
        t.exec("talkToFillimanInGrotto-dialog2", t.chat.play, {
            "npc:Hmmm, good, the transformation is complete.",
            "player:A silver sickle? What's that?",
            "npc:The sickle is the symbol and weapon of the Druid",
            "choose:Where would I get a silver sickle?",
            "player:Where would I get a silver sickle?",
            "npc:You could make one yourself",
            "choose:What will you do to the silver sickle?",
            "player:What will you do to the silver sickle?",
            "npc:Why, I will give it my blessings",
            "choose:How can a blessed sickle help me to defeat the Ghasts?",
            "player:How can a blessed sickle help me to defeat the Ghasts?",
            "npc:My blessings will entice nature to bloom",
            "choose:Ok, thanks.",
            "player:Ok thanks.",
        })
        t.chat.close()
        t.expect("quest.stage.full_transform", t.quest.expect_stage("full_transform"))

        -- searchAltar / blessSickle: hand the silver sickle in, 70 -> 75
        t.exec("searchAltar", t.player.click_loc, "druidic_spirit_grotto", 1)
        t.exec("blessSickle", t.chat.play, {
            "npc:Have you brought me the silver sickle?",
            "player:Yes, here it is.",
            "npc:My friend, I will bless it for you",
            "end",
        })
        t.expect("blessSickle-reopen", t.await({
            level = function() return t.chat.kind() == "mesbox" end,
            note = "sickle reopen",
        }, 20))
        t.exec("blessSickle-dialog2", t.chat.play, {
            "mesbox:Your sickle has been blessed!",
            "npc:Now you can go forth and make the swamp bloom.",
            "npc:Before I can make this grotto into an Altar of Nature",
            "mesbox:The nature spirit gives you an empty pouch.",
            "npc:You'll need this in order to collect together",
        })
        t.chat.close()
        t.expect("quest.stage.blessed_sickle", t.quest.expect_stage("blessed_sickle"))
        t.exec("blessSickle-sickle", t.inv.await, "silver_sickle_blessed", 1, 8)
        t.exec("blessSickle-pouch", t.inv.await, "druid_pouch_empty", 1, 8)

        -- the empty pouch before the sickle has bloomed anything (quest_druidspirit.rs2:333), asked
        -- inside the grotto: out in the swamp a ghast's swing closes the mesbox a tick later
        t.exec("fillPouches-early", t.player.inv_op, "druid_pouch_empty", 1)
        t.exec("fillPouches-early-text", t.chat.play, { "mesbox:You've not been told how to use this item yet." })
        t.chat.close()

        -- fillPouches: out of the grotto, off the island, bloom the swamp with the blessed sickle, pick
        -- three, fill the pouch, 75 -> 90
        t.exec("leaveGrotto", t.player.climb, { loc = "underground_rootwall_door", op = 1, op_name = "Exit",
            at = { 3442, 9733, 0 }, dest = { 3440, 3337, 0 } })
        island_out("fillPouches.bridge")
        t.exec("goto-fillPouches", t.player.goto_tile, 3414, 3360, 0)
        t.ticks(2)
        local casts = 0
        local total = 0
        while total < 3 and casts < 10 do
            casts = casts + 1
            t.player.inv_op("silver_sickle_blessed", 3)
            t.ticks(2)
            local names = { "log_druidicspirit2", "branch_druidicspirit2", "peartree_druidicspirit2" }
            for i = 1, 3 do
                if total < 3 then
                    t.player.click_loc(names[i], 2)
                    t.ticks(1)
                end
                local _, a = t.inv.count("mortmyremushroom")
                local _, b = t.inv.count("mortmyrebuddingstem")
                local _, c = t.inv.count("mortmyrepear")
                total = (a or 0) + (b or 0) + (c or 0)
            end
            t.ticks(1)
        end
        t.check("fillPouches-harvest", total >= 3, "harvested " .. tostring(total) .. " after " .. casts .. " cast(s)")
        t.expect("quest.stage.picked_sickle", t.quest.expect_stage("picked_sickle"))
        t.exec("fillPouches", t.player.inv_op, "druid_pouch_empty", 1)
        t.exec("fillPouches-msg", t.msg.expect, "natures harvests to your druid pouch")
        t.exec("fillPouches-inv", t.inv.await, "druid_pouch", 1, 8)
        t.expect("quest.stage.added_pouch", t.quest.expect_stage("added_pouch"))

        -- killGhasts: three real kills, 90 -> 105
        t.exec("killGhasts-equip", t.player.equip, "silver_sickle_blessed")
        t.exec("goto-killGhasts", t.player.goto_tile, 3414, 3360, 0)
        t.ticks(2)
        local stages = { "killed_ghast1", "killed_ghast2", "killed_ghast3" }
        for k = 1, 3 do
            local _, pouch = t.inv.count("druid_pouch")
            if (pouch or 0) == 0 then
                t.exec("killGhasts-unequip" .. k, t.player.inv_op, "silver_sickle_blessed", 1)
                local refill = 0
                local got = 0
                while got < 3 and refill < 10 do
                    refill = refill + 1
                    t.player.inv_op("silver_sickle_blessed", 3)
                    t.ticks(2)
                    local names = { "log_druidicspirit2", "branch_druidicspirit2", "peartree_druidicspirit2" }
                    for i = 1, 3 do
                        if got < 3 then
                            t.player.click_loc(names[i], 2)
                            t.ticks(1)
                        end
                        local _, a = t.inv.count("mortmyremushroom")
                        local _, b = t.inv.count("mortmyrebuddingstem")
                        local _, c = t.inv.count("mortmyrepear")
                        got = (a or 0) + (b or 0) + (c or 0)
                    end
                    t.ticks(1)
                end
                t.exec("killGhasts-refill" .. k, t.player.inv_op, "druid_pouch_empty", 1)
                t.exec("killGhasts-reequip" .. k, t.player.equip, "silver_sickle_blessed")
            end
            t.exec("killGhasts-visible" .. k, t.npc.await_present, "ghast_vis", 12, 90)
            -- Single-way combat: a second revealed ghast swinging at the player renews the player's
            -- claim, and every press on another copy answers "I'm already under attack." (engine
            -- %lastcombat+8, as LostCity; seam32 sourced_carry_overs traced it). So a refused press
            -- moves on to a copy not yet pressed -- the one fighting the player is among them.
            local attack_result, attack_detail
            local pressed_slots = {}
            for try = 1, 8 do
                local pick = nil
                if try > 1 then
                    local _, _, copies = t.npc.tiles("ghast_vis", 12)
                    for _, row in ipairs(copies or {}) do
                        if not pressed_slots[row.slot] then
                            pick = { slot = row.slot }
                            break
                        end
                    end
                    if pick == nil then
                        pressed_slots = {}
                    end
                end
                attack_result, attack_detail = t.player.attack("ghast_vis", 2, 15, pick)
                if attack_result == "ok" then break end
                local pressed = string.match(tostring(attack_detail), "pressed slot (%d+)")
                if pressed then
                    pressed_slots[tonumber(pressed)] = true
                end
                t.ticks(4)
            end
            t.check("killGhasts-attack" .. k, attack_result == "ok", tostring(attack_result) .. ": " .. tostring(attack_detail))
            local dead_result, dead_detail = t.npc.await_dead_engaged(300, 4, { eat = { item = "lobster", below = 20 } })
            t.step("killGhasts-dead" .. k, dead_result == "ok" and "PASS" or "FAIL", tostring(dead_result) .. " " .. tostring(dead_detail))
            -- the fight's margin: lowest hp at least a quarter of the maximum AND food left
            local lowest = tonumber(tostring(dead_detail):match("lowest hp (%d+)/") or "")
            local _, hitpoints = t.skill.read("hitpoints")
            local max_hp = type(hitpoints) == "table" and hitpoints.base_level or nil
            local food_result, food_left = t.inv.count("lobster")
            t.check("killGhasts-dead" .. k .. ".margin", lowest ~= nil and max_hp ~= nil and food_result == "ok"
                and lowest * 4 >= max_hp and (food_left or 0) >= 1,
                "lowest hp " .. tostring(lowest) .. "/" .. tostring(max_hp) .. ", lobsters left " .. tostring(food_left)
                    .. " of 12 staged (margin: lowest hp >= a quarter of max AND at least one lobster left)")
            t.ticks(4)
            t.expect("quest.stage." .. stages[k], t.quest.expect_stage(stages[k]))
            t.ticks(8)
        end

        -- enterGrottoAgain / talkToNatureSpiritToFinish: back over the bridge, into the grotto. The xp
        -- snapshot is taken on the island, after the walk through the swamp (a ghast met on the way
        -- would add combat xp of its own).
        island_in("enterGrottoAgain.bridge")
        t.exec("walk-enterGrottoAgain", t.player.walk_to, 3440, 3334)
        local snap_result, snapshot = t.skill.snapshot()
        t.step("reward.snapshot", snap_result == "ok" and "PASS" or "FAIL", "skill.snapshot before the hand-in -> " .. tostring(snap_result))
        t.exec("enterGrottoAgain", t.player.climb, { loc = "grotto_door_druidicspirit", op = 1, op_name = "Enter",
            at = { 3440, 3337, 0 }, dest = { 3442, 9734, 0 } })
        t.exec("talkToNatureSpiritToFinish", t.player.click_loc, "druidic_spirit_grotto", 1)
        t.exec("talkToNatureSpiritToFinish-dialog", t.chat.play, {
            "npc:Hello again my friend, have you defeated three Ghasts",
            "player:Yes, I've killed all three",
            "npc:Many thanks my friend, you have completed your quest!",
            "end",
        })
        t.var.await_server("varp307_druidspirit", 110, 40)
        t.await({ level = function() return t.chat.kind() == "npc" end, note = "farewell chat" }, 60)
        t.exec("talkToNatureSpiritToFinish-farewell", t.chat.play, {
            "npc:Welcome to my Altar to Nature! Farewell my friend, and keep those Ghasts at bay!",
            "end",
        })
        t.ticks(3)
        t.quest.expect_complete()
        local cr = t.skill.expect_gain("crafting", 3000, snapshot)
        t.check("reward.crafting", cr == "ok", "crafting +3000 xp -> " .. tostring(cr))
        local dr = t.skill.expect_gain("defence", 2000, snapshot)
        t.check("reward.defence", dr == "ok", "defence +2000 xp -> " .. tostring(dr))
        local hr = t.skill.expect_gain("hitpoints", 2000, snapshot)
        t.check("reward.hitpoints", hr == "ok", "hitpoints +2000 xp -> " .. tostring(hr))
        t.finish(0)
    end,
}
