#include "test_harness.h"

#include <stdlib.h>

/*
 * Emit-buffer golden snapshot: build a deterministic tree exercising layers,
 * scrolling, clipping, text, rects, sprites, and a drag, then serialize the
 * emit commands one per line and diff against the checked-in golden.
 *
 * The emit buffer is renderer-independent (logical geometry + scene ids), so
 * this catches layout/emit/scroll/drag regressions headlessly, with no cache
 * or SDL. Regenerate with UPDATE_GOLDEN=1 after an intentional change:
 *   UPDATE_GOLDEN=1 make -C src test-uitree
 */

#define GOLDEN_PATH "ui/test/golden/basic.emit.txt"
#define ACTUAL_PATH "build/basic.emit.actual.txt"

static void
serialize_emit(struct UITreeEmitBuffer const* buf, FILE* out)
{
    for( int i = 0; i < buf->count; i++ )
    {
        struct UITreeEmitDesc const* d = &buf->cmds[i];
        fprintf(
            out,
            "kind=%d id=%d xy=%d,%d wh=%d,%d clip=%d,%d,%d,%d scene=%d atlas=%d "
            "color=0x%x filled=%d if3=%d trans=%d scroll=%d,%d",
            (int)d->kind,
            d->component_id,
            d->x,
            d->y,
            d->w,
            d->h,
            d->clip.x,
            d->clip.y,
            d->clip.w,
            d->clip.h,
            d->scene_id,
            d->atlas_index,
            (unsigned)d->color,
            d->filled,
            (int)d->if3,
            d->trans,
            d->scroll_off_x,
            d->scroll_off_y);
        if( d->kind == UITREE_EMIT_TEXT && d->text )
            fprintf(out, " text=\"%s\" font=%d center=%d", d->text, d->font_id, d->text_center);
        fprintf(out, "\n");
    }
}

static char*
read_whole(char const* path, long* out_len)
{
    FILE* f = fopen(path, "rb");
    char* data;
    long len;
    if( !f )
        return NULL;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc((size_t)len + 1);
    if( !data )
    {
        fclose(f);
        return NULL;
    }
    if( len > 0 && fread(data, 1, (size_t)len, f) != (size_t)len )
    {
        free(data);
        fclose(f);
        return NULL;
    }
    data[len] = '\0';
    fclose(f);
    if( out_len )
        *out_len = len;
    return data;
}

