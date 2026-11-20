#include "render/panel_overlay_stage.h"

#include "ui/torirs_chrome_panel_draw.h"

#include <assert.h>

/* Keep the kept list's entries for every owner except `owner`, in order. */
static int
panel_overlay_stage_compact_without(
    struct PanelOverlayStage* stage,
    uint32_t owner)
{
    int out = 0;

    for( int i = 0; i < stage->kept_count; i++ )
    {
        if( stage->kept_owner[i] == owner )
            continue;
        if( out != i )
        {
            stage->kept[out] = stage->kept[i];
            stage->kept_owner[out] = stage->kept_owner[i];
            stage->kept_row[out] = stage->kept_row[i];
        }
        out++;
    }
    return out;
}

bool
PanelOverlayStage_Reset(
    struct PanelOverlayStage* stage,
    uint32_t generation)
{
    bool moved = false;

    assert(stage);
    if( stage->kept_count > 0 || stage->generation != generation )
    {
        PanelOverlayStage_Bump(stage);
        moved = true;
    }
    stage->kept_count = 0;
    stage->staged_count = 0;
    stage->open = 0;
    stage->overflow = 0;
    stage->generation = generation;
    return moved;
}

uint32_t
PanelOverlayStage_Generation(struct PanelOverlayStage const* stage)
{
    assert(stage);
    return stage->generation;
}

uint32_t
PanelOverlayStage_Revision(struct PanelOverlayStage const* stage)
{
    assert(stage);
    return stage->revision;
}

void
PanelOverlayStage_Bump(struct PanelOverlayStage* stage)
{
    assert(stage);
    stage->revision++;
    if( stage->revision == 0 )
        stage->revision++;
}

void
PanelOverlayStage_Begin(
    struct PanelOverlayStage* stage,
    int origin_x,
    int origin_y,
    int scale,
    int clip_x,
    int clip_y,
    int clip_w,
    int clip_h)
{
    assert(stage);
    stage->staged_count = 0;
    stage->overflow = 0;
    stage->open = 1;
    stage->origin_x = origin_x;
    stage->origin_y = origin_y;
    stage->scale = scale;
    stage->clip_x = clip_x;
    stage->clip_y = clip_y;
    stage->clip_w = clip_w;
    stage->clip_h = clip_h;
}

void
PanelOverlayStage_End(struct PanelOverlayStage* stage)
{
    assert(stage);
    stage->open = 0;
    stage->staged_count = 0;
    stage->overflow = 0;
}

bool
PanelOverlayStage_Push(
    struct PanelOverlayStage* stage,
    struct UITreeEntityOverlay const* item)
{
    struct ToriRSChromeRect clip;

    assert(stage);
    assert(item);
    /* No well open is a legitimate state: a panel draw nobody prepared. Its
     * drawing goes nowhere. */
    if( !stage->open )
        return false;
    if( stage->staged_count >= PANEL_OVERLAY_STAGE_MAX )
    {
        stage->overflow = 1;
        return false;
    }
    clip.x = stage->clip_x;
    clip.y = stage->clip_y;
    clip.w = stage->clip_w;
    clip.h = stage->clip_h;
    if( !ToriRSChromePanelDraw_Transform(
            item,
            stage->origin_x,
            stage->origin_y,
            stage->scale,
            clip,
            &stage->staged[stage->staged_count]) )
        return false;
    stage->staged_count++;
    return true;
}

int
PanelOverlayStage_StagedCount(struct PanelOverlayStage const* stage)
{
    assert(stage);
    return stage->open ? stage->staged_count : 0;
}

bool
PanelOverlayStage_Commit(
    struct PanelOverlayStage* stage,
    uint32_t generation,
    uint32_t owner,
    int row)
{
    int other = 0;
    int out;

    assert(stage);
    if( generation == 0 || owner == 0 || generation != stage->generation ||
        stage->overflow || stage->staged_count == 0 )
        return false;

    for( int i = 0; i < stage->kept_count; i++ )
        if( stage->kept_owner[i] != owner )
            other++;
    if( other + stage->staged_count > PANEL_OVERLAY_STAGE_MAX )
        return false;

    /* Compact the other owners only after capacity and identity passed, so a
     * refused commit leaves the last complete picture intact. */
    out = panel_overlay_stage_compact_without(stage, owner);
    for( int i = 0; i < stage->staged_count; i++ )
    {
        stage->kept[out] = stage->staged[i];
        stage->kept_owner[out] = owner;
        stage->kept_row[out] = row;
        out++;
    }
    stage->kept_count = out;
    PanelOverlayStage_Bump(stage);
    return true;
}

bool
PanelOverlayStage_Retire(
    struct PanelOverlayStage* stage,
    uint32_t owner)
{
    int out;

    assert(stage);
    if( owner == 0 )
        return false;
    out = panel_overlay_stage_compact_without(stage, owner);
    if( out == stage->kept_count )
        return false;
    stage->kept_count = out;
    PanelOverlayStage_Bump(stage);
    return true;
}

bool
PanelOverlayStage_Move(
    struct PanelOverlayStage* stage,
    uint32_t owner,
    int dx,
    int dy)
{
    bool moved = false;

    assert(stage);
    if( owner == 0 || (dx == 0 && dy == 0) )
        return false;
    for( int i = 0; i < stage->kept_count; i++ )
    {
        struct UITreeEntityOverlay* item;

        if( stage->kept_owner[i] != owner )
            continue;
        item = &stage->kept[i];
        item->x += dx;
        item->y += dy;
        item->clip_x += dx;
        item->clip_y += dy;
        moved = true;
    }
    if( moved )
        PanelOverlayStage_Bump(stage);
    return moved;
}

int
PanelOverlayStage_KeptCount(struct PanelOverlayStage const* stage)
{
    assert(stage);
    return stage->kept_count;
}

bool
PanelOverlayStage_Kept(
    struct PanelOverlayStage const* stage,
    int index,
    struct UITreeEntityOverlay* out,
    uint32_t* out_owner,
    int* out_row)
{
    assert(stage);
    assert(out);
    assert(out_owner);
    assert(out_row);
    if( index < 0 || index >= stage->kept_count || stage->generation == 0 )
        return false;
    *out = stage->kept[index];
    *out_owner = stage->kept_owner[index];
    *out_row = stage->kept_row[index];
    return true;
}

char const*
PanelOverlayStage_KeptText(
    struct PanelOverlayStage const* stage,
    int index)
{
    assert(stage);
    assert(index >= 0);
    assert(index < stage->kept_count);
    return stage->kept[index].text;
}

int
PanelOverlayStage_StageOwnerLocal(
    struct PanelOverlayStage* stage,
    uint32_t owner,
    int origin_x,
    int origin_y)
{
    int count = 0;

    assert(stage);
    for( int i = 0; i < stage->kept_count; i++ )
    {
        struct UITreeEntityOverlay* item;

        if( stage->kept_owner[i] != owner || count >= PANEL_OVERLAY_STAGE_MAX )
            continue;
        item = &stage->staged[count++];
        *item = stage->kept[i];
        item->x -= origin_x;
        item->y -= origin_y;
        item->clip_x -= origin_x;
        item->clip_y -= origin_y;
    }
    return count;
}

struct UITreeEntityOverlay*
PanelOverlayStage_Staged(struct PanelOverlayStage* stage)
{
    assert(stage);
    return stage->staged;
}
