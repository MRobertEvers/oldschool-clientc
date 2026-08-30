#ifndef PLATFORM_SDL2_RENDERER_SOFT3D_DEBUG_U_C
#define PLATFORM_SDL2_RENDERER_SOFT3D_DEBUG_U_C

/*
 * Measurement and diagnostic probes for the software renderer.
 *
 * Included by platform_sdl2_renderer_soft3d.c once the command and scene types
 * are in scope. Everything here is instrumentation: nothing in this file puts
 * a pixel on the screen that a shipping frame depends on, and the renderer
 * keeps only a one-line call at each site.
 *
 * They stay RUNTIME-armed rather than compile-time gated, unlike the toridraw
 * NDJSON log next door: these sites are per-frame, per-command or per-model
 * rather than per-face, so an unarmed build pays one predicted branch, and the
 * recipes in docs/LARGE_LOCS_PAINTER.md and docs/java_parity/PLAN.md are
 * written against a stock binary -- a probe that needs a rebuild to answer a
 * question about the build you already have is a probe nobody reaches for.
 * Every accessor reads its variable once into a static; see the getenv
 * hot-path rule.
 *
 * Diagnostics -- what is drawing, or not drawing:
 *
 *   TORIRS_SPRITE_CENSUS=1        how often the opaque sprite fast path's
 *                                 precondition holds, and over how much area
 *   TORIRS_DRAW_TRACE=<vertices>  why a model of at least that many vertices
 *                                 did or did not rasterize, edge-triggered
 *   TORIRS_PIXOWNER=x0,x1,y0,y1[,RRGGBB]
 *                                 which draw command last wrote each pixel of
 *                                 a rect
 *   TORIRS_PIXOWNER_OUT=<path>    where that goes (default stderr)
 *   TORIRS_PIXOWNER_AT=<frame>    which frame to report (default: the last)
 *
 * Ablations -- decompose the frame by deletion. Each renders a deliberately
 * wrong image and exists only to be timed against the baseline:
 *
 *   TORIRS_ABL_NOMODELS=1         drop the 3D model pass entirely
 *   TORIRS_ABL_NORASTER=1         keep project + hittest + sort, write no pixels
 *   TORIRS_ABL_NOCHROME=1         keep the 3D pass, drop 2D drawing around it
 *
 * A/B arms -- two implementations of the same thing, one binary:
 *
 *   TORIDRAW_FRAME_AB_KERNELS=a,b face-sort kernel per frame-A/B arm
 *   TORIDRAW_FRAME_AB_BATCH=a,b   raster batching per frame-A/B arm
 *
 * And one compile-time census, because poisoning the framebuffer changes what
 * the frame looks like:
 *
 *   make -C src TORIDRAW_PROBE_CFLAGS=-DTORIDRAW_FB_POISON=1
 *   TORIDRAW_FB_POISON_FILE=<path>   where the census goes (default stderr)
 *
 * Output goes through TORIRS_REPORT, not TORIRS_LOG: it is already gated by
 * the flag that turned it on, and TORIRS_LOG is compiled out under OPT=1 --
 * the only build whose numbers anyone wants.
 */

/* Defined below the probes, in the renderer. The walk at the bottom of this
 * file drives the command stream itself, so it needs both. */
static void
soft3d_execute_measured(
    struct ToriRS_Soft3D* soft,
    struct ToriRS_RenderCommand const* cmd);
static int
soft3d_cmd_is_draw(enum ToriRS_RenderCommandKind kind);

/* ---- sprite opacity census (TORIRS_SPRITE_CENSUS=1) ---------------------- *
 *
 * A fast path is worth only what its precondition is worth, and that has to be
 * counted rather than assumed -- the opaque-sprite one measured as no change
 * at all.
 */

static double g_spr_opaque_n;
static double g_spr_opaque_px;
static double g_spr_mixed_n;
static double g_spr_mixed_px;

