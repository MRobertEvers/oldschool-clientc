-- java.util.stream for the Quest Helper port. Streams are eager over a Lua
-- array ({n = count, ...}): every stream the plugin builds is finite and
-- small, and eager evaluation keeps each operation one plain loop.

local J, C = J, C

local function def(key, super, ifaces, body)
  J.def(key, { kind = "class", super = super or "java.lang.Object", ifaces = ifaces or {}, body = body })
end

local function stream_body(key)
  return function(cls)
    local function mk(t) local s = setmetatable({}, cls); s.t = t; return s end
    cls.__from = mk
    local function map_to(target, t, f, sam)
      local out = { n = t.n }
      for i = 1, t.n do out[i] = f[sam](f, t[i]) end
      return C[target].__from(out)
    end
    function cls.filter(self, p)
      local t, out, n = self.t, {}, 0
      for i = 1, t.n do local v = t[i]; if p:test(v) then n = n + 1; out[n] = v end end
      out.n = n
      return mk(out)
    end
    function cls.map(self, f)
      local sam = (key == "java.util.stream.IntStream") and "applyAsInt" or "apply"
      if f[sam] == nil then sam = "apply" end
      local t, out = self.t, { n = self.t.n }
      for i = 1, t.n do out[i] = f[sam](f, t[i]) end
      return mk(out)
    end
    function cls.mapToInt(self, f) return map_to("java.util.stream.IntStream", self.t, f, "applyAsInt") end
    function cls.mapToLong(self, f) return map_to("java.util.stream.IntStream", self.t, f, "applyAsLong") end
    function cls.mapToDouble(self, f) return map_to("java.util.stream.IntStream", self.t, f, "applyAsDouble") end
    function cls.mapToObj(self, f) return map_to("java.util.stream.Stream", self.t, f, "apply") end
    function cls.boxed(self) return C["java.util.stream.Stream"].__from(self.t) end
    function cls.asDoubleStream(self) return self end
    function cls.flatMap(self, f)
      local t, out, n = self.t, {}, 0
      for i = 1, t.n do
        local s = f:apply(t[i])
        if s ~= nil then
          local st = s.t
          for j = 1, st.n do n = n + 1; out[n] = st[j] end
        end
      end
      out.n = n
      return mk(out)
    end
    function cls.distinct(self)
      local seen = C["java.util.HashSet"].__new__()
      local t, out, n = self.t, {}, 0
      for i = 1, t.n do local v = t[i]; if seen:add(v) then n = n + 1; out[n] = v end end
      out.n = n
      return mk(out)
    end
    local function sorted(self, c)
      local t = J.aclone(self.t)
      J.stable_sort(t, t.n, c)
      return mk(t)
    end
    cls.sorted__ = function(self) return sorted(self, nil) end
    cls.sorted__Comparator = sorted
    cls.sorted = cls.sorted__
    function cls.limit(self, k)
      local t, out = self.t, { n = math.min(k, self.t.n) }
      for i = 1, out.n do out[i] = t[i] end
      return mk(out)
    end
    function cls.skip(self, k)
      local t, out, n = self.t, {}, 0
      for i = k + 1, t.n do n = n + 1; out[n] = t[i] end
      out.n = n
      return mk(out)
    end
    function cls.peek(self, c) for i = 1, self.t.n do c:accept(self.t[i]) end return self end
    function cls.forEach(self, c) for i = 1, self.t.n do c:accept(self.t[i]) end end
    cls.forEachOrdered = cls.forEach
    function cls.anyMatch(self, p) for i = 1, self.t.n do if p:test(self.t[i]) then return true end end return false end
    function cls.allMatch(self, p) for i = 1, self.t.n do if not p:test(self.t[i]) then return false end end return true end
    function cls.noneMatch(self, p) for i = 1, self.t.n do if p:test(self.t[i]) then return false end end return true end
    local function first(self)
      if key == "java.util.stream.IntStream" then
        if self.t.n == 0 then return C["java.util.OptionalInt"].empty() end
        return C["java.util.OptionalInt"].of(self.t[1])
      end
      if self.t.n == 0 then return C["java.util.Optional"].empty() end
      return C["java.util.Optional"].ofNullable(self.t[1])
    end
    cls.findFirst = first
    cls.findAny = first
    function cls.count(self) return self.t.n end
    function cls.sum(self)
      local s = 0
      for i = 1, self.t.n do s = s + self.t[i] end
      return s
    end
    function cls.average(self)
      if self.t.n == 0 then return C["java.util.OptionalDouble"].empty() end
      return C["java.util.OptionalDouble"].of(cls.sum(self) / self.t.n)
    end
    local function extreme(self, c, sign)
      local t = self.t
      if t.n == 0 then
        if key == "java.util.stream.IntStream" then return C["java.util.OptionalInt"].empty() end
        return C["java.util.Optional"].empty()
      end
      local best = t[1]
      for i = 2, t.n do
        local r = c and c:compare(t[i], best) or J.compare(t[i], best)
        if r * sign > 0 then best = t[i] end
      end
      if key == "java.util.stream.IntStream" then return C["java.util.OptionalInt"].of(best) end
      return C["java.util.Optional"].ofNullable(best)
    end
    function cls.max(self, c) return extreme(self, c, 1) end
    function cls.min(self, c) return extreme(self, c, -1) end
    cls.max__Comparator = cls.max
    cls.min__Comparator = cls.min
    cls.max__ = function(self) return extreme(self, nil, 1) end
    cls.min__ = function(self) return extreme(self, nil, -1) end
    function cls.reduce__Object_BinaryOperator(self, init, op)
      local acc = init
      for i = 1, self.t.n do acc = op:apply(acc, self.t[i]) end
      return acc
    end
    cls.reduce__int_IntBinaryOperator = function(self, init, op)
      local acc = init
      for i = 1, self.t.n do acc = op:applyAsInt(acc, self.t[i]) end
      return acc
    end
    function cls.reduce__BinaryOperator(self, op)
      local t = self.t
      if t.n == 0 then return C["java.util.Optional"].empty() end
      local acc = t[1]
      for i = 2, t.n do acc = op:apply(acc, t[i]) end
      return C["java.util.Optional"].ofNullable(acc)
    end
    function cls.toArray__(self) return J.aclone(self.t) end
    cls.toArray__IntFunction = cls.toArray__
    function cls.toList(self)
      local l = C["java.util.ArrayList"].__new__()
      for i = 1, self.t.n do l:add(self.t[i]) end
      return l
    end
    function cls.collect__Collector(self, col) return col:__collect(self.t) end
    cls.collect = cls.collect__Collector
    function cls.iterator(self) return J.array_iterator(self.t, self.t.n, nil) end
    function cls.__iter(self)
      local i, t = 0, self.t
      return function() i = i + 1; if i <= t.n then return i, t[i] end end
    end
    function cls.parallel(self) return self end
    function cls.sequential(self) return self end
    function cls.unordered(self) return self end
    function cls.onClose(self) return self end
    function cls.close(self) end
    -- statics
    function cls.of__ObjectArr(a) return mk(J.aclone(a)) end
    function cls.of__Object(v) return mk({ v, n = 1 }) end
    function cls.of__intArr(a) return mk(J.aclone(a)) end
    function cls.of__int(v) return mk({ v, n = 1 }) end
    function cls.empty() return mk({ n = 0 }) end
    function cls.concat(a, b)
      local out, n = {}, 0
      for i = 1, a.t.n do n = n + 1; out[n] = a.t[i] end
      for i = 1, b.t.n do n = n + 1; out[n] = b.t[i] end
      out.n = n
      return mk(out)
    end
    function cls.range(a, b)
      local out, n = {}, 0
      for i = a, b - 1 do n = n + 1; out[n] = i end
      out.n = n
      return mk(out)
    end
    function cls.rangeClosed(a, b) return cls.range(a, b + 1) end
    function cls.generate() J.throw(C["java.lang.UnsupportedOperationException"].__new__String("infinite stream")) end
  end
