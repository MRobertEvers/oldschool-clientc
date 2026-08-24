#include "test_harness.h"

enum RefreshMutationKind
{
    REFRESH_MUTATION_NONE,
    REFRESH_MUTATION_HOST,
    REFRESH_MUTATION_LAYOUT,
    REFRESH_MUTATION_HOVER,
};

struct RefreshMutationProbe
{
    struct UITree* tree;
    struct UITreeHost* host;
    int* hovered_component_id;
    enum RefreshMutationKind mutation;
    int armed;
};

static int
find_emit_desc(
    struct UITreeEmitBuffer const* buf,
    int component_id)
{
    for( int i = 0; i < buf->count; i++ )
        if( buf->cmds[i].component_id == component_id )
            return i;
    return -1;
}

static int
refresh_mutation_request(void* user, struct UITreeHostRequest* req)
{
    struct RefreshMutationProbe* probe = (struct RefreshMutationProbe*)user;

    if( probe->armed && req->kind == UITREE_HOST_BEGIN_OVERLAYS )
    {
        probe->armed = 0;
        switch( probe->mutation )
        {
        case REFRESH_MUTATION_HOST:
            UITree_HostInputsChanged(
                probe->host,
                UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_OVERLAYS));
            break;
        case REFRESH_MUTATION_LAYOUT:
            UITree_LayoutInvalidateBoxes(probe->tree);
            break;
        case REFRESH_MUTATION_HOVER:
            *probe->hovered_component_id = 680;
            break;
        case REFRESH_MUTATION_NONE:
            break;
        }
    }
    return UITree_Host(NULL, req);
}

static void
test_emit_retain_gate(void)
{
    struct UITree* tree = UITree_New(8);
    struct UITree* other_tree = UITree_New(1);
    struct TestHostState hs;
    struct UITreeHost host;
    struct UITreeEmitBuffer emit;
    struct UITreeEmitBuffer other_emit;
    struct UITreeEmitRetainGate gate = { 0 };
    UITreeHostInputMask const camera = UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_CAMERA);
    UITreeHostInputMask const overlays = UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_OVERLAYS);
    int32_t rect;

    printf("TEST: production retained-emit gate identity\n");
    TEST_ASSERT(tree != NULL, "retain-gate tree allocation");
    if( !tree )
    {
        UITree_Free(other_tree);
        return;
    }
    TEST_ASSERT(other_tree != NULL, "retain-gate second tree allocation");

    UITree_TestHostInit(&host, &hs);
    rect = UITree_TestPushXy(tree, -1, UIELEM_RS_RECT, 680, 5, 6, 40, 30);
    TEST_ASSERT(rect >= 0, "retain-gate fixture component");
    UITree_TestResolve(tree);
    UITree_EmitBufferInit(&emit);
    UITree_EmitBufferInit(&other_emit);
    UITree_EmitWalk(tree, &host, &emit, -1);

    TEST_ASSERT(
        !UITree_EmitRetainGateQuiet(tree, &host, &emit, -1, &gate),
        "an unprimed gate never retains");
    UITree_EmitRetainGateCapture(tree, &emit, -1, &gate);
    TEST_ASSERT(
        UITree_EmitRetainGateQuiet(tree, &host, &emit, -1, &gate),
        "a freshly captured settled frame is reusable");
    other_emit.host_input_stamp = emit.host_input_stamp;
    other_emit.publication_seq = emit.publication_seq;
    TEST_ASSERT(
        !UITree_EmitRetainGateQuiet(tree, &host, &other_emit, -1, &gate),
        "a gate cannot authorize a different buffer with matching source stamps");
    emit.count = 0;
    UITree_EmitWalk(tree, &host, &emit, -1);
    TEST_ASSERT(
        !UITree_EmitRetainGateQuiet(tree, &host, &emit, -1, &gate),
        "a gate cannot authorize a newer uncaptured buffer publication");
    UITree_EmitRetainGateCapture(tree, &emit, -1, &gate);
    if( other_tree )
    {
        struct UITreeEmitRetainGate wrong_owner = gate;
        wrong_owner.source_tree = other_tree;
        TEST_ASSERT(
            !UITree_EmitRetainGateQuiet(tree, &host, &emit, -1, &wrong_owner),
            "a captured identity cannot be reused by a different tree instance");
    }
    TEST_ASSERT(
        !UITree_EmitRetainGateQuiet(tree, &host, &emit, 680, &gate),
        "hover identity participates in retention");

    /* Camera state was not read by this fixture; changing an unrelated host
     * domain must not manufacture a full walk. The unconditional empty overlay
     * request was read, so that domain must reject the same retained buffer. */
    UITree_HostInputsChanged(&host, camera);
    TEST_ASSERT(
        UITree_EmitRetainGateQuiet(tree, &host, &emit, -1, &gate),
        "an unread host domain does not reject retention");
    UITree_HostInputsChanged(&host, overlays);
    TEST_ASSERT(
        !UITree_EmitRetainGateQuiet(tree, &host, &emit, -1, &gate),
        "a consumed host domain rejects retention");

    emit.count = 0;
    UITree_EmitWalk(tree, &host, &emit, -1);
    UITree_EmitRetainGateCapture(tree, &emit, -1, &gate);
    TEST_ASSERT(
        UITree_EmitRetainGateQuiet(tree, &host, &emit, -1, &gate),
        "a full walk refreshes host dependency identity");

    UITree_LayoutInvalidateBoxes(tree);
    TEST_ASSERT(
        !UITree_EmitRetainGateQuiet(tree, &host, &emit, -1, &gate),
        "pending layout work rejects retention before its sequence advances");
    UITree_EnsureLayout(tree);
    TEST_ASSERT(
        !UITree_EmitRetainGateQuiet(tree, &host, &emit, -1, &gate),
        "a completed new layout sequence rejects the old buffer");

    emit.count = 0;
    UITree_EmitWalk(tree, &host, &emit, -1);
    UITree_EmitRetainGateCapture(tree, &emit, -1, &gate);
    TEST_ASSERT(UITree_ApplyColour(tree, 680, 0x123456), "mutate retain-gate fixture");
    TEST_ASSERT(
        !UITree_EmitRetainGateQuiet(tree, &host, &emit, -1, &gate),
        "a visible component mutation rejects retention");

    emit.count = 0;
    UITree_EmitWalk(tree, &host, &emit, -1);
    UITree_EmitRetainGateCapture(tree, &emit, -1, &gate);
    TEST_ASSERT(
        UITree_TestPushXy(tree, -1, UIELEM_RS_RECT, 681, 0, 0, 1, 1) >= 0,
        "topology mutation fixture");
    TEST_ASSERT(
        !UITree_EmitRetainGateQuiet(tree, &host, &emit, -1, &gate),
        "tree topology participates in retention");

    /* Volatile requests execute arbitrary host/plugin callbacks. A callback
     * which changes any term of the original publication identity must make
     * the retained refresh fall back to a full walk, even when dirty_gen stays
     * unchanged. */
    {
        struct RefreshMutationProbe probe = {
            .tree = tree,
            .host = &host,
        };
        int hovered = -1;

        probe.hovered_component_id = &hovered;
        host.user = &probe;
        host.request = refresh_mutation_request;
        UITree_EnsureLayout(tree);
        emit.count = 0;
        UITree_EmitWalk(tree, &host, &emit, hovered);
        UITree_EmitRetainGateCapture(tree, &emit, hovered, &gate);
        TEST_ASSERT(emit.volatile_refs > 0, "refresh-mutation fixture is volatile");
        TEST_ASSERT(
            !emit.volatile_unrefreshable,
            "refresh-mutation fixture permits a retained refresh");
        TEST_ASSERT(
            UITree_EmitRetainGateRefreshVolatile(
                tree, &host, &emit, &hovered, &gate),
            "an unchanged volatile refresh remains reusable");
        TEST_ASSERT(
            !UITree_EmitRetainGateQuiet(tree, &host, &emit, hovered, &gate),
            "a successful refresh advances the exact buffer publication");
        UITree_EmitRetainGateCapture(tree, &emit, hovered, &gate);
        TEST_ASSERT(
            UITree_EmitRetainGateQuiet(tree, &host, &emit, hovered, &gate),
            "capturing a successful refresh publishes its new buffer identity");

        probe.mutation = REFRESH_MUTATION_HOST;
        probe.armed = 1;
        TEST_ASSERT(
            !UITree_EmitRetainGateRefreshVolatile(
                tree, &host, &emit, &hovered, &gate),
            "a host epoch changed inside refresh rejects the retained frame");

        emit.count = 0;
        UITree_EmitWalk(tree, &host, &emit, hovered);
        UITree_EmitRetainGateCapture(tree, &emit, hovered, &gate);
        probe.mutation = REFRESH_MUTATION_LAYOUT;
        probe.armed = 1;
        TEST_ASSERT(
            !UITree_EmitRetainGateRefreshVolatile(
                tree, &host, &emit, &hovered, &gate),
            "layout invalidated inside refresh rejects the retained frame");

        UITree_EnsureLayout(tree);
        emit.count = 0;
        UITree_EmitWalk(tree, &host, &emit, hovered);
        UITree_EmitRetainGateCapture(tree, &emit, hovered, &gate);
        probe.mutation = REFRESH_MUTATION_HOVER;
        probe.armed = 1;
        TEST_ASSERT(
            !UITree_EmitRetainGateRefreshVolatile(
                tree, &host, &emit, &hovered, &gate),
            "hover changed inside refresh rejects the retained frame");
    }

    UITree_EmitBufferFree(&other_emit);
    UITree_EmitBufferFree(&emit);
    UITree_Free(other_tree);
    UITree_Free(tree);
}

