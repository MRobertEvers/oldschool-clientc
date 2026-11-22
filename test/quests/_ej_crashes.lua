-- Enlightened Journey: the three ways a balloon flight ends badly, each from
-- RSProx (docs/rsprox-analysis/quests/enlightened_journey.md, "Crashes"):
--   Bail over Entrana (screen 1, column 0)     -> "You bail, but land safely on Entrana." on 2809,3355
--   Bail on screen 1 column 15 (the sea)       -> 1803,4890; Auguste's pages; the plank to 2867,3368
--   Bail on screen 1 column 14 (the woodland)  -> a copy of the crash site; Auguste; the mound to 3068,3358
-- The presses before each bail are the recorder's landing run's first 14/15
-- (4644 [3496..]), so the balloon is where 4637 [3939] and 4644 [2435] had it.

local ROUTE = "SLRRRRRRRRRERRTRRRRRLRLRRRRRRRRRRLRRRRRRRRRRRRETRRRLRRRRT"

local function fly(t, label, presses)
    t.check(label .. ".ready", t.cheat("::ejfly") == "ok", "::ejfly")
    t.ticks(2)
    t.exec(label .. ".talk", t.player.talk_to, "zep_piccard", 1)
    t.exec(label .. ".talk-dialog", t.chat.play, { "npc:Well, let's get going!", "choose:Okay.", "player:Okay." })
    t.ticks(2)
    local ctl = {}
    for key, name in pairs({ S = "sandbags", L = "logs", R = "relax", T = "tug", E = "tug_emerg" }) do
        local _, w = t.ui.widget("zep_interface_side:zep_btn_" .. name)
        ctl[key] = w
    end
    for i = 1, presses do
        t.ui.invoke(ctl[ROUTE:sub(i, i)], 1)
        t.ticks(1)
    end
    local _, bail = t.ui.widget("zep_interface_side:zep_btn_close")
    t.check(label .. ".bail", t.ui.invoke(bail, 1), "zep_btn_close")
end

return {
    id = "_ej_crashes",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = {
        "::clearinv",
        "::setlevel firemaking 20",
        "::setlevel farming 30",
        "::setlevel crafting 36",
        "::setvar varp101_qp 20",
    },
    run = function(t)
        -- over Entrana: 4641 [64..68]
        fly(t, "entrana", 0)
        t.ticks(2)
        t.exec("entrana-mesbox", t.chat.play, { "mesbox:You bail, but land safely on Entrana." })
        t.ticks(3)
        local _, e = t.world.tile()
        t.check("entrana.tile", e.x == 2809 and e.z == 3355, "on " .. e.x .. "," .. e.z)
        t.check("entrana.stage", select(2, t.var.server("varb2866_zep_quest")) == 90, "still 90")

        -- the sea: 4644 [2435..2504]
        fly(t, "sea", 15)
        t.ticks(4)
        t.exec("sea-mesbox", t.chat.play, { "mesbox:The balloon crashes." })
        t.ticks(2)
        local _, s = t.world.tile()
        t.check("sea.tile", s.x == 1803 and s.z == 4890, "in the sea at " .. s.x .. "," .. s.z)
        t.exec("sea.auguste", t.player.talk_to, "zep_piccard_crash", 1)
        t.exec("sea.auguste-dialog", t.chat.play, {
            "npc:Great Scott! Ocean!", "player:What happened?", "npc:Crashed, off course.",
            "player:Contemplative?", "npc:No...more along the lines of ill", "player:How are you going to get back?",
            "npc:I'm sure a fishing trawler",
        })
        t.exec("sea.plank", t.player.click_loc, "zep_plank", 1)
        t.exec("sea.plank-dialog", t.chat.play, {
            "mesbox:You climb onto the plank and swim to shore.", "mesbox:You manage to swim back to shore.",
        })
        t.ticks(2)
        local _, sh = t.world.tile()
        t.check("sea.shore", sh.x == 2867 and sh.z == 3368, "ashore at " .. sh.x .. "," .. sh.z)

        -- the woodland: 4637 [3939..3991], 4644 [3062..3099]
        fly(t, "land", 14)
        t.ticks(4)
        t.exec("land-mesbox", t.chat.play, { "mesbox:The balloon crashes." })
        t.ticks(2)
        t.exec("land.auguste", t.player.talk_to, "zep_piccard_crashland", 1)
        t.exec("land.auguste-dialog", t.chat.play, {
            "npc:Well, that didn't go as planned.", "player:So what now?", "npc:I'll have to get one of the monks",
        })
        t.exec("land.mound", t.player.click_loc, "zep_dirt_exit", 1)
        t.exec("land.mound-dialog", t.chat.play, { "mesbox:You walk away from the crash site." })
        t.ticks(6)
        t.exec("land.falador-dialog", t.chat.play, { "player:The crash must have really sent us off course" })
        local _, f = t.world.tile()
        t.check("land.exit", f.x == 3068 and f.z == 3358, "out at " .. f.x .. "," .. f.z)
        t.check("land.stage", select(2, t.var.server("varb2866_zep_quest")) == 90, "still 90")
        t.finish(0)
    end,
}
