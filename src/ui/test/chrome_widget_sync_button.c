#include "chrome_widget_sync.h"

#include "ui/torirs_chrome_metrics.h"

#include <string.h>

/* The prim that draws exactly `text` (the pointer, when `borrowed`, or the
 * bytes), or NULL. */
static struct ToriRSChromePrim const*
cw_text_prim(char const* text, int borrowed)
{
    int n = 0;
    struct ToriRSChromePrim const* prims = ToriRSChrome_Prims(&g_cw_ui, &n);

    for( int i = 0; i < n; i++ )
    {
        if( prims[i].kind != TORIRS_CHROME_PRIM_TEXT || !prims[i].text )
            continue;
        if( borrowed ? prims[i].text == text : strcmp(prims[i].text, text) == 0 )
            return &prims[i];
    }
    return NULL;
}

/* The filled rect of `color` whose right edge is `right`, or NULL. */
static struct ToriRSChromePrim const*
cw_fill_ending_at(uint32_t color, int right, int y)
{
    int n = 0;
    struct ToriRSChromePrim const* prims = ToriRSChrome_Prims(&g_cw_ui, &n);

    for( int i = 0; i < n; i++ )
        if( prims[i].kind == TORIRS_CHROME_PRIM_RECT && prims[i].filled &&
            prims[i].color == color && prims[i].x + prims[i].w == right && prims[i].y == y )
            return &prims[i];
    return NULL;
}

/* BUTTON's chord slot (the workbench design's section 15, surface 3): the
 * setter, the keycap Build draws right-aligned in the plate with the caption
 * elided in what is left, the ACTIVATE intent unchanged, and WIDGET_CHORD. */
