-- Desert Treasure I: Kamil, fought the way RSProx index 4628 [1507..1690] recorded it: in from
-- the east under Protect from Melee, the evil presence at the clearing's east zone, Kamil added
-- for the player at 2855,3757 and taking the player at 15 tiles, the ice cast from 10 tiles
-- ("You've been frozen!", a flat 5), Protect from Magic out of reach (he telejumps beside the
-- player), back to Protect from Melee, Fire Bolt to the kill: the drop, the varbits and the page.
-- dt_kamil.rs2 has the recorded ticks; docs/rsprox-analysis/quests/desert_treasure_i_kamil.md the
-- tables.
return {
    id = "dt_kamil",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = {
        "::setvar varb358_deserttreasure 11",
        "::setvar varb382_fd_icewarrior_subquest 2",
        "::setvar varb378_fd_icewarrior_trollskilled 5",
        "::setlevel hitpoints 99",
        "::setlevel prayer 99",
        "::setlevel magic 75",
        "::setlevel defence 60",
        "::clearinv",
        "::give firerune 400",
        "::give airrune 300",
        "::give chaosrune 100",
        "::give shark 10",
        -- the recorder walked (the cold empties the run energy, [1055])
        "::setvar varp173_option_run 0",
        -- 2881,3757: the recorder's tile at [1507], walking west into the clearing
        "::tele 0_45_58_1_45",
    },

    run = function(t)
        t.ticklog.start()
        local function var(name) local _, v = t.var.server(name) return v end
        local function said(text, n)
            local r, lines = t.msg.last(n or 10)
            if r ~= "ok" or type(lines) ~= "table" then return false end
            for _, l in ipairs(lines) do
                if tostring(l.text):find(text, 1, true) ~= nil then return true end
            end
            return false
        end
        local function prayer(button, varbit, want)
            local _, now = t.var.varbit(varbit)
            if now ~= want then
                t.ui.tab("prayer")
                t.ticks(1)
                local _, pw = t.ui.widget(button)
                t.ui.invoke(pw, 1)
                t.ticks(1)
            end
        end
        local function kamil()
            local r, row = t.npc.nearest("icediamond_icewarrior", 30)
            if r == "ok" and type(row) == "table" and row.x then return row end
            return nil
        end
        local function me()
            local _, w = t.world.tile()
            return w
        end
        t.ticks(2)
        -- Protect from Melee is up from before the walk in ([1537]'s prayer_allactive keeps bit 14)
        prayer("prayerbook:prayer15", "varb4118_prayer_protectfrommelee", 1)

        -- the evil presence ([1512]) and his add ([1521] at 2855,3757)
        t.ticklog.mark("leg.presence")
        t.player.walk_to(2870, 3757, 30)
        t.ticks(3)
        t.check("presence.message", said("You can feel an evil presence nearby..."), "the zone's line")
        local k = kamil()
        t.check("presence.spawn", k ~= nil, "Kamil " .. (k and (k.x .. "," .. k.z) or "absent") .. " (recorded 2855..2858,3757)")
        t.check("presence.arrow", var("varb14192_current_hint_arrow") == 7, "current_hint_arrow " .. tostring(var("varb14192_current_hint_arrow")))

        -- the casts from range ([1526] [1530] [1534]): the player walks in to 2865,3757 and stands
        t.ticklog.mark("leg.range")
        t.player.walk_to(2865, 3757, 30)
        t.ticks(10)
        t.check("range.frozen", said("You've been frozen!", 20), "the cast's line")
        k = kamil()
        local w = me()
        -- Graded on Kamil's own tile: on some rolls the test player's auto-retaliate walks it up to him after
        -- the first cast (2859,3757 on the roll named fin__dt_kamil), which is the player closing, not Kamil.
        t.check("range.stands", k ~= nil and w ~= nil and k.x <= 2858 and k.z == 3757, "Kamil " .. (k and (k.x .. "," .. k.z) or "?")
            .. " player " .. (w and (w.x .. "," .. w.z) or "?") .. " (he casts from where he stopped, 2855..2858,3757, and never closes)")

        -- Protect from Magic out of reach ([1546]): he telejumps beside the player ([1547], [1548])
        t.ticklog.mark("leg.jump")
        prayer("prayerbook:prayer13", "varb4116_prayer_protectfrommagic", 1)
        local jumped = false
        for _ = 1, 12 do
            t.ticks(1)
            k = kamil()
            w = me()
            if k ~= nil and w ~= nil and math.abs(k.x - w.x) <= 1 and math.abs(k.z - w.z) <= 2 then jumped = true break end
        end
        t.ticks(2)
        k = kamil()
        t.check("jump.beside", jumped, "Kamil " .. (k and (k.x .. "," .. k.z) or "?") .. " after Protect from Magic (recorded (+1,+2) then the tile north)")
        prayer("prayerbook:prayer15", "varb4118_prayer_protectfrommelee", 1)

        -- Fire Bolt to the kill ([1598]..[1677])
        t.ticklog.mark("leg.kill")
        -- A click while frozen is dropped ([1569]), so the cast is clicked again every few ticks
        -- (the recorder clicked Fire Bolt on him 2..4 times a cast, [1594]..[1676]).
        local casts = 0
        for _ = 1, 90 do
            if var("varb377_fd_icewarrior_dead") == 1 then break end
            -- The switch back to Protect from Melee is read again every round and pressed again if it did not
            -- take: on one roll the press above was lost and he hit through Protect from Magic to the death.
            prayer("prayerbook:prayer15", "varb4118_prayer_protectfrommelee", 1)
            local _, hp = t.skill.read("hitpoints")
            if type(hp) == "table" and hp.level and hp.level < 50 then
                t.player.inv_op("shark", 1)
                t.ticks(2)
            end
            if kamil() == nil then break end
            t.player.cast("fire_bolt", "icediamond_icewarrior", 8)
            casts = casts + 1
            t.ticks(3)
        end
        t.ticks(4)
        t.check("kill", var("varb377_fd_icewarrior_dead") == 1, "Kamil dead after " .. casts .. " Fire Bolt click(s)")
        t.check("kill.subquest", var("varb382_fd_icewarrior_subquest") == 3, "fd_icewarrior_subquest " .. tostring(var("varb382_fd_icewarrior_subquest")))
        t.check("kill.dead", var("varb377_fd_icewarrior_dead") == 1, "fd_icewarrior_dead " .. tostring(var("varb377_fd_icewarrior_dead")))
        t.check("kill.arrow", var("varb14192_current_hint_arrow") == 0, "current_hint_arrow " .. tostring(var("varb14192_current_hint_arrow")))
        local _, raw = t.chat.text()
        t.check("kill.page", tostring(raw):find("bad man", 1, true) ~= nil, "page: " .. tostring(raw))
        t.check("kill.drop", t.world.obj_near("4dose2restore", 3) == "ok" and t.world.obj_near("chocolate_cake", 3) == "ok",
            "Super restore(4) and chocolate cake on his tile")
        t.chat.drain()
        t.ticks(3)
    end,
}
