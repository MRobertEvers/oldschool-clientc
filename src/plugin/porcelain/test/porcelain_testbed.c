/*
 * The shared fake engine. @see porcelain_testbed.h
 *
 * Every entry point records a line before it does anything, so a test's
 * assertion about engine traffic reads like the call it is asserting about.
 * Nothing here models the real widget tree: the point is the CONTRACT, not
 * the raster.
 */

#include "plugin/porcelain/test/porcelain_testbed.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Testbed g_testbed;

/* ------------------------------------------------------------------------ */
/* The log                                                                  */
/* ------------------------------------------------------------------------ */

static void
testbed_log(char const* format, ...)
{
    va_list args;

    g_testbed.calls++;
    if( g_testbed.log_count >= TESTBED_LOG_MAX )
        return;
    va_start(args, format);
    vsnprintf(g_testbed.log[g_testbed.log_count], TESTBED_LOG_LINE, format, args);
    va_end(args);
    g_testbed.log_count++;
}

void
Testbed_ClearLog(void)
{
    g_testbed.log_count = 0;
    g_testbed.calls = 0;
}

int
Testbed_LogCount(void)
{
    return g_testbed.log_count;
}

char const*
Testbed_LogLine(int index)
{
    assert(index >= 0);
    assert(index < g_testbed.log_count);
    return g_testbed.log[index];
}

int
Testbed_LogCountWith(char const* prefix)
{
    size_t const length = strlen(prefix);
    int count = 0;

    assert(prefix);
    for( int i = 0; i < g_testbed.log_count; i++ )
        if( strncmp(g_testbed.log[i], prefix, length) == 0 )
            count++;
    return count;
}

int
Testbed_LogFind(char const* prefix)
{
    size_t const length = strlen(prefix);

    assert(prefix);
    for( int i = 0; i < g_testbed.log_count; i++ )
        if( strncmp(g_testbed.log[i], prefix, length) == 0 )
            return i;
    return -1;
}

void
Testbed_PrintLog(void)
{
    for( int i = 0; i < g_testbed.log_count; i++ )
        fprintf(stderr, "  [%02d] %s\n", i, g_testbed.log[i]);
}

/* ------------------------------------------------------------------------ */
/* Elements                                                                 */
/* ------------------------------------------------------------------------ */

struct TestbedElement*
Testbed_Element(char const* role)
{
    assert(role);
    for( int i = 0; i < TESTBED_ELEMENTS_MAX; i++ )
        if( g_testbed.elements[i].used && strcmp(g_testbed.elements[i].role, role) == 0 )
            return &g_testbed.elements[i];
    return NULL;
}

static struct ToriRS_WidgetRef
testbed_mint_ref(void)
{
    struct ToriRS_WidgetRef ref;

    g_testbed.next_ref++;
    ref.opaque[0] = 0x900d;
    ref.opaque[1] = (uint64_t)g_testbed.next_ref;
    ref.opaque[2] = 1;
    return ref;
}

struct TestbedElement*
Testbed_DeclareElement(char const* role, int x, int y, int width, int height)
{
    assert(role);
    for( int i = 0; i < TESTBED_ELEMENTS_MAX; i++ )
    {
        struct TestbedElement* element = &g_testbed.elements[i];
        if( element->used )
            continue;
        memset(element, 0, sizeof(*element));
        element->used = true;
        snprintf(element->role, sizeof(element->role), "%s", role);
        element->local = (struct ToriRS_WidgetBounds){x, y, width, height};
        element->bounds = element->local;
        element->presented = true;
        element->input_present = true;
        element->incarnation = 1;
        element->ref = testbed_mint_ref();
        /* Every declared element hangs off one shared parent, which is also
         * what the frame-root walk finds. */
        element->parent.opaque[0] = 0x900d;
        element->parent.opaque[1] = 1;
        element->parent.opaque[2] = 1;
        return element;
    }
    assert(0 && "testbed element table full");
    return NULL;
}

/*
 * A topology publication, and what raises one.
 *
 * The host raises TREE_CHANGED for a structural move only -- a widget
 * appearing or going away -- and never for geometry or a hide. Modelled
 * exactly here, because the layer now decides when to re-ask about an
 * unresolved element off this signal, and a fake that published on every
 * mutation would hide a poll that still runs on a clock.
 */
static void
testbed_publish_tree(void)
{
    struct ToriRS_WidgetEvent event;

    g_testbed.tree_generation++;
    if( !g_testbed.tree_watch.fn )
        return;
    memset(&event, 0, sizeof(event));
    event.type = TORIRS_WIDGET_TREE_CHANGED;
    event.native_revision = g_testbed.tree_generation;
    event.role = "";
    g_testbed.tree_watch.fn(&g_testbed.api, g_testbed.tree_watch.user, &event);
}

void
Testbed_PublishTree(void)
{
    testbed_publish_tree();
}

static void
testbed_raise_ref(char const* role, enum ToriRS_WidgetEventType type,
                  struct ToriRS_WidgetRef ref)
{
    struct ToriRS_WidgetEvent event;

    memset(&event, 0, sizeof(event));
    event.type = type;
    event.role = role;
    event.widget = ref;
    for( int i = 0; i < TESTBED_WATCHES_MAX; i++ )
    {
        if( !g_testbed.watches[i].used || strcmp(g_testbed.watches[i].role, role) != 0 )
            continue;
        g_testbed.watches[i].fn(&g_testbed.api, g_testbed.watches[i].user, &event);
    }
}

static void
testbed_raise(char const* role, enum ToriRS_WidgetEventType type)
{
    struct TestbedElement const* element = Testbed_Element(role);
    struct ToriRS_WidgetRef ref;

    memset(&ref, 0, sizeof(ref));
    if( element )
        ref = element->ref;
    testbed_raise_ref(role, type, ref);
}

void
Testbed_BindElement(char const* role)
{
    struct TestbedElement* element = Testbed_Element(role);

    assert(element);
    element->bound = true;
    testbed_publish_tree();
    testbed_raise(role, TORIRS_WIDGET_BOUND);
}

/*
 * The node that comes back is NOT the node that went away.
 *
 * A logout clears the tree and a login builds a fresh one, so the chat bar the
 * player sees afterwards is a different widget that happens to answer to the
 * same name. The contract already says so -- "UNBOUND for the old incarnation
 * then BOUND for the new one" -- and a testbed that handed the same ref back
 * made every re-bind path look correct whether or not it re-read anything,
 * because a stale ref and a fresh one were the same eight bytes.
 *
 * So the identity is retired here, at the unbind, and the UNBOUND event still
 * carries the ref that is going away -- which is the one a listener needs, to
 * recognise WHICH of its widgets it just lost.
 */
void
Testbed_UnbindElement(char const* role)
{
    struct TestbedElement* element = Testbed_Element(role);
    struct ToriRS_WidgetRef previous;

    assert(element);
    previous = element->ref;
    element->bound = false;
    element->ref = testbed_mint_ref();
    element->incarnation++;
    testbed_publish_tree();
    testbed_raise_ref(role, TORIRS_WIDGET_UNBOUND, previous);
}

void
Testbed_MoveElement(char const* role, int x, int y)
{
    struct TestbedElement* element = Testbed_Element(role);

    assert(element);
    element->local.x = x;
    element->local.y = y;
    element->bounds.x = x;
    element->bounds.y = y;
    if( element->bound )
        testbed_raise(role, TORIRS_WIDGET_STATE_CHANGED);
}

void
Testbed_RefuseAnchors(bool refuse)
{
    g_testbed.refuse_anchors = refuse;
}

void
Testbed_KillControl(char const* key)
{
    assert(key);
    for( int i = 0; i < TESTBED_CONTROLS_MAX; i++ )
        if( g_testbed.controls[i].live && strcmp(g_testbed.controls[i].key, key) == 0 )
        {
            g_testbed.controls[i].live = false;
            return;
        }
}

void
Testbed_PresentElement(char const* role, bool presented)
{
    struct TestbedElement* element = Testbed_Element(role);

    assert(element);
    element->presented = presented;
    element->own_hidden = !presented;
    if( element->bound )
        testbed_raise(role, TORIRS_WIDGET_STATE_CHANGED);
}