static void
test_drag_retain_gate(void)
{
    struct UITree* tree = UITree_New(4);
    struct TestHostState hs;
    struct UITreeHost host;
    struct UITreeEmitBuffer emit;
    struct UITreeEmitRetainGate gate = { 0 };
    int32_t rect;
    int at;

    printf("TEST: retained emit rejects drag start / move / release\n");
    TEST_ASSERT(tree != NULL, "drag retain-gate tree allocation");
    if( !tree )
        return;

    UITree_TestHostInit(&host, &hs);
    rect = UITree_TestPushXy(tree, -1, UIELEM_RS_RECT, 690, 5, 6, 40, 30);
    TEST_ASSERT(rect >= 0, "drag retain-gate fixture component");
    tree->components[rect].drag_behavior = 0; /* deferred picked-up drag */
    UITree_TestResolve(tree);
    UITree_EmitBufferInit(&emit);
    UITree_EmitWalk(tree, &host, &emit, -1);
    UITree_EmitRetainGateCapture(tree, &emit, -1, &gate);
    at = find_emit_desc(&emit, 690);
    TEST_ASSERT(
        at >= 0 && emit.cmds[at].x == 5 && emit.cmds[at].y == 6,
        "drag retain-gate baseline command");
    TEST_ASSERT(
        UITree_EmitRetainGateQuiet(tree, &host, &emit, -1, &gate),
        "drag retain-gate baseline is reusable");

    {
        uint32_t const dirty_before = tree->dirty_gen;
        UITree_SetComponentDragActive(tree, rect, 1);
        tree->components[rect].drag_visual_x = 55;
        tree->components[rect].drag_visual_y = 66;
        tree->components[rect].drag_visual_trans = 128;
        TEST_ASSERT(
            tree->dirty_gen != dirty_before,
            "drag activation enters retained-list identity");
    }
    TEST_ASSERT(
        !UITree_EmitRetainGateQuiet(tree, &host, &emit, -1, &gate),
        "drag start cannot reuse the pre-drag command list");
    emit.count = 0;
    UITree_EmitWalk(tree, &host, &emit, -1);
    at = find_emit_desc(&emit, 690);
    TEST_ASSERT(
        at >= 0 && emit.cmds[at].x == 55 && emit.cmds[at].y == 66,
        "drag-start walk publishes the picked-up position");
    UITree_EmitRetainGateCapture(tree, &emit, -1, &gate);
    TEST_ASSERT(
        !UITree_EmitRetainGateQuiet(tree, &host, &emit, -1, &gate),
        "an active drag is never considered retainable");

    /* Production drag ticks update these visual-only coordinates directly.
     * The active-drag gate must therefore reject retention even without a new
     * dirty generation. */
    {
        uint32_t const dirty_before = tree->dirty_gen;
        tree->components[rect].drag_visual_x = 75;
        tree->components[rect].drag_visual_y = 86;
        TEST_ASSERT(
            tree->dirty_gen == dirty_before,
            "drag movement fixture isolates the active-drag retain guard");
    }
    TEST_ASSERT(
        !UITree_EmitRetainGateQuiet(tree, &host, &emit, -1, &gate),
        "drag movement cannot reuse stale visual coordinates");
    emit.count = 0;
    UITree_EmitWalk(tree, &host, &emit, -1);
    at = find_emit_desc(&emit, 690);
    TEST_ASSERT(
        at >= 0 && emit.cmds[at].x == 75 && emit.cmds[at].y == 86,
        "drag-move walk publishes the latest visual coordinates");
    UITree_EmitRetainGateCapture(tree, &emit, -1, &gate);

    {
        uint32_t const dirty_before = tree->dirty_gen;
        UITree_SetComponentDragActive(tree, rect, 0);
        tree->components[rect].drag_visual_trans = -1;
        TEST_ASSERT(
            tree->dirty_gen != dirty_before,
            "drag release enters retained-list identity");
    }
    TEST_ASSERT(
        !UITree_EmitRetainGateQuiet(tree, &host, &emit, -1, &gate),
        "drag release cannot reuse the last dragged command list");
    emit.count = 0;
    UITree_EmitWalk(tree, &host, &emit, -1);
    at = find_emit_desc(&emit, 690);
    TEST_ASSERT(
        at >= 0 && emit.cmds[at].x == 5 && emit.cmds[at].y == 6,
        "release walk restores the component's layout position");
    UITree_EmitRetainGateCapture(tree, &emit, -1, &gate);
    TEST_ASSERT(
        UITree_EmitRetainGateQuiet(tree, &host, &emit, -1, &gate),
        "settled post-release list is reusable again");

    UITree_EmitBufferFree(&emit);
    UITree_Free(tree);
}

