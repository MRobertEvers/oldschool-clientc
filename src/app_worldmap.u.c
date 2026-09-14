/*
 * The WORLD MAP -- the full-screen map the cache's scripts open, as opposed to
 * the minimap. Building its tile raster from the loaded geography, placing the
 * icons the mapelement configs name, and answering what the CS2 world-map
 * opcodes ask of it.
 *
 * A unity fragment of app.c, not a module. It is textually part of app.c's
 * translation unit and included at exactly the point it was cut from, so every
 * helper here stays static and every App field it reads stays where it was.
 * The split is for the reader.
 *
 * The drag and the zoom are not here: panning is UIWorldMapDrag_Tick's and the
 * map's own state is rs_worldmap.c's. What stays is the part that needs an
 * App -- reaching into the cache for a geography square, queueing the loads a
 * cold icon needs, and putting the result in the scene.
 *
 * @see src/app.c, src/game/rs_worldmap.h
 */

/* rs_worldmap_view.h states the region size itself so it can be tested against
 * nothing but itself. This is the one place that sees both numbers, so it is
 * where they are held to being the same one: a drift would offset every region
 * on the map from the icons drawn over it. */
_Static_assert(
    RS_WORLDMAP_REGION_TILES_X == WORLD_MAP_TERRAIN_X,
    "the world map view's region width must be the world's");
_Static_assert(
    RS_WORLDMAP_REGION_TILES_Z == WORLD_MAP_TERRAIN_Z,
    "the world map view's region depth must be the world's");

static bool
app_worldmap_push_icon(
    struct App* app,
    int element_id,
    int screen_x,
    int screen_y)
{
    struct ToriRS_MapElement* element = NULL;
    struct ToriRS_Sprite* sprite;
    struct UITreeWorldMapTile* tile;
    int scene_id;

    if( app->worldmap.tiles.count >= RS_WORLDMAP_TILES_MAX || element_id < 0 )
        return false;

    /* Off-surface icons are not worth a config load. */
    if( screen_x < app->worldmap.drag.box_x - 32 ||
        screen_x > app->worldmap.drag.box_x + app->worldmap.drag.box_w + 32 ||
        screen_y < app->worldmap.drag.box_y - 32 ||
        screen_y > app->worldmap.drag.box_y + app->worldmap.drag.box_h + 32 )
        return false;

    /* Warm the mapelement first so category visibility can gate the sprite
     * load — same order as before the shared helper. */
    element = CacheProvider_MapElementGet(app->provider, element_id);
    if( !element )
    {
        struct ToriRS_Task* task = CreateTask_MapElementLoad(app->provider, element_id);
        if( task )
            ToriRS_TaskQueue_Add(app->runner.queue, task);
        return false;
    }
    /* The one seam both icon sources funnel through, so it is where the map's
     * element-enable state gets its only consumer: WORLDMAP_DISABLEELEMENT(S)
     * / _ELEMENTCATEGORY are write-only until something declines to draw. The
     * key panel's five display toggles are exactly these calls. */
    if( !RS_WorldMap_IconVisible(app->host.worldmap, element_id, element->category) )
        return false;

    scene_id = app_mapfunction_scene_id(app, element_id, &element);
    if( scene_id <= 0 )
        return false; /* cold sprite, or label-only (sprite_id < 0) */

    assert(element);
    sprite = CacheProvider_SpriteGet(app->provider, element->sprite_id);
    assert(sprite && sprite->frame_count > 0);

    /* Flash marker first, so it lands *behind* the icon (tiles draw in push
     * order). Reserve room for both, or the marker would be the last blit that
     * fits and the icon would drop out. */
    if( RS_WorldMap_ShouldFlashIcon(app->host.worldmap, element_id, element->category) &&
        app->worldmap.tiles.count + 1 < RS_WORLDMAP_TILES_MAX )
    {
        int flash_scene = app_worldmap_flash_marker_scene(app);
        if( flash_scene > 0 )
        {
            tile = RS_WorldMapTiles_Push(&app->worldmap.tiles);
            assert(tile);
            tile->scene_id = flash_scene;
            tile->w = 30;
            tile->h = 30;
            tile->x = screen_x - tile->w / 2;
            tile->y = screen_y - tile->h / 2;
        }
    }

    tile = RS_WorldMapTiles_Push(&app->worldmap.tiles);
    assert(tile);
    tile->scene_id = scene_id;
    tile->w =
        sprite->frames[0].crop_width > 0 ? sprite->frames[0].crop_width : sprite->frames[0].width;
    tile->h = sprite->frames[0].crop_height > 0 ? sprite->frames[0].crop_height
                                                : sprite->frames[0].height;
    /* Centred on its tile, like every map icon in the reference. */
    tile->x = screen_x - tile->w / 2;
    tile->y = screen_y - tile->h / 2;
    return true;
}