/* ------------------------------------------------------------------------ */
/* Assets                                                                   */
/* ------------------------------------------------------------------------ */

static struct TestbedAsset*
testbed_asset(char const* name)
{
    for( int i = 0; i < TESTBED_ASSETS_MAX; i++ )
        if( g_testbed.assets[i].used && strcmp(g_testbed.assets[i].name, name) == 0 )
            return &g_testbed.assets[i];
    return NULL;
}

void
Testbed_DeclareAsset(char const* name, enum ToriRS_AssetState state)
{
    assert(name);
    for( int i = 0; i < TESTBED_ASSETS_MAX; i++ )
    {
        struct TestbedAsset* asset = &g_testbed.assets[i];
        if( asset->used )
            continue;
        memset(asset, 0, sizeof(*asset));
        asset->used = true;
        snprintf(asset->name, sizeof(asset->name), "%s", name);
        asset->state = state;
        asset->value = i + 1;
        return;
    }
    assert(0 && "testbed asset table full");
}

void
Testbed_LandAsset(char const* name)
{
    struct TestbedAsset* asset = testbed_asset(name);

    assert(asset);
    asset->state = TORIRS_ASSET_READY;
}

void
Testbed_DeclareImage(char const* name, enum ToriRS_AssetState state, int width, int height)
{
    struct TestbedAsset* asset;

    assert(name);
    Testbed_DeclareAsset(name, state);
    asset = testbed_asset(name);
    assert(asset);
    asset->width = width;
    asset->height = height;
}

void
Testbed_DeclareFile(char const* name, char const* body)
{
    struct TestbedAsset* asset;

    assert(name);
    assert(body);
    Testbed_DeclareAsset(name, TORIRS_ASSET_READY);
    asset = testbed_asset(name);
    assert(asset);
    snprintf(asset->body, sizeof(asset->body), "%s", body);
    asset->has_body = true;
}

bool
Testbed_AssetHeld(char const* name)
{
    struct TestbedAsset const* asset = testbed_asset(name);

    assert(name);
    assert(asset);
    return asset->held;
}

/* ------------------------------------------------------------------------ */
/* Config                                                                   */
/* ------------------------------------------------------------------------ */

static struct TestbedConfigRow*
testbed_config(char const* key, bool create)
{
    struct TestbedConfigRow* free_row = NULL;

    for( int i = 0; i < TESTBED_CONFIG_MAX; i++ )
    {
        struct TestbedConfigRow* row = &g_testbed.config[i];
        if( !row->used )
        {
            if( !free_row )
                free_row = row;
            continue;
        }
        if( strcmp(row->key, key) == 0 )
            return row;
    }
    if( !create )
        return NULL;
    assert(free_row);
    memset(free_row, 0, sizeof(*free_row));
    free_row->used = true;
    snprintf(free_row->key, sizeof(free_row->key), "%s", key);
    return free_row;
}

void
Testbed_SetConfigString(char const* key, char const* value)
{
    struct TestbedConfigRow* row = testbed_config(key, true);

    assert(key);
    assert(value);
    snprintf(row->value, sizeof(row->value), "%s", value);
}

void
Testbed_SetConfigInt(char const* key, int value)
{
    struct TestbedConfigRow* row = testbed_config(key, true);

    assert(key);
    row->number = value;
    row->has_number = true;
}

char const*
Testbed_ConfigString(char const* key)
{
    struct TestbedConfigRow const* row = testbed_config(key, false);

    assert(key);
    return row ? row->value : NULL;
}

/* ------------------------------------------------------------------------ */
/* Controls                                                                 */
/* ------------------------------------------------------------------------ */

struct TestbedControl*
Testbed_Control(char const* key)
{
    assert(key);
    for( int i = 0; i < TESTBED_CONTROLS_MAX; i++ )
        if( g_testbed.controls[i].live && strcmp(g_testbed.controls[i].key, key) == 0 )
            return &g_testbed.controls[i];
    return NULL;
}

void
Testbed_PanelSetSelected(char const* id, char const* value)
{
    assert(id);
    assert(value);
    for( int i = 0; i < g_testbed.panel_row_count; i++ )
        if( strcmp(g_testbed.panel_rows[i].id, id) == 0 )
        {
            snprintf(g_testbed.panel_rows[i].selected,
                     sizeof(g_testbed.panel_rows[i].selected), "%s", value);
            return;
        }
}

void
Testbed_SetPanelAbsent(bool absent)
{
    g_testbed.panel_no_page = absent;
}

void
Testbed_DestroyOwnedControls(void)
{
    for( int i = 0; i < TESTBED_CONTROLS_MAX; i++ )
        if( g_testbed.controls[i].live )
        {
            testbed_log("destroy_owned %s", g_testbed.controls[i].key);
            /* Forgotten outright, not marked hidden: the ref has to stop
             * RESOLVING, which is what makes every setter aimed at it answer
             * STALE_REFERENCE the way the engine does. */
            memset(&g_testbed.controls[i], 0, sizeof(g_testbed.controls[i]));
        }
}

int
Testbed_LiveControls(void)
{
    int count = 0;

    for( int i = 0; i < TESTBED_CONTROLS_MAX; i++ )
        if( g_testbed.controls[i].live )
            count++;
    return count;
}

int
Testbed_LiveWatches(void)
{
    int count = 0;
    for( int i = 0; i < TESTBED_WATCHES_MAX; i++ )
        if( g_testbed.watches[i].used )
            count++;
    return count;
}

static struct TestbedControl*
testbed_control_by_ref(struct ToriRS_WidgetRef ref)
{
    for( int i = 0; i < TESTBED_CONTROLS_MAX; i++ )
        if( g_testbed.controls[i].live && ToriRS_WidgetRefEqual(g_testbed.controls[i].ref, ref) )
            return &g_testbed.controls[i];
    return NULL;
}

/* ------------------------------------------------------------------------ */
/* The widget namespace                                                     */
/* ------------------------------------------------------------------------ */

static enum ToriRS_ContractResult
fake_find(void* context, char const* role, struct ToriRS_WidgetRef* out)
{
    struct TestbedElement const* element;

    (void)context;
    testbed_log("find %s", role);
    element = Testbed_Element(role);
    if( !element || !element->bound )
        return TORIRS_CONTRACT_UNAVAILABLE;
    *out = element->ref;
    return TORIRS_CONTRACT_OK;
}

static enum ToriRS_ContractResult
fake_watch_state(void* context, char const* role, ToriRS_WidgetListener listener, void* user)
{
    (void)context;
    testbed_log("watch_state %s", role);
    /* The host keys a subscription by ROLE NAME and a NULL listener drops it.
     * Modelled here because a library that re-spells an element has to give
     * the old name back, and a fake that kept it would hide the leak. */
    if( !listener )
    {
        for( int i = 0; i < TESTBED_WATCHES_MAX; i++ )
            if( g_testbed.watches[i].used && strcmp(g_testbed.watches[i].role, role) == 0 )
                memset(&g_testbed.watches[i], 0, sizeof(g_testbed.watches[i]));
        return TORIRS_CONTRACT_OK;
    }
    for( int i = 0; i < TESTBED_WATCHES_MAX; i++ )
    {
        if( g_testbed.watches[i].used )
            continue;
        g_testbed.watches[i].used = true;
        snprintf(g_testbed.watches[i].role, sizeof(g_testbed.watches[i].role), "%s", role);
        g_testbed.watches[i].fn = listener;
        g_testbed.watches[i].user = user;
        /* "A new subscription receives BOUND when available." */
        if( Testbed_Element(role) && Testbed_Element(role)->bound )
        {
            struct ToriRS_WidgetEvent event;
            memset(&event, 0, sizeof(event));
            event.type = TORIRS_WIDGET_BOUND;
            event.role = g_testbed.watches[i].role;
            event.widget = Testbed_Element(role)->ref;
            listener(&g_testbed.api, user, &event);
        }
        return TORIRS_CONTRACT_OK;
    }
    return TORIRS_CONTRACT_BUDGET_EXCEEDED;
}

/* "Initial notification and subsequent topology publications." The opening
 * one is part of the contract and the layer must not read it as news. */
