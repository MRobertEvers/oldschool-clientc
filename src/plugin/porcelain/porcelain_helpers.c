/*
 * Porcelain's shared helpers: the things the per-plugin ledger proved every
 * plugin needed and every plugin implemented differently.
 *
 * Each one exists because a specific pair of shipped plugins disagreed about
 * it. The tier operator is the clearest: RuneLite is strictly greater at all
 * three of its sites and skips a tier whose threshold is at or below zero;
 * loot-beam used >= and ground-items used >, and neither had the zero gate,
 * so a zero threshold meant "everything qualifies" here and "this tier is
 * off" in the reference. One helper, one operator, one gate.
 */

#include "plugin/porcelain/porcelain_internal.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Capabilities                                                             */
/* ------------------------------------------------------------------------ */

bool
Porcelain_Has(struct Porcelain* porcelain, char const* capability)
{
    assert(porcelain);
    assert(capability);
    porcelain->counters.engine_calls++;
    return porcelain->api->core.capability(porcelain->api, capability);
}

bool
Porcelain_Require(struct Porcelain* porcelain, char const* capability, char const* feature)
{
    assert(porcelain);
    assert(capability);
    assert(feature);
    if( Porcelain_Has(porcelain, capability) )
        return true;
    /* The feature turns itself off and SAYS SO. A capability that answers
     * false and a feature that silently never runs are the same picture, and
     * the record says the second one cost weeks. */
    Porcelain_RecordFinding(porcelain, "require", PORCELAIN_EL(NONE),
                            PORCELAIN_FINDING_UNSUPPORTED, feature);
    return false;
}

/* ------------------------------------------------------------------------ */
/* Tiers                                                                    */
/* ------------------------------------------------------------------------ */

int
Porcelain_Tier(struct PorcelainTiers const* tiers, int64_t value)
{
    assert(tiers);
    /* Walked from the top, strictly greater: an item worth exactly the low
     * threshold is NOT a low-value item. A threshold at or below zero
     * disables its tier rather than matching everything. */
    if( tiers->insane > 0 && value > tiers->insane )
        return PORCELAIN_TIER_INSANE;
    if( tiers->high > 0 && value > tiers->high )
        return PORCELAIN_TIER_HIGH;
    if( tiers->medium > 0 && value > tiers->medium )
        return PORCELAIN_TIER_MEDIUM;
    if( tiers->low > 0 && value > tiers->low )
        return PORCELAIN_TIER_LOW;
    return PORCELAIN_TIER_NONE;
}

bool
Porcelain_TiersFromConfig(struct Porcelain* porcelain, struct PorcelainTiers* out)
{
    static char const* const KEYS[4] = {"low_value", "medium_value", "high_value",
                                        "insane_value"};
    int64_t values[4] = {0, 0, 0, 0};
    bool any = false;

    assert(porcelain);
    assert(out);
    for( int i = 0; i < 4; i++ )
    {
        int value = 0;
        porcelain->counters.engine_calls++;
        /* The CALLER's own keys. A cross-plugin config read would make one
         * plugin's disable change the other's picture, and the two plugins
         * that share these nine spellings share them on purpose, never by
         * reading each other. */
        if( porcelain->api->config.get_int(porcelain->api, KEYS[i], &value) )
        {
            values[i] = value;
            any = true;
        }
    }
    out->low = values[0];
    out->medium = values[1];
    out->high = values[2];
    out->insane = values[3];
    return any;
}

/* ------------------------------------------------------------------------ */
/* Config lists                                                             */
/* ------------------------------------------------------------------------ */

static bool
porcelain_list_contains(char const* list, char const* item)
{
    size_t const length = strlen(item);
    char const* cursor = list;

    while( cursor && *cursor )
    {
        char const* comma = strchr(cursor, ',');
        size_t span = comma ? (size_t)(comma - cursor) : strlen(cursor);
        char const* start = cursor;
        while( span > 0 && *start == ' ' )
        {
            start++;
            span--;
        }
        while( span > 0 && start[span - 1] == ' ' )
            span--;
        if( span == length )
        {
            size_t i = 0;
            for( ; i < span; i++ )
            {
                char const a = start[i] >= 'A' && start[i] <= 'Z' ? (char)(start[i] + 32) : start[i];
                char const b = item[i] >= 'A' && item[i] <= 'Z' ? (char)(item[i] + 32) : item[i];
                if( a != b )
                    break;
            }
            if( i == span )
                return true;
        }
        cursor = comma ? comma + 1 : NULL;
    }
    return false;
}

/*
 * The three list verbs share ONE model of what a stored list is: the parsed
 * items, sorted and case-insensitively deduplicated. Add used to join
 * `current + "," + item`, which is the same list only when the caller adds in
 * order, and there was no removal verb at all -- so the half of the ledger
 * row that takes a species OUT of a tag list was a raw config.set with the
 * plugin's own join, which is the shape config_list_add exists to stop.
 */

static char
porcelain_fold(char value)
{
    return value >= 'A' && value <= 'Z' ? (char)(value + 32) : value;
}

static int
porcelain_ci_compare(char const* left, char const* right)
{
    for( size_t at = 0;; at++ )
    {
        char const lhs = porcelain_fold(left[at]);
        char const rhs = porcelain_fold(right[at]);
        if( lhs != rhs )
            return lhs < rhs ? -1 : 1;
        if( !lhs )
            return 0;
    }
}

/** The parsed form. `storage` holds the items NUL-separated; `items` points
 *  into it. Nothing here allocates: a reconciler that allocates per frame is
 *  the 2 MB the audit measured. */
struct PorcelainConfigList
{
    char storage[PORCELAIN_CONFIG_VALUE_MAX];
    char const* items[PORCELAIN_CONFIG_LIST_MAX];
    int count;
    /** More items than the table holds. The whole operation is refused:
     *  storing the prefix would DELETE the rest of the person's list. */
    bool overflow;
};

static void
porcelain_list_parse(struct PorcelainConfigList* list, char const* text)
{
    size_t written = 0;

    memset(list, 0, sizeof(*list));
    while( text && *text )
    {
        char const* comma = strchr(text, ',');
        size_t span = comma ? (size_t)(comma - text) : strlen(text);
        char const* start = text;
        bool duplicate = false;

        text = comma ? comma + 1 : NULL;
        while( span > 0 && *start == ' ' )
        {
            start++;
            span--;
        }
        while( span > 0 && start[span - 1] == ' ' )
            span--;
        if( span == 0 )
            continue;
        if( written + span + 1 > sizeof(list->storage) || list->count >= PORCELAIN_CONFIG_LIST_MAX )
        {
            list->overflow = true;
            return;
        }
        memcpy(list->storage + written, start, span);
        list->storage[written + span] = '\0';
        for( int at = 0; at < list->count; at++ )
            if( porcelain_ci_compare(list->items[at], list->storage + written) == 0 )
                duplicate = true;
        if( duplicate )
            continue;
        list->items[list->count++] = list->storage + written;
        written += span + 1;
    }
}

static void
porcelain_list_sort(struct PorcelainConfigList* list)
{
    for( int at = 1; at < list->count; at++ )
    {
        char const* const held = list->items[at];
        int back = at - 1;
        while( back >= 0 && porcelain_ci_compare(list->items[back], held) > 0 )
        {
            list->items[back + 1] = list->items[back];
            back--;
        }
        list->items[back + 1] = held;
    }
}

/*
 * Sort, MEASURE, join, store.
 *
 * Measuring before joining is the rule that must not move: the host's own
 * setter snprintf-truncates and its validator then accepts the fragment, so a
 * list cut mid-name stores a WRONG species and reads back as one. Over the
 * ceiling the stored list is left byte-for-byte as it was.
 */
static bool
porcelain_list_store(struct Porcelain* porcelain, char const* verb, char const* key,
                     struct PorcelainConfigList* list)
{
    char joined[PORCELAIN_CONFIG_VALUE_MAX];
    size_t needed = 1;
    size_t written = 0;

    if( list->overflow )
    {
        Porcelain_RecordFinding(porcelain, verb, PORCELAIN_EL(NONE), PORCELAIN_FINDING_BUDGET, key);
        return false;
    }
    porcelain_list_sort(list);
    for( int at = 0; at < list->count; at++ )
        needed += strlen(list->items[at]) + (at ? 1 : 0);
    if( needed > sizeof(joined) )
    {
        Porcelain_RecordFinding(porcelain, verb, PORCELAIN_EL(NONE), PORCELAIN_FINDING_BUDGET, key);
        return false;
    }
    for( int at = 0; at < list->count; at++ )
    {
        size_t const length = strlen(list->items[at]);
        if( at )
            joined[written++] = ',';
        memcpy(joined + written, list->items[at], length);
        written += length;
    }
    joined[written] = '\0';
    porcelain->counters.engine_calls++;
    if( porcelain->api->config.set(porcelain->api, key, joined) != TORIRS_RESULT_OK )
    {
        Porcelain_RecordFinding(porcelain, verb, PORCELAIN_EL(NONE), PORCELAIN_FINDING_REFUSED,
                                key);
        return false;
    }
    return true;
}

