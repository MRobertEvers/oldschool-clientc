-- The Tourist Trap (desertrescue). Driven end to end through real clicks and
-- dialogue: Irena -> the Mercenary Captain's duel -> the camp gate -> the
-- clothes slave -> the mine -> the cave guard's pineapple hint -> Al Shabim's
-- pineapple deal -> Captain Siad's chest (via the bookcase/sailing distraction)
-- -> the Bedabin tent's anvil (dart tip) and feathers (dart) -> Al Shabim's
-- darts+pineapple -> the cave guard's pineapple -> the deep mine -> Ana ->
-- the mine cart -> the winch -> the flatback cart -> the driver -> Irena's
-- reward. No stage is ever cheated with ::setvar; setup only stages the
-- guide's bring-along items (desert clothes, bronze bars, hammer, feathers)
-- and skill levels (none of which is the quest's own deliverable).
--
-- Content source for every symbol/text/stage number below:
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_desertrescue/
--   scripts/{mercenary_captain,mining_slave,mining_camp_gate,camp_guard,
--   al_shabim,captain_siad,bedabin_nomad_guard,mine_cart_driver,ana,irena,
--   quest_desertrescue}.rs2, configs/desertrescue.constant.
--
-- The "randompunish" insult ladder the queue's last_failure named is
-- mercenary_captain.rs2's ARREST branch (a wrong dialogue choice) -- the
-- ladder driven below is the clean duel path and never reaches it.

return {
    id = "desertrescue",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::give desert_shirt 1",
        "::give desert_robe 1",
        "::give desert_boots 1",
        "::give bronze_bar 3",
        "::give hammer 1",
        "::give feather 50",
        "::setlevel fletching 99",
        "::setlevel smithing 99",
        "::setlevel agility 99",
        "::setlevel thieving 99",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::passive tourtrap_qip_desert_mining_merc_1",
        "::passive tourtrap_qip_desert_mining_merc_2",
        "::passive tourtrap_qip_desert_mining_merc_3",
        "::passive tourtrap_qip_desert_mining_merc_4",
        "::passive desert_wolf",
        "::passive desert_wolf2",
        "::passive desert_wolf3",
        "::passive slave_rowdy",
    },

    run = function(t)
        local br, bd = t.quest.bind({
            varp = "varp197_desertrescue",
            constants = { not_started = 0, complete = 30 },
            row = "quest_touristtrap",
            display = "Tourist Trap",
            points = 2,
        })
        t.step("quest.bind", br == "ok" and "PASS" or "FAIL", bd)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ================= Irena: start the quest =================
        t.exec("goto-talkToIrena", t.player.goto_tile, 3303, 3111, 0)
        t.exec("talkToIrena", t.player.talk_to, "tourtrap_qip_irena_multi_sad", 1)
        t.ticks(1)
        t.exec("talkToIrena-dialog", t.chat.play, {
            "*",
            "*",
            "choose:What's the matter?",
            "*",
            "*",
            "choose:What did she go into the desert for?",
            "*",
            "*",
            "*",
            "*",
            "choose:I'll look for your daughter.",
            "*",
            "*",
            "choose:Okay Irena, calm down. I'll get your daughter back for you.",
            "*",
            "*",
        })
        t.exec("quest.stage.started", t.var.await_server, "varp197_desertrescue", 1, 15)

        -- ================= The Mercenary Captain's duel =================
        t.exec("goto-talkToCaptain", t.player.goto_tile, 3270, 3029, 0)
        t.exec("talkToCaptain", t.player.talk_to, "desertminingcaptain", 1)
        t.ticks(1)
        t.exec("talkToCaptain-dialog", t.chat.play, {
            "*",
            "choose:Wow! A real captain!",
            "*",
            "*",
            "choose:I'd love to work for a tough guy like you!",
            "*",
            "*",
            "choose:Can't I do something for a strong Captain like you?",
            "*",
            "*",
            "*",
            "*",
            "choose:Sorry Sir, I don't think I can do that.",
            "*",
            "*",
            "choose:It's a funny captain who can't fight his own battles!",
            "*",
            "*",
            "*",
            "*",
        })
        t.exec("quest.stage.approached_captain", t.var.await_server, "varp197_desertrescue", 3, 15)
        t.exec("talkToCaptain-attack", t.player.attack, "desertminingcaptain", 2, 20)
        t.exec("talkToCaptain-dead", t.npc.await_dead_engaged, 90, 8)
        t.exec("quest.stage.killed_capt", t.var.await_server, "varp197_desertrescue", 4, 15)
        t.exec("talkToCaptain-key", t.inv.await, "metal_key", 1, 5)

        -- ================= Into the camp; the clothes slave =================
        t.exec("goto-enterCamp", t.player.goto_tile, 3272, 3029, 0)
        t.exec("enterCamp", t.player.click_loc, "miningcampgateclosedl", 1)
        t.exec("quest.stage.entered_camp", t.var.await_server, "varp197_desertrescue", 5, 15)

        t.exec("goto-talkToSlave", t.player.goto_tile, 3302, 3025, 0)
        t.exec("talkToSlave", t.player.talk_to, "tourtrap_qip_mineslave_clothes_multi", 1)
        t.ticks(1)
        t.exec("talkToSlave-dialog-1", t.chat.play, {
            "*",
            "choose:I've just arrived.",
            "*",
            "*",
            "choose:Oh yes, that sounds interesting.",
            "*",
            "*",
            "choose:What's that then?",
            "*",
            "*",
            "choose:I can try to undo them for you.",
            "*",
            "*",
            "*",
            "choose:It's funny you should say that...",
            "*",
            "*",
            "*",
            "*",
            "*",
            "*",
            "*",
            "*",
            "*",
            "*",
            "choose:Yeah, okay, let's give it a go.",
            "*",
            "*",
            "*",
        })
        -- The lock pick is a Thieving roll (stat_random); Thieving 99 nearly
        -- always succeeds, but the content's own retry idiom on a failure is
        -- "Yeah I'll give it another go." inside the same open dialogue (no
        -- if_close), so drain to whichever options page follows (the trade
        -- menu on success, the retry prompt on failure) and retry up to 4
        -- times before giving the trade choice itself.
        local tradeRow
        for i = 1, 4 do
            t.chat.drain({ stop_at = "options", shots = false, max_pages = 10 })
            local _, orows = t.chat.options()
            local retryRow
            for _, row in ipairs(orows or {}) do
                if type(row) == "string" then
                    if string.find(row, "I'll trade", 1, true) then
                        tradeRow = row
                    elseif string.find(row, "another go", 1, true) then
                        retryRow = row
                    end
                end
            end
            if tradeRow then break end
            if not retryRow then break end
            t.exec("talkToSlave-retry" .. i, t.chat.choose, retryRow)
        end
        t.check("talkToSlave-lockpicked", tradeRow ~= nil, "reached the trade menu -> " .. tostring(tradeRow))
        t.exec("quest.stage.freed_slave", t.var.await_server, "varp197_desertrescue", 7, 15)
        t.exec("talkToSlave-dialog-2", t.chat.choose, tradeRow or "Yes, I'll trade.")
        t.exec("talkToSlave-dialog-2-note", t.chat.drain, { shots = true, max_pages = 10 })
        t.exec("quest.stage.traded_clothes", t.var.await_server, "varp197_desertrescue", 8, 15)
        t.exec("talkToSlave-shirt", t.inv.expect_has, "slave_shirt", 1)
        t.exec("talkToSlave-robe", t.inv.expect_has, "slave_robe", 1)
        t.exec("talkToSlave-boots", t.inv.expect_has, "slave_boots", 1)

        t.exec("equipSlaveShirt", t.player.equip, "slave_shirt")
        t.exec("equipSlaveRobe", t.player.equip, "slave_robe")
        t.exec("equipSlaveBoots", t.player.equip, "slave_boots")
        -- trap 24: a click's inventory/worn delta lands a SERVER tick after
        -- the client already shows it -- the mine door reads worn state
        -- server-side (~desertrescue_wearing_slave_robes), so give it a tick
        -- to catch up before pressing the door.
        t.ticks(2)

        -- ================= The mine; the cave guard's pineapple hint =================
        t.exec("goto-enterMine", t.player.goto_tile, 3301, 3035, 0)
        t.exec("enterMine", t.player.click_loc, "thttmineentrancel", 1)
        t.ticks(2)
        t.exec("quest.stage.entered_mine", t.var.await_server, "varp197_desertrescue", 9, 15)

        t.exec("goto-talkToGuard", t.player.goto_tile, 3277, 9415, 0)
        t.exec("talkToGuard", t.player.talk_to, "tourtrap_qip_desert_mining_guard_still_2", 1)
        t.ticks(1)
        t.exec("talkToGuard-dialog", t.chat.play, {
            "*",
            "choose:I'd like to mine in a different area.",
            "*",
            "*",
            "npc:rest they say.",
            "choose:Yes sir, you're quite right sir.",
            "*",
            "*",
            "*",
            "*",
            "*",
            "choose:Yes sir, we understand each other perfectly.",
            "*",
            "*",
            "*",
        })
        t.exec("quest.stage.finding_pineapple", t.var.await_server, "varp197_desertrescue", 10, 15)

        t.exec("goto-leaveMine", t.player.goto_tile, 3278, 9426, 0)
        t.exec("leaveMine", t.player.click_loc, "thttmineexitl", 1)
        -- seam23 (fact m): [label,desertrescue_open_mine_door] is
        -- mes("You push the door."); p_delay(2); say("Ugh!"); THEN it reads
        -- ~desertrescue_wearing_slave_robes and p_teleport()s to the
        -- surface -- click_loc's map_flag settle can return while that
        -- p_delay is still running.  Unequipping inside that window used to
        -- make the worn-state check read false mid-delay, so the branch
        -- took the guard-chase arm (~mercenary_camp_attack) instead of the
        -- teleport, and that guard's own suspended page was what orphaned
        -- Al Shabim's page many rows later ("a resume is already
        -- outstanding") -- not a chat.play race. Await the SURFACE tile
        -- itself (0_51_47_37_28 decodes to 3301,3036,0) before touching the
        -- worn slave clothes, never a blind tick count.
        local leftMine = t.await({ level = function()
            local tres, tile = t.world.tile()
            return tres == "ok" and tile.level == 0 and tile.z < 9000
        end, note = "leaveMine: await the surface tile before unequipping (seam23)" }, 10)
        t.check("leaveMine-surface", leftMine == "ok", "await surface tile -> " .. tostring(leftMine))
        -- mining_camp_gate.rs2's own exit check: a guard within 4 tiles of
        -- the gate reads slave clothes STILL WORN while leaving as "a slave
        -- escaping" and calls the full guard search (confiscates the metal
        -- key, cell). Not needed again until the mine reopens below, so
        -- take them off before crossing the gate outward.
        t.exec("unequipShirtForGate", t.player.unequip, "slave_shirt")
        t.exec("unequipRobeForGate", t.player.unequip, "slave_robe")
        t.exec("unequipBootsForGate", t.player.unequip, "slave_boots")
        t.ticks(2)

        -- ================= Al Shabim: the pineapple deal =================
        t.exec("goto-leaveCamp", t.player.goto_tile, 3274, 3029, 0)
        t.exec("leaveCamp", t.player.click_loc, "miningcampgateclosedl", 1)
        t.ticks(2)

        t.exec("goto-talkToShabim", t.player.goto_tile, 3171, 3027, 0)
        t.exec("talkToShabim", t.player.talk_to, "al_shabim", 1)
        t.ticks(1)
        t.exec("talkToShabim-dialog", t.chat.play, {
            "*",
            "*",
            "choose:I am looking for a pineapple.",
            "*",
            "*",
            "*",
            "*",
            "*",
            "choose:Yes, I'm interested.",
            "*",
            "*",
            "*",
            "*",
            "*",
        })
        t.exec("quest.stage.given_bedobin_key", t.var.await_server, "varp197_desertrescue", 11, 15)
        t.exec("talkToShabim-key", t.inv.expect_has, "thbedobinkey", 1)

        -- ================= Captain Siad's chest =================
        t.exec("goto-enterCampForTask", t.player.goto_tile, 3272, 3029, 0)
        t.exec("enterCampForTask", t.player.click_loc, "miningcampgateclosedl", 1)
        t.ticks(2)
        t.exec("goto-searchBookcase", t.player.goto_tile, 3288, 3033, 1)
        t.exec("searchBookcase", t.player.click_loc, "capt_siad_bookcase", 2)
        t.exec("searchBookcase-note", t.chat.drain, { shots = true, max_pages = 10 })

        t.exec("talkToSiad", t.player.talk_to, "capt_siad", 1)
        t.ticks(1)
        t.exec("talkToSiad-dialog", t.chat.play, {
            "*",
            "choose:I wanted to have a chat?",
            "*",
            "*",
            "choose:You seem to have a lot of books!",
            "*",
            "*",
            "choose:So, you're interested in sailing?",
            "*",
            "*",
            "*",
            "choose:I could tell by the cut of your jib.",
            "*",
            "*",
            "*",
            "*",
            "*",
        })
        local rsail, vsail = t.var.server("varp5981_desertrescue_map_mechanisms")
        t.check("talkToSiad-distracted", rsail == "ok" and vsail and (math.floor(vsail / 256) % 2) == 1, "map_mechanisms -> " .. tostring(vsail) .. " (bit 8 = distracted_siad)")

        t.exec("searchChest", t.player.click_loc, "captain_siads_chest_closed", 1)
        t.exec("searchChest-note", t.chat.drain, { shots = true, max_pages = 10 })
        t.exec("quest.stage.retrieved_plans", t.var.await_server, "varp197_desertrescue", 12, 15)
        t.exec("searchChest-plans", t.inv.await, "thcaptplans", 1, 5)

        -- ================= Al Shabim: show the plans =================
        t.exec("goto-leaveCamp2", t.player.goto_tile, 3274, 3029, 0)
        t.exec("leaveCamp2", t.player.click_loc, "miningcampgateclosedl", 1)
        t.ticks(2)
        t.exec("goto-returnToShabim", t.player.goto_tile, 3171, 3027, 0)
        t.exec("returnToShabim", t.player.talk_to, "al_shabim", 1)
        t.ticks(1)
        t.exec("returnToShabim-dialog", t.chat.play, {
            "*",
            "*",
            "*",
            "choose:Yes, I'm very interested.",
            "*",
            "*",
            "choose:Yes, I'm kind of curious.",
            "*",
            "*",
            "*",
        })
        t.exec("quest.stage.shown_plans_shabim", t.var.await_server, "varp197_desertrescue", 13, 15)

        -- ================= The Bedabin tent: anvil and darts =================
        t.exec("goto-talkToTentGuard", t.player.goto_tile, 3170, 3044, 0)
        t.exec("talkToTentGuard", t.player.talk_to, "bedabin_guard", 1)
        t.exec("talkToTentGuard-note", t.chat.drain, { shots = true, max_pages = 10 })
        -- bedabin_nomad_guard.rs2's [opnpc1,bedabin_guard] if_closes the
        -- dialogue, THEN ~forcewalk()s to the door, loc_change()s it open,
        -- p_teleport()s inside (0_49_47_33_38 decodes to 3169,3046,0) and
        -- THEN runs a TRAILING p_delay(3) -- after the teleport, not
        -- before it. A flat tick count raced that trailing delay: useAnvil
        -- landed while [opnpc1,bedabin_guard] was still "waiting" on it,
        -- and the anvil's own objbox call was silently dropped
        -- ("dropping [proc,objbox_scaled], which suspended while
        -- [opnpc1,bedabin_guard] waits" in client.log -- seam23 fact m).
        -- Await the inside-tent tile itself, then clear the full trailing
        -- delay, before the next suspending click.
        local insideTent = t.await({ level = function()
            local tres, tile = t.world.tile()
            return tres == "ok" and tile.x == 3169 and tile.z == 3046 and tile.level == 0
        end, note = "talkToTentGuard: await the inside-tent tile" }, 10)
        t.check("talkToTentGuard-inside", insideTent == "ok", "await inside tile -> " .. tostring(insideTent))
        t.ticks(4)

        -- No further goto here: the tent guard's own talk already
        -- p_teleport'd the player inside; a fresh goto_tile would teleport
        -- back OUTSIDE the tent to an unrelated world tile.
        -- [oplocu,experimental_anvil]'s forge branch deletes the bronze bar
        -- FIRST, then rolls stat_random(smithing, 61, 245) -- at smithing
        -- 99 that is 246/256, ~96%, not a guarantee ("You waste the bronze
        -- bar through an unlucky accident."). Setup carries 3 bronze bars,
        -- so retry with a fresh bar rather than assume the first swing
        -- lands.
        local dartTipMade = false
        for i = 1, 3 do
            local rowBase = i == 1 and "useAnvil" or ("useAnvil-retry" .. (i - 1))
            t.exec(rowBase, t.player.use_on, "bronze_bar", t.player.by_symbol("loc", "experimental_anvil"))
            -- use_on's ok can settle on the walk-up map_flag alone (nothing
            -- else happened yet from the driver's point of view); the
            -- anvil's own ~objbox("Do you want to follow the technical
            -- plans?") is a later packet, so chat.play's first entry can
            -- find nothing open yet. Await the objbox itself before
            -- spelling the page list.
            local awAnvil = t.await({ level = function() return t.chat.kind() ~= "none" end, note = "the anvil's objbox after use_on's map_flag settle" }, 10)
            t.check(rowBase .. "-open", awAnvil == "ok", "await open -> " .. tostring(awAnvil))
            t.exec(rowBase .. "-dialog", t.chat.play, {
                "*",
                "choose:Yes, I'd like to try.",
            })
            -- The forge chain is mes()/anim()/p_delay() log lines, not
            -- pages, until the very last ~mesbox -- give it real time
            -- before draining rather than awaiting a dialogue-open edge
            -- that can flip inside one tick and be missed by polling. The
            -- chain itself is FIVE p_delay(2)s before inv_add (~10 ticks),
            -- so 10 ticks flat sat right on the edge and the very next
            -- check below raced it -- pad generously past it.
            t.ticks(16)
            t.exec(rowBase .. "-result", t.chat.drain, { shots = true, max_pages = 5 })
            -- trap 24: the inv_add lands a server tick behind the mesbox
            -- that announces it -- a bare t.inv.has right here can read
            -- false on a swing that actually landed, and re-clicking a
            -- second time (while thprotodarttip DOES exist) reads the
            -- "already made" objbox as the next page and desyncs the
            -- following retry's chat.play list. Poll for it instead.
            local hr, _hv = t.inv.await("thprotodarttip", 1, 8)
            if hr == "ok" then
                dartTipMade = true
                break
            end
        end
        t.check("useAnvil-tip-made", dartTipMade, "made the dart tip -> " .. tostring(dartTipMade))
        t.exec("useAnvil-tip", t.inv.await, "thprotodarttip", 1, 10)
        t.exec("quest.stage.made_dart_tip", t.var.await_server, "varp197_desertrescue", 14, 15)

        t.exec("useFeatherOnTip", t.player.use_item_on_item, "feather", "thprotodarttip")
        t.exec("useFeatherOnTip-note", t.chat.drain, { shots = true, max_pages = 10 })
        for i = 1, 3 do
            local hr, hv = t.inv.has("thprotodart")
            if hr == "ok" and hv then break end
            t.exec("useFeatherOnTip-retry" .. i, t.player.use_item_on_item, "feather", "thprotodarttip")
            t.chat.drain({ shots = false, max_pages = 10 })
        end
        t.exec("useFeatherOnTip-dart", t.inv.await, "thprotodart", 1, 10)
        t.exec("quest.stage.finished_dart", t.var.await_server, "varp197_desertrescue", 15, 15)

        t.exec("leaveTent", t.player.click_loc, "bedabin_tentdoor", 1)
        t.ticks(3)

        -- ================= Al Shabim: the finished dart =================
        t.exec("goto-bringPrototypeToShabim", t.player.goto_tile, 3171, 3027, 0)
        t.exec("bringPrototypeToShabim", t.player.talk_to, "al_shabim", 1)
        -- Fully automatic (al_shabim.rs2's shabim_showdart has no player
        -- choice at all): drain the first half, which ends at if_close when
        -- the plans are handed over.
        t.exec("bringPrototypeToShabim-dialog-1", t.chat.drain, { shots = true, max_pages = 10 })
        t.ticks(3)
        local ar1 = t.await({ level = function() return t.chat.kind() ~= "none" end, note = "Al Shabim's second half after if_close" }, 10)
        t.check("bringPrototypeToShabim-reopen", ar1 == "ok", "await reopen -> " .. tostring(ar1))
        t.exec("bringPrototypeToShabim-dialog-2", t.chat.drain, { shots = true, max_pages = 15 })
        t.exec("quest.stage.learned_darts", t.var.await_server, "varp197_desertrescue", 16, 15)
        t.exec("bringPrototypeToShabim-darts", t.inv.expect_has, "bronze_dart", 6)
        -- trap 24: al_shabim.rs2's shabim_showdart grants the pineapple on
        -- the VERY LAST page of the drain above (inv_add right before its
        -- own closing objbox) -- a bare expect_has right after the drain
        -- reads the inventory a tick before that write lands (client.log
        -- shows the pineapple's own objbox page WAS shown: shot 236). Poll
        -- for it instead of reading it cold.
        t.exec("bringPrototypeToShabim-pineapple", t.inv.await, "tentipineapple", 1, 5)

        -- ================= Back into the camp and mine with the pineapple =================
        t.exec("goto-enterCampWithPineapple", t.player.goto_tile, 3272, 3029, 0)
        t.exec("enterCampWithPineapple", t.player.click_loc, "miningcampgateclosedl", 1)
        t.ticks(2)
        -- The mine door reads worn state again -- put the slave clothes back
        -- on (trap 24: give the worn-state write a tick to reach the server
        -- before pressing the door).
        t.exec("equipSlaveShirt2", t.player.equip, "slave_shirt")
        t.exec("equipSlaveRobe2", t.player.equip, "slave_robe")
        t.exec("equipSlaveBoots2", t.player.equip, "slave_boots")
        t.ticks(2)
        t.exec("goto-enterMineWithPineapple", t.player.goto_tile, 3301, 3035, 0)
        t.exec("enterMineWithPineapple", t.player.click_loc, "thttmineentrancel", 1)
        -- Same [label,desertrescue_open_mine_door] p_delay(2) tail as
        -- leaveMine above (seam23 fact m) -- await the underground landing
        -- tile (0_51_147_14_17 decodes to 3278,9425,0) before the next
        -- suspending click, or talkToGuardWithPineapple can land while the
        -- door script is still finishing and open no dialogue at all (the
        -- press then settles on a stale content line instead -- "The doors
        -- open with some effort!" left over from THIS click -- and reads
        -- PASS with nothing actually said to the guard).
        local enteredMineAgain = t.await({ level = function()
            local tres, tile = t.world.tile()
            return tres == "ok" and tile.x == 3278 and tile.z == 9425 and tile.level == 0
        end, note = "enterMineWithPineapple: await the underground landing tile" }, 10)
        t.check("enterMineWithPineapple-inside", enteredMineAgain == "ok", "await landing tile -> " .. tostring(enteredMineAgain))
        t.ticks(2)

        t.exec("goto-talkToGuardWithPineapple", t.player.goto_tile, 3277, 9415, 0)
        t.exec("talkToGuardWithPineapple", t.player.talk_to, "tourtrap_qip_desert_mining_guard_still_2", 1)
        t.exec("talkToGuardWithPineapple-note", t.chat.drain, { shots = true, max_pages = 10 })
        t.ticks(2)
        t.exec("quest.stage.given_pineapple", t.var.await_server, "varp197_desertrescue", 17, 15)
        t.exec("talkToGuardWithPineapple-consumed", t.inv.expect_absent, "tentipineapple")

        -- ================= The deep mine; the barrel; the mine cart =================
        -- thminecaver sits on a collision-blocked tile (m51_147.jm2 local
        -- 17,6 and 19,7 both carry f1) with a wall filling every tile east
        -- of it, so the driver's own approach-tile hunt from the guard's
        -- position (3277,9415) keeps landing on 3279,9421 and never finds
        -- an adjacent open square. The one open neighbour is 3280,9414 --
        -- directly west of the 3281,9414 copy (jm2 local 16,6 carries no
        -- f1) -- so goto there before pressing it.
        t.exec("goto-enterDeepMine", t.player.goto_tile, 3280, 9414, 0)
        t.exec("enterDeepMine", t.player.click_loc, "thminecaver", 1)
        t.ticks(3)

        t.exec("goto-getBarrel", t.player.goto_tile, 3302, 9419, 0)
        t.exec("getBarrel", t.player.click_loc, "thminebarrel_empty", 2)
        t.exec("getBarrel-dialog", t.chat.play, { "*", "choose:Yeah, cool!", "*" })
        t.exec("getBarrel-have", t.inv.await, "thminebarrel_empty", 1, 5)

        -- [oploc2,touristtrap_minecart] rolls stat_random(agility, 100, 250)
        -- to fit in the cart; on a miss it only logs two mes() lines (no
        -- page, no teleport) and the dialogue closes after the options
        -- page -- retry the press rather than assume agility 99 always
        -- lands it.
        local enteredCart = false
        for i = 1, 4 do
            local rowBase = i == 1 and "enterMineCart" or ("enterMineCart-retry" .. (i - 1))
            t.exec(rowBase, t.player.click_loc, "touristtrap_minecart", 2)
            t.exec(rowBase .. "-dialog", t.chat.play, { "*", "*", "choose:Yes, of course." })
            local opened = t.await({ level = function() return t.chat.kind() ~= "none" end, note = "the cart's own success page after the agility roll" }, 3)
            if opened == "ok" then
                t.exec(rowBase .. "-note", t.chat.drain, { shots = true, max_pages = 5 })
                enteredCart = true
                break
            end
            t.exec(rowBase .. "-fail", t.msg.expect, "fail to fit yourself into the cart")
        end
        t.check("enterMineCart-landed", enteredCart, "landed in the cart -> " .. tostring(enteredCart))
        t.ticks(2)
        t.exec("quest.stage.used_mine_cart", t.var.await_server, "varp197_desertrescue", 18, 15)

        -- ================= Ana =================
        t.exec("goto-talkToAna", t.player.goto_tile, 3300, 9464, 0)
        t.exec("talkToAna", t.player.talk_to, "tourtrap_qip_ana_multi", 1)
        t.ticks(1)
        t.exec("talkToAna-dialog", t.chat.play, {
            "*",
            "*",
            "choose:What's your name.",
            "*",
            "*",
            "*",
            "choose:Do you want to go back to Al-Kharid?",
            "*",
            "*",
            "*",
        })

        t.exec("useBarrelOnAna", t.player.use_on, "thminebarrel_empty", t.player.by_symbol("npc", "tourtrap_qip_ana_multi"))
        local awAna = t.await({ level = function() return t.chat.kind() ~= "none" end, note = "Ana squeezed into the barrel, narrated after if_close" }, 10)
        t.check("useBarrelOnAna-reopen", awAna == "ok", "await reopen -> " .. tostring(awAna))
        t.exec("useBarrelOnAna-note", t.chat.drain, { shots = true, max_pages = 10 })
        t.ticks(2)
        t.exec("quest.stage.caught_ana", t.var.await_server, "varp197_desertrescue", 19, 15)
        t.exec("useBarrelOnAna-inbarrel", t.inv.await, "thanainabarrel", 1, 5)

        -- ================= Ana into the far mine cart; ride back; retrieve her =================
        t.exec("goto-useBarrelOnMineCart", t.player.goto_tile, 3317, 9431, 0)
        t.exec("useBarrelOnMineCart", t.player.use_on, "thanainabarrel", t.player.by_symbol("loc", "touristtrap_minecart"))
        t.exec("useBarrelOnMineCart-note", t.chat.drain, { shots = true, max_pages = 10 })
        t.exec("quest.stage.ana_minecart", t.var.await_server, "varp197_desertrescue", 20, 15)

        -- Same [oploc2,touristtrap_minecart] agility roll as enterMineCart
        -- above -- retry on a miss instead of assuming it always lands.
        local returnedCart = false
        for i = 1, 4 do
            local rowBase = i == 1 and "returnInMineCart" or ("returnInMineCart-retry" .. (i - 1))
            t.exec(rowBase, t.player.click_loc, "touristtrap_minecart", 2)
            t.exec(rowBase .. "-dialog", t.chat.play, { "*", "*", "choose:Yes, of course." })
            local opened = t.await({ level = function() return t.chat.kind() ~= "none" end, note = "the cart's own success page after the agility roll" }, 3)
            if opened == "ok" then
                t.exec(rowBase .. "-note", t.chat.drain, { shots = true, max_pages = 5 })
                returnedCart = true
                break
            end
            t.exec(rowBase .. "-fail", t.msg.expect, "fail to fit yourself into the cart")
        end
        t.check("returnInMineCart-landed", returnedCart, "returned in the cart -> " .. tostring(returnedCart))
        t.ticks(2)

        t.exec("goto-searchBarrelsForAna", t.player.goto_tile, 3302, 9419, 0)
        t.exec("searchBarrelsForAna", t.player.click_loc, "thminebarrel_empty", 2)
        t.exec("searchBarrelsForAna-note", t.chat.drain, { shots = true, max_pages = 10 })
        t.exec("quest.stage.retrieved_ana_minecart", t.var.await_server, "varp197_desertrescue", 21, 15)
        -- trap 24: the stage var and the inv_add that hands Ana's barrel
        -- back are two different channels -- the stage read above landing
        -- does not prove the container has synced yet. Poll for it.
        t.exec("searchBarrelsForAna-have", t.inv.await, "thanainabarrel", 1, 5)

        -- ================= The winch bucket below =================
        t.exec("goto-sendAnaUp", t.player.goto_tile, 3291, 9424, 0)
        t.exec("sendAnaUp", t.player.click_loc, "tourtrap_qip_ropepullthingy2", 2)
        t.exec("sendAnaUp-dialog", t.chat.play, {
            "*",
            "*",
            "choose:Yes please.",
            "*",
            "*",
            "*",
            "*",
            "*",
            "*",
            "*",
            "*",
            -- camp_guard.rs2's [label,camp_merc_barrel_help]: the guard's
            -- own "What was that you said?" line (page 12) is a plain npc
            -- page, one page BEFORE the second p_choice2 -- the previous
            -- list here was one "*" short and graded that page as the
            -- options page.
            "*",
            "choose:I said you were very gregarious!",
            "*",
            "*",
            "*",
            "*",
            "*",
            "*",
        })
        t.exec("quest.stage.ana_lift", t.var.await_server, "varp197_desertrescue", 22, 15)

        -- ================= Back to the surface; the winch; the wooden cart =================
        t.exec("leaveDeepMine", t.player.click_loc, "thminecavel", 1)
        local leftDeepMine = t.await({ level = function()
            local tres, tile = t.world.tile()
            return tres == "ok" and tile.x <= 3279 and tile.z >= 9410 and tile.z <= 9420
        end, note = "leaveDeepMine: await the arrival in mine 1 before travelling on" }, 15)
        t.check("leaveDeepMine-arrived", leftDeepMine == "ok", "await mine 1 arrival -> " .. tostring(leftDeepMine))
        t.ticks(2)
        t.exec("goto-leaveMineForAna", t.player.goto_tile, 3278, 9426, 0)
        t.exec("leaveMineForAna", t.player.click_loc, "thttmineexitl", 1)
        -- Same [label,desertrescue_open_mine_door] p_delay(2) tail as the
        -- first leaveMine above (seam23 fact m) -- await the surface tile
        -- before the next suspending click. Without this the door
        -- script's own trailing message ("The doors open with some
        -- effort!") was still queued behind our player's suspended-script
        -- slot when goto-operateWinch (a ::goto) and operateWinch fired,
        -- so the winch script itself got cut off after its opening mes()
        -- line and never reached its own p_delay(3)/mesbox tail --
        -- operateWinch-open timed out every run and the stage never
        -- advanced past ana_lift (22).
        local leftMineForAna = t.await({ level = function()
            local tres, tile = t.world.tile()
            return tres == "ok" and tile.level == 0 and tile.z < 9000
        end, note = "leaveMineForAna: await the surface tile before the winch" }, 10)
        t.check("leaveMineForAna-surface", leftMineForAna == "ok", "await surface tile -> " .. tostring(leftMineForAna))
        t.ticks(2)

        t.exec("goto-operateWinch", t.player.goto_tile, 3280, 3018, 0)
        t.exec("operateWinch", t.player.click_loc, "tourtrap_qip_ropepullthingy", 2)
        -- [oploc2,tourtrap_qip_ropepullthingy] is mes("You pull on the
        -- winch."); p_delay(3); THEN (with ana_on_lift set) the stage
        -- write + ~mesbox + the Ana chat line -- click_loc's settle can
        -- resolve on that first mes() line before the p_delay(3) clears,
        -- so a drain right after finds nothing open yet ("[frame
        -- unchanged]") and the stage never advances. Await the mesbox
        -- before draining.
        local awWinch = t.await({ level = function() return t.chat.kind() ~= "none" end, note = "the winch's own mesbox after its p_delay(3)" }, 8)
        t.check("operateWinch-open", awWinch == "ok", "await open -> " .. tostring(awWinch))
        t.exec("operateWinch-note", t.chat.drain, { shots = true, max_pages = 10 })
        t.exec("quest.stage.retrieved_ana_lift", t.var.await_server, "varp197_desertrescue", 23, 15)

        t.exec("searchWinchBarrel", t.player.click_loc, "tourtrap_qip_anabarrel_winchside_multi", 2)
        t.exec("searchWinchBarrel-note", t.chat.drain, { shots = true, max_pages = 10 })
        t.exec("quest.stage.retrieved_ana_liftbarrel", t.var.await_server, "varp197_desertrescue", 24, 15)

        t.exec("goto-useBarrelOnCart", t.player.goto_tile, 3289, 3025, 0)
        t.exec("useBarrelOnCart", t.player.use_on, "thanainabarrel", t.player.by_symbol("loc", "tourtrap_qip_multi_flatback_cart"))
        t.exec("useBarrelOnCart-note", t.chat.drain, { shots = true, max_pages = 10 })
        t.exec("quest.stage.ana_on_mining_cart", t.var.await_server, "varp197_desertrescue", 25, 15)

        t.exec("talkToDriver", t.player.talk_to, "mining_cart_driver", 1)
        t.ticks(1)
        t.exec("talkToDriver-dialog", t.chat.play, {
            "*",
            "choose:Nice cart.",
            "*",
            "*",
            "*",
            "*",
            "choose:One wagon wheel says to the other, 'I'll see you around'.",
            "*",
            "*",
            "*",
            "choose:'One good turn deserves another'",
            "*",
            "*",
            "*",
            "choose:Fired... no, shot perhaps!",
            "*",
            "*",
            "choose:In for a penny in for a pound.",
            "*",
            "*",
            "*",
            "*",
            "*",
            "choose:Well, you see, it's like this...",
            "*",
            "*",
            "choose:Prison riot in ten minutes, get your cart out of here!",
            "*",
            "*",
            "*",
            "*",
            "choose:You can't leave me here, I'll get killed!",
            "*",
            "*",
        })
        local rrr, rrv = t.var.server("varp5981_desertrescue_map_mechanisms")
        t.check("talkToDriver-ready", rrr == "ok" and rrv and (math.floor(rrv / 65536) % 2) == 1, "map_mechanisms -> " .. tostring(rrv) .. " (bit 16 = ready_rescue)")

        t.exec("useBarrelOnCart2", t.player.click_loc, "tourtrap_qip_multi_flatback_cart", 2)
        t.exec("useBarrelOnCart2-dialog", t.chat.play, { "*", "choose:Yes, I'll get on.", "*" })
        local awCart = t.await({ level = function() return t.chat.kind() ~= "none" end, note = "the cart departs, narrated after if_close" }, 10)
        t.check("useBarrelOnCart2-reopen", awCart == "ok", "await reopen -> " .. tostring(awCart))
        t.exec("useBarrelOnCart2-note", t.chat.drain, { shots = true, max_pages = 5 })
        t.exec("quest.stage.escaped", t.var.await_server, "varp197_desertrescue", 26, 15)
        t.exec("useBarrelOnCart2-barrel", t.inv.await, "thanainabarrel", 1, 5)

        -- ================= Irena; Ana; the reward =================
        t.exec("unequipShirt", t.player.unequip, "slave_shirt")
        t.exec("unequipRobe", t.player.unequip, "slave_robe")
        t.exec("unequipBoots", t.player.unequip, "slave_boots")

        t.exec("goto-returnToIrena", t.player.goto_tile, 3303, 3111, 0)
        t.exec("returnToIrena", t.player.talk_to, "tourtrap_qip_irena_multi_sad", 1)
        t.exec("returnToIrena-dialog", t.chat.drain, { shots = true, max_pages = 20 })
        t.exec("quest.stage.saved_ana", t.var.await_server, "varp197_desertrescue", 27, 15)

        local aw1 = t.await({ level = function() return t.chat.kind() ~= "none" end, note = "Ana's thanks reopening after Irena's handoff" }, 10)
        t.check("talkToAnaThanks-reopen", aw1 == "ok", "await reopen -> " .. tostring(aw1))
        t.exec("talkToAnaThanks-dialog", t.chat.drain, { shots = true, max_pages = 20 })
        t.exec("talkToAnaThanks-key", t.inv.await, "thgoodminekey", 1, 10)

        local aw2 = t.await({ level = function() return t.chat.kind() ~= "none" end, note = "Irena's reward reopening after Ana's thanks" }, 10)
        t.check("talkToIrenaToFinish-reopen", aw2 == "ok", "await reopen -> " .. tostring(aw2))
        local snapOk, snap = t.skill.snapshot()
        t.check("skill.snapshot", snapOk == "ok", "skill.snapshot -> " .. tostring(snapOk))
        -- ana.rs2's shanty-pass thanks jumps STRAIGHT to irena.rs2's
        -- [label,irena_ana_found] (a cross-script label call, never a
        -- simulated click on desertrescue_irena_talk's own switch), and
        -- that label opens with TWO SEPARATE chatnpc_anim pages ("Thank
        -- you very much..." then "I can offer you increased knowledge in
        -- two of the following areas.") before the first p_choice4 --
        -- both need a leading "*", not one. Same shape for the "choose
        -- your second skill" npc line between the two picks, and the
        -- closing "that's all the skills I can teach you!" after the
        -- second one (irena_checkrewards' queue(desertrescue_complete)
        -- branch).
        t.exec("talkToIrenaToFinish-dialog", t.chat.play, {
            "*",
            "*",
            "choose:Fletching.",
            "*",
            "choose:Fletching.",
            "*",
        })
        t.ticks(3)
        t.exec("quest.stage.complete", t.var.await_server, "varp197_desertrescue", 30, 15)

        t.exec("reward.fletching_xp", t.skill.expect_gain, "fletching", 9300, snap)
        t.quest.expect_complete()
        t.finish(0)
    end,
}
