-- The Fremennik Isles -- relay file. Legs follow docs/quests/ladders/thefremennikisles.legs
-- (route order). Notes: docs/quests/ladders/thefremennikisles.notes.md.
-- Setup stages the prerequisite only (::complete quest_fremenniktrials); every other quest var is
-- already the value fris_debug_reset would write (0: thefremennikisles.constant:46/81/83). The player
-- walks to Rellekka from the Lumbridge fixture through the members' gate.
--
-- Door rule (b72): every door, gate, rope bridge and trapdoor between the player and a target is
-- pressed on every visit, in and out -- Jatizso's city gate (frisd_outer_city_wall_door_left, the south
-- edge of 2413,3797) and King Gjuki's hall door (frisd_town_wall_door, the north edge of 2407,3806);
-- Neitiznot's palisade door (fris_palisade_door_l, the east edge of 2328,3805), Mawnis's hall door
-- (frisb_abode_door, the south edge of 2339,3801) and the yak field gate (frisd_wood_gate_01, the south
-- edge of 2326,3802). The isles' rope bridges (fris_bridges.rs2) are walked by their own op; the bridges
-- to the islet (1 at x 2317, 2 at x 2343) start OUTSIDE the palisade.

local JESTER = { "frisd_jester_hat", "frisd_jester_top", "frisd_jester_legs", "frisd_jester_boots" }
-- Mawnis's shouted instruction (fris_jester.rs2 ~fris_jester_instruction) -> the frisd_jestertask button
local INSTRUCTION = {
    ["Talk, fool"] = "frisd_talkbutton", ["Dance, fool"] = "frisd_dancebutton", ["Juggle, fool"] = "frisd_jugglebutton",
    ["Skip, fool"] = "frisd_skipbutton", ["Pie in your face"] = "frisd_piebutton", ["Jig, you fool"] = "frisd_jigbutton",
    ["Bow, fool"] = "frisd_bowbutton",
}
-- The arctic pines inside the yak field (the ones outside it stand on solid or unreachable ground)
local FIELD_PINES = { { 2315, 3788 }, { 2319, 3787 }, { 2322, 3786 }, { 2325, 3788 }, { 2328, 3790 }, { 2315, 3795 }, { 2313, 3801 } }

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

local function walk(t, name, x, z, ticks)
    t.exec(name, t.player.walk_to, x, z, ticks or 60)
end

-- ---- the doors, pressed on every crossing (pass_door re-reads a door that stands open) ----
local function city_gate_in(t, name)
    t.exec(name, t.player.pass_door, { closed = "frisd_outer_city_wall_door_left", open = "frisd_outer_city_wall_door_left_open",
        at = { 2413, 3797, 0 }, near = { 2413, 3796 }, far = { 2413, 3799 } })
end
local function city_gate_out(t, name)
    t.exec(name, t.player.pass_door, { closed = "frisd_outer_city_wall_door_left", open = "frisd_outer_city_wall_door_left_open",
        at = { 2413, 3797, 0 }, near = { 2413, 3797 }, far = { 2413, 3795 } })
end
local function hall_in(t, name)
    t.exec(name, t.player.pass_door, { closed = "frisd_town_wall_door", open = "frisd_town_wall_door_open",
        at = { 2407, 3806, 0 }, near = { 2407, 3807 }, far = { 2407, 3805 } })
end
local function hall_out(t, name)
    t.exec(name, t.player.pass_door, { closed = "frisd_town_wall_door", open = "frisd_town_wall_door_open",
        at = { 2407, 3806, 0 }, near = { 2407, 3806 }, far = { 2407, 3808 } })
end
local function palisade_in(t, name)
    t.exec(name, t.player.pass_door, { closed = "fris_palisade_door_l", open = "fris_palisade_door_open_l",
        at = { 2328, 3805, 0 }, near = { 2327, 3805 }, far = { 2330, 3805 } })
end
local function palisade_out(t, name)
    t.exec(name, t.player.pass_door, { closed = "fris_palisade_door_l", open = "fris_palisade_door_open_l",
        at = { 2328, 3805, 0 }, near = { 2329, 3805 }, far = { 2327, 3805 } })
end
local function abode_in(t, name)
    t.exec(name, t.player.pass_door, { closed = "frisb_abode_door", open = "frisb_abode_door_open",
        at = { 2339, 3801, 0 }, near = { 2339, 3802 }, far = { 2339, 3799 } })
end
local function abode_out(t, name)
    t.exec(name, t.player.pass_door, { closed = "frisb_abode_door", open = "frisb_abode_door_open",
        at = { 2339, 3801, 0 }, near = { 2339, 3800 }, far = { 2339, 3802 } })
end
local function field_in(t, name)
    t.exec(name, t.player.pass_door, { closed = "frisd_wood_gate_01", open = "frisd_wood_gate_open_01",
        at = { 2326, 3802, 0 }, near = { 2326, 3803 }, far = { 2326, 3800 } })
end
local function field_out(t, name)
    t.exec(name, t.player.pass_door, { closed = "frisd_wood_gate_01", open = "frisd_wood_gate_open_01",
        at = { 2326, 3802, 0 }, near = { 2326, 3801 }, far = { 2326, 3803 } })
end

-- A rope bridge by its own op (fris_bridges.rs2: Walk-across teleports 8 tiles along z; Repair carries
-- the player north). Bridges 1-4 are ground decorations on raw level 1 of a bridge column (loc_level 1).
local function bridge(t, name, loc, lx, lz, sx, sz, dx, dz, op, op_name)
    t.exec(name, t.player.cross_trap, { loc = loc, at = { lx, lz, 0 }, loc_level = 1, src = { sx, sz }, dest = { dx, dz },
        op = op or 1, op_name = op_name or "Walk-across", attempts = 2 })
end

-- Auto Retaliate (combat_tab.rs2 [if_button1,combat_interface:retaliate], varp172_option_nodef 1 = off).
-- Bridge 4's ends stand among the native ice trolls (m36_59.spawn fris_baby_troll_pc 2356,3831 and the
-- rest; the guide: "you'll automatically cross the aggressive trolls"): a troll that bites at one end is
-- chased after the crossing, one step back onto the deck's end tile, so the bridge is crossed with
-- retaliation off and its landing graded on the exact tile.
local function set_retaliate(t, name, want_off)
    local _, before = t.var.server("varp172_option_nodef")
    if (before == 1) ~= want_off then
        t.ui.tab("combat")
        t.ticks(1)
        local wr, w = t.ui.widget("combat_interface:retaliate")
        if wr == "ok" then t.ui.invoke(w, 1) end
        t.ticks(2)
    end
    local _, after = t.var.server("varp172_option_nodef")
    t.check(name, (after == 1) == want_off, "varp172_option_nodef " .. tostring(before) .. " -> " .. tostring(after)
        .. " (want " .. (want_off and "1, Auto Retaliate off" or "0, Auto Retaliate on") .. ")")
    t.ui.tab("inventory")
    t.ticks(1)
