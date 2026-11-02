#ifndef SRC_APP_APP_CATALOG_PALETTE_H
#define SRC_APP_APP_CATALOG_PALETTE_H

/*
 * The Catalog palette's thumbnails (editor/panels/edpanel_catalog_palette.c):
 * the App renders each record the strip asks for, one per frame, from the
 * Preview's default angle into a scene sprite the strip's GRAPH image items
 * show. App layer only (app/app_internal.h's rule).
 */

struct App;

/** Once per frame with the editor up (app_frame.c, beside the Preview's
 *  update). Renders at most one wanted thumbnail; asks for what it still
 *  needs (config, models) once and waits. Nothing on the web: its panels are
 *  DOM, which shows no GRAPH image. */
void
app_catalog_palette_update(struct App* app);

#endif