static int
app_worldmap_build_tiles(
    struct App* app,
    struct UITreeHostRequest* req)
{
    struct RS_WorldMapState* map = app->host.worldmap;
    struct ToriRS_WorldMapArea const* area;
    int box_x = req->u.get_worldmap_tiles.box_x;
    int box_y = req->u.get_worldmap_tiles.box_y;
    int box_w = req->u.get_worldmap_tiles.box_w;
    int box_h = req->u.get_worldmap_tiles.box_h;
    /* Two scales, deliberately: `bake_scale` is the whole-pixel size regions
     * are rendered at (bakes are keyed by it, so it must not follow the zoom
     * animation or every intermediate value would rebake the whole view), and
     * `scale_fp` is where the zoom actually is this frame. Everything measured
     * on screen uses the second; only RegionSprite takes the first. */
    int bake_scale;
    int scale_fp;
    /* Pixel width of a whole region at the live zoom, and the unit every
     * position below is derived from — reference method5686 computes exactly
     * this (`(int)(zoom * 64)`) and lays regions out in multiples of it, so
     * neighbours stay flush instead of drifting apart by a rounding error. */
    int region_px;
    int display_x;
    int display_y;
    int centre_x;
    int centre_y;

    RS_WorldMapTiles_Reset(&app->worldmap.tiles);
    /* Emit-time record of where the tiles were placed this redraw. Click
     * coordinate math reads it; whether the map is open at all is answered by
     * app_worldmap_surface_live, not by this box — emit only runs on redraw
     * frames, and once the interface is hidden it stops writing, so the last
     * rectangle would otherwise outlive the open map. */
    app->worldmap.drag.box_x = box_x;
    app->worldmap.drag.box_y = box_y;
    app->worldmap.drag.box_w = box_w;
    app->worldmap.drag.box_h = box_h;
    *req->u.get_worldmap_tiles.out_items = app->worldmap.tiles.items;
    if( req->u.get_worldmap_tiles.out_background_rgb )
        *req->u.get_worldmap_tiles.out_background_rgb = 0;

    if( !map || box_w <= 0 || box_h <= 0 )
        return 0;
    /* Adopts the areas once the load task has published them. */
    if( !RS_WorldMap_Sync(map) )
        return 0;
    area = RS_WorldMap_CurrentArea(map);
    if( !area )
        return 0;

    /* TORIRS_WORLDMAP_FORCE_MAP=<id>: one-shot area switch for measuring why
     * non-Gielinor surfaces go black. Logs display vs region bounds and leaves
     * the forced area selected for the rest of the run. */
    {
        static int force_done;
        char const* force = getenv("TORIRS_WORLDMAP_FORCE_MAP");
        if( force && !force_done )
        {
            int map_id = (int)strtol(force, NULL, 0);
            force_done = 1;
            RS_WorldMap_SetCurrentMapId(map, map_id);
            area = RS_WorldMap_CurrentArea(map);
            if( area )
            {
                int dx, dy;
                RS_WorldMap_DisplayPosition(map, &dx, &dy);
                TORIRS_ERR(
                    "worldmap FORCE_MAP id=%d name=%s display=%d,%d "
                    "regions x=%d..%d y=%d..%d sources=%d sections=%d zoom=%d\n",
                    area->id,
                    area->internal_name ? area->internal_name : "?",
                    dx,
                    dy,
                    area->region_low_x,
                    area->region_high_x,
                    area->region_low_y,
                    area->region_high_y,
                    area->region_source_count,
                    area->section_count,
                    RS_WorldMap_Zoom(map));
            }
            else
                TORIRS_ERR("worldmap FORCE_MAP id=%d: Area() returned NULL\n", map_id);
        }
    }

