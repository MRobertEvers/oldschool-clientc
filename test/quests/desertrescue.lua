-- The Tourist Trap (desertrescue). Driven end to end through real clicks and
-- dialogue: Irena -> the Mercenary Captain's duel -> the camp gate -> the
-- clothes slave -> the mine -> the cave guard's pineapple hint -> Al Shabim's
-- pineapple deal -> Captain Siad's chest (via the bookcase/sailing distraction)
-- -> the Bedabin tent's anvil (dart tip) and feathers (dart) -> Al Shabim's
-- darts+pineapple -> the cave guard's pineapple -> the deep mine -> Ana ->
-- the mine cart -> the winch -> the flatback cart -> the driver -> Irena's
-- reward. No stage is ever cheated with ::setvar; setup only stages the
-- guide's bring-along items (desert clothes, bronze bars, hammer, feathers),
-- Shantay's 5 coins, waterskins for the desert heat, food for the duel, and
-- skill levels (none of which is the quest's own deliverable).
--
-- Door rule (b60): every closed space is crossed by its own click, graded on
-- the tiles -- the Shantay Pass (the only way on foot into the Kharidian
-- Desert: reach.py 3304,3120 -> 3304,3112 UNREACHABLE at margins 30/80/160),
-- the camp gate (t.player.cross_gate, walk-through), the mine doors and the
-- mine caves (cross_gate, they teleport across), Siad's curtain and ladder,
-- the Bedabin tent door, both mine-cart rides and the escape cart. The long
-- underground leg between the mine door and the guard is walked
-- (t.player.walk_route). The overland desert hops (Irena -> the camp, the
-- camp <-> the Bedabin camp, the escape -> Irena) are plain travel between
-- open tiles (reach.py REACH closed-doors, len 119-127).
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
        "::give coins 5", -- Shantay's price for the pass (shantay.rs2:169-176)
        "::give water_skin4 2", -- the desert heat (desert_heat.rs2 [timer,desert_heat] drinks a dose)
        "::give lobster 4", -- food for the Mercenary Captain's duel (margin row)
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

        -- ---------------------------------------------------------------
        -- The crossings (door rule, b60). Every one is pressed on every
        -- visit and graded on the tiles by the driver's own verbs.
        -- ---------------------------------------------------------------
        -- The camp gate: miningcampgateclosedl 3273,3029 r2 (its wall is the
        -- tile's east edge). mining_camp_gate.rs2 ~desertrescue_open_camp_gate
        -- -> quest_desertrescue.rs2 ~desertrescue_cross_wall p_teleports the
        -- player one tile across and leaves no open leaf: a walk-through.
        local CAMP_IN = { loc = "miningcampgateclosedl", at = { 3273, 3029, 0 }, near = { 3273, 3029 },
            far_ok = function(tile) return tile.x >= 3274 end, far_desc = "inside the mining camp, x >= 3274" }
        local CAMP_OUT = { loc = "miningcampgateclosedl", at = { 3273, 3029, 0 }, near = { 3274, 3029 },
            far_ok = function(tile) return tile.x <= 3273 end, far_desc = "outside the mining camp, x <= 3273" }
        -- The mine doors: [label,desertrescue_open_mine_door] (quest_desertrescue.rs2:254)
        -- p_teleports between the camp (thttmineentrancel 3301,3036 r1) and
        -- the mine (thttmineexitl 3278,9426 r1; its mine side is z >= 9427).
        local MINE_IN = { loc = "thttmineentrancel", at = { 3301, 3036, 0 }, near = { 3301, 3036 },
            far_ok = function(tile) return tile.x == 3278 and (tile.z == 9426 or tile.z == 9427) end,
            far_desc = "the mine door's tile 3278,9426 or its mine side 3278,9427" }
        -- The way out (quest_desertrescue.rs2:262-265): p_teleport(0_51_47_37_28)
        -- = 3301,3036, the entrance door's own tile in the camp.
        local MINE_OUT = { loc = "thttmineexitl", at = { 3278, 9426, 0 }, near = { 3278, 9427 },
            far_ok = function(tile) return tile.x == 3301 and tile.z == 3036 end,
            far_desc = "the camp side of the mine entrance, 3301,3036" }
        -- The mine caves (thminecaver 3281,9414 r0 / thminecavel 3283,9414 r2,
        -- [label,desertrescue_minecave]): the upper tunnels are x <= 3280,
        -- the deep mine x >= 3284; each side p_teleports to the other.
        local DEEP_IN = { loc = "thminecaver", at = { 3281, 9414, 0 }, near = { 3280, 9414 },
            far_ok = function(tile) return tile.x >= 3284 end, far_desc = "the deep mine, x >= 3284" }
        local DEEP_OUT = { loc = "thminecavel", at = { 3283, 9414, 0 }, near = { 3284, 9414 },
            far_ok = function(tile) return tile.x <= 3280 end, far_desc = "the upper tunnels, x <= 3280" }
        -- The mine door's mine side to the cave guard: 95 tiles round the
        -- cave (reach.py 3278,9427 -> 3277,9415: REACH closed-doors len=95),
        -- waypoints off that flood's path, hops <= 8.
        local DOOR_TO_GUARD = { { 3278, 9427 }, { 3278, 9435 }, { 3278, 9443 }, { 3278, 9451 }, { 3275, 9459 },
            { 3270, 9451 }, { 3271, 9443 }, { 3271, 9435 }, { 3272, 9427 }, { 3275, 9419 }, { 3277, 9415 } }
        -- A "use ITEM on TARGET" step is graded on the item leaving the pack
        -- (the inv write lands a tick behind the page: trap 24, so await it).
        local function item_gone(row, item, ticks)
            local aw = t.await({ level = function()
                local r, n = t.inv.count(item)
                return r == "ok" and n == 0
            end, note = row .. ": " .. item .. " leaves the pack" }, ticks or 5)
            local r, n = t.inv.count(item)
            t.check(row, aw == "ok" and r == "ok" and n == 0,
                item .. " in the pack after the use: " .. tostring(n) .. " (" .. tostring(r) .. ", await " .. tostring(aw) .. ", want 0)")
        end
        local GUARD_TO_DOOR = {}
        for i = #DOOR_TO_GUARD, 1, -1 do
            GUARD_TO_DOOR[#GUARD_TO_DOOR + 1] = DOOR_TO_GUARD[i]
        end

        -- The mine door's landing. [label,desertrescue_open_mine_door]
        -- (OSRS-Content quest_desertrescue.rs2:254-289, seam b60-seam1, as
        -- LostCity quest_desertrescue.rs2:433-440) p_teleports onto the exit
        -- door's own tile 0_51_147_14_18 = 3278,9426, then through the door
        -- (~door_open on thttmineexitl r1) onto the mine side 3278,9427. The 20
        -- tiles south of the door (x 3276-3281, z 9421-9426, maps/m51_147) are
        -- a pocket the map walls in (comp.py: the door is its only edge): the
        -- old landing 0_51_147_14_17 put the player there. MINE_IN passes on
        -- the door tile or the mine side (whichever the poll sees first); this
        -- row then holds the player to the mine side exactly, never the pocket.
        local function mine_side(row)
            local aw = t.await({ level = function()
                local tr, tile = t.world.tile()
                return tr == "ok" and tile.level == 0 and tile.x == 3278 and tile.z == 9427
            end, note = row .. ": through the door onto the mine side 3278,9427" }, 6)
            local _, tile = t.world.tile()
            t.check(row, aw == "ok" and tile ~= nil and tile.level == 0 and tile.x == 3278 and tile.z == 9427,
                "after the mine door: " .. tostring(tile and tile.x) .. "," .. tostring(tile and tile.z) .. ","
                    .. tostring(tile and tile.level) .. " (want 3278,9427,0, the mine side of thttmineexitl; await "
                    .. tostring(aw) .. ")")
        end

        -- ================= Al Kharid: Shantay's pass, through the gate =================
        -- The run's first goto: Lumbridge -> the Shantay Pass compound north of
        -- the gate, overland (reach.py 3206,3233 -> 3304,3121 margin 160: REACH
        -- closed-doors len=453, round the north end of the Al Kharid fence, no
        -- toll gate). Irena stands SOUTH of the gate, in the desert.
        t.exec("goto-buyShantayPass", t.player.goto_tile, 3304, 3121, 0)
        t.exec("buyShantayPass", t.player.talk_to, "shantay", 1)
        t.exec("buyShantayPass-dialog", t.chat.play, {
            "npc:Hello effendi, I am Shantay.",
            "npc:I see you're new. Please read",
            "choose:I want to buy a shantay pass for 5 gold coins.",
            "player:I want to buy a shantay pass for",
            "mesbox:You purchase a Shantay Pass.",
        })
        local passR, passN = t.inv.count("shantay_pass")
        local coinR, coinN = t.inv.count("coins")
        t.check("buyShantayPass-bought", passR == "ok" and passN == 1 and coinR == "ok" and coinN == 0,
            "shantay_pass " .. tostring(passN) .. " (" .. tostring(passR) .. "), coins " .. tostring(coinN)
                .. " (" .. tostring(coinR) .. ", 5 staged, want 0)")

        -- shantay_pass.rs2 [oploc1,shantay_pass_henge_doorway]: from the north
        -- (coordz(coord) > the gate's z 3116, shantay_guard_still within 5) it
        -- reads the poster, takes ONE pass, and [queue,shantay_pass_enter]
        -- p_teleport(3304,3118) + p_telejump(3304,3115) -- a queued teleport
        -- a tick behind the last page. Graded on the tiles and the pass.
        t.player.walk_to(3304, 3118, 10) -- graded below: enterDesert-crossed reads this tile as "before"
        local _, enterDesertBefore = t.world.tile()
        t.exec("enterDesert", t.player.click_loc, "shantay_pass_henge_doorway", 1, { at = { 3302, 3116, 0 } })
        t.exec("enterDesert-dialog", t.chat.play, {
            "mesbox:There is a large poster on the wall",
            "mesbox:The Desert is a VERY Dangerous place",
            "mesbox:That seems pretty scary!",
            "choose:Yeah, that poster doesn't scare me!",
            "npc:Can I see your Shantay Desert Pass",
            "mesbox:You hand over a Shantay Pass.",
            "player:Sure, here you go!",
            "npc:Here, have a disclaimer",
        })
        local inDesert = t.await({ level = function()
            local tres, tile = t.world.tile()
            return tres == "ok" and tile.level == 0 and tile.z <= 3115
        end, note = "enterDesert: the queued shantay_pass_enter teleport south of the gate" }, 10)
        local _, enterDesertAfter = t.world.tile()
        t.check("enterDesert-crossed", inDesert == "ok" and enterDesertBefore ~= nil and enterDesertBefore.z >= 3117
                and enterDesertAfter ~= nil and enterDesertAfter.z <= 3115 and enterDesertAfter.level == 0,
            "before " .. tostring(enterDesertBefore and enterDesertBefore.x) .. "," .. tostring(enterDesertBefore and enterDesertBefore.z)
                .. " (north of the gate, z >= 3117) -> after " .. tostring(enterDesertAfter and enterDesertAfter.x) .. ","
                .. tostring(enterDesertAfter and enterDesertAfter.z) .. "," .. tostring(enterDesertAfter and enterDesertAfter.level)
                .. " (the desert, z <= 3115; await " .. tostring(inDesert) .. ")")
        local passR2, passN2 = t.inv.count("shantay_pass")
        t.check("enterDesert-passHandedOver", passR2 == "ok" and passN2 == 0,
            "shantay_pass 1 -> " .. tostring(passN2) .. " (" .. tostring(passR2) .. ")")
        t.exec("enterDesert-disclaimer", t.inv.await, "thshantaydisc", 1, 5)

        -- ================= Irena: start the quest =================
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
        local capDeadR, capDeadD = t.npc.await_dead_engaged(90, 8, { eat = { item = "lobster", below = 50 } })
        t.check("talkToCaptain-dead", capDeadR, capDeadD)
        -- Margin: the lowest hitpoints the kill wait sampled at least a
        -- quarter of 99 (25) AND food left (desertminingcaptain: hitpoints 80,
        -- attack 32, combat_stats.generated.npc).
        local capLowest = tonumber(string.match(tostring(capDeadD), "lowest hp (%d+)/"))
        local capHpR, capHp = t.skill.read("hitpoints")
        if capHpR == "ok" and type(capHp) == "table" and capHp.level and (capLowest == nil or capHp.level < capLowest) then
            capLowest = capHp.level
        end
        local capFoodR, capFood = t.inv.count("lobster")
        t.check("talkToCaptain-margin", capLowest ~= nil and capLowest >= 25 and capFoodR == "ok" and capFood >= 1,
            "Mercenary Captain: lowest hp " .. tostring(capLowest) .. "/99, lobsters staged 4, left " .. tostring(capFood)
                .. " (" .. tostring(capFoodR) .. ") (margin: lowest hp >= 25, a quarter of 99, AND food left)")
        t.exec("quest.stage.killed_capt", t.var.await_server, "varp197_desertrescue", 4, 15)
        t.exec("talkToCaptain-key", t.inv.await, "metal_key", 1, 5)

        -- ================= Into the camp; the clothes slave =================
        t.exec("enterCamp", t.player.cross_gate, CAMP_IN)
        t.exec("quest.stage.entered_camp", t.var.await_server, "varp197_desertrescue", 5, 15)

        -- In-camp travel (reach.py 3274,3029 -> 3302,3026: REACH closed-doors
        -- len=31; 3302,3025 was a solid tile).
        t.exec("goto-talkToSlave", t.player.goto_tile, 3302, 3026, 0)
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
        -- In-camp travel to the mine door (reach.py REACH closed-doors len=15).
        t.exec("goto-enterMine", t.player.goto_tile, 3301, 3035, 0)
        t.exec("enterMine", t.player.cross_gate, MINE_IN)
        mine_side("enterMine-mineSide")
        t.exec("quest.stage.entered_mine", t.var.await_server, "varp197_desertrescue", 9, 15)

        t.exec("talkToGuard-route", t.player.walk_route, DOOR_TO_GUARD, { level = 0 })
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

        -- leaveMine: walked back round the cave to the door's mine side, then
        -- the door (quest_desertrescue.rs2:262-265 p_teleport 3301,3036).
        -- seam23 (fact m): the door script's p_delay(2) runs before the
        -- worn-state read and the teleport; cross_gate passes only once the
        -- surface tile is reached, so the clothes come off after it.
        t.exec("leaveMine-route", t.player.walk_route, GUARD_TO_DOOR, { level = 0 })
        t.exec("leaveMine", t.player.cross_gate, MINE_OUT)
        t.ticks(2)
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
        -- In-camp travel to the gate (REACH closed-doors len=34).
        t.exec("goto-leaveCamp", t.player.goto_tile, 3275, 3029, 0)
        t.exec("leaveCamp", t.player.cross_gate, CAMP_OUT)
        t.ticks(2)

        -- Overland desert travel (reach.py 3272,3029 -> 3171,3027 REACH len=119).
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
        -- Overland back to open sand west of the gate (REACH len=119).
        t.exec("goto-enterCampForTask", t.player.goto_tile, 3272, 3029, 0)
        t.exec("enterCampForTask", t.player.cross_gate, CAMP_IN)
        t.ticks(2)
        -- Siad's building (33 tiles; comp.py: its edges are the curtain
        -- desertdoorclosed 3291,3030 r1 and the cell door): in-camp travel to
        -- the curtain's south side, the curtain, then the ladder to level 1.
        t.exec("goto-goUpToSiad", t.player.goto_tile, 3291, 3028, 0)
        t.exec("goUpToSiad-curtain", t.player.pass_door, { closed = "desertdoorclosed", open = "desertdooropen",
            at = { 3291, 3030, 0 }, near = { 3291, 3030 }, far = { 3291, 3031 } })
        -- tourtrap_qip_ladder (ladders.loc:1627) has no maplink row: ladders.rs2
        -- [proc,climb] lifts the player one plane on the approach tile.
        t.exec("goUpToSiad", t.player.climb, { loc = "tourtrap_qip_ladder", op = 1, op_name = "Climb-up",
            at = { 3290, 3036, 0 }, src = { 3290, 3035 }, dest = { 3290, 3035, 1 } })
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
        -- Down the ladder, out through the curtain (pass_door walks through
        -- it when it still stands open from the way in), the gate.
        t.exec("leaveSiad", t.player.climb, { loc = "tourtrap_qip_ladder_top", op = 1, op_name = "Climb-down",
            at = { 3290, 3036, 1 }, src = { 3290, 3037 }, dest = { 3290, 3037, 0 } })
        t.exec("leaveSiad-curtain", t.player.pass_door, { closed = "desertdoorclosed", open = "desertdooropen",
            at = { 3291, 3030, 0 }, near = { 3291, 3031 }, far = { 3291, 3029 } })
        t.exec("goto-leaveCamp2", t.player.goto_tile, 3275, 3029, 0)
        t.exec("leaveCamp2", t.player.cross_gate, CAMP_OUT)
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
        local barsR0, bars0 = t.inv.count("bronze_bar")
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
        -- [oplocu,experimental_anvil] inv_del's one bronze bar per swing
        -- (quest_desertrescue.rs2:404), lucky or not.
        local barsR1, bars1 = t.inv.count("bronze_bar")
        t.check("useAnvil-barsUsed", barsR0 == "ok" and barsR1 == "ok" and bars0 - bars1 >= 1,
            "bronze_bar " .. tostring(bars0) .. " -> " .. tostring(bars1) .. " (one per swing, want at least one gone)")
        t.exec("useAnvil-tip", t.inv.await, "thprotodarttip", 1, 10)
        t.exec("quest.stage.made_dart_tip", t.var.await_server, "varp197_desertrescue", 14, 15)

        local featR0, feat0 = t.inv.count("feather")
        t.exec("useFeatherOnTip", t.player.use_item_on_item, "feather", "thprotodarttip")
        t.exec("useFeatherOnTip-note", t.chat.drain, { shots = true, max_pages = 10 })
        for i = 1, 3 do
            local hr, hv = t.inv.has("thprotodart")
            if hr == "ok" and hv then break end
            t.exec("useFeatherOnTip-retry" .. i, t.player.use_item_on_item, "feather", "thprotodarttip")
            t.chat.drain({ shots = false, max_pages = 10 })
        end
        t.exec("useFeatherOnTip-dart", t.inv.await, "thprotodart", 1, 10)
        -- quest_desertrescue.rs2:459/471-472: ten feathers per try, the tip
        -- deleted on the one that works.
        item_gone("useFeatherOnTip-tipGone", "thprotodarttip")
        local featR1, feat1 = t.inv.count("feather")
        t.check("useFeatherOnTip-feathersUsed", featR0 == "ok" and featR1 == "ok" and feat0 - feat1 >= 10 and (feat0 - feat1) % 10 == 0,
            "feather " .. tostring(feat0) .. " -> " .. tostring(feat1) .. " (ten per try)")
        t.exec("quest.stage.finished_dart", t.var.await_server, "varp197_desertrescue", 15, 15)

        -- bedabin_nomad_guard.rs2 [oploc1,bedabin_tentdoor]: from inside
        -- (coordz >= the door's 3046) "You walk out of the tent." p_teleport
        -- 3169,3045, then p_delay(3).
        t.exec("leaveTent", t.player.cross_gate, { loc = "bedabin_tentdoor", at = { 3169, 3046, 0 }, near = { 3169, 3046 },
            far_ok = function(tile) return tile.z <= 3045 end, far_desc = "outside the Bedabin tent, z <= 3045" })
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
        t.exec("enterCampWithPineapple", t.player.cross_gate, CAMP_IN)
        t.ticks(2)
        -- The mine door reads worn state again -- put the slave clothes back
        -- on (trap 24: give the worn-state write a tick to reach the server
        -- before pressing the door).
        t.exec("equipSlaveShirt2", t.player.equip, "slave_shirt")
        t.exec("equipSlaveRobe2", t.player.equip, "slave_robe")
        t.exec("equipSlaveBoots2", t.player.equip, "slave_boots")
        t.ticks(2)
        t.exec("goto-enterMineWithPineapple", t.player.goto_tile, 3301, 3035, 0)
        t.exec("enterMineWithPineapple", t.player.cross_gate, MINE_IN)
        mine_side("enterMineWithPineapple-mineSide")

        t.exec("talkToGuardWithPineapple-route", t.player.walk_route, DOOR_TO_GUARD, { level = 0 })
        t.exec("talkToGuardWithPineapple", t.player.talk_to, "tourtrap_qip_desert_mining_guard_still_2", 1)
        t.exec("talkToGuardWithPineapple-note", t.chat.drain, { shots = true, max_pages = 10 })
        t.ticks(2)
        t.exec("quest.stage.given_pineapple", t.var.await_server, "varp197_desertrescue", 17, 15)
        t.exec("talkToGuardWithPineapple-consumed", t.inv.expect_absent, "tentipineapple")

        -- ================= The deep mine; the barrel; the mine cart =================
        -- The cave into the deep mine, from its west side (x 3280).
        t.exec("enterDeepMine", t.player.cross_gate, DEEP_IN)
        t.ticks(2)

        -- Deep-mine travel along one passage (reach.py 3286,9415 -> 3302,9419 REACH len=20).
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
        -- The ride: quest_desertrescue.rs2 [oploc2,touristtrap_minecart]
        -- p_teleport(0_51_147_55_23) = 3319,9431, the far room, which no walk
        -- reaches from the barrel room (reach.py 3302,9419 -> 3319,9431
        -- UNREACHABLE at margin 80).
        local _, cartTile = t.world.tile()
        t.check("enterMineCart-ride", cartTile ~= nil and cartTile.level == 0 and math.abs(cartTile.x - 3319) <= 1
                and math.abs(cartTile.z - 9431) <= 1,
            "after the cart: " .. tostring(cartTile and cartTile.x) .. "," .. tostring(cartTile and cartTile.z) .. ","
                .. tostring(cartTile and cartTile.level) .. " (want within 1 of 3319,9431,0)")
        t.exec("quest.stage.used_mine_cart", t.var.await_server, "varp197_desertrescue", 18, 15)

        -- ================= Ana =================
        -- Travel in the far room (reach.py 3319,9431 -> 3300,9465 REACH len=61;
        -- 3300,9464 was a solid tile).
        t.exec("goto-talkToAna", t.player.goto_tile, 3300, 9465, 0)
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
        item_gone("useBarrelOnAna-emptyGone", "thminebarrel_empty") -- ana.rs2:125-126

        -- ================= Ana into the far mine cart; ride back; retrieve her =================
        t.exec("goto-useBarrelOnMineCart", t.player.goto_tile, 3317, 9431, 0)
        t.exec("useBarrelOnMineCart", t.player.use_on, "thanainabarrel", t.player.by_symbol("loc", "touristtrap_minecart"))
        t.exec("useBarrelOnMineCart-note", t.chat.drain, { shots = true, max_pages = 10 })
        t.exec("quest.stage.ana_minecart", t.var.await_server, "varp197_desertrescue", 20, 15)
        item_gone("useBarrelOnMineCart-barrelGone", "thanainabarrel") -- quest_desertrescue.rs2:698

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
        -- The ride back: p_teleport(0_51_147_38_9) = 3302,9417, the barrel room.
        local _, backTile = t.world.tile()
        t.check("returnInMineCart-ride", backTile ~= nil and backTile.level == 0 and math.abs(backTile.x - 3302) <= 1
                and math.abs(backTile.z - 9417) <= 1,
            "after the cart: " .. tostring(backTile and backTile.x) .. "," .. tostring(backTile and backTile.z) .. ","
                .. tostring(backTile and backTile.level) .. " (want within 1 of 3302,9417,0)")

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
        -- Deep-mine travel back to the cave's east side (REACH len=3 from the
        -- deep landing), the cave (quest_desertrescue.rs2:354-357 p_teleport
        -- 3278,9415), the walk round to the door, the door.
        t.exec("leaveDeepMine", t.player.cross_gate, DEEP_OUT)
        t.ticks(2)
        t.exec("leaveMineForAna-route", t.player.walk_route, GUARD_TO_DOOR, { level = 0 })
        -- Same door script as leaveMine (seam23 fact m): cross_gate passes on
        -- the surface tile, past the door script's p_delay(2), before the
        -- winch's own suspending press.
        t.exec("leaveMineForAna", t.player.cross_gate, MINE_OUT)
        t.ticks(2)

        -- In-camp travel (reach.py 3301,3036 -> 3280,3018 REACH len=39).
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

        -- In-camp travel (3289,3025 was a solid tile; 3289,3026 REACH).
        t.exec("goto-useBarrelOnCart", t.player.goto_tile, 3289, 3026, 0)
        t.exec("useBarrelOnCart", t.player.use_on, "thanainabarrel", t.player.by_symbol("loc", "tourtrap_qip_multi_flatback_cart"))
        t.exec("useBarrelOnCart-note", t.chat.drain, { shots = true, max_pages = 10 })
        t.exec("quest.stage.ana_on_mining_cart", t.var.await_server, "varp197_desertrescue", 25, 15)
        item_gone("useBarrelOnCart-barrelGone", "thanainabarrel") -- quest_desertrescue.rs2:783

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
        -- The escape: [oploc2,tourtrap_qip_multi_flatback_cart] p_teleport(0_50_47_58_21)
        -- = 3258,3029, past the gates on the desert side.
        local _, escTile = t.world.tile()
        t.check("useBarrelOnCart2-escaped", escTile ~= nil and escTile.level == 0 and escTile.x <= 3272
                and math.abs(escTile.x - 3258) <= 1 and math.abs(escTile.z - 3029) <= 1,
            "after the cart: " .. tostring(escTile and escTile.x) .. "," .. tostring(escTile and escTile.z) .. ","
                .. tostring(escTile and escTile.level) .. " (want within 1 of 3258,3029,0, outside the camp)")

        -- ================= Irena; Ana; the reward =================
        t.exec("unequipShirt", t.player.unequip, "slave_shirt")
        t.exec("unequipRobe", t.player.unequip, "slave_robe")
        t.exec("unequipBoots", t.player.unequip, "slave_boots")

        -- Overland desert travel (reach.py 3258,3029 -> 3303,3111 REACH len=127):
        -- Irena stands south of the Shantay Pass, still in the desert.
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
