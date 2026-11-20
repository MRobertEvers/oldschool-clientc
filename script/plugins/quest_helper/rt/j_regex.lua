-- java.util.regex for the Quest Helper port: a backtracking matcher over the
-- byte string. Covers the syntax the plugin's patterns use: literals and
-- escapes, ., [classes] with ranges and negation, \d \w \s \D \W \S \b \B,
-- ^ $, (groups), (?:...), (?=...) (?!...), (?i), alternation, and the
-- quantifiers * + ? {m} {m,} {m,n} in greedy, lazy (?) and possessive (+)
-- forms. Backreferences are not supported (no pattern uses one).

local J, C = J, C

local CASE_INSENSITIVE = 2

local function lower(c) if c >= 65 and c <= 90 then return c + 32 end return c end

local function class_digit(c) return c >= 48 and c <= 57 end
local function class_word(c) return (c >= 48 and c <= 57) or (c >= 65 and c <= 90) or (c >= 97 and c <= 122) or c == 95 end
local function class_space(c) return c == 32 or (c >= 9 and c <= 13) end

local function parse(src, flags)
  local pos = 1
  local ngroups = 0
  local icase = (flags & CASE_INSENSITIVE) ~= 0

  local function peek() return src:sub(pos, pos) end
  local function eat() local c = src:sub(pos, pos); pos = pos + 1; return c end
  local function err(msg) J.throw(C["java.lang.IllegalArgumentException"].__new__String("regex: " .. msg .. " in " .. src)) end

  local function escape_class(c)
    if c == "d" then return class_digit end
    if c == "D" then return function(x) return not class_digit(x) end end
    if c == "w" then return class_word end
    if c == "W" then return function(x) return not class_word(x) end end
    if c == "s" then return class_space end
    if c == "S" then return function(x) return not class_space(x) end end
    return nil
  end
  local function escape_char(c)
    if c == "n" then return 10 elseif c == "t" then return 9 elseif c == "r" then return 13
    elseif c == "f" then return 12 elseif c == "0" then return 0 end
    return c:byte()
  end

  local function charset()
    -- after '['
    local neg = false
    if peek() == "^" then neg = true; eat() end
    local items = {}
    local first = true
    while true do
      local c = eat()
      if c == "" then err("unterminated class") end
      if c == "]" and not first then break end
      first = false
      local lo
      if c == "\\" then
        local e = eat()
        local fn = escape_class(e)
        if fn then items[#items + 1] = fn; goto continue end
        lo = escape_char(e)
      elseif c == "[" then
        -- nested class union: [a[b]]
        local inner = charset()
        items[#items + 1] = inner
        goto continue
      else
        lo = c:byte()
      end
      if peek() == "-" and src:sub(pos + 1, pos + 1) ~= "]" and src:sub(pos + 1, pos + 1) ~= "" then
        eat()
        local h = eat()
        local hi = h == "\\" and escape_char(eat()) or h:byte()
        local a, b = lo, hi
        items[#items + 1] = function(x) return x >= a and x <= b end
      else
        local v = lo
        items[#items + 1] = function(x) return x == v end
      end
      ::continue::
    end
    local ic = icase
    return function(x)
      local hit = false
      for i = 1, #items do
        if items[i](x) or (ic and (items[i](lower(x)) or items[i](x >= 97 and x <= 122 and x - 32 or x))) then hit = true; break end
      end
      if neg then return not hit end
      return hit
    end
  end

  local alt

  local function atom()
    local c = eat()
    if c == "(" then
      if peek() == "?" then
        eat()
        local k = eat()
        if k == ":" then
          local n = alt()
          if eat() ~= ")" then err("missing )") end
          return { t = "group", n = n }
        elseif k == "=" or k == "!" then
          local n = alt()
          if eat() ~= ")" then err("missing )") end
          return { t = "look", n = n, neg = (k == "!") }
        else
          -- inline flags (?i) or (?i:...)
          pos = pos - 1
          local fl = src:match("^[imsux%-]+", pos) or ""
          pos = pos + #fl
          if fl:find("i") and not fl:find("%-.*i") then icase = true end
          local nx = eat()
          if nx == ")" then return { t = "empty" } end
          if nx == ":" then
            local n = alt()
            if eat() ~= ")" then err("missing )") end
            return { t = "group", n = n }
          end
          err("unsupported group")
        end
      end
      ngroups = ngroups + 1
      local idx = ngroups
      local n = alt()
      if eat() ~= ")" then err("missing )") end
      return { t = "group", n = n, idx = idx }
    elseif c == "[" then
      return { t = "set", f = charset() }
    elseif c == "." then
      return { t = "set", f = function(x) return x ~= 10 end }
    elseif c == "^" then
      return { t = "bol" }
    elseif c == "$" then
      return { t = "eol" }
    elseif c == "\\" then
      local e = eat()
      if e == "b" then return { t = "wb", neg = false } end
      if e == "B" then return { t = "wb", neg = true } end
      if e == "A" then return { t = "bol" } end
      if e == "z" or e == "Z" then return { t = "eol" } end
      if e == "Q" then
        local j = src:find("\\E", pos, true)
        local lit = src:sub(pos, (j or (#src + 1)) - 1)
        pos = j and (j + 2) or (#src + 1)
        local seq = {}
        for i = 1, #lit do local b = lit:byte(i); seq[#seq + 1] = { t = "char", c = b, ic = icase } end
        return { t = "seq", n = seq }
      end
      local fn = escape_class(e)
      if fn then return { t = "set", f = fn } end
      return { t = "char", c = escape_char(e), ic = icase }
    else
      return { t = "char", c = c:byte(), ic = icase }
    end
  end

  local function quantified()
    local a = atom()
    while true do
      local q = peek()
      local min, max
      if q == "*" then min, max = 0, math.huge
      elseif q == "+" then min, max = 1, math.huge
      elseif q == "?" then min, max = 0, 1
      elseif q == "{" and src:match("^{%d+,?%d*}", pos) then
        local m = src:match("^{(%d+,?%d*)}", pos)
        local a1, comma, b1 = m:match("^(%d+)(,?)(%d*)$")
        min = tonumber(a1)
        if comma == "" then max = min elseif b1 == "" then max = math.huge else max = tonumber(b1) end
        pos = pos + #m + 1
        q = nil
      else
        return a
      end
      if q then eat() end
      local mode = "greedy"
      if peek() == "?" then eat(); mode = "lazy" elseif peek() == "+" then eat(); mode = "possessive" end
      a = { t = "rep", n = a, min = min, max = max, mode = mode }
    end
  end

  local function sequence()
    local items = {}
    while pos <= #src do
      local c = peek()
      if c == "|" or c == ")" then break end
      items[#items + 1] = quantified()
    end
    return { t = "seq", n = items }
  end

  alt = function()
    local branches = { sequence() }
    while peek() == "|" do
      eat()
      branches[#branches + 1] = sequence()
    end
    if #branches == 1 then return branches[1] end
    return { t = "alt", n = branches }
  end

  local root = alt()
  if pos <= #src then err("unbalanced )") end
  return root, ngroups
end

-- m(node, s, i, caps, k): try to match node at byte index i; call k(j) with
-- the index after it; return the first truthy k result.
local match_node

local function is_word_at(s, i) local b = s:byte(i); return b ~= nil and class_word(b) end

match_node = function(node, s, i, caps, k)
  local t = node.t
  if t == "char" then
    local b = s:byte(i)
    if b == nil then return nil end
    if b == node.c or (node.ic and lower(b) == lower(node.c)) then return k(i + 1) end
    return nil
  elseif t == "set" then
    local b = s:byte(i)
    if b == nil or not node.f(b) then return nil end
    return k(i + 1)
  elseif t == "seq" then
    local items = node.n
    local function step(idx, j)
      if idx > #items then return k(j) end
      return match_node(items[idx], s, j, caps, function(j2) return step(idx + 1, j2) end)
    end
    return step(1, i)
  elseif t == "alt" then
    for _, b in ipairs(node.n) do
      local r = match_node(b, s, i, caps, k)
      if r then return r end
    end
    return nil
  elseif t == "group" then
    if not node.idx then return match_node(node.n, s, i, caps, k) end
    local idx = node.idx
    local os, oe = caps[2 * idx - 1], caps[2 * idx]
    local r = match_node(node.n, s, i, caps, function(j)
      local ps, pe = caps[2 * idx - 1], caps[2 * idx]
      caps[2 * idx - 1], caps[2 * idx] = i, j
      local r2 = k(j)
      if not r2 then caps[2 * idx - 1], caps[2 * idx] = ps, pe end
      return r2
    end)
    if not r then caps[2 * idx - 1], caps[2 * idx] = os, oe end
    return r
  elseif t == "look" then
    local saved = { table.unpack(caps) }
    local hit = match_node(node.n, s, i, caps, function(j) return j end)
    if node.neg then
      for x = 1, #saved do caps[x] = saved[x] end
      if hit then return nil end
      return k(i)
    end
    if not hit then return nil end
    return k(i)
  elseif t == "bol" then
    if i == 1 then return k(i) end
    return nil
  elseif t == "eol" then
    if i == #s + 1 then return k(i) end
    return nil
  elseif t == "wb" then
    local at = (is_word_at(s, i - 1) ~= is_word_at(s, i))
    if at ~= node.neg then return k(i) end
    return nil
  elseif t == "empty" then
    return k(i)
  elseif t == "rep" then
    local inner, min, max, mode = node.n, node.min, node.max, node.mode
    if mode == "possessive" then
      local j, count = i, 0
      while count < max do
        local nj = match_node(inner, s, j, caps, function(x) return x end)
        if not nj or nj == j then break end
        j = nj; count = count + 1
      end
      if count < min then return nil end
      return k(j)
    end
    local function try(count, j)
      if mode == "lazy" then
        if count >= min then
          local r = k(j)
          if r then return r end
        end
        if count < max then
          return match_node(inner, s, j, caps, function(j2)
            if j2 == j and count >= min then return nil end
            return try(count + 1, j2)
          end)
        end
        return nil
      end
      if count < max then
        local r = match_node(inner, s, j, caps, function(j2)
          if j2 == j then return nil end
          return try(count + 1, j2)
        end)
        if r then return r end
      end
      if count >= min then return k(j) end
      return nil
    end
    return try(0, i)
  end
  error("regex node " .. tostring(t))
end

local cache = {}
local function compile(src, flags)
  flags = flags or 0
  local key = src .. "\0" .. flags
  local c = cache[key]
  if c then return c end
  local root, ng = parse(src, flags)
  c = { root = root, ngroups = ng, src = src, flags = flags }
  cache[key] = c
  return c
end

-- Search s from byte index `from`; anchored = must match at `from` and
-- `whole` = must end at #s + 1. Returns start, end (exclusive), caps.
local function search(re, s, from, anchored, whole)
  local last = anchored and from or (#s + 1)
  for st = from, last do
    local caps = {}
    local e = match_node(re.root, s, st, caps, function(j)
      if whole and j ~= #s + 1 then return nil end
      return j
    end)
    if e then return st, e, caps end
  end
  return nil
end

-- ------------------------------------------------------------------ classes

J.def("java.util.regex.MatchResult", { kind = "interface", super = "java.lang.Object", ifaces = {} })

J.def("java.util.regex.Pattern", { kind = "class", super = "java.lang.Object", ifaces = {}, body = function(cls)
  cls.CASE_INSENSITIVE = CASE_INSENSITIVE
  cls.MULTILINE = 8
  cls.DOTALL = 32
  local function mk(src, flags)
    local o = setmetatable({}, cls)
    o.re = compile(src, flags)
    return o
  end
  function cls.compile__String(src) return mk(src, 0) end
  function cls.compile__String_int(src, flags) return mk(src, flags) end
  function cls.matcher(self, s) return C["java.util.regex.Matcher"].__new_for(self, J.str(s)) end
  function cls.pattern(self) return self.re.src end
  function cls.toString(self) return self.re.src end
  function cls.flags(self) return self.re.flags end
  function cls.matches__String_CharSequence(re, s) return J.regex_matches(J.str(s), re) end
  function cls.quote(s) return "\\Q" .. s .. "\\E" end
  function cls.split__CharSequence(self, s) return J.regex_split(J.str(s), self, 0) end
  function cls.split__CharSequence_int(self, s, lim) return J.regex_split(J.str(s), self, lim) end
  function cls.asPredicate(self)
    return J.fn("java.util.function.Predicate", "test", function(_, s) return search(self.re, s, 1, false, false) ~= nil end)
  end
end })

J.def("java.util.regex.Matcher", { kind = "class", super = "java.lang.Object", ifaces = { "java.util.regex.MatchResult" }, body = function(cls)
  function cls.__new_for(p, s)
    local o = setmetatable({}, cls)
    o.p, o.s, o.at, o.st, o.en, o.caps, o.app = p, s, 1, nil, nil, nil, 1
    return o
  end
  local function set(self, st, en, caps)
    self.st, self.en, self.caps = st, en, caps
    return st ~= nil
  end
  function cls.find__(self)
    if self.at > #self.s + 1 then return set(self, nil) end
    local st, en, caps = search(self.p.re, self.s, self.at, false, false)
    if st then self.at = (en > st) and en or (en + 1) end
    return set(self, st, en, caps)
  end
  function cls.find__int(self, from)
    self.at = from + 1
    return cls.find__(self)
  end
  function cls.matches(self) return set(self, search(self.p.re, self.s, 1, true, true)) end
  function cls.lookingAt(self) return set(self, search(self.p.re, self.s, 1, true, false)) end
  function cls.reset__(self) self.at = 1; self.st = nil; self.app = 1; return self end
  function cls.reset__CharSequence(self, s) self.s = J.str(s); return cls.reset__(self) end
  local function need(self)
    if self.st == nil then J.throw(C["java.lang.IllegalStateException"].__new__String("No match found")) end
  end
  function cls.group__(self) need(self); return self.s:sub(self.st, self.en - 1) end
  function cls.group__int(self, g)
    need(self)
    if g == 0 then return cls.group__(self) end
    if g > self.p.re.ngroups then J.throw(C["java.lang.IndexOutOfBoundsException"].__new__String("No group " .. g)) end
    local a, b = self.caps[2 * g - 1], self.caps[2 * g]
    if a == nil then return nil end
    return self.s:sub(a, b - 1)
  end
  function cls.groupCount(self) return self.p.re.ngroups end
  function cls.start__(self) need(self); return self.st - 1 end
  function cls.end__(self) need(self); return self.en - 1 end
  function cls.start__int(self, g) need(self); if g == 0 then return self.st - 1 end local a = self.caps[2 * g - 1]; return a and a - 1 or -1 end
  function cls.end__int(self, g) need(self); if g == 0 then return self.en - 1 end local b = self.caps[2 * g]; return b and b - 1 or -1 end
  function cls.hitEnd(self) return self.en == #self.s + 1 end
  function cls.toMatchResult(self) return self end
  function cls.results(self)
    local out = { n = 0 }
    while cls.find__(self) do
      local snap = cls.__new_for(self.p, self.s)
      snap.st, snap.en, snap.caps = self.st, self.en, { table.unpack(self.caps) }
      out.n = out.n + 1
      out[out.n] = snap
    end
    return C["java.util.stream.Stream"].__from(out)
  end
  function cls.replaceAll__String(self, rep) return J.regex_replace(self.s, self.p, rep, true) end
  function cls.replaceFirst__String(self, rep) return J.regex_replace(self.s, self.p, rep, false) end
end })

local function expand(rep, s, st, en, caps)
  local out = {}
  local i = 1
  while i <= #rep do
    local c = rep:sub(i, i)
    if c == "\\" then
      out[#out + 1] = rep:sub(i + 1, i + 1)
      i = i + 2
    elseif c == "$" then
      local d = rep:match("^%d", i + 1)
      if d then
        local g = tonumber(d)
        if g == 0 then out[#out + 1] = s:sub(st, en - 1)
        elseif caps[2 * g - 1] then out[#out + 1] = s:sub(caps[2 * g - 1], caps[2 * g] - 1) end
        i = i + 2
      else
        out[#out + 1] = c
        i = i + 1
      end
    else
      out[#out + 1] = c
      i = i + 1
    end
  end
  return table.concat(out)
end

local function re_of(r)
  if type(r) == "table" and r.re then return r.re end
  return compile(r, 0)
end

-- A replace is a pure function of (pattern, input, replacement), and the
-- framework asks the same ones over and over -- QuestOrders.normalizeQuestName
-- runs a backtracking pattern on two names per comparison of every sort of
-- the quest list (~4000 a sort, 20M+ instructions uncached). Remembered per
-- compiled pattern, bounded.
local REPLACE_MEMO_MAX = 4096
local replace_memo = setmetatable({}, { __mode = "k" })

local function regex_replace(s, r, rep, all)
  local re = re_of(r)
  local out = {}
  local at = 1
  local copied = 1
  while at <= #s + 1 do
    local st, en, caps = search(re, s, at, false, false)
    if not st then break end
    out[#out + 1] = s:sub(copied, st - 1)
    out[#out + 1] = expand(rep, s, st, en, caps)
    copied = en
    if en == st then
      out[#out + 1] = s:sub(st, st)
      copied = st + 1
      at = st + 1
    else
      at = en
    end
    if not all then break end
  end
  out[#out + 1] = s:sub(copied)
  return table.concat(out)
end

function J.regex_replace(s, r, rep, all)
  local re = re_of(r)
  local m = replace_memo[re]
  if m == nil then m = { n = 0 }; replace_memo[re] = m end
  local key = (all and "A" or "F") .. #rep .. ":" .. rep .. s
  local v = m[key]
  if v ~= nil then return v end
  v = regex_replace(s, r, rep, all)
  if m.n >= REPLACE_MEMO_MAX then m = { n = 0 }; replace_memo[re] = m end
  m[key] = v
  m.n = m.n + 1
  return v
end

function J.regex_matches(s, r)
  return search(re_of(r), s, 1, true, true) ~= nil
end

local SPECIAL = "[%.%[%]%(%)%*%+%?%{%}%^%$|\\]"

-- String.split: trailing empty strings removed when limit is 0.
function J.regex_split(s, r, limit)
  local parts = {}
  local literal = type(r) == "string" and not r:find(SPECIAL)
  if #s == 0 then return J.arr({ "" }, 1) end
  local at, from = 1, 1
  while true do
    if limit > 0 and #parts == limit - 1 then break end
    local st, en
    if literal then
      st, en = s:find(r, at, true)
      if st then en = en + 1 end
    else
      st, en = search(re_of(r), s, at, false, false)
    end
    if not st then break end
    if en == st then
      -- a zero-width match splits between characters, never at index 0
      if st > #s then break end
      if st == 1 then at = 2; goto continue end
    end
    parts[#parts + 1] = s:sub(from, st - 1)
    from = en
    at = (en > st) and en or (en + 1)
    if at > #s + 1 then break end
    ::continue::
  end
  parts[#parts + 1] = s:sub(from)
  if limit == 0 then
    while #parts > 1 and parts[#parts] == "" do parts[#parts] = nil end
    if #parts == 1 and parts[1] == "" and #s > 0 then parts = {} end
  end
  parts.n = #parts
  return parts
end
