#include "test_harness.h"

#include <string.h>

/*
 * `[role:<name>]` matcher chains and the `role=` key on a [component:…].
 *
 * The cases that matter are the shape of the grammar under nesting -- both
 * `id(if(553, 0))` and `cc(iface(xpdrop), 4)` put a call inside an argument, so
 * an argument splitter that took the first comma it saw would read the second
 * as `cc(iface(xpdrop` -- and the negative ones: a line that does not parse has
 * to be REPORTED and dropped, never accepted as a rung that then silently never
 * resolves on any world.
 */

static struct RevConfigRoleItem const*
find_role(struct RevConfigItemBuffer const* items, char const* name)
{
    for( uint32_t i = 0; i < items->item_count; i++ )
    {
        if( items->items[i].kind == RCITEM_ROLE &&
            strcmp(items->items[i].u.role.name, name) == 0 )
            return &items->items[i].u.role;
    }
    return NULL;
}

static void
test_role_matcher_forms(void)
{
    struct RevConfigRoleMatcher m;

    /* slot(), with and without a member. */
    TEST_ASSERT(revconfig_parse_role_matcher("slot(safe)", &m), "slot() parses");
    TEST_ASSERT(m.kind == REVCONFIG_ROLE_MATCH_SLOT, "slot() kind");
    TEST_ASSERT(strcmp(m.slot, "safe") == 0, "slot() region");
    TEST_ASSERT(m.member[0] == '\0', "slot() with no member states none");

    TEST_ASSERT(
        revconfig_parse_role_matcher("slot(chat_buttons, report)", &m),
        "slot() with member parses");
    TEST_ASSERT(strcmp(m.slot, "chat_buttons") == 0, "slot() member region");
    TEST_ASSERT(strcmp(m.member, "report") == 0, "slot() member kept verbatim");

    /* A member that is a number stays a string -- which numbering it is in is
     * the role's business, not the parser's. */
    TEST_ASSERT(revconfig_parse_role_matcher("slot(sidebar, 10)", &m), "slot() numeric member");
    TEST_ASSERT(strcmp(m.member, "10") == 0, "numeric member kept verbatim");

    /* id(), flat and packed. */
    TEST_ASSERT(revconfig_parse_role_matcher("id(2449)", &m), "id() parses");
    TEST_ASSERT(m.kind == REVCONFIG_ROLE_MATCH_ID, "id() kind");
    TEST_ASSERT(m.ref.kind == REVCONFIG_ROLE_MATCH_ID, "id() ref kind");
    TEST_ASSERT(m.ref.value == 2449, "id() flat dat1 uid");

    TEST_ASSERT(revconfig_parse_role_matcher("id(if(553, 0))", &m), "id(if()) parses");
    TEST_ASSERT(m.ref.value == (553 << 16), "id(if()) packs the uid");

    TEST_ASSERT(
        revconfig_parse_role_matcher("id((160 << 16) | 28)", &m), "id() takes arithmetic");
    TEST_ASSERT(m.ref.value == ((160 << 16) | 28), "id() arithmetic value");

    /* iface(), with and without a child. */
    TEST_ASSERT(revconfig_parse_role_matcher("iface(logout)", &m), "iface() parses");
    TEST_ASSERT(m.kind == REVCONFIG_ROLE_MATCH_IFACE, "iface() kind");
    TEST_ASSERT(strcmp(m.ref.name, "logout") == 0, "iface() name");
    TEST_ASSERT(m.ref.value == 0, "iface() with no child means the group root");

    TEST_ASSERT(revconfig_parse_role_matcher("iface(xpdrop, 4)", &m), "iface() with child");
    TEST_ASSERT(strcmp(m.ref.name, "xpdrop") == 0, "iface() child name");
    TEST_ASSERT(m.ref.value == 4, "iface() child value");

    /* clientcode(). */
    TEST_ASSERT(revconfig_parse_role_matcher("clientcode(205)", &m), "clientcode() parses");
    TEST_ASSERT(m.kind == REVCONFIG_ROLE_MATCH_CLIENTCODE, "clientcode() kind");
    TEST_ASSERT(m.value == 205, "clientcode() value");

    /* cc(), the nesting case. */
    TEST_ASSERT(revconfig_parse_role_matcher("cc(iface(xpdrop), 4)", &m), "cc(iface()) parses");
    TEST_ASSERT(m.kind == REVCONFIG_ROLE_MATCH_CC, "cc() kind");
    TEST_ASSERT(m.ref.kind == REVCONFIG_ROLE_MATCH_IFACE, "cc() anchor is an iface");
    TEST_ASSERT(strcmp(m.ref.name, "xpdrop") == 0, "cc() anchor name");
    TEST_ASSERT(m.value == 4, "cc() sub id");

    TEST_ASSERT(
        revconfig_parse_role_matcher("cc(id(if(162, 42)), 7)", &m), "cc(id(if())) parses");
    TEST_ASSERT(m.ref.kind == REVCONFIG_ROLE_MATCH_ID, "cc() anchor is a uid");
    TEST_ASSERT(m.ref.value == ((162 << 16) | 42), "cc() anchor uid");
    TEST_ASSERT(m.value == 7, "cc() sub id past a nested call");

    /* Whitespace is not significant anywhere. */
    TEST_ASSERT(
        revconfig_parse_role_matcher("  cc( iface( xpdrop ) , 4 )  ", &m),
        "cc() tolerates spacing");
    TEST_ASSERT(strcmp(m.ref.name, "xpdrop") == 0, "spacing trimmed from a nested name");
    TEST_ASSERT(m.value == 4, "spacing trimmed from a sub id");
}

