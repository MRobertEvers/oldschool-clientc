/*
 * Named-UI policy without a host, UITree, renderer, or cache.
 *
 * The cases deliberately rebuild bases and reverse contribution order.  A
 * retained semantic reference must stay put, while facet ownership is a pure
 * result of the complete declaration set rather than callback order.
 */

#include "plugin/torirs_plugin_ui.h"

#include <stdio.h>
#include <string.h>

static int checks;
static int failures;

#define CHECK(expr, message)                                                                       \
    do                                                                                             \
    {                                                                                              \
        checks++;                                                                                  \
        if( !(expr) )                                                                              \
        {                                                                                          \
            failures++;                                                                            \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (message));                    \
        }                                                                                          \
    } while( 0 )

static struct ToriRS_UiRegistry registry_a;
static struct ToriRS_UiRegistry registry_b;

static struct ToriRS_UiNode
node_value(
    int x,
    int y,
    int width,
    int height,
    uint32_t flags,
    int image,
    char const* label,
    char const* action)
{
    struct ToriRS_UiNode node = {
        .struct_size = sizeof(node),
        .bounds = { x, y, width, height },
        .anchor = TORIRS_ANCHOR_TOP_LEFT,
        .paint_order = TORIRS_UI_PAINT_AFTER_PARENT,
        .flags = flags,
        .image = { image },
        .label = label,
        .action = action,
    };
    return node;
}

static struct ToriRS_UiContribution
contribution(
    char const* name,
    int mode,
    uint32_t facets,
    struct ToriRS_UiNode value)
{
    struct ToriRS_UiContribution declaration = {
        .struct_size = sizeof(declaration),
        .node = name,
        .mode = mode,
        .facets = facets,
        .value = value,
    };
    return declaration;
}

static bool
rect_is(
    struct ToriRS_Rect const* rect,
    int x,
    int y,
    int width,
    int height)
{
    return rect->x == x && rect->y == y && rect->width == width && rect->height == height;
}

static void
test_names_and_aliases(void)
{
    struct ToriRS_UiNodeRef canonical;
    struct ToriRS_UiNodeRef alias;
    struct ToriRS_UiNodeRef private_a;
    struct ToriRS_UiNodeRef private_b;

    ToriRS_UiRegistry_Init(&registry_a);
    canonical = ToriRS_UiRegistry_Ref(&registry_a, "frame.chat.button.report");
    alias = ToriRS_UiRegistry_Ref(&registry_a, "report_button");
    CHECK(canonical.value != 0, "the core frame vocabulary is registered at init");
    CHECK(alias.value == canonical.value, "a legacy role aliases the canonical node");
    CHECK(
        ToriRS_UiRegistry_Ref(&registry_a, "minimap_edge").value ==
            ToriRS_UiRegistry_Ref(&registry_a, "frame.minimap.housing").value,
        "the legacy minimap housing aliases its canonical node");
    CHECK(
        ToriRS_UiRegistry_Ref(&registry_a, "orb_run").value ==
            ToriRS_UiRegistry_Ref(&registry_a, "frame.orb.run").value,
        "the legacy orb name aliases its canonical node");
    CHECK(
        ToriRS_UiRegistry_Ref(&registry_a, "xp_drops").value ==
            ToriRS_UiRegistry_Ref(&registry_a, "frame.xp.drops").value,
        "the legacy XP name aliases its canonical node");
    CHECK(
        strcmp(ToriRS_UiRegistry_Name(&registry_a, alias), "frame.chat.button.report") == 0,
        "name lookup always returns canonical spelling");
    CHECK(
        ToriRS_UiRegistry_NodeAt(&registry_a, 0).value != 0 &&
            ToriRS_UiRegistry_NodeAt(&registry_a, ToriRS_UiRegistry_NodeCount(&registry_a)).value ==
                0,
        "interned nodes can be enumerated without exposing array indices as identity");

    CHECK(ToriRS_UiRegistry_NameIsValid("frame.sidebar.tab.0"), "numeric path segment is valid");
    CHECK(
        ToriRS_UiRegistry_NameIsValid("plugin.xp-tracker.counter.main"),
        "plugin ids and dotted locals form valid canonical names");
    CHECK(!ToriRS_UiRegistry_NameIsValid("frame"), "a canonical name is dotted");
    CHECK(!ToriRS_UiRegistry_NameIsValid("frame..chat"), "an empty path segment is invalid");
    CHECK(!ToriRS_UiRegistry_NameIsValid("frame.Chat"), "canonical names are lowercase");
    CHECK(!ToriRS_UiRegistry_NameIsValid("frame.chat_button"), "underscores are legacy only");
    CHECK(
        ToriRS_UiRegistry_Ref(&registry_a, "frame.typo").value == 0,
        "plugins cannot mint unknown core vocabulary names");

    private_a = ToriRS_UiRegistry_PrivateRef(&registry_a, "alpha-plugin", "counter.main");
    private_b = ToriRS_UiRegistry_PrivateRef(&registry_a, "beta-plugin", "counter.main");
    CHECK(private_a.value != 0 && private_b.value != 0, "private local names intern");
    CHECK(private_a.value != private_b.value, "plugin namespaces cannot collide");
    CHECK(
        strcmp(
            ToriRS_UiRegistry_Name(&registry_a, private_a), "plugin.alpha-plugin.counter.main") ==
            0,
        "private names auto-canonicalize");
    CHECK(
        ToriRS_UiRegistry_PrivateRef(&registry_a, "alpha-plugin", "plugin.beta-plugin.counter.main")
                .value == 0,
        "a plugin cannot spell another plugin's private namespace");
    CHECK(
        ToriRS_UiRegistry_PrivateRef(&registry_a, "bad.plugin", "counter").value == 0,
        "a malformed plugin id is refused");
}

