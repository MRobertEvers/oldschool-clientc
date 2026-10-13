#include "plugin/plugins/nxt_cox_helper.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/*
 * The Chambers of Xeric helper's box, painted. Every number is measured off
 * the C++ client's one published picture; @see nxt_cox_helper.c.
 */

/* ---- the picture's measurements -------------------------------------- */

#define NXT_COX_BOX_W 140
#define NXT_COX_PAD 5
#define NXT_COX_ROW_PITCH 12
/** The point rows sit this much lower than the room rows' pitch implies. */
#define NXT_COX_SECTION_GAP 2
/** And the time row this much lower than the point rows' pitch. */
#define NXT_COX_TIME_GAP 2
#define NXT_COX_ARROW_SIZE 11
/** The arrow's corner, from the box's right edge and the time row's top. */
#define NXT_COX_ARROW_RIGHT 6
#define NXT_COX_ARROW_DY 1
#define NXT_COX_PLATE_ALPHA 155u
#define NXT_COX_FRAME_OUTER 0x383023u
#define NXT_COX_FRAME_INNER 0x5a5245u
#define NXT_COX_TEXT 0xff981fu
/** raids_overlay_databox_1511's own cut-off for a score it will not print. */
#define NXT_COX_SCORE_LOTS 2000000000

/* ---- the view: what one paint draws ---------------------------------- */

void
NxtCoxHelper_FormatScore(char* out, size_t cap, int score)
{
    char digits[16];
    int len;
    int at = 0;

    assert(out);
    assert(cap >= 16);
    if( score >= NXT_COX_SCORE_LOTS )
    {
        snprintf(out, cap, "Lots!");
        return;
    }
    if( score < 0 )
        score = 0;
    /* tostring_spacer(n, ","): thousands, comma-separated. */
    len = snprintf(digits, sizeof(digits), "%d", score);
    for( int i = 0; i < len; i++ )
    {
        if( i > 0 && (len - i) % 3 == 0 )
            out[at++] = ',';
        out[at++] = digits[i];
    }
    out[at] = '\0';
}

void
NxtCoxHelper_FormatTime(char* out, size_t cap, int ticks)
{
    /* torirs_fmt_hms's own arithmetic -- scale(60, 100, ticks) -- but printed
     * the way the picture does it: always hh:mm:ss. */
    int const seconds = (ticks > 0 ? ticks : 0) * 60 / 100;

    assert(out);
    assert(cap >= 12);
    snprintf(out, cap, "%02d:%02d:%02d", (seconds / 3600) % 100, (seconds / 60) % 60, seconds % 60);
}

int
NxtCoxHelper_Height(struct NxtCoxHelperView const* view)
{
    assert(view);
    return NxtCoxHelper_TimeTop(view) + NXT_COX_ROW_PITCH + NXT_COX_PAD;
}

int
NxtCoxHelper_TimeTop(struct NxtCoxHelperView const* view)
{
    int rows_end;

    assert(view);
    rows_end = NXT_COX_PAD;
    if( view->room_count > 0 )
        rows_end += view->room_count * NXT_COX_ROW_PITCH + NXT_COX_SECTION_GAP;
    return rows_end + 2 * NXT_COX_ROW_PITCH + NXT_COX_TIME_GAP;
}

void
NxtCoxHelper_ArrowRect(struct NxtCoxHelperView const* view, struct ToriRS_Rect* out)
{
    assert(view);
    assert(out);
    out->x = NXT_COX_BOX_W - NXT_COX_ARROW_RIGHT - NXT_COX_ARROW_SIZE + 1;
    out->y = NxtCoxHelper_TimeTop(view) + NXT_COX_ARROW_DY;
    out->width = NXT_COX_ARROW_SIZE;
    out->height = NXT_COX_ARROW_SIZE;
}

int
NxtCoxHelper_Width(void)
{
    return NXT_COX_BOX_W;
}

void
NxtCoxHelper_Paint(
    struct NxtCoxHelperView const* view,
    struct NxtCoxHelperArt const* art,
    uint32_t* argb,
    int w,
    int h)
{
    struct ToriRS_Rect arrow;
    char line[64];
    int y;

    assert(view);
    assert(art);
    assert(art->text);
    assert(art->plate_px);
    assert(argb);
    assert(w == NXT_COX_BOX_W);
    assert(h == NxtCoxHelper_Height(view));

    /* The plate: every pixel of the box, translucent. */
    for( int j = 0; j < h; j++ )
    {
        for( int i = 0; i < w; i++ )
        {
            uint32_t const texel =
                art->plate_px[(size_t)(j % art->plate_h) * (size_t)art->plate_w + (size_t)(i % art->plate_w)];
            argb[(size_t)j * (size_t)w + (size_t)i] = (NXT_COX_PLATE_ALPHA << 24) | (texel & 0x00FFFFFFu);
        }
    }
    PluginDraw_Frame(argb, w, h, 0, 0, w, h, NXT_COX_FRAME_OUTER);
    PluginDraw_Frame(argb, w, h, 1, 1, w - 2, h - 2, NXT_COX_FRAME_INNER);

    y = NXT_COX_PAD;
    for( int r = 0; r < view->room_count; r++ )
    {
        PluginDraw_Text(argb, w, h, NXT_COX_PAD, y, art->text, view->rooms[r].kind, NXT_COX_TEXT);
        snprintf(line, sizeof(line), "%s %c", view->rooms[r].name, view->rooms[r].letter);
        PluginDraw_TextRight(argb, w, h, w - NXT_COX_PAD, y, art->text, line, NXT_COX_TEXT);
        y += NXT_COX_ROW_PITCH;
    }
    if( view->room_count > 0 )
        y += NXT_COX_SECTION_GAP;

    snprintf(line, sizeof(line), "Total points: %s", view->total);
    PluginDraw_Text(argb, w, h, NXT_COX_PAD, y, art->text, line, NXT_COX_TEXT);
    y += NXT_COX_ROW_PITCH;
    snprintf(line, sizeof(line), "Your points: %s", view->yours);
    PluginDraw_Text(argb, w, h, NXT_COX_PAD, y, art->text, line, NXT_COX_TEXT);
    y += NXT_COX_ROW_PITCH + NXT_COX_TIME_GAP;
    assert(y == NxtCoxHelper_TimeTop(view));
    snprintf(line, sizeof(line), "Time: %s", view->time);
    PluginDraw_Text(argb, w, h, NXT_COX_PAD, y, art->text, line, NXT_COX_TEXT);

    /*
     * The arrow, as the picture has it while the list is open. Collapsed, the
     * same art turned half round -- the picture shows only the open state, so
     * the closed one is this client's guess.
     */
    if( art->arrow_px )
    {
        NxtCoxHelper_ArrowRect(view, &arrow);
        for( int j = 0; j < arrow.height && j < art->arrow_h; j++ )
        {
            for( int i = 0; i < arrow.width && i < art->arrow_w; i++ )
            {
                int const sx = view->collapsed ? art->arrow_w - 1 - i : i;
                int const sy = view->collapsed ? art->arrow_h - 1 - j : j;
                uint32_t const texel = art->arrow_px[(size_t)sy * (size_t)art->arrow_w + (size_t)sx];
                if( (texel >> 24) == 0 )
                    continue;
                argb[(size_t)(arrow.y + j) * (size_t)w + (size_t)(arrow.x + i)] = texel | 0xFF000000u;
            }
        }
    }
}

