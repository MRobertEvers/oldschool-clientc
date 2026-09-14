/*
 * Porcelain: the frame verbs.
 *
 * The two biggest plugins in the tree are frame providers -- gameframe-layout
 * at 3,911 lines and mobile-gameframe at 4,440 -- and between them they ask
 * the engine eleven lineage questions, walk 32 parents per fence to find the
 * clipping root, carry one toplevel's pixel numbers onto another, and hold
 * two hard-coded tab orders. Every one of those is a question with a DATA
 * answer that nothing had spelled yet. These verbs are those answers.
 *
 * The rule the whole file obeys: no lane name, no revision number, no
 * toplevel compared against a literal. A root is a KEY -- `cache.frame_root()`
 * joined to a tab name and looked up in the profile's `[tabs:<root>]`
 * section -- and never a branch. Where a root states nothing the arrangement
 * is DERIVED from the boxes the lane's own stones report, which is right on
 * every root that lays its tabs out in plain runs and is exactly what the
 * override exists to correct on the two that do not.
 *
 * What is NOT here, and where it is instead: the offers themselves. A
 * provider publishes them in ToriRS_PluginDef.frames and the host resolves
 * auto/native before the provider starts, so registration order cannot change
 * which one is asked for. Porcelain_Frame binds a DESCRIPTION to an offer id;
 * Porcelain_FrameEvent is the plugin forwarding the host's own on_gameframe,
 * because ToriRS_PluginDef is const and a library cannot install a callback
 * into it.
 */

#include "plugin/porcelain/porcelain_internal.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* The vocabulary                                                           */
/* ------------------------------------------------------------------------ */

/*
 * The fourteen sidebar tabs, by name.
 *
 * A VOCABULARY, in the same sense PORCELAIN_ORB_ROLES is one: every revision
 * since 2001 numbers these fourteen panels, and a plugin that had to spell
 * its own fourteen is the remap table this layer deletes. Nothing about a
 * LANE is stated here -- which of them a lane has, and what number it gives
 * each, are both the profile's `[tabs]` map, asked for one name at a time.
 *
 * The order is the numbering the shipped profiles use, and it is only a
 * starting order: every answer this file gives is keyed by NAME.
 */
static char const* const PORCELAIN_TAB_NAME[] = {
    "combat", "stats",   "quests", "inventory", "equipment", "prayer", "magic",
    "clan",   "account", "friends", "logout",   "options",   "emotes", "music",
    NULL};

char const* const*
Porcelain_TabNames(void)
{
    return PORCELAIN_TAB_NAME;
}

static int
porcelain_tab_name_count(void)
{
    int count = 0;

    while( PORCELAIN_TAB_NAME[count] )
        count++;
    return count;
}

/* ------------------------------------------------------------------------ */
/* Per-handle frame state                                                   */
/* ------------------------------------------------------------------------ */

/*
 * Kept beside `struct Porcelain` rather than inside it, the way the claim
 * table is: a handle that never provides a frame pays nothing for the rows,
 * and the reconciler's own struct stays the reconciler's.
 */
struct PorcelainFrameOffer
{
    bool used;
    char id[TORIRS_PLUGIN_FRAME_ID_MAX];
    int canvas;
    int min_width;
    int min_height;
    PorcelainDescribeFn fn;
    void* user;
};

struct PorcelainFrameState
{
    bool used;
    struct Porcelain* owner;
    struct PorcelainFrameOffer offers[PORCELAIN_FRAME_OFFERS_MAX];

    /** The offer the host last asked for, or -1 between provisions. */
    int active;
    int canvas_width;
    int canvas_height;
    struct ToriRS_WidgetBounds safe;

    /** The last answer this handle gave the host. A provider that has not
     *  come up yet is still waiting for the lane. @see Porcelain_FrameWaiting */
    bool waiting;
    /** Porcelain_Unsupported inside the frame describe. @see the frame arm. */
    bool unsupported;
    char unsupported_reason[PORCELAIN_DETAIL_MAX];
};

