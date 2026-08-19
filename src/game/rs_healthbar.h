#ifndef RS_HEALTHBAR_H
#define RS_HEALTHBAR_H

/*
 * Healthbar types: how an entity's overhead bar is sized, filled and faded.
 *SC=/private/tmp/claude-501/-Users-matthewevers-Documents-git-repos-3draster/bb62e08d-71d6-4b3e-a75a-36486c9400c2/scratchpad;
 *for cam in "6464,-3000,4400,180,0" "6464,-4200,5400,300,0" "5200,-2600,5400,160,1600"; do n=$(echo
 *$cam | tr ',' '_'); SDL_VIDEODRIVER=dummy MOCK230_ALLOW_STALE_SCRIPTS=1 MOCK230_SAVES=$SC/saves2
 *TORIRS_SIM_CMD="200,tob 5;280,tobgo" TORIRS_WEDGE_CAM="$cam" TORIRS_MAX_FRAMES=460
 *TORIRS_EXIT_BMP=$SC/xc_$n.bmp ./src/torirs --manifest manifest_osrs239_torirs.ini --user probe1
 *--pass test > /dev/null 2>&1; sips -s format png $SC/xc_$n.bmp --out $SC/xc_$n.png >/dev/null
 *2>&1; done; ls $SC/xc_*.png The client used to draw every bar as a 30x5 green/red rectangle and
 *take the *fill value the server sent* as the bar's pixel width. That is two mistakes stacked: the
 *fill is a fraction of `width`, not a pixel count, and the pixel count is the front sprite's, which
 *runs from 30 up to 160 across the 85 records in cache.osrs239. A boss bar came out as a
 *screen-wide sliver with a green stub at the left, because a fill byte that decoded to 158 became a
 * 158-pixel bar.
 *
 * What the reference does (`class381`, and the health-bar block of
 * drawEntities) is:
 *
 *   bar_width = front && back ? front_sprite.width - 2*padding : type.width
 *   drawn     = fill * bar_width / type.width
 *
 * so `width` (config opcode 14) is only ever a denominator. The two agree for
 * 84 of the 85 records -- `standard_shield_60` is 60 wide and declares 60 --
 * which is exactly why conflating them survives casual testing; healthbar_9
 * pairs a 100px `headbar_olmtimer_100` sprite with a declared width of 120.
 *
 * Sprites are the normal case and rectangles the fallback: the reference draws
 * the back sprite in full, clips to the filled span, and draws the front over
 * it. Only when a type names neither sprite does it fill two rectangles.
 *
 * The defaults below are the reference constructor's, so a cache with no group
 * 33 -- or an id no record covers -- keeps behaving exactly as the hardcoded
 * client did. See `3rd/rscache/src/datatypes/dat2_config_healthbar.h`.
 */

enum
{
    /** class381 var10: the fill denominator when opcode 14 is absent. */
    RS_HEALTHBAR_DEFAULT_WIDTH = 30,
    /** class381 var7: cycles the bar outlives its update when opcode 5 is
     *  absent. The standard bar overrides it to 300, which is the 300-cycle
     *  window the overlay used to hardcode as `combat_cycle = cycle + 400`
     *  against a `> cycle + 100` test. */
    RS_HEALTHBAR_DEFAULT_PERSIST = 70,
    /** Rectangle-fallback bar height (reference fills 5 rows). */
    RS_HEALTHBAR_FALLBACK_HEIGHT = 5
};

struct RS_HealthbarType
{
    /** Opcode 7: the filled half. -1 = none, and then no sprite bar draws. */
    int front_sprite;
    /** Opcode 8: the empty half, drawn underneath. -1 = none. */
    int back_sprite;
    /** Opcode 14. A denominator, not a pixel count -- see the header. */
    int width;
    /** Opcode 15. Trimmed off both ends of the sprite to get the bar span.
     *  No record in any measured cache carries it, so this is always 0. */
    int padding;
    /** Opcode 5, in 20ms cycles. */
    int persist_cycles;
    /** Opcode 11: where inside `persist_cycles` the fade starts. -1 = never
     *  fades, which is the constructor default and what most records get. */
    int fade_threshold;
};

struct RS_Healthbars
{
    /** Indexed by healthbar id; ids are dense enough to array. */
    struct RS_HealthbarType* types;
    int count;
};

void
RS_Healthbars_Init(struct RS_Healthbars* healthbars);

void
RS_Healthbars_Free(struct RS_Healthbars* healthbars);

/** Takes ownership of `types`. */
int
RS_Healthbars_SetTypes(
    struct RS_Healthbars* healthbars,
    struct RS_HealthbarType* types,
    int count);

/** Fill one record with the reference constructor's defaults. */
void
RS_Healthbars_Defaults(struct RS_HealthbarType* out);

/**
 * The record for `id`, or the reference's defaults when the table has none.
 *
 * Never NULL: a cache with no group 33 is a supported configuration (dat1 has
 * no such type at all), and the caller's job is to draw a bar either way. The
 * returned pointer is owned by the table -- or is a shared default record --
 * and must not be freed.
 */
struct RS_HealthbarType const*
RS_Healthbars_TypeFor(
    struct RS_Healthbars const* healthbars,
    int id);

#endif
