-- Desert Treasure I: the Ancient Pyramid's mummies in a fight (dt_mummies.rs2), against RSProx
-- 3003 (a Fire Wave hunt of the bottom floor) and the other mummy sessions:
--   fire:  a fire spell turns a pyramid mummy into its burning form on the cast tick (3003 [235]);
--          a water spell puts it out (wiki).
--   death: the mummy becomes mummy_turn_to_ash (718) on its death tick with mummy_on_fire_death
--          twice (burning) or mummy_ash_death + mummy_on_fire_death; gone two ticks on, ashes on
--          its tile (17/17).
--   swing: mummy_attack (641) from a normal mummy, mummy_on_fire_attack (640) from a burning one.
--   sarcophagus: melee does nothing to deserttreasure_mummy_1 (1 hitpoint); a spell kills it and it
--          rises burning (724) with 10 hitpoints, no ash (3003 [5817], [5818]); that one dies to
--          ashes alone.
local ASH = 718
local SARC, SARC_FIRE = 717, 724

return {
    id = "dt_mummy_fight",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = {
        -- auto-retaliate off: ~maxmage's powered staff otherwise swings at whatever hits the
        -- player, over the spell the test pressed
        "::clearinv", "::~maxmage", "::runes", "::setvar varb4070_spellbook 0", "::setvar varp172_option_nodef 1",
        "::setlevel magic 99", "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel prayer 99",
        "::setlevel attack 99", "::setlevel strength 99",
        "::give dragon_scimitar 1", "::give shark 10",
        -- where 3003's recorder stood most on the bottom floor (3246,9325: 178 ticks)
        "::tele 0_50_145_46_45",
    },

    run = function(t)
        t.ticklog.start()
        local function since()
            t.ticklog.mark("m")
            local _, marks = t.ticklog.rows({ kind = "mark" })
            return marks[#marks].serial
        end
        local function rows(kind, s, opts)
            local o = { kind = kind, since = s }
            for k, v in pairs(opts or {}) do o[k] = v end
            local _, r = t.ticklog.rows(o)
            return r or {}
        end
        local function protect_melee()
            local _, now = t.var.varbit("varb4118_prayer_protectfrommelee")
            if now ~= 1 then
                t.ui.tab("prayer")
                t.ticks(1)
                local _, pw = t.ui.widget("prayerbook:prayer15")
                t.ui.invoke(pw, 1)
                t.ticks(1)
            end
        end
        local function kill(sym, spell, tries)
            for _ = 1, tries or 30 do
                local r, row = t.npc.nearest(sym, 12)
                if r ~= "ok" or type(row) ~= "table" then return true end
                protect_melee()
                t.player.cast(spell, sym, 6)
                t.ticks(1)
            end
            return false
        end
        local function retype(s, from, to)
            for _, r in ipairs(rows("npc_retype", s)) do
                if r.from_type == from and r.to_type == to then return r end
            end
            return nil
        end
        local function await_retype(s, from, to, ticks)
            for _ = 1, ticks or 15 do
                local r = retype(s, from, to)
                if r ~= nil then return r end
                t.ticks(1)
            end
            return retype(s, from, to)
        end
        local function sounds_at(s, tick)
            local out = {}
            for _, r in ipairs(rows("sound", s)) do
                if r.tick == tick then out[#out + 1] = r.sound end
            end
            table.sort(out)
            return table.concat(out, ",")
        end
        t.ticks(3)
        protect_melee()

        -- The four pyramid types and their burning forms; the floor mixes them, and a mummy behind
        -- a wall answers "I can't reach that!", so each step takes the first one a cast reaches.
        local KINDS = {
            { "deserttreasure_mummy_2", 720, 725 }, { "deserttreasure_mummy_3", 721, 726 },
            { "deserttreasure_mummy_4", 722, 727 }, { "deserttreasure_mummy_5", 723, 728 },
        }
        local castlog = {}
        local function cast_any(spell, burning)
            for _, k in ipairs(KINDS) do
                local sym = burning and (k[1] .. "_on_fire") or k[1]
                local r = t.npc.nearest(sym, 12)
                if r == "ok" then
                    local cr, cd = t.player.cast(spell, sym, 6)
                    castlog[#castlog + 1] = spell .. ">" .. sym:sub(-1) .. ":" .. tostring(cr) .. (cr ~= "ok" and cr ~= "timeout" and (" " .. tostring(cd):sub(-60)) or "")
                    if cr == "ok" or cr == "timeout" then return k, sym end
                else
                    castlog[#castlog + 1] = spell .. ">" .. sym:sub(-1) .. ":none near"
                end
            end
            return nil, nil
        end
        local function kill_sym(sym, spell)
            for _ = 1, 30 do
                if t.npc.nearest(sym, 12) ~= "ok" then return end
                protect_melee()
                local h = t.skill.read and select(2, t.skill.read("hitpoints"))
                if type(h) == "table" and h.level and h.level < 60 then t.player.inv_op("shark", 1) t.ticks(2) end
                t.player.cast(spell, sym, 6)
                t.ticks(1)
            end
        end

        -- fire: the cast tick
        local s1 = since()
        local k1 = cast_any("fire_wave", false)
        t.ticks(2)
        t.check("fire.cast", k1 ~= nil, "a Fire Wave reached a pyramid mummy")
        local lit = k1 and retype(s1, k1[2], k1[3]) or nil
        local castanim = rows("player_anim", s1)[1]
        t.check("fire.lit", lit ~= nil, tostring(k1 and k1[1]) .. " -> _on_fire")
        t.check("fire.cast_tick", lit ~= nil and castanim ~= nil and lit.tick == castanim.tick,
            "on the cast's tick: retype " .. tostring(lit and lit.tick) .. ", cast anim " .. tostring(castanim and castanim.tick))

        -- the burning mummy dies to ash
        if k1 ~= nil then
            kill_sym(k1[1] .. "_on_fire", "fire_wave")
            t.ticks(4)
        end
        local ash = k1 and retype(s1, k1[3], ASH) or nil
        t.check("burnt.ash", ash ~= nil, "the burning mummy became mummy_turn_to_ash")
        if ash ~= nil then
            t.check("burnt.sounds", sounds_at(s1, ash.tick) == "644,644", "death synths " .. sounds_at(s1, ash.tick) .. " (recorded 644,644)")
            local freed = nil
            for _, r in ipairs(rows("npc_free", s1)) do if r.slot == ash.slot and r.tick >= ash.tick then freed = r break end end
            t.check("burnt.del", freed ~= nil and freed.tick == ash.tick + 2, "gone at " .. tostring(freed and freed.tick) .. ", ash at " .. ash.tick .. " (recorded +2)")
            local ashes = false
            for _, r in ipairs(rows("obj_add", s1)) do
                if freed and r.tick == freed.tick and r.obj == 592 then ashes = true end
            end
            t.check("burnt.ashes", ashes, "ashes on the del tick")
        end

        -- water puts a burning mummy out: ::dt_mummy_burn lights the nearest, ::dt_mummy_douse runs
        -- the same cast hook a Water Wave runs (the hook's wiring is the fire rows above)
        local s2 = since()
        t.cheat("::dt_mummy_burn")
        t.ticks(2)
        local lit2 = nil
        for _, k in ipairs(KINDS) do lit2 = lit2 or retype(s2, k[2], k[3]) end
        t.cheat("::dt_mummy_douse")
        t.ticks(2)
        local out2 = lit2 and retype(s2, lit2.to_type, lit2.from_type) or nil
        t.check("douse", lit2 ~= nil and out2 ~= nil, "lit " .. tostring(lit2 and lit2.to_type) .. ", put out to " .. tostring(out2 and out2.to_type))

        -- a normal death: melee on one mummy (the driver keeps the target)
        local s3 = since()
        t.player.inv_op("dragon_scimitar", 2)
        t.ticks(1)
        local k3 = nil
        protect_melee()
        -- the recorder's two most-stood tiles (3003: 3246,9325 x178, 3234,9325 x227)
        for _, spot in ipairs({ false, "::tele 0_50_145_34_45" }) do
            if spot then t.cheat(spot) t.ticks(3) end
            for _, k in ipairs(KINDS) do
                if t.npc.nearest(k[1], 12) == "ok" then
                    local ar = t.player.attack(k[1], 2, 10)
                    if ar == "ok" then k3 = k break end
                end
            end
            if k3 ~= nil then break end
        end
        if k3 ~= nil then
            t.npc.await_dead(k3[1], 200, 12, 10, { eat = { item = "shark", below = 60, op = 1 } })
            t.ticks(4)
        end
        local ash2 = k3 and retype(s3, k3[2], ASH) or nil
        t.check("plain.ash", ash2 ~= nil, "a mummy killed without fire also turns to ash")
        if ash2 ~= nil then
            t.check("plain.sounds", sounds_at(s3, ash2.tick) == "638,644", "death synths " .. sounds_at(s3, ash2.tick) .. " (recorded 638,644)")
        end

        -- the swings' sounds over the whole fight
        local s641, s640 = 0, 0
        for _, r in ipairs(rows("sound", s1)) do
            if r.sound == 641 then s641 = s641 + 1 end
            if r.sound == 640 then s640 = s640 + 1 end
        end
        t.check("swing.sounds", s641 > 0, "mummy_attack x" .. s641 .. ", mummy_on_fire_attack x" .. s640)

        -- the sarcophagus mummy
        t.cheat("::tele 3_45_77_22_33")
        t.ticks(4)
        local s4 = since()
        t.cheat("::dt_mummy")
        t.ticks(3)
        t.player.inv_op("dragon_scimitar", 2)
        t.ticks(1)
        t.player.attack("deserttreasure_mummy_1", 2, 8)
        t.ticks(6)
        local hurt = 0
        for _, r in ipairs(rows("hit_npc", s4, { type = SARC })) do hurt = hurt + (r.damage or 0) end
        t.check("sarc.melee", hurt == 0, "melee dealt " .. hurt .. " to the sarcophagus mummy (recorded 0)")
        -- Wind Strike can roll 0 on its 1 hitpoint: cast until it rises
        local rose = nil
        for _ = 1, 6 do
            t.player.cast("wind_strike", "deserttreasure_mummy_1", 6)
            rose = await_retype(s4, SARC, SARC_FIRE, 4)
            if rose ~= nil then break end
        end
        t.check("sarc.rises", rose ~= nil, "killed by a spell it rises burning (724)")
        t.check("sarc.no_ash", retype(s4, SARC, ASH) == nil, "no ash the first time")
        -- the burning one takes melee as any mummy does (only the unlit one shrugs it off)
        t.player.attack("deserttreasure_mummy_1_on_fire", 2, 10)
        t.npc.await_dead("deserttreasure_mummy_1_on_fire", 60, 12, 6)
        t.ticks(4)
        local ash3 = retype(s4, SARC_FIRE, ASH)
        t.check("sarc.ash", ash3 ~= nil, "the burning one dies to ash")
        if ash3 ~= nil then
            local loot = {}
            for _, r in ipairs(rows("obj_add", s4)) do
                if r.tick >= ash3.tick and r.tick <= ash3.tick + 2 then loot[#loot + 1] = r.obj end
            end
            local at_once = false
            for _, r in ipairs(rows("obj_add", s4)) do if r.tick == ash3.tick and r.obj == 592 then at_once = true end end
            t.check("sarc.only_ashes", #loot == 1 and loot[1] == 592, "drops " .. table.concat(loot, ",") .. " (ashes only)")
            t.check("sarc.ashes_at_once", at_once, "the ashes land with the turn to ash (3003 [5833])")
        end
    end,
}
