#ifndef SRC_FEATURES_FEATURE_FACTS_H
#define SRC_FEATURES_FEATURE_FACTS_H

/*
 * Feature behaviour this boot resolved from more than the feature table: the
 * two model-lighting regimes after the `[render:light]` overrides, and who
 * draws the mouseover line.
 *
 * Lighting: the era's values first, then each override the manifest stated
 * replaces its one value. The table itself is never written, so a manifest
 * override wins without mutating the era. Re-reading the table (a plugin set
 * a lighting flag) takes both values from the table again and drops the
 * overrides.
 *
 * Mouseover line: `[features] mouseover_text=cache` means the cache's
 * clientscripts draw it and the client's own line stands down. "client" or
 * unstated is the client. Anything else is not a value and leaves the client
 * drawing it.
 *
 * This does not read a manifest or a revconfig, light a model or print. The
 * caller passes the table and the stated overrides, reports a mouseover value
 * this refuses, and copies the lighting into whatever else lights models
 * without one of these in hand.
 */

#include "features/features.h"

#include <stdbool.h>

struct ToriRS_FeatureFacts
{
    /** Effective: NPC body lighting adds the npc type's ambient/contrast. */
    int npc_light_uses_type_ambient_contrast;
    /** Effective: absolute ambient for a player chathead; 0 is the scene
     *  regime. */
    int player_head_light_ambient;
    /** The cache's clientscripts draw the mouseover line. */
    bool mouseover_text_by_cache;
};

/** Both lighting values from the table, no override. */
void
ToriRS_FeatureFacts_LightFromTable(
    struct ToriRS_FeatureFacts* facts,
    struct ToriRS_FeatureTable const* table);

/**
 * Both lighting values from the table, then each override whose `_set` is
 * nonzero in its place.
 */
void
ToriRS_FeatureFacts_ResolveLight(
    struct ToriRS_FeatureFacts* facts,
    struct ToriRS_FeatureTable const* table,
    int npc_type_ambient_contrast_set,
    int npc_type_ambient_contrast,
    int player_head_ambient_set,
    int player_head_ambient);

int
ToriRS_FeatureFacts_NpcTypeAmbientContrast(struct ToriRS_FeatureFacts const* facts);

int
ToriRS_FeatureFacts_PlayerHeadAmbient(struct ToriRS_FeatureFacts const* facts);

/**
 * Who draws the mouseover line, from the `[features] mouseover_text` value.
 * Returns false for a value that is neither empty, "client" nor "cache"; the
 * client draws it then.
 */
bool
ToriRS_FeatureFacts_SetMouseoverText(
    struct ToriRS_FeatureFacts* facts,
    char const* value);

bool
ToriRS_FeatureFacts_MouseoverByCache(struct ToriRS_FeatureFacts const* facts);

#endif
