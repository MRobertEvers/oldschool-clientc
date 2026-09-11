#include "revconfig_refs.h"
#include "test_harness.h"

#include <string.h>

/*
 * The `[<kind>:<name>] id=` sections and the table the client reads them
 * through — the seam that stops a cache id from being a literal in C.
 *
 * The cases that matter are the negative ones: an undeclared name has to answer
 * -1 rather than 0, and `id=` must not be confused with any other section's
 * keys. Both were the failure mode this replaced — a zero that reads as "script
 * 0" or "the interface numbered zero" and does something plausible-looking.
 */
void
test_refs(void)
{
    printf("TEST: cache refs\n");

    static char const ini[] =
        "[script:settings_client_apply]\n"
        "id=3967\n"
        "\n"
        "[iface:xpdrop]\n"
        "id=122\n"
        "\n"
        "[varbit:settings_last_changed]\n"
        "id=9657\n"
        "\n"
        "[varp:npc_text_color]\n"
        "id=3541\n"
        "\n"
        "[seq:human_readyanim]\n"
        "id=808\n"
        "\n"
        "[setting:clear_npc_tags]\n"
        "id=267\n"
        "\n"
        "; a section that declares no id at all\n"
        "[script:never_stated]\n"
        "\n"
        "; dat1 spelling: the scene slot, not an archive\n"
        "[font:b12]\n"
        "font_name=b12\n"
        "cache_font_id=2\n"
        "\n"
        "; dat2 spelling: the fonts-table archive\n"
        "[font:p11]\n"
        "archive_id=494\n"
        "\n"
        "; `id=` is scoped to ref sections; a sprite must not pick it up\n"
        "[sprite:compass]\n"
        "table=sprites\n"
        "archive=compass\n";

    struct RevConfigBuffer* fields = revconfig_buffer_new(128);
    struct RevConfigItemBuffer* items = revconfig_item_buffer_new(32);
    struct RevConfigRefs refs;
    int ref_items = 0;
    int sprite_items = 0;

    TEST_ASSERT(fields && items, "alloc");

    revconfig_load_fields_from_ini_bytes((uint8_t const*)ini, (uint32_t)strlen(ini), fields);
    revconfig_items_build(fields, items);

    for( uint32_t i = 0; i < items->item_count; i++ )
    {
        if( items->items[i].kind == RCITEM_CACHE_REF )
            ref_items++;
        else if( items->items[i].kind == RCITEM_CACHE_SPRITE )
            sprite_items++;
    }
    TEST_ASSERT(ref_items == 7, "six kinds plus the id-less section build refs");
    TEST_ASSERT(sprite_items == 1, "the sprite section is still a sprite");

    RevConfigRefs_Init(&refs);
    RevConfigRefs_AddItems(&refs, items);

    TEST_ASSERT(RevConfigRefs_Get(&refs, "script", "settings_client_apply") == 3967, "script id");
    TEST_ASSERT(RevConfigRefs_Get(&refs, "iface", "xpdrop") == 122, "iface id");
    TEST_ASSERT(
        RevConfigRefs_Get(&refs, "varbit", "settings_last_changed") == 9657, "varbit id");
    TEST_ASSERT(RevConfigRefs_Get(&refs, "varp", "npc_text_color") == 3541, "varp id");
    TEST_ASSERT(RevConfigRefs_Get(&refs, "seq", "human_readyanim") == 808, "seq id");
    TEST_ASSERT(RevConfigRefs_Get(&refs, "setting", "clear_npc_tags") == 267, "setting id");

    /* The three ways of not having an answer, all -1 and none of them 0. */
    TEST_ASSERT(RevConfigRefs_Get(&refs, "script", "no_such_script") == -1, "unknown name is -1");
    TEST_ASSERT(RevConfigRefs_Get(&refs, "iface", "settings_client_apply") == -1, "wrong kind is -1");
    TEST_ASSERT(RevConfigRefs_Get(&refs, "script", "never_stated") == -1, "missing id= is -1");

    /* A font answers a different number per era for the same section. */
    TEST_ASSERT(RevConfigRefs_FontCacheId(&refs, "b12", 1) == 2, "dat1 font is the scene slot");
    TEST_ASSERT(RevConfigRefs_FontCacheId(&refs, "p11", 0) == 494, "dat2 font is the archive id");
    TEST_ASSERT(
        RevConfigRefs_FontCacheId(&refs, "p11", 1) == -1, "p11 states no dat1 slot");
    TEST_ASSERT(RevConfigRefs_FontCacheId(&refs, "q8", 0) == -1, "undeclared font is -1");

    RevConfigRefs_Free(&refs);

    /* Later wins, so a boot manifest's inline sections can override a shared
     * profile without restating it. */
    {
        static char const override_ini[] =
            "[script:settings_client_apply]\n"
            "id=1234\n";
        struct RevConfigBuffer* f2 = revconfig_buffer_new(32);
        struct RevConfigItemBuffer* i2 = revconfig_item_buffer_new(8);
        struct RevConfigRefs merged;

        TEST_ASSERT(f2 && i2, "alloc override");
        revconfig_load_fields_from_ini_bytes(
            (uint8_t const*)override_ini, (uint32_t)strlen(override_ini), f2);
        revconfig_items_build(f2, i2);

        RevConfigRefs_Init(&merged);
        RevConfigRefs_AddItems(&merged, items);
        RevConfigRefs_AddItems(&merged, i2);
        TEST_ASSERT(
            RevConfigRefs_Get(&merged, "script", "settings_client_apply") == 1234,
            "a later declaration replaces an earlier one");
        TEST_ASSERT(
            RevConfigRefs_Get(&merged, "iface", "xpdrop") == 122,
            "and leaves everything it did not name alone");
        RevConfigRefs_Free(&merged);

        revconfig_item_buffer_free(i2);
        revconfig_buffer_free(f2);
    }

    revconfig_item_buffer_free(items);
    revconfig_buffer_free(fields);
}
