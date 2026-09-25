/*
 * Lua 5.5 as one translation unit.
 *
 * The tree's answer to "many third-party .c files, one object" -- the same
 * shape as rscache_unity.c and toridraw_unity.c -- compiled with its own
 * CFLAGS and -w, because upstream is not written to this project's warning
 * settings and should not be edited to satisfy them.
 *
 * What is deliberately NOT here is the point of the file:
 *
 *   lua.c, luac.c   -- each defines main().
 *   loadlib.c       -- dlopen. There is no dynamic loading anywhere in this
 *                      client, and the web lane has no such thing at all.
 *   liolib.c        -- fopen. File access in this tree goes through the IO
 *                      queue (PlatformX_IO); a script that could open a file
 *                      directly would break both that rule and the browser.
 *   loslib.c        -- system(), getenv(), clock, tmpfile.
 *   ldblib.c        -- the debug library reaches around every sandbox above.
 *   lcorolib.c      -- coroutines are not exposed: a handler must return
 *                      within the frame it was called on, and a yielded
 *                      handler is one that never does.
 *   linit.c         -- luaL_openlibs would pull all of the above back in.
 *                      torirs_plugin_lua.c opens its own library set instead.
 *
 * Leaving them out of the BUILD rather than disabling them at runtime is what
 * makes the sandbox structural: there is no symbol to reach around to.
 */

#include "lapi.c"
#include "lauxlib.c"
#include "lbaselib.c"
#include "lcode.c"
#include "lctype.c"
#include "ldebug.c"
#include "ldo.c"
#include "ldump.c"
#include "lfunc.c"
#include "lgc.c"
#include "llex.c"
#include "lmathlib.c"
#include "lmem.c"
#include "lobject.c"
#include "lopcodes.c"
#include "lparser.c"
#include "lstate.c"
#include "lstring.c"
#include "lstrlib.c"
#include "ltable.c"
#include "ltablib.c"
#include "ltm.c"
#include "lundump.c"
#include "lutf8lib.c"
#include "lvm.c"
#include "lzio.c"
