-- A dropped link and the reconnect after it (pm open finding E7).
--
-- Run with TORIRS_NET_DROP_MS=2000 (app_net.c: the link is severed that long
-- after the session's first packet, the headless ::clientdrop). The client
-- re-establishes with a reconnect login presenting its session key; the
-- embedded server must take it back into its slot, as a server does, and the
-- player is still in the game where it stood. Before the fix the embedded
-- server was stopped with the link and a fresh one refused every reconnect:
-- 'reconnect for <name> has no session to reclaim; refusing'. Without the
-- knob it is a plain idle and passes.
return {
    id = "reconnect_after_drop",
    fixture = "fresh_lumbridge.ini",
    setup = {},
    run = function(t)
        t.ticks(3)
        t.exec("goto", t.player.goto_tile, 3212, 3246, 0)
        local before = t.session._tile_text()
        t.ticks(150)
        local _, name = t.session.screen()
        t.check("still.in.game", name == "game", "screen " .. tostring(name))
        local after = t.session._tile_text()
        t.check("same.tile", after == before, tostring(before) .. " -> " .. tostring(after))
        t.ticks(3)
    end,
}
