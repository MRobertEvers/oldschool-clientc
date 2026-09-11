#include "uitree_builder_manifest.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define TEST_ASSERT(cond, msg)                                                                     \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                        \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static void
push_field(
    struct RevConfigBuffer* buf,
    enum RevConfigFieldKind kind,
    char const* value)
{
    TEST_ASSERT(revconfig_buffer_push_field(buf, kind, value) == 0, "push_field");
}

static void
test_manifest_from_hand_pushed_fields(void)
{
    struct RevConfigBuffer* fields = revconfig_buffer_new(64);
    struct RevConfigItemBuffer* items = revconfig_item_buffer_new(16);
    TEST_ASSERT(fields && items, "alloc");

    /* [sprite:invback] dat1-style */
    push_field(fields, RCFIELD_ITEMTYPE, "sprite");
    push_field(fields, RCFIELD_ITEMNAME, "invback");
    push_field(fields, RCFIELD_CACHE_FORMAT, "pix8");
    push_field(fields, RCFIELD_CACHE_DATA_FILENAME, "invback.dat");
    push_field(fields, RCFIELD_CACHE_INDEX_FILENAME, "index.dat");
    push_field(fields, RCFIELD_ITEMDONE, "");

    /* [sprite:cross] dat2-style */
    push_field(fields, RCFIELD_ITEMTYPE, "sprite");
    push_field(fields, RCFIELD_ITEMNAME, "cross");
    push_field(fields, RCFIELD_CACHE_ARCHIVE_ID, "297");
    push_field(fields, RCFIELD_CACHE_ATLAS_COUNT, "2");
    push_field(fields, RCFIELD_ITEMDONE, "");

    /* [font:b12] */
    push_field(fields, RCFIELD_ITEMTYPE, "font");
    push_field(fields, RCFIELD_ITEMNAME, "b12");
    push_field(fields, RCFIELD_CACHE_ARCHIVE_ID, "496");
    push_field(fields, RCFIELD_CACHE_FONT_ID, "2");
    push_field(fields, RCFIELD_ITEMDONE, "");

    /* [component:sidebar] needs RS load */
    push_field(fields, RCFIELD_ITEMTYPE, "component");
    push_field(fields, RCFIELD_ITEMNAME, "sidebar");
    push_field(fields, RCFIELD_UICOMPONENT_TYPE, "sidebar");
    push_field(fields, RCFIELD_UICOMPONENT_COMPONENTNO, "149");
    push_field(fields, RCFIELD_UICOMPONENT_INV, "inventory");
    push_field(fields, RCFIELD_ITEMDONE, "");

    /* [component:compass] builtin, no RS. Two hotkey= lines: the repeat is the
     * point — one component may advertise several effects. */
    push_field(fields, RCFIELD_ITEMTYPE, "component");
    push_field(fields, RCFIELD_ITEMNAME, "compass");
    push_field(fields, RCFIELD_UICOMPONENT_TYPE, "compass");
    push_field(fields, RCFIELD_UICOMPONENT_SPRITE, "cross");
    push_field(fields, RCFIELD_UICOMPONENT_WIDTH, "33");
    push_field(fields, RCFIELD_UICOMPONENT_HEIGHT, "33");
    push_field(fields, RCFIELD_UICOMPONENT_HOTKEY, "select_tab");
    push_field(fields, RCFIELD_UICOMPONENT_HOTKEY, "some_future_effect");
    push_field(fields, RCFIELD_ITEMDONE, "");

    /* [layout] entries */
    push_field(fields, RCFIELD_ITEMTYPE, "layout");
    push_field(fields, RCFIELD_UILAYOUT_GROUP, "fixed");
    push_field(fields, RCFIELD_UILAYOUT_NAME, "compass_slot");
    push_field(fields, RCFIELD_UILAYOUT_COMPONENT, "compass");
    push_field(fields, RCFIELD_UILAYOUT_X, "10");
    push_field(fields, RCFIELD_UILAYOUT_Y, "20");
    push_field(fields, RCFIELD_ITEMDONE, "");

    push_field(fields, RCFIELD_ITEMTYPE, "layout");
    push_field(fields, RCFIELD_UILAYOUT_GROUP, "fixed");
    push_field(fields, RCFIELD_UILAYOUT_NAME, "sidebar_slot");
    push_field(fields, RCFIELD_UILAYOUT_COMPONENT, "sidebar");
    push_field(fields, RCFIELD_UILAYOUT_PARENT, "compass_slot");
    push_field(fields, RCFIELD_ITEMDONE, "");

    /* [hotkey:f1] / [hotkey:3] — two keys onto the same component + effect,
     * plus one malformed section (no e=) that must be dropped here rather than
     * reaching the bake. */
    push_field(fields, RCFIELD_ITEMTYPE, "hotkey");
    push_field(fields, RCFIELD_ITEMNAME, "f1");
    push_field(fields, RCFIELD_HOTKEY_COMPONENT, "compass");
    push_field(fields, RCFIELD_HOTKEY_EFFECT, "select_tab");
    push_field(fields, RCFIELD_ITEMDONE, "");

    push_field(fields, RCFIELD_ITEMTYPE, "hotkey");
    push_field(fields, RCFIELD_ITEMNAME, "3");
    push_field(fields, RCFIELD_HOTKEY_COMPONENT, "compass");
    push_field(fields, RCFIELD_HOTKEY_EFFECT, "select_tab");
    push_field(fields, RCFIELD_ITEMDONE, "");

    push_field(fields, RCFIELD_ITEMTYPE, "hotkey");
    push_field(fields, RCFIELD_ITEMNAME, "f2");
    push_field(fields, RCFIELD_HOTKEY_COMPONENT, "compass");
    push_field(fields, RCFIELD_ITEMDONE, "");

    /* [inv:inventory] */
    push_field(fields, RCFIELD_ITEMTYPE, "inv");
    push_field(fields, RCFIELD_ITEMNAME, "inventory");
    push_field(fields, RCFIELD_INV_ITEM, "1333");
    push_field(fields, RCFIELD_INV_ITEM, "4151");
    push_field(fields, RCFIELD_ITEMDONE, "");

    revconfig_items_build(fields, items);

    struct UIBuilderManifest manifest;
    uibuilder_manifest_init(&manifest);
    TEST_ASSERT(uibuilder_manifest_from_revconfig(&manifest, items) == 0, "from_revconfig");

    TEST_ASSERT(manifest.sprite_count == 2, "sprite req count");
    TEST_ASSERT(manifest.font_count == 1, "font req count");
    TEST_ASSERT(manifest.component_count == 1, "component req count");
    TEST_ASSERT(manifest.inv_count == 1, "inv seed count");
    TEST_ASSERT(manifest.op_count == 2, "tree op count");

    TEST_ASSERT(strcmp(manifest.sprites[0].name, "invback") == 0, "sprite0 name");
    TEST_ASSERT(manifest.sprites[0].archive_id == -1, "dat1 sprite archive_id");
    TEST_ASSERT(manifest.sprites[1].archive_id == 297, "dat2 sprite archive_id");

    TEST_ASSERT(manifest.components[0].iface_id == 149, "packed iface");
    TEST_ASSERT(manifest.components[0].packed_id == (149 << 16), "packed id");

    TEST_ASSERT(manifest.ops[0].kind == UIBUILDER_OP_PUSH_BUILTIN, "compass builtin");
    TEST_ASSERT(strcmp(manifest.ops[0].type, "compass") == 0, "compass type");
    TEST_ASSERT(manifest.ops[1].kind == UIBUILDER_OP_PUSH_RS_SUBTREE, "sidebar rs");
    TEST_ASSERT(manifest.ops[1].componentno == 149, "sidebar componentno");

    /* The compass op carries the component name (the bake resolves bindings by
     * component, not by layout-entry name) and both advertised effects. */
    TEST_ASSERT(strcmp(manifest.ops[0].component_name, "compass") == 0, "op component name");
    TEST_ASSERT(manifest.ops[0].hotkey_count == 2, "advertised effect count");
    TEST_ASSERT(strcmp(manifest.ops[0].hotkeys[0], "select_tab") == 0, "advertised effect 0");

    /* Two well-formed bindings kept in order; the one missing e= is dropped. */
    TEST_ASSERT(manifest.hotkey_count == 2, "hotkey binding count");
    TEST_ASSERT(strcmp(manifest.hotkeys[0].key_name, "f1") == 0, "binding0 key");
    TEST_ASSERT(strcmp(manifest.hotkeys[0].component_name, "compass") == 0, "binding0 component");
    TEST_ASSERT(strcmp(manifest.hotkeys[0].effect, "select_tab") == 0, "binding0 effect");
    TEST_ASSERT(strcmp(manifest.hotkeys[1].key_name, "3") == 0, "binding1 key");

    TEST_ASSERT(manifest.invs[0].item_count == 2, "inv items");
    TEST_ASSERT(manifest.invs[0].obj_ids[0] == 1333, "inv obj0");
    TEST_ASSERT(manifest.invs[0].obj_counts[0] == 1, "inv count default");

    TEST_ASSERT(uibuilder_pack_component_id(149) == (149 << 16), "pack small");
    TEST_ASSERT(uibuilder_pack_component_id(10616833) == 10616833, "pack already");

    uibuilder_manifest_free(&manifest);
    revconfig_item_buffer_free(items);
    revconfig_buffer_free(fields);
}