static struct PorcelainFrameState g_frames[PORCELAIN_HANDLES_MAX];

static struct PorcelainFrameState*
porcelain_frame_state(struct Porcelain* porcelain, bool create)
{
    struct PorcelainFrameState* free_slot = NULL;

    assert(porcelain);
    for( int i = 0; i < PORCELAIN_HANDLES_MAX; i++ )
    {
        if( !g_frames[i].used )
        {
            if( !free_slot )
                free_slot = &g_frames[i];
            continue;
        }
        if( g_frames[i].owner == porcelain )
            return &g_frames[i];
    }
    if( !create )
        return NULL;
    /* One row per handle and the two tables are the same size, so a handle
     * that exists always has a row to take. */
    assert(free_slot);
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->used = true;
    free_slot->owner = porcelain;
    free_slot->active = -1;
    return free_slot;
}

void
Porcelain_FrameForget(struct Porcelain* porcelain)
{
    struct PorcelainFrameState* state;

    /* Reached from Porcelain_Close, which is a deallocator and takes NULL. */
    if( !porcelain )
        return;
    state = porcelain_frame_state(porcelain, false);
    if( state )
        memset(state, 0, sizeof(*state));
}

void
Porcelain_FrameResetForTesting(void)
{
    memset(g_frames, 0, sizeof(g_frames));
}

/*
 * The frame arm of Porcelain_Unsupported.
 *
 * The verb already records a finding, which is what a non-frame plugin wants
 * from it. A frame provider needs one thing more: the host asked a question
 * ("can you lay this frame out here?") and the answer has to reach it as
 * TORIRS_FRAME_UNSUPPORTED with the reason written, or the lane's own frame
 * comes down and nothing replaces it. Classic Fixed over the mobile top is
 * exactly that case.
 */
void
Porcelain_FrameNoteUnsupported(struct Porcelain* porcelain, char const* reason)
{
    struct PorcelainFrameState* state;

    assert(porcelain);
    assert(reason);
    state = porcelain_frame_state(porcelain, false);
    /* No frame state means this plugin provides no frame; the finding the
     * verb already recorded is the whole of the answer. */
    if( !state )
        return;
    state->unsupported = true;
    Porcelain_CopyString(state->unsupported_reason, sizeof(state->unsupported_reason), reason);
}

/* ------------------------------------------------------------------------ */
/* Registration and the gameframe event                                     */
/* ------------------------------------------------------------------------ */

void
Porcelain_Frame(struct Porcelain* porcelain, char const* offer_id, int canvas, int min_width,
                int min_height, PorcelainDescribeFn fn, void* user)
{
    struct PorcelainFrameState* state;

    assert(porcelain);
    assert(offer_id);
    assert(offer_id[0] != '\0');
    assert(fn);
    assert(canvas == TORIRS_FRAME_CANVAS_FIXED || canvas == TORIRS_FRAME_CANVAS_WINDOW);
    assert(min_width >= 0);
    assert(min_height >= 0);
    assert(strlen(offer_id) < TORIRS_PLUGIN_FRAME_ID_MAX);

    state = porcelain_frame_state(porcelain, true);
    for( int i = 0; i < PORCELAIN_FRAME_OFFERS_MAX; i++ )
    {
        struct PorcelainFrameOffer* offer = &state->offers[i];
        if( offer->used && strcmp(offer->id, offer_id) != 0 )
            continue;
        if( !offer->used )
        {
            offer->used = true;
            Porcelain_CopyString(offer->id, sizeof(offer->id), offer_id);
        }
        offer->canvas = canvas;
        offer->min_width = min_width;
        offer->min_height = min_height;
        offer->fn = fn;
        offer->user = user;
        return;
    }
    Porcelain_RecordFinding(porcelain, "frame", PORCELAIN_EL(NONE), PORCELAIN_FINDING_BUDGET,
                            offer_id);
}

