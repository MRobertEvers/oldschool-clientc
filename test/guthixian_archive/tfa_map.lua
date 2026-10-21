-- One-off: print the Guardian room's blocked tiles (::tfamap) from the static map.
return {
    id = "tfa_map",
    fixture = "fresh_lumbridge.ini",
    max_frames = 20000,
    setup = { "::goto 4126 5573 0" },
    run = function(t)
        t.ticks(4)
        t.cheat("::tfamap 4108 5565 4146 5625", false)
        t.ticks(4)
        t.expect("tfa_map", "ok", "printed")
    end,
}