    /* Drop resident Gielinor (or previous-area) bakes on area change so the new
     * area does not churn the LRU on its first frames. */
    {
        static int last_area_id = -1;
        int area_id = area->id;
        if( last_area_id >= 0 && last_area_id != area_id )
            RS_WorldMapRender_Clear(app->worldmap.render, app->scene);
        last_area_id = area_id;
    }

    if( req->u.get_worldmap_tiles.out_background_rgb )
        *req->u.get_worldmap_tiles.out_background_rgb = area->background_colour & 0xFFFFFF;

    /* The widget owns the surface size; the scripts read it back through
     * WORLDMAP_GETSIZE, so it has to be told what it actually got. */
    RS_WorldMap_SetDisplayPixelSize(map, box_w, box_h);

    /* TORIRS_WORLDMAP_ZOOM="z[,z2[,at_frame]]": force the zoom, optionally
     * switching to z2 after at_frame frames (default 300). The zoom buttons are
     * CS2 ops on the surface chrome, so a headless run cannot press them, and
     * the transition between two zooms is the thing worth capturing. */
    {
        char const* forced = getenv("TORIRS_WORLDMAP_ZOOM");
        if( forced )
        {
            char* end = NULL;
            long first = strtol(forced, &end, 0);
            long second = first;
            long at_frame = 300;
            if( end && *end == ',' )
            {
                second = strtol(end + 1, &end, 0);
                if( end && *end == ',' )
                    at_frame = strtol(end + 1, NULL, 0);
            }
            RS_WorldMap_SetZoom(map, (int)(app->worldmap.debug_frame < at_frame ? first : second));
        }
    }

    bake_scale = RS_WorldMap_ZoomScale(map);
    scale_fp = RS_WorldMap_ZoomScaleFp(map);
    if( bake_scale <= 0 )
        bake_scale = 1;
    if( scale_fp <= 0 )
        scale_fp = RS_WORLDMAP_ZOOM_SCALE_ONE;
    region_px = WORLD_MAP_TERRAIN_X * scale_fp / RS_WORLDMAP_ZOOM_SCALE_ONE;
    if( region_px <= 0 )
        region_px = 1;
    RS_WorldMap_DisplayPosition(map, &display_x, &display_y);
    if( display_x < 0 || display_y < 0 )
        return 0;

    centre_x = box_x + box_w / 2;
    centre_y = box_y + box_h / 2;
    RS_WorldMapRender_BeginFrame(app->worldmap.render);
    /* The mapscene pack lives in the bridge's static-sprite registry, which the
     * renderer cannot reach; hand it over before any bake. */
    RS_WorldMapRender_SetMapScenes(
        app->worldmap.render,
        UITreeSceneBridge_StaticSpriteSceneId(&app->bridge, STATIC_SPRITE_MAPSCENE));

    {
        struct RS_WorldMapRegionBounds bounds;
        struct RS_WorldMapViewport viewport;

        bounds.low_x = area->region_low_x;
        bounds.low_y = area->region_low_y;
        bounds.high_x = area->region_high_x;
        bounds.high_y = area->region_high_y;
        viewport.display_tile_x = display_x;
        viewport.display_tile_z = display_y;
        viewport.box_w = box_w;
        viewport.box_h = box_h;
        viewport.region_px = region_px;
        RS_WorldMapVisits_Build(&app->worldmap.visits, &bounds, &viewport);
    }

    for( int i = 0; i < app->worldmap.visits.count; i++ )
    {
        {
            int region_x = app->worldmap.visits.items[i].region_x;
            int region_y = app->worldmap.visits.items[i].region_y;
            struct UITreeWorldMapTile* tile;
            int size = 0;
            int fallback_scene_id = -1;
            int scene_id;

            scene_id = RS_WorldMapRender_RegionSprite(
                app->worldmap.render,
                app->provider,
                app->scene,
                app->runner.queue,
                area,
                region_x,
                region_y,
                bake_scale,
                &size,
                &fallback_scene_id);
            /* Mid-zoom the right bake may not exist yet; a bake of the same
             * region at the previous zoom stands in, stretched, so the view
             * scales continuously instead of blinking through the background. */
            if( scene_id < 0 )
                scene_id = fallback_scene_id;
            if( scene_id < 0 )
                continue;

            tile = RS_WorldMapTiles_Push(&app->worldmap.tiles);
            if( !tile )
                break;
            tile->scene_id = scene_id;
            tile->x = centre_x + (region_x * WORLD_MAP_TERRAIN_X - display_x) * region_px /
                                     WORLD_MAP_TERRAIN_X;
            tile->y =
                centre_y - ((region_y * WORLD_MAP_TERRAIN_Z + WORLD_MAP_TERRAIN_Z) - display_y) *
                               region_px / WORLD_MAP_TERRAIN_Z;
            /* The box is a region at the *live* zoom either way — that is what
             * makes both the stand-in bake and a bake at another zoom step line
             * up with their neighbours while the transition runs. */
            tile->w = region_px;
            tile->h = region_px;
            tile->scaled = 1;
            (void)size;
        }
    }

