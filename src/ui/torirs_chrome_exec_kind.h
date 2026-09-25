#ifndef SRC_TORIRS_CHROME_EXEC_KIND_H
#define SRC_TORIRS_CHROME_EXEC_KIND_H

/*
 * Which executor a surface is bound to, and its spelling.
 *
 * Split out of torirs_chrome_exec.h because naming an executor is not the same
 * job as being one: a boot manifest, an env var and a CLI flag all have to turn
 * "browser" into a kind, and none of them has a chrome, a display list or a
 * platform. Keeping the enum and its two name functions in a unit that includes
 * nothing lets bootmanifest.c -- and the io_server and the manifest test that
 * link it -- resolve a name without linking the whole ui/ layer behind it.
 */

/**
 * BUFFER is internal fallback state, not a user-selectable executor. WEB and
 * BROWSER are the only external presenters: respectively the Emscripten page
 * DOM and the embedded local web engine used by desktop hosts.
 */
enum ToriRSChromeExecKind
{
    TORIRS_CHROME_EXEC_BUFFER = 0,
    /** Real DOM controls, driven from wasm through the page's channel. */
    TORIRS_CHROME_EXEC_WEB,
    /** Shared DOM bundle in one attached WebView2/MSHTML browser control. */
    TORIRS_CHROME_EXEC_BROWSER,
    TORIRS_CHROME_EXEC_COUNT
};

/** Diagnostic name for a kind, including internal BUFFER. Never NULL. */
char const*
ToriRSChromeExec_KindName(int kind);

/** @return enum ToriRSChromeExecKind, or -1 when `name` names none. */
int
ToriRSChromeExec_KindFromName(char const* name);

/**
 * Where plugin destinations are offered. Here for the same reason the executor
 * kinds are: a boot manifest and an env var name it.
 */
enum ToriRSPluginNavMode
{
    /** The lane's own pop-out column when its profile names one and it is on
     *  screen; the separate rail otherwise. @see ui/torirs_chrome_popout_nav.h */
    TORIRS_PLUGIN_NAV_AUTO = 0,
    /** Always the separate rail. */
    TORIRS_PLUGIN_NAV_RAIL,
};

/** `auto` or `rail`; -1 for anything else. */
int
ToriRSPluginNav_ModeFromName(char const* name);

/** Never NULL. */
char const*
ToriRSPluginNav_ModeName(int mode);

#endif
