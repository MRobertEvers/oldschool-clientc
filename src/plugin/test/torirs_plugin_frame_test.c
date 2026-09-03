#include "plugin/torirs_plugin_frame.h"

#include <stdio.h>
#include <string.h>

static int checks;
static int failures;

#define CHECK(expr, message)                                                      \
    do                                                                            \
    {                                                                             \
        checks++;                                                                 \
        if( !(expr) )                                                             \
        {                                                                         \
            failures++;                                                           \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (message)); \
        }                                                                         \
    } while( 0 )

static struct ToriRS_EngineFrameOffer const DESKTOP[] = {
    { "classic-fixed", "Classic Fixed", TORIRS_PLUGIN_CANVAS_FIXED, 765, 503 },
    { "modern-resizable", "Modern Resizable", TORIRS_PLUGIN_CANVAS_FOLLOW_WINDOW,
      765, 503 },
    { NULL, NULL, 0, 0, 0 },
};

static struct ToriRS_EngineFrameOffer const MOBILE[] = {
    { "stone-drawer", "Stone Drawer", TORIRS_PLUGIN_CANVAS_FOLLOW_WINDOW, 640, 480 },
    { NULL, NULL, 0, 0, 0 },
};

int
main(void)
{
    struct PluginFrameCatalog a;
    struct PluginFrameCatalog b;
    struct PluginFrameCatalogEntry const* row;
    struct ToriRS_EngineFrameOffer invalid[] = {
        { "Bad/Id", "Bad", TORIRS_PLUGIN_CANVAS_FIXED, 765, 503 },
        { NULL, NULL, 0, 0, 0 },
    };

    PluginFrameCatalog_Init(&a);
    CHECK(PluginFrameCatalog_Count(&a) == 0, "a fresh catalogue is empty");
    CHECK(
        PluginFrameCatalog_Add(&a, 4, "gameframe-layout", DESKTOP) ==
            PLUGIN_FRAME_CATALOG_OK,
        "a provider publishes all static offers");
    CHECK(PluginFrameCatalog_Count(&a) == 2, "both desktop offers were committed");
    CHECK(
        PluginFrameCatalog_Find(&a, "gameframe-layout/modern-resizable") == 1,
        "canonical ids address offers directly");
    row = PluginFrameCatalog_At(&a, 1);
    CHECK(
        row && row->plugin == 4 && row->canvas == TORIRS_PLUGIN_CANVAS_FOLLOW_WINDOW &&
            row->width == 765 && row->height == 503,
        "the catalogue retains provider and canvas policy");

    CHECK(
        PluginFrameCatalog_Add(&a, 5, "mobile-gameframe", invalid) ==
            PLUGIN_FRAME_CATALOG_INVALID,
        "malformed local ids are refused");
    CHECK(
        PluginFrameCatalog_Count(&a) == 2,
        "an invalid provider cannot leave a partial catalogue behind");
    CHECK(
        PluginFrameCatalog_Add(&a, 4, "gameframe-layout", DESKTOP) ==
            PLUGIN_FRAME_CATALOG_DUPLICATE,
        "a duplicate canonical id fails loudly");
    CHECK(
        PluginFrameCatalog_Count(&a) == 2,
        "a duplicate provider is also atomic");

    CHECK(
        PluginFrameCatalog_Add(&a, 7, "mobile-gameframe", MOBILE) ==
            PLUGIN_FRAME_CATALOG_OK,
        "an independent mobile provider composes into the catalogue");
    PluginFrameCatalog_SetAvailable(&a, 7, 0);
    row = PluginFrameCatalog_At(
        &a, PluginFrameCatalog_Find(&a, "mobile-gameframe/stone-drawer"));
    CHECK(row && !row->available, "availability changes without removing the saved id");
    PluginFrameCatalog_RemovePlugin(&a, 7);
    CHECK(
        PluginFrameCatalog_Find(&a, "mobile-gameframe/stone-drawer") < 0 &&
            PluginFrameCatalog_Count(&a) == 2,
        "a failed provider registration can roll its offers back atomically");

    /* Registration order may affect presentation order, but never what an id
     * resolves to or which provider it names. */
    PluginFrameCatalog_Init(&b);
    CHECK(
        PluginFrameCatalog_Add(&b, 7, "mobile-gameframe", MOBILE) ==
                PLUGIN_FRAME_CATALOG_OK &&
            PluginFrameCatalog_Add(&b, 4, "gameframe-layout", DESKTOP) ==
                PLUGIN_FRAME_CATALOG_OK,
        "the reverse registration order is accepted");
    row = PluginFrameCatalog_At(
        &b, PluginFrameCatalog_Find(&b, "gameframe-layout/classic-fixed"));
    CHECK(
        row && row->plugin == 4 && strcmp(row->title, "Classic Fixed") == 0,
        "the exact saved id resolves identically in reverse order");

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
