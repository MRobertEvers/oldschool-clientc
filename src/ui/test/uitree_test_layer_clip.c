/*
 * A layer's children are clipped by EVERY ancestor layer on an OldSchool tree
 * (UITree.layer_clip_compounds; rev 239 class163.method5467), and only by
 * their own layer and the surface on a 2004 one.
 *
 * The scene is interface 660, the collection-log popup: a container that
 * notification_start (clientscript 3346) grows from 1x2, first in width to
 * 178, then in height to 100; a content layer the container's size; and the
 * title strip, a layer 8 px down from the content's top holding the thinbox
 * rectangles. While the container is 2 px tall the strip is wholly outside it
 * and the official client draws none of it. Clipped only to its own box, the
 * strip drew in full the moment the container was wider than 16 px.
 */
#include "test_harness.h"

static struct UITreeEmitDesc const*
rect_desc(struct UITreeEmitBuffer const* buffer, int32_t node)
{
    for( int i = 0; i < buffer->count; i++ )
        if( buffer->cmds[i].node_index == node && buffer->cmds[i].kind == UITREE_EMIT_RECT )
            return &buffer->cmds[i];
    return NULL;
}

/* Whether the clip-aware input walk (the one menus and clicks use) finds
 * `node` under the point. UITree_HitTest is a plain geometric walk that no
 * clip has ever applied to, so it cannot answer this. */
static int
collects(
    struct UITree const* tree,
    struct UITreeHost const* host,
    int px,
    int py,
    int32_t node)
{
    int32_t hits[16];
    int n = UITree_CollectNodesAt(tree, host, px, py, hits, 16);
    for( int i = 0; i < n; i++ )
        if( hits[i] == node )
            return 1;
    return 0;
}

void test_layer_clip_compounds(void)
{
    struct UITree* tree = UITree_New(16);
    struct UITreeHost host;
    struct TestHostState state;
    struct UITreeEmitBuffer buffer;
    int32_t root = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 660 << 16, 0, 0, 765, 503);
    int32_t container = UITree_TestPushXy(tree, root, UIELEM_RS_LAYER, (660 << 16) | 1, 100, 10, 1, 2);
    int32_t content = UITree_TestPushXy(tree, container, UIELEM_RS_LAYER, (660 << 16) | 2, 0, 0, 0, 0);
    int32_t title = UITree_TestPushXy(tree, content, UIELEM_RS_LAYER, (660 << 16) | 6, 0, 8, 16, 20);
    int32_t box = UITree_TestPushXy(tree, title, UIELEM_RS_RECT, -1, 0, 0, 0, 0);
    struct UITreeEmitDesc const* d;

    /* notification_display.if: content and box are their parent's size,
     * title is 16 narrower than the content, centred, at y=8. */
    UITree_SetSizeModesAt(tree, content, 0, 0, 1, 1);
    UITree_SetPositionModesAt(tree, title, 0, 8, 1, 0);
    UITree_SetSizeModesAt(tree, title, 16, 20, 1, 0);
    UITree_SetSizeModesAt(tree, box, 0, 0, 1, 1);
    /* An option makes the box a menu target, so the input walk reports it. */
    {
        struct UITreeMenuOptions* opts = UITree_MenuOptionsMut(&tree->components[box]);
        assert(opts);
        snprintf(opts->option, sizeof(opts->option), "Check");
    }
    UITree_TestHostInit(&host, &state);
    UITree_EmitBufferInit(&buffer);
#define PUBLISH() do { UITree_TestResolve(tree); buffer.count = 0; UITree_EmitWalk(tree, &host, &buffer, -1); } while( 0 )

    /* Phase 1, part way: 150 wide, still 2 tall. */
    UITree_SetSizeAt(tree, container, 150, 2);
    tree->layer_clip_compounds = 0;
    PUBLISH();
    d = rect_desc(&buffer, box);
    printf("LAYER_CLIP 2004 150x2: box %s collected=%d\n", d ? "drawn" : "absent",
           collects(tree, &host, 175, 25, box));
    TEST_ASSERT(d != NULL, "2004: a nested layer clips only to its own box, so the strip draws");

    tree->layer_clip_compounds = 1;
    PUBLISH();
    d = rect_desc(&buffer, box);
    printf("LAYER_CLIP osrs 150x2: box %s collected=%d\n", d ? "drawn" : "absent",
           collects(tree, &host, 175, 25, box));
    TEST_ASSERT(d == NULL, "OldSchool: a strip wholly below a 2px container draws nothing");
    TEST_ASSERT(!collects(tree, &host, 175, 25, box), "OldSchool: nor is it under the pointer");

    /* Phase 2, part way: the container has opened 20 of its 100 px, so the
     * strip (y 18..38 on screen) shows its top 12 rows and no more. */
    UITree_SetSizeAt(tree, container, 178, 20);
    PUBLISH();
    d = rect_desc(&buffer, box);
    printf("LAYER_CLIP osrs 178x20: box clip=%d,%d %dx%d\n",
           d ? d->clip.x : -1, d ? d->clip.y : -1, d ? d->clip.w : -1, d ? d->clip.h : -1);
    TEST_ASSERT(d && d->clip.y == 18 && d->clip.h == 12, "OldSchool: the strip is cut at the container's bottom");
    TEST_ASSERT(collects(tree, &host, 175, 25, box), "OldSchool: the rows shown are under the pointer");
    TEST_ASSERT(!collects(tree, &host, 175, 33, box), "OldSchool: the cut-off rows are not");

    tree->layer_clip_compounds = 0;
    PUBLISH();
    d = rect_desc(&buffer, box);
    TEST_ASSERT(d && d->clip.y == 18 && d->clip.h == 20, "2004: the strip keeps its whole box");

    /* Fully open: the whole strip, on either rule. */
    tree->layer_clip_compounds = 1;
    UITree_SetSizeAt(tree, container, 178, 100);
    PUBLISH();
    d = rect_desc(&buffer, box);
    TEST_ASSERT(d && d->clip.x == 108 && d->clip.y == 18 && d->clip.w == 162 && d->clip.h == 20,
                "OldSchool: an open container leaves the strip its own box");
#undef PUBLISH
    UITree_EmitBufferFree(&buffer);
    UITree_Free(tree);
}
