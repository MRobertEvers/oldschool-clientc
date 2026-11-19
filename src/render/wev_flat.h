#ifndef SRC_RENDER_WEV_FLAT_H
#define SRC_RENDER_WEV_FLAT_H

/*
 * How a FLATTENED world entity is drawn: a sailing hull that the overlap rule
 * (app_wev_decide_flatten, app/app_wev.c) squashes to 1% of its height and paints one colour.
 *
 * The decision of WHICH hulls flatten, and that a flattened hull carries no
 * players or npcs (World::suppress_dynamic_population), is common to every
 * strategy and stays in app/app_wev.c. What differs is how the flattened deck
 * reaches the raster, and that is this table:
 *
 *   live     (wev_flat_live.c) the deob's way, class467.method10419: the
 *            painter descends into the deck like any hull, and the frame draws
 *            a Y-scaled, recoloured copy of every model in it, every frame
 *            (torirs_frame_flat.u.h). Poses and deck changes show at once.
 *
 *   outline  (wev_flat_outline.c) not the deob's: the deck's footprint is
 *            rasterised once into a grid, merged into rectangles and kept as
 *            ONE flat-shaded scene element. The painter emits that element in
 *            place of the deck, and it is rebuilt only when the deck's
 *            geometry changes (World::geometry_serial). Animations are ignored.
 *            On RSProx 4281 (24 flat hulls) the live way sends ~446k faces a
 *            frame to the raster; this sends a few thousand.
 *
 * Chosen once at App init: TORIRS_WEV_FLAT=live|outline, outline by default.
 * Every entry point is called only for a hull whose `flattened` is set.
 *
 * This knows no App. Each operation is handed what it reads: the ToriDraw
 * scene the root and every deck share, the worldview registry a hull's deck is
 * looked up in, the parent painter, the frame transform. The App units
 * (app/app_wev.c, app.c) hold one struct WevFlat and pass those in.
 */

#include <stdbool.h>

struct Painter;
struct ToriDraw_Scene;
struct ToriRS_FrameViewXform;
struct Wev;
struct WorldviewRegistry;

struct WevFlatOps
{
    char const* name;

    /** The strategy's state, owned by the holder until destroy. NULL is a valid
     *  state for a strategy that keeps none. */
    void* (*create)(void);
    /** Release every scene element and allocation the state holds. The scene
     *  is still alive. Accepts the state create returned, NULL included. */
    void (*destroy)(void* state, struct ToriDraw_Scene* scene);

    /**
     * Bind flattened hull `wev` into its parent's painter for this frame, in
     * place of painter_set_world_entity_view. `camera_sx/sz` are the deck-space
     * camera tile the full-detail binding would use. Leaving the view unbound
     * is allowed: the painter then emits the hull's markers with nothing
     * between them. The deck is `worldviews`' view `wev->id`; anything built
     * goes into `scene`. `debug` prints what a build made (TORIRS_WEV_DEBUG).
     */
    void (*bind_painter)(
        void* state,
        struct ToriDraw_Scene* scene,
        struct WorldviewRegistry* worldviews,
        struct Wev const* wev,
        struct Painter* parent_painter,
        int camera_sx,
        int camera_sz,
        bool debug);

    /** Fill the flatten fields of the hull's frame transform (flatten_scale,
     *  flatten_y_offset, flat_hsl, flat); the rest is already set. */
    void (*bind_frame)(
        void* state,
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
        struct ToriDraw_Scene* scene,
        int view_id);
};

/** The strategy the App holds: one table and the state it made. */
struct WevFlat
{
    struct WevFlatOps const* ops;
    void* state;
};

extern struct WevFlatOps const g_wev_flat_live;
extern struct WevFlatOps const g_wev_flat_outline;

/** "live" or "outline"; NULL selects the default (outline). Any other name is
 *  a typo in an environment variable and aborts. */
struct WevFlatOps const*
WevFlat_OpsByName(char const* name);

void
WevFlat_Init(
    struct WevFlat* flat,
    struct WevFlatOps const* ops);

/** Destroy the state (its scene elements go from `scene`) and clear the
 *  table. A flat never initialised, or already freed, is a no-op. */
void
WevFlat_Free(
    struct WevFlat* flat,
    struct ToriDraw_Scene* scene);

#endif