/*
 * A boot manifest carrying its root layout inline, written the way one is
 * actually checked out: CRLF, and with the boot dialect's own sections around
 * the RevConfig ones.
 *
 * The CRLF is not decoration. Read in text mode on Windows the CRT eats the
 * '\r' of every line, so fread returns fewer bytes than the file is long — a
 * loader that reads the size with ftell and treats a short read as failure
 * loads nothing, and loads nothing *silently*, because an empty layout still
 * produces a tree. The two `[component:decoy]`/`[layout:root]` sections are the
 * other half: unprefixed sections belong to the boot dialect and must not be
 * read as RevConfig, or every manifest key spelled `left`, `top`, `index`,
 * `filename` or `format` would start meaning something to the wrong parser.
 */
static char const k_inline_manifest[] =
    "; boot manifest with the root layout inline\r\n"
    "[cache:boot]\r\n"
    "epoch=dat2\r\n"
    "dir=cache.osrs239\r\n"
    "\r\n"
    "[ui:boot]\r\n"
    "logic=cs2\r\n"
    "chrome=revconfig\r\n"
    "interface_id=161\r\n"
    "\r\n"
    "[component:decoy]\r\n"
    "type=debug_overlay\r\n"
    "\r\n"
    "[layout:root]\r\n"
    "c=decoy\r\n"
    "\r\n"
    "[revconfig:component:gameframe]\r\n"
    "type=rs_iface\r\n"
    "\r\n"
    "[revconfig:component:overlay]\r\n"
    "type=debug_overlay\r\n"
    "\r\n"
    "[revconfig:component:cross]\r\n"
    "type=cross\r\n"
    "\r\n"
    "[revconfig:component:minimenu]\r\n"
    "type=minimenu\r\n"
    "font=496\r\n"
    "\r\n"
    "[revconfig:layout:root]\r\n"
    "c=gameframe\r\n"
    "=\r\n"
    "c=overlay\r\n"
    "=\r\n"
    "c=cross\r\n"
    "=\r\n"
    "c=minimenu\r\n";