static char const*
porcelain_list_read(struct Porcelain* porcelain, char const* key)
{
    char const* current = NULL;

    porcelain->counters.engine_calls++;
    if( !porcelain->api->config.get_string(porcelain->api, key, &current) )
        return NULL;
    return current;
}

bool
Porcelain_ConfigListAdd(struct Porcelain* porcelain, char const* key, char const* item)
{
    struct PorcelainConfigList list;
    char const* current;
    size_t const length = strlen(item);

    assert(porcelain);
    assert(key);
    assert(item);
    assert(item[0]);

    current = porcelain_list_read(porcelain, key);
    if( current && porcelain_list_contains(current, item) )
        return true;
    porcelain_list_parse(&list, current);
    if( !list.overflow )
    {
        /* The item's own bytes go at the END of the storage, past everything
         * the parse wrote, so the pointers already handed out stay valid. */
        size_t used = 0;
        for( int at = 0; at < list.count; at++ )
            used += strlen(list.items[at]) + 1;
        if( used + length + 1 > sizeof(list.storage) || list.count >= PORCELAIN_CONFIG_LIST_MAX )
        {
            list.overflow = true;
        }
        else
        {
            memcpy(list.storage + used, item, length + 1);
            list.items[list.count++] = list.storage + used;
        }
    }
    return porcelain_list_store(porcelain, "config_list_add", key, &list);
}

/*
 * The other half of the row. Absent is TRUE and costs no write: "this species
 * is not tagged" is the state the caller asked for, and a set that stores the
 * same string is a config write, a save and a reconcile for nothing.
 */
bool
Porcelain_ConfigListRemove(struct Porcelain* porcelain, char const* key, char const* item)
{
    struct PorcelainConfigList list;
    char const* current;
    int kept = 0;

    assert(porcelain);
    assert(key);
    assert(item);
    assert(item[0]);

    current = porcelain_list_read(porcelain, key);
    if( !current || !porcelain_list_contains(current, item) )
        return true;
    porcelain_list_parse(&list, current);
    for( int at = 0; at < list.count; at++ )
        if( porcelain_ci_compare(list.items[at], item) != 0 )
            list.items[kept++] = list.items[at];
    list.count = kept;
    return porcelain_list_store(porcelain, "config_list_remove", key, &list);
}

/*
 * The whole list at once, for a caller that HAS the set -- a settings page
 * writing back every ticked row. Without it the only way to state a list was
 * add-in-a-loop, which cannot express a removal at all and writes the config
 * once per item.
 */
bool
Porcelain_ConfigListSet(struct Porcelain* porcelain, char const* key, char const* const* items,
                        int count)
{
    struct PorcelainConfigList list;
    size_t written = 0;

    assert(porcelain);
    assert(key);
    assert(count >= 0);

    memset(&list, 0, sizeof(list));
    /* An empty list is a documented state -- "nothing is tagged" -- and the
     * caller says it with a NULL array, so the assert comes after. */
    if( count == 0 )
        return porcelain_list_store(porcelain, "config_list_set", key, &list);
    assert(items);
    for( int at = 0; at < count; at++ )
    {
        size_t length;
        bool duplicate = false;

        assert(items[at]);
        length = strlen(items[at]);
        if( length == 0 )
            continue;
        if( written + length + 1 > sizeof(list.storage) || list.count >= PORCELAIN_CONFIG_LIST_MAX )
        {
            list.overflow = true;
            break;
        }
        memcpy(list.storage + written, items[at], length + 1);
        for( int seen = 0; seen < list.count; seen++ )
            if( porcelain_ci_compare(list.items[seen], list.storage + written) == 0 )
                duplicate = true;
        if( duplicate )
            continue;
        list.items[list.count++] = list.storage + written;
        written += length + 1;
    }
    return porcelain_list_store(porcelain, "config_list_set", key, &list);
}

/* ------------------------------------------------------------------------ */
/* Menu tags                                                                */
/* ------------------------------------------------------------------------ */

uint32_t
Porcelain_MenuTag(int subject, int op)
{
    /*
     * Subject and intended operation both frozen at build time. The server
     * may reuse a slot while the menu is open, and re-resolving state at
     * select time is exactly what made a retained Tag row retarget a
     * different species.
     *
     * Sixteen operations per subject, not the two and four the two shipped
     * plugins each picked, so one encoding serves both and a third plugin
     * does not need a fifth.
     */
    assert(subject >= 0);
    assert(op >= 0);
    assert(op < PORCELAIN_MENU_TAG_OPS);
    return (uint32_t)subject * (uint32_t)PORCELAIN_MENU_TAG_OPS + (uint32_t)op;
}

/*
 * The inverse, which did not exist.
 *
 * A verb that only encodes leaves the plugin owning half the encoding: every
 * consumer wrote `tag / 16` and `tag % 16` with the 16 spelled out, so
 * raising PORCELAIN_MENU_TAG_OPS would silently re-target every retained menu
 * row in every shipped plugin -- a Tag row that fires a different operation
 * on a different subject, with nothing in the build that says so. With the
 * decoder here the constant is the library's alone.
 */
void
Porcelain_MenuUntag(uint32_t tag, int* out_subject, int* out_op)
{
    assert(out_subject);
    assert(out_op);
    *out_subject = (int)(tag / (uint32_t)PORCELAIN_MENU_TAG_OPS);
    *out_op = (int)(tag % (uint32_t)PORCELAIN_MENU_TAG_OPS);
}

/* ------------------------------------------------------------------------ */
/* Settings                                                                 */
/* ------------------------------------------------------------------------ */

bool
Porcelain_Setting(struct Porcelain* porcelain, char const* varbit_name, unsigned flags)
{
    int id = -1;
    int value;

    assert(porcelain);
    assert(varbit_name);

    /*
     * ONE lookup, not two. `varbit:<name>` as a capability and `named_id`
     * over the same name are the same profile row read twice; asking the
     * capability first would be an engine call that could never change the
     * answer, and a rule no test could turn red.
     *
     * Absent is OFF, with ONE finding across many reads -- the finding table
     * coalesces on (verb, element, result), so a per-row read does not flood.
     * A multiplier read off a var that does not exist would be a silent 1.0.
     */
    porcelain->counters.engine_calls++;
    if( !porcelain->api->cache.named_id(porcelain->api, "varbit", varbit_name, &id) || id < 0 )
    {
        Porcelain_RecordFinding(porcelain, "setting", PORCELAIN_ROLE_EL(varbit_name),
                                PORCELAIN_FINDING_ABSENT, varbit_name);
        return false;
    }
    porcelain->counters.engine_calls++;
    value = porcelain->api->cache.varbit(porcelain->api, id);
    if( flags & PORCELAIN_SETTING_INVERTED )
        return value == 0;
    return value != 0;
}

/* ------------------------------------------------------------------------ */
/* Key edges                                                                */
/* ------------------------------------------------------------------------ */

/*
 * The three modifiers a reveal key can be, and "off". Nothing else is
 * offered: LibToriRS_KeyCode carries the modifiers a plugin gates on, and a
 * larger table here would be a second copy of the client's keymap to keep
 * true.
 */
static int
porcelain_key_code(char const* name)
{
    if( !name || !name[0] || strcmp(name, "off") == 0 || strcmp(name, "none") == 0 )
        return -1;
    if( strcmp(name, "shift") == 0 )
        return TORIRS_KEY_SHIFT;
    if( strcmp(name, "ctrl") == 0 || strcmp(name, "control") == 0 )
        return TORIRS_KEY_CTRL;
    if( strcmp(name, "escape") == 0 )
        return TORIRS_KEY_ESCAPE;
    if( strcmp(name, "tab") == 0 )
        return TORIRS_KEY_TAB;
    if( strcmp(name, "space") == 0 )
        return TORIRS_KEY_SPACE;
    /*
     * A key is a NUMBER, and the five names above are a convenience, not the
     * vocabulary. A hotkey a user can rebind to any key was unreachable while
     * this understood five strings and nothing else -- the shipped screenshot
     * hotkey is an arbitrary code, so using this verb would have changed its
     * behaviour and emitted an unexpected finding on every run.
     */
    {
        char* end = NULL;
        long const value = strtol(name, &end, 10);
        if( end != name && *end == '\0' && value > 0 && value < (1 << 30) )
            return (int)value;
    }
    if( name[1] == '\0' )
        return (unsigned char)name[0];
    return -1;
}

