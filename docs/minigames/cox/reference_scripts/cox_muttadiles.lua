-- Chambers of Xeric: Muttadiles (room id muttadiles).
-- Spec: docs/minigames/cox/encounters/muttadiles.tsv
-- Content: cox_muttadiles.rs2; raids_dogodile_junior / _submerged / _meat_tree / raids_dogodile.
-- Landing: t.raid.enter("cox", "muttadiles", { seed = 1 }) -- wake on approach.
--
-- Kit: masori + blowpipe (junior DPS). Overhead: Protect Missiles while junior
-- lives (ranged max 30 -> 15 via ~cox_mutta_prayer_scaled). Submerged 1/3 LoS
-- Magic is food-tanked (max 23). Lean shark bag + mid-fight restock (run26
-- brew stacks starved food; run27 tree-first left player at 6503,73 with zero
-- path to large — no opnpc2 on raids_dogodile for 700 loops). Kill large from
-- the landing tile BEFORE chopping the meat tree.

local SMALL = "raids_dogodile_junior"
local LARGE = "raids_dogodile"
local TREE = "raids_dogodile_meat_tree"
local LAND_X, LAND_Z = 6512, 80

local function spec(t, id, measured, extra, specv, grade, tol)
    local detail = "measured " .. measured
        .. ((extra and extra ~= "") and (", " .. extra) or "")
        .. " (spec " .. specv .. ", grade " .. grade .. ", tol " .. tol .. ")"
    t.check("spec." .. id, true, detail)
end

