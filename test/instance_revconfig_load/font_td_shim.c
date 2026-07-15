#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/td/toriauxlibtd.h"
#include "toridraw/toridraw_font.h"
#include "toridraw/toridraw_model.h"
#include "toridraw/toridraw_scene.h"
#include "toridraw/toridraw_sprite.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct ToriDraw_Scene*
ToriDraw_SceneNew(uint32_t flags)
{
    struct ToriDraw_Scene* scene = calloc(1, sizeof(*scene));
    if( !scene )
        return NULL;
    scene->flags = flags;
    if( !ToriDraw_SceneGraphInit(scene) )
    {
        ToriDraw_SceneGraphShutdown(scene);
        free(scene);
        return NULL;
    }
    return scene;
}

void
ToriDraw_SceneFree(struct ToriDraw_Scene* scene)
{
    if( !scene )
        return;
    ToriDraw_SceneGraphShutdown(scene);
    free(scene);
}

struct ToriAuxLibTD
{
    struct ToriAuxLibCore* core;
    struct ToriAuxLibCache* c;
    struct ToriDraw_Scene* scene;
};

struct ToriAuxLibTD*
ToriAuxLibTD_New(
    struct ToriAuxLibCore* core,
    struct ToriAuxLibCache* c,
    struct ToriDraw_Scene* scene)
{
    struct ToriAuxLibTD* td = calloc(1, sizeof(*td));
    if( !td )
        return NULL;
    td->core = core;
    td->c = c;
    td->scene = scene;
    return td;
}

void
ToriAuxLibTD_Free(struct ToriAuxLibTD* td)
{
    free(td);
}

struct ToriAuxLibCore*
ToriAuxLibTD_Core(struct ToriAuxLibTD* td)
{
    return td ? td->core : NULL;
}

struct ToriAuxLibCache*
ToriAuxLibTD_C(struct ToriAuxLibTD* td)
{
    return td ? td->c : NULL;
}

struct ToriDraw_Scene*
ToriAuxLibTD_Scene(struct ToriAuxLibTD* td)
{
    return td ? td->scene : NULL;
}

struct ToriDraw_Font*
ToriAuxLibTD_FontNewFromCore(const struct ToriAuxLibCore_Font* src)
{
    if( !src )
        return NULL;

    struct ToriDraw_Font* font = calloc(1, sizeof(struct ToriDraw_Font));
    if( !font )
        return NULL;

    for( int i = 0; i < TORIAUXLIBCORE_FONT_GLYPH_COUNT; i++ )
    {
        font->glyph_width[i] = src->glyph_width[i];
        font->glyph_height[i] = src->glyph_height[i];
        font->offset_x[i] = src->offset_x[i];
        font->offset_y[i] = src->offset_y[i];
        font->advance[i] = src->advance[i];
        if( src->glyph_alpha[i] && src->glyph_width[i] > 0 && src->glyph_height[i] > 0 )
        {
            size_t len = (size_t)src->glyph_width[i] * (size_t)src->glyph_height[i];
            font->glyph_alpha[i] = malloc(len);
            if( !font->glyph_alpha[i] )
                goto fail;
            memcpy(font->glyph_alpha[i], src->glyph_alpha[i], len);
        }
    }
    font->advance[TORIDRAW_FONT_GLYPH_COUNT] = src->advance[TORIAUXLIBCORE_FONT_GLYPH_COUNT];
    memcpy(font->draw_width, src->draw_width, sizeof(font->draw_width));
    font->line_height = src->line_height;
    memcpy(font->charcodeset, src->charcodeset, sizeof(font->charcodeset));
    if( !ToriDraw_FontValidate(font) )
        goto fail;
    return font;

fail:
    ToriDraw_FontFree(font);
    return NULL;
}

bool
ToriAuxLibTD_Font(
    struct ToriAuxLibTD* td,
    int font_id)
{
    if( !td || font_id < 0 )
        return false;

    if( ToriDraw_SceneFontHas(td->scene, font_id) )
        return true;

    struct ToriAuxLibCore_Font* core_font = ToriAuxLibCore_FontGet(td->core, font_id);
    if( !core_font )
        return false;

    struct ToriDraw_Font* font = ToriAuxLibTD_FontNewFromCore(core_font);
    if( !font )
        return false;

    ToriDraw_SceneFontAdd(td->scene, font_id, font);
    return true;
}

static struct ToriDraw_Sprite*
font_td_shim_sprite_frame_from_core(const struct ToriAuxLibCore_SpriteFrame* src)
{
    if( !src || !src->pixels_argb || src->width <= 0 || src->height <= 0 )
        return NULL;

    size_t pixel_count = (size_t)src->width * (size_t)src->height;
    uint32_t* pixels = malloc(pixel_count * sizeof(uint32_t));
    if( !pixels )
        return NULL;
    memcpy(pixels, src->pixels_argb, pixel_count * sizeof(uint32_t));

    struct ToriDraw_Sprite* sprite =
        ToriDraw_SpriteNewFromArgbOwned(pixels, src->width, src->height);
    if( !sprite )
        return NULL;

    sprite->crop_x = src->crop_x;
    sprite->crop_y = src->crop_y;
    sprite->crop_width = src->crop_width > 0 ? src->crop_width : src->width;
    sprite->crop_height = src->crop_height > 0 ? src->crop_height : src->height;
    return sprite;
}

bool
ToriAuxLibTD_Sprite(
    struct ToriAuxLibTD* td,
    int element_id)
{
    if( !td || element_id < 0 )
        return false;

    if( ToriDraw_SceneSpriteHas(td->scene, element_id) )
        return true;

    struct ToriAuxLibCore_Sprite* core_sprite = ToriAuxLibCore_SpriteGet(td->core, element_id);
    if( !core_sprite || core_sprite->frame_count <= 0 || !core_sprite->frames )
        return false;

    struct ToriDraw_Sprite** sprites =
        calloc((size_t)core_sprite->frame_count, sizeof(struct ToriDraw_Sprite*));
    if( !sprites )
        return false;

    int loaded = 0;
    for( int i = 0; i < core_sprite->frame_count; i++ )
    {
        sprites[i] = font_td_shim_sprite_frame_from_core(&core_sprite->frames[i]);
        if( sprites[i] )
            loaded++;
        else
            break;
    }

    if( loaded != core_sprite->frame_count )
    {
        for( int i = 0; i < loaded; i++ )
            ToriDraw_SpriteFree(sprites[i]);
        free(sprites);
        return false;
    }

    ToriDraw_SceneSpriteAdd(td->scene, element_id, sprites, core_sprite->frame_count);
    return true;
}

struct ToriDraw_ModelHandle
ToriAuxLibTD_Model(
    struct ToriAuxLibTD* td,
    int model_id)
{
    struct ToriDraw_ModelHandle none = { .kind = TORIDRAWMK_NONE };
    if( !td || model_id < 0 )
        return none;

    struct ToriDraw_ModelHandle existing = ToriDraw_SceneModelGet(td->scene, model_id);
    if( existing.kind == TORIDRAWMK_MODEL )
        return existing;

    if( !ToriAuxLibCore_ModelHas(td->core, model_id) )
        return none;

    struct ToriDraw_Model* model = ToriDraw_ModelNew(1, 1, 0);
    if( !model )
        return none;

    struct ToriDraw_ModelHandle hnd = {
        .kind = TORIDRAWMK_MODEL,
        .u.model.model = model,
    };
    ToriDraw_SceneModelAdd(td->scene, model_id, hnd);
    return hnd;
}