bool
Porcelain_KeyEdge(struct Porcelain* porcelain, char const* config_key, PorcelainEdgeFn fn,
                  void* user)
{
    assert(porcelain);
    assert(config_key);
    assert(fn);

    /* input.key_held can never be true on a touch lane -- there is no
     * keyboard frame -- so a feature gated on one is silently unreachable
     * there. ABSENT with one finding is the honest answer. */
    if( Porcelain_Has(porcelain, "touch") )
    {
        Porcelain_RecordFinding(porcelain, "key_edge", PORCELAIN_ROLE_EL(config_key),
                                PORCELAIN_FINDING_ABSENT, "touch lane has no key");
        return false;
    }
    for( int i = 0; i < PORCELAIN_KEY_EDGES_MAX; i++ )
    {
        struct PorcelainKeyEdgeWatch* watch = &porcelain->key_edges[i];
        if( watch->used )
            continue;
        memset(watch, 0, sizeof(*watch));
        watch->used = true;
        Porcelain_CopyString(watch->config_key, sizeof(watch->config_key), config_key);
        watch->fn = fn;
        watch->user = user;
        watch->code = -1;
        return true;
    }
    Porcelain_RecordFinding(porcelain, "key_edge", PORCELAIN_ROLE_EL(config_key),
                            PORCELAIN_FINDING_BUDGET, "key edge table full");
    return false;
}

/*
 * "Is this key down RIGHT NOW", asked once, where the question is asked.
 *
 * The edge form is the wrong shape for a question asked at a right-click: to
 * use it a plugin had to declare a config key it never wanted, fence every
 * frame, and mirror the edge into a boolean of its own -- up to two engine
 * calls every frame to answer something asked once per click. This is one
 * engine call at the click and no state at all.
 *
 * `key` is the same vocabulary the edge's CONFIG VALUE uses: a name, a
 * decimal code, or a single character. A name that resolves to nothing is a
 * finding and false, never a silent "not held" -- "the modifier is up" and
 * "there is no such key" look identical to a caller and mean opposite things.
 */
bool
Porcelain_KeyDown(struct Porcelain* porcelain, char const* key)
{
    int code;

    assert(porcelain);
    assert(key);

    /* Same refusal as the edge form, and for the same reason: there is no
     * keyboard frame on a touch lane, so key_held can never be true there and
     * a feature gated on one is silently unreachable. */
    if( Porcelain_Has(porcelain, "touch") )
    {
        Porcelain_RecordFinding(porcelain, "key_down", PORCELAIN_ROLE_EL(key),
                                PORCELAIN_FINDING_ABSENT, "touch lane has no key");
        return false;
    }
    code = porcelain_key_code(key);
    if( code < 0 )
    {
        Porcelain_RecordFinding(porcelain, "key_down", PORCELAIN_ROLE_EL(key),
                                PORCELAIN_FINDING_ABSENT, key);
        return false;
    }
    porcelain->counters.engine_calls++;
    return porcelain->api->input.key_held(porcelain->api, code);
}

void
Porcelain_NoteKey(struct Porcelain* porcelain, int key, bool down)
{
    assert(porcelain);

    /*
     * The EDGE, from the press. The fence used to poll input.key_held, which
     * costs an engine call per watch per frame and cannot see a key that goes
     * down and up inside one frame -- so a tap was a hotkey that sometimes
     * did nothing.
     */
    for( int i = 0; i < PORCELAIN_KEY_EDGES_MAX; i++ )
    {
        struct PorcelainKeyEdgeWatch* watch = &porcelain->key_edges[i];
        if( !watch->used || watch->code < 0 || watch->code != key )
            continue;
        if( down == watch->down )
            continue;
        watch->down = down;
        watch->fn(porcelain->api, watch->user, down);
    }
}

/*
 * A limitation this lane has, said out loud.
 *
 * Every UNSUPPORTED finding was expected=0, so a plugin that declared a real
 * lane limitation FAILED the clean gate for declaring it, and going quiet was
 * the only way to pass. The declaration is itself one expected finding -- the
 * point is that the limitation is visible -- and it marks every later
 * UNSUPPORTED finding that names the same feature, whichever route recorded
 * it.
 */
void
Porcelain_ExpectUnsupported(struct Porcelain* porcelain, char const* feature, char const* why)
{
    assert(porcelain);
    assert(feature);
    assert(why);

    for( int i = 0; i < PORCELAIN_EXPECT_UNSUPPORTED_MAX; i++ )
    {
        struct PorcelainExpectUnsupported* slot = &porcelain->expect_unsupported[i];
        if( slot->used && strcmp(slot->feature, feature) == 0 )
            return;
        if( slot->used )
            continue;
        memset(slot, 0, sizeof(*slot));
        slot->used = true;
        Porcelain_CopyString(slot->feature, sizeof(slot->feature), feature);
        Porcelain_CopyString(slot->why, sizeof(slot->why), why);
        Porcelain_RecordFinding(porcelain, "unsupported", PORCELAIN_ROLE_EL(feature),
                                PORCELAIN_FINDING_UNSUPPORTED, feature);
        return;
    }
    Porcelain_RecordFinding(porcelain, "expect_unsupported", PORCELAIN_ROLE_EL(feature),
                            PORCELAIN_FINDING_BUDGET, "declaration table full");
}

/* ------------------------------------------------------------------------ */
/* Images, models and derived images                                        */
/* ------------------------------------------------------------------------ */

static enum PorcelainAssetState
porcelain_map_asset_state(enum ToriRS_AssetState state)
{
    switch( state )
    {
    case TORIRS_ASSET_READY:
        return PORCELAIN_ASSET_READY;
    case TORIRS_ASSET_PENDING:
        return PORCELAIN_ASSET_PENDING;
    case TORIRS_ASSET_MISSING:
        return PORCELAIN_ASSET_MISSING;
    default:
        break;
    }
    /* INVALID, BUDGET and ERROR are all terminal and all mean the picture
     * will never arrive. The budget one is the interesting case: over the
     * ceiling the host returns -1 with one log line and the plugin's refresh
     * silently returns early, so the orb simply never appears. */
    return PORCELAIN_ASSET_ERROR;
}

static void
porcelain_report_terminal_asset(struct Porcelain* porcelain, char const* verb, char const* name,
                                enum PorcelainAssetState state, bool* reported)
{
    if( state != PORCELAIN_ASSET_MISSING && state != PORCELAIN_ASSET_ERROR )
        return;
    if( *reported )
        return;
    *reported = true;
    Porcelain_RecordFinding(porcelain, verb, PORCELAIN_ROLE_EL(name),
                            state == PORCELAIN_ASSET_MISSING ? PORCELAIN_FINDING_ASSET_MISSING
                                                             : PORCELAIN_FINDING_ASSET_ERROR,
                            name);
}

struct ToriRS_ImageRef
Porcelain_Image(struct Porcelain* porcelain, char const* name, enum PorcelainAssetState* out_state)
{
    struct PorcelainImageSlot* free_slot = NULL;
    struct ToriRS_ImageRef ref;

    assert(porcelain);
    assert(name);
    assert(out_state);
    memset(&ref, 0, sizeof(ref));

    for( int i = 0; i < PORCELAIN_IMAGES_MAX; i++ )
    {
        struct PorcelainImageSlot* slot = &porcelain->images[i];
        if( !slot->used )
        {
            if( !free_slot )
                free_slot = slot;
            continue;
        }
        if( strcmp(slot->name.text, name) != 0 )
            continue;
        slot->last_used_run = porcelain->run;
        /* A terminal state is REMEMBERED. Re-asking every frame is how a
         * missing model became "not asked" and was re-asked for ever with no
         * line anywhere. */
        if( slot->state == PORCELAIN_ASSET_PENDING )
        {
            porcelain->counters.engine_calls++;
            slot->state = porcelain_map_asset_state(
                porcelain->api->assets.image(porcelain->api, name, &slot->ref));
            if( slot->state != PORCELAIN_ASSET_PENDING )
                porcelain->stamp[PORCELAIN_INPUT_ASSET]++;
            porcelain_report_terminal_asset(porcelain, "image", name, slot->state,
                                            &slot->terminal_reported);
        }
        *out_state = slot->state;
        return slot->ref;
    }
    if( !free_slot )
    {
        Porcelain_RecordFinding(porcelain, "image", PORCELAIN_ROLE_EL(name),
                                PORCELAIN_FINDING_BUDGET, name);
        *out_state = PORCELAIN_ASSET_ERROR;
        return ref;
    }
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->used = true;
    Porcelain_CopyString(free_slot->name.text, sizeof(free_slot->name.text), name);
    free_slot->last_used_run = porcelain->run;
    porcelain->counters.engine_calls++;
    free_slot->state = porcelain_map_asset_state(
        porcelain->api->assets.image(porcelain->api, name, &free_slot->ref));
    porcelain_report_terminal_asset(porcelain, "image", name, free_slot->state,
                                    &free_slot->terminal_reported);
    *out_state = free_slot->state;
    return free_slot->ref;
}

