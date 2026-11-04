#include "chrome_widget_sync.h"

#include <string.h>

/* Truncation is never silent (the workbench contract's R2): a string longer
 * than its buffer is still cut, and recorded -- once per widget serial -- in a
 * ring the host drains for its console line. */
void
cw_test_truncation(void)
{
    struct ToriRSChromeTruncation rec;
    char longest[301];
    char chord[41];
    int panel;
    int label;
    int text;
    int button;
    int many[TORIRS_CHROME_TRUNCATION_RING + 4];
    int in_order = 1;

    memset(longest, 'x', sizeof(longest) - 1);
    longest[sizeof(longest) - 1] = '\0';
    memcpy(longest, "Settings for ", 13);
    memset(chord, 'K', sizeof(chord) - 1);
    chord[sizeof(chord) - 1] = '\0';

    cw_reset();
    panel = ToriRSChrome_PanelAdd(&g_cw_ui, TORIRS_CHROME_PANEL_WINDOW, 10, 10, 300, "Panel");
    CW_CHECK(ToriRSChrome_TakeTruncation(&g_cw_ui, &rec) == 0 && rec.widget == 0 && rec.field == 0,
        "an empty ring gives nothing, zeroed");

    /* A 300-byte label: cut to 63 as before, and recorded once. */
    label = ToriRSChrome_Checkbox(&g_cw_ui, panel, "Grid", 0);
    ToriRSChrome_SetLabel(&g_cw_ui, label, longest);
    CW_CHECK(strlen(g_cw_ui.widgets[label].label) == TORIRS_CHROME_LABEL_MAX - 1,
        "the label is still cut to its buffer");
    CW_CHECK(ToriRSChrome_TakeTruncation(&g_cw_ui, &rec) == 1, "and the cut is recorded");
    CW_CHECK(rec.widget == label && rec.serial == g_cw_ui.widgets[label].serial,
        "naming the widget and its serial");
    CW_CHECK(rec.widget_kind == TORIRS_CHROME_W_CHECKBOX, "and its kind");
    CW_CHECK(rec.field == TORIRS_CHROME_TRUNC_LABEL &&
                 strcmp(ToriRSChrome_TruncationFieldName(rec.field), "label") == 0,
        "and which string it was");
    CW_CHECK(rec.kept == TORIRS_CHROME_LABEL_MAX - 1 && rec.length == 300,
        "how much was kept of how much");
    CW_CHECK(strncmp(rec.head, "Settings for ", 13) == 0 &&
                 strlen(rec.head) == TORIRS_CHROME_TRUNCATION_HEAD - 1,
        "and the head of what was handed over");
    CW_CHECK(ToriRSChrome_TakeTruncation(&g_cw_ui, &rec) == 0, "one record, drained");

    /* The same widget rebuilt every frame with the same label says it once. */
    for( int i = 0; i < 5; i++ )
    {
        ToriRSChrome_SetLabel(&g_cw_ui, label, "short");
        ToriRSChrome_SetLabel(&g_cw_ui, label, longest);
    }
    CW_CHECK(ToriRSChrome_TakeTruncation(&g_cw_ui, &rec) == 0, "once per widget serial");

    /* A recycled handle is a new serial, and says it again. */
    ToriRSChrome_WidgetRemove(&g_cw_ui, label);
    label = ToriRSChrome_Checkbox(&g_cw_ui, panel, longest, 0);
    CW_CHECK(ToriRSChrome_TakeTruncation(&g_cw_ui, &rec) == 1 && rec.widget == label &&
                 rec.serial == g_cw_ui.widgets[label].serial,
        "a new widget in a recycled handle is recorded afresh, from its add call");

    /* Text (191 bytes), and a chord (23). */
    text = ToriRSChrome_TextInput(&g_cw_ui, panel, "Name", "");
    ToriRSChrome_SetText(&g_cw_ui, text, longest);
    button = ToriRSChrome_Button(&g_cw_ui, panel, "Build");
    ToriRSChrome_ButtonSetChord(&g_cw_ui, button, chord);
    CW_CHECK(strlen(g_cw_ui.widgets[button].chord) == TORIRS_CHROME_CHORD_MAX - 1,
        "a chord is cut to its buffer");
    CW_CHECK(ToriRSChrome_TakeTruncation(&g_cw_ui, &rec) == 1 && rec.widget == text &&
                 rec.field == TORIRS_CHROME_TRUNC_TEXT && rec.kept == TORIRS_CHROME_INPUT_MAX - 1,
        "a text cut is recorded as text");
    CW_CHECK(ToriRSChrome_TakeTruncation(&g_cw_ui, &rec) == 1 && rec.widget == button &&
                 rec.field == TORIRS_CHROME_TRUNC_CHORD &&
                 rec.kept == TORIRS_CHROME_CHORD_MAX - 1 && rec.length == 40,
        "and a chord cut as a chord, oldest first");
    CW_CHECK(strcmp(ToriRSChrome_TruncationFieldName(TORIRS_CHROME_TRUNC_TOOLTIP_CHORD),
                 "tooltip chord") == 0,
        "every field has a name for the console");

    /* The ring: four more cuts than it holds keeps the newest, oldest first,
     * and counts the four it overwrote. */
    CW_CHECK(ToriRSChrome_TakeTruncationsDropped(&g_cw_ui) == 0, "nothing dropped yet");
    for( int i = 0; i < TORIRS_CHROME_TRUNCATION_RING + 4; i++ )
    {
        many[i] = ToriRSChrome_Label(&g_cw_ui, panel, "row");
        ToriRSChrome_SetTooltip(&g_cw_ui, many[i], longest);
    }
    for( int i = 4; i < TORIRS_CHROME_TRUNCATION_RING + 4; i++ )
        if( !ToriRSChrome_TakeTruncation(&g_cw_ui, &rec) || rec.widget != many[i] ||
            rec.field != TORIRS_CHROME_TRUNC_TOOLTIP )
            in_order = 0;
    CW_CHECK(in_order, "a full ring keeps the newest, in order");
    CW_CHECK(ToriRSChrome_TakeTruncation(&g_cw_ui, &rec) == 0, "and no more than it holds");
    CW_CHECK(ToriRSChrome_TakeTruncationsDropped(&g_cw_ui) == 4, "the overwritten are counted");
    CW_CHECK(ToriRSChrome_TakeTruncationsDropped(&g_cw_ui) == 0, "and the count is taken once");

    /* A string that fits records nothing. */
    ToriRSChrome_SetLabel(&g_cw_ui, button, "Fits");
    ToriRSChrome_SetTooltipChord(&g_cw_ui, button, "F5");
    CW_CHECK(ToriRSChrome_TakeTruncation(&g_cw_ui, &rec) == 0, "a string that fits is not a cut");
}
