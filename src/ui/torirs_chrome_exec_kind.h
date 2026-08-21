#ifndef SRC_TORIRS_CHROME_EXEC_KIND_H
#define SRC_TORIRS_CHROME_EXEC_KIND_H

/*
 * Which executor a surface is bound to, and its spelling.
 *
 * Split out of torirs_chrome_exec.h because naming an executor is not the same
 * job as being one: a boot manifest, an env var and a CLI flag all have to turn
 * "sdl" into a kind, and none of them has a chrome, a display list or a
 * platform. Keeping the enum and its two name functions in a unit that includes
 * nothing lets bootmanifest.c -- and the io_server and the manifest test that
 * link it -- resolve a name without linking the whole ui/ layer behind it.
 */

/**
 * Ordered so BUFFER is 0: it is the default, the fallback, and what a zeroed
 * config means, and all three of those should be the same value rather than
 * three places that have to agree on a name.
 */
enum ToriRSChromeExecKind
{
    TORIRS_CHROME_EXEC_BUFFER = 0,
    /** A second OS window, chrome prims blitted into its own surface. */
    TORIRS_CHROME_EXEC_SDL,
    /** Real DOM controls, driven from wasm through the page's channel. */
    TORIRS_CHROME_EXEC_WEB,
    /** An owned Win32 tool window of common controls. */
    TORIRS_CHROME_EXEC_GDI,
    /** A game-native interface behind a sidebar button. */
    TORIRS_CHROME_EXEC_CS2,
    TORIRS_CHROME_EXEC_COUNT
};

/** The name a config or an env var spells, e.g. "buffer". Never NULL. */
char const*
ToriRSChromeExec_KindName(int kind);

/** @return enum ToriRSChromeExecKind, or -1 when `name` names none. */
int
ToriRSChromeExec_KindFromName(char const* name);

#endif