/*
 * The picture's own size.
 *
 * A control that wants to be as big as its picture had to reach past this
 * layer to api->assets.image_size, on the handle this layer had just handed
 * it -- the one call that kept the shipped screenshot port's api.assets count
 * off zero. The size is only knowable once the asset is READY, so this
 * answers false until then and the item falls back to what it was given.
 */
bool
Porcelain_ImageSize(struct Porcelain* porcelain, char const* name, int* out_width,
                    int* out_height)
{
    enum PorcelainAssetState state = PORCELAIN_ASSET_READY;
    struct ToriRS_ImageRef ref;

    assert(porcelain);
    assert(name);
    assert(out_width);
    assert(out_height);

    *out_width = 0;
    *out_height = 0;
    ref = Porcelain_Image(porcelain, name, &state);
    if( state != PORCELAIN_ASSET_READY )
        return false;
    porcelain->counters.engine_calls++;
    if( !porcelain->api->assets.image_size(porcelain->api, ref, out_width, out_height) )
    {
        *out_width = 0;
        *out_height = 0;
        return false;
    }
    return true;
}

struct ToriRS_ModelRef
Porcelain_Model(struct Porcelain* porcelain, char const* name, enum PorcelainAssetState* out_state)
{
    struct PorcelainModelSlot* free_slot = NULL;
    struct ToriRS_ModelRef ref;

    assert(porcelain);
    assert(name);
    assert(out_state);
    memset(&ref, 0, sizeof(ref));

    for( int i = 0; i < PORCELAIN_MODELS_MAX; i++ )
    {
        struct PorcelainModelSlot* slot = &porcelain->models[i];
        if( !slot->used )
        {
            if( !free_slot )
                free_slot = slot;
            continue;
        }
        if( strcmp(slot->name.text, name) != 0 )
            continue;
        if( slot->state == PORCELAIN_ASSET_PENDING )
        {
            porcelain->counters.engine_calls++;
            slot->state = porcelain_map_asset_state(
                porcelain->api->assets.model(porcelain->api, name, &slot->ref));
            if( slot->state != PORCELAIN_ASSET_PENDING )
                porcelain->stamp[PORCELAIN_INPUT_ASSET]++;
            porcelain_report_terminal_asset(porcelain, "model", name, slot->state,
                                            &slot->terminal_reported);
        }
        *out_state = slot->state;
        return slot->ref;
    }
    if( !free_slot )
    {
        Porcelain_RecordFinding(porcelain, "model", PORCELAIN_ROLE_EL(name),
                                PORCELAIN_FINDING_BUDGET, name);
        *out_state = PORCELAIN_ASSET_ERROR;
        return ref;
    }
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->used = true;
    Porcelain_CopyString(free_slot->name.text, sizeof(free_slot->name.text), name);
    porcelain->counters.engine_calls++;
    free_slot->state = porcelain_map_asset_state(
        porcelain->api->assets.model(porcelain->api, name, &free_slot->ref));
    porcelain_report_terminal_asset(porcelain, "model", name, free_slot->state,
                                    &free_slot->terminal_reported);
    *out_state = free_slot->state;
    return free_slot->ref;
}

struct ToriRS_ImageRef
Porcelain_Derived(struct Porcelain* porcelain, char const* key, void const* inputs,
                  size_t inputs_len, int width, int height, PorcelainPaintFn paint, void* user,
                  enum PorcelainDerivedState* out_state)
{
    uint64_t const hash = Porcelain_HashBytes(Porcelain_HashString(0, key), inputs, inputs_len);
    struct PorcelainDerivedSlot* free_slot = NULL;
    struct PorcelainDerivedSlot* slot = NULL;
    struct ToriRS_ImageRef ref;
    uint32_t* pixels;

    assert(porcelain);
    assert(key);
    assert(paint);
    assert(out_state);
    assert(width > 0);
    assert(height > 0);
    assert(inputs || inputs_len == 0);
    memset(&ref, 0, sizeof(ref));

    for( int i = 0; i < PORCELAIN_DERIVED_MAX; i++ )
    {
        if( !porcelain->derived[i].used )
        {
            if( !free_slot )
                free_slot = &porcelain->derived[i];
            continue;
        }
        if( strcmp(porcelain->derived[i].key.text, key) == 0 )
        {
            slot = &porcelain->derived[i];
            break;
        }
    }
    if( slot && slot->inputs_hash == hash && slot->width == width && slot->height == height )
    {
        /* Painted at most once per (key, hash of inputs). The key never
         * includes a host revision: an icon_revision that bumps on every miss
         * means a derived hash that never settles. */
        *out_state = slot->state;
        return slot->ref;
    }
    if( !slot )
    {
        if( !free_slot )
        {
            Porcelain_RecordFinding(porcelain, "derived", PORCELAIN_ROLE_EL(key),
                                    PORCELAIN_FINDING_BUDGET, key);
            *out_state = PORCELAIN_DERIVED_FAILED;
            return ref;
        }
        slot = free_slot;
        memset(slot, 0, sizeof(*slot));
        slot->used = true;
        Porcelain_CopyString(slot->key.text, sizeof(slot->key.text), key);
    }
    if( slot->state == PORCELAIN_DERIVED_FAILED && slot->inputs_hash == hash )
    {
        /* FAILED is terminal for these inputs. Retrying for ever is how the
         * masks that never built left a square minimap in a round window. */
        *out_state = PORCELAIN_DERIVED_FAILED;
        return slot->ref;
    }

    pixels = malloc((size_t)width * (size_t)height * sizeof(*pixels));
    assert(pixels);
    porcelain->counters.allocations++;
    memset(pixels, 0, (size_t)width * (size_t)height * sizeof(*pixels));
    slot->inputs_hash = hash;
    slot->width = width;
    slot->height = height;
    if( !paint(porcelain->api, user, pixels, width, height) )
    {
        free(pixels);
        slot->state = PORCELAIN_DERIVED_FAILED;
        if( !slot->terminal_reported )
        {
            slot->terminal_reported = true;
            Porcelain_RecordFinding(porcelain, "derived", PORCELAIN_ROLE_EL(key),
                                    PORCELAIN_FINDING_DERIVED_FAILED, key);
        }
        *out_state = PORCELAIN_DERIVED_FAILED;
        return slot->ref;
    }
    porcelain->counters.engine_calls++;
    {
        enum ToriRS_AssetState const state =
            porcelain->api->assets.image_compose(porcelain->api, key, width, height, pixels,
                                                 &slot->ref);
        free(pixels);
        if( state == TORIRS_ASSET_READY )
            slot->state = PORCELAIN_DERIVED_READY;
        else if( state == TORIRS_ASSET_PENDING )
            slot->state = PORCELAIN_DERIVED_PENDING;
        else
        {
            slot->state = PORCELAIN_DERIVED_FAILED;
            if( !slot->terminal_reported )
            {
                slot->terminal_reported = true;
                Porcelain_RecordFinding(porcelain, "derived", PORCELAIN_ROLE_EL(key),
                                        PORCELAIN_FINDING_DERIVED_FAILED, key);
            }
        }
    }
    porcelain->stamp[PORCELAIN_INPUT_ASSET]++;
    *out_state = slot->state;
    return slot->ref;
}

void
Porcelain_ImageTouch(struct Porcelain* porcelain, char const* name)
{
    assert(porcelain);
    assert(name);
    for( int i = 0; i < PORCELAIN_IMAGES_MAX; i++ )
        if( porcelain->images[i].used && strcmp(porcelain->images[i].name.text, name) == 0 )
        {
            porcelain->images[i].last_used_run = porcelain->run;
            return;
        }
}

