-- java.awt for the Quest Helper port: Color, the geometry (Rectangle, Point,
-- Dimension, Polygon, Line2D, Point2D, Path2D, Area, AffineTransform),
-- BasicStroke, Font / FontMetrics / TextAttribute / FontManager, the images
-- (Image, BufferedImage, ImageIcon) and a Graphics2D over the draw callback's
-- `draw` object. Also the AWT key events the plugin's KeyListeners take.
--
-- The conventions this file is written to -- the passes, the world shapes
-- rl_world hands out (`poly.__world`), the image factories, how a left-aligned
-- string becomes draw.text -- are in docs/QUEST_HELPER_PORT.md, "Passes,
-- shapes and images" and "World shapes".
--
-- The client is reached only through QH.api and the current pass's QH.draw,
-- both read at call time.

local J, C = J, C

local function api() return QH.api end

local function def(key, super, ifaces, body, clinit, kind)
  J.def(key, { kind = kind or "class", super = super or "java.lang.Object", ifaces = ifaces or {}, body = body, clinit = clinit })
end
local function iface(key, ifaces, body)
  J.def(key, { kind = "interface", super = "java.lang.Object", ifaces = ifaces or {}, body = body })
end

local function uoe(msg) J.throw(C["java.lang.UnsupportedOperationException"].__new__String(msg)) end
local function iae(msg) J.throw(C["java.lang.IllegalArgumentException"].__new__String(msg)) end

local floor, ceil, sqrt, cos, sin, abs = math.floor, math.ceil, math.sqrt, math.cos, math.sin, math.abs
local function ri(v) return floor(v + 0.5) end           -- nearest integer, as an integer
local function int(v) return J.d2i(v) end                  -- Java (int) cast

-- ====================================================================== Color

iface("java.awt.Transparency")
iface("java.awt.Paint", { "java.awt.Transparency" })

def("java.awt.Color", nil, { "java.awt.Paint", "java.io.Serializable" }, function(cls)
  local function set(self, r, g, b, a)
    if r < 0 or r > 255 or g < 0 or g > 255 or b < 0 or b > 255 or a < 0 or a > 255 then
      iae("Color parameter outside of expected range: " .. r .. "," .. g .. "," .. b .. "," .. a)
    end
    self.qh_r, self.qh_g, self.qh_b, self.qh_a = r, g, b, a
  end
  local function f255(v) return int(v * 255 + 0.5) end
  function cls.__init__int_int_int(self, r, g, b) set(self, r, g, b, 255) end
  function cls.__init__int_int_int_int(self, r, g, b, a) set(self, r, g, b, a) end
  function cls.__init__int(self, rgb) set(self, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, 255) end
  function cls.__init__int_boolean(self, argb, has_alpha)
    set(self, (argb >> 16) & 255, (argb >> 8) & 255, argb & 255, has_alpha and ((argb >> 24) & 255) or 255)
  end
  function cls.__init__float_float_float(self, r, g, b) set(self, f255(r), f255(g), f255(b), 255) end
  function cls.__init__float_float_float_float(self, r, g, b, a) set(self, f255(r), f255(g), f255(b), f255(a)) end

  function cls.getRed(self) return self.qh_r end
  function cls.getGreen(self) return self.qh_g end
  function cls.getBlue(self) return self.qh_b end
  function cls.getAlpha(self) return self.qh_a end
  function cls.getRGB(self)
    return J.l2i((self.qh_a << 24) | (self.qh_r << 16) | (self.qh_g << 8) | self.qh_b)
  end
  function cls.getTransparency(self)
    if self.qh_a == 255 then return 1 elseif self.qh_a == 0 then return 2 end
    return 3
  end
  -- What draw.* takes: 0xRRGGBB, and the alpha separately.
  function cls.qh_rgb(self) return (self.qh_r << 16) | (self.qh_g << 8) | self.qh_b end
  function cls.qh_alpha(self) return self.qh_a end

  local FACTOR = 0.7
  function cls.darker(self)
    return cls.__new__int_int_int_int(math.max(int(self.qh_r * FACTOR), 0), math.max(int(self.qh_g * FACTOR), 0),
      math.max(int(self.qh_b * FACTOR), 0), self.qh_a)
  end
  function cls.brighter(self)
    local r, g, b, a = self.qh_r, self.qh_g, self.qh_b, self.qh_a
    local i = int(1.0 / (1.0 - FACTOR))
    if r == 0 and g == 0 and b == 0 then return cls.__new__int_int_int_int(i, i, i, a) end
    if r > 0 and r < i then r = i end
    if g > 0 and g < i then g = i end
    if b > 0 and b < i then b = i end
    return cls.__new__int_int_int_int(math.min(int(r / FACTOR), 255), math.min(int(g / FACTOR), 255),
      math.min(int(b / FACTOR), 255), a)
  end
  function cls.equals(self, o)
    return J.inst(o, "java.awt.Color") and o:getRGB() == self:getRGB()
  end
  function cls.hashCode(self) return self:getRGB() end
  function cls.toString(self)
    return "java.awt.Color[r=" .. self.qh_r .. ",g=" .. self.qh_g .. ",b=" .. self.qh_b .. "]"
  end

  -- Color.decode: Integer.decode's spellings (#rrggbb, 0xrrggbb, decimal).
  function cls.decode(s)
    local v
    local hex = s:match("^#(%x+)$") or s:match("^0[xX](%x+)$")
    if hex then v = tonumber(hex, 16) else v = tonumber(s) end
    if v == nil or math.type(v) ~= "integer" then
      J.throw(C["java.lang.NumberFormatException"].__new__String("For input string: \"" .. s .. "\""))
    end
    return cls.__new__int(v)
  end

  function cls.RGBtoHSB(r, g, b, out)
    local cmax = math.max(r, g, b)
    local cmin = math.min(r, g, b)
    local bright = cmax / 255.0
    local sat = cmax ~= 0 and (cmax - cmin) / cmax or 0.0
    local hue = 0.0
    if sat ~= 0 then
      local rc = (cmax - r) / (cmax - cmin)
      local gc = (cmax - g) / (cmax - cmin)
      local bc = (cmax - b) / (cmax - cmin)
      if r == cmax then hue = bc - gc
      elseif g == cmax then hue = 2.0 + rc - bc
      else hue = 4.0 + gc - rc end
      hue = hue / 6.0
      if hue < 0 then hue = hue + 1.0 end
    end
    out = out or J.newarr(3, 0.0)
    out[1], out[2], out[3] = hue, sat, bright
    return out
  end
  function cls.HSBtoRGB(hue, sat, bright)
    local r, g, b = 0, 0, 0
    if sat == 0 then
      r = int(bright * 255.0 + 0.5); g = r; b = r
    else
      local h = (hue - floor(hue)) * 6.0
      local f = h - floor(h)
      local p = bright * (1.0 - sat)
      local q = bright * (1.0 - sat * f)
      local t = bright * (1.0 - (sat * (1.0 - f)))
      local k = int(h)
      local rr, gg, bb
      if k == 0 then rr, gg, bb = bright, t, p
      elseif k == 1 then rr, gg, bb = q, bright, p
      elseif k == 2 then rr, gg, bb = p, bright, t
      elseif k == 3 then rr, gg, bb = p, q, bright
      elseif k == 4 then rr, gg, bb = t, p, bright
      else rr, gg, bb = bright, p, q end
      r, g, b = int(rr * 255.0 + 0.5), int(gg * 255.0 + 0.5), int(bb * 255.0 + 0.5)
    end
    return J.l2i(0xff000000 | (r << 16) | (g << 8) | b)
  end
  function cls.getHSBColor(h, s, b) return cls.__new__int(cls.HSBtoRGB(h, s, b)) end
end, function(cls)
  local function c(r, g, b) return cls.__new__int_int_int(r, g, b) end
  cls.white = c(255, 255, 255); cls.WHITE = cls.white
  cls.lightGray = c(192, 192, 192); cls.LIGHT_GRAY = cls.lightGray
  cls.gray = c(128, 128, 128); cls.GRAY = cls.gray
  cls.darkGray = c(64, 64, 64); cls.DARK_GRAY = cls.darkGray
  cls.black = c(0, 0, 0); cls.BLACK = cls.black
  cls.red = c(255, 0, 0); cls.RED = cls.red
  cls.pink = c(255, 175, 175); cls.PINK = cls.pink
  cls.orange = c(255, 200, 0); cls.ORANGE = cls.orange
  cls.yellow = c(255, 255, 0); cls.YELLOW = cls.yellow
  cls.green = c(0, 255, 0); cls.GREEN = cls.green
  cls.magenta = c(255, 0, 255); cls.MAGENTA = cls.magenta
  cls.cyan = c(0, 255, 255); cls.CYAN = cls.cyan
  cls.blue = c(0, 0, 255); cls.BLUE = cls.blue
end)

-- ====================================================================== geometry

iface("java.awt.Shape", {}, function(cls)
  -- A shape's outline as closed point lists {x1, y1, x2, y2, ...} in user
  -- space, for Graphics2D. Each concrete shape answers its own.
  function cls.qh_rings(self) uoe("Shape.qh_rings: " .. tostring(self.__key)) end
end)

