/*
 * A strip of chips over one GRAPH well (edpanel_chips.h).
 */

#include "edpanel_chips.h"

#include "edpanel_common.h"

#include <stdio.h>
#include <string.h>

#define CHIP_PAD 4
#define CHIP_FILL 0x2c2820
#define CHIP_FILL_ACTIVE 0x4a3c18
#define CHIP_RULE 0x6a6458
#define CHIP_RULE_ACTIVE 0xffd040
#define CHIP_INK 0xd8d0c0
#define CHIP_INK_ACTIVE 0xffff00
#define CHIP_X_INK 0xc06050
#define CHIP_CAP_FILL 0x000000
#define CHIP_CAP_PAD 2

void
edp_chips_init(
    struct EditorUi* ui,
    struct Edp_ChipStrip* strip,
    int panel,
    int lines)
{
    assert(ui);
    assert(strip);
    assert(lines >= 1);
    memset(strip, 0, sizeof(*strip));
    strip->lines = lines;
    strip->graph = ToriRSChrome_Graph(ui->chrome, panel, "", lines * EDP_CHIP_H + (lines - 1) * EDP_CHIP_GAP);
    assert(strip->graph >= 0);
}

void
edp_chips_begin(struct Edp_ChipStrip* strip)
{
    assert(strip);
    strip->count = 0;
}

int
edp_chips_push(
    struct Edp_ChipStrip* strip,
    const char* label,
    const char* tip,
    int active,
    int has_x)
{
    struct Edp_Chip* chip;

    assert(strip);
    assert(label);
    if( strip->count >= EDP_CHIPS_MAX )
        return -1;
    chip = &strip->chips[strip->count];
    snprintf(chip->label, sizeof(chip->label), "%s", label);
    snprintf(chip->tip, sizeof(chip->tip), "%s", tip ? tip : label);
    chip->active = active;
    chip->has_x = has_x;
    chip->chord[0] = '\0';
    /* x, y, w stay as the last layout placed them: edp_chips_end lays the
     * strip out again only when it changed, and the hover reads them on
     * every frame in between. */
    return strip->count++;
}

void
edp_chips_set_chord(
    struct Edp_ChipStrip* strip,
    int index,
    const char* chord)
{
    assert(strip);
    assert(chord);
    assert(index >= 0);
    assert(index < strip->count);
    snprintf(strip->chips[index].chord, sizeof(strip->chips[index].chord), "%s", chord);
}

static int
chips_measure(
    int font,
    const char* text)
{
    return ToriRSChrome_MeasureTextQ(font, 4, text);
}

/** `text` cut to `width` logical px with an ellipsis (R2: measured, never
 *  clipped mid-glyph). */
static void
chips_elide(
    int font,
    const char* text,
    int width,
    char* out,
    size_t out_size)
{
    size_t length;

    snprintf(out, out_size, "%s", text);
    if( chips_measure(font, out) <= width )
        return;
    length = strlen(text);
    while( length > 0 )
    {
        length--;
        snprintf(out, out_size, "%.*s...", (int)length, text);
        if( chips_measure(font, out) <= width )
            return;
    }
    snprintf(out, out_size, "...");
}

/** The factor between a well's device pixels and its items' coordinates:
 *  the integer tier the chrome draws GRAPH items at (dbg_draw_graph), which
 *  at a fractional q is not q -- laid out at q, chips past the edge were
 *  drawn and clipped instead of folding into "+n". */
static int
chips_tier(const struct ToriRSChrome* chrome)
{
    int const tier = ToriRSChrome_Scale(chrome);
    return tier > 0 ? tier : 1;
}

/** Device px `device` from the well's inner corner, in item coordinates. */
static int
chips_unscale(
    const struct ToriRSChrome* chrome,
    int device)
{
    int const tier = chips_tier(chrome);
    /* Floors, so a point left of or above the corner is never chip 0's. */
    return device >= 0 ? device / tier : -((-device + tier - 1) / tier);
}

static uint64_t
chips_hash(
    const struct Edp_ChipStrip* strip,
    int width)
{
    uint64_t hash = 1469598103934665603ull;
    hash = (hash ^ (uint64_t)(unsigned)width) * 1099511628211ull;
    for( int i = 0; i < strip->count; i++ )
    {
        const struct Edp_Chip* chip = &strip->chips[i];
        for( const char* c = chip->label; *c; c++ )
            hash = (hash ^ (uint8_t)*c) * 1099511628211ull;
        for( const char* c = chip->chord; *c; c++ )
            hash = (hash ^ (uint8_t)*c) * 1099511628211ull;
        hash = (hash ^ (uint64_t)(chip->active * 2 + chip->has_x + 7)) * 1099511628211ull;
    }
    return hash;
}