static enum ToriRS_ContractResult
fake_watch_tree(void* context, ToriRS_WidgetListener listener, void* user)
{
    struct ToriRS_WidgetEvent event;

    (void)context;
    testbed_log("watch_tree");
    if( !listener )
    {
        memset(&g_testbed.tree_watch, 0, sizeof(g_testbed.tree_watch));
        return TORIRS_CONTRACT_OK;
    }
    g_testbed.tree_watch.fn = listener;
    g_testbed.tree_watch.user = user;
    memset(&event, 0, sizeof(event));
    event.type = TORIRS_WIDGET_TREE_CHANGED;
    event.native_revision = g_testbed.tree_generation;
    event.role = "";
    listener(&g_testbed.api, user, &event);
    return TORIRS_CONTRACT_OK;
}

static struct TestbedElement*
testbed_element_by_ref(struct ToriRS_WidgetRef ref)
{
    for( int i = 0; i < TESTBED_ELEMENTS_MAX; i++ )
        if( g_testbed.elements[i].used && ToriRS_WidgetRefEqual(g_testbed.elements[i].ref, ref) )
            return &g_testbed.elements[i];
    return NULL;
}

static enum ToriRS_ContractResult
fake_state(void* context, struct ToriRS_WidgetRef ref, struct ToriRS_WidgetState* out)
{
    struct TestbedElement const* element = testbed_element_by_ref(ref);

    (void)context;
    testbed_log("state %llu", (unsigned long long)ref.opaque[1]);
    if( !element )
        return TORIRS_CONTRACT_STALE_REFERENCE;
    out->bounds = element->bounds;
    out->local = element->local;
    out->presented = element->presented;
    out->own_hidden = element->own_hidden;
    out->native_hidden = element->native_hidden;
    out->input_present = element->input_present;
    out->graphic_token = element->graphic_token;
    out->facets = element->facets;
    out->incarnation = element->incarnation;
    return TORIRS_CONTRACT_OK;
}

static enum ToriRS_ContractResult
fake_parent(void* context, struct ToriRS_WidgetRef ref, struct ToriRS_WidgetRef* out)
{
    struct TestbedElement const* element = testbed_element_by_ref(ref);

    (void)context;
    testbed_log("parent %llu", (unsigned long long)ref.opaque[1]);
    if( element )
    {
        *out = element->parent;
        return TORIRS_CONTRACT_OK;
    }
    /* The shared parent has no parent of its own: the walk stops there. */
    return TORIRS_CONTRACT_UNAVAILABLE;
}

static enum ToriRS_ContractResult
fake_bounds(void* context, struct ToriRS_WidgetRef ref, struct ToriRS_WidgetBounds* out)
{
    struct TestbedElement const* element = testbed_element_by_ref(ref);

    (void)context;
    testbed_log("bounds %llu", (unsigned long long)ref.opaque[1]);
    if( element )
    {
        *out = element->bounds;
        return TORIRS_CONTRACT_OK;
    }
    *out = (struct ToriRS_WidgetBounds){0, 0, 800, 500};
    return TORIRS_CONTRACT_OK;
}

static enum ToriRS_ContractResult
testbed_create(char const* what, struct ToriRS_WidgetRef parent, char const* key,
               struct ToriRS_WidgetRef* out)
{
    testbed_log("%s %s", what, key);
    for( int i = 0; i < TESTBED_CONTROLS_MAX; i++ )
    {
        struct TestbedControl* control = &g_testbed.controls[i];
        if( control->live && strcmp(control->key, key) == 0 )
        {
            *out = control->ref;
            return TORIRS_CONTRACT_OK;
        }
    }
    for( int i = 0; i < TESTBED_CONTROLS_MAX; i++ )
    {
        struct TestbedControl* control = &g_testbed.controls[i];
        if( control->live )
            continue;
        memset(control, 0, sizeof(*control));
        control->live = true;
        snprintf(control->key, sizeof(control->key), "%s", key);
        control->parent = parent;
        control->ref = testbed_mint_ref();
        control->opacity = 255;
        *out = control->ref;
        return TORIRS_CONTRACT_OK;
    }
    return TORIRS_CONTRACT_BUDGET_EXCEEDED;
}

static enum ToriRS_ContractResult
fake_create_image(void* context, struct ToriRS_WidgetRef parent, char const* key,
                  struct ToriRS_WidgetRef* out)
{
    (void)context;
    return testbed_create("create_image", parent, key, out);
}

static enum ToriRS_ContractResult
fake_create_text(void* context, struct ToriRS_WidgetRef parent, char const* key,
                 struct ToriRS_WidgetRef* out)
{
    (void)context;
    return testbed_create("create_text", parent, key, out);
}

static enum ToriRS_ContractResult
fake_set_position(void* context, struct ToriRS_WidgetRef ref, int32_t x, int32_t y)
{
    struct TestbedControl* control = testbed_control_by_ref(ref);
    struct TestbedElement* element = testbed_element_by_ref(ref);

    (void)context;
    testbed_log("set_position %s %d,%d", control ? control->key : (element ? element->role : "?"),
                (int)x, (int)y);
    /* A reference to nothing at all: the engine's answer for a node that has
     * been destroyed. @see Testbed_KillControl. */
    if( !control && !element )
        return TORIRS_CONTRACT_STALE_REFERENCE;
    if( control )
    {
        control->x = x;
        control->y = y;
    }
    else if( element )
    {
        element->local.x = x;
        element->local.y = y;
    }
    return TORIRS_CONTRACT_OK;
}

static enum ToriRS_ContractResult
fake_set_size(void* context, struct ToriRS_WidgetRef ref, int32_t width, int32_t height)
{
    struct TestbedControl* control = testbed_control_by_ref(ref);
    struct TestbedElement* element = testbed_element_by_ref(ref);

    (void)context;
    testbed_log("set_size %s %dx%d", control ? control->key : (element ? element->role : "?"),
                (int)width, (int)height);
    /* A reference to nothing at all: the engine's answer for a node that has
     * been destroyed, from EVERY entry point. @see Testbed_KillControl. */
    if( !control && !element )
        return TORIRS_CONTRACT_STALE_REFERENCE;
    if( control )
    {
        control->width = width;
        control->height = height;
    }
    return TORIRS_CONTRACT_OK;
}

static enum ToriRS_ContractResult
fake_set_hidden(void* context, struct ToriRS_WidgetRef ref, bool hidden)
{
    struct TestbedControl* control = testbed_control_by_ref(ref);
    struct TestbedElement* element = testbed_element_by_ref(ref);

    (void)context;
    testbed_log("set_hidden %s %d", control ? control->key : (element ? element->role : "?"),
                hidden ? 1 : 0);
    /* A reference to nothing at all: the engine's answer for a node that has
     * been destroyed, from EVERY entry point. @see Testbed_KillControl. */
    if( !control && !element )
        return TORIRS_CONTRACT_STALE_REFERENCE;
    if( control )
        control->hidden = hidden;
    return TORIRS_CONTRACT_OK;
}

static enum ToriRS_ContractResult
fake_set_image(void* context, struct ToriRS_WidgetRef ref, struct ToriRS_ImageRef image,
               int width, int height)
{
    struct TestbedControl* control = testbed_control_by_ref(ref);
    struct TestbedElement* element = testbed_element_by_ref(ref);

    (void)context;
    testbed_log("set_image %s #%d %dx%d", control ? control->key : (element ? element->role : "?"),
                image.value, width, height);
    /*
     * A ZERO HANDLE IS REFUSED, exactly as the engine refuses it.
     *
     * widget_set_image resolves the image token against the plugin's live
     * resources BEFORE it looks at the size, and a zero token resolves to no
     * slot: INVALID_ARGUMENT, nothing written, picture AND box both lost. The
     * fake used to answer OK and write the box, so a control with no picture
     * looked sized here and was 0x0 in the client -- which is how every
     * blocker the layer ever described shipped invisible and unclickable.
     */
    if( !image.value )
        return TORIRS_CONTRACT_INVALID_ARGUMENT;
    /* A reference to nothing at all: the engine's answer for a node that has
     * been destroyed, from EVERY entry point. @see Testbed_KillControl. */
    if( !control && !element )
        return TORIRS_CONTRACT_STALE_REFERENCE;
    if( control )
    {
        control->width = width;
        control->height = height;
        control->image = image;
    }
    return TORIRS_CONTRACT_OK;
}