static void
test_base_facets_and_generations(void)
{
    struct ToriRS_UiNodeRef report;
    struct ToriRS_UiNode bounds;
    struct ToriRS_UiNode appearance;
    struct ToriRS_UiNode actions;
    struct ToriRS_UiResolvedNode resolved;
    uint32_t generation;

    ToriRS_UiRegistry_Init(&registry_a);
    report = ToriRS_UiRegistry_Ref(&registry_a, "frame.chat.button.report");
    bounds = node_value(10, 20, 80, 24, TORIRS_UI_NODE_BLOCKS_OVERLAY, 0, NULL, NULL);
    bounds.parent = "frame.chat";
    appearance = node_value(0, 0, 0, 0, TORIRS_UI_NODE_VISIBLE, 17, "Report", NULL);
    actions = node_value(0, 0, 0, 0, TORIRS_UI_NODE_ENABLED, 0, NULL, "activate");

    CHECK(
        ToriRS_UiRegistry_AddBase(
            &registry_a, "lane", "report_button", TORIRS_UI_FACET_BOUNDS, &bounds) ==
            TORIRS_UI_REGISTRY_OK,
        "a lane supplies the base bounds facet");
    CHECK(
        ToriRS_UiRegistry_AddBase(
            &registry_a,
            "frame-provider",
            "frame.chat.button.report",
            TORIRS_UI_FACET_APPEARANCE,
            &appearance) == TORIRS_UI_REGISTRY_OK,
        "the selected frame independently supplies appearance");
    CHECK(
        ToriRS_UiRegistry_AddBase(
            &registry_a, "lane", "frame.chat.button.report", TORIRS_UI_FACET_ACTIONS, &actions) ==
            TORIRS_UI_REGISTRY_OK,
        "the lane independently supplies actions");
    CHECK(ToriRS_UiRegistry_Resolve(&registry_a, report, &resolved), "the composed base resolves");
    CHECK(resolved.available_facets == TORIRS_UI_FACET_ALL, "all three base facets compose");
    CHECK(rect_is(&resolved.value.bounds, 10, 20, 80, 24), "base bounds are retained");
    CHECK(resolved.value.parent.value != 0, "the semantic parent was interned");
    CHECK(resolved.value.image.value == 17, "base appearance is retained");
    CHECK(strcmp(resolved.value.label, "Report") == 0, "base label is copied");
    CHECK(strcmp(resolved.value.action, "activate") == 0, "base action is copied");
    CHECK(strcmp(resolved.bounds_provider, "lane") == 0, "bounds provider is diagnostic");
    CHECK(
        strcmp(resolved.appearance_provider, "frame-provider") == 0,
        "appearance provider is diagnostic");
    CHECK(strcmp(resolved.actions_provider, "lane") == 0, "actions provider is diagnostic");
    CHECK(
        (resolved.value.flags &
         (TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ENABLED | TORIRS_UI_NODE_BLOCKS_OVERLAY)) ==
            (TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ENABLED | TORIRS_UI_NODE_BLOCKS_OVERLAY),
        "facet-specific flags compose rather than overwrite each other");

    appearance.image.value = 99;
    CHECK(
        ToriRS_UiRegistry_AddBase(
            &registry_a,
            "other",
            "frame.chat.button.report",
            TORIRS_UI_FACET_APPEARANCE,
            &appearance) == TORIRS_UI_REGISTRY_DUPLICATE,
        "a duplicate base facet is rejected");
    CHECK(
        ToriRS_UiRegistry_Resolve(&registry_a, report, &resolved) &&
            resolved.value.image.value == 17,
        "duplicate rejection does not partially change the base");

    generation = ToriRS_UiRegistry_BaseGeneration(&registry_a);
    ToriRS_UiRegistry_ClearBase(&registry_a);
    CHECK(
        ToriRS_UiRegistry_BaseGeneration(&registry_a) != generation,
        "a tree rebuild advances the base generation");
    CHECK(!ToriRS_UiRegistry_Resolve(&registry_a, report, &resolved), "cleared base is absent");
    CHECK(
        ToriRS_UiRegistry_Ref(&registry_a, "report_button").value == report.value,
        "the cached semantic reference survives the rebuild");

    bounds.bounds = (struct ToriRS_Rect){ 100, 40, 90, 28 };
    CHECK(
        ToriRS_UiRegistry_AddBase(
            &registry_a,
            "new-frame",
            "frame.chat.button.report",
            TORIRS_UI_FACET_BOUNDS,
            &bounds) == TORIRS_UI_REGISTRY_OK,
        "the new tree generation can bind the same semantic name elsewhere");
    CHECK(
        ToriRS_UiRegistry_Resolve(&registry_a, report, &resolved) &&
            rect_is(&resolved.value.bounds, 100, 40, 90, 28),
        "the old ref resolves the new generation's geometry");
}

