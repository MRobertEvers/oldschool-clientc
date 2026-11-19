#ifndef SRC_PLUGIN_PLUGIN_SCREENSHOTS_H
#define SRC_PLUGIN_PLUGIN_SCREENSHOTS_H

/*
 * Frame captures asked for (api->screenshot) and not taken yet.
 *
 * A capture is taken at the top of the NEXT frame, because every caller is
 * upstream of the frame it wants: a plugin recognises a level-up while the
 * packet is being executed, before the interface that announces it has been
 * laid out, let alone drawn.
 *
 * Small on purpose. A capture is a rare, deliberate act, and a plugin that
 * asks for more than a handful in one frame is looping by mistake; a full
 * table refuses the request rather than rendering four identical frames of
 * the same instant.
 *
 * This decides which slot a request takes (the first free one), the file name
 * it is written under (a name with no extension gets ".png", one that has an
 * extension is left alone), whether a capture is waiting, whether a given
 * file is still waiting, and the order captures are handed back in (slot
 * order).
 *
 * This does not resolve where a file goes, render, encode or write anything,
 * or print. The caller resolves the destination into the claimed slot's
 * `path` and waits it only when there is one, logs a refusal, and when it has
 * the frame's pixels takes each waiting capture and queues the write.
 */

#include "asyncio.h"
#include "plugin/torirs_plugin_host.h"
#include "plugin/torirs_plugin_types.h"

#include <stdbool.h>

/** Captures that may wait at once. */
#define PLUGIN_SCREENSHOTS_MAX 4
/** Room for a file name: an asset name plus an added extension. */
#define PLUGIN_SCREENSHOT_NAME_CAP (TORIRS_PLUGIN_ASSET_NAME_MAX + 8)

struct PluginScreenshot
{
    int in_use;
    char plugin[TORIRS_PLUGIN_NAME_MAX];
    char dir[TORIRS_PLUGIN_SCREENSHOT_DIR_MAX];
    char name[PLUGIN_SCREENSHOT_NAME_CAP];
    /** The file this will be written to, resolved when the request was made
     *  rather than when the picture is taken. The plugin is handed it on the
     *  spot -- a deferred write has no other moment to tell anyone where it
     *  went -- so it is worked out once, here, and the writer uses the same
     *  string rather than resolving a second time. */
    char path[TORIRS_IOITEM_MAX_PATH];
};

struct PluginScreenshots
{
    struct PluginScreenshot slots[PLUGIN_SCREENSHOTS_MAX];
};

/**
 * The first free slot, filled with the plugin, the folder (NULL is none) and
 * the file name, ".png" added when the name has no extension. Not waiting
 * yet: the caller resolves `path` and calls PluginScreenshots_Wait. NULL when
 * every slot is waiting.
 */
struct PluginScreenshot*
PluginScreenshots_Claim(
    struct PluginScreenshots* shots,
    char const* plugin,
    char const* dir,
    char const* name);

/** A claimed slot with a resolved path now waits for the next frame. */
void
PluginScreenshots_Wait(struct PluginScreenshot* shot);

/** How many captures are waiting. */
int
PluginScreenshots_Waiting(struct PluginScreenshots const* shots);

/** Whether a capture of exactly this file is still waiting. */
bool
PluginScreenshots_IsWaiting(
    struct PluginScreenshots const* shots,
    char const* path);

/**
 * The first waiting capture's file, its slot now free; NULL when none waits.
 * The string lives in the slot and stays valid until the slot is claimed
 * again.
 */
char const*
PluginScreenshots_Take(struct PluginScreenshots* shots);

#endif
