-- Desert Treasure I: Damis, both forms, fought the way RSProx index 4626 [746..1154] recorded it:
-- the ring of visibility worn and Protect from Melee up, a step into zone 48_32 at 2739,5089 (his
-- add on the next tick at 2740,5089, the arrow on him), water blasts while he stands beside the
-- player, the transformation at kill + 1, prayer potions through the second form's drain, and the
-- diamond off his death tile. dt_damis.rs2 has the recorded ticks;
-- docs/rsprox-analysis/quests/desert_treasure_i_damis.md the tables.
return {
    id = "dt_damis",
    fixture = "fresh_lumbridge.ini",
    max_frames = 80000,
    setup = {
        "::setvar varb358_deserttreasure 11",
        "::setvar varb383_fd_shadowwarrior_quest 3",
        "::setlevel hitpoints 99",
        "::setlevel prayer 70",
        "::setlevel magic 99",
        "::setlevel defence 70",
        "::clearinv",
        "::give fd_ring_visibility 1",
        "::give waterrune 1000",
        "::give airrune 1000",
        "::give deathrune 300",
        "::give 4doseprayerrestore 14",
        "::give shark 6",
        -- two tiles south of the zone the recorder crossed at [746]
        "::tele 0_42_79_51_31",
    },

    run = function(t)
        t.ticklog.start()
        local function count(sym) local r, n = t.inv.count(sym) return r == "ok" and n or 0 end
        local function var(name) local _, v = t.var.server(name) return v end
        local function prayer_now()
            local r, s = t.skill.read("prayer")
            if r == "ok" and type(s) == "table" then return s.level end
            return nil
        end
        local function protect_melee()
            t.ui.tab("prayer")
            t.ticks(1)
            local _, pw = t.ui.widget("prayerbook:prayer15")
            t.ui.invoke(pw, 1)
            t.ticks(1)
        end
        local doses = { "1doseprayerrestore", "2doseprayerrestore", "3doseprayerrestore", "4doseprayerrestore" }
        local function drink_if_low()
            local p = prayer_now()
            if p == nil or p >= 30 then return end
            for _, d in ipairs(doses) do
                if count(d) > 0 then
                    t.player.inv_op(d, 1)
                    t.ticks(1)
                    break
                end
            end
            if var("varb4118_prayer_protectfrommelee") ~= 1 then protect_melee() end
        end

        t.ticks(2)
        t.exec("ring.wear", t.player.inv_op, "fd_ring_visibility", 2)
        t.ticks(1)
        protect_melee()
        t.ticks(1)

        -- the crossing ([746]) and his add on the next tick ([747])
        t.ticklog.mark("leg.cross")
        t.exec("cross", t.player.walk_to, 2739, 5089, 6)
        t.ticks(2)
        t.check("spawn.hint", var("varb14192_current_hint_arrow") == 7,
            "current_hint_arrow " .. tostring(var("varb14192_current_hint_arrow")))

        -- water blasts to both kills; he comes beside the player and swings
        t.ticklog.mark("leg.form1")
        local form2_marked = false
        local casts = 0
        local answers = {}
        for _ = 1, 200 do
            local stage = var("varb383_fd_shadowwarrior_quest")
            if stage == 5 then break end
            if stage == 4 and not form2_marked then
                t.ticklog.mark("leg.form2")
                form2_marked = true
            end
            drink_if_low()
            local sym = stage == 4 and "fd_damis_tougher" or "fd_damis_normal"
            local r = t.player.cast("water_blast", sym, 8)
            answers[tostring(r)] = (answers[tostring(r)] or 0) + 1
            if r ~= "ok" then t.ticks(1) end
            casts = casts + 1
        end
        local summary = ""
        for k, n in pairs(answers) do summary = summary .. k .. "=" .. n .. " " end
        t.check("form1.transform", form2_marked, "fd_shadowwarrior_quest reached 4 (the second form); casts " .. summary)
        t.ticks(8)
        t.ticklog.mark("leg.after")
        t.check("kill.stage", var("varb383_fd_shadowwarrior_quest") == 5,
            "fd_shadowwarrior_quest " .. tostring(var("varb383_fd_shadowwarrior_quest")) .. " after " .. casts .. " casts")
        t.check("kill.hint", var("varb14192_current_hint_arrow") == 0,
            "current_hint_arrow " .. tostring(var("varb14192_current_hint_arrow")))

        -- the diamond on his death tile, owner only ([1151]); the generic pickup ([1153]..[1154])
        t.exec("diamond.take", t.player.click_obj, "fd_dark_diamond", 3)
        t.ticks(4)
        t.check("diamond.held", count("fd_dark_diamond") == 1, "shadow diamonds held: " .. count("fd_dark_diamond"))
        t.ticks(2)
    end,
}
