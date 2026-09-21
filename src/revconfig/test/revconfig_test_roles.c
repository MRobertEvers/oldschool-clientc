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
    TEST_ASSERT(revconfig_parse_role_matcher("slot(safe_gamechrome)", &m), "slot() parses");
    TEST_ASSERT(m.kind == REVCONFIG_ROLE_MATCH_SLOT, "slot() kind");
    TEST_ASSERT(strcmp(m.slot, "safe_gamechrome") == 0, "slot() region");
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

/* D10: any(v1,…) inside one rung's numeric argument, and cc()'s optional
 * third-argument type filter. */
static void
test_role_matcher_any_and_cc_type(void)
{
    struct RevConfigRoleMatcher m;

    /* id(any(...)): two alternates. */
    TEST_ASSERT(revconfig_parse_role_matcher("id(any(553, 554))", &m), "id(any()) parses");
    TEST_ASSERT(m.kind == REVCONFIG_ROLE_MATCH_ID, "id(any()) kind");
    TEST_ASSERT(m.ref.value == 553, "id(any()) first value");
    TEST_ASSERT(m.ref.any_count == 1, "id(any()) one extra value");
    TEST_ASSERT(m.ref.any_value[0] == 554, "id(any()) second value");

    /* A plain id() states no alternates -- any_count stays 0 for every
     * existing caller that never looks at it. */
    TEST_ASSERT(revconfig_parse_role_matcher("id(2449)", &m), "id() still parses");
    TEST_ASSERT(m.ref.any_count == 0, "id() plain form has no alternates");

    /* clientcode(any(...)). */
    TEST_ASSERT(
        revconfig_parse_role_matcher("clientcode(any(10, 11))", &m), "clientcode(any()) parses");
    TEST_ASSERT(m.value == 10, "clientcode(any()) first value");
    TEST_ASSERT(m.any_count == 1, "clientcode(any()) one extra value");
    TEST_ASSERT(m.any_value[0] == 11, "clientcode(any()) second value");

    /* iface(name, any(...)): the child position. */
    TEST_ASSERT(
        revconfig_parse_role_matcher("iface(xpdrop, any(4, 5, 6))", &m),
        "iface(any()) parses");
    TEST_ASSERT(strcmp(m.ref.name, "xpdrop") == 0, "iface(any()) name");
    TEST_ASSERT(m.ref.value == 4, "iface(any()) first child");
    TEST_ASSERT(m.ref.any_count == 2, "iface(any()) two extra children");
    TEST_ASSERT(m.ref.any_value[0] == 5 && m.ref.any_value[1] == 6, "iface(any()) rest");

    /* any() with the maximum four values. */
    TEST_ASSERT(
        revconfig_parse_role_matcher("clientcode(any(1, 2, 3, 4))", &m),
        "any() with 4 values parses");
    TEST_ASSERT(m.any_count == 3, "any() with 4 values has 3 extras");

    /* Rejected: 0 args, and more than REVCONFIG_ROLE_MAX_ANY_VALUES. */
    TEST_ASSERT(!revconfig_parse_role_matcher("clientcode(any())", &m), "any() needs >=1 value");
    TEST_ASSERT(
        !revconfig_parse_role_matcher("clientcode(any(1, 2, 3, 4, 5))", &m),
        "any() with 5 values is refused");

    /* cc(<anchor>, <sub_id>, <type>): the two-argument form is unaffected,
     * and the type name is kept verbatim (translated to a tree enum only by
     * uitree_role_load.c, which does not build in this test). */
    TEST_ASSERT(revconfig_parse_role_matcher("cc(iface(xpdrop), 4)", &m), "cc() two-arg form");
    TEST_ASSERT(m.cc_type[0] == '\0', "cc() two-arg form states no type filter");

    TEST_ASSERT(
        revconfig_parse_role_matcher("cc(iface(chatmenu, 1), 3, text)", &m),
        "cc() three-arg form parses");
    TEST_ASSERT(m.value == 3, "cc() three-arg form keeps the sub id");
    TEST_ASSERT(strcmp(m.cc_type, "text") == 0, "cc() three-arg form keeps the type name");

    TEST_ASSERT(
        revconfig_parse_role_matcher("cc(iface(chatmenu, 1), any(1, 2), text)", &m),
        "cc() combines any() with a type filter");
    TEST_ASSERT(m.value == 1 && m.any_count == 1 && m.any_value[0] == 2,
        "cc() any() sub ids survive the trailing type argument");
    TEST_ASSERT(strcmp(m.cc_type, "text") == 0, "cc() type filter survives an any() sub id");

    TEST_ASSERT(
        !revconfig_parse_role_matcher("cc(iface(xpdrop), 4, )", &m),
        "cc() with an empty type argument is refused");
}

