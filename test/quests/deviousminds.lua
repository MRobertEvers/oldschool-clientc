-- Devious Minds. Content: OSRS-Content/osrs239-content/server/scripts/quests/quest_deviousminds/
-- Setup stages the four prerequisite quests, the three skill levels and what the guide
-- lists as brought along (mithril 2h sword, bow string, large pouch).
--
-- Entrana carries no weapon or armour, and the trip there goes through the Abyss's Law
-- rift: the guide's makeIllumPouch step says "You will also be going to Entrana via the
-- Abyss, so you must bank all combat gear", and the OSRS wiki's Law Altar page says the
-- altar "can also be accessed through the Abyss ... although the same list of prohibited
-- items applies". A pickaxe and an axe are weapons there (weapon_pickaxe / weapon_axe:
-- the Port Sarim monk's ~has_entrana_restricted_items refuses them,
-- areas/port_sarim/scripts/monk_of_entrana.rs2:46). So the test carries neither: the
-- Abyss's outer ring is passed by the obstacles that need no weapon -- the eyes
-- (Thieving), the gap (Agility) or the boil (a tinderbox, Firemaking) -- and nothing has
-- to be banked before the Port Sarim monk's search either.
--
-- Travel, every visit, both ways (the door rule):
--   * the Varrock members' gate fai_varrock_member_gatel 3319,3468 (doubledoors.loc:558,
--     an opening double gate) is the only way on foot between Varrock and the Paterdomus
--     monk (reach.py at margins 60/80/160: NEEDS-DOOR via it), so it is passed by
--     t.player.pass_door on all six crossings;
--   * Doric's whetstone (2953,3451) stands in the hut room behind poordoor 2949,3450;
--   * the Mage of Zamorak (3106,3556) is north of the Wilderness Ditch, crossed by its
--     own Cross op (wilderness_ditch.rs2) after the warning strip's pages;
--   * Entrana is an island: on by the Port Sarim monk's boat and ship_from_entrana_off,
--     off by shipmonk2's boat and ship_to_entrana_off, each gangplank by t.player.climb
--     (deck level 1 -> jetty level 0). The Law Altar's exit portal lands on Entrana too.
--   * The Entrana church is walked into through its open north end (reach.py: REACH
--     closed-doors from the portal landing, len 40, and from the dock, len 43).
local GATE = { closed = "fai_varrock_member_gatel", open = "fai_varrock_member_gatel_open",
    at = { 3319, 3468, 0 } }
local GATE_WEST = { 3317, 3468 }   -- open ground on the Varrock side
local GATE_EAST = { 3322, 3468 }   -- open ground on the Paterdomus side

local function tile_text(r, tt)
    if r ~= "ok" or not tt then
        return tostring(r)
    end
    return tt.x .. "," .. tt.z .. "," .. tostring(tt.level)
end