static void
test_role_matcher_rejects(void)
{
    struct RevConfigRoleMatcher m;

    printf("  (five 'unrecognised role matcher' lines below are the point)\n");

    /* Every one of these must be REPORTED and refused rather than becoming a
     * rung that quietly never matches. */
    TEST_ASSERT(!revconfig_parse_role_matcher("slot", &m), "a bare word is not a matcher");
    TEST_ASSERT(!revconfig_parse_role_matcher("wibble(4)", &m), "an unknown form is refused");
    TEST_ASSERT(!revconfig_parse_role_matcher("id(4", &m), "an unclosed call is refused");
    TEST_ASSERT(!revconfig_parse_role_matcher("id(4) junk", &m), "a trailing tail is refused");
    TEST_ASSERT(!revconfig_parse_role_matcher("cc(iface(x))", &m), "cc() needs its sub id");
}

static void
test_role_sections(void)
{
    static char const ini[] =
        "[role:logout_screen]\n"
        "match=iface(logout)\n"
        "match=id(2449)\n"
        "\n"
        "[role:report_button]\n"
        "match=slot(chat_buttons, report)\n"
        "match=clientcode(601)\n"
        "\n"
        "; a role that declares nothing at all\n"
        "[role:never_stated]\n"
        "\n"
        "[component:chat_button_report]\n"
        "type=chat_button\n"
        "filter=report\n"
        "role=report_button\n"
        "\n"
        "[component:world]\n"
        "type=world\n";

    struct RevConfigBuffer* fields = revconfig_buffer_new(64);
    struct RevConfigItemBuffer* items = revconfig_item_buffer_new(16);
    struct RevConfigRoleItem const* role;

    revconfig_load_fields_from_ini_bytes((uint8_t const*)ini, (uint32_t)strlen(ini), fields);
    revconfig_items_build(fields, items);

    role = find_role(items, "logout_screen");
    TEST_ASSERT(role != NULL, "[role:logout_screen] built an item");
    if( role )
    {
        TEST_ASSERT(role->matcher_count == 2, "logout_screen kept both rungs");
        /* Declaration order is the resolution order, so it has to survive. */
        TEST_ASSERT(
            role->matchers[0].kind == REVCONFIG_ROLE_MATCH_IFACE,
            "logout_screen rung 0 is the iface");
        TEST_ASSERT(
            role->matchers[1].kind == REVCONFIG_ROLE_MATCH_ID &&
                role->matchers[1].ref.value == 2449,
            "logout_screen rung 1 is the dat1 uid");
    }

    role = find_role(items, "report_button");
    TEST_ASSERT(role != NULL, "[role:report_button] built an item");
    if( role )
    {
        TEST_ASSERT(role->matcher_count == 2, "report_button kept both rungs");
        TEST_ASSERT(
            strcmp(role->matchers[0].member, "report") == 0, "report_button rung 0 member");
    }

    role = find_role(items, "never_stated");
    TEST_ASSERT(role != NULL, "a role section with no match= is still an item");
    if( role )
        TEST_ASSERT(role->matcher_count == 0, "…carrying no rungs");

    /* The baked-tag channel: role= on a component. */
    int tagged = 0;
    int untagged_is_clean = 0;
    for( uint32_t i = 0; i < items->item_count; i++ )
    {
        if( items->items[i].kind != RCITEM_UICOMPONENT )
            continue;
        if( strcmp(items->items[i].u.uicomponent.name, "chat_button_report") == 0 &&
            strcmp(items->items[i].u.uicomponent.role, "report_button") == 0 )
            tagged = 1;
        if( strcmp(items->items[i].u.uicomponent.name, "world") == 0 &&
            items->items[i].u.uicomponent.role[0] == '\0' )
            untagged_is_clean = 1;
    }
    TEST_ASSERT(tagged, "component role= is carried onto the item");
    TEST_ASSERT(untagged_is_clean, "a component with no role= carries none");

    revconfig_buffer_free(fields);
    revconfig_item_buffer_free(items);
}

void
test_roles(void)
{
    printf("TEST: semantic roles\n");

    test_role_matcher_forms();
    test_role_matcher_rejects();
    test_role_sections();
}
