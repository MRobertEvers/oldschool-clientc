-- RuneLite's overlay system for the Quest Helper port: Overlay, OverlayPanel,
-- OverlayManager, OverlayUtil, the panel components (PanelComponent,
-- LineComponent, TitleComponent, TextComponent, BackgroundComponent,
-- TooltipComponent), ModelOutlineRenderer, Tooltip/TooltipManager,
-- WorldMapPoint/WorldMapPointManager, ImageUtil -- and QH.render, the
-- per-pass renderer the plugin entry calls:
--
--   on_draw_world  -> QH.render("world", draw)
--   on_draw_canvas -> QH.render("canvas", draw)
--
-- Which overlay goes in which pass, and how shapes and images reach `draw`,
-- is docs/QUEST_HELPER_PORT.md "Passes, shapes and images". Layout follows
-- RuneLite's OverlayRenderer / SnapCorners / the components line for line
-- where it can.

local J, C = J, C

local function api() return QH.api end

local function def(key, super, ifaces, body, clinit, kind)
  J.def(key, { kind = kind or "class", super = super or "java.lang.Object", ifaces = ifaces or {}, body = body, clinit = clinit })
end
local function iface(key, ifaces, body)
  J.def(key, { kind = "interface", super = "java.lang.Object", ifaces = ifaces or {}, body = body })
end

local P = "net.runelite.client.ui.overlay."
local function Rect(x, y, w, h) return C["java.awt.Rectangle"].__new__int_int_int_int(x, y, w, h) end
local function Pt(x, y) return C["java.awt.Point"].__new__int_int(x, y) end
local function Dim(w, h) return C["java.awt.Dimension"].__new__int_int(w, h) end
local function color(r, g, b, a) return C["java.awt.Color"].__new__int_int_int_int(r, g, b, a) end

-- Text.removeTags' rule for measuring: every <...> tag is dropped.
local function remove_tags(s) return (s:gsub("<[^>]*>", "")) end

-- Module state: the runtime's services are singletons (one per root
-- injector), and QH.render reads what they hold.
local S = {
  overlays = {},       -- in add order
  map_points = {},
  tooltips = {},
  failed = {},         -- overlay class key -> true once its error is logged
  prev_tooltip = { w = 0, h = 0 },
}

-- ====================================================================== components

iface(P .. "RenderableEntity")
iface(P .. "components.LayoutableRenderableEntity", { P .. "RenderableEntity" })

J.ext_enum(P .. "components.ComponentOrientation", { "HORIZONTAL", "VERTICAL" }, {})

def(P .. "components.ComponentConstants", nil, {}, function(cls) end, function(cls)
  cls.STANDARD_BORDER = 4
  cls.STANDARD_WIDTH = 129
  cls.STANDARD_BACKGROUND_COLOR = color(70, 61, 50, 156)
end)

local function constants() return C[P .. "components.ComponentConstants"] end

def(P .. "components.BackgroundComponent", nil, { P .. "RenderableEntity" }, function(cls)
  local BORDER_OFFSET = 2
  local COLOR_OFFSET = 0.2
  local OUTER, INNER, ALPHA = 1 - COLOR_OFFSET, 1 + COLOR_OFFSET, 1 + 2 * COLOR_OFFSET
  function cls.__init__(self)
    self.backgroundColor = constants().STANDARD_BACKGROUND_COLOR
    self.rectangle = Rect(0, 0, 0, 0)
    self.fill = true
  end
  function cls.__init__Color_Rectangle_boolean(self, c, r, f) self.backgroundColor, self.rectangle, self.fill = c, r, f end
  function cls.setBackgroundColor(self, c) self.backgroundColor = c end
  function cls.setRectangle(self, r) self.rectangle = r end
  function cls.setFill(self, f) self.fill = f end
  function cls.render(self, g)
    local bg = self.backgroundColor
    local int = J.d2i
    local outside = color(int(bg:getRed() * OUTER), int(bg:getGreen() * OUTER), int(bg:getBlue() * OUTER),
      math.min(255, int(bg:getAlpha() * ALPHA)))
    local inside = color(math.min(255, int(bg:getRed() * INNER)), math.min(255, int(bg:getGreen() * INNER)),
      math.min(255, int(bg:getBlue() * INNER)), math.min(255, int(bg:getAlpha() * ALPHA)))
    local r = self.rectangle
    if self.fill then
      g:setColor(bg)
      g:fill(r)
    end
    local half = BORDER_OFFSET // 2
    g:setColor(outside)
    g:draw(Rect(r.x, r.y, r.width - half, r.height - half))
    g:setColor(inside)
    g:draw(Rect(r.x + half, r.y + half, r.width - BORDER_OFFSET - half, r.height - BORDER_OFFSET - half))
    return Dim(r.width, r.height)
  end
end)

-- TextComponent: <col=rrggbb> runs, a black shadow (or outline) under each.
def(P .. "components.TextComponent", nil, { P .. "RenderableEntity" }, function(cls)
  function cls.__init__(self)
    self.text = ""
    self.positionX, self.positionY = 0, 0
    self.color = C["java.awt.Color"].WHITE
    self.outline = false
    self.font = nil
  end
  function cls.setText(self, t) self.text = t end
  function cls.setColor(self, c) self.color = c end
  function cls.setOutline(self, o) self.outline = o end
  function cls.setFont(self, f) self.font = f end
  function cls.setPosition__int_int(self, x, y) self.positionX, self.positionY = x, y end
  function cls.setPosition__Point(self, p) self.positionX, self.positionY = p.x, p.y end
  local function render_text(self, g, c, x, y, s)
    if s == "" then return end
    g:setColor(C["java.awt.Color"].BLACK)
    if self.outline then
      g:drawString__String_int_int(s, x, y + 1)
      g:drawString__String_int_int(s, x, y - 1)
      g:drawString__String_int_int(s, x + 1, y)
      g:drawString__String_int_int(s, x - 1, y)
    else
      g:drawString__String_int_int(s, x + 1, y + 1)
    end
    g:setColor(c)
    g:drawString__String_int_int(s, x, y)
  end
  function cls.render(self, g)
    local original
    if self.font ~= nil then
      original = g:getFont()
      g:setFont(self.font)
    end
    local fm = g:getFontMetrics__()
    local text = self.text or ""
    local c = self.color
    local width = 0
    local idx = 1
    while true do
      local s, e, hex = text:find("<col=(%x%x%x?%x?%x?%x?)>", idx)
      if s == nil then break end
      local part = text:sub(idx, s - 1)
      render_text(self, g, c, self.positionX + width, self.positionY, part)
      width = width + fm:stringWidth(part)
      c = C["java.awt.Color"].decode("#" .. hex)
      idx = e + 1
    end
    local rest = text:sub(idx)
    render_text(self, g, c, self.positionX + width, self.positionY, rest)
    width = width + fm:stringWidth(rest)
    local height = fm:getHeight()
    if original ~= nil then g:setFont(original) end
    return Dim(width, height)
  end
end)

local function new_text() return C[P .. "components.TextComponent"].__new__() end

