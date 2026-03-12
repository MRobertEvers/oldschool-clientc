-- core.lua
--
-- Application logic.  Completely platform-agnostic.
-- Reads a file and logs its content on every tick.

local platform = require("platform")

local INSTANCE    = _G.INSTANCE_ID or "unknown"
local INTERVAL_MS = 3000
local counter     = 0

while true do
    counter = counter + 1
    local content = platform.read_file("data.txt")
    platform.log(INSTANCE, string.format("tick #%d  |  %s", counter, content))
    platform.sleep(INTERVAL_MS)
end
