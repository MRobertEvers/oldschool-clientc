#include "game/rs_sailing_hud.h"

#include "revconfig/revconfig_refs.h"

#include <assert.h>
#include <stdio.h>

void
RS_SailingHud_Reset(
    struct RS_SailingHud* hud,
    struct RevConfigRefs const* refs)
{
    assert(hud);
    assert(refs);
    hud->ids.at_helm_varbit =
        RevConfigRefs_Get(refs, "varbit", "varb19205_sailing_player_at_helm");
    hud->ids.captain_role_varbit =
        RevConfigRefs_Get(refs, "varbit", "varb19233_sailing_captain_role");
    for( int slot = 0; slot < RS_SAILING_HUD_CREW_SLOTS; ++slot )
    {
        char key[48];
        snprintf(key, sizeof(key), "sailing_crew_duty_%d", slot + 1);
        hud->ids.crew_duty_varbit[slot] = RevConfigRefs_Get(refs, "varbit", key);
        snprintf(key, sizeof(key), "sailing_crew_roster_%d", slot + 1);
        hud->ids.crew_roster_varbit[slot] = RevConfigRefs_Get(refs, "varbit", key);
    }
    hud->ids.crew_category = RevConfigRefs_Get(refs, "category", "sailing_crew");
    hud->ids.arrow_model[RS_SAILING_HUD_ARROW_HOVER] =
        RevConfigRefs_Get(refs, "model", "sailing_heading_hover");
    hud->ids.arrow_model[RS_SAILING_HUD_ARROW_SELECTED] =
        RevConfigRefs_Get(refs, "model", "sailing_heading_selected");
    for( int i = 0; i < RS_SAILING_HUD_ARROWS; ++i )
        hud->arrow_element[i] = -1;
    hud->selected_heading = 0;
    hud->selected_until = 0;
}

void
RS_SailingHud_Select(
    struct RS_SailingHud* hud,
    int heading,
    uint64_t cycle)
{
    assert(hud);
    hud->selected_heading = heading;
    hud->selected_until = cycle + RS_SAILING_HUD_SELECTED_CYCLES;
}

int
RS_SailingHud_SelectedHeading(
    struct RS_SailingHud const* hud,
    uint64_t cycle)
{
    assert(hud);
    return cycle < hud->selected_until ? hud->selected_heading : -1;
}

void
RS_SailingHud_SelectionSave(
    struct RS_SailingHud const* hud,
    uint64_t cycle,
    int* out_heading,
    uint64_t* out_remaining)
{
    assert(hud);
    assert(out_heading);
    assert(out_remaining);
    *out_heading = hud->selected_heading;
    *out_remaining = hud->selected_until > cycle ? hud->selected_until - cycle : 0;
}

void
RS_SailingHud_SelectionRestore(
    struct RS_SailingHud* hud,
    uint64_t cycle,
    int heading,
    uint64_t remaining)
{
    assert(hud);
    hud->selected_heading = heading;
    hud->selected_until = cycle + remaining;
}

int
RS_SailingHud_ArrowElement(
    struct RS_SailingHud const* hud,
    int arrow)
{
    assert(hud);
    assert(arrow >= 0);
    assert(arrow < RS_SAILING_HUD_ARROWS);
    return hud->arrow_element[arrow];
}

void
RS_SailingHud_ArrowElementSet(
    struct RS_SailingHud* hud,
    int arrow,
    int element)
{
    assert(hud);
    assert(arrow >= 0);
    assert(arrow < RS_SAILING_HUD_ARROWS);
    hud->arrow_element[arrow] = element;
}

int
RS_SailingHud_ArrowElementsClaim(
    struct RS_SailingHud const* hud,
    int* out,
    int max)
{
    int n = 0;

    assert(hud);
    assert(out);
    assert(max >= 0);
    for( int i = 0; i < RS_SAILING_HUD_ARROWS && n < max; ++i )
        if( hud->arrow_element[i] >= 0 )
            out[n++] = hud->arrow_element[i];
    return n;
}
