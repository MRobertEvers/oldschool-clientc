#ifndef TORIRS_PLUGIN_DRAW_H
#define TORIRS_PLUGIN_DRAW_H

/*
 * A drawing kit for plugins that compose their own pictures.
 *
 * WHY A PLUGIN COMPOSES AT ALL. The panel gives a plugin a bounded drawing
 * well (TORIRS_PLUGIN_W_CUSTOM) and four verbs to fill it with -- rect, line,
 * text and image -- and one of those four is not enough on its own:
 * api->draw_text sets in the client's hitsplat face, one centred string per
 * overlay item, with no way to measure a string. A page that wants the game's
 * OWN caption face, laid out in columns, therefore ships a baked atlas of that
 * face and sets the text itself, into a buffer it publishes as one image and
 * blits once. That is what xp-drop-orbs and item-stats each do, and this is
 * the third and fourth plugin to need it -- which is what makes it a kit
 * rather than a fourth copy.
 *
 * It is deliberately NOT part of the plugin ABI. Everything here is arithmetic
 * over a caller's own buffer plus two api verbs (asset_load / image_*), so it
 * compiles into whichever plugins want it and costs nothing to the ones that
 * do not. A plugin outside this tree writes its own; the contract it is held
 * to is the api, not this file.
 */

#include "plugin/torirs_plugin.h"

#include <stdint.h>

/** Printable ASCII, which is the whole range a baked atlas carries. */
#define PLUGIN_DRAW_GLYPH_FIRST 32
#define PLUGIN_DRAW_GLYPH_COUNT 96

/** One glyph's place in an atlas, as its .ini states it. */
struct PluginDraw_Glyph
{
    int x;
    int y;
    int w;
    int h;
    int off_x;
    int off_y;
    int advance;
};

/**
 * One baked face: where each glyph is, and the pixels behind them.
 *
 * The pair of files tools/fontbake_atlas.py writes. Held by the plugin rather
 * than by this kit, so a plugin that ships two faces -- the CS2 loot tracker
 * sets its headers in one and its captions in another -- has two of these and
 * no ambiguity about which a call is drawing in.
 */
struct PluginDraw_Atlas
{
    struct PluginDraw_Glyph glyph[PLUGIN_DRAW_GLYPH_COUNT];
    int ready;
    /** The line box, which is what a caption's height is measured in. */
    int line_h;
    /** The decoded sheet, owned here and freed by PluginDraw_AtlasFree. */
    int image;
    uint32_t* px;
    int w;
    int h;
};

/* ---- pixels -------------------------------------------------------------- */

/** Blend one ARGB pixel over `buf`. `alpha` 0..255; out-of-range is a no-op. */
void PluginDraw_Pixel(
    uint32_t* buf, int w, int h, int x, int y, uint32_t rgb, int alpha);
/** A filled rect. */
void PluginDraw_Fill(
    uint32_t* buf, int w, int h, int x, int y, int rw, int rh, uint32_t rgb, int alpha);
/** A one-pixel outline, which is what an unfilled interface rect is. */
void PluginDraw_Frame(
    uint32_t* buf, int w, int h, int x, int y, int rw, int rh, uint32_t rgb);

/**
 * Copy a source rect, multiplying the ink by `tint` when one is given (0 draws
 * the source as it was baked).
 *
 * The multiply is what lets ONE baked white glyph row serve every colour a
 * page sets text in: every glyph pixel is either white or the baked black
 * shadow, so scaling by a colour gives that colour and leaves the shadow
 * black. Baking a row per colour would be a copy of the same glyph pack per
 * caption colour.
 */
void PluginDraw_Blit(
    uint32_t* dst,
    int dw,
    int dh,
    int dx,
    int dy,
    uint32_t const* src,
    int sw,
    int sh,
    int sx,
    int sy,
    int cw,
    int ch,
    uint32_t tint);

/** Tile `src` across a rect, which is what cc_settiling does. */
void PluginDraw_Tile(
    uint32_t* dst,
    int dw,
    int dh,
    int dx,
    int dy,
    int rw,
    int rh,
    uint32_t const* src,
    int sw,
    int sh,
    uint32_t tint);

/* ---- assets -------------------------------------------------------------- */

/**
 * Decode one shipped PNG into a plugin-owned buffer, once.
 *
 * @return 1 when `*px` holds the pixels. 0 is the ORDINARY state for the first
 * frames after a start -- the file crosses the IO queue -- and means "ask
 * again", not "failed".
 */
int PluginDraw_ImageLoad(
    struct ToriRS_PluginCtx* ctx,
    struct ToriRS_PluginApi const* api,
    char const* name,
    int* handle,
    uint32_t** px,
    int* w,
    int* h);
/** Give one back. Idempotent. */
void PluginDraw_ImageFree(uint32_t** px, int* handle);

/** Load `<name>.ini` and `<name>.png` into `atlas`. Same 0/1 contract. */
int PluginDraw_AtlasLoad(
    struct ToriRS_PluginCtx* ctx,
    struct ToriRS_PluginApi const* api,
    struct PluginDraw_Atlas* atlas,
    char const* name);
void PluginDraw_AtlasFree(struct PluginDraw_Atlas* atlas);

/* ---- text ---------------------------------------------------------------- */

/** How wide `text` sets in `atlas`. */
int PluginDraw_TextWidth(struct PluginDraw_Atlas const* atlas, char const* text);
/** `text` with `x` as the pen and `top` as the line box's top. */
void PluginDraw_Text(
    uint32_t* buf,
    int w,
    int h,
    int x,
    int top,
    struct PluginDraw_Atlas const* atlas,
    char const* text,
    uint32_t tint);
/** `text` ENDING at `right`, for a column of right-set values. */
void PluginDraw_TextRight(
    uint32_t* buf,
    int w,
    int h,
    int right,
    int top,
    struct PluginDraw_Atlas const* atlas,
    char const* text,
    uint32_t tint);
/** `text` centred in `[x, x+width)`. */
void PluginDraw_TextCenter(
    uint32_t* buf,
    int w,
    int h,
    int x,
    int width,
    int top,
    struct PluginDraw_Atlas const* atlas,
    char const* text,
    uint32_t tint);

#endif /* TORIRS_PLUGIN_DRAW_H */
