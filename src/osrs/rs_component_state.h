#ifndef RS_COMPONENT_STATE_H
#define RS_COMPONENT_STATE_H

#include <stdbool.h>
#include <stdint.h>

struct DashSprite;

struct RSLayerDynState
{
    int scroll_position;
    bool hidden;
    struct DashSprite* cached_sprite;
};

struct RSModelDynState
{
    int seq_frame;
    int seq_cycle;
    struct DashSprite* cached_sprite;
};

struct RSTextDynState
{
    char* text_override;
    int active_state; /* 0 = use config; 1 = forced active */
    struct DashSprite* cached_sprite;
};

struct RSInvDynState
{
    struct DashSprite* cached_sprite;
};

struct RSComponentStatePool
{
    struct RSLayerDynState* layer;
    struct RSModelDynState* model;
    struct RSTextDynState* text;
    struct RSInvDynState* inv;
    int count;
};

struct BuildCacheDat;

void
rs_component_state_seed_from_buildcachedat(
    struct RSComponentStatePool* pool,
    struct BuildCacheDat* buildcachedat);

struct RSComponentStatePool*
rs_component_state_pool_new(int max_component_id_exclusive);

void
rs_component_state_pool_free(struct RSComponentStatePool* pool);

struct RSLayerDynState*
rs_layer_state(struct RSComponentStatePool* pool, int component_id);

struct RSModelDynState*
rs_model_state(struct RSComponentStatePool* pool, int component_id);

struct RSTextDynState*
rs_text_state(struct RSComponentStatePool* pool, int component_id);

struct RSInvDynState*
rs_inv_state(struct RSComponentStatePool* pool, int component_id);

#endif
