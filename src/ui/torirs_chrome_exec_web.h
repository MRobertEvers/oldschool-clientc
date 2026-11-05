#ifndef SRC_UI_TORIRS_CHROME_EXEC_WEB_H
#define SRC_UI_TORIRS_CHROME_EXEC_WEB_H

#include <stdint.h>

/*
 * Tiny application-shell request channel for the web rail.
 *
 * The DOM rail remains after the widget executor is shut down, so it cannot
 * use the executor's polled widget-intent queue to reopen itself. JavaScript
 * writes this single idempotent desired state through the exported setter;
 * the frame thread consumes it without waiting on the browser main thread.
 */

#ifdef __cplusplus
extern "C" {
#endif

void ToriRSChromeExecWeb_RequestOpen(int open);
int ToriRSChromeExecWeb_TakeOpenRequest(int* open);

/** Browser rail callbacks. Both are retained wasm exports and remain valid
 * while the selected page executor is unbound. */
void ToriRSChromeExecWeb_RequestSelect(
    int plugin_index, uint32_t selection_generation);
void ToriRSChromeExecWeb_RequestLayout(
    uint32_t selection_generation,
    uint32_t page_generation,
    int width,
    int height,
    int custom_width,
    int scale_milli,
    int size_class,
    int visible,
    int game_visible);

/**
 * One picture a GRAPH IMAGE item names (TORIRS_CHROME_GRAPH_IMAGE: the square
 * picker's world map), keyed by its scene id: `argb` is `width` x `height`
 * 0xAARRGGBB, copied before this returns. The page's host turns it into a
 * `graph.image` envelope (rgbaBase64) and the DOM executor draws every IMAGE
 * item naming `scene_id` from it, on a canvas under the well's other items.
 * Independent of the bound executor, like the open request: the picture
 * outlives page snapshots, and a replaced iframe gets the last of each id
 * again. Returns 1 when the page took it.
 */
int ToriRSChromeExecWeb_GraphImage(
    int scene_id,
    uint32_t version,
    int width,
    int height,
    uint32_t const* argb);

#ifdef __cplusplus
}
#endif

#endif
