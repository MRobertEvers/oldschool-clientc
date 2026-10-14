#include "ps_world.h"

#include "app.h"
#include "engine/world_builder/minimap.h"
#include "game/rs_entity_sync.h"
#include "game/rs_healthbar.h"
#include "net/rev/packets/pkt_player_appearance.h"
#include "ps_compose.h"
#include "render/torirs_pick.h"
#include "world/entity_npc.h"
#include "world/entity_objstack.h"
#include "world/entity_player.h"
#include "world/entity_scenery.h"
#include "world/world.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ACTORS 4096
#define ANIM_NONE 0xFFFF

/* The client's design recolours (src/engine/entity_model_build.c, reference
 * ClientPlayer.recol1d / recol2d): index 0 is the identity colour every kit
 * ships with, the player's colour index picks its replacement. */
static const int k_recol_hair[] = { 6798, 107, 10283, 16, 4797, 7744, 5799, 4634,
                                    33697, 22433, 2983, 54193 };
static const int k_recol_torso[] = { 8741, 12, 64030, 43162, 7735, 8404, 1701, 38430,
                                     24094, 10153, 56621, 4783, 1341, 16578, 35003, 25239 };
static const int k_recol_legs[] = { 25238, 8742, 12, 64030, 43162, 7735, 8404, 1701,
                                    38430, 24094, 10153, 56621, 4783, 1341, 16578, 35003 };
static const int k_recol_feet[] = { 4626, 11146, 6439, 12, 4758, 10270 };
static const int k_recol_skin[] = { 4550, 4537, 5681, 5673, 5790, 6806, 8076, 4574 };
static const int k_recol2d[] = { 9104, 10275, 7595, 3610, 7975, 8526, 918, 38802,
                                 24466, 10145, 58654, 5027, 1457, 16565, 34991, 25486 };
static const struct
{
    const int* palette;
    int count;
} k_recol1d[5] = {
    { k_recol_hair, 12 }, { k_recol_torso, 16 }, { k_recol_legs, 16 },
    { k_recol_feet, 6 },  { k_recol_skin, 8 },
};

/* When each actor's current sequence began, so a one-shot (an attack) plays
 * from its first frame and a loop keeps its phase. Keyed by pool and slot. */
struct AnimClock
{
    int anim_id;
    uint64_t start_ms;
};

/* What the last draw put where, for picking. */
struct Drawn
{
    int x0, y0, x1, y1; /* bounds of the pixels actually written */
    int element_id;
    int gx; /* ground point */
    const struct WorldEntityFacet_Combat* combat;
};

struct PS_World
{
    struct PS_Fetch* fetch;
    struct AnimClock npc_clock[4096];
    struct AnimClock player_clock[2048];
    uint32_t* scratch; /* one humanoid cell, composited */
    int scratch_n;
    /* The baked ground, and what it was baked from. */
    uint32_t* ground;
    int ground_w;
    int ground_h;
    const struct Minimap* ground_minimap;
    unsigned ground_seq;
    int ground_level;
    /* The last draw's camera and actors, in draw order. */
    int last_cam_x, last_cam_z, last_cx, last_cy, last_level, last_valid;
    struct Drawn drawn[MAX_ACTORS];
    int drawn_count;
};

enum ActorKind
{
    ACTOR_DECOR = 0, /* ground decor first at equal depth: a flower never covers a foot */
    ACTOR_ITEM = 1,  /* a ground item, on the decor, under anyone standing there */
    ACTOR_LOC = 2,
    ACTOR_NPC = 3,
    ACTOR_PLAYER = 4,
};

struct Actor
{
    int screen_x; /* ground point */
    int screen_y;
    int is_player; /* enum ActorKind */
    int index;
};

/* Loc shapes drawn as sprites: centrepieces (10, and 11, the diagonal one) and
 * ground decor (22). Walls come from the ground bake; roofs are not drawn in a
 * top-down world. */
static int
loc_drawn_shape(int shape)
{
    return shape == 10 || shape == 11 || shape == 22;
}


struct PS_World*
PS_WorldNew(struct PS_Fetch* fetch)
{
    assert(fetch);
    struct PS_World* w = (struct PS_World*)calloc(1, sizeof(*w));
    assert(w);
    w->fetch = fetch;
    return w;
}

void
PS_WorldFree(struct PS_World* w)
{
    if( !w )
        return;
    free(w->scratch);
    free(w->ground);
    free(w);
}

/* ------------------------------------------------------------------ pixels */

