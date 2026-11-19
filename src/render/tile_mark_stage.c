#include "render/tile_mark_stage.h"

#include <assert.h>

int
TileMarkStage_Key(
    int scene_x,
    int scene_z,
    int level)
{
    assert(scene_x >= 0);
    assert(scene_x < TILE_MARK_STAGE_SCENE_MAX);
    assert(scene_z >= 0);
    assert(scene_z < TILE_MARK_STAGE_SCENE_MAX);
    assert(level >= 0);
    assert(level < TILE_MARK_STAGE_LEVELS);
    return ((scene_x * TILE_MARK_STAGE_SCENE_MAX) + scene_z) * TILE_MARK_STAGE_LEVELS + level;
}

void
TileMarkStage_Reset(struct TileMarkStage* stage)
{
    assert(stage);
    /* The counts, not the arrays: nothing reads past `count`. */
    stage->count = 0;
    stage->overflow = 0;
    stage->unplaced = 0;
    stage->open = 0;
    stage->open_key = 0;
    stage->clip_x = 0;
    stage->clip_y = 0;
    stage->clip_w = 0;
    stage->clip_h = 0;
}

void
TileMarkStage_Begin(
    struct TileMarkStage* stage,
    int key,
    int clip_x,
    int clip_y,
    int clip_w,
    int clip_h)
{
    assert(stage);
    assert(!stage->open);
    stage->open = 1;
    stage->open_key = key;
    stage->clip_x = clip_x;
    stage->clip_y = clip_y;
    stage->clip_w = clip_w;
    stage->clip_h = clip_h;
}

void
TileMarkStage_End(struct TileMarkStage* stage)
{
    assert(stage);
    assert(stage->open);
    stage->open = 0;
}

bool
TileMarkStage_IsOpen(struct TileMarkStage const* stage)
{
    assert(stage);
    return stage->open != 0;
}

bool
TileMarkStage_Push(
    struct TileMarkStage* stage,
    struct UITreeEntityOverlay const* item)
{
    struct ToriRS_WorldTileMark* mark;

    assert(stage);
    assert(item);
    assert(stage->open);
    if( stage->count >= TILE_MARK_STAGE_MAX )
    {
        stage->overflow = 1;
        return false;
    }
    mark = &stage->marks[stage->count];
    mark->after_command = TILE_MARK_STAGE_UNPLACED;
    mark->item = *item;
    mark->item.clip_x = stage->clip_x;
    mark->item.clip_y = stage->clip_y;
    mark->item.clip_w = stage->clip_w;
    mark->item.clip_h = stage->clip_h;
    stage->keys[stage->count] = stage->open_key;
    stage->count++;
    return true;
}

int
TileMarkStage_Count(struct TileMarkStage const* stage)
{
    assert(stage);
    return stage->count;
}

struct ToriRS_WorldTileMark const*
TileMarkStage_Marks(struct TileMarkStage const* stage)
{
    assert(stage);
    return stage->marks;
}

int
TileMarkStage_PlaceBegin(
    struct TileMarkStage const* stage,
    struct TileMarkPlacement* placement)
{
    assert(stage);
    assert(placement);
    placement->tile_count = 0;
    for( int i = 0; i < stage->count; i++ )
    {
        int at = 0;

        while( at < placement->tile_count && placement->tile_key[at] != stage->keys[i] )
            at++;
        if( at == placement->tile_count )
        {
            placement->tile_key[placement->tile_count] = stage->keys[i];
            placement->tile_at[placement->tile_count] = TILE_MARK_STAGE_UNPLACED;
            placement->tile_count++;
        }
    }
    return placement->tile_count;
}

void
TileMarkPlacement_Saw(
    struct TileMarkPlacement* placement,
    int command_index,
    int key)
{
    assert(placement);
    /* LAST wins, deliberately: a tile can put down several storeys of ground
     * in one pass and a marker emitted after the first of them would be
     * painted over by the rest. */
    for( int i = 0; i < placement->tile_count; i++ )
        if( placement->tile_key[i] == key )
            placement->tile_at[i] = command_index;
}

void
TileMarkStage_PlaceFinish(
    struct TileMarkStage* stage,
    struct TileMarkPlacement const* placement)
{
    assert(stage);
    assert(placement);

    for( int i = 0; i < stage->count; i++ )
    {
        int at = 0;

        while( at < placement->tile_count && placement->tile_key[at] != stage->keys[i] )
            at++;
        assert(at < placement->tile_count);
        stage->marks[i].after_command = placement->tile_at[at];
        if( placement->tile_at[at] == TILE_MARK_STAGE_UNPLACED )
            stage->unplaced++;
    }

    /* Insertion sort, because it is stable: see the header. */
    for( int i = 1; i < stage->count; i++ )
    {
        struct ToriRS_WorldTileMark mark = stage->marks[i];
        int key = stage->keys[i];
        int j = i - 1;

        while( j >= 0 && stage->marks[j].after_command > mark.after_command )
        {
            stage->marks[j + 1] = stage->marks[j];
            stage->keys[j + 1] = stage->keys[j];
            j--;
        }
        stage->marks[j + 1] = mark;
        stage->keys[j + 1] = key;
    }
}
