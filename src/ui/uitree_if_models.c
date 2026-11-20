#include "ui/uitree_if_models.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <string.h>

void
UIIfModels_Free(struct UIIfModels* models)
{
    if( !models )
        return;
    ToriMem_Free(ToriMem_Process(), models->heads);
    ToriMem_Free(ToriMem_Process(), models->player_models);
    memset(models, 0, sizeof(*models));
}

int
UIIfModels_HeadIndex(
    struct UIIfModels const* models,
    int com_id)
{
    assert(models);
    for( int i = 0; i < models->head_count; i++ )
        if( models->heads[i].com_id == com_id )
            return i;
    return -1;
}

int
UIIfModels_SetHead(
    struct UIIfModels* models,
    int com_id,
    enum UIIfHeadKind kind,
    int npc_id,
    int zoom,
    uint32_t close_serial)
{
    int i;

    assert(models);
    i = UIIfModels_HeadIndex(models, com_id);
    if( i < 0 )
    {
        i = models->head_count;
        if( models->head_count == models->head_cap )
        {
            int cap = models->head_cap ? models->head_cap * 2 : 16;
            models->heads =
                ToriMem_Realloc(ToriMem_Process(), models->heads, (size_t)cap * sizeof(*models->heads));
            assert(models->heads);
            models->head_cap = cap;
        }
        models->heads[i].anim_id = -1; /* preserved across a head update (below) */
        models->head_count++;
    }
    models->heads[i].com_id = com_id;
    models->heads[i].kind = (int)kind;
    models->heads[i].npc_id = npc_id;
    models->heads[i].zoom = zoom;
    models->heads[i].applied_gen = 0;
    models->heads[i].close_serial = close_serial;
    models->heads[i].landed = 0;
    models->heads[i].scene_id = -1;
    models->heads[i].landed_npc_id = -1;
    models->heads[i].custom_serial = 0;
    models->heads[i].custom_npc_slot = -1;
    models->heads[i].custom_npc_spawn_serial = 0;
    models->heads[i].active_npc_slot = -1;
    models->heads[i].active_npc_spawn_serial = 0;
    models->heads[i].active_npc_id = -1;
    models->heads[i].active_custom_serial = 0;
    models->heads[i].active_mirror = 0;
    return i;
}

int
UIIfModels_HeadFor(
    struct UIIfModels const* models,
    int com_id,
    enum UIIfHeadKind kind,
    int npc_id,
    int zoom,
    uint32_t close_serial)
{
    int i;
    struct UIIfHead const* head;

    assert(models);
    i = UIIfModels_HeadIndex(models, com_id);
    if( i < 0 )
        return -1;
    head = &models->heads[i];
    if( head->kind != (int)kind || head->npc_id != npc_id || head->zoom != zoom ||
        head->close_serial != close_serial || head->landed )
        return -1;
    return i;
}

void
UIIfModels_HeadLanded(
    struct UIIfModels* models,
    int index,
    int scene_id,
    int landed_npc_id)
{
    assert(models);
    assert(index >= 0);
    assert(index < models->head_count);
    models->heads[index].landed = 1;
    models->heads[index].scene_id = scene_id;
    models->heads[index].landed_npc_id = landed_npc_id;
    models->heads[index].applied_gen = 0;
}

void
UIIfModels_RemoveHeadAt(
    struct UIIfModels* models,
    int index)
{
    assert(models);
    assert(index >= 0);
    assert(index < models->head_count);
    models->heads[index] = models->heads[models->head_count - 1];
    models->head_count--;
}

bool
UIIfModels_SetHeadAnim(
    struct UIIfModels* models,
    int com_id,
    int anim_id)
{
    int i;

    assert(models);
    i = UIIfModels_HeadIndex(models, com_id);
    if( i < 0 )
        return false;
    models->heads[i].anim_id = anim_id;
    models->heads[i].applied_gen = 0;
    return true;
}

int
UIIfModels_PlayerModelIndex(
    struct UIIfModels const* models,
    int com_id)
{
    assert(models);
    for( int i = 0; i < models->player_model_count; i++ )
        if( models->player_models[i].com_id == com_id )
            return i;
    return -1;
}