void
cw_test_button(void)
{
    struct ToriRSChromeTheme const* th;
    struct ToriRSChromeCmd const* cmd;
    struct ToriRSChromePrim const* key;
    struct ToriRSChromePrim const* cap;
    struct ToriRSChromePrim const* caption;
    struct ToriRSChromeWidget const* w;
    struct ToriRSChromeWidget const* plain_w;
    int panel;
    int build;
    int plain;
    int lng;
    int add_at = -1;
    int chord_at = -1;
    int cap_x;
    int caption_w;

    cw_reset();
    th = &g_cw_ui.theme;
    panel = ToriRSChrome_PanelAdd(&g_cw_ui, TORIRS_CHROME_PANEL_WINDOW, 10, 10, 300, "Panel");
    build = ToriRSChrome_Button(&g_cw_ui, panel, "Build");
    plain = ToriRSChrome_Button(&g_cw_ui, panel, "Revert");
    lng = ToriRSChrome_Button(&g_cw_ui, panel, "Pack every server script");
    ToriRSChrome_ButtonSetChord(&g_cw_ui, build, "Ctrl+B");
    ToriRSChrome_ButtonSetChord(&g_cw_ui, lng, "F5");
    CW_CHECK(strcmp(g_cw_ui.widgets[build].chord, "Ctrl+B") == 0, "the setter copies the chord");

    /* The command: one WIDGET_CHORD after the ADD, none for a button without. */
    cw_run();
    for( int i = 0; i < g_cw_rec.count; i++ )
    {
        if( g_cw_rec.cmds[i].widget != build )
            continue;
        if( g_cw_rec.cmds[i].kind == TORIRS_CHROME_CMD_WIDGET_ADD )
            add_at = i;
        if( g_cw_rec.cmds[i].kind == TORIRS_CHROME_CMD_WIDGET_CHORD )
            chord_at = i;
    }
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_CHORD, build);
    CW_CHECK(cmd && strcmp(cmd->label, "Ctrl+B") == 0, "WIDGET_CHORD carries the chord");
    CW_CHECK(add_at >= 0 && chord_at > add_at, "after the ADD it describes");
    CW_CHECK(cw_find(TORIRS_CHROME_CMD_WIDGET_CHORD, plain) == NULL,
        "a button with no chord is sent none");

    /* What Build draws: the keycap three pixels inside the plate's right
     * edge, twelve tall, the chord borrowed from the widget in the Small
     * face and the hint yellow. The row is one grid row either way. */
    w = &g_cw_ui.widgets[build];
    plain_w = &g_cw_ui.widgets[plain];
    CW_CHECK(w->h == TORIRS_CHROME_M_ROW_H && plain_w->h == TORIRS_CHROME_M_ROW_H,
        "a keycap does not change the row height");
    CW_CHECK(w->w == plain_w->w && w->w == TORIRS_CHROME_M_LABEL_W,
        "nor the plate's width");
    key = cw_text_prim(w->chord, 1);
    CW_CHECK(key != NULL, "the chord is drawn, borrowed from the widget");
    CW_CHECK(key && key->font_slot == TORIRS_CHROME_FONT_SMALL, "in the Small face");
    CW_CHECK(key && key->color == th->menu_hover_text, "in the hint yellow");
    cap = cw_fill_ending_at(
        th->input_bg,
        w->x + w->w - TORIRS_CHROME_M_KEYCAP_INSET,
        w->y + TORIRS_CHROME_M_KEYCAP_INSET);
    CW_CHECK(cap != NULL, "a dark key, right-aligned inside the plate");
    CW_CHECK(cap && cap->h == TORIRS_CHROME_M_KEYCAP_H, "twelve tall, inside the row");
    CW_CHECK(cap &&
            cap->w == ToriRSChrome_MeasureText(TORIRS_CHROME_FONT_SMALL, 1, "Ctrl+B") +
                          2 * TORIRS_CHROME_M_KEYCAP_PAD_X,
        "as wide as the chord and its air");
    CW_CHECK(key && cap && key->x >= cap->x && key->x < cap->x + cap->w, "the chord on the key");
    caption = cw_text_prim(w->text, 1);
    CW_CHECK(caption != NULL, "a caption that fits is the widget's own, not a copy");
    CW_CHECK(cw_text_prim(plain_w->text, 1) != NULL, "a button without a key is drawn as before");

    /* A caption that does not fit beside the key ends in an ellipsis inside
     * the track the key leaves. */
    w = &g_cw_ui.widgets[lng];
    cap_x = w->x + w->w - TORIRS_CHROME_M_KEYCAP_INSET -
            (ToriRSChrome_MeasureText(TORIRS_CHROME_FONT_SMALL, 1, "F5") +
             2 * TORIRS_CHROME_M_KEYCAP_PAD_X);
    CW_CHECK(cw_text_prim(w->text, 1) == NULL, "a long caption is not drawn whole");
    {
        int n = 0;
        struct ToriRSChromePrim const* prims = ToriRSChrome_Prims(&g_cw_ui, &n);
        caption = NULL;
        for( int i = 0; i < n; i++ )
            if( prims[i].kind == TORIRS_CHROME_PRIM_TEXT && prims[i].text &&
                strncmp(prims[i].text, "Pack", 4) == 0 )
                caption = &prims[i];
    }
    CW_CHECK(caption && strlen(caption->text) > 3 &&
                 strcmp(caption->text + strlen(caption->text) - 3, "...") == 0,
        "it ends in an ellipsis");
    caption_w = caption ? ToriRSChrome_MeasureText(g_cw_ui.theme.font_row, 1, caption->text) : 0;
    CW_CHECK(caption && caption->x + caption_w <= cap_x - TORIRS_CHROME_M_KEYCAP_GAP,
        "measured against the room left of the key");
    CW_CHECK(caption && caption->x >= w->x + TORIRS_CHROME_M_FIELD_PAD_X, "and right of the inset");

    /* The intent: the key is part of the button, so a click on it presses it,
     * and ACTIVATE from a presentation is unchanged. */
    w = &g_cw_ui.widgets[build];
    cw_click(w->x + w->w - TORIRS_CHROME_M_KEYCAP_INSET - 2, w->y + w->h / 2);
    CW_CHECK(ToriRSChrome_TakeActivated(&g_cw_ui) == build,
        "a click on the key presses the button");
    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_ACTIVATE, panel, build, 0, 0, 0, NULL) == 1 &&
                 ToriRSChrome_TakeActivated(&g_cw_ui) == build,
        "ACTIVATE fires a button that wears a chord");

    /* Disabled: the key stays, dimmed, so the chord is still learned. */
    ToriRSChrome_SetDisabled(&g_cw_ui, build, 1);
    (void)ToriRSChrome_Build(&g_cw_ui);
    key = cw_text_prim(g_cw_ui.widgets[build].chord, 1);
    CW_CHECK(key && key->color == th->text_dim, "a disabled button's key is dimmed, not dropped");
    ToriRSChrome_SetDisabled(&g_cw_ui, build, 0);

    /* Compare-then-set, and "" clears. */
    cw_run();
    ToriRSChrome_ButtonSetChord(&g_cw_ui, build, "Ctrl+B");
    CW_CHECK(cw_run() == 0, "the same chord again records nothing");
    ToriRSChrome_ButtonSetChord(&g_cw_ui, build, "F7");
    cw_run();
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_CHORD, build);
    CW_CHECK(cmd && strcmp(cmd->label, "F7") == 0 && cw_count(TORIRS_CHROME_CMD_WIDGET_CHORD) == 1,
        "a rebind is one WIDGET_CHORD");
    ToriRSChrome_ButtonSetChord(&g_cw_ui, build, "");
    cw_run();
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_CHORD, build);
    CW_CHECK(cmd && cmd->label[0] == '\0', "clearing it is sent as an empty chord");
    (void)ToriRSChrome_Build(&g_cw_ui);
    CW_CHECK(cw_fill_ending_at(
                 th->input_bg,
                 g_cw_ui.widgets[build].x + g_cw_ui.widgets[build].w - TORIRS_CHROME_M_KEYCAP_INSET,
                 g_cw_ui.widgets[build].y + TORIRS_CHROME_M_KEYCAP_INSET) == NULL,
        "and the key is gone");

    /* A fresh executor is caught up with the chord in the snapshot. */
    ToriRSChrome_ButtonSetChord(&g_cw_ui, build, "Ctrl+B");
    {
        struct ToriRSChromeExec exec = ToriRSChromeExec_Recorder(&g_cw_rec);
        (void)ToriRSChromeSync_Init(&g_cw_sync, &exec);
    }
    cw_run();
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_CHORD, build);
    CW_CHECK(cmd && strcmp(cmd->label, "Ctrl+B") == 0, "a full snapshot restates the chord");

    /* At 2x the key is still sized from the row: twice the inset, twice the
     * height, and the row is still one grid row. */
    ToriRSChrome_SetScale(&g_cw_ui, 2);
    (void)ToriRSChrome_Build(&g_cw_ui);
    w = &g_cw_ui.widgets[build];
    cap = cw_fill_ending_at(
        th->input_bg,
        w->x + w->w - 2 * TORIRS_CHROME_M_KEYCAP_INSET,
        w->y + 2 * TORIRS_CHROME_M_KEYCAP_INSET);
    CW_CHECK(w->h == 2 * TORIRS_CHROME_M_ROW_H && cap && cap->h == 2 * TORIRS_CHROME_M_KEYCAP_H,
        "at 2x the key scales with the row");
}