static void
test_rich_facet_payloads(void)
{
    struct ToriRS_UiNode bounds;
    struct ToriRS_UiNode appearance;
    struct ToriRS_UiNode actions;
    struct ToriRS_UiNode legacy;
    struct ToriRS_UiResolvedNode resolved;
    struct ToriRS_UiNodeRef report;
    char label[] = "Report abuse";
    char activate[] = "activate";
    char inspect[] = "inspect";

    ToriRS_UiRegistry_Init(&registry_a);
    report = ToriRS_UiRegistry_Ref(&registry_a, "frame.chat.button.report");
    bounds = node_value(10, 20, 80, 24, TORIRS_UI_NODE_BLOCKS_OVERLAY, -1, NULL, NULL);
    bounds.parent = "frame.chat";
    bounds.anchor = TORIRS_ANCHOR_BOTTOM_RIGHT;
    bounds.paint_order = TORIRS_UI_PAINT_BEFORE_PARENT;
    bounds.clip = TORIRS_UI_CLIP_PARENT;
    appearance = node_value(0, 0, 0, 0, TORIRS_UI_NODE_VISIBLE, 11, label, NULL);
    appearance.state_image_mask = (1u << TORIRS_UI_VISUAL_HOVER) |
                                  (1u << TORIRS_UI_VISUAL_DISABLED);
    appearance.state_images[TORIRS_UI_VISUAL_HOVER].value = 12;
    appearance.state_images[TORIRS_UI_VISUAL_DISABLED].value = 13;
    appearance.label_x = 7;
    appearance.label_y = 9;
    actions = node_value(0, 0, 0, 0, TORIRS_UI_NODE_ENABLED, -1, NULL, NULL);
    actions.hit_rect_mode = TORIRS_UI_HIT_RECT_CUSTOM;
    actions.hit_rect = (struct ToriRS_Rect){ 8, 18, 86, 30 };
    actions.action_count = 2;
    actions.actions[0] = activate;
    actions.actions[1] = inspect;

    CHECK(
        ToriRS_UiRegistry_AddBase(
            &registry_a, "lane", "frame.chat.button.report", TORIRS_UI_FACET_BOUNDS, &bounds) ==
            TORIRS_UI_REGISTRY_OK,
        "rich bounds facet registers");
    CHECK(
        ToriRS_UiRegistry_AddBase(
            &registry_a,
            "frame",
            "frame.chat.button.report",
            TORIRS_UI_FACET_APPEARANCE,
            &appearance) == TORIRS_UI_REGISTRY_OK,
        "rich appearance facet registers");
    CHECK(
        ToriRS_UiRegistry_AddBase(
            &registry_a,
            "lane",
            "frame.chat.button.report",
            TORIRS_UI_FACET_ACTIONS,
            &actions) == TORIRS_UI_REGISTRY_OK,
        "rich actions facet registers");
    label[0] = 'X';
    activate[0] = 'X';
    inspect[0] = 'X';
    CHECK(ToriRS_UiRegistry_Resolve(&registry_a, report, &resolved), "rich node resolves");
    CHECK(
        resolved.value.parent.value != 0 && resolved.value.anchor == TORIRS_ANCHOR_BOTTOM_RIGHT &&
            resolved.value.paint_order == TORIRS_UI_PAINT_BEFORE_PARENT &&
            resolved.value.clip == TORIRS_UI_CLIP_PARENT,
        "bounds owns parent, anchor, paint order, and clip");
    CHECK(
        resolved.value.image.value == 11 &&
            resolved.value.state_images[TORIRS_UI_VISUAL_IDLE].value == 11 &&
            resolved.value.state_images[TORIRS_UI_VISUAL_HOVER].value == 12 &&
            resolved.value.state_images[TORIRS_UI_VISUAL_DISABLED].value == 13 &&
            resolved.value.state_images[TORIRS_UI_VISUAL_ACTIVE].value == -1,
        "appearance retains independent images for every visual state");
    CHECK(
        strcmp(resolved.value.label, "Report abuse") == 0 && resolved.value.label_x == 7 &&
            resolved.value.label_y == 9 &&
            (resolved.value.flags & TORIRS_UI_NODE_VISIBLE) != 0,
        "appearance copies label placement and visibility");
    CHECK(
        rect_is(&resolved.value.hit_rect, 8, 18, 86, 30) && resolved.value.action_count == 2 &&
            strcmp(resolved.value.actions[0], "activate") == 0 &&
            strcmp(resolved.value.actions[1], "inspect") == 0 &&
            strcmp(resolved.value.action, "activate") == 0 &&
            (resolved.value.flags & TORIRS_UI_NODE_ENABLED) != 0,
        "actions copies an independent hit rectangle and bounded named set");

    ToriRS_UiRegistry_Init(&registry_b);
    legacy = node_value(1, 2, 3, 4, TORIRS_UI_NODE_VISIBLE, 21, "legacy", "activate");
    legacy.struct_size = TORIRS_UI_NODE_LEGACY_SIZE;
    legacy.clip = 999;
    legacy.action_count = TORIRS_UI_NAMED_ACTIONS_MAX + 1;
    CHECK(
        ToriRS_UiRegistry_AddBase(
            &registry_b, "old-plugin", "frame.orb.run", TORIRS_UI_FACET_ALL, &legacy) ==
            TORIRS_UI_REGISTRY_OK,
        "the original descriptor prefix remains source compatible");
    CHECK(
        ToriRS_UiRegistry_Resolve(
            &registry_b, ToriRS_UiRegistry_Ref(&registry_b, "frame.orb.run"), &resolved) &&
            resolved.value.clip == TORIRS_UI_CLIP_NONE && resolved.value.action_count == 1 &&
            rect_is(&resolved.value.hit_rect, 1, 2, 3, 4),
        "legacy shorthands normalize to complete retained facets");

    actions.action_count = TORIRS_UI_NAMED_ACTIONS_MAX + 1;
    CHECK(
        ToriRS_UiRegistry_AddBase(
            &registry_b, "bad", "frame.orb.prayer", TORIRS_UI_FACET_ACTIONS, &actions) ==
            TORIRS_UI_REGISTRY_INVALID,
        "an over-budget named action set is rejected before retention");
}

static struct ToriRS_UiContributionRef
add_contribution(
    struct ToriRS_UiRegistry* registry,
    char const* plugin,
    char const* name,
    int mode,
    uint32_t facets,
    struct ToriRS_UiNode value)
{
    struct ToriRS_UiContribution declaration = contribution(name, mode, facets, value);
    struct ToriRS_UiContributionRef ref = { 0 };
    enum ToriRS_UiRegistryResult const result =
        ToriRS_UiRegistry_AddContribution(registry, plugin, &declaration, &ref);

    CHECK(result == TORIRS_UI_REGISTRY_OK, "contribution registration succeeds");
    return ref;
}

