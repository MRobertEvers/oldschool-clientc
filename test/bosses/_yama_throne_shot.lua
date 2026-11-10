-- _yama_throne_shot: a probe, not a gate. Into a domain by the Voice's Travel, up to the
-- throne, and a photograph of Yama sitting on it (the owner's 2026-11-10 report: the
-- throne drawn over him). Run on the live lane through tools/quest_gate/run.py.
return {
    id = "_yama_throne_shot",
    fixture = "fresh_lumbridge.ini",
    max_frames = 20000,
    setup = {
        "::resetcharacter", "::setlevel hitpoints 99", "::god",
        "::setvar varb16284_chasm_voice_progress 2",
        "::setvar varp9561_yama_contract_signed 1",
        "::goto 1442 10080 0",
    },
    run = function(t)
        t.ticks(2)
        t.exec("voice.travel", t.player.talk_to, "voice_of_yama", 3)
        t.ticks(8)
        local _, tile = t.world.tile()
        local bx, bz = tile.x - tile.x % 64, tile.z - tile.z % 64
        t.exec("walk.throne", t.player.goto_tile, bx + 29, bz + 36, 0)
        t.ticks(4)
        t.drive._shot_plan = function() return nil, nil, "fixed" end
        t.drive.camera(0, 383, 1400)
        t.ticks(2)
        t.shot("throne_front")
        t.drive.camera(1800, 330, 1300)
        t.ticks(2)
        t.shot("throne_side")
        -- The owner's report is after a kill: challenge, kill by cheat, then the throne
        -- npc re-added at K+5 with npc_yama_noncombat_spawn02.
        t.player.talk_to("yama_throne", 3)
        t.ticks(6)
        t.cheat("::yamakill", false)
        for i = 1, 6 do
            t.ticks(2)
            t.drive.camera(0, 383, 1400)
            t.shot("after_kill_" .. (i * 2))
        end
        t.ticks(20)
        t.shot("after_kill_32")
    end,
}
