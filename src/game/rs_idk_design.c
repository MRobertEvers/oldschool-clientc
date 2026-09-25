#include "rs_idk_design.h"

#include "engine/cache_provider.h"
#include "engine/entity_model_build.h"
#include "engine/player_appearance.h"
#include "engine/torirs_types.h"
#include "game/rs_clientcode.h"

#include <assert.h>
#include <string.h>

/* Body part id a kit must carry to be offered for `part` at this gender.
 * Reference: `IdkType.list[kit].part === part + (idkDesignGender ? 0 : 7)`,
 * where idkDesignGender true is male. */
static int
design_body_part_id(
    struct RS_IdkDesign const* design,
    int part)
{
    return part + (design->gender == 0 ? 0 : RS_IDK_DESIGN_PARTS);
}

static int
design_kit_matches(
    struct CacheProvider* provider,
    int kit_id,
    int want_body_part)
{
    struct ToriRS_Idk* idk;
    if( kit_id < 0 )
        return 0;
    idk = CacheProvider_IdkGet(provider, kit_id);
    if( !idk || idk->not_selectable )
        return 0;
    return idk->body_part_id == want_body_part;
}

void
RS_IdkDesign_Init(struct RS_IdkDesign* design)
{
    assert(design);
    memset(design, 0, sizeof(*design));
    design->gender = 0;
    for( int i = 0; i < RS_IDK_DESIGN_PARTS; i++ )
        design->parts[i] = -1;
    design->redraw = 1;
}

int
RS_IdkDesign_KitTableCount(struct CacheProvider* provider)
{
    int count = 0;
    assert(provider);
    for( int id = 0; id < PLAYER_IDK_SCAN_MAX; id++ )
    {
        if( CacheProvider_IdkHas(provider, id) )
            count = id + 1;
    }
    return count;
}

int
RS_IdkDesign_ResolveKitCount(
    struct RS_IdkDesign* design,
    struct CacheProvider* provider)
{
    assert(design && provider);
    design->kit_count = RS_IdkDesign_KitTableCount(provider);
    return design->kit_count;
}

void
RS_IdkDesign_Validate(
    struct RS_IdkDesign* design,
    struct CacheProvider* provider)
{
    assert(design && provider);

    design->redraw = 1;
    design->load_requested_count = 0;

    for( int part = 0; part < RS_IDK_DESIGN_PARTS; part++ )
    {
        int want = design_body_part_id(design, part);
        design->parts[part] = -1;
        for( int kit = 0; kit < design->kit_count; kit++ )
        {
            if( design_kit_matches(provider, kit, want) )
            {
                design->parts[part] = kit;
                break;
            }
        }
    }
}

int
RS_IdkDesign_EnsureResolved(
    struct RS_IdkDesign* design,
    struct CacheProvider* provider)
{
    assert(design && provider);
    if( design->kit_count > 0 )
        return 1;
    /* The idk configs arrive with the design interface's own asset load, so
     * the first caller after the mount is the one that resolves the table —
     * whether that is the preview's rebuild or an early button click. */
    if( RS_IdkDesign_ResolveKitCount(design, provider) <= 0 )
        return 0;
    RS_IdkDesign_Validate(design, provider);
    return 1;
}

int
RS_IdkDesign_ColourCount(int part)
{
    return PlayerModel_DesignColourCount(part);
}

int
RS_IdkDesign_LoadRequestAdd(
    struct RS_IdkDesign* design,
    int model_id)
{
    assert(design);
    for( int i = 0; i < design->load_requested_count; i++ )
    {
        if( design->load_requested[i] == model_id )
            return 0;
    }
    /* Window full: fall through and let the caller re-queue. A duplicate load
     * is wasteful but correct, and only happens past 64 distinct models. */
    if( design->load_requested_count >= RS_IDK_DESIGN_LOAD_TRACK_MAX )
        return 1;
    design->load_requested[design->load_requested_count++] = model_id;
    return 1;
}

/* Step the kit id one at a time (wrapping over the whole IdkType table) until
 * one is selectable and belongs to this part+gender. @see the header for why
 * this walk and an index into a filtered list are the same sequence. */
int
RS_IdkDesign_StepKitId(
    struct CacheProvider* provider,
    int kit_count,
    int body_part_id,
    int kit,
    int step)
{
    assert(provider);
    assert(step == 1 || step == -1);

    /* Reference guards the whole walk on `kit !== -1`: with no kit to stand on
     * there is nothing to step from. */
    if( kit < 0 || kit_count <= 0 )
        return -1;

    /* The reference loops unbounded — it relies on the part always having at
     * least one match (the kit it is standing on). Bound it at one full lap so
     * a cache whose kits do not satisfy that cannot hang the client. */
    for( int i = 0; i < kit_count; i++ )
    {
        kit += step;
        if( kit < 0 )
            kit = kit_count - 1;
        else if( kit >= kit_count )
            kit = 0;

        if( design_kit_matches(provider, kit, body_part_id) )
            return kit;
    }
    return -1;
}

/* Reference clientButton, CC_CHANGE_HEAD_L..CC_CHANGE_FEET_R. */
static void
design_cycle_part(
    struct RS_IdkDesign* design,
    struct CacheProvider* provider,
    int part,
    int direction)
{
    int next = RS_IdkDesign_StepKitId(
        provider,
        design->kit_count,
        design_body_part_id(design, part),
        design->parts[part],
        direction == 0 ? -1 : 1);

    if( next < 0 )
        return;
    design->parts[part] = next;
    design->redraw = 1;
    design->load_requested_count = 0;
}

static void
design_cycle_colour(
    struct RS_IdkDesign* design,
    int part,
    int direction)
{
    int count = RS_IdkDesign_ColourCount(part);
    int colour = design->colours[part];

    if( count <= 0 )
        return;

    if( direction == 0 )
    {
        colour--;
        if( colour < 0 )
            colour = count - 1;
    }
    else
    {
        colour++;
        if( colour >= count )
            colour = 0;
    }

    design->colours[part] = colour;
    design->redraw = 1;
}

int
RS_IdkDesign_Button(
    struct RS_IdkDesign* design,
    struct CacheProvider* provider,
    int client_code)
{
    assert(design && provider);

    if( client_code >= RS_CC_CHANGE_HEAD_L && client_code <= RS_CC_SWITCH_TO_FEMALE )
        RS_IdkDesign_EnsureResolved(design, provider);

    if( client_code >= RS_CC_CHANGE_HEAD_L && client_code <= RS_CC_CHANGE_FEET_R )
    {
        design_cycle_part(
            design,
            provider,
            (client_code - RS_CC_CHANGE_HEAD_L) / 2,
            client_code & 1);
        return 1;
    }

    if( client_code >= RS_CC_RECOLOUR_HAIR_L && client_code <= RS_CC_RECOLOUR_SKIN_R )
    {
        design_cycle_colour(
            design, (client_code - RS_CC_RECOLOUR_HAIR_L) / 2, client_code & 1);
        return 1;
    }

    /* Reference only re-validates on an actual change, so re-clicking the
     * already-selected gender does not reset the parts to their defaults. */
    if( client_code == RS_CC_SWITCH_TO_MALE )
    {
        if( design->gender != 0 )
        {
            design->gender = 0;
            RS_IdkDesign_Validate(design, provider);
        }
        return 1;
    }
    if( client_code == RS_CC_SWITCH_TO_FEMALE )
    {
        if( design->gender != 1 )
        {
            design->gender = 1;
            RS_IdkDesign_Validate(design, provider);
        }
        return 1;
    }

    return 0;
}
