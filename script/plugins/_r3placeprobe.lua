-- Placement probe: where does a described control actually LAND against the
-- element it names, on a lane that runs a frame provider?
--
-- The target has to be inside a subtree a provider rebuilds or the probe
-- passes on a tree that is broken: a chat-button REPLACE is byte-identical to
-- baseline on all six lanes because no provider re-places it. So the list
-- below is wide on purpose -- the orb block and its members, the minimap and
-- compass that sit in the same block, the chat bar, the sidebar and one tab.
--
-- Printed per lane: the element's drawn box, its parent-local box and its
-- incarnation. Each control's own drawn box comes out of the capture's
-- OWNED_WIDGET dump keyed by the key below. A correct REPLACE or AT_ELEMENT
-- puts the control's drawn box exactly on the target's drawn box.
local plugin = { id = "r3placeprobe", version = "1" }

local TARGETS = {
    "orbs", "orb:run", "orb:hitpoints", "minimap", "compass",
    "chat_bar", "chat_input", "sidebar", "report_button", "viewport",
}
local frames = 0

local function describe(d)
    for index, target in ipairs(TARGETS) do
        d.control({
            key = string.format("r3rep%02d", index),
            place = { kind = "replace", on = target },
            w = 6, h = 6, hit = false,
        })
        d.control({
            key = string.format("r3ael%02d", index),
            place = { kind = "at_element", on = target },
            w = 6, h = 6, hit = false,
        })
        d.control({
            key = string.format("r3ins%02d", index),
            place = { kind = "inside", on = target, corner = "bottom_right" },
            w = 6, h = 6, hit = false,
        })
    end
end

function plugin.on_start(api)
    api.porcelain.open()
    api.porcelain.describe(describe)
end

local function report(api, at)
    for index, target in ipairs(TARGETS) do
        local state = api.porcelain.element(target)
        api.core.log(string.format(
            "R3PLACE at=%s key=%02d target=%s bind=%s box=%d,%d,%d,%d local=%d,%d,%d,%d inc=%d",
            at, index, target, state.bind,
            state.box.x, state.box.y, state.box.width, state.box.height,
            state.local_box.x, state.local_box.y,
            state.local_box.width, state.local_box.height,
            state.incarnation))
    end
end

function plugin.on_frame_start(api)
    frames = frames + 1
    api.porcelain.fence()
    api.porcelain.commit()
    if frames == 120 or frames == 300 or frames == 600 then
        report(api, "frame" .. frames)
    end
end

return plugin