static int
porcelain_frame_find(struct PorcelainFrameState const* state, char const* offer_id)
{
    for( int i = 0; i < PORCELAIN_FRAME_OFFERS_MAX; i++ )
        if( state->offers[i].used && strcmp(state->offers[i].id, offer_id) == 0 )
            return i;
    return -1;
}

/*
 * Did the description this run just built depend on an element that has not
 * resolved yet?
 *
 * That is the difference between PENDING and READY, and it is the gate both
 * providers get wrong in the same way: the one-shot PENDING that was never
 * re-asked is the defect that kept gameframe-layout from ever coming up. Here
 * the question is asked of the watches the run itself touched -- a watch the
 * description no longer mentions cannot hold the frame back.
 */
static bool
porcelain_frame_run_pending(struct Porcelain const* porcelain)
{
    for( int i = 0; i < PORCELAIN_WATCHES_MAX; i++ )
    {
        struct PorcelainWatch const* watch = &porcelain->watches[i];
        if( !watch->used || watch->last_wanted_run != porcelain->run )
            continue;
        if( watch->state.bind == PORCELAIN_PENDING )
            return true;
    }
    return false;
}

/* A description that stages nothing: the release path. */
static void
porcelain_frame_empty_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    (void)describe;
    (void)user;
}

int
Porcelain_FrameEvent(struct Porcelain* porcelain, struct ToriRS_GameframeEvent const* event)
{
    struct PorcelainFrameState* state;
    struct PorcelainFrameOffer const* offer;
    int index;

    assert(porcelain);
    assert(event);
    assert(event->offer_id);

    state = porcelain_frame_state(porcelain, false);
    if( !state )
    {
        /* The host asked this plugin for a frame it never described. Louder
         * than a dropped answer: the lane's frame is already coming down. */
        Porcelain_RecordFinding(porcelain, "frame_event", PORCELAIN_EL(NONE),
                                PORCELAIN_FINDING_REFUSED, event->offer_id);
        return TORIRS_FRAME_UNSUPPORTED;
    }

    if( !event->active )
    {
        /*
         * A release is a description that stages nothing.
         *
         * The diff is the only undo there is -- the engine keeps no pre-claim
         * snapshot -- so running one empty describe takes the moves, the
         * hides, the skins and every owned control back off in one pass, and
         * the claims go with them. Both shipped providers undress from
         * on_frame_start instead, and on a provider switch the outgoing one
         * never gets another frame start: its chat dressing survives to
         * teardown.
         */
        state->active = -1;
        state->unsupported = false;
        state->waiting = false;
        Porcelain_Describe(porcelain, porcelain_frame_empty_describe, NULL);
        Porcelain_Fence(porcelain);
        Porcelain_Relinquish(porcelain);
        return TORIRS_FRAME_READY;
    }

    index = porcelain_frame_find(state, event->offer_id);
    if( index < 0 )
    {
        Porcelain_RecordFinding(porcelain, "frame_event", PORCELAIN_EL(NONE),
                                PORCELAIN_FINDING_REFUSED, event->offer_id);
        if( event->reason && event->reason_capacity > 0 )
            snprintf(event->reason, event->reason_capacity,
                     "no description is bound to the offer '%s'", event->offer_id);
        return TORIRS_FRAME_UNSUPPORTED;
    }
    offer = &state->offers[index];

    /* The canvas is an INPUT: a resize re-runs describe, and an unchanged one
     * costs a compare. The safe rect rides with it because a soft keyboard
     * moving is the same event to a layout as the window moving. */
    if( state->active != index || state->canvas_width != event->width ||
        state->canvas_height != event->height || state->safe.x != event->safe.x ||
        state->safe.y != event->safe.y || state->safe.width != event->safe.width ||
        state->safe.height != event->safe.height )
    {
        state->active = index;
        state->canvas_width = event->width;
        state->canvas_height = event->height;
        state->safe.x = event->safe.x;
        state->safe.y = event->safe.y;
        state->safe.width = event->safe.width;
        state->safe.height = event->safe.height;
        Porcelain_Note(porcelain, PORCELAIN_INPUT_CANVAS);
    }

    state->unsupported = false;
    state->unsupported_reason[0] = '\0';
    Porcelain_Describe(porcelain, offer->fn, offer->user);
    Porcelain_Fence(porcelain);

    if( state->unsupported )
    {
        if( event->reason && event->reason_capacity > 0 )
            snprintf(event->reason, event->reason_capacity, "%s", state->unsupported_reason);
        return TORIRS_FRAME_UNSUPPORTED;
    }
    if( porcelain_frame_run_pending(porcelain) )
    {
        state->waiting = true;
        if( event->reason && event->reason_capacity > 0 )
            snprintf(event->reason, event->reason_capacity,
                     "'%s' is waiting for the lane's own surfaces", offer->id);
        return TORIRS_FRAME_PENDING;
    }
    state->waiting = false;
    return TORIRS_FRAME_READY;
}