static char const k_inline_path[] = "uitree_builder_test_inline.tmp.ini";

/*
 * The two viewport widgets the server drives but no interface owns: the
 * multi-combat indicator (SET_MULTIWAY) and the system-update countdown
 * (UPDATE_REBOOT_TIMER). Every number either one draws with -- the headicons
 * frame, the two positions, the colour -- is stated in the shipped revconfig
 * and nowhere in C, so this reads THAT file rather than a fixture: a rename or
 * a dropped line there is the failure worth catching, and a fixture copy would
 * pass right through it.
 */
static void
test_viewport_widgets_from_shipped_revconfig(void)
{
    struct UIBuilderManifest manifest;
    struct UIBuilderManifestSources src = { 0 };
    struct UIBuilderTreeOp const* multiway = NULL;
    struct UIBuilderTreeOp const* reboot = NULL;

    src.ui_ini_path = "../revconfig/rs245_2lc/rs245_2lc_dat1_ui.ini";
    src.cache_ini_path = "../revconfig/rs245_2lc/rs245_2lc_dat1_cache.ini";
    src.root_interface_id = 161;

    uibuilder_manifest_init(&manifest);
    TEST_ASSERT(
        uibuilder_manifest_from_sources(&manifest, &src) == 0, "shipped revconfig parses");

    for( int i = 0; i < manifest.op_count; i++ )
    {
        if( strcmp(manifest.ops[i].type, "multiway") == 0 )
            multiway = &manifest.ops[i];
        else if( strcmp(manifest.ops[i].type, "reboot_timer") == 0 )
            reboot = &manifest.ops[i];
    }

    TEST_ASSERT(multiway != NULL, "revconfig declares the multiway indicator");
    if( multiway )
    {
        /* Frame 1 of the headicons pack, by name and bracket index -- the
         * builder resolves both halves, so a bare `headicons` here would
         * silently draw frame 0 (protect-from-melee). */
        TEST_ASSERT(
            strcmp(multiway->sprite_ref, "headicons[1]") == 0, "multiway sprite frame");
        /* Reference plots it at 472,296 inside the viewport, so it hangs off
         * the viewport node with those coordinates unchanged. */
        TEST_ASSERT(strcmp(multiway->parent_name, "world_viewport") == 0, "multiway parent");
        TEST_ASSERT(multiway->x == 472 && multiway->y == 296, "multiway position");
    }

    TEST_ASSERT(reboot != NULL, "revconfig declares the reboot countdown");
    if( reboot )
    {
        TEST_ASSERT(reboot->has_font_ref, "reboot countdown names a font");
        TEST_ASSERT(strcmp(reboot->font_ref, "p12") == 0, "reboot countdown font");
        /* Yellow. Stated decimal because that is what the field parses as; a
         * hex spelling would read back as 0 and draw the line in black. */
        TEST_ASSERT(reboot->color == 16776960, "reboot countdown colour");
        TEST_ASSERT(strcmp(reboot->parent_name, "world_viewport") == 0, "reboot parent");
        TEST_ASSERT(reboot->x == 4 && reboot->y == 329, "reboot countdown position");
    }

    uibuilder_manifest_free(&manifest);
}

