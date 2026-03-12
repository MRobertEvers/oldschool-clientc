local platform = require("platform")

local archive = platform.cache_read(0, 1)

print("Done")
print(archive)