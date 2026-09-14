#include "revconfig_refs.h"
#include "test_harness.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>

/*
 * `[tabs]`, `[tabs:<root>]`, and the dat1 derivation that answers the same
 * question on a profile that has neither.
 *
 * "inventory is tab 3" was a bare 3 in whatever C, Lua or role name wanted it,
 * and it is a bare 3 that differs per revision -- which is the whole class of
 * bug revconfig exists to delete. The cases that matter here are the ones
 * where the two lanes disagree about the SPELLING and have to agree about the
 * answer, and the ones where a number is absent rather than zero.
 */

/** Build the items one INI body describes and merge them into `refs`. */
static void
merge_refs(
    struct RevConfigRefs* refs,
    char const* ini)
{
    struct RevConfigBuffer* fields = revconfig_buffer_new(128);
    struct RevConfigItemBuffer* items = revconfig_item_buffer_new(32);

    TEST_ASSERT(fields && items, "alloc");
    revconfig_load_fields_from_ini_bytes((uint8_t const*)ini, (uint32_t)strlen(ini), fields);
    revconfig_items_build(fields, items);
    RevConfigRefs_AddItems(refs, items);

    revconfig_item_buffer_free(items);
    revconfig_buffer_free(fields);
}

static void
test_tabs_base_map(void)
{
    /* The osrs239 map, verbatim, plus a section after it -- a free-key section
     * that swallowed the next header would be invisible otherwise. */
    static char const ini[] =
        "[tabs]\n"
        "combat=0\n"
        "stats=1\n"
        "quests=2\n"
        "inventory=3\n"
        "equipment=4\n"
        "prayer=5\n"
        "magic=6\n"
        "clan=7\n"
        "account=8\n"
        "friends=9\n"
        "logout=10\n"
        "options=11\n"
        "emotes=12\n"
        "music=13\n"
        "\n"
        "[iface:chat]\n"
        "id=162\n";
    struct RevConfigRefs refs;

    RevConfigRefs_Init(&refs);
    merge_refs(&refs, ini);

    TEST_ASSERT(RevConfigRefs_Get(&refs, "tab", "inventory") == 3, "inventory is tab 3");
    TEST_ASSERT(RevConfigRefs_Get(&refs, "tab", "music") == 13, "music is tab 13");
    /* Tab 0 is the case a "0 means unstated" table gets wrong. */
    TEST_ASSERT(RevConfigRefs_Get(&refs, "tab", "combat") == 0, "combat is tab 0, not unstated");
    TEST_ASSERT(RevConfigRefs_Get(&refs, "tab", "sailing") == -1, "a tab this rev lacks is -1");
    TEST_ASSERT(RevConfigRefs_Get(&refs, "iface", "chat") == 162, "the next section still parses");
    /* `tab` is its own kind; a tab name must not answer as anything else. */
    TEST_ASSERT(RevConfigRefs_Get(&refs, "iface", "inventory") == -1, "a tab is not an iface");

    RevConfigRefs_Free(&refs);
}

static void
test_tabs_per_root(void)
{
    /* 164 and 601 as the osrs239 profile states them: one strip with a
     * detached logout, and two columns with a detached logout. */
    static char const ini[] =
        "[tabs]\n"
        "combat=0\n"
        "inventory=3\n"
        "logout=10\n"
        "\n"
        "[tabs:164]\n"
        "detached=logout\n"
        "\n"
        "[tabs:601]\n"
        "columns=inventory,equipment,prayer,magic,combat,quests\n"
        "columns=stats,emotes,music,clan,friends,account,options\n"
        "detached=logout\n";
    struct RevConfigRefs refs;

    RevConfigRefs_Init(&refs);
    merge_refs(&refs, ini);

    /* Column, then position within it. Both are per-root, so the key carries
     * the root: the same tab is arranged differently on 164 and on 601. */
    TEST_ASSERT(RevConfigRefs_Get(&refs, "tabcol", "601:inventory") == 0, "inventory: left column");
    TEST_ASSERT(RevConfigRefs_Get(&refs, "tabpos", "601:inventory") == 0, "inventory: top of it");
    TEST_ASSERT(RevConfigRefs_Get(&refs, "tabpos", "601:combat") == 4, "combat: fifth down");
    TEST_ASSERT(RevConfigRefs_Get(&refs, "tabcol", "601:stats") == 1, "stats: right column");
    TEST_ASSERT(RevConfigRefs_Get(&refs, "tabpos", "601:options") == 6, "options: bottom of it");

    TEST_ASSERT(RevConfigRefs_Get(&refs, "tabdetach", "164:logout") == 1, "164 hangs logout apart");
    TEST_ASSERT(RevConfigRefs_Get(&refs, "tabdetach", "601:logout") == 1, "601 does too");
    TEST_ASSERT(
        RevConfigRefs_Get(&refs, "tabdetach", "601:inventory") == -1,
        "a tab in a column is not detached");
    /* 164 states no columns at all -- one plain strip -- and that reads the
     * same as a root this section never mentions. */
    TEST_ASSERT(
        RevConfigRefs_Get(&refs, "tabcol", "164:inventory") == -1, "164 states no columns");
    TEST_ASSERT(
        RevConfigRefs_Get(&refs, "tabcol", "548:inventory") == -1, "nor does an unnamed root");
    /* The arrangement is per-root and the base map is not; neither leaks. */
    TEST_ASSERT(RevConfigRefs_Get(&refs, "tab", "601:inventory") == -1, "a root key is not a tab");
    TEST_ASSERT(RevConfigRefs_Get(&refs, "tabcol", "inventory") == -1, "and a tab is not a root key");

    RevConfigRefs_Free(&refs);
}

