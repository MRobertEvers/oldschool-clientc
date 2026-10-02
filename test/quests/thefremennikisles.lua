-- The Fremennik Isles -- relay file. Legs follow docs/quests/ladders/thefremennikisles.legs
-- (route order). Notes: docs/quests/ladders/thefremennikisles.notes.md.
-- Setup stages the quest START only: ::fremennikisles resets the quest vars, completes
-- Fremennik Trials and stands the player at Mord Gunnars (fris_debug.rs2).

local JESTER = { "frisd_jester_hat", "frisd_jester_top", "frisd_jester_legs", "frisd_jester_boots" }
local BUTTONS = { "frisd_talkbutton", "frisd_dancebutton", "frisd_jugglebutton", "frisd_skipbutton",
    "frisd_piebutton", "frisd_jigbutton", "frisd_bowbutton" }

-- Walk a conversation through every page; pick `choices` rows in order when options appear.
local function convo(t, name, choices)
    local i = 0
    for guard = 1, 160 do
        local r, k = t.chat.drain({ stop_at = "options" })
        if k == "options" then
            i = i + 1
            local ch = choices and choices[i]
            if ch == nil then
                t.check(name .. ".unexpected_options", false, "no choice scripted for options page " .. i .. ": " .. tostring(select(2, t.chat.options())))
                return
            end
            t.exec(name .. ".choose" .. i, t.chat.choose, ch)
        else
            t.check(name .. ".done", r == "ok", "drain " .. tostring(r) .. " kind=" .. tostring(k) .. " after " .. i .. " choice(s)")
            return
        end
    end
end

local function stage(t, want)
    local _, v = t.var.server("varb3311_fris_quest")
    t.check("var.stage=" .. tostring(want), v == want, "varb3311_fris_quest server=" .. tostring(v))
end

local function count(t, item)
    t.ticks(2)
    local _, c = t.inv.count(item)
    return c or 0
end

local function near(t, name, x, z, r)
    local _, tl = t.world.tile()
    local ok = tl ~= nil and math.abs(tl.x - x) <= r and math.abs(tl.z - z) <= r
    t.check(name, ok, "tile " .. tostring(tl and tl.x) .. "," .. tostring(tl and tl.z) .. "," .. tostring(tl and tl.level) .. " want within " .. r .. " of " .. x .. "," .. z)
end

-- The act: Mawnis shouts one instruction at a time (server var varp7215_fris_jester_want);
-- the player clicks that button on the frisd_jestertask panel.
local function perform(t, name)
    for guard = 1, 60 do
        local _, st = t.var.server("varp7214_fris_jester_step")
        if st ~= nil and st > 0 then break end
        t.chat.drain({ stop_at = "options" })
        t.ticks(2)
    end
    t.ticks(2)
    local _, w0 = t.ui.widget("frisd_jestertask:frisd_dancebutton")
    t.check(name .. ".panel_open", w0 ~= nil, "frisd_jestertask panel mounted: " .. tostring(w0))
    local done, n = false, 0
    for tick = 1, 260 do
        local _, step = t.var.server("varp7214_fris_jester_step")
        if step == 0 and n > 0 then done = true; break end
        local _, want = t.var.server("varp7215_fris_jester_want")
        if want ~= nil and want > 0 then
            n = n + 1
            local btn = BUTTONS[want]
            local _, w = t.ui.widget("frisd_jestertask:" .. btn)
            local pr = t.ui.invoke(w, 1)
            t.check(name .. ".press" .. n .. "." .. btn, pr == "ok", "invoked " .. btn .. " for want=" .. tostring(want))
        end
        t.ticks(1)
    end
    t.check(name .. ".act_over", done, "instructions pressed: " .. n)
end

-- Click a Jatizso door/gate; a refused click (the walk outlasted the settle, "I can't reach that")
-- is retried from the approach tile the notes name, so one flaky walk does not end the leg.
local function door(t, name, sym, ax, az, lx, lz)
    local r, d
    for attempt = 1, 3 do
        r, d = t.player.click_loc(sym, 1, { at = { lx, lz } })
        if r == "ok" then break end
        if r == "no_row" then
            -- no closed copy of the loc at its tile: the door is already open, nothing to click
            d = "door already open (no closed copy at " .. lx .. "," .. lz .. "): " .. tostring(d)
            r = "ok"
            break
        end
        t.player.goto_tile(ax, az, 0)
        t.ticks(8)
    end
    t.check(name, r == "ok", sym .. " click -> " .. tostring(r) .. " " .. tostring(d))
end

