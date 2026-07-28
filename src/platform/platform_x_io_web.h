#ifndef SRC_PLATFORM_PLATFORM_X_IO_WEB_H
#define SRC_PLATFORM_PLATFORM_X_IO_WEB_H

/*
 * The web IO backend's one host-facing call. Everything else it exposes is
 * exported to JavaScript (torirs_io_*) rather than called from C.
 */

/**
 * Give the page's IO harness a turn.
 *
 * Called once at the top of every frame. The requests a task queued last frame
 * are sitting in wasm memory doing nothing until someone carries them to the
 * server, and the frame loop is the only thing guaranteed to run — so it is
 * what does the carrying. The harness copies out whatever is waiting, sends it,
 * and calls back in when the answer lands.
 *
 * Safe and cheap when nothing is queued (one JS call that returns immediately),
 * and safe before the harness exists — it is a no-op until the page installs
 * Module.torirsIO.
 */
void
PlatformXIO_Web_Pump(void);

#endif