/** Where each chip goes in `width` logical px over the strip's lines, what
 *  it shows, and which are folded into the "+n" chip. `reserve` keeps the
 *  "+n" chip's room on the last line for the line's FIRST chip too, which is
 *  otherwise elided against the whole line (a later chip may still fit). */
static void
chips_place(
    struct Edp_ChipStrip* strip,
    int font,
    int width,
    int reserve,
    char (*shown)[EDP_CHIP_LABEL_MAX + 4])
{
    int const x_w = chips_measure(font, "x") + CHIP_PAD;
    int const more_w = chips_measure(font, "+99") + 2 * CHIP_PAD;
    size_t tip_at = 0;
    int x = 0;
    int line = 0;

    strip->hidden = 0;
    strip->more_x = -1;
    strip->more_tip[0] = '\0';
    for( int i = 0; i < strip->count; i++ )
        strip->chips[i].x = -1;
    for( int i = 0; i < strip->count; i++ )
    {
        struct Edp_Chip* chip = &strip->chips[i];
        int const cap_w = chip->chord[0] ? chips_measure(font, chip->chord) + 2 * CHIP_CAP_PAD + CHIP_PAD : 0;
        int const extra = (chip->has_x ? x_w : 0) + cap_w;
        int const last_line = line == strip->lines - 1;
        /* The last line keeps room for a "+n" chip when more follow. */
        int const room = width - (last_line && i + 1 < strip->count ? more_w + EDP_CHIP_GAP : 0);
        int w;

        chips_elide(font, chip->label, (reserve && x == 0 ? room : width) - 2 * CHIP_PAD - extra, shown[i],
            sizeof(shown[i]));
        w = chips_measure(font, shown[i]) + 2 * CHIP_PAD + extra;
        if( x > 0 && x + w > room )
        {
            if( last_line )
            {
                /* Every chip from here on is the "+n" chip's. */
                strip->hidden = strip->count - i;
                for( int j = i; j < strip->count; j++ )
                {
                    int const n = snprintf(strip->more_tip + tip_at, sizeof(strip->more_tip) - tip_at, "%s%s",
                        tip_at ? ", " : "", strip->chips[j].label);
                    if( n > 0 && tip_at + (size_t)n < sizeof(strip->more_tip) )
                        tip_at += (size_t)n;
                }
                break;
            }
            line++;
            x = 0;
            /* the line below has its own room; measure this chip there */
            i--;
            continue;
        }
        chip->x = x;
        chip->y = line * (EDP_CHIP_H + EDP_CHIP_GAP);
        chip->w = w;
        x += w + EDP_CHIP_GAP;
    }
    if( strip->hidden > 0 )
    {
        char more[8];
        snprintf(more, sizeof(more), "+%d", strip->hidden);
        strip->more_x = x;
        strip->more_y = line * (EDP_CHIP_H + EDP_CHIP_GAP);
        strip->more_w = chips_measure(font, more) + 2 * CHIP_PAD;
    }
}

void
edp_chips_end(
    struct EditorUi* ui,
    struct Edp_ChipStrip* strip)
{
    struct ToriRSChrome* chrome;
    int font;
    int width;
    uint64_t hash;
    char shown[EDP_CHIPS_MAX][EDP_CHIP_LABEL_MAX + 4];

    assert(ui);
    assert(strip);
    chrome = ui->chrome;
    font = chrome->theme.font_row;
    /* The well's inner width (inside its two rules) in item coordinates. */
    width = chrome->widgets[strip->graph].w > 0
                ? chips_unscale(chrome, chrome->widgets[strip->graph].w - 2 * ToriRSChrome_Px(chrome, 1))
                : 200;
    hash = chips_hash(strip, width);
    if( hash == strip->hash )
        return;
    strip->hash = hash;

    chips_place(strip, font, width, 0, shown);
    /* A first chip on the last line that took the whole line leaves the "+n"
     * chip past the edge (R2): place again with its room kept. */
    if( strip->hidden > 0 && strip->more_x + strip->more_w > width )
        chips_place(strip, font, width, 1, shown);