static inline uint32_t
shade(
    uint32_t rgb,
    int pct)
{
    int r = (int)((rgb >> 16) & 0xFF) * pct / 100;
    int g = (int)((rgb >> 8) & 0xFF) * pct / 100;
    int b = (int)(rgb & 0xFF) * pct / 100;
    r = r > 255 ? 255 : r;
    g = g > 255 ? 255 : g;
    b = b > 255 ? 255 : b;
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void
fill_rect(
    int* px,
    int stride,
    int x0,
    int y0,
    int x1,
    int y1,
    int cx0,
    int cy0,
    int cx1,
    int cy1,
    uint32_t argb)
{
    if( x0 < cx0 )
        x0 = cx0;
    if( y0 < cy0 )
        y0 = cy0;
    if( x1 > cx1 )
        x1 = cx1;
    if( y1 > cy1 )
        y1 = cy1;
    for( int y = y0; y < y1; y++ )
        for( int x = x0; x < x1; x++ )
            px[y * stride + x] = (int)argb;
}

/* A translucent dark ellipse under an actor: the overworld shadow. */
static void
shadow(
    int* px,
    int stride,
    int gx,
    int gy,
    int rx,
    int cx0,
    int cy0,
    int cx1,
    int cy1)
{
    int ry = rx / 3 > 2 ? rx / 3 : 2;
    for( int y = gy - ry; y <= gy + ry; y++ )
        for( int x = gx - rx; x <= gx + rx; x++ )
        {
            if( x < cx0 || y < cy0 || x >= cx1 || y >= cy1 )
                continue;
            long dx = x - gx, dy = y - gy;
            if( dx * dx * ry * ry + dy * dy * rx * rx > (long)rx * rx * ry * ry )
                continue;
            uint32_t p = (uint32_t)px[y * stride + x];
            px[y * stride + x] = (int)shade(p, 72);
        }
}

/* Blit a cell with 0/255 alpha so (ax, ay) in the cell lands on (sx, sy),
 * growing `bb` (x0, y0, x1, y1) by every pixel written. */
static void
blit_cell(
    int* bb,
    int* px,
    int stride,
    const uint32_t* cell,
    int cell_stride,
    int cw,
    int ch,
    int sx,
    int sy,
    int ax,
    int ay,
    int cx0,
    int cy0,
    int cx1,
    int cy1)
{
    for( int y = 0; y < ch; y++ )
    {
        int dy = sy - ay + y;
        if( dy < cy0 || dy >= cy1 )
            continue;
        for( int x = 0; x < cw; x++ )
        {
            int dx = sx - ax + x;
            if( dx < cx0 || dx >= cx1 )
                continue;
            uint32_t p = cell[(size_t)y * (size_t)cell_stride + (size_t)x];
            if( (p >> 24) >= 128 )
            {
                px[dy * stride + dx] = (int)(0xFF000000u | p);
                if( dx < bb[0] )
                    bb[0] = dx;
                if( dy < bb[1] )
                    bb[1] = dy;
                if( dx + 1 > bb[2] )
                    bb[2] = dx + 1;
                if( dy + 1 > bb[3] )
                    bb[3] = dy + 1;
            }
        }
    }
}

/* -------------------------------------------------------------------- tiles */

/*
 * The ground is the minimap's own bake (minimap_bake_argb: the same overlay
 * shape masks, walls and see-through rules the minimap draws), 4 pixels a tile,
 * enlarged to PS_TILE_PX. Re-baked only when the map changes (minimap_seq) or
 * the level does -- never per frame.
 */
/* The bake's own wall and door inks (minimap.c minimap_draw_wall_tile). */
#define BAKE_WALL 0xFFFFFFFFu
#define BAKE_DOOR 0xFFEE0000u
#define GROUND_WALL 0xFF3A3040u /* a dark fence line, not the minimap's white */
#define GROUND_DOOR 0xFF9A5A32u /* a wooden door */

/* The minimap's colours are the OSRS ground's, which read muddy at this size;
 * a Pokemon overworld is brighter and more saturated. */
static uint32_t
ground_colour(uint32_t argb)
{
    if( argb == BAKE_WALL )
        return GROUND_WALL;
    if( argb == BAKE_DOOR )
        return GROUND_DOOR;
    float r = (float)((argb >> 16) & 0xFF) / 255.0f;
    float g = (float)((argb >> 8) & 0xFF) / 255.0f;
    float b = (float)(argb & 0xFF) / 255.0f;
    float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    if( mx <= 0.0f )
        return argb;
    /* Push each channel away from the grey axis (saturation x1.3), then
     * brighten by 12%, clamped. */
    float grey = (mx + mn) * 0.5f;
    float k = 1.30f, lift = 1.12f;
    float rgb[3] = { r, g, b };
    uint32_t out = 0xFF000000u;
    for( int i = 0; i < 3; i++ )
    {
        float v = (grey + (rgb[i] - grey) * k) * lift;
        v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        out |= (uint32_t)(v * 255.0f + 0.5f) << (16 - 8 * i);
    }
    return out;
}

static void
ensure_ground(
    struct PS_World* w,
    struct World* world,
    int level)
{
    if( w->ground && w->ground_minimap == world->minimap && w->ground_seq == world->minimap_seq &&
        w->ground_level == level )
        return;
    free(w->ground);
    w->ground = NULL;
    w->ground_minimap = world->minimap;
    w->ground_seq = world->minimap_seq;
    w->ground_level = level;
    if( !world->minimap )
        return;
    w->ground = minimap_bake_argb(world->minimap, level, world->tile_flags, &w->ground_w,
                                  &w->ground_h);
    if( w->ground )
        for( int i = 0; i < w->ground_w * w->ground_h; i++ )
            w->ground[i] = ground_colour(w->ground[i]);
}

static void
draw_tiles(
    struct PS_World* w,
    struct World* world,
    int level,
    int* px,
    int stride,
    int cx0,
    int cy0,
    int cx1,
    int cy1,
    int cx,
    int cy,
    int cam_x,
    int cam_z)
{
    ensure_ground(w, world, level);
    if( !w->ground )
    {
        fill_rect(px, stride, cx0, cy0, cx1, cy1, cx0, cy0, cx1, cy1, 0xFF101418u);
        return;
    }
    int const units_h = w->ground_h * 32; /* 4 bake pixels a 128-unit tile */
    for( int sy = cy0; sy < cy1; sy++ )
    {
        /* World z grows north; the bake's rows and the screen's grow down. */
        int wz = cam_z - (sy - cy) * 128 / PS_TILE_PX;
        int my = (units_h - 1 - wz) >> 5;
        int ty = ((sy - cy) * 128 / PS_TILE_PX + cam_z) & 127;
        for( int sx = cx0; sx < cx1; sx++ )
        {
            int wx = cam_x + (sx - cx) * 128 / PS_TILE_PX;
            int mx = wx >> 5;
            if( wx < 0 || wz < 0 || mx >= w->ground_w || my < 0 || my >= w->ground_h )
            {
                px[sy * stride + sx] = (int)0xFF101418u;
                continue;
            }
            uint32_t c = w->ground[my * w->ground_w + mx];
            /* A soft checker of 4-pixel cells inside each tile, so open ground
             * reads as ground rather than a flat swatch. */
            int tx = wx & 127;
            int pct = (((tx >> 4) + (ty >> 4)) & 1) ? 100 : 95;
            px[sy * stride + sx] = (int)shade(c & 0xFFFFFFu, pct);
        }
    }
}

/* ----------------------------------------------------------------- overlays */

/*
 * The 3D client projects its health bars and hitsplats with its camera; this
 * one hangs them off each sprite's drawn bounds: a Pokemon-style HP bar over
 * the head, damage in a small pixel font on the body. Timing is the app's own
 * (world cycle, the entity's combat facet), so they appear and expire with it.
 */

/* 3x5 digits, one row per byte, bit 2 = leftmost column. */
static const uint8_t k_digits[10][5] = {
    { 7, 5, 5, 5, 7 }, { 2, 6, 2, 2, 7 }, { 7, 1, 7, 4, 7 }, { 7, 1, 7, 1, 7 },
    { 5, 5, 7, 1, 1 }, { 7, 4, 7, 1, 7 }, { 7, 4, 7, 5, 7 }, { 7, 1, 1, 1, 1 },
    { 7, 5, 7, 5, 7 }, { 7, 5, 7, 1, 7 },
};

static void
put(
    int* px,
    int stride,
    int x,
    int y,
    uint32_t argb,
    int cx0,
    int cy0,
    int cx1,
    int cy1)
{
    if( x >= cx0 && x < cx1 && y >= cy0 && y < cy1 )
        px[y * stride + x] = (int)argb;
}

static void
draw_number(
    int* px,
    int stride,
    int value,
    int cx,
    int cy,
    int cx0,
    int cy0,
    int cx1,
    int cy1)
{
    char buf[8];
    int n = snprintf(buf, sizeof(buf), "%d", value);
    int w = n * 4 - 1;
    int x0 = cx - w / 2, y0 = cy - 2;
    /* Dark outline first, then the white glyphs over it. */
    for( int pass = 0; pass < 2; pass++ )
        for( int i = 0; i < n; i++ )
            for( int r = 0; r < 5; r++ )
                for( int c = 0; c < 3; c++ )
                {
                    if( !(k_digits[buf[i] - '0'][r] & (4 >> c)) )
                        continue;
                    int x = x0 + i * 4 + c, y = y0 + r;
                    if( pass == 0 )
                    {
                        for( int dy = -1; dy <= 1; dy++ )
                            for( int dx = -1; dx <= 1; dx++ )
                                put(px, stride, x + dx, y + dy, 0xFF1A1420u, cx0, cy0, cx1, cy1);
                    }
                    else
                        put(px, stride, x, y, 0xFFFFFFFFu, cx0, cy0, cx1, cy1);
                }
}

static void
draw_splat(
    int* px,
    int stride,
    int x,
    int y,
    int value,
    int cx0,
    int cy0,
    int cx1,
    int cy1)
{
    /* A zero is a block or a miss: blue. Anything else is damage: red. */
    uint32_t fill = value == 0 ? 0xFF3A6ED8u : 0xFFD83A3Au;
    int r = 7;
    for( int dy = -r - 1; dy <= r + 1; dy++ )
        for( int dx = -r - 1; dx <= r + 1; dx++ )
        {
            int d = dx * dx + dy * dy;
            if( d <= r * r )
                put(px, stride, x + dx, y + dy, fill, cx0, cy0, cx1, cy1);
            else if( d <= (r + 1) * (r + 1) )
                put(px, stride, x + dx, y + dy, 0xFF1A1420u, cx0, cy0, cx1, cy1);
        }
    draw_number(px, stride, value, x, y, cx0, cy0, cx1, cy1);
}

static int
bar_fill(
    struct App* app,
    const struct WorldEntityFacet_Combat* c,
    int cycle,
    float* out_frac)
{
    if( c->healthbar_type >= 0 )
    {
        if( cycle >= c->healthbar_end_cycle )
            return 0;
        const struct RS_HealthbarType* t = RS_Healthbars_TypeFor(&app->healthbars, c->healthbar_type);
        int width = t && t->width > 0 ? t->width : RS_HEALTHBAR_DEFAULT_WIDTH;
        int fill;
        if( cycle < c->healthbar_start_cycle )
            fill = c->healthbar_held_fill >= 0 ? c->healthbar_held_fill : c->healthbar_start_fill;
        else if( c->healthbar_duration > 0 &&
                 cycle < c->healthbar_start_cycle + c->healthbar_duration )
            fill = c->healthbar_start_fill + (c->healthbar_end_fill - c->healthbar_start_fill) *
                                                 (cycle - c->healthbar_start_cycle) /
                                                 c->healthbar_duration;
        else
            fill = c->healthbar_end_fill;
        *out_frac = (float)fill / (float)width;
        return 1;
    }
    /* A dat1 session's legacy bar: hitpoints, shown while in combat. */
    if( c->combat_cycle > cycle && c->total_health > 0 )
    {
        *out_frac = (float)c->health / (float)c->total_health;
        return 1;
    }
    return 0;
}

static void
draw_overlays(
    struct PS_World* w,
    struct App* app,
    int* px,
    int stride,
    int cx0,
    int cy0,
    int cx1,
    int cy1)
{
    int const cycle = app->world->cycle;
    for( int i = 0; i < w->drawn_count; i++ )
    {
        const struct Drawn* d = &w->drawn[i];
        const struct WorldEntityFacet_Combat* c = d->combat;
        if( !c )
            continue;
        float frac;
        if( bar_fill(app, c, cycle, &frac) )
        {
            frac = frac < 0.0f ? 0.0f : (frac > 1.0f ? 1.0f : frac);
            int bw = 30, bx = d->gx - bw / 2, by = d->y0 - 7;
            uint32_t col = frac > 0.5f ? 0xFF48D058u : (frac > 0.2f ? 0xFFF0C030u : 0xFFE04030u);
            fill_rect(px, stride, bx - 1, by - 1, bx + bw + 1, by + 5, cx0, cy0, cx1, cy1,
                      0xFF1A1420u);
            fill_rect(px, stride, bx, by, bx + bw, by + 4, cx0, cy0, cx1, cy1, 0xFF505860u);
            fill_rect(px, stride, bx, by, bx + (int)(bw * frac + 0.5f), by + 4, cx0, cy0, cx1, cy1,
                      col);
            fill_rect(px, stride, bx, by, bx + (int)(bw * frac + 0.5f), by + 1, cx0, cy0, cx1,
                      cy1, shade(col & 0xFFFFFFu, 125));
        }
        /* Up to four at once, laid out like the reference: centre, above,
         * left, right of the body's middle. */
        static const int off[WORLD_ENTITY_DAMAGE_SLOTS][2] = { { 0, 0 }, { 0, -16 }, { -15, -8 },
                                                               { 15, -8 } };
        int my = (d->y0 + d->y1) / 2;
        for( int s = 0; s < WORLD_ENTITY_DAMAGE_SLOTS; s++ )
        {
            if( c->damage_start_cycles[s] > cycle || c->damage_cycles[s] <= cycle )
                continue;
            draw_splat(px, stride, d->gx + off[s][0], my + off[s][1], c->damage_values[s], cx0,
                       cy0, cx1, cy1);
        }
    }
}

/* ------------------------------------------------------------------- actors */

static int
current_seq(
    const struct WorldEntityFacet_Animation* anim,
    const struct WorldEntityFacet_IdleAnimations* idle)
{
    /* An action slot that was never set reads 0 as well. */
    if( anim->primary.anim_id != ANIM_NONE && anim->primary.anim_id != 0 &&
        anim->primary.delay == 0 )
        return anim->primary.anim_id;
    /* A movement slot that was never set reads 0, not "none". */
    if( anim->secondary.anim_id != ANIM_NONE && anim->secondary.anim_id != 0 )
        return anim->secondary.anim_id;
    return idle->readyanim;
}

static int
clock_ms(
    struct AnimClock* c,
    int seq,
    uint64_t now)
{
    if( c->anim_id != seq )
    {
        c->anim_id = seq;
        c->start_ms = now;
    }
    return (int)(now - c->start_ms);
}

static void
placeholder(
    int* bb,
    int* px,
    int stride,
    int gx,
    int gy,
    uint32_t argb,
    int cx0,
    int cy0,
    int cx1,
    int cy1)
{
    fill_rect(px, stride, gx - 6, gy - 22, gx + 6, gy, cx0, cy0, cx1, cy1, argb);
    bb[0] = gx - 6;
    bb[1] = gy - 22;
    bb[2] = gx + 6;
    bb[3] = gy;
}

static void
draw_npc(
    int* bb,
    struct PS_World* w,
    struct WorldEntity_NPC* npc,
    int idx,
    int gx,
    int gy,
    int* px,
    int stride,
    int cx0,
    int cy0,
    int cx1,
    int cy1,
    uint64_t now)
{
    char key[96];
    enum PS_FetchState st;
    int seq = current_seq(&npc->animation, &npc->idle_animations);
    const struct PS_Sheet* s = NULL;
    if( seq >= 0 )
    {
        snprintf(key, sizeof(key), "npc/%d/%d", npc->npc_id, seq);
        s = PS_FetchGet(w->fetch, key, &st);
    }
    if( !s && npc->idle_animations.readyanim >= 0 && npc->idle_animations.readyanim != seq )
    {
        seq = npc->idle_animations.readyanim;
        snprintf(key, sizeof(key), "npc/%d/%d", npc->npc_id, seq);
        s = PS_FetchGet(w->fetch, key, &st);
    }
    shadow(px, stride, gx, gy, 6 + 5 * (npc->size > 0 ? npc->size : 1), cx0, cy0, cx1, cy1);
    if( !s )
    {
        placeholder(bb, px, stride, gx, gy, 0xFFC04040u, cx0, cy0, cx1, cy1);
        return;
    }
    struct AnimClock* c = &w->npc_clock[idx & 4095];
    int frame = PS_SheetFrameAt(s, clock_ms(c, seq, now), seq != npc->animation.primary.anim_id);
    int facing = PS_FacingFromYaw(npc->orientation.yaw);
    blit_cell(bb, px, stride, PS_SheetCell(s, facing, frame), s->width, s->head.cell_w,
              s->head.cell_h, gx, gy, s->head.ground_x, s->head.ground_y, cx0, cy0, cx1, cy1);
}

static void
draw_player(
    int* bb,
    struct PS_World* w,
    struct WorldEntity_Player* pl,
    int idx,
    int gx,
    int gy,
    int* px,
    int stride,
    int cx0,
    int cy0,
    int cx1,
    int cy1,
    uint64_t now)
{
    struct PS_ComposeInput in;
    memset(&in, 0, sizeof(in));
    int seq = current_seq(&pl->animation, &pl->idle_animations);
    if( seq < 0 )
        seq = 808;
    /* Exactly the appearance slots: a kit, an obj, or nothing. */
    for( int s = 0; s < 12 && in.layer_count < PS_COMPOSE_MAX_LAYERS; s++ )
    {
        int v = pl->appearance.slots[s];
        char key[96];
        char const* g = pl->gender ? "f" : "m";
        if( Appearance_SlotKind(v) == APPEARANCE_SLOT_KIT )
            snprintf(key, sizeof(key), "layer/%s/kit/%d/%d", g, Appearance_SlotKit(v), seq);
        else if( Appearance_SlotKind(v) == APPEARANCE_SLOT_OBJ )
            snprintf(key, sizeof(key), "layer/%s/obj/%d/%d", g, Appearance_SlotObj(v), seq);
        else
            continue;
        enum PS_FetchState st;
        const struct PS_Sheet* sh = PS_FetchGet(w->fetch, key, &st);
        if( sh )
            in.layers[in.layer_count++] = sh;
    }
    for( int part = 0; part < 5; part++ )
    {
        int c = pl->appearance.colors[part];
        if( c > 0 && c < k_recol1d[part].count )
        {
            in.recolours[in.recolour_count].from_hsl16 = (uint16_t)k_recol1d[part].palette[0];
            in.recolours[in.recolour_count++].to_hsl16 = (uint16_t)k_recol1d[part].palette[c];
            if( part == 1 && c < 16 )
            {
                in.recolours[in.recolour_count].from_hsl16 = (uint16_t)k_recol2d[0];
                in.recolours[in.recolour_count++].to_hsl16 = (uint16_t)k_recol2d[c];
            }
        }
    }
    shadow(px, stride, gx, gy, 11, cx0, cy0, cx1, cy1);
    if( in.layer_count == 0 )
    {
        placeholder(bb, px, stride, gx, gy, 0xFF4070C0u, cx0, cy0, cx1, cy1);
        return;
    }
    const struct PS_Sheet* first = in.layers[0];
    int cw = first->head.cell_w, ch = first->head.cell_h;
    if( w->scratch_n < cw * ch )
    {
        free(w->scratch);
        w->scratch = (uint32_t*)malloc((size_t)cw * (size_t)ch * sizeof(uint32_t));
        assert(w->scratch);
        w->scratch_n = cw * ch;
    }
    struct AnimClock* c = &w->player_clock[idx & 2047];
    in.facing = PS_FacingFromYaw(pl->orientation.yaw);
    in.frame = PS_SheetFrameAt(first, clock_ms(c, seq, now), seq != pl->animation.primary.anim_id);
    PS_Compose(&in, w->scratch, cw, ch);
    blit_cell(bb, px, stride, w->scratch, cw, cw, ch, gx, gy, first->head.ground_x,
              first->head.ground_y, cx0, cy0, cx1, cy1);
}

static void
draw_loc(
    int* bb,
    struct PS_World* w,
    struct WorldEntity_Scenery* sc,
    int gx,
    int gy,
    int* px,
    int stride,
    int cx0,
    int cy0,
    int cx1,
    int cy1,
    uint64_t now)
{
    char key[96];
    enum PS_FetchState st;
    /* A loc that never animated reads 0 there, not "none": it is static. */
    int seq = sc->animation.anim_id != ANIM_NONE && sc->animation.anim_id != 0
                  ? sc->animation.anim_id
                  : -1;
    snprintf(key, sizeof(key), "loc/%d/%d/%d", sc->loc_id, sc->shape, seq);
    const struct PS_Sheet* s = PS_FetchGet(w->fetch, key, &st);
    /* No placeholder: a loc with no sheet yet is simply not drawn, rather
     * than a block on every tree. Its miss is logged for the generator. */
    if( !s )
        return;
    /* Locs loop on one shared clock: they have no start to remember. */
    int frame = PS_SheetFrameAt(s, (int)(now % 1000000u), 1);
    blit_cell(bb, px, stride, PS_SheetCell(s, sc->angle & 3, frame), s->width, s->head.cell_w,
              s->head.cell_h, gx, gy, s->head.ground_x, s->head.ground_y, cx0, cy0, cx1, cy1);
}

static void
draw_item(
    int* bb,
    struct PS_World* w,
    struct WorldEntity_ObjStack* st,
    int gx,
    int gy,
    int* px,
    int stride,
    int cx0,
    int cy0,
    int cx1,
    int cy1)
{
    char key[64];
    enum PS_FetchState fs;
    snprintf(key, sizeof(key), "obj/%d", st->obj_id);
    const struct PS_Sheet* s = PS_FetchGet(w->fetch, key, &fs);
    if( !s )
        return;
    blit_cell(bb, px, stride, PS_SheetCell(s, 0, 0), s->width, s->head.cell_w, s->head.cell_h,
              gx, gy, s->head.ground_x, s->head.ground_y, cx0, cy0, cx1, cy1);
}

static int
by_screen_y(
    const void* a,
    const void* b)
{
    const struct Actor* x = (const struct Actor*)a;
    const struct Actor* y = (const struct Actor*)b;
    return x->screen_y != y->screen_y ? x->screen_y - y->screen_y : x->is_player - y->is_player;
}

void
PS_WorldDraw(
    struct PS_World* w,
    struct App* app,
    int* px,
    int stride,
    int x0,
    int y0,
    int x1,
    int y1,
    int cx,
    int cy,
    uint64_t now)
{
    assert(w);
    assert(app);
    assert(px);
    struct World* world = app->world;
    w->drawn_count = 0;
    w->last_valid = 0;
    if( !world )
    {
        fill_rect(px, stride, x0, y0, x1, y1, x0, y0, x1, y1, 0xFF101418u);
        return;
    }

    /* The camera follows the local player; before it exists, the scene centre. */
    int cam_x = world->_scene_size * 64, cam_z = world->_scene_size * 64, level = 0;
    int local_idx = -1;
    if( RS_EntitySync_FindPlayer(&app->esync,
                                 app->esync.local_pid >= 0 ? app->esync.local_pid : 2047,
                                 &local_idx, NULL) )
    {
        struct WorldEntity_Player* me =
            (struct WorldEntity_Player*)World_EntityPoolGet(&world->entities.player, local_idx);
        if( me )
        {
            cam_x = (int)me->draw_position.x;
            cam_z = (int)me->draw_position.z;
            level = me->grid_position.level;
        }
    }
    draw_tiles(w, world, level, px, stride, x0, y0, x1, y1, cx, cy, cam_x, cam_z);
    w->last_cam_x = cam_x;
    w->last_cam_z = cam_z;
    w->last_cx = cx;
    w->last_cy = cy;
    w->last_level = level;
    w->last_valid = 1;

    static struct Actor actors[MAX_ACTORS];
    int n = 0;
    for( int i = World_EntityPoolHead(&world->entities.npc); i >= 0 && n < MAX_ACTORS;
         i = World_EntityPoolNext(&world->entities.npc, i) )
    {
        struct WorldEntity_NPC* npc =
            (struct WorldEntity_NPC*)World_EntityPoolAt(&world->entities.npc, i);
        if( npc->grid_position.level != level || npc->multinpc_hidden )
            continue;
        actors[n].screen_x = cx + ((int)npc->draw_position.x - cam_x) * PS_TILE_PX / 128;
        actors[n].screen_y = cy - ((int)npc->draw_position.z - cam_z) * PS_TILE_PX / 128;
        actors[n].is_player = ACTOR_NPC;
        actors[n++].index = i;
    }
    for( int i = World_EntityPoolHead(&world->entities.scenery); i >= 0 && n < MAX_ACTORS;
         i = World_EntityPoolNext(&world->entities.scenery, i) )
    {
        struct WorldEntity_Scenery* sc =
            (struct WorldEntity_Scenery*)World_EntityPoolAt(&world->entities.scenery, i);
        if( sc->grid_position.level != level || !loc_drawn_shape(sc->shape) )
            continue;
        /* The footprint's centre: where the scene builder centres the model. */
        int fx = sc->grid_position.x * 128 + (sc->size_x > 0 ? sc->size_x : 1) * 64;
        int fz = sc->grid_position.z * 128 + (sc->size_z > 0 ? sc->size_z : 1) * 64;
        int sx = cx + (fx - cam_x) * PS_TILE_PX / 128;
        int sy = cy - (fz - cam_z) * PS_TILE_PX / 128;
        if( sx < x0 - 160 || sx > x1 + 160 || sy < y0 - 32 || sy > y1 + 240 )
            continue;
        actors[n].screen_x = sx;
        actors[n].screen_y = sy;
        actors[n].is_player = sc->shape == 22 ? ACTOR_DECOR : ACTOR_LOC;
        actors[n++].index = i;
    }
    for( int i = World_EntityPoolHead(&world->entities.obj_stack); i >= 0 && n < MAX_ACTORS;
         i = World_EntityPoolNext(&world->entities.obj_stack, i) )
    {
        struct WorldEntity_ObjStack* st =
            (struct WorldEntity_ObjStack*)World_EntityPoolAt(&world->entities.obj_stack, i);
        if( st->grid_position.level != level )
            continue;
        int sx = cx + (st->grid_position.x * 128 + 64 - cam_x) * PS_TILE_PX / 128;
        int sy = cy - (st->grid_position.z * 128 + 64 - cam_z) * PS_TILE_PX / 128;
        if( sx < x0 - 64 || sx > x1 + 64 || sy < y0 - 32 || sy > y1 + 64 )
            continue;
        actors[n].screen_x = sx;
        actors[n].screen_y = sy;
        actors[n].is_player = ACTOR_ITEM;
        actors[n++].index = i;
    }
    for( int i = World_EntityPoolHead(&world->entities.player); i >= 0 && n < MAX_ACTORS;
         i = World_EntityPoolNext(&world->entities.player, i) )
    {
        struct WorldEntity_Player* pl =
            (struct WorldEntity_Player*)World_EntityPoolAt(&world->entities.player, i);
        if( pl->grid_position.level != level )
            continue;
        actors[n].screen_x = cx + ((int)pl->draw_position.x - cam_x) * PS_TILE_PX / 128;
        actors[n].screen_y = cy - ((int)pl->draw_position.z - cam_z) * PS_TILE_PX / 128;
        actors[n].is_player = ACTOR_PLAYER;
        actors[n++].index = i;
    }
    qsort(actors, (size_t)n, sizeof(actors[0]), by_screen_y);
    for( int k = 0; k < n; k++ )
    {
        struct Actor* a = &actors[k];
        if( a->screen_x < x0 - 128 || a->screen_x > x1 + 128 || a->screen_y < y0 - 32 ||
            a->screen_y > y1 + 160 )
            continue;
        int bb[4] = { 1 << 30, 1 << 30, -(1 << 30), -(1 << 30) };
        int element_id;
        const struct WorldEntityFacet_Combat* combat = NULL;
        if( a->is_player == ACTOR_ITEM )
        {
            struct WorldEntity_ObjStack* st = (struct WorldEntity_ObjStack*)World_EntityPoolAt(
                &world->entities.obj_stack, a->index);
            draw_item(bb, w, st, a->screen_x, a->screen_y, px, stride, x0, y0, x1, y1);
            element_id = st->element_id;
        }
        else if( a->is_player == ACTOR_LOC || a->is_player == ACTOR_DECOR )
        {
            struct WorldEntity_Scenery* sc =
                (struct WorldEntity_Scenery*)World_EntityPoolAt(&world->entities.scenery, a->index);
            draw_loc(bb, w, sc, a->screen_x, a->screen_y, px, stride, x0, y0, x1, y1, now);
            element_id = sc->element_id;
        }
        else if( a->is_player == ACTOR_PLAYER )
        {
            struct WorldEntity_Player* pl =
                (struct WorldEntity_Player*)World_EntityPoolAt(&world->entities.player, a->index);
            draw_player(bb, w, pl, a->index, a->screen_x, a->screen_y, px, stride, x0, y0, x1, y1,
                        now);
            element_id = pl->element_id;
            combat = &pl->combat;
        }
        else
        {
            struct WorldEntity_NPC* npc =
                (struct WorldEntity_NPC*)World_EntityPoolAt(&world->entities.npc, a->index);
            draw_npc(bb, w, npc, a->index, a->screen_x, a->screen_y, px, stride, x0, y0, x1, y1,
                     now);
            element_id = npc->element_id;
            combat = &npc->combat;
        }
        if( bb[0] < bb[2] && w->drawn_count < MAX_ACTORS )
        {
            struct Drawn* d = &w->drawn[w->drawn_count++];
            d->x0 = bb[0];
            d->y0 = bb[1];
            d->x1 = bb[2];
            d->y1 = bb[3];
            d->element_id = element_id;
            d->gx = a->screen_x;
            d->combat = combat;
        }
    }
    draw_overlays(w, app, px, stride, x0, y0, x1, y1);
    /* POKECLIENT_ACTORS=1: where every actor landed, every 100 world cycles,
     * so a headless check can aim a click at one. */
    static int last_dump = -1000;
    if( getenv("POKECLIENT_ACTORS") && world->cycle - last_dump >= 100 )
    {
        last_dump = world->cycle;
        for( int i = 0; i < w->drawn_count; i++ )
            fprintf(stderr, "actor cycle=%d element=%d box=%d,%d-%d,%d\n", world->cycle,
                    w->drawn[i].element_id, w->drawn[i].x0, w->drawn[i].y0, w->drawn[i].x1,
                    w->drawn[i].y1);
    }
}

void
PS_WorldPick(
    struct PS_World* w,
    int mx,
    int my,
    struct ToriRS_PickHits* hits)
{
    assert(w);
    assert(hits);
    ToriRS_PickHitsReset(hits);
    if( !w->last_valid )
        return;
    /* Back to front, as the 3D raster reports: the ground first, nearest last. */
    int wx = w->last_cam_x + (mx - w->last_cx) * 128 / PS_TILE_PX;
    int wz = w->last_cam_z - (my - w->last_cy) * 128 / PS_TILE_PX;
    if( wx >= 0 && wz >= 0 )
        ToriRS_PickHitsAdd(hits, -1, true, wx >> 7, wz >> 7, w->last_level, 0);
    for( int i = 0; i < w->drawn_count; i++ )
    {
        const struct Drawn* d = &w->drawn[i];
        if( mx >= d->x0 && mx < d->x1 && my >= d->y0 && my < d->y1 && d->element_id >= 0 )
            ToriRS_PickHitsAdd(hits, d->element_id, false, -1, -1, w->last_level, 0);
    }
}
