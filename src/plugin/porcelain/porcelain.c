/*
 * Porcelain: lifecycle, elements, the description and its reconcile, the
 * direct motion path, and arbitration.
 *
 * Read plugin/torirs_plugin_api.h's Porcelain block first: it carries the
 * vocabulary and the three rules. This file is the machine that keeps them.
 *
 * The shape of the reconcile, once, because every plugin that wrote it by
 * hand wrote it differently:
 *
 *   fence
 *     |- has an input stamp moved?  no  -> refresh geometry only, and that
 *     |                                    costs zero engine calls when no
 *     |                                    target box moved.
 *     |- yes -> run describe into SCRATCH (never touching APPLIED)
 *     |         diff by key:
 *     |           missing from scratch  -> remove, and only that control
 *     |           hash equal            -> no engine call at all
 *     |           hash different        -> the minimal setter set
 *     |           new                   -> create, then apply everything
 *     `- stage the writes; ONE revalidate for every plugin, at the commit.
 *
 * A describe that errors or overruns a capacity leaves the applied
 * description untouched and records one finding. That is deliberate: a
 * half-applied description is the flicker class, and the record has three of
 * them.
 */

#include "plugin/porcelain/porcelain_internal.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Process state                                                            */
/* ------------------------------------------------------------------------ */

/*
 * Handles in open order. Open order IS the host's event order: the host
 * starts plugins in event_priority order and each calls open from its own
 * on_start, so `order` answers "who claimed first" without Porcelain having
 * to ask the host anything.
 */
static struct Porcelain g_handles[PORCELAIN_HANDLES_MAX];
static int g_handle_count;
static struct PorcelainClaim g_claims[PORCELAIN_CLAIMS_MAX];

/*
 * The commit epoch. Every handle fences into the current epoch; the commit
 * issues one revalidate for the whole epoch and opens the next. A fence that
 * finds its handle already fenced in this epoch means somebody forgot to
 * commit, which is a budget finding and a flush, never a dropped write.
 */
static uint32_t g_epoch = 1;
static bool g_epoch_dirty;
static struct ToriRS_WidgetRef g_epoch_revalidate_ref;
static struct ToriRS_Api* g_epoch_api;

/*
 * The trace gate, read once. getenv on a per-frame path is the hot-path
 * defect the audit already found elsewhere in this tree.
 */
static int g_trace = -1;

static bool
porcelain_trace_enabled(void)
{
    if( g_trace < 0 )
        g_trace = getenv("TORIRS_TRACE_NATIVE_UI") ? 1 : 0;
    return g_trace != 0;
}

/* ------------------------------------------------------------------------ */
/* Small shared helpers                                                     */
/* ------------------------------------------------------------------------ */

