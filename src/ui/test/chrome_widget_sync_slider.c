#include "chrome_widget_sync.h"

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

    /* Drag the knob from the left end to the right end. */
    w = &g_cw_ui.widgets[radius];
    left = w->x + 104 + 2;
    right = w->x + w->w - 36 - 2;
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
