-- java.awt.Color for harness runs made before rt/awt.lua exists (the
-- rendering agent's file). Loaded only when rt/awt.lua is absent: the same
-- constructor and getter spellings as the contract (inventory.tsv).
local J, C = J, C
if J.defined("java.awt.Color") then return end

J.def("java.awt.Color", { kind = "class", super = "java.lang.Object", ifaces = {}, body = function(cls)
  local function clamp(v) return v & 0xFF end
  function cls.__init__int_int_int(self, r, g, b) self.argb = (0xFF << 24) | (clamp(r) << 16) | (clamp(g) << 8) | clamp(b) end
  function cls.__init__int_int_int_int(self, r, g, b, a) self.argb = (clamp(a) << 24) | (clamp(r) << 16) | (clamp(g) << 8) | clamp(b) end
  function cls.__init__int(self, rgb) self.argb = 0xFF000000 | (rgb & 0xFFFFFF) end
  function cls.__init__int_boolean(self, argb, has_alpha)
    if has_alpha then self.argb = argb & 0xFFFFFFFF else self.argb = 0xFF000000 | (argb & 0xFFFFFF) end
  end
  function cls.getRGB(self) return J.l2i(self.argb) end
  function cls.getRed(self) return (self.argb >> 16) & 0xFF end
  function cls.getGreen(self) return (self.argb >> 8) & 0xFF end
  function cls.getBlue(self) return self.argb & 0xFF end
  function cls.getAlpha(self) return (self.argb >> 24) & 0xFF end
  function cls.equals(self, o) return type(o) == "table" and o.argb == self.argb end
  function cls.hashCode(self) return J.l2i(self.argb) end
  function cls.toString(self)
    return "java.awt.Color[r=" .. cls.getRed(self) .. ",g=" .. cls.getGreen(self) .. ",b=" .. cls.getBlue(self) .. "]"
  end
end, clinit = function(cls)
  local function c(r, g, b) return cls.__new__int_int_int(r, g, b) end
  cls.WHITE, cls.LIGHT_GRAY, cls.GRAY, cls.DARK_GRAY = c(255, 255, 255), c(192, 192, 192), c(128, 128, 128), c(64, 64, 64)
  cls.BLACK, cls.RED, cls.PINK, cls.ORANGE = c(0, 0, 0), c(255, 0, 0), c(255, 175, 175), c(255, 200, 0)
  cls.YELLOW, cls.GREEN, cls.MAGENTA, cls.CYAN, cls.BLUE = c(255, 255, 0), c(0, 255, 0), c(255, 0, 255), c(0, 255, 255), c(0, 0, 255)
end })
