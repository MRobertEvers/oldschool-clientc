-- Cabin Fever, end to end through the real client (b73 author pass).
--
-- Spec: Quest Helper CabinFever.java (steps.put 0..130 + the ConditionalStep
-- leaves), wiki Cabin_Fever oldid 15365342, Cabin_Fever/Quick_guide oldid
-- 14478674, Transcript:Cabin_Fever oldid 15263353, Cannon_(Cabin_Fever)
-- oldid 15241817. Content: OSRS-Content quest_cabinfever (rewritten in b73 to
-- those sources; configs/cabinfever.constant names every one).
--
-- WALLS (door rule). Every goto departs from and lands on open ground; every
-- gate, barrier, door, gangplank, ladder and net is pressed on every visit.
-- Checked with sample_tools/reach.py (doors closed, --root this worktree):
--   * Lumbridge 3206,3233 -> the Varrock members' gate 3318,3468, then
--     Morytania the way Priest in Peril opens it (rumdeal.lua's route): the
--     Paterdomus trapdoor, the two mausoleum gates, Drezel's advice (60 -> 61)
--     and the holy barrier (lands 3423,3485).
--   * 3423,3485 -> 3651,3485, outside Port Phasmatys' west Energy Barrier:
--     REACH len 294. op4 Pay-toll (2 ecto-tokens; wiki walkthrough "Bring
--     ecto-tokens") into the town, x >= 3653 (ahoy_hub.rs2).
--   * 3653,3485 -> 3669,3497 (the Green Ghost's door, ahoy_harbour_door
--     3670,3497): REACH len 28; the door both ways; 3669,3497 -> 3709,3496
--     (the east dock, west of fever_gangplank 3710,3496): REACH len 51.
--   * Aboard The Adventurous every level change is a ladder or a net
--     (maps/m28_75; the nets' landings are quest_cabinfever/configs/
--     cabinfever_maplink.dbrow), every ship-to-ship crossing a rope swing.
--     No goto on the battle map. The finale fades to The Other Inn on Mos
--     Le'Harmless (the quest's own move, cabinfever_shared.rs2).
--
-- Setup: the three prerequisite quests (::complete their dbrows), the four
-- requirement skills at exactly the requirement, Priest in Peril's
-- Wolfbane dagger (Drezel's barrier advice needs it, mausoleum_drezel.rs2),
-- two ecto-tokens for the toll, rune armour and sharks for the level-57
-- pirates (Quest Helper getCombatRequirements "Able to survive multiple
-- pirates (level 57) attacking you"; getItemRecommended food). Defence and
-- Hitpoints 70 are a margin: no Cabin Fever script reads a combat stat
-- (grep quest_cabinfever: only ~cabinfever_meets_requirements reads stats,
-- the four requirement skills). Ranged stays at the requirement, 40: the
-- cannon's hit roll reads it (Cannon page, low 15 high 240), so the firing
-- loops are bounded retries, not a raised stat.

return {
    id = "cabinfever",
    fixture = "fresh_lumbridge.ini",
    max_frames = 300000, -- Morytania on foot, two crossings each way, and two firing loops of up to 30 shots
    setup = {
        "::clearinv",
        "::complete quest_piratestreasure",
        "::complete quest_rumdeal",
        "::complete quest_priestinperil",
        "::setlevel agility 42",
        "::setlevel crafting 45",
        "::setlevel smithing 50",
        "::setlevel ranged 40",
        "::setlevel defence 70",
        "::setlevel hitpoints 70",
        "::give dagger_wolfbane 1", -- Priest in Peril's reward; Drezel's holy-barrier advice needs it held (mausoleum_drezel.rs2:29-34)
        "::give ectotoken 2", -- Port Phasmatys toll (ahoy_hub.rs2 ^ahoy_barrier_toll)
        "::give rune_full_helm 1",
        "::give rune_chainbody 1",
        "::give rune_platelegs 1",
        "::give rune_kiteshield 1",
        "::give shark 8",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp655_fever_quest",
            constants = {
                not_started = 0, accepted = 10, set_sail = 20, sabotage = 30, sabotaged = 40,
                repair_hull = 50, hull_repaired = 60, plunder = 70, plundered = 80,
                fix_cannon = 90, cannon_fixed = 100, canisters = 110, canisters_done = 120,
                cannonballs = 130, complete = 140,
            },
            row = "quest_cabinfever",
            display = "Cabin Fever",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3)
        t.exec("quest.stage.not_started", t.quest.expect_stage, "not_started")

        for _, item in ipairs({ "rune_full_helm", "rune_chainbody", "rune_platelegs", "rune_kiteshield" }) do
            t.exec("setup.wear." .. item, t.player.equip, item)
        end

        -- ------------------------------------------------------------------
        -- Helpers. Reads only: the hit points (eat a shark below half), the
        -- tile, the stage, what the backpack holds.
        -- ------------------------------------------------------------------
        local hp_low = 99
        local function hp_watch(where)
            local r, hp = t.skill.read("hitpoints")
            if r ~= "ok" or type(hp) ~= "table" then
                return
            end
            if hp.level < hp_low then
                hp_low = hp.level
            end
            if hp.level < 35 then
                local cr, sharks = t.inv.count("shark")
                if cr == "ok" and sharks > 0 then
                    t.player.inv_op("shark", 1)
                    t.ticks(2)
                    t.note(where .. ": ate a shark at " .. hp.level .. " hp")
                end
            end
        end
        local function margin_row(name)
            local _, sharks = t.inv.count("shark")
            local r, hp = t.skill.read("hitpoints")
            t.check(name, r == "ok" and hp_low >= 18 and (sharks or 0) > 0,
                "lowest hitpoints seen " .. hp_low .. " of 70 (want >= 18, a quarter), now "
                    .. tostring(r == "ok" and hp.level or "?") .. ", sharks left " .. tostring(sharks))
        end
        local function stage()
            local _, v = t.quest.stage()
            return v or -1
        end
        local function count(item)
            local r, n = t.inv.count(item)
            if r ~= "ok" then
                return 0
            end
            return n or 0
        end
        local function tile_is(x, z, level)
            local r, tile = t.world.tile()
            return r == "ok" and tile.x == x and tile.z == z and tile.level == level, tile
        end
        local function tile_text()
            local r, tile = t.world.tile()
            if r ~= "ok" then
                return "?"
            end
            return tile.x .. "," .. tile.z .. "," .. tile.level
        end
        -- The swing lands on the other ship's mast top: 1823,4835,2 (enemy)
        -- or 1817,4830,2 (The Adventurous). It can fail (Transcript "Falling on
        -- a rope swing"; the roll is the Barbarian rope swing's,
        -- cabinfever_transport.rs2): the swimmer climbs aboard the far ship's
        -- main deck at the foot of its net, 1823,4835,1 / 1817,4831,1. Either
        -- way the player has crossed; a fall skips the climb down the net.
        -- Returns true when the swing fell.
        local function swing_landed(name, x, z, dx, dz)
            local function landed()
                return (tile_is(x, z, 2)) or (tile_is(dx, dz, 1))
            end
            t.await({ level = landed, note = name .. ": swung across" }, 10)
            if tile_is(dx, dz, 1) then
                t.exec(name .. ".fell", t.chat.play, { "mesbox:You fall in the water with a splash!",
                    "mesbox:You have to swim across; which puts a considerable drain on your energy." })
                t.check(name .. ".landed", (tile_is(dx, dz, 1)), "the swing fell: the player swam across to "
                    .. tile_text() .. " (want the far deck " .. dx .. "," .. dz .. ",1)")
                return true
            end
            t.check(name .. ".landed", (tile_is(x, z, 2)), "after the swing the player is at " .. tile_text()
                .. " (want the mast top " .. x .. "," .. z .. ",2, or the far deck " .. dx .. "," .. dz .. ",1 after a fall)")
            return false
        end
        -- Locker: open it if it stands closed, then the open leaf's Search.
        local function locker_open(name, closed, open, x, z)
            -- Right after a climb the hold's locs may not be drawn yet: wait
            -- for either leaf first.
            t.await({ level = function()
                return t.world.loc_near(open, 6) == "ok"
                    or t.world.loc_near(closed, 6) == "ok"
            end, note = name .. ": the locker is in view" }, 8)
            local r = t.world.loc_near(open, 6)
            if r ~= "ok" then
                t.exec(name .. ".open", t.player.click_loc, closed, 1, { at = { x, z, 0 } })
                t.await({ level = function() return t.world.loc_near(open, 6) == "ok" end,
                    note = name .. ": the locker opens" }, 6)
            end
        end
        local function pick(name, open, x, z, row, item, want)
            local before = count(item)
            t.exec(name .. ".search", t.player.click_loc, open, 1, { at = { x, z, 0 } })
            t.exec(name .. ".choose", t.chat.play, { "options", "choose:" .. row })
            t.exec(name .. ".got", t.inv.await, item, before + want, 6)
        end
        local function in_band(x, z, level)
            return level == 1 and x >= 1822 and x <= 1826 and z >= 4832 and z <= 4835
        end
        -- The guide: "Make sure pirates line up with your cannon before
        -- firing" (Quick guide). A visible read of the enemy deck.
        local function pirate_lined_up()
            for i = 1, 10 do
                local sym = string.format("fever_pirate_enemy_%02d", i)
                local r, _, rows = t.npc.tiles(sym, 16)
                if r == "ok" and type(rows) == "table" then
                    for _, row in ipairs(rows) do
                        if in_band(row.x, row.z, row.level) then
                            return true, sym .. " at " .. row.x .. "," .. row.z
                        end
                    end
                end
            end
            return false, "no pirate in the band x 1822-1826 z 4832-4835"
        end

        -- ------------------------------------------------------------------
        -- Into Morytania (rumdeal.lua's route).
        -- ------------------------------------------------------------------
        t.exec("goto-enterMorytania.varrockGate", t.player.goto_tile, 3318, 3468, 0)
        t.exec("enterMorytania.varrockGate", t.player.pass_door, { closed = "fai_varrock_member_gatel",
            open = "fai_varrock_member_gatel_open", at = { 3319, 3468, 0 }, near = { 3318, 3468 }, far = { 3321, 3468 } })
        t.exec("goto-enterMorytania.trapdoor", t.player.goto_tile, 3405, 3506, 0)
        t.exec("enterMorytania.openTrapdoor", t.player.click_loc, "trapdoor", 1, { at = { 3405, 3507, 0 } })
        t.await({
            level = function()
                return t.world.loc_near("trapdoor_open", 3, { at = { 3405, 3507, 0 } }) == "ok"
            end,
            note = "enterMorytania: the trapdoor opens",
        }, 6)
        local tdo_r = t.world.loc_near("trapdoor_open", 3, { at = { 3405, 3507, 0 } })
        t.check("enterMorytania.trapdoorOpen", tdo_r == "ok", "trapdoor_open on 3405,3507,0 -> " .. tostring(tdo_r))
        t.exec("enterMorytania.descend", t.player.cross_trap, { loc = "trapdoor_open", op = 1, op_name = "Climb-down",
            at = { 3405, 3507, 0 }, src = { 3405, 3506 }, dest = { 3405, 9906 }, attempts = 2 })
        t.exec("enterMorytania.gate1", t.player.cross_gate, { loc = "pip_underground_door1", at = { 3405, 9895, 0 },
            near = { 3405, 9896 }, far_ok = function(tile) return tile.z > 6400 and tile.z <= 9894 end,
            far_desc = "south of the golden-key gate, z <= 9894", ticks = 30 })
        t.exec("enterMorytania.gate2", t.player.cross_gate, { loc = "pip_underground_door2", at = { 3431, 9897, 0 },
            near = { 3430, 9897 }, far_ok = function(tile) return tile.z > 6400 and tile.x >= 3432 end,
            far_desc = "Drezel's side of the second gate, x >= 3432", ticks = 60 })
        t.exec("enterMorytania.talkToDrezel", t.player.talk_to, "priestperiltrappedmonk2", 1)
        t.exec("enterMorytania.talkToDrezel-dialog", t.chat.play, {
            "player:So can I pass through that barrier now?",
            "npc:Ah, ",
            "npc:Morytania is an evil land",
            "npc:You should take some basic precautions",
            "npc:In many ways Werewolves",
            "npc:and it is a holy relic",
            "npc:wolf form is incredibly powerful",
            "player:Okay, I will keep it equipped",
        })
        t.exec("enterMorytania.drezelAdvice", t.var.await_server, "varp302_priestperil", 61, 8)
        t.exec("enterMorytania.holyBarrier", t.player.cross_gate, { loc = "pip_underground_wall_side_withportal",
            at = { 3440, 9886, 0 }, near = { 3440, 9887 },
            far_ok = function(tile) return tile.x == 3423 and tile.z == 3485 end,
            far_desc = "east of the Salve at 3423,3485 (mausoleum_interactions.rs2 p_telejump(0_53_54_31_29))" })

        -- Port Phasmatys: the west Energy Barrier (3652,3485, faces east into
        -- the town), op4 Pay-toll.
        t.exec("goto-enterPhasmatys", t.player.goto_tile, 3651, 3485, 0)
        t.exec("enterPhasmatys", t.player.cross_gate, { loc = "ahoy_town_barrier_multi", op = 4, at = { 3652, 3485, 0 },
            near = { 3651, 3485 }, far_ok = function(tile) return tile.x >= 3653 end,
            far_desc = "inside Port Phasmatys, east of the west barrier, x >= 3653" })
        t.check("enterPhasmatys.toll", count("ectotoken") == 0, "ecto-tokens left " .. count("ectotoken") .. " (want 0: the 2-token toll)")

        -- ------------------------------------------------------------------
        -- talkToBill: The Green Ghost (ahoy_harbour_door 3670,3497).
        -- ------------------------------------------------------------------
        local INN_IN = { closed = "ahoy_harbour_door", open = "ahoy_harbour_door_open", at = { 3670, 3497, 0 },
            near = { 3669, 3497 }, far = { 3672, 3497 } }
        local INN_OUT = { closed = "ahoy_harbour_door", open = "ahoy_harbour_door_open", at = { 3670, 3497, 0 },
            near = { 3671, 3497 }, far = { 3669, 3497 } }
        t.exec("goto-talkToBill", t.player.goto_tile, 3669, 3497, 0)
        t.exec("talkToBill.doorIn", t.player.pass_door, INN_IN)
        t.exec("talkToBill", t.player.talk_to, "fever_teach", 1)
        t.exec("talkToBill-dialog", t.chat.play, {
            "npc:Mumblelandlubber",
            "player:Hello!",
            "npc:What right have ye",
            "player:Well it is quite a nice day.",
            "npc:Aye, that it is.",
            "npc:Will ye forgive an old sailor",
            "player:What's the problem?",
            "npc:I dunno",
            "npc:Well, ye look like ye might have the right stuff",
            "options",
            "choose:Yes, I've always wanted to be a pirate!",
            "player:Yes, I've always wanted to be a pirate!",
            "npc:I don't think ye could pirate",
            "player:Awww...",
            "npc:But since nobody else",
            "npc:I be Bill Teach",
            "npc:I'm in port doin'",
            "player:Mos Le'Harmless? Where is that?",
            "npc:It's a pirate stronghold",
            "npc:Well, one of the best",
            "player:Will it be OK for me",
            "npc:Of course",
            "npc:This brings me back",
            "npc:Last night I got into a war",
            "player:How horrible!",
            "npc:By desertin'.",
            "player:All of them?",
            "npc:Every last one!",
            "npc:The second I leave port",
            "npc:Ye said ye would help me out",
            "options",
            "choose:/^Yes, I am a %a+ of my word%.$/",
            "player:of my word.",
            "npc:Ye struck me as a",
            "npc:My ship is the Adventurous",
            "player:You won't...err",
            "npc:I'm sure I won't.",
        })
        t.exec("quest.stage.accepted", t.quest.expect_stage, "accepted")
        t.exec("talkToBill.doorOut", t.player.pass_door, INN_OUT)

        -- goOnBillBoat / talkToBillOnBoat: the east dock's gangplank, then Bill
        -- aboard (fever_port_ship_teach, shown as Bill from stage 10).
        t.exec("goto-goOnBillBoat", t.player.goto_tile, 3709, 3496, 0)
        -- The gangplank is stored on raw level 1 (the client's copy is
        -- 3710,3496,1, a bridge deck): loc_level names it.
        t.exec("goOnBillBoat", t.player.climb, { loc = "fever_gangplank", op = 1, op_name = "Cross",
            at = { 3710, 3496, 0 }, loc_level = 1, dest = { 3712, 3496, 1 } })
        t.exec("talkToBillOnBoat", t.player.talk_to, "fever_port_ship_teach", 1)
        t.exec("talkToBillOnBoat-dialog", t.chat.play, {
            "npc:Ye came!",
            "player:You seem rather surprised.",
            "npc:Well, I had the feelin'",
            "player:Don't ye worry about me",
            "npc:Aye, I should have remembered",
            "npc:Ye all ready to go",
            "options",
            "choose:Let's go Cap'n!",
            "player:Let's go Cap'n!",
        })
        -- The ship sails: a fade to black and the jump to the battle
        -- (cabinfever_shared.rs2 [proc,cabinfever_fade_to]), then the opening
        -- attack's conversation.
        t.await({ level = function() return (tile_is(1815, 4834, 1)) end, note = "talkToBillOnBoat: The Adventurous sails" }, 20)
        t.check("talkToBillOnBoat.atSea", (tile_is(1815, 4834, 1)), "after setting sail the player is at "
            .. tile_text() .. " (want The Adventurous' deck 1815,4834,1)")
        t.await({ level = function() return t.chat.kind() ~= "none" end, note = "talkToBillOnBoat: the opening scene" }, 10)
        t.exec("talkToBillOnBoat-scene", t.chat.play, {
            "player:Well, it looks like they've found us",
            "npc:Ye don't say",
            "npc:Well, without anyone else",
            "player:What will you be doing?",
            "npc:Well if their first shot",
            "npc:Arr! They seek te hole the ship!",
            "player:How?",
            "npc:If ye grab some fuse",
            "npc:Just take the fuse",
            "npc:A lantern, a tinderbox",
            "npc:End of problem.",
            "player:How do I get back?",
            "npc:They have masts as well",
            "npc:Just do the same trick",
            "player:Arr! I'll hop to it Cap'n!",
            "npc:Stop doin' that!",
            "npc:Ye ain't a pirate yet",
            "npc:I'll be on deck",
            "player:Now to take care of that cannon!",
        })
        t.exec("quest.stage.sabotage", t.quest.expect_stage, "sabotage")
        t.exec("talkToBillOnBoat.cannonBroken", t.var.await_server, "varb1741_fever_cannon", 1, 4)

        -- ------------------------------------------------------------------
        -- Sabotage (stage 30).
        -- ------------------------------------------------------------------
        t.exec("enterHull", t.player.climb, { loc = "fever_ship_laddertop", op = 1, op_name = "Climb-down",
            at = { 1815, 4836, 1 }, src = { 1815, 4837 }, dest = { 1815, 4837, 0 } })
        -- take4Ropes: the Repair Locker, one rope per search (Repair_Locker page).
        locker_open("take4Ropes", "fever_repair_locker", "fever_repair_locker_open", 1814, 4832)
        t.exec("take4Ropes", t.player.click_loc, "fever_repair_locker_open", 1, { at = { 1814, 4832, 0 } })
        t.exec("take4Ropes.choose", t.chat.play, { "options", "choose:Rope" })
        t.exec("take4Ropes.got", t.inv.await, "rope", 1, 6)
        for i = 2, 4 do
            locker_open("take4Ropes.rope" .. i, "fever_repair_locker", "fever_repair_locker_open", 1814, 4832)
            pick("take4Ropes.rope" .. i, "fever_repair_locker_open", 1814, 4832, "Rope", "rope", 1)
        end
        -- take1Fuse: the Gun Locker.
        locker_open("take1Fuse", "fever_weapons_locker", "fever_weapons_locker_open", 1816, 4833)
        t.exec("take1Fuse", t.player.click_loc, "fever_weapons_locker_open", 1, { at = { 1816, 4833, 0 } })
        t.exec("take1Fuse.choose", t.chat.play, { "options", "choose:Fuse" })
        t.exec("take1Fuse.got", t.inv.await, "fever_fuse", 1, 6)
        -- takeTinderbox: the floor spawn at the south end of the hold.
        t.exec("takeTinderbox", t.player.click_obj, "tinderbox", 3)
        t.exec("takeTinderbox.got", t.inv.await, "tinderbox", 1, 6)
        t.exec("leaveHullForSabo", t.player.climb, { loc = "fever_ship_ladder", op = 1, op_name = "Climb-up",
            at = { 1815, 4836, 0 }, src = { 1815, 4837 }, dest = { 1815, 4837, 1 } })
        t.exec("climbUpNetForSabo", t.player.climb, { loc = "fever_climbing_net", op = 1, op_name = "Climb",
            at = { 1816, 4831, 1 }, src = { 1817, 4831 }, dest = { 1817, 4830, 2 } })
        local ropes0 = count("rope")
        t.exec("useRopeOnSailForSabo", t.player.use_on, "rope", t.player.by_symbol("loc", "fever_sail1_hoistedl_climb"))
        local fell1 = swing_landed("useRopeOnSailForSabo", 1823, 4835, 1823, 4835)
        t.check("useRopeOnSailForSabo.ropeSpent", count("rope") == ropes0 - 1, "ropes " .. ropes0 .. " -> " .. count("rope") .. " (want one spent)")
        if not fell1 then
            t.exec("leaveEnemySail", t.player.climb, { loc = "fever_climb_down_location", op = 1, op_name = "Climb-down",
                at = { 1823, 4834, 2 }, src = { 1823, 4835 }, dest = { 1823, 4835, 1 } })
        end
        hp_watch("leaveEnemySail")
        -- pickUpRope: the spare on the enemy deck's south-west corner.
        local ropes1 = count("rope")
        t.exec("pickUpRope", t.player.click_obj, "rope", 3)
        t.exec("pickUpRope.got", t.inv.await, "rope", ropes1 + 1, 6)
        hp_watch("pickUpRope")
        t.exec("useFuseOnEnemyBarrel", t.player.use_on, "fever_fuse", t.player.by_symbol("loc", "fever_multi_gunpowder_barrel"))
        t.exec("useFuseOnEnemyBarrel-page", t.chat.play, { "mesbox:You attach the fuse to the barrel." })
        t.exec("useFuseOnEnemyBarrel.fused", t.var.await_server, "varb1756_fever_gunpowder_barrel", 2, 4)
        hp_watch("useFuseOnEnemyBarrel")
        -- The light can fail ("The fuse refuses to light. Try again."; Quick
        -- guide "this can fail"; a Firemaking 64/512 roll at Firemaking 1,
        -- cabinfever_sabotage.rs2): press again, at most 30 times.
        local lights = 0
        local lit = false
        while not lit and lights < 30 do
            lights = lights + 1
            t.exec("lightEnemyFuse", t.player.use_on, "tinderbox", t.player.by_symbol("loc", "fever_multi_fuse_2"))
            t.await({ level = function() return t.chat.kind() == "mesbox" end, note = "lightEnemyFuse: the fuse's answer" }, 6)
            local _, text = t.chat.text()
            if type(text) == "string" and text:find("refuses to light", 1, true) then
                t.exec("lightEnemyFuse-refused", t.chat.play, { "mesbox:The fuse refuses to light. Try again." })
            else
                lit = true
                t.exec("lightEnemyFuse-page", t.chat.play, { "mesbox:You light the fuse...", "mesbox:...and destroy the barrel and the cannon." })
            end
            hp_watch("lightEnemyFuse")
        end
        t.check("lightEnemyFuse.tries", lit, "the fuse lit on press " .. lights .. " of at most 30")
        t.exec("quest.stage.sabotaged", t.quest.expect_stage, "sabotaged")
        t.exec("lightEnemyFuse.enemyCannon", t.var.await_server, "varb1749_fever_enemy_cannon", 1, 4)
        hp_watch("lightEnemyFuse")
        t.exec("climbEnemyNetAfterSabo", t.player.climb, { loc = "fever_climbing_net", op = 1, op_name = "Climb",
            at = { 1823, 4834, 1 }, src = { 1823, 4835 }, dest = { 1823, 4835, 2 } })
        t.exec("useRopeOnEnemySailAfterSabo", t.player.use_on, "rope", t.player.by_symbol("loc", "fever_sail1_hoistedl_climb"))
        local fell2 = swing_landed("useRopeOnEnemySailAfterSabo", 1817, 4830, 1817, 4831)
        margin_row("sabotage.margin")
        if not fell2 then
            t.exec("leaveSail", t.player.climb, { loc = "fever_climb_down_location", op = 1, op_name = "Climb-down",
                at = { 1816, 4831, 2 }, src = { 1817, 4830 }, dest = { 1817, 4831, 1 } })
        end
        t.exec("talkToBillAfterSabo", t.player.talk_to, "fever_quest_ship_teach", 1)
        t.exec("talkToBillAfterSabo-dialog", t.chat.play, {
            "player:Arr! I've blown their cannon to smithereens",
            "npc:Yer not a pirate yet",
            "npc:Well, ye've sorted out their cannon",
            "npc:Go below deck and sort out the leaks.",
            "player:Will smearing swamp paste",
            "npc:No ",
            "npc:Ye'll need to get some planks",
            "player:Arr! I'll have this ship ship-shape",
            "npc:Wait 'till we make landfall",
        })
        t.exec("quest.stage.repair_hull", t.quest.expect_stage, "repair_hull")

        -- ------------------------------------------------------------------
        -- Repairs (stage 50).
        -- ------------------------------------------------------------------
        t.exec("goDownToFixLeak", t.player.climb, { loc = "fever_ship_laddertop", op = 1, op_name = "Climb-down",
            at = { 1815, 4836, 1 }, src = { 1815, 4837 }, dest = { 1815, 4837, 0 } })
        locker_open("takeHoleItems1", "fever_repair_locker", "fever_repair_locker_open", 1814, 4832)
        t.exec("takeHoleItems1", t.player.click_loc, "fever_repair_locker_open", 1, { at = { 1814, 4832, 0 } })
        t.exec("takeHoleItems1.choose", t.chat.play, { "options", "choose:Hammer" })
        t.exec("takeHoleItems1.got", t.inv.await, "hammer", 1, 6)
        for i = 1, 3 do
            locker_open("takeHoleItems1.planks" .. i, "fever_repair_locker", "fever_repair_locker_open", 1814, 4832)
            pick("takeHoleItems1.planks" .. i, "fever_repair_locker_open", 1814, 4832, "Repair plank", "fever_repair_plank", 2)
            locker_open("takeHoleItems1.tacks" .. i, "fever_repair_locker", "fever_repair_locker_open", 1814, 4832)
            pick("takeHoleItems1.tacks" .. i, "fever_repair_locker_open", 1814, 4832, "Tacks", "fever_tack", 10)
            locker_open("takeHoleItems1.paste" .. i, "fever_repair_locker", "fever_repair_locker_open", 1814, 4832)
            pick("takeHoleItems1.paste" .. i, "fever_repair_locker_open", 1814, 4832, "Swamp paste", "swamppaste", 1)
        end
        t.check("takeHoleItems1.kit", count("hammer") == 1 and count("fever_repair_plank") == 6
            and count("fever_tack") == 30 and count("swamppaste") == 3,
            "hammer " .. count("hammer") .. ", planks " .. count("fever_repair_plank") .. ", tacks "
                .. count("fever_tack") .. ", paste " .. count("swamppaste") .. " (want 1/6/30/3)")
        t.exec("repairHole1", t.player.click_loc, "fever_multi_hole_1", 1)
        t.exec("repairHole1-page", t.chat.play, { "mesbox:You nail the Planks over the the hole!" })
        t.exec("repairHole1.planked", t.var.await_server, "varb1751_fever_hole_1", 1, 4)
        t.exec("pasteHole1", t.player.click_loc, "fever_multi_hole_1", 1)
        t.exec("pasteHole1-page", t.chat.play, { "mesbox:You smear the Swamp Paste over the hole!" })
        t.exec("pasteHole1.proofed", t.var.await_server, "varb1751_fever_hole_1", 2, 4)
        t.exec("repairHole2", t.player.click_loc, "fever_multi_hole_2", 1)
        t.exec("repairHole2-page", t.chat.play, { "mesbox:You nail the Planks over the the hole!" })
        t.exec("repairHole2.planked", t.var.await_server, "varb1757_fever_hole_2", 1, 4)
        t.exec("pasteHole2", t.player.click_loc, "fever_multi_hole_2", 1)
        t.exec("pasteHole2-page", t.chat.play, { "mesbox:You smear the Swamp Paste over the hole!" })
        t.exec("pasteHole2.proofed", t.var.await_server, "varb1757_fever_hole_2", 2, 4)
        t.exec("repairHole3", t.player.click_loc, "fever_multi_hole_3", 1)
        t.exec("repairHole3-page", t.chat.play, { "mesbox:You nail the Planks over the the hole!" })
        t.exec("repairHole3.planked", t.var.await_server, "varb1758_fever_hole_3", 1, 4)
        t.exec("pasteHole3", t.player.click_loc, "fever_multi_hole_3", 1)
        t.exec("pasteHole3-page", t.chat.play, { "mesbox:You smear the Swamp Paste over the hole!" })
        t.exec("pasteHole3.proofed", t.var.await_server, "varb1758_fever_hole_3", 2, 4)
        t.exec("quest.stage.hull_repaired", t.quest.expect_stage, "hull_repaired")
        t.exec("goUpAfterRepair", t.player.climb, { loc = "fever_ship_ladder", op = 1, op_name = "Climb-up",
            at = { 1815, 4836, 0 }, src = { 1815, 4837 }, dest = { 1815, 4837, 1 } })
        t.exec("talkToBillAfterRepair", t.player.talk_to, "fever_quest_ship_teach", 1)
        t.exec("talkToBillAfterRepair-dialog", t.chat.play, {
            "player:The holes be plugged Cap'n!",
            "npc:Nailed shut and waterproofed?",
            "player:Aye!",
            "npc:Good work",
            "npc:Why not pay them a visit",
            "player:Arr! I'll plunder their booty",
            "npc:Gadzooks...",
            "npc:Come back here and drop it",
            "player:Aye aye Cap'n! One plunderin'",
            "npc:I've been a pirate all me life",
            "npc:Stop it!",
        })
        t.exec("quest.stage.plunder", t.quest.expect_stage, "plunder")

        -- ------------------------------------------------------------------
        -- Plunder (stage 70). Two ropes are needed for the trip; the four
        -- from the locker plus the deck spare leave three.
        -- ------------------------------------------------------------------
        t.check("plunder.ropes", count("rope") >= 2, "ropes held " .. count("rope") .. " (want >= 2: Quest Helper ropes2)")
        t.exec("goUpToSailToLoot", t.player.climb, { loc = "fever_climbing_net", op = 1, op_name = "Climb",
            at = { 1816, 4831, 1 }, src = { 1817, 4831 }, dest = { 1817, 4830, 2 } })
        t.exec("useRopeOnSailToLoot", t.player.use_on, "rope", t.player.by_symbol("loc", "fever_sail1_hoistedl_climb"))
        if not swing_landed("useRopeOnSailToLoot", 1823, 4835, 1823, 4835) then
            t.exec("leaveEnemySail.toLoot", t.player.climb, { loc = "fever_climb_down_location", op = 1, op_name = "Climb-down",
                at = { 1823, 4834, 2 }, src = { 1823, 4835 }, dest = { 1823, 4835, 1 } })
        end
        hp_watch("leaveEnemySail.toLoot")
        t.exec("enterEnemyHullForLoot", t.player.climb, { loc = "fever_ship_laddertop", op = 1, op_name = "Climb-down",
            at = { 1824, 4829, 1 }, src = { 1824, 4828 }, dest = { 1824, 4828, 0 } })
        hp_watch("enterEnemyHullForLoot")
        -- lootEnemyShip: chest 3, crate 2, barrel 1 (the three loc pages).
        t.exec("lootEnemyShip", t.player.click_loc, "fever_multi_chest", 1)
        t.exec("lootEnemyShip-page", t.chat.play, { "mesbox:You find some plunder!" })
        t.exec("lootEnemyShip.chest", t.inv.await, "fever_plunder", 3, 6)
        hp_watch("lootEnemyShip.chest")
        t.exec("lootEnemyShip.crate", t.player.click_loc, "fever_multi_crate", 1)
        t.exec("lootEnemyShip.crate-page", t.chat.play, { "mesbox:You find some plunder!" })
        t.exec("lootEnemyShip.crateGot", t.inv.await, "fever_plunder", 5, 6)
        hp_watch("lootEnemyShip.crate")
        t.exec("lootEnemyShip.barrel", t.player.click_loc, "fever_multi_barrel", 1)
        t.exec("lootEnemyShip.barrel-page", t.chat.play, { "mesbox:You find some plunder!" })
        t.exec("lootEnemyShip.barrelGot", t.inv.await, "fever_plunder", 6, 6)
        hp_watch("lootEnemyShip.barrel")
        t.exec("lootEnemyShip.chestEmpty", t.player.click_loc, "fever_multi_chest", 1)
        t.exec("lootEnemyShip.chestEmpty-page", t.chat.play, { "mesbox:This chest has been recently plundered." })
        -- hopWorld: "Hop worlds so that the chest resets" -- a logout and back
        -- in (the loc pages: "logging out or switching worlds will cause it to
        -- respawn immediately"). Nobody logs out under attack, and the hold's
        -- pirates keep attacking: the wiki's way out of them is the mast ("If
        -- you need to escape from them for a moment, climb up the net on the
        -- main mast"), so the logout is made on the enemy mast top.
        t.exec("hopWorld.ladderUp", t.player.climb, { loc = "fever_ship_ladder", op = 1, op_name = "Climb-up",
            at = { 1824, 4829, 0 }, src = { 1824, 4828 }, dest = { 1824, 4828, 1 } })
        t.exec("hopWorld.netUp", t.player.climb, { loc = "fever_climbing_net", op = 1, op_name = "Climb",
            at = { 1823, 4834, 1 }, src = { 1823, 4835 }, dest = { 1823, 4835, 2 } })
        t.ticks(20)
        local relog_r, relog_d = t.session.relog()
        t.check("hopWorld", relog_r == "ok", "session.relog -> " .. tostring(relog_r) .. " " .. tostring(relog_d)
            .. "; player at " .. tile_text())
        t.exec("hopWorld.chestReset", t.var.await_server, "varb1754_fever_chest", 0, 6)
        t.exec("hopWorld.netDown", t.player.climb, { loc = "fever_climb_down_location", op = 1, op_name = "Climb-down",
            at = { 1823, 4834, 2 }, src = { 1823, 4835 }, dest = { 1823, 4835, 1 } })
        t.exec("hopWorld.ladderDown", t.player.climb, { loc = "fever_ship_laddertop", op = 1, op_name = "Climb-down",
            at = { 1824, 4829, 1 }, src = { 1824, 4828 }, dest = { 1824, 4828, 0 } })
        hp_watch("hopWorld.ladderDown")
        t.exec("lootEnemyShip.chest2", t.player.click_loc, "fever_multi_chest", 1)
        t.exec("lootEnemyShip.chest2-page", t.chat.play, { "mesbox:You find some plunder!" })
        t.exec("lootEnemyShip.chest2Got", t.inv.await, "fever_plunder", 9, 6)
        hp_watch("lootEnemyShip.chest2")
        t.exec("lootEnemyShip.barrel2", t.player.click_loc, "fever_multi_barrel", 1)
        t.exec("lootEnemyShip.barrel2-page", t.chat.play, { "mesbox:You find some plunder!" })
        t.exec("lootEnemyShip.barrel2Got", t.inv.await, "fever_plunder", 10, 6)
        hp_watch("lootEnemyShip.barrel2")
        t.exec("leaveEnemyHullWithLoot", t.player.climb, { loc = "fever_ship_ladder", op = 1, op_name = "Climb-up",
            at = { 1824, 4829, 0 }, src = { 1824, 4828 }, dest = { 1824, 4828, 1 } })
        hp_watch("leaveEnemyHullWithLoot")
        t.exec("climbNetWithLoot", t.player.climb, { loc = "fever_climbing_net", op = 1, op_name = "Climb",
            at = { 1823, 4834, 1 }, src = { 1823, 4835 }, dest = { 1823, 4835, 2 } })
        t.exec("useRopeOnSailWithLoot", t.player.use_on, "rope", t.player.by_symbol("loc", "fever_sail1_hoistedl_climb"))
        local fell4 = swing_landed("useRopeOnSailWithLoot", 1817, 4830, 1817, 4831)
        margin_row("plunder.margin")
        if not fell4 then
            t.exec("leaveSail.withLoot", t.player.climb, { loc = "fever_climb_down_location", op = 1, op_name = "Climb-down",
                at = { 1816, 4831, 2 }, src = { 1817, 4830 }, dest = { 1817, 4831, 1 } })
        end
        t.exec("enterHullWithLoot", t.player.climb, { loc = "fever_ship_laddertop", op = 1, op_name = "Climb-down",
            at = { 1815, 4836, 1 }, src = { 1815, 4837 }, dest = { 1815, 4837, 0 } })
        t.exec("useLootOnChest", t.player.use_on, "fever_plunder", t.player.by_symbol("loc", "fever_plunder_deposit"))
        t.exec("useLootOnChest-page", t.chat.play, { "mesbox:You deposit 10 loads of plunder in the chest." })
        t.exec("useLootOnChest.stored", t.var.await_server, "varb1752_fever_plunder_points", 10, 4)
        t.exec("quest.stage.plundered", t.quest.expect_stage, "plundered")
        t.exec("goUpAfterLoot", t.player.climb, { loc = "fever_ship_ladder", op = 1, op_name = "Climb-up",
            at = { 1815, 4836, 0 }, src = { 1815, 4837 }, dest = { 1815, 4837, 1 } })
        t.exec("talkToBillAfterLoot", t.player.talk_to, "fever_quest_ship_teach", 1)
        t.exec("talkToBillAfterLoot-dialog", t.chat.play, {
            "npc:I warn ye",
            "player:Ahoy shipmate!",
            "npc:I should have seen that comin'",
            "player:Cap'n! I've put ten loads",
            "player:Are we all done now?",
            "npc:Not yet!",
            "player:How, the cannon is broken.",
            "npc:Alright",
            "npc:See, there is a secret technique",
            "npc:We call this magical process",
            "player:I know what repairing is!",
            "player:How do I repair the cannon?",
            "npc:Ye need to remove the old barrel",
            "player:And then what?",
            "npc:I don't want to overload ye",
        })
        t.exec("quest.stage.fix_cannon", t.quest.expect_stage, "fix_cannon")

        -- ------------------------------------------------------------------
        -- The cannon (stage 90): a new barrel from the Gun Locker.
        -- ------------------------------------------------------------------
        t.exec("goDownForBarrel", t.player.climb, { loc = "fever_ship_laddertop", op = 1, op_name = "Climb-down",
            at = { 1815, 4836, 1 }, src = { 1815, 4837 }, dest = { 1815, 4837, 0 } })
        locker_open("takeBarrel", "fever_weapons_locker", "fever_weapons_locker_open", 1816, 4833)
        t.exec("takeBarrel", t.player.click_loc, "fever_weapons_locker_open", 1, { at = { 1816, 4833, 0 } })
        t.exec("takeBarrel.choose", t.chat.play, { "options", "choose:Cannon barrel" })
        t.exec("takeBarrel.got", t.inv.await, "fever_cannon", 1, 6)
        t.exec("goUpWithBarrel", t.player.climb, { loc = "fever_ship_ladder", op = 1, op_name = "Climb-up",
            at = { 1815, 4836, 0 }, src = { 1815, 4837 }, dest = { 1815, 4837, 1 } })
        t.exec("useBarrel", t.player.click_loc, "fever_multi_cannon", 1)
        t.await({ level = function() return t.chat.kind() == "mesbox" end, note = "useBarrel: the barrel is swapped" }, 6)
        t.exec("useBarrel-page", t.chat.play, { "mesbox:You attach the new barrel to the cannon." })
        t.exec("useBarrel.intact", t.var.await_server, "varb1741_fever_cannon", 0, 4)
        t.exec("quest.stage.cannon_fixed", t.quest.expect_stage, "cannon_fixed")
        t.exec("talkToBillAfterBarrel", t.player.talk_to, "fever_quest_ship_teach", 1)
        t.exec("talkToBillAfterBarrel-dialog", t.chat.play, {
            "player:Cap'n I...",
            "npc:Have ye fixed the cannon yet",
            "player:Yes.",
            "npc:Ye did? Great!",
            "npc:Now ye can give those pirates",
            "npc:Ye'll need to load the cannon",
            "npc:First, ye take some powder",
            "npc:Then ye use a ramrod",
            "npc:Then ye use a canister round",
            "npc:Finally ye use some fuse",
            "npc:Then, ye'll need to use the ramrod",
            "npc:Ye got all that?",
            "player:Yes, but if I forget",
            "npc:And te think this was all",
        })
        t.exec("quest.stage.canisters", t.quest.expect_stage, "canisters")

        -- ------------------------------------------------------------------
        -- Canisters (stage 110): powder, ramrod, canister, fuse, Fire!, ramrod
        -- to clean (Cannon page "Firing procedure"); three pirates down.
        -- A bounded loop: the roll is the cannon's own (Ranged 40).
        -- ------------------------------------------------------------------
        t.exec("goDownForRamrod", t.player.climb, { loc = "fever_ship_laddertop", op = 1, op_name = "Climb-down",
            at = { 1815, 4836, 1 }, src = { 1815, 4837 }, dest = { 1815, 4837, 0 } })
        locker_open("getRamrod", "fever_weapons_locker", "fever_weapons_locker_open", 1816, 4833)
        t.exec("getRamrod", t.player.click_loc, "fever_weapons_locker_open", 1, { at = { 1816, 4833, 0 } })
        t.exec("getRamrod.choose", t.chat.play, { "options", "choose:Ramrod" })
        t.exec("getRamrod.got", t.inv.await, "fever_cannon_prod", 1, 6)
        -- Up to four fuses and four rounds held (the walkthrough: "at least 4
        -- is recommended"); the backpack also carries the food.
        local function stock(name, ammo_row, ammo)
            for i = 1, 4 do
                if count("fever_fuse") < 4 then
                    locker_open(name .. ".fuse" .. i, "fever_weapons_locker", "fever_weapons_locker_open", 1816, 4833)
                    pick(name .. ".fuse" .. i, "fever_weapons_locker_open", 1816, 4833, "Fuse", "fever_fuse", 1)
                end
                if count(ammo) < 4 then
                    locker_open(name .. ".ammo" .. i, "fever_weapons_locker", "fever_weapons_locker_open", 1816, 4833)
                    pick(name .. ".ammo" .. i, "fever_weapons_locker_open", 1816, 4833, ammo_row, ammo, 1)
                end
            end
        end
        -- A cannon that blew up (Cannon page: "The exploded gun barrel will
        -- then need to be replaced in the same way as at the beginning") is
        -- repaired the guide's way: takeBarrel / goUpWithBarrel / useBarrel
        -- (Quest Helper fireCannons[cannonBroken] leaves).
        local function repair_if_broken(prefix)
            local _, state = t.var.varbit("varb1741_fever_cannon")
            if state ~= 1 then
                return
            end
            t.exec(prefix .. ".goDownForBarrel", t.player.climb, { loc = "fever_ship_laddertop", op = 1, op_name = "Climb-down",
                at = { 1815, 4836, 1 }, src = { 1815, 4837 }, dest = { 1815, 4837, 0 } })
            locker_open(prefix .. ".takeBarrel", "fever_weapons_locker", "fever_weapons_locker_open", 1816, 4833)
            pick(prefix .. ".takeBarrel", "fever_weapons_locker_open", 1816, 4833, "Cannon barrel", "fever_cannon", 1)
            t.exec(prefix .. ".goUpWithBarrel", t.player.climb, { loc = "fever_ship_ladder", op = 1, op_name = "Climb-up",
                at = { 1815, 4836, 0 }, src = { 1815, 4837 }, dest = { 1815, 4837, 1 } })
            t.exec(prefix .. ".useBarrel", t.player.click_loc, "fever_multi_cannon", 1)
            t.await({ level = function() return t.chat.kind() == "mesbox" end, note = prefix .. ": the barrel is swapped" }, 6)
            t.exec(prefix .. ".useBarrel-page", t.chat.play, { "mesbox:You attach the new barrel to the cannon." })
            t.exec(prefix .. ".useBarrel.intact", t.var.await_server, "varb1741_fever_cannon", 0, 4)
        end
        stock("getRamrod", "Canister", "fever_cannister")
        t.exec("goUpToCannon", t.player.climb, { loc = "fever_ship_ladder", op = 1, op_name = "Climb-up",
            at = { 1815, 4836, 0 }, src = { 1815, 4837 }, dest = { 1815, 4837, 1 } })
        local cannon = t.player.by_symbol("loc", "fever_multi_cannon")
        local shots = 0
        while stage() >= 110 and stage() < 120 and shots < 30 do
            if count("fever_cannister") == 0 or count("fever_fuse") == 0 then
                t.exec("canisters.restock.down", t.player.climb, { loc = "fever_ship_laddertop", op = 1, op_name = "Climb-down",
                    at = { 1815, 4836, 1 }, src = { 1815, 4837 }, dest = { 1815, 4837, 0 } })
                stock("canisters.restock", "Canister", "fever_cannister")
                t.exec("canisters.restock.up", t.player.climb, { loc = "fever_ship_ladder", op = 1, op_name = "Climb-up",
                    at = { 1815, 4836, 0 }, src = { 1815, 4837 }, dest = { 1815, 4837, 1 } })
            end
            repair_if_broken("canisters.repair")
            shots = shots + 1
            t.exec("getPowder", t.player.click_loc, "fever_your_gunpowder_barrel", 1)
            t.exec("getPowder.got", t.inv.await, "fever_gunpowder", 1, 15)
            t.exec("usePowder", t.player.use_on, "fever_gunpowder", cannon)
            t.exec("usePowder-page", t.chat.play, { "mesbox:You pour the powder into the cannon." })
            t.exec("useRamrod", t.player.use_on, "fever_cannon_prod", cannon)
            t.exec("useRamrod-page", t.chat.play, { "mesbox:You shove the Ramrod into the cannon barrel." })
            t.exec("useCanister", t.player.use_on, "fever_cannister", cannon)
            t.exec("useCanister-page", t.chat.play, { "mesbox:You roll the canister round into the cannon." })
            t.exec("useFuse", t.player.use_on, "fever_fuse", cannon)
            t.exec("useFuse-page", t.chat.play, { "mesbox:You ready the cannon for firing." })
            t.exec("useFuse.armed", t.var.await_server, "varb1741_fever_cannon", 3, 4)
            -- "Make sure pirates line up with your cannon before firing."
            t.await({ level = function() return (pirate_lined_up()) end, note = "fireCannon: a pirate in the field of fire" }, 30)
            local lined, who = pirate_lined_up()
            local _, kills0 = t.var.server("varp7336_fever_canister_kills")
            t.exec("fireCannon", t.player.click_loc, "fever_multi_cannon", 1)
            t.exec("fireCannon-page", t.chat.play, { "mesbox:You fire the cannon at the crew!" })
            local _, verdict = t.chat.text()
            local hit = type(verdict) == "string" and verdict:find("You hit them!", 1, true) ~= nil
            t.chat.drain({})
            local _, kills1 = t.var.server("varp7336_fever_canister_kills")
            -- A hit is one more kill, a miss none: the page and the count must agree.
            t.check("fireCannon.shot" .. shots, kills0 ~= nil and kills1 ~= nil
                and ((hit and kills1 == kills0 + 1) or (not hit and kills1 == kills0)),
                "shot " .. shots .. ": " .. tostring(who) .. (lined and "" or " (fired anyway)") .. "; page '"
                    .. tostring(verdict) .. "'; kills " .. tostring(kills0) .. " -> " .. tostring(kills1)
                    .. "; stage " .. stage())
            t.exec("useRamrodToClean", t.player.use_on, "fever_cannon_prod", cannon)
            t.exec("useRamrodToClean-page", t.chat.play, { "mesbox:You clean out the cannon." })
            t.exec("useRamrodToClean.clean", t.var.await_server, "varb1746_fever_cannon_clean", 0, 4)
            hp_watch("canisters")
        end
        -- repeatCanisterSteps: "Repeat this 3-4 times until indicated to stop."
        t.check("repeatCanisterSteps", stage() == 120 and select(2, t.var.server("varp7336_fever_canister_kills")) == 3, "the load/fire/clean cycle ran " .. shots
            .. " time(s) until three pirates were down (stage " .. stage() .. ", want 120; at most 30 shots)")
        t.exec("quest.stage.canisters_done", t.quest.expect_stage, "canisters_done")
        t.exec("talkToBillAfterCanisterCannon", t.player.talk_to, "fever_quest_ship_teach", 1)
        t.exec("talkToBillAfterCanisterCannon-dialog", t.chat.play, {
            "player:Well, that seems to have showed them",
            "npc:Great! This ordeal will soon be over!",
            "player:Aye Cap'n, we'll beat these pirates yet!",
            "npc:Pirates?",
            "npc:Yes, that ordeal too!",
            "npc:we'll send them to the bottom of the sea.",
            "player:How?",
            "npc:Well, ye'll shoot holes",
            "npc:Essentially ye do what ye just did",
            "player:Arr! I'll sink them scurvy dogs!",
            "npc:Again with the 'Arr!'",
            "npc:Give it three tries",
        })
        t.exec("quest.stage.cannonballs", t.quest.expect_stage, "cannonballs")

        -- ------------------------------------------------------------------
        -- Cannon balls (stage 130): the Quick guide's sequence -- Empty-Out,
        -- ramrod to clean, powder, ramrod, ball, fuse, Fire! -- until three
        -- holes; the third is the finale and the quest's end.
        -- ------------------------------------------------------------------
        -- The canisters left over are no use against the hull.
        local drops = 0
        while count("fever_cannister") > 0 and drops < 6 do
            drops = drops + 1
            t.exec("goDownForBalls.dropCanister", t.player.drop, "fever_cannister")
            t.ticks(1)
        end
        t.exec("goDownForBalls", t.player.climb, { loc = "fever_ship_laddertop", op = 1, op_name = "Climb-down",
            at = { 1815, 4836, 1 }, src = { 1815, 4837 }, dest = { 1815, 4837, 0 } })
        locker_open("getBalls", "fever_weapons_locker", "fever_weapons_locker_open", 1816, 4833)
        local balls0 = count("fever_cannon_ball")
        t.exec("getBalls", t.player.click_loc, "fever_weapons_locker_open", 1, { at = { 1816, 4833, 0 } })
        t.exec("getBalls.choose", t.chat.play, { "options", "choose:Cannon ball" })
        t.exec("getBalls.got", t.inv.await, "fever_cannon_ball", balls0 + 1, 6)
        stock("getBalls", "Cannon ball", "fever_cannon_ball")
        t.exec("goUpToCannonWithBalls", t.player.climb, { loc = "fever_ship_ladder", op = 1, op_name = "Climb-up",
            at = { 1815, 4836, 0 }, src = { 1815, 4837 }, dest = { 1815, 4837, 1 } })

        local reward_snapshot_result, reward_before = t.skill.snapshot()
        t.step("reward.snapshot", reward_snapshot_result == "ok" and "PASS" or "FAIL", "skill.snapshot before the hand-in -> " .. tostring(reward_snapshot_result))
        local book_before = count("fever_piracy_book")

        local ball_shots = 0
        while stage() == 130 and ball_shots < 30 do
            if count("fever_cannon_ball") == 0 or count("fever_fuse") == 0 then
                t.exec("cannonballs.restock.down", t.player.climb, { loc = "fever_ship_laddertop", op = 1, op_name = "Climb-down",
                    at = { 1815, 4836, 1 }, src = { 1815, 4837 }, dest = { 1815, 4837, 0 } })
                stock("cannonballs.restock", "Cannon ball", "fever_cannon_ball")
                t.exec("cannonballs.restock.up", t.player.climb, { loc = "fever_ship_ladder", op = 1, op_name = "Climb-up",
                    at = { 1815, 4836, 0 }, src = { 1815, 4837 }, dest = { 1815, 4837, 1 } })
            end
            repair_if_broken("cannonballs.repair")
            ball_shots = ball_shots + 1
            t.exec("resetCannon", t.player.click_loc, "fever_multi_cannon", 2)
            t.exec("resetCannon-page", t.chat.play, { "mesbox:You clean out the cannon." })
            local _, dirty = t.var.varbit("varb1746_fever_cannon_clean")
            if dirty == 1 then
                t.exec("useRamrodToCleanForBalls", t.player.use_on, "fever_cannon_prod", cannon)
                t.exec("useRamrodToCleanForBalls-page", t.chat.play, { "mesbox:You clean out the cannon." })
                t.exec("useRamrodToCleanForBalls.clean", t.var.await_server, "varb1746_fever_cannon_clean", 0, 4)
            end
            t.exec("getPowderForBalls", t.player.click_loc, "fever_your_gunpowder_barrel", 1)
            t.exec("getPowderForBalls.got", t.inv.await, "fever_gunpowder", 1, 15)
            t.exec("usePowderForBalls", t.player.use_on, "fever_gunpowder", cannon)
            t.exec("usePowderForBalls-page", t.chat.play, { "mesbox:You pour the powder into the cannon." })
            t.exec("useRamrodForBalls", t.player.use_on, "fever_cannon_prod", cannon)
            t.exec("useRamrodForBalls-page", t.chat.play, { "mesbox:You shove the Ramrod into the cannon barrel." })
            t.exec("useBall", t.player.use_on, "fever_cannon_ball", cannon)
            t.exec("useBall-page", t.chat.play, { "mesbox:You roll the cannon ball into the cannon." })
            t.exec("useFuseForBalls", t.player.use_on, "fever_fuse", cannon)
            t.exec("useFuseForBalls-page", t.chat.play, { "mesbox:You ready the cannon for firing." })
            t.exec("useFuseForBalls.armed", t.var.await_server, "varb1741_fever_cannon", 3, 4)
            local _, holes0 = t.var.varbit("varb1750_fever_holes_in_the_hull")
            t.exec("fireCannonForBalls", t.player.click_loc, "fever_multi_cannon", 1)
            t.exec("fireCannonForBalls-page", t.chat.play, { "mesbox:You shoot the cannon ball at the enemy ship!" })
            local _, verdict = t.chat.text()
            local holed = type(verdict) == "string" and verdict:find("hole in the hull", 1, true) ~= nil
            t.exec("fireCannonForBalls-verdict", t.chat.play, { "mesbox:*" })
            local _, holes1 = t.var.varbit("varb1750_fever_holes_in_the_hull")
            -- "You put (another) hole in the hull!" is one more hole, "You fail
            -- to damage the hull." none: the page and the varbit must agree.
            t.check("fireCannonForBalls.shot" .. ball_shots, holes1 ~= nil and holes0 ~= nil
                and ((holed and holes1 == holes0 + 1) or (not holed and holes1 == holes0)),
                "ball " .. ball_shots .. ": page '" .. tostring(verdict) .. "'; holes " .. tostring(holes0) .. " -> " .. tostring(holes1))
            if (holes1 or 0) >= 3 then
                break
            end
            hp_watch("cannonballs")
        end

        -- The finale (Transcript "End of quest cutscene"): Bill on deck, the
        -- fade to The Other Inn, Mama, the book, the scroll.
        t.exec("repeatBallSteps.finale", t.chat.play, {
            "npc:They're sunk now!",
            "npc:Let's head to the island!",
        })
        local function in_inn()
            local r, tile = t.world.tile()
            return r == "ok" and tile.level == 0 and tile.x >= 3660 and tile.x <= 3675 and tile.z >= 2974 and tile.z <= 2988
        end
        t.await({ level = in_inn, note = "repeatBallSteps: the fade to The Other Inn" }, 20)
        t.check("repeatBallSteps.inn", in_inn(), "after the finale's sail the player is at " .. tile_text()
            .. " (want The Other Inn, Mos Le'Harmless)")
        t.await({ level = function() return t.chat.kind() ~= "none" end, note = "repeatBallSteps: Mama and Bill" }, 10)
        t.exec("repeatBallSteps.inn-scene", t.chat.play, {
            "npc:Bill? You look like you've been to Davey Jones'",
            "npc:Oh, Miss La'Fiette",
            "npc:My crew deserted",
            "npc:was as piratical as a bunch of Dwellberries",
            "player:I am still here you know",
            "npc:I remember.",
            "npc:I'll leave you two alone.",
            "npc:All right...",
            "npc:yer now a pirate, but",
            "player:Arr!",
            "npc:BUT ye can't just go around",
            "npc:No self-respectin' pirate",
            "npc:Well, except for Fancy Dan",
            "npc:Here, have this book.",
            "npc:Or at least learn ye",
            "player:So all I have to do",
            "npc:Aye...that be all",
            "npc:Ye'll need that book",
            "npc:Outsiders tend not",
            "npc:Anyway, I'd have been shark bait",
            "npc:If ye get a moment",
            "npc:I'm sure as soon as ye stop",
            "npc:If ye need a lift back",
        })

        t.quest.expect_complete()

        t.check("reward.smithing", t.skill.expect_gain("smithing", 7000, reward_before))
        t.check("reward.crafting", t.skill.expect_gain("crafting", 7000, reward_before))
        t.check("reward.agility", t.skill.expect_gain("agility", 7000, reward_before))
        t.check("reward.fever_piracy_book", count("fever_piracy_book") == book_before + 1,
            "fever_piracy_book " .. book_before .. " -> " .. count("fever_piracy_book") .. " (want +1)")

        -- The 10,000 coins: Bill's share, claimed by talking to him after the
        -- quest (Transcript "Claiming the loot from Bill Teach after the quest").
        local coins_before = count("coins")
        t.exec("reward.coins.talk", t.player.talk_to, "fever_harmless_teach", 1)
        t.exec("reward.coins.talk-dialog", t.chat.play, {
            "player:Can I have that gold now?",
            "npc:Sure ye can. Here.",
        })
        t.exec("reward.coins.got", t.inv.await, "coins", coins_before + 10000, 6)
        t.check("reward.coins", count("coins") == coins_before + 10000,
            "coins " .. coins_before .. " -> " .. count("coins") .. " (want +10000)")
        t.exec("reward.goldClaimed", t.var.await_server, "varb1765_fever_gold", 1, 4)

        t.finish(0)
    end,
}
