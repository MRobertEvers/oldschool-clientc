#include "features/feature_facts.h"

#include <assert.h>
#include <string.h>

void
ToriRS_FeatureFacts_LightFromTable(
    struct ToriRS_FeatureFacts* facts,
    struct ToriRS_FeatureTable const* table)
{
    assert(facts);
    assert(table);
    facts->npc_light_uses_type_ambient_contrast = table->npc_light_uses_type_ambient_contrast;
    facts->player_head_light_ambient = table->player_head_light_ambient;
}

void
ToriRS_FeatureFacts_ResolveLight(
    struct ToriRS_FeatureFacts* facts,
    struct ToriRS_FeatureTable const* table,
    int npc_type_ambient_contrast_set,
    int npc_type_ambient_contrast,
    int player_head_ambient_set,
    int player_head_ambient)
{
    ToriRS_FeatureFacts_LightFromTable(facts, table);
    if( npc_type_ambient_contrast_set )
        facts->npc_light_uses_type_ambient_contrast = npc_type_ambient_contrast;
    if( player_head_ambient_set )
        facts->player_head_light_ambient = player_head_ambient;
}

int
ToriRS_FeatureFacts_NpcTypeAmbientContrast(struct ToriRS_FeatureFacts const* facts)
{
    assert(facts);
    return facts->npc_light_uses_type_ambient_contrast;
}

int
ToriRS_FeatureFacts_PlayerHeadAmbient(struct ToriRS_FeatureFacts const* facts)
{
    assert(facts);
    return facts->player_head_light_ambient;
}

bool
ToriRS_FeatureFacts_SetMouseoverText(
    struct ToriRS_FeatureFacts* facts,
    char const* value)
{
    assert(facts);
    assert(value);

    facts->mouseover_text_by_cache = false;
    if( strcmp(value, "cache") == 0 )
        facts->mouseover_text_by_cache = true;
    else if( value[0] && strcmp(value, "client") != 0 )
        return false;
    return true;
}

bool
ToriRS_FeatureFacts_MouseoverByCache(struct ToriRS_FeatureFacts const* facts)
{
    assert(facts);
    return facts->mouseover_text_by_cache;
}