static int
write_fixture(char const* path, char const* text)
{
    /* "wb", so the CRLFs above reach the file as written rather than being
     * translated twice. */
    FILE* f = fopen(path, "wb");
    if( !f )
        return 0;
    size_t len = strlen(text);
    size_t wrote = fwrite(text, 1, len, f);
    fclose(f);
    return wrote == len;
}

static void
test_manifest_from_inline_sources(void)
{
    struct UIBuilderManifest manifest;
    struct UIBuilderManifestSources src = { 0 };

    if( !write_fixture(k_inline_path, k_inline_manifest) )
    {
        TEST_ASSERT(0, "write inline fixture");
        return;
    }

    src.inline_ini_path = k_inline_path;
    src.root_interface_id = 161;

    uibuilder_manifest_init(&manifest);
    TEST_ASSERT(uibuilder_manifest_from_sources(&manifest, &src) == 0, "from_sources inline");

    /* Four ops, not five: the unprefixed `[layout:root] c=decoy` is the boot
     * dialect's and stays out of this. Not zero either — that is the CRLF read. */
    TEST_ASSERT(manifest.op_count == 4, "inline op count");
    if( manifest.op_count == 4 )
    {
        /* Declaration order is sibling order is paint order. The frame is
         * declared first and the overlay second, so the overlay paints over it —
         * and because the cache pack is baked as the rs_iface node's children,
         * nothing the CS2 scripts do to the frame can get between them. */
        TEST_ASSERT(
            strcmp(manifest.ops[0].component_name, "gameframe") == 0, "inline op0 component");
        TEST_ASSERT(manifest.ops[0].kind == UIBUILDER_OP_PUSH_RS_SUBTREE, "inline op0 kind");
        TEST_ASSERT(strcmp(manifest.ops[0].type, "rs_iface") == 0, "inline op0 type");
        /* No componentno= on the mount: it resolves to root_interface_id, which
         * is where the interface id lives once and only once. */
        TEST_ASSERT(manifest.ops[0].componentno == 161, "inline op0 root iface");
        TEST_ASSERT(strcmp(manifest.ops[2].type, "cross") == 0, "inline cross from revconfig");
        TEST_ASSERT(
            strcmp(manifest.ops[3].type, "minimenu") == 0,
            "inline minimenu from revconfig");

        TEST_ASSERT(
            strcmp(manifest.ops[1].component_name, "overlay") == 0, "inline op1 component");
        TEST_ASSERT(manifest.ops[1].kind == UIBUILDER_OP_PUSH_BUILTIN, "inline op1 kind");
        TEST_ASSERT(strcmp(manifest.ops[1].type, "debug_overlay") == 0, "inline op1 type");
    }

    /* The rs_iface mount is the only op that needs a cache pack loaded. */
    TEST_ASSERT(manifest.component_count == 1, "inline component req count");
    if( manifest.component_count == 1 )
        TEST_ASSERT(manifest.components[0].iface_id == 161, "inline component req iface");

    uibuilder_manifest_free(&manifest);
    remove(k_inline_path);
}