static void
soft3d_dbg_sprite_census_dump(void)
{
    double n = g_spr_opaque_n + g_spr_mixed_n;
    double px = g_spr_opaque_px + g_spr_mixed_px;
    if( n <= 0.0 )
        return;
    TORIRS_REPORT(
        "\n=== sprite opacity census ===\n"
        "all-opaque : %10.0f blits (%5.1f%%)  %12.0f px (%5.1f%%)\n"
        "mixed      : %10.0f blits (%5.1f%%)  %12.0f px (%5.1f%%)\n",
        g_spr_opaque_n, 100.0 * g_spr_opaque_n / n, g_spr_opaque_px,
        px > 0 ? 100.0 * g_spr_opaque_px / px : 0.0,
        g_spr_mixed_n, 100.0 * g_spr_mixed_n / n, g_spr_mixed_px,
        px > 0 ? 100.0 * g_spr_mixed_px / px : 0.0);
}

static int
soft3d_dbg_sprite_census_armed(void)
{
    static int armed = -1;
    if( armed < 0 )
    {
        armed = getenv("TORIRS_SPRITE_CENSUS") ? 1 : 0;
        if( armed )
            atexit(soft3d_dbg_sprite_census_dump);
    }
    return armed;
}

/** One plain blit, classified by whether the whole sprite was opaque. */
static void
soft3d_dbg_sprite_census_note(struct ToriDraw_Sprite* spr, int px)
{
    assert(spr);
    if( !soft3d_dbg_sprite_census_armed() )
        return;
    if( ToriDraw_SpriteAlphaClass(spr) == TORIDRAW_SPRITE_ALPHA_ALL_OPAQUE )
    {
        g_spr_opaque_n += 1.0;
        g_spr_opaque_px += (double)px;
    }
    else
    {
        g_spr_mixed_n += 1.0;
        g_spr_mixed_px += (double)px;
    }
}

/* ---- draw trace (TORIRS_DRAW_TRACE=<min_vertex_count>) ------------------- *
 *
 * Per-frame, unsampled, why a big model did or did not rasterize.
 *
 * Built for "I can still mouse over and click the Queen but nothing is drawn".
 * That symptom localises in soft3d_draw_model and nowhere else, because the
 * pick there runs BEFORE the face sort: a model that projects VISIBLE and then
 * sorts to zero faces stays fully clickable and paints nothing. The
 * TORIDRAW_SORT_DEBUG/NDJSON counters answer the same question but are gated
 * and sampled, so they miss the transition that causes it.
 *
 * Vertex count selects the model rather than an element id, because element
 * ids change every time the entity respawns and the model that matters here is
 * the largest thing in the scene.
 *
 * Edge-triggered: the last verdict is remembered so the trace can be left on
 * for a whole session and still only speak when something changes. Per-frame
 * logging is what made the first version unusable -- the volume buried the one
 * frame that mattered.
 */

static int g_draw_trace_last_cull = -999;
static int g_draw_trace_last_sorted = -999;
static int g_draw_trace_drawn_frames = 0;

static int
soft3d_dbg_draw_trace_min_vertices(void)
{
    static int cached = -1;
    if( cached < 0 )
    {
        char const* v = getenv("TORIRS_DRAW_TRACE");
        cached = (v && *v) ? atoi(v) : 0;
    }
    return cached;
}

/** Is this the model the trace was armed for? */
static int
soft3d_dbg_draw_trace_watches(struct ToriRS_RenderCommand_Model const* cmd)
{
    int const min_vertices = soft3d_dbg_draw_trace_min_vertices();

    assert(cmd);
    return min_vertices > 0 && ToriDraw_ModelKindIsFull(cmd->model.kind) &&
           cmd->model.u.model.model &&
           cmd->model.u.model.model->vertex_count >= min_vertices;
}

/**
 * The projection verdict: 0 is TORIDRAW_CULL_VISIBLE. Anything else and the
 * model is gone before the sort ever sees it.
 */