end

-- Margin row for a fight (gaps-combat.md): the lowest hp any kill wait read against a quarter of max,
-- and food left.
local function fight_margin(t, name, details, food_before, food, what)
    local lowest = nil
    for _, d in ipairs(details) do
        for text in tostring(d):gmatch("lowest hp (%d+)/") do
            local v = tonumber(text)
            if lowest == nil or v < lowest then lowest = v end
        end
    end
    local _, hitpoints = t.skill.read("hitpoints")
    local max_hp = type(hitpoints) == "table" and hitpoints.base_level or nil
    -- `food` is one symbol or a list (any food left in the backpack counts)
    local foods = type(food) == "table" and food or { food }
    local food_result, food_left = "ok", 0
    for _, f in ipairs(foods) do
        local r, c = t.inv.count(f)
        if r ~= "ok" then food_result = r end
        food_left = food_left + (c or 0)
    end
    t.check(name, lowest ~= nil and max_hp ~= nil and food_result == "ok" and lowest * 4 >= max_hp and (food_left or 0) >= 1,
        what .. ": lowest hp " .. tostring(lowest) .. "/" .. tostring(max_hp) .. ", " .. table.concat(foods, "+") .. " "
        .. tostring(food_before) .. " -> " .. tostring(food_left) .. " (margin: lowest hp >= a quarter of max AND food left)")
end

