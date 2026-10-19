-- _skilling_tells: a Chambers of Xeric resource room's skilling sends what
-- Jagex's server sends for the same actions (docs/rsprox-analysis/cox/skilling.md,
-- tools/cox/rsprox_skilling.py). No recording farms, brews or cooks inside the
-- raid, so the rows are Jagex's ordinary patches, herbs and fires:
--   RK-S1 planting: farming_seed_dibbing (2291) with farming_dibbing (2432) on
--         the click, the seeded patch 3 ticks later;
--   RK-P1 picking: one herb every 3 ticks, each with pick (2581), and its
--         picking_low (2282) a tick before it;
--   RK-C1 cleaning: one of herblore_clean_herb_1..4 (3920-3923) a herb;
--   RK-M1 brewing: human_herbing_vial (363) with grind (2608), answering its
--         click as promptly as a fire's strike does (the bot answers a make
--         menu with 1, so the 2-tick batch cadence is not driven here);
--   RK-F1 a fire: human_createfire (733) with tinderbox_strike (2599), the
--         fire (26185) with fire_lit (2596) 4 ticks on (or 4k);
-- and then the raid's own rule: the cooked fish, eaten hurt, pays its cook
-- 4 + 8 x tier points (wiki Chambers of Xeric, "Cooking Food").
-- Played by the room solver (cox2_supplies.lua) in cox_resource's kit, room A.
local function kit()
    local lines = { "::clearinv" }
    for _, s in ipairs({ "attack", "strength", "defence", "hitpoints", "prayer", "magic", "ranged", "agility",
            "farming", "herblore", "fishing", "hunter", "cooking", "construction", "firemaking", "woodcutting" }) do
        lines[#lines + 1] = "::setlevel " .. s .. " 99"
    end
    for _, obj in ipairs({ "fris_kingly_helm", "barbassault_penance_fighter_torso", "black_dragonhide_chaps",
            "hundred_gauntlets_level_10", "abyssal_tentacle", "dragon_parryingdagger", "enchanted_onyx_amulet",
            "tzhaar_cape_fire", "dragon_boots", "nzone_berzerker_ring" }) do
        lines[#lines + 1] = "::give " .. obj .. " 1"
        lines[#lines + 1] = "::wield " .. obj
    end
    lines[#lines + 1] = "::~charge abyssal_tentacle 10000"
    lines[#lines + 1] = "::clearinv"
    for _, g in ipairs({ "raids_seed_buchuleaf 2", "raids_endarkened_juice 4", "raids_stinkhorn_mushroom 2",
            "raids_cicely 1", "fishing_rod 1", "raids_fishingbait 30", "hunting_butterfly_net 1" }) do
        lines[#lines + 1] = "::give " .. g
    end
    return lines
end

local function by_tick(rows)
    local out = {}
    for _, r in ipairs(rows) do
        out[r.tick] = out[r.tick] or {}
        table.insert(out[r.tick], r)
    end
    return out
end

local function has(list, pred)
    for _, x in ipairs(list or {}) do if pred(x) then return true end end
    return false
end

local RAW, COOKED = {}, {}
for tier = 0, 6 do
    RAW[#RAW + 1] = "raids_fish" .. tier .. "_raw"
    COOKED[#COOKED + 1] = "raids_fish" .. tier .. "_cooked"
end

local function held(t, names)
    for _, nm in ipairs(names) do
        local _, c = t.inv.count(nm)
        if (tonumber(c) or 0) > 0 then return nm end
    end
    return nil
end

local function points(t)
    local r, v = t.var.varp("varp4609_raids_playerscore")
    return r == "ok" and tonumber(v) or -1
end

return {
    id = "_skilling_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 400000,
    setup = kit(),
    run = function(t)
        t.ticklog.start()
        local n = t.raid.cox_run_n()
        local er, ed = t.raid.cox_test_enter_long("resource", 2 * (n // 2), 80)
        t.check("tells.enter", er == "ok", tostring(er) .. " " .. tostring(ed and ed.seed))
        if er ~= "ok" then t.finish(1) return end
        t.ticks(2)
        local result, detail = t.raid.cox_resource_solve({ make = { aid = 2, revit = 1, enhance = 1 }, food = 1,
            max_ticks = 1500 })
        t.check("tells.solve", result == "ok", tostring(detail))
        if result ~= "ok" then t.finish(1) return end

        -- A FIRE of kindling, the raw fish cooked on it, eaten hurt.
        for _, g in ipairs({ "::give raids_wood 5", "::give tinderbox 1", "::give raids_vial_overload_4 1" }) do
            t.cheat(g, false)
        end
        t.ticks(2)
        local _, fire_click = t.tick()
        t.player.use_item_on_item("tinderbox", "raids_wood")
        local fire
        for _ = 1, 40 do
            t.ticks(1)
            local me = t.SUP.me()
            if me then
                fire = t.SUP.first({ ox = me.x - 16, oz = me.z - 16, level = me.level }, "fire")
            end
            if fire then break end
        end
        t.check("tells.fire", fire ~= nil, "a kindling fire")
        if fire == nil then t.finish(1) return end
        local raw = held(t, RAW)
        t.check("tells.raw", raw ~= nil, "a raw raid fish")
        if raw == nil then t.finish(1) return end
        t.player.use_on(raw, t.player.by_symbol("loc", "fire"), { at = { fire.x, fire.z, fire.level } })
        local cooked
        for _ = 1, 20 do
            t.ticks(1)
            cooked = held(t, COOKED)
            local _, burnt = t.inv.count("raids_fish_burnt")
            if cooked or (tonumber(burnt) or 0) > 0 then break end
        end
        t.check("tells.cooked", cooked ~= nil, tostring(cooked))
        if cooked == nil then t.finish(1) return end
        t.player.inv_op("raids_vial_overload_4", 1)
        t.ticks(8)
        local before = points(t)
        t.player.inv_op(cooked, 1)
        t.ticks(3)
        local tier = tonumber(string.match(cooked, "raids_fish(%d)_cooked"))
        local paid = points(t) - before
        t.check("tells.cook_points", paid == 4 + 8 * tier,
            string.format("%s eaten hurt paid %d (want %d)", cooked, paid, 4 + 8 * tier))

        -- ONE aid brewed by hand, its click's tick kept (the make menu cannot
        -- open: one herb, one vial, one secondary)
        t.cheat("::clearinv", false)
        for _, g in ipairs({ "::give raids_buchuleaf 1", "::give raids_vial_water 1", "::give raids_endarkened_juice 1" }) do
            t.cheat(g, false)
        end
        t.ticks(2)
        local _, brew_click = t.tick()
        t.player.use_item_on_item("raids_buchuleaf", "raids_vial_water")
        t.ticks(6)
        local rows = {}
        for _, k in ipairs({ "loc_set", "player_anim", "sound" }) do
            local r, got = t.ticklog.rows({ kind = k })
            t.check("tells.rows." .. k, r == "ok", tostring(r))
            rows[k] = r == "ok" and got or {}
            t.ticks(1)
        end
        local snd, panim, locs = by_tick(rows.sound), by_tick(rows.player_anim), by_tick(rows.loc_set)
        local bad = {}
        local counts = { plant = 0, pick = 0, clean = 0, brew = 0, fire = 0 }
        local picks, brews = {}, {}
        for _, a in ipairs(rows.player_anim) do
            local A = a.tick
            if a.seq == 2291 then
                counts.plant = counts.plant + 1
                if not has(snd[A], function(s) return s.sound == 2432 end) then bad[#bad + 1] = string.format("t%d: dibbing without farming_dibbing", A) end
                if not has(locs[A + 3], function(l) return l.loc >= 29997 and l.loc <= 29999 end) then
                    bad[#bad + 1] = string.format("t%d: no seeded patch on A+3", A)
                end
                if has(locs[A], function(l) return l.loc >= 29997 and l.loc <= 29999 end) then
                    bad[#bad + 1] = string.format("t%d: seeded on the click", A)
                end
            elseif a.seq == 363 then
                counts.brew = counts.brew + 1
                brews[#brews + 1] = A
                if not has(snd[A], function(s) return s.sound == 2608 end) then bad[#bad + 1] = string.format("t%d: a brew without grind", A) end
            elseif a.seq == 733 then
                counts.fire = counts.fire + 1
                if not has(snd[A], function(s) return s.sound == 2599 end) then bad[#bad + 1] = string.format("t%d: createfire without tinderbox_strike", A) end
            end
        end
        for _, s in ipairs(rows.sound) do
            if s.sound == 2581 then
                counts.pick = counts.pick + 1
                picks[#picks + 1] = s.tick
                if not has(panim[s.tick - 1], function(a) return a.seq == 2282 end) then bad[#bad + 1] = string.format("t%d: a pick without picking_low on P-1", s.tick) end
            elseif s.sound >= 3920 and s.sound <= 3923 then
                counts.clean = counts.clean + 1
            end
        end
        for i = 2, #picks do
            local d = picks[i] - picks[i - 1]
            if d < 3 then bad[#bad + 1] = string.format("picks t%d and t%d", picks[i - 1], picks[i]) end
        end
        -- (a make-menu batch, one potion every 2 ticks, is not drivable here:
        -- scriptrun's bot answers the menu with its opening quantity, 1)
        local lit = 0
        for _, l in ipairs(rows.loc_set) do
            if l.loc == 26185 then
                lit = lit + 1
                if not has(snd[l.tick], function(s) return s.sound == 2596 end) then bad[#bad + 1] = string.format("t%d: a fire without fire_lit", l.tick) end
                local first
                for _, a in ipairs(rows.player_anim) do
                    if a.seq == 733 and a.tick < l.tick and (first == nil or a.tick < first) then first = a.tick end
                end
                if first == nil or (l.tick - first) % 4 ~= 0 then
                    bad[#bad + 1] = string.format("t%d: the fire %s ticks after the first strike", l.tick, tostring(first and (l.tick - first)))
                end
            end
        end
        -- RK-F1 / RK-M1: the strike and the potion both answer their click
        -- on its own tick (the recordings' A+0 / A+1 split is the proxy's,
        -- the same for both)
        local strike, brewed
        for _, a in ipairs(rows.player_anim) do
            if a.seq == 733 and a.tick >= fire_click and strike == nil then strike = a.tick end
            if a.seq == 363 and a.tick >= brew_click and brewed == nil then brewed = a.tick end
        end
        if strike == nil or brewed == nil or (brewed - brew_click) ~= (strike - fire_click) then
            bad[#bad + 1] = string.format("strike %s after its click, potion %s after its click (want the same)",
                tostring(strike and strike - fire_click), tostring(brewed and brewed - brew_click))
        end
        t.check("tells.counts", counts.plant > 0 and counts.pick > 1 and counts.clean > 0 and counts.brew > 1 and lit > 0,
            string.format("%d plants, %d picks, %d cleans, %d brews, %d strikes, %d fires", counts.plant, counts.pick,
                counts.clean, counts.brew, counts.fire, lit))
        t.check("tells.recorded", #bad == 0, #bad == 0 and "every tell as recorded" or table.concat(bad, "; "))
        t.finish((#bad == 0 and paid == 4 + 8 * tier and lit > 0) and 0 or 1)
    end,
}