    /* Icons in a second pass, so no later region paints over an earlier
     * region's icons: everything in this list draws in order. */
    for( int i = 0; i < app->worldmap.visits.count; i++ )
    {
        {
            int region_x = app->worldmap.visits.items[i].region_x;
            int region_y = app->worldmap.visits.items[i].region_y;
            struct RS_WorldMapRegionIcon const* icons = NULL;
            int scene_id = RS_WorldMapRender_RegionSprite(
                app->worldmap.render,
                app->provider,
                app->scene,
                app->runner.queue,
                area,
                region_x,
                region_y,
                bake_scale,
                NULL,
                NULL);
            int icon_count =
                scene_id < 0
                    ? 0
                    : RS_WorldMapRender_RegionIcons(app->worldmap.render, scene_id, &icons);

            for( int i = 0; i < icon_count; i++ )
                app_worldmap_push_icon(
                    app,
                    icons[i].element_id,
                    centre_x + (region_x * WORLD_MAP_TERRAIN_X + icons[i].tile_x - display_x) *
                                   region_px / WORLD_MAP_TERRAIN_X,
                    centre_y - (region_y * WORLD_MAP_TERRAIN_Z + icons[i].tile_y - display_y) *
                                   region_px / WORLD_MAP_TERRAIN_Z);
        }
    }

    app_worldmap_build_icons(app, area, centre_x, centre_y, display_x, display_y, region_px);

    /*
     * The HINT ARROW's target, marked on the map -- All Settings row 272,
     * "Clue scroll helper - Worldmap marker".
     *
     * Same payload as row 273's in-world arrow and deliberately so: neither row
     * has a reader in the cache or in the NXT engine, and the only marker family
     * the reference carries is the hint arrow's own three sprites
     * (`GetSpriteHintMapMarkersID` / `...HintMapEdgeID` / `...HintHeadIconsID`).
     * So one server-sent coord is what both rows are about, and the server's
     * choice of whether to send it is the setting; this is the map half of
     * drawing it.
     *
     * Only the COORD form. An npc or player subject moves, and the world map is
     * a static surface the player pans by hand -- a marker that chased an npc
     * across it would be redrawing a map the player is reading. The reference
     * marks a coord for the same reason.
     *
     * Last, so it lands over every icon: a marker underneath a mapfunction is a
     * marker nobody sees, and the icons are exactly what a clue step points at.
     */
    if( app->hint_arrow.type == APP_HINT_ARROW_COORD )
    {
        int map_x, map_y;

        /* Plane 0: the hint packet carries no plane, and the world map surface
         * is composited from one anyway. */
        if( app->worldmap.tiles.count < RS_WORLDMAP_TILES_MAX &&
            ToriRS_WorldMapArea_Position(
                area, 0, app->hint_arrow.target, app->hint_arrow.tile_z, &map_x, &map_y) )
        {
            int const flash_scene = app_worldmap_flash_marker_scene(app);
            int const x = centre_x + (map_x - display_x) * region_px / WORLD_MAP_TERRAIN_X;
            int const y = centre_y - (map_y - display_y) * region_px / WORLD_MAP_TERRAIN_Z;

            if( flash_scene > 0 && x > app->worldmap.drag.box_x - 32 &&
                x < app->worldmap.drag.box_x + app->worldmap.drag.box_w + 32 &&
                y > app->worldmap.drag.box_y - 32 && y < app->worldmap.drag.box_y + app->worldmap.drag.box_h + 32 )
            {
                /*
                 * The synthesised flash disc, not one of `worldmap_marker_0..8`.
                 * Those are the PLAYER-PLACED markers -- `marker_0` is the yellow
                 * X somebody dropped by hand -- and reusing one would make a
                 * server hint indistinguishable from the player's own note to
                 * themselves. The disc is already what this client draws to say
                 * "look here" (see `app_worldmap_flash_marker_scene`), and the
                 * cache names no hint-marker asset to prefer over it.
                 */
                struct UITreeWorldMapTile* tile = RS_WorldMapTiles_Push(&app->worldmap.tiles);

                assert(tile);
                tile->scene_id = flash_scene;
                tile->w = 30;
                tile->h = 30;
                tile->x = x - tile->w / 2;
                tile->y = y - tile->h / 2;
            }
        }
    }