def("java.awt.geom.Dimension2D", nil, { "java.lang.Cloneable" })
def("java.awt.Dimension", "java.awt.geom.Dimension2D", { "java.io.Serializable" }, function(cls)
  function cls.__init__(self) self.width, self.height = 0, 0 end
  function cls.__init__int_int(self, w, h) self.width, self.height = w, h end
  function cls.__init__Dimension(self, d) self.width, self.height = d.width, d.height end
  function cls.getWidth(self) return self.width end
  function cls.getHeight(self) return self.height end
  function cls.getSize(self) return cls.__new__int_int(self.width, self.height) end
  function cls.setSize__int_int(self, w, h) self.width, self.height = w, h end
  function cls.setSize__Dimension(self, d) self.width, self.height = d.width, d.height end
  function cls.setSize__double_double(self, w, h) self.width, self.height = int(ceil(w)), int(ceil(h)) end
  function cls.equals(self, o) return J.inst(o, "java.awt.Dimension") and o.width == self.width and o.height == self.height end
  function cls.hashCode(self) local s = self.width + self.height; return J.l2i(s * (s + 1) // 2 + self.width) end
  function cls.toString(self) return "java.awt.Dimension[width=" .. self.width .. ",height=" .. self.height .. "]" end
end)

def("java.awt.geom.Point2D", nil, { "java.lang.Cloneable" }, function(cls)
  function cls.distance__double_double(self, x, y) local dx, dy = self:getX() - x, self:getY() - y; return sqrt(dx * dx + dy * dy) end
  function cls.distance__Point2D(self, p) return self:distance__double_double(p:getX(), p:getY()) end
  function cls.distanceSq__double_double(self, x, y) local dx, dy = self:getX() - x, self:getY() - y; return dx * dx + dy * dy end
  function cls.setLocation__Point2D(self, p) self:setLocation__double_double(p:getX(), p:getY()) end
  function cls.equals(self, o)
    return J.inst(o, "java.awt.geom.Point2D") and o:getX() == self:getX() and o:getY() == self:getY()
  end
  function cls.hashCode(self) return J.hash(self:getX() + 0.0) ~ (31 * J.hash(self:getY() + 0.0)) end
end)
def("java.awt.geom.Point2D$Double", "java.awt.geom.Point2D", { "java.io.Serializable" }, function(cls)
  function cls.__init__(self) self.x, self.y = 0.0, 0.0 end
  function cls.__init__double_double(self, x, y) self.x, self.y = x, y end
  function cls.getX(self) return self.x end
  function cls.getY(self) return self.y end
  function cls.setLocation__double_double(self, x, y) self.x, self.y = x, y end
  function cls.toString(self) return "Point2D.Double[" .. J.str(self.x + 0.0) .. ", " .. J.str(self.y + 0.0) .. "]" end
end)
def("java.awt.Point", "java.awt.geom.Point2D", { "java.io.Serializable" }, function(cls)
  function cls.__init__(self) self.x, self.y = 0, 0 end
  function cls.__init__int_int(self, x, y) self.x, self.y = x, y end
  function cls.__init__Point(self, p) self.x, self.y = p.x, p.y end
  function cls.getX(self) return self.x end
  function cls.getY(self) return self.y end
  function cls.getLocation(self) return cls.__new__int_int(self.x, self.y) end
  function cls.setLocation__int_int(self, x, y) self.x, self.y = x, y end
  function cls.setLocation__Point(self, p) self.x, self.y = p.x, p.y end
  function cls.setLocation__double_double(self, x, y) self.x, self.y = int(floor(x + 0.5)), int(floor(y + 0.5)) end
  function cls.move(self, x, y) self.x, self.y = x, y end
  function cls.translate(self, dx, dy) self.x, self.y = self.x + dx, self.y + dy end
  function cls.equals(self, o)
    if J.inst(o, "java.awt.Point") then return o.x == self.x and o.y == self.y end
    return C["java.awt.geom.Point2D"].equals(self, o)
  end
  function cls.toString(self) return "java.awt.Point[x=" .. self.x .. ",y=" .. self.y .. "]" end
end)

-- RectangularShape: everything derived from getX/getY/getWidth/getHeight.
def("java.awt.geom.RectangularShape", nil, { "java.awt.Shape", "java.lang.Cloneable" }, function(cls)
  function cls.getMinX(self) return self:getX() end
  function cls.getMinY(self) return self:getY() end
  function cls.getMaxX(self) return self:getX() + self:getWidth() end
  function cls.getMaxY(self) return self:getY() + self:getHeight() end
  function cls.getCenterX(self) return self:getX() + self:getWidth() / 2.0 end
  function cls.getCenterY(self) return self:getY() + self:getHeight() / 2.0 end
  function cls.isEmpty(self) return self:getWidth() <= 0 or self:getHeight() <= 0 end
  function cls.contains__double_double(self, x, y)
    local x0, y0 = self:getX(), self:getY()
    return x >= x0 and y >= y0 and x < x0 + self:getWidth() and y < y0 + self:getHeight()
  end
  function cls.contains__Point2D(self, p) return self:contains__double_double(p:getX(), p:getY()) end
  function cls.contains__double_double_double_double(self, x, y, w, h)
    if self:isEmpty() or w <= 0 or h <= 0 then return false end
    local x0, y0 = self:getX(), self:getY()
    return x >= x0 and y >= y0 and (x + w) <= x0 + self:getWidth() and (y + h) <= y0 + self:getHeight()
  end
  function cls.intersects__double_double_double_double(self, x, y, w, h)
    if self:isEmpty() or w <= 0 or h <= 0 then return false end
    local x0, y0 = self:getX(), self:getY()
    return x + w > x0 and y + h > y0 and x < x0 + self:getWidth() and y < y0 + self:getHeight()
  end
  function cls.intersects__Rectangle2D(self, r) return self:intersects__double_double_double_double(r:getX(), r:getY(), r:getWidth(), r:getHeight()) end
  function cls.contains__Rectangle2D(self, r) return self:contains__double_double_double_double(r:getX(), r:getY(), r:getWidth(), r:getHeight()) end
  function cls.getBounds(self)
    local x1, y1 = floor(self:getX()), floor(self:getY())
    local x2, y2 = ceil(self:getMaxX()), ceil(self:getMaxY())
    return C["java.awt.Rectangle"].__new__int_int_int_int(int(x1), int(y1), int(x2 - x1), int(y2 - y1))
  end
  function cls.getBounds2D(self)
    return C["java.awt.geom.Rectangle2D$Double"].__new__double_double_double_double(self:getX() + 0.0, self:getY() + 0.0, self:getWidth() + 0.0, self:getHeight() + 0.0)
  end
  function cls.getFrame(self) return self:getBounds2D() end
  function cls.qh_rings(self)
    local x, y, w, h = self:getX(), self:getY(), self:getWidth(), self:getHeight()
    return { { x, y, x + w, y, x + w, y + h, x, y + h } }
  end
end)
def("java.awt.geom.Rectangle2D", "java.awt.geom.RectangularShape", {}, function(cls)
  function cls.setRect__Rectangle2D(self, r) self:setRect__double_double_double_double(r:getX(), r:getY(), r:getWidth(), r:getHeight()) end
  function cls.equals(self, o)
    if not J.inst(o, "java.awt.geom.Rectangle2D") then return false end
    return self:getX() == o:getX() and self:getY() == o:getY() and self:getWidth() == o:getWidth() and self:getHeight() == o:getHeight()
  end
  function cls.hashCode(self) return J.hash(self:getX() + 0.0) ~ J.hash(self:getY() + 0.0) ~ J.hash(self:getWidth() + 0.0) ~ J.hash(self:getHeight() + 0.0) end
end)
def("java.awt.geom.Rectangle2D$Double", "java.awt.geom.Rectangle2D", { "java.io.Serializable" }, function(cls)
  function cls.__init__(self) self.x, self.y, self.width, self.height = 0.0, 0.0, 0.0, 0.0 end
  function cls.__init__double_double_double_double(self, x, y, w, h) self.x, self.y, self.width, self.height = x, y, w, h end
  function cls.getX(self) return self.x end
  function cls.getY(self) return self.y end
  function cls.getWidth(self) return self.width end
  function cls.getHeight(self) return self.height end
  function cls.setRect__double_double_double_double(self, x, y, w, h) self.x, self.y, self.width, self.height = x, y, w, h end
  function cls.toString(self)
    return "java.awt.geom.Rectangle2D$Double[x=" .. J.str(self.x + 0.0) .. ",y=" .. J.str(self.y + 0.0) .. ",w=" .. J.str(self.width + 0.0) .. ",h=" .. J.str(self.height + 0.0) .. "]"
  end
end)

def("java.awt.Rectangle", "java.awt.geom.Rectangle2D", { "java.awt.Shape", "java.io.Serializable" }, function(cls)
  local function new(x, y, w, h) return cls.__new__int_int_int_int(x, y, w, h) end
  function cls.__init__(self) self.x, self.y, self.width, self.height = 0, 0, 0, 0 end
  function cls.__init__int_int_int_int(self, x, y, w, h) self.x, self.y, self.width, self.height = x, y, w, h end
  function cls.__init__int_int(self, w, h) self.x, self.y, self.width, self.height = 0, 0, w, h end
  function cls.__init__Point_Dimension(self, p, d) self.x, self.y, self.width, self.height = p.x, p.y, d.width, d.height end
  function cls.__init__Point(self, p) self.x, self.y, self.width, self.height = p.x, p.y, 0, 0 end
  function cls.__init__Dimension(self, d) self.x, self.y, self.width, self.height = 0, 0, d.width, d.height end
  function cls.__init__Rectangle(self, r) self.x, self.y, self.width, self.height = r.x, r.y, r.width, r.height end
  function cls.getX(self) return self.x end
  function cls.getY(self) return self.y end
  function cls.getWidth(self) return self.width end
  function cls.getHeight(self) return self.height end
  function cls.getBounds(self) return new(self.x, self.y, self.width, self.height) end
  function cls.getBounds2D(self) return new(self.x, self.y, self.width, self.height) end
  function cls.getLocation(self) return C["java.awt.Point"].__new__int_int(self.x, self.y) end
  function cls.getSize(self) return C["java.awt.Dimension"].__new__int_int(self.width, self.height) end
  function cls.setBounds__int_int_int_int(self, x, y, w, h) self.x, self.y, self.width, self.height = x, y, w, h end
  function cls.setBounds__Rectangle(self, r) self.x, self.y, self.width, self.height = r.x, r.y, r.width, r.height end
  function cls.setRect__double_double_double_double(self, x, y, w, h)
    local x0, y0 = floor(x), floor(y)
    self.x, self.y = int(x0), int(y0)
    self.width, self.height = int(ceil(x + w) - x0), int(ceil(y + h) - y0)
  end
  function cls.setLocation__int_int(self, x, y) self.x, self.y = x, y end
  function cls.setLocation__Point(self, p) self.x, self.y = p.x, p.y end
  function cls.setSize__int_int(self, w, h) self.width, self.height = w, h end
  function cls.setSize__Dimension(self, d) self.width, self.height = d.width, d.height end
  function cls.translate(self, dx, dy) self.x, self.y = self.x + dx, self.y + dy end
  function cls.grow(self, h, v)
    self.x, self.y = self.x - h, self.y - v
    self.width, self.height = self.width + 2 * h, self.height + 2 * v
  end
  function cls.isEmpty(self) return self.width <= 0 or self.height <= 0 end
  function cls.contains__int_int(self, px, py)
    local w, h = self.width, self.height
    if (w | h) < 0 then return false end
    local x, y = self.x, self.y
    if px < x or py < y then return false end
    return px < x + w and py < y + h
  end
  function cls.contains__Point(self, p) return self:contains__int_int(p.x, p.y) end
  function cls.contains__int_int_int_int(self, X, Y, W, H)
    local w, h = self.width, self.height
    if (w | h | W | H) < 0 then return false end
    local x, y = self.x, self.y
    if X < x or Y < y then return false end
    return X + W <= x + w and Y + H <= y + h
  end
  function cls.contains__Rectangle(self, r) return self:contains__int_int_int_int(r.x, r.y, r.width, r.height) end
  function cls.intersects__Rectangle(self, r)
    local tw, th, rw, rh = self.width, self.height, r.width, r.height
    if rw <= 0 or rh <= 0 or tw <= 0 or th <= 0 then return false end
    return r.x + rw > self.x and r.y + rh > self.y and r.x < self.x + tw and r.y < self.y + th
  end
  function cls.intersection(self, r)
    local x1, y1 = math.max(self.x, r.x), math.max(self.y, r.y)
    local x2, y2 = math.min(self.x + self.width, r.x + r.width), math.min(self.y + self.height, r.y + r.height)
    return new(x1, y1, x2 - x1, y2 - y1)
  end
  function cls.union(self, r)
    if self.width < 0 or self.height < 0 then return new(r.x, r.y, r.width, r.height) end
    if r.width < 0 or r.height < 0 then return new(self.x, self.y, self.width, self.height) end
    local x1, y1 = math.min(self.x, r.x), math.min(self.y, r.y)
    local x2, y2 = math.max(self.x + self.width, r.x + r.width), math.max(self.y + self.height, r.y + r.height)
    return new(x1, y1, x2 - x1, y2 - y1)
  end
  function cls.add__int_int(self, px, py)
    if self.width < 0 or self.height < 0 then self.x, self.y, self.width, self.height = px, py, 0, 0; return end
    local x1, y1 = math.min(self.x, px), math.min(self.y, py)
    local x2, y2 = math.max(self.x + self.width, px), math.max(self.y + self.height, py)
    self.x, self.y, self.width, self.height = x1, y1, x2 - x1, y2 - y1
  end
  function cls.add__Point(self, p) self:add__int_int(p.x, p.y) end
  function cls.add__Rectangle(self, r)
    local u = self:union(r)
    self.x, self.y, self.width, self.height = u.x, u.y, u.width, u.height
  end
  function cls.equals(self, o)
    if J.inst(o, "java.awt.Rectangle") then
      return o.x == self.x and o.y == self.y and o.width == self.width and o.height == self.height
    end
    return C["java.awt.geom.Rectangle2D"].equals(self, o)
  end
  function cls.toString(self)
    return "java.awt.Rectangle[x=" .. self.x .. ",y=" .. self.y .. ",width=" .. self.width .. ",height=" .. self.height .. "]"
  end
end)

-- Polygon: int points; `poly.__world` (rl_world) says what it is in the world.
local function ring_contains(ring, x, y)
  local n = #ring // 2
  if n < 3 then return false end
  local inside = false
  local j = n
  for i = 1, n do
    local xi, yi = ring[2 * i - 1], ring[2 * i]
    local xj, yj = ring[2 * j - 1], ring[2 * j]
    if (yi > y) ~= (yj > y) then
      if x < (xj - xi) * (y - yi) / (yj - yi) + xi then inside = not inside end
    end
    j = i
  end
  return inside
end
local function rings_bounds(rings)
  local x1, y1, x2, y2 = math.huge, math.huge, -math.huge, -math.huge
  for _, r in ipairs(rings) do
    for i = 1, #r, 2 do
      local x, y = r[i], r[i + 1]
      if x < x1 then x1 = x end
      if x > x2 then x2 = x end
      if y < y1 then y1 = y end
      if y > y2 then y2 = y end
    end
  end
  if x1 > x2 then return nil end
  return x1, y1, x2, y2
end

def("java.awt.Polygon", nil, { "java.awt.Shape", "java.io.Serializable" }, function(cls)
  function cls.__init__(self)
    self.npoints = 0
    self.xpoints = { n = 0 }
    self.ypoints = { n = 0 }
  end
  function cls.__init__intArr_intArr_int(self, xs, ys, n)
    if n < 0 then J.throw(C["java.lang.NegativeArraySizeException"].__new__String("npoints < 0")) end
    if n > xs.n or n > ys.n then J.throw(C["java.lang.IndexOutOfBoundsException"].__new__String("npoints > xpoints.length || npoints > ypoints.length")) end
    self.npoints = n
    self.xpoints = { n = n }
    self.ypoints = { n = n }
    for i = 1, n do self.xpoints[i] = xs[i]; self.ypoints[i] = ys[i] end
  end
  function cls.addPoint(self, x, y)
    local n = self.npoints + 1
    self.npoints = n
    self.xpoints[n] = x; self.xpoints.n = math.max(self.xpoints.n, n)
    self.ypoints[n] = y; self.ypoints.n = math.max(self.ypoints.n, n)
    self.qh_bounds = nil
  end
  function cls.reset(self) self.npoints = 0; self.qh_bounds = nil end
  function cls.invalidate(self) self.qh_bounds = nil end
  function cls.translate(self, dx, dy)
    for i = 1, self.npoints do
      self.xpoints[i] = self.xpoints[i] + dx
      self.ypoints[i] = self.ypoints[i] + dy
    end
    self.qh_bounds = nil
  end
  function cls.qh_rings(self)
    local r = {}
    for i = 1, self.npoints do r[2 * i - 1] = self.xpoints[i]; r[2 * i] = self.ypoints[i] end
    return { r }
  end
  function cls.getBounds(self)
    if self.npoints == 0 then return C["java.awt.Rectangle"].__new__() end
    local b = self.qh_bounds
    if b == nil then
      local x1, y1, x2, y2 = rings_bounds(self:qh_rings())
      b = { x1, y1, x2 - x1, y2 - y1 }
      self.qh_bounds = b
    end
    return C["java.awt.Rectangle"].__new__int_int_int_int(b[1], b[2], b[3], b[4])
  end
  function cls.getBoundingBox(self) return self:getBounds() end
  function cls.getBounds2D(self) return self:getBounds() end
  function cls.contains__double_double(self, x, y)
    if self.npoints <= 2 or not self:getBounds():contains__double_double(x, y) then return false end
    return ring_contains(self:qh_rings()[1], x, y)
  end
  function cls.contains__int_int(self, x, y) return self:contains__double_double(x + 0.0, y + 0.0) end
  function cls.contains__Point(self, p) return self:contains__double_double(p.x + 0.0, p.y + 0.0) end
  function cls.contains__Point2D(self, p) return self:contains__double_double(p:getX(), p:getY()) end
  function cls.inside(self, x, y) return self:contains__double_double(x + 0.0, y + 0.0) end
  function cls.intersects__double_double_double_double(self, x, y, w, h)
    -- conservative: bounds overlap and a corner or a vertex inside the other
    if not self:getBounds():intersects__double_double_double_double(x, y, w, h) then return false end
    if self:contains__double_double(x, y) or self:contains__double_double(x + w, y) or
       self:contains__double_double(x, y + h) or self:contains__double_double(x + w, y + h) then return true end
    for i = 1, self.npoints do
      local px, py = self.xpoints[i], self.ypoints[i]
      if px >= x and px < x + w and py >= y and py < y + h then return true end
    end
    return false
  end
  function cls.intersects__Rectangle2D(self, r) return self:intersects__double_double_double_double(r:getX(), r:getY(), r:getWidth(), r:getHeight()) end
end)

def("java.awt.geom.Line2D", nil, { "java.awt.Shape", "java.lang.Cloneable" }, function(cls)
  function cls.getP1(self) return C["java.awt.geom.Point2D$Double"].__new__double_double(self:getX1(), self:getY1()) end
  function cls.getP2(self) return C["java.awt.geom.Point2D$Double"].__new__double_double(self:getX2(), self:getY2()) end
  function cls.getBounds2D(self)
    local x1, y1, x2, y2 = self:getX1(), self:getY1(), self:getX2(), self:getY2()
    local x, y = math.min(x1, x2), math.min(y1, y2)
    return C["java.awt.geom.Rectangle2D$Double"].__new__double_double_double_double(x + 0.0, y + 0.0, abs(x2 - x1) + 0.0, abs(y2 - y1) + 0.0)
  end
  function cls.getBounds(self) return self:getBounds2D():getBounds() end
  function cls.contains__double_double(self, x, y) return false end
  function cls.contains__Point2D(self, p) return false end
  function cls.qh_segment(self) return self:getX1(), self:getY1(), self:getX2(), self:getY2() end
end)
def("java.awt.geom.Line2D$Double", "java.awt.geom.Line2D", { "java.io.Serializable" }, function(cls)
  function cls.__init__(self) self.x1, self.y1, self.x2, self.y2 = 0.0, 0.0, 0.0, 0.0 end
  function cls.__init__double_double_double_double(self, x1, y1, x2, y2) self.x1, self.y1, self.x2, self.y2 = x1, y1, x2, y2 end
  function cls.__init__Point2D_Point2D(self, a, b) self.x1, self.y1, self.x2, self.y2 = a:getX(), a:getY(), b:getX(), b:getY() end
  function cls.getX1(self) return self.x1 end
  function cls.getY1(self) return self.y1 end
  function cls.getX2(self) return self.x2 end
  function cls.getY2(self) return self.y2 end
  function cls.setLine__double_double_double_double(self, x1, y1, x2, y2) self.x1, self.y1, self.x2, self.y2 = x1, y1, x2, y2 end
end)

-- Path2D: straight segments only (moveTo/lineTo/closePath), the subset the
-- plugin builds. Curves are refused loudly.
def("java.awt.geom.Path2D", nil, { "java.awt.Shape", "java.lang.Cloneable" }, function(cls)
  cls.WIND_EVEN_ODD = 0
  cls.WIND_NON_ZERO = 1
  local function cur(self)
    local s = self.qh_subs[#self.qh_subs]
    if s == nil then J.throw(C["java.awt.geom.IllegalPathStateException"].__new__String("missing initial moveto in path definition")) end
    return s
  end
  function cls.qh_start(self) self.qh_subs = {} end
  function cls.moveTo(self, x, y)
    local last = self.qh_subs[#self.qh_subs]
    if last and #last.pts == 2 and not last.closed then last.pts[1], last.pts[2] = x, y; return end
    self.qh_subs[#self.qh_subs + 1] = { pts = { x, y }, closed = false }
  end
  function cls.lineTo(self, x, y)
    local s = cur(self)
    s.pts[#s.pts + 1] = x
    s.pts[#s.pts + 1] = y
  end
  function cls.closePath(self)
    local s = self.qh_subs[#self.qh_subs]
    if s then s.closed = true end
  end
  function cls.quadTo() uoe("Path2D.quadTo") end
  function cls.curveTo() uoe("Path2D.curveTo") end
  function cls.reset(self) self.qh_subs = {} end
  function cls.qh_rings(self)
    local out = {}
    for _, s in ipairs(self.qh_subs) do if #s.pts >= 6 then out[#out + 1] = s.pts end end
    return out
  end
  function cls.qh_paths(self) return self.qh_subs end
  function cls.getBounds2D(self)
    local all = {}
    for _, s in ipairs(self.qh_subs) do all[#all + 1] = s.pts end
    local x1, y1, x2, y2 = rings_bounds(all)
    if x1 == nil then return C["java.awt.geom.Rectangle2D$Double"].__new__() end
    return C["java.awt.geom.Rectangle2D$Double"].__new__double_double_double_double(x1 + 0.0, y1 + 0.0, x2 - x1 + 0.0, y2 - y1 + 0.0)
  end
  function cls.getBounds(self) return self:getBounds2D():getBounds() end
  function cls.contains__double_double(self, x, y)
    -- every subpath is implicitly closed for containment (Java's rule)
    local inside = false
    for _, s in ipairs(self.qh_subs) do
      if ring_contains(s.pts, x, y) then inside = not inside end
    end
    return inside
  end
  function cls.contains__Point2D(self, p) return self:contains__double_double(p:getX(), p:getY()) end
end)
def("java.awt.geom.Path2D$Double", "java.awt.geom.Path2D", { "java.io.Serializable" }, function(cls)
  function cls.__init__(self) self:qh_start() end
  function cls.__init__int(self, rule) self:qh_start() end
  function cls.__init__Shape(self, s)
    self:qh_start()
    for _, r in ipairs(s:qh_rings()) do
      self.qh_subs[#self.qh_subs + 1] = { pts = { table.unpack(r) }, closed = true }
    end
  end
end)
def("java.awt.geom.Path2D$Float", "java.awt.geom.Path2D$Double", {})

-- Area: the shapes it was built from, kept as a list (no boolean geometry):
-- a fill of an Area of hulls still draws each hull natively. add() is a
-- union by listing; subtract/intersect would need real geometry and refuse.
def("java.awt.geom.Area", nil, { "java.awt.Shape", "java.lang.Cloneable" }, function(cls)
  function cls.__init__(self) self.qh_shapes = {} end
  function cls.__init__Shape(self, s)
    if J.inst(s, "java.awt.geom.Area") then
      self.qh_shapes = { table.unpack(s.qh_shapes) }
    else
      self.qh_shapes = { s }
    end
  end
  function cls.add(self, a)
    for _, s in ipairs(a.qh_shapes) do self.qh_shapes[#self.qh_shapes + 1] = s end
  end
  function cls.subtract() uoe("Area.subtract") end
  function cls.intersect() uoe("Area.intersect") end
  function cls.exclusiveOr() uoe("Area.exclusiveOr") end
  function cls.reset(self) self.qh_shapes = {} end
  function cls.qh_list(self) return self.qh_shapes end
  function cls.isEmpty(self)
    for _, s in ipairs(self.qh_shapes) do
      local b = s:getBounds()
      if not b:isEmpty() then return false end
    end
    return true
  end
  function cls.getBounds(self)
    local u = nil
    for _, s in ipairs(self.qh_shapes) do
      local b = s:getBounds()
      if not b:isEmpty() then u = u and u:union(b) or b end
    end
    return u or C["java.awt.Rectangle"].__new__()
  end
  function cls.getBounds2D(self) return self:getBounds() end
  function cls.contains__double_double(self, x, y)
    for _, s in ipairs(self.qh_shapes) do
      if s:contains__double_double(x, y) then return true end
    end
    return false
  end
  function cls.contains__Point2D(self, p) return self:contains__double_double(p:getX(), p:getY()) end
  function cls.qh_rings(self)
    local out = {}
    for _, s in ipairs(self.qh_shapes) do
      for _, r in ipairs(s:qh_rings()) do out[#out + 1] = r end
    end
    return out
  end
  function cls.clone(self) return cls.__new__Shape(self) end
end)

def("java.awt.geom.AffineTransform", nil, { "java.lang.Cloneable", "java.io.Serializable" }, function(cls)
  local function set(self, m00, m10, m01, m11, m02, m12)
    self.m00, self.m10, self.m01, self.m11, self.m02, self.m12 = m00, m10, m01, m11, m02, m12
  end
  function cls.__init__(self) set(self, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0) end
  function cls.__init__AffineTransform(self, t) set(self, t.m00, t.m10, t.m01, t.m11, t.m02, t.m12) end
  function cls.__init__double_double_double_double_double_double(self, m00, m10, m01, m11, m02, m12)
    set(self, m00, m10, m01, m11, m02, m12)
  end
  function cls.getScaleX(self) return self.m00 end
  function cls.getScaleY(self) return self.m11 end
  function cls.getShearX(self) return self.m01 end
  function cls.getShearY(self) return self.m10 end
  function cls.getTranslateX(self) return self.m02 end
  function cls.getTranslateY(self) return self.m12 end
  function cls.isIdentity(self)
    return self.m00 == 1 and self.m11 == 1 and self.m01 == 0 and self.m10 == 0 and self.m02 == 0 and self.m12 == 0
  end
  function cls.setToIdentity(self) set(self, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0) end
  function cls.setToTranslation(self, tx, ty) set(self, 1.0, 0.0, 0.0, 1.0, tx, ty) end
  function cls.setToRotation__double(self, th) local c, s = cos(th), sin(th); set(self, c, s, -s, c, 0.0, 0.0) end
  function cls.setTransform(self, t) set(self, t.m00, t.m10, t.m01, t.m11, t.m02, t.m12) end
  function cls.concatenate(self, t)
    local a00, a01, a02, a10, a11, a12 = self.m00, self.m01, self.m02, self.m10, self.m11, self.m12
    set(self,
      a00 * t.m00 + a01 * t.m10,
      a10 * t.m00 + a11 * t.m10,
      a00 * t.m01 + a01 * t.m11,
      a10 * t.m01 + a11 * t.m11,
      a00 * t.m02 + a01 * t.m12 + a02,
      a10 * t.m02 + a11 * t.m12 + a12)
  end
  function cls.preConcatenate(self, t)
    local c = cls.__new__AffineTransform(t)
    c:concatenate(self)
    set(self, c.m00, c.m10, c.m01, c.m11, c.m02, c.m12)
  end
  function cls.translate(self, tx, ty)
    self.m02 = self.m00 * tx + self.m01 * ty + self.m02
    self.m12 = self.m10 * tx + self.m11 * ty + self.m12
  end
  function cls.rotate__double(self, th)
    local c, s = cos(th), sin(th)
    local m00, m01, m10, m11 = self.m00, self.m01, self.m10, self.m11
    self.m00 = m00 * c + m01 * s
    self.m01 = -m00 * s + m01 * c
    self.m10 = m10 * c + m11 * s
    self.m11 = -m10 * s + m11 * c
  end
  function cls.rotate__double_double_double(self, th, x, y)
    self:translate(x, y)
    self:rotate__double(th)
    self:translate(-x, -y)
  end
  function cls.scale(self, sx, sy)
    self.m00, self.m10 = self.m00 * sx, self.m10 * sx
    self.m01, self.m11 = self.m01 * sy, self.m11 * sy
  end
  function cls.qh_apply(self, x, y)
    return self.m00 * x + self.m01 * y + self.m02, self.m10 * x + self.m11 * y + self.m12
  end
  -- true when the transform only translates (rectangles stay rectangles)
  function cls.qh_translation_only(self)
    return self.m00 == 1 and self.m11 == 1 and self.m01 == 0 and self.m10 == 0
  end
  function cls.transform__Point2D_Point2D(self, src, dst)
    local x, y = self:qh_apply(src:getX(), src:getY())
    dst = dst or C["java.awt.geom.Point2D$Double"].__new__()
    dst:setLocation__double_double(x, y)
    return dst
  end
  function cls.clone(self) return cls.__new__AffineTransform(self) end
  function cls.equals(self, o)
    return J.inst(o, "java.awt.geom.AffineTransform") and o.m00 == self.m00 and o.m01 == self.m01 and o.m02 == self.m02
      and o.m10 == self.m10 and o.m11 == self.m11 and o.m12 == self.m12
  end
  function cls.getTranslateInstance(tx, ty) local t = cls.__new__(); t:setToTranslation(tx, ty); return t end
  function cls.getRotateInstance__double(th) local t = cls.__new__(); t:setToRotation__double(th); return t end
end)

-- ====================================================================== strokes

iface("java.awt.Stroke")
def("java.awt.BasicStroke", nil, { "java.awt.Stroke" }, function(cls)
  cls.CAP_BUTT, cls.CAP_ROUND, cls.CAP_SQUARE = 0, 1, 2
  cls.JOIN_MITER, cls.JOIN_ROUND, cls.JOIN_BEVEL = 0, 1, 2
  local function set(self, w, cap, join)
    if w < 0 then iae("negative width") end
    self.qh_width, self.qh_cap, self.qh_join = w, cap, join
  end
  function cls.__init__(self) set(self, 1.0, 2, 0) end
  function cls.__init__float(self, w) set(self, w, 2, 0) end
  function cls.__init__float_int_int(self, w, cap, join) set(self, w, cap, join) end
  function cls.__init__float_int_int_float(self, w, cap, join, miter) set(self, w, cap, join) end
  function cls.getLineWidth(self) return self.qh_width end
  function cls.getEndCap(self) return self.qh_cap end
  function cls.getLineJoin(self) return self.qh_join end
  function cls.equals(self, o)
    return J.inst(o, "java.awt.BasicStroke") and o.qh_width == self.qh_width and o.qh_cap == self.qh_cap and o.qh_join == self.qh_join
  end
  function cls.hashCode(self) return J.hash(self.qh_width + 0.0) end
end)

-- ====================================================================== fonts
--
-- The client draws one font. A Font is kept so the plugin's font handling
-- round-trips (bold, underline, deriveFont); FontMetrics measure with the
-- current pass's draw.text_width / text_height.

def("java.awt.font.TextAttribute", nil, { "java.io.Serializable" }, function(cls)
  function cls.__init(self, name) self.qh_name = name end
  function cls.toString(self) return "java.awt.font.TextAttribute(" .. self.qh_name .. ")" end
end, function(cls)
  for _, n in ipairs({ "FAMILY", "WEIGHT", "WIDTH", "POSTURE", "SIZE", "TRANSFORM", "SUPERSCRIPT", "FONT",
    "FOREGROUND", "BACKGROUND", "UNDERLINE", "STRIKETHROUGH", "KERNING", "LIGATURES", "TRACKING" }) do
    cls[n] = cls.__new(n:lower())
  end
  cls.UNDERLINE_ON = 0
  cls.STRIKETHROUGH_ON = true
  cls.WEIGHT_REGULAR = 1.0
  cls.WEIGHT_BOLD = 2.0
  cls.POSTURE_REGULAR = 0.0
  cls.POSTURE_OBLIQUE = 0.2
end)

def("java.awt.Font", nil, { "java.io.Serializable" }, function(cls)
  cls.PLAIN, cls.BOLD, cls.ITALIC = 0, 1, 2
  cls.DIALOG, cls.SANS_SERIF, cls.SERIF, cls.MONOSPACED = "Dialog", "SansSerif", "Serif", "Monospaced"
  local function set(self, name, style, size, underline)
    self.qh_name = name or "Default"
    self.qh_style = (style & ~3) ~= 0 and 0 or style
    self.qh_size = size
    self.qh_underline = underline
  end
  function cls.__init__String_int_int(self, name, style, size) set(self, name, style, size + 0.0, nil) end
  function cls.__init__Font(self, f) set(self, f.qh_name, f.qh_style, f.qh_size, f.qh_underline) end
  function cls.__init__Map(self, attrs)
    local TA = C["java.awt.font.TextAttribute"]
    local name = attrs:get(TA.FAMILY) or "Default"
    local size = attrs:get(TA.SIZE) or 12.0
    local weight = attrs:get(TA.WEIGHT) or 1.0
    local posture = attrs:get(TA.POSTURE) or 0.0
    local style = (weight >= 2.0 and 1 or 0) | (posture > 0 and 2 or 0)
    set(self, name, style, size + 0.0, attrs:get(TA.UNDERLINE))
  end
  function cls.getName(self) return self.qh_name end
  function cls.getFontName(self) return self.qh_name end
  function cls.getFamily(self) return self.qh_name end
  function cls.getStyle(self) return self.qh_style end
  function cls.getSize(self) return int(self.qh_size + 0.5) end
  function cls.getSize2D(self) return self.qh_size end
  function cls.isPlain(self) return self.qh_style == 0 end
  function cls.isBold(self) return (self.qh_style & 1) ~= 0 end
  function cls.isItalic(self) return (self.qh_style & 2) ~= 0 end
  function cls.qh_isUnderlined(self) return self.qh_underline ~= nil end
  local function derive(self, style, size, underline)
    local f = cls.__new__Font(self)
    f.qh_style, f.qh_size, f.qh_underline = style, size, underline
    return f
  end
  function cls.deriveFont__int_float(self, style, size) return derive(self, style, size, self.qh_underline) end
  function cls.deriveFont__float(self, size) return derive(self, self.qh_style, size, self.qh_underline) end
  function cls.deriveFont__int(self, style) return derive(self, style, self.qh_size, self.qh_underline) end
  function cls.deriveFont__int_AffineTransform(self, style, tx) return derive(self, style, self.qh_size, self.qh_underline) end
  function cls.deriveFont__AffineTransform(self, tx) return derive(self, self.qh_style, self.qh_size, self.qh_underline) end
  function cls.deriveFont__Map(self, attrs)
    local TA = C["java.awt.font.TextAttribute"]
    local f = cls.__new__Font(self)
    local fam = attrs:get(TA.FAMILY)
    if fam ~= nil then f.qh_name = fam end
    local size = attrs:get(TA.SIZE)
    if size ~= nil then f.qh_size = size + 0.0 end
    local weight = attrs:get(TA.WEIGHT)
    if weight ~= nil then f.qh_style = (f.qh_style & 2) | (weight >= 2.0 and 1 or 0) end
    local posture = attrs:get(TA.POSTURE)
    if posture ~= nil then f.qh_style = (f.qh_style & 1) | (posture > 0 and 2 or 0) end
    if attrs:containsKey(TA.UNDERLINE) then f.qh_underline = attrs:get(TA.UNDERLINE) end
    return f
  end
  function cls.getAttributes(self)
    local TA = C["java.awt.font.TextAttribute"]
    local m = C["java.util.HashMap"].__new__()
    m:put(TA.FAMILY, self.qh_name)
    m:put(TA.SIZE, self.qh_size)
    m:put(TA.WEIGHT, self:isBold() and 2.0 or 1.0)
    m:put(TA.POSTURE, self:isItalic() and 0.2 or 0.0)
    if self.qh_underline ~= nil then m:put(TA.UNDERLINE, self.qh_underline) end
    return m
  end
  function cls.equals(self, o)
    return J.inst(o, "java.awt.Font") and o.qh_name == self.qh_name and o.qh_style == self.qh_style
      and o.qh_size == self.qh_size and o.qh_underline == self.qh_underline
  end
  function cls.hashCode(self) return J.hash(self.qh_name) ~ self.qh_style ~ J.hash(self.qh_size) end
  function cls.toString(self)
    local st = self:isBold() and (self:isItalic() and "bolditalic" or "bold") or (self:isItalic() and "italic" or "plain")
    return "java.awt.Font[family=" .. self.qh_name .. ",name=" .. self.qh_name .. ",style=" .. st .. ",size=" .. self:getSize() .. "]"
  end
end)

-- FontMetrics over the current pass's draw object. Outside a pass there is
-- nothing to measure with: that is a caller bug, and it aborts. While the
-- client's font is not resident draw.text_width/text_height answer nil (and
-- draw.text draws nothing on those frames): text measures 0 then.
local function measure_draw()
  local d = QH.draw
  assert(d, "FontMetrics used outside a draw pass (QH.draw is nil)")
  return d
end
local function text_w(d, s) return d.text_width(s) or 0 end
local function text_h(d) return d.text_height() or 0 end
def("java.awt.FontMetrics", nil, { "java.io.Serializable" }, function(cls)
  function cls.__init__Font(self, f) self.qh_font = f end
  function cls.getFont(self) return self.qh_font end
  function cls.stringWidth(self, s)
    if s == nil then J.npe("stringWidth(null)") end
    if s == "" then return 0 end
    return text_w(measure_draw(), s)
  end
  function cls.charWidth__char(self, c) return self:stringWidth(utf8.char(c)) end
  function cls.charWidth__int(self, c) return self:stringWidth(utf8.char(c)) end
  function cls.getHeight(self) return text_h(measure_draw()) end
  function cls.getDescent(self) return text_h(measure_draw()) // 4 end
  function cls.getAscent(self) local h = text_h(measure_draw()); return h - h // 4 end
  function cls.getMaxAscent(self) return self:getAscent() end
  function cls.getMaxDescent(self) return self:getDescent() end
  function cls.getLeading(self) return 0 end
  function cls.getStringBounds__String_Graphics(self, s, g)
    return C["java.awt.geom.Rectangle2D$Double"].__new__double_double_double_double(0.0, -self:getAscent() + 0.0,
      self:stringWidth(s) + 0.0, self:getHeight() + 0.0)
  end
end)

def("net.runelite.client.ui.FontManager", nil, {}, function(cls)
  local cache = {}
  local function font(key, name, style)
    local f = cache[key]
    if f == nil then
      f = C["java.awt.Font"].__new__String_int_int(name, style, 16)
      cache[key] = f
    end
    return f
  end
  function cls.getRunescapeFont() return font("plain", "RuneScape", 0) end
  function cls.getRunescapeBoldFont() return font("bold", "RuneScape Bold", 1) end
  function cls.getRunescapeSmallFont() return font("small", "RuneScape Small", 0) end
  function cls.getDefaultFont() return font("default", "Dialog", 0) end
  function cls.getDefaultBoldFont() return font("default_bold", "Dialog", 1) end
end)

-- ====================================================================== images
--
-- A BufferedImage is one of:
--   ref     a live ImageRef (qh_of_ref)
--   source  fn() -> ref or nil while pending, asked when drawn (qh_of_source)
--   asset   a plugin asset by name (ImageUtil.loadImageResource)
--   canvas  new BufferedImage(w, h, type): a list of drawing ops replayed into
--           pixels when every source they read is ready
--   derived pixels computed from another image (fillImage, resizeImage, ...)
-- Pixels (canvas, derived) reach the client through api.assets.image_compose,
-- once per version, held in a bounded cache that releases what it evicts.

iface("java.awt.image.ImageObserver")
iface("java.awt.image.RenderedImage")
iface("java.awt.image.WritableRenderedImage", { "java.awt.image.RenderedImage" })

def("java.awt.Image", nil, {}, function(cls)
  cls.SCALE_DEFAULT, cls.SCALE_FAST, cls.SCALE_SMOOTH, cls.SCALE_REPLICATE, cls.SCALE_AREA_AVERAGING = 1, 2, 4, 8, 16
  function cls.getWidth__ImageObserver(self, o) return self:getWidth() end
  function cls.getHeight__ImageObserver(self, o) return self:getHeight() end
  function cls.flush(self) end
  function cls.getScaledInstance(self, w, h, hints)
    local sw, sh = self:getWidth(), self:getHeight()
    if w < 0 and h < 0 then w, h = sw, sh
    elseif w < 0 then w = int(sw * h / sh)
    elseif h < 0 then h = int(sh * w / sw) end
    return C["java.awt.image.BufferedImage"].qh_scaled(self, w, h, (hints & 4) ~= 0 or (hints & 16) ~= 0)
  end
end)

-- Known sizes of the plugin's own resources (script/plugins/assets/quest-helper/),
-- so a composite built while an asset is still loading gets the right size.
-- Read from the PNG headers of quest-helper/src/main/resources and RuneLite's
-- util/clue_arrow.png.
local RESOURCE_SIZE = {
  ["blueHitsplat.png"] = { 24, 23 }, ["close.png"] = { 18, 18 }, ["collapsed.png"] = { 12, 12 },
  ["discord.png"] = { 128, 128 }, ["expanded.png"] = { 12, 12 }, ["github.png"] = { 32, 32 },
  ["hamburger.png"] = { 18, 12 }, ["icon_background.png"] = { 32, 32 }, ["info_icon.png"] = { 15, 15 },
  ["maxHitsplat.png"] = { 24, 24 }, ["patreon.png"] = { 503, 481 }, ["quest_icon.png"] = { 20, 20 },
  ["quest_step_arrow.png"] = { 49, 49 }, ["quest_step_arrow_135.png"] = { 49, 49 },
  ["quest_step_arrow_180.png"] = { 49, 49 }, ["quest_step_arrow_225.png"] = { 49, 49 },
  ["quest_step_arrow_270.png"] = { 49, 49 }, ["quest_step_arrow_315.png"] = { 49, 49 },
  ["quest_step_arrow_45.png"] = { 62, 56 }, ["quest_step_arrow_90.png"] = { 49, 49 },
  ["redHitsplat.png"] = { 24, 23 }, ["settings_icon.png"] = { 18, 18 }, ["start.png"] = { 8, 12 },
  ["util_clue_arrow.png"] = { 32, 39 },
}

-- The bounded cache of composed refs: image -> {ref, ver}; least recently
-- used goes first, and its ref is released.
local COMPOSED_MAX = 128
local composed = {}      -- img -> {ref = , ver = , used = }
local composed_n = 0
local compose_clock = 0
local compose_serial = 0
local function composed_evict()
  local oldest, oldest_used = nil, math.huge
  for img, e in pairs(composed) do
    if e.used < oldest_used then oldest, oldest_used = img, e.used end
  end
  if oldest then
    api().assets.image_release(composed[oldest].ref)
    composed[oldest] = nil
    composed_n = composed_n - 1
  end
end
local function composed_ref(img, ver, pixels_fn)
  compose_clock = compose_clock + 1
  local e = composed[img]
  if e and e.ver == ver then e.used = compose_clock; return e.ref end
  local w, h = img:getWidth(), img:getHeight()
  if w <= 0 or h <= 0 then return nil end
  local px = pixels_fn()
  if px == nil then return nil end
  compose_serial = compose_serial + 1
  local ref = api().assets.image_compose("qh-img-" .. compose_serial .. ".png", w, h, px)
  if ref == nil then return nil end
  if e then
    api().assets.image_release(e.ref)
  else
    if composed_n >= COMPOSED_MAX then composed_evict() end
    composed_n = composed_n + 1
  end
  composed[img] = { ref = ref, ver = ver, used = compose_clock }
  return ref
end

-- src-over of one ARGB pixel onto another
local function blend(dst, src)
  local sa = (src >> 24) & 255
  if sa == 255 then return src & 0xFFFFFFFF end
  if sa == 0 then return dst end
  local da = (dst >> 24) & 255
  local oa = sa + da * (255 - sa) // 255
  if oa == 0 then return 0 end
  local function ch(shift)
    local s = (src >> shift) & 255
    local d = (dst >> shift) & 255
    return (s * sa + d * da * (255 - sa) // 255) // oa
  end
  return (oa << 24) | (ch(16) << 16) | (ch(8) << 8) | ch(0)
end

-- Resample `sp` (sw x sh) to dw x dh: nearest neighbour, or an area average
-- (Image.SCALE_SMOOTH's look) when `smooth`.
local function resample(sp, sw, sh, dw, dh, smooth)
  local out = {}
  for y = 0, dh - 1 do
    for x = 0, dw - 1 do
      local v
      if smooth and (sw > dw or sh > dh) then
        local x0, x1 = x * sw // dw, math.max(x * sw // dw + 1, (x + 1) * sw // dw)
        local y0, y1 = y * sh // dh, math.max(y * sh // dh + 1, (y + 1) * sh // dh)
        local a, r, g, b, n = 0, 0, 0, 0, 0
        for yy = y0, math.min(y1, sh) - 1 do
          for xx = x0, math.min(x1, sw) - 1 do
            local p = sp[yy * sw + xx + 1]
            local pa = (p >> 24) & 255
            a = a + pa
            r = r + ((p >> 16) & 255) * pa
            g = g + ((p >> 8) & 255) * pa
            b = b + (p & 255) * pa
            n = n + 1
          end
        end
        if a == 0 then v = 0
        else v = ((a // n) << 24) | ((r // a) << 16) | ((g // a) << 8) | (b // a) end
      else
        local sx, sy = x * sw // dw, y * sh // dh
        v = sp[sy * sw + sx + 1]
      end
      out[y * dw + x + 1] = v
    end
  end
  return out
end

local image_serial = 0

def("java.awt.image.BufferedImage", "java.awt.Image", { "java.awt.image.WritableRenderedImage", "java.awt.Transparency" }, function(cls)
  cls.TYPE_CUSTOM, cls.TYPE_INT_RGB, cls.TYPE_INT_ARGB, cls.TYPE_INT_ARGB_PRE = 0, 1, 2, 3
  cls.TYPE_INT_BGR, cls.TYPE_3BYTE_BGR, cls.TYPE_4BYTE_ABGR = 4, 5, 6
  cls.TYPE_BYTE_GRAY, cls.TYPE_BYTE_BINARY, cls.TYPE_BYTE_INDEXED = 10, 12, 13

  local function base(self, kind, w, h, typ)
    image_serial = image_serial + 1
    self.qh_id = image_serial
    self.qh_kind = kind
    self.qh_w, self.qh_h = w, h
    self.qh_type = typ or 2
    self.qh_ver = 0
  end
  local function blank(kind, w, h, typ)
    local o = setmetatable({}, cls)
    base(o, kind, w, h, typ)
    return o
  end

  function cls.__init__int_int_int(self, w, h, typ)
    if w <= 0 or h <= 0 then iae("Width (" .. w .. ") and height (" .. h .. ") cannot be <= 0") end
    base(self, "canvas", w, h, typ)
    self.qh_ops = {}
  end

  -- Factories (docs/QUEST_HELPER_PORT.md "Passes, shapes and images").
  function cls.qh_of_ref(ref, w, h)
    assert(ref)
    local o = blank("ref", w, h)
    o.qh_refv = ref
    return o
  end
  function cls.qh_of_source(fn, w, h)
    assert(fn)
    local o = blank("source", w, h)
    o.qh_fn = fn
    return o
  end
  function cls.qh_of_asset(name)
    assert(name)
    local sz = RESOURCE_SIZE[name]
    local o = blank("asset", sz and sz[1], sz and sz[2])
    o.qh_asset = name
    return o
  end
  -- pixels_fn() -> ARGB array (w*h) or nil while what it reads is pending
  function cls.qh_derived(w, h, pixels_fn)
    assert(pixels_fn)
    local o = blank("derived", w, h)
    o.qh_derive = pixels_fn
    return o
  end
  function cls.qh_scaled(src, w, h, smooth)
    assert(src)
    if w <= 0 or h <= 0 then iae("Width (" .. w .. ") and height (" .. h .. ") cannot be <= 0") end
    return cls.qh_derived(w, h, function()
      local sp = src:qh_pixels()
      if sp == nil then return nil end
      return resample(sp, src:getWidth(), src:getHeight(), w, h, smooth)
    end)
  end

  -- rl_client's spelling of a source image: a canvas image carrying
  -- `img.host_image = fn`, fn() -> ref?, state. It is a source, not pixels.
  local function host_kind(self)
    local k = self.qh_kind
    if k == "canvas" and rawget(self, "host_image") ~= nil then return "host" end
    return k
  end

  -- The image's ref now, or nil while pending.
  function cls.qh_ref(self)
    local k = host_kind(self)
    if k == "ref" then return self.qh_refv end
    if k == "host" then
      local ref, state = self.host_image()
      if ref ~= nil and (state == nil or state == "ready") then return ref end
      return nil
    end
    if k == "source" then return self.qh_fn() end
    if k == "asset" then
      if self.qh_refv == nil then
        local ref, state = api().assets.image(self.qh_asset)
        if ref ~= nil and state == "ready" then self.qh_refv = ref end
        if state == "missing" or state == "invalid" or state == "error" then
          if not self.qh_reported then
            self.qh_reported = true
            J.log("quest-helper: image asset " .. self.qh_asset .. " is " .. tostring(state))
          end
        end
      end
      return self.qh_refv
    end
    return composed_ref(self, self.qh_ver, function() return self:qh_pixels() end)
  end

  local function ref_size(self)
    local ref = self:qh_ref()
    if ref == nil then return nil end
    local w, h = api().assets.image_size(ref)
    return w, h
  end
  function cls.getWidth(self)
    if self.qh_w == nil then
      local w, h = ref_size(self)
      if w == nil then return 0 end
      self.qh_w, self.qh_h = w, h
    end
    return self.qh_w
  end
  function cls.getHeight(self)
    if self.qh_h == nil then
      local w, h = ref_size(self)
      if h == nil then return 0 end
      self.qh_w, self.qh_h = w, h
    end
    return self.qh_h
  end
  function cls.getType(self) return self.qh_type end
  function cls.getTransparency(self) return 3 end

  -- Row-major ARGB (1-based), or nil while pending.
  function cls.qh_pixels(self)
    local k = host_kind(self)
    if k == "ref" or k == "source" or k == "asset" or k == "host" then
      local ref = self:qh_ref()
      if ref == nil then return nil end
      if self.qh_px_ref ~= ref then
        local px = api().assets.image_pixels(ref)
        if px == nil then return nil end
        self.qh_px, self.qh_px_ref = px, ref
        if self.qh_w == nil then self.qh_w, self.qh_h = api().assets.image_size(ref) end
      end
      return self.qh_px
    end
    if self.qh_px ~= nil and self.qh_px_ver == self.qh_ver then return self.qh_px end
    local px
    if k == "derived" then
      px = self.qh_derive()
    else
      px = self:qh_replay()
    end
    if px == nil then return nil end
    self.qh_px, self.qh_px_ver = px, self.qh_ver
    return px
  end

  -- Canvas: replay the ops into a fresh pixel array.
  function cls.qh_replay(self)
    local w, h = self.qh_w, self.qh_h
    local px = {}
    for i = 1, w * h do px[i] = 0 end
    for _, op in ipairs(self.qh_ops) do
      local kind = op[1]
      if kind == "image" then
        local src, dx, dy, dw, dh = op[2], op[3], op[4], op[5], op[6]
        local sp = src:qh_pixels()
        if sp == nil then return nil end
        local sw, sh = src:getWidth(), src:getHeight()
        if dw ~= sw or dh ~= sh then sp = resample(sp, sw, sh, dw, dh, false) end
        local cx0, cy0, cx1, cy1 = op[7], op[8], op[9], op[10]
        for y = 0, dh - 1 do
          local ty = dy + y
          if ty >= 0 and ty < h and ty >= cy0 and ty < cy1 then
            for x = 0, dw - 1 do
              local tx = dx + x
              if tx >= 0 and tx < w and tx >= cx0 and tx < cx1 then
                local i = ty * w + tx + 1
                px[i] = blend(px[i], sp[y * dw + x + 1])
              end
            end
          end
        end
      elseif kind == "fill" or kind == "clear" then
        local x0, y0 = math.max(op[2], 0, op[7]), math.max(op[3], 0, op[8])
        local x1, y1 = math.min(op[2] + op[4], w, op[9]), math.min(op[3] + op[5], h, op[10])
        local argb = op[6]
        for y = y0, y1 - 1 do
          for x = x0, x1 - 1 do
            local i = y * w + x + 1
            px[i] = kind == "clear" and argb or blend(px[i], argb)
          end
        end
      elseif kind == "set" then
        px[op[3] * w + op[2] + 1] = op[4]
      end
    end
    return px
  end
  function cls.qh_op(self, op)
    assert(host_kind(self) == "canvas", "only a new BufferedImage(w, h, type) can be drawn into")
    self.qh_ops[#self.qh_ops + 1] = op
    self.qh_ver = self.qh_ver + 1
  end

  function cls.getGraphics(self) return C["qh.awt.ImageGraphics"].qh_over_image(self) end
  function cls.createGraphics(self) return self:getGraphics() end
  function cls.getRGB__int_int(self, x, y)
    local px = self:qh_pixels()
    if px == nil then return 0 end
    return J.l2i(px[y * self:getWidth() + x + 1])
  end
  function cls.setRGB__int_int_int(self, x, y, argb)
    if x < 0 or y < 0 or x >= self.qh_w or y >= self.qh_h then
      J.throw(C["java.lang.ArrayIndexOutOfBoundsException"].__new__String("Coordinate out of bounds!"))
    end
    self:qh_op({ "set", x, y, argb & 0xFFFFFFFF })
  end
  function cls.getSubimage(self, x, y, w, h)
    local src = self
    return cls.qh_derived(w, h, function()
      local sp = src:qh_pixels()
      if sp == nil then return nil end
      local sw = src:getWidth()
      local out = {}
      for yy = 0, h - 1 do
        for xx = 0, w - 1 do out[yy * w + xx + 1] = sp[(y + yy) * sw + x + xx + 1] end
      end
      return out
    end)
  end
  function cls.toString(self)
    return "BufferedImage@" .. string.format("%x", self.qh_id) .. ": type = " .. self.qh_type .. " " .. tostring(self.qh_kind)
  end
end)

iface("javax.swing.Icon")
def("javax.swing.ImageIcon", nil, { "javax.swing.Icon", "java.io.Serializable" }, function(cls)
  function cls.__init__(self) self.qh_image = nil; self.qh_desc = nil end
  function cls.__init__Image(self, img) self.qh_image = img end
  function cls.__init__Image_String(self, img, desc) self.qh_image = img; self.qh_desc = desc end
  function cls.getImage(self) return self.qh_image end
  function cls.setImage(self, img) self.qh_image = img end
  function cls.getDescription(self) return self.qh_desc end
  function cls.setDescription(self, d) self.qh_desc = d end
  function cls.getIconWidth(self) return self.qh_image and self.qh_image:getWidth() or -1 end
  function cls.getIconHeight(self) return self.qh_image and self.qh_image:getHeight() or -1 end
  function cls.paintIcon(self, comp, g, x, y)
    if self.qh_image then g:drawImage__Image_int_int_ImageObserver(self.qh_image, x, y, nil) end
  end
end)

-- ====================================================================== Graphics2D

iface("java.awt.Composite")
def("java.awt.AlphaComposite", nil, { "java.awt.Composite" }, function(cls)
  cls.CLEAR, cls.SRC, cls.SRC_OVER, cls.DST_OVER, cls.SRC_IN, cls.DST_IN = 1, 2, 3, 4, 5, 6
  cls.SRC_OUT, cls.DST_OUT, cls.DST, cls.SRC_ATOP, cls.DST_ATOP, cls.XOR = 7, 8, 9, 10, 11, 12
  function cls.__init(self, rule, alpha) self.qh_rule, self.qh_alpha = rule, alpha end
  function cls.getInstance__int(rule) return cls.__new(rule, 1.0) end
  function cls.getInstance__int_float(rule, a) return cls.__new(rule, a) end
  function cls.getRule(self) return self.qh_rule end
  function cls.getAlpha(self) return self.qh_alpha end
end, function(cls)
  cls.SrcOver = cls.__new(3, 1.0)
  cls.Src = cls.__new(2, 1.0)
  cls.Clear = cls.__new(1, 1.0)
  cls.SrcAtop = cls.__new(10, 1.0)
end)

def("java.awt.RenderingHints", nil, {}, function(cls)
  function cls.__init(self) end
end, function(cls)
  for _, n in ipairs({ "KEY_ANTIALIASING", "VALUE_ANTIALIAS_ON", "VALUE_ANTIALIAS_OFF", "VALUE_ANTIALIAS_DEFAULT",
    "KEY_RENDERING", "VALUE_RENDER_QUALITY", "VALUE_RENDER_SPEED", "KEY_INTERPOLATION",
    "VALUE_INTERPOLATION_BILINEAR", "VALUE_INTERPOLATION_NEAREST_NEIGHBOR", "VALUE_INTERPOLATION_BICUBIC",
    "KEY_TEXT_ANTIALIASING", "VALUE_TEXT_ANTIALIAS_ON", "VALUE_TEXT_ANTIALIAS_OFF", "KEY_STROKE_CONTROL",
    "VALUE_STROKE_PURE", "VALUE_STROKE_NORMALIZE" }) do
    cls[n] = n
  end
end)

-- -------------------------------------------------- clipping helpers (device space)

-- Sutherland-Hodgman against the rectangle [x0, x1) x [y0, y1).
local function clip_poly(pts, x0, y0, x1, y1)
  local function pass(input, inside, cut)
    local out = {}
    local n = #input // 2
    if n == 0 then return out end
    local px, py = input[2 * n - 1], input[2 * n]
    local pin = inside(px, py)
    for i = 1, n do
      local cx, cy = input[2 * i - 1], input[2 * i]
      local cin = inside(cx, cy)
      if cin then
        if not pin then local ix, iy = cut(px, py, cx, cy); out[#out + 1] = ix; out[#out + 1] = iy end
        out[#out + 1] = cx; out[#out + 1] = cy
      elseif pin then
        local ix, iy = cut(px, py, cx, cy); out[#out + 1] = ix; out[#out + 1] = iy
      end
      px, py, pin = cx, cy, cin
    end
    return out
  end
  local function at_x(xe) return function(ax, ay, bx, by) return xe, ay + (by - ay) * (xe - ax) / (bx - ax) end end
  local function at_y(ye) return function(ax, ay, bx, by) return ax + (bx - ax) * (ye - ay) / (by - ay), ye end end
  pts = pass(pts, function(x) return x >= x0 end, at_x(x0))
  pts = pass(pts, function(x) return x <= x1 end, at_x(x1))
  pts = pass(pts, function(_, y) return y >= y0 end, at_y(y0))
  pts = pass(pts, function(_, y) return y <= y1 end, at_y(y1))
  return pts
end

-- Liang-Barsky; nil when the segment misses the rectangle.
local function clip_line(ax, ay, bx, by, x0, y0, x1, y1)
  local dx, dy = bx - ax, by - ay
  local t0, t1 = 0.0, 1.0
  local function edge(p, q)
    if p == 0 then return q >= 0 end
    local r = q / p
    if p < 0 then
      if r > t1 then return false end
      if r > t0 then t0 = r end
    else
      if r < t0 then return false end
      if r < t1 then t1 = r end
    end
    return true
  end
  if edge(-dx, ax - x0) and edge(dx, x1 - ax) and edge(-dy, ay - y0) and edge(dy, y1 - ay) then
    return ax + t0 * dx, ay + t0 * dy, ax + t1 * dx, ay + t1 * dy
  end
  return nil
end

def("java.awt.Graphics", nil, {}, function(cls)
  function cls.create__int_int_int_int(self, x, y, w, h)
    local g = self:create__()
    g:translate__int_int(x, y)
    g:clipRect(0, 0, w, h)
    return g
  end
end)

def("java.awt.Graphics2D", "java.awt.Graphics", {}, function(cls)
  -- A Graphics2D over the current pass's draw object (rl_overlay makes one
  -- per overlay render).
  function cls.qh_over(draw)
    assert(draw)
    local g = setmetatable({}, cls)
    g.qh_d = draw
    g.qh_color = C["java.awt.Color"].WHITE
    g.qh_bg = C["java.awt.Color"].BLACK
    g.qh_font = C["net.runelite.client.ui.FontManager"].getRunescapeFont()
    g.qh_stroke = C["java.awt.BasicStroke"].__new__()
    g.qh_tx = C["java.awt.geom.AffineTransform"].__new__()
    g.qh_clip = nil      -- {x0, y0, x1, y1} device space, or nil
    g.qh_composite = C["java.awt.AlphaComposite"].SrcOver
    g.qh_hints = {}
    return g
  end

  function cls.create__(self)
    -- an object's metatable is its class (getmetatable is not in the sandbox)
    local g = setmetatable({}, self.__cls)
    for k, v in pairs(self) do g[k] = v end
    g.qh_tx = self.qh_tx:clone()
    g.qh_hints = {}
    for k, v in pairs(self.qh_hints) do g.qh_hints[k] = v end
    return g
  end
  function cls.dispose(self) end

  -- ---------------------------------------------- state
  function cls.getColor(self) return self.qh_color end
  function cls.setColor(self, c) if c ~= nil then self.qh_color = c end end
  function cls.setPaint(self, p) if J.inst(p, "java.awt.Color") then self.qh_color = p end end
  function cls.getPaint(self) return self.qh_color end
  function cls.getBackground(self) return self.qh_bg end
  function cls.setBackground(self, c) self.qh_bg = c end
  function cls.getFont(self) return self.qh_font end
  function cls.setFont(self, f) if f ~= nil then self.qh_font = f end end
  function cls.getFontMetrics__(self) return C["java.awt.FontMetrics"].__new__Font(self.qh_font) end
  function cls.getFontMetrics__Font(self, f) return C["java.awt.FontMetrics"].__new__Font(f) end
  function cls.getStroke(self) return self.qh_stroke end
  function cls.setStroke(self, s) self.qh_stroke = s end
  function cls.getComposite(self) return self.qh_composite end
  function cls.setComposite(self, c) self.qh_composite = c end
  function cls.setRenderingHint(self, k, v) self.qh_hints[k] = v end
  function cls.getRenderingHint(self, k) return self.qh_hints[k] end
  function cls.setRenderingHints(self, m) end
  function cls.getRenderingHints(self) return nil end
  function cls.addRenderingHints(self, m) end

  function cls.getTransform(self) return self.qh_tx:clone() end
  function cls.setTransform(self, t) self.qh_tx = t:clone() end
  function cls.transform(self, t) self.qh_tx:concatenate(t) end
  function cls.translate__int_int(self, x, y) self.qh_tx:translate(x, y) end
  function cls.translate__double_double(self, x, y) self.qh_tx:translate(x, y) end
  function cls.rotate__double(self, th) self.qh_tx:rotate__double(th) end
  function cls.rotate__double_double_double(self, th, x, y) self.qh_tx:rotate__double_double_double(th, x, y) end
  function cls.scale(self, sx, sy) self.qh_tx:scale(sx, sy) end

  -- Clip, kept as a device-space rectangle (the bounds of a non-rectangular
  -- clip shape, which is what the plugin's clips are: widget and map boxes).
  local function user_rect_to_device(self, x, y, w, h)
    local t = self.qh_tx
    local ax, ay = t:qh_apply(x, y)
    local bx, by = t:qh_apply(x + w, y)
    local cx, cy = t:qh_apply(x + w, y + h)
    local dx, dy = t:qh_apply(x, y + h)
    return math.min(ax, bx, cx, dx), math.min(ay, by, cy, dy), math.max(ax, bx, cx, dx), math.max(ay, by, cy, dy)
  end
  function cls.setClip__Shape(self, s)
    if s == nil then self.qh_clip = nil; return end
    local b = s:getBounds()
    local x0, y0, x1, y1 = user_rect_to_device(self, b.x, b.y, b.width, b.height)
    self.qh_clip = { x0, y0, x1, y1 }
  end
  function cls.setClip__int_int_int_int(self, x, y, w, h)
    local x0, y0, x1, y1 = user_rect_to_device(self, x, y, w, h)
    self.qh_clip = { x0, y0, x1, y1 }
  end
  function cls.clipRect(self, x, y, w, h)
    local x0, y0, x1, y1 = user_rect_to_device(self, x, y, w, h)
    local c = self.qh_clip
    if c then x0, y0, x1, y1 = math.max(x0, c[1]), math.max(y0, c[2]), math.min(x1, c[3]), math.min(y1, c[4]) end
    self.qh_clip = { x0, y0, math.max(x0, x1), math.max(y0, y1) }
  end
  function cls.clip(self, s)
    if s == nil then self.qh_clip = nil; return end
    local b = s:getBounds()
    self:clipRect(b.x, b.y, b.width, b.height)
  end
  local function device_to_user_rect(self, x0, y0, x1, y1)
    -- inverse of a translation-only transform; rotated clips answer their device box
    local t = self.qh_tx
    if t:qh_translation_only() then x0, y0, x1, y1 = x0 - t.m02, y0 - t.m12, x1 - t.m02, y1 - t.m12 end
    local fx, fy = floor(x0), floor(y0)
    return C["java.awt.Rectangle"].__new__int_int_int_int(int(fx), int(fy), int(ceil(x1) - fx), int(ceil(y1) - fy))
  end
  function cls.getClip(self)
    local c = self.qh_clip
    if c == nil then return nil end
    return device_to_user_rect(self, c[1], c[2], c[3], c[4])
  end
  function cls.getClipBounds__(self) return self:getClip() end
  function cls.hitClip(self, x, y, w, h)
    local c = self.qh_clip
    if c == nil then return true end
    local x0, y0, x1, y1 = user_rect_to_device(self, x, y, w, h)
    return x1 > c[1] and y1 > c[2] and x0 < c[3] and y0 < c[4]
  end

  -- ---------------------------------------------- device primitives
  local function rgba(self)
    local c = self.qh_color
    return c:qh_rgb(), c:qh_alpha()
  end
  local function fill_dev_rect(self, x, y, w, h)
    local c = self.qh_clip
    local x1, y1 = x + w, y + h
    if c then
      x, y = math.max(x, c[1]), math.max(y, c[2])
      x1, y1 = math.min(x1, c[3]), math.min(y1, c[4])
    end
    local ix, iy = ri(x), ri(y)
    local iw, ih = ri(x1) - ix, ri(y1) - iy
    if iw <= 0 or ih <= 0 then return end
    local rgb, a = rgba(self)
    if a == 0 then return end
    self.qh_d.rect(ix, iy, iw, ih, rgb, a)
  end
  -- draw.polygon takes at most POLY_MAX points; a longer outline (a big
  -- zone's tile edges) is filled as fan pieces from its first point, which
  -- tile a convex or star-shaped outline exactly.
  local POLY_MAX = 256
  local function fill_dev_poly(self, pts)
    local c = self.qh_clip
    if c then pts = clip_poly(pts, c[1], c[2], c[3], c[4]) end
    if #pts < 6 then return end
    local rgb, a = rgba(self)
    if a == 0 then return end
    local flat = {}
    for i = 1, #pts do flat[i] = ri(pts[i]) end
    local n = #flat // 2
    if n <= POLY_MAX then
      self.qh_d.polygon(flat, rgb, a)
      return
    end
    local k = 2
    while k < n do
      local last = math.min(n, k + POLY_MAX - 2)
      local piece = { flat[1], flat[2] }
      for i = k, last do piece[#piece + 1] = flat[2 * i - 1]; piece[#piece + 1] = flat[2 * i] end
      self.qh_d.polygon(piece, rgb, a)
      k = last
    end
  end
  -- One stroked segment in device space at `width` pixels; square caps
  -- (BasicStroke's default) extend each end by half the width.
  local function stroke_dev_seg(self, ax, ay, bx, by, width, cap)
    if width <= 1.0 then
      local c = self.qh_clip
      if c then
        ax, ay, bx, by = clip_line(ax, ay, bx, by, c[1], c[2], c[3], c[4])
        if ax == nil then return end
      end
      local rgb, a = rgba(self)
      if a == 0 then return end
      self.qh_d.line(ri(ax), ri(ay), ri(bx), ri(by), rgb, a)
      return
    end
    local dx, dy = bx - ax, by - ay
    local len = sqrt(dx * dx + dy * dy)
    local hw = width / 2.0
    if len == 0 then
      if cap == 0 then return end
      fill_dev_poly(self, { ax - hw, ay - hw, ax + hw, ay - hw, ax + hw, ay + hw, ax - hw, ay + hw })
      return
    end
    local ux, uy = dx / len, dy / len
    if cap ~= 0 then ax, ay, bx, by = ax - ux * hw, ay - uy * hw, bx + ux * hw, by + uy * hw end
    local nx, ny = -uy * hw, ux * hw
    fill_dev_poly(self, { ax + nx, ay + ny, bx + nx, by + ny, bx - nx, by - ny, ax - nx, ay - ny })
  end
  local function stroke_width(self)
    local s = self.qh_stroke
    local w = (s and s.getLineWidth) and s:getLineWidth() or 1.0
    local t = self.qh_tx
    local scale = sqrt(abs(t.m00 * t.m11 - t.m01 * t.m10))
    return w * scale, (s and s.qh_cap) or 2
  end
  local function stroke_user_path(self, pts, closed)
    local t = self.qh_tx
    local w, cap = stroke_width(self)
    local n = #pts // 2
    if n < 2 then return end
    -- An opaque closed outline that the clip does not cut is one engine
    -- polygon border (proper joins, one draw); draw.polygon's border has no
    -- alpha, so a translucent one is stroked segment by segment.
    local rgb, a = rgba(self)
    if closed and n >= 3 and n <= POLY_MAX and a == 255 then
      local flat = {}
      local inside = true
      local c = self.qh_clip
      local hw = w / 2
      for i = 1, n do
        local x, y = t:qh_apply(pts[2 * i - 1], pts[2 * i])
        if c and (x - hw < c[1] or y - hw < c[2] or x + hw > c[3] or y + hw > c[4]) then inside = false; break end
        flat[2 * i - 1], flat[2 * i] = ri(x), ri(y)
      end
      if inside then
        self.qh_d.polygon(flat, rgb, 0, rgb, math.max(1, ri(w)))
        return
      end
    end
    local last = closed and n or n - 1
    for i = 1, last do
      local j = i % n + 1
      local ax, ay = t:qh_apply(pts[2 * i - 1], pts[2 * i])
      local bx, by = t:qh_apply(pts[2 * j - 1], pts[2 * j])
      stroke_dev_seg(self, ax, ay, bx, by, w, cap)
    end
  end
  local function fill_user_poly(self, pts)
    local t = self.qh_tx
    local dev = {}
    for i = 1, #pts, 2 do
      local x, y = t:qh_apply(pts[i], pts[i + 1])
      dev[i], dev[i + 1] = x, y
    end
    fill_dev_poly(self, dev)
  end
  local function fill_user_rect(self, x, y, w, h)
    if w <= 0 or h <= 0 then return end
    local t = self.qh_tx
    if t:qh_translation_only() then
      fill_dev_rect(self, x + t.m02, y + t.m12, w, h)
    else
      fill_user_poly(self, { x, y, x + w, y, x + w, y + h, x, y + h })
    end
  end
  -- Graphics.drawRect / draw(Rectangle): the outline covers w+1 x h+1 pixels.
  local function draw_user_rect(self, x, y, w, h)
    if w < 0 or h < 0 then return end
    local t = self.qh_tx
    local sw = stroke_width(self)
    if t:qh_translation_only() and sw == floor(sw) then
      local s = math.max(1, int(sw))
      local a = s // 2
      x, y = x + t.m02, y + t.m12
      fill_dev_rect(self, x - a, y - a, w + s, s)
      fill_dev_rect(self, x - a, y + h - a, w + s, s)
      if h - s > 0 then
        fill_dev_rect(self, x - a, y - a + s, s, h - s)
        fill_dev_rect(self, x + w - a, y - a + s, s, h - s)
      end
    else
      stroke_user_path(self, { x, y, x + w, y, x + w, y + h, x, y + h }, true)
    end
  end

  -- ---------------------------------------------- world shapes (rl_world's poly.__world)
  local function world_width(self)
    local w = stroke_width(self)
    return math.max(1, ri(w))
  end
  local function world_tiles(w, fn)
    local sx, sz = w.size_x or 1, w.size_z or 1
    for i = 0, sx - 1 do
      for j = 0, sz - 1 do fn(w.tile_x + i, w.tile_z + j) end
    end
  end
  -- A hull names one element, or several (a RuneLiteObject drawn as one
  -- scene instance per merged model: `elements`).
  local function each_hull(w, fn)
    if w.elements ~= nil then
      for _, e in ipairs(w.elements) do fn(e) end
    else
      fn(w.element_id)
    end
  end
  local function world_fill(self, w)
    local rgb, a = rgba(self)
    if w.kind == "hull" then
      each_hull(w, function(e) self.qh_d.world_hull(e, rgb, a) end)
    elseif w.kind == "tile" then
      if a == 0 then return end
      world_tiles(w, function(x, z) self.qh_d.world_tile(x, z, w.level, rgb, rgb, a, 0) end)
    else
      uoe("Graphics2D.fill: unknown world shape kind " .. tostring(w.kind))
    end
  end
  local function world_draw(self, w, shape)
    local rgb = self.qh_color:qh_rgb()
    if w.kind == "hull" then
      each_hull(w, function(e) self.qh_d.world_hull(e, rgb, 0) end)
    elseif w.kind == "tile" then
      if (w.size_x or 1) == 1 and (w.size_z or 1) == 1 then
        self.qh_d.world_tile(w.tile_x, w.tile_z, w.level, rgb, rgb, 0, world_width(self))
      else
        for _, r in ipairs(shape:qh_rings()) do stroke_user_path(self, r, true) end
      end
    else
      uoe("Graphics2D.draw: unknown world shape kind " .. tostring(w.kind))
    end
  end
  -- OverlayUtil.renderPolygon / renderHoverableArea on a world shape: one
  -- engine call with the wash and the border. True when `shape` was one.
  function cls.qh_world_outline_fill(self, shape, border, fill, stroke)
    local w = rawget(shape, "__world")
    if w == nil then
      if J.inst(shape, "java.awt.geom.Area") then
        local list = shape:qh_list()
        local any_world = false
        for _, s in ipairs(list) do if rawget(s, "__world") then any_world = true end end
        if not any_world then return false end
        for _, s in ipairs(list) do
          if not self:qh_world_outline_fill(s, border, fill, stroke) then
            local old_c, old_s = self.qh_color, self.qh_stroke
            self.qh_color = border; if stroke then self.qh_stroke = stroke end
            self:draw(s)
            self.qh_color = fill
            self:fill(s)
            self.qh_color, self.qh_stroke = old_c, old_s
          end
        end
        return true
      end
      return false
    end
    local fa = fill and fill:qh_alpha() or 0
    if w.kind == "hull" then
      each_hull(w, function(e) self.qh_d.world_hull(e, border:qh_rgb(), fa) end)
    elseif w.kind == "tile" then
      local old_s = self.qh_stroke
      if stroke then self.qh_stroke = stroke end
      local width = world_width(self)
      if (w.size_x or 1) == 1 and (w.size_z or 1) == 1 then
        self.qh_d.world_tile(w.tile_x, w.tile_z, w.level, fill and fill:qh_rgb() or border:qh_rgb(), border:qh_rgb(), fa, width)
      else
        if fa > 0 then
          world_tiles(w, function(x, z) self.qh_d.world_tile(x, z, w.level, fill:qh_rgb(), fill:qh_rgb(), fa, 0) end)
        end
        local old_c = self.qh_color
        self.qh_color = border
        for _, r in ipairs(shape:qh_rings()) do stroke_user_path(self, r, true) end
        self.qh_color = old_c
      end
      self.qh_stroke = old_s
    else
      uoe("world shape kind " .. tostring(w.kind))
    end
    return true
  end

  -- ---------------------------------------------- Shape drawing
  function cls.fill(self, s)
    if s == nil then J.npe("Graphics2D.fill(null)") end
    local w = rawget(s, "__world")
    if w then return world_fill(self, w) end
    if J.inst(s, "java.awt.geom.RectangularShape") then
      return fill_user_rect(self, s:getX(), s:getY(), s:getWidth(), s:getHeight())
    end
    if J.inst(s, "java.awt.geom.Line2D") then return end
    if J.inst(s, "java.awt.geom.Area") then
      for _, sub in ipairs(s:qh_list()) do self:fill(sub) end
      return
    end
    for _, r in ipairs(s:qh_rings()) do fill_user_poly(self, r) end
  end
  function cls.draw(self, s)
    if s == nil then J.npe("Graphics2D.draw(null)") end
    local w = rawget(s, "__world")
    if w then return world_draw(self, w, s) end
    if J.inst(s, "java.awt.geom.RectangularShape") then
      return draw_user_rect(self, s:getX(), s:getY(), s:getWidth(), s:getHeight())
    end
    if J.inst(s, "java.awt.geom.Line2D") then
      local t = self.qh_tx
      local x1, y1, x2, y2 = s:qh_segment()
      local ax, ay = t:qh_apply(x1, y1)
      local bx, by = t:qh_apply(x2, y2)
      local width, cap = stroke_width(self)
      return stroke_dev_seg(self, ax, ay, bx, by, width, cap)
    end
    if J.inst(s, "java.awt.geom.Area") then
      for _, sub in ipairs(s:qh_list()) do self:draw(sub) end
      return
    end
    if J.inst(s, "java.awt.geom.Path2D") then
      for _, p in ipairs(s:qh_paths()) do stroke_user_path(self, p.pts, p.closed) end
      return
    end
    for _, r in ipairs(s:qh_rings()) do stroke_user_path(self, r, true) end
  end

  function cls.fillRect(self, x, y, w, h) fill_user_rect(self, x, y, w, h) end
  function cls.drawRect(self, x, y, w, h) draw_user_rect(self, x, y, w, h) end
  function cls.clearRect(self, x, y, w, h)
    local old = self.qh_color
    self.qh_color = self.qh_bg
    fill_user_rect(self, x, y, w, h)
    self.qh_color = old
  end
  function cls.fillRoundRect(self, x, y, w, h, aw, ah) fill_user_rect(self, x, y, w, h) end
  function cls.drawRoundRect(self, x, y, w, h, aw, ah) draw_user_rect(self, x, y, w, h) end
  function cls.drawLine(self, x1, y1, x2, y2)
    local t = self.qh_tx
    local ax, ay = t:qh_apply(x1, y1)
    local bx, by = t:qh_apply(x2, y2)
    local width, cap = stroke_width(self)
    stroke_dev_seg(self, ax, ay, bx, by, width, cap)
  end
  local function oval(x, y, w, h)
    local pts, n = {}, 24
    local cx, cy, rx, ry = x + w / 2.0, y + h / 2.0, w / 2.0, h / 2.0
    for i = 0, n - 1 do
      local a = 2 * math.pi * i / n
      pts[2 * i + 1] = cx + rx * cos(a)
      pts[2 * i + 2] = cy + ry * sin(a)
    end
    return pts
  end
  function cls.fillOval(self, x, y, w, h) fill_user_poly(self, oval(x, y, w, h)) end
  function cls.drawOval(self, x, y, w, h) stroke_user_path(self, oval(x, y, w, h), true) end
  local function arr_pts(xs, ys, n)
    local pts = {}
    for i = 1, n do pts[2 * i - 1] = xs[i]; pts[2 * i] = ys[i] end
    return pts
  end
  function cls.fillPolygon__Polygon(self, p) self:fill(p) end
  function cls.drawPolygon__Polygon(self, p) self:draw(p) end
  function cls.fillPolygon__intArr_intArr_int(self, xs, ys, n) fill_user_poly(self, arr_pts(xs, ys, n)) end
  function cls.drawPolygon__intArr_intArr_int(self, xs, ys, n) stroke_user_path(self, arr_pts(xs, ys, n), true) end
  function cls.drawPolyline(self, xs, ys, n) stroke_user_path(self, arr_pts(xs, ys, n), false) end

  -- ---------------------------------------------- text
  -- draw.text centres on x with y the baseline; a Java string starts at x.
  -- The client keeps 99 bytes of one text item, so a longer string is drawn
  -- in pieces.
  local TEXT_MAX = 96
  local function pieces(s)
    if #s <= TEXT_MAX then return { s } end
    local out, i = {}, 1
    while i <= #s do
      local j = math.min(i + TEXT_MAX - 1, #s)
      while j < #s and j > i and (s:byte(j + 1) & 0xC0) == 0x80 do j = j - 1 end
      out[#out + 1] = s:sub(i, j)
      i = j + 1
    end
    return out
  end
  function cls.qh_text_dev(self, s, x, y)
    local c = self.qh_clip
    if c and (x < c[1] or x >= c[3] or y < c[2] or y > c[4]) then return end
    local d = self.qh_d
    local rgb = self.qh_color:qh_rgb()
    local ix, iy = ri(x), ri(y)
    for _, p in ipairs(pieces(s)) do
      local w = d.text_width(p)
      if w == nil then return end   -- the font is not resident: text draws nothing this frame
      d.text(ix + w // 2, iy, p, rgb)
      ix = ix + w
    end
  end
  function cls.drawString__String_int_int(self, s, x, y)
    if s == nil then J.npe("drawString(null)") end
    if s == "" then return end
    local dx, dy = self.qh_tx:qh_apply(x, y)
    self:qh_text_dev(s, dx, dy)
  end
  function cls.drawString__String_float_float(self, s, x, y) self:drawString__String_int_int(s, x, y) end

  -- ---------------------------------------------- images
  local function draw_ref(self, ref, x, y, w, h)
    local c = self.qh_clip
    local ix, iy = ri(x), ri(y)
    if c then
      if ix >= c[3] or iy >= c[4] or ix + w <= c[1] or iy + h <= c[2] then return end
      if ix >= c[1] and iy >= c[2] and ix + w <= c[3] and iy + h <= c[4] then
        self.qh_d.image(ref, ix, iy)
      else
        local cx, cy = ri(c[1]), ri(c[2])
        self.qh_d.image_clip(ref, ix, iy, { x = cx, y = cy, width = ri(c[3]) - cx, height = ri(c[4]) - cy })
      end
      return
    end
    self.qh_d.image(ref, ix, iy)
  end
  local function scaled(img, w, h)
    local cache = rawget(img, "qh_scaled_cache")
    if cache == nil then cache = {}; img.qh_scaled_cache = cache end
    local key = w .. "x" .. h
    local s = cache[key]
    if s == nil then
      s = C["java.awt.image.BufferedImage"].qh_scaled(img, w, h, false)
      cache[key] = s
    end
    return s
  end
  function cls.drawImage__Image_int_int_ImageObserver(self, img, x, y, obs)
    if img == nil then return true end
    local ref = img:qh_ref()
    if ref == nil then return false end
    local dx, dy = self.qh_tx:qh_apply(x, y)
    draw_ref(self, ref, dx, dy, img:getWidth(), img:getHeight())
    return true
  end
  function cls.drawImage__Image_int_int_int_int_ImageObserver(self, img, x, y, w, h, obs)
    if img == nil then return true end
    if w <= 0 or h <= 0 then return true end
    if w ~= img:getWidth() or h ~= img:getHeight() then img = scaled(img, w, h) end
    return self:drawImage__Image_int_int_ImageObserver(img, x, y, obs)
  end
  function cls.drawImage__Image_int_int_Color_ImageObserver(self, img, x, y, bg, obs)
    return self:drawImage__Image_int_int_ImageObserver(img, x, y, obs)
  end
  function cls.drawImage__BufferedImage_BufferedImageOp_int_int(self, img, op, x, y)
    self:drawImage__Image_int_int_ImageObserver(img, x, y, nil)
  end
end)

-- A Graphics2D whose target is a canvas BufferedImage: drawImage, fillRect
-- and fill(Rectangle) become ops the image replays into its pixels. Nothing
-- else is drawn into an image by the plugin; anything else refuses loudly.
def("qh.awt.ImageGraphics", "java.awt.Graphics2D", {}, function(cls)
  function cls.qh_over_image(img)
    assert(img)
    local g = setmetatable({}, cls)
    g.qh_img = img
    g.qh_color = C["java.awt.Color"].WHITE
    g.qh_bg = C["java.awt.Color"].BLACK
    g.qh_font = C["net.runelite.client.ui.FontManager"].getRunescapeFont()
    g.qh_stroke = C["java.awt.BasicStroke"].__new__()
    g.qh_tx = C["java.awt.geom.AffineTransform"].__new__()
    g.qh_clip = nil
    g.qh_composite = C["java.awt.AlphaComposite"].SrcOver
    g.qh_hints = {}
    return g
  end
  local function clip4(self)
    local c = self.qh_clip
    if c == nil then return -math.huge, -math.huge, math.huge, math.huge end
    return c[1], c[2], c[3], c[4]
  end
  local function argb(c) return (c:qh_alpha() << 24) | c:qh_rgb() end
  local function dev_xy(self, x, y)
    local t = self.qh_tx
    if not t:qh_translation_only() then uoe("drawing into a BufferedImage under a rotation") end
    return ri(x + t.m02), ri(y + t.m12)
  end
  function cls.drawImage__Image_int_int_ImageObserver(self, img, x, y, obs)
    if img == nil then return true end
    local dx, dy = dev_xy(self, x, y)
    local c0, c1, c2, c3 = clip4(self)
    self.qh_img:qh_op({ "image", img, dx, dy, img:getWidth(), img:getHeight(), c0, c1, c2, c3 })
    return true
  end
  function cls.drawImage__Image_int_int_int_int_ImageObserver(self, img, x, y, w, h, obs)
    if img == nil or w <= 0 or h <= 0 then return true end
    local dx, dy = dev_xy(self, x, y)
    local c0, c1, c2, c3 = clip4(self)
    self.qh_img:qh_op({ "image", img, dx, dy, w, h, c0, c1, c2, c3 })
    return true
  end
  local function rect_op(self, kind, x, y, w, h, color)
    if w <= 0 or h <= 0 then return end
    local dx, dy = dev_xy(self, x, y)
    local c0, c1, c2, c3 = clip4(self)
    self.qh_img:qh_op({ kind, dx, dy, ri(w), ri(h), argb(color) & 0xFFFFFFFF, c0, c1, c2, c3 })
  end
  function cls.fillRect(self, x, y, w, h) rect_op(self, "fill", x, y, w, h, self.qh_color) end
  function cls.clearRect(self, x, y, w, h) rect_op(self, "clear", x, y, w, h, self.qh_bg) end
  function cls.fill(self, s)
    if s ~= nil and J.inst(s, "java.awt.geom.RectangularShape") then
      return rect_op(self, "fill", s:getX(), s:getY(), s:getWidth(), s:getHeight(), self.qh_color)
    end
    uoe("ImageGraphics.fill: only rectangles are drawn into images")
  end
  function cls.draw(self, s) uoe("ImageGraphics.draw") end
  function cls.drawRect() uoe("ImageGraphics.drawRect") end
  function cls.drawLine() uoe("ImageGraphics.drawLine") end
  function cls.drawString__String_int_int() uoe("ImageGraphics.drawString") end
  function cls.drawString__String_float_float() uoe("ImageGraphics.drawString") end
  function cls.fillOval() uoe("ImageGraphics.fillOval") end
  function cls.drawOval() uoe("ImageGraphics.drawOval") end
  function cls.qh_world_outline_fill() return false end
end)

-- ====================================================================== AWT events

def("java.util.EventObject", nil, { "java.io.Serializable" }, function(cls)
  function cls.__init__Object(self, src) self.source = src end
  function cls.getSource(self) return self.source end
end)
def("java.awt.AWTEvent", "java.util.EventObject", {}, function(cls)
  function cls.__init__Object_int(self, src, id) self.source = src; self.id = id; self.consumed = false end
  function cls.getID(self) return self.id end
  function cls.consume(self) self.consumed = true end
  function cls.isConsumed(self) return self.consumed end
end)
def("java.awt.event.InputEvent", "java.awt.AWTEvent", {}, function(cls)
  cls.SHIFT_DOWN_MASK, cls.CTRL_DOWN_MASK, cls.META_DOWN_MASK, cls.ALT_DOWN_MASK = 64, 128, 256, 512
  cls.SHIFT_MASK, cls.CTRL_MASK, cls.META_MASK, cls.ALT_MASK = 1, 2, 4, 8
  function cls.getModifiers(self) return self.modifiers or 0 end
  function cls.getModifiersEx(self) return self.modifiers or 0 end
  function cls.getWhen(self) return self.when or 0 end
  function cls.isShiftDown(self) return ((self.modifiers or 0) & (64 | 1)) ~= 0 end
  function cls.isControlDown(self) return ((self.modifiers or 0) & (128 | 2)) ~= 0 end
  function cls.isAltDown(self) return ((self.modifiers or 0) & (512 | 8)) ~= 0 end
  function cls.isMetaDown(self) return ((self.modifiers or 0) & (256 | 4)) ~= 0 end
end)
def("java.awt.event.KeyEvent", "java.awt.event.InputEvent", {}, function(cls)
  cls.KEY_TYPED, cls.KEY_PRESSED, cls.KEY_RELEASED = 400, 401, 402
  cls.CHAR_UNDEFINED, cls.VK_UNDEFINED = 65535, 0
  cls.VK_ENTER, cls.VK_BACK_SPACE, cls.VK_TAB, cls.VK_SHIFT, cls.VK_CONTROL, cls.VK_ALT = 10, 8, 9, 16, 17, 18
  cls.VK_ESCAPE, cls.VK_SPACE, cls.VK_LEFT, cls.VK_UP, cls.VK_RIGHT, cls.VK_DOWN, cls.VK_DELETE = 27, 32, 37, 38, 39, 40, 127
  cls.VK_PAGE_UP, cls.VK_PAGE_DOWN, cls.VK_END, cls.VK_HOME = 33, 34, 35, 36
  for i = 0, 9 do cls["VK_" .. i] = 48 + i end
  for i = 0, 25 do cls["VK_" .. string.char(65 + i)] = 65 + i end
  for i = 1, 12 do cls["VK_F" .. i] = 111 + i end
  function cls.__init__Component_int_long_int_int_char(self, src, id, when, mods, code, ch)
    self.source, self.id, self.when, self.modifiers, self.keyCode, self.keyChar = src, id, when, mods, code, ch
    self.consumed = false
  end
  function cls.__init__Component_int_long_int_int(self, src, id, when, mods, code)
    cls.__init__Component_int_long_int_int_char(self, src, id, when, mods, code, 65535)
  end
  function cls.getKeyCode(self) return self.keyCode end
  function cls.getKeyChar(self) return self.keyChar end
  function cls.setKeyCode(self, c) self.keyCode = c end
  function cls.setKeyChar(self, c) self.keyChar = c end
  function cls.getKeyLocation(self) return 1 end
  function cls.isActionKey(self) return false end
end)
iface("java.awt.MenuContainer")