static enum ToriRS_ContractResult
fake_set_mask(void* context, struct ToriRS_WidgetRef ref, struct ToriRS_ImageRef image)
{
    struct TestbedElement* element = testbed_element_by_ref(ref);

    (void)context;
    testbed_log("set_mask %s #%d", element ? element->role : "?", image.value);
    return TORIRS_CONTRACT_OK;
}

static enum ToriRS_ContractResult
fake_set_opacity(void* context, struct ToriRS_WidgetRef ref, int opacity)
{
    struct TestbedControl* control = testbed_control_by_ref(ref);
    struct TestbedElement* element = testbed_element_by_ref(ref);

    (void)context;
    testbed_log("set_opacity %s %d", control ? control->key : (element ? element->role : "?"),
                opacity);
    /* A reference to nothing at all: the engine's answer for a node that has
     * been destroyed, from EVERY entry point. @see Testbed_KillControl. */
    if( !control && !element )
        return TORIRS_CONTRACT_STALE_REFERENCE;
    if( control )
        control->opacity = opacity;
    return TORIRS_CONTRACT_OK;
}

static enum ToriRS_ContractResult
fake_set_anchor(void* context, struct ToriRS_WidgetRef ref, struct ToriRS_WidgetRef target,
                enum ToriRS_WidgetRelation relation)
{
    struct TestbedControl* control = testbed_control_by_ref(ref);
    struct TestbedElement const* element = testbed_element_by_ref(target);
    struct TestbedElement* subject = testbed_element_by_ref(ref);
    struct TestbedControl const* target_control = testbed_control_by_ref(target);

    (void)context;
    testbed_log("set_anchor %s -> %s rel=%d",
                control          ? control->key
                : subject        ? subject->role
                                 : "?",
                element                ? element->role
                : target_control       ? target_control->key
                                       : "?",
                (int)relation);
    if( g_testbed.refuse_anchors )
        return TORIRS_CONTRACT_UNAVAILABLE;
    if( control )
    {
        control->anchor = target;
        control->relation = relation;
    }
    /* The subject of a `raise` is a native element. @see TestbedElement. */
    if( subject )
    {
        subject->anchor = target;
        subject->anchor_relation = relation;
    }
    return TORIRS_CONTRACT_OK;
}

static enum ToriRS_ContractResult
fake_set_on_op(void* context, struct ToriRS_WidgetRef ref, char const* label,
               ToriRS_WidgetListener listener, void* user)
{
    struct TestbedControl* control = testbed_control_by_ref(ref);
    struct TestbedElement const* element = testbed_element_by_ref(ref);

    (void)context;
    testbed_log("set_on_op %s %s", control ? control->key : "?", label ? label : "(none)");
    /* A reference to nothing at all: the engine's answer for a node that has
     * been destroyed, from EVERY entry point. @see Testbed_KillControl. */
    if( !control && !element )
        return TORIRS_CONTRACT_STALE_REFERENCE;
    if( control )
    {
        control->armed = label != NULL;
        snprintf(control->label, sizeof(control->label), "%s", label ? label : "");
        control->op = listener;
        control->op_user = user;
    }
    return TORIRS_CONTRACT_OK;
}

static enum ToriRS_ContractResult
fake_set_text(void* context, struct ToriRS_WidgetRef ref, char const* text)
{
    struct TestbedControl* control = testbed_control_by_ref(ref);
    struct TestbedElement const* element = testbed_element_by_ref(ref);

    (void)context;
    testbed_log("set_text %s %s", control ? control->key : "?", text ? text : "");
    /* A reference to nothing at all: the engine's answer for a node that has
     * been destroyed, from EVERY entry point. @see Testbed_KillControl. */
    if( !control && !element )
        return TORIRS_CONTRACT_STALE_REFERENCE;
    if( control )
        snprintf(control->text, sizeof(control->text), "%s", text ? text : "");
    return TORIRS_CONTRACT_OK;
}

static enum ToriRS_ContractResult
fake_set_text_color(void* context, struct ToriRS_WidgetRef ref, uint32_t rgb)
{
    struct TestbedControl* control = testbed_control_by_ref(ref);
    struct TestbedElement const* element = testbed_element_by_ref(ref);

    (void)context;
    testbed_log("set_text_color %s %06x", control ? control->key : "?", rgb);
    /* A reference to nothing at all: the engine's answer for a node that has
     * been destroyed, from EVERY entry point. @see Testbed_KillControl. */
    if( !control && !element )
        return TORIRS_CONTRACT_STALE_REFERENCE;
    return TORIRS_CONTRACT_OK;
}

static enum ToriRS_ContractResult
fake_set_text_align(void* context, struct ToriRS_WidgetRef ref, int horizontal, int vertical)
{
    struct TestbedControl* control = testbed_control_by_ref(ref);
    struct TestbedElement const* element = testbed_element_by_ref(ref);

    (void)context;
    testbed_log("set_text_align %s %d,%d", control ? control->key : "?", horizontal, vertical);
    /* A reference to nothing at all: the engine's answer for a node that has
     * been destroyed, from EVERY entry point. @see Testbed_KillControl. */
    if( !control && !element )
        return TORIRS_CONTRACT_STALE_REFERENCE;
    return TORIRS_CONTRACT_OK;
}

static enum ToriRS_ContractResult
fake_set_text_outline(void* context, struct ToriRS_WidgetRef ref, bool outline)
{
    struct TestbedControl* control = testbed_control_by_ref(ref);
    struct TestbedElement const* element = testbed_element_by_ref(ref);

    (void)context;
    testbed_log("set_text_outline %s %d", control ? control->key : "?", outline ? 1 : 0);
    /* A reference to nothing at all: the engine's answer for a node that has
     * been destroyed, from EVERY entry point. @see Testbed_KillControl. */
    if( !control && !element )
        return TORIRS_CONTRACT_STALE_REFERENCE;
    return TORIRS_CONTRACT_OK;
}

/**
 * The engine's cheapest read, and the one the layer asks a ref's LIVENESS
 * with: a freed node answers STALE_REFERENCE from every entry point.
 *
 * A control this fake has forgotten is therefore stale, not merely invisible,
 * which is exactly what Testbed_DestroyControl leaves behind.
 */
static enum ToriRS_ContractResult
fake_widget_visible(void* context, struct ToriRS_WidgetRef ref, bool* out)
{
    struct TestbedControl const* control = testbed_control_by_ref(ref);
    struct TestbedElement const* element = testbed_element_by_ref(ref);

    (void)context;
    if( control )
    {
        if( out )
            *out = !control->hidden;
        return TORIRS_CONTRACT_OK;
    }
    if( element )
    {
        if( out )
            *out = element->presented;
        return TORIRS_CONTRACT_OK;
    }
    return TORIRS_CONTRACT_STALE_REFERENCE;
}

static enum ToriRS_ContractResult
fake_remove(void* context, struct ToriRS_WidgetRef ref)
{
    struct TestbedControl* control = testbed_control_by_ref(ref);

    (void)context;
    testbed_log("remove %s", control ? control->key : "?");
    if( control )
        control->live = false;
    return TORIRS_CONTRACT_OK;
}

static enum ToriRS_ContractResult
fake_reset(void* context, struct ToriRS_WidgetRef ref)
{
    struct TestbedElement const* element = testbed_element_by_ref(ref);

    (void)context;
    testbed_log("reset %s", element ? element->role : "?");
    return TORIRS_CONTRACT_OK;
}

static enum ToriRS_ContractResult
fake_revalidate(void* context, struct ToriRS_WidgetRef ref)
{
    (void)context;
    (void)ref;
    testbed_log("revalidate");
    return TORIRS_CONTRACT_OK;
}

/* ------------------------------------------------------------------------ */
/* The other namespaces                                                     */
/* ------------------------------------------------------------------------ */

static void
fake_log(struct ToriRS_Api* api, char const* format, ...)
{
    (void)api;
    (void)format;
}