    {
        int const ascent = ToriRSChrome_FontLineHeightQ(font, 4);
        int const box = ToriRSChrome_FontLineBoxQ(font, 4);
        int const x_w = chips_measure(font, "x") + CHIP_PAD;

        ToriRSChrome_GraphBegin(chrome, strip->graph);
        for( int i = 0; i < strip->count; i++ )
        {
            const struct Edp_Chip* chip = &strip->chips[i];
            int const cap_w = chip->chord[0] ? chips_measure(font, chip->chord) + 2 * CHIP_CAP_PAD + CHIP_PAD : 0;
            int const w = chip->w;
            int baseline;

            if( chip->x < 0 )
                continue;
            baseline = chip->y + (EDP_CHIP_H - box) / 2 + ascent;
            ToriRSChrome_GraphRect(chrome, strip->graph, chip->x, chip->y, w, EDP_CHIP_H,
                chip->active ? CHIP_FILL_ACTIVE : CHIP_FILL, 1, EDP_CHIP_KEY_BASE + i);
            ToriRSChrome_GraphRect(chrome, strip->graph, chip->x, chip->y, w, EDP_CHIP_H,
                chip->active ? CHIP_RULE_ACTIVE : CHIP_RULE, 0, 0);
            ToriRSChrome_GraphText(chrome, strip->graph, chip->x + CHIP_PAD, baseline,
                chip->active ? CHIP_INK_ACTIVE : CHIP_INK, shown[i]);
            if( cap_w > 0 )
            {
                /* The keycap: a dark key set into the chip, the chord on it. */
                int const at = chip->x + w - (chip->has_x ? x_w : 0) - cap_w + CHIP_PAD / 2;
                ToriRSChrome_GraphRect(chrome, strip->graph, at, chip->y + 2, cap_w - CHIP_PAD, EDP_CHIP_H - 4,
                    CHIP_CAP_FILL, 1, 0);
                ToriRSChrome_GraphRect(chrome, strip->graph, at, chip->y + 2, cap_w - CHIP_PAD, EDP_CHIP_H - 4,
                    CHIP_RULE, 0, 0);
                ToriRSChrome_GraphText(chrome, strip->graph, at + CHIP_CAP_PAD, baseline, CHIP_INK_ACTIVE, chip->chord);
            }
            if( chip->has_x )
            {
                int const at = chip->x + w - x_w;
                /* The x is its own key, drawn over the chip's. */
                ToriRSChrome_GraphRect(chrome, strip->graph, at, chip->y + 1, x_w - 1, EDP_CHIP_H - 2, CHIP_FILL, 1,
                    EDP_CHIP_X_BASE + i);
                ToriRSChrome_GraphText(chrome, strip->graph, at + 1, baseline, CHIP_X_INK, "x");
            }
        }
        if( strip->hidden > 0 )
        {
            char more[8];
            int const baseline = strip->more_y + (EDP_CHIP_H - box) / 2 + ascent;
            snprintf(more, sizeof(more), "+%d", strip->hidden);
            ToriRSChrome_GraphRect(chrome, strip->graph, strip->more_x, strip->more_y, strip->more_w, EDP_CHIP_H,
                CHIP_FILL, 1, EDP_CHIP_MORE_KEY);
            ToriRSChrome_GraphRect(chrome, strip->graph, strip->more_x, strip->more_y, strip->more_w, EDP_CHIP_H,
                CHIP_RULE, 0, 0);
            ToriRSChrome_GraphText(chrome, strip->graph, strip->more_x + CHIP_PAD, baseline, CHIP_INK, more);
        }
        ToriRSChrome_GraphEnd(chrome, strip->graph);
    }
}

int
edp_chips_key_of(
    const struct Edp_ChipStrip* strip,
    int key,
    int* out_x)
{
    assert(strip);
    assert(out_x);
    *out_x = 0;
    if( key >= EDP_CHIP_X_BASE && key < EDP_CHIP_X_BASE + strip->count )
    {
        *out_x = 1;
        return key - EDP_CHIP_X_BASE;
    }
    if( key >= EDP_CHIP_KEY_BASE && key < EDP_CHIP_KEY_BASE + strip->count )
        return key - EDP_CHIP_KEY_BASE;
    return -1;
}

void
edp_chips_hover(
    struct EditorUi* ui,
    struct Edp_ChipStrip* strip)
{
    struct ToriRSChrome* chrome;
    const struct ToriRSChromeWidget* w;
    const char* tip = "";
    int lx;
    int ly;

    assert(ui);
    assert(strip);
    chrome = ui->chrome;
    if( chrome->hover != strip->graph )
        return;
    w = &chrome->widgets[strip->graph];
    lx = chips_unscale(chrome, chrome->hover_x - (w->x + edp_px(ui, 1)));
    ly = chips_unscale(chrome, chrome->hover_y - (w->y + edp_px(ui, 1)));
    for( int i = 0; i < strip->count; i++ )
    {
        const struct Edp_Chip* chip = &strip->chips[i];
        if( chip->x >= 0 && lx >= chip->x && lx < chip->x + chip->w && ly >= chip->y && ly < chip->y + EDP_CHIP_H )
            tip = chip->tip;
    }
    if( strip->more_x >= 0 && lx >= strip->more_x && lx < strip->more_x + strip->more_w && ly >= strip->more_y
        && ly < strip->more_y + EDP_CHIP_H )
        tip = strip->more_tip;
    ToriRSChrome_SetTooltip(chrome, strip->graph, tip);
}