static void
soft3d_dbg_draw_trace_cull(
    struct ToriRS_RenderCommand_Model const* cmd,
    struct ToriDraw_Position const* position,
    int cull)
{
    struct ToriDraw_Model const* m;
    struct ToriDraw_BoundsCylinder const* bc;

    assert(cmd);
    assert(position);
    if( !soft3d_dbg_draw_trace_watches(cmd) )
        return;

    m = cmd->model.u.model.model;
    bc = m->has_bounds_cylinder ? &m->bounds_cylinder : NULL;
    if( cull != g_draw_trace_last_cull )
    {
        TORIRS_REPORT(
            "draw_trace: element=%d vc=%d faces=%d CULL %d -> %d (0=visible) "
            "pos=(%d,%d,%d) radius=%d min_y=%d max_y=%d bias=%d after %d drawn frames\n",
            cmd->element_id, m->vertex_count, m->face_count, g_draw_trace_last_cull, cull,
            position->x, position->y, position->z, bc ? bc->radius : -1, bc ? bc->min_y : 0,
            bc ? bc->max_y : 0, bc ? bc->min_z_depth_any_rotation : -1,
            g_draw_trace_drawn_frames);
        g_draw_trace_last_cull = cull;
        g_draw_trace_drawn_frames = 0;
    }
    if( cull != TORIDRAW_CULL_VISIBLE )
        g_draw_trace_drawn_frames++;
}

/**
 * Faces surviving the depth/priority sort. Zero here after a VISIBLE cull is
 * exactly the invisible-but-clickable state.
 *
 * Only the transitions matter: went-to-zero is that state, came-back is the
 * recovery. A drifting face count on a model that keeps drawing is noise.
 */
static void
soft3d_dbg_draw_trace_sorted(
    struct ToriRS_RenderCommand_Model const* cmd,
    int sorted)
{
    assert(cmd);
    if( !soft3d_dbg_draw_trace_watches(cmd) )
        return;

    if( (sorted <= 0) != (g_draw_trace_last_sorted <= 0) )
    {
        TORIRS_REPORT(
            "draw_trace: element=%d SORTED %d -> %d %s after %d frames\n",
            cmd->element_id, g_draw_trace_last_sorted, sorted,
            sorted <= 0 ? "(RASTERIZES NOTHING - invisible but still clickable)"
                        : "(drawing again)",
            g_draw_trace_drawn_frames);
        g_draw_trace_drawn_frames = 0;
    }
    g_draw_trace_last_sorted = sorted;
    g_draw_trace_drawn_frames++;
}

/* ---- ablation arms (measurement only) ------------------------------------ *
 *
 * Three deletions that decompose `render` by subtraction rather than by
 * instrumentation. TORIRS_PERF is ~69% of the frame on the XP box, so its own
 * split of r_project / r_sort / r_raster cannot be read as absolute time;
 * three runs of a build that simply does less can.
 *
 * Read once each; off is one predicted branch.
 */

/** Drop the whole 3D model pass -- projection, hittest, face sort and raster. */
static int
soft3d_dbg_abl_nomodels(void)
{
    static int armed = -1;
    if( armed < 0 )
        armed = getenv("TORIRS_ABL_NOMODELS") ? 1 : 0;
    return armed;
}

/**
 * Keep the projection, the hittest and the face sort; write no pixels. The
 * difference against the baseline is what rasterisation actually costs, and
 * the difference against NOMODELS is what deciding-what-to-draw costs.
 */
static int
soft3d_dbg_abl_noraster(void)
{
    static int armed = -1;
    if( armed < 0 )
        armed = getenv("TORIRS_ABL_NORASTER") ? 1 : 0;
    return armed;
}

/**
 * Keep the 3D pass and every state/resource command; drop the 2D *drawing*
 * outside it -- the sidebar, chatback, minimap, compass and every sprite and
 * glyph composing them.
 *
 * This puts an upper bound on what damage-gated chrome rasterisation could
 * recover, by deleting all of it: a damage system that never redrew a single
 * chrome pixel could not beat this number.
 */
static int
soft3d_dbg_abl_nochrome(void)
{
    static int armed = -1;
    if( armed < 0 )
        armed = getenv("TORIRS_ABL_NOCHROME") ? 1 : 0;
    return armed;
}

/* ---- A/B arms ------------------------------------------------------------ */

