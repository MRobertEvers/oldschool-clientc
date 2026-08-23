-- Development probe: the semantic role verbs, on whatever lane it is booted on.
-- Not shipped in plugins.ini; loaded on demand via TORIRS_PLUGIN_MANIFEST.
--
-- The point of this file is that it contains no ids and no revision test. It
-- names four elements and draws a box round each one it finds, and the same
-- source works on a 2004 dat1 gameframe (where the report button is a client
-- builtin with no component id at all) and on an OldSchool CS2 toplevel (where
-- it is component 31 of interface 162). If a role is absent the probe says so
-- and draws nothing -- which is what a plugin that has not been told should do,
-- rather than guessing a rectangle.
---@type torirs.Plugin
local plugin = {
    name = "roleprobe",
    config = {
        { key = "tint", type = "color", label = "Tint", default = "#20C0FF" },
        -- Off by default: pressing Report abuse opens a real report dialog, so
        -- the one verb with a side effect is the one you have to ask for.
        { key = "press_report", type = "bool", label = "Press report on start", default = "false" },
    },
}

-- Bound once each, not spelled at the call sites. @see torirs.Role.
local WATCHED = { "safe", "viewport", "minimap", "report_button", "logout_screen" }
local roles = {}

local draws = 0

function plugin.on_start(api)
    -- Bound here, but NOT asked here. on_start runs before the gameframe is
    -- built, so every role would answer "not here" and a plugin that latched
    -- the answer would be wrong for the rest of the session. Binding the name
    -- is free; resolving it is what has to happen late, and every call below
    -- does it afresh.
    for _, name in ipairs(WATCHED) do
        roles[name] = api.role(name)
    end
    api.log("ROLEPROBE start (", #WATCHED, " roles bound; asking once the frame is up)")
end

local function report(api)
    for _, name in ipairs(WATCHED) do
        local r = roles[name]
        local box = r.rect()
        if box then
            api.log(
                "ROLEPROBE  ", name, "-> ", box.x, box.y, box.w, box.h,
                "visible=", r.visible() and "yes" or "no",
                "id=", r.id() or "none")
        else
            -- An answer, not a fault: this revision has no such element, or
            -- the interface carrying it is not open yet.
            api.log("ROLEPROBE  ", name, "-> not here")
        end
    end

    if api.config.press_report then
        -- op 2 is "Report abuse" on an OldSchool chat strip; a 2004 chat button
        -- is unnumbered, so 0 is the press there. Asking for both is how one
        -- source covers both, and a press that finds nothing simply returns
        -- false.
        local pressed = roles.report_button.click(2) or roles.report_button.click(0)
        api.log("ROLEPROBE report press -> ", pressed and "dispatched" or "no such element")
    end
end

local canvas_draws = 0

function plugin.on_frame(api, ev)
    -- Reporting rides the FRAME and not the canvas draw, because it must
    -- happen on every lane whether or not this plugin ends up with a canvas
    -- surface to draw on.
    draws = draws + 1
    -- Twice: once before anything is touched and once after, so a role whose
    -- visibility follows client state (a sidebar panel follows the selected
    -- tab) is seen to CHANGE rather than just to have some value.
    if draws == 20 or draws == 80 then
        api.log("ROLEPROBE report at frame ", draws, " (canvas draws ", canvas_draws, ")")
        report(api)
    end
end

function plugin.on_draw_canvas(api, draw)
    canvas_draws = canvas_draws + 1
    -- One outline per role that resolves and is on screen. Anything absent is
    -- simply not drawn, so the picture is the answer.
    for _, name in ipairs(WATCHED) do
        local r = roles[name]
        local box = r.rect()
        if box and r.visible() then
            draw.rect(box.x, box.y, box.w, box.h, api.config.tint, 0)
        end
    end

end

return plugin