    app->worldmap.debug_frame++;
    if( getenv("TORIRS_WORLDMAP_DEBUG") && app->worldmap.debug_frame % 300 == 0 )
    {
        /* Queue depth is the tell for the surface freezing: the runner is
         * serial (one task per IO round trip), so a backlog that climbs every
         * frame means loads are being queued faster than they can retire, and
         * anything newly in view waits behind all of it. */
        int queued = 0;
        int min_region_x = 0, max_region_x = 0, min_region_y = 0, max_region_y = 0;
        for( struct ToriRS_Task* task = app->runner.queue ? app->runner.queue->head : NULL;
             task && queued < 100000;
             task = task->next )
            queued++;
        /* Read back off the visit list rather than recomputed: the trace is
         * here to say what the frame actually asked for, and a second copy of
         * the bounds arithmetic could disagree with the first. */
        for( int i = 0; i < app->worldmap.visits.count; i++ )
        {
            struct RS_WorldMapVisit const* visit = &app->worldmap.visits.items[i];
            if( i == 0 || visit->region_x < min_region_x )
                min_region_x = visit->region_x;
            if( i == 0 || visit->region_x > max_region_x )
                max_region_x = visit->region_x;
            if( i == 0 || visit->region_y < min_region_y )
                min_region_y = visit->region_y;
            if( i == 0 || visit->region_y > max_region_y )
                max_region_y = visit->region_y;
        }
        TORIRS_ERR(
            "worldmap frame: display=%d,%d zoom=%d bake_scale=%d region_px=%d "
            "regions x=%d..%d y=%d..%d visits=%d blits=%d queued_tasks=%d\n",
            display_x,
            display_y,
            RS_WorldMap_Zoom(map),
            bake_scale,
            region_px,
            min_region_x,
            max_region_x,
            min_region_y,
            max_region_y,
            app->worldmap.visits.count,
            app->worldmap.tiles.count,
            queued);
    }

    return app->worldmap.tiles.count;
}

/*
 * Overview pane (clientCode 1401): scale-blit the current area's compositetexture
 * into the widget box. Red viewport rects are CS2 on overview_overlay — not here.
 * SpriteNewFromArgbOwned takes the pixel buffer, so each upload copies from the
 * area-owned decode; SceneSpriteAdd frees the previous overview sprite.
 */
static int
app_worldmap_ensure_overview_scene(
    struct App* app,
    struct ToriRS_WorldMapArea const* area)
{
    uint32_t* copy;
    struct ToriDraw_Sprite* sprite;
    struct ToriDraw_Sprite** sprites;
    size_t nbytes;

    assert(app);
    assert(area);
    assert(area->overview_pixels);
    assert(area->overview_width > 0);
    assert(area->overview_height > 0);

    if( app->worldmap.overview_area_id == area->id &&
        app->worldmap.overview_scene_id == UITREE_SCENE_WORLD_MAP_OVERVIEW_SPRITE_ID )
        return app->worldmap.overview_scene_id;

    nbytes = (size_t)area->overview_width * (size_t)area->overview_height * sizeof(*copy);
    copy = malloc(nbytes);
    assert(copy);
    memcpy(copy, area->overview_pixels, nbytes);

    sprite = ToriDraw_SpriteNewFromArgbOwned(copy, area->overview_width, area->overview_height);
    if( !sprite )
    {
        free(copy);
        return -1;
    }
    sprites = malloc(sizeof(*sprites));
    assert(sprites);
    sprites[0] = sprite;
    ToriDraw_SceneSpriteAdd(app->scene, UITREE_SCENE_WORLD_MAP_OVERVIEW_SPRITE_ID, sprites, 1);
    app->worldmap.overview_scene_id = UITREE_SCENE_WORLD_MAP_OVERVIEW_SPRITE_ID;
    app->worldmap.overview_area_id = area->id;
    return app->worldmap.overview_scene_id;
}