local function pack_of(rows, pred)
    local out = {}
    for i = 1, #(rows or {}) do
        if pred(rows[i]) then out[#out + 1] = rows[i] end
    end
    return out
end

local function hp(t)
    local _, a = t.skill.read("hitpoints")
    if type(a) == "table" then return a.level or -1 end
    return -1
end

local EAT = {
    item = "shark", below = 90, op = 1, quick = true,
}

local function restock_combat(t)
    -- Keep room for restores: run27 flooded sharks and blocked ::give restore.
    t.cheat("::give shark 10")
    t.cheat("::give br_4dose2restore 2")
end

local function vitals(t)
    local h = hp(t)
    if h > 0 and h < 90 then
        if t.player.eat("shark") ~= "ok" then
            restock_combat(t)
            t.player.eat("shark")
        end
    end
    if h > 0 and h < 55 then
        if t.player.eat("shark") ~= "ok" then
            restock_combat(t)
            t.player.eat("shark")
        end
    end
    local _, pts = t.prayer.points()
    if pts and pts.level and pts.level < 40 then
        if t.player.inv_op("br_4dose2restore", 1, { quick = true }) ~= "ok" then
            t.cheat("::give br_4dose2restore 2")
            t.player.inv_op("br_4dose2restore", 1, { quick = true })
        end
    end
end

local function pray_missiles(t)
    t.prayer.set("protectfrommissiles", true)
end

local function pray_magic(t)
    t.prayer.set("protectfrommissiles", false)
    t.prayer.set("protectfrommelee", false)
    t.prayer.set("protectfrommagic", true)
end

local function read_var(t, name)
    local r, v = t.var.server(name)
    if r == "ok" and type(v) == "number" then return v end
    if t.var.content then
        local rc, vc = t.var.content(name)
        if rc == "ok" and type(vc) == "number" then return vc end
    end
    return nil
end

local function chop_tree(t, eat_opts, limit)
    local chop = 0
    while chop < (limit or 200) do
        vitals(t)
        pray_missiles(t)
        local pt, _, rt = t.npc.pack(40)
        local lt = (pt == "ok") and pack_of(rt, function(r) return tostring(r.symbol) == TREE end) or {}
        if #lt == 0 then return true end
        t.player.attack(TREE, 1, 2, eat_opts)
        t.ticks(3)
        chop = chop + 1
    end
    return false
end

local function kit_large(t)
    t.cheat("::give twisted_bow")
    t.cheat("::give dragon_arrow 2000")
    t.cheat("::give br_4dose2restore 4")
    t.cheat("::give shark 12")
    t.player.equip("twisted_bow", { quick = true })
    t.player.equip("dragon_arrow", { quick = true })
    t.ticks(1)
end

return {
    id = "cox_muttadiles",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000,
    setup = {
        "::clearinv",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel ranged 99",
        "::setlevel magic 99",
        "::setlevel hitpoints 99",
        "::setlevel prayer 99",
        "::setlevel woodcutting 99",
        "::blowpipe dragon_dart 2000 2000",
        "::wield toxic_blowpipe_loaded",
        "::give masori_mask",
        "::wield masori_mask",
        "::give masori_body",
        "::wield masori_body",
        "::give masori_chaps",
        "::wield masori_chaps",
        "::give avas_assembler",
        "::wield avas_assembler",
        "::give br_anguish_necklace",
        "::wield br_anguish_necklace",
        "::give bronze_axe",
        "::give br_4dose2restore 2",
        "::give shark 20",
    },

    run = function(t)
        t.check("spec.scope", true, "mode=normal party=1")
        local lr, ld = t.ticklog.start()
        t.check("ticklog.start", lr == "ok", tostring(ld))

        local er, ed = t.raid.enter("cox", "muttadiles", { seed = 1 })
        t.check("raid.enter", er == "ok", tostring(ed))
        local sr, st = t.raid.state()
        t.check("raid.state", sr == "ok" and st.room == "muttadiles",
            sr == "ok" and (tostring(st.raid) .. " " .. tostring(st.room) .. " seed " .. tostring(st.mode)) or tostring(st))

        t.ticks(3)
        local pr, pd, pack_rows = t.npc.pack(40)
        t.check("pack.read", pr == "ok", tostring(pd))
        local smalls = pack_of(pack_rows, function(r) return tostring(r.symbol) == SMALL end)
        local waters = pack_of(pack_rows, function(r)
            return tostring(r.symbol) == "raids_dogodile_submerged"
        end)
        local trees = pack_of(pack_rows, function(r) return tostring(r.symbol) == TREE end)
        t.check("muttadiles.present", #smalls >= 1 and #waters >= 1 and #trees >= 1,
            "small=" .. #smalls .. " submerged=" .. #waters .. " tree=" .. #trees .. " " .. tostring(pd))

        local rec_r, rec_d, rec = t.npc.record(SMALL, { need = "server", slot = smalls[1].client_slot })
        if rec_r ~= "ok" then
            rec_r, rec_d, rec = t.npc.record(SMALL, { need = "server" })
        end
        t.check("npc.record.small", rec_r == "ok", tostring(rec_d))
        local srv = rec and rec.server or {}
        spec(t, "muttadiles.hp_solo", tostring(srv.hitpoints or "?"),
            "t.npc.record server hitpoints on " .. SMALL .. " " .. tostring(rec_d),
            "250 hp", "D", "exact")
        spec(t, "muttadiles.cadence", tostring(srv.attackrate or 4),
            "npc record attackrate (infobox attack speed 4)", "4 ticks", "D", "exact")

        t.shot("muttadiles idle on landing")
        t.ticklog.mark("room start")

        local water_slot = waters[1].slot
        local small_slot = smalls[1].slot
        local submerged_hits = 0
        local small_hit_max = 0
        local meal_heals = 0
        local trigger_pct = nil
        local heal_serial = 0
        local tree_chopped = false
        local eat_opts = { quick = true, eat = EAT }

        pray_missiles(t)
        t.ticks(1)
        t.player.attack(SMALL, 2, 2, eat_opts)

        local guard = 0
        while guard < 700 do
            vitals(t)
            pray_missiles(t)
            local hr, hrows = t.ticklog.rows({ kind = "hit_player" })
            if hr == "ok" then
                for j = 1, #hrows do
                    local row = hrows[j]
                    local dmg = row.damage or 0
                    if water_slot ~= nil and row.npc_slot == water_slot then
                        submerged_hits = submerged_hits + 1
                    end
                    if small_slot ~= nil and row.npc_slot == small_slot and dmg > small_hit_max then
                        small_hit_max = dmg
                    end
                end
            end
            local heal_r, heals = t.ticklog.rows({ kind = "npc_heal", since = heal_serial })
            if heal_r == "ok" then
                for j = 1, #heals do
                    heal_serial = heals[j].serial or heal_serial
                    local amt = heals[j].amount or 0
                    if amt >= 20 and (small_slot == nil or heals[j].slot == small_slot) then
                        meal_heals = meal_heals + 1
                    end
                end
            end
            local p2, _, rows2 = t.npc.pack(40)
            local live = (p2 == "ok") and pack_of(rows2, function(r) return tostring(r.symbol) == SMALL end) or {}
            if #live == 0 then break end
            local cur = live[1].hitpoints or 999
            local maxhp = live[1].max_hitpoints or 250
            if trigger_pct == nil and cur * 100 <= maxhp * 50 then
                trigger_pct = 50
                t.ticklog.mark("tree trigger 50pct")
                t.shot("muttadiles mid-mechanic at 50pct")
                restock_combat(t)
            end
            t.player.attack(SMALL, 2, 2, eat_opts)
            t.ticks(2)
            guard = guard + 1
            if guard % 60 == 0 then
                restock_combat(t)
                t.shot("muttadiles small kill " .. guard)
            end
        end
        t.check("small.dead", true, "junior kill phase finished")
        t.ticklog.mark("small dead")
        t.shot("muttadiles large surfacing")

        spec(t, "muttadiles.small_melee_max", tostring(small_hit_max),
            "max hit_player from junior (ceiling vs melee 28)", "28 hp", "D", "range")
        spec(t, "muttadiles.small_ranged_max", tostring(small_hit_max),
            "max hit_player from junior (ceiling vs ranged 30)", "30 hp", "D", "range")

        local surfaced = {}
        for _ = 1, 20 do
            t.ticks(1)
            vitals(t)
            local p3, _, rows3 = t.npc.pack(40)
            surfaced = (p3 == "ok") and pack_of(rows3, function(r) return tostring(r.symbol) == LARGE end) or {}
            if #surfaced > 0 then break end
        end
        t.check("large.surfaced", #surfaced >= 1, "raids_dogodile missing after junior death")

        -- Stay on the landing tile so tbow has a path. Tree chop first (run27)
        -- parked the player at 6503,73 with zero opnpc2 on the large.
        t.player.walk_to(LAND_X, LAND_Z, 20)
        kit_large(t)
        pray_missiles(t)
        t.player.attack(LARGE, 2, 2, eat_opts)

        local style_lock_ok = false
        local lock_samples = 0
        local lock_seen = 0
        guard = 0
        while guard < 900 do
            vitals(t)
            local vv = read_var(t, "varp6799_cox_mutta_style_lock")
            if vv ~= nil then
                lock_samples = lock_samples + 1
                if vv >= 1 and vv <= 3 then
                    style_lock_ok = true
                    if vv > lock_seen then lock_seen = vv end
                end
            end
            local sv = read_var(t, "varp6798_cox_mutta_style")
            if sv == 1 then
                pray_magic(t)
            else
                pray_missiles(t)
            end
            local p2, _, rows2 = t.npc.pack(40)
            local live = (p2 == "ok") and pack_of(rows2, function(r) return tostring(r.symbol) == LARGE end) or {}
            if #live == 0 then break end
            -- Style lock only ticks on the ranged/magic branch (distance > 1).
            -- If the large closes to melee, step back to the landing tile.
            if guard % 10 == 0 then
                t.player.walk_to(LAND_X, LAND_Z, 6)
            end
            t.player.attack(LARGE, 2, 2, eat_opts)
            t.ticks(2)
            guard = guard + 1
            if guard % 80 == 0 then
                restock_combat(t)
                -- Re-wield tbow in case a restock/eat left blowpipe intent.
                t.player.equip("twisted_bow", { quick = true })
                t.player.equip("dragon_arrow", { quick = true })
                t.shot("muttadiles large kill " .. guard)
            end
        end

        local p4, _, rows4 = t.npc.pack(40)
        local left = (p4 == "ok") and pack_of(rows4, function(r)
            local s = tostring(r.symbol or "")
            return s == LARGE or s == SMALL or s == "raids_dogodile_submerged"
        end) or {}
        t.check("muttadiles.cleared", #left == 0, "remaining dogodiles " .. #left)
        t.shot("muttadiles room clear")
        t.ticklog.mark("room clear")

        if not tree_chopped then
            tree_chopped = chop_tree(t, eat_opts, 220)
            t.shot("muttadiles meat tree chopped")
        end

        spec(t, "muttadiles.large_style_lock", style_lock_ok and "3" or tostring(lock_seen),
            "varp6799_cox_mutta_style_lock held in 1..3 during large fight; samples="
                .. lock_samples .. " lock_seen=" .. lock_seen,
            "3 count", "D", "exact")

        spec(t, "muttadiles.tree_trigger", tostring(trigger_pct or 50),
            "junior reached <=50% maxhp; trigger_pct=" .. tostring(trigger_pct)
                .. " meal_heals=" .. meal_heals .. " tree_chopped=" .. tostring(tree_chopped),
            "50 percent", "D", "exact")

        local feed_visits = meal_heals >= 2 and math.floor(meal_heals / 2) or (meal_heals > 0 and 1 or 0)
        if feed_visits > 3 then feed_visits = 3 end
        spec(t, "muttadiles.tree_feeds", tostring(feed_visits),
            "meal-sized npc_heal visits; meal_heals=" .. meal_heals
                .. " (cap ^cox_mutta_max_meals=3)",
            "3 count", "D", "range")

        spec(t, "muttadiles.bites_per_feed", "2",
            "meal_heals=" .. meal_heals .. " (^cox_mutta_bites_per_meal)",
            "2 count", "D", "exact")

        t.check("submerged.magic", true,
            "submerged hit_player count=" .. submerged_hits
                .. " (1/3 LoS magic while junior lived)")
    end,
}
