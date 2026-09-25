-- Gertrude's Cat (quest_fluffs). Hand-authored from the scaffold, resumed
-- after t.player.use_item_on_item landed (was BLOCKED on the missing OPHELDU
-- verb) and again after a review rejection for two missing item-reward
-- rows; see QUEUE.tsv's last_failure history for both.
--
-- Flow (areas/varrock/scripts/gertrude.rs2, quests/quest_fluffs/scripts/
-- quest_fluffs.rs2): talk to Gertrude and accept -> talk to Shilop, pay 100
-- coins for the lumber-mill tip -> find Fluffs (gertrudescat, a public map
-- npc, always spawned) and give her the milk -> pick doogle leaves, rub them
-- on a raw sardine (item-on-item, OPHELDU, quest_fluffs.rs2:142-168) to
-- season it, feed Fluffs -> she sends kittens mewing from one of six crates
-- (%fluffs_crate = random(6), quest_fluffs.rs2:124/248, tiles decoded from
-- quest_fluffs.constant's ^fluffs_crate_0..5) -> give the found kitten back
-- to Fluffs -> hand in to Gertrude for the reward.
--
-- Driven for real throughout: Gertrude's accept dialogue, paying Shilop the
-- 100 coins (a real p_choice3 -> p_choice2 branch with the coins actually
-- deducted), walking upstairs to Fluffs and using the milk on her (a real
-- OPNPCU click, %fluffs actually advances), picking a live doogleleaves obj,
-- seasoning the sardine with a real use_item_on_item (OPHELDU) press, feeding
-- it to Fluffs, searching each of the six live kittens_mew crate npcs by its
-- own tile until the one matching %fluffs_crate answers with the kitten
-- mesbox, giving the found kitten back to Fluffs (opnpcu, gertrudekittens),
-- and hand-in to Gertrude (opnpc1, %fluffs=rescued branch) which settles the
-- rewards synchronously (~fluffs_settle_rewards): 1525 Cooking XP, a random
-- one of six pet-kitten colours (~gertrude_give_cat), a Chocolate Cake and a
-- Stew (quest_fluffs.rs2:326-350).

return {
    id = "fluffs",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so requirements fit
        "::give bucket_milk 1", -- Quest Helper prerequisite -- not obtainable during the quest
        "::give raw_sardine 1", -- Quest Helper prerequisite
        "::give coins 100", -- Quest Helper prerequisite -- Shilop's price, paid via dialogue below
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "fluffs",
            constants = {
                complete = 6,
                gave_milk = 3,
                gave_sardine = 4,
                not_started = 0,
                paid_boy = 2,
                questpoints = 1,
                rescued = 5,
                started = 1,
            },
            row = "quest_gertrudescat",
            display = "Gertrude's Cat",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3) -- setup cheats' effects are not client-side yet

        -- Talk to Gertrude west of Varrock (gertrude.rs2:10) and accept.
        t.exec("goto-talkToGertrude", t.player.goto_tile, 3151, 3410, 0)
        t.exec("talkToGertrude", t.player.talk_to, "gertrude", 1)
        t.exec("talkToGertrude-dialog", t.chat.play, {
            "player:Hello, are you okay?",
            "npc:Do I look ok? Those kids drive me crazy.",
            "npc:I'm sorry. It's just that I've lost her.",
            "player:Lost who?",
            "npc:Fluffs, poor Fluffs. She never hurt anyone.",
            "player:Who's Fluffs?",
            "npc:My beloved feline friend Fluffs. She's been purring by my side for almost a decade. Please, could you go search for her while I look over the kids?",
            "choose:Well, I suppose I could.",
            "player:Well, I suppose I could.",
            "npc:Really? Thank you so much! I really have no idea where she could be!",
            "npc:I think my sons, Shilop and Wilough, saw the cat last. They'll be out in the market place.",
            "player:Alright then, I'll see what I can do.",
        })
        t.exec("quest.stage.started", t.quest.expect_stage, "started")

        -- Talk to Shilop in Varrock Square (fluffs_boy_dialogue -> fluffs_boy
        -- -> fluffs_boy_secondary, "What will make you tell me?" -> fluffs_pay,
        -- "Okay then, I'll pay." -- pays the 100 coins for real).
        t.exec("goto-talkToShilop", t.player.goto_tile, 3221, 3434, 0)
        t.exec("talkToShilop", t.player.talk_to, "shilop", 1)
        t.exec("talkToShilop-dialog", t.chat.play, {
            "player:Hello there, I've been looking for you.",
            "npc:I didn't mean to take it! I just forgot to pay.",
            "player:What? I'm trying to help your mum find Fluffs.",
            "npc:I might be able to help. Fluffs followed me to our secret play area and I haven't seen her since.",
            "player:Where is this play area?",
            "npc:If I told you that, it wouldn't be a secret.",
            "choose:What will make you tell me?",
            "player:What will make you tell me?",
            "npc:Well...now you ask, I am a bit short on cash.",
            "player:How much?",
            "npc:10 coins.",
            "npc:10 coins?!",
            "npc:I'll handle this.",
            "npc:100 coins should cover it.",
            "player:100 coins! Why should I pay you?",
            "npc:You shouldn't, but we won't help otherwise. We never liked that cat anyway, so what do you say?",
            "choose:Okay then, I'll pay.",
            "player:Okay then, I'll pay.",
            "mesbox:You give the lad 100 coins.",
            "player:There you go, now where did you see Fluffs ?",
            "npc:We play at an abandoned lumber mill to the north east. Just beyond the Jolly Boar Inn. I saw Fluffs running around in there.",
            "player:Anything else?",
            "npc:Well, you'll have to find the broken fence to get in. I'm sure you can manage that.",
        })
        t.exec("quest.stage.paid_boy", t.quest.expect_stage, "paid_boy")
        local coins_result, coins_left = t.inv.count("coins")
        t.check("spentCoins", coins_result == "ok" and coins_left == 0,
            "coins after paying Shilop 100 = " .. tostring(coins_left) .. " (read " .. tostring(coins_result) .. ")")

        -- Fluffs (gertrudescat) is upstairs in the lumber mill loft,
        -- ^fluffs_cat_coord = 1_51_54_42_56 -> 3306,3512,1. Give her the milk
        -- (OPNPCU, quest_fluffs.rs2:216-234).
        t.exec("goto-fluffsCat", t.player.goto_tile, 3306, 3512, 1)
        local cat = t.player.by_symbol("npc", "gertrudescat")
        t.exec("giveMilkToFluffs", t.player.use_on, "bucket_milk", cat)
        -- OPNPCU's own p_delay (quest_fluffs.rs2:229-234) lands after
        -- use_on's settle resolves (measured 2026-09-19: the settle's
        -- `map_flag` arm can win the race before the server-side write) --
        -- not client-side yet without this.
        t.ticks(3)
        t.exec("quest.stage.gave_milk", t.quest.expect_stage, "gave_milk")
        local milk_result, milk_left = t.inv.count("bucket_milk")
        t.check("milkConsumed", milk_result == "ok" and milk_left == 0,
            "bucket_milk after feeding Fluffs = " .. tostring(milk_left) .. " (read " .. tostring(milk_result) .. ")")

        -- Doogle leaves grow behind Gertrude's house (m49_53.spawn OBJ block,
        -- e.g. 3151,3399,0). Pick one up -- the seasoning step past this needs
        -- it and the raw sardine already carried.
        t.exec("goto-pickDoogleLeaves", t.player.goto_tile, 3151, 3399, 0)
        -- click_obj answers `ok` with a nil detail on this path (the await
        -- branch, pointer.lua:1431-1437) -- hollow through t.exec (measured
        -- 2026-09-19: "why=hollow -- ok with no detail"). Call it directly.
        local pick_result, pick_detail = t.player.click_obj("doogleleaves")
        t.check("pickDoogleLeaves", pick_result == "ok",
            "click_obj(doogleleaves) -> " .. tostring(pick_result) .. " " .. tostring(pick_detail))
        t.expect("haveDoogleLeaves", t.inv.expect_has("doogleleaves", 1))
        t.expect("haveRawSardine", t.inv.expect_has("raw_sardine", 1))

        -- Both ingredients for the seasoned sardine (quest_fluffs.rs2:142-168,
        -- [proc,fluffs_make_sardine] via [opheldu,doogleleaves]/[opheldu,
        -- raw_sardine]) are now in the backpack -- a real item-on-item OPHELDU
        -- press (arms doogleleaves, clicks raw_sardine's cell). The recipe
        -- pauses on its own ~mesbox ("You rub the doogle leaves over the
        -- sardine."), so the verb settles on that page and the seasoned
        -- sardine only lands once it is dismissed (section 8's gap note).
        t.exec("makeSeasonedSardine", t.player.use_item_on_item, "doogleleaves", "raw_sardine")
        local sardine_close_result, sardine_close_detail = t.chat.continue_()
        t.check("makeSeasonedSardine-dismiss", sardine_close_result == "ok",
            "chat.continue_ after makeSeasonedSardine -> " .. tostring(sardine_close_result)
                .. " " .. tostring(sardine_close_detail))
        local sardine_await_result = t.inv.await("seasoned_sardine", 1, 10)
        t.check("makeSeasonedSardine-sync", sardine_await_result == "ok",
            "inv.await seasoned_sardine 1 -> " .. tostring(sardine_await_result))
        t.expect("haveSeasonedSardine", t.inv.expect_has("seasoned_sardine", 1))

        -- Feed the seasoned sardine to Fluffs (OPNPCU, quest_fluffs.rs2:235-
        -- 248) -- back upstairs to the loft, gertrudescat re-resolved fresh
        -- since goto-pickDoogleLeaves moved off her tile. This is also the
        -- action that rolls %fluffs_crate = random(6) for the kitten hunt.
        t.exec("goto-fluffsCat2", t.player.goto_tile, 3306, 3512, 1)
        local cat2 = t.player.by_symbol("npc", "gertrudescat")
        t.exec("giveSardineToFluffs", t.player.use_on, "seasoned_sardine", cat2)
        t.ticks(3) -- OPNPCU's own p_delay lands after the click's settle, same race as the milk step
        t.exec("quest.stage.gave_sardine", t.quest.expect_stage, "gave_sardine")
        local sardine_result, sardine_left = t.inv.count("seasoned_sardine")
        t.check("sardineConsumed", sardine_result == "ok" and sardine_left == 0,
            "seasoned_sardine after feeding Fluffs = " .. tostring(sardine_left) .. " (read " .. tostring(sardine_result) .. ")")

        -- Six mewing-crate tiles, decoded from quest_fluffs.constant's
        -- ^fluffs_crate_0..5 (level_regionX_regionY_localX_localY, section 8's
        -- formula: worldX = regionX*64+localX, worldZ = regionY*64+localY).
        -- %fluffs_crate was just rolled to one of these six by the feed above
        -- -- [opnpc1,kittens_mew] only grants the kitten when npc_coord
        -- matches it, so every live crate npc is tried by its own tile until
        -- the one that does answers with the kitten mesbox.
        local crate_tiles = {
            { x = 3305, z = 3500, level = 0 }, -- ^fluffs_crate_0 = 0_51_54_41_44
            { x = 3310, z = 3499, level = 0 }, -- ^fluffs_crate_1 = 0_51_54_46_43
            { x = 3307, z = 3507, level = 0 }, -- ^fluffs_crate_2 = 0_51_54_43_51
            { x = 3303, z = 3506, level = 0 }, -- ^fluffs_crate_3 = 0_51_54_39_50
            { x = 3298, z = 3514, level = 0 }, -- ^fluffs_crate_4 = 0_51_54_34_58
            { x = 3315, z = 3515, level = 0 }, -- ^fluffs_crate_5 = 0_51_54_51_59
        }
        -- Checked IMMEDIATELY after each talk_to, no extra t.ticks in
        -- between: talk_to's own settle already waits out
        -- [opnpc1,kittens_mew]'s p_delay(4), and this cluster sits right
        -- against the wilderness line, so the "proceed with caution" system
        -- warning can also come up mid-search on one of these tiles -- an
        -- unrelated mesbox that has to be told apart from the kitten one and
        -- dismissed, not left open to swallow the next crate's click.
        local kitten_found = false
        local kitten_crate_index = nil
        for i = 1, #crate_tiles do
            if not kitten_found then
                local tile = crate_tiles[i]
                t.exec("goto-crate" .. i, t.player.goto_tile, tile.x, tile.z, tile.level)
                -- Not through t.exec: standing exactly on the crate npc's own
                -- tile (goto_tile lands on it, same as npc_coord) can make
                -- talk_to's own re-press-to-confirm land on bare ground once
                -- the kitten mesbox is already open and covering the world
                -- (measured: click_result answers "menu has no row for it"
                -- with the "You find a kitten!" mesbox already up in the
                -- SAME shot) -- the real ground truth is the dialogue that
                -- chat.play reads right after, not that raw click result.
                local click_result, click_detail = t.player.talk_to("kittens_mew", 1)
                local play_result, play_detail = t.chat.play({ "mesbox:You find a kitten!" })
                if play_result == "ok" then
                    kitten_found = true
                    kitten_crate_index = i
                elseif play_result == "mismatch" then
                    -- an unrelated dialogue is up (measured: the wilderness-
                    -- boundary warning near this crate cluster) -- dismiss it
                    -- so it cannot swallow the next crate's click.
                    t.chat.continue_()
                end
                local outcome_ok = play_result == "ok" or play_result == "not_visible" or play_result == "mismatch"
                t.check("searchCrate" .. i, outcome_ok,
                    "crate " .. i .. " at " .. tile.x .. "," .. tile.z .. " -- talk_to(kittens_mew) -> "
                        .. tostring(click_result) .. " " .. tostring(click_detail)
                        .. "; chat.play(mesbox:You find a kitten!) -> " .. tostring(play_result) .. " " .. tostring(play_detail))
            end
        end
        t.check("kittenFound", kitten_found == true,
            "kittens_mew crate search: found at crate index " .. tostring(kitten_crate_index)
                .. " of 6 tried (%fluffs_crate matched)")
        t.expect("haveKitten", t.inv.expect_has("gertrudekittens", 1))

        -- Give the found kitten back to Fluffs (OPNPCU, quest_fluffs.rs2:249-
        -- 259) -- she runs off home with her offspring, advancing to rescued.
        t.exec("goto-fluffsCat3", t.player.goto_tile, 3306, 3512, 1)
        local cat3 = t.player.by_symbol("npc", "gertrudescat")
        t.exec("giveKittenToFluffs", t.player.use_on, "gertrudekittens", cat3)
        t.ticks(3) -- same OPNPCU settle race as the milk and sardine steps
        t.exec("quest.stage.rescued", t.quest.expect_stage, "rescued")
        local kitten_left_result, kitten_left = t.inv.count("gertrudekittens")
        t.check("kittenGivenConsumed", kitten_left_result == "ok" and kitten_left == 0,
            "gertrudekittens after giving to Fluffs = " .. tostring(kitten_left) .. " (read " .. tostring(kitten_left_result) .. ")")

        -- Hand in to Gertrude (opnpc1,gertrude's %fluffs=^fluffs_rescued
        -- branch, gertrude.rs2:49-61) -- opens with the PLAYER's line (trap
        -- 18). ~fluffs_settle_rewards fires synchronously right after the
        -- last mesbox is dismissed: 1525 Cooking XP, a random pet-kitten
        -- colour, a Chocolate Cake and a Stew. Snapshot cooking XP before the
        -- hand-in, per the reward rule.
        local xp_snapshot_result, xp_snapshot = t.skill.snapshot()
        t.check("xpSnapshot", xp_snapshot_result == "ok", "skill.snapshot before hand-in -> " .. tostring(xp_snapshot_result))
        t.exec("goto-handInGertrude", t.player.goto_tile, 3151, 3410, 0)
        t.exec("handInGertrude", t.player.talk_to, "gertrude", 1)
        t.exec("handInGertrude-dialog", t.chat.play, {
            "player:Hello Gertrude. Fluffs ran off with her kitten.",
            "npc:You're back! Thank you! Thank you! Fluffs just came back! I think she was just upset as she couldn't find her kitten.",
            "mesbox:Gertrude gives you a hug.",
            "npc:If you hadn't found her kitten it would have died out there!",
            "player:That's okay, I like to do my bit.",
            "npc:I don't know how to thank you. I have no real material possessions. I do have kittens! I can only really look after one.",
            "player:Well, if it needs a home.",
            "npc:I would sell it to my cousin in West Ardougne. I hear there's a rat epidemic there. But it's too far.",
            "npc:Here you go, look after her and thank you again!",
            "mesbox:Gertrude gives you a kitten. And some food!",
        })
        t.ticks(3) -- fluffs_settle_rewards's own writes land behind the chat ack, same race as trap in section 8

        t.quest.expect_complete()

        -- Reward rows -- the literal grant quest_fluffs.rs2:326-350 makes,
        -- not a number read back from the scroll. The pet kitten is one of
        -- six random colours (~gertrude_give_cat's switch_int(random(6))),
        -- so the six documented colours are tried and exactly one must be
        -- present, then asserted by its own resolved name.
        local kitten_names = {
            "kittenobject", "kittenobject_light", "kittenobject_brown",
            "kittenobject_black", "kittenobject_browngrey", "kittenobject_bluegrey",
        }
        local kitten_seen, kitten_seen_count = nil, 0
        for i = 1, #kitten_names do
            local has_result, has = t.inv.has(kitten_names[i])
            if has_result == "ok" and has then
                kitten_seen = kitten_names[i]
                kitten_seen_count = kitten_seen_count + 1
            end
        end
        t.check("reward.kittenColour", kitten_seen_count == 1,
            "pet kitten reward: " .. tostring(kitten_seen) .. " present, "
                .. kitten_seen_count .. " of the six documented colours found")
        if kitten_seen then
            t.expect("reward.kitten", t.inv.expect_has(kitten_seen, 1))
        else
            t.expect("reward.kitten", "refused", "no pet-kitten colour found in inventory after hand-in")
        end
        t.expect("reward.chocolateCake", t.inv.expect_has("chocolate_cake", 1))
        t.expect("reward.stew", t.inv.expect_has("stew", 1))
        t.exec("reward.cookingXp", t.skill.expect_gain, "cooking", 1525, xp_snapshot)

        t.finish(0)
    end,
}