/*
 * A manifest that declares no RevConfig at all still boots: from_sources
 * synthesises the single rs_iface mount, which is what the interface-open path
 * used to produce. This is the whole compatibility story for the manifests that
 * have not been ported.
 */
static void
test_manifest_default_root_layout(void)
{
    struct UIBuilderManifest manifest;
    struct UIBuilderManifestSources src = { 0 };

    src.root_interface_id = 548;

    uibuilder_manifest_init(&manifest);
    TEST_ASSERT(uibuilder_manifest_from_sources(&manifest, &src) == 0, "from_sources default");
    TEST_ASSERT(manifest.op_count == 1, "default op count");
    if( manifest.op_count == 1 )
    {
        TEST_ASSERT(manifest.ops[0].kind == UIBUILDER_OP_PUSH_RS_SUBTREE, "default op kind");
        TEST_ASSERT(manifest.ops[0].componentno == 548, "default op root iface");
    }
    TEST_ASSERT(manifest.component_count == 1, "default component req count");

    uibuilder_manifest_free(&manifest);
}

/*
 * Layout/asset groups. The title screen and the gameframe live in one pair of
 * revconfig files and must not bake into each other's trees: the gameframe
 * would otherwise carry the title's every-frame flame repaint for the whole
 * session, and the title bake would pull the whole in-game atlas.
 */
static void
build_grouped(
    struct UIBuilderManifest* out,
    char const* select,
    char const* exclude)
{
    struct RevConfigBuffer* fields = revconfig_buffer_new(64);
    struct RevConfigItemBuffer* items = revconfig_item_buffer_new(16);
    TEST_ASSERT(fields && items, "alloc grouped");

    /* Untagged sprite: wanted by every build. */
    push_field(fields, RCFIELD_ITEMTYPE, "sprite");
    push_field(fields, RCFIELD_ITEMNAME, "compass");
    push_field(fields, RCFIELD_CACHE_ARCHIVE_ID, "169");
    push_field(fields, RCFIELD_ITEMDONE, "");

    /* Title-only sprite. */
    push_field(fields, RCFIELD_ITEMTYPE, "sprite");
    push_field(fields, RCFIELD_ITEMNAME, "titlebox");
    push_field(fields, RCFIELD_CACHE_ARCHIVE_ID, "1000");
    push_field(fields, RCFIELD_CACHE_GROUP, "title");
    push_field(fields, RCFIELD_ITEMDONE, "");

    /* Title-only font. */
    push_field(fields, RCFIELD_ITEMTYPE, "font");
    push_field(fields, RCFIELD_ITEMNAME, "q8");
    push_field(fields, RCFIELD_CACHE_ARCHIVE_ID, "497");
    push_field(fields, RCFIELD_CACHE_GROUP, "title");
    push_field(fields, RCFIELD_ITEMDONE, "");

    push_field(fields, RCFIELD_ITEMTYPE, "component");
    push_field(fields, RCFIELD_ITEMNAME, "compass");
    push_field(fields, RCFIELD_UICOMPONENT_TYPE, "compass");
    push_field(fields, RCFIELD_ITEMDONE, "");

    push_field(fields, RCFIELD_ITEMTYPE, "component");
    push_field(fields, RCFIELD_ITEMNAME, "login_box");
    push_field(fields, RCFIELD_UICOMPONENT_TYPE, "sprite");
    push_field(fields, RCFIELD_ITEMDONE, "");

    push_field(fields, RCFIELD_ITEMTYPE, "layout");
    push_field(fields, RCFIELD_UILAYOUT_GROUP, "fixed");
    push_field(fields, RCFIELD_UILAYOUT_NAME, "compass_slot");
    push_field(fields, RCFIELD_UILAYOUT_COMPONENT, "compass");
    push_field(fields, RCFIELD_ITEMDONE, "");

    push_field(fields, RCFIELD_ITEMTYPE, "layout");
    push_field(fields, RCFIELD_UILAYOUT_GROUP, "title");
    push_field(fields, RCFIELD_UILAYOUT_NAME, "box");
    push_field(fields, RCFIELD_UILAYOUT_COMPONENT, "login_box");
    push_field(fields, RCFIELD_ITEMDONE, "");

    revconfig_items_build(fields, items);
    uibuilder_manifest_init(out);
    TEST_ASSERT(
        uibuilder_manifest_from_revconfig_grouped(out, items, -1, select, exclude) == 0,
        "grouped build");

    revconfig_item_buffer_free(items);
    revconfig_buffer_free(fields);
}