static void
test_modes_and_private_teardown(void)
{
    struct ToriRS_UiNodeRef xp;
    struct ToriRS_UiNodeRef private_node;
    struct ToriRS_UiContributionRef modify;
    struct ToriRS_UiContributionRef fallback;
    struct ToriRS_UiContributionRef appearance;
    struct ToriRS_UiContributionRef collides;
    struct ToriRS_UiContributionRef private_ref;
    struct ToriRS_UiContributionStatus status;
    struct ToriRS_UiResolvedNode resolved;
    struct ToriRS_UiNode value;
    char label[] = "private label";

    ToriRS_UiRegistry_Init(&registry_a);
    xp = ToriRS_UiRegistry_Ref(&registry_a, "frame.xp.drops");
    value = node_value(1, 2, 30, 40, 0, 0, NULL, NULL);
    modify = add_contribution(
        &registry_a, "modifier", "xp_drops", TORIRS_UI_MODIFY, TORIRS_UI_FACET_BOUNDS, value);
    value.bounds = (struct ToriRS_Rect){ 5, 6, 70, 80 };
    fallback = add_contribution(
        &registry_a,
        "fallback",
        "frame.xp.drops",
        TORIRS_UI_PROVIDE_IF_MISSING,
        TORIRS_UI_FACET_BOUNDS,
        value);
    value = node_value(0, 0, 0, 0, TORIRS_UI_NODE_VISIBLE, 42, "XP", NULL);
    appearance = add_contribution(
        &registry_a,
        "skin",
        "frame.xp.drops",
        TORIRS_UI_REPLACE_OR_PROVIDE,
        TORIRS_UI_FACET_APPEARANCE,
        value);

    CHECK(ToriRS_UiRegistry_Resolve(&registry_a, xp, &resolved), "providers create an absent node");
    CHECK(
        resolved.available_facets == (TORIRS_UI_FACET_BOUNDS | TORIRS_UI_FACET_APPEARANCE),
        "independent provided facets compose");
    CHECK(strcmp(resolved.bounds_provider, "fallback") == 0, "provide-if-missing owns bounds");
    CHECK(strcmp(resolved.appearance_provider, "skin") == 0, "replacement owns appearance");
    CHECK(
        ToriRS_UiRegistry_ContributionStatus(&registry_a, modify, &status) &&
            status.state == TORIRS_UI_CONTRIBUTION_TARGET_ABSENT,
        "MODIFY explicitly reports an absent base target");
    CHECK(
        ToriRS_UiRegistry_ContributionStatus(&registry_a, fallback, &status) &&
            status.state == TORIRS_UI_CONTRIBUTION_ACTIVE,
        "PROVIDE_IF_MISSING activates without a base");
    CHECK(
        ToriRS_UiRegistry_ContributionStatus(&registry_a, appearance, &status) &&
            status.state == TORIRS_UI_CONTRIBUTION_ACTIVE,
        "REPLACE_OR_PROVIDE activates without a base");

    value = node_value(9, 9, 9, 9, 0, 0, NULL, NULL);
    collides = add_contribution(
        &registry_a,
        "replacement",
        "frame.xp.drops",
        TORIRS_UI_REPLACE_OR_PROVIDE,
        TORIRS_UI_FACET_BOUNDS,
        value);
    CHECK(ToriRS_UiRegistry_Resolve(&registry_a, xp, &resolved), "independent appearance remains");
    CHECK(
        resolved.available_facets == TORIRS_UI_FACET_APPEARANCE &&
            resolved.conflict_facets == TORIRS_UI_FACET_BOUNDS,
        "conflicting bounds stay absent while independent appearance remains");
    CHECK(
        ToriRS_UiRegistry_ContributionStatus(&registry_a, collides, &status) &&
            status.state == TORIRS_UI_CONTRIBUTION_CONFLICT &&
            strcmp(status.conflict_plugin, "fallback") == 0,
        "a conflict status names the other provider");
    CHECK(
        ToriRS_UiRegistry_RemovePlugin(&registry_a, "replacement") == 1,
        "teardown removes contribution");
    CHECK(
        !ToriRS_UiRegistry_ContributionStatus(&registry_a, collides, &status),
        "a torn-down contribution handle becomes invalid");
    CHECK(
        ToriRS_UiRegistry_Resolve(&registry_a, xp, &resolved) &&
            strcmp(resolved.bounds_provider, "fallback") == 0,
        "removing a contender deterministically activates the remaining one");

    value = node_value(
        3, 4, 20, 10, TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ENABLED, 8, label, "activate");
    private_ref = add_contribution(
        &registry_a,
        "private-owner",
        "badge.counter",
        TORIRS_UI_PROVIDE_IF_MISSING,
        TORIRS_UI_FACET_ALL,
        value);
    label[0] = 'X';
    private_node = ToriRS_UiRegistry_PrivateRef(&registry_a, "private-owner", "badge.counter");
    CHECK(
        ToriRS_UiRegistry_Resolve(&registry_a, private_node, &resolved) &&
            strcmp(resolved.value.label, "private label") == 0,
        "retained declarations copy caller-owned strings");
    CHECK(
        ToriRS_UiRegistry_RemovePlugin(&registry_a, "private-owner") == 1,
        "private plugin teardown removes all of its retained state");
    CHECK(
        !ToriRS_UiRegistry_Resolve(&registry_a, private_node, &resolved),
        "private node becomes absent");
    CHECK(
        !ToriRS_UiRegistry_ContributionStatus(&registry_a, private_ref, &status),
        "private contribution handle does not survive teardown");
    CHECK(
        ToriRS_UiRegistry_PrivateRef(&registry_a, "private-owner", "badge.counter").value ==
            private_node.value,
        "the semantic private ref itself remains stable across reload");
}

