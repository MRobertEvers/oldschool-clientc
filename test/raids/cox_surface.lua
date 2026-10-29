-- cox_surface: Mount Quidamortem, every npc and loc on the summit, the way
-- RSProx shows them (docs/rsprox-analysis/cox/quidamortem.md):
--   Captain Rimor (Talk-to, Claim-cape), the bank chest, the deposit box, the
--   storage unit, the Challenge Mode score board, the Recruiting board (Make
--   party, Scaling, layout, Advertise, Back, My party, Disband), the steps
--   (the once-only warning, "Inviting party...", in, out, and in again with no
--   warning), the Mountain Guide (the unlock, a trip to the Shayzien outpost
--   and back), Stuliette, Gnosi and the Natural Historian.
--
-- GREEN: every check below. One raider.
--
--   ./src/build_<you>/torirsserver --scriptrun test/raids/cox_surface.lua --bots 1 \
--       --name su1 --ticks 3000 --session /tmp/surface --fixture test/raids/fixtures/fresh_lumbridge.ini
--
-- The board's buttons are cc children its clientscripts build. Scriptrun has no
-- clientscript VM, so there a press is the RESUME_PAUSEBUTTON the child would
-- send (api_drive.resume(contents, sub)); on the live lane it is the button's
-- own static component (enum_1538 / enum_1539 name them) clicked.

local LIST = "raids_lobby_partylist"
local DETAILS = "raids_lobby_partydetails"