static int
manifest_has_sprite(struct UIBuilderManifest const* m, char const* name)
{
    for( int i = 0; i < m->sprite_count; i++ )
    {
        if( strcmp(m->sprites[i].name, name) == 0 )
            return 1;
    }
    return 0;
}

static int
manifest_has_op(struct UIBuilderManifest const* m, char const* name)
{
    for( int i = 0; i < m->op_count; i++ )
    {
        if( strcmp(m->ops[i].name, name) == 0 )
            return 1;
    }
    return 0;
}

static void
test_manifest_layout_groups(void)
{
    struct UIBuilderManifest manifest;

    /* No selectors: every group, which is what every profile got before the
     * key existed. */
    build_grouped(&manifest, NULL, NULL);
    TEST_ASSERT(manifest_has_op(&manifest, "compass_slot"), "ungrouped build takes fixed");
    TEST_ASSERT(manifest_has_op(&manifest, "box"), "ungrouped build takes title");
    TEST_ASSERT(manifest.sprite_count == 2, "ungrouped build takes both sprites");
    TEST_ASSERT(manifest.font_count == 1, "ungrouped build takes the title font");
    uibuilder_manifest_free(&manifest);

    /* Title bake: the title layout plus every untagged asset. */
    build_grouped(&manifest, "title", NULL);
    TEST_ASSERT(!manifest_has_op(&manifest, "compass_slot"), "title build drops fixed");
    TEST_ASSERT(manifest_has_op(&manifest, "box"), "title build takes title");
    TEST_ASSERT(manifest_has_sprite(&manifest, "titlebox"), "title build takes titlebox");
    TEST_ASSERT(manifest_has_sprite(&manifest, "compass"), "title build takes untagged sprite");
    TEST_ASSERT(manifest.font_count == 1, "title build takes the title font");
    uibuilder_manifest_free(&manifest);

    /* Gameframe bake: everything except the title. */
    build_grouped(&manifest, NULL, "title");
    TEST_ASSERT(manifest_has_op(&manifest, "compass_slot"), "game build takes fixed");
    TEST_ASSERT(!manifest_has_op(&manifest, "box"), "game build drops title");
    TEST_ASSERT(!manifest_has_sprite(&manifest, "titlebox"), "game build drops titlebox");
    TEST_ASSERT(manifest_has_sprite(&manifest, "compass"), "game build keeps untagged sprite");
    TEST_ASSERT(manifest.font_count == 0, "game build drops the title font");
    uibuilder_manifest_free(&manifest);
}

/*
 * The shipped title screen, baked from the real revconfig.
 *
 * The gameframe and the title screen come out of one pair of files, so this
 * pins the two halves of that: selecting the title group yields the login
 * widgets and none of the gameframe, and the gameframe build yields the
 * reverse. A title node loose in the in-game tree would repaint every frame
 * for the whole session.
 */
