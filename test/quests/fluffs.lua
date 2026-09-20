-- Gertrude's Cat (quest_fluffs). Hand-authored from the scaffold after two
-- rejections; see QUEUE.tsv's last_failure history for both.
--
-- Flow (areas/varrock/scripts/gertrude.rs2, quests/quest_fluffs/scripts/
-- quest_fluffs.rs2): talk to Gertrude and accept -> talk to Shilop, pay 100
-- coins for the lumber-mill tip -> find Fluffs (gertrudescat, a public map
-- npc, always spawned) and give her the milk -> pick doogle leaves, rub them
-- on a raw sardine (item-on-item, OPHELDU) to season it, feed Fluffs -> she
-- sends kittens mewing from one of six crates (%fluffs_crate = random(6),
-- quest_fluffs.rs2:124/248) -> give the found kitten back to Fluffs -> hand
-- in to Gertrude for the reward.
--
-- BLOCKED at "season the sardine": OPHELDU (one held item used on ANOTHER
-- held item, quest_fluffs.rs2:142-168's doogleleaves+raw_sardine recipe) has
-- no verb in this driver. t.player.use_on only accepts a world {kind=npc|
-- loc|obj} target (pointer.lua:1696-1698) -- ground objs, not backpack
-- slots -- and t.player.inv_op refuses a negative op outright, because that
-- is use_on's own internal arming half, not something a test can drive on
-- its own (pointer.lua:1504-1506, "op<0 is refused ... that is use_on's
-- arming half"). Nothing past ^fluffs_gave_milk is reachable without a
-- seasoned sardine, so the crate hunt, the kitten hand-off and the Gertrude
-- reward never get played -- reported, not driven around.
--
-- Everything up to there IS driven for real: Gertrude's accept dialogue,
-- paying Shilop the 100 coins (a real p_choice3 -> p_choice2 branch with the
-- coins actually deducted), walking upstairs to Fluffs and using the milk on
-- her (a real OPNPCU click, %fluffs actually advances).

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
        -- raw_sardine]) are now in the backpack. That recipe fires only on an
        -- item USED ON ANOTHER HELD ITEM -- OPHELDU -- and no verb in this
        -- driver can drive that click: use_on's own target check accepts only
        -- {kind=npc|loc|obj} (pointer.lua:1696-1698), and every one of those
        -- three resolves through a WORLD pool (api_drive.npcs/locs/objs,
        -- pointer.lua:357-367) that a backpack slot is never a member of --
        -- there is no {kind=...} a quest file could build to name "the raw
        -- sardine in slot N" as use_on's target. The other half of arming a
        -- Use interaction, inv_op's negative op, is reserved for use_on's own
        -- internals and refuses a caller outright before touching the world
        -- at all -- proven here with no side effect:
        local arm_result, arm_detail = t.player.inv_op("doogleleaves", -1)
        t.check("makeSeasonedSardine.no_item_on_item_verb", arm_result == "unsupported",
            "t.player.inv_op(\"doogleleaves\", -1) -> " .. tostring(arm_result) .. " " .. tostring(arm_detail)
                .. " -- the 'arm for Use' half use_on itself calls internally "
                .. "(pointer.lua:1504-1506,1721); no verb then exists to press a SECOND "
                .. "backpack slot while armed, which is what OPHELDU (item-on-item) needs")

        t.blocked("test/quests/fluffs.lua:makeSeasonedSardine -- no item-on-item (OPHELDU) verb "
            .. "exists in this driver; quest_fluffs.rs2:142-168's fluffs_make_sardine needs "
            .. "doogleleaves used ON raw_sardine (both backpack slots) to reach "
            .. "^fluffs_gave_sardine, and that is the only path past ^fluffs_gave_milk -- the "
            .. "crate hunt, kitten hand-off and Gertrude reward are all unreached behind it")
        return
    end,
}
