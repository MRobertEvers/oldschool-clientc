-- Desert Treasure I: the pyramid's sarcophagus mummies on their add (dt_pyramid.rs2
-- ~dt_sarcophagus_emerge), from the player tiles RSProx index 4629 recorded:
--   [4526] the player on 2902,4961 beside the lid at 2903,4961 (rotation 0): the mummy is added on
--          the sarcophagus's tile, says "Rawr!", faces the player's tile, and is on the tile in
--          front the next tick.
--   [4582] the player on 2920,4961, the lid at 2918,4963 (rotation 1): the mummy is added on the
--          tile in front (2918,4964) playing deserttreasure_mummy_emerge (1930), says "Rawr!",
--          faces the player's tile, no glide.
-- ::dt_mummy opens the nearest shut sarcophagus without the roll. Each add is read from the tick
-- log (the add tick's own rows), not by polling the npc after it.
local MUMMY = 717

return {
    id = "dt_mummies",
    fixture = "fresh_lumbridge.ini",
    max_frames = 20000,
    setup = {
        "::setlevel hitpoints 99",
        "::setlevel defence 99",
        "::clearinv",
        "::tele 3_45_77_22_33",
    },

    run = function(t)
        t.ticklog.start()
        local function since_mark()
            local _, marks = t.ticklog.rows({ kind = "mark" })
            return marks[#marks].serial
        end
        local function first(kind, since, extra)
            local opts = { kind = kind, since = since, type = MUMMY }
            for k, v in pairs(extra or {}) do opts[k] = v end
            local _, rows = t.ticklog.rows(opts)
            return rows and rows[1] or nil
        end
        local function at(r) return r and (r.x .. "," .. r.z) or "none" end
        -- The add's face is its coord face (x >= 0); an entity face is written with x = -1.
        local function coord_face(since)
            local _, rows = t.ticklog.rows({ kind = "npc_face", since = since, type = MUMMY,
                where = function(r) return r.x >= 0 end })
            return rows and rows[1] or nil
        end
        t.ticks(3)

        t.ticklog.mark("leg.rot0")
        local s0 = since_mark()
        t.cheat("::dt_mummy")
        t.ticks(3)
        local add = first("npc_spawn", s0)
        t.check("rot0.add", add ~= nil and add.x == 2903 and add.z == 4961, "added on " .. at(add) .. " (recorded the sarcophagus, 2903,4961)")
        local say = first("npc_say", s0, { text = "Rawr!" })
        t.check("rot0.rawr", say ~= nil and add ~= nil and say.tick == add.tick, "\"Rawr!\" on the add tick")
        local face = coord_face(s0)
        t.check("rot0.face", face ~= nil and add ~= nil and face.x == 2902 and face.z == 4961 and face.tick == add.tick,
            "faces " .. at(face) .. " on the add (recorded the player's tile, 2902,4961)")
        t.check("rot0.no_emerge", first("npc_anim", s0, { seq = 1930 }) == nil, "no emerge seq (0 of 5 recorded at rotations 0, 2, 3)")
        local _, row = t.npc.nearest("deserttreasure_mummy_1", 4)
        t.check("rot0.glide", row ~= nil and row.x == 2902 and row.z == 4961, "then in front, " .. at(row) .. " (recorded 2902,4961 at T+1)")
        t.cheat("::tele 0_50_50_0_0")
        t.ticks(40)

        t.cheat("::tele 3_45_77_40_33")
        t.ticks(3)
        t.ticklog.mark("leg.rot1")
        local s1 = since_mark()
        t.cheat("::dt_mummy")
        t.ticks(3)
        add = first("npc_spawn", s1)
        t.check("rot1.add", add ~= nil and add.x == 2918 and add.z == 4964, "added on " .. at(add) .. " (recorded in front, 2918,4964)")
        local emerge = first("npc_anim", s1, { seq = 1930 })
        t.check("rot1.emerge", emerge ~= nil and add ~= nil and emerge.tick == add.tick, "deserttreasure_mummy_emerge on the add tick")
        say = first("npc_say", s1, { text = "Rawr!" })
        t.check("rot1.rawr", say ~= nil and add ~= nil and say.tick == add.tick, "\"Rawr!\" on the add tick")
        face = coord_face(s1)
        t.check("rot1.face", face ~= nil and face.x == 2920 and face.z == 4961, "faces " .. at(face) .. " (recorded the player's tile, 2920,4961)")
        t.ticks(2)
    end,
}
