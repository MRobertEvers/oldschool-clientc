-- `gc2` is a GameCache2 light-userdata injected by the C host before this
-- script runs (see gamecache2_test_run_worldbuild_script in lua_gc2_test.c).

local request_buffer = GameCache2.IORequestBuffer.New()
GameCache2.Dat1.WorldBuild_BeginFetch(gc2, request_buffer)
-- Platform.LoadArchives yields to C, which loads the requested archives from
-- CacheDat and resumes the coroutine with the resulting archives userdata.
local archives = Platform.LoadArchives(request_buffer)
GameCache2.Dat1.WorldBuild_BeginLoad(gc2, archives)
