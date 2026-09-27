-- quest-driver / session: leave the world and come back, through the client's
-- own screens.  Seam 18 D (driver-logout-relogin-verb, 2026-09-26).
--
-- Why a test needs it: a guide step that says "or log out" (Prying Times'
-- killTheTroll: "Kill the Drink Troll, or log out") is a player action like
-- any other, and so is anything content hangs off [logout,_] / [login] --
-- temporary npcs that despawn with the session, `scope=temp` varps that a
-- relog zeroes, instance exits.  Before this file no verb could do it: the
-- driver had no word for logout or relog at all.
--
-- THE PATH IS THE PLAYER'S, both ways.
--
--   t.session.logout()  opens the logout side tab (the lane's own tab map,
--       `logout=10` in revconfig/osrs239/osrs239_dat2_cache.ini) and presses
--       `logout:logout` (182:8, "Click here to logout") as op 1 through
--       api_drive.if_click -- the dispatcher a real click reaches.  The
--       server answers with `[if_button,logout:logout] p_logout;`
--       (interface_logout/scripts/logout.rs2); the client's app_logout_tick
--       reads the closed session and App_Logout puts the title screen up.
--       The verb settles on the SCREEN (api.core.screen(): 30 game -> 10
--       title) and says how many frames that took, so a logout that only
--       happened because the client's own 5 s fallback fired (content
--       refused, combat) reads as such: that fallback is 250 logic cycles,
--       and the server's answer lands within a tick or two.
--
--   t.session.login(user, password)  does what a person at the login screen
--       does.  App_Logout empties both fields and puts the MAIN MENU up
--       (the reference's own logout), so: wait for the title tree to finish
--       its bake, press "Existing User" (btn_existing_user), type the name,
--       Enter (username -> password, rs_title.c RS_Title_HandleKey), type the
--       password, Enter (submit).  The client dials through the same
--       app_title_tick -> ToriRS_Network_ConnectLogin path the boot's
--       prefill used, the embedded server loads the save its own logout
--       just wrote (TORIRSSERVER_SAVES, run.py's per-session copy), and the
--       verb settles on the screen reaching `game` AND the world answering
--       a player tile again.
--
--   t.session.relog()  is the two in a row -- the "or log out" step.
--
-- The run survives it: the quest coroutine, the ledger, the shot counter and
-- every QD table live in the plugin's Lua state, which a logout does not
-- touch (the plugin host keeps running plugins on the title screen; only
-- gameframe-bound API answers change).  The DRIVER's clock does not: an await
-- deadline is counted in the client world's logic cycles, and nothing
-- promises they advance while the title screen is up, so every wait in this
-- file counts its OWN polls (one per frame, `pump` runs at on_frame_start)
-- and gives up on that count -- a stalled cycle counter cannot hang it.
--
-- WHO LOGS IN.  run.py launches every client with `--user <name> --pass
-- test` and makes the session directory build/quest_gate/<name>/ (its
-- launch_client / prepare_session), so the account name is the session
-- directory's last component -- api_drive.session().dir, the one piece of
-- run identity the driver can read.  `QD.session.PASSWORD` is run.py's
-- `--pass`.  Either can be passed explicitly.
--
-- Results: `ok` (detail names what happened and how long it took),
-- `refused` (wrong screen to start from, or a login the handshake refused
-- and bounced back to the title screen -- the detail names which), `timeout`
-- (the screen never moved; the detail names where it stopped), `not_visible`
-- (the logout button never displayed after its tab press), `not_found`
-- (logout:logout never resolved after its tab was opened).

-- api.core.screen()'s numbers: enum AppScreen, handed across unchanged
-- (torirs_plugin_bridge.u.c app_plugin_screen; torirs_plugin_host.c pins
-- them with static_asserts).
QD.session.SCREEN = { boot = 0, title = 10, connecting = 20, game = 30 }
QD.session.SCREEN_NAME = { [0] = "boot", [10] = "title", [20] = "connecting", [30] = "game" }

-- run.py launch_client's `--pass`.
QD.session.PASSWORD = "test"

-- One run frame is one 20 ms logic cycle (TORIRS_EMBED_CLOCK_MS=20) and a
-- server tick is 600 ms, so 30 polls ~ one tick.  Budgets below are polls.
QD.session.POLLS_PER_TICK = 30
-- Logout: the server answers within a tick or two; the client's own
-- fallback ends the session at 250 cycles (APP_LOGOUT_WAIT_CYCLES). 400
-- covers both with room, and the detail says which it was.
QD.session.LOGOUT_POLLS = 400
QD.session.LOGOUT_FALLBACK_POLLS = 250
-- The logout panel paints after its tab press (TAB_PAINT_TICKS' 12 ticks).
QD.session.LOGOUT_PAINT_POLLS = 12 * 30
-- No screen change this long after a displayed press = the press was lost.
QD.session.LOGOUT_REPRESS_POLLS = 90
-- Title bake after a logout, then a login handshake and the world bake.
QD.session.TITLE_READY_POLLS = 600
QD.session.TITLE_SETTLED_RUN = 5
QD.session.LOGIN_POLLS = 1500
QD.session.WORLD_POLLS = 900

-- "Existing User", the main menu's right-hand button: revconfig
-- osrs239_ui.ini `[layout:title]` node existing_btn at 389,271, size 147x41
-- (`[component:btn_existing_user]`).  The press point is inside it but clear
-- of everything the LOGIN FORM draws at the same pixels -- the form's
-- hide-username toggle (392,281 + label at 414,294) and its Cancel button
-- (389,301) sit under the lower half of this button, so a press that landed
-- a frame late on the form must not toggle or cancel anything.  The title
-- root is xalign=center in a 765-wide layout and run.py opens every client
-- at --window 765x503, so these are window pixels as-is.
QD.session.EXISTING_USER_AT = { x = 520, y = 275 }

function QD.session.screen()
    local number = api_core.screen()
    return "ok", QD.session.SCREEN_NAME[number] or ("screen " .. tostring(number)), number
end

-- Wait until `ready()` is true, polling once per frame, at most `polls`
-- times.  Returns (true, polls_spent) or (false, polls_spent).  The await's
-- own deadline is generous and never the one that ends this: the poll count
-- is (the banner's "a stalled cycle counter cannot hang it").
function QD.session._await_polls(ready, polls, note)
    local spent = 0
    local met = false
    await({
        level = function()
            if ready() then
                met = true
                return true
            end
            spent = spent + 1
            return spent >= polls
        end,
        note = note,
    }, math.floor(polls / QD.session.POLLS_PER_TICK) + 20)
    if not met and ready() then
        met = true
    end
    return met, spent
end

function QD.session._screen_is(number)
    return function() return api_core.screen() == number end
end

function QD.session._user()
    local session = api_drive.session()
    local dir = session and session.dir or ""
    local user = string.match(dir, "([^/\\]+)[/\\]*$")
    return user
end

function QD.session._tile_text()
    local result, tile = api_drive.player_tile()
    if result ~= "ok" or type(tile) ~= "table" then
        return nil
    end
    return string.format("%d,%d,%d", tile.x, tile.z, tile.level)
end

-- t.session.logout() -> ok | refused | not_found | timeout
function QD.session.logout()
    local number = api_core.screen()
    if number ~= QD.session.SCREEN.game then
        return "refused", "session.logout: not in the world (screen "
            .. (QD.session.SCREEN_NAME[number] or tostring(number)) .. ")"
    end
    local from = QD.session._tile_text() or "?"
    local tab_result, tab_detail = QD.ui.tab("logout")
    if tab_result ~= "ok" then
        return tab_result, "session.logout: the logout tab did not open: " .. tostring(tab_detail)
    end
    -- The panel paints a frame or more after the tab press, and a press on
    -- a node that is not DISPLAYED yet is dropped with no packet while
    -- api_drive.if_click still answers ok (pointer.lua's _worn_press banner:
    -- app_minimenu_run_option's pick_live check).  Measured here too:
    -- s18d_relog1 pressed logout:logout the frame the tab opened and sat on
    -- the game screen for 400 frames.  So wait for widget_presented first.
    local component
    local shown, shown_polls = QD.session._await_polls(function()
        local result, id = api_drive.component("logout:logout")
        if result ~= "ok" then
            return false
        end
        component = id
        local presented_result, presented = api_drive.widget_presented(id)
        return presented_result == "ok" and presented == true
    end, QD.session.LOGOUT_PAINT_POLLS, "session.logout paint")
    if not shown then
        if component == nil then
            return "not_found", "session.logout: logout:logout never resolved after the logout tab opened"
        end
        return "not_visible", string.format(
            "session.logout: logout:logout (component %d) was never displayed in %d frame(s) after the tab press",
            component, shown_polls)
    end
    local click_result, click_detail = api_drive.if_click(component, 1)
    if click_result ~= "ok" then
        return click_result, "session.logout: the press on logout:logout answered "
            .. tostring(click_result) .. " " .. tostring(click_detail)
    end
    local left, polls = QD.session._await_polls(
        QD.session._screen_is(QD.session.SCREEN.title),
        QD.session.LOGOUT_REPRESS_POLLS, "session.logout -> title")
    local repressed = ""
    if not left and api_core.screen() == QD.session.SCREEN.game then
        -- Nothing moved in three ticks: the server's answer lands within one
        -- or two.  A second press is harmless -- app_logout_tick never
        -- restarts a wait that is already armed -- and the detail says it
        -- happened.
        api_drive.if_click(component, 1)
        local more
        left, more = QD.session._await_polls(
            QD.session._screen_is(QD.session.SCREEN.title),
            QD.session.LOGOUT_POLLS, "session.logout -> title (second press)")
        polls = polls + more
        repressed = " [pressed twice: nothing moved for "
            .. QD.session.LOGOUT_REPRESS_POLLS .. " frames after the first]"
    end
    if not left then
        local _, name = QD.session.screen()
        return "timeout", string.format(
            "session.logout: pressed logout:logout at %s; still on the %s screen after %d frame(s)%s",
            from, name, polls, repressed)
    end
    local how = polls < QD.session.LOGOUT_FALLBACK_POLLS
        and "the server closed the session"
        or "the client's own 5 s fallback ended it (the server never answered)"
    return "ok", string.format(
        "logged out from %s via logout:logout (displayed after %d frame(s)); title screen after %d frame(s) -- %s%s",
        from, shown_polls, polls, how, repressed)
end

-- t.session.login(user, password) -> ok | refused | timeout
function QD.session.login(user, password)
    user = user or QD.session._user()
    password = password or QD.session.PASSWORD
    if not user or user == "" then
        return "refused", "session.login: no account name (session dir "
            .. tostring((api_drive.session() or {}).dir) .. ")"
    end
    local number = api_core.screen()
    if number ~= QD.session.SCREEN.title then
        return "refused", "session.login: not on the title screen (screen "
            .. (QD.session.SCREEN_NAME[number] or tostring(number)) .. ")"
    end
    -- The title tree bakes after App_Logout; a press before it is laid out
    -- lands on nothing.  Ready = title screen AND the frame settled for a
    -- few consecutive polls (api_drive.settled: no async work, frame
    -- settled, no world load in flight).
    local settled_run = 0
    local ready, ready_polls = QD.session._await_polls(function()
        if api_core.screen() ~= QD.session.SCREEN.title then
            settled_run = 0
            return false
        end
        if api_drive.settled() then
            settled_run = settled_run + 1
        else
            settled_run = 0
        end
        return settled_run >= QD.session.TITLE_SETTLED_RUN
    end, QD.session.TITLE_READY_POLLS, "session.login title ready")
    if not ready then
        return "timeout", string.format(
            "session.login: the title screen never settled in %d frame(s)", ready_polls)
    end
    local at = QD.session.EXISTING_USER_AT
    api_drive.mouse_move(at.x, at.y)
    api_drive.mouse_button("left", 1, at.x, at.y)
    api_drive.mouse_button("left", 0, at.x, at.y)
    -- The click is consumed on the next frame's input pass; the form is
    -- up from then on.  Two frames, then type.
    QD.session._await_polls(function() return false end, 2, "session.login existing user")
    local typed = api_drive.text(user)
    if typed ~= "ok" then
        return typed, "session.login: typing the account name answered " .. tostring(typed)
    end
    QD.key("enter")
    typed = api_drive.text(password)
    if typed ~= "ok" then
        return typed, "session.login: typing the password answered " .. tostring(typed)
    end
    QD.key("enter")
    -- Submit arms a dial (screen -> connecting) and the handshake ends on
    -- the gameframe (screen -> game).
    -- A refused handshake goes connecting -> title again and puts the
    -- server's reply on the form ("Unexpected server response", "Try
    -- again"); nothing will change after that, so it ends the wait at once
    -- rather than spending the whole budget.
    local dialled = false
    local bounced = false
    local entered, login_polls = QD.session._await_polls(function()
        local now = api_core.screen()
        if now == QD.session.SCREEN.connecting then
            dialled = true
        elseif dialled and now == QD.session.SCREEN.title then
            bounced = true
            return true
        end
        return now == QD.session.SCREEN.game
    end, QD.session.LOGIN_POLLS, "session.login -> game")
    if bounced then
        return "refused", string.format(
            "session.login: pressed Existing User at %d,%d, typed '%s' + password, Enter; the "
            .. "handshake dialled and fell back to the title screen after %d frame(s) -- the login "
            .. "was refused (the form shows the reply; client.log has 'login: rejected reply=N')",
            at.x, at.y, user, login_polls)
    end
    if not entered then
        local _, name = QD.session.screen()
        return "timeout", string.format(
            "session.login: pressed Existing User at %d,%d, typed '%s' + password, Enter; "
            .. "still on the %s screen after %d frame(s)", at.x, at.y, user, name, login_polls)
    end
    -- The gameframe is up before the world is: wait for the player's own
    -- tile to answer and the frame to settle, which is what every later
    -- verb reads.
    local tile
    local world, world_polls = QD.session._await_polls(function()
        tile = QD.session._tile_text()
        return tile ~= nil and api_drive.settled()
    end, QD.session.WORLD_POLLS, "session.login world")
    if not world then
        return "timeout", string.format(
            "session.login: in game after %d frame(s) but the world never answered a player tile "
            .. "in %d more", login_polls, world_polls)
    end
    -- The client keeps the side tab the player left it on -- the logout tab,
    -- which is where this verb's own logout pressed -- and every held-item
    -- verb presses backpack cells (QUEST_AUTHORING's use_on-on-another-tab
    -- trap), so the backpack goes back up, the way t.player.emote leaves it.
    local backpack = QD.player._show_backpack_painted()
    return "ok", string.format(
        "logged in as %s through the title screen (Existing User, typed, Enter); "
        .. "title settled in %d, game in %d, world in %d frame(s); at %s; backpack tab %s",
        user, ready_polls, login_polls, world_polls, tile, tostring(backpack))
end

-- t.session.relog() -> ok | the failing half's own answer
function QD.session.relog(user, password)
    local out_result, out_detail = QD.session.logout()
    if out_result ~= "ok" then
        return out_result, out_detail
    end
    local in_result, in_detail = QD.session.login(user, password)
    if in_result ~= "ok" then
        return in_result, out_detail .. "; then " .. tostring(in_detail)
    end
    return "ok", out_detail .. "; then " .. in_detail
end

-- The player-verb spelling the task names: t.player.logout / t.player.login.
QD.player.logout = QD.session.logout
QD.player.login = QD.session.login