static int
fake_screen(struct ToriRS_Api* api)
{
    (void)api;
    return g_testbed.screen;
}

static uint64_t
fake_frame_ms(struct ToriRS_Api* api)
{
    (void)api;
    return g_testbed.frame_ms;
}

static bool
testbed_list_has(char const* list, char const* name)
{
    char const* cursor = list;
    size_t const length = strlen(name);

    while( cursor && *cursor )
    {
        char const* comma = strchr(cursor, ',');
        size_t const span = comma ? (size_t)(comma - cursor) : strlen(cursor);
        if( span == length && strncmp(cursor, name, span) == 0 )
            return true;
        cursor = comma ? comma + 1 : NULL;
    }
    return false;
}

static bool
fake_capability(struct ToriRS_Api* api, char const* name)
{
    (void)api;
    testbed_log("capability %s", name);
    if( strcmp(name, "touch") == 0 )
        return g_testbed.touch;
    return testbed_list_has(g_testbed.capabilities, name);
}

static char const*
fake_plugin_id(struct ToriRS_Api* api)
{
    (void)api;
    return "testbed";
}

static bool
fake_config_get_int(struct ToriRS_Api* api, char const* key, int* out)
{
    struct TestbedConfigRow const* row;

    (void)api;
    testbed_log("config_get_int %s", key);
    row = testbed_config(key, false);
    if( !row || !row->has_number )
        return false;
    *out = row->number;
    return true;
}

static bool
fake_config_get_string(struct ToriRS_Api* api, char const* key, char const** out)
{
    struct TestbedConfigRow const* row;

    (void)api;
    testbed_log("config_get_string %s", key);
    row = testbed_config(key, false);
    if( !row || !row->value[0] )
        return false;
    *out = row->value;
    return true;
}

static enum ToriRS_Result
fake_config_set(struct ToriRS_Api* api, char const* key, char const* value)
{
    struct TestbedConfigRow* row;

    (void)api;
    testbed_log("config_set %s=%s", key, value);
    /* The real host truncates over the ceiling and the validator accepts the
     * fragment. Refuse here instead, so a test that proves Porcelain measured
     * first cannot pass because the fake was kinder than the host. */
    if( strlen(value) + 1 > PORCELAIN_CONFIG_VALUE_MAX )
        return TORIRS_RESULT_INVALID;
    row = testbed_config(key, true);
    snprintf(row->value, sizeof(row->value), "%s", value);
    return TORIRS_RESULT_OK;
}

static bool
fake_config_has(struct ToriRS_Api* api, char const* key)
{
    (void)api;
    return testbed_config(key, false) != NULL;
}

static enum ToriRS_AssetState
fake_asset_image(struct ToriRS_Api* api, char const* name, struct ToriRS_ImageRef* out)
{
    struct TestbedAsset const* asset;

    (void)api;
    testbed_log("assets_image %s", name);
    asset = testbed_asset(name);
    memset(out, 0, sizeof(*out));
    if( !asset )
        return TORIRS_ASSET_MISSING;
    if( asset->state == TORIRS_ASSET_READY )
        out->value = asset->value;
    return asset->state;
}

static enum ToriRS_AssetState
fake_asset_model(struct ToriRS_Api* api, char const* name, struct ToriRS_ModelRef* out)
{
    struct TestbedAsset const* asset;

    (void)api;
    testbed_log("assets_model %s", name);
    asset = testbed_asset(name);
    memset(out, 0, sizeof(*out));
    if( !asset )
        return TORIRS_ASSET_MISSING;
    if( asset->state == TORIRS_ASSET_READY )
        out->value = asset->value;
    return asset->state;
}

/* A picture's own size, and only once it is READY -- the real host cannot
 * answer for an image it has not decoded. */
static bool
fake_image_size(struct ToriRS_Api* api, struct ToriRS_ImageRef image, int* out_width,
                int* out_height)
{
    (void)api;
    testbed_log("image_size #%d", image.value);
    for( int i = 0; i < TESTBED_ASSETS_MAX; i++ )
    {
        struct TestbedAsset const* asset = &g_testbed.assets[i];
        if( !asset->used || asset->state != TORIRS_ASSET_READY || asset->value != image.value )
            continue;
        if( asset->width <= 0 || asset->height <= 0 )
            return false;
        *out_width = asset->width;
        *out_height = asset->height;
        return true;
    }
    return false;
}

static void
fake_image_release(struct ToriRS_Api* api, struct ToriRS_ImageRef image)
{
    (void)api;
    testbed_log("image_release #%d", image.value);
}

static void
fake_model_release(struct ToriRS_Api* api, struct ToriRS_ModelRef model)
{
    (void)api;
    testbed_log("model_release #%d", model.value);
}

static enum ToriRS_AssetState
fake_image_compose(struct ToriRS_Api* api, char const* name, int width, int height,
                   uint32_t const* argb, struct ToriRS_ImageRef* out)
{
    (void)api;
    (void)argb;
    testbed_log("image_compose %s %dx%d", name, width, height);
    out->value = 900 + (int)strlen(name);
    return TORIRS_ASSET_READY;
}

static bool
fake_named_id(struct ToriRS_Api* api, char const* kind, char const* name, int* out)
{
    char needle[128];
    char const* found;

    (void)api;
    testbed_log("named_id %s:%s", kind, name);
    snprintf(needle, sizeof(needle), "%s:%s=", kind, name);
    found = strstr(g_testbed.named_ids, needle);
    if( !found )
        return false;
    *out = atoi(found + strlen(needle));
    return true;
}

/*
 * The size the LANE authored, and the box the lane authored for one numbered
 * member of it. Undeclared is a refusal and not a zero: a surface whose box
 * is a proportion of its parent has no pixel count to report, and handing
 * back 0x0 would read as a surface that is there and empty.
 */
static struct TestbedSurface const*
testbed_surface(int surface, int member)
{
    for( int i = 0; i < TESTBED_SURFACES_MAX; i++ )
        if( g_testbed.surfaces[i].used && g_testbed.surfaces[i].surface == surface &&
            g_testbed.surfaces[i].member == member )
            return &g_testbed.surfaces[i];
    return NULL;
}

static bool
fake_surface_native_size(struct ToriRS_Api* api, int surface, int* out_w, int* out_h)
{
    struct TestbedSurface const* row = testbed_surface(surface, -1);

    (void)api;
    testbed_log("surface_native_size %d", surface);
    if( !row )
        return false;
    *out_w = row->width;
    *out_h = row->height;
    return true;
}

static bool
fake_surface_member_native_box(struct ToriRS_Api* api, int surface, int member, int* out_x,
                               int* out_y, int* out_w, int* out_h)
{
    struct TestbedSurface const* row = testbed_surface(surface, member);

    (void)api;
    testbed_log("surface_member_native_box %d %d", surface, member);
    if( !row )
        return false;
    *out_x = row->x;
    *out_y = row->y;
    *out_w = row->width;
    *out_h = row->height;
    return true;
}

static int
fake_frame_root(struct ToriRS_Api* api)
{
    (void)api;
    testbed_log("frame_root");
    return g_testbed.frame_root;
}

void
Testbed_DeclareSurface(int surface, int member, int x, int y, int width, int height)
{
    for( int i = 0; i < TESTBED_SURFACES_MAX; i++ )
    {
        if( g_testbed.surfaces[i].used )
            continue;
        g_testbed.surfaces[i].used = true;
        g_testbed.surfaces[i].surface = surface;
        g_testbed.surfaces[i].member = member;
        g_testbed.surfaces[i].x = x;
        g_testbed.surfaces[i].y = y;
        g_testbed.surfaces[i].width = width;
        g_testbed.surfaces[i].height = height;
        return;
    }
    assert(0 && "testbed surface table full");
}

void
Testbed_SetFrameRoot(int root)
{
    g_testbed.frame_root = root;
}

static int
fake_varbit(struct ToriRS_Api* api, int id)
{
    (void)api;
    testbed_log("varbit %d", id);
    return id;
}

/* A varp reads back as its id NEGATED, so a test that asked for the wrong kind
 * gets a different number rather than a plausible one: `varbit:x` and `varp:x`
 * answering alike is exactly how a kind mix-up survives a suite. */
