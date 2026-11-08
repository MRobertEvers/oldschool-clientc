#ifndef SRC_APP_APP_RECORD_THUMBS_H
#define SRC_APP_APP_RECORD_THUMBS_H

/*
 * Record thumbnails (workbench 7.2, docs/MAPED_DESIGN.html #catalog): one
 * renderer for every picture of a loc, npc or obj record the editor's
 * panels show small -- the Gallery's tiles, the Catalog's npc rows, the
 * Catalog palette's slots. The record's models are built as the Preview
 * builds them (a loc's centrepiece row, an npc's parts merged, an obj's
 * inventory model resized; recoloured and lit with the config's contrast
 * and ambient) and rastered with the Preview's default pitch and yaw,
 * framed by their bounds the way EditorPreviewCamera_TakeFit frames them,
 * into a square of the asked size.
 *
 * Asked through EditorUiAppServices.record_thumb every frame a tile is
 * drawn (app_record_thumb): a hit answers from the cache, a miss marks the
 * entry wanted and answers 0; app_record_thumbs_update, once a frame beside
 * the Preview's update, renders a few wanted entries, asking for their
 * assets (config, models) once each and giving a record up after
 * APP_RECORD_THUMB_WAIT_FRAMES. The cache is a fixed LRU keyed by (kind,
 * id, size); a scene sprite per entry. App layer only. Nothing on the web:
 * its panels are DOM, which shows no GRAPH image.
 */

#include <stdint.h>

struct App;

/** Entries held at once: the Gallery's screen of tiles a few times over. */
#define APP_RECORD_THUMBS 384
/** Frames a wanted thumbnail may wait on its assets before it is answered
 *  with none. */
#define APP_RECORD_THUMB_WAIT_FRAMES 600
/** Thumbnails rendered a frame at most. */
#define APP_RECORD_THUMBS_PER_FRAME 2
/** Scene sprite ids: APP_RECORD_THUMB_SCENE_BASE + entry. */
#define APP_RECORD_THUMB_SCENE_BASE 0x4C002000

/** The picture of record `id` of `kind` (CacheProvider_CatalogKind: loc,
 *  npc, obj) at `px` square: its scene sprite id, 0 while it is still to be
 *  rendered (asked for), -1 for a record with no model. */
int
app_record_thumb(
    struct App* app,
    uint32_t kind,
    int id,
    int px);

/** Once per frame with the editor up: renders at most
 *  APP_RECORD_THUMBS_PER_FRAME wanted thumbnails. */
void
app_record_thumbs_update(struct App* app);

#endif