/* ------------------------------------------------------------------------ */
/* The usable canvas                                                        */
/* ------------------------------------------------------------------------ */

/*
 * Is this handle a frame provider that has not come up yet?
 *
 * A provider answers PENDING for exactly as long as something its description
 * asked about has not resolved -- that is what PENDING is FOR -- and while it
 * is in that state the lane is still mounting the toplevel the provider is
 * being asked to arrange. Nothing about it is absent yet.
 *
 * Without this, the absence clock ran through the very window the provider
 * exists to wait out: the frame gates its whole description on the viewport,
 * the viewport binds first, and the toplevel's chat, sidebar, modal and orb
 * block arrive four fences later -- so all four were reported ABSENT, once
 * each, on every desktop lane, and bound immediately afterwards. A finding
 * nobody can act on is worse than no finding, because the clean-findings gate
 * is how a real absence gets seen.
 *
 * What this buys is a MULTIPLIER on the clock and not a stop:
 * @see PORCELAIN_ABSENT_FRAME_GRACE, which says why stopping it deadlocks.
 */
bool
Porcelain_FrameWaiting(struct Porcelain* porcelain)
{
    struct PorcelainFrameState const* state;

    assert(porcelain);
    state = porcelain_frame_state(porcelain, false);
    return state && state->waiting;
}

bool
Porcelain_Usable(struct Porcelain* porcelain, struct ToriRS_WidgetBounds* out)
{
    struct PorcelainElementState state;

    assert(porcelain);
    assert(out);

    memset(out, 0, sizeof(*out));
    /*
     * A read-out, and deliberately nothing more: the rect is derived inside
     * the element table already -- a LANE_CHROME strip is subtracted only
     * when it is PRESENTED and spans a full edge, which is why 601's bound,
     * laid-out, hidden 58x46 strip cuts nothing. Deriving it a second time
     * here is how the two answers start to differ.
     */
    if( !Porcelain_Element(porcelain, PORCELAIN_EL(USABLE), &state) )
        return false;
    *out = state.box;
    return true;
}

/* ------------------------------------------------------------------------ */
/* The authored size of a surface, by element                               */
/* ------------------------------------------------------------------------ */

/*
 * Element -> the surface number the API names.
 *
 * This table is the whole of the G55 fix. Both providers keep a private
 * `enum FrameSurface` and pass it straight into frame.surface_native_size as
 * though it were TORIRS_SURFACE_*; the two disagree (plugin COMPASS=2,
 * SIDEBAR=5, MODAL=6 against API SIDEBAR=2, MODAL=5, COMPASS=6) and CHAT
 * matches by luck, which is the only reason the call works at all today. A
 * plugin that names an ELEMENT cannot make that mistake.
 *
 * -1 is "the lane lays out no such surface", which is a real answer for the
 * derived elements and for a control the cache authored rather than a region
 * the frame owns.
 */