/*
 * Which stores the framebuffer clear uses; see the call site.
 *
 * The whole-buffer clear is still under test, so the frame A/B arm picks:
 * arm B clears non-temporally, arm A -- and so every build with the A/B off --
 * keeps the ordinary stores.
 *
 * Gated exactly as that call site is: the Apple clear is one memset_pattern4
 * over the whole buffer and has no arm to A/B, so defining this there would
 * only be an unused function.
 */
#if !defined(__APPLE__)
static int
soft3d_dbg_full_clear_nt(void)
{
    return ToriDraw_FrameAbArm();
}
#endif

/*
 * The frame A/B brackets the clear AND the rasterization that follows it,
 * because the two are coupled through the cache. The arm is read once, inside
 * the region, so the clear and the accounting cannot disagree about which arm
 * this frame was.
 */
static void
soft3d_dbg_frame_ab_begin(struct ToriRS_Soft3D* soft)
{
    assert(soft);

    ToriDraw_FrameAbBegin();
    if( ToriDraw_FrameAbEnabled() && soft->batch_ab[0] >= 0 )
        ToriDraw_RasterBatchSetArmed(soft->batch_ab[ToriDraw_FrameAbArm()]);
}

static void
soft3d_dbg_frame_ab_end(void)
{
    ToriDraw_FrameAbEnd();
}

/*
 * The two arms of the in-frame A/B, from the environment. Each starts as a
 * copy of the held kernel and takes a face-sort kernel by name and a batch
 * setting, so an arm differs from the baseline in exactly what was named.
 */
static const struct ToriDraw_FaceCullSortKernel*
soft3d_dbg_face_sort_kernel_by_name(const char* name, size_t len)
{
    assert(name);
    if( len >= 1 && (name[0] == 'b' || name[0] == '0') )
        return ToriDraw_FaceCullSortKernelGetBucket();
    if( len >= 1 && (name[0] == 'f' || name[0] == '1') )
        return ToriDraw_FaceCullSortKernelGetFlat();
    return NULL;
}

static void
soft3d_dbg_frame_ab_kernels_init(struct ToriRS_Soft3D* soft)
{
    static int announced;
    const char* v;
    int arm;

    assert(soft);
    assert(soft->kernel);

    soft->batch_ab[0] = -1;
    soft->batch_ab[1] = -1;
    for( arm = 0; arm < 2; arm++ )
        soft->kernel_ab[arm] = *soft->kernel;

    v = getenv("TORIDRAW_FRAME_AB_KERNELS");
    if( v )
    {
        const char* comma = strchr(v, ',');
        const char* names[2] = { v, comma ? comma + 1 : v };
        size_t lens[2] = { comma ? (size_t)(comma - v) : strlen(v),
                           comma ? strlen(comma + 1) : strlen(v) };
        for( arm = 0; arm < 2; arm++ )
        {
            const struct ToriDraw_FaceCullSortKernel* k =
                soft3d_dbg_face_sort_kernel_by_name(names[arm], lens[arm]);
            if( k )
                soft->kernel_ab[arm].face_sort = k;
        }
    }
    v = getenv("TORIDRAW_FRAME_AB_BATCH");
    if( v )
    {
        const char* comma = strchr(v, ',');
        soft->batch_ab[0] = v[0] == '1';
        soft->batch_ab[1] = comma ? comma[1] == '1' : soft->batch_ab[0];
    }
    /* Init runs again on every relayout; say it once. */
    if( ToriDraw_FrameAbEnabled() && !announced++ )
        TORIRS_REPORT(
            "soft3d: frame A/B arms: A face_sort=%s batch=%d, B face_sort=%s batch=%d\n",
            soft->kernel_ab[0].face_sort->name,
            soft->batch_ab[0],
            soft->kernel_ab[1].face_sort->name,
            soft->batch_ab[1]);
}

/* ---- pixel ownership (TORIRS_PIXOWNER=x0,x1,y0,y1[,RRGGBB]) -------------- *
 *
 * Which draw command last wrote each pixel of a rect.
 *
 * The rect is snapshotted after every command and the pixels that changed are
 * attributed to that command. Filtering by a final colour answers the question
 * a screenshot cannot -- "what is painting THIS" -- naming the loc id or
 * terrain tile rather than leaving the reader to guess from a draw-order dump.
 * Written to TORIRS_PIXOWNER_OUT (default stderr) at frame TORIRS_PIXOWNER_AT
 * (default: the last frame rendered).
 *
 * O(commands x rect), so keep the rect small; it is inert unless armed and the
 * unarmed render loop is untouched.
 */

