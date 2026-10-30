-- jewellery_teleports: every jewellery teleport the 2026-10-29 recordings
-- (index 4404..4430) use, by the route they used it. The backpack's Rub
-- submenu (`if_subop`, last_subop 1-based) and the worn tab's ops; each lands
-- inside its recorded spread three ticks on, says the recorded purple charge
-- line, and swaps the item for its next charge:
--   games necklace  Rub > Barbarian Outpost (4420 [255], 4421 [1599]),
--                   Rub > Wintertodt Camp (4424 [347] [1344]),
--                   worn op 3 Barbarian Outpost (4349 [302]);
--   ring of dueling Rub > Castle Wars (4420 [1208], 4426 [121]),
--                   worn op 3 Castle Wars (4412 [252], 4426 [869], 4427 [75]);
--   passage         Rub > The Outpost (4420 [1061], 4426 [892], 4429 x3),
--                   Rub > Wizards' Tower (4423 [425]);
--   glory           worn op 2 Edgeville (4421 [1454]), 4 Draynor Village
--                   (4407 [98], ...), 5 Al Kharid (4428 [260]).
local function near(t, x, z, level, r)
    local tr, v = t.world.tile()
    if tr ~= "ok" or type(v) ~= "table" then
        return false, tostring(tr)
    end
    local where = v.x .. "," .. v.z .. "," .. v.level
    return v.level == level and math.abs(v.x - x) <= r and math.abs(v.z - z) <= r, where
end

local function landing(t, name, dest, line, obj)
    local ok, where
    for _ = 1, 8 do
        ok, where = near(t, dest[1], dest[2], dest[3], dest[4])
        if ok then break end
        t.ticks(1)
    end
    t.check(name .. ".land", ok, "want within " .. dest[4] .. " of " .. dest[1] .. "," .. dest[2]
        .. "," .. dest[3] .. ", at " .. tostring(where))
    t.check(name .. ".msg", t.msg.expect(line))
    if obj then
        local cr, count = t.inv.count(obj)
        local wr, worn = t.inv.count_in("worn", obj)
        t.check(name .. ".charge", (cr == "ok" and count == 1) or (wr == "ok" and worn == 1),
            obj .. ": inv " .. tostring(count) .. ", worn " .. tostring(worn))
    end
    t.ticks(2)
end

local function worn_op(t, name, slot_name, op)
    t.ui.tab("equipment")
    t.ticks(1)
    local _, slot = t.ui.widget(slot_name)
    t.check(name .. ".slot", slot ~= nil, slot_name .. " " .. tostring(slot))
    if slot then t.ui.invoke(slot, op) end
end

local OUTPOST_BARB = { 2520, 3571, 0, 2 }
local WINTERTODT = { 1624, 3938, 0, 1 }
local CASTLE_WARS = { 2440, 3090, 0, 2 }
local THE_OUTPOST = { 2430, 3349, 0, 2 }
local WIZARDS_TOWER = { 3114, 3179, 0, 2 }

return {
    id = "jewellery_teleports",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::give necklace_of_minigames_8 1", "::give ring_of_dueling_8 1",
        "::give necklace_of_passage_5 1", "::give amulet_of_glory_4 1",
    },
    run = function(t)
        t.ticklog.start()
        t.ticks(3)

        t.ticklog.mark("games.barbarian")
        t.exec("games.barbarian.op", t.player.inv_op, "necklace_of_minigames_8", 4, { subop = 2 })
        landing(t, "games.barbarian", OUTPOST_BARB,
            "<col=7f007f>Your games necklace has seven uses left.</col>", "necklace_of_minigames_7")
        t.ticklog.mark("games.wintertodt")
        t.exec("games.wintertodt.op", t.player.inv_op, "necklace_of_minigames_7", 4, { subop = 5 })
        landing(t, "games.wintertodt", WINTERTODT,
            "<col=7f007f>Your games necklace has six uses left.</col>", "necklace_of_minigames_6")

        t.ticklog.mark("dueling.castlewars")
        t.exec("dueling.castlewars.op", t.player.inv_op, "ring_of_dueling_8", 4, { subop = 2 })
        landing(t, "dueling.castlewars", CASTLE_WARS,
            "<col=7f007f>Your ring of dueling has seven uses left.</col>", "ring_of_dueling_7")

        t.exec("dueling.wear", t.player.inv_op, "ring_of_dueling_7", 2)
        t.ticks(2)
        t.exec("games.wear", t.player.inv_op, "necklace_of_minigames_6", 2)
        t.ticks(2)
        t.ticklog.mark("passage.wizards")
        t.exec("passage.wizards.op", t.player.inv_op, "necklace_of_passage_5", 4, { subop = 1 })
        landing(t, "passage.wizards", WIZARDS_TOWER,
            "<col=7f007f>Your necklace of passage has four uses left.</col>", "necklace_of_passage_4")

        t.ticklog.mark("dueling.worn")
        worn_op(t, "dueling.worn", "wornitems:slot12", 3)
        landing(t, "dueling.worn", CASTLE_WARS,
            "<col=7f007f>Your ring of dueling has six uses left.</col>", "ring_of_dueling_6")
        t.ticklog.mark("games.worn")
        worn_op(t, "games.worn", "wornitems:slot2", 3)
        landing(t, "games.worn", OUTPOST_BARB,
            "<col=7f007f>Your games necklace has five uses left.</col>", "necklace_of_minigames_5")

        t.ticklog.mark("passage.outpost")
        t.exec("passage.outpost.op", t.player.inv_op, "necklace_of_passage_4", 4, { subop = 2 })
        landing(t, "passage.outpost", THE_OUTPOST,
            "<col=7f007f>Your necklace of passage has three uses left.</col>", "necklace_of_passage_3")

        t.exec("glory.wear", t.player.inv_op, "amulet_of_glory_4", 2)
        t.ticks(2)
        t.ticklog.mark("glory.edgeville")
        worn_op(t, "glory.edgeville", "wornitems:slot2", 2)
        landing(t, "glory.edgeville", { 3087, 3496, 0, 0 },
            "<col=7f007f>Your amulet has three charges left.</col>")
        t.ticklog.mark("glory.draynor")
        worn_op(t, "glory.draynor", "wornitems:slot2", 4)
        landing(t, "glory.draynor", { 3105, 3251, 0, 0 },
            "<col=7f007f>Your amulet has two charges left.</col>")
        t.ticklog.mark("glory.alkharid")
        worn_op(t, "glory.alkharid", "wornitems:slot2", 5)
        landing(t, "glory.alkharid", { 3293, 3163, 0, 0 },
            "<col=7f007f>Your amulet has one charge left.</col>", "amulet_of_glory_1")
        t.finish(0)
    end,
}