static void
test_tabs_columns_on_one_line(void)
{
    /* The `|` spelling, for a root whose columns fit in one 64-byte field. */
    static char const ini[] =
        "[tabs:601]\n"
        "columns=combat, quests | stats,magic\n";
    struct RevConfigRefs refs;

    RevConfigRefs_Init(&refs);
    merge_refs(&refs, ini);

    TEST_ASSERT(RevConfigRefs_Get(&refs, "tabcol", "601:combat") == 0, "before the bar");
    TEST_ASSERT(RevConfigRefs_Get(&refs, "tabpos", "601:quests") == 1, "and its second row");
    TEST_ASSERT(RevConfigRefs_Get(&refs, "tabcol", "601:stats") == 1, "after the bar");
    TEST_ASSERT(RevConfigRefs_Get(&refs, "tabpos", "601:magic") == 1, "and its second row");

    RevConfigRefs_Free(&refs);
}

static void
test_tabs_dat1_fallback(void)
{
    /* The 2004 profile's spelling: the numbering is in the sidebar roles,
     * because that is how its mounts are found. */
    static char const dat1[] =
        "[role:panel_combat]\n"
        "match=slot(sidebar, 0)\n"
        "\n"
        "[role:panel_inventory]\n"
        "match=slot(sidebar, 3)\n"
        "\n"
        "[role:panel_music]\n"
        "match=slot(sidebar, 13)\n"
        "\n"
        "; not a sidebar mount, so it is not a tab\n"
        "[role:logout_screen]\n"
        "match=slot(sidebar, 10)\n"
        "\n"
        "; the dat2 spelling of the same role carries no tab number\n"
        "[role:panel_equipment]\n"
        "match=id(if(387, 0))\n";
    struct RevConfigRefs refs;

    RevConfigRefs_Init(&refs);
    merge_refs(&refs, dat1);

    TEST_ASSERT(RevConfigRefs_Get(&refs, "tab", "inventory") == 3, "derived from the role");
    TEST_ASSERT(RevConfigRefs_Get(&refs, "tab", "combat") == 0, "tab 0 derives too");
    TEST_ASSERT(RevConfigRefs_Get(&refs, "tab", "music") == 13, "and the last one");
    TEST_ASSERT(
        RevConfigRefs_Get(&refs, "tab", "equipment") == -1,
        "an id() mount states no tab number");
    TEST_ASSERT(
        RevConfigRefs_Get(&refs, "tab", "logout_screen") == -1,
        "only panel_<name> is a tab row");

    RevConfigRefs_Free(&refs);
}

static void
test_tabs_explicit_beats_derived(void)
{
    static char const roles[] =
        "[role:panel_inventory]\n"
        "match=slot(sidebar, 3)\n";
    /* A revision that renumbered, stating it outright. */
    static char const tabs[] =
        "[tabs]\n"
        "inventory=7\n";

    {
        struct RevConfigRefs refs;
        RevConfigRefs_Init(&refs);
        merge_refs(&refs, roles);
        merge_refs(&refs, tabs);
        TEST_ASSERT(
            RevConfigRefs_Get(&refs, "tab", "inventory") == 7,
            "a later [tabs] replaces a derived row");
        RevConfigRefs_Free(&refs);
    }
    {
        struct RevConfigRefs refs;
        RevConfigRefs_Init(&refs);
        merge_refs(&refs, tabs);
        merge_refs(&refs, roles);
        TEST_ASSERT(
            RevConfigRefs_Get(&refs, "tab", "inventory") == 7,
            "and an earlier one is not overwritten by the derivation");
        RevConfigRefs_Free(&refs);
    }
}

/* ------------------------------------------------------------------------ */
/* The shipped profiles                                                      */
/* ------------------------------------------------------------------------ */

/*
 * Where the repo root is, from wherever this binary was started.
 *
 * `make -C src test-revconfig` runs it in src/, and a developer running it by
 * hand is as likely to be at the root. Both are tried and the first that has a
 * manifests/ directory wins; a run from neither fails loudly rather than
 * quietly skipping the whole case, which is the failure mode a "profiles still
 * load" test cannot afford.
 */