static int
fake_varp(struct ToriRS_Api* api, int id)
{
    (void)api;
    testbed_log("varp %d", id);
    return -id;
}

static bool
fake_key_held(struct ToriRS_Api* api, int key)
{
    (void)api;
    testbed_log("key_held %d", key);
    return g_testbed.key_held == key;
}

static bool
fake_local_player(struct ToriRS_Api* api, struct ToriRS_PlayerSnapshot* out)
{
    (void)api;
    (void)out;
    testbed_log("local_player");
    return g_testbed.player_present;
}

static bool
fake_skill(struct ToriRS_Api* api, int index, struct ToriRS_SkillSnapshot* out)
{
    (void)api;
    testbed_log("skill %d", index);
    out->index = index;
    out->stated = g_testbed.skill_stated;
    return true;
}

/* ------------------------------------------------------------------------ */
/* The overlay surfaces                                                     */
/* ------------------------------------------------------------------------ */

/* A role spread over members answers them in the role's own numbering:
 * `<role>#<n>` here, so a test can declare a hole. */
static enum ToriRS_ContractResult
fake_find_all(void* context, char const* role, struct ToriRS_WidgetRef* refs, size_t capacity,
              size_t* count)
{
    char member[96];

    (void)context;
    testbed_log("find_all %s", role);
    *count = 0;
    for( int i = 0; i < 16; i++ )
    {
        struct TestbedElement const* element;
        snprintf(member, sizeof(member), "%s#%d", role, i);
        element = Testbed_Element(member);
        if( !element || !element->bound )
            continue;
        if( (size_t)i < capacity )
            refs[i] = element->ref;
        *count = (size_t)i + 1;
    }
    if( *count )
        return *count > capacity ? TORIRS_CONTRACT_BUDGET_EXCEEDED : TORIRS_CONTRACT_OK;
    {
        struct TestbedElement const* element = Testbed_Element(role);
        if( !element || !element->bound )
            return TORIRS_CONTRACT_UNAVAILABLE;
        *count = 1;
        if( capacity == 0 )
            return TORIRS_CONTRACT_BUDGET_EXCEEDED;
        refs[0] = element->ref;
        return TORIRS_CONTRACT_OK;
    }
}

/* A component id is modelled as the element declared under the role
 * "component:<id>", which is what makes a hover's container walkable. */
static enum ToriRS_ContractResult
fake_get_widget(void* context, int32_t component_id, struct ToriRS_WidgetRef* out)
{
    char role[64];
    struct TestbedElement const* element;

    (void)context;
    testbed_log("get_widget %d", component_id);
    snprintf(role, sizeof(role), "component:%d", component_id);
    element = Testbed_Element(role);
    if( !element || !element->bound )
        return TORIRS_CONTRACT_UNAVAILABLE;
    *out = element->ref;
    return TORIRS_CONTRACT_OK;
}

static bool
fake_menu_add(struct ToriRS_Api* api, struct ToriRS_MenuBuildEvent* menu, char const* text,
              uint32_t action_id)
{
    (void)api;
    (void)menu;
    testbed_log("menu_add %s %u", text, action_id);
    if( g_testbed.menu_routes_left <= 0 )
        return false;
    g_testbed.menu_routes_left--;
    return true;
}

static void
fake_notify(struct ToriRS_Api* api, char const* text)
{
    (void)api;
    testbed_log("notify %s", text);
    if( g_testbed.notify_count >= (int)(sizeof(g_testbed.notified) / sizeof(g_testbed.notified[0])) )
        return;
    snprintf(g_testbed.notified[g_testbed.notify_count], sizeof(g_testbed.notified[0]), "%s", text);
    g_testbed.notify_count++;
}

static enum ToriRS_AssetState
fake_asset_request(struct ToriRS_Api* api, char const* name)
{
    struct TestbedAsset* asset = testbed_asset(name);

    (void)api;
    testbed_log("assets_request %s", name);
    if( !asset )
        return TORIRS_ASSET_MISSING;
    if( asset->state == TORIRS_ASSET_READY )
        asset->held = true;
    return asset->state;
}

static bool
fake_asset_bytes(struct ToriRS_Api* api, char const* name, void const** data, size_t* size)
{
    struct TestbedAsset const* asset = testbed_asset(name);

    (void)api;
    testbed_log("assets_bytes %s", name);
    if( !asset || !asset->has_body )
        return false;
    *data = asset->body;
    *size = strlen(asset->body);
    return true;
}

static void
fake_asset_release(struct ToriRS_Api* api, char const* name)
{
    struct TestbedAsset* asset = testbed_asset(name);

    (void)api;
    testbed_log("assets_release %s", name);
    if( asset )
        asset->held = false;
}

static enum ToriRS_ContractResult
fake_script_invalidate(void* context, char const* callback)
{
    (void)context;
    testbed_log("script_invalidate %s", callback);
    return TORIRS_CONTRACT_OK;
}

/* The graphics builder a draw pass hands a callback. Only `context` is
 * modelled: Porcelain draws nothing itself. */
static struct ToriRS_Graphics g_testbed_graphics;

static bool
fake_graphics_context(struct ToriRS_Graphics* draw, struct ToriRS_DrawContext* out)
{
    (void)draw;
    testbed_log("draw_context");
    if( !g_testbed.draw_region_valid )
        return false;
    out->bounds = (struct ToriRS_Rect){0, 0, g_testbed.draw_region.width,
                                       g_testbed.draw_region.height};
    out->clip = out->bounds;
    return true;
}

/*
 * The world hull, with BOTH refusals the host has.
 *
 * The budget is the plugin's per-frame allotment and is checked first,
 * because the host checks it first: a claim it never got as far as testing is
 * not the reason the outline is missing.
 */
static enum ToriRS_Result
fake_graphics_world_hull(struct ToriRS_Graphics* draw, int element_id, uint32_t rgb, int alpha,
                         int shape)
{
    (void)draw;
    (void)rgb;
    (void)alpha;
    testbed_log("world_hull %d shape=%d", element_id, shape);
    if( shape != TORIRS_HULL_BOUNDS && shape != TORIRS_HULL_MESH )
        return TORIRS_RESULT_INVALID;
    if( g_testbed.hull_used >= g_testbed.hull_budget )
        return TORIRS_RESULT_BUDGET;
    if( element_id == g_testbed.hull_claimed_element )
        return TORIRS_RESULT_CONFLICT;
    g_testbed.hull_used++;
    return TORIRS_RESULT_OK;
}

/* The world tile, with the host's one refusal: the frame's own allotment. */
static enum ToriRS_Result
fake_graphics_world_tile(struct ToriRS_Graphics* draw, int tile_x, int tile_z, int level,
                         uint32_t fill_rgb, uint32_t outline_rgb, int outline_width, int alpha)
{
    (void)draw;
    (void)fill_rgb;
    (void)outline_rgb;
    (void)alpha;
    /* The thickness is in the line, because "a wash with no border" and "a
     * wash with a two-pixel border" are different pictures and the log is how
     * this testbed tells two draws apart. */
    testbed_log("world_tile %d,%d,%d w%d", tile_x, tile_z, level, outline_width);
    if( g_testbed.tile_used >= g_testbed.tile_budget )
        return TORIRS_RESULT_BUDGET;
    g_testbed.tile_used++;
    return TORIRS_RESULT_OK;
}

struct ToriRS_Graphics*
Testbed_Graphics(struct ToriRS_Rect region, bool valid)
{
    memset(&g_testbed_graphics, 0, sizeof(g_testbed_graphics));
    g_testbed_graphics.struct_size = sizeof(g_testbed_graphics);
    g_testbed_graphics.context = fake_graphics_context;
    g_testbed_graphics.world_hull = fake_graphics_world_hull;
    g_testbed_graphics.world_tile = fake_graphics_world_tile;
    g_testbed.draw_region = region;
    g_testbed.draw_region_valid = valid;
    return &g_testbed_graphics;
}

/* ------------------------------------------------------------------------ */
/* Assembly                                                                 */