static void
test_host_input_epochs(void)
{
    struct UITreeHost host;
    struct TestHostState hs;
    struct UITreeHostInputStamp stamp;
    UITreeHostInputMask const camera = UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_CAMERA);
    UITreeHostInputMask const pointer = UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_POINTER);
    UITreeHostInputMask const inventory = UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_INVENTORY);
    UITreeHostInputMask const client = UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_CLIENT_STATE);
    UITreeHostInputMask const assets = UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_ASSETS);
    UITreeHostInputMask const world = UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_WORLD);
    UITreeHostInputMask const animation = UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_ANIMATION);
    UITreeHostInputMask const overlays = UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_OVERLAYS);
    UITreeHostInputMask const expected[UITREE_HOST_REQUEST_COUNT] = {
        [UITREE_HOST_IS_ACTIVE] = client | inventory,
        [UITREE_HOST_APPLY_BUTTON_CLICK] = 0,
        [UITREE_HOST_EVAL_TEXT_PLACEHOLDER] = client | inventory,
        [UITREE_HOST_GET_SELECTED_TAB] = client,
        [UITREE_HOST_SET_SELECTED_TAB] = 0,
        [UITREE_HOST_GET_CAMERA_YAW] = camera,
        [UITREE_HOST_GET_CROSS_ACTIVE] = pointer | animation,
        [UITREE_HOST_GET_CROSS_ATLAS_FRAME] = pointer | animation,
        [UITREE_HOST_GET_CROSS_POSITION] = pointer | animation,
        [UITREE_HOST_GET_MINIMENU_VISIBLE] = pointer | client,
        [UITREE_HOST_GET_MINIMENU_STATE] = pointer | client,
        [UITREE_HOST_GET_HOVERTEXT_STATE] = pointer | client,
        [UITREE_HOST_MEASURE_TEXT] = assets,
        [UITREE_HOST_SCENE_SPRITE_HAS] = assets,
        [UITREE_HOST_SCENE_FONT_HAS] = assets,
        [UITREE_HOST_SCENE_MODEL_HAS] = assets,
        [UITREE_HOST_GET_INV_SOURCE_SLOT] = inventory,
        [UITREE_HOST_SET_INV_SOURCE_SLOT] = 0,
        [UITREE_HOST_GET_SCROLLBAR_SCENE] = assets,
        [UITREE_HOST_GET_STATIC_SPRITE_SCENE] = assets,
        [UITREE_HOST_GET_MINIMAP_STATE] = camera | world | assets,
        [UITREE_HOST_GET_MINIMAP_HIDDEN] = client | world,
        [UITREE_HOST_GET_MULTIWAY] = world,
        [UITREE_HOST_GET_REBOOT_TIMER] = client | animation,
        [UITREE_HOST_GET_MINIMAP_DOTS] = camera | world | overlays,
        [UITREE_HOST_GET_ENTITY_OVERLAYS] = camera | world | overlays,
        [UITREE_HOST_GET_CANVAS_OVERLAYS] = overlays,
        [UITREE_HOST_BEGIN_OVERLAYS] = 0,
        [UITREE_HOST_GET_ROLE_OVERLAY_GROUPS] = overlays,
        [UITREE_HOST_SET_ROLE_OVERLAY_CLIP] = 0,
        [UITREE_HOST_GET_FRAME_OVERLAYS] = overlays,
        [UITREE_HOST_GET_WORLDMAP_TILES] = camera | world | assets | overlays,
        [UITREE_HOST_GET_WORLDMAP_OVERVIEW] = camera | world | assets | overlays,
        [UITREE_HOST_GET_TAB_ENABLED] = client,
        [UITREE_HOST_GET_TAB_FLASH_HIDDEN] = client | animation,
        [UITREE_HOST_GET_CHAT_FILTER_MODE] = client,
        [UITREE_HOST_CYCLE_CHAT_FILTER_MODE] = 0,
        [UITREE_HOST_GET_CHAT_STATE] = client,
        [UITREE_HOST_GET_OBJ_NAME] = assets,
        [UITREE_HOST_GET_INV_DRAG] = pointer | inventory,
        [UITREE_HOST_GET_INV_COUNT_FONT] = assets,
        [UITREE_HOST_GET_INV_SELECT_ICON] = inventory | assets,
        [UITREE_HOST_GET_INV_SELECTION] = inventory,
        [UITREE_HOST_GET_OBJ_ICON_PLAIN] = assets,
        [UITREE_HOST_GET_OBJ_ICON_BORDERED] = assets,
        [UITREE_HOST_GET_DEBUG_OVERLAY] = overlays | assets,
        [UITREE_HOST_GET_IF_EVENTS] = client,
    };

    printf("TEST: retained emit host-input epochs\n");

    /* Every current request is deliberately classified. This exact table is a
     * tripwire for additions: an unclassified new kind safely returns ALL in
     * production and fails here until its real dependencies are documented. */
    for( int i = 0; i < UITREE_HOST_REQUEST_COUNT; i++ )
    {
        enum UITreeHostRequestKind const kind = (enum UITreeHostRequestKind)i;
        UITreeHostInputMask const mask = UITree_HostRequestInputMask(kind);

        TEST_ASSERT(!(mask & ~UITREE_HOST_INPUT_ALL), "host request input mask is in range");
        TEST_ASSERT(mask == expected[i], "host request has its documented input dependencies");
    }
    TEST_ASSERT(
        UITree_HostRequestInputMask((enum UITreeHostRequestKind)(UITREE_HOST_REQUEST_COUNT + 17)) ==
            UITREE_HOST_INPUT_ALL,
        "unknown host request conservatively reads every input domain");

    UITree_TestHostInit(&host, &hs);
    TEST_ASSERT(
        UITree_HostPublishInputSignature(&host, UITREE_HOST_INPUT_CAMERA, 0x1234),
        "first semantic source signature advances its domain");
    TEST_ASSERT(
        !UITree_HostPublishInputSignature(&host, UITREE_HOST_INPUT_CAMERA, 0x1234),
        "unchanged semantic source signature is a no-op");
    {
        uint64_t const epoch = host.input_epoch[UITREE_HOST_INPUT_CAMERA];
        TEST_ASSERT(
            UITree_HostPublishInputSignature(&host, UITREE_HOST_INPUT_CAMERA, 0x5678),
            "changed semantic source signature advances its domain");
        TEST_ASSERT(
            host.input_epoch[UITREE_HOST_INPUT_CAMERA] == epoch + 1,
            "signature publication advances exactly one epoch");
        TEST_ASSERT(
            !UITree_HostPublishInputSignature(
                &host, (enum UITreeHostInputDomain)UITREE_HOST_INPUT_DOMAIN_COUNT, 1),
            "invalid signature domain is rejected");
    }
    UITree_HostInputStampCapture(&host, camera | inventory, &stamp);
    TEST_ASSERT(UITree_HostInputStampIsCurrent(&stamp, &host), "fresh host stamp is current");

    UITree_HostInputsChanged(&host, pointer);
    TEST_ASSERT(
        UITree_HostInputStampIsCurrent(&stamp, &host),
        "unobserved host input does not invalidate a stamp");

    UITree_HostInputsChanged(&host, camera);
    TEST_ASSERT(
        !UITree_HostInputStampIsCurrent(&stamp, &host),
        "observed host input invalidates a stamp");

    /* A real walk records only the host domains it reads. The unconditional
     * canvas-overlay query also proves that a zero-result request is tracked:
     * a later plugin overlay can add a descriptor where none existed before. */
    {
        struct UITree* tree = UITree_New(8);
        struct UITreeEmitBuffer emit;
        struct UITreeComponent cached_active = { 0 };
        struct UITreeEmitDesc cached_desc;
        int32_t const compass =
            UITree_TestPushXy(tree, -1, UIELEM_BUILTIN_COMPASS, 700, 0, 0, 32, 32);

        TEST_ASSERT(compass >= 0, "push camera-dependent compass");
        tree->components[compass].u.sprite.scene_id = 1;
        cached_active.type = UIELEM_RS_RECT;
        cached_active.u.rs_rect.color = 0xFF0000;
        cached_active.u.rs_rect.filled = 1;
        cached_active.behavior.active_color = 0x123456;
        cached_active.cs1_active = 1;
        memset(hs.request_count, 0, sizeof(hs.request_count));
        TEST_ASSERT(
            UITree_EmitFill(tree, &host, &cached_active, -1, -1, &cached_desc) &&
                cached_desc.color == 0x123456,
            "emit consumes the typed tree-owned CS1 active publication");
        TEST_ASSERT(
            hs.request_count[UITREE_HOST_IS_ACTIVE] == 0,
            "emit does not round-trip cached CS1 active state through the host");
        memset(&cached_active, 0, sizeof(cached_active));
        cached_active.type = UIELEM_RS_TEXT;
        cached_active.behavior.scripts_count = 1;
        cached_active.u.rs_text.text = "%1";
        cached_active.u.rs_text.font_id = 1;
        cached_active.cs1_values[0] = 42;
        TEST_ASSERT(
            UITree_EmitFill(tree, &host, &cached_active, -1, -1, &cached_desc) &&
                strcmp(cached_desc.text_formatted, "42") == 0,
            "emit formats typed tree-owned CS1 placeholder values");
        TEST_ASSERT(
            hs.request_count[UITREE_HOST_EVAL_TEXT_PLACEHOLDER] == 0,
            "emit does not round-trip cached CS1 placeholder values through the host");
        UITree_EmitBufferInit(&emit);
        memset(hs.request_count, 0, sizeof(hs.request_count));
        UITree_EmitWalk(tree, &host, &emit, -1);
        TEST_ASSERT(
            !(emit.host_input_dependencies & (client | inventory)),
            "cached CS1 active state does not manufacture ambient dependencies");
        TEST_ASSERT(
            emit.host_input_dependencies & camera,
            "emit walk observes the compass camera read");
        TEST_ASSERT(
            emit.host_input_dependencies &
                UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_OVERLAYS),
            "emit walk observes a zero-result overlay read");
        TEST_ASSERT(
            !(emit.host_input_dependencies & pointer),
            "emit walk does not subscribe to an unread pointer domain");
        TEST_ASSERT(
            UITree_EmitBufferHostInputsCurrent(&emit, &host),
            "completed emit records current host inputs");

        UITree_HostInputsChanged(&host, pointer);
        TEST_ASSERT(
            UITree_EmitBufferHostInputsCurrent(&emit, &host),
            "unread pointer change leaves compass emit reusable");
        UITree_HostInputsChanged(&host, camera);
        TEST_ASSERT(
            !UITree_EmitBufferHostInputsCurrent(&emit, &host),
            "camera change rejects retained compass emit");

        /* Same pointer vocabulary, three different host producers. Retained
         * refresh must preserve that provenance rather than replacing plugin
         * frame/canvas output with the world entity list. */
        {
            struct UITreeEntityOverlay item = { 0 };
            struct UITreeEmitBuffer refresh;
            int32_t const overlay =
                UITree_TestPushXy(tree, -1, UIELEM_BUILTIN_ENTITY_OVERLAY, 701, 0, 0, 1, 1);
            int32_t const world =
                UITree_TestPushXy(tree, -1, UIELEM_BUILTIN_WORLD, 702, 0, 0, 1, 1);
            uint8_t const all_overlay_sources =
                (uint8_t)((1u << UITREE_EMIT_OVERLAY_ENTITY) |
                          (1u << UITREE_EMIT_OVERLAY_CANVAS) |
                          (1u << UITREE_EMIT_OVERLAY_FRAME));
            int source_count[UITREE_EMIT_OVERLAY_FRAME + 1] = { 0 };

            TEST_ASSERT(overlay >= 0 && world >= 0, "push retained overlay fixture");
            hs.entity_overlays = &item;
            hs.canvas_overlays = &item;
            hs.frame_overlays = &item;
            hs.entity_overlay_count = 0;
            hs.canvas_overlay_count = 0;
            hs.frame_overlay_count = 0;
            UITree_TestResolve(tree);
            UITree_EmitBufferInit(&refresh);
            UITree_EmitWalk(tree, &host, &refresh, -1);
            TEST_ASSERT(
                (refresh.volatile_overlay_seen & all_overlay_sources) == all_overlay_sources,
                "zero-count overlay sources retain standing refresh records");
            TEST_ASSERT(
                !(refresh.volatile_overlay_nonempty & all_overlay_sources),
                "zero-count standing records do not expose renderer commands");

            memset(hs.request_count, 0, sizeof(hs.request_count));
            hs.entity_overlay_count = 1;
            hs.canvas_overlay_count = 1;
            hs.frame_overlay_count = 1;
            TEST_ASSERT(
                UITree_EmitRefreshVolatile(tree, &host, &refresh),
                "standing overlay records refresh across zero-to-nonzero transitions");
            TEST_ASSERT(
                hs.request_count[UITREE_HOST_GET_ENTITY_OVERLAYS] == 1,
                "world overlay refresh reissues the world request once");
            TEST_ASSERT(
                hs.request_count[UITREE_HOST_GET_CANVAS_OVERLAYS] == 1,
                "canvas overlay refresh preserves canvas provenance");
            TEST_ASSERT(
                hs.request_count[UITREE_HOST_GET_FRAME_OVERLAYS] == 1,
                "frame overlay refresh preserves frame provenance");
            for( int i = 0; i < refresh.count; i++ )
            {
                int const source = refresh.cmds[i].entity_overlay_source;
                if( source >= UITREE_EMIT_OVERLAY_ENTITY &&
                    source <= UITREE_EMIT_OVERLAY_FRAME )
                    source_count[source]++;
            }
            TEST_ASSERT(
                source_count[UITREE_EMIT_OVERLAY_ENTITY] == 1 &&
                    source_count[UITREE_EMIT_OVERLAY_CANVAS] == 1 &&
                    source_count[UITREE_EMIT_OVERLAY_FRAME] == 1,
                "refresh inserts one command with each original overlay source");
            UITree_EmitBufferFree(&refresh);
        }

        UITree_EmitBufferFree(&emit);
        UITree_Free(tree);
    }
}

