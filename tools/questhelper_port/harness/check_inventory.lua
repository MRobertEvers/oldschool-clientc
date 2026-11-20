-- Which external members of gen/inventory.tsv the runtime does not provide.
--
--   /opt/homebrew/bin/lua tools/questhelper_port/harness/check_inventory.lua
--
-- Boots the plugin ENTRY against harness/mock_api.lua (the real rt/ and ui/,
-- every framework module), realises every class a module defined, then looks
-- up each non-com.questhelper row of the inventory:
--
--   MISSING     the owner class exists and neither it nor any implementor has the name
--   NOCLASS     the owner class is not defined at all
--   impl-only   an interface method only its implementing classes carry (normal for
--               Map/List/Shape/...: the generated code calls it on an instance)
--   field?      an instance field (set by a constructor; not checkable statically)
--
-- Functional-interface SAM methods (Runnable.run, Consumer.accept, ...) are
-- provided per lambda by J.fn and are listed as impl-only or MISSING on the
-- interface; that is expected. Exit status 1 when any MISSING or NOCLASS row
-- is not one of those.

local print, dofile, pcall, io, debug, os = print, dofile, pcall, io, debug, os
local HERE = debug.getinfo(1, "S").source:sub(2):match("^(.*)/") or "."
local PLUGINS = HERE .. "/../../../script/plugins"
local ROOT = PLUGINS .. "/quest_helper"
dofile(HERE .. "/mock_api.lua")
local api = MOCK_API
api.core.log = function() end
MOCK.screen = 30
MOCK.player = { name = "Tester", true_x = 3222, true_z = 3218, level = 0, server_pid = 1, element_id = 700,
  combat_level = 3, dest_x = -1, dest_z = -1, flag_x = -1, flag_z = -1, fine_x = 64, fine_z = 64 }

local FRAME = 0
local requested, results = {}, {}
api.sandbox.protect = pcall
api.sandbox.module_request = function(name)
  local r = requested[name]
  if r == nil then
    local f = io.open(ROOT .. "/" .. name:gsub("%.", "/") .. ".lua")
    if not f then requested[name] = "missing"; return "missing" end
    f:close(); requested[name] = FRAME; return "pending"
  end
  if r == "missing" then return "missing" end
  return FRAME > r and "ready" or "pending"
end
-- every key a module J.defs, to realise them all afterwards
local DEFKEYS = {}
local hooked = false
api.sandbox.module_load = function(name)
  if results[name] ~= nil then return results[name] end
  if J and J.def and not hooked then
    hooked = true
    local od = J.def
    J.def = function(k, d) DEFKEYS[k] = true; return od(k, d) end
  end
  local v = dofile(ROOT .. "/" .. name:gsub("%.", "/") .. ".lua")
  if v == nil then v = true end
  results[name] = v
  return v
end

local plugin = dofile(PLUGINS .. "/quest_helper.lua")
MOCK_DECLARE_CONFIG(plugin.config)
plugin.on_start(api)
for _ = 1, 400 do
  FRAME = FRAME + 1; MOCK.now_ms = MOCK.now_ms + 20
  plugin.on_frame_start(api, { now_ms = MOCK.now_ms, drawn_frames = FRAME })
  if plugin._boot.phase == "running" or plugin._boot.phase == "failed" then break end
end
if plugin._boot.phase ~= "running" then
  print("check_inventory: the entry did not boot (" .. plugin._boot.phase .. ")")
  os.exit(1)
end
for k in pairs(DEFKEYS) do pcall(function() return C[k] end) end

local strmt = getmetatable("")
local function strhas(n)
  local ix = strmt.__index
  if type(ix) == "table" then return ix[n] ~= nil end
  if type(ix) == "function" then local ok, v = pcall(ix, "", n); return ok and v ~= nil end
  return false
end
local function implementors(owner, name)
  local found = {}
  for k, cls in pairs(C) do
    if type(cls) == "table" and cls.__supers and cls.__supers[owner] and cls[name] ~= nil then found[#found + 1] = k end
  end
  return found
end
local SAM = { run = true, accept = true, apply = true, get = true, test = true, call = true, compare = true,
  applyAsInt = true, applyAsLong = true, applyAsDouble = true, getAsBoolean = true, getAsInt = true, draw = true }
-- Rows served somewhere a static lookup cannot see.
local EXPECTED = {
  -- Fonts' clinit asks JGenerator.makeJTextArea() for a font: ui/panel.lua's
  -- JGenerator$TextArea answers getFont without being a java.awt.MenuContainer.
  ["java.awt.MenuContainer getFont"] = true,
}

local rows, bad, notes = 0, {}, {}
for line in io.lines(ROOT .. "/gen/inventory.tsv") do
  if line:sub(1, 1) ~= "#" then
    local kind, owner, lua, java, uses = line:match("^([^\t]*)\t([^\t]*)\t([^\t]*)\t([^\t]*)\t([^\t]*)")
    if kind and (not owner:match("^com%.questhelper%.") or kind:match("^manual")) then
      rows = rows + 1
      local ok, cls = pcall(function() return C[owner] end)
      local status
      if not ok or cls == nil then
        if owner == "java.lang.String" or owner == "java.lang.CharSequence" then
          status = (kind == "class" or strhas(lua)) and "ok" or "MISSING"
        elseif kind == "class" and (owner:match("^[a-z]+$") or owner == "Array") then status = "ok"
        else status = "NOCLASS" end
      elseif kind == "class" then status = "ok"
      elseif kind == "field" then status = "field?"
      elseif cls[lua] ~= nil then status = "ok"
      elseif (owner == "java.lang.String" or owner == "java.lang.CharSequence") and strhas(lua) then status = "ok"
      else
        local f = implementors(owner, lua)
        if #f > 0 then status = "impl-only"
        elseif SAM[lua] then status = "lambda"
        elseif EXPECTED[owner .. " " .. lua] then status = "expected"
        else status = "MISSING" end
      end
      local row = string.format("%s\t%s\t%s\t%s\t%s\t%s", status, kind, owner, lua, java, uses)
      if status == "MISSING" or status == "NOCLASS" then bad[#bad + 1] = row
      elseif status ~= "ok" then notes[#notes + 1] = row end
    end
  end
end
if os.getenv("QH_INV_VERBOSE") then for _, r in ipairs(notes) do print(r) end end
for _, r in ipairs(bad) do print(r) end
print(string.format("check_inventory: %d external rows, %d missing, %d impl-only/lambda/field/expected (QH_INV_VERBOSE=1 lists them)",
  rows, #bad, #notes))
os.exit(#bad == 0 and 0 or 1)
