#ifndef SRC_UI_UITREE_CANVAS_FLOOR_H
#define SRC_UI_UITREE_CANVAS_FLOOR_H

/**
 * The two measurements that decide how narrow the canvas may get.
 *
 * Both are full passes over the component array, asked once a frame, and both
 * read nothing but the tree -- which is why they are here rather than on App.
 *
 * They answer different halves of the same question. The STRIP is furniture
 * carved out of the canvas edge: the resizable toplevels lay their children
 * out beside it, so a canvas of exactly the frame's floor leaves the frame
 * short by the strip's width. The CORE is the lane's own floor, read from the
 * one moment the layout states it -- when a block it lays inside the carved
 * area comes out WIDER than that area.
 *
 * The core is 0 when everything fits, which is also the answer when there is
 * no such block. Only the overflow case is measurable: a block that fits tells
 * you nothing about how small it could have been, and needs nothing.
 *
 * Both answer 0 for a tree that is not there yet. A boot frame is resized
 * before the tree is built, and a canvas with no chrome on it is the truth at
 * that moment rather than a caller's mistake.
 */

struct UITree;

/** Width of the right-hand furniture carved out of the canvas. */
int
UITree_MeasureRightChromeStripWidth(struct UITree const* tree);

/** The lane toplevel's own minimum width, or 0 when nothing overflows. */
int
UITree_MeasureLaneFrameCoreWidth(struct UITree const* tree);

#endif /* SRC_UI_UITREE_CANVAS_FLOOR_H */
