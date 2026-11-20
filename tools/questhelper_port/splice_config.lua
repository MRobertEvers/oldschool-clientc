-- Splice gen_config_decl.lua's items into script/plugins/quest_helper.lua
-- between its "BEGIN GENERATED CONFIG" / "END GENERATED CONFIG" markers.
-- build.sh runs it after regenerating gen/, so the definition's config list
-- is always the one rl_core's ConfigManager decides on.
--
--   /opt/homebrew/bin/lua tools/questhelper_port/splice_config.lua <decl.lua>
local decl_path = assert(arg[1], "usage: splice_config.lua <decl.lua>")
local HERE = debug.getinfo(1, "S").source:sub(2):match("^(.*)/") or "."
local entry_path = HERE .. "/../../script/plugins/quest_helper.lua"

local f = assert(io.open(decl_path))
local decl = f:read("a")
f:close()
local items = {}
for line in decl:gmatch("[^\n]+") do
  if line:match("^%s*{ key = ") then items[#items + 1] = "        " .. line:gsub("^%s+", "") end
end
assert(#items > 0, "no config items in " .. decl_path)

f = assert(io.open(entry_path))
local src = f:read("a")
f:close()
local b = src:find("%-%- BEGIN GENERATED CONFIG[^\n]*\n")
local _, be = src:find("%-%- BEGIN GENERATED CONFIG[^\n]*\n")
local e = src:find("[ \t]*%-%- END GENERATED CONFIG")
assert(b and e and e > be, "markers not found in " .. entry_path)
local out = src:sub(1, be) .. table.concat(items, "\n") .. "\n" .. src:sub(e)
if out ~= src then
  f = assert(io.open(entry_path, "w"))
  f:write(out)
  f:close()
end
io.stderr:write(string.format("splice_config: %d items\n", #items))