static char const*
repo_root(void)
{
    static char const* const CANDIDATES[] = { "..", ".", "../.." };
    for( size_t i = 0; i < sizeof(CANDIDATES) / sizeof(CANDIDATES[0]); i++ )
    {
        char path[512];
        DIR* dir;
        snprintf(path, sizeof(path), "%s/manifests", CANDIDATES[i]);
        dir = opendir(path);
        if( dir )
        {
            closedir(dir);
            return CANDIDATES[i];
        }
    }
    return NULL;
}

/**
 * Load one profile file and check it came through whole.
 *
 * "Loads without a parse error" has no return code to read -- the loader
 * reports and carries on -- so the test asks the questions a broken file would
 * fail: it produced items at all, and every `[role:…]` in it kept at least one
 * matcher. A malformed `match=` line is DROPPED by the role parser, so a role
 * that ends with none is exactly the tell of a grammar this file got wrong.
 */
static void
check_profile_loads(char const* path)
{
    struct RevConfigBuffer* fields = revconfig_buffer_new(512);
    struct RevConfigItemBuffer* items = revconfig_item_buffer_new(128);
    int roles = 0;
    int roles_without_matchers = 0;
    char message[640];

    TEST_ASSERT(fields && items, "alloc");

    revconfig_load_fields_from_ini(path, fields);
    snprintf(message, sizeof(message), "%s produced fields", path);
    TEST_ASSERT(fields->field_count > 0, message);

    revconfig_items_build(fields, items);
    snprintf(message, sizeof(message), "%s produced items", path);
    TEST_ASSERT(items->item_count > 0, message);

    for( uint32_t i = 0; i < items->item_count; i++ )
    {
        if( items->items[i].kind != RCITEM_ROLE )
            continue;
        roles++;
        if( items->items[i].u.role.matcher_count == 0 )
        {
            roles_without_matchers++;
            fprintf(stderr, "  %s: [role:%s] kept no match= line\n",
                path,
                items->items[i].u.role.name);
        }
    }
    (void)roles;
    snprintf(message, sizeof(message), "%s: every role kept a matcher", path);
    TEST_ASSERT(roles_without_matchers == 0, message);

    revconfig_item_buffer_free(items);
    revconfig_buffer_free(fields);
}

/**
 * Every revconfig file a boot manifest names still loads.
 *
 * The manifests are the authority on which profiles ship: a file nothing names
 * is not in the boot path, and a file a manifest names and this test cannot
 * open is a boot that dies at the title screen. Scanning them beats listing
 * the profiles here, which would go stale the first time a lane is added.
 */
static void
test_shipped_profiles(void)
{
    char const* root = repo_root();
    char manifest_dir[512];
    DIR* dir;
    struct dirent* entry;
    int checked = 0;
    /* Each profile is named by several manifests; loading one twice is only
     * slow, but the duplicate report would bury a real failure. */
    char seen[64][512];
    int seen_count = 0;

    TEST_ASSERT(root != NULL, "found the repo root (run from the root or from src/)");
    if( !root )
        return;

    snprintf(manifest_dir, sizeof(manifest_dir), "%s/manifests", root);
    dir = opendir(manifest_dir);
    TEST_ASSERT(dir != NULL, "opened manifests/");
    if( !dir )
        return;

    while( (entry = readdir(dir)) != NULL )
    {
        char manifest_path[512];
        FILE* file;
        char line[1024];

        if( entry->d_name[0] == '.' )
            continue;
        if( !strstr(entry->d_name, ".ini") )
            continue;
        snprintf(manifest_path, sizeof(manifest_path), "%s/%s", manifest_dir, entry->d_name);
        file = fopen(manifest_path, "rb");
        if( !file )
            continue;

        while( fgets(line, sizeof(line), file) )
        {
            char const* value = NULL;
            char profile_path[512];
            int duplicate = 0;
            size_t length;

            if( strncmp(line, "revconfig_ui=", 13) == 0 )
                value = line + 13;
            else if( strncmp(line, "revconfig_cache=", 16) == 0 )
                value = line + 16;
            if( !value )
                continue;

            length = strlen(value);
            while( length > 0 && (value[length - 1] == '\n' || value[length - 1] == '\r' ||
                                  value[length - 1] == ' ') )
                length--;
            if( length == 0 )
                continue;
            /* Manifest paths resolve against the manifest's own directory. */
            snprintf(
                profile_path, sizeof(profile_path), "%s/%.*s", manifest_dir, (int)length, value);

            for( int i = 0; i < seen_count; i++ )
                duplicate = duplicate || strcmp(seen[i], profile_path) == 0;
            if( duplicate )
                continue;
            if( seen_count < (int)(sizeof(seen) / sizeof(seen[0])) )
                snprintf(seen[seen_count++], sizeof(seen[0]), "%s", profile_path);

            check_profile_loads(profile_path);
            checked++;
        }
        fclose(file);
    }
    closedir(dir);

    /* Four lanes ship a revconfig pair; a scan that found none has found the
     * wrong directory, and every assertion above would have passed vacuously. */
    TEST_ASSERT(checked >= 4, "the manifests name at least four revconfig files");
}

