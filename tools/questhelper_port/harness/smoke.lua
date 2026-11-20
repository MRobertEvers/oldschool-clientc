local RT = arg[1]
J = { protect = pcall }
for _, f in ipairs({"j_core","j_lang","j_util","j_stream","j_regex","j_misc"}) do dofile(RT .. "/" .. f .. ".lua") end
local function check(name, got, want) if got ~= want then print("FAIL", name, tostring(got), tostring(want)) else io.write(".") end end
-- strings
check("trim", ("  hi \t"):trim(), "hi")
check("split", C["java.util.Arrays"].toString__ObjectArr(("a,b,,c,,"):split__String(",")), "[a, b, , c]")
check("splitre", C["java.util.Arrays"].toString__ObjectArr(("x emblem y of the flag z"):split__String(" (emblem|of the flag) ")), "[x, y, z]")
check("fmt", J.format("%s=%d %.2f %,d", J.arr({"a", 5, 3.14159, 1234567}, 4)), "a=5 3.14 1,234,567")
check("dstr", J.dstr(5.0) .. " " .. J.dstr(0.1) .. " " .. J.dstr(1e10), "5.0 0.1 1.0E10")
check("idiv", J.idiv(-7, 2), -3)
check("hash", ("hello"):hashCode(), 99162322)
-- regex
local P = C["java.util.regex.Pattern"]
local m = P.compile__String("^You add ([0-9]) gallons* of coolant to your ([0-9])-gallon jug.(?: It now contains ([0-9]) gallons* of coolant[.])*"):matcher("You add 3 gallons of coolant to your 5-gallon jug. It now contains 3 gallons of coolant.")
check("find", m:find__(), true); check("g1", m:group__int(1), "3"); check("g2", m:group__int(2), "5"); check("g3", m:group__int(3), "3")
local q = P.compile__String("(?:^|)'([^']*?)'(?:\\s|$)"):matcher("say 'hello there' now")
check("lazy", q:find__() and q:group__int(1), "hello there")
check("icase", P.compile__String_int("(?i)(a\\b)|(the\\b)|(an\\b)", 2):matcher("THE dog"):find__(), true)
check("replaceAll", ("a[b]c"):replaceAll("\\[|\\]", ""), "abc")
check("replace2", ("Hi there!"):replaceAll("[^a-zA-Z0-9_-]+", "_"), "Hi_there_")
-- collections: Java HashMap order for small ints and strings
local hm = C["java.util.HashMap"].__new__()
for _, k in ipairs({7, 3, 20, 1, 16}) do hm:put(k, k * 10) end
local ks = {}; for _, k in J.iter(hm:keySet()) do ks[#ks+1] = k end
check("hmorder", table.concat(ks, ","), "16,1,3,20,7")
local hs = C["java.util.HashSet"].__new__()
for _, k in ipairs({"banana","apple","cherry"}) do hs:add(k) end
local ss = {}; for _, k in J.iter(hs) do ss[#ss+1] = k end
check("hsorder", table.concat(ss, ","), "banana,apple,cherry")
local l = C["java.util.List"].of__Object_Object_Object(3, 1, 2)
local sorted = l:stream():sorted__():map(J.fn("java.util.function.Function","apply",function(_,x) return x*2 end)):collect__Collector(C["java.util.stream.Collectors"].toList())
check("stream", sorted:toString(), "[2, 4, 6]")
check("joining", l:stream():map(J.fn("java.util.function.Function","apply",function(_,x) return J.str(x) end)):collect__Collector(C["java.util.stream.Collectors"].joining__CharSequence("-")), "3-1-2")
-- classes, enums, try
J.def("t.Base", {kind="class", super="java.lang.Object", ifaces={}, body=function(cls, super)
  function cls.__init__int(self, v) super.__init(self); cls.__fields(self); self.v = v end
  function cls.__fields(self) self.list = C["java.util.ArrayList"].__new__() end
  function cls.get(self) return self.v end
end})
J.def("t.Sub", {kind="class", super="t.Base", ifaces={"java.lang.Runnable"}, body=function(cls, super)
  function cls.__init(self) super.__init__int(self, 41); cls.__fields(self) end
  function cls.__fields(self) end
  function cls.get(self) return super.get(self) + 1 end
end})
local s = C["t.Sub"].__new()
check("inherit", s:get(), 42); check("inst", J.inst(s, "java.lang.Runnable"), true); check("inst2", J.inst(s, "t.Base"), true)
J.def("t.E", {kind="enum", super="java.lang.Enum", ifaces={}, body=function(cls) function cls.__init(self) end end,
  clinit=function(cls) cls.A = J.enum_new(cls, "A", 0, "__init"); cls.B = J.enum_new(cls, "B", 1, "__init"); cls.__values = J.arr({cls.A, cls.B}, 2) end})
check("enum", C["t.E"].values()[2]:name() .. C["t.E"].valueOf__String("A"):ordinal(), "B0")
local st, rv = J.try(function() J.throw(C["java.lang.IllegalStateException"].__new__String("boom")) end,
  function(e) if J.catches(e, "java.lang.RuntimeException") then return 1, J.exception(e):getMessage() end J.rethrow(e) end)
check("try", rv, "boom")
local okk, st2, rv2 = pcall(J.try, function() local x = nil; return 1, x.y end,
  function(e) if J.catches(e, "java.lang.NullPointerException") then return 1, "npe" end J.rethrow(e) end)
if not okk then print("ERR", J.str(st2), type(st2) == "table" and J.str(st2.pending) or "") end
check("trynpe", rv2, "npe")
print("\nsmoke done")
local js = C["com.google.gson.Gson"].__new__()
local m = js:fromJson__String_Type('{"a":true,"b":false}', nil)
check("json", m:get("b") == false and m:get("a") == true, true)
check("jsonarr", js:toJson__Object(js:fromJson__String_Class("[1,2,3]", nil)), "[1,2,3]")
local il = C["com.google.common.collect.ImmutableList"].of__Object_Object(1, 2)
check("guava", il:size(), 2)
print("\nsmoke2 done")