static int
porcelain_surface_of(enum PorcelainElementKind kind)
{
    switch( kind )
    {
    case PORCELAIN_EL_VIEWPORT:
        return TORIRS_SURFACE_VIEWPORT;
    case PORCELAIN_EL_MINIMAP:
        return TORIRS_SURFACE_MINIMAP;
    case PORCELAIN_EL_COMPASS:
        return TORIRS_SURFACE_COMPASS;
    case PORCELAIN_EL_CHAT:
        return TORIRS_SURFACE_CHAT;
    case PORCELAIN_EL_CHAT_BAR:
    case PORCELAIN_EL_CHAT_FILTER:
        return TORIRS_SURFACE_CHAT_BUTTONS;
    case PORCELAIN_EL_SIDEBAR:
    case PORCELAIN_EL_TAB:
    case PORCELAIN_EL_PANEL:
        return TORIRS_SURFACE_SIDEBAR;
    case PORCELAIN_EL_MODAL:
        return TORIRS_SURFACE_MODAL;
    case PORCELAIN_EL_ORBS:
        return TORIRS_SURFACE_ORBS;
    default:
        return -1;
    }
}

/*
 * The tab's number in THIS lane's numbering, or -1.
 *
 * `[tabs]` on a dat2 profile; derived from `[role:panel_<name>]
 * match=slot(sidebar, <n>)` on a dat1 one, by revconfig, so the question has
 * one spelling on every lane. A name the lane does not declare is -1, which
 * is how rs289lc's missing clan tab answers -- and answering it is the whole
 * of the "left the seventh rock bare" defect.
 */
static int
porcelain_tab_number(struct Porcelain* porcelain, char const* name)
{
    struct ToriRS_CacheApi* cache = &porcelain->api->cache;
    int number = -1;

    assert(name);
    if( !cache->named_id )
        return -1;
    if( !cache->named_id(porcelain->api, "tab", name, &number) )
        return -1;
    return number;
}

/*
 * The member number this element answers to inside its surface, or -1 for an
 * element that IS the whole surface.
 *
 * The role's own numbering and never a position in a list: the recorded
 * defect was a compaction that handed tab 9's plan to tab 8, and a member
 * this frame does not have is an invalid slot, not a gap that closes.
 */
static int
porcelain_surface_member(struct Porcelain* porcelain, struct PorcelainElement element,
                         bool* out_is_member)
{
    *out_is_member = true;
    switch( element.kind )
    {
    case PORCELAIN_EL_CHAT_FILTER:
        return element.member;
    case PORCELAIN_EL_TAB:
        assert(element.role);
        return porcelain_tab_number(porcelain, element.role);
    case PORCELAIN_EL_ORBS:
        /*
         * ORBS carries the BLOCK's own member numbering -- the activity
         * adviser, the world-map globe, the wiki banner -- and ORB carries
         * the four stat orbs by profile role. Two numberings, and they are
         * not the same set: member 0 of the block is the adviser, not the
         * hitpoints globe. ORBS[0] is still the block itself, because that is
         * what "the orbs" means to every whole-role query.
         */
        if( element.member <= 0 )
        {
            *out_is_member = false;
            return -1;
        }
        return element.member;
    default:
        *out_is_member = false;
        return -1;
    }
}

bool
Porcelain_NativeSize(struct Porcelain* porcelain, struct PorcelainElement element,
                     struct ToriRS_WidgetBounds* out)
{
    struct ToriRS_FrameApi const* frame;
    int surface;
    int member;
    bool is_member = false;
    int x = 0, y = 0, w = 0, h = 0;

    assert(porcelain);
    assert(out);
    assert(element.kind > PORCELAIN_EL_NONE);
    assert(element.kind < PORCELAIN_EL_KIND_COUNT);

    memset(out, 0, sizeof(*out));
    surface = porcelain_surface_of(element.kind);
    if( surface < 0 )
    {
        Porcelain_RecordFinding(porcelain, "native_size", element,
                                PORCELAIN_FINDING_UNSUPPORTED, "the lane lays out no such surface");
        return false;
    }
    member = porcelain_surface_member(porcelain, element, &is_member);
    if( is_member && member < 0 )
    {
        /* A member this lane does not number. Already one finding from the
         * element table when the description asked for it; this one names the
         * verb that could not answer. */
        Porcelain_RecordFinding(porcelain, "native_size", element, PORCELAIN_FINDING_ABSENT,
                                "this lane numbers no such member");
        return false;
    }

