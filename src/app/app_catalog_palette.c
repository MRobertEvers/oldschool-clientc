/*
 * The Catalog palette's thumbnails. See app_catalog_palette.h.
 *
 * Since workbench 7.2 the pictures come from the shared record renderer
 * (app_record_thumbs.c: the Gallery's tiles and the Catalog's npc rows use
 * the same one), asked at the palette's cell size; this file only carries
 * the strip's wanted slots to it and the answers back.
 */

#include "app/app_catalog_palette.h"

#include "app/app_internal.h"
#include "app/app_record_thumbs.h"
#include "editor/panels/edpanels.h"

#include <assert.h>

#if !defined(__EMSCRIPTEN__)

void
app_catalog_palette_update(struct App* app)
{
    struct EditorUi* ui;
    int kind;
    int id;
    int slot;
    int scene_id;

    assert(app);
    if( !app->editor || !app->editor_panel.visible || !app->editor_panel.ui || !app->scene || !app->provider )
        return;
    ui = app->editor_panel.ui;
    if( !Editor_CatalogPalette_ThumbWanted(ui, &kind, &id, &slot) )
        return;
    /* The shared renderer (app_record_thumbs.c) answers once its assets are
     * in: a picture, or none for a record with no model. */
    scene_id = app_record_thumb(app, (uint32_t)kind, id, EDITOR_CATALOG_PALETTE_THUMB_PX);
    if( scene_id > 0 )
        Editor_CatalogPalette_ThumbDone(ui, slot, scene_id);
    else if( scene_id < 0 )
        Editor_CatalogPalette_ThumbDone(ui, slot, 0);
}

#else /* __EMSCRIPTEN__ */

void
app_catalog_palette_update(struct App* app)
{
    (void)app;
}

#endif