-- Arctic pine logs from the field's pines: each tree is chopped until it falls (its press answers
-- no copy) or the backpack holds `want` logs.
local function chop_logs(t, pfx, want)
    local pressed = {}
    for round = 1, 3 do
        for i, tr in ipairs(FIELD_PINES) do
            if count(t, "arctic_pine_log") >= want then break end
            for press = 1, 8 do
                local have = count(t, "arctic_pine_log")
                if have >= want then break end
                local r, d = t.player.click_loc("arctic_pine", 1, { at = { tr[1], tr[2] } })
                pressed[#pressed + 1] = tr[1] .. "," .. tr[2] .. "=" .. tostring(r)
                if r ~= "ok" then break end
                t.inv.await("arctic_pine_log", have + 1, 60)
            end
        end
        if count(t, "arctic_pine_log") >= want then break end
    end
    local got = count(t, "arctic_pine_log")
    t.check(pfx .. ".logs", got >= want, "arctic pine logs " .. got .. " (want " .. want .. "); presses " .. table.concat(pressed, " "))
end

-- Yak kills in the field until `enough()`; one margin row for the field.
local function yak_fight(t, pfx, kills, enough, pick)
    local details, food0 = {}, count(t, "lobster")
    for i = 1, kills do
        if enough() then break end
        t.exec(pfx .. i .. "-attack", t.player.attack, "yak", 2, 20)
        local _, d = t.exec(pfx .. i .. "-dead", t.npc.await_dead_engaged, 200, 3, { eat = { item = "lobster", below = 40 } })
        details[#details + 1] = d
        t.ticks(1)
        pick(i)
    end
    fight_margin(t, pfx .. ".margin", details, food0, "lobster", "the yaks in the Neitiznot field")
end

-- Mawnis's overhead line (npc_say), if he is saying one right now.
local function burgher_says(t)
    for _, sym in ipairs({ "fris_r_burgher", "fris_r_burgher_crown" }) do
        local r, _, rows = t.npc.tiles(sym, 16)
        if r == "ok" and type(rows) == "table" then
            for _, row in ipairs(rows) do
                if row.overhead ~= nil and row.overhead ~= "" and (row.overhead_timer or 0) > 0 then
                    return row.overhead, row.overhead_timer
                end
            end
        end
    end
    return nil, nil
end

-- The act: Mawnis shouts one instruction at a time over his head (fris_jester.rs2:426-427); the
-- player clicks that button on the frisd_jestertask panel before the next one.
local function perform(t, name)
    local mounted = false
    for guard = 1, 60 do
        local wr = t.ui.widget("frisd_jestertask:frisd_dancebutton")
        if wr == "ok" then mounted = true; break end
        t.chat.drain({ stop_at = "options" })
        t.ticks(2)
    end
    t.check(name .. ".panel_open", mounted, "frisd_jestertask panel mounted: " .. tostring(mounted))
    local n, last_timer, ended, fool, heard = 0, -1, false, false, {}
    for tick = 1, 320 do
        local text, timer = burgher_says(t)
        if text ~= nil and text:find("Useless fool", 1, true) then fool = true; break end
        local btn = text and INSTRUCTION[text]
        if btn ~= nil and timer > last_timer then
            n = n + 1
            heard[#heard + 1] = text
            local wr, w = t.ui.widget("frisd_jestertask:" .. btn)
            local pr = t.ui.invoke(w, 1)
            t.check(name .. ".press" .. n .. "." .. btn, wr == "ok" and pr == "ok",
                "Mawnis said '" .. text .. "' (overhead timer " .. tostring(timer) .. "): invoked " .. btn .. " -> " .. tostring(pr))
        end
        last_timer = timer or -1
        if n > 0 and t.ui.widget("frisd_jestertask:frisd_dancebutton") ~= "ok" then ended = true; break end
        t.ticks(1)
    end
    t.check(name .. ".act_over", ended and not fool and n == 10,
        "instructions heard and pressed: " .. n .. " (" .. table.concat(heard, " / ") .. "); panel closed=" .. tostring(ended)
        .. (fool and "; Mawnis: Useless fool!" or ""))
end

-- Jatizso: dock -> city gate -> King Gjuki's hall (its door is on the north side).
local function jatizso_to_king(t, pfx)
    walk(t, "walk-" .. pfx .. "-cityGate", 2413, 3795, 60)
    city_gate_in(t, pfx .. ".cityGateIn")
    walk(t, "walk-" .. pfx .. "-hall", 2407, 3807, 40)
    hall_in(t, pfx .. ".hallDoorIn")
end
-- King's hall -> city gate -> the dock beside Mord.
local function king_to_dock(t, pfx)
    hall_out(t, pfx .. ".hallDoorOut")
    walk(t, "walk-" .. pfx .. "-cityGate", 2413, 3798, 40)
    city_gate_out(t, pfx .. ".cityGateOut")
    walk(t, "walk-" .. pfx .. "-dock", 2420, 3782, 60)
end
-- Neitiznot dock -> palisade door -> inside the village.
local function dock_to_village(t, pfx)
    walk(t, "walk-" .. pfx .. "-palisade", 2327, 3805, 60)
    palisade_in(t, pfx .. ".palisadeIn")
end
-- Mawnis's hall -> palisade -> the dock beside Maria.
local function hall_to_dock(t, pfx)
    abode_out(t, pfx .. ".abodeOut")
    palisade_out(t, pfx .. ".palisadeOut")
    walk(t, "walk-" .. pfx .. "-dock", 2311, 3782, 60)
end
-- The king wanders about his hall: step in and try again.
local function kingtalk(t, name)
    t.exec(name, function(...)
        local r, d = t.player.talk_to(...)
        for try = 1, 4 do
            if r == "ok" then break end
            t.player.walk_to(2407, 3803, 10)
            t.ticks(3)
            r, d = t.player.talk_to(...)
        end
        return r, d
    end, "fris_r_king", 1)
end

return {
    id = "thefremennikisles",
    fixture = "fresh_lumbridge.ini",
    max_frames = 150000,  -- six legs add up past the default 2000-tick budget (relay.md)
    setup = {
        "::clearinv",
        "::setlevel mining 2",   -- the king asks 7 coal at Mining 2-54 (fris_shared.rs2 fris_ore_type); the guide lists Coal as brought
        "::complete quest_fremenniktrials",  -- the prerequisite; Mord Gunnars on Rellekka's pier starts the quest
        "::give raw_tuna 1",     -- guide item requirement of talkToGjuki: Raw tuna
        "::give coal 7",         -- guide item requirement of bringOreToGjuki: Coal (Mining 2 variant)
        "::setlevel crafting 30",      -- leg 2: spinning yak hair into rope needs Crafting 30 (spinning.dbrow spin_yak_hair)
        "::setlevel woodcutting 56",   -- leg 2: splitting arctic pine logs needs Woodcutting 56 (fris_bridges.rs2 fris_stump_split)
        "::setlevel construction 20",  -- leg 2: repairing a bridge needs Construction 20 (fris_bridges.rs2 fris_bridge_repair)
        "::give knife 1",              -- leg 2: guide item "Knife" (repairBridge)
        "::give bronze_axe 1",         -- leg 2: guide item "Any axe" (split logs)
        "::give rune_scimitar 1",      -- leg 2: guide "Melee gear" (yaks drop the hair)
        "::give lobster 4",            -- leg 2: guide "Food" for the yak fights (leg 6's food is bought from Keepa in leg 4)
        "::give nails_bronze 1",       -- leg 5: guide item "Bronze nail" (no shop on the route sells nails); needle, thread and hammer are bought in Neitiznot
        "::setlevel attack 40",        -- leg 2: melee levels for the yak field (hp 10 died to a yak, run 1)
        "::setlevel strength 40",
        "::setlevel defence 30",
        "::setlevel hitpoints 40",
        "::setlevel crafting 46",      -- leg 5: yak-hide body armour needs Crafting 46 (fris_shared.rs2 ^fris_req_crafting_body)
        "::setlevel hitpoints 90",     -- leg 6: guide "Melee gear; Food + potions" for the troll caves and the 150-hp king
        "::setlevel defence 80",       -- leg 6: melee gear for the Ice Troll King
        "::setlevel attack 90",
        "::setlevel strength 90",
        "::give amulet_of_strength 1", -- leg 6: guide "Melee gear" for the Ice Troll King; neck, cape and ring are the slots
        "::give tzhaar_cape_obsidian 1", -- the jester outfit and the yak armour never take, so they are worn from leg 1 on
        "::give berzerker_ring 1",     -- (worn at once: the backpack is full when leg 2 chops the arctic pines)
        "::setlevel prayer 99",        -- leg 6: killKing "Use the Protect from Magic prayer" (level 37); Protect from Melee (43) for the runts
        -- No dialogue on this route branches on the combat level (grep of quest_thefremennikisles/scripts: no ~player_combat_level).
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
            -- the leg-6 melee gear goes on first (setup; slots nothing later on the route takes)
            for _, it in ipairs({ "amulet_of_strength", "tzhaar_cape_obsidian", "berzerker_ring" }) do
                t.exec("wear-" .. it, t.player.equip, it)
                t.ticks(1)
            end
            -- From the Lumbridge fixture every walk to Rellekka opens the members' gate membergater
            -- 2933,3320 (the only way on foot): overland to its south side, the gate by its verb, then
            -- overland from its north side to Mord's pier (olafsquest.lua's shape).
            t.exec("goto-memberGate", t.player.goto_tile, 2933, 3318, 0)
            t.exec("talkToMord.memberGate", t.player.cross_gate, { loc = "membergater", at = { 2933, 3320, 0 },
                near = { 2933, 3318 }, far_ok = function(tile) return tile.z >= 3320 and math.abs(tile.x - 2933) <= 2 end,
                far_desc = "north of the members' gate, z >= 3320", far = { 2933, 3322 } })
            t.exec("goto-talkToMord", t.player.goto_tile, 2645, 3709, 0)
            t.exec("talkToMord", t.player.talk_to, "fris_r_ferryman_rellekka", 1)
            convo(t, "talkToMord-dialog", { "Yes.", "Can you ferry me to Jatizso?" })
            t.expect("quest.stage.started", t.quest.expect_stage("started"))
            t.ticks(4)
            near(t, "travelToJatizso", 2420, 3782, 6)

            jatizso_to_king(t, "talkToGjuki")
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

            -- out of the hall and the gate to the dock, then ferry home and on to Neitiznot
            king_to_dock(t, "returnToRellekkaFromJatizso")
            t.exec("returnToRellekkaFromJatizso", t.player.talk_to, "fris_r_ferryman_izso", 1)
            convo(t, "returnToRellekkaFromJatizso-dialog", { "Can you ferry me to Rellekka?" })
            t.ticks(4)
            near(t, "returnToRellekka.landed", 2644, 3709, 6)
            t.exec("travelToNeitiznot", t.player.talk_to, "fris_r_ferry_rellikka", 1)
            convo(t, "travelToNeitiznot-dialog", { "Can you ferry me to Neitiznot?" })
            t.ticks(4)
            near(t, "travelToNeitiznot.landed", 2311, 3782, 6)

            -- Slug, in the outfit with empty hands
            dock_to_village(t, "talkToSlug")
            walk(t, "walk-talkToSlug", 2336, 3809, 20)
            for _, it in ipairs(JESTER) do
                t.exec("wear-" .. it, t.player.equip, it)
                t.ticks(1)
            end
            t.exec("talkToSlug", t.player.talk_to, "fris_spymaster", 1)
            convo(t, "talkToSlug-dialog", { "Free stuff please.", "I am ready." })
            t.expect("quest.stage.spy1_briefed", t.quest.expect_stage("spy1_briefed"))

            abode_in(t, "goSpyOnMawnis.abodeIn")
            t.exec("goSpyOnMawnis", t.player.talk_to, "fris_r_burgher_crown", 1)
            convo(t, "goSpyOnMawnis-dialog", {})
            perform(t, "performForMawnis")
            t.ticks(6)
            convo(t, "performForMawnis-end", {})
            t.expect("quest.stage.spy1_done", t.quest.expect_stage("spy1_done"))

            abode_out(t, "tellSlugReport1.abodeOut")
            walk(t, "walk-tellSlugReport1", 2336, 3809, 20)
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
            t.check("leg.1.state", sv == 90, "tile " .. tl.x .. "," .. tl.z .. "," .. tl.level .. " stage=" .. tostring(sv)
                .. "; backpack: jester hat/top/tights/boots (unworn), coins, knife, axe, nails, lobsters")
            -- LEG 1 END
        end },

        { name = "mawnis_bridges", run = function(t)
            t.ticks(3)
            -- LEG 2 BEGIN: talkToMawnis (the player is at Slug's, inside the palisade)
            t.exec("wield-sword", t.player.equip, "rune_scimitar")
            t.ticks(1)
            abode_in(t, "talkToMawnis.abodeIn")
            t.exec("talkToMawnis", t.player.talk_to, "fris_r_burgher_crown", 1)
            convo(t, "talkToMawnis-dialog", {})
            stage(t, 100)

            -- rope: kill yaks in the field for their hair, spin it at the wheel (2352,3794)
            abode_out(t, "yaks.abodeOut")
            palisade_out(t, "yaks.palisadeOut")
            field_in(t, "yaks.fieldIn")
            yak_fight(t, "yak", 14, function() return count(t, "yak_hair") >= 8 end,
                function(i) t.exec("yak" .. i .. "-hair", t.player.click_obj, "yak_hair", 3) end)
            t.check("yakhair.8", count(t, "yak_hair") >= 8, "yak hair in the backpack: " .. count(t, "yak_hair"))
            field_out(t, "spinWheel.fieldOut")
            palisade_in(t, "spinWheel.palisadeIn")
            walk(t, "walk-spinWheel", 2352, 3796, 40)
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
            abode_in(t, "talkToMawnisWithLogs.abodeIn")
            t.exec("talkToMawnisWithLogs", t.player.talk_to, "fris_r_burgher_crown", 1)
            convo(t, "talkToMawnisWithLogs-dialog", {})
            stage(t, 110)

            -- 8 arctic pine logs from the field's pines, split on the stump (2342,3807)
            abode_out(t, "chopLogs.abodeOut")
            palisade_out(t, "chopLogs.palisadeOut")
            field_in(t, "chopLogs.fieldIn")
            chop_logs(t, "chopLogs", 8)
            field_out(t, "splitLogs.fieldOut")
            palisade_in(t, "splitLogs.palisadeIn")
            walk(t, "walk-splitLogs", 2342, 3806, 40)
            local stump = t.player.by_symbol("loc", "iznot_shield_stump")
            t.exec("splitLogs", t.player.use_on, "arctic_pine_log", stump)
            convo(t, "splitLogs-dialog", { "Split logs" })
            t.exec("splitLogs.got", t.inv.await, "arctic_pine_split", 8, 80)
            t.ticks(2)

            abode_in(t, "talkToMawnisAfterItems.abodeIn")
            t.exec("talkToMawnisAfterItems", t.player.talk_to, "fris_r_burgher_crown", 1)
            convo(t, "talkToMawnisAfterItems-dialog", {})
            stage(t, 140)

            -- the bridges: out of the village, over bridge 1 to the islet, repair 3 (west) and 4 (centre),
            -- each carries the player north and is walked back; home over bridge 2
            abode_out(t, "repairBridge1.abodeOut")
            palisade_out(t, "repairBridge1.palisadeOut")
            walk(t, "walk-repairBridge1-bridge1", 2317, 3823, 60)
            bridge(t, "repairBridge1.bridge1", "frisb_bridge_1_s", 2317, 3824, 2317, 3823, 2317, 3832)
            walk(t, "walk-repairBridge1", 2314, 3839, 30)
            bridge(t, "repairBridge1", "frisb_bridge_3_s", 2314, 3840, 2314, 3839, 2314, 3848, 2, "Repair")
            bridge(t, "repairBridge1-back", "frisb_bridge_3_n", 2314, 3847, 2314, 3848, 2314, 3839)
            -- bridge 4's Repair carries the player to 2355,3848 (fris_bridges.rs2:74; shortest-path transports.tsv
            -- "2355 3839 0 -> 2355 3848 0 Walk-across Rope bridge 21312"), graded on that exact tile: Auto Retaliate
            -- off first, or the south bank's troll is chased one step back onto the deck (2355,3847)
            set_retaliate(t, "repairBridge2-retaliateOff", true)
            walk(t, "walk-repairBridge2", 2355, 3839, 80)
            local rope0, split0 = count(t, "rope"), count(t, "arctic_pine_split")
            bridge(t, "repairBridge2", "frisb_bridge_4_s", 2355, 3840, 2355, 3839, 2355, 3848, 2, "Repair")
            t.check("repairBridge2.repaired", count(t, "rope") == rope0 - 4 and count(t, "arctic_pine_split") == split0 - 4,
                "rope " .. rope0 .. " -> " .. count(t, "rope") .. ", split logs " .. split0 .. " -> " .. count(t, "arctic_pine_split"))
            near(t, "repairBridge2.crossed", 2355, 3848, 0)
            bridge(t, "repairBridge2-back", "frisb_bridge_4_n", 2355, 3847, 2355, 3848, 2355, 3839)
            walk(t, "walk-talkToMawnisAfterRepair-bridge2", 2343, 3829, 40)
            bridge(t, "talkToMawnisAfterRepair.bridge2", "frisb_bridge_2_n", 2343, 3828, 2343, 3829, 2343, 3820)
            set_retaliate(t, "talkToMawnisAfterRepair-retaliateOn", false)
            dock_to_village(t, "talkToMawnisAfterRepair")
            abode_in(t, "talkToMawnisAfterRepair.abodeIn")
            t.exec("talkToMawnisAfterRepair", t.player.talk_to, "fris_r_burgher_crown", 1)
            convo(t, "talkToMawnisAfterRepair-dialog", {})
            stage(t, 160)
            local _, tl = t.world.tile()
            local _, sv = t.var.server("varb3311_fris_quest")
            t.check("leg.2.state", sv == 160, "tile " .. tl.x .. "," .. tl.z .. "," .. tl.level .. " stage=" .. tostring(sv)
                .. "; backpack: knife, axe, nails, jester pieces, coins; rune scimitar worn")
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
                        if t.chat.count(amount) == "ok" then
                            t.note(name .. ".typed: typed " .. amount .. " into the Enter amount prompt")
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
            hall_to_dock(t, "leaveNeitiznotToReport")
            t.exec("leaveNeitiznotToReport", t.player.talk_to, "fris_r_ferry_iznot", 1)
            talk("leaveNeitiznotToReport-dialog", { "Can you ferry me to Rellekka?" })
            t.ticks(4)
            near(t, "leaveNeitiznotToReport.arrived", 2644, 3709, 8)

            -- Rellekka -> Jatizso with Mord
            t.exec("travelToJatizsoToReport", t.player.talk_to, "fris_r_ferryman_rellekka", 1)
            talk("travelToJatizsoToReport-dialog", { "Can you ferry me to Jatizso?" })
            t.ticks(4)
            near(t, "travelToJatizsoToReport.arrived", 2420, 3782, 6)

            jatizso_to_king(t, "talkToGjukiToReport")
            kingtalk(t, "talkToGjukiToReport")
            talk("talkToGjukiToReport-dialog", {})
            t.ticks(2)
            stage(t, 200)
            t.exec("taxbag.held", t.inv.await, "frisd_taxbag_empty", 1, 10)

            -- the window round: Keepa 5000, Vanligga 5000, Skuli 6000, Hring 8000 (all inside the city wall)
            hall_out(t, "collectFromKeepa.hallDoorOut")
            walk(t, "walk-collectFromKeepa", 2417, 3815, 40)
            t.exec("collectFromKeepa", t.player.talk_to, "frisd_cook", 1)
            taxtalk("collectFromKeepa", 5000, PAY)
            walk(t, "walk-collectFromVanligga", 2405, 3812, 40)
            t.exec("collectFromVanligga", t.player.talk_to, "frisd_izso_landlady", 1)
            taxtalk("collectFromVanligga", 5000, PAY)
            walk(t, "walk-collectFromSkuli", 2395, 3803, 40)
            t.exec("collectFromSkuli", t.player.talk_to, "frisd_weaponmerchant", 1)
            taxtalk("collectFromSkuli", 6000, PAY)
            walk(t, "walk-collectFromHring", 2397, 3797, 30)
            t.exec("collectFromHring", t.player.talk_to, "frisd_oremerchant", 1)
            taxtalk("collectFromHring", 8000, PAY)

            walk(t, "walk-talkToGjukiAfterCollection1", 2407, 3807, 40)
            hall_in(t, "talkToGjukiAfterCollection1.hallDoorIn")
            kingtalk(t, "talkToGjukiAfterCollection1")
            talk("talkToGjukiAfterCollection1-dialog", {})
            t.ticks(2)
            stage(t, 210)

            -- the beard round: Hring, Raum, Skuli, Keepa, Flosi, 1000 each
            hall_out(t, "collectFromHringAgain.hallDoorOut")
            walk(t, "walk-collectFromHringAgain", 2397, 3797, 40)
            t.exec("collectFromHringAgain", t.player.talk_to, "frisd_oremerchant", 1)
            talk("collectFromHringAgain-dialog", { PAY })
            t.exec("collectFromRaum", t.player.talk_to, "frisd_armourmerchant", 1)
            talk("collectFromRaum-dialog", { PAY })
            walk(t, "walk-collectFromSkuliAgain", 2395, 3803, 30)
            t.exec("collectFromSkuliAgain", t.player.talk_to, "frisd_weaponmerchant", 1)
            talk("collectFromSkuliAgain-dialog", { PAY })
            walk(t, "walk-collectFromKeepaAgain", 2417, 3815, 60)
            t.exec("collectFromKeepaAgain", t.player.talk_to, "frisd_cook", 1)
            talk("collectFromKeepaAgain-dialog", { PAY })
            walk(t, "walk-collectFromFlosi", 2418, 3814, 20)
            t.exec("collectFromFlosi", t.player.talk_to, "frisd_fishmerchant", 1)
            talk("collectFromFlosi-dialog", { PAY })

            walk(t, "walk-talkToGjukiAfterCollection2", 2407, 3807, 40)
            hall_in(t, "talkToGjukiAfterCollection2.hallDoorIn")
            kingtalk(t, "talkToGjukiAfterCollection2")
            talk("talkToGjukiAfterCollection2-dialog", {})
            t.ticks(2)
            stage(t, 230)
            local _, tl = t.world.tile()
            local _, sv = t.var.server("varb3311_fris_quest")
            t.check("leg.3.state", sv == 230 and tl ~= nil, "tile " .. tostring(tl and tl.x) .. "," .. tostring(tl and tl.z) .. "," .. tostring(tl and tl.level) .. " stage=" .. tostring(sv)
                .. "; backpack: coins, knife, axe, nails, jester pieces unworn, lobster; rune scimitar worn; tax bag handed back")
            -- LEG 3 END
        end },
        { name = "spy_again_decree", run = function(t)
            t.ticks(3)
            -- LEG 4 BEGIN: travelToNeitiznotToSpyAgain
            -- out of the king's hall to the Jatizso dock, then Mord and Maria
            king_to_dock(t, "returnToRellekkaFromJatizsoToSpyAgain")
            t.exec("returnToRellekkaFromJatizsoToSpyAgain", t.player.talk_to, "fris_r_ferryman_izso", 1)
            convo(t, "returnToRellekkaFromJatizsoToSpyAgain-dialog", { "Can you ferry me to Rellekka?" })
            t.ticks(4)
            near(t, "returnToRellekkaFromJatizsoToSpyAgain.landed", 2644, 3709, 6)
            t.exec("travelToNeitiznotToSpyAgain", t.player.talk_to, "fris_r_ferry_rellikka", 1)
            convo(t, "travelToNeitiznotToSpyAgain-dialog", { "Can you ferry me to Neitiznot?" })
            t.ticks(4)
            near(t, "travelToNeitiznotToSpyAgain.landed", 2311, 3782, 6)

            -- Slug, in the outfit with empty hands (the scimitar leg 2 wielded comes off)
            dock_to_village(t, "talkToSlugToSpyAgain")
            walk(t, "walk-talkToSlugToSpyAgain", 2336, 3809, 20)
            t.exec("unwield-sword", t.player.unequip, "rune_scimitar")
            t.ticks(2)
            for _, it in ipairs(JESTER) do
                t.exec("wear-" .. it, t.player.equip, it)
                t.ticks(1)
            end
            t.exec("talkToSlugToSpyAgain", t.player.talk_to, "fris_spymaster", 1)
            convo(t, "talkToSlugToSpyAgain-dialog", {})
            t.expect("quest.stage.spy2_briefed", t.var.expect("varb3311_fris_quest", 235))

            abode_in(t, "goSpyOnMawnisAgain.abodeIn")
            t.exec("goSpyOnMawnisAgain", t.player.talk_to, "fris_r_burgher_crown", 1)
            convo(t, "goSpyOnMawnisAgain-dialog", {})
            perform(t, "performForMawnisAgain")
            t.ticks(6)
            convo(t, "performForMawnisAgain-end", {})
            stage(t, 240)

            abode_out(t, "reportBackToSlugAgain.abodeOut")
            walk(t, "walk-reportBackToSlugAgain", 2336, 3809, 20)
            local c1 = count(t, "coins")
            t.exec("reportBackToSlugAgain", t.player.talk_to, "fris_spymaster", 1)
            convo(t, "reportBackToSlugAgain-dialog", { "Yes, I am.",
                "They are in a secluded bay, near Etceteria.",
                "They will be given some potions.",
                "I have been helping Neitiznot." })
            t.check("reportBackToSlugAgain.paid", count(t, "coins") > c1, "coins " .. c1 .. " -> " .. count(t, "coins"))
            stage(t, 260)

            -- Neitiznot -> Rellekka (Maria) -> Jatizso (Mord) -> the king
            palisade_out(t, "returnToRellekkaFromNeitiznotAfterSpy2.palisadeOut")
            walk(t, "walk-returnToRellekkaFromNeitiznotAfterSpy2-dock", 2311, 3782, 60)
            t.exec("returnToRellekkaFromNeitiznotAfterSpy2", t.player.talk_to, "fris_r_ferry_iznot", 1)
            convo(t, "returnToRellekkaFromNeitiznotAfterSpy2-dialog", { "Can you ferry me to Rellekka?" })
            t.ticks(4)
            near(t, "returnToRellekkaFromNeitiznotAfterSpy2.landed", 2644, 3709, 8)
            t.exec("travelToJatizsoAfterSpy2", t.player.talk_to, "fris_r_ferryman_rellekka", 1)
            convo(t, "travelToJatizsoAfterSpy2-dialog", { "Can you ferry me to Jatizso?" })
            t.ticks(4)
            near(t, "travelToJatizsoAfterSpy2.landed", 2420, 3782, 6)
            jatizso_to_king(t, "talkToGjukiAfterSpy2")
            kingtalk(t, "talkToGjukiAfterSpy2")
            convo(t, "talkToGjukiAfterSpy2-dialog", {})
            t.ticks(2)
            stage(t, 270)
            t.exec("decree.held", t.inv.await, "frisd_reciept", 1, 10)

            -- food for the troll caves (guide: "Food + potions"), bought from Keepa Kettilon on the way out
            hall_out(t, "buyFood.hallDoorOut")
            walk(t, "walk-buyFood", 2417, 3815, 40)
            local lob0 = count(t, "lobster")
            t.exec("buyFood.open", t.shop.open, "frisd_cook", 3, "frisd_cook")
            t.exec("buyFood.lobster", t.shop.buy, "lobster", 10)
            local buyFood_close = t.shop.close()
            t.check("buyFood.close", buyFood_close == "ok", "shop.close -> " .. tostring(buyFood_close))
            t.check("buyFood.got", count(t, "lobster") == lob0 + 10, "lobsters " .. lob0 .. " -> " .. count(t, "lobster"))

            -- the decree to Mawnis: Mord, Maria, the jester outfit off, Mawnis
            walk(t, "walk-returnToRellekkaFromJatizsoWithDecree-cityGate", 2413, 3798, 40)
            city_gate_out(t, "returnToRellekkaFromJatizsoWithDecree.cityGateOut")
            walk(t, "walk-returnToRellekkaFromJatizsoWithDecree-dock", 2420, 3782, 60)
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
            dock_to_village(t, "talkToMawnisWithDecree")
            abode_in(t, "talkToMawnisWithDecree.abodeIn")
            t.exec("talkToMawnisWithDecree", t.player.talk_to, "fris_r_burgher_crown", 1)
            convo(t, "talkToMawnisWithDecree-dialog", {})
            t.ticks(2)
            -- the decree dialogue sets stage 275 (fris_mawnis.rs2:347) and the same dialogue ends by
            -- setting 280 (fris_mawnis.rs2:361), so 275 never rests: the stage is read after it.
            stage(t, 280)

            local _, tl = t.world.tile()
            local _, sv = t.var.server("varb3311_fris_quest")
            t.check("leg.4.state", sv == 280 and tl ~= nil, "tile " .. tostring(tl and tl.x) .. "," .. tostring(tl and tl.z) .. "," .. tostring(tl and tl.level) .. " stage=" .. tostring(sv)
                .. "; backpack: coins, knife, axe, nails, jester pieces unworn, rune scimitar unworn, lobsters; decree handed in (stage 280 yak_armour: Thakkrad wants yak hides)")
            -- LEG 4 END
        end },
        { name = "armour_and_shield", run = function(t)
            t.ticks(3)
            -- LEG 5 BEGIN: getYakArmour (the player is in Mawnis's hall)
            t.exec("wield-sword-leg5", t.player.equip, "rune_scimitar")
            t.ticks(1)
            stage(t, 280)
            -- the jester outfit (spying is over) and the knife (both bridges are mended) are spent: out of the
            -- backpack so the hides, the crafting kit and the logs fit beside the cave food
            for _, it in ipairs({ "frisd_jester_hat", "frisd_jester_top", "frisd_jester_legs", "frisd_jester_boots", "knife" }) do
                if count(t, it) > 0 then t.exec("drop-" .. it, t.player.drop, it) end
            end

            -- yaks drop hide and hair: three hides (body takes 2, legs 1), one hair for the shield's rope
            abode_out(t, "yakHides.abodeOut")
            palisade_out(t, "yakHides.palisadeOut")
            field_in(t, "yakHides.fieldIn")
            yak_fight(t, "yakHide", 10, function() return count(t, "yak_hide") >= 3 and count(t, "yak_hair") >= 1 end,
                function(i)
                    t.exec("yakHide" .. i .. "-hide", t.player.click_obj, "yak_hide", 3)
                    t.ticks(1)
                    if count(t, "yak_hair") < 1 then t.exec("yakHide" .. i .. "-hair", t.player.click_obj, "yak_hair", 3) end
                end)
            t.check("yakHide.three", count(t, "yak_hide") >= 3 and count(t, "yak_hair") >= 1, "hides " .. count(t, "yak_hide") .. " hair " .. count(t, "yak_hair"))
            field_out(t, "cureHides.fieldOut")
            palisade_in(t, "cureHides.palisadeIn")

            -- Thakkrad (in Mawnis's hall) cures them (5 gp each)
            abode_in(t, "cureHides.abodeIn")
            t.exec("cureHides", t.player.talk_to, "fris_r_engineer", 1)
            convo(t, "cureHides-dialog", { "Cure my yak-hide, please.", "Cure all my hides." })
            t.exec("cureHides.got", t.inv.await, "yak_hide_cured", 3, 10)

            -- guide items Needle, Thread and Hammer: Neitiznot supplies (frisb_n_sto) sells all three
            abode_out(t, "buyCraftingKit.abodeOut")
            t.exec("buyCraftingKit.open", t.shop.open, "frisb_n_sto", 3, "frisb_n_shop")
            t.exec("buyCraftingKit.needle", t.shop.buy, "needle", 1)
            t.exec("buyCraftingKit.thread", t.shop.buy, "thread", 3)
            t.exec("buyCraftingKit.hammer", t.shop.buy, "hammer", 1)
            local buyCraftingKit_close = t.shop.close()
            t.check("buyCraftingKit.close", buyCraftingKit_close == "ok", "shop.close -> " .. tostring(buyCraftingKit_close))
            t.check("buyCraftingKit.got", count(t, "needle") == 1 and count(t, "thread") >= 3 and count(t, "hammer") == 1,
                "needle " .. count(t, "needle") .. " thread " .. count(t, "thread") .. " hammer " .. count(t, "hammer"))
            -- needle and thread: body (2 hides) then legs (1 hide)
            t.exec("craftYakBody", t.player.use_item_on_item, "needle", "yak_hide_cured")
            convo(t, "craftYakBody-dialog", { "Yak-hide body (2 hides)" })
            t.exec("craftYakBody.got", t.inv.await, "yak_hide_armour_body", 1, 20)
            t.ticks(2)
            t.exec("craftYakLegs", t.player.use_item_on_item, "needle", "yak_hide_cured")
            convo(t, "craftYakLegs-dialog", { "Yak-hide legs (1 hide)" })
            t.exec("craftYakLegs.got", t.inv.await, "yak_hide_armour_greaves", 1, 20)
            t.ticks(2)

            abode_in(t, "getYakArmour.abodeIn")
            t.exec("getYakArmour", t.player.talk_to, "fris_r_burgher_crown", 1)
            convo(t, "getYakArmour-dialog", {})
            t.ticks(2)
            stage(t, 290)

            -- the shield: one rope (spun from the hair), two arctic pine logs, hammer and nail at the stump
            abode_out(t, "spinShieldRope.abodeOut")
            walk(t, "walk-spinShieldRope", 2352, 3796, 40)
            local wheel = t.player.by_symbol("loc", "iznot_spinning_wheel")
            t.ticks(4)
            t.exec("spinShieldRope", t.player.use_on, "yak_hair", wheel)
            local mr = t.ui.await_open("skillmulti", 10)
            local cr, cell = t.ui.widget("skillmulti:a")
            t.check("spinShieldRope.menu", mr == "ok" and cr == "ok", "skillmulti menu=" .. tostring(mr) .. " cell=" .. tostring(cr))
            t.ui.invoke(cell, 1)
            t.exec("spinShieldRope.got", t.inv.await, "rope", 1, 20)
            palisade_out(t, "chopShieldLogs.palisadeOut")
            field_in(t, "chopShieldLogs.fieldIn")
            chop_logs(t, "chopShieldLogs", 2)
            field_out(t, "makeShield-stump.fieldOut")
            palisade_in(t, "makeShield-stump.palisadeIn")
            walk(t, "walk-makeShield-stump", 2342, 3806, 40)
            local stump = t.player.by_symbol("loc", "iznot_shield_stump")
            t.exec("makeShield-stump", t.player.use_on, "arctic_pine_log", stump)
            convo(t, "makeShield-stump-dialog", { "Neitiznot shield" })
            t.exec("makeShield-stump.got", t.inv.await, "fremmenik_round_shield", 1, 40)
            t.ticks(2)

            abode_in(t, "makeShield.abodeIn")
            t.exec("makeShield", t.player.talk_to, "fris_r_burgher_crown", 1)
            convo(t, "makeShield-dialog", {})
            t.ticks(2)
            stage(t, 300)

            local _, tl = t.world.tile()
            local _, sv = t.var.server("varb3311_fris_quest")
            t.check("leg.5.state", sv == 300 and tl ~= nil, "tile " .. tostring(tl and tl.x) .. "," .. tostring(tl and tl.z) .. "," .. tostring(tl and tl.level) .. " stage=" .. tostring(sv)
                .. "; backpack: yak-hide body and greaves, Neitiznot shield (fremmenik_round_shield), coins, axe, hammer, needle, thread, lobsters; rune scimitar worn")
            -- LEG 5 END
        end },
        { name = "cave_king_hand_in", run = function(t)
            t.ticks(3)
            -- LEG 6 BEGIN: enterCave (the player is in Mawnis's hall)
            stage(t, 300)
            -- guide: "Be prepared in your yak armour, Neitiznot shield, and a melee weapon" + food
            t.exec("wear-yakBody", t.player.equip, "yak_hide_armour_body")
            t.ticks(1)
            t.exec("wear-yakLegs", t.player.equip, "yak_hide_armour_greaves")
            t.ticks(1)
            t.exec("wear-shield", t.player.equip, "fremmenik_round_shield")
            t.ticks(1)
            -- spent tools out of the backpack so Bork's supplies fit
            for _, it in ipairs({ "bronze_axe", "hammer", "needle", "thread", "rope" }) do
                if count(t, it) > 0 then t.exec("drop-" .. it, t.player.drop, it) end
            end
            t.check("leg.6.pack", count(t, "lobster") >= 10, "lobster " .. count(t, "lobster"))

            -- to the northern isle: out of the village, bridge 1 to the islet, the mended bridge 3 north (clear of
            -- bridge 4's trolls), overland to the eastern cave trapdoor (2401,3889, entered from the west tile: notes)
            abode_out(t, "enterCave.abodeOut")
            palisade_out(t, "enterCave.palisadeOut")
            walk(t, "walk-enterCave-bridge1", 2317, 3823, 60)
            bridge(t, "enterCave.bridge1", "frisb_bridge_1_s", 2317, 3824, 2317, 3823, 2317, 3832)
            walk(t, "walk-enterCave-bridge3", 2314, 3839, 30)
            bridge(t, "enterCave.bridge3", "frisb_bridge_3_s", 2314, 3840, 2314, 3839, 2314, 3848)
            t.exec("goto-enterCave", t.player.goto_tile, 2400, 3889, 0)
            t.exec("enterCave", t.player.click_loc, "fris_troll_trapdoor_r2", 1)
            convo(t, "enterCave-dialog", {})
            t.ticks(2)
            stage(t, 310)
            local _, tl = t.world.tile()
            t.check("enterCave.inside", tl ~= nil and tl.level == 1 and tl.z > 10000, "tile " .. tostring(tl and tl.x) .. "," .. tostring(tl and tl.z) .. "," .. tostring(tl and tl.level))

            -- Bork Sigmundson hands out Prayer and Strength potions and tuna (fris_cave.rs2:~90), one request each;
            -- the tuna feeds the troll kills and the top-up before the king, the lobsters are kept for the king
            walk(t, "walk-borkSupplies", 2396, 10293, 30)
            for bi, want in ipairs({ "I need Prayer potions!", "I need Strength potions!", "Give me food, Bork!" }) do
                t.exec("borkSupplies" .. bi, t.player.talk_to, "frisb_n_phy", 1)
                convo(t, "borkSupplies" .. bi .. "-dialog", { want })
                t.ticks(2)
            end
            t.check("borkSupplies.got", count(t, "3doseprayerrestore") >= 1 and count(t, "strength4") >= 1 and count(t, "tuna") >= 1,
                "prayer " .. count(t, "3doseprayerrestore") .. " strength " .. count(t, "strength4") .. " tuna " .. count(t, "tuna"))

            -- killTrolls: the frenzied ice trolls of Bork's camp (m37_160.spawn fris_*_lowxp, the cave's western
            -- side; wiki "kill 10 frenzied ice trolls (of any kind)"; the guide's KillTrolls point 2390,10280,1)
            t.exec("drinkStrength", t.player.inv_op, "strength4", 1)
            t.ticks(2)
            -- the runts out-trade a melee fighter that must eat (runs 1-2 died): Protect from Melee for the ten kills
            do
                local tab_result = t.ui.tab("prayer")
                t.ticks(2)
                local wr, w = t.ui.widget("prayerbook:prayer15")
                t.ui.invoke(w, 1)
                t.ticks(2)
                local _, on = t.var.varbit("varb4118_prayer_protectfrommelee")
                t.check("killTrolls-protectMelee", tab_result == "ok" and wr == "ok" and on == 1, "varb4118_prayer_protectfrommelee " .. tostring(on))
            end
            local troll_details, troll_food = {}, count(t, "tuna")
            for i = 1, 30 do
                local _, left = t.var.server("varb3312_fris_task")
                if left == 0 then break end
                if (i == 4 or i == 7 or i == 9) and count(t, "3doseprayerrestore") > 0 then
                    t.exec("killTrolls" .. i .. "-drinkPrayer", t.player.inv_op, "3doseprayerrestore", 1)
                    t.ticks(2)
                end
                local target = nil
                for _, sym in ipairs({ "fris_baby_troll_lowxp", "fris_trollf_lowxp", "fris_trollm_lowxp", "fris_troll_bodyguard_lowxp" }) do
                    if t.npc.nearest(sym, 15) == "ok" then target = sym; break end
                end
                t.check("killTrolls" .. i .. "-present", target ~= nil, "nearest frenzied troll within 15: " .. tostring(target))
                if target == nil then break end
                t.exec("killTrolls" .. i .. "-attack", t.player.attack, target, 2, 30, { eat = { item = "tuna", below = 45 } })
                local _, d = t.exec("killTrolls" .. i .. "-dead", t.npc.await_dead_engaged, 200, 3, { eat = { item = "tuna", below = 45 } })
                troll_details[#troll_details + 1] = d
                t.ticks(2)
            end
            local _, left = t.var.server("varb3312_fris_task")
            t.check("killTrolls.ten", left == 0, "varb3312_fris_task (trolls still needed) = " .. tostring(left))
            fight_margin(t, "killTrolls.margin", troll_details, troll_food, "tuna", "the ten frenzied trolls")

            -- Protect from Magic for the king, then bridge 6 south
            walk(t, "walk-enterKingRoom", 2385, 10264, 40)
            -- Bork's tuna tops the player up at the bridge, before the king (the ten kills and the walk through the
            -- camp leave him near the eat line), and the Strength potion is drunk again
            for k = 1, 8 do
                local _, hp = t.skill.read("hitpoints")
                local now = type(hp) == "table" and hp.level or nil
                if now == nil or now >= 85 or count(t, "tuna") <= 1 then break end   -- one tuna stays in reserve
                t.exec("enterKingRoom-eatTuna" .. k, t.player.inv_op, "tuna", 1)
                t.ticks(3)
            end
            do
                local _, hp = t.skill.read("hitpoints")
                t.check("enterKingRoom-toppedUp", type(hp) == "table" and hp.level >= 65,
                    "hitpoints " .. tostring(type(hp) == "table" and hp.level) .. "/" .. tostring(type(hp) == "table" and hp.base_level)
                    .. ", tuna left " .. count(t, "tuna"))
            end
            for _, pot in ipairs({ "1dose1strength", "2dose1strength", "3dose1strength", "strength4" }) do
                if count(t, pot) > 0 then t.exec("enterKingRoom-drink-" .. pot, t.player.inv_op, pot, 1); t.ticks(2); break end
            end
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
                -- Ultimate Strength and Incredible Reflexes beside it (prayerbook:prayer11/12): the king has 150
                -- hitpoints, and every eat is an attack the scimitar does not make
                for _, pr in ipairs({ { "prayerbook:prayer11", "varb4114_prayer_ultimatestrength" },
                        { "prayerbook:prayer12", "varb4115_prayer_incrediblereflexes" } }) do
                    local br, bw = t.ui.widget(pr[1])
                    if br == "ok" then t.ui.invoke(bw, 1) end
                    t.ticks(2)
                    local _, bon = t.var.varbit(pr[2])
                    t.check("enterKingRoom-" .. pr[2], br == "ok" and bon == 1, pr[1] .. " -> " .. tostring(br) .. "; " .. pr[2] .. " " .. tostring(bon))
                end
            end
            -- Auto Retaliate off for the crossing: a camp troll that bites at the bridge is otherwise chased off the
            -- landing tile; back on for the king, whose knock-back special breaks the player's attack
            set_retaliate(t, "enterKingRoom-retaliateOff", true)
            t.exec("enterKingRoom", t.player.cross_trap, { loc = "frisb_bridge_6_n", at = { 2385, 10263, 1 }, src = { 2385, 10264 },
                dest = { 2385, 10259 }, op_name = "Walk-across", attempts = 1 })
            set_retaliate(t, "killKing-retaliateOn", false)
            local king_food = count(t, "lobster") + count(t, "tuna")
            local _, k1 = t.exec("killKing-attack", t.player.attack, "fris_troll_king_true", 2, 40, { eat = { item = "lobster", below = 50 } })
            local _, k2 = t.exec("killKing-dead", t.npc.await_dead_engaged, 600, 6, { eat = { item = "lobster", below = 50 } })
            fight_margin(t, "killKing.margin", { k1, k2 }, king_food, { "lobster", "tuna" }, "the Ice Troll King")
            t.ticks(2)
            stage(t, 320)

            t.exec("decapitateKing", t.player.click_loc, "fris_troll_king_dead_head", 1)
            t.exec("decapitateKing.got", t.inv.await, "frisr_trollkinghead", 1, 20)
            t.ticks(2)
            stage(t, 325)

            -- out: bridge 7 east from the king's pocket, the stone ladder at the cave's east end, then home over
            -- bridges 3 and 1 to Mawnis
            walk(t, "walk-leaveCave-bridge7", 2393, 10258, 30)
            t.exec("leaveCave.bridge7", t.player.cross_trap, { loc = "frisb_bridge_7_w", at = { 2394, 10258, 1 }, src = { 2393, 10258 },
                dest = { 2398, 10258 }, op_name = "Walk-across", attempts = 1 })
            walk(t, "walk-leaveCave", 2420, 10279, 60)
            -- the stone ladder comes up beside the north-west cave mouth, 2319,3892 (shortest-path transports.tsv,
            -- maplink.dbrow); home the way the leg came, bridges 3 and 1
            t.exec("leaveCave", t.player.climb, { loc = "fris_mine_wall_column_ladderr1", at = { 2421, 10279, 1 }, src = { 2420, 10279 },
                dest = { 2319, 3892, 0 }, slack = 0, op_name = "Climb-up" })
            t.exec("goto-finishQuest-bridge3", t.player.goto_tile, 2314, 3848, 0)
            bridge(t, "finishQuest.bridge3", "frisb_bridge_3_n", 2314, 3847, 2314, 3848, 2314, 3839)
            walk(t, "walk-finishQuest-bridge1", 2317, 3832, 30)
            bridge(t, "finishQuest.bridge1", "frisb_bridge_1_n", 2317, 3831, 2317, 3832, 2317, 3823)
            dock_to_village(t, "finishQuest")
            abode_in(t, "finishQuest.abodeIn")
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