/* derive=<fact>[(<argument>)] -- rides UITreeRoleTable.fallback instead of a
 * match= chain. */
static void
test_role_derive(void)
{
    char fact[32];
    int argument;

    TEST_ASSERT(
        revconfig_parse_role_derive("dialog_continue", fact, sizeof(fact), &argument),
        "a bare fact name parses");
    TEST_ASSERT(strcmp(fact, "dialog_continue") == 0, "bare fact name kept verbatim");
    TEST_ASSERT(argument == -1, "a bare fact name states no argument");

    TEST_ASSERT(
        revconfig_parse_role_derive("button_type(6)", fact, sizeof(fact), &argument),
        "fact(<expr>) parses");
    TEST_ASSERT(strcmp(fact, "button_type") == 0, "fact(<expr>) name");
    TEST_ASSERT(argument == 6, "fact(<expr>) argument");

    /* 0 must stay distinguishable from "no argument stated". */
    TEST_ASSERT(
        revconfig_parse_role_derive("button_type(0)", fact, sizeof(fact), &argument),
        "fact(0) parses");
    TEST_ASSERT(argument == 0, "fact(0) argument is 0, not -1");

    TEST_ASSERT(
        !revconfig_parse_role_derive("", fact, sizeof(fact), &argument),
        "an empty derive= line is refused");
    TEST_ASSERT(
        !revconfig_parse_role_derive("button_type(", fact, sizeof(fact), &argument),
        "an unclosed call is refused");
    TEST_ASSERT(
        !revconfig_parse_role_derive("button_type(abc)", fact, sizeof(fact), &argument),
        "a non-numeric argument is refused");

    /* Sections: derive= alongside match=, the way [role:dialog_continue] and
     * [role:pause_pending] are written in the generated ini. */
    {
        static char const ini[] =
            "[role:dialog_continue]\n"
            "derive=dialog_continue\n"
            "\n"
            "[role:dialog_npc_continue]\n"
            "match=iface(chat_left, 5)\n";

        struct RevConfigBuffer* fields = revconfig_buffer_new(32);
        struct RevConfigItemBuffer* items = revconfig_item_buffer_new(8);
        struct RevConfigRoleItem const* role;

        revconfig_load_fields_from_ini_bytes((uint8_t const*)ini, (uint32_t)strlen(ini), fields);
        revconfig_items_build(fields, items);

        role = find_role(items, "dialog_continue");
        TEST_ASSERT(role != NULL, "[role:dialog_continue] built an item");
        if( role )
        {
            TEST_ASSERT(strcmp(role->derive_fact, "dialog_continue") == 0,
                "derive= fact carried onto the item");
            TEST_ASSERT(role->matcher_count == 0, "a derive= role states no match= rungs");
        }

        role = find_role(items, "dialog_npc_continue");
        TEST_ASSERT(role != NULL, "[role:dialog_npc_continue] built an item");
        if( role )
            TEST_ASSERT(
                role->derive_fact[0] == '\0', "a match= role carries no derive= fact");

        revconfig_item_buffer_free(items);
        revconfig_buffer_free(fields);
    }
}

/* revconfig_derive_sibling_path: the suffix swap that chains a lane's
 * generated role file beside its hand-edited cache ini. */
static void
test_derive_sibling_path(void)
{
    char out[256];

    TEST_ASSERT(
        revconfig_derive_sibling_path(
            "revconfig/osrs239/osrs239_dat2_cache.ini",
            "_dat2_cache.ini",
            "_dat2_roles.gen.ini",
            out,
            sizeof(out)) != NULL,
        "a matching suffix derives a sibling path");
    TEST_ASSERT(
        strcmp(out, "revconfig/osrs239/osrs239_dat2_roles.gen.ini") == 0,
        "the derived path swaps only the suffix");

    TEST_ASSERT(
        revconfig_derive_sibling_path(
            "revconfig/rs245_2lc/rs245_2lc_dat1_cache.ini",
            "_dat2_cache.ini",
            "_dat2_roles.gen.ini",
            out,
            sizeof(out)) == NULL,
        "a dat1 cache ini derives nothing");

    TEST_ASSERT(
        revconfig_derive_sibling_path(
            "x_dat2_cache.ini", "_dat2_cache.ini", "_dat2_roles.gen.ini", out, 4) == NULL,
        "an output buffer too small derives nothing");
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
    test_role_matcher_any_and_cc_type();
    test_role_derive();
    test_derive_sibling_path();
    test_role_sections();
}
