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
 *
 * TWO NAMES, and the second is the one that was missing. Everything this gate
 * guards is a PLUGIN diagnostic -- a finding, a reparent, a re-create -- and
 * it answered only to the WIDGET-TREE trace, which is a different subject that
 * a capture has to know to ask for separately. A plugin whose entire output on
 * a lane is its finding (`nxt-cannon-ammo` declining a revision with no cannon
 * varps is the case that found this) therefore produced a log with nothing in
 * it, and the capture was indistinguishable from a plugin that never ran.
 * TORIRS_PLUGIN_LOG is the flag that already means "log what the plugins did",
 * and every capture in the shot harness sets it.
 *
 * Not a lane fact and not a per-lane switch: both lanes read the same two
 * names.
 */
static int g_trace = -1;

static bool
porcelain_trace_enabled(void)
{
    if( g_trace < 0 )
        g_trace = (getenv("TORIRS_TRACE_NATIVE_UI") || getenv("TORIRS_PLUGIN_LOG")) ? 1 : 0;
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
    if( !source )
    {
        destination[0] = '\0';
        return;
    }
    length = strlen(source);
    /*
     * An over-long key, role or caption is a caller bug: truncated, it would
     * silently alias a different identity. So it asserts -- and the assert
     * carries the string, because "length < capacity" on its own tells you
     * nothing about WHICH of a plugin's forty strings was too long, and two
     * people have now spent a debug cycle finding that out by hand.
     */
    assert(length < capacity && source);
    /*
     * And it must not run off the end when NDEBUG has removed that assert.
     *
     * This used to copy length + 1 bytes unconditionally. In a release build,
     * which is every build the gate and the matrix run, that is not a
     * truncation -- it is a buffer overflow, past the end of whatever struct
     * holds the destination, with the caller's own string as the payload. A
     * 145-character reason found it. Clamping is the lesser evil by a wide
     * margin, and the assert above is what stops an aliasing truncation ever
     * reaching a release build in the first place.
     */
    if( length >= capacity )
        length = capacity - 1;
    memcpy(destination, source, length);
    destination[length] = '\0';
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

/*
 * A declared lane limitation, matched on the finding's DETAIL.
 *
 * Every route that records an UNSUPPORTED finding already carries the feature
 * name there -- Require's `feature`, the describe builder's `reason` -- so one
 * declaration covers all of them without a second key. Without this, a plugin
 * that honestly said "this lane cannot do X" failed the clean gate for saying
 * so, and going quiet was the only way to pass.
 *
 * It answers with the DECLARATION and not a yes-or-no, because the reason is
 * the half a reader needs. Both lookups used to answer bool, the `why` the two
 * expect verbs stored was read by nothing at all, and an UNSUPPORTED finding
 * therefore named its feature twice -- once as the element and once as the
 * detail -- and never said the cause. Returning the slot costs the same walk
 * and hands the caller both answers.
 */
static struct PorcelainExpectUnsupported*
porcelain_unsupported_declaration(struct Porcelain* porcelain, char const* detail)
{
    if( !detail )
        return NULL;
    for( int i = 0; i < PORCELAIN_EXPECT_UNSUPPORTED_MAX; i++ )
        if( porcelain->expect_unsupported[i].used &&
            strcmp(porcelain->expect_unsupported[i].feature, detail) == 0 )
            return &porcelain->expect_unsupported[i];
    return NULL;
}

static struct PorcelainExpectAbsent*
porcelain_absence_declaration(struct Porcelain* porcelain, struct PorcelainElement element)
{
    uint64_t const key = Porcelain_ElementKey(element);

    for( int i = 0; i < PORCELAIN_EXPECT_MAX; i++ )
        if( porcelain->expects[i].used &&
            Porcelain_ElementKey(porcelain->expects[i].element) == key )
            return &porcelain->expects[i];
    return NULL;
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

/*
 * ` why=<the declaration's reason>`, or NOTHING AT ALL.
 *
 * Written only where there is a reason to write, and that is not tidiness: an
 * undeclared finding is the line every log comparator keys on, and giving all
 * of them a constant `why=-` would have renamed every one of those keys for a
 * field that says nothing. The field is present exactly when a declaration
 * covers the finding -- which is exactly the line a reader is asking "why is
 * this here" about.
 */
static void
porcelain_why_field(char const* why, char* out, size_t capacity)
{
    assert(why);
    assert(out);
    assert(capacity > 0);
    if( !why[0] )
    {
        out[0] = '\0';
        return;
    }
    snprintf(out, capacity, " why=%s", why);
}

void
Porcelain_RecordFinding(struct Porcelain* porcelain, char const* verb,
                        struct PorcelainElement element, int result, char const* detail)
{
    struct PorcelainFindingSlot* free_slot = NULL;
    struct PorcelainExpectUnsupported const* unsupported_declaration;
    struct PorcelainExpectAbsent const* absence_declaration;
    char const* why = "";
    char why_field[PORCELAIN_DETAIL_MAX + 8];
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
    /*
     * The declaration that covers this finding, asked ONCE for both of its
     * answers: whether the finding is expected, and the sentence the plugin
     * wrote to say why. An ABSENT_EXPECTED finding is expected by its own
     * result and still carries a declaration, so it is looked up too.
     */
    unsupported_declaration = result == PORCELAIN_FINDING_UNSUPPORTED
                                  ? porcelain_unsupported_declaration(porcelain, detail)
                                  : NULL;
    absence_declaration = (result == PORCELAIN_FINDING_ABSENT ||
                           result == PORCELAIN_FINDING_ABSENT_EXPECTED)
                              ? porcelain_absence_declaration(porcelain, element)
                              : NULL;
    if( unsupported_declaration )
        why = unsupported_declaration->why;
    else if( absence_declaration )
        why = absence_declaration->why;
    Porcelain_CopyString(free_slot->why, sizeof(free_slot->why), why);
    free_slot->pub.why = free_slot->why;
    free_slot->pub.expected = result == PORCELAIN_FINDING_ABSENT_EXPECTED ||
                              (result == PORCELAIN_FINDING_ABSENT && absence_declaration != NULL) ||
                              (result == PORCELAIN_FINDING_UNSUPPORTED &&
                               unsupported_declaration != NULL);
    free_slot->pub.first_frame = porcelain->frame;
    free_slot->pub.count = 1;

    if( !porcelain_trace_enabled() )
        return;
    Porcelain_FormatElement(free_slot->pub.element, text, sizeof(text));
    /* One line at birth, with the running count readable through `findings`
     * and restated at close. A line per frame is the defect the record calls
     * "logged every frame or not at all". */
    porcelain_why_field(free_slot->why, why_field, sizeof(why_field));
    fprintf(stderr,
            "PORCELAIN_FINDING plugin=%s verb=%s element=%s result=%d detail=%s "
            "expected=%d%s first_frame=%u count=%u\n",
            porcelain->plugin_id, free_slot->verb, text, free_slot->pub.result,
            free_slot->detail[0] ? free_slot->detail : "-", free_slot->pub.expected ? 1 : 0,
            why_field, free_slot->pub.first_frame, free_slot->pub.count);
}

static void
porcelain_trace_final_findings(struct Porcelain* porcelain)
{
    char why_field[PORCELAIN_DETAIL_MAX + 8];
    char text[PORCELAIN_NAME_MAX];

    if( !porcelain_trace_enabled() )
        return;
    for( int i = 0; i < PORCELAIN_FINDINGS_MAX; i++ )
    {
        struct PorcelainFindingSlot* slot = &porcelain->findings[i];
        if( !slot->used || slot->pub.count < 2 )
            continue;
        Porcelain_FormatElement(slot->pub.element, text, sizeof(text));
        porcelain_why_field(slot->why, why_field, sizeof(why_field));
        fprintf(stderr,
                "PORCELAIN_FINDING plugin=%s verb=%s element=%s result=%d detail=%s "
                "expected=%d%s first_frame=%u count=%u\n",
                porcelain->plugin_id, slot->verb, text, slot->pub.result,
                slot->detail[0] ? slot->detail : "-", slot->pub.expected ? 1 : 0, why_field,
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

/* An absence reported before the declaration arrived becomes the declared
 * one, in place, keeping its first_frame and its count -- and its REASON, so
 * a finding that predates the declaration reads back the same as one that
 * follows it. */
static void
porcelain_relabel_absence(struct Porcelain* porcelain, struct PorcelainElement element,
                          char const* why)
{
    assert(why);
    for( int i = 0; i < PORCELAIN_FINDINGS_MAX; i++ )
    {
        struct PorcelainFindingSlot* slot = &porcelain->findings[i];
        if( !slot->used || slot->pub.result != PORCELAIN_FINDING_ABSENT )
            continue;
        if( Porcelain_ElementKey(slot->pub.element) != Porcelain_ElementKey(element) )
            continue;
        slot->pub.result = PORCELAIN_FINDING_ABSENT_EXPECTED;
        slot->pub.expected = true;
        Porcelain_CopyString(slot->why, sizeof(slot->why), why);
        slot->pub.why = slot->why;
    }
}

/*
 * The same, for a limitation. Matched on the finding's DETAIL, because that is
 * where the feature name lands on every route that records an UNSUPPORTED
 * one -- `require`, `notify` with no notifier, the overlay latch.
 *
 * WHAT THIS DOES NOT FIX, said here so nobody relies on it: the TRACE LINE.
 * A finding is written to stderr once, at birth, and that line carries the
 * label it had then. Relabelling corrects the table -- what `findings` answers
 * and what a plugin can read back -- and leaves an `expected=0` line in a
 * capture that the log-reading gates will still fail on. A plugin whose
 * limitation must be clean in a CAPTURE has to declare it before the verb that
 * discovers it; the three nxt builtins do exactly that, and say why.
 */
void
Porcelain_RelabelUnsupported(struct Porcelain* porcelain, char const* feature)
{
    struct PorcelainExpectUnsupported const* declaration;

    assert(porcelain);
    assert(feature);
    declaration = porcelain_unsupported_declaration(porcelain, feature);
    for( int i = 0; i < PORCELAIN_FINDINGS_MAX; i++ )
    {
        struct PorcelainFindingSlot* slot = &porcelain->findings[i];
        if( !slot->used || slot->pub.result != PORCELAIN_FINDING_UNSUPPORTED )
            continue;
        if( strcmp(slot->detail, feature) != 0 )
            continue;
        slot->pub.expected = true;
        /* The declaration is already in the table when this runs -- that is
         * what prompted the call -- so the reason comes from there rather
         * than through a second parameter that could disagree with it. */
        if( declaration )
        {
            Porcelain_CopyString(slot->why, sizeof(slot->why), declaration->why);
            slot->pub.why = slot->why;
        }
    }
}

void
Porcelain_ExpectAbsent(struct Porcelain* porcelain, struct PorcelainElement element,
                       char const* why)
{
    assert(porcelain);
    assert(why);
    /*
     * Legal at any time, and that is the point. The honest source for "this
     * lane has no report button" is the element STATE, which is PENDING at
     * on_start -- so an on_start-only verb could only be called
     * unconditionally, and an unconditional declaration failed every lane
     * that HAS the element with ABSENT_UNEXPECTEDLY_PRESENT. A plugin now
     * declares where the absence is reported, and the already-recorded
     * finding is re-labelled below rather than left to fail the clean gate.
     * The other direction still bites: a declared-absent element that BINDS
     * is a failure, so a stale declaration cannot go quiet.
     */
    porcelain_relabel_absence(porcelain, element, why);
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

/*
 * Something new bound: nothing is absent yet.
 *
 * The absence clock used to run from the first ask, and it starts as soon as
 * ANY element of the handle binds -- which for an overlay is the moment its
 * one target arrives and for a frame PROVIDER is two fences before the
 * toplevel finishes mounting. The provider gates its whole description on the
 * viewport; the chat, the sidebar and the modal of the same toplevel bind
 * after it, and at two fences all three were reported absent, once each, on
 * every desktop lane, and bound on the fence after.
 *
 * A lane is still mounting for as long as things are still arriving, so that
 * is the rule: the clock counts fences since the last NEW binding rather than
 * fences since the first ask. An element the lane really does not have is
 * still reported, two fences after the last of its neighbours turned up.
 */
static void
porcelain_note_new_binding(struct Porcelain* porcelain)
{
    for( int i = 0; i < PORCELAIN_WATCHES_MAX; i++ )
        if( porcelain->watches[i].used && porcelain->watches[i].state.bind == PORCELAIN_PENDING )
            porcelain->watches[i].pending_fences = 0;
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
        porcelain_note_new_binding(porcelain);
        porcelain_read_state(porcelain, watch);
        /* A declared absence that BINDS is a failure. A stale declaration has
         * to fail loudly in both directions or it silently excuses a real
         * regression for the rest of the session. */
        if( porcelain_absence_declaration(porcelain, watch->element) )
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

/*
 * The tree listener: one subscription for the whole handle.
 *
 * TREE_CHANGED is topology and only topology -- a move, a hide or a re-skin
 * raises nothing here -- which makes it the exact signal a SETTLED unresolved
 * watch needs, and nothing a bound one does.
 *
 * The contract's opening notification is taken as news like any other, and
 * that costs nothing rather than needing a guard: a watch is only ABSENT
 * after the absence clock has run, which takes fences, and every PENDING
 * watch is re-asked on those fences anyway.
 */
static void
porcelain_tree_listener(struct ToriRS_Api* api, void* user, struct ToriRS_WidgetEvent const* event)
{
    struct Porcelain* porcelain = user;

    assert(api);
    (void)api;
    assert(porcelain);
    assert(event);
    if( event->type != TORIRS_WIDGET_TREE_CHANGED )
        return;
    porcelain->tree_moved = true;
}

/*
 * Taken beside the first role watch and never again.
 *
 * Here rather than in Porcelain_Open because a subscription is only legal
 * inside this plugin's own non-draw callback, which is exactly the context
 * `porcelain_watch_subscribe` has already proved it is in -- and a handle
 * that never watches a role has nothing to poll and owes the host nothing.
 */
static void
porcelain_tree_subscribe(struct Porcelain* porcelain)
{
    struct ToriRS_WidgetApi const* widgets = &porcelain->api->widgets;

    assert(porcelain);
    if( porcelain->tree_subscribed )
        return;
    assert(widgets->watch_tree);
    porcelain->tree_subscribed = true;
    porcelain->counters.engine_calls++;
    if( widgets->watch_tree(widgets->context, porcelain_tree_listener, porcelain) !=
        TORIRS_CONTRACT_OK )
    {
        /*
         * Without it there is no signal, and a settled absence would stay
         * settled for the life of the session rather than quietly costing a
         * role lookup a frame. Say so once: a silent downgrade here is the
         * kind of thing that reads as "the lane does not have that element".
         */
        porcelain->tree_subscribed = false;
        Porcelain_RecordFinding(porcelain, "element", PORCELAIN_EL(NONE),
                                PORCELAIN_FINDING_REFUSED, "@tree");
    }
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
    porcelain_tree_subscribe(porcelain);
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
        /*
         * CREATED, not merely read.
         *
         * A read-only lookup answers NULL until something else has asked for
         * the same element, so the rect came back as the whole root and the
         * subtraction this function exists to do never happened -- silently,
         * because an un-subtracted rect is a perfectly plausible answer.
         * Measured: the resizable frame laid itself out in the 807-wide canvas
         * OldSchool grows to keep its own frame whole beside interface 728's
         * popout strip, which pushed the strip out to 807, which grew the
         * canvas to 849, and the two chased each other a frame at a time. The
         * provider it replaced found the strip by name and got it right.
         */
        struct PorcelainWatch const* watch = Porcelain_WatchFor(porcelain, strip, true);
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
                                porcelain_absence_declaration(porcelain, watch->element)
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
    /*
     * A canvas placement has no depth target, and saying so is worth a
     * finding rather than a quiet drop.
     *
     * AT_CANVAS, AT_USABLE and WITHIN all PARENT, and a child is already
     * after every subtree inside its parent. For an OVERLAY that is the whole
     * answer and the reason those placements cost no anchor: one live anchor
     * switches on the frame's depth handling, which the audit priced at
     * 13.5 ms a frame, and a unit with no anchor cannot be got above by
     * another unit's ordering.
     *
     * For a FRAME PROVIDER it is the wrong answer, and refusing the depth
     * target outright made one impossible to write. Its surround is placed at
     * canvas coordinates under the clipping root -- there is no other parent
     * that does not clip -- and it must draw UNDER the lane's own chat, map,
     * panels and orb block, every one of which is inside that same subtree.
     * "After every subtree inside it" is exactly what it must not be.
     * Measured on classic548: the surround painted over the inventory's
     * contents, the orb block's globe and wiki banner, and the XP button.
     *
     * So a depth target STATED on a parenting placement is honoured: the item
     * still parents to the root, and the anchor says where in the order it
     * goes. An anchor to one's own parent would be ANCHOR_INVALID, so the
     * target has to be something else -- which a stated depth always is, or
     * the plugin would not have stated it. Nothing is charged to a placement
     * that states none, so every overlay keeps its anchorless form.
     * @see the gameframe-layout port report.
     */
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
    /* The layer composes keys for the second node a REPLACE's hit box needs,
     * and the tilde is what it composes them with. A described key carrying
     * one could name a node the layer means to own by itself -- and
     * create_image is idempotent per (parent, key), so the two would silently
     * become one widget. @see PORCELAIN_HIT_KEY_MARK. */
    assert(strchr(source->key, PORCELAIN_HIT_KEY_MARK) == NULL);

    /* -1 is PORCELAIN_OPACITY_INVISIBLE; 0 is the unset field of a zeroed
     * struct. Anything below that is a plugin computing into the field. */
    assert(source->opacity >= PORCELAIN_OPACITY_INVISIBLE);
    assert(source->opacity <= 255);

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
    hash = Porcelain_HashBytes(hash, &edit->over.kind, sizeof(edit->over.kind));
    hash = Porcelain_HashBytes(hash, &edit->over.member, sizeof(edit->over.member));
    hash = Porcelain_HashString(hash, edit->over_role);
    hash = Porcelain_HashBytes(hash, &edit->behind, sizeof(edit->behind));
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
Porcelain_Raise(struct ToriRS_PorcelainDescribe* describe, struct PorcelainElement element,
                struct PorcelainElement over, bool behind)
{
    struct PorcelainNormalEdit* edit = porcelain_push_edit(describe, PORCELAIN_EDIT_RAISE, element);

    if( !edit )
        return;
    edit->over = over;
    if( over.role )
    {
        Porcelain_CopyString(edit->over_role, sizeof(edit->over_role), over.role);
        edit->over.role = edit->over_role;
    }
    edit->behind = behind;
    /* Watched like any other described element, so that a target which
     * rebinds re-runs the description that raised over it. Kind NONE is this
     * plugin's own topmost control and watches nothing. */
    if( over.kind > PORCELAIN_EL_NONE )
        (void)Porcelain_WatchFor(describe->porcelain, over, true);
    porcelain_commit_edit(describe->porcelain, edit, PORCELAIN_ASPECT_DEPTH,
                          PORCELAIN_ASPECT_DEPTH, false, "raise");
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
    Porcelain_FrameNoteUnsupported(porcelain, reason);
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
    /*
     * Zero is the PICTURE's own size, which is what the field is documented
     * to mean; the target's size is the last resort, for a text box and for a
     * control with no picture at all. It used to read the target's outright,
     * so a w=0 REPLACE camera stretched to the report button's 80x22 -- and
     * the only way round it was to ask api->assets for the size of the handle
     * this layer had just handed out.
     */
    int natural_width = 0;
    int natural_height = 0;
    int width;
    int height;
    /* INSIDE's coordinates are the target's PARENT's; WITHIN's are the
     * target's own, so its origin is zero. */
    int const origin_x = item->place.kind == PORCELAIN_WITHIN ? 0 : target->local.x;
    int const origin_y = item->place.kind == PORCELAIN_WITHIN ? 0 : target->local.y;

    if( item->kind != PORCELAIN_ITEM_TEXT && item->has_image &&
        (item->width <= 0 || item->height <= 0) )
        (void)Porcelain_ImageSize(porcelain, item->image.text, &natural_width, &natural_height);
    width = item->width > 0 ? item->width
                            : (natural_width > 0 ? natural_width : target->local.width);
    height = item->height > 0 ? item->height
                              : (natural_height > 0 ? natural_height : target->local.height);

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
    case PORCELAIN_WITHIN:
        switch( item->place.corner_or_side )
        {
        case PORCELAIN_TOP_RIGHT:
            box.x = origin_x + target->local.width - width - item->place.dx;
            box.y = origin_y + item->place.dy;
            break;
        case PORCELAIN_BOTTOM_LEFT:
            box.x = origin_x + item->place.dx;
            box.y = origin_y + target->local.height - height - item->place.dy;
            break;
        case PORCELAIN_BOTTOM_RIGHT:
            box.x = origin_x + target->local.width - width - item->place.dx;
            box.y = origin_y + target->local.height - height - item->place.dy;
            break;
        case PORCELAIN_CENTRE:
            box.x = origin_x + (target->local.width - width) / 2 + item->place.dx;
            box.y = origin_y + (target->local.height - height) / 2 + item->place.dy;
            break;
        case PORCELAIN_TOP_LEFT:
        default:
            box.x = origin_x + item->place.dx;
            box.y = origin_y + item->place.dy;
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

/*
 * A setter's answer about one of THIS plugin's own controls.
 *
 * STALE_REFERENCE is the one result that is not a refusal: it says the engine
 * has already destroyed the node, which happens whenever the tree the control
 * hangs in is replaced -- a frame root swap, a toplevel rebuild. The layer
 * repairs that by itself (the next reconcile finds no live node and creates
 * one), so reporting it to the plugin is telling it about the layer's own
 * housekeeping. Measured on the gate's remount lane: exactly one, a tab face
 * whose picture changed on the fence between the toplevel being replaced and
 * the frame root rebinding, and nothing a provider could have done about it.
 *
 * It is not swallowed, either: the slot is MARKED, so the reconcile stops
 * writing at the dead node instead of doing it again on every fence the
 * description keeps matching, and the ordinary re-create runs on the fence
 * the element rebinds.
 *
 * EVERY setter that writes at one of this item's own two nodes comes through
 * here, and that is the whole of the rule. It used to be only the three the
 * geometry pass makes -- set_position, set_size, set_image -- and the split
 * was invisible while the only measured remount also moved a box. It is not
 * invisible on a readout: performance-display states four rows INSIDE the
 * viewport and never moves them, so the geometry pass writes nothing after
 * the create and the only per-frame setter is set_text. On the CS1 lane the
 * four controls die at frame 7 and their element does not rebind until 207,
 * and for every one of those two hundred fences the string had moved, so the
 * layer wrote at the dead node and filed the answer as the plugin's refusal:
 * 363 dead engine calls and one undeclared PORCELAIN_FINDING that no CS2 lane
 * raises, which is a port-gate failure on a lane difference the plugin cannot
 * see, let alone declare.
 *
 * `detail` rather than applied->item.key.text, because a REPLACE's hit box is
 * this item's SECOND node and is named `<key>__hit` in a finding; both hang
 * under applied->parent and die in the same event, so either answering
 * STALE_REFERENCE marks the same slot.
 *
 * Two setters deliberately stay OUT: `create`, whose STALE_REFERENCE is about
 * the PARENT and whose repair is the re-create arm rather than this flag (and
 * marking there was measured as sixty-three `create tab.04` findings), and
 * `set_anchor`, which takes a second reference and so cannot say WHICH of the
 * two nodes the engine called dead.
 */
static void
porcelain_note_item_result(struct Porcelain* porcelain, struct PorcelainAppliedItem* applied,
                           char const* verb, enum ToriRS_ContractResult result,
                           char const* detail)
{
    assert(porcelain);
    assert(applied);
    assert(verb);
    assert(detail);
    if( result == TORIRS_CONTRACT_STALE_REFERENCE )
    {
        /*
         * Marked, and NOTHING else.
         *
         * Not an input stamp, which is the obvious move and was measured
         * wrong: bumping PORCELAIN_INPUT_ELEMENT makes every item's target
         * read as moved, so the whole description is torn down and rebuilt
         * against a root that has not rebound yet, and the rebuild is refused
         * -- sixty-three `create tab.04` findings on the gate's remount lane,
         * in place of the one refusal this arm exists to remove. The element
         * stamp belongs to the widget events that actually say what the tree
         * did; a dead reference is a symptom of one of those, not a second
         * source of truth about it.
         *
         * The repair is the ordinary one: the fence the element rebinds,
         * porcelain_item_target_moved fires, the slot is removed and built
         * again with the new parent. Until then this flag keeps the reconcile
         * from writing at a node that is not there.
         */
        applied->stale = true;
        return;
    }
    porcelain_note_result(porcelain, verb, applied->item.place.on, result, detail);
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

    /*
     * THE DESCRIPTION SAYS THE SAME THING IT SAID LAST TIME: write nothing.
     *
     * This is the arm that makes Porcelain_Set usable, and its absence made
     * the verb unusable by the one plugin whose subject is motion. The fence
     * re-asserts every live item's geometry from the description on the
     * no-input-moved branch -- which it must, or a control whose target moved
     * stops following it -- and the direct path writes `live_*` and leaves
     * `desired` alone. So a Set that moved a control was undone on the very
     * next fence: two set_position calls per moving control per frame, where
     * the direct path exists to cost one, and xp-drop-orbs drove its motion
     * through the description plus an invalidate instead.
     *
     * The rule the two boxes were built for, finally written down: if nothing
     * the DESCRIPTION says has changed, the layer has nothing to say. A
     * target that moved changes `box`, and then the animation IS rebased --
     * which is the right way round, because a control whose parent moved and
     * kept its old offset is drift, and drift survives every screenshot.
     *
     * `image_dirty` is the exception, and it is why this is not simply an
     * early return on an equal box: an owned image control takes its picture
     * through the same set_image as its size, so a picture that changed under
     * an unmoved box has to reach the engine.
     */
    if( applied->desired_written && !applied->image_dirty && applied->desired.x == box.x &&
        applied->desired.y == box.y && applied->desired.width == box.width &&
        applied->desired.height == box.height )
        return;
    applied->desired = box;
    applied->desired_written = true;
    /* Nothing defers for a picture that does not exist, so the flag the
     * property pass raised for it is answered here and once. */
    if( applied->item.kind != PORCELAIN_ITEM_TEXT && !applied->item.has_image )
        applied->image_dirty = false;
    if( applied->live_x != box.x || applied->live_y != box.y )
    {
        porcelain->counters.engine_calls++;
        porcelain->counters.setters++;
        porcelain_note_item_result(
            porcelain, applied, "set_position",
            widgets->set_position(widgets->context, applied->ref, box.x, box.y),
            applied->item.key.text);
        applied->live_x = box.x;
        applied->live_y = box.y;
        porcelain->dirty = true;
    }
    if( (applied->item.kind == PORCELAIN_ITEM_TEXT || !applied->item.has_image) &&
        (applied->live_w != box.width || applied->live_h != box.height) )
    {
        /*
         * A control with NO PICTURE takes its box through set_size, not
         * set_image.
         *
         * `image = NULL` is the layer's own spelling of "exists, draws
         * nothing" -- an invisible hit box, which is what Porcelain_Blocker
         * is and what a REPLACE's hit box below is. It has no image handle,
         * and the engine refuses a zero one outright (INVALID_ARGUMENT in
         * widget_set_image, which resolves the token before it looks at the
         * size), so routing its box through set_image wrote the picture and
         * the SIZE in one refused call: every blocker the layer has ever
         * described stayed 0x0, drew nothing and hit nothing, and the two
         * frame providers ship a 1x1 transparent PNG to get a box at all.
         * The testbed answered OK to a zero handle, which is why the suite
         * never saw it.
         */
        porcelain->counters.engine_calls++;
        porcelain->counters.setters++;
        porcelain_note_item_result(
            porcelain, applied, "set_size",
            widgets->set_size(widgets->context, applied->ref, box.width, box.height),
            applied->item.key.text);
        applied->live_w = box.width;
        applied->live_h = box.height;
        porcelain->dirty = true;
    }
    else if( applied->item.kind != PORCELAIN_ITEM_TEXT && applied->item.has_image &&
             applied->image_state != PORCELAIN_ASSET_PENDING &&
             (applied->image_dirty || applied->live_w != box.width ||
              applied->live_h != box.height) )
    {
        /* An owned image control takes its size through set_image, which is
         * also where the picture lives; the two travel together.
         *
         * And NOT while that picture is still decoding. Porcelain_Image
         * answers PENDING with a zero handle, the property pass stores it,
         * and this wrote it: the engine refuses a zero image ref outright, so
         * every control described before its art landed cost one REFUSED
         * set_image. porcelain_refresh_image already asks again on the fence
         * the asset lands and raises image_dirty, so the write is deferred
         * rather than lost -- which is also why live_w/live_h must not be
         * updated here. @see porcelain_refresh_image. */
        porcelain->counters.engine_calls++;
        porcelain->counters.setters++;
        porcelain_note_item_result(porcelain, applied, "set_image",
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
    porcelain_note_item_result(porcelain, applied, "set_hidden",
                               widgets->set_hidden(widgets->context, applied->ref, hidden),
                               applied->item.key.text);
    applied->applied_hidden = hidden;
    applied->hidden_written = true;
    porcelain->dirty = true;
}

/** Create the control and write everything the new item states. */
/*
 * The picture a PENDING asset could not give us yet.
 *
 * The property pass resolves an image only when the item is fresh or the
 * NAME changed, and an asset landing changes neither -- so a control created
 * while its picture was still loading kept the blank it was created with for
 * the rest of the session, and every port worked round it by refusing to
 * describe until the asset was READY. Asking again costs one table lookup per
 * unfinished picture per fence and nothing at all once it has landed.
 */
static void
porcelain_refresh_image(struct Porcelain* porcelain, struct PorcelainAppliedItem* applied)
{
    struct ToriRS_ImageRef image;
    enum PorcelainAssetState state;

    if( !applied->live || applied->item.kind == PORCELAIN_ITEM_TEXT )
        return;
    if( !applied->item.has_image || applied->image_state != PORCELAIN_ASSET_PENDING )
        return;
    image = Porcelain_Image(porcelain, applied->item.image.text, &state);
    applied->image_state = state;
    if( state != PORCELAIN_ASSET_READY || image.value == applied->image_ref.value )
        return;
    applied->image_ref = image;
    applied->image_dirty = true;
}

/*
 * WHICH node a described item belongs under, by placement kind alone.
 *
 * Factored out because the answer has to be asked twice: once at create, and
 * once at every reconcile, to find out that it has CHANGED. It was only asked
 * at create, and a control whose target remounted under a different parent
 * stayed a child of the parent it was born under -- moved by the setters into
 * coordinates that mean something else, which is the defect the minimap-orbs
 * port carries about a hundred lines of settle counters and incarnation
 * guards to work around.
 */
static bool
porcelain_item_parent(struct Porcelain* porcelain, struct PorcelainNormalItem const* item,
                      struct PorcelainElementState const* target, struct ToriRS_WidgetRef* out)
{
    struct ToriRS_WidgetApi const* widgets = &porcelain->api->widgets;

    assert(item);
    assert(target);
    assert(out);
    if( item->place.kind == PORCELAIN_AT_CANVAS || item->place.kind == PORCELAIN_AT_USABLE ||
        item->place.kind == PORCELAIN_WITHIN )
    {
        *out = target->ref;
        return true;
    }
    porcelain->counters.engine_calls++;
    if( widgets->parent(widgets->context, target->ref, out) != TORIRS_CONTRACT_OK ||
        !ToriRS_WidgetRefValid(*out) )
        return false;
    return true;
}

/*
 * Could this item's parent have changed since it was created?
 *
 * Asked before the engine is, because `widgets.parent` is an engine call and
 * the layer's whole claim is that an identical re-description costs nothing.
 * Three things can move the answer and all three are already in hand: the
 * placement kind (which decides WHICH node is the parent), the target's
 * reference (a remount is a new node), and the ELEMENT input stamp (a
 * re-place or a rebuild reaches this library only as a widget event, and
 * every one of those bumps it).
 *
 * The element stamp and NOT the state's own `stamp` field: that one is the
 * fence counter, and a widget event raised between two fences carries the
 * fence that has not ended yet -- the same number the create recorded. It
 * reads like a fine trigger and never fires.
 */
static bool
porcelain_item_target_moved(struct Porcelain const* porcelain,
                            struct PorcelainAppliedItem const* applied,
                            struct PorcelainNormalItem const* wanted,
                            struct PorcelainElementState const* target)
{
    if( applied->item.place.kind != wanted->place.kind )
        return true;
    if( !ToriRS_WidgetRefEqual(applied->target_ref, target->ref) )
        return true;
    return applied->target_element_stamp != porcelain->stamp[PORCELAIN_INPUT_ELEMENT];
}

/**
 * Does this control still exist?
 *
 * There is no verb that asks -- so this asks the cheapest read there is and
 * reads only the RESULT: a ref whose node the engine has freed answers
 * STALE_REFERENCE from every entry point, which is how the two shipped Lua
 * op tables already detect a retired control. The visibility itself is
 * discarded.
 *
 * Asked ONLY where the target moved and the parent did not, which is the one
 * case that can be wrong: everywhere else either the control was just created,
 * or the parent changed and it is being re-made anyway, or nothing about the
 * tree moved at all. Asking unconditionally would be one engine call per item
 * per reconcile for an answer that is almost always yes.
 */
static bool
porcelain_item_alive(struct Porcelain* porcelain, struct PorcelainAppliedItem const* applied)
{
    struct ToriRS_WidgetApi const* widgets = &porcelain->api->widgets;
    bool visible = false;

    porcelain->counters.engine_calls++;
    return widgets->visible(widgets->context, applied->ref, &visible) !=
           TORIRS_CONTRACT_STALE_REFERENCE;
}

static bool
porcelain_create_item(struct Porcelain* porcelain, struct PorcelainAppliedItem* applied,
                      struct PorcelainElementState const* target)
{
    struct ToriRS_WidgetApi const* widgets = &porcelain->api->widgets;
    struct ToriRS_WidgetRef parent;
    enum ToriRS_ContractResult result;

    if( !porcelain_item_parent(porcelain, &applied->item, target, &parent) )
    {
        /* Without the target's parent there is no sibling to create, and a
         * control parented to the target itself cannot take REPLACE. */
        Porcelain_RecordFinding(porcelain, "create", applied->item.place.on,
                                PORCELAIN_FINDING_REFUSED, "no parent");
        return false;
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
    applied->target_ref = target->ref;
    applied->target_element_stamp = porcelain->stamp[PORCELAIN_INPUT_ELEMENT];
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
            porcelain_note_item_result(
                porcelain, applied, "set_text",
                widgets->set_text(widgets->context, applied->ref, wanted->text),
                wanted->key.text);
            porcelain->dirty = true;
        }
        if( fresh || previous.rgb != wanted->rgb )
        {
            porcelain->counters.engine_calls++;
            porcelain->counters.setters++;
            porcelain_note_item_result(
                porcelain, applied, "set_text_color",
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
            porcelain_note_item_result(
                porcelain, applied, "set_text_align",
                widgets->set_text_align(widgets->context, applied->ref, wanted->align, 0),
                wanted->key.text);
            porcelain->dirty = true;
        }
        if( fresh || previous.outline != wanted->outline )
        {
            porcelain->counters.engine_calls++;
            porcelain->counters.setters++;
            porcelain_note_item_result(
                porcelain, applied, "set_text_outline",
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
        applied->image_state = state;
        applied->image_dirty = true;
    }

    if( fresh || previous.opacity != wanted->opacity )
    {
        int const opacity = wanted->opacity == PORCELAIN_OPACITY_DEFAULT      ? 255
                            : wanted->opacity == PORCELAIN_OPACITY_INVISIBLE ? 0
                                                                             : wanted->opacity;
        /*
         * A described opacity that FELL to zero is a fade that reached its
         * floor, and zero is the one value this field cannot carry: it is the
         * unset field of a zeroed struct. Inverting it into "fully painted"
         * without a word is the class this layer exists to remove, so say it.
         * PORCELAIN_OPACITY_INVISIBLE is the value that means what the author
         * of such a loop meant.
         */
        if( !fresh && wanted->opacity == PORCELAIN_OPACITY_DEFAULT &&
            previous.opacity != PORCELAIN_OPACITY_DEFAULT )
            Porcelain_RecordFinding(porcelain, "opacity", wanted->place.on,
                                    PORCELAIN_FINDING_REFUSED, wanted->key.text);
        porcelain->counters.engine_calls++;
        porcelain->counters.setters++;
        porcelain_note_item_result(porcelain, applied, "set_opacity",
                                   widgets->set_opacity(widgets->context, applied->ref, opacity),
                                   wanted->key.text);
        applied->applied_opacity = opacity;
        porcelain->dirty = true;
    }

}

/*
 * What the PICTURE's own op should be.
 *
 * Not a line inside the property pass any more, because it is no longer a
 * function of the description alone: a REPLACE whose hit box is standing
 * (below) must not also offer the row from the picture, or the pointer over
 * the art builds "Take screenshot" twice. The hit box comes and goes with the
 * target's presentation, which the description knows nothing about, so the
 * arm is compared against what was last written rather than against the
 * previous description.
 */
static void
porcelain_apply_op(struct Porcelain* porcelain, struct PorcelainAppliedItem* applied,
                   struct PorcelainNormalItem const* wanted)
{
    struct ToriRS_WidgetApi const* widgets = &porcelain->api->widgets;
    /* `.enabled` false is DRAWN, INERT, NO MENU ROW -- a NULL listener, not a
     * control that keeps a dead row the way the retained menu bug did. */
    bool const armed = wanted->hit && wanted->enabled && wanted->on_op && !applied->hit_live;

    if( applied->op_armed_written && applied->op_armed == armed &&
        (!armed || strcmp(applied->op_label_armed, wanted->op_label) == 0) )
        return;
    porcelain->counters.engine_calls++;
    porcelain->counters.setters++;
    porcelain_note_item_result(
        porcelain, applied, "set_on_op",
        widgets->set_on_op(widgets->context, applied->ref, armed ? wanted->op_label : NULL,
                           armed ? porcelain_op_listener : NULL, armed ? applied : NULL),
        wanted->key.text);
    applied->op_armed = armed;
    applied->op_armed_written = true;
    Porcelain_CopyString(applied->op_label_armed, sizeof(applied->op_label_armed),
                         wanted->op_label);
    porcelain->dirty = true;
}

/*
 * Does this item's REPLACE owe the target's box a hit?
 *
 * Only an ARMED one: a REPLACE that states `hit = false` is paint standing in
 * for paint, and the plugin has said in as many words that nothing should
 * answer there.
 */
static bool
porcelain_replace_wants_hit(struct PorcelainNormalItem const* item)
{
    assert(item);
    return item->place.kind == PORCELAIN_REPLACE && item->kind != PORCELAIN_ITEM_TEXT &&
           item->hit && item->enabled && item->on_op != NULL;
}

static void
porcelain_remove_hit(struct Porcelain* porcelain, struct PorcelainAppliedItem* applied)
{
    struct ToriRS_WidgetApi const* widgets = &porcelain->api->widgets;

    assert(porcelain);
    assert(applied);
    if( !applied->hit_live )
        return;
    porcelain->counters.engine_calls++;
    porcelain->counters.removes++;
    (void)widgets->remove(widgets->context, applied->hit_ref);
    applied->hit_live = false;
    applied->hit_box_written = false;
    applied->hit_anchor_written = false;
    applied->hit_label[0] = '\0';
    memset(&applied->hit_ref, 0, sizeof(applied->hit_ref));
    memset(&applied->hit_box, 0, sizeof(applied->hit_box));
    memset(&applied->hit_anchor_target, 0, sizeof(applied->hit_anchor_target));
    porcelain->dirty = true;
}

/*
 * THE BOX A REPLACE CONSUMED, ANSWERED.
 *
 * A REPLACE does not overlay the target: the frame reorder DROPS the target's
 * records -- paint, input and hover in one list -- and writes this control's
 * where they were (uitree_frame.c, anchor_plan_write). The control is the size
 * of its picture, so the difference between the two boxes answers nothing at
 * all: the native op is not there to take the click and neither is the
 * replacement. On the shipped camera that is a ring 1,497 pixels wide around a
 * 20x16 icon in a 79x23 button, on both lanes, and it reads as a button that
 * ignores most of itself.
 *
 * So the layer stands a second owned node the size of the TARGET's box and
 * arms the op on that one. It is an ordinary sibling anchored OVER the target,
 * not a second REPLACE -- REPLACE is arbitrated one to an element, and the
 * reorder writes the OVER children after the replacement anyway, so the hit
 * box is in the frame whether or not the picture is.
 *
 * It draws nothing (no picture, hence no emit: `scene_id <= 0` returns false)
 * and it shares this applied slot, so the op reports the ITEM's key and
 * Porcelain_Set still moves the picture. The picture is disarmed while this
 * stands -- @see porcelain_apply_op -- or the pointer over the art would build
 * the row twice.
 */
static void
porcelain_apply_replace_hit(struct Porcelain* porcelain, struct PorcelainAppliedItem* applied,
                            struct PorcelainNormalItem const* wanted,
                            struct PorcelainElementState const* target)
{
    struct ToriRS_WidgetApi const* widgets = &porcelain->api->widgets;
    char key[PORCELAIN_KEY_MAX + sizeof(PORCELAIN_HIT_KEY_SUFFIX)];

    assert(porcelain);
    assert(applied);
    assert(wanted);
    assert(target);

    /*
     * Gone with the target's presentation, rather than merely hidden.
     *
     * `set_hidden` writes the node's own widget_hidden bit, and the menu
     * walk's gate for the node ITSELF (UITree_NodeNativeGate: `hit_visible`)
     * does not read that bit -- so a hidden invisible box would keep taking
     * clicks over a Report button the lane had put away. The picture does not
     * have that problem: its REPLACE anchor makes the engine inherit the
     * target's visibility both ways.
     */
    if( !porcelain_replace_wants_hit(wanted) || !target->presented )
    {
        porcelain_remove_hit(porcelain, applied);
        return;
    }
    snprintf(key, sizeof(key), "%s" PORCELAIN_HIT_KEY_SUFFIX, wanted->key.text);
    if( !applied->hit_live )
    {
        enum ToriRS_ContractResult const result =
            widgets->create_image(widgets->context, applied->parent, key, &applied->hit_ref);
        porcelain->counters.engine_calls++;
        porcelain->counters.creates++;
        if( result != TORIRS_CONTRACT_OK )
        {
            memset(&applied->hit_ref, 0, sizeof(applied->hit_ref));
            porcelain_note_result(porcelain, "create", wanted->place.on, result, key);
            return;
        }
        applied->hit_live = true;
        applied->hit_box_written = false;
        applied->hit_anchor_written = false;
        applied->hit_label[0] = '\0';
        porcelain->dirty = true;
    }
    if( !applied->hit_box_written || applied->hit_box.x != target->local.x ||
        applied->hit_box.y != target->local.y )
    {
        porcelain->counters.engine_calls++;
        porcelain->counters.setters++;
        porcelain_note_item_result(
            porcelain, applied, "set_position",
            widgets->set_position(widgets->context, applied->hit_ref, target->local.x,
                                  target->local.y),
            key);
        porcelain->dirty = true;
    }
    if( !applied->hit_box_written || applied->hit_box.width != target->local.width ||
        applied->hit_box.height != target->local.height )
    {
        porcelain->counters.engine_calls++;
        porcelain->counters.setters++;
        porcelain_note_item_result(
            porcelain, applied, "set_size",
            widgets->set_size(widgets->context, applied->hit_ref, target->local.width,
                              target->local.height),
            key);
        porcelain->dirty = true;
    }
    applied->hit_box = target->local;
    applied->hit_box_written = true;
    if( strcmp(applied->hit_label, wanted->op_label) != 0 )
    {
        porcelain->counters.engine_calls++;
        porcelain->counters.setters++;
        porcelain_note_item_result(porcelain, applied, "set_on_op",
                                   widgets->set_on_op(widgets->context, applied->hit_ref,
                                                      wanted->op_label, porcelain_op_listener,
                                                      applied),
                                   key);
        Porcelain_CopyString(applied->hit_label, sizeof(applied->hit_label), wanted->op_label);
        porcelain->dirty = true;
    }
    if( !applied->hit_anchor_written ||
        !ToriRS_WidgetRefEqual(applied->hit_anchor_target, target->ref) )
    {
        enum ToriRS_ContractResult const result = widgets->set_anchor(
            widgets->context, applied->hit_ref, target->ref, TORIRS_WIDGET_RELATION_OVER);
        porcelain->counters.engine_calls++;
        porcelain->counters.setters++;
        porcelain_note_result(porcelain, "set_anchor", wanted->place.on, result, key);
        /* Only a WRITTEN anchor is remembered, for the reason the picture's
         * own anchor states at length. */
        if( result == TORIRS_CONTRACT_OK || result == TORIRS_CONTRACT_PENDING )
        {
            applied->hit_anchor_target = target->ref;
            applied->hit_anchor_written = true;
        }
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
        /*
         * A depth target has to PAINT, not merely resolve.
         *
         * They are different questions and the engine answers the ordering
         * one with records: a unit anchored OVER a target that emits none
         * keeps its own native draw index, which is later than everything it
         * was meant to sit under. That is the live defect the minimap-orbs
         * port measured 1:1 across the six minimap states -- the desktop
         * frame's housing plate anchors over whichever of compass or minimap
         * RESOLVES, and on the three states that suppress the compass the
         * plate paints over the whole orb column, the lane's own art
         * included. Falling back to the placement's own element is the same
         * thing an unbound target already does; the finding is so the
         * provider learns it lost instead of seeing a rendering bug.
         */
        bool const parenting = applied->item.place.kind == PORCELAIN_AT_CANVAS ||
                               applied->item.place.kind == PORCELAIN_AT_USABLE ||
                               applied->item.place.kind == PORCELAIN_WITHIN;
        if( !Porcelain_Element(porcelain, applied->item.place.depth, &depth_state) )
        {
            /* Not resolved YET is not a loss. A parenting placement has no
             * second target to fall back to -- see below -- so it simply does
             * not anchor this fence and is asked again at the next. */
            if( parenting )
                return;
        }
        else if( depth_state.presented )
            anchor = depth_state.ref;
        else if( parenting )
        {
            /*
             * NO anchor, rather than an anchor to the parent.
             *
             * A parent-relative placement falls back to the placement's own
             * element, which is a real node and a legal target. A parenting one
             * has no such element: its target IS the parent, and an anchor to
             * one's own parent is ANCHOR_INVALID -- so the fallback was a
             * refusal, recorded as a finding, on every frame between the
             * description naming the compass and the compass being laid out.
             * Leaving it unanchored is what the placement means without a
             * depth target anyway, and the next fence asks again.
             */
            return;
        }
        else
            Porcelain_RecordFinding(porcelain, "set_anchor", applied->item.place.depth,
                                    PORCELAIN_FINDING_REFUSED, "the depth target draws nothing");
    }
    relation = applied->item.place.kind == PORCELAIN_REPLACE
                   ? TORIRS_WIDGET_RELATION_REPLACE
                   : (applied->item.place.behind ? TORIRS_WIDGET_RELATION_BEHIND
                                                 : TORIRS_WIDGET_RELATION_OVER);
    if( !fresh && ToriRS_WidgetRefEqual(applied->anchor_target, anchor) )
        return;
    if( (applied->item.place.kind == PORCELAIN_AT_CANVAS ||
         applied->item.place.kind == PORCELAIN_AT_USABLE ||
         applied->item.place.kind == PORCELAIN_WITHIN) &&
        applied->item.place.depth.kind <= PORCELAIN_EL_NONE )
    {
        /* Nothing to anchor to: the target IS the parent, and an anchor to
         * one's own parent is ANCHOR_INVALID. WITHIN exists so that a corner
         * ornament costs no live anchor -- one live anchor makes
         * UITree_FrameHasDepth true, which the ledger prices at 13.5 ms a
         * frame on osrs239. A parenting placement that STATES a depth target
         * is asking for that anchor on purpose. @see porcelain_push_item. */
        applied->anchor_target = anchor;
        return;
    }
    porcelain->counters.engine_calls++;
    porcelain->counters.setters++;
    {
        enum ToriRS_ContractResult const result =
            widgets->set_anchor(widgets->context, applied->ref, anchor, relation);
        porcelain_note_result(porcelain, "set_anchor", applied->item.place.on, result,
                              applied->item.key.text);
        /*
         * Only a WRITTEN anchor is remembered.
         *
         * The target is what the next fence compares against to decide there
         * is nothing to do, so recording one the engine refused meant the
         * anchor was asked for exactly once and never again. Measured: the
         * desktop frame's map housing asks to sit over the compass on the
         * fence the frame is first described, three frames before the compass
         * is laid out; the engine refused it, the layer wrote it down as done,
         * and the plate kept its native draw index for the rest of the session
         * -- painting over the orb block's world-map globe, its wiki banner
         * and the activity adviser. The old provider re-anchored on every plan
         * pass and recovered by accident.
         */
        if( result != TORIRS_CONTRACT_OK && result != TORIRS_CONTRACT_PENDING )
            return;
    }
    applied->anchor_target = anchor;
    porcelain->dirty = true;
}

static void
porcelain_remove_item(struct Porcelain* porcelain, struct PorcelainAppliedItem* applied)
{
    struct ToriRS_WidgetApi const* widgets = &porcelain->api->widgets;

    if( !applied->live )
        return;
    /* The hit box first: it is this item's second node, and a slot cleared
     * with it still live would leak an armed, invisible rectangle over the
     * target for the rest of the session. */
    porcelain_remove_hit(porcelain, applied);
    porcelain->counters.engine_calls++;
    porcelain->counters.removes++;
    (void)widgets->remove(widgets->context, applied->ref);
    memset(applied, 0, sizeof(*applied));
    porcelain->dirty = true;
}

/* ------------------------------------------------------------------------ */
/* Applying an element edit                                                 */
/* ------------------------------------------------------------------------ */

/*
 * What a RAISE anchors to.
 *
 * Kind NONE means "above everything this plugin owns", and that is the frame
 * provider's whole sentence: it draws a surround under the clipping root, so
 * every piece of it is after the lane's own subtree, and the chat, the map,
 * the panels and the orb block have to come back above the surround. The node
 * to name is the LAST control the description stated, and a description has
 * no way to name one of its own controls -- a depth target is an ELEMENT.
 *
 * Any other kind is an ordinary element and, like an item's `place.depth`, it
 * has to PAINT: a unit anchored over a target that emits nothing keeps its own
 * native draw index, which is later than everything it was meant to sit under.
 */
static bool
porcelain_raise_anchor(struct Porcelain* porcelain, struct PorcelainNormalEdit const* wanted,
                       struct ToriRS_WidgetRef* out)
{
    struct PorcelainElementState over;

    assert(porcelain);
    assert(wanted);
    assert(out);
    if( wanted->over.kind > PORCELAIN_EL_NONE )
    {
        if( !Porcelain_Element(porcelain, wanted->over, &over) )
            return false;
        if( !over.presented )
        {
            Porcelain_RecordFinding(porcelain, "raise", wanted->over, PORCELAIN_FINDING_REFUSED,
                                    "the depth target draws nothing");
            return false;
        }
        *out = over.ref;
        return true;
    }
    {
        struct PorcelainAppliedItem const* top = NULL;
        for( int i = 0; i < PORCELAIN_ITEMS_MAX; i++ )
        {
            struct PorcelainAppliedItem const* applied = &porcelain->applied_items[i];
            if( !applied->live || applied->owner != porcelain )
                continue;
            if( !top || applied->order > top->order )
                top = applied;
        }
        /* Nothing owned yet. Not a failure and not a finding: the first fence
         * of a frame states its chrome and its surfaces in one description,
         * and the surfaces are raised on the fence after the chrome exists. */
        if( !top )
            return false;
        *out = top->ref;
        return true;
    }
}

static void
porcelain_apply_edit(struct Porcelain* porcelain, struct PorcelainAppliedEdit* applied,
                     struct PorcelainNormalEdit const* wanted, bool fresh)
{
    struct ToriRS_WidgetApi const* widgets = &porcelain->api->widgets;
    struct PorcelainElementState target;
    struct PorcelainNormalEdit const previous_edit = applied->edit;

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
        /*
         * A half that STOPS being described is taken off.
         *
         * A skin is a setter and the engine keeps no pre-edit snapshot, so the
         * only undo there is is `reset`, and reset is reached only when the
         * whole edit stops being described. A layout that re-skins the compass
         * followed by one that skins only its MASK left the first layout's
         * rose behind for ever: the element is still skinned, so the edit is
         * still live, and the art half is simply never revisited. The provider
         * this replaced reset every surface before every plan, which is the
         * blunt version of this and cost a reset per role per pass.
         */
        if( previous_edit.kind == PORCELAIN_EDIT_SKIN &&
            ((previous_edit.has_image && !wanted->has_image) ||
             (previous_edit.has_mask && !wanted->has_mask)) )
        {
            porcelain->counters.engine_calls++;
            (void)widgets->reset(widgets->context, target.ref);
        }
        if( wanted->has_image )
        {
            enum PorcelainAssetState state = PORCELAIN_ASSET_READY;
            struct ToriRS_ImageRef const image =
                Porcelain_Image(porcelain, wanted->image.text, &state);
            if( state != PORCELAIN_ASSET_READY )
                /*
                 * Asked again next fence.
                 *
                 * A skin is applied only when the description changed, and an
                 * asset landing does not change a description -- so a re-skin
                 * described before its picture had decoded was dropped once
                 * and the widget kept the lane's own art for the session.
                 * Measured: the OldSchool frame's compass rose, which is
                 * named on the first describe and decodes several frames later.
                 * Clearing the remembered hash is what makes the next fence
                 * call this fresh; the item path solves the same problem with
                 * porcelain_refresh_image.
                 */
                applied->edit.hash = 0;
            else
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
            if( state != PORCELAIN_ASSET_READY )
                applied->edit.hash = 0;
            else
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
    case PORCELAIN_EDIT_RAISE:
    {
        struct ToriRS_WidgetRef anchor;
        enum ToriRS_ContractResult result;

        if( !porcelain_raise_anchor(porcelain, wanted, &anchor) )
            break;
        porcelain->counters.engine_calls++;
        porcelain->counters.setters++;
        result = widgets->set_anchor(widgets->context, target.ref, anchor,
                                     wanted->behind ? TORIRS_WIDGET_RELATION_BEHIND
                                                    : TORIRS_WIDGET_RELATION_OVER);
        porcelain_note_result(porcelain, "raise", wanted->element, result, wanted->element_role);
        /* A refused raise is NOT recorded as applied: the same defect the
         * item anchor had. @see porcelain_apply_anchor. */
        if( result != TORIRS_CONTRACT_OK && result != TORIRS_CONTRACT_PENDING )
            return;
        break;
    }
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
    Porcelain_PanelRunBegin(porcelain);
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
        /*
         * A live control whose parent is no longer the right one is RE-MADE,
         * not moved.
         *
         * The engine has no re-parent verb, and a control left under the
         * parent it was born under is being positioned in coordinates that
         * belong to a different node -- which looks like drift, not like a
         * missing control, so it survives every screenshot. Remove-then-
         * create is the only expression the engine has, and it is free in the
         * steady state: reconcile runs only when an input moved.
         */
        if( applied && porcelain_item_target_moved(porcelain, applied, wanted, &target) )
        {
            struct ToriRS_WidgetRef parent;
            if( !porcelain_item_parent(porcelain, wanted, &target, &parent) )
            {
                porcelain_remove_item(porcelain, applied);
                Porcelain_RecordFinding(porcelain, "reparent", wanted->place.on,
                                        PORCELAIN_FINDING_REFUSED, "no parent");
                continue;
            }
            if( !ToriRS_WidgetRefEqual(parent, applied->parent) )
            {
                porcelain->counters.reparents++;
                /*
                 * The WHOLE reference in `was` and `now`, not just its middle
                 * field.
                 *
                 * A widget reference is tree:index:incarnation and the compare
                 * above is on all three. Printing the index alone made a login
                 * -- where the 2004 lane replaces the tree and the plugin's
                 * rows hang under a NEW node that happens to carry the old
                 * one's index -- read as `was=1 now=1`, a reparent under a
                 * parent that did not change, and it was filed as a defect on
                 * exactly that reading. The index is the field least likely to
                 * differ; it must not be the only one shown.
                 */
                if( porcelain_trace_enabled() )
                    fprintf(stderr,
                            "PORCELAIN_REPARENT plugin=%s key=%s frame=%u "
                            "was=%llu:%llu:%llu now=%llu:%llu:%llu\n",
                            porcelain->plugin_id, wanted->key.text, porcelain->frame,
                            (unsigned long long)applied->parent.opaque[0],
                            (unsigned long long)applied->parent.opaque[1],
                            (unsigned long long)applied->parent.opaque[2],
                            (unsigned long long)parent.opaque[0],
                            (unsigned long long)parent.opaque[1],
                            (unsigned long long)parent.opaque[2]);
                porcelain_remove_item(porcelain, applied);
                applied = NULL;
            }
            else if( !porcelain_item_alive(porcelain, applied) )
            {
                /*
                 * Same parent NODE, and the control under it is gone anyway.
                 *
                 * A `layout` verb rebuilds the whole HUD, and the frame root
                 * can be the SAME node on the other side of that while
                 * everything under it -- the owned controls included -- has
                 * been torn down. The parent compare above then says nothing
                 * moved, the control is kept, and the setters below write to
                 * a handle the engine has already freed: every one of them
                 * comes back STALE_REFERENCE and files a finding the plugin
                 * can neither prevent nor declare. That is what the seventh
                 * gate lane was reporting against performance-display, at a
                 * frame that moved with the readout's one-second timer --
                 * intermittent because it needed a fence that BOTH re-applied
                 * a property and followed the remount.
                 *
                 * So the existence question is asked in the same place as the
                 * parent question, because both are about the same event, and
                 * the answer is a re-create rather than a refusal: the layer's
                 * promise is that a described control exists, and absorbing
                 * this is what makes that true.
                 */
                porcelain->counters.recreates++;
                if( porcelain_trace_enabled() )
                    fprintf(stderr, "PORCELAIN_RECREATE plugin=%s key=%s frame=%u\n",
                            porcelain->plugin_id, wanted->key.text, porcelain->frame);
                porcelain_remove_item(porcelain, applied);
                applied = NULL;
            }
            else
            {
                /* Asked and answered: do not ask again until the target moves
                 * again. */
                applied->target_ref = target.ref;
                applied->target_element_stamp = porcelain->stamp[PORCELAIN_INPUT_ELEMENT];
            }
        }
        if( applied && applied->stale )
        {
            /*
             * The engine destroyed this node under us: write NOTHING to it
             * and wait.
             *
             * Not "drop it and create another", which is the obvious move and
             * is wrong: the node died because the tree it hung in was
             * replaced, so its PARENT is gone too, and a create against a
             * dead parent fails -- measured on the gate's remount lane as
             * sixty-three `create tab.04` findings in place of the one refusal
             * this was meant to remove. The arm above re-creates it properly,
             * with the new parent, on the fence the target rebinds; until then
             * the slot is simply not touched, which is the honest description
             * of a widget that is not there.
             * @see porcelain_note_item_result.
             */
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
        /* Description order, kept so that "over everything this plugin
         * owns" has an answer. @see ToriRS_PorcelainApi::raise */
        applied->order = i;
        porcelain_refresh_image(porcelain, applied);
        /* Before the picture's own op is decided, because whether this node
         * exists is what decides it. @see porcelain_apply_op. */
        porcelain_apply_replace_hit(porcelain, applied, wanted, &target);
        if( !fresh && applied->item.hash == wanted->hash )
        {
            /* Unchanged hash: NO engine call for any property. Geometry still
             * follows the target, and that costs nothing when it did not
             * move. */
            porcelain_apply_geometry(porcelain, applied,
                                     porcelain_place_box(porcelain, wanted, &target));
            porcelain_apply_op(porcelain, applied, wanted);
            porcelain_apply_anchor(porcelain, applied, &target, false);
            goto visibility;
        }
        porcelain_apply_properties(porcelain, applied, wanted, fresh);
        porcelain_apply_geometry(porcelain, applied,
                                 porcelain_place_box(porcelain, wanted, &target));
        porcelain_apply_op(porcelain, applied, wanted);
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
        struct PorcelainElementState target_state;
        struct ToriRS_WidgetRef target_ref_of_edit;
        bool fresh = true;

        memset(&target_ref_of_edit, 0, sizeof(target_ref_of_edit));
        if( Porcelain_Element(porcelain, wanted->element, &target_state) )
            target_ref_of_edit = target_state.ref;
        for( int j = 0; j < PORCELAIN_EDITS_MAX && !slot; j++ )
            if( porcelain->applied_edits[j].live &&
                porcelain->applied_edits[j].edit.kind == wanted->kind &&
                Porcelain_ElementKey(porcelain->applied_edits[j].edit.element) ==
                    Porcelain_ElementKey(wanted->element) )
            {
                slot = &porcelain->applied_edits[j];
                /*
                 * A changed hash, OR a changed NODE.
                 *
                 * An edit is a setter on somebody else's widget, and the only
                 * record that it was written is this slot. When a remount
                 * replaces the node the element resolves to, the description is
                 * word for word the same -- so the hash matches, nothing is
                 * written, and every surface keeps the box the LANE gave the
                 * new node. The plate, the map and the chat all snap back to
                 * the toplevel's own layout while the frame's surround stays
                 * where the provider drew it.
                 *
                 * This is the defect the gate's remount164 lane exists for,
                 * and it was invisible to the six static lanes because none of
                 * them ever replaces a node. The item path already had its own
                 * arm for the same thing (porcelain_item_target_moved).
                 */
                fresh = slot->edit.hash != wanted->hash ||
                        !ToriRS_WidgetRefEqual(slot->ref, target_ref_of_edit);
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

    /* 4. Rows, against api->panel. Its own reconciler: the element diff above
     *    owns controls, this one owns a declaration the host holds. */
    Porcelain_PanelReconcile(porcelain);
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
/*
 * Login to game, and game back to login.
 *
 * One int read per fence through a function pointer, which is the cheapest
 * question the core api answers, and it buys the only signal a description
 * has for "the client is somewhere else now". Everything else the layer polls
 * is a consequence of the tree moving; the screen is not, and a plugin that
 * places at a canvas corner rather than against an element sees no tree
 * movement at all when the player logs out.
 *
 * Raised as an input rather than acted on, because what a transition MEANS is
 * the plugin's business: some want to disappear at the login screen, some want
 * to stay, and the layer has no standing to choose. It only has to make sure
 * the question gets asked again.
 */
static void
porcelain_note_screen(struct Porcelain* porcelain)
{
    struct ToriRS_CoreApi const* core = &porcelain->api->core;
    int screen;

    assert(porcelain);
    if( !core->screen )
        return;
    screen = core->screen(porcelain->api);
    if( screen == porcelain->screen )
        return;
    porcelain->screen = screen;
    porcelain->stamp[PORCELAIN_INPUT_SCREEN]++;
}

static void
porcelain_resolve_pending(struct Porcelain* porcelain)
{
    struct ToriRS_WidgetRef ref;
    /*
     * ONE read of the tree flag for the whole sweep, taken and cleared here.
     *
     * Re-asking is what this function used to do on every fence FOR EVER, and
     * the "for ever" is the whole of the layer's rest cost: four role lookups
     * a frame on any lane with no docked strip, because Porcelain_Usable
     * CREATES four lane-chrome watches and nothing ever resolved them.
     *
     * The line this draws is PENDING against ABSENT, and the two words
     * already mean it. PENDING is "the lane may still be mounting this and
     * the layer has not finished waiting" -- a window the absence clock
     * bounds at PORCELAIN_ABSENT_FENCES, during which a re-ask every fence is
     * what makes a provider come up cleanly. ABSENT is "the wait is over",
     * and it is unbounded: nothing the lane does short of publishing a new
     * tree can change that answer, so nothing short of a publication is worth
     * asking about.
     *
     * The clock below is not the expensive half and still runs every fence:
     * counting is free, and an element the lane has not got has to be able to
     * become ABSENT whether or not the tree ever moves again.
     */
    bool const tree_moved = porcelain->tree_moved;

    porcelain->tree_moved = false;
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
        /*
         * A re-ask: every fence while PENDING, and after that only when the
         * topology published.
         *
         * Two things can turn an unresolved watch into a bound one. The
         * element can APPEAR, which the host reports to the watch itself --
         * it re-resolves every subscription by name on a publication, a watch
         * that has never resolved included -- so this find is a second
         * opinion rather than the only route. The element can also appear
         * under the OTHER SPELLING, which the host cannot report because the
         * subscription is on a name the lane does not use; that one is only
         * reachable from here. @see porcelain_watch_respell.
         *
         * Both are tree changes, which is why a publication is enough. The
         * PENDING half is not there because it is needed, it is there because
         * it is what a mounting lane already gets and a re-ask in a window two
         * fences long is not a cost: dropping it moved a provider's first
         * describe by a fence, which is a plugin placing its controls at a
         * fallback box one fewer time. Better, and not this change's to make.
         */
        if( tree_moved || watch->state.bind != PORCELAIN_ABSENT )
        {
            porcelain_watch_respell(porcelain, watch);
            if( porcelain_engine_find(porcelain, watch->role, &ref) == TORIRS_CONTRACT_OK )
            {
                bool const was_absent = watch->state.bind == PORCELAIN_ABSENT;
                watch->state.bind = PORCELAIN_BOUND;
                porcelain->any_element_bound = true;
                porcelain_forget_absence(porcelain, watch->element);
                watch->state.ref = ref;
                watch->pending_fences = 0;
                if( was_absent )
                    watch->absence_reported = false;
                porcelain_note_new_binding(porcelain);
                porcelain_read_state(porcelain, watch);
                porcelain->stamp[PORCELAIN_INPUT_ELEMENT]++;
                continue;
            }
        }
        if( watch->state.bind == PORCELAIN_ABSENT )
        {
            /* An element that comes back binds through the watch listener, so
             * ABSENT is never permanent -- only quiet. */
            continue;
        }
        if( !porcelain->any_element_bound )
        {
            /* Nothing of this lane's interface has resolved yet: still
             * mounting, so nothing here is absent. */
            watch->pending_fences = 0;
            continue;
        }
        /* A frame provider that has not come up yet is still watching its
         * toplevel mount. @see PORCELAIN_ABSENT_FRAME_GRACE. */
        if( ++watch->pending_fences >=
            (Porcelain_FrameWaiting(porcelain) ? PORCELAIN_ABSENT_FENCES * PORCELAIN_ABSENT_FRAME_GRACE
                                               : PORCELAIN_ABSENT_FENCES) )
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

    porcelain_note_screen(porcelain);
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
    /*
     * A describe asked to be described again. The reconcile above has already
     * taken the description that asked, which is the whole point; the bump
     * lands here so the NEXT fence re-runs it. @see Porcelain_Invalidate.
     */
    if( porcelain->invalidate_after_fence )
    {
        porcelain->invalidate_after_fence = false;
        Porcelain_Note(porcelain, PORCELAIN_INPUT_EXPLICIT);
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
            porcelain_note_item_result(
                porcelain, applied, "set",
                widgets->set_position(widgets->context, applied->ref, x, y), key);
            applied->live_x = x;
            applied->live_y = y;
            porcelain->dirty = true;
        }
    }
    /*
     * Text on the direct path. A per-frame readout IS a string that changes
     * every frame, and without this the only way to move one was to describe
     * and invalidate -- a whole reconcile pass to write four characters.
     * The applied item's copy is updated too, so the next describe with the
     * same text is still a no-op rather than a setter that undoes this.
     */
    if( (motion->mask & PORCELAIN_MOTION_TEXT) && motion->text )
    {
        if( strcmp(applied->item.text, motion->text) != 0 )
        {
            porcelain->counters.engine_calls++;
            porcelain->counters.setters++;
            porcelain_note_item_result(
                porcelain, applied, "set",
                widgets->set_text(widgets->context, applied->ref, motion->text), key);
            Porcelain_CopyString(applied->item.text, sizeof(applied->item.text), motion->text);
            applied->item.has_text = true;
            applied->item.hash = porcelain_item_hash(&applied->item);
            porcelain->dirty = true;
        }
    }
    if( motion->mask & PORCELAIN_MOTION_RGB )
    {
        if( motion->rgb != applied->item.rgb )
        {
            porcelain->counters.engine_calls++;
            porcelain->counters.setters++;
            porcelain_note_item_result(
                porcelain, applied, "set",
                widgets->set_text_color(widgets->context, applied->ref, motion->rgb), key);
            applied->item.rgb = motion->rgb;
            applied->item.hash = porcelain_item_hash(&applied->item);
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
            porcelain_note_item_result(
                porcelain, applied, "set",
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
            porcelain_note_item_result(porcelain, applied, "set",
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
    porcelain->builder.raise = Porcelain_Raise;
    porcelain->builder.skin = Porcelain_Skin;
    porcelain->builder.opacity = Porcelain_Opacity;
    porcelain->builder.unsupported = Porcelain_Unsupported;
    porcelain->builder.row = Porcelain_Row;
    porcelain->builder.reidentify = Porcelain_Reidentify;
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
    /* Not a screen the client can be on, so the first fence reads a change and
     * the stamp starts out agreeing with what is actually up. */
    porcelain->screen = -1;
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
    Porcelain_PanelClose(porcelain);
    /* Relinquish BEFORE the handle dies: the host keeps both frame providers
     * alive for one fence by design, so an outgoing provider that kept its
     * claims would hand the incoming one eight findings instead of a frame. */
    Porcelain_ClaimDropAll(porcelain);
    Porcelain_FrameForget(porcelain);
    memset(porcelain, 0, sizeof(*porcelain));
}

void
Porcelain_DescribeNow(struct Porcelain* porcelain)
{
    assert(porcelain);
    assert(porcelain->used);
    assert(porcelain->describe_fn);
    porcelain_run_describe(porcelain);
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

/**
 * Ask for a fresh description at the NEXT fence.
 *
 * Called from outside a describe, that is one stamp bump and nothing else.
 *
 * Called from INSIDE one it used to be the same bump, and the bump was read
 * by the fence's own two-pass loop: the inputs had moved, so the describe ran
 * a second time and it was the SECOND pass's scratch that reached the
 * reconcile. The description being written when the plugin asked to be asked
 * again was therefore thrown away -- silently, and with no counter, finding or
 * assert to say so. A describe that stated nothing in order to have its
 * controls removed removed nothing at all, because the keys were still in the
 * final scratch; that cost the minimap-orbs port a regression that reached
 * the integration branch, and the plugin had to span its drop across two
 * fences and explain why in a comment.
 *
 * So the bump is DEFERRED to the end of the fence. The description the plugin
 * was writing is the one that is reconciled -- which is what "invalidate"
 * plainly reads as -- and the re-describe happens at the next fence.
 *
 * The two-pass loop is untouched, because it is not for this: it exists so a
 * watch that binds synchronously inside a describe (an ELEMENT or ASSET stamp
 * moved by the engine DURING the run that meant to consume it) is consumed in
 * the same fence rather than re-describing for ever. Those bumps are the
 * engine answering a question the run asked; this one is the plugin stating
 * an intent about the NEXT run, and conflating them is what made it silent.
 */
void
Porcelain_Invalidate(struct Porcelain* porcelain)
{
    assert(porcelain);
    if( porcelain->describing )
    {
        porcelain->invalidate_after_fence = true;
        return;
    }
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
    Porcelain_PanelCountersReset(porcelain);
}

void
Porcelain_ResetForTesting(void)
{
    memset(g_handles, 0, sizeof(g_handles));
    memset(g_claims, 0, sizeof(g_claims));
    Porcelain_PanelResetForTesting();
    Porcelain_FrameResetForTesting();
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
    .expect_unsupported = Porcelain_ExpectUnsupported,
    .image_size = Porcelain_ImageSize,
    .cancel_every = Porcelain_CancelEvery,
    .note_key = Porcelain_NoteKey,
    .draw_context = Porcelain_DrawContext,
    .menu_add = Porcelain_MenuAdd,
    .note_menu = Porcelain_NoteMenu,
    .hover = Porcelain_Hover,
    .native_overlay = Porcelain_NativeOverlay,
    .note_script = Porcelain_NoteScript,
    .table = Porcelain_Table,
    .notify = Porcelain_Notify,
    .panel = Porcelain_Panel,
    .panel_build = Porcelain_PanelBuild,
    .panel_action = Porcelain_PanelAction,
    .panel_draw = Porcelain_PanelDraw,
    .setting_value = Porcelain_SettingValue,
    .panel_restate = Porcelain_Restate,
    .panel_scroll = Porcelain_PanelScroll,
    .panel_scroll_to = Porcelain_PanelScrollTo,
    /* round three */
    .hull = Porcelain_Hull,
    .tile = Porcelain_Tile,
    .finding = Porcelain_Finding,
    .menu_untag = Porcelain_MenuUntag,
    .key_down = Porcelain_KeyDown,
    .config_list_remove = Porcelain_ConfigListRemove,
    .config_list_set = Porcelain_ConfigListSet,
    .frame = Porcelain_Frame,
    .frame_event = Porcelain_FrameEvent,
    .usable = Porcelain_Usable,
    .native_size = Porcelain_NativeSize,
    .lane_icon = Porcelain_LaneIcon,
    .tab_group_count = Porcelain_TabGroupCount,
    .tab_group = Porcelain_TabGroup,
    .tab_detached = Porcelain_TabDetached,
    .counters_read = Porcelain_CountersRead,
    .counters_reset = Porcelain_CountersReset,
};

struct ToriRS_PorcelainApi const*
ToriRS_PorcelainApiTable(void)
{
    return &PORCELAIN_TABLE;
}