struct UIIfPlayerModel*
UIIfModels_ClaimPlayerModel(
    struct UIIfModels* models,
    int com_id,
    int scene_base,
    uint32_t close_serial)
{
    struct UIIfPlayerModel* model;
    int i;
    int scene_id;

    assert(models);
    assert(UIIfModels_PlayerModelIndex(models, com_id) < 0);

    /* A slot a close freed keeps its scene id; otherwise a new one. */
    for( i = 0; i < models->player_model_count; i++ )
        if( models->player_models[i].com_id < 0 )
            break;
    if( i < models->player_model_count )
    {
        scene_id = models->player_models[i].scene_id;
    }
    else
    {
        if( i == models->player_model_cap )
        {
            int cap = models->player_model_cap ? models->player_model_cap * 2 : 8;
            models->player_models = ToriMem_Realloc(
                ToriMem_Process(), models->player_models, (size_t)cap * sizeof(*models->player_models));
            assert(models->player_models);
            models->player_model_cap = cap;
        }
        scene_id = scene_base + i;
        models->player_model_count++;
    }
    model = &models->player_models[i];
    memset(model, 0, sizeof(*model));
    model->com_id = com_id;
    model->scene_id = scene_id;
    model->anim_id = -1;
    model->close_serial = close_serial;
    return model;
}

void
UIIfModels_FreePlayerModelAt(
    struct UIIfModels* models,
    int index)
{
    assert(models);
    assert(index >= 0);
    assert(index < models->player_model_count);
    models->player_models[index].com_id = -1;
    models->player_models[index].built = 0;
}

bool
UIIfModels_SetPlayerModelAnim(
    struct UIIfModels* models,
    int com_id,
    int anim_id)
{
    int i;

    assert(models);
    i = UIIfModels_PlayerModelIndex(models, com_id);
    if( i < 0 )
        return false;
    models->player_models[i].anim_id = anim_id;
    models->player_models[i].applied_gen = 0;
    return true;
}

void
UIIfModels_Forget(
    struct UIIfModels* models,
    int com_id)
{
    int i;

    assert(models);
    i = UIIfModels_HeadIndex(models, com_id);
    if( i >= 0 )
        UIIfModels_RemoveHeadAt(models, i); /* SetHead keeps one entry per component */
    for( i = 0; i < models->player_model_count; i++ )
        if( models->player_models[i].com_id == com_id )
            UIIfModels_FreePlayerModelAt(models, i);
}

void
UIIfModels_RestoreHeads(
    struct UIIfModels* models,
    struct UIIfHead const* heads,
    int count)
{
    assert(models);
    assert(count >= 0);
    if( count > models->head_cap )
    {
        models->heads = ToriMem_Realloc(ToriMem_Process(), models->heads, (size_t)count * sizeof(models->heads[0]));
        assert(models->heads);
        models->head_cap = count;
    }
    if( count > 0 )
    {
        assert(heads);
        memcpy(models->heads, heads, (size_t)count * sizeof(models->heads[0]));
    }
    models->head_count = count;
}

void
UIIfModels_RestorePlayerModels(
    struct UIIfModels* models,
    struct UIIfPlayerModel const* player_models,
    int count)
{
    assert(models);
    assert(count >= 0);
    if( count > models->player_model_cap )
    {
        models->player_models = ToriMem_Realloc(
            ToriMem_Process(), models->player_models, (size_t)count * sizeof(models->player_models[0]));
        assert(models->player_models);
        models->player_model_cap = count;
    }
    for( int i = count; i < models->player_model_count; i++ )
        UIIfModels_FreePlayerModelAt(models, i);
    if( count > 0 )
    {
        assert(player_models);
        memcpy(models->player_models, player_models, (size_t)count * sizeof(models->player_models[0]));
    }
    if( count > models->player_model_count )
        models->player_model_count = count;
}

bool
UIIfModels_LocalPlayerChanged(
    struct UIIfModels const* models,
    int const slots[12],
    int const colors[5],
    int gender)
{
    struct UIIfLocalPlayerModel const* built;

    assert(models);
    assert(slots);
    assert(colors);
    built = &models->local_player;
    return !built->built || built->gender != gender ||
           memcmp(built->slots, slots, sizeof(built->slots)) != 0 ||
           memcmp(built->colors, colors, sizeof(built->colors)) != 0;
}

void
UIIfModels_LocalPlayerBuilt(
    struct UIIfModels* models,
    int const slots[12],
    int const colors[5],
    int gender)
{
    assert(models);
    assert(slots);
    assert(colors);
    memcpy(models->local_player.slots, slots, sizeof(models->local_player.slots));
    memcpy(models->local_player.colors, colors, sizeof(models->local_player.colors));
    models->local_player.gender = gender;
    models->local_player.built = 1;
}