void
Porcelain_ReleaseAllAssets(struct Porcelain* porcelain)
{
    assert(porcelain);
    for( int i = 0; i < PORCELAIN_IMAGES_MAX; i++ )
        if( porcelain->images[i].used && porcelain->images[i].state == PORCELAIN_ASSET_READY )
        {
            porcelain->counters.engine_calls++;
            porcelain->api->assets.image_release(porcelain->api, porcelain->images[i].ref);
            memset(&porcelain->images[i], 0, sizeof(porcelain->images[i]));
        }
    for( int i = 0; i < PORCELAIN_MODELS_MAX; i++ )
        if( porcelain->models[i].used && porcelain->models[i].state == PORCELAIN_ASSET_READY )
        {
            porcelain->counters.engine_calls++;
            porcelain->api->assets.model_release(porcelain->api, porcelain->models[i].ref);
            memset(&porcelain->models[i], 0, sizeof(porcelain->models[i]));
        }
    for( int i = 0; i < PORCELAIN_DERIVED_MAX; i++ )
        if( porcelain->derived[i].used && porcelain->derived[i].state == PORCELAIN_DERIVED_READY )
        {
            porcelain->counters.engine_calls++;
            porcelain->api->assets.image_release(porcelain->api, porcelain->derived[i].ref);
            memset(&porcelain->derived[i], 0, sizeof(porcelain->derived[i]));
        }
}

/* ------------------------------------------------------------------------ */
/* Readiness and cadence                                                    */
/* ------------------------------------------------------------------------ */

void
Porcelain_WhenReady(struct Porcelain* porcelain, unsigned what, PorcelainReadyFn fn, void* user)
{
    assert(porcelain);
    assert(fn);
    for( int i = 0; i < PORCELAIN_READY_MAX; i++ )
    {
        struct PorcelainReadyWatch* watch = &porcelain->ready[i];
        if( watch->used )
            continue;
        memset(watch, 0, sizeof(*watch));
        watch->used = true;
        watch->what = what;
        watch->fn = fn;
        watch->user = user;
        return;
    }
    Porcelain_RecordFinding(porcelain, "when_ready", PORCELAIN_EL(NONE), PORCELAIN_FINDING_BUDGET,
                            "ready table full");
}

/*
 * A timer is identified by its (fn, user), and registering the same pair
 * again re-states it in place.
 *
 * The table is fixed at PORCELAIN_TIMERS_MAX, and an append-only
 * registration meant a user dragging a refresh-interval slider leaked a slot
 * per change -- sixteen drags and the readout stopped, with a budget finding
 * to explain it.
 */
static struct PorcelainTimer*
porcelain_timer_slot(struct Porcelain* porcelain, PorcelainTickFn fn, void* user,
                     char const* verb)
{
    struct PorcelainTimer* free_slot = NULL;

    for( int i = 0; i < PORCELAIN_TIMERS_MAX; i++ )
    {
        struct PorcelainTimer* timer = &porcelain->timers[i];
        if( timer->used && timer->fn == fn && timer->user == user )
            return timer;
        if( !timer->used && !free_slot )
            free_slot = timer;
    }
    if( free_slot )
        return free_slot;
    Porcelain_RecordFinding(porcelain, verb, PORCELAIN_EL(NONE), PORCELAIN_FINDING_BUDGET,
                            "timer table full");
    return NULL;
}

void
Porcelain_Every(struct Porcelain* porcelain, enum PorcelainCadence cadence, PorcelainTickFn fn,
                void* user)
{
    struct PorcelainTimer* timer;

    assert(porcelain);
    assert(fn);
    assert(cadence >= 0 && cadence < PORCELAIN_CADENCE_COUNT);
    timer = porcelain_timer_slot(porcelain, fn, user, "every");
    if( !timer )
        return;
    memset(timer, 0, sizeof(*timer));
    timer->used = true;
    timer->cadence = cadence;
    timer->fn = fn;
    timer->user = user;
}

void
Porcelain_CancelEvery(struct Porcelain* porcelain, PorcelainTickFn fn, void* user)
{
    assert(porcelain);
    assert(fn);
    for( int i = 0; i < PORCELAIN_TIMERS_MAX; i++ )
    {
        struct PorcelainTimer* timer = &porcelain->timers[i];
        if( timer->used && timer->fn == fn && timer->user == user )
            memset(timer, 0, sizeof(*timer));
    }
}

void
Porcelain_EveryServerTick(struct Porcelain* porcelain, PorcelainTickFn fn, void* user)
{
    /* The server tick fires on EVERY lane now: after the end-of-tick packet
     * where the wire has it, after player info elsewhere, never both. The
     * synthesised 600 ms cadence the sketch proposed is deleted. */
    Porcelain_Every(porcelain, PORCELAIN_SERVER_TICK, fn, user);
}

void
Porcelain_EveryMs(struct Porcelain* porcelain, int milliseconds, PorcelainTickFn fn, void* user)
{
    struct PorcelainTimer* timer;

    assert(porcelain);
    assert(fn);
    assert(milliseconds > 0);
    timer = porcelain_timer_slot(porcelain, fn, user, "every_ms");
    if( !timer )
        return;
    if( timer->used && timer->is_ms )
    {
        /* A re-interval, not a second timer. The next due time moves with it
         * so a slider dragged shorter takes effect now rather than after the
         * old interval has run out. */
        timer->milliseconds = milliseconds;
        timer->next_due_ms = timer->last_fired_ms + (uint64_t)milliseconds;
        return;
    }
    memset(timer, 0, sizeof(*timer));
    timer->used = true;
    timer->is_ms = true;
    timer->milliseconds = milliseconds;
    timer->fn = fn;
    timer->user = user;
}

void
Porcelain_Tick(struct Porcelain* porcelain, enum PorcelainCadence cadence)
{
    assert(porcelain);
    assert(cadence >= 0 && cadence < PORCELAIN_CADENCE_COUNT);
    for( int i = 0; i < PORCELAIN_TIMERS_MAX; i++ )
    {
        struct PorcelainTimer* timer = &porcelain->timers[i];
        if( !timer->used || timer->is_ms || timer->cadence != cadence )
            continue;
        timer->fn(porcelain->api, timer->user, 0);
    }
}

static unsigned
porcelain_ready_bits(struct Porcelain* porcelain)
{
    unsigned bits = 0;
    struct ToriRS_PlayerSnapshot player;

    porcelain->counters.engine_calls++;
    if( porcelain->api->core.screen(porcelain->api) == TORIRS_SCREEN_GAME )
        bits |= PORCELAIN_READY_GAME;
    memset(&player, 0, sizeof(player));
    porcelain->counters.engine_calls++;
    if( porcelain->api->world.local_player(porcelain->api, &player) )
        bits |= PORCELAIN_READY_PLAYER | PORCELAIN_READY_WORLD;
    if( porcelain->api->game && porcelain->api->game->skill )
    {
        struct ToriRS_SkillSnapshot skill;
        memset(&skill, 0, sizeof(skill));
        skill.struct_size = sizeof(skill);
        porcelain->counters.engine_calls++;
        /* The STATED bit, not "the table is populated": the pre-login table
         * is a fresh account's, and a tracker that seeded itself from it took
         * the login burst for one enormous gain. */
        if( porcelain->api->game->skill(porcelain->api, 0, &skill) && skill.stated )
            bits |= PORCELAIN_READY_STATS;
    }
    {
        bool derived_ready = true;
        for( int i = 0; i < PORCELAIN_DERIVED_MAX; i++ )
            if( porcelain->derived[i].used &&
                porcelain->derived[i].state == PORCELAIN_DERIVED_PENDING )
                derived_ready = false;
        if( derived_ready )
            bits |= PORCELAIN_READY_DERIVED;
    }
    return bits;
}

/* ------------------------------------------------------------------------ */
/* The overlay verbs                                                        */
/* ------------------------------------------------------------------------ */

static struct ToriRS_Rect
porcelain_rect_of(struct ToriRS_WidgetBounds box)
{
    struct ToriRS_Rect rect;

    rect.x = (int)box.x;
    rect.y = (int)box.y;
    rect.width = (int)box.width;
    rect.height = (int)box.height;
    return rect;
}

/*
 * The drawable rect of the pass now running, and one element's box on it.
 *
 * All three passes set a region now -- the world pass was the last to get
 * one, and until it did this verb's premise was false for six of the seven
 * overlay plugins, every one of which had written its own `draw->context`
 * call and its own fallback for the false it got back.
 */
bool
Porcelain_DrawContext(struct Porcelain* porcelain, struct ToriRS_Graphics* draw,
                      struct PorcelainElement element, struct PorcelainDrawContext* out)
{
    struct ToriRS_DrawContext context;
    struct PorcelainElementState canvas;
    struct PorcelainElementState state;

