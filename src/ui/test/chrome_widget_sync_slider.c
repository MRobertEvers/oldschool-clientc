#include "chrome_widget_sync.h"

#include <stdio.h>
#include <string.h>

/* SLIDER: an int range on VALUE (value, min, max, step), a knob that sweeps,
 * the wheel and the arrows step it, and VALUE is the intent that sets it. */
void
cw_test_slider(void)
{
    struct ToriRSChromeCmd const* cmd;
    int panel;
    int radius;
    struct ToriRSChromeWidget const* w;
    int left;
    int right;

    cw_reset();
    panel = ToriRSChrome_PanelAdd(&g_cw_ui, TORIRS_CHROME_PANEL_WINDOW, 10, 10, 300, "Brush");
    radius = ToriRSChrome_Slider(&g_cw_ui, panel, "Radius", 1, 16, 4, 1);
    CW_CHECK(radius >= 0 && ToriRSChrome_SliderValue(&g_cw_ui, radius) == 4, "a slider at 4");
    cw_run();
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_VALUE, radius);
    CW_CHECK(cmd && cmd->value == 4 && cmd->x == 1 && cmd->y == 16 && cmd->w == 1,
        "VALUE carries value, min, max and step");
    CW_CHECK(cw_prims_with_slot(TORIRS_CHROME_SKIN_SLIDER_KNOB) == 1, "the baked knob is drawn");

    CW_CHECK(cw_prims_with_slot(TORIRS_CHROME_SKIN_SLIDER_TRACK_LEFT) == 1 &&
                 cw_prims_with_slot(TORIRS_CHROME_SKIN_SLIDER_TRACK_RIGHT) == 1 &&
                 cw_prims_with_slot(TORIRS_CHROME_SKIN_SLIDER_TRACK) >= 4,
        "the track is one bar: two caps and the middle repeated between them");

    /* Drag the knob from the left end to the right end. The caption column
     * is 30% of the row (a short caption), so the track is over half of it. */
    w = &g_cw_ui.widgets[radius];
    left = w->x + w->w * 30 / 100 + 2;
    right = w->x + w->w - 36 - 2;
    CW_CHECK(right - left >= w->w / 2, "the track takes at least half the row");
    ToriRSChrome_MouseMove(&g_cw_ui, left, w->y + 9);
    ToriRSChrome_MouseDown(&g_cw_ui, left, w->y + 9);
    CW_CHECK(ToriRSChrome_SliderValue(&g_cw_ui, radius) == 1, "pressing at the left end is the minimum");
    CW_CHECK(ToriRSChrome_WantsPointer(&g_cw_ui, 790, 590), "the drag owns the pointer anywhere");
    ToriRSChrome_MouseMove(&g_cw_ui, right + 200, w->y + 300);
    CW_CHECK(ToriRSChrome_SliderValue(&g_cw_ui, radius) == 16, "dragging past the end clamps to the maximum");
    CW_CHECK(ToriRSChrome_TakeActivated(&g_cw_ui) == -1, "no activation until release");
    ToriRSChrome_MouseUp(&g_cw_ui, right, w->y + 9);
    CW_CHECK(ToriRSChrome_TakeActivated(&g_cw_ui) == radius, "release activates");

    /* Wheel and keys step. */
    (void)ToriRSChrome_Build(&g_cw_ui);
    ToriRSChrome_MouseWheel(&g_cw_ui, w->x + 150, w->y + 9, 1);
    CW_CHECK(ToriRSChrome_SliderValue(&g_cw_ui, radius) == 15, "the wheel steps down");
    (void)ToriRSChrome_FocusWidget(&g_cw_ui, radius, 0);
    ToriRSChrome_KeyEdit(&g_cw_ui, TORIRS_CHROME_KEY_LEFT);
    CW_CHECK(ToriRSChrome_SliderValue(&g_cw_ui, radius) == 14, "Left steps down");
    ToriRSChrome_KeyEdit(&g_cw_ui, TORIRS_CHROME_KEY_HOME);
    CW_CHECK(ToriRSChrome_SliderValue(&g_cw_ui, radius) == 1, "Home is the minimum");

    /* The intent, with snapping to the step. */
    ToriRSChrome_SliderSetRange(&g_cw_ui, radius, 0, 100, 5);
    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_VALUE, panel, radius, 42, 0, 0, NULL) == 1 &&
                 ToriRSChrome_SliderValue(&g_cw_ui, radius) == 40,
        "VALUE sets the slider, snapped to its step");
    cw_run();
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_VALUE, radius);
    CW_CHECK(cmd && cmd->value == 40 && cmd->y == 100 && cmd->w == 5, "and the executor is told");
    ToriRSChrome_SliderSet(&g_cw_ui, radius, 41);
    CW_CHECK(cw_run() == 0, "a value that snaps to the same step records nothing");
}

