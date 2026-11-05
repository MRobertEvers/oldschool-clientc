#include "chrome_widget_sync.h"

#include "ui/torirs_chrome_bake.h"
#include "ui/torirs_chrome_metrics.h"
#include "ui/torirs_chrome_skin.h"

#include <stdlib.h>
#include <string.h>

/* Where the photographs go: the build's object directory, from the makefile. */
#ifndef CW_OUT_DIR
#define CW_OUT_DIR "build_opt"
#endif

/*
 * The chrome scale as a rational (workbench step 6.0b): the one rounding
 * function and its inverse, the measurements at every scale, the SCALE command
 * the executor seam carries, the design's equal-rows assertion, and one
 * photograph of a Catalog-shaped panel per scale for review.
 */

static char const* const sc_layers[] = { "Hidden", "Wall", "Roof" };

/** A Catalog-shaped panel: a search field, filter rows, a list, actions. */
static int
sc_build_catalog(struct ToriRSChrome* ui, int* out_rows, int* out_row_count)
{
    int panel = ToriRSChrome_PanelAdd(
        ui, TORIRS_CHROME_PANEL_WINDOW, ToriRSChrome_Px(ui, 8), ToriRSChrome_Px(ui, 8),
        ToriRSChrome_Px(ui, 260), "Catalog (F8)");
    int list;
    int n = 0;

    ToriRSChrome_PanelSetFramed(ui, panel, 1);
    ToriRSChrome_PanelSetClosable(ui, panel, 1);
    out_rows[n++] = ToriRSChrome_TextInput(ui, panel, "Search", "oak");
    out_rows[n++] = ToriRSChrome_Dropdown(ui, panel, "Layer", sc_layers, 3, 1);
    out_rows[n++] = ToriRSChrome_Checkbox(ui, panel, "Members only", 1);
    out_rows[n++] = ToriRSChrome_Checkbox(ui, panel, "Show hidden", 0);
    out_rows[n++] = ToriRSChrome_Label(ui, panel, "1 to 100 of 63,307");
    (void)ToriRSChrome_KeyValue(ui, panel, "Selected", "1276 Oak tree");
    list = ToriRSChrome_VList(ui, panel, "", 5, 63307);
    ToriRSChrome_VListSetRow(ui, list, 0, "1276  Oak tree");
    ToriRSChrome_VListSetRow(ui, list, 1, "1277  Oak tree (stump)");
    ToriRSChrome_VListSetRow(ui, list, 2, "1278  Willow");
    ToriRSChrome_VListSetRow(ui, list, 3, "1281  Maple tree");
    ToriRSChrome_VListSetRow(ui, list, 4, "1282  Yew");
    {
        int const b = ToriRSChrome_Button(ui, panel, "Add to palette");
        ToriRSChrome_ButtonSetChord(ui, b, "Ctrl+P");
    }
    *out_row_count = n;
    return panel;
}

static void
sc_write_bmp(char const* path, uint32_t const* argb, int w, int h)
{
    FILE* f = fopen(path, "wb");
    int const row = w * 3;
    int const pad = (4 - row % 4) % 4;
    int const size = 54 + (row + pad) * h;
    unsigned char header[54];

    CW_CHECK(f != NULL, "open the photograph for writing");
    if( !f )
        return;
    memset(header, 0, sizeof(header));
    header[0] = 'B';
    header[1] = 'M';
    for( int i = 0; i < 4; i++ )
    {
        header[2 + i] = (unsigned char)(size >> (8 * i));
        header[18 + i] = (unsigned char)(w >> (8 * i));
        header[22 + i] = (unsigned char)(h >> (8 * i));
    }
    header[10] = 54;
    header[14] = 40;
    header[26] = 1;
    header[28] = 24;
    fwrite(header, 1, sizeof(header), f);
    for( int y = h - 1; y >= 0; y-- )
    {
        static unsigned char const zero[3];
        for( int x = 0; x < w; x++ )
        {
            uint32_t const p = argb[y * w + x];
            unsigned char const bgr[3] = { (unsigned char)p, (unsigned char)(p >> 8),
                                           (unsigned char)(p >> 16) };
            fwrite(bgr, 1, 3, f);
        }
        fwrite(zero, 1, (size_t)pad, f);
    }
    fclose(f);
}

static void
sc_setup(int q)
{
    cw_reset();
    g_cw_ui.skin_tile_w = ToriRSChromeSkin_ForSlot(TORIRS_CHROME_SKIN_PANEL_BODY)->w;
    g_cw_ui.skin_tile_h = ToriRSChromeSkin_ForSlot(TORIRS_CHROME_SKIN_PANEL_BODY)->h;
    ToriRSChromeBake_SetScaleQ(&g_cw_ui, q);
}