static void
test_title_group_from_shipped_revconfig(void)
{
    struct UIBuilderManifest manifest;
    struct UIBuilderManifestSources src = { 0 };
    int login_inputs = 0;
    int login_buttons = 0;
    int progress = 0;
    int world = 0;

    src.ui_ini_path = "../revconfig/rs245_2lc/rs245_2lc_dat1_ui.ini";
    src.cache_ini_path = "../revconfig/rs245_2lc/rs245_2lc_dat1_cache.ini";
    src.root_interface_id = 161;
    src.layout_group = "title";

    uibuilder_manifest_init(&manifest);
    TEST_ASSERT(
        uibuilder_manifest_from_sources(&manifest, &src) == 0, "title group parses");

    for( int i = 0; i < manifest.op_count; i++ )
    {
        if( strcmp(manifest.ops[i].type, "login_input") == 0 )
            login_inputs++;
        else if( strcmp(manifest.ops[i].type, "login_button") == 0 )
            login_buttons++;
        else if( strcmp(manifest.ops[i].type, "title_progress") == 0 )
            progress++;
        else if( strcmp(manifest.ops[i].type, "world") == 0 )
            world++;
    }
    TEST_ASSERT(login_inputs == 2, "title group has both credential fields");
    TEST_ASSERT(login_buttons == 5, "title group has its five buttons");
    TEST_ASSERT(progress == 1, "title group has the loading bar");
    TEST_ASSERT(world == 0, "title group has no world viewport");
    TEST_ASSERT(manifest_has_sprite(&manifest, "titlebox"), "title group loads titlebox");

    /* The two credential rows must not collapse into one another's settings:
     * a username field wearing the password's mask hides what the player is
     * typing, and one wearing its prefix mislabels it. */
    {
        struct UIBuilderTreeOp const* user = NULL;
        struct UIBuilderTreeOp const* pass = NULL;
        for( int i = 0; i < manifest.op_count; i++ )
        {
            if( strcmp(manifest.ops[i].component_name, "login_username") == 0 )
                user = &manifest.ops[i];
            else if( strcmp(manifest.ops[i].component_name, "login_password") == 0 )
                pass = &manifest.ops[i];
        }
        TEST_ASSERT(user && pass, "both credential rows present");
        if( user && pass )
        {
            TEST_ASSERT(strcmp(user->title_field, "username") == 0, "username row is the username");
            TEST_ASSERT(strcmp(pass->title_field, "password") == 0, "password row is the password");
            TEST_ASSERT(user->title_mask[0] == 0, "username is not masked");
            TEST_ASSERT(pass->title_mask[0] == '*', "password is masked");
            TEST_ASSERT(user->title_maxlen == 12, "username cap");
            TEST_ASSERT(pass->title_maxlen == 20, "password cap");
            /* The trailing space is the reference's own label text, and it
             * only survives because the value is quoted -- revconfig trims
             * everything else. */
            TEST_ASSERT(
                strcmp(user->title_prefix, "Username: ") == 0, "username prefix keeps its space");
            TEST_ASSERT(
                strcmp(pass->title_prefix, "Password: ") == 0, "password prefix keeps its space");
            TEST_ASSERT(user->title_charset[0] != 0, "username charset survived");
        }
    }

    /* A centred row needs a box to centre in; without one it centres on the
     * box's left edge and half the line falls outside the panel. */
    for( int i = 0; i < manifest.op_count; i++ )
    {
        if( strcmp(manifest.ops[i].type, "login_message") != 0 &&
            strcmp(manifest.ops[i].type, "title_progress_text") != 0 )
            continue;
        TEST_ASSERT(manifest.ops[i].width > 0, "centred title row has a width");
    }

    uibuilder_manifest_free(&manifest);

    /* The gameframe half: the login widgets must not ride along. */
    src.layout_group = NULL;
    src.layout_group_exclude = "title";
    login_inputs = 0;
    world = 0;
    uibuilder_manifest_init(&manifest);
    TEST_ASSERT(
        uibuilder_manifest_from_sources(&manifest, &src) == 0, "gameframe parses");
    for( int i = 0; i < manifest.op_count; i++ )
    {
        if( strcmp(manifest.ops[i].type, "login_input") == 0 )
            login_inputs++;
        else if( strcmp(manifest.ops[i].type, "world") == 0 )
            world++;
    }
    TEST_ASSERT(login_inputs == 0, "gameframe drops the credential fields");
    TEST_ASSERT(world == 1, "gameframe keeps its world viewport");
    TEST_ASSERT(!manifest_has_sprite(&manifest, "titlebox"), "gameframe drops titlebox");

    uibuilder_manifest_free(&manifest);
}

int
main(void)
{
    g_failures = 0;
    test_manifest_from_hand_pushed_fields();
    test_manifest_from_inline_sources();
    test_manifest_default_root_layout();
    test_manifest_layout_groups();
    test_viewport_widgets_from_shipped_revconfig();
    test_title_group_from_shipped_revconfig();
    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("uitree_builder_test_manifest: ok\n");
    return 0;
}
