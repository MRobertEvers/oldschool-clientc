-- The Quest Helper side panel: RuneLite's Swing com.questhelper.panel.* on
-- this client's plugin page (Porcelain rows + custom drawing wells).
--
-- Not transpiled. The framework reaches the panel only through the names in
-- gen/inventory.tsv's `manual` rows, and those are defined here with exactly
-- those keys and spellings:
--
--   com.questhelper.panel.QuestHelperPanel  __new(plugin, questManager, configManager),
--     addQuest, refresh, refreshSkillFiltering, removeQuest,
--     updateHighlight__Client_QuestStep, updateItemRequirements, updateLocks,
--     updateRegionFilterVisibility, updateStepsTexts, and the field questActive
--   com.questhelper.panel.JGenerator         makeJTextArea__ (Fonts' clinit)
--
-- What the page is built from, and why:
--
--   * The plugin page holds at most 48 controls (TORIRS_PLUGIN_WIDGETS_MAX,
--     PORCELAIN_ROWS_MAX), a row string over 191 bytes DROPS the row, and the
--     reconciler compares (key, kind) only. So the long, coloured, clickable
--     content -- the quest list, the requirements, the sections and their
--     steps -- is a small number of custom wells that paint lines of text and
--     item icons, and map a click's y back to a line. Native rows carry the
--     controls: the selects, the buttons, the assist-level page.
--   * The search box is a raw TORIRS_PANEL_INPUT node declared in on_ui_build
--     BEFORE Porcelain's rows: Porcelain has no input row kind, and the host
--     routes the field's "text" action to on_ui_action under its own id.
--   * Quests load on demand. A quest whose module is not loaded is asked for
--     through QH.load_quest(questHelperQuest, callback) (the plugin entry's),
--     and a loading row stands until the callback.
--
-- The plugin entry forwards the page callbacks to QH.ui (see the bottom of
-- this file and docs/QUEST_HELPER_PORT.md, "The panel").
--
-- The client is reached only through QH.api, read at call time.

-- RuneLite scrolls the panel back to the top on a new quest; with no page of
-- this plugin's up (panel_scroll() -1) there is nothing to move, and asking
-- is a porcelain finding.
local function scroll_to_top()
  local p = QH.api.porcelain
  if p.panel_scroll() >= 0 then p.panel_scroll_to(0) end
end

local J, C = J, C
local function api() return QH.api end

QH = QH or {}
local UI = {}
QH.ui = UI

-- The QuestHelperPanel the plugin built. QuestHelperPlugin.startUp makes one;
-- a restart makes a new one, and the page follows the newest.
local current = nil

-- ------------------------------------------------------------------ constants

local QUEST_HELPER_GROUP = "questhelper"
local QUEST_BACKGROUND_GROUP = "questhelpervars"

-- RuneLite's colours (ColorScheme, java.awt.Color), as 0xRRGGBB.
local WHITE = 0xFFFFFF
local BLACK = 0x000000
local GRAY = 0x808080             -- Color.GRAY
local LIGHT_GRAY = 0xC0C0C0       -- Color.LIGHT_GRAY
local DARK_GRAY = 0x404040        -- Color.DARK_GRAY
local BRAND_ORANGE = 0xDC8A00     -- ColorScheme.BRAND_ORANGE
local DARK_GRAY_COLOR = 0x282828  -- ColorScheme.DARK_GRAY_COLOR
local DARKER_GRAY_COLOR = 0x1E1E1E
local HEADER_BG = 0x151515        -- DARKER_GRAY_COLOR.darker()
local HEADER_DIM = 0xB2B2B2       -- Color.WHITE.darker(): a collapsed header
local IN_PROGRESS = 0xF0CF7B      -- QuestSelectPanel's new Color(240, 207, 123)
local FILTERED_BG = 0xB20000      -- Color.RED.darker(): a filtered skill
local STEP_BORDER = 0x393939      -- DARK_GRAY_COLOR.brighter(): between steps
local BOX_RGB = 0xA5A5A5          -- ColorScheme.LIGHT_GRAY_COLOR
local LINK_RGB = 0x00B2B2         -- Color.cyan.darker()

-- The host's ceilings (torirs_porcelain.h, torirs_plugin_types.h).
local ROW_TEXT_MAX = 191          -- PORCELAIN_ROW_TEXT_MAX less the terminator
local WELL_MIN = 48               -- TORIRS_PANEL_CUSTOM_HEIGHT_MIN
local WELL_MAX = 2000             -- under TORIRS_PANEL_CUSTOM_HEIGHT_MAX (2048)
local ROWS_BUDGET = 46            -- 48 controls, less the search field, less one spare
local PANEL_NODE_INPUT = 5        -- enum ToriRS_PanelNodeKind TORIRS_PANEL_INPUT
local SEARCH_ID = "qh_search"

local ICON_W, ICON_H = 36, 32     -- an inventory item sprite
local CHECK = 12                  -- a check box's side
local CHECK_ZONE = 22             -- the strip at a line's right edge it answers in
local PAD = 3                     -- a line's inner top/bottom

-- ------------------------------------------------------------------ helpers

local function fit(s)
  s = s or ""
  if #s <= ROW_TEXT_MAX then return s end
  local cut = ROW_TEXT_MAX - 3
  -- back off to a UTF-8 lead byte, so a code point is never split
  while cut > 0 and (s:byte(cut + 1) or 0) & 0xC0 == 0x80 do cut = cut - 1 end
  return s:sub(1, cut) .. "..."
end

local function strip(s)
  if s == nil then return "" end
  s = J.str(s)
  s = s:gsub("<br>", "\n"):gsub("<[^>]*>", ""):gsub("\r", "")
  return s
end

local function rgb_of(color, fallback)
  if color == nil then return fallback end
  return color:getRGB() & 0xFFFFFF
end

-- Each distinct failure is logged once: the panel re-reads every requirement
-- every tick, and one broken requirement must not fill the log.
local logged, logged_n = {}, 0
local function log(msg)
  if logged[msg] then return end
  if logged_n >= 256 then logged, logged_n = {}, 0 end
  logged[msg], logged_n = true, logged_n + 1
  if J.log then J.log("quest-helper panel: " .. msg) end
end

-- A framework call that may fail (a helper's requirement reading a var the
-- client has not stated, a quest module still loading). The page shows the
-- line greyed instead of losing the whole panel, and the log says why.
local function try(what, f, ...)
  local ok, a, b = J.protect(f, ...)
  if not ok then
    log(what .. ": " .. J.str(a))
    return false, nil, a
  end
  return true, a, b
end

local function is_pending(err)
  return type(err) == "table" and rawget(err, "pending") ~= nil
end

local function jlist(x)
  local out = {}
  if x == nil then return out end
  for _, v in J.iter(x) do out[#out + 1] = v end
  return out
end

local function inst(o, key) return o ~= nil and J.inst(o, key) end

-- Text.titleCase(Enum): what RuneLite's DropdownRenderer shows.
local function title_case(e)
  local s, n = J.str(e:toString()), e:name()
  if s ~= n then return s end
  s = s:lower()
  s = s:gsub("(%a)([%w]*)", function(a, b) return a:upper() .. b end)
  return (s:gsub("_", " "))
end

-- RuneLite's Text.matchesSearchTerms: every term is in some keyword, or a
-- keyword is within Jaro-Winkler 0.9 of it.
local function jaro_winkler(a, b)
  if a == b then return 1.0 end
  local la, lb = #a, #b
  if la == 0 or lb == 0 then return 0.0 end
  local range = math.max(0, math.max(la, lb) // 2 - 1)
  local am, bm = {}, {}
  local matches = 0
  for i = 1, la do
    local lo, hi = math.max(1, i - range), math.min(lb, i + range)
    for j = lo, hi do
      if not bm[j] and a:byte(i) == b:byte(j) then
        am[i], bm[j] = true, true
        matches = matches + 1
        break
      end
    end
  end
  if matches == 0 then return 0.0 end
  local t, k = 0, 1
  for i = 1, la do
    if am[i] then
      while not bm[k] do k = k + 1 end
      if a:byte(i) ~= b:byte(k) then t = t + 1 end
      k = k + 1
    end
  end
  t = t / 2
  local jaro = (matches / la + matches / lb + (matches - t) / matches) / 3
  local prefix = 0
  for i = 1, math.min(4, la, lb) do
    if a:byte(i) == b:byte(i) then prefix = prefix + 1 else break end
  end
  return jaro + prefix * 0.1 * (1 - jaro)
end

local function matches_search(terms, keywords)
  for _, term in ipairs(terms) do
    local any = false
    for _, k in ipairs(keywords) do
      if k:find(term, 1, true) or jaro_winkler(k, term) > 0.9 then any = true; break end
    end
    if not any then return false end
  end
  return true
end

-- ------------------------------------------------------------------ metrics

-- The well font's measures. Learned in paint (the only place a draw object
-- exists) through draw.text_width / draw.text_height, and used by the next
-- layout; until then a fixed-pitch guess. A change re-lays the page out at
-- the next frame, never from inside paint.
local M = { cw = {}, guess_w = 6, text_h = 12, width = 296, dirty = false, measured_h = false }
local CP = "[%z\1-\127\194-\244][\128-\191]*"

local function line_h() return M.text_h + 4 end
local function ascent() return M.text_h - 2 end

local function text_w(s)
  local w = 0
  for ch in s:gmatch(CP) do w = w + (M.cw[ch] or M.guess_w) end
  return w
end

local function learn(draw, s)
  if type(draw.text_width) ~= "function" then return end
  for ch in s:gmatch(CP) do
    if M.cw[ch] == nil then
      local w = draw.text_width(ch)
      if type(w) == "number" and w >= 0 then
        M.cw[ch] = w
        if w ~= M.guess_w then M.dirty = true end
      else
        M.cw[ch] = M.guess_w
      end
    end
  end
end

-- Wrapped text by (width, text). The page re-lays out every tick a quest is
-- open, and nearly every line is the same as the tick before.
local wrap_cache, wrap_cached = {}, 0

local function wrap_uncached(text, width)
  local out = {}
  for para in (text .. "\n"):gmatch("(.-)\n") do
    if para:find("%S") == nil then
      out[#out + 1] = ""
    else
      local line, lw = nil, 0
      local space = M.cw[" "] or M.guess_w
      for word in para:gmatch("%S+") do
        local ww = text_w(word)
        if line ~= nil and lw + space + ww <= width then
          line, lw = line .. " " .. word, lw + space + ww
        else
          if line ~= nil then out[#out + 1] = line end
          if ww <= width then
            line, lw = word, ww
          else
            -- a word wider than the well: break it by code point
            local piece, pw = "", 0
            for ch in word:gmatch(CP) do
              local cw = M.cw[ch] or M.guess_w
              if pw + cw > width and piece ~= "" then
                out[#out + 1] = piece
                piece, pw = "", 0
              end
              piece, pw = piece .. ch, pw + cw
            end
            line, lw = piece, pw
          end
        end
      end
      if line ~= nil then out[#out + 1] = line end
    end
  end
  if #out == 0 then out[1] = "" end
  return out
end

local function wrap(text, width)
  local k = width .. "\0" .. text
  local hit = wrap_cache[k]
  if hit then return hit end
  if wrap_cached >= 4096 then wrap_cache, wrap_cached = {}, 0 end
  hit = wrap_uncached(text, width)
  wrap_cache[k], wrap_cached = hit, wrap_cached + 1
  return hit
end

local function forget_wraps() wrap_cache, wrap_cached = {}, 0 end

-- ------------------------------------------------------------------ lines and wells

-- A line of a well:
--   text, rgb           what it says (markup already stripped), and in what colour
--   bg                  a fill behind the whole line, or nil
--   indent              left inset
--   icon                {id, qty}: an item sprite at the left
--   sub, sub_rgb        a second, dimmer paragraph under the text (a tooltip)
--   check, check_on     a box at the right: nil none, else ticked; check_on false = greyed
--   border              a rule along its top
--   gap                 empty space above it
--   key                 stable identity for the hit map
--   click(panel, line), menu(panel, line), on_check(panel, line)
local function L(t) return t end

local function layout_line(l, width)
  local x0 = 4 + (l.indent or 0)
  local right = width - 4 - (l.check ~= nil and CHECK_ZONE or 0)
  l.tx = x0 + (l.icon and (ICON_W + 4) or 0)
  local avail = math.max(24, right - l.tx)
  l.wrapped = wrap(l.text or "", avail)
  l.sub_wrapped = (l.sub and l.sub ~= "") and wrap(l.sub, avail) or nil
  local n = #l.wrapped + (l.sub_wrapped and #l.sub_wrapped or 0)
  local h = n * line_h() + PAD * 2
  if l.icon and h < ICON_H + 4 then h = ICON_H + 4 end
  if l.check ~= nil and h < CHECK + 8 then h = CHECK + 8 end
  l.body_h = h
  l.h = h + (l.gap or 0)
end

local serial = 0
local function next_serial() serial = serial + 1; return serial end

-- Split `lines` across wells `prefix1..N`, none taller than WELL_MAX, at most
-- `max_wells`. A line never straddles two wells.
local function flow(prefix, lines, width, max_wells)
  local wells, cur = {}, nil
  local cut = false
  for _, l in ipairs(lines) do
    layout_line(l, width)
    if cur == nil or (cur.h + l.h > WELL_MAX and #cur.lines > 0) then
      if #wells >= max_wells then cut = true; break end
      cur = { lines = {}, h = 0 }
      wells[#wells + 1] = cur
    end
    l.y = cur.h
    cur.lines[#cur.lines + 1] = l
    cur.h = cur.h + l.h
  end
  if cut and cur then
    local more = L { text = "(more than this page can show)", rgb = GRAY }
    layout_line(more, width)
    more.y = cur.h
    cur.lines[#cur.lines + 1] = more
    cur.h = cur.h + more.h
  end
  for i, w in ipairs(wells) do
    w.key = prefix .. i
    if w.h > WELL_MAX + 40 then w.h = WELL_MAX + 40 end
  end
  return wells
end

local function paint_sig(w, width)
  local p = { tostring(width), tostring(M.text_h) }
  for _, l in ipairs(w.lines) do
    p[#p + 1] = table.concat({ l.text or "", tostring(l.rgb), tostring(l.bg), tostring(l.indent),
      l.icon and (l.icon[1] .. "x" .. l.icon[2] .. ":" .. tostring(l.icon_state)) or "",
      l.sub or "", tostring(l.sub_rgb), tostring(l.check), tostring(l.check_on), tostring(l.border),
      tostring(l.y), tostring(l.h) }, "\1")
  end
  return table.concat(p, "\2")
end

local function hit_sig(w)
  local p = {}
  for _, l in ipairs(w.lines) do
    p[#p + 1] = (l.key or "") .. "@" .. l.y .. "+" .. l.h .. (l.check ~= nil and "c" or "")
  end
  return table.concat(p, "\2")
end

-- ------------------------------------------------------------------ JGenerator

-- Fonts.clinit asks for a JTextArea's font, its attributes and an underlined
-- derivation. Nothing in the port draws with a Swing font; this is that
-- question answered, not a font.
J.def("com.questhelper.panel.JGenerator$PanelFont", { kind = "class", super = "java.lang.Object", ifaces = {},
  body = function(cls, super)
    function cls.__init(self, attributes)
      super.__init(self)
      self.attributes = attributes
    end
    function cls.getAttributes(self)
      return C["java.util.HashMap"].__new__Map(self.attributes)
    end
    function cls.deriveFont__Map(self, attributes)
      return cls.__new(C["java.util.HashMap"].__new__Map(attributes))
    end
    function cls.deriveFont__int_float(self, style, size)
      local a = C["java.util.HashMap"].__new__Map(self.attributes)
      a:put("style", style)
      a:put("size", size)
      return cls.__new(a)
    end
  end })

J.def("com.questhelper.panel.JGenerator$TextArea", { kind = "class", super = "java.lang.Object", ifaces = {},
  body = function(cls, super)
    function cls.__init(self, text)
      super.__init(self)
      self.text = text or ""
      self.font = C["com.questhelper.panel.JGenerator$PanelFont"].__new(C["java.util.HashMap"].__new__())
    end
    function cls.getFont(self) return self.font end
    function cls.setFont(self, f) self.font = f end
    function cls.getText(self) return self.text end
    function cls.setText(self, t) self.text = t end
    function cls.setForeground(self, c) self.foreground = c end
  end })

J.def("com.questhelper.panel.JGenerator", { kind = "class", super = "java.lang.Object", ifaces = {},
  body = function(cls, super)
    function cls.__init(self) super.__init(self) end
    function cls.makeJTextArea__() return C["com.questhelper.panel.JGenerator$TextArea"].__new(nil) end
    function cls.makeJTextArea__String(text) return C["com.questhelper.panel.JGenerator$TextArea"].__new(text) end
  end })

-- ------------------------------------------------------------------ requirement groups

-- QuestRequirementsPanel: a header and one line per requirement, coloured by
-- Requirement.getColor once update() has run.
local Group = {}
Group.__index = Group

local function new_group(title, reqs, show_even_if_empty)
  local g = setmetatable({ title = title, show_empty = show_even_if_empty, items = {}, visible = true }, Group)
  g:set(reqs)
  return g
end

function Group:set(reqs)
  self.items = {}
  self.visible = true
  if reqs == nil then return end
  for _, req in J.iter(reqs) do
    local item = { req = req, text = "", rgb = WHITE, shown = true }
    local ok, text = try("requirement text", function() return req:getDisplayText() end)
    item.text = ok and strip(text) or "(requirement unavailable)"
    if inst(req, "com.questhelper.requirements.item.ItemRequirement") then
      item.is_item = true
      local okid, id, qty = try("item id", function()
        local d = req:getDisplayItemId()
        if d == nil or d <= 0 then d = req:getId() end
        return d, req:getQuantity()
      end)
      if okid and id ~= nil and id > 0 then item.icon_id, item.qty = id, (qty and qty > 0) and qty or 1 end
      local okt, tip = try("tooltip", function() return req:getTooltip() end)
      if okt and tip ~= nil and tip ~= "" then item.tooltip = strip(tip) end
    end
    if inst(req, "com.questhelper.requirements.quest.QuestRequirement") then
      local okq, qhq = try("required quest", function() return req:getQuest() end)
      if okq then item.quest = qhq end
    end
    self.items[#self.items + 1] = item
  end
end

-- QuestRequirementsPanel.update.
function Group:update(client, config)
  local active = 0
  for _, item in ipairs(self.items) do
    local req = item.req
    local okd, shown = try("shouldDisplayText", function() return req:shouldDisplayText(client) end)
    item.shown = (not okd) or shown
    if item.shown then
      active = active + 1
      local okt, text = try("requirement text", function() return req:getDisplayText() end)
      if okt then item.text = strip(text) end
      local okc, color = try("requirement colour", function() return req:getColor(client, config) end)
      item.rgb = okc and rgb_of(color, WHITE) or GRAY
      if item.is_item and not inst(req, "com.questhelper.requirements.item.NoItemRequirement") then
        local okp, tip = try("tooltip", function() return req:getTooltip() end)
        item.tooltip = (okp and tip ~= nil and tip ~= "") and strip(tip) or nil
      end
    end
  end
  self.visible = active > 0 or (#self.items == 0 and self.show_empty)
  self.updated = true
end

function Group:lines(out, panel, indent)
  if not self.visible then return end
  if #self.items == 0 and not self.show_empty then return end
  out[#out + 1] = L { key = "g:" .. self.title, text = self.title, rgb = WHITE, bg = DARKER_GRAY_COLOR, gap = 6, indent = indent }
  if #self.items == 0 then
    out[#out + 1] = L { key = "g:none:" .. self.title, text = "None", rgb = GRAY, indent = indent }
    return
  end
  for i, item in ipairs(self.items) do
    if item.shown then
      local l = L { key = "r:" .. self.title .. ":" .. i, text = item.text, rgb = item.rgb, indent = indent,
        sub = item.tooltip, sub_rgb = GRAY }
      if item.icon_id then
        l.icon = { item.icon_id, item.qty }
        l.icon_ref, l.icon_state = panel:icon(item.icon_id, item.qty)
      end
      if item.quest then
        l.click = function(p) p:open_required_quest(item.quest) end
      end
      out[#out + 1] = l
    end
  end
end

-- ------------------------------------------------------------------ step sections

-- QuestStepPanel (kind "steps") and QuestSectionSection (kind "top").
local Section = {}
Section.__index = Section

local function steps_of(details)
  local out = {}
  for _, step in J.iter(details:getSteps()) do
    if inst(step, "com.questhelper.steps.PortTaskStep") then
      for _, s2 in J.iter(step:getStepsList()) do out[#out + 1] = s2 end
    else
      out[#out + 1] = step
    end
  end
  return out
end

local function step_text(step)
  local t = step:getText()
  if t == nil then return "" end
  local parts = {}
  for _, line in J.iter(t) do parts[#parts + 1] = strip(line) end
  return table.concat(parts, "\n\n")
end

local function contains_current(details, current_step)
  if current_step == nil then return false end
  local ok, yes = try("PanelDetails.contains", function() return details:contains__QuestStep(current_step) end)
  return ok and yes
end

local function directly_contains(details, current_step)
  if current_step == nil then return false end
  return details:getSteps():contains(current_step)
end

local function new_section(panel, details, current_step, depth)
  local s = setmetatable({ panel = panel, details = details, depth = depth, visible = true,
    collapsed = false, highlighted = false, lockable = false, lock_on = true, lock_selected = false,
    auto_locked = false }, Section)
  s.highlighted = contains_current(details, current_step)
  local quest = panel.questManager:getSelectedQuest()
  s.quest = quest
  if inst(details, "com.questhelper.panel.TopLevelPanelDetails") then
    s.kind = "top"
    s.children = {}
    local list = jlist(details:getPanelDetails())
    local draggable = false
    for _, pd in ipairs(list) do if pd:getId() ~= -2147483648 then draggable = true end end
    if draggable then
      local ok, order = try("sidebar order", function() return panel.plugin:loadSidebarOrder(quest) end)
      if ok and order ~= nil then
        local idx = {}
        for i, id in J.iter(order) do idx[id] = i end
        local pos = {}
        for i, pd in ipairs(list) do pos[pd] = i end
        table.sort(list, function(a, b)
          local ia, ib = idx[a:getId()] or math.maxinteger, idx[b:getId()] or math.maxinteger
          if ia ~= ib then return ia < ib end
          return pos[a] < pos[b]
        end)
      end
    end
    for _, pd in ipairs(list) do
      local child = new_section(panel, pd, current_step, depth + 1)
      if pd:getLockingQuestSteps() ~= nil and (pd:getVars() == nil or quest == nil
          or pd:getVars():contains(quest:getVar__())) then
        child.lockable = true
      end
      s.children[#s.children + 1] = child
    end
  else
    s.kind = "steps"
    s.steps = {}
    s.persisted = {}
    if quest ~= nil then
      local ok, map = try("manual skips", function()
        return C["com.questhelper.panel.ManualStepSkipStore"].load__ConfigManager_Gson_String(
          quest:getConfigManager(), quest.gson, quest:getDisplayedQuestName())
      end)
      if ok and map ~= nil then
        for _, k in J.iter(map:keySet()) do if map:get(k) == true then s.persisted[k] = true end end
      end
    end
    local client = panel.plugin:getClient()
    for _, step in ipairs(steps_of(details)) do
      local row = { step = step, rgb = LIGHT_GRAY, shown = step:isShowInSidebar(), text = "" }
      local okt, text = try("step text", step_text, step)
      row.text = okt and text or ""
      local m = step:getSidebarManualSkipRequirement()
      local pk = step:getSidebarManualSkipPersistenceKey()
      if m ~= nil and pk ~= nil and not pk:isBlank() then
        if s.persisted[pk] then m:setShouldPass(true) end
        local okc, checked = try("manual skip", function() return client ~= nil and m:check__Client(client) end)
        row.skip = { m = m, pk = pk, selected = okc and checked or false }
        s.has_skips = true
      end
      s.steps[#s.steps + 1] = row
    end
    if details:getRequirements() ~= nil then
      s.required = new_group("Bring the following items:", details:getRequirements(), false)
    end
    if details:getRecommended() ~= nil then
      s.recommended = new_group("Optionally bring the following items:", details:getRecommended(), false)
    end
  end
  if current_step == nil or not directly_contains(details, current_step) then s.collapsed = true end
  return s
end

function Section:set_lockable(can) self.lockable = can end

local function hide_passes(details, client)
  local hide = details:getHideCondition()
  if hide == nil then return false end
  local ok, yes = try("hide condition", function() return hide:check__Client(client) end)
  return ok and yes
end

local function lockable_for(details, quest)
  if details:getLockingQuestSteps() == nil then return false end
  return details:getVars() == nil or (quest ~= nil and details:getVars():contains(quest:getVar__()))
end

-- AbstractQuestSection.updateHighlightCheck.
function Section:update_highlight_check(client, new_step, quest)
  if hide_passes(self.details, client) then
    self.visible = false
    return false
  end
  self.visible = true
  self:set_lockable(lockable_for(self.details, quest))
  local highlighted = false
  if self.kind == "top" then
    for _, child in ipairs(self.children) do
      if child:update_highlight_check(client, new_step, quest) then
        highlighted = true
        self.last_child = child
      end
    end
    if highlighted then
      self.collapsed = false
      self.highlighted = true
    else
      self:remove_highlight()
    end
    return highlighted
  end
  for _, row in ipairs(self.steps) do
    local step = row.step
    local cond = step:getConditionToHide()
    local hidden = false
    if cond ~= nil then
      local ok, yes = try("condition to hide", function() return cond:check__Client(client) end)
      hidden = ok and yes
    end
    if not hidden then
      local fade = step:getFadeCondition()
      if fade ~= nil then
        local ok, yes = try("fade condition", function() return fade:check__Client(client) end)
        if ok and yes then
          row.rgb = DARK_GRAY
          local okt, t = try("fade text", function() return fade:getDisplayText() end)
          row.tooltip = okt and strip(t) or nil
        else
          row.rgb = LIGHT_GRAY
          row.tooltip = nil
        end
      end
      if not highlighted and new_step ~= nil then
        local ok, yes = try("containsSteps", function()
          return step:containsSteps(new_step, C["java.util.HashSet"].__new__())
        end)
        if ok and yes then
          highlighted = true
          self:update_highlight(row)
        end
      end
    end
  end
  if not highlighted then self:remove_highlight() end
  return highlighted
end

-- QuestStepPanel.updateHighlight.
function Section:update_highlight(row)
  self.collapsed = false
  if self.last_row and self.last_row ~= row then self.last_row.rgb = LIGHT_GRAY end
  row.rgb = BRAND_ORANGE
  self.highlighted = true
  self.last_row = row
end

function Section:remove_highlight()
  self.highlighted = false
  if self.kind == "top" then
    if self.last_child then self.last_child:remove_highlight() end
  else
    local active = self.panel:active_step()
    for _, row in ipairs(self.steps) do
      if row.step == active then row.rgb = LIGHT_GRAY end
    end
    if self.last_row then self.last_row.rgb = LIGHT_GRAY end
  end
  self.collapsed = true
end

-- AbstractQuestSection.updateLock. QuestOverviewPanel.updateLocks asks the
-- top-level sections only, and QuestSectionSection does not pass it down.
function Section:update_lock()
  local locking = self.details:getLockingQuestSteps()
  if locking ~= nil then
    -- isLocked() first: it is what re-reads the locking condition and sets
    -- the step's unlockable flag (QuestStep.isLocked).
    local okl, locked = try("section lock", function() return locking:isLocked() end)
    if not okl then return end
    if locking:isUnlockable() then
      self.auto_locked = false
      self.lock_on = true
    else
      if not self.auto_locked then self.collapsed = true end
      self.auto_locked = true
      self.lock_on = false
    end
    if locked then self.lock_selected = true end
  end
end

function Section:lock(locked)
  self.details:getLockingQuestSteps():setLockedManually(locked)
  self.lock_selected = locked
  self.collapsed = locked
end

function Section:update_text()
  if self.kind == "top" then
    for _, child in ipairs(self.children) do child:update_text() end
    return
  end
  for _, row in ipairs(self.steps) do
    local ok, text = try("step text", step_text, row.step)
    if ok then row.text = text end
  end
end

-- QuestStepPanel.updateRequirements / updateStepVisibility.
function Section:update_requirements(client, config, quest)
  if self.kind == "top" then
    for _, child in ipairs(self.children) do child:update_requirements(client, config, quest) end
    self:update_visibility(client, quest)
    return
  end
  if self.required then self.required:update(client, config) end
  if self.recommended then self.recommended:update(client, config) end
  self:update_visibility(client, quest)
  for _, row in ipairs(self.steps) do
    if row.skip then
      local ok, checked = try("manual skip", function() return client ~= nil and row.skip.m:check__Client(client) end)
      row.skip.selected = ok and checked or false
    end
  end
end

function Section:update_visibility(client, quest)
  local changed = false
  if self.kind == "top" then
    for _, child in ipairs(self.children) do
      if child:update_visibility(client, quest) then changed = true end
    end
  else
    for _, row in ipairs(self.steps) do
      local step = row.step
      local old = step:isShowInSidebar()
      local cond = step:getConditionToHide()
      local now = true
      if cond ~= nil then
        local ok, yes = try("condition to hide", function() return cond:check__Client(client) end)
        now = not (ok and yes)
      end
      if old ~= now then changed = true end
      step:setShowInSidebar(now)
      row.shown = now
    end
  end
  if changed then self:update_highlight_check(client, self.panel:active_step(), quest) end
  return changed
end

function Section:set_skip(row, selected)
  local m, pk = row.skip.m, row.skip.pk
  m:setShouldPass(selected)
  self.persisted[pk] = selected or nil
  row.skip.selected = selected
  if self.quest ~= nil then self.quest:notifyManualSidebarSkipChanged(pk, selected) end
end

function Section:tick_up_to(target)
  for _, row in ipairs(self.steps) do
    if row.skip then self:set_skip(row, true) end
    if row == target then break end
  end
end

function Section:reset_skips()
  if self.quest ~= nil then self.quest:resetAllManualSidebarSkips() end
  for _, row in ipairs(self.steps) do
    if row.skip then
      row.skip.m:setShouldPass(false)
      self.persisted[row.skip.pk] = nil
      row.skip.selected = false
    end
  end
end

function Section:lines(out, panel, id)
  if not self.visible then return end
  local indent = (self.depth - 1) * 8
  local header_rgb = self.highlighted and BLACK or (self.collapsed and HEADER_DIM or WHITE)
  local header = L { key = "s:" .. id, text = (self.collapsed and "+ " or "- ") .. strip(self.details:getHeader()),
    rgb = header_rgb, bg = self.highlighted and BRAND_ORANGE or HEADER_BG, gap = 5, indent = indent,
    click = function(p) self.collapsed = not self.collapsed; p:changed() end }
  if self.lockable then
    header.check = self.lock_selected
    header.check_on = self.lock_on
    header.on_check = function(p)
      if not self.lock_on then return end
      self:lock(not self.lock_selected)
      p:changed()
    end
  end
  if self.kind == "top" and self.children and #self.children > 1 and self.panel:draggable(self) then
    header.menu = function(p) p:reset_section_order(self) end
  end
  out[#out + 1] = header
  if self.collapsed then return end
  if self.kind == "top" then
    for i, child in ipairs(self.children) do child:lines(out, panel, id .. "." .. i) end
    return
  end
  if self.required then self.required:lines(out, panel, indent + 4) end
  if self.recommended then self.recommended:lines(out, panel, indent + 4) end
  for i, row in ipairs(self.steps) do
    if row.shown then
      local l = L { key = "t:" .. id .. ":" .. i, text = row.text, rgb = row.rgb, indent = indent + 4,
        border = true, sub = row.tooltip, sub_rgb = DARK_GRAY }
      if row.skip then
        l.check = row.skip.selected
        l.check_on = true
        l.on_check = function(p) self:set_skip(row, not row.skip.selected); p:changed() end
        -- RuneLite's context menu on the box: "Tick all up to this step".
        l.menu = function(p) self:tick_up_to(row); p:changed() end
      end
      out[#out + 1] = l
    end
  end
  if self.has_skips then
    out[#out + 1] = L { key = "x:" .. id, text = "Reset all tick boxes", rgb = LINK_RGB, indent = indent + 4,
      click = function(p) self:reset_skips(); p:changed() end }
  end
end

function Section:ids(out)
  local id = self.details:getId()
  if id ~= -2147483648 then out[#out + 1] = id end
  if self.kind == "top" then for _, c in ipairs(self.children) do c:ids(out) end end
end

-- ------------------------------------------------------------------ QuestHelperPanel

local VIEW_LIST, VIEW_OVERVIEW, VIEW_SETTINGS, VIEW_LOADING = "list", "overview", "settings", "loading"

local FILTER_KEY = "com.questhelper.QuestHelperConfig$QuestFilter"
local DIFFICULTY_KEY = "com.questhelper.questhelpers.QuestDetails$Difficulty"
local ORDER_KEY = "com.questhelper.QuestHelperConfig$QuestOrdering"

local ASSIST = {
  { level = "full", title = "Full Assistance", button = "Use full assistance", set = "setToFullAssistance",
    text = { "Gives you the full helper experience, highlighting objects & dialogue options, puzzler solvers, and with arrows so you know where to go." } },
  { level = "medium", title = "Medium Assistance", button = "Use medium assistance", set = "setToMediumAssistance",
    text = { "This enables most overlay-based help, but hides puzzle solutions.",
             "This is a good option if you want to still have guidance, but still solve certain parts for yourself." } },
  { level = "minimum", title = "Minimum Assistance", button = "Use minimum assistance", set = "setToMinimumAssistance",
    text = { "This disables most overlay-based help, and hides puzzle solutions.",
             "This is a good option if you just want the sidebar with helpful information, as it will still track where you are in the quest, and show items needed." } },
}

J.def("com.questhelper.panel.QuestHelperPanel", { kind = "class", super = "java.lang.Object", ifaces = {},
  body = function(cls, super)
    function cls.__zero(self)
      super.__zero(self)
      self.questActive = false
    end

    -- QuestHelperPanel(QuestHelperPlugin, QuestManager, ConfigManager)
    function cls.__init(self, plugin, questManager, configManager)
      super.__init(self)
      assert(plugin, "QuestHelperPanel needs the plugin")
      assert(questManager, "QuestHelperPanel needs the quest manager")
      assert(configManager, "QuestHelperPanel needs the config manager")
      self.plugin = plugin
      self.questManager = questManager
      self.configManager = configManager
      self.questActive = false
      self.view = VIEW_LIST
      self.search = ""
      self.entries = {}                -- the quest list: {header=} or {helper=, qhq=, state=}
      self.list_message = "Please log in to see available quests"
      self.message_visible = true
      self.filter_value, self.difficulty_value, self.order_value = nil, nil, nil
      self.skills_open, self.regions_open = false, false
      self.skill_count = 0
      self.region_visible = false
      self.regions = {}                -- LeagueRegion -> true
      self.list_wells_max = 1
      self.icons = {}                  -- "id:qty" -> {ref, state}
      self.icons_pending = false
      self.well_state = {}             -- key -> {psig, pkey, hsig, hkey}
      self.wells_by_key = {}
      self.rev, self.rows_rev, self.rows = 1, 0, {}
      self.owed = true
      self.qhq_of = nil
      current = self
      self.constructing = true
      self:updateRegionFilterVisibility(self:is_league())
      self:refreshSkillFiltering()
      self.constructing = false
    end

    -- -------------------------------------------------------------- plumbing

    function cls.changed(self)
      self.rev = self.rev + 1
      if self.in_paint or self.constructing then
        self.owed = true
        return
      end
      api().porcelain.invalidate()
    end

    function cls.config(self) return self.plugin:getConfig__() end

    function cls.client(self) return self.plugin:getClient() end

    function cls.is_league(self)
      local ok, yes = try("world types", function()
        return self:client():getWorldType():contains(C["net.runelite.api.WorldType"].SEASONAL)
      end)
      return ok and yes
    end

    function cls.icon(self, id, qty)
      local k = id .. ":" .. qty
      local e = self.icons[k]
      if e == nil or e.state == "pending" then
        local ref, state = api().game.item_image(id, qty)
        e = { ref = ref, state = state or (ref and "ready" or "missing") }
        self.icons[k] = e
      end
      if e.state == "pending" then self.icons_pending = true end
      return e.ref, e.state
    end

    -- The QuestHelperQuest that owns a helper, without touching the helper:
    -- a helper whose module is not loaded is a proxy that loads on any read.
    function cls.qhq(self, helper)
      if self.qhq_of == nil then
        self.qhq_of = setmetatable({}, { __mode = "k" })
        local vals = C["com.questhelper.questinfo.QuestHelperQuest"].values()
        for i = 1, vals.n do self.qhq_of[vals[i]:getQuestHelper()] = vals[i] end
      end
      local q = self.qhq_of[helper]
      if q ~= nil then return q end
      return helper:getQuest()
    end

    function cls.active_step(self)
      local sel = self.plugin:getSelectedQuest()
      if sel == nil then return nil end
      local cur = sel:getCurrentStep()
      if cur == nil then return nil end
      return cur:getActiveStep()
    end

    function cls.draggable(self, section)
      for _, child in ipairs(section.children or {}) do
        if child.details:getId() ~= -2147483648 then return true end
      end
      return false
    end

    function cls.reset_section_order(self, section)
      local quest = self.questManager:getSelectedQuest()
      if quest == nil then return end
      local ids = {}
      section:ids(ids)
      local list = C["java.util.ArrayList"].__new__()
      for _, id in ipairs(ids) do list:add(id) end
      self.plugin:resetSidebarOrderForSection(quest, list)
    end

    -- -------------------------------------------------------------- the list

    -- QuestHelperPanel.refresh(List<QuestHelper>, boolean, Map<QuestHelperQuest, QuestState>, QuestFilter...)
    function cls.refresh(self, questHelpers, loggedOut, completedQuests, questFilters)
      local config = self:config()
      self.filter_value = config:filterListBy():name()
      self.difficulty_value = config:difficulty():name()
      self.order_value = config:orderListBy():name()
      local entries = {}
      local NOT_STARTED = C["net.runelite.api.QuestState"].NOT_STARTED
      local function add_helper(helper)
        local qhq = self:qhq(helper)
        local keywords = {}
        for _, k in J.iter(qhq:getKeywords()) do keywords[#keywords + 1] = J.str(k):lower() end
        entries[#entries + 1] = { helper = helper, qhq = qhq, name = J.str(qhq:getName()), keywords = keywords,
          state = completedQuests:getOrDefault(qhq, NOT_STARTED) }
      end
      local filters = questFilters and questFilters.n or 0
      if filters > 0 then
        for i = 1, filters do
          local filter = questFilters[i]
          local matched = {}
          for _, helper in J.iter(questHelpers) do
            if self:section_matches(filter, helper) then matched[#matched + 1] = helper end
          end
          if #matched > 0 then entries[#entries + 1] = { header = J.str(filter:getDisplayName()) } end
          for _, helper in ipairs(matched) do add_helper(helper) end
        end
      else
        for _, helper in J.iter(questHelpers) do add_helper(helper) end
      end
      self.entries = entries
      local more = false
      for _, q in J.iter(completedQuests:keySet()) do
        if completedQuests:get(q) ~= C["net.runelite.api.QuestState"].FINISHED then more = true; break end
      end
      if #entries == 0 or not more then
        self.list_message = loggedOut and "Please log in to see available quests"
          or "There are no quests available that match your current filters"
      end
      self.message_visible = #entries == 0
      -- The list's wells are counted from the whole list, not the search, so
      -- typing changes what they paint and never how many there are.
      self.list_wells_max = math.max(1, (#entries * line_h() + WELL_MAX - 1) // WELL_MAX + 1)
      self:changed()
    end

    -- A section filter (QuestFilter.QUEST, MINIQUEST, ...) reads only
    -- helper.getQuest(); asked of the helper itself it would load the quest's
    -- module. So it is asked of a stand-in whose getQuest is the enum's.
    function cls.section_matches(self, filter, helper)
      local qhq = self:qhq(helper)
      local stand_in = setmetatable({}, { __index = function(_, k)
        if k == "getQuest" then return function() return qhq end end
        local v = helper[k]
        if type(v) == "function" then return function(_, ...) return v(helper, ...) end end
        return v
      end })
      local ok, yes = try("quest filter", function() return filter:test(stand_in) end)
      return ok and yes
    end

    function cls.visible_entries(self)
      if self.search == "" then return self.entries end
      local terms = {}
      for t in self.search:lower():gmatch("[^ ]+") do terms[#terms + 1] = t end
      local out = {}
      for _, e in ipairs(self.entries) do
        if e.header == nil and matches_search(terms, e.keywords) then out[#out + 1] = e end
      end
      return out
    end

    function cls.list_lines(self)
      local out = {}
      local pass = rgb_of(self:config():passColour(), 0x00FF00)
      for i, e in ipairs(self:visible_entries()) do
        if e.header then
          out[#out + 1] = L { key = "h:" .. e.header, text = e.header, rgb = WHITE, bg = DARKER_GRAY_COLOR, gap = i > 1 and 4 or 0 }
        else
          local rgb = WHITE
          if e.state == C["net.runelite.api.QuestState"].FINISHED then rgb = pass
          elseif e.state == C["net.runelite.api.QuestState"].IN_PROGRESS then rgb = IN_PROGRESS end
          out[#out + 1] = L { key = "q:" .. e.name, text = e.name, rgb = rgb,
            click = function(p)
              p:setSelectedQuest(e.helper)
              p:emptyBar()
            end }
        end
      end
      return out
    end

    function cls.emptyBar(self)
      if self.search == "" then return end
      self.search = ""
      api().panel.set_text(SEARCH_ID, "")
      self:changed()
    end

    -- QuestHelperPanel.onSearchBarChanged.
    function cls.on_search(self, text)
      self.search = text or ""
      if self.view == VIEW_SETTINGS then return end
      if self.view == VIEW_LOADING then return end
      if self.current_quest == nil or self.search ~= "" then
        self.view = VIEW_LIST
      else
        self.view = VIEW_OVERVIEW
      end
      self:changed()
    end

    function cls.refreshSkillFiltering(self)
      local n = 0
      local skills = C["net.runelite.api.Skill"].values()
      for i = 1, skills.n do
        if self.configManager:getConfiguration__String_String(QUEST_BACKGROUND_GROUP, "skillfilter" .. skills[i]:getName()) == "true" then
          n = n + 1
        end
      end
      self.skill_count = n
      self:changed()
    end

    function cls.updateRegionFilterVisibility(self, isLeagueWorld)
      local v = self:config():regionFilterVisibility()
      local RFV = C["com.questhelper.QuestHelperConfig$RegionFilterVisibility"]
      local show
      if v == RFV.SHOW then show = true
      elseif v == RFV.HIDE then show = false
      else show = isLeagueWorld and true or false end
      self.region_visible = show
      if not show and next(self.regions) ~= nil then
        self.regions = {}
        C["com.questhelper.config.LeagueFiltering"].setSelectedRegions(nil)
        self:regions_changed()
      end
      self:changed()
    end

    function cls.regions_changed(self)
      local qm = self.questManager
      self.plugin:getClientThread():invokeLater__Runnable(J.fn("java.lang.Runnable", "run", function() qm:updateQuestList() end))
      self:changed()
    end

    function cls.toggle_region(self, region)
      if self.regions[region] then self.regions[region] = nil else self.regions[region] = true end
      local set = C["java.util.EnumSet"].noneOf(nil)
      for r in pairs(self.regions) do set:add(r) end
      C["com.questhelper.config.LeagueFiltering"].setSelectedRegions((not set:isEmpty()) and C["java.util.EnumSet"].copyOf__Collection(set) or nil)
      self:regions_changed()
    end

    function cls.toggle_skill(self, skill)
      local key = "skillfilter" .. skill:getName()
      local now = self.configManager:getConfiguration__String_String(QUEST_BACKGROUND_GROUP, key) == "true"
      self.configManager:setConfiguration__String_String_String(QUEST_BACKGROUND_GROUP, key, now and "false" or "true")
      self:changed()
    end

    function cls.skill_lines(self)
      local out = {}
      local skills = C["net.runelite.api.Skill"].values()
      local cols = 3
      local row
      for i = 1, skills.n do
        local skill = skills[i]
        local filtered = self.configManager:getConfiguration__String_String(QUEST_BACKGROUND_GROUP, "skillfilter" .. skill:getName()) == "true"
        if (i - 1) % cols == 0 then row = { cells = {} }; out[#out + 1] = row end
        row.cells[#row.cells + 1] = { text = J.str(skill:getName()), bg = filtered and FILTERED_BG or DARKER_GRAY_COLOR,
          rgb = filtered and WHITE or LIGHT_GRAY, act = function(p) p:toggle_skill(skill) end }
      end
      return out, cols
    end

    function cls.region_lines(self)
      local out = {}
      local regions = C["com.questhelper.questinfo.LeagueRegion"].values()
      local cols = 3
      local row
      for i = 1, regions.n do
        local region = regions[i]
        local on = self.regions[region] == true
        if (i - 1) % cols == 0 then row = { cells = {} }; out[#out + 1] = row end
        row.cells[#row.cells + 1] = { text = J.str(region:getDisplayName()), bg = on and BRAND_ORANGE or DARKER_GRAY_COLOR,
          rgb = on and WHITE or GRAY, act = function(p) p:toggle_region(region) end }
      end
      return out, cols
    end

    -- -------------------------------------------------------------- quest selection

    function cls.logged_in(self)
      local ok, yes = try("game state", function()
        return self:client():getGameState() == C["net.runelite.api.GameState"].LOGGED_IN
      end)
      return ok and yes
    end

    -- Run `then_do(helper)` once the helper's quest module is loaded, showing a
    -- loading row meanwhile. QH.load_quest is the plugin entry's.
    function cls.with_loaded(self, helper, then_do)
      if J.lazy_forced(helper) then
        then_do(helper)
        return
      end
      assert(QH.load_quest, "QH.load_quest: the plugin entry provides quest loading")
      local qhq = self:qhq(helper)
      self.loading = { helper = helper, name = J.str(qhq:getName()), back = self.view }
      self.view = VIEW_LOADING
      self:changed()
      QH.load_quest(qhq, function(ok, err)
        if self.loading == nil or self.loading.helper ~= helper then return end
        local back = self.loading.back
        if ok then
          self.loading = nil
          self.view = back == VIEW_LOADING and VIEW_LIST or back
          self:changed()
          then_do(helper)
        else
          self.loading.failed = err and J.str(err) or "the quest could not be loaded"
          self:changed()
        end
      end)
    end

    -- QuestHelperPanel.setSelectedQuest.
    function cls.setSelectedQuest(self, questHelper)
      if questHelper == nil or not self:logged_in() then
        self:deactivate_settings()
        return
      end
      self:with_loaded(questHelper, function(helper)
        if self.configManager:getConfiguration__String_String(QUEST_BACKGROUND_GROUP, "selected-assist-level") == "true" then
          self.questManager:startUpQuest__QuestHelper_boolean(helper, true)
        else
          self.assist_helper = helper
          self.view = VIEW_SETTINGS
          self:changed()
        end
      end)
    end

    function cls.open_required_quest(self, qhq)
      local helper = qhq:getQuestHelper()
      self:with_loaded(helper, function(h) self.questManager:startUpQuest__QuestHelper_boolean(h, true) end)
    end

    function cls.activate_settings(self)
      self.assist_helper = nil
      self.view = VIEW_SETTINGS
      self:changed()
    end

    function cls.deactivate_settings(self)
      if self.questActive and self.search == "" and self.current_quest ~= nil then
        self.view = VIEW_OVERVIEW
      else
        self.view = VIEW_LIST
      end
      self:changed()
    end

    function cls.choose_assist(self, entry)
      C["com.questhelper.config.AssistanceLevel"][entry.set](self.configManager)
      self.configManager:setConfiguration__String_String_String(QUEST_BACKGROUND_GROUP, "selected-assist-level", "true")
      local helper = self.assist_helper
      self:setSelectedQuest(helper)
      self:changed()
    end

    -- -------------------------------------------------------------- the overview

    -- QuestHelperPanel.addQuest / QuestOverviewPanel.addQuest.
    function cls.addQuest(self, quest, isActive)
      assert(quest, "addQuest needs a quest")
      self.view = VIEW_OVERVIEW
      self.questActive = true
      self.current_quest = quest
      self.sections = {}
      self.configs = {}
      self.ov_wells = 1
      local step = nil
      if isActive then
        local cur = quest:getCurrentStep()
        step = cur and cur:getActiveStep() or nil
      end
      if quest:getCurrentStep() ~= nil then
        self.name = J.str(quest:getQuest():getName())
        self:setup_requirements(quest)
        for _, pd in J.iter(quest:getPanels()) do
          self.sections[#self.sections + 1] = new_section(self, pd, step, 1)
        end
      end
      scroll_to_top()
      self:changed()
    end

    function cls.removeQuest(self)
      self.questActive = false
      self.current_quest = nil
      self.sections = nil
      self.groups = nil
      self.configs = nil
      self.name = nil
      if self.view ~= VIEW_LOADING then self.view = VIEW_LIST end
      scroll_to_top()
      self:changed()
    end

    function cls.setup_requirements(self, quest)
      local config = self:config()
      local g = {}
      local configs = quest:getConfigs()
      if configs ~= nil then
        for _, hc in J.iter(configs) do self.configs[#self.configs + 1] = hc end
      end
      local general
      if config:showFullRequirements() then
        local ok, list, err = try("aggregated requirements", cls.aggregated_requirements, quest)
        if ok then general = list
        else
          if is_pending(err) then self.retry_requirements = quest end
          general = quest:getGeneralRequirements()
        end
      else
        general = quest:getGeneralRequirements()
      end
      g.general = new_group("General requirements:", general, false)
      g.recommended = new_group("Recommended:", quest:getGeneralRecommended(), false)
      g.items = new_group("Item requirements:", quest:getItemRequirements(), true)
      g.items_rec = new_group("Recommended items:", quest:getItemRecommended(), true)
      local combat = quest:getCombatRequirements()
      if combat == nil then g.combat = "None"
      else
        local parts = {}
        for _, c in J.iter(combat) do parts[#parts + 1] = strip(c) end
        g.combat = table.concat(parts, "\n")
      end
      local notes = quest:getNotes()
      if notes ~= nil then
        local parts = {}
        for _, n in J.iter(notes) do parts[#parts + 1] = strip(n) end
        g.notes = table.concat(parts, "\n\n")
      end
      if config:hideQuestRewards() then
        g.rewards_hidden = true
      else
        local rewards = quest:getQuestRewards()
        if rewards ~= nil and not rewards:isEmpty() then
          local parts, last = {}, nil
          for _, r in J.iter(rewards) do
            if last ~= nil then
              parts[#parts + 1] = "\n"
              if last:rewardType() ~= r:rewardType() then parts[#parts + 1] = "\n" end
            end
            last = r
            parts[#parts + 1] = strip(r:getDisplayText())
          end
          g.rewards = table.concat(parts)
        end
      end
      g.wiki = self:wiki_url(quest)
      self.groups = g
    end

    function cls.wiki_url(self, quest)
      local q = quest:getQuest()
      if q == nil then return nil end
      local name = q:name()
      -- Java catches the valueOf miss and builds the URL; so does this, quietly.
      local ok, res = J.protect(function()
        return C["com.questhelper.questinfo.ExternalQuestResources"].valueOf(name:upper()):getWikiURL()
      end)
      if ok and res ~= nil then return J.str(res) end
      local enc = name:gsub("[^%w%-%._~]", function(c) return string.format("%%%02X", c:byte()) end)
      return "https://oldschool.runescape.wiki/w/" .. enc:lower()
    end

    -- QuestOverviewPanel.getAggregatedRequirements (static in Java).
    function cls.aggregated_requirements(quest)
      local all, seen = {}, {}
      local function collect(q)
        if q:getQuest() == nil then return end
        local reqs = q:getQuest():getQuestHelper():getGeneralRequirements()
        if reqs == nil then return end
        for _, r in J.iter(reqs) do
          if inst(r, "com.questhelper.requirements.quest.QuestRequirement") then
            local id = J.str(r:getQuest():getName())
            if not seen[id] then
              seen[id] = true
              all[#all + 1] = r
              collect(r:getQuest():getQuestHelper())
            end
          else
            local dup = false
            for _, a in ipairs(all) do if J.equals(a, r) then dup = true; break end end
            if not dup then all[#all + 1] = r end
          end
        end
      end
      collect(quest)
      local skills, skill_order, qp, others = {}, {}, 0, {}
      for _, r in ipairs(all) do
        if inst(r, "com.questhelper.requirements.player.SkillRequirement") then
          local s = r:getSkill()
          local have = skills[s]
          if have == nil then skill_order[#skill_order + 1] = s end
          if have == nil or r:getRequiredLevel() > have:getRequiredLevel() then skills[s] = r end
        elseif inst(r, "com.questhelper.requirements.quest.QuestPointRequirement") then
          if qp == 0 or r:getRequiredQuestPoints() > qp then qp = r:getRequiredQuestPoints() end
        else
          others[#others + 1] = r
        end
      end
      local out = C["java.util.ArrayList"].__new__()
      for _, r in ipairs(others) do out:add(r) end
      if qp ~= 0 then out:add(C["com.questhelper.requirements.quest.QuestPointRequirement"].__new__int(qp)) end
      for _, s in ipairs(skill_order) do out:add(skills[s]) end
      return out
    end

    function cls.updateStepsTexts(self)
      if self.sections == nil then return end
      for _, s in ipairs(self.sections) do s:update_text() end
      self:changed()
    end

    -- QuestHelperPanel.updateHighlight(Client, QuestStep).
    function cls.updateHighlight__Client_QuestStep(self, client, newStep)
      if self.current_quest == nil or self.sections == nil then return end
      for _, s in ipairs(self.sections) do s:update_highlight_check(client, newStep, self.current_quest) end
      self:changed()
    end

    function cls.updateLocks(self)
      if self.sections == nil then return end
      for _, s in ipairs(self.sections) do s:update_lock() end
      self:changed()
    end

    -- QuestOverviewPanel.updateRequirements(Client).
    function cls.updateItemRequirements(self, client)
      if self.groups == nil then return end
      local config = self:config()
      for _, k in ipairs({ "general", "recommended", "items", "items_rec" }) do self.groups[k]:update(client, config) end
      for _, s in ipairs(self.sections or {}) do s:update_requirements(client, config, self.current_quest) end
      if self.retry_requirements and self.retry_requirements == self.current_quest then
        local quest = self.retry_requirements
        self.retry_requirements = nil
        local ok, list = try("aggregated requirements", cls.aggregated_requirements, quest)
        if ok then
          self.groups.general = new_group("General requirements:", list, false)
          self.groups.general:update(client, config)
        end
      end
      self:changed()
    end

    function cls.overview_lines(self)
      local out = {}
      local g = self.groups
      if g then
        g.general:lines(out, self, 0)
        g.recommended:lines(out, self, 0)
        g.items:lines(out, self, 0)
        g.items_rec:lines(out, self, 0)
        out[#out + 1] = L { key = "o:combat", text = "Enemies to defeat:", rgb = WHITE, bg = DARKER_GRAY_COLOR, gap = 6 }
        out[#out + 1] = L { key = "o:combat:t", text = g.combat, rgb = GRAY }
        if g.notes then
          out[#out + 1] = L { key = "o:notes", text = "Notes:", rgb = WHITE, bg = DARKER_GRAY_COLOR, gap = 6 }
          out[#out + 1] = L { key = "o:notes:t", text = g.notes, rgb = GRAY }
        end
        out[#out + 1] = L { key = "o:rewards", text = "Rewards:", rgb = WHITE, bg = DARKER_GRAY_COLOR, gap = 6 }
        if g.rewards_hidden then
          out[#out + 1] = L { key = "o:rewards:t", text = "Hidden by the \"Hide quest rewards\" config", rgb = GRAY }
        elseif g.rewards then
          out[#out + 1] = L { key = "o:rewards:t", text = g.rewards, rgb = WHITE }
        else
          out[#out + 1] = L { key = "o:rewards:t", text = "None", rgb = GRAY }
        end
        if g.wiki then
          out[#out + 1] = L { key = "o:wiki", text = "External Resources:", rgb = WHITE, bg = DARKER_GRAY_COLOR, gap = 6 }
          -- QuestOverviewPanel's wiki button: LinkBrowser.browse(externalResource)
          out[#out + 1] = L { key = "o:wiki:t", text = "RuneScape Wiki: " .. g.wiki, rgb = LINK_RGB,
            click = function() C["net.runelite.client.util.LinkBrowser"].browse(g.wiki) end }
        end
      end
      for i, s in ipairs(self.sections or {}) do s:lines(out, self, tostring(i)) end
      return out
    end

    -- -------------------------------------------------------------- rows

    function cls.well_rows(self, rows, prefix, lines, max_wells)
      local wells = flow(prefix, lines, M.width, max_wells)
      for _, w in ipairs(wells) do
        local st = self.well_state[w.key]
        if st == nil then st = {}; self.well_state[w.key] = st end
        local ps, hs = paint_sig(w, M.width), hit_sig(w)
        if st.psig ~= ps then st.psig, st.pkey = ps, next_serial() end
        if st.hsig ~= hs then st.hsig, st.hkey = hs, next_serial() end
        self.wells_by_key[w.key] = w
        rows[#rows + 1] = { key = w.key, kind = "custom", height = math.max(WELL_MIN, w.h),
          hit_key = st.hkey, paint_key = st.pkey, paint = UI.well, on_action = UI.well, on_menu = UI.well }
      end
      return #wells
    end

    -- A fixed number of wells: the ones past the content paint nothing.
    function cls.padded_well_rows(self, rows, prefix, lines, count)
      local n = self:well_rows(rows, prefix, lines, count)
      for i = n + 1, count do
        local key = prefix .. i
        self.wells_by_key[key] = { key = key, lines = {}, h = 0 }
        local st = self.well_state[key] or {}
        self.well_state[key] = st
        if st.psig ~= "" then st.psig, st.pkey = "", next_serial() end
        if st.hsig ~= "" then st.hsig, st.hkey = "", next_serial() end
        rows[#rows + 1] = { key = key, kind = "custom", height = WELL_MIN, hit_key = st.hkey, paint_key = st.pkey,
          paint = UI.well, on_action = UI.well, on_menu = UI.well }
      end
    end

    function cls.grid_row(self, rows, key, grid, cols)
      -- a grid well: `grid` is rows of cells, each {text, bg, rgb, act}
      local lines = {}
      for r, row in ipairs(grid) do
        lines[#lines + 1] = L { key = key .. ":" .. r, text = "", cells = row.cells, cols = cols }
      end
      local cell_h = line_h() + 6
      local y = 0
      for _, l in ipairs(lines) do
        l.y, l.h, l.body_h, l.wrapped = y, cell_h + 2, cell_h, {}
        y = y + l.h
      end
      local w = { key = key, lines = lines, h = y, grid = true }
      local st = self.well_state[key] or {}
      self.well_state[key] = st
      local p = { tostring(M.width) }
      for _, l in ipairs(lines) do
        for _, c in ipairs(l.cells) do p[#p + 1] = c.text .. ":" .. c.bg .. ":" .. c.rgb end
      end
      local ps = table.concat(p, "|")
      if st.psig ~= ps then st.psig, st.pkey = ps, next_serial() end
      if st.hkey == nil then st.hsig, st.hkey = "grid", next_serial() end
      self.wells_by_key[key] = w
      rows[#rows + 1] = { key = key, kind = "custom", height = math.max(WELL_MIN, y), hit_key = st.hkey,
        paint_key = st.pkey, paint = UI.well, on_action = UI.well, on_menu = UI.well }
    end

    local function enum_select(rows, key, label, enum_key, values, selected, on_pick)
      local options = {}
      for i = 1, values.n do
        options[#options + 1] = { value = values[i]:name(), label = fit(title_case(values[i])) }
      end
      rows[#rows + 1] = { key = key, kind = "select", label = label, text = selected or options[1].value,
        options = options, on_action = function(_, action)
          if action.text == nil or action.text == "" then return end
          on_pick(C[enum_key].valueOf(action.text))
        end }
    end

    function cls.list_rows(self, rows)
      local cm = self.plugin:getConfigManager()
      -- QuestHelperPanel's Discord / GitHub / Patreon buttons (LinkBrowser.browse)
      local links = { cells = {} }
      for _, link in ipairs({ { "Discord", "https://discord.gg/XCfwNnz6RB" },
          { "GitHub", "https://github.com/Zoinkwiz/quest-helper" },
          { "Patreon", "https://www.patreon.com/zoinkwiz" } }) do
        links.cells[#links.cells + 1] = { text = link[1], bg = DARKER_GRAY_COLOR, rgb = LINK_RGB,
          act = function() C["net.runelite.client.util.LinkBrowser"].browse(link[2]) end }
      end
      self:grid_row(rows, "qh_web", { links }, 3)
      local shown = 0
      for _, e in ipairs(self:visible_entries()) do if e.header == nil then shown = shown + 1 end end
      rows[#rows + 1] = { key = "qh_msg", kind = "paragraph",
        text = fit(self.message_visible and self.list_message
          or (shown .. (shown == 1 and " helper" or " helpers") .. (self.search ~= "" and (" match \"" .. self.search .. "\"") or ""))) }
      local filters = C[FILTER_KEY].displayFilters()
      enum_select(rows, "qh_filter", "Filters", FILTER_KEY, filters, self.filter_value, function(v)
        self.filter_value = v:name()
        cm:setConfiguration__String_String_Object(QUEST_HELPER_GROUP, "filterListBy", v)
      end)
      enum_select(rows, "qh_difficulty", "Difficulty", DIFFICULTY_KEY, C[DIFFICULTY_KEY].values(), self.difficulty_value, function(v)
        self.difficulty_value = v:name()
        cm:setConfiguration__String_String_Object(QUEST_HELPER_GROUP, "questDifficulty", v)
      end)
      enum_select(rows, "qh_order", "Ordering", ORDER_KEY, C[ORDER_KEY].values(), self.order_value, function(v)
        self.order_value = v:name()
        cm:setConfiguration__String_String_Object(QUEST_HELPER_GROUP, "orderListBy", v)
      end)
      rows[#rows + 1] = { key = "qh_skill", kind = "action_row", label = "Skill filtering",
        text = (self.skill_count > 0 and (self.skill_count .. " active ") or "") .. (self.skills_open and "(hide)" or "(show)"),
        on_action = function() self.skills_open = not self.skills_open; self:changed() end }
      if self.skills_open then
        local grid, cols = self:skill_lines()
        self:grid_row(rows, "qh_skills", grid, cols)
      end
      if self.region_visible then
        local n = 0
        for _ in pairs(self.regions) do n = n + 1 end
        rows[#rows + 1] = { key = "qh_region", kind = "action_row", label = "Region filtering",
          text = (n > 0 and (n .. " active ") or "") .. (self.regions_open and "(hide)" or "(show)"),
          on_action = function() self.regions_open = not self.regions_open; self:changed() end }
        if self.regions_open then
          local grid, cols = self:region_lines()
          self:grid_row(rows, "qh_regions", grid, cols)
        end
      end
      local room = math.max(1, ROWS_BUDGET - #rows)
      local lines = self:list_lines()
      self.list_wells_max = math.max(self.list_wells_max, #flow("qh_l", lines, M.width, room))
      self:padded_well_rows(rows, "qh_l", lines, math.min(room, self.list_wells_max))
    end

    function cls.overview_rows(self, rows)
      rows[#rows + 1] = { key = "qh_qname", kind = "heading", text = fit(self.name or "") }
      rows[#rows + 1] = { key = "qh_close", kind = "button", text = "Close helper",
        on_action = function() self.questManager:shutDownQuest__boolean(false) end }
      local cm = self.plugin:getConfigManager()
      for i, hc in ipairs(self.configs or {}) do
        if #rows >= ROWS_BUDGET - 2 then break end
        local values = hc:getEnums()
        local key = hc:getKey()
        local cur = cm:getRSProfileConfiguration__String_String(QUEST_BACKGROUND_GROUP, key)
        local selected = nil
        for j = 1, values.n do if values[j]:name() == cur then selected = cur end end
        local options = {}
        for j = 1, values.n do options[#options + 1] = { value = values[j]:name(), label = fit(title_case(values[j])) } end
        if #options > 0 then
          rows[#rows + 1] = { key = "qh_cfg" .. i, kind = "select", label = fit(J.str(hc:getName())),
            text = selected or options[1].value, options = options,
            on_action = function(_, action)
              for j = 1, values.n do
                if values[j]:name() == action.text then
                  cm:setRSProfileConfiguration(QUEST_BACKGROUND_GROUP, key, values[j])
                end
              end
              self:changed()
            end }
        end
      end
      -- Never fewer wells than this quest has had: a section collapsing is
      -- then setters on wells that exist, not a page rebuild.
      local room = math.max(1, ROWS_BUDGET - #rows)
      local lines = self:overview_lines()
      local need = #flow("qh_o", lines, M.width, room)
      self.ov_wells = math.min(room, math.max(self.ov_wells or 1, need))
      self:padded_well_rows(rows, "qh_o", lines, self.ov_wells)
    end

    function cls.settings_rows(self, rows)
      rows[#rows + 1] = { key = "qh_a_title", kind = "heading", text = "Choose Assist Level" }
      local active = self.configManager:getConfiguration__String_String(QUEST_HELPER_GROUP,
        C["com.questhelper.config.AssistanceLevel"].ASSISTANCE_LEVEL_KEY)
      for i, a in ipairs(ASSIST) do
        rows[#rows + 1] = { key = "qh_a" .. i .. "h", kind = "label", text = a.title .. (active == a.level and " (active)" or "") }
        for j, t in ipairs(a.text) do
          rows[#rows + 1] = { key = "qh_a" .. i .. "p" .. j, kind = "paragraph", text = fit(t) }
        end
        rows[#rows + 1] = { key = "qh_a" .. i .. "b", kind = "button", text = a.button,
          on_action = function() self:choose_assist(a) end }
      end
    end

    function cls.loading_rows(self, rows)
      local l = self.loading
      if l == nil then return end
      if l.failed then
        rows[#rows + 1] = { key = "qh_msg", kind = "paragraph", text = fit("Could not load " .. l.name .. ": " .. l.failed) }
      else
        rows[#rows + 1] = { key = "qh_msg", kind = "paragraph", text = fit("Loading " .. l.name .. "...") }
      end
      rows[#rows + 1] = { key = "qh_cancel", kind = "button", text = l.failed and "Back" or "Cancel",
        on_action = function()
          local back = self.loading and self.loading.back or VIEW_LIST
          self.loading = nil
          self.view = back == VIEW_LOADING and VIEW_LIST or back
          self:changed()
        end }
    end

    function cls.build_rows(self)
      local rows = {}
      self.wells_by_key = {}
      local settings = self.view == VIEW_SETTINGS
      rows[#rows + 1] = { key = "qh_settings", kind = "button", text = settings and "Back" or "Assist level",
        on_action = function()
          if self.view == VIEW_SETTINGS then self:deactivate_settings() else self:activate_settings() end
        end }
      if self.view == VIEW_LIST then self:list_rows(rows)
      elseif self.view == VIEW_OVERVIEW then self:overview_rows(rows)
      elseif self.view == VIEW_SETTINGS then self:settings_rows(rows)
      elseif self.view == VIEW_LOADING then self:loading_rows(rows) end
      assert(#rows <= ROWS_BUDGET + 1, "quest helper panel declared more rows than the page holds")
      return rows
    end

    -- Porcelain's describe hook (forwarded by QH.ui.describe).
    function cls.describe(self, d)
      if self.rows_rev ~= self.rev then
        self.icons_pending = false
        self.rows = self:build_rows()
        self.rows_rev = self.rev
      end
      for _, r in ipairs(self.rows) do d.row(r) end
    end

    -- -------------------------------------------------------------- wells

    -- A failure inside paint must not leave in_paint standing: every later
    -- change would then wait for a frame that never re-describes.
    function cls.well_paint(self, key, draw)
      local w = self.wells_by_key[key]
      if w == nil then return end
      self.in_paint = true
      try("paint " .. key, cls.paint_lines, self, w, draw)
      self.in_paint = false
    end

    function cls.paint_lines(self, w, draw)
      local ctx = draw.context()
      local width = ctx and ctx.bounds and ctx.bounds.width or M.width
      if width > 0 and width ~= M.width then M.width = width; M.dirty = true end
      if not M.measured_h and type(draw.text_height) == "function" then
        M.measured_h = true
        local h = draw.text_height()
        if type(h) == "number" and h > 0 and h ~= M.text_h then M.text_h = h; M.dirty = true end
      end
      local lh, asc = line_h(), ascent()
      for _, l in ipairs(w.lines) do
        local top = l.y + (l.gap or 0)
        local h = l.body_h
        if l.cells then
          local cw = width // l.cols
          for c, cell in ipairs(l.cells) do
            local x = (c - 1) * cw
            draw.rect(x + 1, top, cw - 2, h, cell.bg)
            learn(draw, cell.text)
            local tw = text_w(cell.text)
            draw.text(x + math.max(2, (cw - tw) // 2), top + (h - lh) // 2 + asc + 2, cell.text, cell.rgb)
          end
        else
          if l.bg then draw.rect(0, top, width, h, l.bg) end
          if l.border then draw.line(0, top, width - 1, top, STEP_BORDER) end
          if l.icon_ref then draw.image(l.icon_ref, 4 + (l.indent or 0), top + (h - ICON_H) // 2) end
          local ty = top + PAD
          for _, s in ipairs(l.wrapped) do
            if s ~= "" then
              learn(draw, s)
              draw.text(l.tx, ty + asc, s, l.rgb)
            end
            ty = ty + lh
          end
          for _, s in ipairs(l.sub_wrapped or {}) do
            if s ~= "" then
              learn(draw, s)
              draw.text(l.tx, ty + asc, s, l.sub_rgb or GRAY)
            end
            ty = ty + lh
          end
          if l.check ~= nil then
            local bx, by = width - CHECK - 6, top + (h - CHECK) // 2
            local rgb = l.check_on == false and DARK_GRAY or BOX_RGB
            draw.line(bx, by, bx + CHECK, by, rgb)
            draw.line(bx, by + CHECK, bx + CHECK, by + CHECK, rgb)
            draw.line(bx, by, bx, by + CHECK, rgb)
            draw.line(bx + CHECK, by, bx + CHECK, by + CHECK, rgb)
            if l.check then draw.rect(bx + 3, by + 3, CHECK - 5, CHECK - 5, rgb) end
          end
        end
      end
    end

    function cls.well_action(self, key, ev)
      try("click " .. key, cls.well_hit, self, key, ev)
    end

    function cls.well_hit(self, key, ev)
      local w = self.wells_by_key[key]
      if w == nil then return end
      local hit = nil
      for _, l in ipairs(w.lines) do
        if ev.y >= l.y and ev.y < l.y + l.h then hit = l; break end
      end
      if hit == nil then return end
      if hit.cells then
        if ev.action ~= "activate" then return end
        local cw = M.width // hit.cols
        local cell = hit.cells[ev.x // math.max(1, cw) + 1]
        if cell then cell.act(self) end
        return
      end
      if ev.action == "menu" then
        if hit.menu then hit.menu(self, hit) end
        return
      end
      if ev.action ~= "activate" then return end
      if hit.check ~= nil and hit.on_check and ev.x >= M.width - CHECK_ZONE then
        hit.on_check(self, hit)
        return
      end
      if hit.click then hit.click(self, hit) end
    end

    -- Per frame: a measure learned in paint, or an icon that landed, re-lays
    -- the page out here (paint may not schedule native work).
    function cls.frame(self)
      if M.dirty then
        M.dirty = false
        forget_wraps()
        self:changed()
        return
      end
      if self.owed then
        self.owed = false
        self:changed()
        return
      end
      if self.icons_pending then
        for k, e in pairs(self.icons) do
          if e.state == "pending" then
            local id, qty = k:match("^(%d+):(%d+)$")
            local ref, state = api().game.item_image(tonumber(id), tonumber(qty))
            if state ~= "pending" then
              e.ref, e.state = ref, state or (ref and "ready" or "missing")
              self.icons_pending = false
              self:changed()
              return
            end
          end
        end
      end
    end
  end })

-- ------------------------------------------------------------------ the entry's hooks

-- What the plugin entry registers in on_start: api.porcelain.panel(UI.descriptor).
UI.descriptor = { width = 320, faces = "page" }

-- One closure for paint, on_action and on_menu of every well: the binding
-- keeps one callback per row, so the three must be the same function, told
-- apart by what they are handed (an action table carries `action`).
function UI.well(key, arg)
  assert(current, "a quest helper well with no panel")
  if type(arg) == "table" and rawget(arg, "action") ~= nil then
    return current:well_action(key, arg)
  end
  return current:well_paint(key, arg)
end

-- From the entry's describe function: the page's rows.
function UI.describe(d)
  if current == nil then return end
  current:describe(d)
end

-- on_ui_build: the search field (a raw node, before Porcelain's rows), then
-- Porcelain's rows.
function UI.build(a, panel, view)
  if view ~= "settings" and current ~= nil then
    panel.node({ kind = PANEL_NODE_INPUT, id = SEARCH_ID, label = "Search", text = fit(current.search) })
  end
  a.porcelain.panel_build(view)
end

-- on_ui_action: Porcelain's rows first, then the search field.
function UI.action(a, ev)
  if a.porcelain.panel_action(ev) then return true end
  if ev.id == SEARCH_ID and ev.action == "text" and current ~= nil then
    current:on_search(ev.text)
    return true
  end
  return false
end

-- on_ui_draw.
function UI.draw(a, node, draw)
  return a.porcelain.panel_draw(node)
end

-- on_frame_start.
function UI.frame(a)
  if current ~= nil then current:frame() end
end

-- The panel the plugin built, for tests and the entry.
function UI.current() return current end

-- Test seam: the metrics table.
UI._metrics = M