static void
test_contribution_restatement(void)
{
    struct ToriRS_UiNode initial = node_value(
        1,
        2,
        30,
        40,
        TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ENABLED,
        7,
        "old",
        "activate");
    struct ToriRS_UiNode update = node_value(
        99, 98, 97, 96, TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ACTIVE, 8, "new", NULL);
    struct ToriRS_UiContribution declaration = contribution(
        "badge", TORIRS_UI_REPLACE_OR_PROVIDE, TORIRS_UI_FACET_ALL, initial);
    struct ToriRS_UiContributionRef contribution_ref;
    struct ToriRS_UiResolvedNode resolved;
    struct ToriRS_UiChange change;
    struct ToriRS_UiNodeRef node;
    uint32_t revision;
    char label[] = "updated";

    ToriRS_UiRegistry_Init(&registry_a);
    CHECK(
        ToriRS_UiRegistry_AddContribution(
            &registry_a, "owner", &declaration, &contribution_ref) == TORIRS_UI_REGISTRY_OK,
        "restatable contribution registers");
    while( ToriRS_UiRegistry_ChangeNext(&registry_a, &change) )
        ;
    revision = ToriRS_UiRegistry_Revision(&registry_a);
    CHECK(
        ToriRS_UiRegistry_UpdateContribution(
            &registry_a, contribution_ref, TORIRS_UI_FACET_APPEARANCE, &initial) ==
                TORIRS_UI_REGISTRY_OK &&
            ToriRS_UiRegistry_Revision(&registry_a) == revision &&
            ToriRS_UiRegistry_ChangeCount(&registry_a) == 0,
        "identical facet restatement records no retained mutation");

    update.label = label;
    update.state_image_mask = 1u << TORIRS_UI_VISUAL_ACTIVE;
    update.state_images[TORIRS_UI_VISUAL_ACTIVE].value = 18;
    CHECK(
        ToriRS_UiRegistry_UpdateContribution(
            &registry_a, contribution_ref, TORIRS_UI_FACET_APPEARANCE, &update) ==
            TORIRS_UI_REGISTRY_OK,
        "one declared facet can be restated atomically");
    label[0] = 'X';
    node = ToriRS_UiRegistry_PrivateRef(&registry_a, "owner", "badge");
    CHECK(
        ToriRS_UiRegistry_Resolve(&registry_a, node, &resolved) &&
            rect_is(&resolved.value.bounds, 1, 2, 30, 40) &&
            strcmp(resolved.value.label, "updated") == 0 && resolved.value.image.value == 8 &&
            resolved.value.state_images[TORIRS_UI_VISUAL_ACTIVE].value == 18 &&
            strcmp(resolved.value.action, "activate") == 0,
        "restatement copies strings and preserves facets it did not own in the call");
    CHECK(
        ToriRS_UiRegistry_ChangeCount(&registry_a) == 1 &&
            ToriRS_UiRegistry_ChangeNext(&registry_a, &change) &&
            change.node.value == node.value && change.facets == TORIRS_UI_FACET_APPEARANCE,
        "restatement journals only the facet that actually changed");
    CHECK(
        ToriRS_UiRegistry_UpdateContribution(
            &registry_a, contribution_ref, 1u << 7, &update) == TORIRS_UI_REGISTRY_INVALID,
        "restatement rejects undeclared or unknown facets");
}

static void
populate_conflict(
    struct ToriRS_UiRegistry* registry,
    bool reverse)
{
    static char const* const forward[] = { "zulu", "alpha", "middle" };
    static char const* const backward[] = { "middle", "alpha", "zulu" };
    char const* const* order = reverse ? backward : forward;
    struct ToriRS_UiNode base = node_value(
        20, 30, 40, 50, TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ENABLED, 5, "base", "base-action");

    ToriRS_UiRegistry_Init(registry);
    CHECK(
        ToriRS_UiRegistry_AddBase(
            registry, "base-frame", "frame.orb.run", TORIRS_UI_FACET_ALL, &base) ==
            TORIRS_UI_REGISTRY_OK,
        "conflict fixture gets a complete base");
    for( int i = 0; i < 3; i++ )
    {
        struct ToriRS_UiNode value =
            node_value(0, 0, 0, 0, TORIRS_UI_NODE_VISIBLE, 20 + i, order[i], NULL);
        (void)add_contribution(
            registry, order[i], "orb_run", TORIRS_UI_MODIFY, TORIRS_UI_FACET_APPEARANCE, value);
    }
    {
        struct ToriRS_UiNode value =
            node_value(0, 0, 0, 0, TORIRS_UI_NODE_ENABLED, 0, NULL, "toggle-run");
        (void)add_contribution(
            registry,
            "actions-only",
            "frame.orb.run",
            TORIRS_UI_MODIFY,
            TORIRS_UI_FACET_ACTIONS,
            value);
    }
}

static void
test_conflict_order_independence(void)
{
    struct ToriRS_UiNodeRef run_a;
    struct ToriRS_UiNodeRef run_b;
    struct ToriRS_UiResolvedNode a;
    struct ToriRS_UiResolvedNode b;
    char provider[TORIRS_PLUGIN_NAME_MAX];
    static char const* const expected[] = { "alpha", "middle", "zulu" };

    populate_conflict(&registry_a, false);
    populate_conflict(&registry_b, true);
    run_a = ToriRS_UiRegistry_Ref(&registry_a, "frame.orb.run");
    run_b = ToriRS_UiRegistry_Ref(&registry_b, "frame.orb.run");
    CHECK(
        ToriRS_UiRegistry_Resolve(&registry_a, run_a, &a) &&
            ToriRS_UiRegistry_Resolve(&registry_b, run_b, &b),
        "both declaration orders resolve");
    CHECK(
        a.conflict_facets == TORIRS_UI_FACET_APPEARANCE &&
            b.conflict_facets == TORIRS_UI_FACET_APPEARANCE,
        "both orders identify the same exclusive-facet conflict");
    CHECK(
        a.value.image.value == 5 && b.value.image.value == 5 &&
            strcmp(a.appearance_provider, "base-frame") == 0 &&
            strcmp(b.appearance_provider, "base-frame") == 0,
        "more than one contender leaves base appearance active");
    CHECK(
        strcmp(a.value.action, "toggle-run") == 0 && strcmp(b.value.action, "toggle-run") == 0 &&
            strcmp(a.actions_provider, "actions-only") == 0 &&
            strcmp(b.actions_provider, "actions-only") == 0,
        "an independent actions facet still composes in both orders");
    CHECK(
        ToriRS_UiRegistry_ConflictCount(&registry_a, run_a, TORIRS_UI_FACET_APPEARANCE) == 3 &&
            ToriRS_UiRegistry_ConflictCount(&registry_b, run_b, TORIRS_UI_FACET_APPEARANCE) == 3,
        "all contenders are reported");
    for( int i = 0; i < 3; i++ )
    {
        CHECK(
            ToriRS_UiRegistry_ConflictPluginAt(
                &registry_a, run_a, TORIRS_UI_FACET_APPEARANCE, i, provider, sizeof(provider)) &&
                strcmp(provider, expected[i]) == 0,
            "forward registration reports lexical conflict names");
        CHECK(
            ToriRS_UiRegistry_ConflictPluginAt(
                &registry_b, run_b, TORIRS_UI_FACET_APPEARANCE, i, provider, sizeof(provider)) &&
                strcmp(provider, expected[i]) == 0,
            "reverse registration reports the identical conflict names");
    }
}

