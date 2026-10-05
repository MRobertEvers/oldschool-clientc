-- Enter the Abyss (miniquest). Content:
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_entertheabyss/scripts/entertheabyss.rs2
-- dbrow: OSRS-Content/osrs239-content/configs/all.dbrow [miniquest_entertheabyss]
--   questpoints=0, endstate=4 (^eta_complete), stat_xp_awarded=20,10000
--   (runecraft, 1000 xp unscaled), displayname="Enter the Abyss".
--
-- Flow driven here (fresh_lumbridge.ini + ::entertheabyss resets
-- %abyssal_miniquest=0 and teleports to the Wilderness Mage of Zamorak):
--   1. rcu_zammy_mage1 (Wilderness, 3106,3558,0) -- choosing "Alright,
--      I'll go." (never "Let's see what you're selling.", which is a
--      dead-end shop refusal that never advances the stage) sets
--      %abyssal_miniquest = eta_varrock.
--   2. rcu_zammy_mage1_edge (Varrock Chaos Temple, 3259,3383,0), stage
--      eta_varrock -- the real Chaos Temple offer tree (Transcript:
--      Enter_the_Abyss oldid=15263217, content parity fc44e385e0): top
--      menu -> "Where do you get your runes from?" -> care-to-share 4-way
--      -> "Maybe I could make it worth your while?" (mercenary branch) ->
--      "Yes, but I can still help you as well." -> Deal/No deal/think ->
--      "Deal." grants an empty scrying orb and sets
--      %abyssal_miniquest = eta_orb. (The Saradomin-refusal,
--      not-interested, loyal-branch and rat-walk options are dead ends
--      the transcript itself marks as such; this file drives the one real
--      path through to the deal, same as the Wilderness mage's own
--      "Alright, I'll go." choice above.)
--   3. Teleport to the Rune Essence from three distinct NPCs while
--      carrying the orb (aubury 3253,3402,0; head_wizard 3103,9571,0;
--      ardounge_wizard 2683,3326,0) -- ~eta_charge_orb marks one essence
--      spot per distinct source and converts the orb on the third.
--   4. rcu_zammy_mage1_edge again, stage eta_orb with a full orb carried
--      -- hands it over, %abyssal_miniquest = eta_reward.
--   5. rcu_zammy_mage1_edge a third time, stage eta_reward -- the real
--      orb-handover/reward conversation (@eta_reward_handover), ending at
--      a p_choice3 of two OPTIONAL lore topics (Abyss / Z.M.I., which loop
--      back to the same menu) plus "I'd better be off."; choosing that
--      third option is what calls ~eta_quest_complete (XP + items + stage
--      -> eta_complete, opens the reward scroll).
--
-- Doors (b58 re-drive, the orchestrator's goto rule): every goto departs
-- from and lands on an open tile outside; every building is entered and
-- left on foot through its door, and the Rune Essence mine is left through
-- its exit portal (essence_mine.rs2 @essence_mine_exit), which sets you down
-- inside the teleporter's own room (runecraft.constant ^essence_mine_to_*),
-- so each visit walks back out through that room's door.
--   Wilderness Ditch: ditch_wilderness_cover (op1 Cross) at 3106,3521, crossed
--     south 3106,3523 -> 3106,3520 (wilderness_ditch.rs2).
--   Varrock Zamorak chapel: fai_varrock_poor_door_flipped 3255,3388 (street
--     x <= 3255, chapel x >= 3256; maps/m50_52.jl2).
--   Aubury's shop: fai_varrock_poor_door 3253,3398 on its north wall, open in
--     the map (fai_varrock_poor_door_open at 3253,3399); shop z >= 3399.
--   Wizards' Tower: north door fai_wiztower_poor_door 3109,3167, the ladder
--     room's diagonal door fai_wiztower_poor_door 3107,3162, the ladder
--     wizards_tower_laddertop 3104,3162 (maplink src 3105,3162 -> 3104,9576),
--     and Sedridor's basement room (x 3096-3107 z 9566-9574) behind poordoor
--     3108,9570 (maps/m48_149.jl2); back up wizards_tower_ladder 3103,9576.
--   Cromperty's house (x 2679-2686 z 3318-3327): castledoubledoorl/r
--     2678,3325/3324 (maps/m41_51.jl2).

return {
    id = "entertheabyss",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so the scrying orb fits
        "::entertheabyss", -- @eta_debug_reset: %runemysteries=complete, %abyssal_miniquest=0, teleports to the Wilderness mage
        "::complete quest_runemysteries", -- prerequisite Quest Helper lists (RUNE_MYSTERIES); already true, harmless no-op here
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp492_abyssal_miniquest",
            constants = {
                not_started = 0,
                varrock = 1,
                orb = 2,
                reward = 3,
                complete = 4,
            },
            row = "miniquest_entertheabyss",
            display = "Enter the Abyss",
            points = 0, -- dbrow questpoints=0 -- this is a miniquest, no quest points
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        local function tile_text(r, tt)
            return r == "ok" and type(tt) == "table" and (tt.x .. "," .. tt.z .. "," .. tt.level) or tostring(r)
        end

        -- Wait for a teleport or climb the click queued to land.
        local function await_tile(pred, ticks, what)
            return t.await({
                level = function()
                    local r, tt = t.world.tile()
                    return r == "ok" and type(tt) == "table" and pred(tt)
                end,
                note = what,
            }, ticks)
        end

        -- Cross one door on foot (the b56/b57 pass_door pattern: wanted.lua, priest). Walk to the
        -- tile on this side; if the closed leaf stands on door_x,door_z ON THE PLAYER'S LEVEL (the
        -- loc pool holds every level: the Wizards' Tower has a door copy on level 2 above
        -- 3107,3162), click THAT copy; otherwise an earlier press (or the map) left it open, so
        -- assert the open leaf stands within 1 of the door tile, off it, on this level -- a row that
        -- fails when neither leaf is there -- and do not press it again. Then walk through and
        -- check the far tile.
        local function pass_door(prefix, closed_sym, open_sym, door_x, door_z, near_x, near_z, far_x, far_z, far_ok, far_desc)
            t.player.walk_to(near_x, near_z, 40)
            local nr, nt = t.world.tile()
            t.check(prefix .. ".atDoor", nr == "ok" and math.abs(nt.x - near_x) <= 1 and math.abs(nt.z - near_z) <= 1,
                "walked to " .. near_x .. "," .. near_z .. " beside the door at " .. door_x .. "," .. door_z .. " -> " .. tile_text(nr, nt))
            local here = (nr == "ok") and nt.level or 0
            local cr, cd = t.world.loc_near(closed_sym, 3)
            if cr == "ok" and cd.tile_x == door_x and cd.tile_z == door_z and cd.level == here then
                t.exec(prefix .. ".openDoor", t.player.click_loc, closed_sym, 1, { at = { door_x, door_z } })
                t.ticks(1)
            else
                local orr, od = t.world.loc_near(open_sym, 3)
                t.check(prefix .. ".doorStandsOpen", orr == "ok" and od.level == here and not (od.tile_x == door_x and od.tile_z == door_z)
                        and math.abs(od.tile_x - door_x) <= 1 and math.abs(od.tile_z - door_z) <= 1,
                    closed_sym .. " at " .. door_x .. "," .. door_z .. ": " .. (cr == "ok" and ("nearest closed copy at " .. cd.tile_x .. "," .. cd.tile_z .. "," .. cd.level) or tostring(cr))
                        .. "; " .. open_sym .. ": " .. (orr == "ok" and ("open leaf at " .. od.tile_x .. "," .. od.tile_z .. "," .. od.level) or tostring(orr))
                        .. " (want the open leaf within 1 of the door tile on level " .. here .. ": already open, so it is walked through, not pressed again)")
            end
            t.player.walk_to(far_x, far_z, 40)
            local fr, ft = t.world.tile()
            t.check(prefix .. ".throughDoor", fr == "ok" and far_ok(ft),
                "walked through to " .. far_x .. "," .. far_z .. " -> " .. tile_text(fr, ft) .. " (want " .. far_desc .. ")")
        end

        -- Climb a ladder from its maplink src tile (ladders_stairs/configs/maplink.dbrow keys
        -- ~maplink_try on the PLAYER's coord) and grade the row on the landing tile.
        local function climb(name, sym, at_x, at_z, src_x, src_z, want_x, want_z)
            t.player.walk_to(src_x, src_z, 40)
            local sr, st = t.world.tile()
            t.check(name .. ".atLadder", sr == "ok" and st.x == src_x and st.z == src_z and st.level == 0,
                "walked to the maplink src " .. src_x .. "," .. src_z .. ",0 beside " .. sym .. " " .. at_x .. "," .. at_z .. " -> " .. tile_text(sr, st))
            local cr, cd = t.player.click_loc(sym, 1, { at = { at_x, at_z } })
            await_tile(function(tt) return tt.x == want_x and tt.z == want_z end, 8, name .. ": waiting for the climb to land")
            local lr, lt = t.world.tile()
            t.check(name, (cr == "ok" or cr == "timeout") and lr == "ok" and lt.x == want_x and lt.z == want_z and lt.level == 0,
                "from " .. tile_text(sr, st) .. " click_loc(" .. sym .. " at " .. at_x .. "," .. at_z .. ") -> " .. tostring(cr) .. " " .. tostring(cd)
                    .. "; landed " .. tile_text(lr, lt) .. " (want the maplink dest " .. want_x .. "," .. want_z .. ",0)")
        end

        -- The Rune Essence mine (region 45,75): a teleporter's jump lands on a random
        -- essence_mine_teleports tile; the way out is the nearest of the four exit portals
        -- (blankrunestone_exit_portal, maps/m45_75.jl2; reach.py walks every landing tile to its
        -- nearest portal with nothing in between).
        local function in_mine(tt)
            return tt.level == 0 and tt.x >= 2880 and tt.x <= 2943 and tt.z >= 4800 and tt.z <= 4863
        end
        local PORTALS = { { 2885, 4850 }, { 2889, 4813 }, { 2932, 4854 }, { 2933, 4815 } }

        -- Ask a teleporter for the Rune Essence while carrying the orb: the curse's p_delay(4)
        -- lands the jump several ticks after the page closes, so await the mine before anything
        -- else (docs sec 2, "A dialogue or loc that TELEPORTS you"), and read the spot varbit
        -- ~eta_charge_orb writes for this teleporter.
        local function essence_teleport(name, npc, greeting, spot_var)
            local orr, orb_n = t.inv.count("scrying_orb_empty")
            t.check(name .. ".carriesOrb", orr == "ok" and orb_n == 1,
                "scrying_orb_empty count=" .. tostring(orb_n) .. " (" .. tostring(orr) .. ") before asking " .. npc .. " (want 1: the guide's item for this step)")
            t.exec(name, t.player.talk_to, npc, 1)
            t.exec(name .. "-dialog", t.chat.play, {
                "npc:" .. greeting,
                "choose:Can you teleport me to the Rune Essence?",
                "player:Can you teleport me to the Rune Essence?",
            })
            local ar = await_tile(in_mine, 15, "arrival in the essence mine after " .. npc .. "'s teleport")
            local mr, mt = t.world.tile()
            t.check("await-essence-" .. npc, ar == "ok" and mr == "ok" and in_mine(mt),
                "await arrival -> " .. tostring(ar) .. "; t.world.tile() -> " .. tile_text(mr, mt)
                    .. " (want the essence mine, x 2880-2943 z 4800-4863, level 0)")
            t.expect(name .. ".spot", t.var.await_server(spot_var, 1, 5))
        end

        -- Leave the mine by its nearest exit portal and check where it set us down.
        local function leave_mine(prefix, back_ok, back_desc)
            local r, tt = t.world.tile()
            local best, best_d = PORTALS[1], 1e9
            if r == "ok" then
                for _, p in ipairs(PORTALS) do
                    local d = math.max(math.abs(p[1] - tt.x), math.abs(p[2] - tt.z))
                    if d < best_d then
                        best, best_d = p, d
                    end
                end
            end
            t.exec(prefix .. ".exitPortal", t.player.click_loc, "blankrunestone_exit_portal", 1, { at = { best[1], best[2] } })
            await_tile(function(w) return not in_mine(w) end, 15, prefix .. ": waiting for the portal's jump")
            local wr, wt = t.world.tile()
            t.check(prefix .. ".leftMine", wr == "ok" and not in_mine(wt) and back_ok(wt),
                "from " .. tile_text(r, tt) .. " through the portal at " .. best[1] .. "," .. best[2] .. " -> " .. tile_text(wr, wt)
                    .. " (want " .. back_desc .. ")")
        end

        local function chapel_in(prefix)
            pass_door(prefix .. ".chapelDoorIn", "fai_varrock_poor_door_flipped", "fai_varrock_poor_door_open_flipped", 3255, 3388, 3254, 3388, 3257, 3387,
                function(tt) return tt.x >= 3256 and tt.level == 0 end, "inside the chapel, x >= 3256")
        end
        local function chapel_out(prefix)
            pass_door(prefix .. ".chapelDoorOut", "fai_varrock_poor_door_flipped", "fai_varrock_poor_door_open_flipped", 3255, 3388, 3257, 3387, 3253, 3388,
                function(tt) return tt.x <= 3255 and tt.level == 0 end, "back on the street west of the door, x <= 3255")
        end

        -- ---- 1. Wilderness Mage of Zamorak: accept the errand ----
        -- ::entertheabyss stands the player beside the mage on the open Wilderness plain.
        t.exec("goto-talkToMageInWildy", t.player.goto_tile, 3106, 3558, 0)
        t.exec("talkToMageInWildy", t.player.talk_to, "rcu_zammy_mage1", 1)
        t.exec("talkToMageInWildy-dialog", t.chat.play, {
            "npc:This location is unsafe",
            "choose:Alright, I'll go.",
            "npc:Good. Do not linger here.",
        })
        t.expect("quest.stage.varrock", t.quest.expect_stage("varrock"))

        -- ---- 2. Varrock Mage of Zamorak: earn the scrying orb ----
        -- Out of the Wilderness on foot: walk south to the Wilderness Ditch and Cross it by click
        -- (areas/area_wilderness/scripts/wilderness_ditch.rs2 ~wilderness_ditch_cross: the copies at
        -- z 3521-3522 are angle 0, so from z 3523 the jump lands on z-1 = 3520, three tiles south).
        -- The warning pages only open on a jump NORTH (deeper into the Wilderness), and the on-foot
        -- strip warning's zones are z 3512-3519, which the walk never enters, so nothing may be open
        -- after the jump. The goto then leaves the open ground south of the ditch for the open
        -- street outside the chapel (overland travel between two open tiles).
        t.player.walk_to(3106, 3523, 60)
        local dr, dt = t.world.tile()
        t.check("walkToWildernessDitch", dr == "ok" and dt.x == 3106 and dt.z == 3523 and dt.level == 0,
            "walked from the mage's plain to the ditch's north side -> " .. tile_text(dr, dt) .. " (want 3106,3523,0)")
        local xr, xd = t.player.click_loc("ditch_wilderness_cover", 1, { at = { 3106, 3521 } })
        await_tile(function(tt) return tt.z <= 3520 end, 8, "crossWildernessDitch: waiting for the jump to land")
        local lr, lt = t.world.tile()
        t.check("crossWildernessDitch", (xr == "ok" or xr == "timeout") and lr == "ok" and lt.x == 3106 and lt.z == 3520 and lt.level == 0,
            "from " .. tile_text(dr, dt) .. " click_loc(ditch_wilderness_cover Cross at 3106,3521) -> " .. tostring(xr) .. " " .. tostring(xd)
                .. "; landed " .. tile_text(lr, lt) .. " (want 3106,3520,0: the south side, out of the Wilderness)")
        local ck = t.chat.kind()
        t.check("crossWildernessDitch.noWarning", ck == "none",
            "chat.kind() after the south jump -> " .. tostring(ck) .. " (want none: the ditch warns only on a jump north)")
        t.exec("goto-talkToMageInVarrock", t.player.goto_tile, 3253, 3388, 0) -- from 3106,3520 to the street outside the chapel door
        chapel_in("talkToMageInVarrock")
        t.exec("talkToMageInVarrock", t.player.talk_to, "rcu_zammy_mage1_edge", 1)
        t.exec("talkToMageInVarrock-dialog", t.chat.play, {
            "npc:Ah, you again. The Wilderness is hardly the appropriate place for a conversation",
            "player:Err... I didn't really want anything.",
            "npc:So why did you approach me?",
            "player:I was just wondering why you sell runes in the Wilderness?",
            "npc:Well I can't go doing it in the middle of Varrock",
            "choose:Where do you get your runes from?",
            "player:Where do you get your runes from?",
            "npc:Well we craft them of course.",
            "player:We?",
            "npc:My associates and I. Despite the best attempts",
            "player:I can't imagine they like you crafting runes much",
            "npc:Ha! I'm sure they'd love to, but we have methods of runecrafting",
            "player:Care to share?",
            "npc:Why would I? You are not a member of our institute",
            "choose:Maybe I could make it worth your while?",
            "player:Maybe I could make it worth your while?",
            "npc:How? What do you have to offer?",
            "player:Well what is it you want?",
            "npc:Until recently, our runecrafting secrets allowed us to produce runes",
            "npc:From what we can gather, they've somehow rediscovered how to access the lost Rune Essence Mine.",
            "player:Ah, well I know all about that. I was actually the one to help them do it!",
            "npc:You did what? You helped the Order of Wizards?",
            "player:Err...",
            "choose:Yes, but I can still help you as well.",
            "player:Yes, but I can still help you as well.",
            "npc:So you're a mercenary with no allegiance?",
            "npc:Alright, if you help us access the Rune Essence Mine, we will share our runecrafting secrets",
            "choose:Deal.",
            "player:Deal.",
            "npc:Good. Now, all I need from you is the spell that will teleport me to the Rune Essence Mine.",
            "player:Err... I don't actually know the spell.",
            "npc:What? Then how do you get there.",
            "player:Oh, well the people who do know the spell just teleport me there directly.",
            "npc:Hmm... I see. That makes this slightly more complex",
            "player:How?",
            "npc:I'll give you a scrying orb with a standard cypher spell cast upon it.",
            "npc:If you teleport to the Rune Essence Mine from three different locations",
            "npc:Do you know of three different people who can teleport you there?",
            "player:Maybe?",
            "npc:Well if not, I'm sure one of those fools in the Order of Wizards can tell you. Now, here's the orb.",
        })
        t.ticks(1) -- let the granted orb's inv update land before reading it
        local orb_empty_result, orb_empty_count = t.inv.count("scrying_orb_empty")
        t.check("gather.orb_granted", orb_empty_result == "ok" and orb_empty_count == 1,
            string.format("scrying_orb_empty count=%s (read %s)", tostring(orb_empty_count), tostring(orb_empty_result)))
        t.expect("quest.stage.orb", t.quest.expect_stage("orb"))

        -- ---- 3. Charge the orb: teleport to the Rune Essence from three
        -- distinct sources while carrying it (~eta_charge_orb), in the guide's
        -- order: Aubury, Sedridor, Cromperty. Each teleport is awaited into the
        -- mine (essence_teleport), and each visit leaves the mine through its
        -- exit portal back into the teleporter's room and walks out its door.
        chapel_out("talkToMageInVarrock")

        -- Aubury's shop is 10 tiles north of the chapel door: walked, no goto.
        pass_door("talkToAubury.shopDoorIn", "fai_varrock_poor_door", "fai_varrock_poor_door_open", 3253, 3398, 3253, 3397, 3253, 3400,
            function(tt) return tt.z >= 3399 and tt.level == 0 end, "inside Aubury's shop, z >= 3399")
        essence_teleport("talkToAubury", "aubury", "Do you want to buy some runes?", "varb2315_rcu_essencespot_aubury")
        leave_mine("talkToAubury", function(tt) return tt.level == 0 and tt.z >= 3399 and tt.z <= 3405 and math.abs(tt.x - 3253) <= 3 end,
            "inside Aubury's shop, within 2 of ^essence_mine_to_aubury 3253,3401")
        pass_door("talkToAubury.shopDoorOut", "fai_varrock_poor_door", "fai_varrock_poor_door_open", 3253, 3398, 3253, 3400, 3253, 3396,
            function(tt) return tt.z <= 3397 and tt.level == 0 end, "on the street south of the shop, z <= 3397")

        -- Wizards' Tower: the island outside its north door (reach.py: 3109,3169 walks the
        -- island to 3100,3170 with every door shut), in through two doors, down the ladder.
        t.exec("goto-goDownInWizardsTower", t.player.goto_tile, 3109, 3169, 0)
        pass_door("goDownInWizardsTower.towerIn", "fai_wiztower_poor_door", "fai_wiztower_poor_door_open", 3109, 3167, 3109, 3168, 3109, 3164,
            function(tt) return tt.z <= 3166 and tt.level == 0 end, "inside the tower's hall, z <= 3166")
        pass_door("goDownInWizardsTower.ladderRoomIn", "fai_wiztower_poor_door", "fai_wiztower_poor_door_open", 3107, 3162, 3108, 3163, 3105, 3162,
            function(tt) return tt.x <= 3106 and tt.level == 0 end, "inside the ladder room, x <= 3106")
        climb("goDownInWizardsTower", "wizards_tower_laddertop", 3104, 3162, 3105, 3162, 3104, 9576)
        -- Sedridor's room: reach.py walks the ladder foot 3104,9576 to 3109,9570 with doors shut.
        pass_door("talkToSedridor.roomIn", "poordoor", "poordooropen", 3108, 9570, 3109, 9570, 3106, 9570,
            function(tt) return tt.x <= 3107 and tt.z >= 9566 and tt.z <= 9574 end, "inside Sedridor's room, x <= 3107")
        essence_teleport("talkToSedridor", "head_wizard", "Welcome adventurer, to the world renowned Wizards' Tower.", "varb2314_rcu_essencespot_wizardstower")
        leave_mine("talkToSedridor", function(tt) return tt.level == 0 and math.abs(tt.x - 3106) <= 2 and math.abs(tt.z - 9572) <= 2 end,
            "the basement within 2 of ^essence_mine_to_sedridor 3106,9572")
        pass_door("talkToSedridor.roomOut", "poordoor", "poordooropen", 3108, 9570, 3107, 9570, 3109, 9570,
            function(tt) return tt.x >= 3108 end, "in the corridor east of Sedridor's door, x >= 3108")
        climb("talkToSedridor.ladderUp", "wizards_tower_ladder", 3103, 9576, 3104, 9576, 3105, 3162)
        pass_door("talkToSedridor.ladderRoomOut", "fai_wiztower_poor_door", "fai_wiztower_poor_door_open", 3107, 3162, 3105, 3162, 3108, 3163,
            function(tt) return tt.x >= 3107 and tt.level == 0 end, "back in the tower's hall, x >= 3107")
        pass_door("talkToSedridor.towerOut", "fai_wiztower_poor_door", "fai_wiztower_poor_door_open", 3109, 3167, 3109, 3165, 3109, 3169,
            function(tt) return tt.z >= 3167 and tt.level == 0 end, "outside the tower's north door, z >= 3167")

        -- Cromperty's house, East Ardougne: the street west of its double door (reach.py:
        -- 2677,3325 walks to 2662,3305 with every door shut).
        t.exec("goto-talkToCromperty", t.player.goto_tile, 2677, 3325, 0)
        pass_door("talkToCromperty.houseIn", "castledoubledoorl", "opencastledoubledoorl", 2678, 3325, 2677, 3325, 2680, 3324,
            function(tt) return tt.x >= 2679 and tt.level == 0 end, "inside Cromperty's house, x >= 2679")
        essence_teleport("talkToCromperty", "ardounge_wizard", "Hello there.", "varb2316_rcu_essencespot_cromperty")

        -- eta_charge_orb converts the orb on the third distinct spot and
        -- prints this system line (mes(), not a dialogue page) DURING
        -- ~teleport_to_essence_mine's own p_delay(4), before the p_telejump
        -- that the await-essence row above already waited out -- so the line
        -- is already in the ring by here: t.msg.expect (any line still in the
        -- ring), never t.msg.await (only lines newer than the call; docs sec 8).
        t.expect("gather.orb_charged", t.msg.expect("absorbed enough teleport information"))
        local orb_full_result, orb_full_count = t.inv.count("scrying_orb_full")
        local orb_left_result, orb_left_count = t.inv.count("scrying_orb_empty")
        t.check("gather.orb_full", orb_full_result == "ok" and orb_full_count == 1 and orb_left_result == "ok" and orb_left_count == 0,
            string.format("scrying_orb_full count=%s (read %s), scrying_orb_empty count=%s (read %s) (want 1 and 0: the third spot swapped the orb)",
                tostring(orb_full_count), tostring(orb_full_result), tostring(orb_left_count), tostring(orb_left_result)))

        leave_mine("talkToCromperty", function(tt) return tt.level == 0 and tt.x >= 2679 and tt.x <= 2686 and tt.z >= 3318 and tt.z <= 3327 end,
            "inside Cromperty's house, within 2 of ^essence_mine_to_cromperty 2684,3322")
        pass_door("talkToCromperty.houseOut", "castledoubledoorl", "opencastledoubledoorl", 2678, 3325, 2679, 3325, 2676, 3325,
            function(tt) return tt.x <= 2677 and tt.level == 0 end, "on the street west of the double door, x <= 2677")

        -- ---- 4. Hand the full orb back to the Varrock Mage ----
        t.exec("goto-talkToMageAfterTeleports", t.player.goto_tile, 3253, 3388, 0) -- the street outside the chapel door
        chapel_in("talkToMageAfterTeleports")
        t.exec("talkToMageAfterTeleports", t.player.talk_to, "rcu_zammy_mage1_edge", 1)
        t.exec("talkToMageAfterTeleports-dialog", t.chat.play, {
            "player:Yes I have! I've got it right here!",
            "npc:Excellent. Give it here",
            "npc:The Z.M.I. can now reach the essence mine",
        })
        t.expect("quest.stage.reward", t.quest.expect_stage("reward"))

        -- ---- 5. Reward snapshot, then talk a third time to complete ----
        local reward_snapshot_result, reward_before = t.skill.snapshot()
        local runecraft_before = type(reward_before) == "table" and reward_before.runecraft or nil
        t.check("reward.snapshot", reward_snapshot_result == "ok" and type(runecraft_before) == "table" and runecraft_before.experience == 0,
            "skill.snapshot before the hand-in -> " .. tostring(reward_snapshot_result) .. "; runecraft xp "
                .. (type(runecraft_before) == "table" and tostring(runecraft_before.experience) or tostring(runecraft_before))
                .. " (want 0: the fresh character has crafted nothing, and no essence teleport pays xp)")

        t.exec("talkToMageToFinish", t.player.talk_to, "rcu_zammy_mage1_edge", 1)
        t.exec("talkToMageToFinish-dialog", t.chat.play, {
            "player:Here you go.",
            "mesbox:You hand the orb to the Mage of Zamorak.",
            "npc:Right, let's take a look at this orb",
            "npc:Yes, this will do nicely. Once again, the Zamorak Magical Institute has overcome the Order of Wizards!",
            "npc:You have done well. Now, time for us to uphold our end of the bargain.",
            "npc:The reason we are able to craft so many runes is because we do not visit the runic altars",
            "player:How?",
            "npc:Via another plane known as the Abyss.",
            "player:So can I use the Abyss?",
            "npc:Yes. Visit me in the Wilderness whenever you wish to be teleported there.",
            "player:How is it dangerous?",
            "npc:There are creatures there that will hunt and attack any visitors on sight.",
            "player:What do you mean?",
            "npc:Just don't expect to be using any prayers in there.",
            "npc:Anyway, you may also have this pouch as well. I'm sure you will find it useful. Now, we're done here.",
            "choose:I'd better be off.",
            "player:I'd better be off.",
        })
        t.ticks(3) -- completion is asynchronous -- not padding (docs sec 8)

        t.quest.expect_complete()

        -- ---- Rewards: literal values the quest documents (dbrow), never
        -- read back from the scroll ----
        t.check("reward.runecraft", t.skill.expect_gain("runecraft", 1000, reward_before))
        local reward_book_result, reward_book_detail = t.inv.expect_has("rcu_instruction_book", 1)
        t.check("reward.rcu_instruction_book", reward_book_result, reward_book_detail)
        local reward_pouch_result, reward_pouch_detail = t.inv.expect_has("rcu_pouch_small", 1)
        t.check("reward.rcu_pouch_small", reward_pouch_result, reward_pouch_detail)

        t.finish(0)
    end,
}
