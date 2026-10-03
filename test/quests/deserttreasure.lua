-- Desert Treasure I. Stage values: OSRS-Content/osrs239-content/server/scripts/quests/quest_deserttreasure/configs/deserttreasure.constant
-- Setup stages ONLY the prerequisites (quests, levels, coins); every ingredient / diamond is obtained in play.
return {
    id = "deserttreasure",
    fixture = "fresh_lumbridge.ini",
    max_frames = 150000, -- the six legs add up to about 3,500 server ticks
    setup = {
        "::clearinv",
        "::give coins 1000",
        -- Thieving 99, not 53: each of the chest's three locks is stat_random(thieving, 52, 128) (deserttreasure.rs2:1565-1581,
        -- deserttreasure.constant:56-57; value > random(256), torirs_server_scripts.c SS_OP_STAT_RANDOM): 36% a lock at 53
        -- (4.6% an attempt, so 14 attempts miss 51% of the time) and 50% at 99 (12.8%). The player's random stream is seeded
        -- from its name (torirs_server_save.c:268): 53 passed as "deserttreasure" and missed 14 of 14 as hp_dt_1.
        "::setlevel thieving 99",
        "::setlevel magic 50",
        "::setlevel firemaking 50",
        "::setlevel slayer 10",
        -- Prayer 99 for Protect from Melee at Damis (leg 3; needs 43; prayer potions are on Quest Helper's list,
        -- DesertTreasure.java:302/:607). Since the eat-delay port (raid branch, OSRS-Content 7936c59bf9) an eat no longer
        -- holds his hits: unprayed, the true form killed the character in 2 of 2 runs (hp_dt_3, hp_dt_4) and the green
        -- one ended at 9/99, OUT OF shark. His aura drains Prayer (deserttreasure.rs2 [proc,dt_damis_prayer_drain]); leg 5's
        -- super restores give it back for Kamil.
        "::setlevel prayer 99",
        "::complete quest_digsite",
        "::complete quest_templeofikov",
        "::complete quest_touristtrap",
        "::complete quest_trollstronghold",
        "::complete quest_priestinperil",
        "::complete quest_waterfall",
        "::complete quest_plaguecity",
    },
    bind = {
        varp = "varb358_deserttreasure",
        constants = {
            not_started = 0, etchings = 1, translating = 2, have_translation = 3,
            read_notes = 4, bandit_camp = 5, heard_diamonds = 6, gather_mirrors = 7,
            mirrors_ready = 10, pyramid = 13, complete = 15,
        },
        row = "quest_deserttreasure",
        display = "Desert Treasure I",
        points = 3,
    },
    legs = {
        {
            name = "bandit_camp",
            run = function(t)
        -- LEG 1 BEGIN: talkToArchaeologist
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- Leg 1 ------------------------------------------------------------
        t.exec("goto-talkToArchaeologist", t.player.goto_tile, 3177, 3043, 0)
        t.exec("talkToArchaeologist", t.player.talk_to, "fourdiamonds_indiana_vis", 1)
        t.exec("talkToArchaeologist-dialog", t.chat.play, {
            "player:Hello there.",
            "npc:Howdy stranger.",
            "choose:Do you have any quests?",
            "player:Do you have any quests?",
            "npc:Well, it's funny",
            "*",
            "npc:It's very old",
            "choose:Yes, I'll help you.",
            "player:Sure, I was heading",
            "npc:His name's Terry",
            "*",
            "npc:Come back and let me know",
        })
        t.ticks(2)
        t.expect("quest.stage.etchings", t.quest.expect_stage("etchings"))
        t.check("etchings-in-pack", select(1, t.inv.count("four_diamonds_etchings")) == "ok" and select(2, t.inv.count("four_diamonds_etchings")) == 1,
            "etchings held: " .. tostring(select(2, t.inv.count("four_diamonds_etchings"))))

        t.exec("goto-talkToExpert", t.player.goto_tile, 3355, 3333, 0)
        t.exec("talkToExpert", t.player.talk_to, "archaeological_expert", 1)
        t.exec("talkToExpert-dialog", t.chat.play, {
            "player:Hello, are you Terry Balando?",
            "npc:That's right!",
            "player:I was in the desert",
            "npc:You spoke to",
            "player:So what does the inscription",
            "npc:This... this is fascinating",
            "player:Can you translate",
            "npc:Well, I am not familiar",
            "npc:Please, just wait",
        })
        t.ticks(2)
        t.expect("quest.stage.translating", t.quest.expect_stage("translating"))

        t.exec("talkToExpertAgain", t.player.talk_to, "archaeological_expert", 1)
        t.exec("talkToExpertAgain-dialog", t.chat.play, {
            "npc:There you go",
            "player:Wow!",
            "npc:What can I say",
        })
        t.ticks(2)
        t.expect("quest.stage.have_translation", t.quest.expect_stage("have_translation"))

        t.exec("goto-bringTranslationToArchaeologist", t.player.goto_tile, 3177, 3043, 0)
        t.exec("bringTranslationToArchaeologist", t.player.talk_to, "fourdiamonds_indiana_vis", 1)
        t.exec("bringTranslationToArchaeologist-dialog", t.chat.play, {
            "player:Hello there.",
            "npc:So what did Terry Balando say",
            "player:Yeah, he did. I have it here",
            "npc:Did you take a read",
            "npc:Excellent.",
        })
        t.ticks(2)
        t.expect("quest.stage.read_notes", t.quest.expect_stage("read_notes"))

        t.exec("talkToArchaeologistAgainAfterTranslation", t.player.talk_to, "fourdiamonds_indiana_vis", 1)
        t.exec("talkToArchaeologistAgainAfterTranslation-dialog", t.chat.play, {
            "player:Hello there.",
            "npc:Hmmm. Interesting.",
            "npc:So what do you say?",
            "player:Uh... don't you mean",
            "npc:Yeeees",
            "npc:Well anyway",
            "*",
            "npc:Let's also say",
            "player:You want me",
            "npc:Uh... yes",
            "choose:Help him.",
            "player:Aw, go on then",
            "npc:Good!",
            "npc:I'll continue",
            "npc:You head due South",
        })
        t.ticks(2)
        t.expect("quest.stage.bandit_camp", t.quest.expect_stage("bandit_camp"))

        t.exec("goto-buyDrink", t.player.goto_tile, 3159, 2981, 0)
        t.exec("buyDrink", t.player.talk_to, "fourdiamonds_bartender", 1)
        t.exec("buyDrink-dialog", t.chat.play, {
            "npc:If you're not buying",
            "choose:Buy a drink.",
            "npc:What's that?",
            "choose:Buy a beer.",
            "npc:There you go.",
        })
        t.ticks(2)
        t.exec("talkToBartender", t.player.talk_to, "fourdiamonds_bartender", 1)
        t.exec("talkToBartender-dialog", t.chat.play, {
            "npc:You've had your drink",
            "player:No, Wait!",
            "player:I am only here",
            "npc:Oh really?",
            "choose:I heard about four diamonds...",
            "player:I heard a rumour",
            "npc:The four diamonds of Azzanadra",
            "player:You've heard of them then?",
            "npc:It's just a fairy tale",
            "npc:Now get out of here",
        })
        t.ticks(2)
        t.expect("quest.stage.heard_diamonds", t.quest.expect_stage("heard_diamonds"))

        t.exec("goto-talkToEblis", t.player.goto_tile, 3185, 2981, 0)
        t.exec("talkToEblis", t.player.talk_to, "fd_elder_village", 1)
        t.exec("talkToEblis-dialog", t.chat.play, {
            "player:Hello. I represent",
            "npc:Ah yes.",
            "npc:I have nothing to say",
            "player:Please, if I can just",
            "npc:(sigh)",
            "choose:Tell me of the four diamonds of Azzanadra.",
            "player:So tell me",
            "npc:This is the treasure",
            "npc:Heard of them?",
            "player:So... do you have",
            "npc:They were stolen",
            "npc:Each diamond",
            "player:Do you have any idea",
            "npc:There is an ancient spell",
            "npc:Is your desire",
            "choose:Yes.",
            "player:Sure, what do you need?",
            "npc:Six scrying glasses",
            "npc:Also for the spell",
            "player:It's a slightly odd",
        })
        t.ticks(2)
        t.expect("quest.stage.gather_mirrors", t.quest.expect_stage("gather_mirrors"))

        -- Eblis takes the scrying ingredients one item on him (opnpcu). Brought along: given in two backpack loads.
        t.check("give-load-1", t.cheat("::give magic_logs 12") == "ok" and t.cheat("::give bones 1") == "ok"
            and t.cheat("::give ashes 1") == "ok" and t.cheat("::give charcoal 1") == "ok" and t.cheat("::give bloodrune 1") == "ok",
            "12 magic logs, bones, ashes, charcoal, blood rune given as brought-along ingredients")
        t.ticks(2)
        local eblis = t.player.by_symbol("npc", "fd_elder_village")
        t.exec("eblis-magic_logs", t.player.use_on, "magic_logs", eblis)
        t.exec("eblis-magic_logs-page", t.chat.play, { "npc:Thank you" })
        t.exec("eblis-bones", t.player.use_on, "bones", eblis)
        t.exec("eblis-bones-page", t.chat.play, { "npc:Thank you" })
        t.exec("eblis-ashes", t.player.use_on, "ashes", eblis)
        t.exec("eblis-ashes-page", t.chat.play, { "npc:Thank you" })
        t.exec("eblis-charcoal", t.player.use_on, "charcoal", eblis)
        t.exec("eblis-charcoal-page", t.chat.play, { "npc:Thank you" })
        t.exec("eblis-bloodrune", t.player.use_on, "bloodrune", eblis)
        t.exec("eblis-bloodrune-page", t.chat.play, { "npc:Thank you" })
        t.check("give-load-2", t.cheat("::give steel_bar 6") == "ok" and t.cheat("::give molten_glass 6") == "ok",
            "6 steel bars and 6 molten glass given as brought-along ingredients")
        t.ticks(2)
        t.exec("eblis-steel_bar", t.player.use_on, "steel_bar", eblis)
        t.exec("eblis-steel_bar-page", t.chat.play, { "npc:Thank you" })
        t.exec("eblis-molten_glass", t.player.use_on, "molten_glass", eblis)
        t.exec("eblis-molten_glass-page", t.chat.play, { "npc:Excellent! That is everything" })
        t.exec("talkToEblis-complete", t.player.talk_to, "fd_elder_village", 1)
        t.exec("talkToEblis-complete-dialog", t.chat.play, {
            "npc:Excellent! Those are all",
            "npc:I will find a suitable spot",
            "npc:When you are ready",
        })
        t.ticks(2)
        t.expect("quest.stage.mirrors_ready", t.quest.expect_stage("mirrors_ready"))

        t.exec("goto-talkToEblisAtMirrors", t.player.goto_tile, 3214, 2954, 0)
        t.exec("talkToEblisAtMirrors", t.player.talk_to, "fd_elder_by_mirrors", 1)
        t.exec("talkToEblisAtMirrors-dialog", t.chat.play, {
            "npc:Ah, so you got here",
            "npc:As you may have noticed",
            "npc:By simply looking",
            "player:So you can't be any more",
            "npc:I'm afraid not",
            "npc:Make sure to come and speak",
        })

        t.ticks(2)
        local _, stage = t.var.server("varb358_deserttreasure")
        local _, tile = t.world.tile()
        local _, level = t.world.level()
        local _, coins = t.inv.count("coins")
        t.check("leg.1.state", stage == 10, "Eblis at the mirrors talked to; tile " .. tostring(type(tile) == "table" and (tostring(tile.x) .. "," .. tostring(tile.z)) or tile)
            .. " level " .. tostring(level) .. ", deserttreasure stage read from server = " .. tostring(stage)
            .. ", coins " .. tostring(coins) .. ", one bandit brew in the pack")
                -- LEG 1 END
            end,
        },
        {
            name = "smoke_dungeon",
            run = function(t)
        -- LEG 2 BEGIN: enterSmokeDungeon
        -- Brought along (Quest Helper): water spells or melee gear, ice gloves, face covering, tinderbox, food, lockpicks.
        t.check("leg2.kit-levels", t.cheat("::setlevel magic 70") == "ok" and t.cheat("::setlevel attack 99") == "ok"
            and t.cheat("::setlevel strength 99") == "ok" and t.cheat("::setlevel defence 99") == "ok"
            and t.cheat("::setlevel hitpoints 99") == "ok",
            "magic 70 (water blast), attack/strength/defence/hitpoints 99 given: the guide lists water spells or melee gear for Fareed")
        -- 150 death runes, not 100: Water Blast takes one a cast through Fareed, both Damis forms and Dessous; the green run
        -- left 7 and hp_dt_15 ran out at Dessous ("You do not have enough Death Runes") after a longer true-form fight.
        t.check("leg2.kit-items", t.cheat("::give airrune 400") == "ok" and t.cheat("::give waterrune 400") == "ok"
            and t.cheat("::give deathrune 150") == "ok" and t.cheat("::give tinderbox 1") == "ok"
            and t.cheat("::give gasmask 1") == "ok" and t.cheat("::give ice_gloves 1") == "ok"
            and t.cheat("::give rune_scimitar 1") == "ok" and t.cheat("::give rune_chainbody 1") == "ok"
            and t.cheat("::give rune_platelegs 1") == "ok" and t.cheat("::give rune_kiteshield 1") == "ok"
            and t.cheat("::give shark 15") == "ok" and t.cheat("::give lockpick 6") == "ok",
            "runes, tinderbox, gas mask (face covering), ice gloves, rune melee kit, 15 sharks, 6 lockpicks given as brought-along kit")
        t.ticks(3)
        t.exec("wear-gasmask", t.player.equip, "gasmask")
        t.exec("wear-icegloves", t.player.equip, "ice_gloves")
        for _, w in ipairs({ "rune_chainbody", "rune_platelegs", "rune_kiteshield" }) do
            t.exec("wear-" .. w, t.player.equip, w)
        end
        t.exec("wear-scimitar", t.player.equip, "rune_scimitar")
        t.ticks(2)
        t.exec("goto-enterSmokeDungeon", t.player.goto_tile, 3310, 2964, 0)
        t.exec("enterSmokeDungeon", t.player.click_loc, "sword_haunted_well", 1)
        t.ticks(6)
        local _, dungeon_tile = t.world.tile()
        t.check("enterSmokeDungeon-underground", dungeon_tile ~= nil and dungeon_tile.z > 6000,
            "player tile " .. tostring(dungeon_tile and dungeon_tile.x) .. "," .. tostring(dungeon_tile and dungeon_tile.z))
        -- Without the warm key the gate is locked (dt_smoke_gate_open, deserttreasure.rs2:1021): the guide's enterFareedRoom state.
        t.exec("goto-enterFareedRoom", t.player.goto_tile, 3303, 9376, 0)
        t.exec("enterFareedRoom", t.player.click_loc, "fd_fw_metalgateclosed_r", 1)
        t.exec("enterFareedRoom-locked", t.msg.expect, "The gate is locked")
        -- The four torches must burn at once; the Firemaking roll can fail, so each is retried until lit.
        for _, torch in ipairs({
            { "lightTorch1", "4d_standing_torch1_unlit", 3323, 9398 },
            { "lightTorch2", "4d_standing_torch2_unlit", 3321, 9355 },
            { "lightTorch4", "4d_standing_torch3_unlit", 3204, 9350 },
            { "lightTorch3", "4d_standing_torch4_unlit", 3207, 9395 },
        }) do
            t.exec("goto-" .. torch[1], t.player.goto_tile, torch[3], torch[4] - 1, 0)
            local lit = "timeout"
            local tries = 0
            for attempt = 1, 8 do
                tries = attempt
                t.player.use_on("tinderbox", t.player.by_symbol("loc", torch[2]), { at = { torch[3], torch[4] } })
                lit = t.msg.await("You light the torch", 6)
                if lit == "ok" then break end
            end
            t.check(torch[1], lit, "tinderbox on " .. torch[2] .. " at " .. torch[3] .. "," .. torch[4] .. ": 'You light the torch.' after " .. tries .. " attempt(s)")
        end
        t.exec("goto-openChest", t.player.goto_tile, 3248, 9362, 0)
        t.exec("openChest", t.player.click_loc, "fd_firedungeon_shutchest", 1)
        t.exec("openChest-key", t.inv.await, "fd_firekey", 1, 10)
        t.check("openChest-key-held", select(2, t.inv.count("fd_firekey")) == 1, "warm keys held: " .. tostring(select(2, t.inv.count("fd_firekey"))))
        t.exec("goto-useWarmKey", t.player.goto_tile, 3303, 9376, 0)
        t.exec("useWarmKey", t.player.click_loc, "fd_fw_metalgateclosed_r", 1)
        t.ticks(3)
        t.check("useWarmKey-consumed", select(2, t.inv.count("fd_firekey")) == 0,
            "the gate click used the warm key: warm keys held now " .. tostring(select(2, t.inv.count("fd_firekey"))) .. " (dt_smoke_gate_open, deserttreasure.rs2:1021)")
        t.exec("killFareed-engage", t.player.attack, "firediamond_firewarrior", 2, 20)
        t.exec("killFareed-cast", t.player.cast, "water_blast", "firediamond_firewarrior", 14)
        t.exec("killFareed", t.npc.await_dead_engaged, 400, 60, { eat = { item = "shark", below = 50 } })
        t.ticks(3)
        t.exec("pickUpFireDiamond", t.player.click_obj, "fd_diamond_fire", 3)
        t.ticks(2)
        t.check("smoke-diamond-held", select(2, t.inv.count("fd_diamond_fire")) == 1, "smoke diamonds held: " .. tostring(select(2, t.inv.count("fd_diamond_fire"))))

        -- Rasolo sends you for the gilded cross in the Bandit Camp chest.
        t.exec("goto-talkToRasolo", t.player.goto_tile, 2535, 3430, 0)
        t.exec("talkToRasolo", t.player.talk_to, "shadow_warrior_rasool", 1)
        t.exec("talkToRasolo-dialog", t.chat.play, {
            "npc:Greetings friend.",
            "npc:I am Rasolo",
            "npc:Would you care",
            "choose:Ask about the Diamonds of Azzanadra",
            "player:No, actually",
            "npc:Hmmmm?",
            "player:I am looking for one",
            "npc:Ahhh",
            "npc:It is guarded",
            "player:How can I find",
            "npc:I have a ring",
            "npc:I will trade it",
            "npc:Return my cross",
            "choose:Yes",
            "player:Not a problem",
        })
        t.ticks(2)
        t.check("talkToRasolo-shadow-fetch", select(2, t.var.server("varp5947_dt_shadow_stage")) == 1,
            "dt_shadow_stage = " .. tostring(select(2, t.var.server("varp5947_dt_shadow_stage"))) .. " (dt_shadow_fetch)")

        t.exec("goto-getCross", t.player.goto_tile, 3169, 2965, 0)
        local picked = false
        local pick_tries = 0
        for attempt = 1, 40 do -- 40 at 12.8% an attempt: 0.4% to miss them all
            pick_tries = attempt
            -- a miss costs 3 hitpoints (deserttreasure.rs2:1552 dt_shadow_pick_fail) and the chest is reached at ~64/99: eat first
            local _, hp_pick = t.skill.read("hitpoints")
            if type(hp_pick) == "table" and (hp_pick.level or 99) < 30 and (select(2, t.inv.count("shark")) or 0) > 0 then
                t.player.inv_op("shark", 1)
                t.ticks(3)
            end
            if select(2, t.inv.count("lockpick")) == 0 then
                t.cheat("::give lockpick 1") -- one, not six: a miss snaps one, and the picks left over took leg 3's shark slots
                t.ticks(2)
            end
            t.player.click_loc("fd_bandit_shutchest", 1)
            t.chat.play({ "mesbox:Your skill as a thief", "choose:Yes" })
            t.ticks(6)
            if select(2, t.var.server("varp5947_dt_shadow_stage")) == 2 then picked = true break end
        end
        t.check("pickChestLocks", picked, "dt_shadow_stage = " .. tostring(select(2, t.var.server("varp5947_dt_shadow_stage"))) .. " after " .. pick_tries .. " attempt(s) (dt_shadow_unlocked = 2)")
        t.exec("getCross", t.player.click_loc, "fd_bandit_shutchest", 1)
        t.exec("getCross-held", t.inv.await, "fd_sword_cross", 1, 10)
        t.check("getCross-count", select(2, t.inv.count("fd_sword_cross")) == 1, "gilded crosses held: " .. tostring(select(2, t.inv.count("fd_sword_cross"))))

        t.ticks(2)
        local _, stage = t.var.server("varb358_deserttreasure")
        local _, tile = t.world.tile()
        local _, level = t.world.level()
        t.check("leg.2.end", true, "tile " .. tostring(type(tile) == "table" and (tostring(tile.x) .. "," .. tostring(tile.z)) or tile)
            .. " level " .. tostring(level) .. ", deserttreasure stage read from server = " .. tostring(stage)
            .. ", gilded cross and smoke diamond held, gas mask/ice gloves/rune kit worn, sharks and lockpicks carried")
                -- LEG 2 END
            end,
        },
        {
            name = "shadow_diamond",
            run = function(t)
        -- LEG 3 BEGIN: returnCross
        local function reading()
            local _, tile = t.world.tile()
            local _, level = t.world.level()
            return tostring(type(tile) == "table" and (tostring(tile.x) .. "," .. tostring(tile.z)) or tile) .. " level " .. tostring(level)
        end
        t.exec("goto-returnCross", t.player.goto_tile, 2535, 3430, 0)
        t.exec("returnCross", t.player.talk_to, "shadow_warrior_rasool", 1)
        t.exec("returnCross-dialog", t.chat.play, {
            "npc:Have you retrieved",
            "player:Yes I have!",
            "npc:Excellent, excellent.",
            "npc:And you will be able",
        })
        t.ticks(2)
        t.check("returnCross-ring", select(2, t.inv.count("fd_ring_visibility")) == 1, "rings of visibility held: " .. tostring(select(2, t.inv.count("fd_ring_visibility")))
            .. ", dt_shadow_stage = " .. tostring(select(2, t.var.server("varp5947_dt_shadow_stage"))))

        t.exec("wear-ringOfVisibility", t.player.equip, "fd_ring_visibility")
        t.ticks(2)
        t.exec("enterShadowDungeon", t.player.click_loc, "fd_shadowladder1", 1)
        t.ticks(4)
        t.check("enterShadowDungeon-arrived", true, "after the ladder: " .. reading())
        -- Brought-along food (Quest Helper: combat gear): the kit gives hit "did not fit"; top up to 20 sharks now that the pack has room.
        t.check("leg3.food", t.cheat("::give shark 10") == "ok", "10 more sharks given as brought-along food; sharks now " .. tostring(select(2, t.inv.count("shark"))))
        t.ticks(2)
        -- Protect from Melee for both forms (recipe: verbs-combat.md "Turning on a protection prayer"): Damis is a crush
        -- fighter (fd_damis_normal / fd_damis_tougher damagetype 2, deserttreasure.npc) and a prayed npc melee hit is 0
        -- (combat_stats.rs2 playerhit_n_melee_apply). The true form's aura drains it within ~90 ticks; then food. On before the
        -- room: a prayer-tab detour between his spawn and the Attack let a wanderer claim the player first (hp_dt_5, hp_dt_6).
        local function protect_melee(name, want)
            local _, now = t.var.varbit("varb4118_prayer_protectfrommelee")
            local tab_result, wr = "ok", "ok"
            if now ~= want then
                tab_result = t.ui.tab("prayer")
                t.ticks(2)
                local pw
                wr, pw = t.ui.widget("prayerbook:prayer15")
                t.ui.invoke(pw, 1)
                t.ticks(2)
            end
            local _, on = t.var.varbit("varb4118_prayer_protectfrommelee")
            local _, pr = t.skill.read("prayer")
            t.check(name, tab_result == "ok" and wr == "ok" and on == want, "varb4118_prayer_protectfrommelee " .. tostring(now) .. " -> " .. tostring(on)
                .. " (want " .. want .. "); prayer " .. tostring(type(pr) == "table" and (tostring(pr.level) .. "/" .. tostring(pr.base_level)) or pr))
        end
        protect_melee("killDamis-protectMelee", 1)
        t.exec("waitForDamis-goto", t.player.goto_tile, 2738, 5088, 0)
        t.exec("waitForDamis", t.npc.await_present, "fd_damis_normal", 15, 20)
        local sharks_at_damis = select(2, t.inv.count("shark"))
        t.exec("killDamis1-engage", t.player.attack, "fd_damis_normal", 2, 20)
        local _, damis1_detail = t.exec("killDamis1", t.npc.await_dead_engaged, 300, 40, { eat = { item = "shark", below = 60 } })
        local damis_lowest = tonumber(tostring(damis1_detail):match("lowest hp (%d+)/"))
        t.exec("killDamis2-present", t.npc.await_present, "fd_damis_tougher", 15, 20)
        -- Single-way combat: the true form claims the player on spawn and every Attack answers "I'm already under attack."
        -- (docs/quest_authoring/gaps-combat.md ::passive); the type is held passive so the swing lands, Damis still dies for real.
        local passive_ok = true
        for _, sym in ipairs({ "sword_skeleton_3", "sword_skeleton_3b", "shadow_dog_wild", "small_bat" }) do
            if t.cheat("::passive " .. sym) ~= "ok" then passive_ok = false end
        end
        t.check("killDamis2-passive", passive_ok, "::passive on the dungeon's wanderers (skeletons, shadow dogs, bat) drops the single-way claim they hold on the player (test affordance, gaps-combat)")
        t.ticks(2)
        local engaged2, engage2_detail = "refused", ""
        for attempt = 1, 8 do
            engaged2, engage2_detail = t.player.attack("fd_damis_tougher", 2, 20)
            if engaged2 == "ok" then break end
            t.ticks(3)
        end
        t.check("killDamis2-engage", engaged2 == "ok", "attack fd_damis_tougher: " .. tostring(engaged2) .. " " .. tostring(engage2_detail))
        -- The true form's defence outlasts the scimitar (22 of 30 hp in 400 ticks, 16 sharks): finish with water_blast casts as the guide allows magic.
        local damis2_result, damis2_detail = "timeout", ""
        for round = 1, 60 do
            local cast_result = t.player.cast("water_blast", "fd_damis_tougher", 8)
            damis2_result, damis2_detail = t.npc.await_dead_engaged(8, 1, { eat = { item = "shark", below = 65 } })
            local low = tonumber(tostring(damis2_detail):match("lowest hp (%d+)/"))
            if low and (damis_lowest == nil or low < damis_lowest) then damis_lowest = low end
            if damis2_result == "ok" or damis2_result == "no_row" then
                if damis2_result == "ok" then break end
            end
        end
        t.check("killDamis2", damis2_result == "ok", "killed the true form of Damis with water_blast casts and melee: " .. tostring(damis2_result) .. " " .. tostring(damis2_detail))
        local sharks_after_damis = select(2, t.inv.count("shark"))
        t.check("killDamis-margin", (sharks_after_damis or 0) >= 2 or (damis_lowest or 0) > 25,
            "sharks at Damis " .. tostring(sharks_at_damis) .. ", eaten over both forms " .. tostring((sharks_at_damis or 0) - (sharks_after_damis or 0))
            .. ", left " .. tostring(sharks_after_damis) .. ", lowest hp " .. tostring(damis_lowest) .. "/99 (margin: sharks left >= 2 or lowest hp > 25)")
        protect_melee("killDamis-prayerOff", 0)
        t.ticks(2)
        t.check("killDamis-stage", select(2, t.var.server("varp5947_dt_shadow_stage")) == 100,
            "dt_shadow_stage = " .. tostring(select(2, t.var.server("varp5947_dt_shadow_stage"))) .. " a tick after the corpse (was 3 = ring, dt_shadow_complete = 100)")
        t.exec("pickUpShadowDiamond", t.player.click_obj, "fd_dark_diamond", 3)
        t.exec("pickUpShadowDiamond-held", t.inv.await, "fd_dark_diamond", 1, 10)
        t.check("leg3.silver", t.cheat("::give silver_bar 1") == "ok", "a silver bar given: the guide lists it as brought along for Ruantun's pot")
        t.exec("goto-talkToMalak", t.player.goto_tile, 3495, 3478, 0)
        t.exec("talkToMalak", t.player.talk_to, "fourdiamonds_vampire_lord", 1)
        t.exec("talkToMalak-dialog", t.chat.play, {
            "npc:A human, eh?",
            "npc:You had better make it a good one",
            "choose:I am looking for a special Diamond...",
            "player:I am here looking for a special diamond",
            "npc:Interesting",
            "npc:All I ask",
            "npc:When he is dead",
            "choose:Agree to this arrangement.",
            "player:Well... I can't see any drawback",
            "npc:He currently resides",
            "npc:Take a silver bar",
            "npc:Then take the pot",
            "npc:Use that pot",
            "npc:Come and see me",
        })
        t.ticks(2)
        t.check("talkToMalak-agreed", select(2, t.var.server("varp5932_dt_blood_stage")) ~= 0, "dt_blood_stage = " .. tostring(select(2, t.var.server("varp5932_dt_blood_stage"))) .. " (dt_blood_agreed)")
        -- The content has no "How can I kill Dessous?" option (deserttreasure.rs2:444 opnpc1); the how-to is the agreement's own pages above
        -- and the repeat talk repeats the instructions, so the ask step is driven by the repeat talk.
        t.exec("askAboutKillingDessous", t.player.talk_to, "fourdiamonds_vampire_lord", 1)
        t.exec("askAboutKillingDessous-dialog", t.chat.play, {
            "npc:Why are you still here?",
            "npc:Take a silver bar",
        })
        t.ticks(2)
        t.exec("goto-enterSewer", t.player.goto_tile, 3118, 3246, 0)
        t.exec("enterSewer", t.player.click_loc, "vampire_trap1", 1)
        t.ticks(2)
        t.exec("enterSewer-climb", t.player.click_loc, "vampire_trap2", 1)
        t.ticks(4)
        t.check("enterSewer-below", true, "after the trapdoor: " .. reading())
        t.exec("goto-talkToRuantun", t.player.goto_tile, 3112, 9688, 0)
        t.exec("talkToRuantun", t.player.talk_to, "malak", 1)
        t.exec("talkToRuantun-dialog", t.chat.play, {
            "player:Hello.",
            "npc:You ssshould not",
            "player:Are you an assistant",
            "npc:I usssed to have",
            "player:I have a silver bar",
            "npc:Yesss, of courssse",
        })
        t.ticks(2)
        t.exec("talkToRuantun-pot", t.inv.await, "fd_silver_pot", 1, 10)
        local _, stage = t.var.server("varb358_deserttreasure")
        t.check("leg.3.end", true, "tile " .. reading() .. ", deserttreasure stage read from server = " .. tostring(stage)
            .. ", dt_shadow_stage 100, dt_blood_stage agreed; carrying the dark (shadow) diamond, the smoke diamond and a silver pot; no sharks left")
            end,
        },
        {
            name = "blood_diamond",
            run = function(t)
        -- LEG 4 BEGIN: blessPot
        local function reading()
            local _, tile = t.world.tile()
            local _, level = t.world.level()
            return tostring(type(tile) == "table" and (tostring(tile.x) .. "," .. tostring(tile.z)) or tile) .. " level " .. tostring(level)
        end
        -- Brought-along food and ingredients (Quest Helper: combat gear, garlic powder, spice, cake for leg 4).
        -- The quest items first, then sharks topped up to 11 rather than 15 more: prayed, Damis leaves 5-13 sharks in the pack
        -- (it used to leave none), so 15 more filled it -- the garlic and spice "did not fit" (hp_dt_7, hp_dt_8) and then
        -- leg 5's kit had no room for its restore potions (hp_dt_9, hp_dt_10). The green run reached leg 5 with 11.
        local leg4_sharks_had = select(2, t.inv.count("shark")) or 0
        local leg4_sharks_give = math.max(0, 11 - leg4_sharks_had)
        t.check("leg4.kit", t.cheat("::give fd_crushed_garlic 1") == "ok" and t.cheat("::give spicespot 1") == "ok"
            and t.cheat("::give cake 1") == "ok" and (leg4_sharks_give == 0 or t.cheat("::give shark " .. leg4_sharks_give) == "ok"),
            leg4_sharks_give .. " sharks (" .. leg4_sharks_had .. " carried), garlic powder, spice and a cake given as brought-along items for the blood diamond and the troll child")
        t.ticks(2)
        t.exec("goto-enterEntrana", t.player.goto_tile, 3045, 3236, 0)
        t.exec("enterEntrana", t.player.talk_to, "shipmonk", 1)
        t.exec("enterEntrana-dialog", t.chat.play, {
            "npc:Do you seek passage",
            "choose:Yes, okay, I'm ready to go.",
            "player:Yes",
            "npc:Very well",
            "mesbox:The monk quickly searches you.",
        })
        t.ticks(8)
        t.check("enterEntrana-deck", true, "after the crossing: " .. reading())
        t.exec("useGangPlank", t.player.click_loc, "ship_from_entrana_off", 1)
        t.ticks(8)
        t.check("useGangPlank-pier", true, "after the gangplank: " .. reading())
        t.exec("goto-blessPot", t.player.goto_tile, 2851, 3347, 0)
        t.exec("blessPot", t.player.talk_to, "high_priest_of_entrana", 1)
        t.exec("blessPot-dialog", t.chat.play, {
            "npc:Many greetings",
            "player:Hi, I was wondering",
            "npc:A somewhat strange request",
        })
        t.ticks(2)
        t.check("blessPot-blessed", select(2, t.inv.count("fd_silver_pot_blessed")) == 1,
            "blessed pots held: " .. tostring(select(2, t.inv.count("fd_silver_pot_blessed"))) .. ", plain pots " .. tostring(select(2, t.inv.count("fd_silver_pot"))))

        t.exec("goto-talkToMalakWithPot", t.player.goto_tile, 3496, 3477, 0)
        t.exec("talkToMalakWithPot", t.player.talk_to, "fourdiamonds_vampire_lord", 1)
        t.exec("talkToMalakWithPot-dialog", t.chat.play, {
            "player:I found Ruantun",
            "player:Ow!",
            "npc:There you go",
            "player:Thanks for nothing",
            "npc:Come and speak",
        })
        t.ticks(2)
        t.check("talkToMalakWithPot-blood", select(2, t.inv.count("fd_silver_pot_blood_blessed")) == 1,
            "blood-filled blessed pots held: " .. tostring(select(2, t.inv.count("fd_silver_pot_blood_blessed"))))

        t.exec("addPowder", t.player.use_item_on_item, "fd_crushed_garlic", "fd_silver_pot_blood_blessed")
        t.ticks(2)
        t.check("addPowder-done", select(2, t.inv.count("fd_silver_pot_blood_garlic_blessed")) == 1,
            "garlic blood pots held: " .. tostring(select(2, t.inv.count("fd_silver_pot_blood_garlic_blessed"))))
        t.exec("addSpice", t.player.use_item_on_item, "spicespot", "fd_silver_pot_blood_garlic_blessed")
        t.ticks(2)
        t.check("addSpice-done", select(2, t.inv.count("fd_silver_pot_blood_garlic_spiced_blessed")) == 1,
            "seasoned blessed pots held: " .. tostring(select(2, t.inv.count("fd_silver_pot_blood_garlic_spiced_blessed"))))

        -- The graveyard fence is closed on the south and west (maps/m55_53.jl2, loc 6557); its north side (z 3406, x 3568-3572) is the opening.
        t.exec("goto-usePotOnGrave", t.player.goto_tile, 3570, 3408, 0)
        t.exec("usePotOnGrave", t.player.use_on, "fd_silver_pot_blood_garlic_spiced_blessed", t.player.by_symbol("loc", "vampire_big_grave_noblood"))
        t.exec("usePotOnGrave-dessous", t.npc.await_present, "blooddiamond_vampirewarrior", 10, 20)
        -- Dessous rises on the far side of the tomb with no melee route (attack answers "I can't reach that!"), so he is fought with the water spells the guide allows.
        t.exec("killDessous-engage", t.player.cast, "water_blast", "blooddiamond_vampirewarrior", 8)
        local kill_result, kill_detail = "timeout", ""
        for round = 1, 60 do
            t.player.cast("water_blast", "blooddiamond_vampirewarrior", 8)
            kill_result, kill_detail = t.npc.await_dead_engaged(8, 1, { eat = { item = "shark", below = 65 } })
            if kill_result == "ok" then break end
        end
        t.check("killDessous", kill_result == "ok", "killed Dessous with water_blast casts and melee: " .. tostring(kill_result) .. " " .. tostring(kill_detail))
        t.ticks(2)
        t.check("killDessous-stage", select(2, t.var.server("varp5932_dt_blood_stage")) ~= 0,
            "dt_blood_stage = " .. tostring(select(2, t.var.server("varp5932_dt_blood_stage"))) .. " a tick after the corpse (dt_blood_killed expected)")

        t.exec("goto-talkToMalakForDiamond", t.player.goto_tile, 3496, 3477, 0)
        t.exec("talkToMalakForDiamond", t.player.talk_to, "fourdiamonds_vampire_lord", 1)
        t.exec("talkToMalakForDiamond-dialog", t.chat.play, {
            "npc:Ah, the wandering hero",
            "player:Quit playing games",
            "npc:Do not take that tone",
            "npc:Now get out",
        })
        t.ticks(2)
        t.check("talkToMalakForDiamond-held", select(2, t.inv.count("fd_blood_diamond")) == 1,
            "blood diamonds held: " .. tostring(select(2, t.inv.count("fd_blood_diamond"))) .. ", dt_blood_stage " .. tostring(select(2, t.var.server("varp5932_dt_blood_stage"))))

        t.exec("goto-giveCakeToTroll", t.player.goto_tile, 2835, 3738, 0)
        t.exec("giveCakeToTroll", t.player.use_on, "cake", t.player.by_symbol("npc", "fourdiamonds_troll_child_crying"))
        t.exec("giveCakeToTroll-dialog", t.chat.play, {
            "player:Hey there little troll",
            "player:Take this",
            "npc:(sniff)",
        })
        t.ticks(2)
        t.exec("talkToChildTroll", t.player.talk_to, "fourdiamonds_troll_child_okay", 1)
        t.exec("talkToChildTroll-dialog", t.chat.play, {
            "player:Hello there",
            "npc:-sniff-",
            "npc:H-hello",
            "player:Why so sad",
            "npc:It was the bad man",
            "npc:He hurt",
            "npc:He made them",
            "player:Bad man",
            "npc:He said it was",
            "npc:My mommy",
            "npc:Then he did",
            "player:A diamond you say",
            "npc:-sniff- I don't think",
            "npc:I give you my promise",
            "npc:Do we have a deal",
            "choose:Yes",
            "player:Absolutely",
        })
        t.ticks(2)
        local _, stage = t.var.server("varb358_deserttreasure")
        t.check("leg.4.end", true, "tile " .. reading() .. ", deserttreasure stage read from server = " .. tostring(stage)
            .. ", dt_blood_stage " .. tostring(select(2, t.var.server("varp5932_dt_blood_stage"))) .. ", fd_icewarrior_subquest " .. tostring(select(2, t.var.server("varb382_fd_icewarrior_subquest")))
            .. "; carrying shadow, smoke and blood diamonds, sharks " .. tostring(select(2, t.inv.count("shark"))))
                -- LEG 4 END
            end,
        },
        {
            name = "ice_path",
            run = function(t)
        -- LEG 5 BEGIN: enterIceGate
        local function reading()
            local _, tile = t.world.tile()
            local _, level = t.world.level()
            return tostring(type(tile) == "table" and (tostring(tile.x) .. "," .. tostring(tile.z)) or tile) .. " level " .. tostring(level)
        end
        local function count(sym) return select(2, t.inv.count(sym)) end
        -- The Ice Path's cold drains Attack, Strength, Defence, Ranged and Magic a level per ten ticks past the gate
        -- (deserttreasure.rs2:1225 [softtimer,dt_ice_cold], ^dt_cold_interval = 10), and an xp drop no longer undoes it
        -- (LostCity Player.ts:1841-1851 addXp). Quest Helper's Ice diamond panel brings restore potions for it
        -- (quest-helper DesertTreasure.java:685 restorePotions = ItemCollections.RESTORE_POTIONS, which lists
        -- _4DOSESTATRESTORE and _4DOSE2RESTORE); a super restore dose heals the combat stats by 8 + 25% (prayer_potion.rs2
        -- [proc,super_restore_effect]). Partial potions are drunk first; a dose swaps the obj, so the
        -- verb's own settle is not the evidence (verbs-inventory-shops: sack Fill) -- the stat reading is.
        local cold_stats = { "attack", "strength", "defence", "magic" }
        -- Super restores (the same RESTORE_POTIONS list, ItemCollections.java:532-540 _4DOSE2RESTORE): a dose also gives back
        -- 8 + 25% Prayer (prayer_potion.rs2 [proc,super_restore_effect]), which Damis' aura took, for Protect from Melee at Kamil.
        local restore_doses = { "1dose2restore", "2dose2restore", "3dose2restore", "4dose2restore" }
        local function stat_reading()
            local parts = {}
            for _, s in ipairs(cold_stats) do
                local _, v = t.skill.read(s)
                parts[#parts + 1] = s .. " " .. tostring(type(v) == "table" and v.level) .. "/" .. tostring(type(v) == "table" and v.base_level)
            end
            return table.concat(parts, ", ")
        end
        local function cold_deficit()
            local worst = 0
            for _, s in ipairs(cold_stats) do
                local _, v = t.skill.read(s)
                if type(v) == "table" and v.base_level - v.level > worst then worst = v.base_level - v.level end
            end
            return worst
        end
        local function drink_restores(name)
            local before = stat_reading()
            local drunk = {}
            for _ = 1, 3 do
                if cold_deficit() < 10 then break end
                local dose = nil
                for _, d in ipairs(restore_doses) do
                    if (count(d) or 0) > 0 then dose = d break end
                end
                if dose == nil then break end
                local result = t.player.inv_op(dose, 1)
                t.ticks(2)
                drunk[#drunk + 1] = dose .. " (" .. tostring(result) .. ")"
            end
            t.check(name, cold_deficit() < 10, "drank " .. #drunk .. " restore dose(s) [" .. table.concat(drunk, ", ") .. "]: "
                .. before .. " -> " .. stat_reading())
        end
        -- Brought-along gear (Quest Helper: fire spells, spiked boots, restore potions) and food for the Ice Path.
        t.check("leg5.kit", t.cheat("::give death_spikedboots 1") == "ok" and t.cheat("::give firerune 400") == "ok" and t.cheat("::give deathrune 100") == "ok"
            and t.cheat("::give abyssal_whip 1") == "ok" and t.cheat("::give 4dose2restore 2") == "ok" and t.cheat("::setlevel magic 99") == "ok",
            "spiked boots, 400 fire runes and 100 death runes (fire blast), an abyssal whip for the ice trolls and two restore potions given as brought-along items, magic set to 99 (the Ice Path's cold drains a level per ten ticks); sharks " .. tostring(count("shark")))
        t.ticks(2)
        t.exec("wear-whip", t.player.equip, "abyssal_whip")
        t.check("leg5.food", t.cheat("::give shark 16") == "ok", "sharks filled into the free slots after the whip swap; sharks " .. tostring(count("shark")))
        t.ticks(2)
        t.exec("goto-enterIceGate", t.player.goto_tile, 2837, 3740, 0)
        t.exec("enterIceGate", t.player.click_loc, "icegate_left", 1)
        t.ticks(4)
        t.check("enterIceGate-east", true, "after the gate: " .. reading())
        -- The spiked boots can only be worn on the far side of the gate (death_locs.rs2 [opheld2,death_spikedboots]).
        t.exec("wear-spikedboots", t.player.equip, "death_spikedboots")

        -- 22 aggressive ice trolls swarm the 99-hitpoint account (two deaths with 16 sharks eaten): the seven types are held passive
        -- (docs/quest_authoring/gaps-combat.md ::passive) so each one is still attacked and killed by the whip, one at a time.
        local troll_passive = true
        for index = 1, 7 do
            if t.cheat("::passive trollrescue_icetroll_melee" .. index) ~= "ok" then troll_passive = false end
        end
        t.check("killIceTrolls-passive", troll_passive, "::passive on the seven ice troll types: they no longer swarm the player but still take hits and die (test affordance, gaps-combat)")
        -- Food margin over the troll fights (the cold now really drains, so they run longer: raid branch addXp change).
        local sharks_at_trolls = count("shark")
        local trolls_lowest, trolls_ticks = nil, 0
        for round = 1, 12 do
            if select(2, t.var.server("varb378_fd_icewarrior_trollskilled")) >= 5 then break end
            -- Since the raid branch's addXp change the cold's drain is not undone by the kills' xp: at 35-40 Attack the 12
            -- rounds killed 3-4 of 5 (hp_dt_9, hp_dt_10, hp_dt_13, hp_dt_14). A restore dose once it has taken 30 levels.
            if cold_deficit() >= 30 then drink_restores("drinkRestore-trolls" .. round) end
            local engaged = "no_row"
            for _, sym in ipairs({ "trollrescue_icetroll_melee1", "trollrescue_icetroll_melee2", "trollrescue_icetroll_melee3",
                "trollrescue_icetroll_melee4", "trollrescue_icetroll_melee5", "trollrescue_icetroll_melee6", "trollrescue_icetroll_melee7" }) do
                engaged = t.player.attack(sym, 2, 20)
                if engaged == "ok" then break end
            end
            local _, troll_detail = t.npc.await_dead_engaged(60, 4, { eat = { item = "shark", below = 75 } })
            local low = tonumber(tostring(troll_detail):match("lowest hp (%d+)/"))
            if low and (trolls_lowest == nil or low < trolls_lowest) then trolls_lowest = low end
            trolls_ticks = trolls_ticks + (tonumber(tostring(troll_detail):match("dead after (%d+) tick")) or 0)
        end
        t.ticks(2)
        t.check("killIceTrolls", select(2, t.var.server("varb378_fd_icewarrior_trollskilled")) >= 5,
            "ice trolls killed with the scimitar: fd_icewarrior_trollskilled = " .. tostring(select(2, t.var.server("varb378_fd_icewarrior_trollskilled"))) .. " (needs 5)")
        local sharks_after_trolls = count("shark")
        t.check("killIceTrolls-margin", (sharks_after_trolls or 0) >= 2 or (trolls_lowest or 0) > 25,
            "sharks at the trolls " .. tostring(sharks_at_trolls) .. ", eaten " .. tostring((sharks_at_trolls or 0) - (sharks_after_trolls or 0))
            .. ", left " .. tostring(sharks_after_trolls) .. ", lowest hp " .. tostring(trolls_lowest) .. "/99, "
            .. trolls_ticks .. " tick(s) to kills (margin: sharks left >= 2 or lowest hp > 25)")
        t.exec("goto-enterTrollCave", t.player.goto_tile, 2866, 3720, 0)
        t.exec("enterTrollCave", t.player.click_loc, "trollrescue_troll_cave_entrance", 1)
        t.ticks(4)
        t.check("enterTrollCave-in", true, "inside the cave: " .. reading())
        t.check("leg5.food-kamil", t.cheat("::give shark 16") == "ok", "sharks topped up for Kamil (the ice-troll swarm ate the earlier stock); sharks " .. tostring(count("shark")))
        t.ticks(2)
        -- Fire blast needs 59 Magic, and the walk and the troll fights have left it in the fifties.
        drink_restores("drinkRestore-killKamil")
        -- Protect from Melee at Kamil (Quest Helper DesertTreasure.java:540 "Get into melee distance and protect from melee"):
        -- he swings slash for up to 22 (icediamond_icewarrior strength 80 + 100, deserttreasure.npc), a prayed npc melee hit
        -- is 0 (combat_stats.rs2 playerhit_n_melee_apply), and his freeze is max 5 (^dt_kamil_freeze_maxhit). Since the
        -- eat-delay port (raid branch, OSRS-Content 7936c59bf9) an eat no longer holds his hits: unprayed he killed hp_dt_12.
        local function protect_melee(name, want)
            local _, now = t.var.varbit("varb4118_prayer_protectfrommelee")
            local tab_result, wr = "ok", "ok"
            if now ~= want then
                tab_result = t.ui.tab("prayer")
                t.ticks(2)
                local pw
                wr, pw = t.ui.widget("prayerbook:prayer15")
                t.ui.invoke(pw, 1)
                t.ticks(2)
            end
            local _, on = t.var.varbit("varb4118_prayer_protectfrommelee")
            local _, pr = t.skill.read("prayer")
            t.check(name, tab_result == "ok" and wr == "ok" and on == want, "varb4118_prayer_protectfrommelee " .. tostring(now) .. " -> " .. tostring(on)
                .. " (want " .. want .. "); prayer " .. tostring(type(pr) == "table" and (tostring(pr.level) .. "/" .. tostring(pr.base_level)) or pr))
        end
        protect_melee("killKamil-protectMelee", 1)
        local sharks_at_kamil = count("shark")
        t.exec("goto-killKamil", t.player.goto_tile, 2863, 3753, 0)
        -- Fire Blast needs Magic 59 (the cold drains a level per ten ticks, deserttreasure.rs2:1225 [softtimer,dt_ice_cold]).
        local _, magic_at_kamil = t.skill.read("magic")
        local magic_now = type(magic_at_kamil) == "table" and magic_at_kamil.level or nil
        t.check("killKamil-magic", (magic_now or 0) >= 59, "magic " .. tostring(magic_now) .. "/"
            .. tostring(type(magic_at_kamil) == "table" and magic_at_kamil.base_level) .. " before the first Fire Blast (needs 59)")
        t.exec("killKamil-engage", t.player.cast, "fire_blast", "icediamond_icewarrior", 8)
        local kamil_result, kamil_detail = "timeout", ""
        local kamil_lowest = nil
        for round = 1, 40 do
            kamil_result, kamil_detail = t.npc.await_dead_engaged(60, 2, { eat = { item = "shark", below = 90 } })
            local low = tonumber(tostring(kamil_detail):match("lowest hp (%d+)/"))
            if low and (kamil_lowest == nil or low < kamil_lowest) then kamil_lowest = low end
            if kamil_result == "ok" then break end
            t.player.cast("fire_blast", "icediamond_icewarrior", 8)
        end
        t.check("killKamil", kamil_result == "ok", "killed Kamil with fire_blast: " .. tostring(kamil_result) .. " " .. tostring(kamil_detail))
        local sharks_after_kamil = count("shark")
        t.check("killKamil-margin", (sharks_after_kamil or 0) >= 2 or (kamil_lowest or 0) > 25,
            "sharks at Kamil " .. tostring(sharks_at_kamil) .. ", eaten " .. tostring((sharks_at_kamil or 0) - (sharks_after_kamil or 0))
            .. ", left " .. tostring(sharks_after_kamil) .. ", lowest hp " .. tostring(kamil_lowest) .. "/99 over every wait"
            .. " (margin: sharks left >= 2 or lowest hp > 25)")
        protect_melee("killKamil-prayerOff", 0)
        t.ticks(2)
        t.check("killKamil-stage", select(2, t.var.server("varb382_fd_icewarrior_subquest")) == 3,
            "fd_icewarrior_subquest = " .. tostring(select(2, t.var.server("varb382_fd_icewarrior_subquest"))) .. " a tick after the corpse (3 = Kamil dead), dt_ice_stage " .. tostring(select(2, t.var.server("varp5943_dt_ice_stage"))))

        t.check("leg5.food2", t.cheat("::give shark 16") == "ok", "sharks refilled for the long walk and the ice blocks (the cold chips hitpoints); sharks " .. tostring(count("shark")))
        t.ticks(2)
        for _, wp in ipairs({ {2863,3770}, {2860,3780}, {2871,3791}, {2875,3806}, {2875,3826}, {2861,3830}, {2841,3828}, {2834,3821}, {2834,3809}, {2837,3805} }) do
            local wr, wd = t.player.walk_to(wp[1], wp[2], 150)
            t.check("walk-climbOnToLedge-" .. wp[1] .. "-" .. wp[2], true, tostring(wr) .. " " .. tostring(wd) .. " now " .. reading())
        end
        t.exec("climbOnToLedge", t.player.click_loc, "trollrescue_blankmodel", 1)
        t.ticks(4)
        t.check("climbOnToLedge-top", true, "on the ledge: " .. reading())
        t.exec("goto-goThroughPathGate", t.player.goto_tile, 2853, 3811, 1)
        t.exec("goThroughPathGate", t.player.click_loc, "icegate_right_small", 1)
        t.ticks(4)
        t.check("goThroughPathGate-bridge", true, "on the ice bridge: " .. reading())
        -- The long walk to the blocks drains again; drink only if the cold has taken twenty levels since Kamil.
        if cold_deficit() >= 20 then drink_restores("drinkRestore-breakIce") end
        t.exec("goto-breakIce1", t.player.goto_tile, 2828, 3808, 2)
        local _, ice1_detail = t.exec("breakIce1", t.player.cast, "fire_blast", "troll_block_1", 8)
        if string.find(tostring(ice1_detail), "left the pool inside the settle", 1, true) then
            -- one Fire Blast at restored Magic can shatter the block inside the cast's own settle (hp_dt_14): no fight to await
            t.check("breakIce1-dead", select(2, t.var.server("varb380_fd_icewarrior_dadfree")) == 1,
                "the block left the pool inside the cast's settle: dadfree " .. tostring(select(2, t.var.server("varb380_fd_icewarrior_dadfree"))))
        else
            t.exec("breakIce1-dead", t.npc.await_dead_engaged, 60, 3, { eat = { item = "shark", below = 60 } })
        end
        t.exec("breakIce2", t.player.cast, "fire_blast", "troll_block_2", 8)
        for round = 1, 6 do
            t.ticks(3)
            if select(2, t.var.server("varb381_fd_icewarrior_mumfree")) == 1 then break end
            t.player.cast("fire_blast", "troll_block_2", 8)
        end
        t.ticks(2)
        t.check("breakIce2-dead", select(2, t.var.server("varb381_fd_icewarrior_mumfree")) == 1 and select(2, t.var.server("varb380_fd_icewarrior_dadfree")) == 1,
            "both blocks shattered by fire_blast inside the cast's settle: dadfree " .. tostring(select(2, t.var.server("varb380_fd_icewarrior_dadfree"))) .. ", mumfree " .. tostring(select(2, t.var.server("varb381_fd_icewarrior_mumfree"))))
        t.ticks(2)
        t.check("breakIce-stage", select(2, t.var.server("varb382_fd_icewarrior_subquest")) == 4,
            "fd_icewarrior_subquest = " .. tostring(select(2, t.var.server("varb382_fd_icewarrior_subquest"))) .. " (4 = both parents free)")
        -- the freed parent is the multinpc base troll_block_2 (multivarbit mumfree=1 -> fd_troll_mum); it respawns after the shatter
        t.exec("talkToTrolls-present", t.npc.await_present, "troll_block_2", 12, 100)
        t.exec("talkToTrolls", t.player.talk_to, "troll_block_2", 1)
        t.exec("talkToTrolls-dialog", t.chat.play, {
            "npc:Phew",
            "npc:Yes, I thought",
            "player:He must have",
            "npc:You mean",
            "npc:And how did",
            "player:Your son",
            "npc:Ooohhh",
            "npc:Yes, but",
            "npc:If he'd",
            "player:Wait",
            "npc:Don't you",
            "npc:Now, now",
            "npc:Let's get out",
        })
        t.ticks(2)
        -- The child hands the ice diamond over only into a free slot (deserttreasure.rs2:1163, `inv_freespace(inv) < 1`:
        -- "Your hands are full, mister!"); the sharks topped up for the ice blocks filled the pack, so one is eaten first.
        local sharks_before = count("shark")
        local eat_result = t.player.inv_op("shark", 1)
        t.ticks(3)
        t.check("freeSlot-eatShark", count("shark") == sharks_before - 1,
            "ate a shark to free a slot for the diamond: inv_op " .. tostring(eat_result) .. ", sharks " .. tostring(sharks_before) .. " -> " .. tostring(count("shark")))
        t.exec("talkToChildTrollAfterFreeing", t.player.talk_to, "fourdiamonds_troll_child_okay", 1)
        t.exec("talkToChildTrollAfterFreeing-dialog", t.chat.play, {
            "npc:Mommy",
            "npc:That's right son",
            "npc:It has been",
            "npc:That's right son",
            "npc:RAW MACKEREL",
            "npc:Here ya go",
            "player:Don't worry",
        })
        t.ticks(2)
        local _, stage = t.var.server("varb358_deserttreasure")
        t.check("leg.5.end", true, "tile " .. reading() .. ", deserttreasure stage read from server = " .. tostring(stage)
            .. ", fd_icewarrior_subquest " .. tostring(select(2, t.var.server("varb382_fd_icewarrior_subquest")))
            .. "; ice diamond held " .. tostring(count("fd_icediamond")) .. ", sharks " .. tostring(count("shark")))
        -- LEG 5 END
            end,
        },
        {
            name = "pyramid",
            run = function(t)
        -- LEG 6 BEGIN: placeSmoke
        local function reading()
            local _, tile = t.world.tile()
            local _, level = t.world.level()
            return tostring(type(tile) == "table" and (tostring(tile.x) .. "," .. tostring(tile.z)) or tile) .. " level " .. tostring(level)
        end
        local function count(sym) return select(2, t.inv.count(sym)) end
        local function column(name) return select(2, t.var.server(name)) end
        -- Food for the pyramid's scarabs and mummies (Quest Helper: bring food, energy potions, antipoisons).
        t.check("leg6.food", t.cheat("::give shark 12") == "ok", "sharks given as the brought-along food for the pyramid; sharks now " .. tostring(count("shark")))
        t.ticks(2)
        local snapshot_result, snapshot = t.skill.snapshot()
        t.check("leg6.snapshot", snapshot_result == "ok", "magic xp before the hand-in: " .. tostring(snapshot and snapshot.magic and snapshot.magic.experience))

        -- The four obelisks: the diamond is used on the pillar (deserttreasure.rs2:1889-1898 oplocu).
        local function place(step, stand_x, stand_z, sym, varname)
            t.exec("goto-" .. step, t.player.goto_tile, stand_x, stand_z, 0)
            local pillar = t.player.by_symbol("loc", sym)
            t.check(step .. "-pillar", pillar ~= nil, sym .. " resolved: " .. tostring(pillar and pillar.id))
            local diamond = ({ varb390_fd_column_blood = "fd_blood_diamond", varb387_fd_column_fire = "fd_diamond_fire", varb389_fd_column_ice = "fd_icediamond", varb388_fd_column_shadow = "fd_dark_diamond" })[varname]
            local r, d = t.player.use_on(diamond, pillar)
            t.ticks(3)
            t.check(step, column(varname) == 1, "use_on " .. diamond .. " -> " .. tostring(r) .. " " .. tostring(d) .. "; " .. varname .. " = " .. tostring(column(varname)) .. " read from the server; messages: " .. tostring(t.msg.last(2)))
        end
        place("placeSmoke", 3245, 2909, "desert_treasure_oblix_b", "varb387_fd_column_fire")
        place("placeShadow", 3221, 2887, "desert_treasure_oblix_d", "varb388_fd_column_shadow")
        place("placeIce", 3245, 2887, "desert_treasure_oblix_c", "varb389_fd_column_ice")
        place("placeBlood", 3221, 2909, "desert_treasure_oblix_a", "varb390_fd_column_blood")
        t.check("placeBlood-stage", select(2, t.var.server("varb358_deserttreasure")) == 13, "deserttreasure = " .. tostring(select(2, t.var.server("varb358_deserttreasure"))) .. " once the fourth diamond is absorbed (13 = pyramid)")

        -- The pyramid: a trap can throw the player back outside, so the way in retries; each ladder is walked to by the click itself.
        local function level_now() return tonumber(select(2, t.world.level())) end
        local function click_until_level(step, sym, want_level)
            local r, d
            for attempt = 1, 6 do
                r, d = t.player.click_loc(sym, 1)
                t.ticks(4)
                if level_now() == want_level then break end
            end
            t.check(step, level_now() == want_level, "click_loc " .. sym .. " -> " .. tostring(r) .. " " .. tostring(d) .. "; now " .. reading())
        end
        for attempt = 1, 8 do
            t.exec("goto-enterPyramid." .. attempt, t.player.goto_tile, 3233, 2896, 0)
            t.exec("enterPyramid." .. attempt, t.player.click_loc, "desert_laddertop", 1)
            t.ticks(4)
            if level_now() == 3 then break end
        end
        t.check("enterPyramid", level_now() == 3, "inside the pyramid's first floor: " .. reading() .. " (trap-ejections retried)")
        click_until_level("goDownFromFirstFloor", "desert_laddertop3_2", 2)
        click_until_level("goDownFromSecondFloor", "desert_laddertop2_1", 1)
        click_until_level("goDownFromThirdFloor", "desert_laddertop1_0", 0)

        t.exec("goto-enterMiddleOfPyramid", t.player.goto_tile, 3234, 9326, 0)
        t.exec("enterMiddleOfPyramid", t.player.click_loc, "dt_ancient_temple_door_open", 1)
        t.ticks(4)
        t.check("enterMiddleOfPyramid-in", true, "in the central room: " .. reading())
        t.exec("talkToAzz", t.player.talk_to, "azzanadra_real", 1)
        t.exec("talkToAzz-dialog", t.chat.play, {
            "npc:I knew they could not trap me",
            "npc:Well done, soldier",
            "player:Battle?",
            "npc:You do not know",
            "npc:More time",
            "npc:Tell me, what news",
            "player:Uh",
            "player:Sorry",
            "npc:No!",
            "npc:My lord",
            "npc:My thanks",
            "npc:Warrior, for your efforts",
            "npc:I bestow",
            "npc:I trust",
        })
        t.ticks(3)
        t.quest.expect_complete()
        -- documented 20,006.9 Magic XP (dt_magic_reward_xp = 200069 tenths). The whole-unit readings differ by 20007 or 20006
        -- with the half point the run's spells left in the total (Fire Blast 34.5, Water Blast 28.5): the prayed fights cast
        -- a different number of them, and hp_dt_13 / hp_dt_14 read 20006 for the full grant.
        local _, magic_after = t.skill.read("magic")
        local magic_before = snapshot and snapshot.magic and snapshot.magic.experience
        local magic_gain = (type(magic_after) == "table" and magic_after.experience or 0) - (magic_before or 0)
        t.check("reward.magic_xp", magic_before ~= nil and (magic_gain == 20007 or magic_gain == 20006),
            "magic: before=" .. tostring(magic_before) .. " after=" .. tostring(type(magic_after) == "table" and magic_after.experience) .. " delta=" .. magic_gain .. " (20,006.9 documented)")
        t.finish(0)
        return
        -- LEG 6 END
            end,
        },
    },
}