    assert(porcelain);
    assert(draw);
    assert(draw->context);
    assert(out);

    memset(out, 0, sizeof(*out));
    memset(&context, 0, sizeof(context));
    context.struct_size = sizeof(context);
    porcelain->counters.engine_calls++;
    if( !draw->context(draw, &context) )
    {
        Porcelain_RecordFinding(porcelain, "draw_context", element, PORCELAIN_FINDING_REFUSED,
                                "the pass set no draw region");
        return false;
    }
    out->bounds = context.bounds;
    out->clip = context.clip;

    /*
     * A canvas-space answer is only usable where the pass IS the canvas. The
     * world and canvas passes are, at origin zero; a panel well is not, and a
     * tooltip clamped against a well's local rectangle is the retired
     * placement bug that flipped it up over the minimap. Said as data: the
     * pass is canvas space when its drawable rect is the canvas's size.
     */
    if( !Porcelain_Element(porcelain, PORCELAIN_EL(CANVAS), &canvas) )
        return true;
    out->canvas_space = context.bounds.width == (int)canvas.box.width &&
                        context.bounds.height == (int)canvas.box.height;
    if( !out->canvas_space )
        return true;
    (void)Porcelain_Element(porcelain, PORCELAIN_EL(USABLE), &state);
    out->usable = porcelain_rect_of(state.box);
    if( element.kind == PORCELAIN_EL_NONE )
        return true;
    out->element_bound = Porcelain_Element(porcelain, element, &state);
    if( out->element_bound )
        out->element = porcelain_rect_of(state.box);
    return true;
}

/*
 * menu.add, with the refusal made loud.
 *
 * The host's route table is bounded and shared; over it, `add` answers false.
 * Both shipped overlays dropped that bool -- one kept adding rows that would
 * never appear, the other broke out of its loop -- and in neither case did
 * anybody, plugin or user, learn that a row was missing.
 */
bool
Porcelain_MenuAdd(struct Porcelain* porcelain, struct ToriRS_MenuBuildEvent* menu,
                  char const* text, uint32_t action_id)
{
    assert(porcelain);
    assert(menu);
    assert(text);

    porcelain->counters.engine_calls++;
    if( porcelain->api->menu.add(porcelain->api, menu, text, action_id) )
        return true;
    Porcelain_RecordFinding(porcelain, "menu_add", PORCELAIN_ROLE_EL("menu"),
                            PORCELAIN_FINDING_REFUSED, text);
    return false;
}

/*
 * draw->world_hull, with the two refusals turned into findings.
 *
 * Both were dropped end to end until this existed: the host answered void,
 * the v2 builder answered OK unconditionally, and every shipped highlighter
 * spells the call `(void)draw->world_hull(...)` because there was nothing to
 * read. What that cost is not an error message -- it is half the outlines in
 * a mass of tagged npcs, gone, with the plugin still reporting itself armed.
 *
 * The two reasons are different findings on purpose. BUDGET is this plugin's
 * own frame allotment and is a bug in what it asked for; CONFLICT is another
 * plugin holding the entity's APPEARANCE, which is arbitration working, and
 * the loser is entitled to know it lost. Both coalesce on (verb, element,
 * result) at element NONE, so a thousand refused entities are one line each
 * and not a thousand.
 */
bool
Porcelain_Hull(struct Porcelain* porcelain, struct ToriRS_Graphics* draw, int element_id,
               uint32_t rgb, int alpha, int shape)
{
    enum ToriRS_Result result;

    assert(porcelain);
    assert(draw);
    assert(draw->world_hull);

    porcelain->counters.engine_calls++;
    result = draw->world_hull(draw, element_id, rgb, alpha, shape);
    if( result == TORIRS_RESULT_OK )
        return true;
    if( result == TORIRS_RESULT_BUDGET )
    {
        Porcelain_RecordFinding(porcelain, "world_hull", PORCELAIN_EL(NONE),
                                PORCELAIN_FINDING_BUDGET,
                                "the frame's draw budget; the rest of the outlines were dropped");
        return false;
    }
    if( result == TORIRS_RESULT_CONFLICT )
    {
        Porcelain_RecordFinding(porcelain, "world_hull", PORCELAIN_EL(NONE),
                                PORCELAIN_FINDING_ARBITRATION_LOST,
                                "another plugin holds this entity's appearance");
        return false;
    }
    Porcelain_RecordFinding(porcelain, "world_hull", PORCELAIN_EL(NONE),
                            PORCELAIN_FINDING_REFUSED, "the hull shape is not one of the two");
    return false;
}

/*
 * A plugin's OWN finding, in the channel its verbs' findings already use.
 *
 * Without this a plugin that fails at something Porcelain has no verb for --
 * a table row it could not parse, a server fact that never arrived -- writes
 * a core.log line, which the gate does not read, no plugin can query back,
 * and nothing coalesces. Two shipped ledger rows are exactly that. `verb`
 * names what failed; the (verb, element, result) coalescing is the same one,
 * so a per-frame failure is still one line.
 */
void
Porcelain_Finding(struct Porcelain* porcelain, char const* verb, struct PorcelainElement element,
                  int result, char const* detail)
{
    assert(porcelain);
    assert(verb);
    assert(verb[0]);
    /* OK is not a finding: the table says what is WRONG now, and a plugin
     * recording successes would push real refusals out of a fixed table. */
    assert(result != PORCELAIN_FINDING_OK);
    Porcelain_RecordFinding(porcelain, verb, element, result, detail);
}

/*
 * Which container a hovered cell belongs to.
 *
 * Answered by walking the cell up to a panel this vocabulary can name. The
 * panels are asked through the watch table directly rather than through
 * Porcelain_Element, because "this lane has no bank panel" is a fact the
 * hover does not depend on: it answers OTHER and carries the container id,
 * and an ABSENT finding for it on every lane would be noise.
 */
static enum PorcelainContainer
porcelain_container_of(struct Porcelain* porcelain, int component_id)
{
    static char const* const PANEL_NAMES[] = {"inventory", "equipment", "bank"};
    static enum PorcelainContainer const PANEL_KINDS[] = {
        PORCELAIN_CONTAINER_INV, PORCELAIN_CONTAINER_WORN, PORCELAIN_CONTAINER_BANK};
    struct ToriRS_WidgetApi const* widgets = &porcelain->api->widgets;
    struct ToriRS_WidgetRef cursor;

    if( component_id < 0 )
        return PORCELAIN_CONTAINER_NONE;
    porcelain->counters.engine_calls++;
    if( widgets->get_widget(widgets->context, component_id, &cursor) != TORIRS_CONTRACT_OK )
        return PORCELAIN_CONTAINER_OTHER;
    for( int hop = 0; hop < 32; hop++ )
    {
        struct ToriRS_WidgetRef parent;
        for( int i = 0; i < 3; i++ )
        {
            struct PorcelainWatch const* watch =
                Porcelain_WatchFor(porcelain, PORCELAIN_PANEL_EL(PANEL_NAMES[i]), true);
            if( !watch || watch->state.bind != PORCELAIN_BOUND )
                continue;
            if( ToriRS_WidgetRefEqual(watch->state.ref, cursor) )
                return PANEL_KINDS[i];
        }
        porcelain->counters.engine_calls++;
        if( widgets->parent(widgets->context, cursor, &parent) != TORIRS_CONTRACT_OK )
            break;
        if( !ToriRS_WidgetRefValid(parent) )
            break;
        cursor = parent;
    }
    return PORCELAIN_CONTAINER_OTHER;
}

/*
 * The hover pass of the menu build is the client's ONE answer to "what is
 * under the pointer", and it runs every frame. A right-click build is not a
 * hover: while the menu is open the rebuild stops, the stash goes stale
 * within a frame, and the tooltip stops drawing -- which is the reference
 * client's isMenuOpen() gate, for free.
 */
void
Porcelain_NoteMenu(struct Porcelain* porcelain, struct ToriRS_MenuBuildEvent const* menu)
{
    assert(porcelain);
    assert(menu);

    if( !menu->hover_pass )
        return;
    for( int i = 0; i < menu->row_count && i < TORIRS_PLUGIN_MENU_ROWS_MAX; i++ )
    {
        struct ToriRS_MenuRow const* row = &menu->rows[i];
        if( row->pick_kind != PORCELAIN_MENU_PICK_INV_SLOT || row->target_id < 0 )
            continue;
        memset(&porcelain->hover, 0, sizeof(porcelain->hover));
        porcelain->hover.obj = row->target_id;
        porcelain->hover.slot = row->slot;
        porcelain->hover.container_id = row->component_id;
        porcelain->hover.container = porcelain_container_of(porcelain, row->component_id);
        porcelain->hover.frame = porcelain->frame;
        porcelain->hover_live = true;
        return;
    }
    porcelain->hover_live = false;
}