    frame = &porcelain->api->frame;
    porcelain->counters.engine_calls++;
    if( is_member )
    {
        if( !frame->surface_member_native_box(porcelain->api, surface, member, &x, &y, &w, &h) )
        {
            /* Not a refusal and not an absence: the lane HAS the member and
             * states its box as a proportion of something, so there is no
             * pixel count to hand back. The caller then knows it has to
             * choose, which is the only honest answer. */
            Porcelain_RecordFinding(porcelain, "native_size", element,
                                    PORCELAIN_FINDING_UNSUPPORTED,
                                    "the lane states no pixel box for this member");
            return false;
        }
    }
    else if( !frame->surface_native_size(porcelain->api, surface, &w, &h) )
    {
        Porcelain_RecordFinding(porcelain, "native_size", element, PORCELAIN_FINDING_UNSUPPORTED,
                                "the lane states no pixel size for this surface");
        return false;
    }
    out->x = x;
    out->y = y;
    out->width = w;
    out->height = h;
    return true;
}

/* ------------------------------------------------------------------------ */
/* The lane's own icon numbering                                            */
/* ------------------------------------------------------------------------ */

int
Porcelain_LaneIcon(struct Porcelain* porcelain, struct PorcelainElement tab)
{
    struct PorcelainElementState state;
    int number;

    assert(porcelain);
    assert(tab.kind == PORCELAIN_EL_TAB);
    assert(tab.role);

    number = porcelain_tab_number(porcelain, tab.role);
    if( number < 0 )
        return -1;
    /*
     * Numbered AND present. The two are different questions and the frames
     * ledger has the bug from answering only the first: a 2004 rail over an
     * OldSchool lane wearing the 2004 icon set left the live seventh rock
     * bare and put the ignore face on Friends. A lane that numbers a tab it
     * does not mount -- LostCity's `componentno=-1` seventh slot -- has no
     * icon for it, and a stone that draws one invites a tap that does nothing.
     */
    if( !Porcelain_Element(porcelain, tab, &state) )
        return -1;
    return number;
}

/* ------------------------------------------------------------------------ */
/* Tab arrangement                                                          */
/* ------------------------------------------------------------------------ */

/*
 * The root, as a KEY.
 *
 * This is the one place a toplevel id is read, and it is never compared: it
 * is joined to a tab name and handed to the profile. `if (toplevel == 601)`
 * is the shape this file exists to make unnecessary; `[tabs:601] columns=` is
 * the same fact, stated where a new revision can restate it.
 */
static int
porcelain_frame_root_id(struct Porcelain* porcelain)
{
    struct ToriRS_CacheApi* cache = &porcelain->api->cache;

    if( !cache->frame_root )
        return -1;
    porcelain->counters.engine_calls++;
    return cache->frame_root(porcelain->api);
}

static int
porcelain_tab_root_fact(struct Porcelain* porcelain, int root, char const* kind, char const* name)
{
    struct ToriRS_CacheApi* cache = &porcelain->api->cache;
    char key[PORCELAIN_NAME_MAX];
    int value = -1;

    assert(name);
    if( root < 0 || !cache->named_id )
        return -1;
    snprintf(key, sizeof(key), "%d:%s", root, name);
    if( !cache->named_id(porcelain->api, kind, key, &value) )
        return -1;
    return value;
}

/* One tab, as the arrangement pass sees it. */
struct PorcelainTabSeat
{
    char const* name;
    /** The raw coordinate the seat is banded by, or the stated group index. */
    int group;
    /** Within the band, ascending. */
    int order;
    /*
     * How wide a band this seat's own stone makes, along the grouping axis.
     * Zero for a STATED arrangement, where the group is already an index and
     * any change of it is a new group.
     *
     * A band and not an equality, because a 2004 stone is not on a grid: the
     * sideicons carry their own baked offsets and the seven stones of the top
     * rail report y 171, 171, 171, 172, 173, 173, 173. Grouping on the exact
     * coordinate reads that one rail as five rows, and a frame that walked
     * them would put the account icon where friends belongs -- which is the
     * defect the whole verb exists to remove, arriving by another door.
     */
    int extent;
};

