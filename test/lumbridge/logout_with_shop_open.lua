-- Logging out with the general store open (pm open finding E11).
--
-- A shop opens its side half (shopside) in the gameframe's side modal, and
-- while that has a sub every tab stone is hidden: a player has to close the
-- shop before there is a logout stone to press. t.session.logout() used to
-- press the tab under the shop and wait for a logout button that never
-- showed ('not_visible session.logout: logout:logout ... was never displayed
-- in 360 frame(s) after the tab press'); it now closes the modal first, as a
-- player does. Then a login, so the relog after it is checked too.
local function said(r, d) return tostring(r) .. " " .. tostring(d) end
return {
    id = "logout_with_shop_open",
    fixture = "fresh_lumbridge.ini",
    setup = {},
    run = function(t)
        t.ticks(3)
        t.exec("goto-store", t.player.goto_tile, 3212, 3246, 0)
        t.ticks(2)
        t.exec("shop_open", t.shop.open, "generalshopkeeper1", 3, "generalshop1")
        local r, d = t.session.logout()
        t.check("logout.shop", r == "ok", said(r, d))
        r, d = t.session.login()
        t.check("login.after", r == "ok", said(r, d))
        t.ticks(3)
    end,
}