/* The leftmost x of the sprites in `slot`, or -1. */
static int
cw_slot_left(int slot)
{
    int n = 0;
    int left = -1;
    struct ToriRSChromePrim const* prims;

    (void)ToriRSChrome_Build(&g_cw_ui);
    prims = ToriRSChrome_Prims(&g_cw_ui, &n);
    for( int i = 0; i < n; i++ )
        if( prims[i].kind == TORIRS_CHROME_PRIM_SPRITE && prims[i].sprite_slot == slot
            && (left < 0 || prims[i].x < left) )
            left = prims[i].x;
    return left;
}

/* The text prim that starts with `head`, or NULL. */
static struct ToriRSChromePrim const*
cw_text_starting(char const* head)
{
    int n = 0;
    struct ToriRSChromePrim const* prims = ToriRSChrome_Prims(&g_cw_ui, &n);

    for( int i = 0; i < n; i++ )
        if( prims[i].kind == TORIRS_CHROME_PRIM_TEXT && prims[i].text
            && strncmp(prims[i].text, head, strlen(head)) == 0 )
            return &prims[i];
    return NULL;
}

/* A slider's caption with its chord suffix (the Terrain panel's "Strength
 * Shift+[ ]") is measured whole at every quarter scale: the track starts past
 * it, and a caption the row cannot hold beside the least track is ellipsised
 * before the track (R2), never drawn on under it. */
void
cw_test_slider_long_label(void)
{
    static int const k_q[] = { 4, 5, 6, 7, 8 };
    static char const k_caption[] = "Strength  Shift+[ ]";
    static char const k_long[] = "Strength of the smoothing brush, toward the neighbours  Ctrl+Shift+[ ]";

    for( int i = 0; i < (int)(sizeof(k_q) / sizeof(k_q[0])); i++ )
    {
        int const q = k_q[i];
        int panel;
        int slider;
        int track;
        int font;
        int end;
        char what[160];
        struct ToriRSChromePrim const* text;

        cw_reset();
        ToriRSChrome_SetScaleQ(&g_cw_ui, q);
        font = g_cw_ui.theme.font_row;
        /* The Terrain panel's width, 220 chrome px. */
        panel = ToriRSChrome_PanelAdd(
            &g_cw_ui, TORIRS_CHROME_PANEL_WINDOW, 10, 10, ToriRSChrome_Px(&g_cw_ui, 220), "Terrain");
        slider = ToriRSChrome_Slider(&g_cw_ui, panel, k_caption, 0, 100, 50, 5);
        (void)cw_run();
        track = cw_slot_left(TORIRS_CHROME_SKIN_SLIDER_TRACK_LEFT);
        text = cw_text_starting("Strength");
        snprintf(what, sizeof(what), "q=%d: the caption is drawn whole (%s)", q, text ? text->text : "none");
        CW_CHECK(text && strcmp(text->text, k_caption) == 0, what);
        end = text ? text->x + ToriRSChrome_MeasureTextQ(font, q, text->text) : -1;
        snprintf(what, sizeof(what), "q=%d: the track starts past the caption and its chord (text ends %d, track %d)",
            q, end, track);
        CW_CHECK(text && track >= 0 && end < track, what);

        /* A caption no row of this width holds beside a usable track. */
        ToriRSChrome_SetLabel(&g_cw_ui, slider, k_long);
        (void)cw_run();
        track = cw_slot_left(TORIRS_CHROME_SKIN_SLIDER_TRACK_LEFT);
        text = cw_text_starting("Strength");
        snprintf(what, sizeof(what), "q=%d: a caption too long for the row is ellipsised (%s)", q,
            text ? text->text : "none");
        CW_CHECK(text && strlen(text->text) > 3 && strcmp(text->text + strlen(text->text) - 3, "...") == 0, what);
        end = text ? text->x + ToriRSChrome_MeasureTextQ(font, q, text->text) : -1;
        snprintf(what, sizeof(what), "q=%d: and still ends before the track (%d, %d)", q, end, track);
        CW_CHECK(text && track >= 0 && end < track, what);
        snprintf(what, sizeof(what), "q=%d: the track keeps three knobs of travel", q);
        CW_CHECK(track >= 0
                     && g_cw_ui.widgets[slider].x + g_cw_ui.widgets[slider].w - ToriRSChrome_Px(&g_cw_ui, 36) - track
                            >= 3 * ToriRSChrome_Px(&g_cw_ui, 16),
            what);
    }
}