static void
porcelain_tab_sort(struct PorcelainTabSeat* seats, int count)
{
    /* Insertion sort on (group, order). Fourteen rows at most, and the cost
     * that matters is not this -- it is that the answer is stable, so a
     * description keyed on it does not churn when two stones share an edge. */
    for( int i = 1; i < count; i++ )
    {
        struct PorcelainTabSeat const seat = seats[i];
        int j = i - 1;
        while( j >= 0 && (seats[j].group > seat.group ||
                          (seats[j].group == seat.group && seats[j].order > seat.order)) )
        {
            seats[j + 1] = seats[j];
            j--;
        }
        seats[j + 1] = seat;
    }
}

/*
 * Seat every tab this lane BINDS, stated or derived.
 *
 * Stated first: a `[tabs:<root>]` section that names columns is the root
 * saying the derivation would be wrong here, and the two roots that say so
 * are the two the derivation IS wrong on -- 164 hangs logout off the top bar,
 * where walking the stones by row puts it at the head of the run, and 601
 * stacks thirteen stones in two columns that a row walk reads as thirteen
 * rows of one.
 *
 * Derived otherwise, from the BOXES the lane's own stones report: a group is
 * a shared row (or column) coordinate, and within a group the order is the
 * other axis ascending. That is right on 548, on 161 and on a root the
 * profile never listed, which is the case the whole design turns on.
 *
 * A detached tab is in no group at all, which is what detached MEANS; it is
 * left out here and answered by Porcelain_TabDetached.
 */
static int
porcelain_tab_seats(struct Porcelain* porcelain, int axis, struct PorcelainTabSeat* seats,
                    int capacity)
{
    int const root = porcelain_frame_root_id(porcelain);
    int const name_count = porcelain_tab_name_count();
    int count = 0;

    for( int i = 0; i < name_count && count < capacity; i++ )
    {
        char const* const name = PORCELAIN_TAB_NAME[i];
        struct PorcelainElement const element = PORCELAIN_TAB_EL(name);
        struct PorcelainElementState state;
        int column;

        if( !Porcelain_Element(porcelain, element, &state) )
            continue;
        if( porcelain_tab_root_fact(porcelain, root, "tabdetach", name) == 1 )
            continue;

        seats[count].name = name;
        column = porcelain_tab_root_fact(porcelain, root, "tabcol", name);
        if( column >= 0 )
        {
            int const position = porcelain_tab_root_fact(porcelain, root, "tabpos", name);
            /*
             * A stated arrangement is stated in COLUMNS, because that is the
             * shape the roots that need stating have. Asked for rows, the
             * same statement reads the other way round: a column index is a
             * position within a row and a position within a column is the row
             * itself. One table, both questions, no second section.
             */
            seats[count].group = axis == PORCELAIN_TAB_COLUMNS ? column : position;
            seats[count].order = axis == PORCELAIN_TAB_COLUMNS ? position : column;
            /* A stated group is already an index: any change of it opens a
             * new group, so there is no band to be inside. */
            seats[count].extent = 0;
        }
        else
        {
            /*
             * The CANVAS box, not the parent-local one. Tab order is SCREEN
             * order -- that is the whole content of the rule -- and 161 puts
             * its two strips in two containers, so the parent-local y of the
             * bottom strip's stones is the same as the top strip's and a
             * derivation off `local` reads fourteen stones as one row.
             */
            seats[count].group = axis == PORCELAIN_TAB_COLUMNS ? state.box.x : state.box.y;
            seats[count].order = axis == PORCELAIN_TAB_COLUMNS ? state.box.y : state.box.x;
            seats[count].extent =
                axis == PORCELAIN_TAB_COLUMNS ? state.box.width : state.box.height;
            if( seats[count].extent < 1 )
                seats[count].extent = 1;
        }
        count++;
    }

    /*
     * Band, then re-sort.
     *
     * Two passes because the two keys are not independent: the band is found
     * by walking the seats in coordinate order, and the ORDER within a band
     * is the other axis -- so a single sort on the raw coordinate puts the
     * top rail's stones in y order (171 before 173) rather than in the left-
     * to-right order the band is supposed to have.
     */
    porcelain_tab_sort(seats, count);
    {
        int band = 0;
        int start = count > 0 ? seats[0].group : 0;
        for( int i = 0; i < count; i++ )
        {
            int const width = seats[i].extent;
            bool const opened = width > 0 ? (seats[i].group - start) >= width
                                          : seats[i].group != start;
            if( i > 0 && opened )
            {
                band++;
                start = seats[i].group;
            }
            seats[i].group = band;
        }
    }
    porcelain_tab_sort(seats, count);
    return count;
}