struct PixOwnerRec
{
    int cmd_index;
    int kind;      /* enum ToriRS_RenderCommandKind */
    int element_id;
    int loc_id;
    int terrain;   /* 1 = terrain tile */
    int tile_x, tile_z, tile_level;
    int world_x, world_y, world_z;
};

static int g_pixowner_armed = -1;
static int g_pixowner_rect[4];
static int g_pixowner_want_colour = -1;
static long g_pixowner_at = -1;
static long g_pixowner_frame;
static uint32_t* g_pixowner_prev;
static struct PixOwnerRec* g_pixowner_owner; /* one per rect pixel */
static int g_pixowner_cmd_index;
static int g_pixowner_active; /* this frame is the one being recorded */

static int
soft3d_dbg_pixowner_armed(void)
{
    if( g_pixowner_armed < 0 )
    {
        char const* env = getenv("TORIRS_PIXOWNER");
        char const* at = getenv("TORIRS_PIXOWNER_AT");
        char colour[16] = { 0 };
        g_pixowner_armed = 0;
        if( env && env[0] )
        {
            int got = sscanf(env, "%d,%d,%d,%d,%15s", &g_pixowner_rect[0], &g_pixowner_rect[1],
                             &g_pixowner_rect[2], &g_pixowner_rect[3], colour);
            if( got >= 4 )
            {
                g_pixowner_armed = 1;
                if( got >= 5 && colour[0] )
                    g_pixowner_want_colour = (int)strtol(colour, NULL, 16);
            }
        }
        g_pixowner_at = (at && at[0]) ? strtol(at, NULL, 0) : -1;
    }
    return g_pixowner_armed;
}

static void
soft3d_dbg_pixowner_begin(void)
{
    int rect_w = g_pixowner_rect[1] - g_pixowner_rect[0] + 1;
    int rect_h = g_pixowner_rect[3] - g_pixowner_rect[2] + 1;
    size_t count;

    g_pixowner_frame++;
    /* -1 = "the last frame": record every frame and let the final one win. */
    g_pixowner_active = (g_pixowner_at < 0 || g_pixowner_frame == g_pixowner_at);
    g_pixowner_cmd_index = 0;
    if( !g_pixowner_active || rect_w <= 0 || rect_h <= 0 )
        return;
    count = (size_t)rect_w * (size_t)rect_h;
    if( !g_pixowner_prev )
    {
        g_pixowner_prev = calloc(count, sizeof(*g_pixowner_prev));
        assert(g_pixowner_prev);
        g_pixowner_owner = calloc(count, sizeof(*g_pixowner_owner));
        assert(g_pixowner_owner);
    }
    for( size_t i = 0; i < count; i++ )
    {
        g_pixowner_owner[i].cmd_index = -1;
        g_pixowner_owner[i].element_id = -1;
        g_pixowner_owner[i].loc_id = -1;
    }
}

static void
soft3d_dbg_pixowner_after_command(
    struct ToriRS_Soft3D const* soft,
    struct ToriRS_RenderCommand const* cmd)
{
    int x0 = g_pixowner_rect[0], x1 = g_pixowner_rect[1];
    int y0 = g_pixowner_rect[2], y1 = g_pixowner_rect[3];
    int rect_w = x1 - x0 + 1;
    int index = g_pixowner_cmd_index++;

    assert(soft);
    assert(cmd);
    if( !g_pixowner_active )
        return;
    if( x1 >= soft->width )
        x1 = soft->width - 1;
    if( y1 >= soft->height )
        y1 = soft->height - 1;