static void
build_reference_tree(struct UITree* tree, struct UITreeHost* host)
{
    (void)host;

    /* Root layer with a title text and a scrollable list. */
    int32_t root = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, (500 << 16) | 0, 0, 0, 400, 300);

    struct UITreeNodeSpec txt;
    memset(&txt, 0, sizeof(txt));
    txt.type = UIELEM_RS_TEXT;
    txt.component_id = (500 << 16) | 1;
    txt.x = 10;
    txt.y = 4;
    txt.width = 380;
    txt.height = 20;
    txt.u.rs_text.text = "Golden Interface";
    txt.u.rs_text.color = 0xFF981F;
    txt.u.rs_text.font_id = 1;
    txt.u.rs_text.center = 1;
    UITree_Push(tree, root, &txt);

    /* Scrolled list: viewport 120 tall, content 300, scrolled by 60. */
    int32_t list =
        UITree_TestPushXy(tree, root, UIELEM_RS_LAYER, (500 << 16) | 2, 10, 30, 200, 120);
    tree->components[list].u.rs_layer.scroll_height = 300;

    for( int row = 0; row < 6; row++ )
    {
        int32_t r = UITree_TestPushXy(
            tree, list, UIELEM_RS_RECT, (500 << 16) | (10 + row), 4, row * 48, 180, 40);
        tree->components[r].behavior.button_type = 1;
        tree->components[r].u.rs_rect.color = 0x101010 * (row + 1);
        tree->components[r].u.rs_rect.filled = 1;
    }

    /* Item icon graphic (type-5 with item overlay). */
    struct UITreeNodeSpec gfx;
    memset(&gfx, 0, sizeof(gfx));
    gfx.type = UIELEM_RS_GRAPHIC;
    gfx.component_id = (500 << 16) | 20;
    gfx.x = 250;
    gfx.y = 40;
    gfx.width = 36;
    gfx.height = 32;
    gfx.u.rs_graphic.scene_id = 5;
    int32_t gi = UITree_Push(tree, root, &gfx);
    tree->components[gi].item_id = 4151;
    tree->components[gi].item_count = 1;
    tree->components[gi].item_scene_id = 42;

    /* Non-scrollable layer with an oversized child: every positive-size layer
     * clips its children, not only scrollable ones (readme "interfacex — layer
     * clipping", bank divider 488x0 in a 478x40 layer). The child keeps its
     * full width; the emitted clip is the layer's bounds. */
    int32_t frame =
        UITree_TestPushXy(tree, root, UIELEM_RS_LAYER, (500 << 16) | 40, 240, 140, 100, 40);
    int32_t divider = UITree_TestPushXy(
        tree, frame, UIELEM_RS_RECT, (500 << 16) | 41, -10, 10, 150, 8);
    tree->components[divider].u.rs_rect.color = 0x704A2C;
    tree->components[divider].u.rs_rect.filled = 1;

    /* A dragged widget rendered on the deferred pass. */
    struct UITreeNodeSpec drag;
    memset(&drag, 0, sizeof(drag));
    drag.type = UIELEM_RS_GRAPHIC;
    drag.component_id = (500 << 16) | 30;
    drag.x = 300;
    drag.y = 200;
    drag.width = 40;
    drag.height = 40;
    drag.u.rs_graphic.scene_id = 6;
    int32_t di = UITree_Push(tree, root, &drag);
    tree->components[di].draggable = 1;
    UITree_SetComponentDragActive(tree, di, 1);
    tree->components[di].drag_visual_x = 320;
    tree->components[di].drag_visual_y = 180;
    tree->components[di].drag_visual_trans = 128;

    UITree_TestResolve(tree);
    tree->components[list].scroll_y = 60;
}

void
test_emit_golden(void)
{
    printf("TEST: emit-buffer golden snapshot\n");

    struct UITree* tree = UITree_New(32);
    struct TestHostState hs;
    struct UITreeHost host;
    struct UITreeEmitBuffer buf;
    FILE* out;

    UITree_TestHostInit(&host, &hs);
    build_reference_tree(tree, &host);

    UITree_EmitBufferInit(&buf);
    UITree_EmitWalk(tree, &host, &buf, -1);

    out = fopen(ACTUAL_PATH, "wb");
    TEST_ASSERT(out != NULL, "open actual emit snapshot for write");
    if( out )
    {
        serialize_emit(&buf, out);
        fclose(out);
    }

    if( getenv("UPDATE_GOLDEN") )
    {
        FILE* g = fopen(GOLDEN_PATH, "wb");
        TEST_ASSERT(g != NULL, "open golden for write (UPDATE_GOLDEN)");
        if( g )
        {
            serialize_emit(&buf, g);
            fclose(g);
            printf("  golden updated: %s (%d cmds)\n", GOLDEN_PATH, buf.count);
        }
    }
    else
    {
        long glen = 0, alen = 0;
        char* golden = read_whole(GOLDEN_PATH, &glen);
        char* actual = read_whole(ACTUAL_PATH, &alen);
        TEST_ASSERT(golden != NULL, "golden exists (run UPDATE_GOLDEN=1 to create)");
        if( golden && actual )
        {
            int same = glen == alen && memcmp(golden, actual, (size_t)glen) == 0;
            TEST_ASSERT(same, "emit snapshot matches golden (diff " GOLDEN_PATH
                              " vs " ACTUAL_PATH ")");
        }
        free(golden);
        free(actual);
    }

    UITree_EmitBufferFree(&buf);
    UITree_Free(tree);
}