void
cw_test_scale(void)
{
    static int const photographed[] = { 4, 5, 6, 7, 8, 12 };

    /* ---- the one rounding function, and its inverse ---- */
    {
        int linear = 1;
        int symmetric = 1;
        int inverse = 1;
        for( int px = -300; px <= 300; px++ )
        {
            linear &= ToriRSChrome_ScalePx(4, px) == px && ToriRSChrome_ScalePx(8, px) == 2 * px &&
                      ToriRSChrome_ScalePx(12, px) == 3 * px;
            for( int q = TORIRS_CHROME_SCALE_Q_MIN; q <= TORIRS_CHROME_SCALE_Q_MAX; q++ )
                symmetric &= ToriRSChrome_ScalePx(q, -px) == -ToriRSChrome_ScalePx(q, px);
        }
        for( int q = TORIRS_CHROME_SCALE_Q_MIN; q <= TORIRS_CHROME_SCALE_Q_MAX; q++ )
            for( int d = -200; d <= 600; d++ )
            {
                int const c = ToriRSChrome_UnscalePx(q, d);
                if( q % 4 == 0 )
                    inverse &= c == d / (q / 4);
                else
                    inverse &= ToriRSChrome_ScalePx(q, c) <= d && d < ToriRSChrome_ScalePx(q, c + 1);
            }
        CW_CHECK(linear, "at q = 4, 8, 12 ScalePx is the integer multiply");
        CW_CHECK(symmetric, "ScalePx rounds halves away from zero");
        CW_CHECK(inverse, "UnscalePx finds the chrome cell a device pixel is in");
        CW_CHECK(ToriRSChrome_ScalePx(6, TORIRS_CHROME_M_ROW_H) == 27, "an 18 px row is 27 at 1.5x");
        CW_CHECK(ToriRSChrome_ScalePx(5, 18) == 23 && ToriRSChrome_ScalePx(7, 18) == 32,
            "and 23 at 1.25x, 32 at 1.75x");
        CW_CHECK(ToriRSChrome_ScaleTier(4) == 1 && ToriRSChrome_ScaleTier(5) == 2 &&
                     ToriRSChrome_ScaleTier(8) == 2 && ToriRSChrome_ScaleTier(10) == 3,
            "the tier is the nearest at or above");
    }

    /* ---- measuring: the integer API is the rational one at 4 * scale ---- */
    {
        static char const* const samples[] = { "", "Catalog (F8)", "1 to 100 of 63,307",
                                               "Ctrl+Shift+Z", "\x7f\xa3 ~|}" };
        int same = 1;
        for( int slot = 0; slot < TORIRS_CHROME_FONT_SLOT_COUNT; slot++ )
            for( int s = 1; s <= 3; s++ )
            {
                same &= ToriRSChrome_FontLineHeightQ(slot, 4 * s) ==
                        ToriRSChrome_FontLineHeight(slot, s);
                same &= ToriRSChrome_FontLineBoxQ(slot, 4 * s) == ToriRSChrome_FontLineBox(slot, s);
                for( int i = 0; i < (int)(sizeof(samples) / sizeof(samples[0])); i++ )
                    same &= ToriRSChrome_MeasureTextQ(slot, 4 * s, samples[i]) ==
                            ToriRSChrome_MeasureText(slot, s, samples[i]);
            }
        CW_CHECK(same, "MeasureText, FontLineHeight and FontLineBox are the Q forms at 4 * scale");
        CW_CHECK(ToriRSChrome_FontLineBoxQ(TORIRS_CHROME_FONT_BODY, 6) == 24 &&
                     ToriRSChrome_FontLineHeightQ(TORIRS_CHROME_FONT_BODY, 6) == 18,
            "Body's 16 px line box is 24 at 1.5x, its baseline 18");
    }

    /* ---- the SCALE command ---- */
    {
        struct ToriRSChromeCmd const* cmd;
        int panel;

        cw_reset();
        panel = ToriRSChrome_PanelAdd(&g_cw_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 200, "Scale");
        (void)ToriRSChrome_Label(&g_cw_ui, panel, "row");
        (void)cw_run();
        CW_CHECK(cw_count(TORIRS_CHROME_CMD_SCALE) == 0,
            "a 1x chrome sends no SCALE: its stream is the one it always was");

        ToriRSChrome_SetScaleQ(&g_cw_ui, 6);
        CW_CHECK(ToriRSChrome_Scale(&g_cw_ui) == 2 && ToriRSChrome_ScaleQ(&g_cw_ui) == 6,
            "1.5x reads its bitmaps from tier 2");
        (void)cw_run();
        cmd = cw_find(TORIRS_CHROME_CMD_SCALE, -1);
        CW_CHECK(cw_count(TORIRS_CHROME_CMD_SCALE) == 1, "a scale change sends one SCALE");
        CW_CHECK(cmd && cmd->value == 6 && cmd->w == 4, "SCALE carries the rational 6 / 4");
        CW_CHECK(cmd && cmd->panel == -1 && cmd->widget == -1, "SCALE is chrome-wide");
        (void)cw_run();
        CW_CHECK(cw_count(TORIRS_CHROME_CMD_SCALE) == 0, "and nothing on a steady frame");

        ToriRSChrome_SetScaleQ(&g_cw_ui, 6);
        (void)cw_run();
        CW_CHECK(cw_count(TORIRS_CHROME_CMD_SCALE) == 0, "setting the same scale sends nothing");

        ToriRSChrome_SetScale(&g_cw_ui, 1);
        (void)cw_run();
        cmd = cw_find(TORIRS_CHROME_CMD_SCALE, -1);
        CW_CHECK(cmd && cmd->value == 4 && cmd->w == 4, "back to 1x is said, as 4 / 4");

        /* A fresh executor bound to a chrome already at 1.25x is told so
         * before any panel. */
        ToriRSChrome_SetScaleQ(&g_cw_ui, 5);
        {
            struct ToriRSChromeExec exec;
            ToriRSChromeRecorder_Init(&g_cw_rec);
            exec = ToriRSChromeExec_Recorder(&g_cw_rec);
            (void)ToriRSChromeSync_Init(&g_cw_sync, &exec);
        }
        (void)cw_run();
        cmd = cw_find(TORIRS_CHROME_CMD_SCALE, -1);
        CW_CHECK(cmd && cmd->value == 5, "a fresh executor hears the scale");
        {
            int scale_at = -1;
            int open_at = -1;
            for( int i = 0; i < g_cw_rec.count; i++ )
            {
                if( g_cw_rec.cmds[i].kind == TORIRS_CHROME_CMD_SCALE && scale_at < 0 )
                    scale_at = i;
                if( g_cw_rec.cmds[i].kind == TORIRS_CHROME_CMD_PANEL_OPEN && open_at < 0 )
                    open_at = i;
            }
            CW_CHECK(scale_at >= 0 && open_at > scale_at, "before any panel opens");
        }

        /* The page's own density (ToriRSChrome_SetPageScale): a DOM's CSS
         * pixel is already devicePixelRatio device pixels, so the zoom it is
         * told is the scale over that. A 2x chrome on a 2x page is no zoom at
         * all -- the page is the canvas chrome's physical size, never 4x. */
        ToriRSChrome_SetScale(&g_cw_ui, 1);
        ToriRSChrome_SetPageScale(&g_cw_ui, 4, 100);
        (void)cw_run();
        ToriRSChrome_SetScale(&g_cw_ui, 2);
        ToriRSChrome_SetPageScale(&g_cw_ui, 8, 200);
        (void)cw_run();
        CW_CHECK(cw_count(TORIRS_CHROME_CMD_SCALE) == 0,
            "SetScale(2) on a DPR-2 page sends no SCALE: the DOM zoom stays 1, as before the rational scale");
        CW_CHECK(g_cw_sync.scale_value == g_cw_sync.scale_den, "the executor holds a zoom of 1");
        {
            struct ToriRSChromeExec exec;
            ToriRSChromeRecorder_Init(&g_cw_rec);
            exec = ToriRSChromeExec_Recorder(&g_cw_rec);
            (void)ToriRSChromeSync_Init(&g_cw_sync, &exec);
        }
        (void)cw_run();
        CW_CHECK(cw_count(TORIRS_CHROME_CMD_SCALE) == 0,
            "a fresh executor on a DPR-2 page at Auto hears no SCALE: unzoomed is its default");
        ToriRSChrome_SetPageScale(&g_cw_ui, 8, 100);
        (void)cw_run();
        cmd = cw_find(TORIRS_CHROME_CMD_SCALE, -1);
        CW_CHECK(cmd && cmd->value == 8 && cmd->w == 4, "on a 1x page the same 2x is 8 / 4, a 2x zoom");
        ToriRSChrome_SetPageScale(&g_cw_ui, 6, 150);
        (void)cw_run();
        cmd = cw_find(TORIRS_CHROME_CMD_SCALE, -1);
        CW_CHECK(cmd && cmd->value == 12 && cmd->w == 12,
            "Auto on a 1.5x page is 1.5x over 1.5: no zoom, said once as 12 / 12 after the 2x");
        /* The page scale is the asked one, finer than the layout's: the
         * native chrome draws 1.5x at 2x (main.c), the DOM zooms 1.5x. */
        ToriRSChrome_SetPageScale(&g_cw_ui, 6, 100);
        (void)cw_run();
        cmd = cw_find(TORIRS_CHROME_CMD_SCALE, -1);
        CW_CHECK(cmd && cmd->value == 6 && cmd->w == 4 && ToriRSChrome_ScaleQ(&g_cw_ui) == 8,
            "a 1.5x page scale over a 2x layout says 6 / 4");
        ToriRSChrome_SetPageScale(&g_cw_ui, 6, 100);
        (void)cw_run();
        CW_CHECK(cw_count(TORIRS_CHROME_CMD_SCALE) == 0, "the same page scale again says nothing");
        ToriRSChrome_SetScale(&g_cw_ui, 1);
        ToriRSChrome_SetPageScale(&g_cw_ui, 4, 100);
        (void)cw_run();
    }

    /* ---- every row in a panel is the same height, at every scale ---- */
    for( int q = TORIRS_CHROME_SCALE_Q_MIN; q <= TORIRS_CHROME_SCALE_Q_MAX; q++ )
    {
        int rows[16];
        int count = 0;
        int uneven = 0;
        int pitch_uneven = 0;
        char what[96];

        sc_setup(q);
        (void)sc_build_catalog(&g_cw_ui, rows, &count);
        (void)ToriRSChrome_Build(&g_cw_ui);
        for( int i = 0; i < count; i++ )
            if( g_cw_ui.widgets[rows[i]].h != g_cw_ui.widgets[rows[0]].h )
                uneven++;
        for( int i = 2; i < count; i++ )
            if( g_cw_ui.widgets[rows[i]].y - g_cw_ui.widgets[rows[i - 1]].y !=
                g_cw_ui.widgets[rows[1]].y - g_cw_ui.widgets[rows[0]].y )
                pitch_uneven++;
        snprintf(what, sizeof(what), "q = %d: every row is the same height", q);
        CW_CHECK(uneven == 0, what);
        snprintf(what, sizeof(what), "q = %d: every row is the same pitch", q);
        CW_CHECK(pitch_uneven == 0, what);
        snprintf(what, sizeof(what), "q = %d: a row is ScalePx(18) tall", q);
        CW_CHECK(g_cw_ui.widgets[rows[0]].h == ToriRSChrome_ScalePx(q, TORIRS_CHROME_M_ROW_H), what);
    }

    /* ---- the photographs: one Catalog per scale, for review ---- */
    for( int i = 0; i < (int)(sizeof(photographed) / sizeof(photographed[0])); i++ )
    {
        int const q = photographed[i];
        int rows[16];
        int count = 0;
        int panel;
        struct ToriRSChromeRect r;
        uint32_t* pixels;
        int w;
        int h;
        char path[256];

        sc_setup(q);
        panel = sc_build_catalog(&g_cw_ui, rows, &count);
        (void)ToriRSChrome_Build(&g_cw_ui);
        CW_CHECK(g_cw_ui.overflow == 0, "the Catalog fit the display list");
        r = ToriRSChrome_PanelRect(&g_cw_ui, panel);
        w = r.x + r.w + ToriRSChrome_Px(&g_cw_ui, 8);
        h = r.y + r.h + ToriRSChrome_Px(&g_cw_ui, 8);
        pixels = malloc(sizeof(uint32_t) * (size_t)w * (size_t)h);
        CW_CHECK(pixels != NULL, "photograph buffer");
        if( !pixels )
            continue;
        for( int k = 0; k < w * h; k++ )
            pixels[k] = 0xFF1E1E1Eu;
        ToriRSChromeBake_Present(&g_cw_ui, pixels, w, h, w);
        snprintf(path, sizeof(path), "%s/scale_q%d.bmp", CW_OUT_DIR, q);
        sc_write_bmp(path, pixels, w, h);
        printf("chrome scale: wrote %s (%dx%d)\n", path, w, h);
        free(pixels);
    }
    cw_reset();
}