def(P .. "components.PanelComponent", nil, { P .. "components.LayoutableRenderableEntity" }, function(cls)
  function cls.__init(self)
    local k = constants()
    self.bounds = Rect(0, 0, 0, 0)
    self.preferredLocation = Pt(0, 0)
    self.preferredSize = Dim(k.STANDARD_WIDTH, 0)
    self.backgroundColor = k.STANDARD_BACKGROUND_COLOR
    self.children = C["java.util.ArrayList"].__new__()
    self.orientation = C[P .. "components.ComponentOrientation"].VERTICAL
    self.wrap = false
    self.border = Rect(k.STANDARD_BORDER, k.STANDARD_BORDER, k.STANDARD_BORDER, k.STANDARD_BORDER)
    self.gap = Pt(0, 0)
    self.childDimensions = Dim(0, 0)
  end
  function cls.getBounds(self) return self.bounds end
  function cls.getChildren(self) return self.children end
  function cls.setPreferredLocation(self, p) self.preferredLocation = p end
  function cls.setPreferredSize(self, d) self.preferredSize = d end
  function cls.getPreferredSize(self) return self.preferredSize end
  function cls.setBackgroundColor(self, c) self.backgroundColor = c end
  function cls.getBackgroundColor(self) return self.backgroundColor end
  function cls.setOrientation(self, o) self.orientation = o end
  function cls.setWrap(self, w) self.wrap = w end
  function cls.setBorder(self, r) self.border = r end
  function cls.setGap(self, p) self.gap = p end

  function cls.render(self, g)
    if self.children:isEmpty() then return nil end
    local border, cd, pl, ps, gap = self.border, self.childDimensions, self.preferredLocation, self.preferredSize, self.gap
    local VERTICAL = C[P .. "components.ComponentOrientation"].VERTICAL
    local vertical = self.orientation == VERTICAL
    local dimension = Dim(border.x + cd.width + border.width, border.y + cd.height + border.height)
    if self.backgroundColor ~= nil then
      local bgc = C[P .. "components.BackgroundComponent"].__new__()
      bgc:setRectangle(C["java.awt.Rectangle"].__new__Point_Dimension(pl, dimension))
      bgc:setBackgroundColor(self.backgroundColor)
      bgc:render(g)
    end
    local baseX, baseY = pl.x + border.x, pl.y + border.y
    local width, height = 0, 0
    local x, y = baseX, baseY
    local cpw, cph = ps.width - border.x - border.width, ps.height - border.y - border.height
    local totalWidth, totalHeight = 0, 0
    for _, child in J.iter(self.children) do
      if not self.wrap then
        if vertical then child:setPreferredSize(Dim(cpw, 0)) else child:setPreferredSize(Dim(0, cph)) end
      end
      child:setPreferredLocation(Pt(x, y))
      local d = child:render(g)
      if vertical then
        height = height + d.height + gap.y
        y = baseY + height
        width = math.max(width, d.width)
      else
        width = width + d.width + gap.x
        x = baseX + width
        height = math.max(height, d.height)
      end
      totalWidth = math.max(totalWidth, width)
      totalHeight = math.max(totalHeight, height)
      if self.wrap then
        if vertical then
          if cph > 0 and height >= cph then
            height = 0
            y = baseY
            local diff = d.width + gap.x
            x = x + diff
            width = width + diff
          end
        else
          if cpw > 0 and width >= cpw then
            width = 0
            x = baseX
            local diff = d.height + gap.y
            y = y + diff
            height = height + diff
          end
        end
      end
    end
    if vertical then totalHeight = totalHeight - gap.y else totalWidth = totalWidth - gap.x end
    cd:setSize__int_int(totalWidth, totalHeight)
    self.bounds:setLocation__Point(pl)
    self.bounds:setSize__Dimension(dimension)
    return dimension
  end
end)