end

J.def("java.util.stream.BaseStream", { kind = "interface", super = "java.lang.Object", ifaces = { "java.lang.AutoCloseable" } })
def("java.util.stream.Stream", nil, { "java.util.stream.BaseStream" }, stream_body("java.util.stream.Stream"))
def("java.util.stream.IntStream", nil, { "java.util.stream.BaseStream" }, stream_body("java.util.stream.IntStream"))
J.def("java.util.stream.LongStream", { kind = "class", super = "java.util.stream.IntStream", ifaces = {} })
J.def("java.util.stream.DoubleStream", { kind = "class", super = "java.util.stream.IntStream", ifaces = {} })
J.def("java.util.stream.StreamSupport", { kind = "class", super = "java.lang.Object", ifaces = {}, body = function(cls)
  function cls.stream(spliterator) return C["java.util.stream.Stream"].__from(spliterator) end
end })

-- Collector objects: __collect(t) turns the stream's array into the result.
def("java.util.stream.Collector", nil, {}, function(cls)
  function cls.__make(f) local o = setmetatable({}, cls); o.__collect = function(_, t) return f(t) end; return o end
end)

def("java.util.stream.Collectors", nil, {}, function(cls)
  local function col(f) return C["java.util.stream.Collector"].__make(f) end
  local function into(coll, t) for i = 1, t.n do coll:add(t[i]) end return coll end
  function cls.toList() return col(function(t) return into(C["java.util.ArrayList"].__new__(), t) end) end
  cls.toUnmodifiableList = cls.toList
  function cls.toSet() return col(function(t) return into(C["java.util.HashSet"].__new__(), t) end) end
  cls.toUnmodifiableSet = cls.toSet
  function cls.toCollection(supplier) return col(function(t) return into(supplier:get(), t) end) end
  local function to_map(kf, vf, merge, supplier)
    return col(function(t)
      local m = supplier and supplier:get() or C["java.util.HashMap"].__new__()
      for i = 1, t.n do
        local k, v = kf:apply(t[i]), vf:apply(t[i])
        if m:containsKey(k) then
          if not merge then J.throw(C["java.lang.IllegalStateException"].__new__String("Duplicate key " .. J.str(k))) end
          m:put(k, merge:apply(m:get(k), v))
        else
          m:put(k, v)
        end
      end
      return m
    end)
  end
  function cls.toMap__Function_Function(kf, vf) return to_map(kf, vf, nil, nil) end
  function cls.toMap__Function_Function_BinaryOperator(kf, vf, m) return to_map(kf, vf, m, nil) end
  function cls.toMap__Function_Function_BinaryOperator_Supplier(kf, vf, m, s) return to_map(kf, vf, m, s) end
  local function joining(sep, pre, post)
    return col(function(t)
      local parts = {}
      for i = 1, t.n do parts[i] = J.str(t[i]) end
      return (pre or "") .. table.concat(parts, sep or "") .. (post or "")
    end)
  end
  function cls.joining__() return joining("") end
  function cls.joining__CharSequence(sep) return joining(J.str(sep)) end
  function cls.joining__CharSequence_CharSequence_CharSequence(sep, pre, post) return joining(J.str(sep), J.str(pre), J.str(post)) end
  local function grouping(kf, supplier, down)
    return col(function(t)
      local m = supplier and supplier:get() or C["java.util.HashMap"].__new__()
      local buckets = {}
      local order = {}
      for i = 1, t.n do
        local k = kf:apply(t[i])
        local l = m:get(k)
        if l == nil then
          l = { n = 0 }
          m:put(k, l)
          order[#order + 1] = k
        end
        l.n = l.n + 1
        l[l.n] = t[i]
      end
      for _, k in ipairs(order) do
        local arr = m:get(k)
        if down then m:put(k, down:__collect(arr)) else m:put(k, into(C["java.util.ArrayList"].__new__(), arr)) end
      end
      return m
    end)
  end
  function cls.groupingBy__Function(kf) return grouping(kf, nil, nil) end
  function cls.groupingBy__Function_Collector(kf, d) return grouping(kf, nil, d) end
  function cls.groupingBy__Function_Supplier_Collector(kf, s, d) return grouping(kf, s, d) end
  function cls.counting() return col(function(t) return t.n end) end
  function cls.summingInt(f) return col(function(t) local s = 0; for i = 1, t.n do s = s + f:applyAsInt(t[i]) end return s end) end
  function cls.mapping(f, d)
    return col(function(t)
      local out = { n = t.n }
      for i = 1, t.n do out[i] = f:apply(t[i]) end
      return d:__collect(out)
    end)
  end
  function cls.partitioningBy__Predicate(p)
    return col(function(t)
      local m = C["java.util.LinkedHashMap"].__new__()
      local yes, no = C["java.util.ArrayList"].__new__(), C["java.util.ArrayList"].__new__()
      for i = 1, t.n do if p:test(t[i]) then yes:add(t[i]) else no:add(t[i]) end end
      m:put(false, no)
      m:put(true, yes)
      return m
    end)
  end
end)