    for( int y = y0; y <= y1; y++ )
    {
        for( int x = x0; x <= x1; x++ )
        {
            size_t slot = (size_t)(y - y0) * (size_t)rect_w + (size_t)(x - x0);
            uint32_t now = (uint32_t)soft->pixels[y * soft->stride + x] & 0xFFFFFFu;
            if( now == g_pixowner_prev[slot] )
                continue;
            g_pixowner_prev[slot] = now;
            g_pixowner_owner[slot].cmd_index = index;
            g_pixowner_owner[slot].kind = (int)cmd->kind;
            if( cmd->kind == TORIRSRC_DRAW_MODEL )
            {
                g_pixowner_owner[slot].element_id = cmd->u.model.element_id;
                g_pixowner_owner[slot].terrain = cmd->u.model.pick_terrain ? 1 : 0;
                g_pixowner_owner[slot].tile_x = cmd->u.model.pick_tile_x;
                g_pixowner_owner[slot].tile_z = cmd->u.model.pick_tile_z;
                g_pixowner_owner[slot].tile_level = cmd->u.model.pick_tile_level;
                g_pixowner_owner[slot].world_x = cmd->u.model.world_position.x;
                g_pixowner_owner[slot].world_y = cmd->u.model.world_position.y;
                g_pixowner_owner[slot].world_z = cmd->u.model.world_position.z;
                /* No loc id here: the renderer has no World to resolve an
                 * element through. `cmd=` indexes the same stream
                 * TORIRS_DRAW_ORDER prints, which carries the loc id -- that is
                 * the join, and it keeps this probe free of a world lookup. */
                g_pixowner_owner[slot].loc_id = -1;
            }
            else
            {
                g_pixowner_owner[slot].element_id = -1;
                g_pixowner_owner[slot].loc_id = -1;
                g_pixowner_owner[slot].terrain = 0;
            }
        }
    }
}

/** One row per (owner, colour), sorted by pixel count. */
struct PixOwnerAgg
{
    uint32_t colour;
    struct PixOwnerRec rec;
    int pixels;
};

static void
soft3d_dbg_pixowner_end(void)
{
    int x0 = g_pixowner_rect[0], x1 = g_pixowner_rect[1];
    int y0 = g_pixowner_rect[2], y1 = g_pixowner_rect[3];
    int rect_w = x1 - x0 + 1;
    int rect_h = y1 - y0 + 1;
    char const* out_path;
    struct PixOwnerAgg* agg;
    int agg_count = 0;
    FILE* out;

    if( !g_pixowner_active )
        return;

    /* With no TORIRS_PIXOWNER_AT every frame is recorded and every frame
     * prints; the file is opened "w" so the last frame is what survives, which
     * is the usual want. Point _OUT at a file rather than reading stderr. */
    out_path = getenv("TORIRS_PIXOWNER_OUT");
    out = (out_path && out_path[0]) ? fopen(out_path, "w") : stderr;
    if( !out )
        out = stderr;

    fprintf(out, "# pixel owners, rect x%d..%d y%d..%d, frame %ld\n", x0, x1, y0, y1,
            g_pixowner_frame);
    if( g_pixowner_want_colour >= 0 )
        fprintf(out, "# filtered to colour %06x\n", (unsigned)g_pixowner_want_colour);
    fprintf(out, "# colour cmd kind elem loc terrain tile pixels\n");

    agg = calloc((size_t)rect_w * (size_t)rect_h, sizeof(*agg));
    assert(agg);
    for( int i = 0; i < rect_w * rect_h; i++ )
    {
        uint32_t colour = g_pixowner_prev[i];
        struct PixOwnerRec* rec = &g_pixowner_owner[i];
        int found = -1;
        if( rec->cmd_index < 0 )
            continue;
        if( g_pixowner_want_colour >= 0 && colour != (uint32_t)g_pixowner_want_colour )
            continue;
        for( int a = 0; a < agg_count && found < 0; a++ )
            if( agg[a].colour == colour && agg[a].rec.cmd_index == rec->cmd_index )
                found = a;
        if( found < 0 )
        {
            found = agg_count++;
            agg[found].colour = colour;
            agg[found].rec = *rec;
        }
        agg[found].pixels++;
    }
    for( int a = 0; a < agg_count; a++ )
    {
        int best = a;
        for( int b = a + 1; b < agg_count; b++ )
            if( agg[b].pixels > agg[best].pixels )
                best = b;
        if( best != a )
        {
            struct PixOwnerAgg tmp = agg[a];
            agg[a] = agg[best];
            agg[best] = tmp;
        }
        fprintf(out, "%06x cmd=%d kind=%d elem=%d loc=%d %s", (unsigned)agg[a].colour,
                agg[a].rec.cmd_index, agg[a].rec.kind, agg[a].rec.element_id,
                agg[a].rec.loc_id, agg[a].rec.terrain ? "TERRAIN" : "loc");
        if( agg[a].rec.terrain )
            fprintf(out, " tile=%d,%d L%d", agg[a].rec.tile_x, agg[a].rec.tile_z,
                    agg[a].rec.tile_level);
        else
            fprintf(out, " wpos=%d,%d,%d", agg[a].rec.world_x, agg[a].rec.world_y,
                    agg[a].rec.world_z);
        fprintf(out, " pixels=%d\n", agg[a].pixels);
    }
    free(agg);

    if( out != stderr )
        fclose(out);
}

