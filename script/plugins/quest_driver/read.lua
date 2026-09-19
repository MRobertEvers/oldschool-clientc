-- quest-driver / read: what the page SAYS and what it SHOWS.
-- Owner: verbs-read (docs/ARCHITECT.md).
--
-- Text and presented-state reads go through api.widgets (plan 5.6: "No
-- engine change"): api.widgets.get(component_id) is the same reverse lookup
-- chat.lua's chat.options/options_title use, and :text()/:state().presented
-- answer everything below that is not an IDENTITY. The component id itself
-- comes from api.drive.component(symbol, sub) -- the qualified
-- "<iface>:<child>" content symbol (D11), never a numeric literal and never
-- a client op string.
--
-- Identity reads (chat.head, chat.item) go through api.drive.widget_model,
-- which returns the raw identity the server sent on
-- IF_SETNPCHEAD / IF_SETOBJECT / IF_SETMODEL. The composite scene model id
-- cannot answer "which npc is this" and must not be used.
--
-- This file, like quest_driver/chat.lua, reads `api_widgets` as a
-- chunk-local upvalue it does not itself declare: today only `api_drive` is
-- captured by quest_driver/core.lua's core_bind (core-scheduler's file).
-- The one-line addition (`api_widgets = api.widgets`) is reported in this
-- pass, not made here -- see the report.
--
-- Private helpers hang off QD.read as PLAIN FIELDS, never a top-level
-- `local`: every one of these eight files is concatenated into ONE chunk
-- before it compiles, so a `local` outside a function body is a register in
-- that single outer function, and only core.lua may spend one (docs/ARCHITECT.md
-- section 3, "Only core.lua declares chunk-scope locals"). QD.read is not
-- one of core.lua's declared public namespaces (this file's own verbs are
-- QD.chat.*/QD.scroll.*/QD.levelup.*) -- it is a private table this file
-- adds to QD for its own helpers, the same way chat.lua adds private fields
-- onto the QD.chat table it shares with verbs-chat.
--
-- expect_text strips <col=..>, </col>, <br>, <str>, <u> and <shad=..> here,
-- in Lua: the client has no tag parser, and a colour-wrapped line that fails
-- to match is the defect this stripping exists to prevent.  The negative case
-- is asserted explicitly -- `not_found`, never a silent `ok`.

QD.read = {}

-- The seven dialog_*_text roles (plan 1c), by their qualified content symbol.
QD.read._text_symbols = {
    "chat_left:text",
    "chat_right:text",
    "messagebox:text",
    "messagebox_titled:text",
    "messagebox_url:text",
    "objectbox:text",
    "objectbox_double:text",
}

QD.read._head_symbols = { "chat_left:head", "chat_right:head" }
QD.read._name_symbols = { "chat_left:name", "chat_right:name" }

-- levelup_display:<name> per plan 1c -- the 25 sibling skill containers
-- levelup.skill scans for the one the server did not hide.
QD.read._levelup_skills = {
    "agility", "attack", "construction", "cooking", "crafting", "defence",
    "farming", "firemaking", "fishing", "fletching", "herblore", "hitpoints",
    "hunter", "magic", "mining", "prayer", "ranged", "runecraft", "slayer",
    "smithing", "strength", "thieving", "woodcutting", "combat", "sailing",
}