/* ------------------------------------------------------------------------ */
/* The panel: a fake page model with the host's own identity rules          */
/* ------------------------------------------------------------------------ */

struct TestbedPanelRow*
Testbed_PanelRow(char const* id)
{
    assert(id);
    for( int i = 0; i < g_testbed.panel_row_count; i++ )
        if( g_testbed.panel_rows[i].used && strcmp(g_testbed.panel_rows[i].id, id) == 0 )
            return &g_testbed.panel_rows[i];
    return NULL;
}

struct TestbedPanelRow*
Testbed_PanelRowAt(int index)
{
    if( index < 0 || index >= g_testbed.panel_row_count )
        return NULL;
    return &g_testbed.panel_rows[index];
}

int
Testbed_PanelRowCount(void)
{
    return g_testbed.panel_row_count;
}

/** The setter fence the host applies: a row that is not declared takes none. */
static struct TestbedPanelRow*
testbed_panel_mutable(char const* id)
{
    struct TestbedPanelRow* row = Testbed_PanelRow(id);
    if( !row )
        g_testbed.panel_orphan_setters++;
    return row;
}

static enum ToriRS_Result
fake_panel_request(struct ToriRS_Api* api, struct ToriRS_PanelDescriptor const* description)
{
    (void)api;
    assert(description);
    testbed_log("panel_request %s %d", description->icon_asset ? description->icon_asset : "-",
                description->preferred_width);
    g_testbed.panel_requests++;
    g_testbed.panel_width = description->preferred_width;
    snprintf(g_testbed.panel_icon, sizeof(g_testbed.panel_icon), "%s",
             description->icon_asset ? description->icon_asset : "");
    return TORIRS_RESULT_OK;
}

static void
fake_panel_invalidate(struct ToriRS_Api* api)
{
    (void)api;
    testbed_log("panel_invalidate");
    g_testbed.panel_invalidates++;
    /* Exactly what the buffer executor does: the whole page goes, the scroll
     * with it, and every retained custom run is retired. */
    memset(g_testbed.panel_rows, 0, sizeof(g_testbed.panel_rows));
    g_testbed.panel_row_count = 0;
}

static void
fake_panel_attention(struct ToriRS_Api* api, bool wanted)
{
    (void)api;
    testbed_log("panel_attention %d", wanted ? 1 : 0);
}

static enum ToriRS_Result
fake_panel_set_text(struct ToriRS_Api* api, char const* id, char const* text)
{
    struct TestbedPanelRow* row;
    (void)api;
    assert(id);
    testbed_log("panel_set_text %s %s", id, text ? text : "");
    row = testbed_panel_mutable(id);
    if( !row )
        return TORIRS_RESULT_NOT_FOUND;
    snprintf(row->text, sizeof(row->text), "%s", text ? text : "");
    return TORIRS_RESULT_OK;
}

static enum ToriRS_Result
fake_panel_set_label(struct ToriRS_Api* api, char const* id, char const* label)
{
    struct TestbedPanelRow* row;
    (void)api;
    assert(id);
    testbed_log("panel_set_label %s %s", id, label ? label : "");
    row = testbed_panel_mutable(id);
    if( !row )
        return TORIRS_RESULT_NOT_FOUND;
    snprintf(row->label, sizeof(row->label), "%s", label ? label : "");
    return TORIRS_RESULT_OK;
}

static int
fake_panel_scroll(struct ToriRS_Api* api)
{
    (void)api;
    /* -1 is "no page", which is NOT 0 -- the top of one there is. */
    return g_testbed.panel_no_page ? -1 : g_testbed.panel_scroll;
}

static enum ToriRS_Result
fake_panel_scroll_to(struct ToriRS_Api* api, int scroll)
{
    (void)api;
    testbed_log("panel_scroll_to %d", scroll);
    if( g_testbed.panel_no_page )
        return TORIRS_RESULT_NOT_FOUND;
    g_testbed.panel_scroll = scroll < 0 ? 0 : scroll;
    return TORIRS_RESULT_OK;
}

static enum ToriRS_Result
fake_panel_set_value(struct ToriRS_Api* api, char const* id, int value)
{
    struct TestbedPanelRow* row;
    (void)api;
    assert(id);
    testbed_log("panel_set_value %s %d", id, value);
    row = testbed_panel_mutable(id);
    if( !row )
        return TORIRS_RESULT_NOT_FOUND;
    row->value = value;
    return TORIRS_RESULT_OK;
}

static enum ToriRS_Result
fake_panel_set_height(struct ToriRS_Api* api, char const* id, int preferred_height)
{
    struct TestbedPanelRow* row;
    (void)api;
    assert(id);
    testbed_log("panel_set_height %s %d", id, preferred_height);
    row = testbed_panel_mutable(id);
    if( !row )
        return TORIRS_RESULT_NOT_FOUND;
    row->height = preferred_height;
    return TORIRS_RESULT_OK;
}

static void
testbed_panel_store_options(struct TestbedPanelRow* row, char const* value,
                            struct ToriRS_SelectOption const* options, int option_count)
{
    row->option_count = option_count > 16 ? 16 : option_count;
    for( int i = 0; i < row->option_count; i++ )
    {
        snprintf(row->option_value[i], sizeof(row->option_value[i]), "%s", options[i].value);
        snprintf(row->option_label[i], sizeof(row->option_label[i]), "%s", options[i].label);
        row->option_enabled[i] = options[i].enabled;
    }
    snprintf(row->selected, sizeof(row->selected), "%s", value ? value : "");
}

static enum ToriRS_Result
fake_panel_set_options(struct ToriRS_Api* api, char const* id, char const* value,
                       struct ToriRS_SelectOption const* options, int option_count)
{
    struct TestbedPanelRow* row;
    (void)api;
    assert(id);
    testbed_log("panel_set_options %s %s %d", id, value ? value : "", option_count);
    row = testbed_panel_mutable(id);
    if( !row )
        return TORIRS_RESULT_NOT_FOUND;
    if( g_testbed.panel_refuse_options )
        return TORIRS_RESULT_INVALID;
    testbed_panel_store_options(row, value, options, option_count);
    return TORIRS_RESULT_OK;
}

static void
fake_panel_redraw(struct ToriRS_Api* api, char const* id)
{
    (void)api;
    testbed_log("panel_redraw %s", id ? id : "");
}

static enum ToriRS_Result
fake_panel_reidentify(struct ToriRS_Api* api, char const* id)
{
    struct TestbedPanelRow* row;
    (void)api;
    assert(id);
    testbed_log("panel_reidentify %s", id);
    row = testbed_panel_mutable(id);
    if( !row )
        return TORIRS_RESULT_NOT_FOUND;
    /* A fresh serial and nothing else: the node stays between the same
     * neighbours and no other row is touched. */
    row->serial = ++g_testbed.panel_next_serial;
    return TORIRS_RESULT_OK;
}

/* The builder ------------------------------------------------------------ */

static enum ToriRS_Result
fake_builder_node(struct ToriRS_PanelBuilder* builder, struct ToriRS_PanelNode const* node)
{
    struct TestbedPanelRow* row;

    (void)builder;
    assert(node);
    if( node->struct_size < TORIRS_PANEL_NODE_REQUIRED_SIZE || !node->id || !node->id[0] )
        return TORIRS_RESULT_INVALID;
    if( g_testbed.panel_row_count >=
        (int)(sizeof(g_testbed.panel_rows) / sizeof(g_testbed.panel_rows[0])) )
        return TORIRS_RESULT_BUDGET;
    testbed_log("panel_node %d %s", node->kind, node->id);
    /* The host's declaration is idempotent on a repeated id. */
    row = Testbed_PanelRow(node->id);
    if( !row )
    {
        row = &g_testbed.panel_rows[g_testbed.panel_row_count++];
        memset(row, 0, sizeof(*row));
        row->used = true;
        snprintf(row->id, sizeof(row->id), "%s", node->id);
        row->serial = ++g_testbed.panel_next_serial;
    }
    row->declared++;
    row->kind = node->kind;
    snprintf(row->label, sizeof(row->label), "%s", node->label ? node->label : "");
    if( node->text )
        snprintf(row->text, sizeof(row->text), "%s", node->text);
    row->value = node->value;
    row->height = node->preferred_height;
    if( node->kind == TORIRS_PANEL_SELECT )
    {
        if( !node->text )
            return TORIRS_RESULT_INVALID;
        testbed_panel_store_options(row, node->text, node->options, node->option_count);
    }
    return TORIRS_RESULT_OK;
}