/* ---- framebuffer poison census (-DTORIDRAW_FB_POISON=1) ------------------ *
 *
 * Survivor census for the frame clear. Poison is a colour the palette cannot
 * produce, so any pixel still carrying it at end of frame was written by the
 * clear and by nothing else.
 *
 * Compile-time, unlike everything above, because it changes what the clear
 * writes: an armed frame is not a frame anyone can look at.
 */

#if defined(TORIDRAW_FB_POISON) && TORIDRAW_FB_POISON

#define FB_POISON_VALUE 0xFFDEADBEu
/** Frames to let the scene come up before anything is believed. */
#define FB_POISON_WARMUP 12

/** What the frame clear writes. Poison when the census is compiled in. */
#define SOFT3D_DBG_CLEAR_COLOUR FB_POISON_VALUE

static unsigned long long g_fb_poison_frames;
static unsigned long long g_fb_poison_survivors;
static unsigned long long g_fb_poison_total;
static int g_fb_poison_min_x = 1 << 30;
static int g_fb_poison_max_x = -1;
static int g_fb_poison_min_y = 1 << 30;
static int g_fb_poison_max_y = -1;
static unsigned g_fb_poison_worst;
static unsigned g_fb_poison_best = 0xFFFFFFFFu;
static unsigned g_fb_poison_last;
static unsigned long long g_fb_poison_blank;
static unsigned long long g_fb_poison_counted;
static int g_fb_poison_atexit;
static long g_fb_poison_warmup;

static void
soft3d_dbg_fb_poison_dump(void)
{
    const char* path = getenv("TORIDRAW_FB_POISON_FILE");
    FILE* f = path ? fopen(path, "w") : stderr;
    double frames = (double)(g_fb_poison_frames ? g_fb_poison_frames : 1);

    assert(f);
    fprintf(f, "fb poison census over %llu frames\n", g_fb_poison_frames);
    fprintf(f, "  pixels cleared per frame: %.0f\n",
            (double)g_fb_poison_total / frames);
    fprintf(f, "  frames counted after %d warmup: %llu (%llu of them drew nothing)\n",
            FB_POISON_WARMUP, g_fb_poison_counted, g_fb_poison_blank);
    fprintf(f, "  survivors/frame: min %u, mean %.0f, max %u; last frame %u\n",
            g_fb_poison_best == 0xFFFFFFFFu ? 0u : g_fb_poison_best,
            (double)g_fb_poison_survivors
                / (double)(g_fb_poison_counted ? g_fb_poison_counted : 1),
            g_fb_poison_worst, g_fb_poison_last);
    if( g_fb_poison_max_x >= 0 )
        fprintf(f, "  survivor bbox: x %d..%d, y %d..%d (%dx%d)\n",
                g_fb_poison_min_x, g_fb_poison_max_x,
                g_fb_poison_min_y, g_fb_poison_max_y,
                g_fb_poison_max_x - g_fb_poison_min_x + 1,
                g_fb_poison_max_y - g_fb_poison_min_y + 1);
    else
        fprintf(f, "  survivor bbox: none -- every pixel was overdrawn\n");
    if( path )
        fclose(f);
}