-- symbol -> live component id, or nil (DriveUi_Component: DRIVE_NOT_FOUND is
-- a legitimate "this content pack has no such component" answer, not a
-- driver bug -- treated the same as "not mounted right now"). sub is -1
-- ("no sub index", lua_drive_component's own default, drive_ui.c:579) --
-- every symbol here names a leaf, and DriveUi_Component only does the child
-- lookup at all when sub >= 0 (drive_ui.c:78).
function QD.read._component_of(symbol)
    local result, component_id = api_drive.component(symbol, -1)
    if result ~= "ok" then
        return nil
    end
    return component_id
end

function QD.read._text_of(component_id)
    local widget = api_widgets.get(component_id)
    if not widget then
        return nil
    end
    return widget:text()
end

function QD.read._is_presented(component_id)
    local widget = api_widgets.get(component_id)
    if not widget then
        return false
    end
    local state = widget:state()
    return state ~= nil and state.presented == true
end

function QD.read._is_own_hidden(component_id)
    local widget = api_widgets.get(component_id)
    if not widget then
        return true
    end
    local state = widget:state()
    return state == nil or state.own_hidden == true
end

-- First of `symbols` (in order) that is currently mounted AND presented, as
-- text -- or nil while nothing in the set is showing. chat.text/name poll
-- this every level check; a dialogue side that has not mounted yet is
-- exactly the state the polling loop exists to step past.
function QD.read._presented_text(symbols)
    for _, sym in ipairs(symbols) do
        local component_id = QD.read._component_of(sym)
        if component_id and QD.read._is_presented(component_id) then
            return QD.read._text_of(component_id) or ""
        end
    end
    return nil
end

-- Same shape for an identity read: the first presented symbol's
-- widget_model, or nil.
function QD.read._presented_model(symbols)
    for _, sym in ipairs(symbols) do
        local component_id = QD.read._component_of(sym)
        if component_id and QD.read._is_presented(component_id) then
            local result, model = api_drive.widget_model(component_id)
            if result == "ok" then
                return model
            end
        end
    end
    return nil
end

-- objectbox: one entry. objectbox_double: two, slot 1 gates on its own
-- presented state (plan 5.6: "chat.item returns one entry for objectbox and
-- up to two for objectbox_double").
function QD.read._presented_items()
    local single_id = QD.read._component_of("objectbox:item")
    if single_id and QD.read._is_presented(single_id) then
        local result, model = api_drive.widget_model(single_id)
        if result == "ok" then
            return { { slot = 1, kind = model.kind, id = model.id } }
        end
    end

    local model1_id = QD.read._component_of("objectbox_double:model1")
    local model2_id = QD.read._component_of("objectbox_double:model2")
    if model1_id and model2_id and QD.read._is_presented(model1_id) then
        local r1, model1 = api_drive.widget_model(model1_id)
        local r2, model2 = api_drive.widget_model(model2_id)
        if r1 == "ok" and r2 == "ok" then
            return {
                { slot = 1, kind = model1.kind, id = model1.id },
                { slot = 2, kind = model2.kind, id = model2.id },
            }
        end
    end
    return nil
end

function QD.read._strip_tags(text)
    text = text:gsub("<col=[^>]*>", "")
    text = text:gsub("</col>", "")
    text = text:gsub("<br>", "")
    text = text:gsub("<str>", "")
    text = text:gsub("<u>", "")
    text = text:gsub("<shad=[^>]*>", "")
    return text
end

function QD.chat.text()
    local result, detail = await({
        event = "sub_mounted",
        level = function() return QD.read._presented_text(QD.read._text_symbols) ~= nil end,
        note = "chat.text",
    }, 10)
    if result ~= "ok" then
        return result, detail
    end
    local text = QD.read._presented_text(QD.read._text_symbols)
    if text == nil then
        return "no_row", "chat.text: presented text vanished before the read"
    end
    return "ok", text
end

function QD.chat.head()
    local result, detail = await({
        event = "sub_mounted",
        level = function() return QD.read._presented_model(QD.read._head_symbols) ~= nil end,
        note = "chat.head",
    }, 10)
    if result ~= "ok" then
        return result == "timeout" and "unsupported" or result, detail
    end
    local model = QD.read._presented_model(QD.read._head_symbols)
    if not model then
        return "unsupported", "chat.head: no head role presented"
    end
    return "ok", model
end

function QD.chat.name()
    local result, detail = await({
        event = "sub_mounted",
        level = function() return QD.read._presented_text(QD.read._name_symbols) ~= nil end,
        note = "chat.name",
    }, 10)
    if result ~= "ok" then
        return result == "timeout" and "unsupported" or result, detail
    end
    local text = QD.read._presented_text(QD.read._name_symbols)
    if text == nil then
        return "unsupported", "chat.name: no name role presented"
    end
    return "ok", text
end

function QD.chat.item()
    local result, detail = await({
        event = "sub_mounted",
        level = function() return QD.read._presented_items() ~= nil end,
        note = "chat.item",
    }, 10)
    if result ~= "ok" then
        return result == "timeout" and "unsupported" or result, detail
    end
    local items = QD.read._presented_items()
    if not items then
        return "unsupported", "chat.item: no item role presented"
    end
    return "ok", items
end

function QD.chat.expect_text(substring)
    local result, detail = await({
        event = "sub_mounted",
        level = function()
            local text = QD.read._presented_text(QD.read._text_symbols)
            return text ~= nil and QD.read._strip_tags(text):find(substring, 1, true) ~= nil
        end,
        note = "chat.expect_text(" .. tostring(substring) .. ")",
    }, 10)
    if result ~= "ok" then
        return "not_found", detail
    end
    return "ok", substring
end

function QD.chat.expect_head(npc)
    local symbol_result, npc_id = api_drive.symbol("npc", npc)
    if symbol_result ~= "ok" then
        return "not_found", "chat.expect_head: unknown npc " .. tostring(npc)
    end

    local result, detail = await({
        event = "sub_mounted",
        level = function()
            local model = QD.read._presented_model(QD.read._head_symbols)
            return model ~= nil and model.kind == "npc" and model.id == npc_id
        end,
        note = "chat.expect_head(" .. tostring(npc) .. ")",
    }, 10)
    if result == "ok" then
        return "ok", npc
    end
    -- A head IS showing, it just is not this npc: the mismatch is the
    -- answer, not the clock running out.
    if QD.read._presented_model(QD.read._head_symbols) ~= nil then
        return "not_found", detail
    end
    return result, detail
end

function QD.chat.expect_item(obj)
    local symbol_result, obj_id = api_drive.symbol("obj", obj)
    if symbol_result ~= "ok" then
        return "not_found", "chat.expect_item: unknown obj " .. tostring(obj)
    end

    local function matches()
        local items = QD.read._presented_items()
        if not items then
            return false
        end
        for _, item in ipairs(items) do
            if item.kind == "obj" and item.id == obj_id then
                return true
            end
        end
        return false
    end

    local result, detail = await({
        event = "sub_mounted",
        level = matches,
        note = "chat.expect_item(" .. tostring(obj) .. ")",
    }, 10)
    if result == "ok" then
        return "ok", obj
    end
    if QD.read._presented_items() ~= nil then
        return "not_found", detail
    end
    return result, detail
end

function QD.read._scroll_universe_presented()
    local component_id = QD.read._component_of("questscroll:universe")
    return component_id ~= nil and QD.read._is_presented(component_id)
end

function QD.scroll.title()
    local result, detail = await({
        event = "sub_mounted",
        level = QD.read._scroll_universe_presented,
        note = "scroll.title",
    }, 8)
    if result ~= "ok" then
        return result == "timeout" and "not_visible" or result, detail
    end

    local name_id = QD.read._component_of("questscroll:quest_title")
    local points_id = QD.read._component_of("questscroll:quest_points")
    return "ok", {
        name = (name_id and QD.read._text_of(name_id)) or "",
        points = (points_id and QD.read._text_of(points_id)) or "",
    }
end

function QD.scroll.rewards()
    local result, detail = await({
        event = "sub_mounted",
        level = QD.read._scroll_universe_presented,
        note = "scroll.rewards",
    }, 8)
    if result ~= "ok" then
        return result == "timeout" and "not_visible" or result, detail
    end

    local award_id = QD.read._component_of("questscroll:award_text")
    local lines = { (award_id and QD.read._text_of(award_id)) or "" }
    for i = 1, 7 do
        local reward_id = QD.read._component_of("questscroll:quest_reward" .. i)
        -- Blank reward rows come back as empty strings, not omitted (plan 5.6).
        lines[#lines + 1] = (reward_id and QD.read._text_of(reward_id)) or ""
    end

    local icon = { kind = "none", id = -1 }
    local icon_id = QD.read._component_of("questscroll:quest_model")
    if icon_id then
        local icon_result, icon_model = api_drive.widget_model(icon_id)
        if icon_result == "ok" then
            icon = icon_model
        end
    end
    return "ok", { lines = lines, icon = icon }
end

function QD.scroll.close()
    local close_id = QD.read._component_of("questscroll:close_button")
    if not close_id then
        return "no_row", "scroll.close: no close_button role"
    end

    -- questscroll:close_button carries a static op1 -- an ordinary numbered
    -- op button (add_menu_ops_rows -> k_inv_button_action -> IF_BUTTON),
    -- NOT a resume (plan 5.6).
    local click_result, click_detail = api_drive.if_click(close_id, 1)
    if click_result ~= "ok" then
        return click_result, click_detail
    end

    local result, detail = await({
        event = "sub_closed",
        level = function() return not QD.read._scroll_universe_presented() end,
        note = "scroll.close",
    }, 5)
    return result, detail
end

function QD.read._levelup_universe_presented()
    local component_id = QD.read._component_of("levelup_display:universe")
    return component_id ~= nil and QD.read._is_presented(component_id)
end

-- The box has no content opener anywhere in this tree today (plan U16), so
-- phase-1 gates this as a synthetic-tree unit test: a stored/derived tree
-- state that raises levelup_display:universe presented with exactly one of
-- the 25 skill containers unhidden. Re-gate against a real mount only if the
-- product decision is to open it.
function QD.levelup.skill()
    local result, detail = await({
        event = "sub_mounted",
        level = QD.read._levelup_universe_presented,
        note = "levelup.skill",
    }, 8)
    if result ~= "ok" then
        return result, detail
    end

    local skill_name = nil
    for _, name in ipairs(QD.read._levelup_skills) do
        local component_id = QD.read._component_of("levelup_display:" .. name)
        if component_id and not QD.read._is_own_hidden(component_id) then
            skill_name = name
            break
        end
    end
    if not skill_name then
        return "no_row", "levelup.skill: no unhidden skill container"
    end

    local text1_id = QD.read._component_of("levelup_display:text1")
    local text2_id = QD.read._component_of("levelup_display:text2")
    local line1 = (text1_id and QD.read._text_of(text1_id)) or ""
    local line2 = (text2_id and QD.read._text_of(text2_id)) or ""
    local level_text = line2 ~= "" and (line1 .. " " .. line2) or line1
    return "ok", { skill = skill_name, level_text = level_text }
end

-- levelup_display:continue ships the same pause-arming onload literal as the
-- chat pages (plan 5.6), so it resumes through the same seam chat.continue_
-- drives (D1: RESUME_PAUSEBUTTON, action_index -1) -- api.drive.resume
-- directly, not QD.chat.continue_, which is scoped to chat_modal_host and
-- would refuse to arm a component outside it.
function QD.levelup.continue_()
    local continue_id = QD.read._component_of("levelup_display:continue")
    if not continue_id then
        return "no_row", "levelup.continue: no continue role"
    end

    local pending_result, pending_id = api_drive.pause_pending()
    if pending_result == "ok" and pending_id == continue_id then
        return "covered", "levelup.continue: a resume is already outstanding"
    end

    local resume_result, resume_detail = api_drive.resume(continue_id)
    if resume_result ~= "ok" then
        return resume_result, resume_detail
    end

    return await({
        match = function(ev) return ev.kind == "resume_answered" and ev.a == continue_id end,
        event = "resume_answered",
        note = "levelup.continue",
    }, 5)
end
