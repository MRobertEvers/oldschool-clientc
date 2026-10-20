-- _toa_akkha_visual: scratch, for eyes. Akkha from the crystal through his first two specials (a style
-- change on each, the Memory Blast's disappear and appear), a shot every 2 ticks, then Osmumten's chat.
local function keep_alive(t) t.cheat("::boost hitpoints 255 0", false) end
return {
    id = "_toa_akkha_visual",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = { "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99" },
    run = function(t)
        t.cheat("::toa 7", false)
        t.ticks(4)
        t.player.click_loc("akkha_teleport", 2)
        t.ticks(6)
        keep_alive(t)
        t.shot("start")
        for k = 1, 70 do
            t.ticks(2)
            keep_alive(t)
            if k >= 16 then t.shot(string.format("t%03d", k * 2)) end
        end
        t.cheat("::toabossdone", false)
        t.ticks(3)
        t.player.talk_to("toa_osmumten_akkha", 1)
        t.ticks(2)
        t.shot("osmumten-chat")
        t.ticks(3)
        t.shot("osmumten-chat-later")
    end,
}
