#include "test_harness.h"

#include <stdio.h>
#include <stdlib.h>

#include <toridraw_font.h>

int g_failures = 0;

/* toridraw_font.c is not part of the current toridraw build, but ToriDraw_SceneClear /
 * ToriDraw_SceneGraphShutdown reference ToriDraw_FontFree. Provide a matching implementation
 * so the scene graph links; our tests never register fonts, so it is effectively a no-op. */
void
ToriDraw_FontFree(struct ToriDraw_Font* font)
{
    if( !font )
        return;
    for( int i = 0; i < TORIDRAW_FONT_GLYPH_COUNT; i++ )
    {
        free(font->glyph_alpha[i]);
        font->glyph_alpha[i] = NULL;
    }
    free(font);
}

int
main(void)
{
    printf("== world_builder unit tests ==\n");
    test_painters_smoke();
    test_builder_lifecycle();

    printf("== world_builder cache render test ==\n");
    test_world_builder_cache_render();

    if( g_failures == 0 )
        printf("ALL TESTS PASSED\n");
    else
        printf("%d TEST(S) FAILED\n", g_failures);

    return g_failures == 0 ? 0 : 1;
}