static void
test_base_presence_modes_and_duplicates(void)
{
    struct ToriRS_UiNode base = node_value(0, 0, 100, 100, TORIRS_UI_NODE_VISIBLE, 1, "base", NULL);
    struct ToriRS_UiNode replacement =
        node_value(0, 0, 0, 0, TORIRS_UI_NODE_VISIBLE, 2, "replacement", NULL);
    struct ToriRS_UiNode fallback_value = node_value(7, 8, 9, 10, 0, 0, NULL, NULL);
    struct ToriRS_UiContributionRef modify;
    struct ToriRS_UiContributionRef fallback;
    struct ToriRS_UiContributionStatus status;
    struct ToriRS_UiResolvedNode resolved;
    struct ToriRS_UiNodeRef minimap;
    struct ToriRS_UiContribution duplicate;

    ToriRS_UiRegistry_Init(&registry_a);
    minimap = ToriRS_UiRegistry_Ref(&registry_a, "frame.minimap");
    CHECK(
        ToriRS_UiRegistry_AddBase(
            &registry_a, "lane", "frame.minimap", TORIRS_UI_FACET_ALL, &base) ==
            TORIRS_UI_REGISTRY_OK,
        "mode fixture has a base node");
    modify = add_contribution(
        &registry_a,
        "modifier",
        "frame.minimap",
        TORIRS_UI_MODIFY,
        TORIRS_UI_FACET_APPEARANCE,
        replacement);
    fallback = add_contribution(
        &registry_a,
        "fallback",
        "frame.minimap",
        TORIRS_UI_PROVIDE_IF_MISSING,
        TORIRS_UI_FACET_BOUNDS,
        fallback_value);
    CHECK(
        ToriRS_UiRegistry_Resolve(&registry_a, minimap, &resolved) &&
            resolved.value.image.value == 2 && rect_is(&resolved.value.bounds, 0, 0, 100, 100),
        "MODIFY replaces its facet while missing-node fallback stays inactive");
    CHECK(
        ToriRS_UiRegistry_ContributionStatus(&registry_a, modify, &status) &&
            status.state == TORIRS_UI_CONTRIBUTION_ACTIVE,
        "MODIFY is active against a base node");
    CHECK(
        ToriRS_UiRegistry_ContributionStatus(&registry_a, fallback, &status) &&
            status.state == TORIRS_UI_CONTRIBUTION_INACTIVE,
        "PROVIDE_IF_MISSING is inactive against a base node");

    duplicate = contribution("minimap", TORIRS_UI_MODIFY, TORIRS_UI_FACET_ACTIONS, replacement);
    CHECK(
        ToriRS_UiRegistry_AddContribution(&registry_a, "modifier", &duplicate, NULL) ==
            TORIRS_UI_REGISTRY_DUPLICATE,
        "one plugin cannot register the same node twice");

    CHECK(
        ToriRS_UiRegistry_RemoveBaseProvider(&registry_a, "lane") == 3,
        "base teardown removes each facet");
    CHECK(
        ToriRS_UiRegistry_Resolve(&registry_a, minimap, &resolved) &&
            resolved.available_facets == TORIRS_UI_FACET_BOUNDS &&
            rect_is(&resolved.value.bounds, 7, 8, 9, 10),
        "when the base disappears, fallback provides and MODIFY becomes absent");
    CHECK(
        ToriRS_UiRegistry_ContributionStatus(&registry_a, modify, &status) &&
            status.state == TORIRS_UI_CONTRIBUTION_TARGET_ABSENT,
        "MODIFY follows base presence live");
    CHECK(
        ToriRS_UiRegistry_ContributionStatus(&registry_a, fallback, &status) &&
            status.state == TORIRS_UI_CONTRIBUTION_ACTIVE,
        "fallback follows base presence live");
}

static void
drain_changes(struct ToriRS_UiRegistry* registry)
{
    struct ToriRS_UiChange change;

    while( ToriRS_UiRegistry_ChangeNext(registry, &change) )
        ;
}

static void
test_change_journal(void)
{
    struct ToriRS_UiNode appearance =
        node_value(0, 0, 0, 0, TORIRS_UI_NODE_VISIBLE, 3, "run", NULL);
    struct ToriRS_UiNode actions =
        node_value(0, 0, 0, 0, TORIRS_UI_NODE_ENABLED, 0, NULL, "toggle-run");
    struct ToriRS_UiNodeRef run;
    struct ToriRS_UiChange change;

    ToriRS_UiRegistry_Init(&registry_a);
    CHECK(ToriRS_UiRegistry_ChangeCount(&registry_a) == 0, "initial vocabulary emits no UI work");
    run = ToriRS_UiRegistry_Ref(&registry_a, "frame.orb.run");
    (void)add_contribution(
        &registry_a,
        "skin",
        "frame.orb.run",
        TORIRS_UI_REPLACE_OR_PROVIDE,
        TORIRS_UI_FACET_APPEARANCE,
        appearance);
    (void)add_contribution(
        &registry_a,
        "actions",
        "orb_run",
        TORIRS_UI_REPLACE_OR_PROVIDE,
        TORIRS_UI_FACET_ACTIONS,
        actions);
    CHECK(
        ToriRS_UiRegistry_ChangeCount(&registry_a) == 1,
        "several mutations of one semantic node queue one executor command");
    CHECK(
        ToriRS_UiRegistry_ChangeNext(&registry_a, &change) && change.node.value == run.value &&
            change.facets == (TORIRS_UI_FACET_APPEARANCE | TORIRS_UI_FACET_ACTIONS) &&
            change.revision == ToriRS_UiRegistry_Revision(&registry_a),
        "the queued command records the exact changed facets and committed revision");
    CHECK(
        !ToriRS_UiRegistry_ChangeNext(&registry_a, &change),
        "draining changes requires no scan for an empty tail");

    CHECK(ToriRS_UiRegistry_RemovePlugin(&registry_a, "skin") == 1, "one provider is removed");
    CHECK(
        ToriRS_UiRegistry_ChangeNext(&registry_a, &change) && change.node.value == run.value &&
            change.facets == TORIRS_UI_FACET_APPEARANCE,
        "removal is an explicit retained-state command too");

    /* Base presence controls MODIFY/PROVIDE_IF_MISSING eligibility for every
     * facet.  The journal must include that indirect change as well. */
    ToriRS_UiRegistry_Init(&registry_a);
    run = ToriRS_UiRegistry_Ref(&registry_a, "frame.orb.run");
    appearance = node_value(1, 2, 30, 40, 0, 0, NULL, NULL);
    (void)add_contribution(
        &registry_a,
        "fallback",
        "frame.orb.run",
        TORIRS_UI_PROVIDE_IF_MISSING,
        TORIRS_UI_FACET_BOUNDS,
        appearance);
    drain_changes(&registry_a);
    CHECK(
        ToriRS_UiRegistry_AddBase(
            &registry_a, "lane", "frame.orb.run", TORIRS_UI_FACET_ACTIONS, &actions) ==
            TORIRS_UI_REGISTRY_OK,
        "adding the first base facet changes fallback eligibility");
    CHECK(
        ToriRS_UiRegistry_ChangeNext(&registry_a, &change) && change.node.value == run.value &&
            change.facets == (TORIRS_UI_FACET_BOUNDS | TORIRS_UI_FACET_ACTIONS),
        "the journal records indirect facet changes caused by base presence");
}

