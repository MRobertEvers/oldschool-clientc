-- defend_timing: when the target's defend animation and the attacker's block are sent, for the
-- tick log. Recorded rule (docs/rsprox-analysis/combat/defend_timing.md): the npc's defend goes
-- out on the ATTACK tick, delayed to the projectile's end (0 for melee); the man's own melee
-- swing sends the player's block on that swing tick. One man each for a scimitar, a shortbow
-- and Fire Blast; read with tools/rsprox/ticklog_tells.py.
local function wait(t, n)
    local left = n
    while left > 0 do
        local step = math.min(left, 5)
        t.ticks(step)
        t.cheat("::boost hitpoints 255 0", false)
        left = left - step
    end
end

return {
    id = "defend_timing",
    fixture = "fresh_lumbridge.ini",
    max_frames = 40000,
    setup = {
        "::clearinv", "::setlevel attack 60", "::setlevel strength 60", "::setlevel defence 1",
        "::setlevel ranged 60", "::setlevel magic 60", "::setlevel hitpoints 99",
        "::give rune_scimitar", "::give shortbow", "::give bronze_arrow 200",
        "::give airrune 200", "::give firerune 100", "::give deathrune 40",
        "::goto 3222 3219 0",
    },
    run = function(t)
        t.ticklog.start()
        t.exec("equip.scimitar", t.player.equip, "rune_scimitar")
        t.cheat("::spawn man", false)
        t.ticks(2)
        t.ticklog.mark("melee")
        t.player.attack("man", 2, 30)
        wait(t, 20)
        t.exec("equip.bow", t.player.equip, "shortbow")
        t.exec("equip.arrows", t.player.equip, "bronze_arrow")
        t.cheat("::spawn man", false)
        t.ticks(2)
        t.ticklog.mark("ranged")
        t.player.attack("man", 2, 30)
        wait(t, 20)
        t.exec("unequip.bow", t.player.unequip, "shortbow")
        t.cheat("::spawn man", false)
        t.ticks(2)
        t.ticklog.mark("magic")
        t.player.cast("fire_blast", "man", 14)
        wait(t, 12)
        t.expect("defend.run", "ok", "done")
    end,
}
