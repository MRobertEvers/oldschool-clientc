-- java.util for the Quest Helper port: collections, Arrays, Collections,
-- Objects, Optional, Comparator, functional interfaces, atomics.
--
-- HashMap and HashSet iterate in Java's order (bucket index under the
-- current capacity, then insertion order within a bucket): helper code may
-- depend on how a HashMap<Integer, QuestStep> iterates, and in Java that is
-- ascending for small keys.

local J, C = J, C

local function def(key, super, ifaces, body, kind)
  J.def(key, { kind = kind or "class", super = super or "java.lang.Object", ifaces = ifaces or {}, body = body })
end
local function iface(key, ifaces, body)
  J.def(key, { kind = "interface", super = "java.lang.Object", ifaces = ifaces or {}, body = body })
end

local equals, hash = J.equals, J.hash

local function uoe() J.throw(C["java.lang.UnsupportedOperationException"].__new__String("unsupported")) end
local function nse() J.throw(C["java.util.NoSuchElementException"].__new__String("no element")) end
local function ioob(i, n) J.throw(C["java.lang.IndexOutOfBoundsException"].__new__String("Index " .. tostring(i) .. " out of bounds for length " .. tostring(n))) end

-- ------------------------------------------------------------------ iterators

iface("java.util.Iterator", {}, function(cls)
  function cls.remove(self) uoe() end
  function cls.forEachRemaining(self, c) while self:hasNext() do c:accept(self:next()) end end
end)
iface("java.util.ListIterator", { "java.util.Iterator" })

-- An iterator over a snapshot array; remove() calls back with the element.
def("java.util.J$ArrayIterator", nil, { "java.util.ListIterator" }, function(cls)
  function cls.__new_for(a, n, on_remove)
    local o = setmetatable({}, cls)
    o.a, o.n, o.i, o.on_remove = a, n, 0, on_remove
    return o
  end
  function cls.hasNext(self) return self.i < self.n end
  function cls.next(self)
    if self.i >= self.n then nse() end
    self.i = self.i + 1
    return self.a[self.i]
  end
  function cls.remove(self)
    if not self.on_remove then uoe() end
    self.on_remove(self.a[self.i], self.i)
  end
end)
local function array_iterator(a, n, on_remove)
  return C["java.util.J$ArrayIterator"].__new_for(a, n, on_remove)
end
J.array_iterator = array_iterator

-- ------------------------------------------------------------------ interfaces

iface("java.util.Collection", { "java.lang.Iterable" })
iface("java.util.SequencedCollection", { "java.util.Collection" })
iface("java.util.List", { "java.util.SequencedCollection" })
iface("java.util.Set", { "java.util.Collection" })
iface("java.util.SortedSet", { "java.util.Set" })
iface("java.util.NavigableSet", { "java.util.SortedSet" })
iface("java.util.Queue", { "java.util.Collection" })
iface("java.util.Deque", { "java.util.Queue" })
iface("java.util.RandomAccess")
iface("java.util.Map")
iface("java.util.SortedMap", { "java.util.Map" })
iface("java.util.NavigableMap", { "java.util.SortedMap" })
iface("java.util.Map$Entry", {}, function(cls)
  function cls.comparingByKey__() return J.fn("java.util.Comparator", "compare", function(_, a, b) return J.compare(a:getKey(), b:getKey()) end) end
  function cls.comparingByValue__() return J.fn("java.util.Comparator", "compare", function(_, a, b) return J.compare(a:getValue(), b:getValue()) end) end
  function cls.comparingByValue__Comparator(c) return J.fn("java.util.Comparator", "compare", function(_, a, b) return c:compare(a:getValue(), b:getValue()) end) end
  function cls.comparingByKey__Comparator(c) return J.fn("java.util.Comparator", "compare", function(_, a, b) return c:compare(a:getKey(), b:getKey()) end) end
end)

-- ------------------------------------------------------------------ AbstractCollection

local function to_lua_array(coll)
  local t, n = {}, 0
  for _, v in J.iter(coll) do n = n + 1; t[n] = v end
  return t, n
end
J.to_lua_array = to_lua_array