return {
    id = "cox_surface",
    fixture = "fresh_lumbridge.ini",
    frames = 40000,
    setup = { "::maxstats", "::give coins 1000", "::tele 0_19_55_18_50" },
    run = function(t)
        local function close()
            t.ui.close_modal()
            t.ticks(1)
        end
        local function sv(name)
            local r, v = t.var.server(name)
            return r == "ok" and v or nil
        end
        -- one board button: its enum sub id under `contents` (scriptrun), or the
        -- static button the player clicks (live)
        local function press(iface, button, sub)
            local r = t.ui.press_pause(iface .. ":contents", sub, iface .. ":" .. button)
            t.ticks(2)
            return r
        end

        t.check("surface.ticklog", t.ticklog.start() == "ok", "")
        t.ticks(2)

        -- Captain Rimor
        local tr = t.player.talk_to("raids_temple_captain", 1)
        local cr, cd = t.chat.play({ "npc:Beware the Chambers of Xeric, adventurer.", "npc:These dungeons",
            "npc:Remember that you may be kicked out", "npc:Do not allow your curiosity",
            "player:Thanks for the warning." })
        t.check("rimor.talk", cr == "ok", tostring(tr) .. " " .. tostring(cd))
        tr = t.player.talk_to("raids_temple_captain", 3)
        cr, cd = t.chat.play({ "player:Can I have a cape like yours?", "npc:Complete one hundred" })
        t.check("rimor.cape_refused", cr == "ok", tostring(tr) .. " " .. tostring(cd))
        t.cheat("::setvar varp1735_total_completed_xericchambers_challenge 600")
        t.ticks(2)
        tr = t.player.talk_to("raids_temple_captain", 3)
        cr, cd = t.chat.play({ "player:Can I have a cape like yours?", "npc:Of course, you have earned it.",
            "options", "choose:Take Xeric's warrior.", "*", "player:Thanks!" })
        t.check("rimor.cape", cr == "ok" and t.inv.has("cox_challenge_cape_t2"), tostring(tr) .. " " .. tostring(cd))

        -- the bank tent: chest, deposit box, storage unit
        local br, bd = t.bank.open("tox_bank_chest", 1)
        t.check("bank.chest", br == "ok", tostring(bd))
        t.bank.close()
        t.player.click_loc("bank_deposit_box", 1)
        t.check("bank.depositbox", t.ui.await_open("bank_depositbox", 20) == "ok", "")
        close()
        t.player.click_loc("raids_storage_lobby", 2)
        t.check("storage.open", t.ui.await_open("raids_storage_private", 20) == "ok", "")
        t.check("storage.outside", sv("varb5432_raids_client_indungeon") == 0, tostring(sv("varb5432_raids_client_indungeon")))
        close()

        -- the Challenge Mode score board
        t.player.click_loc("raids_challenge_scores", 1)
        t.check("scores.open", t.ui.await_open("raids_challenge", 20) == "ok", "")
        close()

        -- the Recruiting board: a party, its settings, listed, then back
        t.player.click_loc("raids_party_recruitment", 1)
        t.check("board.list", t.ui.await_open(LIST, 20) == "ok", "")
        t.check("board.no_party", sv("varp1427_raids_party_groupholder") == -1, tostring(sv("varp1427_raids_party_groupholder")))
        press(LIST, "myparty", 1)
        t.check("board.made", t.ui.await_open(DETAILS, 10) == "ok", "")
        local party = sv("varp1427_raids_party_groupholder")
        t.check("board.party", party ~= nil and party > 0, tostring(party))
        -- Scaling's prompt is the chatbox count prompt, which scriptrun does not
        -- model (its meslayer_mode is always 0): pressed on the live lane only.
        if not t.raid.tob_headless() then
            press(DETAILS, "scaling_layer", 8)
            cr, cd = t.chat.play({ "count:5" })
            t.ticks(2)
            t.check("board.scaling", cr == "ok" and sv("varb9539_raids_scaling") == 5
                and sv("varb9541_raids_remember_last_scaling") == 5, tostring(cd))
        end
        press(DETAILS, "mappool_layer", 10)
        cr, cd = t.chat.play({ "options", "choose:Full" })
        t.ticks(2)
        t.check("board.layout", cr == "ok" and sv("varb5408_raids_map_pool_selected") == 2, tostring(cd))
        press(DETAILS, "advertise", 1)
        press(DETAILS, "back", 4)
        t.check("board.back", t.ui.await_open(LIST, 10) == "ok", "")
        close()

        -- the steps: the warning once, in as the party's leader, out, in again
        t.player.click_loc("raids_entrance_steps", 1)
        cr, cd = t.chat.play({ "mesbox:WARNING!", "options", "choose:Accept warning and proceed." })
        t.check("steps.warning", cr == "ok", tostring(cd))
        t.check("steps.inviting", t.msg.expect("Inviting party...") == "ok", "")
        t.ticks(3)
        local hr, here = t.raid.cox_here()
        t.check("steps.in", hr == "ok" and here.name == "raid_start", tostring(hr))
        -- (varb27 lives on varp 1428, which is never transmitted -- Jagex does not
        -- send it either -- so the acceptance is read off the second entry below.)
        t.player.click_loc("raids_exit_steps_reload", 1)
        cr, cd = t.chat.play({ "options", "choose:Leave the Chambers." })
        t.ticks(3)
        t.check("steps.out", cr == "ok" and sv("varp6587_cox_active") == 0, tostring(cd))
        t.player.click_loc("raids_entrance_steps", 1)
        t.ticks(14)
        hr, here = t.raid.cox_here()
        t.check("steps.again", hr == "ok" and here.name == "raid_start", "no warning the second time")
        t.player.click_loc("raids_exit_steps_reload", 1)
        t.chat.play({ "options", "choose:Leave the Chambers." })
        t.ticks(3)

        -- My party, then Disband
        t.player.click_loc("raids_party_recruitment", 1)
        t.ui.await_open(LIST, 20)
        press(LIST, "myparty", 1)
        t.check("board.mine", t.ui.await_open(DETAILS, 10) == "ok", "")
        press(DETAILS, "disband", 2)
        t.check("board.disbanded", sv("varp1427_raids_party_groupholder") == -1
            and t.ui.await_open(LIST, 10) == "ok", tostring(sv("varp1427_raids_party_groupholder")))
        close()

        -- the expedition, by the temple
        t.player.goto_tile(1272, 3562, 0)
        tr = t.player.talk_to("raids_temple_gnosi", 1)
        cr, cd = t.chat.play({ "player:Hello. What do you do here?", "npc:My name is Gnosi.", "player:*", "npc:*",
            "npc:*", "npc:*", "options", "choose:Who is Xeric?", "player:Who is Xeric?", "npc:*", "npc:*",
            "options", "choose:Goodbye.", "player:Goodbye." })
        t.check("gnosi.talk", cr == "ok", tostring(tr) .. " " .. tostring(cd))
        tr = t.player.talk_to("raids_temple_historian", 1)
        cr, cd = t.chat.play({ "player:Hello.", "npc:*" })
        t.check("historian.talk", cr == "ok", tostring(tr) .. " " .. tostring(cd))

        -- the Mountain Guide: the unlock, then to the outpost and back
        tr = t.player.talk_to("raids_mountainguide_temple_multi", 1)
        cr, cd = t.chat.play({ "player:Hello. What you are doing here?", "npc:*", "player:*", "npc:*", "npc:*",
            "player:Thanks!" })
        t.check("guide.unlock", cr == "ok" and sv("varb5421_raids_guide_travel_unlock") == 1, tostring(tr) .. " " .. tostring(cd))
        tr = t.player.talk_to("raids_mountainguide_temple_multi", 1)
        -- the trip: no spoken answer, a fade, the teleport two ticks after the click (RSProx index 4425 [1615..1618])
        cr, cd = t.chat.play({ "player:Can you take me somewhere?", "npc:Of course. Where would you like to go?",
            "options", "choose:The Shayzien Outpost." })
        t.ticks(5)
        local _, tile = t.world.tile()
        t.check("guide.outpost", cr == "ok" and tile ~= nil and math.abs(tile.x - 1401) <= 3 and math.abs(tile.z - 3536) <= 3,
            tostring(cd) .. " at " .. (tile and (tile.x .. "," .. tile.z) or "?"))
        tr = t.player.talk_to("raids_mountainguide_wall_multi", 3)
        cr, cd = t.chat.play({ "options", "choose:Mount Quidamortem." })
        t.ticks(5)
        _, tile = t.world.tile()
        t.check("guide.back", cr == "ok" and tile ~= nil and math.abs(tile.x - 1275) <= 3 and math.abs(tile.z - 3559) <= 3,
            tostring(tr) .. " " .. tostring(cd) .. " at " .. (tile and (tile.x .. "," .. tile.z) or "?"))

        -- Stuliette, at the minecart
        t.player.goto_tile(1258, 3550, 0)
        tr = t.player.talk_to("lovakengj_minecart_quidamortem", 1)
        cr, cd = t.chat.play({ "npc:Can I help you today?", "options", "choose:I'm fine, thanks.", "player:I'm fine, thanks." })
        t.check("stuliette.talk", cr == "ok", tostring(tr) .. " " .. tostring(cd))
        tr = t.player.talk_to("lovakengj_minecart_quidamortem", 3)
        cr, cd = t.chat.play({ "options", "choose:More destinations...", "options", "choose:Mount Quidamortem.",
            "npc:You're already here, human." })
        t.check("stuliette.here", cr == "ok", tostring(tr) .. " " .. tostring(cd))

        t.finish(0)
    end,
}