void
Testbed_PanelBuild(struct Porcelain* porcelain, int view)
{
    struct ToriRS_PanelBuilder builder;

    assert(porcelain);
    /* A build IS the page having been cleared: the host frees the widget list
     * before it asks for a declaration. */
    memset(g_testbed.panel_rows, 0, sizeof(g_testbed.panel_rows));
    g_testbed.panel_row_count = 0;
    g_testbed.panel_view = view;
    memset(&builder, 0, sizeof(builder));
    builder.struct_size = sizeof(builder);
    builder.node = fake_builder_node;
    Porcelain_PanelBuild(porcelain, &builder, view);
}

bool
Testbed_PanelAction(struct Porcelain* porcelain, char const* id, int action, int value,
                    char const* text)
{
    struct ToriRS_PanelActionEvent event;
    struct TestbedPanelRow const* row;

    assert(porcelain);
    assert(id);
    memset(&event, 0, sizeof(event));
    event.id = id;
    event.action = action;
    event.value = value;
    event.text = text ? text : "";
    row = Testbed_PanelRow(id);
    event.widget_serial = row ? row->serial : 0;
    return Porcelain_PanelAction(porcelain, &event);
}

/* ------------------------------------------------------------------------ */

void
Testbed_Reset(void)
{
    memset(&g_testbed, 0, sizeof(g_testbed));
    g_testbed.screen = TORIRS_SCREEN_GAME;
    g_testbed.next_ref = 100;
    /* -1 is "no gameframe is open", which is what a root KEY means before
     * one is. A scenario that cares states its own. */
    g_testbed.frame_root = -1;

    g_testbed.api.struct_size = sizeof(g_testbed.api);
    g_testbed.api.major_version = TORIRS_PLUGIN_API_MAJOR;
    g_testbed.api.minor_version = TORIRS_PLUGIN_API_MINOR;
    g_testbed.api.instance = &g_testbed;

    /* The host's route table is shared and bounded; a test that wants the
     * refusal turns this down. */
    g_testbed.menu_routes_left = 24;

    /* The draw allotment, and nothing claimed. A test that wants either
     * refusal turns the budget down or names the claimed element. */
    g_testbed.hull_budget = 64;
    g_testbed.hull_claimed_element = -1;
    g_testbed.tile_budget = 64;

    g_testbed.api.widgets.context = &g_testbed;
    g_testbed.api.widgets.find = fake_find;
    g_testbed.api.widgets.find_all = fake_find_all;
    g_testbed.api.widgets.get_widget = fake_get_widget;
    g_testbed.api.widgets.watch_state = fake_watch_state;
    g_testbed.api.widgets.watch_tree = fake_watch_tree;
    g_testbed.api.widgets.state = fake_state;
    g_testbed.api.widgets.parent = fake_parent;
    g_testbed.api.widgets.bounds = fake_bounds;
    g_testbed.api.widgets.position = fake_bounds;
    g_testbed.api.widgets.create_image = fake_create_image;
    g_testbed.api.widgets.create_text = fake_create_text;
    g_testbed.api.widgets.set_position = fake_set_position;
    g_testbed.api.widgets.set_size = fake_set_size;
    g_testbed.api.widgets.set_hidden = fake_set_hidden;
    g_testbed.api.widgets.set_image = fake_set_image;
    g_testbed.api.widgets.set_mask = fake_set_mask;
    g_testbed.api.widgets.set_opacity = fake_set_opacity;
    g_testbed.api.widgets.set_anchor = fake_set_anchor;
    g_testbed.api.widgets.set_on_op = fake_set_on_op;
    g_testbed.api.widgets.set_text = fake_set_text;
    g_testbed.api.widgets.set_text_color = fake_set_text_color;
    g_testbed.api.widgets.set_text_align = fake_set_text_align;
    g_testbed.api.widgets.set_text_outline = fake_set_text_outline;
    g_testbed.api.widgets.visible = fake_widget_visible;
    g_testbed.api.widgets.remove = fake_remove;
    g_testbed.api.widgets.reset = fake_reset;
    g_testbed.api.widgets.revalidate = fake_revalidate;

    g_testbed.api.core.struct_size = sizeof(g_testbed.api.core);
    g_testbed.api.core.log = fake_log;
    g_testbed.api.core.screen = fake_screen;
    g_testbed.api.core.frame_ms = fake_frame_ms;
    g_testbed.api.core.capability = fake_capability;
    g_testbed.api.core.plugin_id = fake_plugin_id;

    g_testbed.api.config.struct_size = sizeof(g_testbed.api.config);
    g_testbed.api.config.has = fake_config_has;
    g_testbed.api.config.get_int = fake_config_get_int;
    g_testbed.api.config.get_string = fake_config_get_string;
    g_testbed.api.config.set = fake_config_set;

    g_testbed.api.core.notify = fake_notify;

    g_testbed.api.menu.struct_size = sizeof(g_testbed.api.menu);
    g_testbed.api.menu.add = fake_menu_add;

    g_testbed.api.scripts.context = &g_testbed;
    g_testbed.api.scripts.invalidate = fake_script_invalidate;

    g_testbed.api.assets.struct_size = sizeof(g_testbed.api.assets);
    g_testbed.api.assets.request = fake_asset_request;
    g_testbed.api.assets.bytes = fake_asset_bytes;
    g_testbed.api.assets.release = fake_asset_release;
    g_testbed.api.assets.image = fake_asset_image;
    g_testbed.api.assets.image_size = fake_image_size;
    g_testbed.api.assets.model = fake_asset_model;
    g_testbed.api.assets.image_release = fake_image_release;
    g_testbed.api.assets.model_release = fake_model_release;
    g_testbed.api.assets.image_compose = fake_image_compose;

    g_testbed.api.cache.struct_size = sizeof(g_testbed.api.cache);
    g_testbed.api.cache.named_id = fake_named_id;
    g_testbed.api.cache.varbit = fake_varbit;
    g_testbed.api.cache.frame_root = fake_frame_root;
    g_testbed.api.cache.varp = fake_varp;

    g_testbed.api.frame.struct_size = sizeof(g_testbed.api.frame);
    g_testbed.api.frame.surface_native_size = fake_surface_native_size;
    g_testbed.api.frame.surface_member_native_box = fake_surface_member_native_box;

    g_testbed.api.input.struct_size = sizeof(g_testbed.api.input);
    g_testbed.api.input.key_held = fake_key_held;

    g_testbed.api.world.struct_size = sizeof(g_testbed.api.world);
    g_testbed.api.world.local_player = fake_local_player;

    g_testbed.game.struct_size = sizeof(g_testbed.game);
    g_testbed.game.skill = fake_skill;
    g_testbed.api.game = &g_testbed.game;

    g_testbed.api.panel.struct_size = sizeof(g_testbed.api.panel);
    g_testbed.api.panel.request = fake_panel_request;
    g_testbed.api.panel.invalidate = fake_panel_invalidate;
    g_testbed.api.panel.attention = fake_panel_attention;
    g_testbed.api.panel.set_text = fake_panel_set_text;
    g_testbed.api.panel.set_label = fake_panel_set_label;
    g_testbed.api.panel.set_value = fake_panel_set_value;
    g_testbed.api.panel.set_height = fake_panel_set_height;
    g_testbed.api.panel.set_options = fake_panel_set_options;
    g_testbed.api.panel.redraw = fake_panel_redraw;
    g_testbed.api.panel.reidentify = fake_panel_reidentify;
    g_testbed.api.panel.scroll = fake_panel_scroll;
    g_testbed.api.panel.scroll_to = fake_panel_scroll_to;

    g_testbed.api.porcelain = ToriRS_PorcelainApiTable();

    Porcelain_ResetForTesting();
    Testbed_ClearLog();
}

struct ToriRS_Api*
Testbed_Api(void)
{
    return &g_testbed.api;
}