void
test_mutate_emit(void)
{
    printf("TEST: mutate / emit / hide\n");

    test_emit_retain_gate();
    test_drag_retain_gate();
    test_host_input_epochs();

    struct UITree* tree = UITree_New(16);
    struct TestHostState hs;
    struct UITreeHost host;
    UITree_TestHostInit(&host, &hs);

    int32_t layer = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 400, 0, 0, 200, 200);
    TEST_ASSERT(UITree_ApplyHide(tree, 400, 1), "apply hide");
    TEST_ASSERT(tree->components[layer].behavior.hide == 1, "hide set");
    TEST_ASSERT(!UITree_ComponentVisibleById(&tree->components[layer], -1), "hide gated");
    TEST_ASSERT(UITree_ComponentVisibleById(&tree->components[layer], 400), "hide shown when hovered");
    TEST_ASSERT(UITree_ApplyHide(tree, 400, 0), "unhide");

    /* EmitWalk hard-skips hidden node and its children (interfacex semantics). */
    {
        struct UITreeNodeSpec child;
        memset(&child, 0, sizeof(child));
        child.type = UIELEM_RS_RECT;
        child.component_id = 450;
        child.width = 10;
        child.height = 10;
        child.u.rs_rect.color = 0xabcdef;
        child.u.rs_rect.filled = 1;
        int32_t ci = UITree_Push(tree, layer, &child);
        TEST_ASSERT(ci >= 0, "push child under layer");

        UITree_TestResolve(tree);
        TEST_ASSERT(UITree_ApplyHide(tree, 400, 1), "hide layer for emit walk");

        struct UITreeEmitBuffer buf;
        UITree_EmitBufferInit(&buf);
        UITree_EmitWalk(tree, &host, &buf, -1);
        TEST_ASSERT(buf.count == 0, "hidden subtree emits nothing");
        UITree_EmitBufferFree(&buf);

        TEST_ASSERT(UITree_ApplyHide(tree, 400, 0), "unhide layer after emit walk");
    }

    int32_t dyn = UITree_CcCreate(tree, layer, 400, 3, 1);
    TEST_ASSERT(dyn >= 0, "cc_create");
    TEST_ASSERT(tree->components[dyn].dynamic == 1, "dynamic");
    TEST_ASSERT(tree->components[dyn].type == UIELEM_RS_RECT, "cc type rect for widget 3");
    TEST_ASSERT(tree->components[dyn].u.rs_rect.filled == 0,
                "cc rect defaults to outline (scripts call setfill for tint)");
    TEST_ASSERT(tree->components[dyn].if3 == 0, "cc inherits if3=0 from parent");
    TEST_ASSERT(tree->components[layer].is_dirty == 1 || tree->components[dyn].is_dirty == 1,
                "cc dirties");

    tree->components[layer].if3 = 1;
    int32_t dyn_if3 = UITree_CcCreate(tree, layer, 400, 5, 2);
    TEST_ASSERT(dyn_if3 >= 0, "cc_create graphic under if3 parent");
    TEST_ASSERT(tree->components[dyn_if3].if3 == 1, "cc inherits if3=1 from parent");
    TEST_ASSERT(tree->components[dyn_if3].type == UIELEM_RS_GRAPHIC, "cc type graphic for widget 5");
    tree->components[layer].if3 = 0;

    /* Widget types 6 (model) and 9 (line) must not fall through to CC_OBJ —
     * that left world-map key/overview icons blank (ApplyModel no-ops on
     * CC_OBJ; emit skips obj_id<=0). */
    {
        int32_t model = UITree_CcCreate(tree, layer, 400, 6, 3);
        TEST_ASSERT(model >= 0, "cc_create model");
        TEST_ASSERT(tree->components[model].type == UIELEM_RS_MODEL, "cc type model for widget 6");
        TEST_ASSERT(
            tree->components[model].u.rs_model.gamecache_model_id == -1,
            "model id unset until setmodel");
        TEST_ASSERT(
            tree->components[model].u.rs_model.active_model_id == -1,
            "active model id unset until set");
        TEST_ASSERT(tree->components[model].u.rs_model.zoom == 100, "model default zoom 100");
        TEST_ASSERT(UITree_ApplyModel(tree, tree->components[model].component_id, 42),
                    "applymodel on cc model");
        TEST_ASSERT(
            tree->components[model].u.rs_model.gamecache_model_id == 42, "applymodel wrote id");

        int32_t line = UITree_CcCreate(tree, layer, 400, 9, 4);
        TEST_ASSERT(line >= 0, "cc_create line");
        TEST_ASSERT(tree->components[line].type == UIELEM_RS_LINE, "cc type line for widget 9");
    }

    int indices[8];
    int n = UITree_CollectDynamicChildIndices(tree, 400, 0, indices, 8);
    TEST_ASSERT(n >= 1, "collect dynamic indices");

    UITree_CcDeleteAll(tree, layer);
    int32_t still = UITree_FindChildBySubid(tree, layer, 400, 1);
    TEST_ASSERT(still < 0 || !tree->components[still].dynamic ||
                    tree->components[still].parent < 0,
                "dynamic detached after deleteall");

    /* EmitFill kinds */
    {
        struct UITreeNodeSpec rect;
        memset(&rect, 0, sizeof(rect));
        rect.type = UIELEM_RS_RECT;
        rect.component_id = 501;
        rect.width = 20;
        rect.height = 20;
        rect.u.rs_rect.color = 0x112233;
        rect.u.rs_rect.filled = 1;
        int32_t ri = UITree_Push(tree, -1, &rect);

        struct UITreeNodeSpec text;
        memset(&text, 0, sizeof(text));
        text.type = UIELEM_RS_TEXT;
        text.component_id = 502;
        text.width = 40;
        text.height = 16;
        text.u.rs_text.font_id = 1;
        text.u.rs_text.text = "ok";
        text.u.rs_text.color = 0xFFFFFF;
        int32_t ti = UITree_Push(tree, -1, &text);

        struct UITreeNodeSpec gfx;
        memset(&gfx, 0, sizeof(gfx));
        gfx.type = UIELEM_RS_GRAPHIC;
        gfx.component_id = 503;
        gfx.width = 16;
        gfx.height = 16;
        gfx.u.rs_graphic.scene_id = 9;
        int32_t gi = UITree_Push(tree, -1, &gfx);

        struct UITreeNodeSpec side;
        memset(&side, 0, sizeof(side));
        side.type = UIELEM_BUILTIN_SIDEBAR;
        side.width = 10;
        side.height = 10;
        side.u.sidebar.tabno = 0;
        int32_t si = UITree_Push(tree, -1, &side);

        UITree_TestResolve(tree);

        struct UITreeEmitDesc desc;
        TEST_ASSERT(UITree_EmitFill(tree, &host, &tree->components[ri], ri, -1, &desc), "emit rect");
        TEST_ASSERT(desc.kind == UITREE_EMIT_RECT, "kind rect");
        TEST_ASSERT(desc.color == 0x112233, "rect color");

        TEST_ASSERT(UITree_EmitFill(tree, &host, &tree->components[ti], ti, -1, &desc), "emit text");
        TEST_ASSERT(desc.kind == UITREE_EMIT_TEXT, "kind text");
        TEST_ASSERT(desc.text && desc.text[0] == 'o', "text ptr");

        /* Golden clientscript 600 uses this exact setter for NPC body text.
         * Pin both the live widget mutation and the values handed to either
         * renderer; a host-only opcode test cannot catch a dropped UITree
         * field. */
        TEST_ASSERT(UITree_ApplyTextAlign(tree, 502, 1, 1, 16), "apply text alignment");
        TEST_ASSERT(tree->components[ti].u.rs_text.center == 1, "text horizontal centre set");
        TEST_ASSERT(tree->components[ti].u.rs_text.y_align == 1, "text vertical centre set");
        TEST_ASSERT(tree->components[ti].u.rs_text.line_height == 16, "text line height set");
        TEST_ASSERT(UITree_EmitFill(tree, &host, &tree->components[ti], ti, -1, &desc),
                    "emit aligned text");
        TEST_ASSERT(desc.text_center == 1, "emit horizontal centre");
        TEST_ASSERT(desc.text_y_align == 1, "emit vertical centre");
        TEST_ASSERT(desc.text_line_height == 16, "emit revision-239 line height");

        TEST_ASSERT(UITree_EmitFill(tree, &host, &tree->components[gi], gi, -1, &desc), "emit graphic");
        TEST_ASSERT(desc.kind == UITREE_EMIT_SPRITE, "kind sprite");
        TEST_ASSERT(desc.scene_id == 9, "sprite scene_id");

        TEST_ASSERT(!UITree_EmitFill(tree, &host, &tree->components[si], si, -1, &desc),
                    "sidebar no emit");
        TEST_ASSERT(!UITree_EmitFill(tree, &host, &tree->components[layer], layer, -1, &desc),
                    "layer no emit without scrollbar");

        tree->components[layer].u.rs_layer.scroll_height = 400;
        tree->components[layer].if3 = 0;
        UITree_TestResolve(tree);
        TEST_ASSERT(UITree_EmitFill(tree, &host, &tree->components[layer], layer, -1, &desc),
                    "layer emits scrollbar when scroll_height > height");
        TEST_ASSERT(desc.kind == UITREE_EMIT_SCROLLBAR_V, "kind scrollbar_v");
        TEST_ASSERT(desc.scroll_content == 400, "scroll_content height");
        TEST_ASSERT(desc.w == 16, "scrollbar thickness");

        tree->components[layer].scroll_y = 50;
        TEST_ASSERT(UITree_EmitFill(tree, &host, &tree->components[layer], layer, -1, &desc),
                    "scrollbar emit with scroll_y");
        TEST_ASSERT(desc.scroll_off_y == 50, "scroll_off_y from component");

        tree->components[layer].scroll_y = 500;
        TEST_ASSERT(UITree_EmitFill(tree, &host, &tree->components[layer], layer, -1, &desc),
                    "scrollbar emit locally clamps an out-of-range value");
        TEST_ASSERT(desc.scroll_off_y == 200, "emit uses the clamped scrollbar offset");
        TEST_ASSERT(
            tree->components[layer].scroll_y == 500,
            "emit does not mutate canonical scroll state during a read");
        TEST_ASSERT(UITree_SetScrollPosAt(tree, layer, 0, 50), "restore scroll through typed setter");
    }

    TEST_ASSERT(UITree_ApplyColour(tree, 501, 0x99), "apply colour");
    TEST_ASSERT(UITree_ApplyPosition(tree, 501, 3, 4), "apply position");
    TEST_ASSERT(UITree_ApplySize(tree, 501, 11, 12), "apply size");

    /*
     * A slot mount empties the slot -- except for what the PROFILE authored
     * into it.
     *
     * This is the whole of a bug that only appears against a real server: a
     * revconfig control placed in a sidebar tab is there in an offline boot,
     * and gone the moment the login burst of IF_SETTABs mounts the server's
     * own interface over it, because task_slot_mount clears the container
     * first. It reads as the control failing to build rather than as something
     * sweeping it away, which is why the behaviour is pinned here rather than
     * left to the one lane that would notice.
     */
    {
        int32_t const owner = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 900, 0, 0, 100, 100);
        int32_t const from_cache =
            UITree_TestPushXy(tree, owner, UIELEM_RS_RECT, 901, 0, 0, 10, 10);
        int32_t const authored = UITree_TestPushXy(
            tree, owner, UIELEM_RS_RECT, TORIRS_REVCONFIG_ID_BASE + 0, 0, 20, 10, 10);
        int32_t const authored_child =
            UITree_TestPushXy(tree, authored, UIELEM_RS_TEXT, -1, 0, 0, 10, 10);

        TEST_ASSERT(from_cache >= 0 && authored >= 0, "slot children pushed");
        TEST_ASSERT(authored_child >= 0, "authored control has a child of its own");

        UITree_ClearChildren(tree, owner);

        TEST_ASSERT(
            UITree_FindByComponentId(tree, 901) < 0,
            "a cache-mounted child is reclaimed by the clear");
        TEST_ASSERT(
            UITree_FindByComponentId(tree, TORIRS_REVCONFIG_ID_BASE + 0) == authored,
            "a profile-authored child survives the clear");
        TEST_ASSERT(
            tree->components[owner].first_child == authored,
            "the survivor is relinked as the owner's child");
        TEST_ASSERT(
            tree->components[authored].next_sibling < 0,
            "and the reclaimed sibling is not still linked after it");
        /* The whole subtree, not just the node with the id on it: the three
         * graphics and the label under a button carry no id of their own. */
        TEST_ASSERT(
            !tree->components[authored_child].freed,
            "the survivor keeps its own children");
    }

    UITree_Free(tree);
}