bool
Porcelain_Hover(struct Porcelain* porcelain, struct PorcelainHover* out)
{
    assert(porcelain);
    assert(out);

    memset(out, 0, sizeof(*out));
    if( !porcelain->hover_live )
        return false;
    /* One frame of liveness. The menu build and the draw pass are not in the
     * same frame on every lane, so the window is "this frame or the last" and
     * not "this frame"; anything older is a pointer that has stopped being
     * answered for. */
    if( porcelain->frame - porcelain->hover.frame > 1 )
    {
        porcelain->hover_live = false;
        return false;
    }
    *out = porcelain->hover;
    return true;
}

/* Hand every suppressed native back to its own visibility. */
static void
porcelain_overlay_handoff(struct Porcelain* porcelain)
{
    struct ToriRS_WidgetApi const* widgets = &porcelain->api->widgets;
    struct PorcelainNativeOverlay* overlay = &porcelain->overlay;

    for( int i = 0; i < overlay->hidden_count; i++ )
    {
        porcelain->counters.engine_calls++;
        (void)widgets->reset(widgets->context, overlay->hidden[i]);
    }
    overlay->hidden_count = 0;
    overlay->state = PORCELAIN_NATIVE_OVERLAY_FORMATTING;
}

void
Porcelain_NativeOverlay(struct Porcelain* porcelain, char const* labels_role,
                        char const* callback, PorcelainScriptFn fn, void* user)
{
    struct PorcelainNativeOverlay* overlay;

    assert(porcelain);
    assert(labels_role);
    assert(callback);
    assert(fn);

    overlay = &porcelain->overlay;
    memset(overlay, 0, sizeof(*overlay));
    overlay->used = true;
    Porcelain_CopyString(overlay->labels_role, sizeof(overlay->labels_role), labels_role);
    Porcelain_CopyString(overlay->callback, sizeof(overlay->callback), callback);
    overlay->fn = fn;
    overlay->user = user;

    /* ABSENT is decided once, here. A lane with no script VM never raises the
     * callback, and an overlay that sat in SUPPRESSING for ever -- hiding
     * natives it would never replace -- is worse than one that stands down. */
    if( !Porcelain_Has(porcelain, "cs2_scripts") )
    {
        overlay->state = PORCELAIN_NATIVE_OVERLAY_ABSENT;
        Porcelain_RecordFinding(porcelain, "native_overlay", PORCELAIN_ROLE_EL(callback),
                                PORCELAIN_FINDING_UNSUPPORTED, "no native caption hook here");
        return;
    }
    overlay->state = PORCELAIN_NATIVE_OVERLAY_SUPPRESSING;
    porcelain->counters.engine_calls++;
    if( porcelain->api->scripts.invalidate(porcelain->api->scripts.context, callback) !=
        TORIRS_CONTRACT_OK )
        Porcelain_RecordFinding(porcelain, "native_overlay", PORCELAIN_ROLE_EL(callback),
                                PORCELAIN_FINDING_REFUSED, "invalidate refused");
}

void
Porcelain_NoteScript(struct Porcelain* porcelain, struct ToriRS_ScriptEvent const* event)
{
    struct PorcelainNativeOverlay* overlay;

    assert(porcelain);
    assert(event);

    overlay = &porcelain->overlay;
    if( !overlay->used || overlay->state == PORCELAIN_NATIVE_OVERLAY_ABSENT )
        return;
    if( !event->name || strcmp(event->name, overlay->callback) != 0 )
        return;
    /* The FIRST callback is the handoff: from here the cache's own captions
     * carry the plugin's fields, so the plugin's stand-ins come down and the
     * natives go back to their own visibility. */
    if( overlay->state == PORCELAIN_NATIVE_OVERLAY_SUPPRESSING )
        porcelain_overlay_handoff(porcelain);
    if( !overlay->fn(porcelain->api, overlay->user, event) )
        Porcelain_RecordFinding(porcelain, "native_overlay", PORCELAIN_ROLE_EL(overlay->callback),
                                PORCELAIN_FINDING_REFUSED, "the caption was refused");
}

void
Porcelain_OverlayRelease(struct Porcelain* porcelain)
{
    assert(porcelain);
    if( !porcelain->overlay.used ||
        porcelain->overlay.state != PORCELAIN_NATIVE_OVERLAY_SUPPRESSING )
        return;
    porcelain_overlay_handoff(porcelain);
}

enum PorcelainNativeOverlayState
Porcelain_NativeOverlayState(struct Porcelain* porcelain)
{
    assert(porcelain);
    return porcelain->overlay.used ? porcelain->overlay.state : PORCELAIN_NATIVE_OVERLAY_ABSENT;
}

/* Hide whatever the labels role currently matches. Runs per fence while the
 * latch is SUPPRESSING and not at all afterwards. */
static void
porcelain_overlay_fence(struct Porcelain* porcelain)
{
    struct ToriRS_WidgetApi const* widgets = &porcelain->api->widgets;
    struct PorcelainNativeOverlay* overlay = &porcelain->overlay;
    struct ToriRS_WidgetRef refs[PORCELAIN_OVERLAY_LABELS_MAX];
    size_t count = 0;

    if( !overlay->used || overlay->state != PORCELAIN_NATIVE_OVERLAY_SUPPRESSING )
        return;
    porcelain->counters.engine_calls++;
    if( widgets->find_all(widgets->context, overlay->labels_role, refs,
                          PORCELAIN_OVERLAY_LABELS_MAX, &count) != TORIRS_CONTRACT_OK )
        return;
    if( count > PORCELAIN_OVERLAY_LABELS_MAX )
    {
        Porcelain_RecordFinding(porcelain, "native_overlay",
                                PORCELAIN_ROLE_EL(overlay->labels_role),
                                PORCELAIN_FINDING_BUDGET, "more labels than the latch holds");
        count = PORCELAIN_OVERLAY_LABELS_MAX;
    }
    overlay->hidden_count = 0;
    for( size_t i = 0; i < count; i++ )
    {
        if( !ToriRS_WidgetRefValid(refs[i]) )
            continue;
        porcelain->counters.engine_calls++;
        if( widgets->set_hidden(widgets->context, refs[i], true) != TORIRS_CONTRACT_OK )
            continue;
        overlay->hidden[overlay->hidden_count++] = refs[i];
    }
}

/*
 * A shipped data file, read once.
 *
 * The bytes are released the moment the parse returns: the parsed form lives
 * in the plugin, and the two shipped overlays that each held their own copy
 * of prices.txt held it for the life of the process for nothing.
 */
bool
Porcelain_Table(struct Porcelain* porcelain, char const* asset, PorcelainParseFn parse,
                void* user)
{
    struct PorcelainTableSlot* slot = NULL;
    enum ToriRS_AssetState state;
    void const* data = NULL;
    size_t size = 0;
    bool parsed;

    assert(porcelain);
    assert(asset);
    assert(parse);

    for( int i = 0; i < PORCELAIN_TABLES_MAX && !slot; i++ )
        if( porcelain->tables[i].used &&
            strcmp(porcelain->tables[i].asset.text, asset) == 0 )
            slot = &porcelain->tables[i];
    for( int i = 0; i < PORCELAIN_TABLES_MAX && !slot; i++ )
        if( !porcelain->tables[i].used )
        {
            slot = &porcelain->tables[i];
            memset(slot, 0, sizeof(*slot));
            slot->used = true;
            Porcelain_CopyString(slot->asset.text, sizeof(slot->asset.text), asset);
        }
    if( !slot )
    {
        Porcelain_RecordFinding(porcelain, "table", PORCELAIN_ROLE_EL(asset),
                                PORCELAIN_FINDING_BUDGET, "table slots full");
        return false;
    }
    if( slot->parsed )
        return true;
    if( slot->failed )
        return false;

    porcelain->counters.engine_calls++;
    state = porcelain->api->assets.request(porcelain->api, asset);
    /* PENDING is the web lane fetching it; retried, never a finding. */
    if( state == TORIRS_ASSET_PENDING )
        return false;
    if( state != TORIRS_ASSET_READY )
    {
        slot->failed = true;
        Porcelain_RecordFinding(porcelain, "table", PORCELAIN_ROLE_EL(asset),
                                state == TORIRS_ASSET_MISSING ? PORCELAIN_FINDING_ASSET_MISSING
                                                              : PORCELAIN_FINDING_ASSET_ERROR,
                                asset);
        return false;
    }
    porcelain->counters.engine_calls++;
    if( !porcelain->api->assets.bytes(porcelain->api, asset, &data, &size) )
    {
        slot->failed = true;
        Porcelain_RecordFinding(porcelain, "table", PORCELAIN_ROLE_EL(asset),
                                PORCELAIN_FINDING_ASSET_ERROR, asset);
        return false;
    }
    parsed = parse(porcelain->api, user, data, size);
    porcelain->counters.engine_calls++;
    porcelain->api->assets.release(porcelain->api, asset);
    if( !parsed )
    {
        slot->failed = true;
        Porcelain_RecordFinding(porcelain, "table", PORCELAIN_ROLE_EL(asset),
                                PORCELAIN_FINDING_REFUSED, "the parse refused the bytes");
        return false;
    }
    slot->parsed = true;
    return true;
}