return {
    id = "deviousminds",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel smithing 65",
        "::setlevel runecraft 50",
        "::setlevel fletching 50",
        "::complete quest_wanted",
        "::complete quest_trollstronghold",
        "::complete quest_dorics",
        "::complete miniquest_entertheabyss",
        "::give mithril_2h_sword 1",
        "::give bow_string 1",
        "::give rcu_pouch_large 1",
        -- The Abyss's weaponless obstacles: eyes (Thieving), gap (Agility), boil (tinderbox,
        -- Firemaking). The flat level+1% roll (runecraft_abyss.rs2 abyss_obstacle_attempt)
        -- always passes at 99.
        "::setlevel firemaking 99",
        "::setlevel thieving 99",
        "::setlevel agility 99",
        "::give tinderbox 1",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb1465_devious_main",
            constants = { not_started = 0, accepted = 10, bowsword_given = 20, orb_given = 30,
                cutscene_done = 40, priest_spoken = 50, monk_found_dead = 60,
                reported_priest = 70, complete = 80 },
            row = "quest_deviousminds",
            display = "Devious Minds",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        local wr, wv = t.var.server("varb1051_wanted_main")
        -- deviousminds_monk.rs2:193 gates the quest on %varb1051_wanted_main >= ^wanted_complete
        -- (= 11, quest_wanted/configs/wanted.constant:28).
        t.check("prereq.wanted_main", wr == "ok" and wv == 11,
            "varb1051_wanted_main = " .. tostring(wv) .. " (" .. tostring(wr) .. ") (want 11, ^wanted_complete)")

        -- The Varrock members' gate, pressed (or found standing open) on every crossing.
        local function gate_east(name)
            t.exec("goto-" .. name .. ".gateWest", t.player.goto_tile, GATE_WEST[1], GATE_WEST[2], 0)
            t.exec(name .. ".memberGateEast", t.player.pass_door, { closed = GATE.closed, open = GATE.open,
                at = GATE.at, near = { 3318, 3468 }, far = { 3321, 3468 } })
        end
        local function gate_west(name)
            t.exec("goto-" .. name .. ".gateEast", t.player.goto_tile, GATE_EAST[1], GATE_EAST[2], 0)
            t.exec(name .. ".memberGateWest", t.player.pass_door, { closed = GATE.closed, open = GATE.open,
                at = GATE.at, near = { 3321, 3468 }, far = { 3318, 3468 } })
        end

        -- ---- talkToMonk: the hooded monk outside Paterdomus ----
        gate_east("monkTrip")
        t.exec("goto-talkToMonk", t.player.goto_tile, 3406, 3494, 0)
        t.exec("talkToMonk", t.player.talk_to, "devious_monk_hooded", 1)
        t.exec("talkToMonk-dialog", t.chat.play, {
            "npc:Good day to you, adventurer.",
            "player:And to you.",
            "npc:I am on my return journey",
            "player:Morytania?",
            "npc:Indeed, but as a faithful",
            "player:I see.",
            "npc:Before you depart",
            "choose:Yes.",
            "player:Of course.",
            "npc:On my travels",
            "npc:If you could help me",
            "player:A new weapon?",
            "npc:Yes, now pay attention.",
            "npc:Once the blade",
            "player:Alright, I'll be back",
        })
        t.ticks(2)
        t.expect("quest.stage.accepted", t.quest.expect_stage("accepted"))

        -- ---- makeBlade: Doric's whetstone, in the hut room behind poordoor 2949,3450 ----
        gate_west("whetstoneTrip")
        t.exec("goto-makeBlade", t.player.goto_tile, 2947, 3450, 0)
        t.exec("whetstoneTrip.hutDoorIn", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 2949, 3450, 0 }, near = { 2948, 3450 }, far = { 2950, 3450 } })
        local sw0r, sw0 = t.inv.count("mithril_2h_sword")
        t.exec("makeBlade", t.player.use_on, "mithril_2h_sword", t.player.by_symbol("loc", "devious_whetstone"))
        t.exec("makeBlade-dialog", t.chat.play, { "choose:Yes." })
        t.exec("makeBlade-inv", t.inv.await, "devious_slenderblade", 1, 20)
        local sw1r, sw1 = t.inv.count("mithril_2h_sword")
        t.check("makeBlade.swordGround", sw0r == "ok" and sw1r == "ok" and sw0 == 1 and sw1 == 0,
            "mithril_2h_sword " .. tostring(sw0) .. " -> " .. tostring(sw1) .. " (want 1 -> 0: ground into the slender blade)")
        t.ticks(2)
        t.exec("makeBlade-box", t.chat.continue_, true)
        t.exec("whetstoneTrip.hutDoorOut", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 2949, 3450, 0 }, near = { 2950, 3450 }, far = { 2947, 3450 } })

        -- ---- makeBowSword: bow string on the slender blade ----
        t.exec("makeBowSword", t.player.use_item_on_item, "bow_string", "devious_slenderblade")
        t.exec("makeBowSword-inv", t.inv.await, "devious_bowsword", 1, 10)
        local bsr, bs = t.inv.count("bow_string")
        local blr, bl = t.inv.count("devious_slenderblade")
        t.check("makeBowSword.consumed", bsr == "ok" and blr == "ok" and bs == 0 and bl == 0,
            "bow_string " .. tostring(bs) .. ", devious_slenderblade " .. tostring(bl) .. " (want both 0: strung into the bow-sword)")

        -- ---- talkToMonk2: the bow-sword back to the monk ----
        gate_east("monkReturnTrip")
        t.exec("goto-talkToMonk2", t.player.goto_tile, 3406, 3494, 0)
        t.exec("talkToMonk2", t.player.talk_to, "devious_monk_hooded", 1)
        t.exec("talkToMonk2-dialog", t.chat.play, {
            "npc:Hello again, adventurer.",
            "player:I have it right here",
            "*",
            "npc:Excellent!",
            "npc:Now, I think it's high time",
            "npc:On that note",
            "player:What is it?",
            "npc:I have a special gift",
            "player:Meaning?",
            "npc:I would like my companions",
            "player:Smuggled?",
            "npc:I confess",
            "player:Hmm... Fair enough",
            "npc:Our island",
            "player:You speak of the Abyss",
            "npc:Excellent!",
            "player:The Abyss isn't exactly",
            "npc:The same voices",
            "player:Alright, I'll get that done",
            "npc:Wonderful! Here is the orb",
            "*",
        })
        t.ticks(2)
        t.expect("quest.stage.orb_given", t.quest.expect_stage("orb_given"))
        local oc, on = t.inv.count("devious_glowingorb")
        t.check("talkToMonk2-orb", oc == "ok" and on == 1, "orb count = " .. tostring(on))

        -- ---- makeIllumPouch: the orb into the large pouch ----
        t.exec("makeIllumPouch", t.player.use_item_on_item, "devious_glowingorb", "rcu_pouch_large")
        t.exec("makeIllumPouch-inv", t.inv.await, "devious_glowingpouch", 1, 10)
        local o2r, o2 = t.inv.count("devious_glowingorb")
        local p2r, p2 = t.inv.count("rcu_pouch_large")
        t.check("makeIllumPouch.consumed", o2r == "ok" and p2r == "ok" and o2 == 0 and p2 == 0,
            "devious_glowingorb " .. tostring(o2) .. ", rcu_pouch_large " .. tostring(p2) .. " (want both 0: the orb hidden in the pouch)")

        -- No combat gear on the way to Entrana (guide: "bank all combat gear"): the sword was
        -- ground and the bow-sword handed over; the setup gave no pickaxe or axe.
        local carried, carry_text = false, ""
        for _, item in ipairs({ "mithril_2h_sword", "devious_slenderblade", "devious_bowsword", "bronze_pickaxe", "bronze_axe" }) do
            local cr, c = t.inv.count(item)
            if not (cr == "ok" and c == 0) then
                carried = true
            end
            carry_text = carry_text .. item .. "=" .. tostring(c) .. " "
        end
        t.check("mageTrip.noCombatGear", not carried, carry_text .. "(want every one 0 in the pack)")

        -- ---- teleToAbyss: the Mage of Zamorak, north of the Wilderness Ditch ----
        gate_west("mageTrip")
        t.exec("goto-mageTrip.ditchSouth", t.player.goto_tile, 3106, 3505, 0)
        -- wilderness_warning.rs2: walking north into the strip at z 3512-3515 (unwarned this
        -- session, %varp5753_wilderness = 0) stops the walk with three pages; the ditch then
        -- warns no more (wilderness_ditch.rs2 reads the same varp).
        t.player.walk_to(3106, 3520, 30)
        t.exec("mageTrip.wildernessWarning", t.chat.play, {
            "mesbox:WARNING! Proceed with caution",
            "mesbox:The further north you go",
            "mesbox:In the wilderness an indicator",
        })
        t.player.walk_to(3106, 3520, 20)
        local war, warn = t.var.server("varp5753_wilderness")
        local dr, dt = t.world.tile()
        t.check("mageTrip.ditchSide", dr == "ok" and dt.x == 3106 and dt.z == 3520 and dt.level == 0,
            "walked to the ditch's south side -> " .. tile_text(dr, dt) .. " (want 3106,3520,0); varp5753_wilderness = "
                .. tostring(warn) .. " (" .. tostring(war) .. ")")
        t.exec("mageTrip.crossDitch", t.player.cross_trap, { loc = "ditch_wilderness_cover", op_name = "Cross",
            at = { 3106, 3521, 0 }, src = { 3106, 3520 }, dest = { 3106, 3523 } })
        t.exec("goto-teleToAbyss", t.player.goto_tile, 3106, 3558, 0)
        t.exec("teleToAbyss", t.player.talk_to, "rcu_zammy_mage1b", 4)
        t.ticks(6)
        local _, at = t.world.tile()
        t.check("teleToAbyss-tile", at ~= nil and at.z > 4000, "tile " .. tostring(at and at.x) .. "," .. tostring(at and at.z))

        -- The outer ring: a weaponless obstacle (eyes / gap / boil) carries the player inward.
        -- A pickaxe or axe family (runecraft_abyss.rs2 abyss_shell_click) answers a mesbox
        -- without the tool; it is read, dismissed, and the next obstacle tried.
        local passed = false
        local tried = {}
        for _, sym in ipairs({ "rcu_abyssal_barrier_eyes1", "rcu_abyssal_barrier_agility", "rcu_abyssal_barrier_boil1",
            "rcu_abyssal_barrier_tendrils1", "rcu_abyssal_barrier_teeth1" }) do
            if not passed and t.world.loc_near(sym, 14) == "ok" then
                for attempt = 1, 4 do
                    if passed then break end
                    local cr = t.player.click_loc(sym, 1)
                    t.ticks(6)
                    local said = ""
                    if t.chat.kind() == "mesbox" then
                        local _, txt = t.chat.text()
                        said = " mesbox '" .. tostring(txt) .. "'"
                        t.chat.continue_()
                        t.ticks(1)
                    end
                    local _, now = t.world.tile()
                    tried[#tried + 1] = sym .. ":" .. tostring(cr) .. said .. "@" .. now.x .. "," .. now.z
                    if now.x >= 3023 and now.x <= 3056 and now.z >= 4818 and now.z <= 4848 then passed = true end
                    if said ~= "" then break end
                end
            end
        end
        t.check("abyss-obstacle-inner", passed, table.concat(tried, " "))
        t.exec("enterLawRift", t.player.click_loc, "abyss_exit_to_law", 1)
        t.ticks(8)
        local _, lt = t.world.tile()
        t.check("enterLawRift-tile", lt.x >= 2400 and lt.x < 2500, "tile " .. lt.x .. "," .. lt.z)
        t.exec("leaveLawAltar", t.player.click_loc, "lawtemple_exit_portal", 1)
        t.ticks(8)
        local _, et = t.world.tile()
        t.check("leaveLawAltar-tile", et.x > 2790 and et.x < 2890 and et.z > 3300, "tile " .. et.x .. "," .. et.z)

        -- ---- usePouchOnAltar: the Entrana church, open from the north ----
        t.exec("goto-church", t.player.goto_tile, 2851, 3347, 0)
        t.exec("usePouchOnAltar", t.player.use_on, "devious_glowingpouch", t.player.by_symbol("loc", "devious_altar"))
        t.exec("usePouchOnAltar.cutscene", t.cutscene.await, "usePouchOnAltar", { expect = {
            { op = "moveto" }, { op = "lookat" }, { op = "reset" } }, timeout = 200, quiet = 80 })
        local gpr, gp = t.inv.count("devious_glowingpouch")
        t.check("usePouchOnAltar.pouchGone", gpr == "ok" and gp == 0,
            "devious_glowingpouch " .. tostring(gp) .. " (want 0: left on the altar)")
        t.ticks(20)
        t.chat.continue_(true)
        t.ticks(3)
        t.chat.continue_(true)
        t.expect("quest.stage.cutscene_done", t.quest.expect_stage("cutscene_done"))
        t.exec("talkToHighPriest", t.player.talk_to, "high_priest_of_entrana", 1)
        t.exec("talkToHighPriest-dialog", t.chat.play, {"npc:The relic","npc:Adventurer","player:Uh","npc:What is it","player:I put","npc:What?","player:There was","npc:No worshipper","player:I'll go"})
        t.ticks(2)
        t.expect("quest.stage.priest_spoken", t.quest.expect_stage("priest_spoken"))

        -- Off Entrana by shipmonk2's boat (areas/entrana/scripts/monk_of_entrana.rs2:27-38:
        -- p_telejump(1_47_50_40_31) = the deck at Port Sarim, 3048,3231,1), then the gangplank.
        local function leave_entrana(name)
            t.exec("goto-" .. name .. ".shipmonk2", t.player.goto_tile, 2832, 3336, 0)
            t.exec(name .. ".leaveEntrana", t.player.talk_to, "shipmonk2", 1)
            t.exec(name .. ".leaveEntrana-dialog", t.chat.play, {
                "npc:Do you wish to leave holy Entrana?",
                "choose:Yes, I'm ready to go.",
                "player:Yes, I'm ready to go.",
                "npc:Okay, let's board",
            })
            t.await({ level = function()
                local r, tt = t.world.tile()
                return r == "ok" and tt.level == 1 and tt.x > 3000
            end, note = name .. ": the sail to Port Sarim" }, 12)
            local sr, st = t.world.tile()
            t.check(name .. ".onDeckAtPortSarim", sr == "ok" and st.level == 1
                    and math.abs(st.x - 3048) <= 2 and math.abs(st.z - 3231) <= 2,
                "t.world.tile() -> " .. tile_text(sr, st) .. " (want the deck, p_telejump(1_47_50_40_31) = 3048,3231,1)")
            t.exec(name .. ".gangplankAshore", t.player.climb, { loc = "ship_to_entrana_off", op_name = "Cross",
                at = { 3048, 3232, 1 }, dest = { 3048, 3234, 0 }, slack = 1 })
        end

        -- ---- gotoDeadMonk: back to Paterdomus ----
        leave_entrana("deadMonkTrip")
        gate_east("deadMonkTrip")
        t.exec("goto-deadmonk", t.player.goto_tile, 3406, 3494, 0)
        t.exec("gotoDeadMonk", t.player.talk_to, "devious_monk_dead", 1)
        t.exec("gotoDeadMonk-dialog", t.chat.play, {"mesbox:The poor guy","player:This isn't good"})
        t.ticks(2)
        t.expect("quest.stage.monk_found_dead", t.quest.expect_stage("monk_found_dead"))

        -- ---- talkToEntranaMonk / useGangPlank: the Port Sarim monk's boat ----
        gate_west("sarimTrip")
        t.exec("goto-talkToEntranaMonk", t.player.goto_tile, 3045, 3236, 0)
        t.exec("talkToEntranaMonk", t.player.talk_to, "shipmonk", 1)
        t.exec("talkToEntranaMonk-dialog", t.chat.play, {
            "npc:Do you seek passage",
            "choose:Yes, okay, I'm ready to go.",
            "player:Yes",
            "npc:Very well",
            "mesbox:The monk quickly searches you.",
        })
        t.await({ level = function()
            local r, tt = t.world.tile()
            return r == "ok" and tt.level == 1 and tt.x < 2900
        end, note = "talkToEntranaMonk: the sail to Entrana" }, 12)
        local dkr, dk = t.world.tile()
        t.check("talkToEntranaMonk-deck", dkr == "ok" and dk.level == 1 and math.abs(dk.x - 2834) <= 2 and math.abs(dk.z - 3331) <= 2,
            "t.world.tile() -> " .. tile_text(dkr, dk) .. " (want the deck, p_telejump(1_44_52_18_3) = 2834,3331,1: the search passed)")
        t.exec("useGangPlank", t.player.climb, { loc = "ship_from_entrana_off", op_name = "Cross",
            at = { 2834, 3333, 1 }, dest = { 2834, 3335, 0 }, slack = 1 })

        -- ---- talkToHighPriest (second visit) ----
        t.exec("goto-church2", t.player.goto_tile, 2851, 3347, 0)
        t.exec("talkToHighPriest2", t.player.talk_to, "high_priest_of_entrana", 1)
        t.exec("talkToHighPriest2-dialog", t.chat.play, {"npc:Adventurer","player:I went","npc:What?","player:It looked","npc:This is not good","player:I'll head"})
        t.ticks(2)
        t.expect("quest.stage.reported_priest", t.quest.expect_stage("reported_priest"))

        -- ---- talkToSirTiffy: Falador park ----
        leave_entrana("tiffyTrip")
        local _, snap = t.skill.snapshot()
        t.exec("goto-tiffy", t.player.goto_tile, 2997, 3371, 0)
        t.exec("talkToSirTiffy", t.player.talk_to, "rd_teleporter_guy", 1)
        t.exec("talkToSirTiffy-dialog", t.chat.play, {"npc:Jolly good","npc:Now how","choose:Devious Minds.","player:Devious","player:I've got","npc:This wouldn't","player:Uh","npc:Part of","player:Well","*","player:... and so","npc:Good","player:Is there","npc:Not yet"})
        t.quest.expect_complete()
        local sg = t.skill.expect_gain("smithing", 6500, snap)
        t.check("reward.smithing", sg == "ok", "smithing gain 6500 -> " .. tostring(sg))
        local rg = t.skill.expect_gain("runecraft", 5000, snap)
        t.check("reward.runecraft", rg == "ok", "runecraft gain 5000 -> " .. tostring(rg))
        local fg = t.skill.expect_gain("fletching", 5000, snap)
        t.check("reward.fletching", fg == "ok", "fletching gain 5000 -> " .. tostring(fg))
        t.finish(0)
    end,
}