void
test_apply_object_silhouette(void)
{
    printf("TEST: ApplyObject silhouette vs bank grid\n");

    struct UITree* tree = UITree_New(16);

    /* Bank/grid: many CC_OBJ siblings under one parent. SETOBJECT on 0/1 must
     * not hide sibling sub_id 2 (that was the IF 12 / IF 605 3rd-icon bug). */
    {
        int32_t parent = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 100, 0, 0, 200, 200);
        int32_t c0 = UITree_CcCreate(tree, parent, 100, 2, 0);
        int32_t c1 = UITree_CcCreate(tree, parent, 100, 2, 1);
        int32_t c2 = UITree_CcCreate(tree, parent, 100, 2, 2);
        TEST_ASSERT(c0 >= 0 && c1 >= 0 && c2 >= 0, "bank cc_create 0/1/2");
        TEST_ASSERT(tree->components[c0].type == UIELEM_CC_OBJ, "c0 is cc_obj");
        TEST_ASSERT(tree->components[c1].dynamic_child_index == 1, "c1 sub_id 1");
        TEST_ASSERT(tree->components[c2].dynamic_child_index == 2, "c2 sub_id 2");

        TEST_ASSERT(
            UITree_ApplyObject(tree, tree->components[c0].component_id, 995, 1, 10, 0, 0),
            "setobject bank slot 0");
        TEST_ASSERT(
            UITree_ApplyObject(tree, tree->components[c1].component_id, 1333, 1, 11, 0, 0),
            "setobject bank slot 1");
        TEST_ASSERT(!tree->components[c2].behavior.hide, "bank child 2 not hidden by siblings");

        TEST_ASSERT(
            UITree_ApplyObject(tree, tree->components[c2].component_id, 1153, 1, 12, 0, 0),
            "setobject bank slot 2");
        TEST_ASSERT(!tree->components[c2].behavior.hide, "bank child 2 stays visible");
        TEST_ASSERT(tree->components[c2].item_id == 1153, "bank child 2 item set");
        TEST_ASSERT(tree->components[c2].item_scene_id == 12, "bank child 2 scene_id");
    }

    /* Equipment slot: d1 overlay + d2 silhouette — SETOBJECT on overlay hides
     * silhouette; clear unhides it. */
    {
        int32_t slot = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 200, 0, 0, 36, 32);
        int32_t overlay = UITree_CcCreate(tree, slot, 200, 2, 1);
        int32_t sil = UITree_CcCreate(tree, slot, 200, 5, 2);
        TEST_ASSERT(overlay >= 0 && sil >= 0, "equipment d1/d2 create");
        TEST_ASSERT(tree->components[sil].type == UIELEM_RS_GRAPHIC, "d2 is graphic");

        TEST_ASSERT(
            UITree_ApplyObject(tree, tree->components[overlay].component_id, 1153, 1, 20, 0, 0),
            "setobject equipment overlay");
        TEST_ASSERT(tree->components[sil].behavior.hide == 1, "silhouette hidden while occupied");
        TEST_ASSERT(!tree->components[overlay].behavior.hide, "overlay visible");

        TEST_ASSERT(
            UITree_ApplyObject(tree, tree->components[overlay].component_id, -1, 0, -1, 0, 0),
            "clear equipment overlay");
        TEST_ASSERT(tree->components[sil].behavior.hide == 0, "silhouette shown when cleared");

        /* SETOBJECT on static parent redirects to d1 overlay. */
        TEST_ASSERT(UITree_ApplyObject(tree, 200, 1725, 1, 21, 0, 0), "setobject via static parent");
        TEST_ASSERT(tree->components[overlay].item_id == 1725, "redirect set overlay item");
        TEST_ASSERT(tree->components[sil].behavior.hide == 1, "silhouette hidden after redirect");
    }

    /*
     * IF_SETANIM restarts a *different* sequence and leaves the running one
     * alone. The second half is the one with a bug behind it: a dialogue
     * chathead is rebound (model + anim) every time the tree generation moves,
     * so a reset-on-every-apply pinned it to frame 0 for the whole
     * conversation while the animator dutifully advanced it in between.
     */
    {
        int32_t model = UITree_TestPushXy(tree, -1, UIELEM_RS_MODEL, 700, 0, 0, 32, 32);
        TEST_ASSERT(model >= 0, "push model widget");

        TEST_ASSERT(UITree_ApplyModelAnim(tree, 700, 588), "set chathead anim");
        TEST_ASSERT(tree->components[model].u.rs_model.anim_seq_id == 588, "anim seq stored");

        /* Two ticks of the animator. */
        tree->components[model].u.rs_model.anim_frame = 7;
        tree->components[model].u.rs_model.anim_frame_cycle = 3;

        TEST_ASSERT(UITree_ApplyModelAnim(tree, 700, 588), "re-apply the same anim");
        TEST_ASSERT(tree->components[model].u.rs_model.anim_frame == 7,
                    "re-applying the running anim keeps the frame");
        TEST_ASSERT(tree->components[model].u.rs_model.anim_frame_cycle == 3,
                    "re-applying the running anim keeps the cycle");

        TEST_ASSERT(UITree_ApplyModelAnim(tree, 700, 591), "switch to another anim");
        TEST_ASSERT(tree->components[model].u.rs_model.anim_seq_id == 591, "new anim seq stored");
        TEST_ASSERT(tree->components[model].u.rs_model.anim_frame == 0,
                    "a different anim restarts at frame 0");
        TEST_ASSERT(tree->components[model].u.rs_model.anim_frame_cycle == 0,
                    "a different anim restarts at cycle 0");
    }

    UITree_Free(tree);
}
