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

/**
 * How many reads the client is currently waiting on.
 *
 * The host uses this to pace the frame loop. A frame can only consume the IO
 * that has arrived, so while reads are outstanding the loop wants to run at
 * event-loop rate, not display rate — the game's logic ticks are driven by the
 * wall clock and so do not speed up with it, which makes a faster loop pure
 * drain and no extra production.
 */
int
PlatformXIO_Web_PendingTotal(void);

/**
 * Whether a cache read may block the frame that issued it.
 *
 * On (the default) a read is a synchronous round trip that returns before
 * Process does, which is what keeps a boot -- several hundred archives, read
 * one after another -- from costing an event-loop turn per archive.
 *
 * Off, the read is queued and the frame-gated path delivers it a turn later.
 * That is what a live client wants: a synchronous XMLHttpRequest freezes the
 * main thread for longer than the request takes, and the reads a live client
 * issues are exactly the ones that coincide with something appearing on screen
 * -- the first play of a sound id stalls the frame its hitsplat is drawn on.
 *
 * The host is expected to turn it off once the client is up. Nothing breaks if
 * it never does; that is how this backend behaved before the call existed.
 */
void
PlatformXIO_Web_SetBlockingReads(int allowed);

#endif
