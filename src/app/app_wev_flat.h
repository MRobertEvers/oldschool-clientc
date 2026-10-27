#ifndef SRC_APP_APP_WEV_FLAT_H
#define SRC_APP_APP_WEV_FLAT_H

/*
 * How a FLATTENED world entity is drawn: a sailing hull that the overlap rule
 * (app_wev_decide_flatten) squashes to 1% of its height and paints one colour.
 *
 * The decision of WHICH hulls flatten, and that a flattened hull carries no
 * players or npcs (World::suppress_dynamic_population), is common to every
 * strategy and stays in app_wev.c. What differs is how the flattened deck
 * reaches the raster, and that is this table:
 *
 *   live     (app_wev_flat_live.c) the deob's way, class467.method10419: the
 *            painter descends into the deck like any hull, and the frame draws
 *            a Y-scaled, recoloured copy of every model in it, every frame
 *            (torirs_frame_flat.u.h). Poses and deck changes show at once.
 *
 *   outline  (app_wev_flat_outline.c) not the deob's: the deck's footprint is
 *            rasterised once into a grid, merged into rectangles and kept as
 *            ONE flat-shaded scene element. The painter emits that element in
 *            place of the deck, and it is rebuilt only when the deck's
 *            geometry changes (World::geometry_serial). Animations are ignored.
 *            On RSProx 4281 (24 flat hulls) the live way sends ~446k faces a
 *            frame to the raster; this sends a few thousand.
 *
 * Chosen once at App init: TORIRS_WEV_FLAT=live|outline, outline by default.
 * Every entry point is called only for a hull whose `flattened` is set.
 */

struct App;
struct Painter;
struct ToriRS_FrameViewXform;
struct Wev;

struct AppWevFlatOps
{
    char const* name;

    /** The strategy's state, owned by the App until destroy. NULL is a valid
     *  state for a strategy that keeps none. */
    void* (*create)(void);
    /** Release every scene element and allocation the state holds. The scene
     *  is still alive. Accepts the state create returned, NULL included. */
    void (*destroy)(void* state, struct App* app);

    /**
     * Bind flattened hull `wev` into its parent's painter for this frame, in
     * place of painter_set_world_entity_view. `camera_sx/sz` are the deck-space
     * camera tile the full-detail binding would use. Leaving the view unbound
     * is allowed: the painter then emits the hull's markers with nothing
     * between them.
     */
    void (*bind_painter)(
        void* state,
        struct App* app,
        struct Wev const* wev,
        struct Painter* parent_painter,
        int camera_sx,
        int camera_sz);

    /** Fill the flatten fields of the hull's frame transform (flatten_scale,
     *  flatten_y_offset, flat_hsl, flat); the rest is already set. */
    void (*bind_frame)(
        void* state,
        struct App* app,
        struct Wev const* wev,
        struct ToriRS_FrameViewXform* xf);

    /** Root-dynamic-pool elements this strategy owns, which the root rebuild
     *  sweep must not free (World_ForeignDynamicClaimFn). Writes at most
     *  `max`, returns how many. */
    int (*claim_elements)(
        void* state,
        int* out_element_ids,
        int max);

    /** View `view_id` is going away (App_WevDespawn): drop what was built
     *  for it. Called for every despawned view, flattened or not. */
    void (*release_view)(
        void* state,
        struct App* app,
        int view_id);
};

/** The strategy the App holds: one table and the state it made. */
struct AppWevFlat
{
    struct AppWevFlatOps const* ops;
    void* state;
};

extern struct AppWevFlatOps const g_app_wev_flat_live;
extern struct AppWevFlatOps const g_app_wev_flat_outline;

/** "live" or "outline"; NULL selects the default (outline). Any other name is
 *  a typo in an environment variable and aborts. */
struct AppWevFlatOps const*
AppWevFlat_OpsByName(char const* name);

void
AppWevFlat_Init(
    struct AppWevFlat* flat,
    struct AppWevFlatOps const* ops);

void
AppWevFlat_Free(
    struct AppWevFlat* flat,
    struct App* app);

#endif