static void
soft3d_dbg_fb_poison_scan(const struct ToriRS_Soft3D* soft)
{
    const uint32_t* p = (const uint32_t*)soft->pixels;
    unsigned live = 0;
    int y;

    assert(soft);
    if( !g_fb_poison_atexit )
    {
        g_fb_poison_atexit = 1;
        atexit(soft3d_dbg_fb_poison_dump);
    }
    g_fb_poison_frames++;
    g_fb_poison_total += (unsigned)soft->width * (unsigned)soft->height;

    for( y = 0; y < soft->height; y++ )
    {
        const uint32_t* row = p + (size_t)y * (size_t)soft->width;
        int x;
        for( x = 0; x < soft->width; x++ )
        {
            if( row[x] != FB_POISON_VALUE )
                continue;
            live++;
            if( g_fb_poison_warmup < FB_POISON_WARMUP )
                continue;
            if( x < g_fb_poison_min_x ) g_fb_poison_min_x = x;
            if( x > g_fb_poison_max_x ) g_fb_poison_max_x = x;
            if( y < g_fb_poison_min_y ) g_fb_poison_min_y = y;
            if( y > g_fb_poison_max_y ) g_fb_poison_max_y = y;
        }
    }
    g_fb_poison_last = live;
    if( g_fb_poison_warmup < FB_POISON_WARMUP )
    {
        g_fb_poison_warmup++;
        return;
    }
    g_fb_poison_counted++;
    g_fb_poison_survivors += live;
    if( live > g_fb_poison_worst )
        g_fb_poison_worst = live;
    if( live < g_fb_poison_best )
        g_fb_poison_best = live;
    if( live * 2 >= (unsigned)soft->width * (unsigned)soft->height )
        g_fb_poison_blank++;
}

#define SOFT3D_DBG_FB_POISON_SCAN(soft) soft3d_dbg_fb_poison_scan((soft))

#else

#define SOFT3D_DBG_CLEAR_COLOUR         ((uint32_t)TORIRS_SOFT3D_BG)
#define SOFT3D_DBG_FB_POISON_SCAN(soft) ((void)0)

#endif /* TORIDRAW_FB_POISON */

/* ---- the probe-driven frame walk ----------------------------------------- *
 *
 * Two probes need to drive the command stream themselves rather than sit
 * inside it. They share one walk, so the renderer keeps a single plain loop
 * and pays nothing per command when neither is armed.
 */

static int
soft3d_dbg_frame_walk_armed(void)
{
    return soft3d_dbg_pixowner_armed() || soft3d_dbg_abl_nochrome();
}

static void
soft3d_dbg_frame_walk(
    struct ToriRS_Soft3D* soft,
    struct ToriRS_Frame* frame)
{
    int const pixowner = soft3d_dbg_pixowner_armed();
    int const nochrome = soft3d_dbg_abl_nochrome();
    struct ToriRS_RenderCommand cmd;
    int depth_3d = 0;

    assert(soft);
    assert(frame);

    if( pixowner )
        soft3d_dbg_pixowner_begin();
    while( ToriRS_FrameNextCommand(frame, &cmd) )
    {
        /* NOCHROME drops 2D drawing outside the 3D pass. Loads/unloads and
         * BEGIN/END still run, or the scene state diverges from the command
         * stream and the 3D pass stops being comparable. */
        if( nochrome )
        {
            if( cmd.kind == TORIRSRC_BEGIN_3D )
                depth_3d++;
            else if( cmd.kind == TORIRSRC_END_3D && depth_3d > 0 )
                depth_3d--;
            else if( depth_3d == 0 && soft3d_cmd_is_draw(cmd.kind) )
                continue;
        }
        soft3d_execute_measured(soft, &cmd);
        if( pixowner )
            soft3d_dbg_pixowner_after_command(soft, &cmd);
    }
    if( pixowner )
        soft3d_dbg_pixowner_end();
}

#endif /* PLATFORM_SDL2_RENDERER_SOFT3D_DEBUG_U_C */