static void
test_atomic_base_replace(void)
{
    struct ToriRS_UiBaseDeclaration initial[2];
    struct ToriRS_UiBaseDeclaration duplicate[2];
    struct ToriRS_UiBaseDeclaration cycle[2];
    struct ToriRS_UiBaseDeclaration replacement;
    struct ToriRS_UiNodeRef report;
    struct ToriRS_UiNodeRef minimap;
    struct ToriRS_UiResolvedNode resolved;
    struct ToriRS_UiChange change;
    uint32_t generation;
    uint32_t revision;
    int names;
    int saw_report = 0;
    int saw_minimap = 0;

    ToriRS_UiRegistry_Init(&registry_a);
    initial[0] = (struct ToriRS_UiBaseDeclaration){
        .provider = "old-frame",
        .node = "report_button",
        .facets = TORIRS_UI_FACET_ALL,
        .value = node_value(
            1, 2, 30, 40, TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ENABLED, 7, "old", "activate"),
    };
    initial[1] = (struct ToriRS_UiBaseDeclaration){
        .provider = "old-frame",
        .node = "frame.minimap",
        .facets = TORIRS_UI_FACET_BOUNDS,
        .value = node_value(50, 60, 70, 80, 0, 0, NULL, NULL),
    };
    CHECK(
        ToriRS_UiRegistry_ReplaceBase(&registry_a, initial, 2) == TORIRS_UI_REGISTRY_OK,
        "a complete scratch base commits at once");
    report = ToriRS_UiRegistry_Ref(&registry_a, "frame.chat.button.report");
    minimap = ToriRS_UiRegistry_Ref(&registry_a, "frame.minimap");
    drain_changes(&registry_a);
    generation = ToriRS_UiRegistry_BaseGeneration(&registry_a);
    revision = ToriRS_UiRegistry_Revision(&registry_a);

    duplicate[0] = initial[0];
    duplicate[0].facets = TORIRS_UI_FACET_BOUNDS;
    duplicate[1] = initial[0];
    duplicate[1].node = "frame.chat.button.report";
    duplicate[1].facets = TORIRS_UI_FACET_BOUNDS;
    CHECK(
        ToriRS_UiRegistry_ReplaceBase(&registry_a, duplicate, 2) == TORIRS_UI_REGISTRY_DUPLICATE,
        "alias-equivalent duplicate facets reject the whole candidate");
    CHECK(
        ToriRS_UiRegistry_BaseGeneration(&registry_a) == generation &&
            ToriRS_UiRegistry_Revision(&registry_a) == revision &&
            ToriRS_UiRegistry_ChangeCount(&registry_a) == 0,
        "a rejected duplicate publishes no generation, revision, or command");
    CHECK(
        ToriRS_UiRegistry_Resolve(&registry_a, report, &resolved) &&
            strcmp(resolved.value.label, "old") == 0,
        "a rejected base leaves the prior live tree intact");

    cycle[0] = (struct ToriRS_UiBaseDeclaration){
        .provider = "bad-frame",
        .node = "plugin.fixture.a",
        .facets = TORIRS_UI_FACET_BOUNDS,
        .value = node_value(0, 0, 10, 10, 0, 0, NULL, NULL),
    };
    cycle[0].value.parent = "plugin.fixture.b";
    cycle[1] = (struct ToriRS_UiBaseDeclaration){
        .provider = "bad-frame",
        .node = "plugin.fixture.b",
        .facets = TORIRS_UI_FACET_BOUNDS,
        .value = node_value(0, 0, 10, 10, 0, 0, NULL, NULL),
    };
    cycle[1].value.parent = "plugin.fixture.a";
    names = ToriRS_UiRegistry_NodeCount(&registry_a);
    CHECK(
        ToriRS_UiRegistry_ReplaceBase(&registry_a, cycle, 2) == TORIRS_UI_REGISTRY_INVALID,
        "an indirect parent cycle rejects the whole candidate");
    CHECK(
        ToriRS_UiRegistry_NodeCount(&registry_a) == names &&
            ToriRS_UiRegistry_BaseGeneration(&registry_a) == generation &&
            ToriRS_UiRegistry_Revision(&registry_a) == revision &&
            ToriRS_UiRegistry_ChangeCount(&registry_a) == 0,
        "rejected candidate names and state roll back atomically");
    CHECK(
        ToriRS_UiRegistry_Resolve(&registry_a, report, &resolved) &&
            strcmp(resolved.value.label, "old") == 0,
        "cyclic scratch data cannot blank the active frame");

    replacement = (struct ToriRS_UiBaseDeclaration){
        .provider = "new-frame",
        .node = "frame.chat.button.report",
        .facets = TORIRS_UI_FACET_ALL,
        .value = node_value(
            9, 8, 70, 60, TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ENABLED, 8, "new", "activate"),
    };
    CHECK(
        ToriRS_UiRegistry_ReplaceBase(&registry_a, &replacement, 1) == TORIRS_UI_REGISTRY_OK,
        "a valid replacement commits after rejected candidates");
    CHECK(
        ToriRS_UiRegistry_BaseGeneration(&registry_a) == generation + 1 &&
            ToriRS_UiRegistry_Revision(&registry_a) == revision + 1,
        "the whole replacement is one committed revision");
    CHECK(
        ToriRS_UiRegistry_ChangeCount(&registry_a) == 2,
        "replacement queues the updated node and an omitted-node removal");
    while( ToriRS_UiRegistry_ChangeNext(&registry_a, &change) )
    {
        if( change.node.value == report.value )
            saw_report++;
        if( change.node.value == minimap.value )
            saw_minimap++;
    }
    CHECK(saw_report == 1 && saw_minimap == 1, "base changes are deduplicated by semantic ref");
    CHECK(
        ToriRS_UiRegistry_Resolve(&registry_a, report, &resolved) &&
            strcmp(resolved.value.label, "new") == 0,
        "the committed replacement is now live");
    CHECK(
        !ToriRS_UiRegistry_Resolve(&registry_a, minimap, &resolved),
        "an omitted base node resolves as an explicit removal");
}

