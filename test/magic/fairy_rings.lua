-- The fairy ring network (general/scripts/misc/fairy_rings.rs2): every code
-- dialled from Zanaris's ring with the dial's own buttons, each landing
-- checked against the cache's `fairyring` table (the tile 34 recorded codes
-- landed on, RSProx 92 sessions), and the recorded refusals and shapes:
-- no permission (4404), no staff (4653 [82]), a dead code (DJQ, 4654
-- [1706..1712]: back on the tile the step left, then the player's line), the
-- ring's own code (4654 [1638..1643]), the Zanaris op (4653 [88..92]),
-- Last-destination, and a quest-locked code (BJS) before and after Regicide.
--
--     python3 tools/quest_gate/run.py --no-build --script test/magic/fairy_rings.lua --name fairy_rings
local CODES = {
    { code = 0, name = "AIP", dest = nil },
    { code = 1, name = "AIS", dest = {1429, 3324, 0} },
    { code = 2, name = "AIR", dest = {2700, 3247, 0} },
    { code = 3, name = "AIQ", dest = {2996, 3114, 0} },
    { code = 4, name = "ALP", dest = {2503, 3636, 0} },
    { code = 5, name = "ALS", dest = {2644, 3495, 0} },
    { code = 6, name = "ALR", dest = {3059, 4875, 0} },
    { code = 7, name = "ALQ", dest = {3597, 3495, 0} },
    { code = 8, name = "AKP", dest = {3284, 2706, 0} },
    { code = 9, name = "AKS", dest = {2571, 2956, 0} },
    { code = 10, name = "AKR", dest = {1826, 3540, 0} },
    { code = 11, name = "AKQ", dest = {2319, 3619, 0} },
    { code = 12, name = "AJP", dest = {1651, 3010, 0} },
    { code = 13, name = "AJS", dest = {2500, 3896, 0} },
    { code = 14, name = "AJR", dest = {2780, 3613, 0} },
    { code = 15, name = "AJQ", dest = {2735, 5221, 0} },
    { code = 16, name = "DIP", dest = {3037, 4763, 0} },
    { code = 17, name = "DIS", dest = {3108, 3149, 0} },
    { code = 18, name = "DIR", dest = {3038, 5348, 0} },
    { code = 19, name = "DIQ", dest = nil },
    { code = 20, name = "DLP", dest = {2926, 10455, 0} },
    { code = 21, name = "DLS", dest = {3447, 9824, 0} },
    { code = 22, name = "DLR", dest = {2213, 3099, 0} },
    { code = 23, name = "DLQ", dest = {3423, 3016, 0} },
    { code = 24, name = "DKP", dest = {2900, 3111, 0} },
    { code = 25, name = "DKS", dest = {2744, 3719, 0} },
    { code = 26, name = "DKR", dest = {3129, 3496, 0} },
    { code = 27, name = "DKQ", dest = nil },
    { code = 28, name = "DJP", dest = {2658, 3230, 0} },
    { code = 29, name = "DJS", dest = nil },
    { code = 30, name = "DJR", dest = {1455, 3658, 0} },
    { code = 31, name = "DJQ", dest = nil },
    { code = 32, name = "CIP", dest = {2513, 3884, 0} },
    { code = 33, name = "CIS", dest = {1639, 3868, 0} },
    { code = 34, name = "CIR", dest = {1302, 3762, 0} },
    { code = 35, name = "CIQ", dest = {2528, 3127, 0} },
    { code = 36, name = "CLP", dest = {3082, 3206, 0} },
    { code = 37, name = "CLS", dest = {2682, 3081, 0} },
    { code = 38, name = "CLR", dest = {2740, 2738, 0} },
    { code = 39, name = "CLQ", dest = nil },
    { code = 40, name = "CKP", dest = {2075, 4848, 0} },
    { code = 41, name = "CKS", dest = {3447, 3470, 0} },
    { code = 42, name = "CKR", dest = {2801, 3003, 0} },
    { code = 43, name = "CKQ", dest = {1359, 2941, 0} },
    { code = 44, name = "CJP", dest = nil },
    { code = 45, name = "CJS", dest = nil },
    { code = 46, name = "CJR", dest = {2705, 3576, 0} },
    { code = 47, name = "CJQ", dest = {3178, 2447, 0} },
    { code = 48, name = "BIP", dest = {3410, 3324, 0} },
    { code = 49, name = "BIS", dest = {2635, 3266, 0} },
    { code = 50, name = "BIR", dest = nil },
    { code = 51, name = "BIQ", dest = {3251, 3095, 0} },
    { code = 52, name = "BLP", dest = {2437, 5126, 0} },
    { code = 53, name = "BLS", dest = {1295, 3493, 0} },
    { code = 54, name = "BLR", dest = {2740, 3351, 0} },
    { code = 55, name = "BLQ", dest = {3572, 4372, 0} },
    { code = 56, name = "BKP", dest = {2385, 3035, 0} },
    { code = 57, name = "BKS", dest = {2412, 4434, 0} },
    { code = 58, name = "BKR", dest = {3469, 3431, 0} },
    { code = 59, name = "BKQ", dest = {3041, 4532, 0} },
    { code = 60, name = "BJP", dest = {2267, 2976, 0} },
    { code = 61, name = "BJS", dest = {2150, 3070, 0} },
    { code = 62, name = "BJR", dest = {2650, 4730, 0} },
    { code = 63, name = "BJQ", dest = nil },
}

local ZANARIS_RING = { 2412, 4434 }
local STAND = "0_37_69_44_19" -- 2412,4435, beside Zanaris's ring
local HARDLY = "Wow, fairy magic sure is useful, I hardly moved at all!"

return {
    id = "fairy_rings",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = {
        "::clearinv",
        "::give dramen_staff 1",
        "::give lobster 5",
        "::setlevel hitpoints 99",
    },

    run = function(t)
        t.ticklog.start()
        local function tile()
            local r, tt = t.world.tile()
            if r ~= "ok" or type(tt) ~= "table" then return nil end
            return tt
        end
        local function tile_text()
            local tt = tile()
            if not tt then return "?" end
            return tt.x .. "," .. tt.z .. "," .. tostring(tt.level)
        end
        local function at(x, z, level)
            local tt = tile()
            return tt ~= nil and tt.x == x and tt.z == z and (level == nil or tt.level == level)
        end
        local function setvar(name, value)
            t.cheat("::setvar " .. name .. " " .. tostring(value))
            t.ticks(1)
        end
        local function stand()
            t.cheat("::tele " .. STAND)
            t.ticks(3)
        end
        local function press(sym)
            local r, w = t.ui.widget(sym)
            if r ~= "ok" then return false, "no widget " .. sym end
            local ir, id = t.ui.invoke(w, 1)
            t.ticks(1)
            return ir == "ok", tostring(id)
        end
        -- Configure on Zanaris's ring, turn each wheel clockwise to the code's
        -- letter (the wheels open on A I P), Confirm.
        local function dial(code)
            local r, d = t.player.click_loc("fairyring_homehub", 2, { at = ZANARIS_RING })
            local opened = t.ui.await_open("fairyrings", 6)
            if opened ~= "ok" then return false, "configure: " .. tostring(r) .. " " .. tostring(d) end
            local wheels = { math.floor(code / 16), math.floor(code / 4) % 4, code % 4 }
            for w = 1, 3 do
                for _ = 1, wheels[w] do
                    local ok, why = press("fairyrings:" .. w .. "_clockwise")
                    if not ok then return false, why end
                end
            end
            local _, v1 = t.var.server("varb3985_fairyring_1")
            local _, v2 = t.var.server("varb3986_fairyring_2")
            local _, v3 = t.var.server("varb3987_fairyring_3")
            if v1 ~= wheels[1] or v2 ~= wheels[2] or v3 ~= wheels[3] then
                return false, "wheels " .. tostring(v1) .. tostring(v2) .. tostring(v3)
            end
            local ok, why = press("fairyrings:confirm")
            if not ok then return false, why end
            t.ticks(7)
            return true, "dialled"
        end
        local function finish_chat()
            for _ = 1, 4 do
                if t.chat.kind() == "none" then return end
                t.chat.continue_()
                t.ticks(1)
            end
        end

        -- No permission yet: the recorded refusal, nothing opens.
        t.cheat("::wield dramen_staff")
        t.ticks(1)
        stand()
        t.player.click_loc("fairyring_homehub", 2, { at = ZANARIS_RING })
        t.check("refuse.permission", t.ui.await_open("fairyrings", 3) ~= "ok", "the dial stayed shut without the Godfather's permission")

        -- Fairytale II's permission (the cheat writes %varb2329 = 2).
        t.cheat("::complete quest_fairytale2")
        t.ticks(1)

        -- No staff: the recorded line, nothing opens.
        t.cheat("::clearinv")
        t.ticks(1)
        t.player.unequip("dramen_staff")
        t.ticks(2)
        stand()
        t.player.click_loc("fairyring_homehub", 2, { at = ZANARIS_RING })
        t.check("refuse.staff", t.ui.await_open("fairyrings", 3) ~= "ok", "the dial stayed shut with no staff wielded")
        t.cheat("::give dramen_staff 1")
        t.ticks(1)
        t.cheat("::wield dramen_staff")
        t.ticks(1)

        -- A quest-locked code before its quest: answered like a dead code.
        stand()
        local ok, why = dial(61)
        finish_chat()
        t.check("bjs.locked", ok and at(2412, 4435, 0), "BJS without Regicide: " .. tostring(why) .. ", at " .. tile_text())

        -- Every ring a quest or a visit opens, opened.
        t.cheat("::complete quest_childrenofthesun")
        setvar("varb9632_vmq1", 24)
        setvar("varb9650_varlamore_visited", 1)
        setvar("varb13841_bcs", 14)
        setvar("varb2258_dttd_main", 13)
        setvar("varb19564_grimstone_visited", 1)
        setvar("varp387_routequest", 105)
        setvar("varb4897_zeah_playerhasvisited", 1)
        setvar("varp347_viking", 10)
        setvar("varp365_mm_main", 9)
        setvar("varb18321_tt", 7)
        setvar("varb13599_lotg", 52)
        setvar("varp328_regicide_quest", 15)
        setvar("varp5_grail", 9)

        for _, c in ipairs(CODES) do
            stand()
            t.ticklog.mark("code." .. c.name)
            local dok, dwhy = dial(c.code)
            local _, last = t.var.server("varb5374_fairyring_lastloc")
            if c.dest == nil or c.code == 19 then
                -- No ring (or the house's DIQ): back on the stepped-from tile.
                local kind = t.chat.kind()
                local _, raw = t.chat.text()
                finish_chat()
                t.check("code." .. c.name, dok and at(2412, 4435, 0) and last == c.code,
                    tostring(dwhy) .. "; at " .. tile_text() .. " lastloc " .. tostring(last) .. " page " .. tostring(kind) .. " '" .. tostring(raw) .. "'")
            elseif c.code == 57 then
                -- BKS from Zanaris's own ring: the frame with no move.
                t.check("code.BKS.same_ring", dok and at(2412, 4434, 0) and t.chat.kind() == "none", tostring(dwhy) .. "; at " .. tile_text())
            else
                t.check("code." .. c.name, dok and at(c.dest[1], c.dest[2], c.dest[3]) and last == c.code,
                    tostring(dwhy) .. "; want " .. c.dest[1] .. "," .. c.dest[2] .. " at " .. tile_text() .. " lastloc " .. tostring(last))
            end
            finish_chat()
        end

        -- The left-click Zanaris from a world ring (AIR's) and Last-destination.
        t.cheat("::tele 0_42_50_12_48")
        t.ticks(3)
        t.player.click_loc("fairyring_minorhub", 1, { at = { 2700, 3247 } })
        t.ticks(8)
        t.check("op.zanaris", at(2412, 4434, 0), "Zanaris op from AIR: at " .. tile_text())
        stand()
        dial(2)
        t.check("dial.air", at(2700, 3247, 0), "AIR: at " .. tile_text())
        stand()
        t.player.click_loc("fairyring_homehub", 3, { at = ZANARIS_RING })
        t.ticks(8)
        t.check("op.last_destination", at(2700, 3247, 0), "Last-destination (AIR): at " .. tile_text())
    end,
}