static int
app_worldmap_build_overview(
    struct App* app,
    struct UITreeHostRequest* req)
{
    struct RS_WorldMapState* map;
    struct ToriRS_WorldMapArea const* area;
    int box_x;
    int box_y;
    int box_w;
    int box_h;
    int scene_id;

    assert(app);
    assert(req);
    assert(req->u.get_worldmap_overview.out_items);

    box_x = req->u.get_worldmap_overview.box_x;
    box_y = req->u.get_worldmap_overview.box_y;
    box_w = req->u.get_worldmap_overview.box_w;
    box_h = req->u.get_worldmap_overview.box_h;

    memset(&app->worldmap.overview_tile, 0, sizeof(app->worldmap.overview_tile));
    *req->u.get_worldmap_overview.out_items = &app->worldmap.overview_tile;
    if( req->u.get_worldmap_overview.out_background_rgb )
        *req->u.get_worldmap_overview.out_background_rgb = 0;

    map = app->host.worldmap;
    if( !map || !RS_WorldMap_IsLoaded(map) )
        return 0;
    area = RS_WorldMap_CurrentArea(map);
    if( !area )
        return 0;

    if( req->u.get_worldmap_overview.out_background_rgb )
        *req->u.get_worldmap_overview.out_background_rgb = area->background_colour & 0xFFFFFF;

    if( !area->overview_pixels || area->overview_width <= 0 || area->overview_height <= 0 )
        return 0;

    scene_id = app_worldmap_ensure_overview_scene(app, area);
    if( scene_id <= 0 )
        return 0;

    app->worldmap.overview_tile.scene_id = scene_id;
    app->worldmap.overview_tile.atlas_index = 0;
    app->worldmap.overview_tile.x = box_x;
    app->worldmap.overview_tile.y = box_y;
    app->worldmap.overview_tile.w = box_w;
    app->worldmap.overview_tile.h = box_h;
    app->worldmap.overview_tile.scaled = 1;
    return 1;
}

/*
 * Map element icons over the surface (banks, altars, shops, ...).
 *
 * The compositemap gives each icon a *source* world coord and a map element id;
 * the area converts the coord to a map surface position, and the element config
 * (MEC, config group 35) gives the sprite. Both the config and the sprite load
 * on demand, so an icon appears a frame or two after the region under it —
 * exactly how the reference behaves on a cold cache.
 */
static void
app_worldmap_build_icons(
    struct App* app,
    struct ToriRS_WorldMapArea const* area,
    int centre_x,
    int centre_y,
    int display_x,
    int display_y,
    int region_px)
{
    for( int i = 0; i < area->icon_count; i++ )
    {
        struct ToriRS_WorldMapIcon const* icon = &area->icons[i];
        int plane;
        int world_x;
        int world_y;
        int map_x;
        int map_y;

        if( icon->hidden )
            continue;

        /* The compositemap stores a *source* world coord; the area's sections
         * say where that lands on the map surface. */
        ToriRS_WorldMapUnpackCoord(icon->coord, &plane, &world_x, &world_y);
        if( !ToriRS_WorldMapArea_Position(area, plane, world_x, world_y, &map_x, &map_y) )
            continue;

        app_worldmap_push_icon(
            app,
            icon->element,
            centre_x + (map_x - display_x) * region_px / WORLD_MAP_TERRAIN_X,
            centre_y - (map_y - display_y) * region_px / WORLD_MAP_TERRAIN_Z);
    }
}

/*
 * A click on the open world map, reported to the server as the absolute tile it
 * landed on (reference ClickWorldMap).
 *
 * The screen -> tile conversion is the inverse of the icon placement above: the
 * box centre shows the view's display position, and each map tile is
 * `zoom scale` pixels wide, with screen y growing opposite map y. That gives a
 * *display* coord (a position on the flattened map surface); the area's
 * sections turn it back into the world coord the surface was baked from, which
 * is what the packet carries.
 */
