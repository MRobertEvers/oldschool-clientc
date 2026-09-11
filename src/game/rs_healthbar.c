#include "game/rs_healthbar.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void
RS_Healthbars_Init(struct RS_Healthbars* healthbars)
{
    assert(healthbars);
    memset(healthbars, 0, sizeof(*healthbars));
}

void
RS_Healthbars_Free(struct RS_Healthbars* healthbars)
{
    if( !healthbars )
        return;
    free(healthbars->types);
    healthbars->types = NULL;
    healthbars->count = 0;
}

int
RS_Healthbars_SetTypes(
    struct RS_Healthbars* healthbars,
    struct RS_HealthbarType* types,
    int count)
{
    assert(healthbars);
    if( count <= 0 )
        return 0;
    assert(types);
    free(healthbars->types);
    healthbars->types = types;
    healthbars->count = count;
    return 1;
}

void
RS_Healthbars_Defaults(struct RS_HealthbarType* out)
{
    assert(out);
    out->front_sprite = -1;
    out->back_sprite = -1;
    out->width = RS_HEALTHBAR_DEFAULT_WIDTH;
    out->padding = 0;
    out->persist_cycles = RS_HEALTHBAR_DEFAULT_PERSIST;
    out->fade_threshold = -1;
}

struct RS_HealthbarType const*
RS_Healthbars_TypeFor(
    struct RS_Healthbars const* healthbars,
    int id)
{
    /* Built once and handed out const, so an id the cache does not cover reads
     * as the reference's freshly-constructed record rather than as a hole. */
    static struct RS_HealthbarType fallback;
    static int fallback_ready = 0;

    assert(healthbars);
    if( healthbars->types && id >= 0 && id < healthbars->count )
        return &healthbars->types[id];
    if( !fallback_ready )
    {
        RS_Healthbars_Defaults(&fallback);
        fallback_ready = 1;
    }
    return &fallback;
}
