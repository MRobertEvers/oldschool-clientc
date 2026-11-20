-- 200 sessions in one client (pm open finding E6), for a leak audit over relogs.
--
-- Run with TORIRS_MEM_DUMP_AT_LOGIN=1: ToriMem's group tree is printed at every
-- login, after the last session is torn down. Measured on pm-fx-core: every
-- client group is flat once full -- session at 118 blocks (the chat ring's cap,
-- reached by cycle 40), ui from cycle 80, plugins, net and assets from cycle 5;
-- the embedded server's world grows with its clock, the same with 4 relogs as
-- with 200.
local function said(r, d) return tostring(r) .. " " .. tostring(d) end
local function session(t, n)
    t.exec("goto-store." .. n, t.player.goto_tile, 3212, 3246, 0)
    t.ticks(2)
    t.exec("shop_open." .. n, t.shop.open, "generalshopkeeper1", 3, "generalshop1")
    local r, d = t.shop.close()
    t.check("shop_close." .. n, r == "ok", said(r, d))
    t.exec("goto-courtyard." .. n, t.player.goto_tile, 3222, 3218, 0)
    t.ticks(3)
    r, d = t.session.logout()
    t.check("logout." .. n, r == "ok", said(r, d))
end
return {
    id = "relog_200_memory",
    fixture = "fresh_lumbridge.ini",
    max_frames = 1200000,
    setup = {},
    run = function(t)
        t.ticks(3)
        for n = 1, 200 do
            session(t, n)
            local r, d = t.session.login()
            t.check("login." .. n, r == "ok", said(r, d))
            t.ticks(2)
        end
    end,
}