uint64_t
Porcelain_HashBytes(uint64_t seed, void const* data, size_t length)
{
    unsigned char const* bytes = data;
    uint64_t hash = seed ? seed : 1469598103934665603ull;

    assert(data || length == 0);
    for( size_t i = 0; i < length; i++ )
    {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

uint64_t
Porcelain_HashString(uint64_t seed, char const* text)
{
    if( !text )
        return Porcelain_HashBytes(seed, "", 0);
    return Porcelain_HashBytes(seed, text, strlen(text));
}

void
Porcelain_CopyString(char* destination, size_t capacity, char const* source)
{
    size_t length;

    assert(destination);
    assert(capacity > 0);
    (void)capacity; /* Read only by the assert below, which NDEBUG removes. */
    if( !source )
    {
        destination[0] = '\0';
        return;
    }
    length = strlen(source);
    /* An over-long key, role or caption is a caller bug: it would silently
     * alias a different identity if it were truncated here. */
    assert(length < capacity);
    memcpy(destination, source, length + 1);
}

uint64_t
Porcelain_ElementKey(struct PorcelainElement element)
{
    uint64_t hash = Porcelain_HashBytes(0, &element.kind, sizeof(element.kind));
    hash = Porcelain_HashBytes(hash, &element.member, sizeof(element.member));
    return Porcelain_HashString(hash, element.role);
}

static char const* const PORCELAIN_KIND_NAMES[PORCELAIN_EL_KIND_COUNT] = {
    "none",       "viewport",  "minimap",     "compass",      "orbs",
    "orb",        "chat",      "chat_bar",    "chat_backing", "chat_input",
    "chat_filter","report",    "public_chat", "sidebar",      "tab",
    "panel",      "modal",     "lane_chrome", "frame_root",   "canvas",
    "safe",       "usable",    "role"};

void
Porcelain_FormatElement(struct PorcelainElement element, char* out, size_t capacity)
{
    char const* name;

    assert(out);
    assert(capacity > 0);
    name = (element.kind >= 0 && element.kind < PORCELAIN_EL_KIND_COUNT)
               ? PORCELAIN_KIND_NAMES[element.kind]
               : "?";
    if( element.role && element.role[0] )
        snprintf(out, capacity, "%s(%s)", name, element.role);
    else if( element.member )
        snprintf(out, capacity, "%s[%d]", name, element.member);
    else
        snprintf(out, capacity, "%s", name);
}

/* ------------------------------------------------------------------------ */
/* Findings                                                                 */
/* ------------------------------------------------------------------------ */

static bool
porcelain_expected_absence(struct Porcelain* porcelain, struct PorcelainElement element)
{
    uint64_t const key = Porcelain_ElementKey(element);

    for( int i = 0; i < PORCELAIN_EXPECT_MAX; i++ )
        if( porcelain->expects[i].used &&
            Porcelain_ElementKey(porcelain->expects[i].element) == key )
            return true;
    return false;
}

/*
 * Take back an absence the lane has since answered.
 *
 * The findings table says what is wrong NOW, not what was ever briefly true.
 * A gameframe mounts in pieces: the chat resolves, then the orbs, then the
 * tabs, and an element asked for between two of those is missing for a moment
 * and then is not. Leaving that in the table makes the clean gate fail on
 * every capture for elements that are all present at the end, and a gate that
 * always fails is a gate nobody reads. An absence that never resolves still
 * stands, which is the one this is for.
 */
static void
porcelain_forget_absence(struct Porcelain* porcelain, struct PorcelainElement element)
{
    for( int i = 0; i < PORCELAIN_FINDINGS_MAX; i++ )
    {
        struct PorcelainFindingSlot* slot = &porcelain->findings[i];
        if( !slot->used || slot->pub.result != PORCELAIN_FINDING_ABSENT )
            continue;
        if( Porcelain_ElementKey(slot->pub.element) != Porcelain_ElementKey(element) )
            continue;
        memset(slot, 0, sizeof(*slot));
    }
}

void
Porcelain_RecordFinding(struct Porcelain* porcelain, char const* verb,
                        struct PorcelainElement element, int result, char const* detail)
{
    struct PorcelainFindingSlot* free_slot = NULL;
    char text[PORCELAIN_NAME_MAX];

    assert(porcelain);
    assert(verb);

    /* Coalesce on (verb, element, result). Everything else -- the detail, the
     * expectation -- belongs to the first occurrence, because a refusal that
     * changes its detail every frame is a different finding, not this one. */
    for( int i = 0; i < PORCELAIN_FINDINGS_MAX; i++ )
    {
        struct PorcelainFindingSlot* slot = &porcelain->findings[i];
        if( !slot->used )
        {
            if( !free_slot )
                free_slot = slot;
            continue;
        }
        if( slot->pub.result == result && strcmp(slot->verb, verb) == 0 &&
            Porcelain_ElementKey(slot->pub.element) == Porcelain_ElementKey(element) )
        {
            slot->pub.count++;
            return;
        }
    }
    if( !free_slot )
        return; /* The budget finding below would itself need a slot. */

    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->used = true;
    Porcelain_CopyString(free_slot->verb, sizeof(free_slot->verb), verb);
    if( detail )
        Porcelain_CopyString(free_slot->detail, sizeof(free_slot->detail), detail);
    if( element.role )
        Porcelain_CopyString(free_slot->role, sizeof(free_slot->role), element.role);
    free_slot->pub.element = element;
    free_slot->pub.element.role = element.role ? free_slot->role : NULL;
    free_slot->pub.verb = free_slot->verb;
    free_slot->pub.detail = free_slot->detail;
    free_slot->pub.result = result;
    free_slot->pub.expected = result == PORCELAIN_FINDING_ABSENT_EXPECTED ||
                              (result == PORCELAIN_FINDING_ABSENT &&
                               porcelain_expected_absence(porcelain, element));
    free_slot->pub.first_frame = porcelain->frame;
    free_slot->pub.count = 1;

    if( !porcelain_trace_enabled() )
        return;
    Porcelain_FormatElement(free_slot->pub.element, text, sizeof(text));
    /* One line at birth, with the running count readable through `findings`
     * and restated at close. A line per frame is the defect the record calls
     * "logged every frame or not at all". */
    fprintf(stderr,
            "PORCELAIN_FINDING plugin=%s verb=%s element=%s result=%d detail=%s "
            "expected=%d first_frame=%u count=%u\n",
            porcelain->plugin_id, free_slot->verb, text, free_slot->pub.result,
            free_slot->detail[0] ? free_slot->detail : "-", free_slot->pub.expected ? 1 : 0,
            free_slot->pub.first_frame, free_slot->pub.count);
}

static void
porcelain_trace_final_findings(struct Porcelain* porcelain)
{
    char text[PORCELAIN_NAME_MAX];

    if( !porcelain_trace_enabled() )
        return;
    for( int i = 0; i < PORCELAIN_FINDINGS_MAX; i++ )
    {
        struct PorcelainFindingSlot* slot = &porcelain->findings[i];
        if( !slot->used || slot->pub.count < 2 )
            continue;
        Porcelain_FormatElement(slot->pub.element, text, sizeof(text));
        fprintf(stderr,
                "PORCELAIN_FINDING plugin=%s verb=%s element=%s result=%d detail=%s "
                "expected=%d first_frame=%u count=%u\n",
                porcelain->plugin_id, slot->verb, text, slot->pub.result,
                slot->detail[0] ? slot->detail : "-", slot->pub.expected ? 1 : 0,
                slot->pub.first_frame, slot->pub.count);
    }
}

int
Porcelain_Findings(struct Porcelain* porcelain, struct PorcelainFinding* out, int capacity)
{
    int written = 0;

    assert(porcelain);
    if( capacity <= 0 )
        return 0;
    assert(out);
    for( int i = 0; i < PORCELAIN_FINDINGS_MAX && written < capacity; i++ )
        if( porcelain->findings[i].used )
            out[written++] = porcelain->findings[i].pub;
    return written;
}

void
Porcelain_ExpectAbsent(struct Porcelain* porcelain, struct PorcelainElement element,
                       char const* why)
{
    assert(porcelain);
    assert(why);
    /* on_start only. A declaration made after the first describe could not
     * mark the absence finding it was meant to excuse. */
    assert(!porcelain->ever_described);
    for( int i = 0; i < PORCELAIN_EXPECT_MAX; i++ )
    {
        struct PorcelainExpectAbsent* slot = &porcelain->expects[i];
        if( slot->used )
            continue;
        memset(slot, 0, sizeof(*slot));
        slot->used = true;
        slot->element = element;
        if( element.role )
        {
            Porcelain_CopyString(slot->element_role, sizeof(slot->element_role), element.role);
            slot->element.role = slot->element_role;
        }
        Porcelain_CopyString(slot->why, sizeof(slot->why), why);
        return;
    }
    Porcelain_RecordFinding(porcelain, "expect_absent", element, PORCELAIN_FINDING_BUDGET,
                            "expectation table full");
}

/* ------------------------------------------------------------------------ */
/* Arbitration                                                              */
/* ------------------------------------------------------------------------ */

/*
 * One owner per aspect per element, the first claimer by open order, stable
 * across frames and inspectable. The loser's item is not applied and it gets
 * a finding naming the winner.
 *
 * The claim is taken during the describe run and released only by
 * relinquish, close, or a describe that stopped asking for it -- never by a
 * competing plugin. Taking it away would make the picture depend on which
 * plugin fenced last, which is the "last child in draw order wins" the engine
 * already does and the reason Porcelain arbitrates above it.
 */
bool
Porcelain_ClaimTake(struct Porcelain* porcelain, struct PorcelainElement element,
                    enum PorcelainAspect aspect, char const** out_winner)
{
    uint64_t const key = Porcelain_ElementKey(element);
    struct PorcelainClaim* free_slot = NULL;

    assert(porcelain);
    assert(out_winner);
    assert(aspect >= 0 && aspect < PORCELAIN_ASPECT_COUNT);
    *out_winner = NULL;

    for( int i = 0; i < PORCELAIN_CLAIMS_MAX; i++ )
    {
        struct PorcelainClaim* claim = &g_claims[i];
        if( !claim->used )
        {
            if( !free_slot )
                free_slot = claim;
            continue;
        }
        if( claim->element_key != key || claim->aspect != aspect )
            continue;
        if( claim->owner == porcelain )
        {
            claim->taken_run = porcelain->run;
            return true;
        }
        *out_winner = claim->owner_id;
        return false;
    }
    if( !free_slot )
    {
        Porcelain_RecordFinding(porcelain, "claim", element, PORCELAIN_FINDING_BUDGET,
                                "claim table full");
        return false;
    }
    free_slot->used = true;
    free_slot->element_key = key;
    free_slot->aspect = aspect;
    free_slot->owner = porcelain;
    free_slot->taken_run = porcelain->run;
    Porcelain_CopyString(free_slot->owner_id, sizeof(free_slot->owner_id), porcelain->plugin_id);
    return true;
}

/*
 * A claim the owner stopped describing is released, or a plugin that once
 * drew an orb would own that orb for the life of the process and the second
 * plugin's finding would never clear.
 */
void
Porcelain_ClaimDropStale(struct Porcelain* porcelain)
{
    assert(porcelain);
    for( int i = 0; i < PORCELAIN_CLAIMS_MAX; i++ )
        if( g_claims[i].used && g_claims[i].owner == porcelain &&
            g_claims[i].taken_run != porcelain->run )
            memset(&g_claims[i], 0, sizeof(g_claims[i]));
}

void
Porcelain_ClaimDropAll(struct Porcelain* porcelain)
{
    assert(porcelain);
    for( int i = 0; i < PORCELAIN_CLAIMS_MAX; i++ )
        if( g_claims[i].used && g_claims[i].owner == porcelain )
            memset(&g_claims[i], 0, sizeof(g_claims[i]));
}

char const*
Porcelain_ClaimOwner(struct PorcelainElement element, enum PorcelainAspect aspect)
{
    uint64_t const key = Porcelain_ElementKey(element);

    for( int i = 0; i < PORCELAIN_CLAIMS_MAX; i++ )
        if( g_claims[i].used && g_claims[i].element_key == key && g_claims[i].aspect == aspect )
            return g_claims[i].owner_id;
    return NULL;
}

void
Porcelain_Relinquish(struct Porcelain* porcelain)
{
    assert(porcelain);
    Porcelain_ClaimDropAll(porcelain);
}

/* ------------------------------------------------------------------------ */
/* Element resolution                                                       */
/* ------------------------------------------------------------------------ */

/*
 * Kind -> role name. Every entry is a role a PROFILE declares; a lane that
 * declares none of them answers ABSENT for that element, which is the whole
 * degradation story. There is no lane name here and there must never be one:
 * the moment a branch reads the lineage, the element table has a defect, not
 * the plugin.
 *
 * The seven slot-backed elements are named by the frame SLOT, not by the
 * profile's `frame_<slot>` rung. The host answers a slot name through the
 * frame slot table on every lane, which is what the shipping frame plugins
 * already ask for ("viewport", "chat", "sidebar", "minimap"); the `frame_*`
 * spelling is the profile's own rung for the binder, and three of those --
 * viewport, minimap and compass -- are not derived on the CS2 lane at all, so
 * naming them here would make those three elements read ABSENT everywhere.
 * The rest are profile roles, which is the right spelling for them: they name
 * a control the cache authored, not a region the frame owns.
 */
static char const* const PORCELAIN_ROLE_BY_KIND[PORCELAIN_EL_KIND_COUNT] = {
    NULL,                     /* NONE */
    "viewport",               /* VIEWPORT */
    "minimap",                /* MINIMAP */
    "compass",                /* COMPASS */
    "orbs",                   /* ORBS */
    NULL,                     /* ORB: per member */
    "chat",                   /* CHAT */
    "chat_bar",               /* CHAT_BAR */
    "chat_backing",           /* CHAT_BACKING */
    "chat_input",             /* CHAT_INPUT */
    NULL,                     /* CHAT_FILTER: per member */
    "report_button",          /* REPORT_BUTTON */
    "public_chat_button",     /* PUBLIC_CHAT_BUTTON */
    "sidebar",                /* SIDEBAR */
    NULL,                     /* TAB: per name */
    NULL,                     /* PANEL: per name */
    "main_modal",             /* MODAL */
    NULL,                     /* LANE_CHROME: per member */
    NULL,                     /* FRAME_ROOT: derived */
    NULL,                     /* CANVAS: derived */
    NULL,                     /* SAFE: derived */
    NULL,                     /* USABLE: derived */
    NULL                      /* ROLE: spelled by the caller */
};

static char const* const PORCELAIN_ORB_ROLES[PORCELAIN_ORB_COUNT] = {
    "orb_hitpoints", "orb_prayer", "orb_run", "orb_spec"};

static int
porcelain_engine_find(struct Porcelain* porcelain, char const* role,
                      struct ToriRS_WidgetRef* out)
{
    struct ToriRS_WidgetApi const* widgets = &porcelain->api->widgets;

    porcelain->counters.engine_calls++;
    return (int)widgets->find(widgets->context, role, out);
}

/*
 * The two elements the lanes spell differently, and the rule that picks
 * between the spellings.
 *
 * A tab stone is a `tab_<name>` builtin on a lane that authored one and a
 * numbered `sidetab_<n>` on a lane whose sidebar is numbered; a chat filter
 * is a `chat_plate_<n>` where the cache enumerates its plates and member
 * <n> of the frame's chat-buttons slot where it does not. Both pairs are
 * DATA: the spelling that the host ANSWERS is this lane's, and neither arm
 * names a lane, a revision or a lineage.
 *
 * It is not enough to ask once. Resolution happens the first time a plugin
 * asks for the element, and the first ask of a session runs before any tree
 * has published -- so NEITHER spelling resolves, the fallback is frozen in,
 * and an element whose other spelling binds two hundred frames later is
 * absent for the rest of the run. That is exactly what TAB and CHAT_FILTER
 * did on the 2004 lane: `tab_inventory` is a real role there, and the watch
 * was on `sidetab_3`. @see porcelain_watch_respell.
 */
#define PORCELAIN_SPELLINGS_MAX 2
/** Members probed when a family is counted or numbered. */
#define PORCELAIN_FAMILY_MEMBERS_MAX 16

static int
porcelain_spellings(struct Porcelain* porcelain, struct PorcelainElement element,
                    char out[PORCELAIN_SPELLINGS_MAX][TORIRS_UI_NAME_MAX])
{
    struct ToriRS_CacheApi* cache = &porcelain->api->cache;
    int number = -1;

    assert(porcelain);
    assert(out);
    switch( element.kind )
    {
    case PORCELAIN_EL_TAB:
        assert(element.role);
        snprintf(out[0], TORIRS_UI_NAME_MAX, "tab_%s", element.role);
        if( cache->named_id &&
            cache->named_id(porcelain->api, "tab", element.role, &number) && number >= 0 )
        {
            snprintf(out[1], TORIRS_UI_NAME_MAX, "sidetab_%d", number);
            return 2;
        }
        return 1;
    case PORCELAIN_EL_CHAT_FILTER:
        assert(element.member >= 0);
        snprintf(out[0], TORIRS_UI_NAME_MAX, "chat_plate_%d", element.member);
        snprintf(out[1], TORIRS_UI_NAME_MAX, "chat_buttons:%d", element.member);
        return 2;
    default:
        break;
    }
    return 0;
}

/** True for the families porcelain_spellings answers. */
static bool
porcelain_has_spellings(enum PorcelainElementKind kind)
{
    return kind == PORCELAIN_EL_TAB || kind == PORCELAIN_EL_CHAT_FILTER;
}

static bool
porcelain_resolve_spelled(struct Porcelain* porcelain, struct PorcelainElement element, char* out,
                          size_t capacity)
{
    char candidates[PORCELAIN_SPELLINGS_MAX][TORIRS_UI_NAME_MAX];
    int const count = porcelain_spellings(porcelain, element, candidates);
    struct ToriRS_WidgetRef ref;

    assert(count > 0);
    for( int i = 0; i < count; i++ )
    {
        if( porcelain_engine_find(porcelain, candidates[i], &ref) != TORIRS_CONTRACT_OK )
            continue;
        Porcelain_CopyString(out, capacity, candidates[i]);
        return true;
    }
    /* Nothing answers yet. Keep the first spelling -- the name the plugin
     * would have looked for by hand -- so an absence is reported under a name
     * its author recognises, and re-ask at every fence until one binds. */
    Porcelain_CopyString(out, capacity, candidates[0]);
    return false;
}

/*
 * How many members of a family this lane HAS.
 *
 * One rule for both spellings, and it is the engine's own: count is ONE PAST
 * THE HIGHEST MEMBER PRESENT (`find_all`'s contract, torirs_plugin_contract.h),
 * so a lane with a hole in the middle -- the 2004 sidebar's unused tab 7 --
 * still answers fourteen rather than seven.
 *
 * The primary spelling wins outright when any member answers it: that is the
 * plan's `native_chat_filters` rule said once. Eight when the cache
 * enumerated eight plates, else however many members the slot carries.
 */
static int
porcelain_count_members(struct Porcelain* porcelain, char const* primary, char const* fallback)
{
    struct ToriRS_WidgetRef ref;
    char role[TORIRS_UI_NAME_MAX];
    int count = 0;

    assert(primary);
    assert(fallback);
    for( int member = 0; member < PORCELAIN_FAMILY_MEMBERS_MAX; member++ )
    {
        snprintf(role, sizeof(role), primary, member);
        if( porcelain_engine_find(porcelain, role, &ref) == TORIRS_CONTRACT_OK )
            count = member + 1;
    }
    if( count > 0 )
        return count;
    for( int member = 0; member < PORCELAIN_FAMILY_MEMBERS_MAX; member++ )
    {
        snprintf(role, sizeof(role), fallback, member);
        if( porcelain_engine_find(porcelain, role, &ref) == TORIRS_CONTRACT_OK )
            count = member + 1;
    }
    return count;
}

bool
Porcelain_ResolveRole(struct Porcelain* porcelain, struct PorcelainElement element, char* out,
                      size_t capacity)
{
    assert(porcelain);
    assert(out);
    assert(capacity > 0);
    assert(element.kind > PORCELAIN_EL_NONE && element.kind < PORCELAIN_EL_KIND_COUNT);

    out[0] = '\0';
    switch( element.kind )
    {
    case PORCELAIN_EL_ORB:
        assert(element.member >= 0 && element.member < PORCELAIN_ORB_COUNT);
        Porcelain_CopyString(out, capacity, PORCELAIN_ORB_ROLES[element.member]);
        return true;
    case PORCELAIN_EL_CHAT_FILTER:
    case PORCELAIN_EL_TAB:
        return porcelain_resolve_spelled(porcelain, element, out, capacity);
    case PORCELAIN_EL_LANE_CHROME:
        assert(element.member >= 0);
        snprintf(out, capacity, "lane_chrome_%d", element.member);
        return true;
    case PORCELAIN_EL_PANEL:
        assert(element.role);
        snprintf(out, capacity, "panel_%s", element.role);
        return true;
    case PORCELAIN_EL_ROLE:
        assert(element.role);
        Porcelain_CopyString(out, capacity, element.role);
        return true;
    case PORCELAIN_EL_FRAME_ROOT:
    case PORCELAIN_EL_CANVAS:
    case PORCELAIN_EL_SAFE:
    case PORCELAIN_EL_USABLE:
        /* Derived, not a role. @see porcelain_frame_root */
        return false;
    default:
        break;
    }
    if( !PORCELAIN_ROLE_BY_KIND[element.kind] )
        return false;
    Porcelain_CopyString(out, capacity, PORCELAIN_ROLE_BY_KIND[element.kind]);
    return true;
}

static void
porcelain_read_state(struct Porcelain* porcelain, struct PorcelainWatch* watch)
{
    struct ToriRS_WidgetApi const* widgets = &porcelain->api->widgets;
    struct ToriRS_WidgetState native;

    memset(&native, 0, sizeof(native));
    native.struct_size = sizeof(native);
    porcelain->counters.engine_calls++;
    if( widgets->state(widgets->context, watch->state.ref, &native) != TORIRS_CONTRACT_OK )
        return;
    watch->state.box = native.bounds;
    watch->state.local = native.local;
    watch->state.presented = native.presented;
    watch->state.own_hidden = native.own_hidden;
    watch->state.native_hidden = native.native_hidden;
    watch->state.input_present = native.input_present;
    watch->state.graphic_token = native.graphic_token;
    watch->state.text_hash = native.text_hash;
    watch->state.facets = native.facets;
    watch->state.incarnation = native.incarnation;
    watch->state.stamp = porcelain->frame;
}

static void
porcelain_watch_listener(struct ToriRS_Api* api, void* user, struct ToriRS_WidgetEvent const* event)
{
    struct PorcelainWatch* watch = user;
    struct Porcelain* porcelain;

    assert(api);
    (void)api; /* The handle carries its own api; this one is the host's. */
    assert(watch);
    assert(event);
    porcelain = watch->owner;
    assert(porcelain);

    switch( event->type )
    {
    case TORIRS_WIDGET_BOUND:
        watch->state.bind = PORCELAIN_BOUND;
        porcelain->any_element_bound = true;
        porcelain_forget_absence(porcelain, watch->element);
        watch->state.ref = event->widget;
        watch->pending_fences = 0;
        porcelain_read_state(porcelain, watch);
        /* A declared absence that BINDS is a failure. A stale declaration has
         * to fail loudly in both directions or it silently excuses a real
         * regression for the rest of the session. */
        if( porcelain_expected_absence(porcelain, watch->element) )
            Porcelain_RecordFinding(porcelain, "element", watch->element,
                                    PORCELAIN_FINDING_ABSENT_UNEXPECTEDLY_PRESENT, watch->role);
        break;
    case TORIRS_WIDGET_UNBOUND:
        watch->state.bind = PORCELAIN_PENDING;
        memset(&watch->state.ref, 0, sizeof(watch->state.ref));
        watch->state.presented = false;
        break;
    case TORIRS_WIDGET_STATE_CHANGED:
        watch->state.ref = event->widget;
        porcelain_read_state(porcelain, watch);
        break;
    default:
        return;
    }
    porcelain->stamp[PORCELAIN_INPUT_ELEMENT]++;
}

static void porcelain_watch_subscribe(struct Porcelain* porcelain, struct PorcelainWatch* watch);

struct PorcelainWatch*
Porcelain_WatchFor(struct Porcelain* porcelain, struct PorcelainElement element, bool create)
{
    uint64_t const key = Porcelain_ElementKey(element);
    struct PorcelainWatch* free_slot = NULL;

    assert(porcelain);
    for( int i = 0; i < PORCELAIN_WATCHES_MAX; i++ )
    {
        struct PorcelainWatch* watch = &porcelain->watches[i];
        if( !watch->used )
        {
            if( !free_slot )
                free_slot = watch;
            continue;
        }
        if( Porcelain_ElementKey(watch->element) == key )
        {
            watch->last_wanted_run = porcelain->run;
            return watch;
        }
    }
    if( !create )
        return NULL;
    if( !free_slot )
    {
        Porcelain_RecordFinding(porcelain, "element", element, PORCELAIN_FINDING_BUDGET,
                                "watch table full");
        return NULL;
    }

    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->used = true;
    free_slot->owner = porcelain;
    free_slot->element = element;
    if( element.role )
    {
        Porcelain_CopyString(free_slot->element_role, sizeof(free_slot->element_role),
                             element.role);
        free_slot->element.role = free_slot->element_role;
    }
    free_slot->last_wanted_run = porcelain->run;
    if( !Porcelain_ResolveRole(porcelain, free_slot->element, free_slot->role,
                               sizeof(free_slot->role)) &&
        free_slot->role[0] == '\0' )
    {
        /* Derived elements (CANVAS, USABLE, FRAME_ROOT) carry no role. They
         * are answered from geometry, never watched. */
        return free_slot;
    }

    porcelain_watch_subscribe(porcelain, free_slot);
    return free_slot;
}

/* watch_state and not watch: a plain watch never receives STATE_CHANGED, and
 * a follower written against BOUND/UNBOUND alone drops its controls the first
 * time a hide moves. */
static void
porcelain_watch_subscribe(struct Porcelain* porcelain, struct PorcelainWatch* watch)
{
    struct ToriRS_WidgetApi const* widgets = &porcelain->api->widgets;

    assert(porcelain);
    assert(watch);
    assert(watch->role[0]);
    porcelain->counters.engine_calls++;
    if( widgets->watch_state(widgets->context, watch->role, porcelain_watch_listener, watch) !=
        TORIRS_CONTRACT_OK )
        Porcelain_RecordFinding(porcelain, "element", watch->element, PORCELAIN_FINDING_REFUSED,
                                watch->role);
}

/* The host keys a subscription by ROLE NAME, so a re-spelling that did not
 * drop the old name would leave a dead watch in a 32-slot table. */
static void
porcelain_watch_unsubscribe(struct Porcelain* porcelain, struct PorcelainWatch* watch)
{
    struct ToriRS_WidgetApi const* widgets = &porcelain->api->widgets;

    assert(porcelain);
    assert(watch);
    assert(watch->role[0]);
    porcelain->counters.engine_calls++;
    (void)widgets->watch_state(widgets->context, watch->role, NULL, NULL);
}

/*
 * Re-ask the SPELLING, not just the binding.
 *
 * An element with more than one spelling is resolved by which one the host
 * answers, and on the first fence of a session it answers neither. A watch
 * frozen on the spelling that happened to be wrong at boot stays absent for
 * the whole run -- the 2004 lane's `tab:inventory` watching `sidetab_3` while
 * the role `tab_inventory` sat bound beside it. Only unresolved watches pay
 * for this: a bound element is never re-spelled.
 */
static void
porcelain_watch_respell(struct Porcelain* porcelain, struct PorcelainWatch* watch)
{
    char role[TORIRS_UI_NAME_MAX];

    assert(porcelain);
    assert(watch);
    if( !porcelain_has_spellings(watch->element.kind) )
        return;
    (void)Porcelain_ResolveRole(porcelain, watch->element, role, sizeof(role));
    if( role[0] == '\0' || strcmp(role, watch->role) == 0 )
        return;
    porcelain_watch_unsubscribe(porcelain, watch);
    Porcelain_CopyString(watch->role, sizeof(watch->role), role);
    watch->absence_reported = false;
    porcelain_watch_subscribe(porcelain, watch);
}

/*
 * The frame root: the clipping parent of everything AT_CANVAS and AT_USABLE.
 * Derived once by walking up from any bound element and cached, because the
 * audit measured a 32-hop root walk per fence in a shipped plugin.
 */
static bool
porcelain_frame_root(struct Porcelain* porcelain, struct ToriRS_WidgetRef* out)
{
    struct ToriRS_WidgetApi const* widgets = &porcelain->api->widgets;
    struct ToriRS_WidgetRef current;
    struct ToriRS_WidgetRef parent;
    bool found = false;

    assert(out);
    if( porcelain->frame_root_known &&
        porcelain->frame_root_stamp == porcelain->stamp[PORCELAIN_INPUT_ELEMENT] )
    {
        *out = porcelain->frame_root;
        return true;
    }
    memset(out, 0, sizeof(*out));
    for( int i = 0; i < PORCELAIN_WATCHES_MAX && !found; i++ )
    {
        struct PorcelainWatch const* watch = &porcelain->watches[i];
        if( !watch->used || watch->state.bind != PORCELAIN_BOUND )
            continue;
        current = watch->state.ref;
        found = true;
    }
    if( !found )
        return false;
    for( int hop = 0; hop < 64; hop++ )
    {
        porcelain->counters.engine_calls++;
        if( widgets->parent(widgets->context, current, &parent) != TORIRS_CONTRACT_OK )
            break;
        if( !ToriRS_WidgetRefValid(parent) )
            break;
        current = parent;
    }
    *out = current;
    porcelain->frame_root = current;
    porcelain->frame_root_known = true;
    porcelain->frame_root_stamp = porcelain->stamp[PORCELAIN_INPUT_ELEMENT];
    return true;
}

/*
 * USABLE: the frame root's box minus a LANE_CHROME strip, and only when that
 * strip is PRESENTED and spans a full edge. 601's lane strip is bound, laid
 * out 58x46 and hidden: it cuts nothing, and a plan that subtracted it
 * because it exists would move the whole frame.
 */
static struct ToriRS_WidgetBounds
porcelain_usable(struct Porcelain* porcelain)
{
    struct ToriRS_WidgetBounds usable;
    struct ToriRS_WidgetRef root;

    memset(&usable, 0, sizeof(usable));
    if( porcelain_frame_root(porcelain, &root) )
    {
        struct ToriRS_WidgetApi const* widgets = &porcelain->api->widgets;
        porcelain->counters.engine_calls++;
        if( widgets->bounds(widgets->context, root, &usable) != TORIRS_CONTRACT_OK )
            memset(&usable, 0, sizeof(usable));
    }
    for( int member = 0; member < 4; member++ )
    {
        struct PorcelainElement const strip = PORCELAIN_CHROME_EL(member);
        struct PorcelainWatch const* watch = Porcelain_WatchFor(porcelain, strip, false);
        if( !watch || watch->state.bind != PORCELAIN_BOUND || !watch->state.presented )
            continue;
        if( watch->state.box.height >= usable.height && watch->state.box.width > 0 )
        {
            /* A full-height strip on one side. */
            if( watch->state.box.x <= usable.x )
            {
                int const cut = watch->state.box.x + watch->state.box.width - usable.x;
                usable.x += cut;
                usable.width -= cut;
            }
            else
            {
                usable.width = watch->state.box.x - usable.x;
            }
        }
        else if( watch->state.box.width >= usable.width && watch->state.box.height > 0 )
        {
            if( watch->state.box.y <= usable.y )
            {
                int const cut = watch->state.box.y + watch->state.box.height - usable.y;
                usable.y += cut;
                usable.height -= cut;
            }
            else
            {
                usable.height = watch->state.box.y - usable.y;
            }
        }
    }
    return usable;
}

bool
Porcelain_Element(struct Porcelain* porcelain, struct PorcelainElement element,
                  struct PorcelainElementState* out)
{
    struct PorcelainWatch* watch;

    assert(porcelain);
    assert(out);
    assert(element.kind > PORCELAIN_EL_NONE && element.kind < PORCELAIN_EL_KIND_COUNT);

    memset(out, 0, sizeof(*out));
    if( element.kind == PORCELAIN_EL_CANVAS || element.kind == PORCELAIN_EL_SAFE ||
        element.kind == PORCELAIN_EL_USABLE || element.kind == PORCELAIN_EL_FRAME_ROOT )
    {
        struct ToriRS_WidgetRef root;
        if( !porcelain_frame_root(porcelain, &root) )
            return false;
        out->ref = root;
        out->bind = PORCELAIN_BOUND;
        out->presented = true;
        out->box = element.kind == PORCELAIN_EL_USABLE
                       ? porcelain_usable(porcelain)
                       : (struct ToriRS_WidgetBounds){0, 0, 0, 0};
        if( element.kind != PORCELAIN_EL_USABLE )
        {
            struct ToriRS_WidgetApi const* widgets = &porcelain->api->widgets;
            porcelain->counters.engine_calls++;
            (void)widgets->bounds(widgets->context, root, &out->box);
        }
        out->local = out->box;
        out->stamp = porcelain->frame;
        return true;
    }

    watch = Porcelain_WatchFor(porcelain, element, true);
    if( !watch )
        return false;
    *out = watch->state;
    if( watch->state.bind == PORCELAIN_ABSENT && !watch->absence_reported )
    {
        watch->absence_reported = true;
        Porcelain_RecordFinding(porcelain, "element", watch->element,
                                porcelain_expected_absence(porcelain, watch->element)
                                    ? PORCELAIN_FINDING_ABSENT_EXPECTED
                                    : PORCELAIN_FINDING_ABSENT,
                                watch->role);
    }
    return watch->state.bind == PORCELAIN_BOUND;
}

int
Porcelain_Count(struct Porcelain* porcelain, enum PorcelainElementKind family)
{
    struct ToriRS_WidgetRef ref;
    char role[TORIRS_UI_NAME_MAX];
    int count = 0;

    assert(porcelain);
    /*
     * A count is LANE DATA: how many of the family's roles this lane's
     * profile actually resolves. Eight filters on the desktop, seven on 601,
     * four on 2004; nobody picks a number.
     */
    switch( family )
    {
    case PORCELAIN_EL_CHAT_FILTER:
        return porcelain_count_members(porcelain, "chat_plate_%d", "chat_buttons:%d");
    case PORCELAIN_EL_LANE_CHROME:
        for( int i = 0; i < 4; i++ )
        {
            snprintf(role, sizeof(role), "lane_chrome_%d", i);
            if( porcelain_engine_find(porcelain, role, &ref) != TORIRS_CONTRACT_OK )
                break;
            count++;
        }
        return count;
    case PORCELAIN_EL_ORB:
        for( int i = 0; i < PORCELAIN_ORB_COUNT; i++ )
            if( porcelain_engine_find(porcelain, PORCELAIN_ORB_ROLES[i], &ref) ==
                TORIRS_CONTRACT_OK )
                count++;
        return count;
    case PORCELAIN_EL_TAB:
        /* The stones, not the mounts: a lane that numbers its sidebar answers
         * `sidetab_<n>`, and a lane whose stones are authored builtins carries
         * one sidebar member per stone. */
        return porcelain_count_members(porcelain, "sidetab_%d", "sidebar:%d");
    default:
        break;
    }
    /* A family with no members answers one when it binds, zero when it does
     * not: "how many of these are there" is still a real question. */
    if( Porcelain_ResolveRole(porcelain, (struct PorcelainElement){family, 0, NULL}, role,
                              sizeof(role)) &&
        porcelain_engine_find(porcelain, role, &ref) == TORIRS_CONTRACT_OK )
        return 1;
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Normalising a described item                                             */
/* ------------------------------------------------------------------------ */

static uint64_t
porcelain_item_hash(struct PorcelainNormalItem const* item)
{
    uint64_t hash = Porcelain_HashString(0, item->key.text);

    hash = Porcelain_HashBytes(hash, &item->kind, sizeof(item->kind));
    hash = Porcelain_HashBytes(hash, &item->has_image, sizeof(item->has_image));
    hash = Porcelain_HashString(hash, item->image.text);
    hash = Porcelain_HashBytes(hash, &item->place.kind, sizeof(item->place.kind));
    hash = Porcelain_HashBytes(hash, &item->place.corner_or_side,
                               sizeof(item->place.corner_or_side));
    hash = Porcelain_HashBytes(hash, &item->place.dx, sizeof(item->place.dx));
    hash = Porcelain_HashBytes(hash, &item->place.dy, sizeof(item->place.dy));
    hash = Porcelain_HashBytes(hash, &item->place.behind, sizeof(item->place.behind));
    hash = Porcelain_HashString(hash, item->place_on_role);
    hash = Porcelain_HashString(hash, item->place_depth_role);
    hash = Porcelain_HashString(hash, item->visible_with_role);
    hash = Porcelain_HashBytes(hash, &item->width, sizeof(item->width));
    hash = Porcelain_HashBytes(hash, &item->height, sizeof(item->height));
    hash = Porcelain_HashBytes(hash, &item->opacity, sizeof(item->opacity));
    hash = Porcelain_HashBytes(hash, &item->has_text, sizeof(item->has_text));
    hash = Porcelain_HashString(hash, item->text);
    hash = Porcelain_HashBytes(hash, &item->rgb, sizeof(item->rgb));
    hash = Porcelain_HashBytes(hash, &item->align, sizeof(item->align));
    hash = Porcelain_HashBytes(hash, &item->outline, sizeof(item->outline));
    hash = Porcelain_HashString(hash, item->op_label);
    hash = Porcelain_HashBytes(hash, &item->on_op, sizeof(item->on_op));
    hash = Porcelain_HashBytes(hash, &item->user, sizeof(item->user));
    hash = Porcelain_HashBytes(hash, &item->hit, sizeof(item->hit));
    hash = Porcelain_HashBytes(hash, &item->enabled, sizeof(item->enabled));
    return hash;
}

static void
porcelain_element_role_copy(struct Porcelain* porcelain, struct PorcelainElement element,
                            char* out, size_t capacity)
{
    out[0] = '\0';
    if( element.kind == PORCELAIN_EL_NONE )
        return;
    (void)Porcelain_ResolveRole(porcelain, element, out, capacity);
}

/* ------------------------------------------------------------------------ */
/* The describe builder                                                     */
/* ------------------------------------------------------------------------ */

static struct Porcelain*
porcelain_of_describe(struct ToriRS_PorcelainDescribe* describe)
{
    struct Porcelain* porcelain;

    assert(describe);
    porcelain = describe->porcelain;
    assert(porcelain);
    /* Every builder verb is legal only inside the describe run. Outside it
     * there is no scratch to write into and the applied description would be
     * mutated behind the diff's back. */
    assert(porcelain->describing);
    return porcelain;
}

static void
porcelain_push_item(struct ToriRS_PorcelainDescribe* describe, enum PorcelainItemKind kind,
                    struct PorcelainItem const* source)
{
    struct Porcelain* porcelain = porcelain_of_describe(describe);
    struct PorcelainNormalItem* item;

    assert(source);
    assert(source->key);
    assert(source->key[0]);
    assert(source->place.on.kind > PORCELAIN_EL_NONE ||
           source->place.kind == PORCELAIN_AT_CANVAS || source->place.kind == PORCELAIN_AT_USABLE);
    /* A text item has no measurable width: nothing answers a string's extent
     * in the widget's face, so the box is the plugin's to state. */
    assert(kind != PORCELAIN_ITEM_TEXT || (source->w > 0 && source->h > 0));

    if( porcelain->scratch_item_count >= PORCELAIN_ITEMS_MAX )
    {
        porcelain->scratch_poisoned = true;
        Porcelain_RecordFinding(porcelain, "item", source->place.on, PORCELAIN_FINDING_BUDGET,
                                source->key);
        return;
    }
    for( int i = 0; i < porcelain->scratch_item_count; i++ )
        /* A duplicate key would make the diff ambiguous and the later item
         * silently win. The plan's identity rule is one key, one item. */
        assert(strcmp(porcelain->scratch_items[i].key.text, source->key) != 0);

    item = &porcelain->scratch_items[porcelain->scratch_item_count];
    memset(item, 0, sizeof(*item));
    Porcelain_CopyString(item->key.text, sizeof(item->key.text), source->key);
    item->kind = kind;
    if( source->image )
    {
        item->has_image = true;
        Porcelain_CopyString(item->image.text, sizeof(item->image.text), source->image);
    }
    item->place = source->place;
    porcelain_element_role_copy(porcelain, source->place.on, item->place_on_role,
                                sizeof(item->place_on_role));
    porcelain_element_role_copy(porcelain, source->place.depth, item->place_depth_role,
                                sizeof(item->place_depth_role));
    porcelain_element_role_copy(porcelain, source->visible_with, item->visible_with_role,
                                sizeof(item->visible_with_role));
    item->width = source->w;
    item->height = source->h;
    item->opacity = source->opacity;
    if( source->text )
    {
        item->has_text = true;
        Porcelain_CopyString(item->text, sizeof(item->text), source->text);
    }
    item->rgb = source->rgb;
    item->align = source->align;
    item->outline = source->outline;
    if( source->op_label )
        Porcelain_CopyString(item->op_label, sizeof(item->op_label), source->op_label);
    item->on_op = source->on_op;
    item->user = source->user;
    item->hit = source->hit;
    item->enabled = source->enabled;
    item->visible_with = source->visible_with;
    item->hash = porcelain_item_hash(item);

    /* Register the watch dependencies this item has, so a target that moves
     * re-runs the describe without the plugin subscribing to anything. */
    if( source->place.on.kind > PORCELAIN_EL_NONE )
        (void)Porcelain_WatchFor(porcelain, source->place.on, true);
    if( source->place.depth.kind > PORCELAIN_EL_NONE )
        (void)Porcelain_WatchFor(porcelain, source->place.depth, true);
    if( source->visible_with.kind > PORCELAIN_EL_NONE )
        (void)Porcelain_WatchFor(porcelain, source->visible_with, true);

    /* REPLACE is an arbitrated aspect; the loser is not applied. */
    if( source->place.kind == PORCELAIN_REPLACE )
    {
        char const* winner = NULL;
        if( !Porcelain_ClaimTake(porcelain, source->place.on, PORCELAIN_ASPECT_REPLACE, &winner) )
        {
            Porcelain_RecordFinding(porcelain, "replace", source->place.on,
                                    PORCELAIN_FINDING_ARBITRATION_LOST, winner);
            return;
        }
    }
    porcelain->scratch_item_count++;
}

void
Porcelain_Control(struct ToriRS_PorcelainDescribe* describe, struct PorcelainItem const* item)
{
    porcelain_push_item(describe, PORCELAIN_ITEM_CONTROL, item);
}

void
Porcelain_Piece(struct ToriRS_PorcelainDescribe* describe, struct PorcelainItem const* item)
{
    porcelain_push_item(describe, PORCELAIN_ITEM_PIECE, item);
}

void
Porcelain_Text(struct ToriRS_PorcelainDescribe* describe, struct PorcelainItem const* item)
{
    porcelain_push_item(describe, PORCELAIN_ITEM_TEXT, item);
}

void
Porcelain_Blocker(struct ToriRS_PorcelainDescribe* describe, char const* key, char const* label,
                  struct PorcelainPlacement place, int width, int height, PorcelainOpFn on_op,
                  void* user)
{
    struct PorcelainItem item;

    assert(key);
    assert(label);
    memset(&item, 0, sizeof(item));
    item.key = key;
    item.image = NULL; /* An invisible hit box; no 1x1 transparent PNG needed. */
    item.place = place;
    item.w = width;
    item.h = height;
    item.op_label = label;
    item.on_op = on_op;
    item.user = user;
    item.hit = true;
    item.enabled = true;
    porcelain_push_item(describe, PORCELAIN_ITEM_BLOCKER, &item);
}

static uint64_t
porcelain_edit_hash(struct PorcelainNormalEdit const* edit)
{
    uint64_t hash = Porcelain_HashBytes(0, &edit->kind, sizeof(edit->kind));

    hash = Porcelain_HashString(hash, edit->element_role);
    hash = Porcelain_HashBytes(hash, &edit->box, sizeof(edit->box));
    hash = Porcelain_HashBytes(hash, &edit->anchor_modes, sizeof(edit->anchor_modes));
    hash = Porcelain_HashString(hash, edit->image.text);
    hash = Porcelain_HashString(hash, edit->mask.text);
    hash = Porcelain_HashBytes(hash, &edit->has_image, sizeof(edit->has_image));
    hash = Porcelain_HashBytes(hash, &edit->has_mask, sizeof(edit->has_mask));
    hash = Porcelain_HashBytes(hash, &edit->opacity, sizeof(edit->opacity));
    return hash;
}

static struct PorcelainNormalEdit*
porcelain_push_edit(struct ToriRS_PorcelainDescribe* describe, enum PorcelainEditKind kind,
                    struct PorcelainElement element)
{
    struct Porcelain* porcelain = porcelain_of_describe(describe);
    struct PorcelainNormalEdit* edit;

    assert(element.kind > PORCELAIN_EL_NONE && element.kind < PORCELAIN_EL_KIND_COUNT);
    if( porcelain->scratch_edit_count >= PORCELAIN_EDITS_MAX )
    {
        porcelain->scratch_poisoned = true;
        Porcelain_RecordFinding(porcelain, "edit", element, PORCELAIN_FINDING_BUDGET,
                                "edit table full");
        return NULL;
    }
    edit = &porcelain->scratch_edits[porcelain->scratch_edit_count];
    memset(edit, 0, sizeof(*edit));
    edit->kind = kind;
    edit->element = element;
    /* The ORIGINAL role, not the resolved one: PorcelainElementKey hashes
     * `role`, and a TAB whose name resolved to sidetab_3 must still key on
     * the name the plugin described. */
    if( element.role )
    {
        Porcelain_CopyString(edit->element_role, sizeof(edit->element_role), element.role);
        edit->element.role = edit->element_role;
    }
    (void)Porcelain_WatchFor(porcelain, element, true);
    return edit;
}

static void
porcelain_commit_edit(struct Porcelain* porcelain, struct PorcelainNormalEdit* edit,
                      enum PorcelainAspect first, enum PorcelainAspect second, bool want_second,
                      char const* verb)
{
    char const* winner = NULL;

    if( !Porcelain_ClaimTake(porcelain, edit->element, first, &winner) )
    {
        Porcelain_RecordFinding(porcelain, verb, edit->element,
                                PORCELAIN_FINDING_ARBITRATION_LOST, winner);
        return;
    }
    if( want_second && !Porcelain_ClaimTake(porcelain, edit->element, second, &winner) )
    {
        Porcelain_RecordFinding(porcelain, verb, edit->element,
                                PORCELAIN_FINDING_ARBITRATION_LOST, winner);
        return;
    }
    edit->hash = porcelain_edit_hash(edit);
    porcelain->scratch_edit_count++;
}

void
Porcelain_Move(struct ToriRS_PorcelainDescribe* describe, struct PorcelainElement element,
               struct ToriRS_WidgetBounds box, int anchor_modes)
{
    struct PorcelainNormalEdit* edit = porcelain_push_edit(describe, PORCELAIN_EDIT_MOVE, element);

    if( !edit )
        return;
    edit->box = box;
    edit->anchor_modes = anchor_modes;
    porcelain_commit_edit(describe->porcelain, edit, PORCELAIN_ASPECT_MOVE_POSITION,
                          PORCELAIN_ASPECT_MOVE_SIZE, box.width > 0 || box.height > 0, "move");
}

void
Porcelain_Hide(struct ToriRS_PorcelainDescribe* describe, struct PorcelainElement element)
{
    struct PorcelainNormalEdit* edit = porcelain_push_edit(describe, PORCELAIN_EDIT_HIDE, element);

    if( !edit )
        return;
    porcelain_commit_edit(describe->porcelain, edit, PORCELAIN_ASPECT_HIDE,
                          PORCELAIN_ASPECT_HIDE, false, "hide");
}

void
Porcelain_Skin(struct ToriRS_PorcelainDescribe* describe, struct PorcelainElement element,
               char const* image, char const* mask)
{
    struct PorcelainNormalEdit* edit = porcelain_push_edit(describe, PORCELAIN_EDIT_SKIN, element);

    if( !edit )
        return;
    /* Independent halves. A skin that states only a mask must not clear the
     * art, and the other way round. */
    assert(image || mask);
    if( image )
    {
        edit->has_image = true;
        Porcelain_CopyString(edit->image.text, sizeof(edit->image.text), image);
    }
    if( mask )
    {
        edit->has_mask = true;
        Porcelain_CopyString(edit->mask.text, sizeof(edit->mask.text), mask);
    }
    porcelain_commit_edit(describe->porcelain, edit,
                          image ? PORCELAIN_ASPECT_SKIN_ART : PORCELAIN_ASPECT_SKIN_MASK,
                          PORCELAIN_ASPECT_SKIN_MASK, image && mask, "skin");
}

void
Porcelain_Opacity(struct ToriRS_PorcelainDescribe* describe, struct PorcelainElement element,
                  int opacity)
{
    struct PorcelainNormalEdit* edit =
        porcelain_push_edit(describe, PORCELAIN_EDIT_OPACITY, element);

    if( !edit )
        return;
    assert(opacity >= 0 && opacity <= 255);
    edit->opacity = opacity;
    porcelain_commit_edit(describe->porcelain, edit, PORCELAIN_ASPECT_OPACITY,
                          PORCELAIN_ASPECT_OPACITY, false, "opacity");
}

void
Porcelain_Unsupported(struct ToriRS_PorcelainDescribe* describe, char const* reason)
{
    struct Porcelain* porcelain = porcelain_of_describe(describe);

    assert(reason);
    Porcelain_RecordFinding(porcelain, "unsupported", PORCELAIN_EL(NONE),
                            PORCELAIN_FINDING_UNSUPPORTED, reason);
}

/* ------------------------------------------------------------------------ */
/* Geometry                                                                 */
/* ------------------------------------------------------------------------ */

/*
 * REPLACE carries NO geometry in the engine: set_anchor reads and writes none
 * of it. Porcelain therefore owns the box for every placement, copies the
 * target's parent-local box plus the centring offset at every fence the
 * target moved, and creates controls as SIBLINGS under the target's parent --
 * a REPLACE from a child of the target is ANCHOR_INVALID.
 */
static struct ToriRS_WidgetBounds
porcelain_place_box(struct Porcelain* porcelain, struct PorcelainNormalItem const* item,
                    struct PorcelainElementState const* target)
{
    struct ToriRS_WidgetBounds box;
    int const width = item->width > 0 ? item->width : target->local.width;
    int const height = item->height > 0 ? item->height : target->local.height;

    box.width = width;
    box.height = height;
    switch( item->place.kind )
    {
    case PORCELAIN_REPLACE:
        /* The target's box, plus the centring offset when the control is
         * smaller than what it stands in for. */
        box.x = target->local.x + (target->local.width - width) / 2 + item->place.dx;
        box.y = target->local.y + (target->local.height - height) / 2 + item->place.dy;
        break;
    case PORCELAIN_INSIDE:
        switch( item->place.corner_or_side )
        {
        case PORCELAIN_TOP_RIGHT:
            box.x = target->local.x + target->local.width - width - item->place.dx;
            box.y = target->local.y + item->place.dy;
            break;
        case PORCELAIN_BOTTOM_LEFT:
            box.x = target->local.x + item->place.dx;
            box.y = target->local.y + target->local.height - height - item->place.dy;
            break;
        case PORCELAIN_BOTTOM_RIGHT:
            box.x = target->local.x + target->local.width - width - item->place.dx;
            box.y = target->local.y + target->local.height - height - item->place.dy;
            break;
        case PORCELAIN_CENTRE:
            box.x = target->local.x + (target->local.width - width) / 2 + item->place.dx;
            box.y = target->local.y + (target->local.height - height) / 2 + item->place.dy;
            break;
        case PORCELAIN_TOP_LEFT:
        default:
            box.x = target->local.x + item->place.dx;
            box.y = target->local.y + item->place.dy;
            break;
        }
        break;
    case PORCELAIN_BESIDE:
        switch( item->place.corner_or_side )
        {
        case PORCELAIN_LEFT:
            box.x = target->local.x - width - item->place.dx;
            box.y = target->local.y + item->place.dy;
            break;
        case PORCELAIN_ABOVE:
            box.x = target->local.x + item->place.dx;
            box.y = target->local.y - height - item->place.dy;
            break;
        case PORCELAIN_BELOW:
            box.x = target->local.x + item->place.dx;
            box.y = target->local.y + target->local.height + item->place.dy;
            break;
        case PORCELAIN_RIGHT:
        default:
            box.x = target->local.x + target->local.width + item->place.dx;
            box.y = target->local.y + item->place.dy;
            break;
        }
        break;
    case PORCELAIN_AT_ELEMENT:
        box.x = target->local.x + item->place.dx;
        box.y = target->local.y + item->place.dy;
        break;
    case PORCELAIN_AT_CANVAS:
        box.x = item->place.dx;
        box.y = item->place.dy;
        break;
    case PORCELAIN_AT_USABLE:
    default:
    {
        struct ToriRS_WidgetBounds const usable = porcelain_usable(porcelain);
        box.x = usable.x + item->place.dx;
        box.y = usable.y + item->place.dy;
        break;
    }
    }
    return box;
}

/* ------------------------------------------------------------------------ */
/* Applying one item                                                        */
/* ------------------------------------------------------------------------ */

static void
porcelain_note_result(struct Porcelain* porcelain, char const* verb,
                      struct PorcelainElement element, enum ToriRS_ContractResult result,
                      char const* detail)
{
    if( result == TORIRS_CONTRACT_OK || result == TORIRS_CONTRACT_PENDING )
        return;
    Porcelain_RecordFinding(porcelain, verb, element, PORCELAIN_FINDING_REFUSED, detail);
}

static void
porcelain_op_listener(struct ToriRS_Api* api, void* user, struct ToriRS_WidgetEvent const* event)
{
    struct PorcelainAppliedItem* applied = user;

    assert(api);
    assert(applied);
    assert(event);
    if( event->type != TORIRS_WIDGET_OPERATION )
        return;
    if( !applied->live || !applied->item.on_op )
        return;
    applied->item.on_op(api, applied->item.user, applied->item.key.text);
}

static bool
porcelain_item_target(struct Porcelain* porcelain, struct PorcelainNormalItem const* item,
                      struct PorcelainElementState* out)
{
    if( item->place.kind == PORCELAIN_AT_CANVAS || item->place.kind == PORCELAIN_AT_USABLE )
    {
        struct ToriRS_WidgetRef root;
        memset(out, 0, sizeof(*out));
        if( !porcelain_frame_root(porcelain, &root) )
            return false;
        out->bind = PORCELAIN_BOUND;
        out->ref = root;
        out->presented = true;
        return true;
    }
    return Porcelain_Element(porcelain, item->place.on, out);
}

static void
porcelain_apply_geometry(struct Porcelain* porcelain, struct PorcelainAppliedItem* applied,
                         struct ToriRS_WidgetBounds box)
{
    struct ToriRS_WidgetApi const* widgets = &porcelain->api->widgets;

    /* No early return on an unchanged box: each write below compares for
     * itself, which is what makes "zero engine calls" true without also
     * making a picture that changed under an unmoved box unreachable. */
    applied->desired = box;
    applied->desired_written = true;
    if( applied->live_x != box.x || applied->live_y != box.y )
    {
        porcelain->counters.engine_calls++;
        porcelain->counters.setters++;
        porcelain_note_result(porcelain, "set_position", applied->item.place.on,
                              widgets->set_position(widgets->context, applied->ref, box.x, box.y),
                              applied->item.key.text);
        applied->live_x = box.x;
        applied->live_y = box.y;
        porcelain->dirty = true;
    }
    if( applied->item.kind == PORCELAIN_ITEM_TEXT &&
        (applied->live_w != box.width || applied->live_h != box.height) )
    {
        porcelain->counters.engine_calls++;
        porcelain->counters.setters++;
        porcelain_note_result(
            porcelain, "set_size", applied->item.place.on,
            widgets->set_size(widgets->context, applied->ref, box.width, box.height),
            applied->item.key.text);
        applied->live_w = box.width;
        applied->live_h = box.height;
        porcelain->dirty = true;
    }
    else if( applied->item.kind != PORCELAIN_ITEM_TEXT &&
             (applied->image_dirty || applied->live_w != box.width ||
              applied->live_h != box.height) )
    {
        /* An owned image control takes its size through set_image, which is
         * also where the picture lives; the two travel together. */
        porcelain->counters.engine_calls++;
        porcelain->counters.setters++;
        porcelain_note_result(porcelain, "set_image", applied->item.place.on,
                              widgets->set_image(widgets->context, applied->ref,
                                                 applied->image_ref, box.width, box.height),
                              applied->item.key.text);
        applied->live_w = box.width;
        applied->live_h = box.height;
        applied->image_dirty = false;
        porcelain->dirty = true;
    }
}

static void
porcelain_apply_hidden(struct Porcelain* porcelain, struct PorcelainAppliedItem* applied,
                       bool hidden)
{
    struct ToriRS_WidgetApi const* widgets = &porcelain->api->widgets;

    if( applied->hidden_written && applied->applied_hidden == hidden )
        return;
    porcelain->counters.engine_calls++;
    porcelain->counters.setters++;
    porcelain_note_result(porcelain, "set_hidden", applied->item.place.on,
                          widgets->set_hidden(widgets->context, applied->ref, hidden),
                          applied->item.key.text);
    applied->applied_hidden = hidden;
    applied->hidden_written = true;
    porcelain->dirty = true;
}

/** Create the control and write everything the new item states. */
static bool
porcelain_create_item(struct Porcelain* porcelain, struct PorcelainAppliedItem* applied,
                      struct PorcelainElementState const* target)
{
    struct ToriRS_WidgetApi const* widgets = &porcelain->api->widgets;
    struct ToriRS_WidgetRef parent;
    enum ToriRS_ContractResult result;

    if( applied->item.place.kind == PORCELAIN_AT_CANVAS ||
        applied->item.place.kind == PORCELAIN_AT_USABLE )
    {
        parent = target->ref;
    }
    else
    {
        porcelain->counters.engine_calls++;
        if( widgets->parent(widgets->context, target->ref, &parent) != TORIRS_CONTRACT_OK ||
            !ToriRS_WidgetRefValid(parent) )
        {
            /* Without the target's parent there is no sibling to create, and
             * a control parented to the target itself cannot take REPLACE. */
            Porcelain_RecordFinding(porcelain, "create", applied->item.place.on,
                                    PORCELAIN_FINDING_REFUSED, "no parent");
            return false;
        }
    }

    porcelain->counters.engine_calls++;
    porcelain->counters.creates++;
    if( applied->item.kind == PORCELAIN_ITEM_TEXT )
        result = widgets->create_text(widgets->context, parent, applied->item.key.text,
                                      &applied->ref);
    else
        result = widgets->create_image(widgets->context, parent, applied->item.key.text,
                                       &applied->ref);
    if( result != TORIRS_CONTRACT_OK )
    {
        porcelain_note_result(porcelain, "create", applied->item.place.on, result,
                              applied->item.key.text);
        return false;
    }
    applied->parent = parent;
    applied->live = true;
    porcelain->dirty = true;
    return true;
}

static void
porcelain_apply_properties(struct Porcelain* porcelain, struct PorcelainAppliedItem* applied,
                           struct PorcelainNormalItem const* wanted, bool fresh)
{
    struct ToriRS_WidgetApi const* widgets = &porcelain->api->widgets;
    struct PorcelainNormalItem const previous = applied->item;

    porcelain->counters.property_applies++;
    applied->item = *wanted;
    if( applied->item.kind == PORCELAIN_ITEM_TEXT )
    {
        if( fresh || previous.has_text != wanted->has_text ||
            strcmp(previous.text, wanted->text) != 0 )
        {
            porcelain->counters.engine_calls++;
            porcelain->counters.setters++;
            porcelain_note_result(porcelain, "set_text", wanted->place.on,
                                  widgets->set_text(widgets->context, applied->ref, wanted->text),
                                  wanted->key.text);
            porcelain->dirty = true;
        }
        if( fresh || previous.rgb != wanted->rgb )
        {
            porcelain->counters.engine_calls++;
            porcelain->counters.setters++;
            porcelain_note_result(
                porcelain, "set_text_color", wanted->place.on,
                widgets->set_text_color(widgets->context, applied->ref, wanted->rgb),
                wanted->key.text);
            porcelain->dirty = true;
        }
        if( fresh || previous.align != wanted->align )
        {
            porcelain->counters.engine_calls++;
            porcelain->counters.setters++;
            /* Alignment 1 is CENTRE, which is what the shipped readout is
             * pinned to; the sketch called it LEFT and was wrong. */
            porcelain_note_result(
                porcelain, "set_text_align", wanted->place.on,
                widgets->set_text_align(widgets->context, applied->ref, wanted->align, 0),
                wanted->key.text);
            porcelain->dirty = true;
        }
        if( fresh || previous.outline != wanted->outline )
        {
            porcelain->counters.engine_calls++;
            porcelain->counters.setters++;
            porcelain_note_result(
                porcelain, "set_text_outline", wanted->place.on,
                widgets->set_text_outline(widgets->context, applied->ref, wanted->outline),
                wanted->key.text);
            porcelain->dirty = true;
        }
    }
    else if( fresh || previous.has_image != wanted->has_image ||
             strcmp(previous.image.text, wanted->image.text) != 0 )
    {
        struct ToriRS_ImageRef image;
        enum PorcelainAssetState state = PORCELAIN_ASSET_READY;
        memset(&image, 0, sizeof(image));
        if( wanted->has_image )
            image = Porcelain_Image(porcelain, wanted->image.text, &state);
        /* A NULL image is an invisible control that still exists and still
         * hits, which is what the frame providers fake with a composed 1x1
         * transparent PNG today. The write itself belongs to the geometry
         * pass, which knows the box. */
        applied->image_ref = image;
        applied->image_dirty = true;
    }

    if( fresh || previous.opacity != wanted->opacity )
    {
        int const opacity = wanted->opacity == PORCELAIN_OPACITY_DEFAULT ? 255 : wanted->opacity;
        porcelain->counters.engine_calls++;
        porcelain->counters.setters++;
        porcelain_note_result(porcelain, "set_opacity", wanted->place.on,
                              widgets->set_opacity(widgets->context, applied->ref, opacity),
                              wanted->key.text);
        applied->applied_opacity = opacity;
        porcelain->dirty = true;
    }

    if( fresh || previous.enabled != wanted->enabled || previous.hit != wanted->hit ||
        previous.on_op != wanted->on_op || strcmp(previous.op_label, wanted->op_label) != 0 )
    {
        bool const armed = wanted->hit && wanted->enabled && wanted->on_op;
        porcelain->counters.engine_calls++;
        porcelain->counters.setters++;
        /* `.enabled` false is DRAWN, INERT, NO MENU ROW -- a NULL listener,
         * not a control that keeps a dead row the way the retained menu bug
         * did. */
        porcelain_note_result(
            porcelain, "set_on_op", wanted->place.on,
            widgets->set_on_op(widgets->context, applied->ref,
                               armed ? wanted->op_label : NULL,
                               armed ? porcelain_op_listener : NULL, armed ? applied : NULL),
            wanted->key.text);
        porcelain->dirty = true;
    }
}

static void
porcelain_apply_anchor(struct Porcelain* porcelain, struct PorcelainAppliedItem* applied,
                       struct PorcelainElementState const* target, bool fresh)
{
    struct ToriRS_WidgetApi const* widgets = &porcelain->api->widgets;
    struct PorcelainElementState depth_state;
    struct ToriRS_WidgetRef anchor = target->ref;
    enum ToriRS_WidgetRelation relation;

    if( applied->item.place.depth.kind > PORCELAIN_EL_NONE )
    {
        if( Porcelain_Element(porcelain, applied->item.place.depth, &depth_state) )
            anchor = depth_state.ref;
    }
    relation = applied->item.place.kind == PORCELAIN_REPLACE
                   ? TORIRS_WIDGET_RELATION_REPLACE
                   : (applied->item.place.behind ? TORIRS_WIDGET_RELATION_BEHIND
                                                 : TORIRS_WIDGET_RELATION_OVER);
    if( !fresh && ToriRS_WidgetRefEqual(applied->anchor_target, anchor) )
        return;
    if( applied->item.place.kind == PORCELAIN_AT_CANVAS ||
        applied->item.place.kind == PORCELAIN_AT_USABLE )
    {
        /* Nothing to anchor to: the frame root IS the parent, and an anchor
         * to one's own parent is ANCHOR_INVALID. */
        applied->anchor_target = anchor;
        return;
    }
    porcelain->counters.engine_calls++;
    porcelain->counters.setters++;
    porcelain_note_result(porcelain, "set_anchor", applied->item.place.on,
                          widgets->set_anchor(widgets->context, applied->ref, anchor, relation),
                          applied->item.key.text);
    applied->anchor_target = anchor;
    porcelain->dirty = true;
}

static void
porcelain_remove_item(struct Porcelain* porcelain, struct PorcelainAppliedItem* applied)
{
    struct ToriRS_WidgetApi const* widgets = &porcelain->api->widgets;

    if( !applied->live )
        return;
    porcelain->counters.engine_calls++;
    porcelain->counters.removes++;
    (void)widgets->remove(widgets->context, applied->ref);
    memset(applied, 0, sizeof(*applied));
    porcelain->dirty = true;
}

/* ------------------------------------------------------------------------ */
/* Applying an element edit                                                 */
/* ------------------------------------------------------------------------ */

static void
porcelain_apply_edit(struct Porcelain* porcelain, struct PorcelainAppliedEdit* applied,
                     struct PorcelainNormalEdit const* wanted, bool fresh)
{
    struct ToriRS_WidgetApi const* widgets = &porcelain->api->widgets;
    struct PorcelainElementState target;

    if( !Porcelain_Element(porcelain, wanted->element, &target) )
        return;
    applied->ref = target.ref;
    applied->edit = *wanted;
    if( !fresh )
        return;
    switch( wanted->kind )
    {
    case PORCELAIN_EDIT_MOVE:
    {
        int32_t x = wanted->box.x;
        int32_t y = wanted->box.y;
        /* PORCELAIN_KEEP_RELATIVE: the member keeps its offset inside the
         * block that moved. The orb block moved as one box loses members 1
         * and 2 without it. */
        if( x == PORCELAIN_KEEP_RELATIVE )
            x = target.local.x;
        if( y == PORCELAIN_KEEP_RELATIVE )
            y = target.local.y;
        porcelain->counters.engine_calls++;
        porcelain->counters.setters++;
        porcelain_note_result(porcelain, "move", wanted->element,
                              widgets->set_position(widgets->context, target.ref, x, y),
                              wanted->element_role);
        if( wanted->box.width > 0 || wanted->box.height > 0 )
        {
            porcelain->counters.engine_calls++;
            porcelain->counters.setters++;
            porcelain_note_result(porcelain, "move", wanted->element,
                                  widgets->set_size(widgets->context, target.ref,
                                                    wanted->box.width, wanted->box.height),
                                  wanted->element_role);
        }
        break;
    }
    case PORCELAIN_EDIT_HIDE:
        porcelain->counters.engine_calls++;
        porcelain->counters.setters++;
        /* A presentation hide, never an unhide: the native bit is
         * authoritative and nothing here may clear it. */
        porcelain_note_result(porcelain, "hide", wanted->element,
                              widgets->set_hidden(widgets->context, target.ref, true),
                              wanted->element_role);
        break;
    case PORCELAIN_EDIT_SKIN:
        if( wanted->has_image )
        {
            enum PorcelainAssetState state = PORCELAIN_ASSET_READY;
            struct ToriRS_ImageRef const image =
                Porcelain_Image(porcelain, wanted->image.text, &state);
            if( state == PORCELAIN_ASSET_READY )
            {
                porcelain->counters.engine_calls++;
                porcelain->counters.setters++;
                /* Width and height 0: a native re-skin keeps the widget's own
                 * geometry, and a non-zero size is INVALID_ARGUMENT. */
                porcelain_note_result(
                    porcelain, "skin", wanted->element,
                    widgets->set_image(widgets->context, target.ref, image, 0, 0),
                    wanted->image.text);
            }
        }
        if( wanted->has_mask )
        {
            enum PorcelainAssetState state = PORCELAIN_ASSET_READY;
            struct ToriRS_ImageRef const mask =
                Porcelain_Image(porcelain, wanted->mask.text, &state);
            if( state == PORCELAIN_ASSET_READY )
            {
                porcelain->counters.engine_calls++;
                porcelain->counters.setters++;
                porcelain_note_result(porcelain, "skin", wanted->element,
                                      widgets->set_mask(widgets->context, target.ref, mask),
                                      wanted->mask.text);
            }
        }
        break;
    case PORCELAIN_EDIT_OPACITY:
        porcelain->counters.engine_calls++;
        porcelain->counters.setters++;
        porcelain_note_result(porcelain, "opacity", wanted->element,
                              widgets->set_opacity(widgets->context, target.ref, wanted->opacity),
                              wanted->element_role);
        break;
    }
    applied->live = true;
    porcelain->dirty = true;
}

static void
porcelain_release_edit(struct Porcelain* porcelain, struct PorcelainAppliedEdit* applied)
{
    struct ToriRS_WidgetApi const* widgets = &porcelain->api->widgets;

    if( !applied->live )
        return;
    porcelain->counters.engine_calls++;
    /* The description diff is the only thing that undoes a move: the engine
     * keeps no pre-edit snapshot, so a move not re-described would survive a
     * release for ever. `reset` drops exactly this owner's edits. */
    (void)widgets->reset(widgets->context, applied->ref);
    memset(applied, 0, sizeof(*applied));
    porcelain->dirty = true;
}

/* ------------------------------------------------------------------------ */
/* The reconcile                                                            */
/* ------------------------------------------------------------------------ */

static struct PorcelainAppliedItem*
porcelain_applied_by_key(struct Porcelain* porcelain, char const* key)
{
    for( int i = 0; i < PORCELAIN_ITEMS_MAX; i++ )
        if( porcelain->applied_items[i].live &&
            strcmp(porcelain->applied_items[i].item.key.text, key) == 0 )
            return &porcelain->applied_items[i];
    return NULL;
}

static struct PorcelainAppliedItem*
porcelain_applied_free(struct Porcelain* porcelain)
{
    for( int i = 0; i < PORCELAIN_ITEMS_MAX; i++ )
        if( !porcelain->applied_items[i].live )
            return &porcelain->applied_items[i];
    return NULL;
}

static bool
porcelain_scratch_has_key(struct Porcelain const* porcelain, char const* key)
{
    for( int i = 0; i < porcelain->scratch_item_count; i++ )
        if( strcmp(porcelain->scratch_items[i].key.text, key) == 0 )
            return true;
    return false;
}

static void
porcelain_run_describe(struct Porcelain* porcelain)
{
    porcelain->run++;
    porcelain->counters.describe_runs++;
    porcelain->scratch_item_count = 0;
    porcelain->scratch_edit_count = 0;
    porcelain->scratch_poisoned = false;
    porcelain->describing = true;
    porcelain->describe_fn(&porcelain->builder, porcelain->describe_user);
    porcelain->describing = false;
    porcelain->ever_described = true;
    Porcelain_ClaimDropStale(porcelain);
}

static void
porcelain_reconcile(struct Porcelain* porcelain)
{
    /* A describe that overran a capacity leaves the applied description
     * exactly as it was. Half a description is the flicker class. */
    if( porcelain->scratch_poisoned )
        return;

    /* 1. A key not re-described is removed -- and ONLY that control. */
    for( int i = 0; i < PORCELAIN_ITEMS_MAX; i++ )
    {
        struct PorcelainAppliedItem* applied = &porcelain->applied_items[i];
        if( applied->live && !porcelain_scratch_has_key(porcelain, applied->item.key.text) )
            porcelain_remove_item(porcelain, applied);
    }

    /* 2. Items, in DESCRIPTION ORDER, so a later item is over an earlier
     *    one: the engine's order comes from the order of these calls. */
    for( int i = 0; i < porcelain->scratch_item_count; i++ )
        if( porcelain->scratch_items[i].has_image )
            Porcelain_ImageTouch(porcelain, porcelain->scratch_items[i].image.text);
    for( int i = 0; i < porcelain->scratch_edit_count; i++ )
    {
        if( porcelain->scratch_edits[i].has_image )
            Porcelain_ImageTouch(porcelain, porcelain->scratch_edits[i].image.text);
        if( porcelain->scratch_edits[i].has_mask )
            Porcelain_ImageTouch(porcelain, porcelain->scratch_edits[i].mask.text);
    }
    for( int i = 0; i < porcelain->scratch_item_count; i++ )
    {
        struct PorcelainNormalItem const* wanted = &porcelain->scratch_items[i];
        struct PorcelainAppliedItem* applied =
            porcelain_applied_by_key(porcelain, wanted->key.text);
        struct PorcelainElementState target;
        bool fresh = false;

        if( !porcelain_item_target(porcelain, wanted, &target) )
        {
            /* PENDING defers (the run reports PENDING and re-runs on the next
             * input stamp); ABSENT skips with the one finding Element already
             * recorded. Either way a control that exists for a target that no
             * longer binds is removed, never left floating and armed. */
            if( applied )
                porcelain_remove_item(porcelain, applied);
            continue;
        }
        if( !applied )
        {
            applied = porcelain_applied_free(porcelain);
            if( !applied )
            {
                Porcelain_RecordFinding(porcelain, "item", wanted->place.on,
                                        PORCELAIN_FINDING_BUDGET, wanted->key.text);
                continue;
            }
            memset(applied, 0, sizeof(*applied));
            applied->owner = porcelain;
            applied->item = *wanted;
            if( !porcelain_create_item(porcelain, applied, &target) )
            {
                memset(applied, 0, sizeof(*applied));
                continue;
            }
            fresh = true;
        }
        else if( applied->item.hash == wanted->hash )
        {
            /* Unchanged hash: NO engine call for any property. Geometry still
             * follows the target, and that costs nothing when it did not
             * move. */
            porcelain_apply_geometry(porcelain, applied,
                                     porcelain_place_box(porcelain, wanted, &target));
            porcelain_apply_anchor(porcelain, applied, &target, false);
            goto visibility;
        }
        porcelain_apply_properties(porcelain, applied, wanted, fresh);
        porcelain_apply_geometry(porcelain, applied,
                                 porcelain_place_box(porcelain, wanted, &target));
        porcelain_apply_anchor(porcelain, applied, &target, fresh);

    visibility:
    {
        bool hidden = !target.presented;
        if( applied->item.visible_with.kind > PORCELAIN_EL_NONE )
        {
            struct PorcelainElementState gate;
            /* OVER inherits nothing: a plate over a hidden compass stays
             * unless the description says it travels with it. */
            if( Porcelain_Element(porcelain, applied->item.visible_with, &gate) )
                hidden = hidden || !gate.presented;
            else
                hidden = true;
        }
        if( applied->item.place.kind == PORCELAIN_REPLACE )
            /* REPLACE inherits the target's native visibility in the engine;
             * restating it here would fight that inheritance. */
            hidden = false;
        porcelain_apply_hidden(porcelain, applied, hidden);
    }
    }

    /* 3. Edits: retained while described, released when not. */
    for( int i = 0; i < PORCELAIN_EDITS_MAX; i++ )
    {
        struct PorcelainAppliedEdit* applied = &porcelain->applied_edits[i];
        bool still_wanted = false;
        if( !applied->live )
            continue;
        for( int j = 0; j < porcelain->scratch_edit_count; j++ )
            if( porcelain->scratch_edits[j].kind == applied->edit.kind &&
                Porcelain_ElementKey(porcelain->scratch_edits[j].element) ==
                    Porcelain_ElementKey(applied->edit.element) )
                still_wanted = true;
        if( !still_wanted )
            porcelain_release_edit(porcelain, applied);
    }
    for( int i = 0; i < porcelain->scratch_edit_count; i++ )
    {
        struct PorcelainNormalEdit const* wanted = &porcelain->scratch_edits[i];
        struct PorcelainAppliedEdit* slot = NULL;
        bool fresh = true;
        for( int j = 0; j < PORCELAIN_EDITS_MAX && !slot; j++ )
            if( porcelain->applied_edits[j].live &&
                porcelain->applied_edits[j].edit.kind == wanted->kind &&
                Porcelain_ElementKey(porcelain->applied_edits[j].edit.element) ==
                    Porcelain_ElementKey(wanted->element) )
            {
                slot = &porcelain->applied_edits[j];
                fresh = slot->edit.hash != wanted->hash;
            }
        if( !slot )
            for( int j = 0; j < PORCELAIN_EDITS_MAX && !slot; j++ )
                if( !porcelain->applied_edits[j].live )
                    slot = &porcelain->applied_edits[j];
        if( !slot )
        {
            Porcelain_RecordFinding(porcelain, "edit", wanted->element, PORCELAIN_FINDING_BUDGET,
                                    "applied edit table full");
            continue;
        }
        porcelain_apply_edit(porcelain, slot, wanted, fresh);
    }
    porcelain->applied_item_count = porcelain->scratch_item_count;
    porcelain->applied_edit_count = porcelain->scratch_edit_count;
}

/* ------------------------------------------------------------------------ */
/* Fence and commit                                                         */
/* ------------------------------------------------------------------------ */

static bool
porcelain_inputs_moved(struct Porcelain const* porcelain)
{
    for( int i = 0; i < PORCELAIN_INPUT_COUNT; i++ )
        if( porcelain->stamp[i] != porcelain->applied_stamp[i] )
            return true;
    return false;
}

/*
 * PENDING elements are retried here and only here: a watch that has failed to
 * resolve for PORCELAIN_ABSENT_FENCES fences is a lane fact, not a race. This
 * is the readiness convergence every plugin wrote by hand, in one place.
 *
 * Nothing settles until this plugin has seen SOMETHING bind, and that is not
 * a refinement -- it is the difference between the findings channel being
 * usable and being noise. A gameframe takes hundreds of frames to mount: its
 * root opens, its packs load, its scripts rearrange it. An element asked for
 * during that is not absent, it is early, and the first live run of this
 * library reported twenty-two elements absent that all bound moments later.
 *
 * The screen saying GAME is not the signal -- it says so long before the frame
 * exists, which is how that first run still got its twenty-two with a
 * readiness gate in place. The signal that a lane HAS an interface is that one
 * of its elements resolved, and it costs nothing to know. Until then every
 * element stays PENDING, which is what PENDING is for. A client that never
 * binds anything reports no absences at all, which is right: twenty-six
 * findings tell nobody anything an empty screen did not.
 */
static void
porcelain_resolve_pending(struct Porcelain* porcelain)
{
    struct ToriRS_WidgetRef ref;

    for( int i = 0; i < PORCELAIN_WATCHES_MAX; i++ )
    {
        struct PorcelainWatch* watch = &porcelain->watches[i];
        if( !watch->used || watch->role[0] == '\0' )
            continue;
        if( watch->state.bind == PORCELAIN_BOUND )
        {
            watch->pending_fences = 0;
            continue;
        }
        porcelain_watch_respell(porcelain, watch);
        if( watch->state.bind == PORCELAIN_ABSENT )
        {
            /* An element that comes back binds through the watch listener, so
             * ABSENT is never permanent -- only quiet. */
            if( porcelain_engine_find(porcelain, watch->role, &ref) == TORIRS_CONTRACT_OK )
            {
                watch->state.bind = PORCELAIN_BOUND;
                porcelain->any_element_bound = true;
                porcelain_forget_absence(porcelain, watch->element);
                watch->state.ref = ref;
                watch->absence_reported = false;
                porcelain_read_state(porcelain, watch);
                porcelain->stamp[PORCELAIN_INPUT_ELEMENT]++;
            }
            continue;
        }
        if( porcelain_engine_find(porcelain, watch->role, &ref) == TORIRS_CONTRACT_OK )
        {
            watch->state.bind = PORCELAIN_BOUND;
            porcelain->any_element_bound = true;
            porcelain_forget_absence(porcelain, watch->element);
            watch->state.ref = ref;
            porcelain_read_state(porcelain, watch);
            porcelain->stamp[PORCELAIN_INPUT_ELEMENT]++;
            continue;
        }
        if( !porcelain->any_element_bound )
        {
            /* Nothing of this lane's interface has resolved yet: still
             * mounting, so nothing here is absent. */
            watch->pending_fences = 0;
            continue;
        }
        if( ++watch->pending_fences >= PORCELAIN_ABSENT_FENCES )
        {
            watch->state.bind = PORCELAIN_ABSENT;
            porcelain->stamp[PORCELAIN_INPUT_ELEMENT]++;
        }
    }
}

static void
porcelain_flush_epoch(void)
{
    if( !g_epoch_dirty || !g_epoch_api )
    {
        g_epoch_dirty = false;
        g_epoch++;
        return;
    }
    if( ToriRS_WidgetRefValid(g_epoch_revalidate_ref) )
    {
        struct ToriRS_WidgetApi const* widgets = &g_epoch_api->widgets;
        /* EXACTLY ONE revalidate for every plugin together. Each one is a
         * full-tree resolve: the xp-drop path was paying fourteen a frame. */
        (void)widgets->revalidate(widgets->context, g_epoch_revalidate_ref);
        for( int i = 0; i < PORCELAIN_HANDLES_MAX; i++ )
            if( g_handles[i].used && g_handles[i].fenced_epoch == g_epoch )
                g_handles[i].counters.revalidates++;
    }
    g_epoch_dirty = false;
    memset(&g_epoch_revalidate_ref, 0, sizeof(g_epoch_revalidate_ref));
    g_epoch_api = NULL;
    g_epoch++;
}

static void
porcelain_mark_epoch(struct Porcelain* porcelain)
{
    if( !porcelain->dirty )
        return;
    g_epoch_dirty = true;
    g_epoch_api = porcelain->api;
    for( int i = 0; i < PORCELAIN_ITEMS_MAX; i++ )
        if( porcelain->applied_items[i].live )
        {
            g_epoch_revalidate_ref = porcelain->applied_items[i].ref;
            return;
        }
    for( int i = 0; i < PORCELAIN_WATCHES_MAX; i++ )
        if( porcelain->watches[i].used &&
            porcelain->watches[i].state.bind == PORCELAIN_BOUND )
        {
            g_epoch_revalidate_ref = porcelain->watches[i].state.ref;
            return;
        }
}

void
Porcelain_Fence(struct Porcelain* porcelain)
{
    assert(porcelain);
    assert(porcelain->used);

    if( porcelain->fenced_epoch == g_epoch )
    {
        /* Somebody skipped the commit. Flush rather than drop the writes, and
         * say so: a silently late revalidate is a frame of stale layout. */
        Porcelain_RecordFinding(porcelain, "commit", PORCELAIN_EL(NONE), PORCELAIN_FINDING_BUDGET,
                                "fence without commit");
        porcelain_flush_epoch();
    }
    porcelain->fenced_epoch = g_epoch;
    porcelain->frame++;
    porcelain->dirty = false;

    porcelain_resolve_pending(porcelain);
    Porcelain_HelpersFence(porcelain);

    if( porcelain->describe_fn && (porcelain_inputs_moved(porcelain) || !porcelain->ever_described) )
    {
        /*
         * At most two passes. Registering a watch inside a describe can raise
         * BOUND synchronously, which moves the element stamp DURING the run
         * that was meant to consume it; without the second pass the first
         * settled frame would still be re-describing on the next one for
         * ever, and the steady state would never cost nothing.
         */
        for( int pass = 0; pass < 2; pass++ )
        {
            memcpy(porcelain->applied_stamp, porcelain->stamp, sizeof(porcelain->stamp));
            porcelain_run_describe(porcelain);
            if( !porcelain_inputs_moved(porcelain) )
                break;
        }
        porcelain_reconcile(porcelain);
    }
    else if( porcelain->describe_fn )
    {
        /* No input moved. Geometry still follows every target, and that is
         * zero engine calls when no target box moved. */
        for( int i = 0; i < PORCELAIN_ITEMS_MAX; i++ )
        {
            struct PorcelainAppliedItem* applied = &porcelain->applied_items[i];
            struct PorcelainElementState target;
            if( !applied->live )
                continue;
            if( !porcelain_item_target(porcelain, &applied->item, &target) )
                continue;
            porcelain_apply_geometry(porcelain, applied,
                                     porcelain_place_box(porcelain, &applied->item, &target));
        }
    }
    porcelain_mark_epoch(porcelain);
}

void
Porcelain_Commit(struct ToriRS_Api* api)
{
    assert(api);
    if( !g_epoch_api )
        g_epoch_api = api;
    porcelain_flush_epoch();
}

/* ------------------------------------------------------------------------ */
/* The direct motion path                                                   */
/* ------------------------------------------------------------------------ */

enum ToriRS_Result
Porcelain_Set(struct Porcelain* porcelain, char const* key, struct PorcelainMotion const* motion)
{
    struct ToriRS_WidgetApi const* widgets;
    struct PorcelainAppliedItem* applied;

    assert(porcelain);
    assert(key);
    assert(motion);

    applied = porcelain_applied_by_key(porcelain, key);
    if( !applied )
    {
        /* A key that is not in the applied description is a plugin that
         * believes it drew something it did not. */
        Porcelain_RecordFinding(porcelain, "set", PORCELAIN_EL(NONE), PORCELAIN_FINDING_REFUSED,
                                key);
        return TORIRS_RESULT_NOT_FOUND;
    }
    widgets = &porcelain->api->widgets;
    if( motion->mask & (PORCELAIN_MOTION_X | PORCELAIN_MOTION_Y) )
    {
        int32_t const x = (motion->mask & PORCELAIN_MOTION_X) ? motion->x : applied->live_x;
        int32_t const y = (motion->mask & PORCELAIN_MOTION_Y) ? motion->y : applied->live_y;
        if( x != applied->live_x || y != applied->live_y )
        {
            porcelain->counters.engine_calls++;
            porcelain->counters.setters++;
            porcelain_note_result(porcelain, "set", applied->item.place.on,
                                  widgets->set_position(widgets->context, applied->ref, x, y),
                                  key);
            applied->live_x = x;
            applied->live_y = y;
            porcelain->dirty = true;
        }
    }
    if( motion->mask & PORCELAIN_MOTION_OPACITY )
    {
        assert(motion->opacity >= 0 && motion->opacity <= 255);
        if( motion->opacity != applied->applied_opacity )
        {
            porcelain->counters.engine_calls++;
            porcelain->counters.setters++;
            porcelain_note_result(
                porcelain, "set", applied->item.place.on,
                widgets->set_opacity(widgets->context, applied->ref, motion->opacity), key);
            applied->applied_opacity = motion->opacity;
            porcelain->dirty = true;
        }
    }
    if( (motion->mask & PORCELAIN_MOTION_IMAGE) && motion->image )
    {
        if( strcmp(applied->item.image.text, motion->image) != 0 )
        {
            enum PorcelainAssetState state = PORCELAIN_ASSET_READY;
            struct ToriRS_ImageRef const image = Porcelain_Image(porcelain, motion->image, &state);
            porcelain->counters.engine_calls++;
            porcelain->counters.setters++;
            porcelain_note_result(porcelain, "set", applied->item.place.on,
                                  widgets->set_image(widgets->context, applied->ref, image,
                                                     applied->live_w, applied->live_h),
                                  key);
            applied->image_ref = image;
            Porcelain_CopyString(applied->item.image.text, sizeof(applied->item.image.text),
                                 motion->image);
            applied->item.has_image = true;
            porcelain->dirty = true;
        }
    }
    porcelain_mark_epoch(porcelain);
    return TORIRS_RESULT_OK;
}

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                */
/* ------------------------------------------------------------------------ */

static void
porcelain_builder_init(struct Porcelain* porcelain)
{
    memset(&porcelain->builder, 0, sizeof(porcelain->builder));
    porcelain->builder.struct_size = sizeof(porcelain->builder);
    porcelain->builder.implementation = porcelain;
    porcelain->builder.porcelain = porcelain;
    porcelain->builder.control = Porcelain_Control;
    porcelain->builder.piece = Porcelain_Piece;
    porcelain->builder.text = Porcelain_Text;
    porcelain->builder.blocker = Porcelain_Blocker;
    porcelain->builder.move = Porcelain_Move;
    porcelain->builder.hide = Porcelain_Hide;
    porcelain->builder.skin = Porcelain_Skin;
    porcelain->builder.opacity = Porcelain_Opacity;
    porcelain->builder.unsupported = Porcelain_Unsupported;
}

struct Porcelain*
Porcelain_Open(struct ToriRS_Api* api, struct ToriRS_PluginDef const* def, void* state)
{
    struct Porcelain* porcelain = NULL;

    assert(api);
    assert(def);
    assert(def->id);

    for( int i = 0; i < PORCELAIN_HANDLES_MAX; i++ )
        if( !g_handles[i].used )
        {
            porcelain = &g_handles[i];
            break;
        }
    /* A handle table that is full is a build that registered more plugins
     * than the layer was sized for; carrying on would silently unarbitrate
     * every one of them. */
    assert(porcelain);

    memset(porcelain, 0, sizeof(*porcelain));
    porcelain->used = true;
    porcelain->order = g_handle_count++;
    porcelain->api = api;
    porcelain->def = def;
    porcelain->state = state;
    Porcelain_CopyString(porcelain->plugin_id, sizeof(porcelain->plugin_id), def->id);
    porcelain_builder_init(porcelain);
    return porcelain;
}

void
Porcelain_Close(struct Porcelain* porcelain)
{
    /* A deallocator. free(NULL) is an idiom and so is this. */
    if( !porcelain )
        return;
    assert(porcelain->used);

    porcelain_trace_final_findings(porcelain);
    for( int i = 0; i < PORCELAIN_ITEMS_MAX; i++ )
        porcelain_remove_item(porcelain, &porcelain->applied_items[i]);
    for( int i = 0; i < PORCELAIN_EDITS_MAX; i++ )
        porcelain_release_edit(porcelain, &porcelain->applied_edits[i]);
    Porcelain_OverlayRelease(porcelain);
    Porcelain_ReleaseAllAssets(porcelain);
    /* Relinquish BEFORE the handle dies: the host keeps both frame providers
     * alive for one fence by design, so an outgoing provider that kept its
     * claims would hand the incoming one eight findings instead of a frame. */
    Porcelain_ClaimDropAll(porcelain);
    memset(porcelain, 0, sizeof(*porcelain));
}

void
Porcelain_Describe(struct Porcelain* porcelain, PorcelainDescribeFn fn, void* user)
{
    assert(porcelain);
    assert(fn);
    porcelain->describe_fn = fn;
    porcelain->describe_user = user;
    porcelain->stamp[PORCELAIN_INPUT_EXPLICIT]++;
}

void
Porcelain_Note(struct Porcelain* porcelain, enum PorcelainInput input)
{
    assert(porcelain);
    assert(input >= 0 && input < PORCELAIN_INPUT_COUNT);
    porcelain->stamp[input]++;
}

void
Porcelain_Invalidate(struct Porcelain* porcelain)
{
    Porcelain_Note(porcelain, PORCELAIN_INPUT_EXPLICIT);
}

void
Porcelain_CountersRead(struct Porcelain* porcelain, struct PorcelainCounters* out)
{
    assert(porcelain);
    assert(out);
    *out = porcelain->counters;
}

void
Porcelain_CountersReset(struct Porcelain* porcelain)
{
    assert(porcelain);
    memset(&porcelain->counters, 0, sizeof(porcelain->counters));
}

void
Porcelain_ResetForTesting(void)
{
    memset(g_handles, 0, sizeof(g_handles));
    memset(g_claims, 0, sizeof(g_claims));
    g_handle_count = 0;
    g_epoch = 1;
    g_epoch_dirty = false;
    g_epoch_api = NULL;
    memset(&g_epoch_revalidate_ref, 0, sizeof(g_epoch_revalidate_ref));
}

/* ------------------------------------------------------------------------ */
/* The table                                                                */
/* ------------------------------------------------------------------------ */

static struct ToriRS_PorcelainApi const PORCELAIN_TABLE = {
    .struct_size = sizeof(struct ToriRS_PorcelainApi),
    .open = Porcelain_Open,
    .close = Porcelain_Close,
    .describe = Porcelain_Describe,
    .invalidate = Porcelain_Invalidate,
    .note = Porcelain_Note,
    .fence = Porcelain_Fence,
    .commit = Porcelain_Commit,
    .relinquish = Porcelain_Relinquish,
    .element = Porcelain_Element,
    .count = Porcelain_Count,
    .set = Porcelain_Set,
    .findings = Porcelain_Findings,
    .expect_absent = Porcelain_ExpectAbsent,
    .has = Porcelain_Has,
    .require = Porcelain_Require,
    .tier = Porcelain_Tier,
    .tiers_from_config = Porcelain_TiersFromConfig,
    .config_list_add = Porcelain_ConfigListAdd,
    .menu_tag = Porcelain_MenuTag,
    .setting = Porcelain_Setting,
    .key_edge = Porcelain_KeyEdge,
    .image = Porcelain_Image,
    .model = Porcelain_Model,
    .derived = Porcelain_Derived,
    .when_ready = Porcelain_WhenReady,
    .every = Porcelain_Every,
    .every_server_tick = Porcelain_EveryServerTick,
    .every_ms = Porcelain_EveryMs,
    .tick = Porcelain_Tick,
    .draw_context = Porcelain_DrawContext,
    .menu_add = Porcelain_MenuAdd,
    .note_menu = Porcelain_NoteMenu,
    .hover = Porcelain_Hover,
    .native_overlay = Porcelain_NativeOverlay,
    .note_script = Porcelain_NoteScript,
    .table = Porcelain_Table,
    .notify = Porcelain_Notify,
};

struct ToriRS_PorcelainApi const*
ToriRS_PorcelainApiTable(void)
{
    return &PORCELAIN_TABLE;
}