/**
 * The two headline facts, on the files that actually ship.
 *
 * The cases above prove the grammar; this proves the DATA -- that the osrs239
 * profile states the map and that the 2004 one still derives it. Either could
 * be deleted by an edit that leaves every parser test green.
 */
static void
test_shipped_tab_maps(void)
{
    char const* root = repo_root();
    char path[512];
    struct RevConfigRefs refs;

    if( !root )
        return;

    RevConfigRefs_Init(&refs);
    snprintf(path, sizeof(path), "%s/revconfig/osrs239/osrs239_dat2_cache.ini", root);
    RevConfigRefs_LoadSources(&refs, NULL, path, NULL);
    TEST_ASSERT(RevConfigRefs_Get(&refs, "tab", "inventory") == 3, "osrs239: inventory is tab 3");
    TEST_ASSERT(RevConfigRefs_Get(&refs, "tab", "combat") == 0, "osrs239: combat is tab 0");
    TEST_ASSERT(RevConfigRefs_Get(&refs, "tab", "music") == 13, "osrs239: music is tab 13");
    TEST_ASSERT(
        RevConfigRefs_Get(&refs, "tabdetach", "164:logout") == 1,
        "osrs239: 164 hangs logout off the top bar");
    TEST_ASSERT(
        RevConfigRefs_Get(&refs, "tabcol", "601:stats") == 1,
        "osrs239: 601 puts stats in the outer column");
    TEST_ASSERT(
        RevConfigRefs_Get(&refs, "varbit", "sidebar_flash_tab") == 3756,
        "osrs239: the flash varbit is declared");
    TEST_ASSERT(
        RevConfigRefs_Get(&refs, "varp", "special_attack_armed") == 301,
        "osrs239: the spec toggle is declared rather than derived");
    TEST_ASSERT(
        RevConfigRefs_Get(&refs, "iface", "stat_boosts") == 708, "osrs239: the stat-boosts HUD");
    RevConfigRefs_Free(&refs);

    RevConfigRefs_Init(&refs);
    snprintf(path, sizeof(path), "%s/revconfig/rs245_2lc/rs245_2lc_dat1_ui.ini", root);
    RevConfigRefs_LoadSources(&refs, path, NULL, NULL);
    TEST_ASSERT(RevConfigRefs_Get(&refs, "tab", "inventory") == 3, "rs245: inventory is tab 3 too");
    TEST_ASSERT(RevConfigRefs_Get(&refs, "tab", "combat") == 0, "rs245: combat is tab 0 too");
    TEST_ASSERT(RevConfigRefs_Get(&refs, "tab", "music") == 13, "rs245: music is tab 13 too");
    /* This revision has no tab 7 and no clan tab; absent, not zero. */
    TEST_ASSERT(RevConfigRefs_Get(&refs, "tab", "clan") == -1, "rs245 has no clan tab");
    RevConfigRefs_Free(&refs);

    RevConfigRefs_Init(&refs);
    snprintf(path, sizeof(path), "%s/revconfig/rs245_2lc/rs245_2lc_dat1_cache.ini", root);
    RevConfigRefs_LoadSources(&refs, NULL, path, NULL);
    TEST_ASSERT(RevConfigRefs_Get(&refs, "varp", "run_mode") == 173, "rs245: the run varp");
    TEST_ASSERT(
        RevConfigRefs_Get(&refs, "varp", "special_attack_energy") == 300, "rs245: the spec varp");
    TEST_ASSERT(RevConfigRefs_Get(&refs, "iface", "orb_run_on") == 153, "rs245: the run stone");
    TEST_ASSERT(RevConfigRefs_Get(&refs, "iface", "orb_run_off") == 152, "rs245: the walk stone");
    /* A dat1 cache has no varbit table; every name has to answer -1. */
    TEST_ASSERT(
        RevConfigRefs_Get(&refs, "varbit", "ground_items_enabled") == -1,
        "rs245 declares no varbits");
    RevConfigRefs_Free(&refs);
}

void
test_tabs(void)
{
    printf("TEST: sidebar tabs\n");
    test_tabs_base_map();
    test_tabs_per_root();
    test_tabs_columns_on_one_line();
    test_tabs_dat1_fallback();
    test_tabs_explicit_beats_derived();
    test_shipped_profiles();
    test_shipped_tab_maps();
}
