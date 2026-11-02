#include "chrome_widget_sync.h"

#include "engine/torirs_chrome_skin_baked.h"
#include "ui/torirs_chrome_skin.h"

/* The baked editor slots: every one is in the bake, the model's size table
 * agrees with it (that is what draws an icon at 2x without stretching it), and
 * the 64-bit availability mask reaches past slot 32. */
void
cw_test_skin(void)
{
    int mismatched = 0;
    int missing = 0;

    CW_CHECK(TORIRS_CHROME_SKIN_SLOT_COUNT == ToriRSChromeSkin_Count(),
        "the semantic slots and the bake agree on the count");
    for( int slot = TORIRS_CHROME_SKIN_TOOL_SELECT; slot < TORIRS_CHROME_SKIN_SLOT_COUNT; slot++ )
    {
        struct ToriRSChromeSkin_Sprite const* sprite = ToriRSChromeSkin_ForSlot(slot);
        int w = 0;
        int h = 0;
        if( !sprite || !sprite->argb || sprite->w <= 0 || sprite->h <= 0 )
        {
            missing++;
            continue;
        }
        if( !ToriRSChrome_SkinSlotSize(slot, &w, &h) || w != sprite->w || h != sprite->h )
        {
            mismatched++;
            fprintf(stderr, "slot %d: model says %dx%d, bake has %dx%d\n", slot, w, h, sprite->w,
                sprite->h);
        }
    }
    CW_CHECK(missing == 0, "every editor slot is baked");
    CW_CHECK(mismatched == 0, "the model's slot sizes are the bake's");
    {
        /* Nothing opaque is an icon nobody can see. */
        int blank = 0;
        for( int slot = TORIRS_CHROME_SKIN_TOOL_SELECT; slot < TORIRS_CHROME_SKIN_SLOT_COUNT; slot++ )
        {
            struct ToriRSChromeSkin_Sprite const* sprite = ToriRSChromeSkin_ForSlot(slot);
            int opaque = 0;
            for( int i = 0; sprite && i < sprite->w * sprite->h; i++ )
                opaque += (sprite->argb[i] >> 24) != 0;
            blank += opaque == 0;
        }
        CW_CHECK(blank == 0, "no editor slot is blank");
    }

    cw_reset();
    ToriRSChrome_SetSkinAvailable(&g_cw_ui, TORIRS_CHROME_SKIN_SAVE, 0);
    CW_CHECK((g_cw_ui.skin_avail >> TORIRS_CHROME_SKIN_SAVE & 1u) == 0 &&
                 (g_cw_ui.skin_avail >> TORIRS_CHROME_SKIN_STOP & 1u) == 1 &&
                 (g_cw_ui.skin_avail & 1u) == 1,
        "a slot past 32 clears its own bit and no other");
    {
        int panel = ToriRSChrome_PanelAdd(&g_cw_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 200, "S");
        (void)ToriRSChrome_IconButton(&g_cw_ui, panel, TORIRS_CHROME_SKIN_SAVE, "Save");
        CW_CHECK(cw_prims_with_slot(TORIRS_CHROME_SKIN_SAVE) == 0,
            "an unavailable icon falls back to its flat form");
        ToriRSChrome_SetScale(&g_cw_ui, 2);
        ToriRSChrome_SetSkinAvailable(&g_cw_ui, TORIRS_CHROME_SKIN_SAVE, 1);
        {
            int n = 0;
            int sized = 0;
            struct ToriRSChromePrim const* prims;
            (void)ToriRSChrome_Build(&g_cw_ui);
            prims = ToriRSChrome_Prims(&g_cw_ui, &n);
            for( int i = 0; i < n; i++ )
                if( prims[i].kind == TORIRS_CHROME_PRIM_SPRITE &&
                    prims[i].sprite_slot == TORIRS_CHROME_SKIN_SAVE && prims[i].w == 32 &&
                    prims[i].h == 32 )
                    sized = 1;
            CW_CHECK(sized, "at 2x an icon is drawn at twice its baked size");
        }
    }
}