def("java.util.AbstractCollection", nil, { "java.util.Collection" }, function(cls)
  function cls.isEmpty(self) return self:size() == 0 end
  function cls.contains(self, o)
    for _, v in J.iter(self) do if equals(v, o) or (v == nil and o == nil) then return true end end
    return false
  end
  function cls.containsAll(self, c)
    for _, v in J.iter(c) do if not self:contains(v) then return false end end
    return true
  end
  function cls.addAll(self, c)
    local changed = false
    local t, n = to_lua_array(c)
    for i = 1, n do if self:add(t[i]) then changed = true end end
    return changed
  end
  function cls.removeAll(self, c)
    local changed = false
    local t, n = to_lua_array(self)
    for i = 1, n do if c:contains(t[i]) then self:remove(t[i]); changed = true end end
    return changed
  end
  function cls.retainAll(self, c)
    local changed = false
    local t, n = to_lua_array(self)
    for i = 1, n do if not c:contains(t[i]) then self:remove(t[i]); changed = true end end
    return changed
  end
  function cls.removeIf(self, p)
    local changed = false
    local t, n = to_lua_array(self)
    for i = 1, n do if p:test(t[i]) then self:remove(t[i]); changed = true end end
    return changed
  end
  function cls.toArray__(self)
    local t, n = to_lua_array(self)
    t.n = n
    return t
  end
  cls.toArray__ObjectArr = cls.toArray__
  cls.toArray__IntFunction = cls.toArray__
  function cls.stream(self)
    local t, n = to_lua_array(self)
    t.n = n
    return C["java.util.stream.Stream"].__from(t)
  end
  cls.parallelStream = cls.stream
  function cls.toString(self)
    local parts = {}
    for _, v in J.iter(self) do parts[#parts + 1] = rawequal(v, self) and "(this Collection)" or J.str(v) end
    return "[" .. table.concat(parts, ", ") .. "]"
  end
  function cls.add(self) uoe() end
  function cls.remove(self) uoe() end
  function cls.clear(self)
    local t, n = to_lua_array(self)
    for i = 1, n do self:remove(t[i]) end
  end
  function cls.__iter(self)
    local it = self:iterator()
    local i = 0
    return function()
      if it:hasNext() then i = i + 1; return i, it:next() end
    end
  end
end)

-- ------------------------------------------------------------------ ArrayList

local function check_index(self, i, n)
  if i < 0 or i >= n then ioob(i, n) end
end

def("java.util.AbstractList", "java.util.AbstractCollection", { "java.util.List" }, function(cls) end)

def("java.util.ArrayList", "java.util.AbstractList", { "java.util.List", "java.util.RandomAccess", "java.lang.Cloneable", "java.io.Serializable" }, function(cls)
  function cls.__init__(self) self.a = {}; self.sz = 0 end
  function cls.__init__int(self) self.a = {}; self.sz = 0 end
  function cls.__init__Collection(self, c)
    local t, n = to_lua_array(c)
    self.a = t; self.sz = n
  end
  function cls.__iter(self)
    local i, a = 0, self.a
    return function()
      i = i + 1
      if i <= self.sz then return i, a[i] end
    end
  end
  function cls.size(self) return self.sz end
  function cls.isEmpty(self) return self.sz == 0 end
  function cls.get(self, i)
    check_index(self, i, self.sz)
    return self.a[i + 1]
  end
  function cls.set(self, i, v)
    check_index(self, i, self.sz)
    local old = self.a[i + 1]
    self.a[i + 1] = v
    return old
  end
  function cls.add(self, v)
    local n = self.sz + 1
    self.a[n] = v
    self.sz = n
    return true
  end
  function cls.add__int_Object(self, i, v)
    if i < 0 or i > self.sz then ioob(i, self.sz) end
    local a = self.a
    for k = self.sz, i + 1, -1 do a[k + 1] = a[k] end
    a[i + 1] = v
    self.sz = self.sz + 1
  end
  cls.addLast = cls.add
  function cls.addFirst(self, v) cls.add__int_Object(self, 0, v) end
  function cls.addAll(self, c)
    local t, n = to_lua_array(c)
    local a, sz = self.a, self.sz
    for i = 1, n do a[sz + i] = t[i] end
    self.sz = sz + n
    return n > 0
  end
  function cls.addAll__int_Collection(self, idx, c)
    local t, n = to_lua_array(c)
    for i = 1, n do cls.add__int_Object(self, idx + i - 1, t[i]) end
    return n > 0
  end
  function cls.remove__int(self, i)
    check_index(self, i, self.sz)
    local a = self.a
    local old = a[i + 1]
    for k = i + 1, self.sz - 1 do a[k] = a[k + 1] end
    a[self.sz] = nil
    self.sz = self.sz - 1
    return old
  end
  function cls.indexOf(self, o)
    local a = self.a
    for i = 1, self.sz do
      local v = a[i]
      if v == o or (v ~= nil and equals(v, o)) then return i - 1 end
    end
    return -1
  end
  function cls.lastIndexOf(self, o)
    local a = self.a
    for i = self.sz, 1, -1 do
      local v = a[i]
      if v == o or (v ~= nil and equals(v, o)) then return i - 1 end
    end
    return -1
  end
  function cls.contains(self, o) return cls.indexOf(self, o) >= 0 end
  function cls.remove(self, o)
    local i = cls.indexOf(self, o)
    if i < 0 then return false end
    cls.remove__int(self, i)
    return true
  end
  function cls.clear(self) self.a = {}; self.sz = 0 end
  function cls.removeIf(self, p)
    local a, n, w = self.a, self.sz, 0
    local out = {}
    for i = 1, n do
      local v = a[i]
      if not p:test(v) then w = w + 1; out[w] = v end
    end
    self.a = out
    self.sz = w
    return w ~= n
  end
  function cls.removeAll(self, c)
    return cls.removeIf(self, J.fn("java.util.function.Predicate", "test", function(_, v) return c:contains(v) end))
  end
  function cls.retainAll(self, c)
    return cls.removeIf(self, J.fn("java.util.function.Predicate", "test", function(_, v) return not c:contains(v) end))
  end
  function cls.iterator(self)
    local n = self.sz
    local snap = {}
    for i = 1, n do snap[i] = self.a[i] end
    local it
    it = array_iterator(snap, n, function(v)
      -- remove the element the iterator last returned, at its live position
      local pos = it.i - 1 - (it.removed or 0)
      cls.remove__int(self, pos)
      it.removed = (it.removed or 0) + 1
    end)
    return it
  end
  cls.listIterator__ = cls.iterator
  function cls.forEach(self, c)
    local a = self.a
    for i = 1, self.sz do c:accept(a[i]) end
  end
  function cls.toArray__(self)
    local t = { n = self.sz }
    for i = 1, self.sz do t[i] = self.a[i] end
    return t
  end
  cls.toArray__ObjectArr = cls.toArray__
  cls.toArray__IntFunction = cls.toArray__
  function cls.stream(self) return C["java.util.stream.Stream"].__from(cls.toArray__(self)) end
  cls.parallelStream = cls.stream
  function cls.sort(self, cmp)
    J.stable_sort(self.a, self.sz, cmp)
  end
  function cls.replaceAll(self, op)
    for i = 1, self.sz do self.a[i] = op:apply(self.a[i]) end
  end
  function cls.subList(self, from, to)
    if from < 0 or to > self.sz or from > to then ioob(from, self.sz) end
    local r = cls.__new__()
    for i = from + 1, to do r:add(self.a[i]) end
    return r
  end
  function cls.getFirst(self) if self.sz == 0 then nse() end return self.a[1] end
  function cls.getLast(self) if self.sz == 0 then nse() end return self.a[self.sz] end
  function cls.removeFirst(self) if self.sz == 0 then nse() end return cls.remove__int(self, 0) end
  function cls.removeLast(self) if self.sz == 0 then nse() end return cls.remove__int(self, self.sz - 1) end
  function cls.reversed(self)
    local r = cls.__new__()
    for i = self.sz, 1, -1 do r:add(self.a[i]) end
    return r
  end
  function cls.ensureCapacity(self) end
  function cls.trimToSize(self) end
  function cls.clone(self) return cls.__new__Collection(self) end
  function cls.equals(self, o)
    if rawequal(self, o) then return true end
    if type(o) ~= "table" or not J.inst(o, "java.util.List") then return false end
    if o:size() ~= self.sz then return false end
    local i = 0
    for _, v in J.iter(o) do
      i = i + 1
      local w = self.a[i]
      if not (w == v or (w ~= nil and equals(w, v))) then return false end
    end
    return true
  end
  function cls.hashCode(self)
    local h = 1
    for i = 1, self.sz do h = J.l2i(31 * h + (self.a[i] == nil and 0 or hash(self.a[i]))) end
    return h
  end
  -- Queue/Deque, for LinkedList and ArrayDeque which share this storage
  function cls.offer(self, v) return cls.add(self, v) end
  cls.offerLast = cls.offer
  function cls.offerFirst(self, v) cls.add__int_Object(self, 0, v); return true end
  function cls.push(self, v) cls.add__int_Object(self, 0, v) end
  function cls.pop(self) return cls.removeFirst(self) end
  function cls.poll(self) if self.sz == 0 then return nil end return cls.remove__int(self, 0) end
  cls.pollFirst = cls.poll
  function cls.pollLast(self) if self.sz == 0 then return nil end return cls.remove__int(self, self.sz - 1) end
  function cls.peek(self) return self.a[1] end
  cls.peekFirst = cls.peek
  function cls.peekLast(self) return self.a[self.sz] end
  function cls.element(self) return cls.getFirst(self) end
  function cls.remove__(self) return cls.removeFirst(self) end
  function cls.descendingIterator(self) return cls.reversed(self):iterator() end
end)

J.def("java.util.LinkedList", { kind = "class", super = "java.util.ArrayList", ifaces = { "java.util.List", "java.util.Deque" }, body = function(cls)
  function cls.__init__(self) self.a = {}; self.sz = 0 end
end })
J.def("java.util.ArrayDeque", { kind = "class", super = "java.util.ArrayList", ifaces = { "java.util.Deque" }, body = function(cls)
  function cls.__init__(self) self.a = {}; self.sz = 0 end
end })
J.def("java.util.Vector", { kind = "class", super = "java.util.ArrayList", ifaces = {}, body = function(cls) end })
J.def("java.util.Stack", { kind = "class", super = "java.util.ArrayList", ifaces = {}, body = function(cls)
  function cls.__init__(self) self.a = {}; self.sz = 0 end
  function cls.push(self, v) self:add(v); return v end
  function cls.pop(self) return self:removeLast() end
  function cls.peek(self) return self:getLast() end
  function cls.empty(self) return self.sz == 0 end
end })
J.def("java.util.concurrent.CopyOnWriteArrayList", { kind = "class", super = "java.util.ArrayList", ifaces = {}, body = function(cls) end })

-- A stable merge sort over a[1..n] (List.sort and Collections.sort are stable).
function J.stable_sort(a, n, cmp)
  local function less_eq(x, y)
    if cmp then return cmp:compare(x, y) <= 0 end
    return J.compare(x, y) <= 0
  end
  local tmp = {}
  local width = 1
  while width < n do
    local i = 1
    while i <= n do
      local mid = math.min(i + width, n + 1)
      local hi = math.min(i + 2 * width, n + 1)
      local l, r, k = i, mid, i
      while l < mid and r < hi do
        if less_eq(a[l], a[r]) then tmp[k] = a[l]; l = l + 1 else tmp[k] = a[r]; r = r + 1 end
        k = k + 1
      end
      while l < mid do tmp[k] = a[l]; l = l + 1; k = k + 1 end
      while r < hi do tmp[k] = a[r]; r = r + 1; k = k + 1 end
      i = i + 2 * width
    end
    for k = 1, n do a[k] = tmp[k] end
    width = width * 2
  end
end

-- ------------------------------------------------------------------ maps

local NULLKEY = {}
local OBJECT_EQUALS -- resolved lazily

local function raw_key(k)
  if k == nil then return NULLKEY, false end
  if type(k) ~= "table" then return k, false end
  if k.__key == nil then return k, false end
  OBJECT_EQUALS = OBJECT_EQUALS or C["java.lang.Object"].equals
  local eq = k.equals
  if eq == OBJECT_EQUALS or eq == C["java.lang.Enum"].equals then return k, false end
  return nil, true
end

local function spread(h, cap)
  local u = h & 0xFFFFFFFF
  u = u ~ (u >> 16)
  return u & (cap - 1)
end

local function table_size_for(c)
  local n = 1
  while n < c do n = n * 2 end
  if n < 1 then n = 1 end
  return n
end

def("java.util.AbstractMap", nil, { "java.util.Map" }, function(cls) end)

-- Entry objects double as Map.Entry.
def("java.util.HashMap$Node", nil, { "java.util.Map$Entry" }, function(cls)
  function cls.getKey(self) return self.k end
  function cls.getValue(self) return self.v end
  function cls.setValue(self, v) local o = self.v; self.v = v; return o end
  function cls.equals(self, o)
    return type(o) == "table" and o.__key ~= nil and J.inst(o, "java.util.Map$Entry")
      and (self.k == o:getKey() or equals(self.k, o:getKey())) and (self.v == o:getValue() or equals(self.v, o:getValue()))
  end
  function cls.hashCode(self) return (self.k == nil and 0 or hash(self.k)) ~ (self.v == nil and 0 or hash(self.v)) end
  function cls.toString(self) return J.str(self.k) .. "=" .. J.str(self.v) end
end)

def("java.util.HashMap", "java.util.AbstractMap", { "java.util.Map", "java.lang.Cloneable", "java.io.Serializable" }, function(cls)
  local Node
  local function init(self, cap)
    self.raw = {}        -- raw key -> node
    self.hashed = {}     -- hash -> { nodes } for keys with their own equals
    self.nodes = {}      -- live nodes, unordered
    self.count = 0
    self.seq = 0
    self.cap = cap or 16
    self.order = nil
  end
  cls.__map_init = init
  function cls.__init__(self) init(self, 16) end
  function cls.__init__int(self, c) init(self, table_size_for(math.max(c, 1))) end
  function cls.__init__int_float(self, c) init(self, table_size_for(math.max(c, 1))) end
  function cls.__init__Map(self, m)
    init(self, table_size_for(math.max(math.floor(m:size() / 0.75) + 1, 16)))
    self:putAll(m)
  end
  local function find(self, k)
    local rk, hashed = raw_key(k)
    if not hashed then return self.raw[rk] end
    local bucket = self.hashed[hash(k)]
    if bucket then
      for _, n in ipairs(bucket) do if equals(n.k, k) then return n end end
    end
    return nil
  end
  cls.__find = find
  local function insert(self, k, v)
    Node = Node or C["java.util.HashMap$Node"]
    local n = setmetatable({ k = k, v = v }, Node)
    self.seq = self.seq + 1
    n.seq = self.seq
    n.h = (k == nil) and 0 or hash(k)
    local rk, hashed = raw_key(k)
    if hashed then
      local b = self.hashed[n.h]
      if not b then b = {}; self.hashed[n.h] = b end
      b[#b + 1] = n
    else
      self.raw[rk] = n
    end
    self.nodes[n] = true
    self.count = self.count + 1
    while self.count > self.cap * 0.75 do self.cap = self.cap * 2 end
    self.order = nil
    return n
  end
  local function unlink(self, n)
    local rk, hashed = raw_key(n.k)
    if hashed then
      local b = self.hashed[n.h]
      for i, x in ipairs(b) do if x == n then table.remove(b, i); break end end
      if #b == 0 then self.hashed[n.h] = nil end
    else
      self.raw[rk] = nil
    end
    self.nodes[n] = nil
    self.count = self.count - 1
    self.order = nil
  end
  cls.__unlink = unlink
  -- The order Java would iterate in.
  function cls.__ordered(self)
    local o = self.order
    if o then return o end
    o = {}
    for n in pairs(self.nodes) do o[#o + 1] = n end
    local cap = self.cap
    table.sort(o, function(a, b)
      local ba, bb = spread(a.h, cap), spread(b.h, cap)
      if ba ~= bb then return ba < bb end
      return a.seq < b.seq
    end)
    self.order = o
    return o
  end
  function cls.size(self) return self.count end
  function cls.isEmpty(self) return self.count == 0 end
  function cls.get(self, k)
    local n = find(self, k)
    if n then return n.v end
    return nil
  end
  function cls.getOrDefault(self, k, d)
    local n = find(self, k)
    if n then return n.v end
    return d
  end
  function cls.containsKey(self, k) return find(self, k) ~= nil end
  function cls.containsValue(self, v)
    for n in pairs(self.nodes) do if n.v == v or equals(n.v, v) then return true end end
    return false
  end
  function cls.put(self, k, v)
    local n = find(self, k)
    if n then local o = n.v; n.v = v; return o end
    insert(self, k, v)
    return nil
  end
  function cls.putIfAbsent(self, k, v)
    local n = find(self, k)
    if n and n.v ~= nil then return n.v end
    if n then n.v = v; return nil end
    insert(self, k, v)
    return nil
  end
  function cls.putAll(self, m)
    for _, e in J.iter(m:entrySet()) do cls.put(self, e:getKey(), e:getValue()) end
  end
  function cls.remove__Object(self, k)
    local n = find(self, k)
    if not n then return nil end
    unlink(self, n)
    return n.v
  end
  function cls.remove__Object_Object(self, k, v)
    local n = find(self, k)
    if not n or not (n.v == v or equals(n.v, v)) then return false end
    unlink(self, n)
    return true
  end
  function cls.clear(self)
    local cap = self.cap
    init(self, cap)
  end
  function cls.computeIfAbsent(self, k, f)
    local n = find(self, k)
    if n and n.v ~= nil then return n.v end
    local v = f:apply(k)
    if v ~= nil then
      if n then n.v = v else insert(self, k, v) end
    end
    return v
  end
  function cls.computeIfPresent(self, k, f)
    local n = find(self, k)
    if not n or n.v == nil then return nil end
    local v = f:apply(k, n.v)
    if v == nil then unlink(self, n) else n.v = v end
    return v
  end
  function cls.compute(self, k, f)
    local n = find(self, k)
    local old = nil
    if n then old = n.v end
    local v = f:apply(k, old)
    if v == nil then
      if n then unlink(self, n) end
    elseif n then n.v = v else insert(self, k, v) end
    return v
  end
  function cls.merge(self, k, v, f)
    local n = find(self, k)
    if not n or n.v == nil then
      if n then n.v = v else insert(self, k, v) end
      return v
    end
    local nv = f:apply(n.v, v)
    if nv == nil then unlink(self, n) else n.v = nv end
    return nv
  end
  function cls.replace__Object_Object(self, k, v)
    local n = find(self, k)
    if not n then return nil end
    local o = n.v
    n.v = v
    return o
  end
  function cls.replace__Object_Object_Object(self, k, ov, nv)
    local n = find(self, k)
    if not n or not equals(n.v, ov) then return false end
    n.v = nv
    return true
  end
  function cls.replaceAll(self, f)
    for _, n in ipairs(self:__ordered()) do n.v = f:apply(n.k, n.v) end
  end
  function cls.forEach(self, f)
    for _, n in ipairs(self:__ordered()) do f:accept(n.k, n.v) end
  end
  function cls.keySet(self) return C["java.util.J$MapView"].__new_for(self, "k") end
  function cls.values(self) return C["java.util.J$MapView"].__new_for(self, "v") end
  function cls.entrySet(self) return C["java.util.J$MapView"].__new_for(self, "e") end
  function cls.equals(self, o)
    if rawequal(self, o) then return true end
    if type(o) ~= "table" or not J.inst(o, "java.util.Map") or o:size() ~= self.count then return false end
    for n in pairs(self.nodes) do
      if not o:containsKey(n.k) then return false end
      local w = o:get(n.k)
      if not (w == n.v or equals(w, n.v)) then return false end
    end
    return true
  end
  function cls.hashCode(self)
    local h = 0
    for n in pairs(self.nodes) do h = J.l2i(h + n:hashCode()) end
    return h
  end
  function cls.toString(self)
    local parts = {}
    for _, n in ipairs(self:__ordered()) do parts[#parts + 1] = J.str(n.k) .. "=" .. J.str(n.v) end
    return "{" .. table.concat(parts, ", ") .. "}"
  end
  function cls.clone(self) return cls.__new__Map(self) end
end)

-- Insertion order.
J.def("java.util.LinkedHashMap", { kind = "class", super = "java.util.HashMap", ifaces = { "java.util.Map" }, body = function(cls, super)
  function cls.__init__(self) super.__map_init(self, 16) end
  function cls.__init__int(self) super.__map_init(self, 16) end
  function cls.__init__int_float_boolean(self) super.__map_init(self, 16) end
  function cls.__init__Map(self, m) super.__map_init(self, 16); self:putAll(m) end
  function cls.__ordered(self)
    local o = self.order
    if o then return o end
    o = {}
    for n in pairs(self.nodes) do o[#o + 1] = n end
    table.sort(o, function(a, b) return a.seq < b.seq end)
    self.order = o
    return o
  end
end })
J.def("java.util.concurrent.ConcurrentHashMap", { kind = "class", super = "java.util.HashMap", ifaces = {}, body = function(cls) end })
J.def("java.util.Hashtable", { kind = "class", super = "java.util.HashMap", ifaces = {}, body = function(cls) end })

-- Sorted by the comparator (or natural order).
J.def("java.util.TreeMap", { kind = "class", super = "java.util.HashMap", ifaces = { "java.util.NavigableMap" }, body = function(cls, super)
  function cls.__init__(self) super.__map_init(self, 16) end
  function cls.__init__Comparator(self, c) super.__map_init(self, 16); self.cmp = c end
  function cls.__init__Map(self, m) super.__map_init(self, 16); self:putAll(m) end
  function cls.__init__SortedMap(self, m) super.__map_init(self, 16); self.cmp = m.cmp; self:putAll(m) end
  local function compare(self, a, b)
    if self.cmp then return self.cmp:compare(a, b) end
    return J.compare(a, b)
  end
  function cls.__ordered(self)
    local o = self.order
    if o then return o end
    o = {}
    for n in pairs(self.nodes) do o[#o + 1] = n end
    table.sort(o, function(a, b) return compare(self, a.k, b.k) < 0 end)
    self.order = o
    return o
  end
  -- keys compare by the comparator, not by equals
  function cls.__find(self, k)
    for n in pairs(self.nodes) do if compare(self, n.k, k) == 0 then return n end end
    return nil
  end
  local function first(self) return self:__ordered()[1] end
  local function last(self) local o = self:__ordered(); return o[#o] end
  function cls.firstKey(self) local n = first(self); if not n then nse() end return n.k end
  function cls.lastKey(self) local n = last(self); if not n then nse() end return n.k end
  function cls.firstEntry(self) return first(self) end
  function cls.lastEntry(self) return last(self) end
  function cls.pollFirstEntry(self) local n = first(self); if n then super.__unlink(self, n) end return n end
  function cls.pollLastEntry(self) local n = last(self); if n then super.__unlink(self, n) end return n end
  local function bound(self, k, pred)
    local best
    for _, n in ipairs(self:__ordered()) do if pred(compare(self, n.k, k)) then best = n end end
    return best
  end
  local function bound_first(self, k, pred)
    for _, n in ipairs(self:__ordered()) do if pred(compare(self, n.k, k)) then return n end end
    return nil
  end
  function cls.floorEntry(self, k) return bound(self, k, function(c) return c <= 0 end) end
  function cls.lowerEntry(self, k) return bound(self, k, function(c) return c < 0 end) end
  function cls.ceilingEntry(self, k) return bound_first(self, k, function(c) return c >= 0 end) end
  function cls.higherEntry(self, k) return bound_first(self, k, function(c) return c > 0 end) end
  function cls.floorKey(self, k) local n = cls.floorEntry(self, k); return n and n.k end
  function cls.lowerKey(self, k) local n = cls.lowerEntry(self, k); return n and n.k end
  function cls.ceilingKey(self, k) local n = cls.ceilingEntry(self, k); return n and n.k end
  function cls.higherKey(self, k) local n = cls.higherEntry(self, k); return n and n.k end
  local function sub(self, pred)
    local r = cls.__new__Comparator(self.cmp)
    for _, n in ipairs(self:__ordered()) do if pred(n.k) then r:put(n.k, n.v) end end
    return r
  end
  function cls.headMap__Object(self, k) return sub(self, function(x) return compare(self, x, k) < 0 end) end
  function cls.headMap__Object_boolean(self, k, inc) return sub(self, function(x) local c = compare(self, x, k); return c < 0 or (inc and c == 0) end) end
  function cls.tailMap__Object(self, k) return sub(self, function(x) return compare(self, x, k) >= 0 end) end
  function cls.tailMap__Object_boolean(self, k, inc) return sub(self, function(x) local c = compare(self, x, k); return c > 0 or (inc and c == 0) end) end
  function cls.descendingMap(self)
    local base = self
    return cls.__new__Comparator(J.fn("java.util.Comparator", "compare", function(_, a, b) return compare(base, b, a) end)):__fill(self)
  end
  function cls.__fill(self, m) self:putAll(m); return self end
  function cls.navigableKeySet(self) return self:keySet() end
  function cls.descendingKeySet(self) return cls.descendingMap(self):keySet() end
  function cls.comparator(self) return self.cmp end
end })

-- EnumMap: ordinal order.
J.def("java.util.EnumMap", { kind = "class", super = "java.util.HashMap", ifaces = {}, body = function(cls, super)
  function cls.__init__Class(self) super.__map_init(self, 16) end
  function cls.__init__EnumMap(self, m) super.__map_init(self, 16); self:putAll(m) end
  function cls.__init__Map(self, m) super.__map_init(self, 16); self:putAll(m) end
  function cls.__ordered(self)
    local o = self.order
    if o then return o end
    o = {}
    for n in pairs(self.nodes) do o[#o + 1] = n end
    table.sort(o, function(a, b) return a.k.__ordinal < b.k.__ordinal end)
    self.order = o
    return o
  end
end })

-- keySet() / values() / entrySet(): live views over the map.
def("java.util.J$MapView", "java.util.AbstractCollection", { "java.util.Set" }, function(cls)
  function cls.__new_for(map, kind)
    local o = setmetatable({}, cls)
    o.m, o.kind = map, kind
    return o
  end
  local function item(kind, n)
    if kind == "k" then return n.k elseif kind == "v" then return n.v end
    return n
  end
  function cls.size(self) return self.m:size() end
  function cls.isEmpty(self) return self.m:size() == 0 end
  function cls.__iter(self)
    local o = self.m:__ordered()
    local snap = {}
    for i = 1, #o do snap[i] = o[i] end
    local i, kind = 0, self.kind
    return function()
      i = i + 1
      local n = snap[i]
      if n then return i, item(kind, n) end
    end
  end
  function cls.iterator(self)
    local o = self.m:__ordered()
    local snap = {}
    for i = 1, #o do snap[i] = item(self.kind, o[i]) end
    local nodes = {}
    for i = 1, #o do nodes[i] = o[i] end
    local m = self.m
    return array_iterator(snap, #snap, function(_, i) m:__unlink(nodes[i]) end)
  end
  function cls.contains(self, x)
    if self.kind == "k" then return self.m:containsKey(x) end
    if self.kind == "v" then return self.m:containsValue(x) end
    local n = self.m:__find(x:getKey())
    return n ~= nil and (n.v == x:getValue() or equals(n.v, x:getValue()))
  end
  function cls.remove(self, x)
    if self.kind == "k" then
      local had = self.m:containsKey(x)
      self.m:remove__Object(x)
      return had
    end
    for _, n in ipairs(self.m:__ordered()) do
      if (self.kind == "v" and (n.v == x or equals(n.v, x))) or (self.kind == "e" and n:equals(x)) then
        self.m:__unlink(n)
        return true
      end
    end
    return false
  end
  function cls.removeIf(self, p)
    local changed = false
    local o = self.m:__ordered()
    local snap = {}
    for i = 1, #o do snap[i] = o[i] end
    for _, n in ipairs(snap) do
      if p:test(item(self.kind, n)) then self.m:__unlink(n); changed = true end
    end
    return changed
  end
  function cls.clear(self) self.m:clear() end
  function cls.equals(self, o)
    if rawequal(self, o) then return true end
    if type(o) ~= "table" or not J.inst(o, "java.util.Collection") or o:size() ~= self:size() then return false end
    return self:containsAll(o)
  end
  function cls.hashCode(self)
    local h = 0
    for _, v in J.iter(self) do h = J.l2i(h + (v == nil and 0 or hash(v))) end
    return h
  end
end)

def("java.util.AbstractMap$SimpleEntry", nil, { "java.util.Map$Entry" }, function(cls)
  function cls.__init__Object_Object(self, k, v) self.k = k; self.v = v end
  function cls.__init__Entry(self, e) self.k = e:getKey(); self.v = e:getValue() end
  function cls.getKey(self) return self.k end
  function cls.getValue(self) return self.v end
  function cls.setValue(self, v) local o = self.v; self.v = v; return o end
  function cls.equals(self, o)
    return type(o) == "table" and o.__key ~= nil and J.inst(o, "java.util.Map$Entry")
      and (self.k == o:getKey() or equals(self.k, o:getKey())) and (self.v == o:getValue() or equals(self.v, o:getValue()))
  end
  function cls.hashCode(self) return (self.k == nil and 0 or hash(self.k)) ~ (self.v == nil and 0 or hash(self.v)) end
  function cls.toString(self) return J.str(self.k) .. "=" .. J.str(self.v) end
end)
J.def("java.util.AbstractMap$SimpleImmutableEntry", { kind = "class", super = "java.util.AbstractMap$SimpleEntry", ifaces = {}, body = function(cls) end })

-- ------------------------------------------------------------------ sets

def("java.util.AbstractSet", "java.util.AbstractCollection", { "java.util.Set" }, function(cls) end)

def("java.util.HashSet", "java.util.AbstractSet", { "java.util.Set", "java.lang.Cloneable", "java.io.Serializable" }, function(cls)
  local function mk(self, m) self.m = m end
  function cls.__map() return C["java.util.HashMap"].__new__() end
  function cls.__init__(self) mk(self, self.__map()) end
  function cls.__init__int(self, c) mk(self, self.__map()) end
  function cls.__init__int_float(self, c) mk(self, self.__map()) end
  function cls.__init__Collection(self, c)
    local m = self.__map()
    if m.__key == "java.util.HashMap" then
      m.cap = table_size_for(math.max(math.floor(c:size() / 0.75) + 1, 16))
    end
    mk(self, m)
    for _, v in J.iter(c) do m:put(v, true) end
  end
  function cls.size(self) return self.m:size() end
  function cls.isEmpty(self) return self.m:size() == 0 end
  function cls.add(self, v)
    if self.m:containsKey(v) then return false end
    self.m:put(v, true)
    return true
  end
  function cls.remove(self, v)
    if not self.m:containsKey(v) then return false end
    self.m:remove__Object(v)
    return true
  end
  function cls.contains(self, v) return self.m:containsKey(v) end
  function cls.clear(self) self.m:clear() end
  function cls.__iter(self) return C["java.util.J$MapView"].__iter(self.m:keySet()) end
  function cls.iterator(self) return self.m:keySet():iterator() end
  function cls.removeIf(self, p) return self.m:keySet():removeIf(p) end
  function cls.equals(self, o)
    if rawequal(self, o) then return true end
    if type(o) ~= "table" or not J.inst(o, "java.util.Set") or o:size() ~= self:size() then return false end
    return self:containsAll(o)
  end
  function cls.hashCode(self)
    local h = 0
    for _, v in J.iter(self) do h = J.l2i(h + (v == nil and 0 or hash(v))) end
    return h
  end
  function cls.clone(self) return cls.__new__Collection(self) end
end)
J.def("java.util.LinkedHashSet", { kind = "class", super = "java.util.HashSet", ifaces = { "java.util.SequencedCollection" }, body = function(cls)
  function cls.__map() return C["java.util.LinkedHashMap"].__new__() end
  function cls.getFirst(self) for _, v in J.iter(self) do return v end nse() end
end })
J.def("java.util.TreeSet", { kind = "class", super = "java.util.HashSet", ifaces = { "java.util.NavigableSet" }, body = function(cls, super)
  function cls.__map() return C["java.util.TreeMap"].__new__() end
  function cls.__init__Comparator(self, c) self.m = C["java.util.TreeMap"].__new__Comparator(c) end
  function cls.__init__SortedSet(self, s) self.m = C["java.util.TreeMap"].__new__Comparator(s.m.cmp); self:addAll(s) end
  function cls.first(self) return self.m:firstKey() end
  function cls.last(self) return self.m:lastKey() end
  function cls.floor(self, k) return self.m:floorKey(k) end
  function cls.ceiling(self, k) return self.m:ceilingKey(k) end
  function cls.higher(self, k) return self.m:higherKey(k) end
  function cls.lower(self, k) return self.m:lowerKey(k) end
  function cls.pollFirst(self) local n = self.m:pollFirstEntry(); return n and n.k end
  function cls.pollLast(self) local n = self.m:pollLastEntry(); return n and n.k end
  function cls.descendingSet(self)
    local r = cls.__new__Comparator(J.fn("java.util.Comparator", "compare", function(_, a, b) return J.compare(b, a) end))
    r:addAll(self)
    return r
  end
end })
J.def("java.util.EnumSet", { kind = "class", super = "java.util.HashSet", ifaces = {}, body = function(cls)
  function cls.__map() return C["java.util.EnumMap"].__new__Class(nil) end
  local function make(items)
    local s = cls.__new__()
    for _, v in ipairs(items) do s:add(v) end
    return s
  end
  function cls.of__Enum(a) return make({ a }) end
  function cls.of__Enum_Enum(a, b) return make({ a, b }) end
  function cls.of__Enum_Enum_Enum(a, b, c) return make({ a, b, c }) end
  function cls.of__Enum_Enum_Enum_Enum(a, b, c, d) return make({ a, b, c, d }) end
  function cls.of__Enum_Enum_Enum_Enum_Enum(a, b, c, d, e) return make({ a, b, c, d, e }) end
  function cls.of__Enum_EnumArr(a, rest)
    local s = make({ a })
    for i = 1, rest.n do s:add(rest[i]) end
    return s
  end
  function cls.noneOf(k) return cls.__new__() end
  function cls.allOf(k)
    local s = cls.__new__()
    local vs = C[k.k].__values
    for i = 1, vs.n do s:add(vs[i]) end
    return s
  end
  function cls.copyOf__Collection(c) local s = cls.__new__(); s:addAll(c); return s end
  cls.copyOf__EnumSet = cls.copyOf__Collection
  function cls.range(a, b)
    local s = cls.__new__()
    local vs = C[a.__key].__values
    for i = 1, vs.n do local v = vs[i]; if v.__ordinal >= a.__ordinal and v.__ordinal <= b.__ordinal then s:add(v) end end
    return s
  end
  function cls.complementOf(other)
    local s = cls.__new__()
    for _, any in J.iter(other) do
      local vs = C[any.__key].__values
      for i = 1, vs.n do if not other:contains(vs[i]) then s:add(vs[i]) end end
      break
    end
    return s
  end
end })

-- ------------------------------------------------------------------ factories

local function list_of(...)
  local l = C["java.util.ArrayList"].__new__()
  local args = table.pack(...)
  for i = 1, args.n do l:add(args[i]) end
  return l
end

-- List/Set/Map interfaces carry the static factories.
do
  local List = { "of__", "of__Object", "of__Object_Object", "of__Object_Object_Object", "of__Object_Object_Object_Object",
    "of__Object_Object_Object_Object_Object", "of__Object_Object_Object_Object_Object_Object",
    "of__Object_Object_Object_Object_Object_Object_Object", "of__Object_Object_Object_Object_Object_Object_Object_Object",
    "of__Object_Object_Object_Object_Object_Object_Object_Object_Object",
    "of__Object_Object_Object_Object_Object_Object_Object_Object_Object_Object" }
  local add_statics = J.extend
  add_statics("java.util.List", function(cls)
    for _, n in ipairs(List) do cls[n] = list_of end
    function cls.of__ObjectArr(a)
      local l = C["java.util.ArrayList"].__new__()
      for i = 1, a.n do l:add(a[i]) end
      return l
    end
    function cls.copyOf(c) return C["java.util.ArrayList"].__new__Collection(c) end
  end)
  add_statics("java.util.Set", function(cls)
    local function set_of(...)
      local s = C["java.util.LinkedHashSet"].__new__()
      local args = table.pack(...)
      for i = 1, args.n do s:add(args[i]) end
      return s
    end
    for _, n in ipairs(List) do cls[n] = set_of end
    function cls.of__ObjectArr(a)
      local s = C["java.util.LinkedHashSet"].__new__()
      for i = 1, a.n do s:add(a[i]) end
      return s
    end
    function cls.copyOf(c) return C["java.util.LinkedHashSet"].__new__Collection(c) end
  end)
  add_statics("java.util.Map", function(cls)
    local function map_of(...)
      local m = C["java.util.LinkedHashMap"].__new__()
      local args = table.pack(...)
      for i = 1, args.n, 2 do m:put(args[i], args[i + 1]) end
      return m
    end
    cls.of__ = map_of
    local name = "of_"
    for _ = 1, 10 do
      name = name .. "_Object_Object"
      cls[name:gsub("^of___", "of__")] = map_of
    end
    function cls.entry(k, v) return C["java.util.AbstractMap$SimpleEntry"].__new__Object_Object(k, v) end
    function cls.ofEntries(es)
      local m = C["java.util.LinkedHashMap"].__new__()
      for i = 1, es.n do m:put(es[i]:getKey(), es[i]:getValue()) end
      return m
    end
    function cls.copyOf(m) return C["java.util.LinkedHashMap"].__new__Map(m) end
    -- Map default methods for implementations that do not define them
    function cls.getOrDefault(self, k, d) if self:containsKey(k) then return self:get(k) end return d end
    function cls.forEach(self, f) for _, e in J.iter(self:entrySet()) do f:accept(e:getKey(), e:getValue()) end end
  end)
end

-- ------------------------------------------------------------------ Collections

def("java.util.Collections", nil, {}, function(cls)
  function cls.emptyList() return C["java.util.ArrayList"].__new__() end
  function cls.emptySet() return C["java.util.LinkedHashSet"].__new__() end
  function cls.emptyMap() return C["java.util.LinkedHashMap"].__new__() end
  function cls.singletonList(v) return list_of(v) end
  function cls.singleton(v) local s = C["java.util.LinkedHashSet"].__new__(); s:add(v); return s end
  function cls.singletonMap(k, v) local m = C["java.util.LinkedHashMap"].__new__(); m:put(k, v); return m end
  function cls.unmodifiableList(l) return l end
  function cls.unmodifiableSet(s) return s end
  function cls.unmodifiableMap(m) return m end
  function cls.unmodifiableCollection(c) return c end
  function cls.unmodifiableSortedSet(s) return s end
  function cls.synchronizedList(l) return l end
  function cls.synchronizedMap(m) return m end
  function cls.synchronizedSet(s) return s end
  function cls.addAll(c, a)
    local ch = false
    for i = 1, a.n do if c:add(a[i]) then ch = true end end
    return ch
  end
  function cls.reverse(l)
    local n = l:size()
    for i = 0, (n // 2) - 1 do
      local a, b = l:get(i), l:get(n - 1 - i)
      l:set(i, b); l:set(n - 1 - i, a)
    end
  end
  function cls.sort__List(l) l:sort(nil) end
  function cls.sort__List_Comparator(l, c) l:sort(c) end
  function cls.shuffle__List(l) end
  function cls.swap(l, i, j) local a = l:get(i); l:set(i, l:get(j)); l:set(j, a) end
  function cls.frequency(c, o)
    local n = 0
    for _, v in J.iter(c) do if v == o or equals(v, o) then n = n + 1 end end
    return n
  end
  function cls.nCopies(n, v)
    local l = C["java.util.ArrayList"].__new__()
    for _ = 1, n do l:add(v) end
    return l
  end
  local function extreme(c, cmp, sign)
    local best, first = nil, true
    for _, v in J.iter(c) do
      if first then best = v; first = false
      else
        local r = cmp and cmp:compare(v, best) or J.compare(v, best)
        if r * sign > 0 then best = v end
      end
    end
    if first then nse() end
    return best
  end
  function cls.max__Collection(c) return extreme(c, nil, 1) end
  function cls.max__Collection_Comparator(c, cmp) return extreme(c, cmp, 1) end
  function cls.min__Collection(c) return extreme(c, nil, -1) end
  function cls.min__Collection_Comparator(c, cmp) return extreme(c, cmp, -1) end
  function cls.reverseOrder__() return J.fn("java.util.Comparator", "compare", function(_, a, b) return J.compare(b, a) end) end
  function cls.reverseOrder__Comparator(c) return J.fn("java.util.Comparator", "compare", function(_, a, b) return c:compare(b, a) end) end
  function cls.disjoint(a, b)
    for _, v in J.iter(a) do if b:contains(v) then return false end end
    return true
  end
end)
J.def_clinit("java.util.Collections", function(cls)
  cls.EMPTY_LIST = cls.emptyList()
  cls.EMPTY_SET = cls.emptySet()
  cls.EMPTY_MAP = cls.emptyMap()
end)

-- ------------------------------------------------------------------ Arrays

def("java.util.Arrays", nil, {}, function(cls)
  function cls.asList(a)
    local l = C["java.util.ArrayList"].__new__()
    for i = 1, a.n do l:add(a[i]) end
    return l
  end
  local function copy_of(a, n)
    local t = { n = n }
    local dv = nil
    if a.n > 0 and type(a[1]) == "number" then dv = (math.type(a[1]) == "integer") and 0 or 0.0 end
    for i = 1, n do
      if i <= a.n then t[i] = a[i] else t[i] = dv end
    end
    return t
  end
  for _, s in ipairs({ "ObjectArr", "intArr", "longArr", "doubleArr", "charArr", "booleanArr", "byteArr", "shortArr", "floatArr" }) do
    cls["copyOf__" .. s .. "_int"] = copy_of
    cls["copyOfRange__" .. s .. "_int_int"] = function(a, from, to)
      local t = { n = to - from }
      for i = 1, to - from do t[i] = a[from + i] end
      return t
    end
    cls["equals__" .. s .. "_" .. s] = function(a, b)
      if rawequal(a, b) then return true end
      if a == nil or b == nil or a.n ~= b.n then return false end
      for i = 1, a.n do if not (a[i] == b[i] or equals(a[i], b[i])) then return false end end
      return true
    end
    cls["hashCode__" .. s] = function(a)
      if a == nil then return 0 end
      local h = 1
      for i = 1, a.n do h = J.l2i(31 * h + (a[i] == nil and 0 or hash(a[i]))) end
      return h
    end
    cls["toString__" .. s] = function(a)
      if a == nil then return "null" end
      local t = {}
      for i = 1, a.n do t[i] = J.str(a[i]) end
      return "[" .. table.concat(t, ", ") .. "]"
    end
    cls["fill__" .. s .. "_" .. (s == "ObjectArr" and "Object" or s:gsub("Arr$", ""))] = function(a, v)
      for i = 1, a.n do a[i] = v end
    end
    cls["sort__" .. s] = function(a) J.stable_sort(a, a.n, nil) end
    cls["stream__" .. s] = function(a)
      local t = J.aclone(a)
      if s == "intArr" or s == "longArr" or s == "doubleArr" then return C["java.util.stream.IntStream"].__from(t) end
      return C["java.util.stream.Stream"].__from(t)
    end
  end
  function cls.sort__ObjectArr_Comparator(a, c) J.stable_sort(a, a.n, c) end
  function cls.deepEquals(a, b) return cls.equals__ObjectArr_ObjectArr(a, b) end
end)

-- ------------------------------------------------------------------ Objects

def("java.util.Objects", nil, {}, function(cls)
  function cls.equals__Object_Object(a, b) return a == b or (a ~= nil and equals(a, b)) end
  cls.equals = cls.equals__Object_Object
  function cls.isNull(o) return o == nil end
  function cls.nonNull(o) return o ~= nil end
  function cls.requireNonNull__Object(o) if o == nil then J.npe() end return o end
  function cls.requireNonNull__Object_String(o, m) if o == nil then J.npe(m) end return o end
  function cls.requireNonNull__Object_Supplier(o, s) if o == nil then J.npe(s:get()) end return o end
  function cls.requireNonNullElse(o, d) if o == nil then return d end return o end
  function cls.hashCode__Object(o) if o == nil then return 0 end return hash(o) end
  function cls.hash(a)
    local h = 1
    for i = 1, a.n do h = J.l2i(31 * h + (a[i] == nil and 0 or hash(a[i]))) end
    return h
  end
  function cls.toString__Object(o) return J.str(o) end
  function cls.toString__Object_String(o, d) if o == nil then return d end return J.str(o) end
  function cls.compare(a, b, c) if a == b then return 0 end return c:compare(a, b) end
end)

-- ------------------------------------------------------------------ Optional

def("java.util.Optional", nil, {}, function(cls)
  local function mk(v) local o = setmetatable({}, cls); o.v = v; return o end
  local EMPTY
  function cls.empty() EMPTY = EMPTY or mk(nil); return EMPTY end
  function cls.of(v) if v == nil then J.npe() end return mk(v) end
  function cls.ofNullable(v) if v == nil then return cls.empty() end return mk(v) end
  function cls.isPresent(self) return self.v ~= nil end
  function cls.isEmpty(self) return self.v == nil end
  function cls.get(self) if self.v == nil then nse() end return self.v end
  cls.orElseThrow__ = cls.get
  function cls.orElseThrow__Supplier(self, s) if self.v == nil then J.throw(s:get()) end return self.v end
  function cls.orElse(self, d) if self.v == nil then return d end return self.v end
  function cls.orElseGet(self, s) if self.v == nil then return s:get() end return self.v end
  function cls.map(self, f) if self.v == nil then return self end return cls.ofNullable(f:apply(self.v)) end
  function cls.flatMap(self, f) if self.v == nil then return self end return f:apply(self.v) end
  function cls.filter(self, p) if self.v ~= nil and p:test(self.v) then return self end return cls.empty() end
  function cls.ifPresent(self, c) if self.v ~= nil then c:accept(self.v) end end
  function cls.ifPresentOrElse(self, c, r) if self.v ~= nil then c:accept(self.v) else r:run() end end
  function cls.or_(self, s) if self.v ~= nil then return self end return s:get() end
  function cls.stream(self)
    if self.v == nil then return C["java.util.stream.Stream"].__from({ n = 0 }) end
    return C["java.util.stream.Stream"].__from({ self.v, n = 1 })
  end
  function cls.equals(self, o) return type(o) == "table" and o.__key == cls.__key and (self.v == o.v or equals(self.v, o.v)) end
  function cls.hashCode(self) return self.v == nil and 0 or hash(self.v) end
  function cls.toString(self) if self.v == nil then return "Optional.empty" end return "Optional[" .. J.str(self.v) .. "]" end
end)
def("java.util.OptionalInt", nil, {}, function(cls)
  local function mk(v) local o = setmetatable({}, cls); o.v = v; return o end
  function cls.empty() return mk(nil) end
  function cls.of(v) return mk(v) end
  function cls.isPresent(self) return self.v ~= nil end
  function cls.isEmpty(self) return self.v == nil end
  function cls.getAsInt(self) if self.v == nil then nse() end return self.v end
  function cls.orElse(self, d) if self.v == nil then return d end return self.v end
  function cls.orElseGet(self, s) if self.v == nil then return s:getAsInt() end return self.v end
  function cls.ifPresent(self, c) if self.v ~= nil then c:accept(self.v) end end
end)
J.def("java.util.OptionalDouble", { kind = "class", super = "java.util.OptionalInt", ifaces = {}, body = function(cls)
  function cls.getAsDouble(self) return cls.getAsInt(self) end
end })

-- ------------------------------------------------------------------ Comparator & functional interfaces

local function comparator(f) return J.fn("java.util.Comparator", "compare", f) end
J.comparator = comparator

iface("java.util.Comparator", {}, function(cls)
  function cls.reversed(self) return comparator(function(_, a, b) return self:compare(b, a) end) end
  local function then_(self, other)
    return comparator(function(_, a, b)
      local r = self:compare(a, b)
      if r ~= 0 then return r end
      return other:compare(a, b)
    end)
  end
  cls.thenComparing__Comparator = then_
  function cls.thenComparing__Function(self, f) return then_(self, cls.comparing__Function(f)) end
  function cls.thenComparing__Function_Comparator(self, f, c) return then_(self, cls.comparing__Function_Comparator(f, c)) end
  function cls.thenComparingInt(self, f) return then_(self, cls.comparingInt(f)) end
  function cls.thenComparingDouble(self, f) return then_(self, cls.comparingDouble(f)) end
  function cls.thenComparingLong(self, f) return then_(self, cls.comparingLong(f)) end
  function cls.comparing__Function(f) return comparator(function(_, a, b) return J.compare(f:apply(a), f:apply(b)) end) end
  function cls.comparing__Function_Comparator(f, c) return comparator(function(_, a, b) return c:compare(f:apply(a), f:apply(b)) end) end
  function cls.comparingInt(f) return comparator(function(_, a, b) return J.compare(f:applyAsInt(a), f:applyAsInt(b)) end) end
  function cls.comparingDouble(f) return comparator(function(_, a, b) return J.compare(f:applyAsDouble(a), f:applyAsDouble(b)) end) end
  function cls.comparingLong(f) return comparator(function(_, a, b) return J.compare(f:applyAsLong(a), f:applyAsLong(b)) end) end
  function cls.naturalOrder() return comparator(function(_, a, b) return J.compare(a, b) end) end
  function cls.reverseOrder() return comparator(function(_, a, b) return J.compare(b, a) end) end
  function cls.nullsFirst(c) return comparator(function(_, a, b)
    if a == nil and b == nil then return 0 elseif a == nil then return -1 elseif b == nil then return 1 end
    return c:compare(a, b) end) end
  function cls.nullsLast(c) return comparator(function(_, a, b)
    if a == nil and b == nil then return 0 elseif a == nil then return 1 elseif b == nil then return -1 end
    return c:compare(a, b) end) end
end)

local F = "java.util.function."
iface(F .. "Function", {}, function(cls)
  function cls.andThen(self, g) return J.fn(F .. "Function", "apply", function(_, x) return g:apply(self:apply(x)) end) end
  function cls.compose(self, g) return J.fn(F .. "Function", "apply", function(_, x) return self:apply(g:apply(x)) end) end
  function cls.identity() return J.fn(F .. "Function", "apply", function(_, x) return x end) end
end)
iface(F .. "UnaryOperator", { F .. "Function" }, function(cls)
  function cls.identity() return J.fn(F .. "UnaryOperator", "apply", function(_, x) return x end) end
end)
iface(F .. "BiFunction", {}, function(cls)
  function cls.andThen(self, g) return J.fn(F .. "BiFunction", "apply", function(_, a, b) return g:apply(self:apply(a, b)) end) end
end)
iface(F .. "BinaryOperator", { F .. "BiFunction" }, function(cls)
  function cls.minBy(c) return J.fn(F .. "BinaryOperator", "apply", function(_, a, b) return c:compare(a, b) <= 0 and a or b end) end
  function cls.maxBy(c) return J.fn(F .. "BinaryOperator", "apply", function(_, a, b) return c:compare(a, b) >= 0 and a or b end) end
end)
iface(F .. "Consumer", {}, function(cls)
  function cls.andThen(self, g) return J.fn(F .. "Consumer", "accept", function(_, x) self:accept(x); g:accept(x) end) end
end)
iface(F .. "BiConsumer", {}, function(cls)
  function cls.andThen(self, g) return J.fn(F .. "BiConsumer", "accept", function(_, a, b) self:accept(a, b); g:accept(a, b) end) end
end)
iface(F .. "Supplier")
iface(F .. "BooleanSupplier")
iface(F .. "IntSupplier")
iface(F .. "DoubleSupplier")
iface(F .. "Predicate", {}, function(cls)
  function cls.and_(self, o) return J.fn(F .. "Predicate", "test", function(_, x) return self:test(x) and o:test(x) end) end
  function cls.or_(self, o) return J.fn(F .. "Predicate", "test", function(_, x) return self:test(x) or o:test(x) end) end
  function cls.negate(self) return J.fn(F .. "Predicate", "test", function(_, x) return not self:test(x) end) end
  function cls.not_(p) return J.fn(F .. "Predicate", "test", function(_, x) return not p:test(x) end) end
  function cls.isEqual(v) return J.fn(F .. "Predicate", "test", function(_, x) return x == v or equals(v, x) end) end
end)
iface(F .. "BiPredicate", {}, function(cls)
  function cls.negate(self) return J.fn(F .. "BiPredicate", "test", function(_, a, b) return not self:test(a, b) end) end
end)
iface(F .. "IntPredicate", {}, function(cls)
  function cls.negate(self) return J.fn(F .. "IntPredicate", "test", function(_, x) return not self:test(x) end) end
  function cls.and_(self, o) return J.fn(F .. "IntPredicate", "test", function(_, x) return self:test(x) and o:test(x) end) end
  function cls.or_(self, o) return J.fn(F .. "IntPredicate", "test", function(_, x) return self:test(x) or o:test(x) end) end
end)
for _, n in ipairs({ "IntFunction", "IntUnaryOperator", "IntConsumer", "IntBinaryOperator", "ToIntFunction",
  "ToIntBiFunction", "ToDoubleFunction", "ToLongFunction", "IntToDoubleFunction", "ObjIntConsumer", "DoubleUnaryOperator",
  "LongFunction", "DoubleFunction" }) do
  iface(F .. n)
end
iface("java.util.concurrent.Callable")

-- ------------------------------------------------------------------ misc

def("java.util.Locale", nil, {}, function(cls)
  function cls.__init__String(self, l) self.l = l end
  function cls.__init__String_String(self, l, c) self.l = l .. "_" .. c end
  function cls.toString(self) return self.l end
  function cls.getDefault() return cls.ROOT end
end)
J.def_clinit("java.util.Locale", function(cls)
  cls.ROOT = cls.__new__String("")
  cls.ENGLISH = cls.__new__String("en")
  cls.US = cls.__new__String_String("en", "US")
  cls.UK = cls.__new__String_String("en", "GB")
end)

iface("java.util.random.RandomGenerator", {}, function(cls)
  function cls.nextInt__(self) return math.floor(J.random() * 4294967296) - 2147483648 end
  function cls.nextInt__int(self, n) return math.floor(J.random() * n) end
  function cls.nextInt__int_int(self, a, b) return a + math.floor(J.random() * (b - a)) end
  function cls.nextDouble__(self) return J.random() end
  function cls.nextBoolean(self) return J.random() < 0.5 end
  function cls.nextFloat__(self) return J.random() end
  function cls.nextLong__(self) return math.floor(J.random() * 9007199254740992) end
end)
def("java.util.Random", nil, { "java.util.random.RandomGenerator", "java.io.Serializable" }, function(cls)
  function cls.__init__(self) end
  function cls.__init__long(self) end
end)
J.def("java.util.concurrent.ThreadLocalRandom", { kind = "class", super = "java.util.Random", ifaces = {}, body = function(cls)
  function cls.current() return cls.__new__() end
end })

local function atomic(key, getter)
  def(key, "java.lang.Number", { "java.io.Serializable" }, function(cls)
    function cls.__init__(self) self.v = getter end
    function cls.__init__int(self, v) self.v = v end
    function cls.__init__long(self, v) self.v = v end
    function cls.__init__boolean(self, v) self.v = v end
    function cls.__init__Object(self, v) self.v = v end
    function cls.get(self) return self.v end
    function cls.set(self, v) self.v = v end
    function cls.lazySet(self, v) self.v = v end
    function cls.intValue(self) return self.v end
    function cls.getAndSet(self, v) local o = self.v; self.v = v; return o end
    function cls.compareAndSet(self, e, v) if self.v == e then self.v = v; return true end return false end
    function cls.getAndIncrement(self) local o = self.v; self.v = o + 1; return o end
    function cls.getAndDecrement(self) local o = self.v; self.v = o - 1; return o end
    function cls.incrementAndGet(self) self.v = self.v + 1; return self.v end
    function cls.decrementAndGet(self) self.v = self.v - 1; return self.v end
    function cls.addAndGet(self, d) self.v = self.v + d; return self.v end
    function cls.getAndAdd(self, d) local o = self.v; self.v = o + d; return o end
    function cls.updateAndGet(self, f) self.v = f:apply(self.v); return self.v end
    function cls.toString(self) return J.str(self.v) end
  end)
end
atomic("java.util.concurrent.atomic.AtomicInteger", 0)
atomic("java.util.concurrent.atomic.AtomicLong", 0)
atomic("java.util.concurrent.atomic.AtomicBoolean", false)
atomic("java.util.concurrent.atomic.AtomicReference", nil)