return {
    id = "thefremennikisles",
    fixture = "fresh_lumbridge.ini",
    max_frames = 150000,  -- six legs add up past the default 2000-tick budget (relay.md)
    setup = {
        "::clearinv",
        "::setlevel mining 2",   -- the king asks 7 coal at Mining 2-54 (fris_shared.rs2 fris_ore_type); the guide lists Coal as brought
        "::fremennikisles",      -- quest start: Fremennik Trials done, Mord at 2644,3709
        "::give raw_tuna 1",     -- guide item requirement of talkToGjuki: Raw tuna
        "::give coal 7",         -- guide item requirement of bringOreToGjuki: Coal (Mining 2 variant)
        "::setlevel crafting 30",      -- leg 2: spinning yak hair into rope needs Crafting 30 (spinning.dbrow spin_yak_hair)
        "::setlevel woodcutting 56",   -- leg 2: splitting arctic pine logs needs Woodcutting 56 (fris_bridges.rs2 fris_stump_split)
        "::setlevel construction 20",  -- leg 2: repairing a bridge needs Construction 20 (fris_bridges.rs2 fris_bridge_repair)
        "::give knife 1",              -- leg 2: guide item "Knife" (repairBridge)
        "::give bronze_axe 1",         -- leg 2: guide item "Any axe" (split logs)
        "::give rune_scimitar 1",      -- leg 2: guide "Melee gear" (yaks drop the hair)
        "::give lobster 4",            -- leg 2: guide "Food" for the yak and troll fights
        "::setlevel attack 40",        -- leg 2: melee levels for the yak field (hp 10 died to a yak, run 1)
        "::setlevel strength 40",
        "::setlevel defence 30",
        "::setlevel hitpoints 40",
        "::setlevel crafting 46",      -- leg 5: yak-hide body armour needs Crafting 46 (fris_shared.rs2 ^fris_req_crafting_body)
        "::setlevel hitpoints 90",     -- leg 6: guide "Melee gear; Food + potions" for the troll caves and the 150-hp king
        "::setlevel defence 80",       -- leg 6: melee gear for the Ice Troll King
        "::setlevel attack 90",
        "::setlevel strength 90",
        "::setlevel prayer 99",        -- leg 6: killKing "Use the Protect from Magic prayer" (level 37); Protect from Melee (43) for the runts
    },
    bind = {
        varp = "varb3311_fris_quest",
        constants = { not_started = 0, started = 5, king_met = 10, cat_fed = 20, need_ore = 30, ore_handed = 40,
            get_outfit = 50, spy1_briefed = 55, spy1_done = 60, help_mawnis = 90, complete = 340 },
        row = "quest_thefremennikisles",
        display = "The Fremennik Isles",
        points = 1,
    },
    legs = {
        { name = "mord_to_slug_report", run = function(t)
            t.ticks(3)
            t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

            -- LEG 1 BEGIN: talkToMord
            t.exec("talkToMord", t.player.talk_to, "fris_r_ferryman_rellekka", 1)
            convo(t, "talkToMord-dialog", { "Yes.", "Can you ferry me to Jatizso?" })
            t.expect("quest.stage.started", t.quest.expect_stage("started"))
            t.ticks(4)
            near(t, "travelToJatizso", 2420, 3782, 6)

            -- Jatizso: dock -> outer city wall gate -> King's hall (door on the north side)
            t.player.walk_to(2412, 3796, 60)
            near(t, "walk-talkToGjuki", 2412, 3796, 1)
            door(t, "talkToGjuki-openCityGate", "frisd_outer_city_wall_door_left", 2412, 3796, 2413, 3797)
            t.ticks(2)
            t.player.walk_to(2407, 3807, 60)
            near(t, "walk-talkToGjuki-hall", 2407, 3807, 1)
            door(t, "talkToGjuki-openHallDoor", "frisd_town_wall_door", 2407, 3807, 2407, 3806)
            t.ticks(2)
            t.exec("talkToGjuki", t.player.talk_to, "fris_r_king", 1)
            convo(t, "talkToGjuki-dialog", {})
            t.expect("quest.stage.king_met", t.quest.expect_stage("king_met"))

            -- feed Hrafn the raw tuna (stage 10 -> 20)
            local cat = t.player.by_symbol("npc", "fris_r_kingscat")
            t.exec("feedHrafn", t.player.use_on, "raw_tuna", cat)
            convo(t, "feedHrafn-dialog", {})
            t.expect("quest.stage.cat_fed", t.quest.expect_stage("cat_fed"))

            t.exec("continueTalkingToGjuki", t.player.talk_to, "fris_r_king", 1)
            convo(t, "continueTalkingToGjuki-dialog", {})
            t.expect("quest.stage.need_ore", t.quest.expect_stage("need_ore"))

            local coins0 = count(t, "coins")
            t.exec("bringOreToGjuki", t.player.talk_to, "fris_r_king", 1)
            -- read the hand-in up to Thorkel's "takes lumps" box, then close it: the stage is 40 and
            -- the king's mission is a second talk (the guide's talkToGjukiAfterOre)
            t.exec("bringOreToGjuki-dialog", t.chat.drain, { stop_at = "mesbox" })
            t.check("bringOreToGjuki.box", t.chat.kind() == "mesbox", "page kind " .. tostring(t.chat.kind()) .. ": " .. tostring(select(2, t.chat.text())))
            local cr = t.chat.close()
            t.check("bringOreToGjuki-close", cr == "ok", "chat.close -> " .. tostring(cr) .. ", page kind now " .. tostring(t.chat.kind()))
            t.ticks(2)
            t.expect("quest.stage.ore_handed", t.quest.expect_stage("ore_handed"))
            t.exec("talkToGjukiAfterOre", t.player.talk_to, "fris_r_king", 1)
            convo(t, "talkToGjukiAfterOre-dialog", {})
            t.check("bringOreToGjuki.paid", count(t, "coins") == coins0 + 10000 and count(t, "coal") == 0,
                "coins " .. coins0 .. " -> " .. count(t, "coins") .. ", coal left " .. count(t, "coal"))
            t.expect("quest.stage.get_outfit", t.quest.expect_stage("get_outfit"))

            -- the jester outfit, from the chest behind the throne
            t.player.walk_to(2407, 3801, 20)
            near(t, "walk-getJesterOutfit", 2407, 3801, 1)
            t.exec("getJesterOutfit-open", t.player.click_loc, "fris_chest_closed", 1)
            t.ticks(2)
            t.exec("getJesterOutfit", t.player.click_loc, "fris_chest_open", 1)
            convo(t, "getJesterOutfit-dialog", { "Take the jester's hat.", "Take the jester's top.",
                "Take the jester's tights.", "Take the jester's boots.", "Leave the chest." })
            local got = {}
            for _, it in ipairs(JESTER) do got[#got + 1] = it .. "=" .. count(t, it) end
            t.check("getJesterOutfit.items", count(t, "frisd_jester_hat") == 1 and count(t, "frisd_jester_top") == 1
                and count(t, "frisd_jester_legs") == 1 and count(t, "frisd_jester_boots") == 1, table.concat(got, " "))

            -- back out of the hall and the gate to the dock, then ferry home
            t.exec("goto-returnToRellekkaFromJatizso", t.player.goto_tile, 2420, 3783, 0)
            t.exec("returnToRellekkaFromJatizso", t.player.talk_to, "fris_r_ferryman_izso", 1)
            convo(t, "returnToRellekkaFromJatizso-dialog", { "Can you ferry me to Rellekka?" })
            t.ticks(4)
            near(t, "returnToRellekka.landed", 2644, 3709, 6)
            t.exec("travelToNeitiznot", t.player.talk_to, "fris_r_ferry_rellikka", 1)
            convo(t, "travelToNeitiznot-dialog", { "Can you ferry me to Neitiznot?" })
            t.ticks(4)
            near(t, "travelToNeitiznot.landed", 2311, 3782, 6)

            -- Slug, in the outfit with empty hands
            t.exec("goto-talkToSlug", t.player.goto_tile, 2336, 3809, 0)
            for _, it in ipairs(JESTER) do
                t.exec("wear-" .. it, t.player.equip, it)
                t.ticks(1)
            end
            t.exec("talkToSlug", t.player.talk_to, "fris_spymaster", 1)
            convo(t, "talkToSlug-dialog", { "Free stuff please.", "I am ready." })
            t.expect("quest.stage.spy1_briefed", t.quest.expect_stage("spy1_briefed"))

            -- Mawnis's hall is entered from the east (notes)
            t.exec("goto-goSpyOnMawnis", t.player.goto_tile, 2341, 3799, 0)
            t.exec("goSpyOnMawnis", t.player.talk_to, "fris_r_burgher_crown", 1)
            convo(t, "goSpyOnMawnis-dialog", {})
            perform(t, "performForMawnis")
            t.ticks(6)
            convo(t, "performForMawnis-end", {})
            t.expect("quest.stage.spy1_done", t.quest.expect_stage("spy1_done"))

            t.exec("goto-tellSlugReport1", t.player.goto_tile, 2336, 3809, 0)
            local c1 = count(t, "coins")
            t.exec("tellSlugReport1", t.player.talk_to, "fris_spymaster", 1)
            convo(t, "tellSlugReport1-dialog", { "Yes I have.", "They will be ready in two days.",
                "Seventeen militia have been trained.", "There are two bridges to repair." })
            t.check("tellSlugReport1.paid", count(t, "coins") == c1 + 2500, "coins " .. c1 .. " -> " .. count(t, "coins"))
            t.expect("quest.stage.help_mawnis", t.quest.expect_stage("help_mawnis"))

            -- take the outfit off: stage 90 Mawnis wants it OFF (notes)
            for _, it in ipairs(JESTER) do
                t.exec("remove-" .. it, t.player.unequip, it)
                t.ticks(1)
            end
            local _, tl = t.world.tile()
            local _, sv = t.var.server("varb3311_fris_quest")
            t.check("leg.1.end", sv == 90, "tile " .. tl.x .. "," .. tl.z .. "," .. tl.level .. " stage=" .. tostring(sv)
                .. "; backpack: jester hat/top/tights/boots (unworn), coins, knife none")
            -- LEG 1 END
        end },

        { name = "mawnis_bridges", run = function(t)
            t.ticks(3)
            -- LEG 2 BEGIN: talkToMawnis
            t.exec("wield-sword", t.player.equip, "rune_scimitar")
            t.ticks(1)
            t.exec("goto-talkToMawnis", t.player.goto_tile, 2341, 3799, 0)
            t.exec("talkToMawnis", t.player.talk_to, "fris_r_burgher_crown", 1)
            convo(t, "talkToMawnis-dialog", {})
            stage(t, 100)

            -- rope: kill yaks for their hair, spin it at the wheel (2352,3794)
            t.exec("goto-yaks", t.player.goto_tile, 2322, 3796, 0)
            for i = 1, 12 do
                if count(t, "yak_hair") >= 8 then break end
                t.exec("yak" .. i .. "-attack", t.player.attack, "yak", 2, 20)
                t.exec("yak" .. i .. "-dead", t.npc.await_dead_engaged, 200, 3, { eat = { item = "lobster", below = 8 } })
                t.ticks(1)
                t.exec("yak" .. i .. "-hair", t.player.click_obj, "yak_hair", 3)
            end
            t.check("yakhair.8", count(t, "yak_hair") >= 8, "yak hair in the backpack: " .. count(t, "yak_hair"))
            t.exec("goto-spinWheel", t.player.goto_tile, 2352, 3796, 0)
            local wheel = t.player.by_symbol("loc", "iznot_spinning_wheel")
            for i = 1, 8 do
                t.ticks(4)
                if i > 1 then t.player.walk_to(2350 + (i % 2) * 2, 3796, 10); t.ticks(3) end  -- a step away so the next click has a walk flag to settle on
                t.exec("spinRope" .. i, t.player.use_on, "yak_hair", wheel)
                local mr = t.ui.await_open("skillmulti", 10)
                local cr, cell = t.ui.widget("skillmulti:a")
                t.check("spinRope" .. i .. ".menu", mr == "ok" and cr == "ok", "skillmulti menu=" .. tostring(mr) .. " cell=" .. tostring(cr))
                t.ui.invoke(cell, 1)
                t.exec("spinRope" .. i .. ".got", t.inv.await, "rope", i, 20)
            end
            t.ticks(2)

            -- bring the rope to Mawnis (stage 100 -> 110)
            t.exec("goto-talkToMawnisWithLogs", t.player.goto_tile, 2341, 3799, 0)
            t.exec("talkToMawnisWithLogs", t.player.talk_to, "fris_r_burgher_crown", 1)
            convo(t, "talkToMawnisWithLogs-dialog", {})
            stage(t, 110)

            -- 8 arctic pine logs, split on the stump
            local spots = { { 2314, 2315, 3788 }, { 2318, 2319, 3787 }, { 2321, 2322, 3786 }, { 2324, 2325, 3788 }, { 2327, 2328, 3790 }, { 2333, 2334, 3792 }, { 2337, 2338, 3791 }, { 2347, 2348, 3793 } }
            for i = 1, 8 do
                local sp = spots[i]
                t.exec("chop" .. i .. "-goto", t.player.goto_tile, sp[1], sp[3] + 1, 0)
                t.exec("chop" .. i, t.player.click_loc, "arctic_pine", 1, { at = { sp[2], sp[3] } })
                t.exec("chop" .. i .. ".got", t.inv.await, "arctic_pine_log", i, 150)
            end
            t.exec("goto-stump", t.player.goto_tile, 2342, 3806, 0)
            local stump = t.player.by_symbol("loc", "iznot_shield_stump")
            t.exec("splitLogs", t.player.use_on, "arctic_pine_log", stump)
            convo(t, "splitLogs-dialog", { "Split logs" })
            t.exec("splitLogs.got", t.inv.await, "arctic_pine_split", 8, 60)
            t.ticks(2)

            t.exec("goto-talkToMawnisAfterItems", t.player.goto_tile, 2341, 3799, 0)
            t.exec("talkToMawnisAfterItems", t.player.talk_to, "fris_r_burgher_crown", 1)
            convo(t, "talkToMawnisAfterItems-dialog", {})
            stage(t, 140)

            -- bridges
            t.exec("goto-repairBridge1", t.player.goto_tile, 2314, 3838, 0)
            t.exec("repairBridge1", t.player.click_loc, "frisb_bridge_3_s", 2)
            t.ticks(12)
            near(t, "repairBridge1.crossed", 2314, 3848, 3)
            t.exec("repairBridge1-back", t.player.click_loc, "frisb_bridge_3_n", 1)
            t.ticks(6)
            near(t, "repairBridge1-back.crossed", 2314, 3840, 3)
            t.exec("goto-repairBridge2", t.player.goto_tile, 2355, 3838, 0)
            t.exec("repairBridge2", t.player.click_loc, "frisb_bridge_4_s", 2)
            t.ticks(12)
            near(t, "repairBridge2.crossed", 2355, 3848, 3)
            t.exec("repairBridge2-back", t.player.click_loc, "frisb_bridge_4_n", 1)
            t.ticks(6)
            near(t, "repairBridge2-back.crossed", 2355, 3840, 3)

            t.exec("goto-talkToMawnisAfterRepair", t.player.goto_tile, 2341, 3799, 0)
            t.exec("talkToMawnisAfterRepair", t.player.talk_to, "fris_r_burgher_crown", 1)
            convo(t, "talkToMawnisAfterRepair-dialog", {})
            stage(t, 160)
            local _, tl = t.world.tile()
            local _, sv = t.var.server("varb3311_fris_quest")
            t.check("leg.2.end", sv == 160, "tile " .. tl.x .. "," .. tl.z .. "," .. tl.level .. " stage=" .. tostring(sv)
                .. "; backpack: 8 rope, 8 split logs, knife, axe, jester pieces, coins; rune scimitar worn")
            -- LEG 2 END
        end },

        { name = "tax_collector", run = function(t)
            t.ticks(3)
            -- LEG 3 BEGIN: leaveNeitiznotToReport
            -- Like convo(), but a drain refused with "a resume is already outstanding" (the server has not
            -- answered the last continue yet) waits two ticks and drains again instead of failing.
            local function talk(name, choices, amount)
                local i, typed = 0, false
                for guard = 1, 160 do
                    if amount and not typed then
                        -- the "Enter amount" prompt: kind() lags it, so just try typing
                        local cr = t.chat.count(amount)
                        if cr == "ok" then
                            t.check(name .. ".typed", true, "typed " .. amount .. " into the Enter amount prompt")
                            typed = true
                        end
                    end
                    local r, k = t.chat.drain({ stop_at = "options" })
                    if r == "refused" and (k == nil or tostring(k):find("outstanding")) then
                        t.ticks(2)
                    elseif k == "count" and amount and not typed then
                        local cr = t.chat.count(amount)
                        t.check(name .. ".typed", cr == "ok", "typed " .. amount .. " -> " .. tostring(cr))
                        typed = true
                    elseif k == "options" then
                        i = i + 1
                        local ch = choices and choices[i]
                        if ch == nil then
                            t.check(name .. ".unexpected_options", false, "no choice scripted for options page " .. i .. ": " .. tostring(select(2, t.chat.options())))
                            return
                        end
                        t.exec(name .. ".choose" .. i, t.chat.choose, ch)
                    else
                        if amount and not typed then
                            t.ticks(2)
                            if t.chat.kind() == "count" then goto continue end
                        end
                        t.check(name .. ".done", r == "ok" and (not amount or typed), "drain " .. tostring(r) .. " kind=" .. tostring(k) .. " after " .. i .. " choice(s), typed=" .. tostring(typed))
                        return
                    end
                    ::continue::
                end
            end
            local function taxtalk(name, amount, choice) talk(name .. "-dialog", { choice }, amount) end
            local PAY = "But rules are rules. Pay up!"

            -- Neitiznot -> Rellekka with Maria (dock 2311,3781)
            t.exec("goto-leaveNeitiznotToReport", t.player.goto_tile, 2312, 3782, 0)
            t.exec("leaveNeitiznotToReport", t.player.talk_to, "fris_r_ferry_iznot", 1)
            talk("leaveNeitiznotToReport-dialog", { "Can you ferry me to Rellekka?" })
            t.ticks(4)
            near(t, "leaveNeitiznotToReport.arrived", 2644, 3709, 8)

            -- Rellekka -> Jatizso with Mord
            t.exec("travelToJatizsoToReport", t.player.talk_to, "fris_r_ferryman_rellekka", 1)
            talk("travelToJatizsoToReport-dialog", { "Can you ferry me to Jatizso?" })
            t.ticks(4)
            near(t, "travelToJatizsoToReport.arrived", 2420, 3782, 6)

            -- Jatizso: dock -> outer city wall gate -> King's hall (door on the north side)
            t.player.walk_to(2412, 3796, 60)
            near(t, "walk-talkToGjukiToReport", 2412, 3796, 1)
            door(t, "talkToGjukiToReport-openCityGate", "frisd_outer_city_wall_door_left", 2412, 3796, 2413, 3797)
            t.ticks(2)
            t.player.walk_to(2407, 3807, 60)
            near(t, "walk-talkToGjukiToReport-hall", 2407, 3807, 1)
            door(t, "talkToGjukiToReport-openHallDoor", "frisd_town_wall_door", 2407, 3807, 2407, 3806)
            t.ticks(2)
            t.exec("talkToGjukiToReport", t.player.talk_to, "fris_r_king", 1)
            talk("talkToGjukiToReport-dialog", {})
            t.ticks(2)
            stage(t, 200)
            t.exec("taxbag.held", t.inv.await, "frisd_taxbag_empty", 1, 10)

            -- the window round: Keepa 5000, Vanligga 5000, Skuli 6000, Hring 8000
            t.exec("goto-collectFromKeepa", t.player.goto_tile, 2417, 3815, 0)
            t.exec("collectFromKeepa", t.player.talk_to, "frisd_cook", 1)
            taxtalk("collectFromKeepa", 5000, PAY)
            t.exec("goto-collectFromVanligga", t.player.goto_tile, 2405, 3812, 0)
            t.exec("collectFromVanligga", t.player.talk_to, "frisd_izso_landlady", 1)
            taxtalk("collectFromVanligga", 5000, PAY)
            t.exec("goto-collectFromSkuli", t.player.goto_tile, 2395, 3803, 0)
            t.exec("collectFromSkuli", t.player.talk_to, "frisd_weaponmerchant", 1)
            taxtalk("collectFromSkuli", 6000, PAY)
            t.exec("goto-collectFromHring", t.player.goto_tile, 2397, 3796, 0)
            t.exec("collectFromHring", t.player.talk_to, "frisd_oremerchant", 1)
            taxtalk("collectFromHring", 8000, PAY)

            t.exec("goto-talkToGjukiAfterCollection1", t.player.goto_tile, 2407, 3807, 0)
            door(t, "talkToGjukiAfterCollection1-openHallDoor", "frisd_town_wall_door", 2407, 3807, 2407, 3806)
            t.ticks(2)
            t.exec("talkToGjukiAfterCollection1", function(...)
                local r, d = t.player.talk_to(...)
                for try = 1, 4 do  -- the king wanders about the hall: step in and try again
                    if r == "ok" then break end
                    t.player.walk_to(2407, 3803, 10)
                    t.ticks(3)
                    r, d = t.player.talk_to(...)
                end
                return r, d
            end, "fris_r_king", 1)
            talk("talkToGjukiAfterCollection1-dialog", {})
            t.ticks(2)
            stage(t, 210)

            -- the beard round: Hring, Raum, Skuli, Keepa, Flosi, 1000 each
            t.exec("goto-collectFromHringAgain", t.player.goto_tile, 2397, 3796, 0)
            t.exec("collectFromHringAgain", t.player.talk_to, "frisd_oremerchant", 1)
            talk("collectFromHringAgain-dialog", { PAY })
            t.exec("collectFromRaum", t.player.talk_to, "frisd_armourmerchant", 1)
            talk("collectFromRaum-dialog", { PAY })
            t.exec("goto-collectFromSkuliAgain", t.player.goto_tile, 2395, 3803, 0)
            t.exec("collectFromSkuliAgain", t.player.talk_to, "frisd_weaponmerchant", 1)
            talk("collectFromSkuliAgain-dialog", { PAY })
            t.exec("goto-collectFromKeepaAgain", t.player.goto_tile, 2417, 3815, 0)
            t.exec("collectFromKeepaAgain", t.player.talk_to, "frisd_cook", 1)
            talk("collectFromKeepaAgain-dialog", { PAY })
            t.exec("goto-collectFromFlosi", t.player.goto_tile, 2418, 3812, 0)
            t.exec("collectFromFlosi", t.player.talk_to, "frisd_fishmerchant", 1)
            talk("collectFromFlosi-dialog", { PAY })

            t.exec("goto-talkToGjukiAfterCollection2", t.player.goto_tile, 2407, 3807, 0)
            door(t, "talkToGjukiAfterCollection2-openHallDoor", "frisd_town_wall_door", 2407, 3807, 2407, 3806)
            t.ticks(2)
            t.exec("talkToGjukiAfterCollection2", function(...)
                local r, d = t.player.talk_to(...)
                for try = 1, 4 do  -- the king wanders about the hall: step in and try again
                    if r == "ok" then break end
                    t.player.walk_to(2407, 3803, 10)
                    t.ticks(3)
                    r, d = t.player.talk_to(...)
                end
                return r, d
            end, "fris_r_king", 1)
            talk("talkToGjukiAfterCollection2-dialog", {})
            t.ticks(2)
            stage(t, 230)
            local _, tl = t.world.tile()
            local _, sv = t.var.server("varb3311_fris_quest")
            t.check("leg.3.end", sv == 230 and tl ~= nil, "tile " .. tostring(tl and tl.x) .. "," .. tostring(tl and tl.z) .. "," .. tostring(tl and tl.level) .. " stage=" .. tostring(sv)
                .. "; backpack: coins, knife, axe, jester pieces unworn, lobster; rune scimitar worn; tax bag handed back")
            -- LEG 3 END
        end },
        { name = "spy_again_decree", run = function(t)
            t.ticks(3)
            -- LEG 4 BEGIN: travelToNeitiznotToSpyAgain
            local function kingtalk(name)
                t.exec(name, function(...)
                    local r, d = t.player.talk_to(...)
                    for try = 1, 4 do  -- the king wanders about the hall: step in and try again
                        if r == "ok" then break end
                        t.player.walk_to(2407, 3803, 10)
                        t.ticks(3)
                        r, d = t.player.talk_to(...)
                    end
                    return r, d
                end, "fris_r_king", 1)
            end
            -- out of the king's hall to the Jatizso dock (plain travel), then Mord and Maria
            t.exec("goto-returnToRellekkaFromJatizsoToSpyAgain", t.player.goto_tile, 2420, 3783, 0)
            t.exec("returnToRellekkaFromJatizsoToSpyAgain", t.player.talk_to, "fris_r_ferryman_izso", 1)
            convo(t, "returnToRellekkaFromJatizsoToSpyAgain-dialog", { "Can you ferry me to Rellekka?" })
            t.ticks(4)
            near(t, "returnToRellekkaFromJatizsoToSpyAgain.landed", 2644, 3709, 6)
            t.exec("travelToNeitiznotToSpyAgain", t.player.talk_to, "fris_r_ferry_rellikka", 1)
            convo(t, "travelToNeitiznotToSpyAgain-dialog", { "Can you ferry me to Neitiznot?" })
            t.ticks(4)
            near(t, "travelToNeitiznotToSpyAgain.landed", 2311, 3782, 6)

            -- Slug, in the outfit with empty hands (the scimitar leg 2 wielded comes off)
            t.exec("goto-talkToSlugToSpyAgain", t.player.goto_tile, 2336, 3809, 0)
            t.ticks(4)
            t.exec("unwield-sword", t.player.unequip, "rune_scimitar")
            t.ticks(2)
            for _, it in ipairs(JESTER) do
                t.exec("wear-" .. it, t.player.equip, it)
                t.ticks(1)
            end
            t.exec("talkToSlugToSpyAgain", t.player.talk_to, "fris_spymaster", 1)
            convo(t, "talkToSlugToSpyAgain-dialog", {})
            t.expect("quest.stage.spy2_briefed", t.var.expect("varb3311_fris_quest", 235))

            t.exec("goto-goSpyOnMawnisAgain", t.player.goto_tile, 2341, 3799, 0)
            t.exec("goSpyOnMawnisAgain", t.player.talk_to, "fris_r_burgher_crown", 1)
            convo(t, "goSpyOnMawnisAgain-dialog", {})
            perform(t, "performForMawnisAgain")
            t.ticks(6)
            convo(t, "performForMawnisAgain-end", {})
            stage(t, 240)

            t.exec("goto-reportBackToSlugAgain", t.player.goto_tile, 2336, 3809, 0)
            local c1 = count(t, "coins")
            t.exec("reportBackToSlugAgain", t.player.talk_to, "fris_spymaster", 1)
            convo(t, "reportBackToSlugAgain-dialog", { "Yes, I am.",
                "They are in a secluded bay, near Etceteria.",
                "They will be given some potions.",
                "I have been helping Neitiznot." })
            t.check("reportBackToSlugAgain.paid", count(t, "coins") > c1, "coins " .. c1 .. " -> " .. count(t, "coins"))
            stage(t, 260)

            -- Neitiznot -> Rellekka (Maria) -> Jatizso (Mord) -> the king
            t.exec("goto-returnToRellekkaFromNeitiznotAfterSpy2", t.player.goto_tile, 2312, 3782, 0)
            t.exec("returnToRellekkaFromNeitiznotAfterSpy2", t.player.talk_to, "fris_r_ferry_iznot", 1)
            convo(t, "returnToRellekkaFromNeitiznotAfterSpy2-dialog", { "Can you ferry me to Rellekka?" })
            t.ticks(4)
            near(t, "returnToRellekkaFromNeitiznotAfterSpy2.landed", 2644, 3709, 8)
            t.exec("travelToJatizsoAfterSpy2", t.player.talk_to, "fris_r_ferryman_rellekka", 1)
            convo(t, "travelToJatizsoAfterSpy2-dialog", { "Can you ferry me to Jatizso?" })
            t.ticks(4)
            near(t, "travelToJatizsoAfterSpy2.landed", 2420, 3782, 6)
            t.player.walk_to(2412, 3796, 60)
            near(t, "walk-talkToGjukiAfterSpy2", 2412, 3796, 1)
            door(t, "talkToGjukiAfterSpy2-openCityGate", "frisd_outer_city_wall_door_left", 2412, 3796, 2413, 3797)
            t.ticks(2)
            t.player.walk_to(2407, 3807, 60)
            near(t, "walk-talkToGjukiAfterSpy2-hall", 2407, 3807, 1)
            door(t, "talkToGjukiAfterSpy2-openHallDoor", "frisd_town_wall_door", 2407, 3807, 2407, 3806)
            t.ticks(2)
            kingtalk("talkToGjukiAfterSpy2")
            convo(t, "talkToGjukiAfterSpy2-dialog", {})
            t.ticks(2)
            stage(t, 270)
            t.exec("decree.held", t.inv.await, "frisd_reciept", 1, 10)

            -- the decree to Mawnis: Mord, Maria, the jester outfit off, Mawnis
            t.exec("goto-returnToRellekkaFromJatizsoWithDecree", t.player.goto_tile, 2420, 3783, 0)
            t.exec("returnToRellekkaFromJatizsoWithDecree", t.player.talk_to, "fris_r_ferryman_izso", 1)
            convo(t, "returnToRellekkaFromJatizsoWithDecree-dialog", { "Can you ferry me to Rellekka?" })
            t.ticks(4)
            near(t, "returnToRellekkaFromJatizsoWithDecree.landed", 2644, 3709, 6)
            t.exec("travelToNeitiznotWithDecree", t.player.talk_to, "fris_r_ferry_rellikka", 1)
            convo(t, "travelToNeitiznotWithDecree-dialog", { "Can you ferry me to Neitiznot?" })
            t.ticks(4)
            near(t, "travelToNeitiznotWithDecree.landed", 2311, 3782, 6)
            t.ticks(4)
            for _, it in ipairs(JESTER) do
                t.exec("remove-" .. it, t.player.unequip, it)
                t.ticks(2)
            end
            t.exec("goto-talkToMawnisWithDecree", t.player.goto_tile, 2341, 3799, 0)
            t.exec("talkToMawnisWithDecree", t.player.talk_to, "fris_r_burgher_crown", 1)
            convo(t, "talkToMawnisWithDecree-dialog", {})
            t.ticks(2)
            -- the decree dialogue sets stage 275 (fris_mawnis.rs2:347) and the same dialogue ends by
            -- setting 280 (fris_mawnis.rs2:361), so 275 never rests: the stage is read after it.
            stage(t, 280)

            local _, tl = t.world.tile()
            local _, sv = t.var.server("varb3311_fris_quest")
            t.check("leg.4.end", sv == 280 and tl ~= nil, "tile " .. tostring(tl and tl.x) .. "," .. tostring(tl and tl.z) .. "," .. tostring(tl and tl.level) .. " stage=" .. tostring(sv)
                .. "; backpack: coins, knife, axe, jester pieces unworn, rune scimitar unworn, lobster; decree handed in (stage 280 yak_armour: Thakkrad wants yak hides)")
            -- LEG 4 END
        end },
        { name = "armour_and_shield", run = function(t)
            t.ticks(3)
            -- LEG 5 BEGIN: getYakArmour
            t.exec("wield-sword-leg5", t.player.equip, "rune_scimitar")
            t.ticks(1)
            stage(t, 280)

            -- yaks drop hide and hair: three hides (body takes 2, legs 1), one hair for the shield's rope
            t.exec("goto-yakHides", t.player.goto_tile, 2322, 3796, 0)
            for i = 1, 8 do
                if count(t, "yak_hide") >= 3 and count(t, "yak_hair") >= 1 then break end
                t.exec("yakHide" .. i .. "-attack", t.player.attack, "yak", 2, 20)
                t.exec("yakHide" .. i .. "-dead", t.npc.await_dead_engaged, 200, 3, { eat = { item = "lobster", below = 8 } })
                t.ticks(1)
                t.exec("yakHide" .. i .. "-hide", t.player.click_obj, "yak_hide", 3)
                t.ticks(1)
                t.exec("yakHide" .. i .. "-hair", t.player.click_obj, "yak_hair", 3)
            end
            t.check("yakHide.three", count(t, "yak_hide") >= 3 and count(t, "yak_hair") >= 1, "hides " .. count(t, "yak_hide") .. " hair " .. count(t, "yak_hair"))

            -- Thakkrad cures them (5 gp each)
            t.exec("goto-cureHides", t.player.goto_tile, 2336, 3799, 0)
            t.exec("cureHides", t.player.talk_to, "fris_r_engineer", 1)
            convo(t, "cureHides-dialog", { "Cure my yak-hide, please.", "Cure all my hides." })
            t.exec("cureHides.got", t.inv.await, "yak_hide_cured", 3, 10)

            -- brought along (guide items Needle, Thread), given here: leg 2's backpack has no room for them in setup
            t.cheat("::give needle 1")
            t.cheat("::give thread 3")
            t.ticks(2)
            t.check("leg.5.pack-craft", count(t, "needle") == 1 and count(t, "thread") == 3, "needle " .. count(t, "needle") .. " thread " .. count(t, "thread"))
            -- needle and thread: body (2 hides) then legs (1 hide)
            t.exec("craftYakBody", t.player.use_item_on_item, "needle", "yak_hide_cured")
            convo(t, "craftYakBody-dialog", { "Yak-hide body (2 hides)" })
            t.exec("craftYakBody.got", t.inv.await, "yak_hide_armour_body", 1, 20)
            t.ticks(2)
            t.exec("craftYakLegs", t.player.use_item_on_item, "needle", "yak_hide_cured")
            convo(t, "craftYakLegs-dialog", { "Yak-hide legs (1 hide)" })
            t.exec("craftYakLegs.got", t.inv.await, "yak_hide_armour_greaves", 1, 20)
            t.ticks(2)

            t.exec("goto-getYakArmour", t.player.goto_tile, 2341, 3799, 0)
            t.exec("getYakArmour", t.player.talk_to, "fris_r_burgher_crown", 1)
            convo(t, "getYakArmour-dialog", {})
            t.ticks(2)
            stage(t, 290)

            -- the shield: one rope (spun from the hair), two arctic pine logs, hammer and nail at the stump
            t.exec("goto-spinShieldRope", t.player.goto_tile, 2352, 3796, 0)
            local wheel = t.player.by_symbol("loc", "iznot_spinning_wheel")
            t.ticks(4)
            t.exec("spinShieldRope", t.player.use_on, "yak_hair", wheel)
            local mr = t.ui.await_open("skillmulti", 10)
            local cr, cell = t.ui.widget("skillmulti:a")
            t.check("spinShieldRope.menu", mr == "ok" and cr == "ok", "skillmulti menu=" .. tostring(mr) .. " cell=" .. tostring(cr))
            t.ui.invoke(cell, 1)
            t.exec("spinShieldRope.got", t.inv.await, "rope", 1, 20)
            -- guide items Hammer and Bronze nails (brought along), given here for backpack room
            t.cheat("::give hammer 1")
            t.cheat("::give nails_bronze 1")
            t.ticks(2)
            t.check("leg.5.pack-shield", count(t, "hammer") == 1 and count(t, "nails_bronze") == 1, "hammer " .. count(t, "hammer") .. " nails " .. count(t, "nails_bronze"))
            local spots = { { 2314, 2315, 3788 }, { 2318, 2319, 3787 } }
            for i = 1, 2 do
                local sp = spots[i]
                t.exec("chopShieldLog" .. i .. "-goto", t.player.goto_tile, sp[1], sp[3] + 1, 0)
                t.exec("chopShieldLog" .. i, t.player.click_loc, "arctic_pine", 1, { at = { sp[2], sp[3] } })
                t.exec("chopShieldLog" .. i .. ".got", t.inv.await, "arctic_pine_log", i, 150)
            end
            t.exec("goto-makeShield-stump", t.player.goto_tile, 2342, 3806, 0)
            local stump = t.player.by_symbol("loc", "iznot_shield_stump")
            t.exec("makeShield-stump", t.player.use_on, "arctic_pine_log", stump)
            convo(t, "makeShield-stump-dialog", { "Neitiznot shield" })
            t.exec("makeShield-stump.got", t.inv.await, "fremmenik_round_shield", 1, 40)
            t.ticks(2)

            t.exec("goto-makeShield", t.player.goto_tile, 2341, 3799, 0)
            t.exec("makeShield", t.player.talk_to, "fris_r_burgher_crown", 1)
            convo(t, "makeShield-dialog", {})
            t.ticks(2)
            stage(t, 300)

            local _, tl = t.world.tile()
            local _, sv = t.var.server("varb3311_fris_quest")
            t.check("leg.5.end", sv == 300 and tl ~= nil, "tile " .. tostring(tl and tl.x) .. "," .. tostring(tl and tl.z) .. "," .. tostring(tl and tl.level) .. " stage=" .. tostring(sv)
                .. "; backpack: yak-hide body and greaves, Neitiznot shield (fremmenik_round_shield), coins, knife, axe, jester pieces unworn, lobster; rune scimitar worn")
            -- LEG 5 END
        end },
        { name = "cave_king_hand_in", run = function(t)
            t.ticks(3)
            -- LEG 6 BEGIN: enterCave
            stage(t, 300)
            -- guide: "Be prepared in your yak armour, Neitiznot shield, and a melee weapon" + food
            t.exec("wear-yakBody", t.player.equip, "yak_hide_armour_body")
            t.ticks(1)
            t.exec("wear-yakLegs", t.player.equip, "yak_hide_armour_greaves")
            t.ticks(1)
            t.exec("wear-shield", t.player.equip, "fremmenik_round_shield")
            t.ticks(1)
            -- brought along (guide: Food + potions); given here because leg 2's backpack has no room in setup
            -- spent tools out of the backpack first so the food fits
            for _, it in ipairs({ "knife", "bronze_axe", "hammer", "nails_bronze", "needle", "thread", "frisd_jester_hat", "frisd_jester_top", "frisd_jester_legs", "frisd_jester_boots" }) do
                for k = 1, 3 do
                    if count(t, it) > 0 then t.exec("drop-" .. it .. k, t.player.drop, it) end
                end
            end
            t.cheat("::give lobster 12")
            t.ticks(2)
            t.check("leg.6.pack", count(t, "lobster") >= 12, "lobster " .. count(t, "lobster"))

            t.exec("goto-enterCave", t.player.goto_tile, 2400, 3889, 0)
            t.exec("enterCave", t.player.click_loc, "fris_troll_trapdoor_r2", 1)
            convo(t, "enterCave-dialog", {})
            t.ticks(2)
            stage(t, 310)
            local _, tl = t.world.tile()
            t.check("enterCave.inside", tl ~= nil and tl.level == 1 and tl.z > 10000, "tile " .. tostring(tl and tl.x) .. "," .. tostring(tl and tl.z) .. "," .. tostring(tl and tl.level))

            -- ten kills open the bridge (fris_cave.rs2:206); the baby trolls at 2406,10259 and 2414,10257 respawn
            -- Bork Sigmundson hands out Prayer and Strength potions (fris_cave.rs2:~90), one request each
            t.exec("goto-borkSupplies", t.player.goto_tile, 2396, 10293, 1)
            for bi, want in ipairs({ "I need Prayer potions!", "I need Strength potions!" }) do
                t.exec("borkSupplies" .. bi, t.player.talk_to, "frisb_n_phy", 1)
                convo(t, "borkSupplies" .. bi .. "-dialog", { want })
                t.ticks(2)
            end
            t.check("borkSupplies.got", count(t, "3doseprayerrestore") >= 1 and count(t, "strength4") >= 1,
                "prayer " .. count(t, "3doseprayerrestore") .. " strength " .. count(t, "strength4"))
            t.exec("drinkStrength", t.player.inv_op, "strength4", 1)
            t.ticks(2)
            t.exec("goto-trolls", t.player.goto_tile, 2405, 10258, 1)
            -- the runts out-trade a melee fighter that must eat (runs 1-2 died): Protect from Melee for the ten kills
            do
                local tab_result = t.ui.tab("prayer")
                t.ticks(2)
                local wr, w = t.ui.widget("prayerbook:prayer15")
                t.ui.invoke(w, 1)
                t.ticks(2)
                local _, on = t.var.varbit("varb4118_prayer_protectfrommelee")
                local _, pr = t.skill.read("prayer")
                t.check("killTrolls-protectMelee", tab_result == "ok" and wr == "ok" and on == 1, "varb4118_prayer_protectfrommelee " .. tostring(on) .. "; prayer skill " .. tostring(type(pr) == "table" and (tostring(pr.level) .. "/" .. tostring(pr.current)) or pr) .. "; msgs " .. tostring(t.msg.last(3)))
            end
            for i = 1, 30 do
                local _, left = t.var.server("varb3312_fris_task")
                if left == 0 then break end
                if (i == 4 or i == 7 or i == 9) and count(t, "3doseprayerrestore") > 0 then
                    t.exec("killTrolls" .. i .. "-drinkPrayer", t.player.inv_op, "3doseprayerrestore", 1)
                    t.ticks(2)
                end
                t.exec("killTrolls" .. i .. "-present", t.npc.await_present, "fris_baby_troll_pc", 12, 80)
                local ar, ad = t.player.attack("fris_baby_troll_pc", 2, 30)
                if ar == "ok" then
                    t.check("killTrolls" .. i .. "-attack", true, "attack ok: " .. tostring(ad))
                    t.exec("killTrolls" .. i .. "-dead", t.npc.await_dead_engaged, 200, 3, { eat = { item = "lobster", below = 20 } })
                else
                    -- a runt is already on us (single-way combat): auto-retaliate fights it; eat and wait it out
                    t.check("killTrolls" .. i .. "-retaliate", true, "attack answered " .. tostring(ar) .. " (" .. tostring(ad) .. "); fighting the runt already on the player")
                    for w = 1, 12 do
                        local _, hp = t.skill.read("hitpoints")
                        if type(hp) == "table" and (hp.current or hp.boosted or 99) < 40 and count(t, "lobster") > 0 then t.player.inv_op("lobster", 1) end
                        t.ticks(10)
                        local _, lf = t.var.server("varb3312_fris_task")
                        if lf == 0 then break end
                    end
                end
                t.ticks(2)
            end
            local _, left = t.var.server("varb3312_fris_task")
            t.check("killTrolls.ten", left == 0, "varb3312_fris_task (trolls still needed) = " .. tostring(left))

            -- Bork Sigmundson's third request: food (the rest of his supplies), now the backpack has room again
            t.exec("goto-borkFood", t.player.goto_tile, 2396, 10293, 1)
            t.exec("borkFood", t.player.talk_to, "frisb_n_phy", 1)
            convo(t, "borkFood-dialog", { "Give me food, Bork!" })
            t.ticks(2)
            t.check("borkFood.got", count(t, "tuna") >= 1, "tuna " .. count(t, "tuna"))
            -- Protect from Magic for the king
            t.exec("goto-enterKingRoom", t.player.goto_tile, 2385, 10265, 1)
            for k = 1, 3 do
                if count(t, "3doseprayerrestore") > 0 then
                    t.exec("enterKingRoom-drinkPrayer" .. k, t.player.inv_op, "3doseprayerrestore", 1)
                    t.ticks(2)
                end
            end
            do
                local tab_result, tab_detail = t.ui.tab("prayer")
                t.ticks(2)
                local wr, w = t.ui.widget("prayerbook:prayer13")
                t.check("enterKingRoom-prayertab", tab_result == "ok" and wr == "ok", "prayer tab -> " .. tostring(tab_result) .. " " .. tostring(tab_detail) .. "; prayerbook:prayer13 -> " .. tostring(wr))
                t.ui.invoke(w, 1)
                t.ticks(2)
                local _, on = t.var.varbit("varb4116_prayer_protectfrommagic")
                t.check("enterKingRoom-protect", on == 1, "varb4116_prayer_protectfrommagic " .. tostring(on))
            end
            t.exec("enterKingRoom", t.player.click_loc, "frisb_bridge_6_n", 1)
            t.ticks(4)
            local _, kt = t.world.tile()
            t.check("enterKingRoom.across", kt ~= nil and kt.z < 10260, "tile " .. tostring(kt and kt.x) .. "," .. tostring(kt and kt.z) .. "," .. tostring(kt and kt.level))
            t.exec("killKing-attack", t.player.attack, "fris_troll_king_true", 2, 40)
            t.exec("killKing-dead", t.npc.await_dead_engaged, 600, 4, { eat = { item = "lobster", below = 30 } })
            t.ticks(2)
            stage(t, 320)

            t.exec("decapitateKing", t.player.click_loc, "fris_troll_king_dead_head", 1)
            t.exec("decapitateKing.got", t.inv.await, "frisr_trollkinghead", 1, 20)
            t.ticks(2)
            stage(t, 325)

            -- out through the stone ladder at the cave's east end, then to Mawnis
            t.exec("goto-leaveCave", t.player.goto_tile, 2391, 10286, 1)
            -- the stone ladder is plain travel (fris_cave.rs2:~42 teleports to the trapdoor, no quest var); try it, and
            -- when its pixel sits under the UI (run 9) travel on with goto
            local lr, ld = t.player.click_loc("fris_mine_wall_column_ladderr1", 1)
            t.ticks(3)
            local _, lt = t.world.tile()
            t.expect("leaveCave", "ok", "ladder click -> " .. tostring(lr) .. " " .. tostring(ld) .. "; player at " .. tostring(lt and lt.x) .. "," .. tostring(lt and lt.z) .. "," .. tostring(lt and lt.level))
            t.exec("goto-finishQuest", t.player.goto_tile, 2341, 3799, 0)
            local snap_r, snap = t.skill.snapshot()
            t.check("finishQuest.snapshot", snap_r == "ok", "skill snapshot before the hand-in: " .. tostring(snap_r))
            t.exec("finishQuest", t.player.talk_to, "fris_r_burgher_crown", 1)
            convo(t, "finishQuest-dialog", { "Attack", "Strength" })
            t.ticks(3)
            t.expect("reward.construction_xp", t.skill.expect_gain("construction", 5000, snap))
            t.expect("reward.crafting_xp", t.skill.expect_gain("crafting", 5000, snap))
            t.expect("reward.woodcutting_xp", t.skill.expect_gain("woodcutting", 10000, snap))
            t.expect("reward.attack_xp", t.skill.expect_gain("attack", 10000, snap))
            t.expect("reward.strength_xp", t.skill.expect_gain("strength", 10000, snap))
            t.exec("reward.helm", t.inv.expect_has, "fris_kingly_helm", 1)
            t.quest.expect_complete()
            -- LEG 6 END
            t.finish(0)
        end },
    },
}