-- LineComponent and its builder (Lombok @Builder, @Builder.Default fields).
def(P .. "components.LineComponent", nil, { P .. "components.LayoutableRenderableEntity" }, function(cls)
  function cls.__init(self, b)
    local k = constants()
    self.left, self.right = b.left, b.right
    self.leftColor = b.leftColor or C["java.awt.Color"].WHITE
    self.rightColor = b.rightColor or C["java.awt.Color"].WHITE
    self.leftFont, self.rightFont = b.leftFont, b.rightFont
    self.preferredLocation = b.preferredLocation or Pt(0, 0)
    self.preferredSize = b.preferredSize or Dim(k.STANDARD_WIDTH, 0)
    self.bounds = Rect(0, 0, 0, 0)
  end
  function cls.builder() return C[P .. "components.LineComponent$LineComponentBuilder"].__new() end
  function cls.getBounds(self) return self.bounds end
  function cls.setLeft(self, s) self.left = s end
  function cls.setRight(self, s) self.right = s end
  function cls.setLeftColor(self, c) self.leftColor = c end
  function cls.setRightColor(self, c) self.rightColor = c end
  function cls.setLeftFont(self, f) self.leftFont = f end
  function cls.setRightFont(self, f) self.rightFont = f end
  function cls.setPreferredLocation(self, p) self.preferredLocation = p end
  function cls.setPreferredSize(self, d) self.preferredSize = d end

  local function line_width(s, fm) return fm:stringWidth(remove_tags(s)) end
  local function split_words(s)
    local out = {}
    -- Java's split(" "): empty pieces between spaces are kept, trailing ones dropped
    local i = 1
    while true do
      local j = s:find(" ", i, true)
      if j == nil then out[#out + 1] = s:sub(i); break end
      out[#out + 1] = s:sub(i, j - 1)
      i = j + 1
    end
    while #out > 0 and out[#out] == "" do out[#out] = nil end
    return out
  end
  local function line_break(text, max_width, fm)
    local words = split_words(text)
    if #words == 0 then return {} end
    local lines = { words[1] }
    local space_left = max_width - fm:stringWidth(words[1])
    local space_w = fm:stringWidth(" ")
    for i = 2, #words do
      local word = words[i]
      local wl = fm:stringWidth(word)
      if wl + space_w > space_left then
        lines[#lines + 1] = word
        space_left = max_width - wl
      else
        lines[#lines] = lines[#lines] .. " " .. word
        space_left = space_left - (space_w + wl)
      end
    end
    return lines
  end

  function cls.render(self, g)
    local left = self.left or ""
    local right = self.right or ""
    local lfont = self.leftFont or g:getFont()
    local rfont = self.rightFont or g:getFont()
    local lfm, rfm = g:getFontMetrics__Font(lfont), g:getFontMetrics__Font(rfont)
    local fmh = math.max(lfm:getHeight(), rfm:getHeight())
    local pl, ps = self.preferredLocation, self.preferredSize
    local baseX, baseY = pl.x, pl.y + fmh
    local x, y = baseX, baseY
    local lfw, rfw = line_width(left, lfm), line_width(right, rfm)
    local tc = new_text()
    if ps.width < lfw + rfw then
      local lsw, rsw = ps.width, 0
      if right ~= "" then
        rsw = J.idiv(ps.width, 3)
        lsw = lsw - rsw
      end
      local ll = line_break(left, lsw, lfm)
      local rl = line_break(right, rsw, rfm)
      local n = math.max(#ll, #rl)
      for i = 1, n do
        if i <= #ll then
          tc:setPosition__int_int(x, y); tc:setText(ll[i]); tc:setColor(self.leftColor); tc:setFont(lfont); tc:render(g)
        end
        if i <= #rl then
          tc:setPosition__int_int(x + ps.width - line_width(rl[i], rfm), y)
          tc:setText(rl[i]); tc:setColor(self.rightColor); tc:setFont(rfont); tc:render(g)
        end
        y = y + fmh
      end
      local d = Dim(ps.width, y - baseY)
      self.bounds:setLocation__Point(pl)
      self.bounds:setSize__Dimension(d)
      return d
    end
    if left ~= "" then
      tc:setPosition__int_int(x, y); tc:setText(left); tc:setColor(self.leftColor); tc:setFont(lfont); tc:render(g)
    end
    if right ~= "" then
      tc:setPosition__int_int(x + ps.width - rfw, y); tc:setText(right); tc:setColor(self.rightColor); tc:setFont(rfont); tc:render(g)
    end
    y = y + fmh
    local d = Dim(ps.width, y - baseY)
    self.bounds:setLocation__Point(pl)
    self.bounds:setSize__Dimension(d)
    return d
  end
end)

def(P .. "components.LineComponent$LineComponentBuilder", nil, {}, function(cls)
  function cls.__init(self) self.qh_b = {} end
  for _, f in ipairs({ "left", "right", "leftColor", "rightColor", "leftFont", "rightFont", "preferredLocation", "preferredSize" }) do
    cls[f] = function(self, v) self.qh_b[f] = v; return self end
  end
  function cls.build(self) return C[P .. "components.LineComponent"].__new(self.qh_b) end
end)

def(P .. "components.TitleComponent", nil, { P .. "components.LayoutableRenderableEntity" }, function(cls)
  function cls.__init(self, b)
    self.text = b.text
    self.color = b.color or C["java.awt.Color"].WHITE
    self.preferredLocation = b.preferredLocation or Pt(0, 0)
    self.preferredSize = b.preferredSize or Dim(constants().STANDARD_WIDTH, 0)
    self.bounds = Rect(0, 0, 0, 0)
  end
  function cls.builder() return C[P .. "components.TitleComponent$TitleComponentBuilder"].__new() end
  function cls.getBounds(self) return self.bounds end
  function cls.setText(self, s) self.text = s end
  function cls.setColor(self, c) self.color = c end
  function cls.setPreferredLocation(self, p) self.preferredLocation = p end
  function cls.setPreferredSize(self, d) self.preferredSize = d end
  function cls.render(self, g)
    local pl, ps = self.preferredLocation, self.preferredSize
    local fm = g:getFontMetrics__()
    local tc = new_text()
    tc:setText(self.text)
    tc:setColor(self.color)
    tc:setPosition__int_int(pl.x + J.idiv(ps.width - fm:stringWidth(self.text), 2), pl.y + fm:getHeight())
    local rendered = tc:render(g)
    local d = Dim(ps.width, rendered.height)
    self.bounds:setLocation__Point(pl)
    self.bounds:setSize__Dimension(d)
    return d
  end
end)
def(P .. "components.TitleComponent$TitleComponentBuilder", nil, {}, function(cls)
  function cls.__init(self) self.qh_b = {} end
  for _, f in ipairs({ "text", "color", "preferredLocation", "preferredSize" }) do
    cls[f] = function(self, v) self.qh_b[f] = v; return self end
  end
  function cls.build(self) return C[P .. "components.TitleComponent"].__new(self.qh_b) end
end)

-- TooltipComponent: </br> lines, <col=..>, </col>; <img=..> mod icons are
-- not drawn (the client gives the plugin no mod icon sprites).
def(P .. "components.TooltipComponent", nil, { P .. "components.LayoutableRenderableEntity" }, function(cls)
  local OFFSET = 4
  function cls.__init__(self)
    self.text = ""
    self.backgroundColor = constants().STANDARD_BACKGROUND_COLOR
    self.position = Pt(0, 0)
    self.bounds = Rect(0, 0, 0, 0)
  end
  function cls.setText(self, t) self.text = t end
  function cls.setBackgroundColor(self, c) self.backgroundColor = c end
  function cls.setModIcons(self, m) end
  function cls.setPreferredLocation(self, p) self.position = p end
  function cls.setPreferredSize(self, d) end
  function cls.getBounds(self) return self.bounds end
  local function lines(text)
    local out, i = {}, 1
    while true do
      local s, e = text:find("</br>", i, true)
      if s == nil then out[#out + 1] = text:sub(i); break end
      out[#out + 1] = text:sub(i, s - 1)
      i = e + 1
    end
    return out
  end
  function cls.render(self, g)
    local fm = g:getFontMetrics__()
    local descent, th = fm:getDescent(), fm:getHeight()
    local ls = lines(self.text or "")
    local tw, tht = 0, 0
    for _, l in ipairs(ls) do
      tw = math.max(tw, fm:stringWidth(remove_tags(l)))
      tht = tht + th
    end
    local x, y = self.position.x, self.position.y
    local bg = C[P .. "components.BackgroundComponent"].__new__()
    bg:setBackgroundColor(self.backgroundColor)
    bg:setRectangle(Rect(x, y, tw + OFFSET * 2, tht + OFFSET * 2))
    bg:render(g)
    local textX, textY = x + OFFSET, y + OFFSET
    local next_color = C["java.awt.Color"].WHITE
    for i, line in ipairs(ls) do
      local lineX = textX
      local baseline = textY + i * th - descent
      local idx = 1
      while true do
        local s, e, tag = line:find("<([^>]*)>", idx)
        local seg = line:sub(idx, (s or (#line + 1)) - 1)
        if seg ~= "" then
          local tc = new_text()
          tc:setColor(next_color); tc:setText(seg); tc:setPosition__int_int(lineX, baseline); tc:render(g)
          lineX = lineX + fm:stringWidth(seg)
        end
        if s == nil then break end
        if tag:sub(1, 4) == "col=" then
          next_color = C["java.awt.Color"].decode("#" .. tag:sub(5))
        elseif tag == "/col" then
          next_color = C["java.awt.Color"].WHITE
        elseif tag:sub(1, 4) ~= "img=" then
          local raw = "<" .. tag .. ">"
          local tc = new_text()
          tc:setColor(next_color); tc:setText(raw); tc:setPosition__int_int(lineX, baseline); tc:render(g)
          lineX = lineX + fm:stringWidth(raw)
        end
        idx = e + 1
      end
    end
    local d = Dim(tw + OFFSET * 2, tht + OFFSET * 2)
    self.bounds:setBounds__int_int_int_int(x, y, d.width, d.height)
    return d
  end
end)

-- ====================================================================== Overlay

def(P .. "OverlayMenuEntry", nil, {}, function(cls)
  function cls.__init(self, action, option, target) self.menuAction, self.option, self.target = action, option, target end
  function cls.getMenuAction(self) return self.menuAction end
  function cls.getOption(self) return self.option end
  function cls.getTarget(self) return self.target end
  function cls.equals(self, o)
    return J.inst(o, P .. "OverlayMenuEntry") and o.menuAction == self.menuAction and o.option == self.option and o.target == self.target
  end
end)

def(P .. "Overlay", nil, { P .. "components.LayoutableRenderableEntity" }, function(cls)
  cls.PRIORITY_LOW, cls.PRIORITY_DEFAULT, cls.PRIORITY_MED, cls.PRIORITY_HIGH, cls.PRIORITY_HIGHEST = 0.0, 0.25, 0.5, 0.75, 1.0
  local function init(self, plugin)
    self.qh_ov = {
      plugin = plugin,
      preferredLocation = nil,
      preferredSize = nil,
      preferredPosition = nil,
      bounds = Rect(0, 0, 0, 0),
      position = C[P .. "OverlayPosition"].TOP_LEFT,
      priority = 0.25,
      layer = C[P .. "OverlayLayer"].UNDER_WIDGETS,
      drawHooks = C["java.util.ArrayList"].__new__(),
      menuEntries = C["java.util.ArrayList"].__new__(),
      resizable = false, minimumSize = 32, resettable = true,
      dragTargetable = false, movable = true, snappable = true,
    }
  end
  function cls.__init__(self) init(self, nil) end
  function cls.__init__Plugin(self, plugin) init(self, plugin) end

  local function o(self) return self.qh_ov end
  function cls.render(self, g) return nil end
  function cls.getPlugin(self) return o(self).plugin end
  function cls.getBounds(self) return o(self).bounds end
  function cls.getPreferredLocation(self) return o(self).preferredLocation end
  function cls.setPreferredLocation(self, p) o(self).preferredLocation = p end
  function cls.getPreferredSize(self) return o(self).preferredSize end
  function cls.setPreferredSize(self, d) o(self).preferredSize = d end
  function cls.getPreferredPosition(self) return o(self).preferredPosition end
  function cls.setPreferredPosition(self, p) o(self).preferredPosition = p end
  function cls.getPosition(self) return o(self).position end
  function cls.setPosition(self, p)
    local ov = o(self)
    ov.position = p
    local POS = C[P .. "OverlayPosition"]
    if p == POS.TOOLTIP or p == POS.DYNAMIC then ov.movable, ov.snappable = false, false
    elseif p == POS.DETACHED then ov.movable, ov.snappable = true, false
    else ov.movable, ov.snappable = true, true end
  end
  function cls.getPriority(self) return o(self).priority end
  function cls.setPriority__float(self, p) o(self).priority = p end
  function cls.setPriority__OverlayPriority(self, p)
    local PR = C[P .. "OverlayPriority"]
    local v = ({ [PR.LOW] = 0.0, [PR.NONE] = 0.25, [PR.MED] = 0.5, [PR.HIGH] = 0.75, [PR.HIGHEST] = 1.0 })[p]
    if v ~= nil then o(self).priority = v end
  end
  function cls.getLayer(self) return o(self).layer end
  function cls.setLayer(self, l) o(self).layer = l end
  function cls.getDrawHooks(self) return o(self).drawHooks end
  function cls.getMenuEntries(self) return o(self).menuEntries end
  function cls.isResizable(self) return o(self).resizable end
  function cls.setResizable(self, v) o(self).resizable = v end
  function cls.getMinimumSize(self) return o(self).minimumSize end
  function cls.setMinimumSize(self, v) o(self).minimumSize = v end
  function cls.isResettable(self) return o(self).resettable end
  function cls.setResettable(self, v) o(self).resettable = v end
  function cls.isDragTargetable(self) return o(self).dragTargetable end
  function cls.setDragTargetable(self, v) o(self).dragTargetable = v end
  function cls.isMovable(self) return o(self).movable end
  function cls.setMovable(self, v) o(self).movable = v end
  function cls.isSnappable(self) return o(self).snappable end
  function cls.setSnappable(self, v) o(self).snappable = v end
  function cls.getName(self) return J.simple_name(self.__key) end
  function cls.drawAfterInterface(self, id) o(self).drawHooks:add((id << 16) | 0xffff) end
  function cls.drawAfterLayer__int_int(self, group, child) o(self).drawHooks:add((group << 16) | child) end
  function cls.drawAfterLayer__int(self, component) o(self).drawHooks:add(component) end
  function cls.onMouseOver(self) end
  function cls.onDrag__Overlay(self, other) return false end
  function cls.getParentBounds(self) return nil end
  function cls.revalidate(self) end
  function cls.addMenuEntry__MenuAction_String_String(self, a, opt, target)
    local e = C[P .. "OverlayMenuEntry"].__new(a, opt, target)
    o(self).menuEntries:add(e)
    return e
  end
  function cls.addMenuEntry__MenuAction_String_String_Consumer(self, a, opt, target, cb)
    local e = cls.addMenuEntry__MenuAction_String_String(self, a, opt, target)
    e.callback = cb
    return e
  end
  function cls.removeMenuEntry(self, a, opt, target)
    local l = o(self).menuEntries
    local i = l:indexOf(C[P .. "OverlayMenuEntry"].__new(a, opt, target))
    if i >= 0 then l:remove__int(i) end
  end
end)

def(P .. "OverlayPanel", P .. "Overlay", {}, function(cls, super)
  local function init(self)
    self.panelComponent = C[P .. "components.PanelComponent"].__new()
    self.qh_panel = { clearChildren = true, dynamicFont = false, preferredColor = nil }
    self:setResizable(true)
  end
  function cls.__init__(self) super.__init__(self); init(self) end
  function cls.__init__Plugin(self, plugin) super.__init__Plugin(self, plugin); init(self) end
  function cls.getPanelComponent(self) return self.panelComponent end
  function cls.setClearChildren(self, v) self.qh_panel.clearChildren = v end
  function cls.isClearChildren(self) return self.qh_panel.clearChildren end
  function cls.setDynamicFont(self, v) self.qh_panel.dynamicFont = v end
  function cls.isDynamicFont(self) return self.qh_panel.dynamicFont end
  function cls.setPreferredColor(self, c) self.qh_panel.preferredColor = c end
  function cls.getPreferredColor(self) return self.qh_panel.preferredColor end
  function cls.render(self, g)
    local pc = self.panelComponent
    local old_size = pc:getPreferredSize()
    local pref = self:getPreferredSize()
    local FM = C["net.runelite.client.ui.FontManager"]
    local k = constants()
    if pref ~= nil then
      pc:setPreferredSize(pref)
      if self.qh_panel.dynamicFont then
        if pref.width >= k.STANDARD_WIDTH * 1.3 then g:setFont(FM.getRunescapeBoldFont())
        elseif pref.width <= k.STANDARD_WIDTH * 0.8 then g:setFont(FM.getRunescapeSmallFont()) end
      end
    end
    local old_bg = pc:getBackgroundColor()
    local pcolor = self.qh_panel.preferredColor
    if pcolor ~= nil and k.STANDARD_BACKGROUND_COLOR:equals(old_bg) then pc:setBackgroundColor(pcolor) end
    local ok, d = J.protect(pc.render, pc, g)
    if self.qh_panel.clearChildren then pc:getChildren():clear() end
    if not ok then error(d, 0) end
    pc:setPreferredSize(old_size)
    pc:setBackgroundColor(old_bg)
    return d
  end
end)

-- ====================================================================== managers

def(P .. "OverlayManager", nil, {}, function(cls)
  function cls.__init(self) end
  cls.__init__ = cls.__init
  local function index_of(o)
    for i, v in ipairs(S.overlays) do if rawequal(v, o) then return i end end
    return nil
  end
  function cls.add(self, o)
    assert(o)
    if index_of(o) then return false end
    S.overlays[#S.overlays + 1] = o
    return true
  end
  function cls.remove(self, o)
    local i = index_of(o)
    if i == nil then return false end
    table.remove(S.overlays, i)
    return true
  end
  function cls.removeIf(self, pred)
    local kept, any = {}, false
    for _, v in ipairs(S.overlays) do
      if pred:test(v) then any = true else kept[#kept + 1] = v end
    end
    S.overlays = kept
    return any
  end
  function cls.anyMatch(self, pred)
    for _, v in ipairs(S.overlays) do if pred:test(v) then return true end end
    return false
  end
  function cls.getOverlays(self)
    local l = C["java.util.ArrayList"].__new__()
    for _, v in ipairs(S.overlays) do l:add(v) end
    return l
  end
  -- Overlays are not dragged or resized here, so there is nothing to persist.
  function cls.saveOverlay(self, o) end
  function cls.resetOverlay(self, o) o:setPreferredPosition(nil); o:setPreferredSize(nil); o:setPreferredLocation(nil) end
  function cls.rebuildOverlayLayers(self) end
end)

def(P .. "tooltip.Tooltip", nil, {}, function(cls)
  function cls.__init__String(self, t) self.text = t end
  function cls.__init__LayoutableRenderableEntity(self, c) self.component = c end
  function cls.getText(self) return self.text end
  function cls.setText(self, t) self.text = t end
  function cls.getComponent(self) return self.component end
  function cls.setComponent(self, c) self.component = c end
  function cls.equals(self, o)
    return J.inst(o, P .. "tooltip.Tooltip") and o.text == self.text and rawequal(o.component, self.component)
  end
  function cls.hashCode(self) return J.hash(self.text) end
  function cls.toString(self) return "Tooltip(text=" .. J.str(self.text) .. ")" end
end)

def(P .. "tooltip.TooltipManager", nil, {}, function(cls)
  function cls.__init(self) end
  cls.__init__ = cls.__init
  function cls.add(self, t) assert(t); S.tooltips[#S.tooltips + 1] = t end
  function cls.addFront(self, t) assert(t); table.insert(S.tooltips, 1, t) end
  function cls.clear(self) S.tooltips = {} end
  function cls.getTooltips(self)
    local l = C["java.util.ArrayList"].__new__()
    for _, v in ipairs(S.tooltips) do l:add(v) end
    return l
  end
end)

def(P .. "worldmap.WorldMapPoint", nil, {}, function(cls)
  function cls.__init__WorldPoint_BufferedImage(self, wp, img)
    self.qh_wmp = { worldPoint = wp, image = img, snapToEdge = false, currentlyEdgeSnapped = false, jumpOnClick = false }
  end
  local function f(self) return self.qh_wmp end
  for _, name in ipairs({ "Image", "WorldPoint", "Target", "ImagePoint", "Name", "Tooltip" }) do
    local field = name:sub(1, 1):lower() .. name:sub(2)
    cls["get" .. name] = function(self) return f(self)[field] end
    cls["set" .. name] = function(self, v) f(self)[field] = v end
  end
  for _, name in ipairs({ "SnapToEdge", "CurrentlyEdgeSnapped", "JumpOnClick" }) do
    local field = name:sub(1, 1):lower() .. name:sub(2)
    cls["is" .. name] = function(self) return f(self)[field] == true end
    cls["set" .. name] = function(self, v) f(self)[field] = v end
  end
  function cls.onEdgeSnap(self) end
  function cls.onEdgeUnsnap(self) end
end)

def(P .. "worldmap.WorldMapPointManager", nil, {}, function(cls)
  function cls.__init(self) end
  cls.__init__ = cls.__init
  function cls.add(self, p) assert(p); S.map_points[#S.map_points + 1] = p end
  function cls.remove(self, p)
    for i, v in ipairs(S.map_points) do
      if rawequal(v, p) then table.remove(S.map_points, i); return end
    end
  end
  function cls.removeIf(self, pred)
    local kept = {}
    for _, v in ipairs(S.map_points) do if not pred:test(v) then kept[#kept + 1] = v end end
    S.map_points = kept
  end
  function cls.getWorldMapPoints(self)
    local l = C["java.util.ArrayList"].__new__()
    for _, v in ipairs(S.map_points) do l:add(v) end
    return l
  end
end)

-- ModelOutlineRenderer: the engine's mesh silhouette of the element
-- (draw.world_hull(..., "mesh")). Thickness and feather have no engine
-- counterpart; the engine's outline is one weight.
def(P .. "outline.ModelOutlineRenderer", nil, {}, function(cls)
  function cls.__init(self) end
  cls.__init__ = cls.__init
  local function outline(element_id, c)
    if element_id == nil or element_id < 0 then return end
    local d = QH.draw
    assert(d, "ModelOutlineRenderer.drawOutline outside a draw pass")
    d.world_hull(element_id, c:qh_rgb(), 0, "mesh")
  end
  local function hull_element(shape)
    if shape == nil then return nil end
    local w = rawget(shape, "__world")
    assert(w, "a world entity's hull carries __world (docs/QUEST_HELPER_PORT.md World shapes)")
    return w.element_id
  end
  function cls.drawOutline__Actor_int_Color_int(self, actor, width, c, feather)
    assert(actor)
    outline(hull_element(actor:getConvexHull()), c)
  end
  cls.drawOutline__NPC_int_Color_int = cls.drawOutline__Actor_int_Color_int
  cls.drawOutline__Player_int_Color_int = cls.drawOutline__Actor_int_Color_int
  function cls.drawOutline__TileObject_int_Color_int(self, obj, width, c, feather)
    assert(obj)
    outline(hull_element(obj:getClickbox()), c)
  end
  cls.drawOutline__GameObject_int_Color_int = cls.drawOutline__TileObject_int_Color_int
  cls.drawOutline__GroundObject_int_Color_int = cls.drawOutline__TileObject_int_Color_int
  cls.drawOutline__ItemLayer_int_Color_int = cls.drawOutline__TileObject_int_Color_int
  cls.drawOutline__DecorativeObject_int_Color_int = cls.drawOutline__TileObject_int_Color_int
  cls.drawOutline__WallObject_int_Color_int = cls.drawOutline__TileObject_int_Color_int
  function cls.drawOutline__RuneLiteObject_int_Color_int(self, obj, width, c, feather)
    assert(obj)
    outline(obj:__element_id(), c)
  end
end)

-- ====================================================================== OverlayUtil

def(P .. "OverlayUtil", nil, {}, function(cls)
  local MINIMAP_DOT_RADIUS = 4
  local function renderPolygon(g, poly, c, fill, stroke)
    if g:qh_world_outline_fill(poly, c, fill, stroke) then return end
    g:setColor(c)
    local original = g:getStroke()
    g:setStroke(stroke)
    g:draw(poly)
    g:setColor(fill)
    g:fill(poly)
    g:setStroke(original)
  end
  function cls.renderPolygon__Graphics2D_Shape_Color(g, poly, c)
    renderPolygon(g, poly, c, color(0, 0, 0, 50), C["java.awt.BasicStroke"].__new__float(2.0))
  end
  function cls.renderPolygon__Graphics2D_Shape_Color_Stroke(g, poly, c, stroke)
    renderPolygon(g, poly, c, color(0, 0, 0, 50), stroke)
  end
  function cls.renderPolygon__Graphics2D_Shape_Color_Color_Stroke(g, poly, c, fill, stroke)
    renderPolygon(g, poly, c, fill, stroke)
  end
  function cls.renderMinimapLocation(g, mini, c)
    g:setColor(C["java.awt.Color"].BLACK)
    g:fillOval(mini:getX() - MINIMAP_DOT_RADIUS // 2, mini:getY() - MINIMAP_DOT_RADIUS // 2 + 1, MINIMAP_DOT_RADIUS, MINIMAP_DOT_RADIUS)
    g:setColor(C["net.runelite.client.util.ColorUtil"].colorWithAlpha(c, 0xFF))
    g:fillOval(mini:getX() - MINIMAP_DOT_RADIUS // 2, mini:getY() - MINIMAP_DOT_RADIUS // 2, MINIMAP_DOT_RADIUS, MINIMAP_DOT_RADIUS)
  end
  function cls.renderTextLocation(g, loc, text, c)
    if text == nil or text == "" then return end
    local x, y = loc:getX(), loc:getY()
    g:setColor(C["java.awt.Color"].BLACK)
    g:drawString__String_int_int(text, x + 1, y + 1)
    g:setColor(color(c:getRed(), c:getGreen(), c:getBlue(), 255))
    g:drawString__String_int_int(text, x, y)
  end
  function cls.renderImageLocation__Graphics2D_Point_BufferedImage(g, loc, img)
    g:drawImage__Image_int_int_ImageObserver(img, loc:getX(), loc:getY(), nil)
  end
  function cls.renderImageLocation__Client_Graphics2D_LocalPoint_BufferedImage_int(client, g, lp, img, z)
    local p = C["net.runelite.api.Perspective"].getCanvasImageLocation(client, lp, img, z)
    if p ~= nil then cls.renderImageLocation__Graphics2D_Point_BufferedImage(g, p, img) end
  end
  function cls.renderActorOverlay(g, actor, text, c)
    local poly = actor:getCanvasTilePoly()
    if poly ~= nil then cls.renderPolygon__Graphics2D_Shape_Color(g, poly, c) end
    local tl = actor:getCanvasTextLocation(g, text, actor:getLogicalHeight() + 40)
    if tl ~= nil then cls.renderTextLocation(g, tl, text, c) end
  end
  function cls.renderActorOverlayImage(g, actor, img, c, z)
    local poly = actor:getCanvasTilePoly()
    if poly ~= nil then cls.renderPolygon__Graphics2D_Shape_Color(g, poly, c) end
    local il = actor:getCanvasImageLocation(img, z)
    if il ~= nil then cls.renderImageLocation__Graphics2D_Point_BufferedImage(g, il, img) end
  end
  function cls.renderTileOverlay__Graphics2D_TileObject_String_Color(g, obj, text, c)
    local poly = obj:getCanvasTilePoly()
    if poly ~= nil then cls.renderPolygon__Graphics2D_Shape_Color(g, poly, c) end
    local mini = obj:getMinimapLocation()
    if mini ~= nil then cls.renderMinimapLocation(g, mini, c) end
    local tl = obj:getCanvasTextLocation(g, text, 0)
    if tl ~= nil then cls.renderTextLocation(g, tl, text, c) end
  end
  function cls.renderTileOverlay__Client_Graphics2D_LocalPoint_BufferedImage_Color(client, g, lp, img, c)
    local poly = C["net.runelite.api.Perspective"].getCanvasTilePoly__Client_LocalPoint(client, lp)
    if poly ~= nil then cls.renderPolygon__Graphics2D_Shape_Color(g, poly, c) end
    cls.renderImageLocation__Client_Graphics2D_LocalPoint_BufferedImage_int(client, g, lp, img, 0)
  end
  function cls.renderHoverableArea(g, area, mouse, fill, border, hover)
    if area == nil then return end
    assert(mouse)
    local b = area:contains__double_double(mouse:getX() + 0.0, mouse:getY() + 0.0) and hover or border
    if g:qh_world_outline_fill(area, b, fill, nil) then return end
    g:setColor(b)
    g:draw(area)
    g:setColor(fill)
    g:fill(area)
  end
  function cls.setGraphicProperties(g) end
end)

-- ====================================================================== ImageUtil

def("net.runelite.client.util.ImageUtil", nil, {}, function(cls)
  local BI = function() return C["java.awt.image.BufferedImage"] end
  -- A resource path names a plugin asset: "/a/b.png" -> "a_b.png".
  function cls.qh_asset_name(path)
    assert(path)
    return (path:gsub("^/", ""):gsub("/", "_"))
  end
  function cls.loadImageResource(klass, path)
    return BI().qh_of_asset(cls.qh_asset_name(path))
  end
  -- RuneLite's overlays call these every frame on the same source
  -- (QuestStep.highlightItem's fillImage of an item icon): a fresh image each
  -- time made awt compose -- and evict and release -- a picture a frame, and
  -- every release walks the whole widget tree twice. So the result is kept per
  -- (source, operation): an unchanging source (an asset, an item icon, a
  -- derived image) answers the same image object. A plain canvas can be drawn
  -- into after the call, so it is never memoised.
  local memo = setmetatable({}, { __mode = "k" })
  local function stable(img)
    local k = rawget(img, "qh_kind")
    return k ~= nil and (k ~= "canvas" or rawget(img, "host_image") ~= nil)
  end
  local function per_pixel_new(img, fn)
    assert(img)
    return BI().qh_derived(img:getWidth(), img:getHeight(), function()
      local sp = img:qh_pixels()
      if sp == nil then return nil end
      local out = {}
      for i = 1, #sp do out[i] = fn(sp[i]) end
      return out
    end)
  end
  local function per_pixel(img, key, fn)
    assert(img)
    if not stable(img) then return per_pixel_new(img, fn) end
    local per = memo[img]
    if per == nil then per = {}; memo[img] = per end
    local out = per[key]
    if out == nil then out = per_pixel_new(img, fn); per[key] = out end
    return out
  end
  function cls.fillImage(img, c)
    local argb = c:getRGB() & 0xFFFFFFFF
    return per_pixel(img, "fill:" .. argb, function(p) return ((p >> 24) & 255) == 0 and p or argb end)
  end
  function cls.recolorImage(img, c)
    local rgb = c:qh_rgb()
    local ca = c:getAlpha()
    return per_pixel(img, "recolor:" .. rgb .. ":" .. ca, function(p)
      local a = (p >> 24) & 255
      if a == 0 then return p end
      return ((a * ca // 255) << 24) | rgb
    end)
  end
  function cls.alphaOffset__Image_float(img, f)
    return per_pixel(img, "alphaf:" .. f, function(p)
      local a = math.max(0, math.min(255, J.d2i(((p >> 24) & 255) * f)))
      return (a << 24) | (p & 0xFFFFFF)
    end)
  end
  function cls.alphaOffset__Image_int(img, off)
    return per_pixel(img, "alphai:" .. off, function(p)
      local a = (p >> 24) & 255
      if a == 0 then return p end
      a = math.max(0, math.min(255, a + off))
      return (a << 24) | (p & 0xFFFFFF)
    end)
  end
  function cls.grayscaleImage(img)
    return per_pixel(img, "gray", function(p)
      local r, g, b = (p >> 16) & 255, (p >> 8) & 255, p & 255
      local y = (r * 299 + g * 587 + b * 114) // 1000
      return (p & 0xFF000000) | (y << 16) | (y << 8) | y
    end)
  end
  function cls.resizeImage__BufferedImage_int_int_boolean(img, w, h, keep)
    if keep then
      local iw, ih = img:getWidth(), img:getHeight()
      if iw > ih then h = J.d2i(ih * w / iw) else w = J.d2i(iw * h / ih) end
    end
    return BI().qh_scaled(img, math.max(1, w), math.max(1, h), true)
  end
  function cls.resizeImage__BufferedImage_int_int(img, w, h)
    return cls.resizeImage__BufferedImage_int_int_boolean(img, w, h, false)
  end
  function cls.resizeCanvas(img, w, h)
    local out = BI().__new__int_int_int(w, h, 2)
    local g = out:getGraphics()
    g:drawImage__Image_int_int_ImageObserver(img, J.idiv(w - img:getWidth(), 2), J.idiv(h - img:getHeight(), 2), nil)
    return out
  end
  function cls.bufferedImageFromImage(img) return img end
end)

-- ====================================================================== QH.render

local function viewport(draw)
  local ctx = draw.context and draw.context()
  local b = ctx and ctx.bounds
  if b then return b.x or 0, b.y or 0, b.width or b.w or 0, b.height or b.h or 0 end
  return 0, 0, 0, 0
end

-- Which pass an overlay is drawn in (docs/QUEST_HELPER_PORT.md "Passes").
local function pass_of(o)
  local L, POS = C[P .. "OverlayLayer"], C[P .. "OverlayPosition"]
  local layer = o:getLayer()
  if layer == L.MANUAL then return nil end
  local pos = o:getPreferredPosition() or o:getPosition()
  local dynamic = pos == POS.DYNAMIC or pos == POS.DETACHED
  if (layer == L.ABOVE_SCENE or layer == L.UNDER_WIDGETS) and dynamic then return "world" end
  return "canvas"
end

-- OverlayManager.OVERLAY_COMPARATOR within a layer; layers in their order.
local function sorted_for(pass)
  local POS = C[P .. "OverlayPosition"]
  local list = {}
  for i, o in ipairs(S.overlays) do
    if pass_of(o) == pass then list[#list + 1] = { o = o, i = i } end
  end
  table.sort(list, function(a, b)
    local la, lb = a.o:getLayer().__ordinal, b.o:getLayer().__ordinal
    if la ~= lb then return la < lb end
    local pa = a.o:getPreferredPosition() or a.o:getPosition()
    local pb = b.o:getPreferredPosition() or b.o:getPosition()
    if pa ~= pb then return pa.__ordinal < pb.__ordinal end
    local qa, qb = a.o:getPriority(), b.o:getPriority()
    if qa ~= qb then
      if pa == POS.DYNAMIC or pa == POS.DETACHED then return qa < qb end
      return qa > qb
    end
    return a.i < b.i
  end)
  return list
end

-- SnapCorners for one pass (RuneLite's BORDER 5, the top-left's 15 px under
-- the mouse-over text), the viewport's corners standing in for the HUD and
-- chatbox origins.
local EXPAND_DOWN, EXPAND_UP, EXPAND_LEFT, EXPAND_RIGHT = 1, 2, 3, 4
local function snap_corners(vx, vy, vw, vh)
  local POS = C[P .. "OverlayPosition"]
  local function corner(x, y, align_x, align_bottom, expand)
    return { px = x, py = y, ax = align_x, bottom = align_bottom, expand = expand }
  end
  local r, b = vx + vw, vy + vh
  return {
    [POS.TOP_LEFT] = corner(vx + 5, vy + 5 + 15, "left", false, EXPAND_DOWN),
    [POS.TOP_CENTER] = corner(vx + vw // 2, vy + 5, "center", false, EXPAND_DOWN),
    [POS.TOP_RIGHT] = corner(r - 5, vy + 5, "right", false, EXPAND_DOWN),
    [POS.BOTTOM_LEFT] = corner(vx + 5, b - 5, "left", true, EXPAND_RIGHT),
    [POS.BOTTOM_RIGHT] = corner(r - 5, b - 5, "right", true, EXPAND_LEFT),
    [POS.ABOVE_CHATBOX_RIGHT] = corner(r - 5, b - 5, "right", true, EXPAND_UP),
    [POS.CANVAS_TOP_RIGHT] = corner(r, vy, "right", false, EXPAND_DOWN),
  }
end
local function next_draw_position(sc, bounds)
  local x, y = sc.px, sc.py
  if sc.ax == "center" then x = x - bounds.width // 2 elseif sc.ax == "right" then x = x - bounds.width end
  if sc.bottom then y = y - bounds.height end
  return x, y
end
local function shift(sc, bounds, padding)
  if sc.expand == EXPAND_LEFT then sc.px = math.min(sc.px, bounds.x - padding)
  elseif sc.expand == EXPAND_RIGHT then sc.px = math.max(sc.px, bounds.x + bounds.width + padding)
  elseif sc.expand == EXPAND_UP then sc.py = math.min(sc.py, bounds.y - padding)
  else sc.py = math.max(sc.py, bounds.y + bounds.height + padding) end
end

local function report_once(key, err)
  if S.failed[key] then return end
  S.failed[key] = true
  J.log("quest-helper: overlay " .. tostring(key) .. " failed: " .. J.str(J.exception(err)))
end

-- Run fn(...) as RuneLite's safeRender does: a Java exception is logged once
-- and the frame goes on; a budget error or a pending module is not ours to
-- swallow.
local function guarded(key, fn, ...)
  local ok, res = J.protect(fn, ...)
  if ok then return true, res end
  if J.catches(res, "java.lang.Exception") then
    report_once(key, res)
    return false, nil
  end
  error(res, 0)
end

local function render_overlays(pass, draw)
  local vx, vy, vw, vh = viewport(draw)
  local corners = snap_corners(vx, vy, vw, vh)
  local POS = C[P .. "OverlayPosition"]
  local FM = C["net.runelite.client.ui.FontManager"]
  for _, e in ipairs(sorted_for(pass)) do
    local o = e.o
    local pos = o:getPreferredPosition() or o:getPosition()
    local bounds = o:getBounds()
    local pref_loc = o:getPreferredLocation()
    local sc = nil
    local x, y
    if pos ~= POS.DYNAMIC and pos ~= POS.TOOLTIP and pos ~= POS.DETACHED and pref_loc == nil then
      sc = corners[pos]
      x, y = next_draw_position(sc, bounds)
    elseif pref_loc ~= nil then
      x, y = pref_loc.x, pref_loc.y
    else
      x, y = bounds.x, bounds.y
    end
    -- clampOverlayLocation, to the canvas
    if vw > 0 and vh > 0 then
      x = math.max(vx, math.min(x, math.max(vx, vx + vw - bounds.width)))
      y = math.max(vy, math.min(y, math.max(vy, vy + vh - bounds.height)))
    end
    if o:getPreferredSize() ~= nil then bounds:setSize__Dimension(o:getPreferredSize()) end
    local g = C["java.awt.Graphics2D"].qh_over(draw)
    g:setFont(FM.getRunescapeFont())
    if vw > 0 and vh > 0 then g:setClip__int_int_int_int(vx, vy, vw, vh) end
    g:translate__int_int(x, y)
    bounds:setLocation__int_int(x, y)
    local ok, d = guarded(o.__key, o.render, o, g)
    if ok and d ~= nil then bounds:setSize__Dimension(d) else bounds:setSize__int_int(0, 0) end
    if sc ~= nil and bounds.width + bounds.height > 0 then shift(sc, bounds, 2) end
  end
end

-- WorldMapOverlay.render: each point's image at its map position; a
-- snap-to-edge point clamps to the map's edge and flips to its edge image.
local function render_world_map(draw)
  S.hovered_map_point = nil
  if #S.map_points == 0 then return end
  if draw.worldmap_bounds == nil then return end
  local mb = draw.worldmap_bounds()
  if mb == nil then return end
  local mw, mh = mb.width or mb.w, mb.height or mb.h
  local vx, vy, vw, vh = viewport(draw)
  local mx, my = api().input.pointer()
  local hovered_point = nil
  for _, p in ipairs(S.map_points) do
    guarded(p.__key, function()
      local img, wp = p:getImage(), p:getWorldPoint()
      if img == nil or wp == nil then return end
      local dx, dy = draw.worldmap_project(wp:getX(), wp:getY(), wp:getPlane())
      if dx == nil then return end
      local g = C["java.awt.Graphics2D"].qh_over(draw)
      local iw, ih = img:getWidth(), img:getHeight()
      if p:isSnapToEdge() then
        g:setClip__int_int_int_int(vx, vy, vw, vh)
        local snapped = Rect(mb.x, mb.y, mw, mh)
        snapped:grow(-(iw // 2), -(ih // 2))
        local unsnapped = C["java.awt.Rectangle"].__new__Rectangle(snapped)
        local ip = p:getImagePoint()
        if ip ~= nil then unsnapped:translate(ip:getX() - iw // 2, ip:getY() - ih // 2) end
        if p:isCurrentlyEdgeSnapped() then unsnapped:grow(-iw, -ih) end
        if unsnapped:contains__int_int(dx, dy) then
          if p:isCurrentlyEdgeSnapped() then
            p:setCurrentlyEdgeSnapped(false)
            p:onEdgeUnsnap()
          end
        else
          dx = math.max(snapped.x, math.min(dx, snapped.x + snapped.width))
          dy = math.max(snapped.y, math.min(dy, snapped.y + snapped.height))
          if not p:isCurrentlyEdgeSnapped() then
            p:setCurrentlyEdgeSnapped(true)
            p:onEdgeSnap()
          end
        end
        -- as RuneLite: this frame still draws the image it measured; a
        -- callback's new image shows from the next frame
      else
        g:setClip__int_int_int_int(mb.x, mb.y, mw, mh)
      end
      local ip = p:getImagePoint()
      if ip == nil then dx, dy = dx - iw // 2, dy - ih // 2
      else dx, dy = dx - ip:getX(), dy - ip:getY() end
      g:drawImage__Image_int_int_ImageObserver(img, dx, dy, nil)
      -- its clickbox, for the hover tooltip and the "Focus on" row
      p.qh_wmp.box = { x = dx, y = dy, w = iw, h = ih }
      hovered_point = hovered_point or (mx ~= nil and mx >= dx and mx < dx + iw and my >= dy and my < dy + ih and p) or nil
    end)
  end
  S.hovered_map_point = hovered_point
  -- WorldMapOverlay: the hovered point's tooltip, at the mouse, over the map
  if hovered_point ~= nil then
    local tip = hovered_point:getTooltip()
    if tip ~= nil and J.str(tip) ~= "" then
      S.tooltips[#S.tooltips + 1] = C[P .. "tooltip.Tooltip"].__new__String(tip)
    end
  end
end

-- Tests: the point a canvas pass found under the mouse.
function QH._set_hovered_map_point(p) S.hovered_map_point = p end

-- WorldMapOverlay's jump-on-click: while a point that jumps is under the
-- mouse, the menu offers "Focus on <name>" on top (the left click), and
-- choosing it glides the map to the point's target (its world point when it
-- has none). From the entry's on_menu_build, between the MenuEntryAdded posts
-- and rl_client's menu_end; the point is the one the last canvas pass drew
-- under the mouse.
function QH.worldmap_menu()
  local p = S.hovered_map_point
  if p == nil or not p:isJumpOnClick() then return end
  local mx, my = api().input.pointer()
  local b = p.qh_wmp.box
  if mx == nil or b == nil or mx < b.x or mx >= b.x + b.w or my < b.y or my >= b.y + b.h then return end
  local client = J.rl_client.client()
  local name = p:getName()
  local target = (name ~= nil and J.str(name) ~= "") and ("<col=ff9040>" .. J.str(name) .. "</col>") or ""
  client:getMenu():createMenuEntry(-1)
    :setOption("Focus on")
    :setTarget(target)
    :setType(C["net.runelite.api.MenuAction"].RUNELITE)
    :onClick__Consumer(J.fn("java.util.function.Consumer", "accept", function()
      local wp = p:getTarget() or p:getWorldPoint()
      if wp ~= nil then client:getWorldMap():setWorldMapPositionTarget(wp) end
    end))
end

-- TooltipOverlay.render: stacked at the mouse, cleared every frame.
local function render_tooltips(draw)
  local tips = S.tooltips
  S.tooltips = {}
  if #tips == 0 then return end
  local mx, my = api().input.pointer()
  if mx == nil then return end
  local vx, vy, vw, vh = viewport(draw)
  local prev = S.prev_tooltip
  local tx = math.min(vx + vw - prev.w, mx)
  local ty = math.min(vy + vh - prev.h, my + 24)
  local g = C["java.awt.Graphics2D"].qh_over(draw)
  g:setFont(C["net.runelite.client.ui.FontManager"].getRunescapeFont())
  local width, height = 0, 0
  for _, t in ipairs(tips) do
    local entity = t:getComponent()
    if entity == nil then
      entity = C[P .. "components.TooltipComponent"].__new__()
      entity:setText(t:getText())
      entity:setBackgroundColor(constants().STANDARD_BACKGROUND_COLOR)
    end
    entity:setPreferredLocation(Pt(tx, ty + height))
    local ok, d = guarded("TooltipOverlay", entity.render, entity, g)
    if ok and d ~= nil then
      height = height + d.height + 2
      width = math.max(width, d.width)
    end
  end
  prev.w, prev.h = width, height
end

local function render_pass(pass, draw)
  render_overlays(pass, draw)
  if pass == "canvas" then
    render_world_map(draw)
    render_tooltips(draw)
  end
end

QH = QH or {}

-- The entry's per-pass call. `draw` is the callback's graphics object.
function QH.render(pass, draw)
  assert(pass == "world" or pass == "canvas", "QH.render: pass is 'world' or 'canvas'")
  assert(draw)
  QH.draw = draw
  local ok, err = J.protect(render_pass, pass, draw)
  QH.draw = nil
  if not ok then error(err, 0) end
end

-- For tests and the entry's teardown: forget every overlay, map point and
-- tooltip (a plugin shutDown removes its own; this is the hard reset).
function QH.render_reset()
  S.overlays, S.map_points, S.tooltips, S.failed = {}, {}, {}, {}
  S.prev_tooltip = { w = 0, h = 0 }
end
