#ifndef SRC_PLATFORM_PLATFORM_CLIPBOARD_H
#define SRC_PLATFORM_PLATFORM_CLIPBOARD_H

/**
 * The operating system's text clipboard.
 *
 * The editor had none (2026-11-09): a name could not be pasted into a field
 * and nothing the editor showed -- a record name, a file path, a script
 * snippet -- could be carried to the user's own text editor. Scripts are
 * written there, not here, so "copy this" is the editor's half of that
 * workflow.
 *
 * Self-contained on purpose: no window, no SDL, no App. Windows talks to the
 * Win32 clipboard; macOS and Linux go through the desktop's own tools
 * (pbcopy / pbpaste, wl-copy / wl-paste, xclip), so a headless test that
 * links a panel links this too and simply gets 0 back on a machine with
 * none of them. The web build has no synchronous clipboard and answers 0.
 *
 * Text is UTF-8 in both directions.
 */

#include <stddef.h>

/** Put `text` on the clipboard. Returns 1 when the system took it, 0 when
 *  this machine has no clipboard to take it (no tool installed, the web). */
int
PlatformClipboard_SetText(const char* text);

/**
 * Read the clipboard's text into `out` (NUL-terminated, cut at `out_size`).
 * Returns the bytes written, 0 when the clipboard is empty, holds no text or
 * cannot be read.
 */
int
PlatformClipboard_GetText(
    char* out,
    size_t out_size);

#endif