int
Porcelain_TabGroupCount(struct Porcelain* porcelain, int axis)
{
    struct PorcelainTabSeat seats[PORCELAIN_TAB_MAX];
    int count;
    int groups = 0;

    assert(porcelain);
    assert(axis == PORCELAIN_TAB_ROWS || axis == PORCELAIN_TAB_COLUMNS);

    count = porcelain_tab_seats(porcelain, axis, seats, PORCELAIN_TAB_MAX);
    for( int i = 0; i < count; i++ )
        if( i == 0 || seats[i].group != seats[i - 1].group )
            groups++;
    /* No ceiling test, and that is a statement rather than an omission: there
     * are at most PORCELAIN_TAB_MAX seats and PORCELAIN_TAB_GROUPS_MAX is
     * that same number, so a group per seat is the worst case and it fits.
     * A budget finding here could never fire, and dead defence reads as live
     * defence to the next person. */
    assert(groups <= PORCELAIN_TAB_GROUPS_MAX);
    return groups;
}

int
Porcelain_TabGroup(struct Porcelain* porcelain, int axis, int group,
                   struct PorcelainElement* out, int capacity)
{
    struct PorcelainTabSeat seats[PORCELAIN_TAB_MAX];
    int count;
    int seen = -1;
    int written = 0;

    assert(porcelain);
    assert(axis == PORCELAIN_TAB_ROWS || axis == PORCELAIN_TAB_COLUMNS);
    assert(group >= 0);
    assert(out);
    assert(capacity > 0);

    count = porcelain_tab_seats(porcelain, axis, seats, PORCELAIN_TAB_MAX);
    for( int i = 0; i < count; i++ )
    {
        if( i == 0 || seats[i].group != seats[i - 1].group )
            seen++;
        if( seen != group )
            continue;
        if( written >= capacity )
        {
            Porcelain_RecordFinding(porcelain, "tab_group", PORCELAIN_EL(NONE),
                                    PORCELAIN_FINDING_BUDGET, seats[i].name);
            break;
        }
        /* The name points into the static vocabulary, so it outlives the
         * call: a caller holding these across a frame is holding a literal. */
        out[written++] = PORCELAIN_TAB_EL(seats[i].name);
    }
    return written;
}

struct PorcelainElement
Porcelain_TabDetached(struct Porcelain* porcelain)
{
    int const root = porcelain_frame_root_id(porcelain);
    int const name_count = porcelain_tab_name_count();

    assert(porcelain);
    for( int i = 0; i < name_count; i++ )
        if( porcelain_tab_root_fact(porcelain, root, "tabdetach", PORCELAIN_TAB_NAME[i]) == 1 )
            return PORCELAIN_TAB_EL(PORCELAIN_TAB_NAME[i]);
    /* NONE and not an absent TAB: "this root detaches nothing" is a different
     * statement from "this root's logout is missing", and a frame that hung a
     * stone off the top bar for the second would be hanging it off nothing. */
    return PORCELAIN_EL(NONE);
}