/*
 * One announcement per (kind, subject) per frame.
 *
 * A stack of twelve bones reaching the ground is twelve spawn events and one
 * line; a tier announcement and a highlight announcement about the same obj
 * are two different things and stay two lines. Both shipped overlays wrote
 * theirs to the log, where nobody playing the game could see them.
 */
void
Porcelain_Notify(struct Porcelain* porcelain, char const* kind, int subject, char const* text)
{
    struct PorcelainNotifySlot* oldest = NULL;

    assert(porcelain);
    assert(kind);
    assert(text);

    if( !porcelain->api->core.notify )
    {
        Porcelain_RecordFinding(porcelain, "notify", PORCELAIN_ROLE_EL(kind),
                                PORCELAIN_FINDING_UNSUPPORTED, "this host has no notifier");
        return;
    }
    for( int i = 0; i < PORCELAIN_NOTIFY_MAX; i++ )
    {
        struct PorcelainNotifySlot* slot = &porcelain->notifies[i];
        if( !slot->used )
        {
            oldest = slot;
            break;
        }
        if( slot->subject == subject && strcmp(slot->kind, kind) == 0 )
        {
            if( slot->frame == porcelain->frame )
                return;
            oldest = slot;
            break;
        }
        if( !oldest || slot->frame < oldest->frame )
            oldest = slot;
    }
    assert(oldest);
    memset(oldest, 0, sizeof(*oldest));
    oldest->used = true;
    Porcelain_CopyString(oldest->kind, sizeof(oldest->kind), kind);
    oldest->subject = subject;
    oldest->frame = porcelain->frame;
    porcelain->counters.engine_calls++;
    porcelain->api->core.notify(porcelain->api, text);
}

/* ------------------------------------------------------------------------ */
/* The per-fence helper pass                                                */
/* ------------------------------------------------------------------------ */

void
Porcelain_HelpersFence(struct Porcelain* porcelain)
{
    unsigned ready;
    uint64_t now_ms;

    assert(porcelain);

    /* 0. The native-overlay latch, while it is suppressing. Nothing after the
     *    handoff, which is why this is a state and not a flag. */
    porcelain_overlay_fence(porcelain);

    /* 1. Images still pending. A transition is an INPUT: it re-runs the
     *    describe, which is how "asset before bind" and "bind before asset"
     *    converge on the same tree. */
    for( int i = 0; i < PORCELAIN_IMAGES_MAX; i++ )
    {
        struct PorcelainImageSlot* slot = &porcelain->images[i];
        if( !slot->used || slot->state != PORCELAIN_ASSET_PENDING )
            continue;
        porcelain->counters.engine_calls++;
        slot->state = porcelain_map_asset_state(
            porcelain->api->assets.image(porcelain->api, slot->name.text, &slot->ref));
        if( slot->state != PORCELAIN_ASSET_PENDING )
            porcelain->stamp[PORCELAIN_INPUT_ASSET]++;
        porcelain_report_terminal_asset(porcelain, "image", slot->name.text, slot->state,
                                        &slot->terminal_reported);
    }

    /* 2. Images nothing has asked for in several runs. Lazy request, lazy
     *    release: eleven of the fifteen orb source images are never read
     *    through a handle again after the pixel copy. */
    for( int i = 0; i < PORCELAIN_IMAGES_MAX; i++ )
    {
        struct PorcelainImageSlot* slot = &porcelain->images[i];
        if( !slot->used || slot->state != PORCELAIN_ASSET_READY )
            continue;
        if( porcelain->run < slot->last_used_run + PORCELAIN_IMAGE_IDLE_RUNS )
            continue;
        porcelain->counters.engine_calls++;
        porcelain->api->assets.image_release(porcelain->api, slot->ref);
        memset(slot, 0, sizeof(*slot));
    }

    /* 3. Frame cadence and millisecond timers. The clock is read only when a
     *    millisecond timer exists: a handle with no timers must make NO
     *    engine call at a fence, or "an unchanged description costs nothing"
     *    is one core.frame_ms short of true. */
    now_ms = 0;
    for( int i = 0; i < PORCELAIN_TIMERS_MAX; i++ )
        if( porcelain->timers[i].used && porcelain->timers[i].is_ms )
        {
            porcelain->counters.engine_calls++;
            now_ms = porcelain->api->core.frame_ms(porcelain->api);
            break;
        }
    for( int i = 0; i < PORCELAIN_TIMERS_MAX; i++ )
    {
        struct PorcelainTimer* timer = &porcelain->timers[i];
        if( !timer->used )
            continue;
        if( timer->is_ms )
        {
            uint64_t elapsed;
            if( now_ms < timer->next_due_ms )
                continue;
            /* The REAL elapsed time, not the interval that was asked for: a
             * frames-per-second figure divides by this, and a clock that
             * assumed its nominal interval printed a number that was wrong by
             * however much the frame budget slipped. */
            elapsed = timer->last_fired_ms ? now_ms - timer->last_fired_ms
                                           : (uint64_t)timer->milliseconds;
            timer->last_fired_ms = now_ms;
            timer->next_due_ms = now_ms + (uint64_t)timer->milliseconds;
            timer->fn(porcelain->api, timer->user, elapsed);
        }
        else if( timer->cadence == PORCELAIN_FRAME )
        {
            timer->fn(porcelain->api, timer->user, 0);
        }
    }

    /* 4. Key bindings. The BINDING is re-read here so a rebind takes effect
     *    without a reload; the edge itself arrives through Porcelain_NoteKey,
     *    because a fence poll cannot see a press that opens and closes inside
     *    one frame and costs an engine call per watch per frame to miss it. */
    for( int i = 0; i < PORCELAIN_KEY_EDGES_MAX; i++ )
    {
        struct PorcelainKeyEdgeWatch* watch = &porcelain->key_edges[i];
        char const* name = NULL;
        int code;
        if( !watch->used )
            continue;
        porcelain->counters.engine_calls++;
        if( !porcelain->api->config.get_string(porcelain->api, watch->config_key, &name) )
            name = NULL;
        code = porcelain_key_code(name);
        if( code != watch->code && watch->down )
        {
            /* The old binding cannot stay down through a rebind. */
            watch->down = false;
            watch->fn(porcelain->api, watch->user, false);
        }
        watch->code = code;
        if( code >= 0 )
            continue;
        if( !watch->absent_reported )
        {
            watch->absent_reported = true;
            Porcelain_RecordFinding(porcelain, "key_edge", PORCELAIN_ROLE_EL(watch->config_key),
                                    PORCELAIN_FINDING_ABSENT, name ? name : "off");
        }
    }

    /* 5. Readiness. Fires once when every named bit holds, and again after a
     *    re-login, because a latch that never re-arms is the claim that
     *    answered -1 for ever. */
    {
        bool any_ready_watch = false;
        for( int i = 0; i < PORCELAIN_READY_MAX; i++ )
            if( porcelain->ready[i].used )
                any_ready_watch = true;
        if( !any_ready_watch )
            return;
        ready = porcelain_ready_bits(porcelain);
        for( int i = 0; i < PORCELAIN_READY_MAX; i++ )
        {
            struct PorcelainReadyWatch* watch = &porcelain->ready[i];
            if( !watch->used )
                continue;
            if( (ready & watch->what) == watch->what )
            {
                if( !watch->fired )
                {
                    watch->fired = true;
                    watch->fn(porcelain->api, watch->user, watch->what);
                }
            }
            else if( !(ready & PORCELAIN_READY_GAME) )
            {
                watch->fired = false;
            }
        }
    }
}
