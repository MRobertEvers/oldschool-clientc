-- Shades of Mort'ton (quest_mortton). Hand-authored from the scaffold; see
-- QUEUE.tsv's last_failure for the prior rejections this resumes from.
--
-- RETRY after 1858fe69a (queue.py show mortton): the shade hunt was losing
-- to four AGGRESSIVE Afflicted types (mort_afflicted_man/_man2/_woman/
-- _woman2) wandering Mort'ton's SINGLE-WAY street -- every Attack on a Loar
-- Shadow refused "I'm already under attack." because an Afflicted had
-- claimed the player. Fixed with four ::passive setup lines
-- (docs/QUEST_SERVER_CHEATS.md section F). The shop blocker from the prior
-- rejection is gone too: t.shop.* landed, so Razmire's builders' store
-- (razmirebuildingstore, op 4) is reachable for real and this file now
-- drives the whole rest of the quest instead of stopping at the temple
-- wall's "not enough resources" mesbox.
--
-- Coins are a bring-along prerequisite for the shop (Quest Helper's own
-- buyTimberLimeAndSwamp step) -- setup has none without an explicit
-- ::give. Razmire's and Ulsquire's cure from Serum 207 lasts only 200
-- ticks (npc_changetype(..., 200) in both razmire_keelgan.rs2 and
-- ulsquire_shauncy.rs2) and the shade hunt alone runs past that, so every
-- return visit re-checks whether the npc is still the afflicted type and,
-- if so, mixes a fresh dose and re-cures before talking -- setup carries
-- five ashes/tarrominvial pairs for that (one mix always yields dose_count
-- 3 on a freshly-named "mort_serum3", so a fresh mix immediately before
-- each cure sidesteps tracking which partially-used mort_serumN survives).
--
-- The temple wall (flamtaer_temple.rs2's [oploc3,_temple_wall]) and the
-- fire altar/funeral pyre (mortton_pyre.rs2's [oploc4,_pyre_remains_loaded]
-- try_light_altar/light_funeral_pyre) are both SELF-RE-ARMING interactions
-- -- every successful roll ends with its own p_oploc(3)/p_oploc(4), the
-- same idiom player.attack's auto-swing uses -- so one press each drives
-- repeated server-side rolls on its own; this file presses once and polls
-- quest.stage() rather than re-pressing per round.

return {
    id = "mortton",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so requirements fit
        "::give tarrominvial 5", -- Quest Helper prerequisite -- one pair per cure (initial x2, re-cures up to x3)
        "::give tinderbox 1", -- Quest Helper prerequisite
        "::give logs 1", -- Quest Helper prerequisite
        "::give ashes 5", -- Quest Helper prerequisite -- paired with tarrominvial above
        "::give hammer 1", -- Quest Helper prerequisite -- needed at the temple wall (trap 16: a tool, not the quest's own deliverable)
        "::give rune_scimitar 1", -- combat gear prerequisite for the five shades (trap 16), same idiom as hunt.lua
        "::give shark 5", -- food prerequisite (trap 26/section 8's player.attack note): "carry food and WEAR the weapon you were given"
        "::give coins 5000", -- bring-along prerequisite for Razmire's builders' store (t.shop.buy needs coins in setup)
        -- Crafting 20 is the quest's OWN requirement (flamtaer_temple.rs2's
        -- ::mortton_repairtemple guard: "if (stat(crafting) < 20)"), not 99:
        -- ~mortton_temple_build_step spends resource POOL per repair scaled
        -- by crafting level (measured ~9 pool/repair at 20, ~31 at 99), so a
        -- higher level than the quest asks for makes the wall repair burn
        -- through Razmire's 5-per-restock materials three times as fast for
        -- no benefit -- keep it at what the quest itself requires.
        "::setlevel crafting 20",
        "::setlevel herblore 15",
        "::setlevel firemaking 99", -- prerequisite gearing for the fire altar and funeral pyre stat_random rolls
        -- Loar shades (level 40, mortton_shades.npc: attack 45 / defence 26 /
        -- strength 30 / hitpoints 38) killed an earlier probe at fresh-
        -- character combat stats -- level up before ever entering the town,
        -- same as the rune scimitar and food above (prerequisite gear, not
        -- the quest's own work).
        "::setlevel hitpoints 99",
        "::setlevel defence 40",
        "::setlevel attack 40",
        "::setlevel strength 40",
        "::complete quest_priestperil", -- Shades of Mort'ton's own prerequisite quest
        -- Herblore itself is locked behind Druidic Ritual (brew_potion.rs2:
        -- 513's ~herblore_unlocked, quest_druid.rs2:33) regardless of level.
        "::complete quest_druidicritual",
        -- Mort'ton's shade street is SINGLE-WAY and four Afflicted types
        -- wander it aggressively (docs/QUEST_SERVER_CHEATS.md section F):
        -- one that aggresses claims the player for
        -- TORIRSSERVER_SINGLEWAY_COMBAT_TICKS past every swing, and every
        -- Attack on a Loar Shadow inside that window is refused "I'm
        -- already under attack." -- measured 20 rounds for 2 of 5 kills
        -- without this, 7 rounds for 5 of 5 with it.
        "::passive mort_afflicted_man",
        "::passive mort_afflicted_man2",
        "::passive mort_afflicted_woman",
        "::passive mort_afflicted_woman2",
        -- shadeshadow_level1 ITSELF is huntmode=aggressive, huntrange 3
        -- (mortton_shades.npc), 31 copies packed along this same street --
        -- runs 2-4 all measured EVERY round's own t.player.attack refused
        -- "I'm already under attack." from the very first press (no prior
        -- engagement to abandon), so something is claiming the player before
        -- our own Attack registers even with the four Afflicted held. Hold
        -- this type passive too: it is still attackable, still takes real
        -- damage from our own t.player.attack presses, still dies for real
        -- and still drops shade_bones1 (docs/QUEST_SERVER_CHEATS.md section
        -- F) -- only its own unprompted aggression/retaliation is removed,
        -- the same driver affordance as the four Afflicted lines above, not
        -- a shortcut around the kill itself.
        "::passive shadeshadow_level1",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "morttonquest",
            constants = {
                complete = 85,
                mortton_can_light_altar = 60,
                mortton_created_pyre_logs = 70,
                mortton_created_sacred_oil = 65,
                mortton_gave_dairy_to_apothecary = 30,
                mortton_kill_shades = 15,
                mortton_killed_1_shade = 20,
                mortton_killed_2_shades = 25,
                mortton_killed_3_shades = 30,
                mortton_killed_4_shades = 35,
                mortton_killed_5_shades = 40,
                mortton_lit_pyre = 80,
                mortton_logs_on_pyre = 75,
                mortton_made_serum = 10,
                mortton_not_started = 0,
                mortton_quest_complete = 85,
                mortton_read_diary = 5,
                mortton_rebuild_temple = 55,
                mortton_received_blood_diary = 10,
                mortton_received_swamp_diary = 9,
                mortton_shades_to_razmire = 45,
                mortton_shades_to_ulsquire = 47,
                mortton_temple_fullpool_warning = 31,
                mortton_ulsquire_temple = 50,
                mortton_unlocked_shade_lair = 29,
                mortton_used_serum_on_razmire = 2,
                mortton_used_serum_on_ulsquire = 0,
                not_started = 0,
                player_made_perm_serum = 7,
                razmire_perm_serum_used = 6,
                razmire_visible = 3,
                shadeattack = 4,
                shades_table_searched = 8,
                shades_ulsquire_oil_given = 28,
                ulsquire_perm_serum_used = 5,
                ulsquire_visible = 1,
            },
            row = "quest_shadesofmortton",
            display = "Shades of Mort'ton",
            points = 3,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3) -- setup cheats' effects are not client-side yet

        t.exec("quest.stage.mortton_not_started", t.quest.expect_stage, "mortton_not_started")

        -- DIAGNOSTIC (run 3): the shade hunt's own screenshot (42-shade.hunt-FAIL.png,
        -- run 2) showed "I'm already under attack." refusing every Attack press,
        -- exactly the single-way symptom the four setup ::passive lines are
        -- supposed to prevent -- but client.log carries no chat/say() text at
        -- all (grepped: neither that line nor any "is passive"/"Passive:"
        -- confirmation appears there), so there is no way to tell from the
        -- log alone whether setup's four ::passive cheats actually registered.
        -- Read the held list back directly.
        -- t.cheat's own detail is hollow here (api_drive.cheat's synchronous
        -- reply, not the async chat line) -- REVERTED last_failure (sampler
        -- sonnet-b13): this row PASSed as "ok (nil)" without ever reading
        -- whether the five setup ::passive lines actually registered (trap
        -- 12). Bare "::passive" prints one "Passive: <name> (<id>)." line
        -- PER held type -- read the ring back and require all five.
        local passive_list_result = t.cheat("::passive")
        local passive_msgs_result, passive_msgs = t.msg.last(8)
        local passive_lines = {}
        if passive_msgs_result == "ok" then
            for i = 1, #passive_msgs do
                if string.find(passive_msgs[i].text, "Passive:", 1, true) then
                    table.insert(passive_lines, passive_msgs[i].text)
                end
            end
        end
        t.check("diagnostic.passiveListHeld",
            passive_list_result == "ok" and #passive_lines == 5,
            "t.cheat(::passive) -> " .. tostring(passive_list_result) .. "; held (" .. tostring(#passive_lines)
                .. "/5 expected -- mort_afflicted_man/_man2/_woman/_woman2/shadeshadow_level1): "
                .. table.concat(passive_lines, " | "))

        -- Search the experiment shelf (all.loc:35647-35662, op1=Search) in
        -- the south building of Mort'ton for Herbi Flax's diary. click_loc
        -- steps off the loc's own tile and walks its far sides before
        -- projecting, so the real op-1 click lands.
        t.exec("goto-searchShelf", t.player.goto_tile, 3481, 3279, 0)
        local shelf_click_result, shelf_click_detail = t.player.click_loc("shades_experimentshelf", 1)
        -- t.settle() does not cover an inventory sync racing the mesbox's
        -- own inv_add (docs/QUEST_AUTHORING.md section 8) -- poll instead.
        local serum_book_result, serum_book_count = t.inv.await("serum_book", 1, 10)
        t.check("searchShelf", shelf_click_result == "ok" and serum_book_result == "ok",
            "click_loc(shades_experimentshelf, op1) -> " .. tostring(shelf_click_result) .. " "
                .. tostring(shelf_click_detail) .. "; inv.await(serum_book, 1) -> "
                .. tostring(serum_book_result) .. " (" .. tostring(serum_book_count) .. ")")
        t.expect("haveSerumBook", t.inv.expect_has("serum_book", 1))
        -- The shelf's own mesbox ("You find an interesting looking book on
        -- the shelf.") is still open -- close it before reading the diary.
        t.exec("searchShelf-dismiss", t.chat.play, {"mesbox:You find an interesting looking book"})

        -- Read the diary (serum_book.rs2:8, [opheld1,serum_book] -- a
        -- numbered held op, op 1). It plays through 25 mesbox pages; the
        -- quest varp write lands after page 20 is dismissed, so the pages
        -- have to actually be clicked through, not skipped.
        t.exec("readDiary", t.player.inv_op, "serum_book", 1)
        t.exec("readDiary-drain", t.chat.drain, {})
        t.exec("quest.stage.mortton_read_diary", t.quest.expect_stage, "mortton_read_diary")

        -- Mix Serum 207 for Razmire's FIRST cure: ashes used ON the
        -- tarrominvial (OPHELDU), brew_potion.rs2:14-18 ->
        -- quest_mortton.rs2:103-111's ~mortton_mix_serum ->
        -- brew_potion.rs2:503-548's ~attempt_brew_potion. A fresh mix always
        -- comes out named "mort_serum3" at dose_count 3
        -- (quest_mortton.obj), so every cure below mixes its OWN fresh pair
        -- right before use rather than tracking a partially-drunk dose
        -- under a different name (mort_serum2/mort_serum1).
        t.exec("mixSerum207-1", t.player.use_item_on_item, "ashes", "tarrominvial")
        local stage_after_mix1_result, stage_after_mix1_value = t.quest.stage()
        t.check("mixSerum207.stageAdvanced", stage_after_mix1_result == "ok" and stage_after_mix1_value == 10,
            "quest.stage() -> " .. tostring(stage_after_mix1_result) .. " (" .. tostring(stage_after_mix1_value)
                .. ") -- mortton_made_serum(10) after mixSerum207-1's OPHELDU click, the fixed behaviour")
        t.expect("mixSerum207.firstDose", t.inv.expect_has("mort_serum3", 1))
        t.exec("quest.stage.mortton_made_serum", t.quest.expect_stage, "mortton_made_serum")

        -- Gear up for real -- ::give put the scimitar in the backpack, it
        -- did not wear it (section 8's player.attack note).
        t.exec("gearUp.scimitar", t.player.equip, "rune_scimitar")

        -- Use Serum 207 on the afflicted Razmire Keelgan (spawn row
        -- m54_51.spawn:35, 3489,3296,0) -- razmire_keelgan.rs2's
        -- razmire_use_item label (oc_category 108, quest_mortton.obj's
        -- serum_207 category) temporarily cures him and opens razmire_talk.
        t.exec("goto-razmire", t.player.goto_tile, 3489, 3296, 0)
        local razmire_afflicted, razmire_afflicted_status = t.player.by_symbol("npc", "razmire_keelgan_afflicted")
        t.check("razmire.find", razmire_afflicted_status == "ok",
            "player.by_symbol(npc, razmire_keelgan_afflicted) -> " .. tostring(razmire_afflicted_status))
        t.exec("razmire.cure", t.player.use_on, "mort_serum3", razmire_afflicted)
        -- razmire_talk (first visit): "Ah, excellent..." -> since
        -- %morttonquest < mortton_kill_shades -> razmire_questions ->
        -- choose "What are all these shadow creatures?" -> razmire_creatures
        -- -> "Yes, I'll dispatch..." sets %morttonquest = mortton_kill_shades.
        -- Chat text copied verbatim from razmire_keelgan.rs2 (row text for
        -- choose:, the ECHOED player line differs by one letter -- "shadow"
        -- vs "shadowy" -- and is spelled exactly as the .rs2 has it).
        t.exec("razmire.acceptKillShades", t.chat.play, {
            "npc:you've made your own serum",
            "choose:What are all these shadow creatures?",
            "player:What are all these shadowy creatures?",
            "npc:Those disgusting entities are the filth",
            "npc:I've heard that they jealously guard",
            "choose:Yes, I'll dispatch those dark and evil creatures.",
            "player:Yes, I'll dispatch those dark and evil creatures.",
            "npc:Great, that's what I wanted to hear",
        })
        t.exec("quest.stage.mortton_kill_shades", t.quest.expect_stage, "mortton_kill_shades")

        -- Mix Serum 207 for Ulsquire's FIRST cure now, while the two are
        -- close together and before the hunt eats the backpack -- one fresh
        -- pair, used on him further down.
        t.exec("mixSerum207-2", t.player.use_item_on_item, "ashes", "tarrominvial")
        t.expect("mixSerum207.secondDose", t.inv.expect_has("mort_serum3", 1))

        -- Kill five Loar shades (mortton_shades.npc's shadeshadow_level1:
        -- hitpoints 38, attack 45, defence 26 -- well within the setup's
        -- 40/40/40/99 combat stats) and loot their remains
        -- (mortton_shades.npc's death_drop=shade_bones1).
        --
        -- t.player.attack takes the npc SYMBOL directly (combat.lua's
        -- QD.player.attack resolves it itself through by_symbol/
        -- npc.nearest) -- not a resolved {kind,id} table the way click_loc/
        -- use_on's target argument works; handing it one raised `bad
        -- argument #2 to 'symbol' (string expected, got table)` on the
        -- first attempt (see queue history).
        --
        -- The first real attempt filled the chat ring edge-to-edge with
        -- "I can't reach that!" on every single one of 41 Attack presses --
        -- not an accuracy roll at all. Razmire's afflicted spawn (and the
        -- cure) sit on his shop's raised porch behind a picket fence
        -- (shade.attack-1.png/44-blocked.png); t.player.walk_near answered
        -- "within 2" from there because it is a bare tile-distance check,
        -- not a path/line-of-sight one, so it never caught the fence. Fixed
        -- with an explicit goto_tile off the porch, to shadeshadow_level1's
        -- own spawn tile (m54_51.spawn, clear street).
        t.exec("goto-shadehunt", t.player.goto_tile, 3488, 3288, 0)

        -- Real combat off the porch still lands hits at a low rate (one
        -- probe kill needed 51+40 real ticks before a still-live read), so
        -- this is a genuine retry loop, not five independent one-shot
        -- fights -- section 8's rule: "a retry loop that writes a t.step
        -- row per ATTEMPT fails the every-row-PASS rule over attempts that
        -- were only a walk... record the loop's OUTCOME row only." Each
        -- round re-finds the nearest live shadow (a prior round's target may
        -- already be dead or may have moved), presses Attack, gives the
        -- fight a real window to land hits, and picks up whatever remains
        -- are on the ground; the loop's own exit condition is shade_bones1
        -- actually reaching 5 in the backpack, read back after every round.
        -- Eat periodically (section 8's player.attack note: "carry food and
        -- EAT IT") -- a defensive top-off, not a reaction to a specific low
        -- reading, since t.skill.read's reading shape is not spelled out in
        -- docs/QUEST_AUTHORING.md's verb table.
        -- shadeshadow_level1 is ITSELF huntmode=aggressive, huntrange 3
        -- (mortton_shades.npc) -- 31 copies packed along this street, so
        -- several aggro at once regardless of the four ::passive Afflicted
        -- types. A loop that re-resolves-and-attacks fresh every round can
        -- abandon a shade it is still mid-fight with (docs/QUEST_AUTHORING.md
        -- section 3's npc.await_dead_engaged note: "a loop that re-resolves
        -- its symbol each attempt abandons the half-killed npc it already
        -- engaged") -- run 2/3 measured exactly that signature (0 kills in 20
        -- rounds, "I'm already under attack." on repeat, 42-shade.hunt-FAIL.png)
        -- because a fresh attack() call every round can pick a DIFFERENT
        -- nearest copy than the one still holding the fight claim on the
        -- player. Track engagement across rounds instead: only resolve+press
        -- a NEW target when not currently engaged, and on a timeout keep
        -- polling await_dead_engaged (it re-issues Attack on the same slot)
        -- rather than re-resolving.
        local shade_rounds = 0
        local shade_bones_count = 0
        local shade_engaged = false
        while shade_bones_count < 5 and shade_rounds < 30 do
            shade_rounds = shade_rounds + 1
            if not shade_engaged then
                local shade_target, shade_target_status = t.player.by_symbol("npc", "shadeshadow_level1")
                t.note("round " .. tostring(shade_rounds) .. " by_symbol: " .. tostring(shade_target_status))
                if shade_target_status == "ok" then
                    t.player.walk_near(shade_target, 20, 1)
                    local attack_result, attack_detail = t.player.attack("shadeshadow_level1", 2, 30)
                    t.note("round " .. tostring(shade_rounds) .. " attack: " .. tostring(attack_result)
                        .. " (" .. tostring(attack_detail) .. ")")
                    shade_engaged = true
                end
            end
            if shade_engaged then
                local dead_result, dead_detail = t.npc.await_dead_engaged(80, 15)
                t.note("round " .. tostring(shade_rounds) .. " await_dead_engaged: " .. tostring(dead_result)
                    .. " (" .. tostring(dead_detail) .. ")")
                if dead_result == "ok" then
                    shade_engaged = false
                    t.player.click_obj("shade_bones1", 3)
                elseif dead_result == "no_row" then
                    -- the stamped engagement is gone (the attack above never
                    -- really landed) -- let the next round resolve fresh.
                    shade_engaged = false
                end
                -- on timeout, shade_engaged stays true and the next round
                -- polls await_dead_engaged again on the SAME slot instead of
                -- abandoning it for a freshly-resolved target.
            end
            if shade_rounds % 5 == 0 then
                t.player.inv_op("shark", 1)
            end
            local bones_result, bones_value = t.inv.count("shade_bones1")
            if bones_result == "ok" and type(bones_value) == "number" then
                shade_bones_count = bones_value
            end
        end
        t.check("shade.hunt", shade_bones_count >= 5,
            "hunted " .. tostring(shade_rounds) .. " round(s) off " .. "3488,3288,0"
                .. ", shade_bones1 in backpack: " .. tostring(shade_bones_count))
        t.expect("player.aliveAfterShades", t.player.alive())
        t.exec("quest.stage.mortton_killed_5_shades", t.quest.expect_stage, "mortton_killed_5_shades")
        t.expect("shade.remainsCollected", t.inv.expect_has("shade_bones1", 5))

        -- Hand two remains to Razmire (razmire_talk's killed_5_shades
        -- branch, razmire_keelgan.rs2:99-112). His cure from mixSerum207-1
        -- lasts only 200 ticks and the hunt above almost certainly ran past
        -- that (npc_changetype(razmire_keelgan_afflicted,200) reverts him
        -- and clears the razmire_visible bit on the same timer) -- check
        -- which type is actually live and, if he has reverted, mix a fresh
        -- dose and re-cure through the afflicted symbol instead of talking
        -- to a base symbol that is not the one spawned right now.
        t.exec("goto-razmire-2", t.player.goto_tile, 3489, 3296, 0)
        local razmire_afflicted_now, razmire_afflicted_now_status =
            t.player.by_symbol("npc", "razmire_keelgan_afflicted")
        if razmire_afflicted_now_status == "ok" then
            t.exec("mixSerum207-3", t.player.use_item_on_item, "ashes", "tarrominvial")
            t.exec("razmire.recure", t.player.use_on, "mort_serum3", razmire_afflicted_now)
        else
            t.exec("razmire.giveRemains", t.player.talk_to, "razmire_keelgan", 1)
        end
        -- razmire_talk's opening line is now ALWAYS "Ah, it's you, how's it
        -- going?" (testbit(mortton_used_serum_on_razmire) was set true on
        -- the very first cure above and never clears), whichever of the two
        -- branches just opened the dialogue -- then the killed_5_shades body
        -- below it. Decline the store offer here with "Ok, thanks." and open
        -- the builders' store separately through t.shop.open (a direct
        -- numbered press, not a menu choice).
        t.exec("razmire.giveRemains-dialogue", t.chat.play, {
            "npc:it's you",
            "npc:have you killed the five shades yet",
            "player:Yes, I have actually!",
            "mesbox:Razmire takes two Shade remains.",
            "npc:I'll experiment on these",
            "npc:Now that you've slayed",
            "choose:Ok, thanks.",
            "player:Ok, thanks",
        })
        t.exec("quest.stage.mortton_shades_to_razmire", t.quest.expect_stage, "mortton_shades_to_razmire")

        -- Free space before shopping, keeping ONE ashes/tarrominvial pair in
        -- reserve for a later re-cure (mixSerum207-4 and on) -- run 5's own
        -- 59-razmire.buyLimestone.png showed the backpack already full of
        -- the leftover pairs plus five shade_bones1/timberbeam/limestonebrick
        -- (none of limestonebrick/timberbeam/swamppaste/ashes/tarrominvial
        -- stack -- each unit is its own slot), and swamppaste then answered
        -- "You don't have enough inventory space." for every one of the 25
        -- asked.
        local ashes_before_drop_result, ashes_before_drop_count = t.inv.count("ashes")
        if ashes_before_drop_result == "ok" and type(ashes_before_drop_count) == "number"
            and ashes_before_drop_count > 1 then
            t.player.drop("ashes")
        end
        local tarrominvial_before_drop_result, tarrominvial_before_drop_count = t.inv.count("tarrominvial")
        if tarrominvial_before_drop_result == "ok" and type(tarrominvial_before_drop_count) == "number"
            and tarrominvial_before_drop_count > 1 then
            t.player.drop("tarrominvial")
        end

        -- Buy the temple's building materials for real: t.shop.* landed
        -- 2026-09-22 (docs/QUEST_AUTHORING.md section 3's shop table).
        -- razmire_keelgan.rs2's [opnpc4,razmire_keelgan] opens
        -- razmire_building_open (~openshop(razmirebuildingstore, ...))
        -- directly once %morttonquest >= mortton_shades_to_razmire, no
        -- dialogue needed. Quest Helper's own buyTimberLimeAndSwamp step:
        -- "Buy 5 timber beams, 5 limestone bricks, and 25 swamp paste from
        -- Razmire's builders' store" -- but none of the three stack, so
        -- swamp paste's count is capped to whatever backpack space is
        -- actually free after the other two, rather than hard-coding 25 and
        -- failing on "not enough inventory space" again.
        t.exec("razmire.shopOpen", t.shop.open, "razmire_keelgan", 4, "razmirebuildingstore")
        t.exec("razmire.buyTimber", t.shop.buy, "timberbeam", 5)
        t.exec("razmire.buyLimestone", t.shop.buy, "limestonebrick", 5)
        local free_slots_before_paste = 0
        for slot_index = 0, 27 do
            local slot_result, slot_cell = t.inv.slot(slot_index)
            if slot_result == "ok" and (slot_cell.name == "" or slot_cell.count == 0) then
                free_slots_before_paste = free_slots_before_paste + 1
            end
        end
        local swamppaste_target = 25
        if free_slots_before_paste - 1 < swamppaste_target then
            swamppaste_target = free_slots_before_paste - 1
        end
        if swamppaste_target < 5 then
            swamppaste_target = 5
        end
        t.check("razmire.spaceForSwamppaste", free_slots_before_paste > 0,
            tostring(free_slots_before_paste) .. " free slot(s) after timber/limestone -- buying "
                .. tostring(swamppaste_target) .. " swamppaste (25 asked, capped to fit)")
        t.exec("razmire.buySwamppaste", t.shop.buy, "swamppaste", swamppaste_target)
        local shop_close_result = t.shop.close()
        t.check("razmire.shopClose", shop_close_result == "ok", "shop.close() -> " .. tostring(shop_close_result))

        -- Use the second Serum 207 dose on the afflicted Ulsquire Shauncy
        -- (spawn row m54_51.spawn:44, 3496,3289,0) -- ulsquire_shauncy.rs2's
        -- ulsquire_talk, at %morttonquest = mortton_shades_to_razmire,
        -- takes the remaining shade_bones1 straight away (no menu).
        t.exec("goto-ulsquire", t.player.goto_tile, 3496, 3289, 0)
        local ulsquire_afflicted, ulsquire_afflicted_status = t.player.by_symbol("npc", "ulsquire_shauncy_afflicted")
        t.check("ulsquire.find", ulsquire_afflicted_status == "ok",
            "player.by_symbol(npc, ulsquire_shauncy_afflicted) -> " .. tostring(ulsquire_afflicted_status))
        t.exec("ulsquire.cure", t.player.use_on, "mort_serum3", ulsquire_afflicted)
        t.exec("ulsquire.giveRemains", t.chat.play, {
            "npc:you've made your own serum",
            "player:Razmire said to come and talk to you.",
            "npc:Oh yes, well, that's very nice.",
            "player:I just slayed 5 shades for Razmire",
            "npc:Oh yes, that would be interesting!",
            "mesbox:You show the shade remains to the priest",
        })
        t.exec("quest.stage.mortton_shades_to_ulsquire", t.quest.expect_stage, "mortton_shades_to_ulsquire")

        -- Talk to Ulsquire again and ask about the temple -- ulsquire.rs2's
        -- ulsquire_temple label sets %morttonquest = mortton_ulsquire_temple
        -- and names exactly what the temple needs (limestone bricks, swamp
        -- paste, wooden planks) "Thankfully Razmire stocks all these items."
        -- Same visit as the cure above (mortton_used_serum_on_ulsquire is
        -- now true), so the opening line is "Ah, hello again, what can I do
        -- for you now?" before the questions_post_shades menu opens.
        t.exec("ulsquire.askTemple", t.player.talk_to, "ulsquire_shauncy", 1)
        t.exec("ulsquire.askTemple-dialogue", t.chat.play, {
            "npc:hello again",
            "choose:What can you tell me about that temple?",
            "player:What can you tell me about that temple?",
            "npc:Hmm, interesting question",
            "player:How can you rebuild a totally destroyed temple?",
            "npc:I couldn't do it alone",
            "choose:Ok thanks",
            "player:Ok, thanks",
        })
        t.exec("quest.stage.mortton_ulsquire_temple", t.quest.expect_stage, "mortton_ulsquire_temple")

        -- The temple wall (flamtaer_temple.rs2's self-re-arming
        -- [oploc3,_temple_wall], 150 separate repair actions across fifteen
        -- wall locs) cannot be driven by clicking and polling inside this
        -- run's ~2,000-server-tick budget (docs/QUEST_AUTHORING.md section
        -- 9) -- ::mortton_repairtemple ([debugproc], landed 2026-09-22,
        -- docs/QUEST_SERVER_CHEATS.md section A) is the sanctioned
        -- fast-forward: it walks the SAME real ~mortton_temple_build_step
        -- against a real wall loc in a guarded loop, spending the real
        -- backpack materials through ~add_temple_resources, and needs no
        -- click at all -- only %morttonquest>=mortton_ulsquire_temple
        -- (already true here), Crafting 20 (setup) and a hammer (setup).
        -- What binds it is MATERIALS, not ticks: 800 resource pool per 5
        -- swamp paste + 1 limestone brick + 1 timber beam, and Razmire's
        -- store restocks only 5 of each -- so run the hook against the load
        -- already bought above, and if the quest has not yet reached
        -- mortton_can_light_altar(60), Razmire's Serum 207 cure has almost
        -- certainly lapsed again (200-tick npc_changetype), re-cure, buy
        -- another load, and run the hook again.
        -- t.cheat's own detail is hollow (nil) here too -- the debugproc's
        -- real answer is a say() line ("The temple is repaired after N
        -- repair(s)." / "Temple repair stopped at N% after M repair(s) -
        -- bring more limestone bricks, wooden planks and swamp paste."), not
        -- a packet reply. REVERTED last_failure: this shape PASSed twice
        -- with 0 repairs actually made (trap 12) -- read the reply back and
        -- FAIL a call that repaired nothing.
        local repair_result_1, repair_detail_1 = t.cheat("::mortton_repairtemple")
        local repair_msg_result_1, repair_msg_1 = t.msg.expect("repair(s)")
        local repair_count_1 = nil
        if repair_msg_result_1 == "ok" then
            repair_count_1 = tonumber(repair_msg_1:match("after (%d+) repair"))
        end
        t.check("temple.repair-1",
            repair_result_1 == "ok" and repair_msg_result_1 == "ok" and repair_count_1 ~= nil and repair_count_1 > 0,
            "t.cheat(::mortton_repairtemple) -> " .. tostring(repair_result_1) .. " (" .. tostring(repair_detail_1)
                .. "); reply: " .. tostring(repair_msg_result_1) .. " (" .. tostring(repair_msg_1)
                .. "); repairs made: " .. tostring(repair_count_1))
        local temple_stage_result, temple_stage_now = t.quest.stage()
        if temple_stage_result ~= "ok" or type(temple_stage_now) ~= "number" then
            temple_stage_now = 0
        end

        local temple_trip = 1
        while temple_stage_now < 60 and temple_trip < 8 do
            temple_trip = temple_trip + 1
            local trip_suffix = "-" .. tostring(temple_trip)

            -- razmire_keelgan.rs2's opnpc1 is defined on BOTH forms
            -- (razmire_keelgan and razmire_keelgan_afflicted) and both reach
            -- [label,razmire_talk] either way -- the afflicted form cures
            -- itself back to normal first if the "visible"/perm-serum bit is
            -- set, the normal form re-afflicts itself first (npc_changetype,
            -- silent) if it is not -- so no cure is needed to reopen the
            -- store, only knowing WHICH symbol is currently spawned (a
            -- player.by_symbol resolve answers "ok" for a content symbol
            -- regardless of live presence, unlike an actual world-pool read,
            -- so this checks presence with t.npc.by_symbol instead).
            t.exec("goto-razmire-temple" .. trip_suffix, t.player.goto_tile, 3489, 3296, 0)

            -- Run 2 measured the real bottleneck: NOT shop stock, backpack
            -- SPACE -- none of timberbeam/limestonebrick/swamppaste stack,
            -- buying a flat 5 more of each every trip regardless of what is
            -- already carried fills the pack with unspent timber/limestone
            -- while swamppaste (spent 5-per-refill, five times faster) never
            -- gets a slot free, and shop.buy answered "You don't have enough
            -- inventory space." on every later trip. Two fixes: only top
            -- timber/limestone up to 5 TOTAL (never re-buy what is already
            -- held), and the shade hunt is over by this point in the run, so
            -- the five non-stacking sharks (trap 26's food requirement,
            -- setup) are no longer load-bearing at their original count --
            -- drop down to one kept for safety and spend the reclaimed
            -- slots on the ingredient that actually runs out. Dropped HERE,
            -- before the shop opens: REVERTED last_failure (sampler
            -- sonnet-b13) -- run with this drop done AFTER shop.attach read
            -- "dropped 4 shark(s), 4 left" every trip (t.player.drop failing
            -- silently with the shop interface open, 23 ticks wasted a
            -- trip), and the row still graded true against that reading.
            local shark_result, shark_have = t.inv.count("shark")
            local shark_drops = 0
            while shark_result == "ok" and type(shark_have) == "number" and shark_have > 1 and shark_drops < 4 do
                t.player.drop("shark")
                shark_drops = shark_drops + 1
                shark_result, shark_have = t.inv.count("shark")
            end
            t.check("temple.freeSharkSlots" .. trip_suffix,
                shark_result == "ok" and type(shark_have) == "number" and shark_have <= 1,
                "dropped " .. tostring(shark_drops) .. " shark(s), " .. tostring(shark_have)
                    .. " left (kept for trap 26's food requirement)")

            local razmire_afflicted_present, razmire_afflicted_present_status =
                t.npc.by_symbol("razmire_keelgan_afflicted")
            local razmire_talk_symbol = "razmire_keelgan"
            if razmire_afflicted_present_status == "ok" then
                razmire_talk_symbol = "razmire_keelgan_afflicted"
            end
            t.exec("razmire.talk" .. trip_suffix, t.player.talk_to, razmire_talk_symbol, 1)
            -- %morttonquest is mortton_rebuild_temple(55) by now (crossed by
            -- the first ::mortton_repairtemple call above), which routes
            -- razmire_talk to a DIFFERENT branch than the
            -- shades_to_razmire-era razmire_store_selection menu used at the
            -- hand-in above: "Ah, it's you, how's it going?" (the
            -- mortton_used_serum_on_razmire bit is already set from the
            -- first cure, so this is never the first-time "made your own
            -- serum" line again) / "Hello there, I've started repairing the
            -- temple." / "That's great, carry on the good work." ->
            -- razmire_questions_post_rebuild's own p_choice5, where case 4
            -- "Can you open a store for me?" goes through
            -- razmire_store_post_oil ("Sure, which store do you wanna see?")
            -- into a SECOND menu (razmire_store_selection_post_oil) before
            -- razmire_building_open actually fires. That label is
            -- ~openshop(razmirebuildingstore,...) directly -- it opens
            -- shopmain with no page of its own, so the list ends at the
            -- "choose:" row (trap 30) and shop.attach binds the screen the
            -- click opened rather than shop.open pressing a closed npc menu
            -- a second time (the fix behind this whole trip loop: shop.buy
            -- answers "this shop was not opened through shop.open" without
            -- it).
            t.exec("razmire.reopenDialogue" .. trip_suffix, t.chat.play, {
                "npc:it's you",
                "player:I've started repairing the temple",
                "npc:carry on the good work",
                "choose:Can you open a store for me?",
                "player:Can you open a store for me?",
                "npc:which store",
                "choose:Can I see the building store please?",
            })
            t.exec("razmire.shopAttach" .. trip_suffix, t.shop.attach, "razmirebuildingstore")

            local timber_result, timber_have = t.inv.count("timberbeam")
            if timber_result ~= "ok" or type(timber_have) ~= "number" then
                timber_have = 0
            end
            if timber_have < 5 then
                t.exec("razmire.buyTimber" .. trip_suffix, t.shop.buy, "timberbeam", 5 - timber_have)
            else
                t.check("razmire.buyTimber" .. trip_suffix, true,
                    "already holding " .. tostring(timber_have) .. " timberbeam -- skipped, not the bottleneck")
            end

            local limestone_result, limestone_have = t.inv.count("limestonebrick")
            if limestone_result ~= "ok" or type(limestone_have) ~= "number" then
                limestone_have = 0
            end
            if limestone_have < 5 then
                t.exec("razmire.buyLimestone" .. trip_suffix, t.shop.buy, "limestonebrick", 5 - limestone_have)
            else
                t.check("razmire.buyLimestone" .. trip_suffix, true,
                    "already holding " .. tostring(limestone_have) .. " limestonebrick -- skipped, not the bottleneck")
            end

            local swamppaste_have_result, swamppaste_have = t.inv.count("swamppaste")
            if swamppaste_have_result ~= "ok" or type(swamppaste_have) ~= "number" then
                swamppaste_have = 0
            end
            local free_slots = 0
            for slot_index = 0, 27 do
                local slot_result, slot_cell = t.inv.slot(slot_index)
                if slot_result == "ok" and (slot_cell.name == "" or slot_cell.count == 0) then
                    free_slots = free_slots + 1
                end
            end
            -- Mirror the initial purchase's floor (razmire.buySwamppaste
            -- above, lines 432-437): the repair debugproc spends 5 swamp
            -- paste per repair action, so capping THIS trip's buy to
            -- whatever free_slots happens to be -- with no floor -- can
            -- leave fewer than 5 held total. REVERTED last_failure (sampler
            -- sonnet-b13): temple.repair-2/-4 called the debugproc sitting
            -- on <5 swamp paste and it silently repaired 0, and the old row
            -- here PASSed on the cheat's bare "ok" without reading that.
            local paste_target = 25 - swamppaste_have
            if paste_target > free_slots then
                paste_target = free_slots
            end
            local paste_floor = 5 - swamppaste_have
            if paste_floor > 0 and paste_target < paste_floor then
                paste_target = paste_floor
            end
            if paste_target < 0 then
                paste_target = 0
            end
            t.check("razmire.spaceForPaste" .. trip_suffix, free_slots > 0 or swamppaste_have >= 5,
                tostring(free_slots) .. " free slot(s), " .. tostring(swamppaste_have)
                    .. " swamppaste already held -- buying " .. tostring(paste_target)
                    .. " more (floor 5 total, cap 25 total)")
            if paste_target > 0 then
                t.exec("razmire.buySwamppaste" .. trip_suffix, t.shop.buy, "swamppaste", paste_target)
            else
                t.check("razmire.buySwamppaste" .. trip_suffix, swamppaste_have >= 5,
                    "already holding " .. tostring(swamppaste_have) .. " swamppaste -- skipped buying more")
            end
            local shop_close_result = t.shop.close()
            t.check("razmire.shopClose" .. trip_suffix, shop_close_result == "ok",
                "shop.close() -> " .. tostring(shop_close_result))

            -- t.cheat's own detail is hollow (nil) here too -- read the
            -- debugproc's real say() reply back and FAIL a call that
            -- repaired nothing (trap 12; same shape as temple.repair-1
            -- above).
            local repair_result, repair_detail = t.cheat("::mortton_repairtemple")
            local repair_msg_result, repair_msg = t.msg.expect("repair(s)")
            local repair_count = nil
            if repair_msg_result == "ok" then
                repair_count = tonumber(repair_msg:match("after (%d+) repair"))
            end
            t.check("temple.repair" .. trip_suffix,
                repair_result == "ok" and repair_msg_result == "ok" and repair_count ~= nil and repair_count > 0,
                "t.cheat(::mortton_repairtemple) -> " .. tostring(repair_result) .. " (" .. tostring(repair_detail)
                    .. "); reply: " .. tostring(repair_msg_result) .. " (" .. tostring(repair_msg)
                    .. "); repairs made: " .. tostring(repair_count))

            local stage_result, stage_value = t.quest.stage()
            if stage_result == "ok" and type(stage_value) == "number" then
                temple_stage_now = stage_value
            end
        end

        t.check("temple.wallsRepaired", temple_stage_now >= 60,
            "after " .. tostring(temple_trip) .. " materials trip(s) and ::mortton_repairtemple call(s), "
                .. "morttonquest reached " .. tostring(temple_stage_now)
                .. " (55=mortton_rebuild_temple, 60=mortton_can_light_altar)")

        if temple_stage_now < 60 then
            t.blocked("the temple wall repair (flamtaer_temple.rs2's ::mortton_repairtemple debugproc) did not "
                .. "reach %morttonquest=mortton_can_light_altar(60) after " .. tostring(temple_trip)
                .. " materials trip(s) (5 timberbeam/5 limestonebrick/up-to-25 swamppaste per trip, capped by "
                .. "Razmire's own restock and by backpack space) -- morttonquest last read "
                .. tostring(temple_stage_now) .. ". Razmire's builders' store restocks only 5 timberbeam/5 "
                .. "limestonebrick per visit, which bounds how much resource pool each trip can refill.")
            return
        end

        -- Ulsquire gives sacred olive oil once the temple is built
        -- (ulsquire_shauncy.rs2's ulsquire_temple_built branch, reached
        -- through questions_post_rebuild). His cure from earlier has almost
        -- certainly lapsed across the temple-repair wait -- same
        -- afflicted-check-and-recure idiom again.
        t.exec("goto-ulsquire-2", t.player.goto_tile, 3496, 3289, 0)
        -- ulsquire_temple_built's free-oil grant is gated on
        -- `inv_freespace(inv) != 0` -- the two shop trips carried real
        -- weight (run 6/7's own screenshots showed the pack near full), so
        -- guarantee at least one slot before the dialogue reaches that
        -- check, or it falls to the "buy it from Razmire instead" branch.
        local free_slots_before_oil = 0
        for slot_index = 0, 27 do
            local slot_result_3, slot_cell_3 = t.inv.slot(slot_index)
            if slot_result_3 == "ok" and (slot_cell_3.name == "" or slot_cell_3.count == 0) then
                free_slots_before_oil = free_slots_before_oil + 1
            end
        end
        if free_slots_before_oil < 1 then
            t.player.drop("ashes")
        end
        -- ulsquire_shauncy.rs2's opnpc1 is defined on BOTH forms, exactly
        -- like razmire_keelgan.rs2 above, and both reach [label,ulsquire_talk]
        -- either way -- no cure needed, only knowing which symbol is
        -- currently live. player.by_symbol resolves a content symbol and
        -- answers "ok" whether or not the npc is actually spawned (the same
        -- false-positive that broke the temple-wall trip loop before this
        -- fix), so presence is checked with t.npc.by_symbol instead.
        local ulsquire_afflicted_present, ulsquire_afflicted_present_status =
            t.npc.by_symbol("ulsquire_shauncy_afflicted")
        local ulsquire_talk_symbol = "ulsquire_shauncy"
        if ulsquire_afflicted_present_status == "ok" then
            ulsquire_talk_symbol = "ulsquire_shauncy_afflicted"
        end
        t.exec("ulsquire.askOil", t.player.talk_to, ulsquire_talk_symbol, 1)
        -- ulsquire_talk's universal opener (bit already true) is "Ah, hello
        -- again, what can I do for you now?", THEN the PLAYER line "Hello
        -- there, I've started repairing the temple." (not an npc line --
        -- run 7's own ulsquire.askOil-dialogue never ran this far, but the
        -- razmire.recure-2-dialogue FAIL at the same universal-opener shape
        -- caught the same mistake there). ulsquire_temple_built loops back
        -- to @ulsquire_questions_post_rebuild's own menu after the mesbox,
        -- so a second "Ok, thanks." is needed to close out.
        t.exec("ulsquire.askOil-dialogue", t.chat.play, {
            "npc:hello again",
            "player:I've started repairing the temple",
            "npc:carry on the good work",
            "choose:What should I do when the temple is built?",
            "player:What should I do when the temple is built?",
            "npc:it seems that the temple was",
            "npc:The pagans believed that sacred oil sanctified",
            "mesbox:Ulsquire gives you some olive oil",
            "player:Thanks!",
            "choose:Ok, thanks.",
            "player:Ok, thanks",
        })
        t.expect("haveOliveOil", t.inv.expect_has("oliveoil3", 1))

        -- Light the fire altar (flamtaer_temple.rs2's
        -- [oploc1,templefire_altar_nofire]) -- crafting/tinderbox/sanctity
        -- are already satisfied from the repair above; another
        -- self-re-arming roll (p_oploc(3) again), poll instead of re-press.
        -- The debugproc-driven repair loop above never walks the player to
        -- the temple at all (it needs no click), so run 3's altarClick
        -- answered "covered ... the world is not picking" while standing at
        -- Ulsquire's tile, 3496,3289 -- go back to the temple first.
        t.exec("goto-temple-altar", t.player.goto_tile, 3506, 3316, 0)
        local altar_click_result, altar_click_detail = t.player.click_loc("templefire_altar_nofire", 1)
        t.check("temple.altarClick", altar_click_result == "ok",
            "click_loc(templefire_altar_nofire, op1) -> " .. tostring(altar_click_result) .. " "
                .. tostring(altar_click_detail))
        local altar_lit = false
        local altar_rounds = 0
        while not altar_lit and altar_rounds < 10 do
            altar_rounds = altar_rounds + 1
            t.ticks(10)
            local altar_target, altar_target_status = t.player.by_symbol("loc", "templefire_altar")
            if altar_target_status == "ok" then
                altar_lit = true
            end
        end
        t.check("temple.altarLit", altar_lit,
            "polled " .. tostring(altar_rounds) .. " round(s) of 10 ticks for loc templefire_altar (lit) to appear")

        -- Sanctify the olive oil in the lit flame (oc_category 110,
        -- oplocu,templefire_altar) -> sacred_oil3, %morttonquest =
        -- mortton_created_sacred_oil.
        local altar_target_2, altar_target_2_status = t.player.by_symbol("loc", "templefire_altar")
        t.check("temple.altarFind", altar_target_2_status == "ok",
            "player.by_symbol(loc, templefire_altar) -> " .. tostring(altar_target_2_status))
        t.exec("temple.sanctifyOil", t.player.use_on, "oliveoil3", altar_target_2)
        t.exec("quest.stage.mortton_created_sacred_oil", t.quest.expect_stage, "mortton_created_sacred_oil")

        -- Sacred oil on the logs setup carried (mortton_pyre.rs2's
        -- [opheldu,_sacred_oil], oc_category(logs)=22) -> logs_pyre,
        -- %morttonquest = mortton_created_pyre_logs. pyre_logs needs 2
        -- doses and sacred_oil3 has 3 (quest_mortton.obj), so one use is
        -- enough.
        -- mortton_pyre.rs2's [opheldu,_sacred_oil] used to check
        -- oc_category(last_item) on both branches, and last_item is ALWAYS
        -- the sacred oil itself for every dispatch rung that can reach this
        -- handler (torirs_server_scripts.c's opheldu_orient invariant:
        -- "last_item is the item the script is bound to, last_useitem is
        -- the other one") -- so the pyre-logs branch was unreachable and
        -- this row reproduced it live ("Nothing interesting happens.", the
        -- prior REVERTED attempt's own finding, reported as a content bug
        -- against mortton_pyre.rs2:2,6,11). OSRS-Content has since fixed it
        -- (read fresh this session: lines 7 and 11 now read
        -- oc_category(last_useitem), with a comment there naming the same
        -- invariant) -- assert the real, now-reachable behaviour instead of
        -- re-reporting the old bug (trap: "Rows that ASSERT THE OLD BUG are
        -- now FAIL and must be rewritten to assert the fixed behaviour").
        t.exec("temple.makePyreLogs", t.player.use_item_on_item, "sacred_oil3", "logs")
        t.expect("havePyreLogs", t.inv.expect_has("logs_pyre", 1))
        t.exec("quest.stage.mortton_created_pyre_logs", t.quest.expect_stage, "mortton_created_pyre_logs")

        -- Build the funeral pyre (mortton_pyre.rs2's [oploc1,temple_pyre] /
        -- [oploc1,_pyre_loaded] / [oploc1,_pyre_remains_loaded]): op1 on the
        -- empty pyre uses whatever pyre logs are carried
        -- (~mortton_best_pyre_logs, only logs_pyre held here), op1 again on
        -- the loaded pyre adds the best shade remains carried
        -- (~mortton_best_pyre_remains) -- two shade_bones1 remain after the
        -- Razmire (2) and Ulsquire (1) hand-ins above, out of the five the
        -- shade hunt dropped. The funeral pyres sit on the shade street,
        -- not at the temple altar -- nearest copy of loc 4093
        -- (all.loc.compack) to the player decoded from maps/m54_50.jl2
        -- (trap 29): level 0, local 51,12 in map square 54,50 -> 3507,3276.
        t.exec("goto-pyre", t.player.goto_tile, 3507, 3276, 0)
        local pyre_near_result, pyre_near_detail = t.world.loc_near("temple_pyre", 15)
        t.check("temple.pyreFind", pyre_near_result == "ok",
            "world.loc_near(temple_pyre, 15) -> " .. tostring(pyre_near_result) .. " " .. tostring(pyre_near_detail))

        t.exec("temple.pyreAddLogs", t.player.click_loc, "temple_pyre", 1)
        t.exec("quest.stage.mortton_logs_on_pyre", t.quest.expect_stage, "mortton_logs_on_pyre")
        -- Section 8: "give a loc_change 2-3 ticks before a second
        -- click_loc can see the new form."
        t.ticks(2)
        local pyre_logs_near_result, pyre_logs_near_detail = t.world.loc_near("temple_pyre_logs", 5)
        t.check("temple.pyreLogsPlaced", pyre_logs_near_result == "ok",
            "world.loc_near(temple_pyre_logs, 5) -> " .. tostring(pyre_logs_near_result) .. " "
                .. tostring(pyre_logs_near_detail))

        local shade_bones_before_result, shade_bones_before = t.inv.count("shade_bones1")
        t.check("temple.shadeBonesBeforePyre", shade_bones_before_result == "ok",
            "inv.count(shade_bones1) -> " .. tostring(shade_bones_before_result) .. " ("
                .. tostring(shade_bones_before) .. ")")
        t.exec("temple.pyreAddRemains", t.player.click_loc, "temple_pyre_logs", 1)
        -- Trap 24: a click verb's `ok` is the server's sentence, one tick
        -- before the container update reaches the client -- a bare
        -- t.inv.count on the line right below read 2 -> 2 (run 2's own
        -- FAIL) though add_remains_to_funeral_pyre's inv_del(inv,
        -- $remains, 1) already ran. Spend the tick first.
        t.ticks(1)
        local shade_bones_after_result, shade_bones_after = t.inv.count("shade_bones1")
        t.check("temple.shadeBonesAfterPyre",
            shade_bones_after_result == "ok" and type(shade_bones_before) == "number"
                and type(shade_bones_after) == "number" and shade_bones_after == shade_bones_before - 1,
            "inv.count(shade_bones1): " .. tostring(shade_bones_before) .. " -> " .. tostring(shade_bones_after)
                .. " (add_remains_to_funeral_pyre's own inv_del)")
        t.ticks(2)
        local pyre_bones_near_result, pyre_bones_near_detail = t.world.loc_near("temple_pyre_bones_logs", 5)
        t.check("temple.pyreRemainsPlaced", pyre_bones_near_result == "ok",
            "world.loc_near(temple_pyre_bones_logs, 5) -> " .. tostring(pyre_bones_near_result) .. " "
                .. tostring(pyre_bones_near_detail))

        -- Light it ([oploc1,_pyre_remains_loaded] -> label
        -- light_funeral_pyre): a stat_random(firemaking, 64, 512) roll that
        -- self-re-arms on its own failure branch via p_oploc(4), same idiom
        -- as the temple wall/fire altar above (file header) -- press once
        -- and poll quest.stage() rather than re-pressing per round.
        t.exec("temple.pyreLight", t.player.click_loc, "temple_pyre_bones_logs", 1)
        local pyre_lit = false
        local pyre_rounds = 0
        while not pyre_lit and pyre_rounds < 10 do
            pyre_rounds = pyre_rounds + 1
            t.ticks(10)
            local pyre_stage_result, pyre_stage_value = t.quest.stage()
            if pyre_stage_result == "ok" and type(pyre_stage_value) == "number" and pyre_stage_value >= 80 then
                pyre_lit = true
            end
        end
        t.check("temple.pyreLit", pyre_lit,
            "polled " .. tostring(pyre_rounds) .. " round(s) of 10 ticks for morttonquest to reach "
                .. "mortton_lit_pyre(80)")

        if not pyre_lit then
            local pyre_stage_final_result, pyre_stage_final_value = t.quest.stage()
            t.blocked("the funeral pyre ([oploc1,_pyre_remains_loaded] -> light_funeral_pyre's self-re-arming "
                .. "stat_random(firemaking, 64, 512) roll, mortton_pyre.rs2) never reached "
                .. "%morttonquest=mortton_lit_pyre(80) after " .. tostring(pyre_rounds) .. " round(s) of 10 ticks "
                .. "with firemaking 99 (setup) -- morttonquest last read " .. tostring(pyre_stage_final_value)
                .. " (" .. tostring(pyre_stage_final_result) .. ")")
            return
        end

        -- The reward queue (funeral_pyre_reward, 2-tick delay) drops loot
        -- and clears %pyre_loc -- settle before talking to Ulsquire.
        t.ticks(3)

        -- Reward snapshot before the hand-in (section 7's rule).
        local skill_snapshot_result, skill_snapshot = t.skill.snapshot()
        t.check("skill.snapshotBeforeComplete", skill_snapshot_result == "ok",
            "skill.snapshot() -> " .. tostring(skill_snapshot_result))

        -- Tell Ulsquire the shade's spirit is at rest (ulsquire_shauncy.rs2's
        -- ulsquire_talk, %morttonquest = mortton_lit_pyre branch) ->
        -- queue(mortton_quest_complete, 0, 0). Serum 207's 200-tick cure
        -- (npc_changetype) may have lapsed across the pyre build above --
        -- opnpc1 on the afflicted form only auto-reverts to @ulsquire_talk
        -- when ulsquire_visible or ulsquire_perm_serum_used is still set,
        -- otherwise it falls to @afflicted_talk and this dialogue would not
        -- match -- re-cure first with the ashes/tarrominvial pair kept in
        -- reserve since before the first shop trip (same idiom as
        -- mixSerum207-3 above) if the afflicted form is what is spawned.
        t.exec("goto-ulsquire-3", t.player.goto_tile, 3496, 3289, 0)
        local ulsquire_afflicted_final, ulsquire_afflicted_final_status =
            t.npc.by_symbol("ulsquire_shauncy_afflicted")
        local ulsquire_final_symbol = "ulsquire_shauncy"
        if ulsquire_afflicted_final_status == "ok" then
            ulsquire_final_symbol = "ulsquire_shauncy_afflicted"
            t.exec("mixSerum207-4", t.player.use_item_on_item, "ashes", "tarrominvial")
            local ulsquire_afflicted_target, ulsquire_afflicted_target_status =
                t.player.by_symbol("npc", "ulsquire_shauncy_afflicted")
            t.check("ulsquire.recureFind", ulsquire_afflicted_target_status == "ok",
                "player.by_symbol(npc, ulsquire_shauncy_afflicted) -> "
                    .. tostring(ulsquire_afflicted_target_status))
            t.exec("ulsquire.recure", t.player.use_on, "mort_serum3", ulsquire_afflicted_target)
        end
        t.exec("ulsquire.completePyre", t.player.talk_to, ulsquire_final_symbol, 1)
        -- ulsquire_talk's universal opener (mortton_used_serum_on_ulsquire
        -- already true) is "Ah, hello again, what can I do for you now?"
        -- BEFORE the stage-specific branch -- same shape ulsquire.askOil-
        -- dialogue above already accounts for; run 2's own FAIL here
        -- (expected kind=player, got npc) missed it on this list.
        t.exec("ulsquire.completePyre-dialogue", t.chat.play, {
            "npc:hello again",
            "player:I've put the Shade's spirit to rest",
            "npc:Great! Well done my friend!",
        })
        -- Completion is asynchronous (section 8): the queue(0,0) call fires
        -- behind this dialogue's own close.
        t.ticks(3)
        t.quest.expect_complete()

        -- Every reward quest_mortton.rs2's own ~quest_complete_rewards
        -- lists: "2000 Herblore XP|2000 Crafting XP|Access to the Shade
        -- Catacombs" (stat_advance(..., 20000) -- the internal *10 unit
        -- t.skill.expect_gain already accepts and names).
        t.exec("reward.herbloreXp", t.skill.expect_gain, "herblore", 2000, skill_snapshot)
        t.exec("reward.craftingXp", t.skill.expect_gain, "crafting", 2000, skill_snapshot)

        t.finish(0)
        return
    end,
}