static void
test_rejected_additions_and_teardown_transaction(void)
{
    struct ToriRS_UiNode value = node_value(0, 0, 10, 10, 0, 0, NULL, NULL);
    struct ToriRS_UiContribution declaration;
    struct ToriRS_UiContributionRef contribution_ref = { 99 };
    struct ToriRS_UiNodeRef owner_node;
    struct ToriRS_UiChange change;
    uint32_t revision;
    int names;

    ToriRS_UiRegistry_Init(&registry_a);
    names = ToriRS_UiRegistry_NodeCount(&registry_a);
    revision = ToriRS_UiRegistry_Revision(&registry_a);
    value.parent = "new-node";
    declaration =
        contribution("new-node", TORIRS_UI_PROVIDE_IF_MISSING, TORIRS_UI_FACET_BOUNDS, value);
    CHECK(
        ToriRS_UiRegistry_AddContribution(
            &registry_a, "bad-plugin", &declaration, &contribution_ref) ==
            TORIRS_UI_REGISTRY_INVALID,
        "a directly self-parented private declaration is rejected");
    CHECK(
        contribution_ref.value == 0 && ToriRS_UiRegistry_NodeCount(&registry_a) == names &&
            ToriRS_UiRegistry_Revision(&registry_a) == revision &&
            ToriRS_UiRegistry_ChangeCount(&registry_a) == 0,
        "a rejected addition cannot leak interned names or executor work");
    {
        bool all_rejected = true;
        for( int i = 0; i < TORIRS_UI_REGISTRY_NODES_MAX + 20; i++ )
        {
            char local[32];
            (void)snprintf(local, sizeof(local), "rejected.%d", i);
            value.parent = local;
            declaration =
                contribution(local, TORIRS_UI_PROVIDE_IF_MISSING, TORIRS_UI_FACET_BOUNDS, value);
            if( ToriRS_UiRegistry_AddContribution(&registry_a, "bad-plugin", &declaration, NULL) !=
                TORIRS_UI_REGISTRY_INVALID )
                all_rejected = false;
        }
        CHECK(
            all_rejected && ToriRS_UiRegistry_NodeCount(&registry_a) == names,
            "repeated rejected declarations cannot exhaust permanent intern slots");
    }

    value.parent = "plugin.owner.b";
    CHECK(
        ToriRS_UiRegistry_AddBase(
            &registry_a, "owner", "plugin.owner.a", TORIRS_UI_FACET_BOUNDS, &value) ==
            TORIRS_UI_REGISTRY_OK,
        "an acyclic plugin-owned base node registers");
    value.parent = "plugin.owner.a";
    declaration = contribution("b", TORIRS_UI_REPLACE_OR_PROVIDE, TORIRS_UI_FACET_BOUNDS, value);
    names = ToriRS_UiRegistry_NodeCount(&registry_a);
    revision = ToriRS_UiRegistry_Revision(&registry_a);
    CHECK(
        ToriRS_UiRegistry_AddContribution(&registry_a, "owner", &declaration, &contribution_ref) ==
            TORIRS_UI_REGISTRY_INVALID,
        "a mixed base/contribution indirect cycle is rejected");
    CHECK(
        ToriRS_UiRegistry_NodeCount(&registry_a) == names &&
            ToriRS_UiRegistry_Revision(&registry_a) == revision,
        "an indirect-cycle rejection is atomic too");

    value.parent = NULL;
    value.flags = TORIRS_UI_NODE_VISIBLE;
    value.image.value = 4;
    declaration = contribution("a", TORIRS_UI_MODIFY, TORIRS_UI_FACET_APPEARANCE, value);
    CHECK(
        ToriRS_UiRegistry_AddContribution(&registry_a, "owner", &declaration, &contribution_ref) ==
            TORIRS_UI_REGISTRY_OK,
        "the owner can add a non-cyclic independent facet");
    owner_node = ToriRS_UiRegistry_PrivateRef(&registry_a, "owner", "a");
    drain_changes(&registry_a);
    revision = ToriRS_UiRegistry_Revision(&registry_a);
    CHECK(
        ToriRS_UiRegistry_RemovePlugin(&registry_a, "owner") == 2,
        "plugin teardown removes one contribution and one base facet");
    CHECK(
        ToriRS_UiRegistry_Revision(&registry_a) == revision + 1,
        "plugin teardown is one coalesced retained-state revision");
    CHECK(
        ToriRS_UiRegistry_ChangeCount(&registry_a) == 1 &&
            ToriRS_UiRegistry_ChangeNext(&registry_a, &change) &&
            change.node.value == owner_node.value &&
            change.facets == (TORIRS_UI_FACET_BOUNDS | TORIRS_UI_FACET_APPEARANCE),
        "teardown queues one merged removal command for the affected node");
}

int
main(void)
{
    test_names_and_aliases();
    test_base_facets_and_generations();
    test_rich_facet_payloads();
    test_modes_and_private_teardown();
    test_contribution_restatement();
    test_conflict_order_independence();
    test_base_presence_modes_and_duplicates();
    test_change_journal();
    test_atomic_base_replace();
    test_rejected_additions_and_teardown_transaction();

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
