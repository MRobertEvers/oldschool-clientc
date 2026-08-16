const path = require("path");
const addon = require(path.join(__dirname, "build/Release/cachelib_js.node"));

// Export constants
const CACHE_MODE_DAT1 = 0;
const CACHE_MODE_DAT2 = 1;

module.exports = {
  CacheLib: addon.CacheLib,
  CACHE_MODE_DAT1,
  CACHE_MODE_DAT2,
};