static void
app_worldmap_click(
    struct App* app,
    int mouse_x,
    int mouse_y)
{
    int display_x = 0;
    int display_y = 0;
    int scale_fp;
    int map_x;
    int map_y;
    int source;
    int plane;
    int abs_x;
    int abs_z;

    assert(app);
    if( !app->host.worldmap || !app->net )
        return;
    RS_WorldMap_DisplayPosition(app->host.worldmap, &display_x, &display_y);
    if( display_x < 0 || display_y < 0 )
        return;
    /* The live scale, not the target: this inverts what was drawn, and mid
     * zoom-transition those differ. */
    scale_fp = RS_WorldMap_ZoomScaleFp(app->host.worldmap);
    if( scale_fp <= 0 )
        return;

    map_x = display_x + (mouse_x - (app->worldmap.drag.box_x + app->worldmap.drag.box_w / 2)) *
                            RS_WORLDMAP_ZOOM_SCALE_ONE / scale_fp;
    map_y = display_y - (mouse_y - (app->worldmap.drag.box_y + app->worldmap.drag.box_h / 2)) *
                            RS_WORLDMAP_ZOOM_SCALE_ONE / scale_fp;

    source =
        RS_WorldMap_DisplayToSource(app->host.worldmap, ToriRS_WorldMapPackCoord(0, map_x, map_y));
    if( source < 0 )
        return;
    ToriRS_WorldMapUnpackCoord(source, &plane, &abs_x, &abs_z);
    if( torirs_env_net_debug() )
        TORIRS_LOG(
            "worldmap_click: screen=%d,%d display=%d,%d -> %d,%d,%d\n",
            mouse_x,
            mouse_y,
            map_x,
            map_y,
            plane,
            abs_x,
            abs_z);
    APP_NET_SEND(
        app,
        net_out_click_world_map(
            app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf), plane, abs_x, abs_z));
}

/*
 * Is the map surface actually on screen? The emit-time box cannot answer this:
 * emit only runs on redraw frames, and once the interface is hidden it stops
 * running at all, so the last box it recorded outlives the open map. Close
 * sets hide only on the group roots (not on the builtin surface node itself),
 * so the ancestor walk and RootIsDisplayable are both required.
 */
static int
app_worldmap_surface_live(struct App* app)
{
    struct UITree* tree;
    int32_t idx;

    assert(app);
    tree = app->tree;
    if( !tree )
        return 0;
    idx = tree->worldmap_index;
    if( idx < 0 || (uint32_t)idx >= tree->component_count )
        return 0;
    if( tree->components[idx].freed || tree->components[idx].type != UIELEM_BUILTIN_WORLDMAP )
        return 0;
    /* Includes cache/script hide, plugin-frame suppression, mount-container
     * ancestry and root displayability.  The old local walk tested only
     * behavior.hide, so a frame-hidden world map still owned drag/click. */
    return !UITree_NodeOrAncestorDisplayHidden(tree, idx);
}

/*
 * Drag to pan the world map, as the App sees it.
 *
 * The pan itself is UIWorldMapDrag (game/rs_worldmap_drag.h). What is here is
 * the half that needs an App: gathering this frame's pointer facts, and
 * turning the result back into a click or a redraw.
 */
static void
app_worldmap_drag_tick(
    struct App* app,
    struct LibToriRS_Input* input,
    int pointer_consumed)
{
    struct UIWorldMapDragInput drag_input;
    enum UIWorldMapDragResult result;

    assert(app);
    assert(input);

    drag_input.mouse_x = input->curr.mouse_x;
    drag_input.mouse_y = input->curr.mouse_y;
    drag_input.left_down = LibToriRS_Input_IsMouseDown(input, TORIRSM_LEFT);
    drag_input.left_held = LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT);
    drag_input.left_up = input->curr.mouse_button_up[TORIRSM_LEFT];
    drag_input.pointer_consumed = pointer_consumed;
    drag_input.minimenu_visible = app->interact.minimenu.visible;
    drag_input.hover_component_id = app->hover_com_id;
    drag_input.surface_live = app_worldmap_surface_live(app);

    result = UIWorldMapDrag_Tick(&app->worldmap.drag, &drag_input, app->host.worldmap);
    if( result == UI_WORLDMAP_DRAG_CLICKED )
        app_worldmap_click(app, drag_input.mouse_x, drag_input.mouse_y);
    else if( result == UI_WORLDMAP_DRAG_PANNED )
        app->need_redraw = 1;
}
