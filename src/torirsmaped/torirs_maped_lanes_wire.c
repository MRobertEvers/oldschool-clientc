/*
 * Editor lanes on the wire (torirs_maped_lanes.h): LANE_* requests over the
 * lane module, and the server's own lane. Compiled INTO torirs_maped.c after
 * the accounts half (BUILD NOTE there): it uses the session helpers and the
 * save path. Never on a link line of its own.
 */

/* The change scan runs on a worker thread where the platform has one (the
 * catalog's warm build has the same shape); the web and win32 lanes scan
 * inline. Nothing on the thread allocates through ToriMem: the rows buffer is
 * the main thread's, filled by the scan and read after the join.
 *
 * Every walk of a mirror goes through that scan: a LANE_CHANGES request, the
 * status bar's periodic COUNT_ONLY and the LIST's per-lane counts all ask
 * lanes_scan_want, which runs one scan at a time and queues the rest
 * (maped->lane_scan_queue); a request answers from the counts' cache as it
 * stands (or "counting", -1) and the landing broadcasts FACT_LANE_COUNT (and,
 * for this server's own lane, FACT_LANE_CHANGES) to every window. The embedded
 * server runs on the editor's UI thread, and a walk of a 100k-file mirror is
 * 3-5 s: before 2026-11-08 the chip's ask walked it there every 15 s whenever
 * the cache had expired, which an ACCOUNTS_LIST (a read) did on every pass. */
#if !defined(__EMSCRIPTEN__) && !defined(_WIN32)
#include <pthread.h>
#define LANES_SCAN_THREAD 1
#else
#define LANES_SCAN_THREAD 0
#endif

struct ToriRSMapEdLaneScan
{
    struct ToriRSMapEdLaneDef lane;
    struct ToriRSMapEdLaneChange* rows;
    struct ToriRSMapEdLaneCounts counts;
    int count;
    /** maped->lane_writes_own / _all when the scan began: what its count is
     *  remembered against (lanes_count_store). */
    uint32_t writes_own;
    uint32_t writes_all;
#if LANES_SCAN_THREAD
    pthread_t thread;
    int thread_live;
#endif
    int done;
};

static void
lanes_scan_run(struct ToriRSMapEdLaneScan* scan)
{
    scan->count = ToriRSMapEdLanes_Changes(&scan->lane, scan->rows, TORIRSMAPED_LANE_CHANGE_ROWS_MAX, &scan->counts);
    __atomic_store_n(&scan->done, 1, __ATOMIC_RELEASE);
}

#if LANES_SCAN_THREAD
static void*
lanes_scan_thread_main(void* arg)
{
    lanes_scan_run(arg);
    return NULL;
}
#endif

static void
lanes_put_changes(
    struct ToriRSMapEdBuf* payload,
    uint32_t state,
    const struct ToriRSMapEdLaneScan* scan)
{
    int const shown = scan ? (scan->count < TORIRSMAPED_LANE_CHANGE_ROWS_MAX ? scan->count
                                                                            : TORIRSMAPED_LANE_CHANGE_ROWS_MAX)
                           : 0;
    ToriRSMapEd_PutU32(payload, state);
    ToriRSMapEd_PutU32(payload, scan ? (uint32_t)scan->counts.modified : 0);
    ToriRSMapEd_PutU32(payload, scan ? (uint32_t)scan->counts.added : 0);
    ToriRSMapEd_PutU32(payload, scan ? (uint32_t)scan->counts.deleted : 0);
    ToriRSMapEd_PutU32(payload, (uint32_t)shown);
    for( int i = 0; i < shown; i++ )
    {
        ToriRSMapEd_PutU32(payload, (uint32_t)scan->rows[i].kind);
        ToriRSMapEd_PutString(payload, scan->rows[i].path);
        ToriRSMapEd_PutU32(payload, scan->rows[i].size);
    }
}

static void
lanes_scan_free(struct ToriRSMapEd* maped)
{
    struct ToriRSMapEdLaneScan* scan = maped->lane_scan;
    if( !scan )
        return;
#if LANES_SCAN_THREAD
    if( scan->thread_live )
        pthread_join(scan->thread, NULL);
#endif
    ToriMem_Free(ToriMem_Editor(), scan->rows);
    ToriMem_Free(ToriMem_Editor(), scan);
    maped->lane_scan = NULL;
}

static struct ToriRSMapEdLaneCountCache*
lanes_count_slot(
    struct ToriRSMapEd* maped,
    const char* name);

static void
lanes_count_store(
    struct ToriRSMapEd* maped,
    const struct ToriRSMapEdLaneDef* lane,
    int total,
    const struct ToriRSMapEdLaneCounts* counts);

static void
lanes_next_boot(
    const struct ToriRSMapEd* maped,
    char* out,
    size_t out_size);

static const char*
lanes_base_tree(const struct ToriRSMapEd* maped);

/** FACT_LANE_COUNT for one lane: to `session`, or to every window when NULL. */
static void
lanes_send_count(
    struct ToriRSMapEd* maped,
    struct ToriRSMapEdSession* session,
    const char* name,
    int is_main,
    const struct ToriRSMapEdLaneCounts* counts)
{
    struct ToriRSMapEdBuf payload;
    char next_boot[TORIRSMAPED_LANE_NAME_MAX + 8];

    memset(&payload, 0, sizeof(payload));
    lanes_next_boot(maped, next_boot, sizeof(next_boot));
    ToriRSMapEd_PutString(&payload, name);
    ToriRSMapEd_PutU32(&payload, (uint32_t)is_main);
    ToriRSMapEd_PutU32(&payload, (uint32_t)counts->modified);
    ToriRSMapEd_PutU32(&payload, (uint32_t)counts->added);
    ToriRSMapEd_PutU32(&payload, (uint32_t)counts->deleted);
    ToriRSMapEd_PutString(&payload, next_boot);
    if( session )
        send_buf(session, TORIRSMAPED_FACT_LANE_COUNT, &payload);
    else
        jobs_broadcast(maped, TORIRSMAPED_FACT_LANE_COUNT, &payload);
    ToriRSMapEd_BufFree(&payload);
}

/** Start the scan of `lane` now: the rows buffer, then the thread (or the
 *  walk inline where there is none). Exactly one scan runs at a time. */
static void
lanes_scan_start(
    struct ToriRSMapEd* maped,
    const struct ToriRSMapEdLaneDef* lane)
{
    struct ToriRSMapEdLaneScan* scan;

    assert(!maped->lane_scan);
    scan = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*scan));
    assert(scan);
    scan->lane = *lane;
    scan->writes_own = maped->lane_writes_own;
    scan->writes_all = maped->lane_writes_all;
    scan->rows = ToriMem_Alloc(ToriMem_Editor(), TORIRSMAPED_LANE_CHANGE_ROWS_MAX * sizeof(*scan->rows));
    assert(scan->rows);
    maped->lane_scan = scan;
#if LANES_SCAN_THREAD
    if( pthread_create(&scan->thread, NULL, lanes_scan_thread_main, scan) == 0 )
        scan->thread_live = 1;
    else
        lanes_scan_run(scan);
#else
    lanes_scan_run(scan);
#endif
}

/** The lane named `name`: this server's own, else one of build/lanes. 0 when
 *  there is no such lane any more (a queued name a Switch or a delete outran). */
static int
lanes_def_by_name(
    struct ToriRSMapEd* maped,
    const char* name,
    struct ToriRSMapEdLaneDef* out)
{
    struct ToriRSMapEdLaneDef* lanes;
    int count;
    int found = 0;

    if( strcmp(name, maped->lane.name) == 0 )
    {
        *out = maped->lane;
        return 1;
    }
    if( !maped->repo_root[0] )
        return 0;
    lanes = ToriMem_Alloc(ToriMem_Editor(), TORIRSMAPED_LANES_MAX * sizeof(*lanes));
    assert(lanes);
    count = ToriRSMapEdLanes_List(maped->repo_root, lanes_base_tree(maped), lanes, TORIRSMAPED_LANES_MAX);
    for( int i = 0; i < count && !found; i++ )
        if( strcmp(lanes[i].name, name) == 0 )
        {
            *out = lanes[i];
            found = 1;
        }
    ToriMem_Free(ToriMem_Editor(), lanes);
    return found;
}

static void
lanes_service(struct ToriRSMapEd* maped);

/** A walk of `lane` is wanted: start it, or queue it behind the one running
 *  (once; the main tree has no base and is never walked). Nothing here walks
 *  on this thread, except where the platform has no worker thread. */
static void
lanes_scan_want(
    struct ToriRSMapEd* maped,
    const struct ToriRSMapEdLaneDef* lane)
{
    if( lane->is_main || !lane->base[0] )
        return;
    if( maped->lane_scan )
    {
        if( strcmp(maped->lane_scan->lane.name, lane->name) == 0 )
            return;
        for( int i = 0; i < maped->lane_scan_queue_count; i++ )
            if( strcmp(maped->lane_scan_queue[i], lane->name) == 0 )
                return;
        if( maped->lane_scan_queue_count < TORIRSMAPED_LANES_MAX )
            snprintf(maped->lane_scan_queue[maped->lane_scan_queue_count++],
                sizeof(maped->lane_scan_queue[0]), "%s", lane->name);
        return;
    }
    lanes_scan_start(maped, lane);
    /* A scan that ran inline (no thread) is done: its landing is now. */
    lanes_service(maped);
}

/** Once per service pass: a finished scan is joined, its count remembered
 *  (the chip and the LIST after it need no walk of their own), its count
 *  broadcast (and its rows, when it is this server's lane), and the next
 *  queued lane's scan started. */
static void
lanes_service(struct ToriRSMapEd* maped)
{
    struct ToriRSMapEdLaneScan* scan = maped->lane_scan;
    struct ToriRSMapEdLaneCounts counts;
    char name[TORIRSMAPED_LANE_NAME_MAX + 8];
    int is_main;
    int own;

    if( !scan || !__atomic_load_n(&scan->done, __ATOMIC_ACQUIRE) )
        return;
#if LANES_SCAN_THREAD
    if( scan->thread_live )
    {
        pthread_join(scan->thread, NULL);
        scan->thread_live = 0;
    }
#endif
    {
        /* Against the writes the scan saw: one landed since wants a newer
         * count. */
        struct ToriRSMapEdLaneCountCache* entry;
        lanes_count_store(maped, &scan->lane, scan->count, &scan->counts);
        entry = lanes_count_slot(maped, scan->lane.name);
        entry->writes_own = scan->writes_own;
        entry->writes_all = scan->writes_all;
    }
    own = strcmp(scan->lane.name, maped->lane.name) == 0;
    if( own )
    {
        struct ToriRSMapEdBuf payload;
        memset(&payload, 0, sizeof(payload));
        lanes_put_changes(&payload, TORIRSMAPED_LANE_CHANGES_DONE, scan);
        jobs_broadcast(maped, TORIRSMAPED_FACT_LANE_CHANGES, &payload);
        ToriRSMapEd_BufFree(&payload);
    }
    counts = scan->counts;
    snprintf(name, sizeof(name), "%s", scan->lane.name);
    is_main = scan->lane.is_main;
    lanes_scan_free(maped);
    lanes_send_count(maped, NULL, name, is_main, &counts);

    /* The next lane waiting for a walk. */
    while( maped->lane_scan_queue_count > 0 && !maped->lane_scan )
    {
        struct ToriRSMapEdLaneDef next;
        int found = lanes_def_by_name(maped, maped->lane_scan_queue[0], &next);
        maped->lane_scan_queue_count--;
        memmove(&maped->lane_scan_queue[0], &maped->lane_scan_queue[1],
            (size_t)maped->lane_scan_queue_count * sizeof(maped->lane_scan_queue[0]));
        if( found && !next.is_main && next.base[0] )
        {
            lanes_scan_start(maped, &next);
            /* Inline (no thread): it is done; land it before the next. */
            lanes_service(maped);
        }
    }
}

/** LANE_CHANGES: start the scan (or join the one running) and answer the
 *  requester "scanning"; the rows reach every window from lanes_service. */
static void
lanes_scan_begin(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped)
{
    struct ToriRSMapEdBuf payload;

    memset(&payload, 0, sizeof(payload));
    if( maped->lane.is_main )
    {
        /* The main tree is its own base: the answer is empty, at once. */
        lanes_put_changes(&payload, TORIRSMAPED_LANE_CHANGES_DONE, NULL);
        send_buf(session, TORIRSMAPED_FACT_LANE_CHANGES, &payload);
        ToriRSMapEd_BufFree(&payload);
        return;
    }
    lanes_put_changes(&payload, TORIRSMAPED_LANE_CHANGES_SCANNING, NULL);
    send_buf(session, TORIRSMAPED_FACT_LANE_CHANGES, &payload);
    ToriRSMapEd_BufFree(&payload);
    lanes_scan_want(maped, &maped->lane);
}

int
ToriRSMapEd_OpenLane(
    struct ToriRSMapEd* maped,
    const char* base_tree,
    const char* repo_root,
    const char* lane_name,
    char* why,
    size_t why_size)
{
    struct ToriRSMapEdLaneDef lane;

    assert(maped);
    assert(base_tree);
    assert(lane_name);
    assert(why);
    why[0] = '\0';
    if( !repo_root || !repo_root[0] )
    {
        snprintf(why, why_size, "a lane needs the repo root (build/lanes lives under it)");
        return 0;
    }
    if( !ToriRSMapEdLanes_Open(repo_root, base_tree, lane_name, &lane, why, why_size) )
        return 0;
    /* The lane's live dir is the lane's: BUILD, RUN and the servers' saves
     * and logs belong to the lane, never to the tree's default lane. */
    if( !lane.is_main )
        setenv("TORIRSMAPED_LIVE_DIR", lane.live, 1);
    ToriRSMapEd_Open(maped, lane.content, repo_root);
    maped->lane = lane;
    maped->lane_bound = 1;
    TORIRS_LOG("torirsmaped: lane '%s': %s%s%s\n", lane.name, lane.content, lane.is_main ? "" : ", mirrors ",
        lane.is_main ? "" : lane.base);
    return 1;
}

static void
lanes_put_def(
    struct ToriRSMapEdBuf* out,
    const struct ToriRSMapEdLaneDef* lane,
    const struct ToriRSMapEd* maped)
{
    ToriRSMapEd_PutString(out, lane->name);
    ToriRSMapEd_PutString(out, lane->content);
    ToriRSMapEd_PutString(out, lane->base);
    ToriRSMapEd_PutU32(out, (uint32_t)lane->is_main);
    ToriRSMapEd_PutU32(out, (uint32_t)(strcmp(lane->content, maped->lane.content) == 0));
}

static void
lanes_published(
    struct ToriRSMapEd* maped,
    uint32_t kind,
    enum EditorHost_Status status,
    const char* message,
    const char* target,
    const struct ToriRSMapEdLaneCounts* counts,
    int reload)
{
    struct ToriRSMapEdBuf payload;

    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutU32(&payload, kind);
    ToriRSMapEd_PutU32(&payload, (uint32_t)status);
    ToriRSMapEd_PutString(&payload, message);
    ToriRSMapEd_PutString(&payload, target ? target : "");
    ToriRSMapEd_PutU32(&payload, counts ? (uint32_t)counts->written : 0);
    ToriRSMapEd_PutU32(&payload, counts ? (uint32_t)counts->removed : 0);
    ToriRSMapEd_PutU32(&payload, (uint32_t)reload);
    jobs_broadcast(maped, TORIRSMAPED_FACT_LANE_PUBLISHED, &payload);
    ToriRSMapEd_BufFree(&payload);
    TORIRS_LOG("torirsmaped: lane %s: %s\n",
        kind == TORIRSMAPED_LANE_PUBLISHED_PUBLISH  ? "publish"
        : kind == TORIRSMAPED_LANE_PUBLISHED_DISCARD ? "discard"
        : kind == TORIRSMAPED_LANE_PUBLISHED_SWITCH  ? "switch"
                                                     : "create",
        message);
}

/** The base every lane is made from: this lane's, or this tree itself. */
static const char*
lanes_base_tree(const struct ToriRSMapEd* maped)
{
    return maped->lane.is_main ? maped->lane.content : maped->lane.base;
}

static void
handle_lane_publish(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    const uint8_t* body,
    uint32_t length)
{
    struct ToriRSMapEdReader reader;
    char target[TORIRSMAPED_LANE_NAME_MAX + 8];
    char new_name[TORIRSMAPED_LANE_NAME_MAX + 8];
    char why[512];
    char message[1200];
    uint32_t flags;
    struct ToriRSMapEdLaneDef to;
    struct ToriRSMapEdLaneCounts counts;
    int saved = 0;
    enum EditorHost_Status status;

    ToriRSMapEd_ReaderInit(&reader, body, length);
    ToriRSMapEd_GetString(&reader, target, sizeof(target));
    ToriRSMapEd_GetString(&reader, new_name, sizeof(new_name));
    flags = ToriRSMapEd_GetU32(&reader);
    if( !ToriRSMapEd_ReaderDone(&reader) )
    {
        session->alive = 0;
        return;
    }
    memset(&counts, 0, sizeof(counts));
    if( maped->lane.is_main )
    {
        lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_PUBLISH, EDITOR_HOST_REFUSED,
            "this editor edits the main tree directly: there is nothing to publish (start it on a lane)", target,
            NULL, 0);
        return;
    }
    if( !maped->writable )
    {
        lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_PUBLISH, EDITOR_HOST_LOCKED,
            "read-only: this server does not hold its lane's lock", target, NULL, 0);
        return;
    }
    /* A scan in flight reads the tree this is about to rewrite. */
    lanes_scan_free(maped);
    maped->lane_scan_queue_count = 0;
    /* What is on disk is what gets published: every dirty document first. */
    status = save_all_docs(maped, &saved);
    if( status != EDITOR_HOST_OK )
    {
        snprintf(message, sizeof(message), "a document did not save; nothing was published");
        lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_PUBLISH, status, message, target, NULL, 0);
        return;
    }
    if( new_name[0] )
    {
        if( !ToriRSMapEdLanes_Create(maped->repo_root, lanes_base_tree(maped), new_name, &to, why, sizeof(why)) )
        {
            snprintf(message, sizeof(message), "cannot make lane '%s': %s", new_name, why);
            lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_PUBLISH, EDITOR_HOST_REFUSED, message, new_name, NULL,
                0);
            return;
        }
        snprintf(target, sizeof(target), "%s", new_name);
    }
    else if( !ToriRSMapEdLanes_Lookup(maped->repo_root, lanes_base_tree(maped), target, &to) )
    {
        snprintf(message, sizeof(message), "no lane named '%s'", target);
        lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_PUBLISH, EDITOR_HOST_ABSENT, message, target, NULL, 0);
        return;
    }
    /* A pack/ copy the base has moved past would publish as a revert. */
    if( !ToriRSMapEdLanes_Follow(&maped->lane, &counts, why, sizeof(why)) )
    {
        snprintf(message, sizeof(message), "lane '%s' cannot follow its base: %s; nothing was published",
            maped->lane.name, why);
        lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_PUBLISH, EDITOR_HOST_REFUSED, message, target, NULL, 0);
        return;
    }
    if( !ToriRSMapEdLanes_Publish(&maped->lane, &to, flags, &counts, why, sizeof(why)) )
    {
        snprintf(message, sizeof(message), "publish to %s stopped: %s (%d written, %d removed before it)", to.name,
            why, counts.written, counts.removed);
        lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_PUBLISH, EDITOR_HOST_IO_ERROR, message, to.name, &counts,
            0);
        return;
    }
    snprintf(message, sizeof(message), "published %d file(s) to %s%s%s%s", counts.written + counts.removed,
        to.name, to.is_main ? " (the main tree)" : "", counts.removed ? ", with deletions" : "",
        (flags & TORIRSMAPED_LANE_PUBLISH_RESET) && strcmp(to.content, maped->lane.base) == 0
            ? "; this lane is clean again"
            : "");
    if( saved )
    {
        size_t const at = strlen(message);
        snprintf(message + at, sizeof(message) - at, " (%d saved first)", saved);
    }
    lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_PUBLISH, EDITOR_HOST_OK, message, to.name, &counts, 0);
}

static void
handle_lane_discard(
    struct ToriRSMapEd* maped)
{
    struct ToriRSMapEdLaneCounts counts;
    char why[512];
    char message[800];

    if( maped->lane.is_main )
    {
        lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_DISCARD, EDITOR_HOST_REFUSED,
            "this editor edits the main tree directly: there is no base to go back to", "", NULL, 0);
        return;
    }
    if( !maped->writable )
    {
        lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_DISCARD, EDITOR_HOST_LOCKED,
            "read-only: this server does not hold its lane's lock", "", NULL, 0);
        return;
    }
    lanes_scan_free(maped);
    maped->lane_scan_queue_count = 0;
    /* Unsaved edits are changes too: the documents go with the files, and
     * every window reopens what it shows from the base. */
    Editor_DocFree(&maped->doc);
    Editor_UndoFree(&maped->undo);
    Editor_DocInit(&maped->doc, NULL);
    Editor_UndoInit(&maped->undo);
    if( !ToriRSMapEdLanes_Discard(&maped->lane, &counts, why, sizeof(why)) )
    {
        snprintf(message, sizeof(message), "discard stopped: %s (%d put back, %d removed before it)", why,
            counts.written, counts.removed);
        lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_DISCARD, EDITOR_HOST_IO_ERROR, message, "", &counts, 1);
        catalog_atlas_changed(maped, 1);
        return;
    }
    snprintf(message, sizeof(message), "discarded %d change(s): the lane is the base again",
        counts.written + counts.removed);
    catalog_atlas_changed(maped, 1);
    lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_DISCARD, EDITOR_HOST_OK, message, "", &counts, 1);
}

/** The lane the next boot serves, "" when build/lanes/lane.ini names none
 *  (or this server has no repo root). */
static void
lanes_next_boot(
    const struct ToriRSMapEd* maped,
    char* out,
    size_t out_size)
{
    out[0] = '\0';
    if( maped->repo_root[0] )
        (void)ToriRSMapEdLanes_BootName(maped->repo_root, out, out_size);
}

/** LANE_SWITCH: stage the lane the next boot serves. A running server cannot
 *  re-point its lane (torirs_maped_lanes.h), so it says so. */
static void
handle_lane_switch(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    const uint8_t* body,
    uint32_t length)
{
    struct ToriRSMapEdReader reader;
    char name[TORIRSMAPED_LANE_NAME_MAX + 8];
    char why[512];
    char message[1200];

    ToriRSMapEd_ReaderInit(&reader, body, length);
    ToriRSMapEd_GetString(&reader, name, sizeof(name));
    if( !ToriRSMapEd_ReaderDone(&reader) )
    {
        session->alive = 0;
        return;
    }
    if( !maped->repo_root[0] )
    {
        lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_SWITCH, EDITOR_HOST_UNSUPPORTED,
            "this server has no repo root: no build/lanes to switch in", name, NULL, 0);
        return;
    }
    if( !ToriRSMapEdLanes_SetBoot(maped->repo_root, lanes_base_tree(maped), name, why, sizeof(why)) )
    {
        snprintf(message, sizeof(message), "Switch refused: %s", why);
        lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_SWITCH, EDITOR_HOST_REFUSED, message, name, NULL, 0);
        return;
    }
    if( strcmp(name, maped->lane.name) == 0 )
        snprintf(message, sizeof(message), "this editor serves lane '%s' now and from every boot on", name);
    else
        snprintf(message, sizeof(message),
            "the next editor boot serves lane '%s' (build/lanes/lane.ini); this one keeps '%s' until it restarts: "
            "a running server cannot re-point its lane",
            name, maped->lane.name);
    lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_SWITCH, EDITOR_HOST_OK, message, name, NULL, 0);
}

/* ---- the change counts' cache ----------------------------------------------------------
 *
 * A count is a walk of the lane's mirror (ToriRSMapEdLanes_Changes: an
 * lstat per entry of a content tree, and the base again), and the embedded
 * server runs on the editor's UI thread: a walk is a frame or more. So the
 * counts are remembered per lane (struct ToriRSMapEdLaneCountCache) and the
 * status bar's periodic COUNT_ONLY, a LIST on the panel's open or after a
 * save, and every popped-out window's own asks answer from here. An entry
 * holds while no request that can write a tree has arrived since it was
 * walked (note_lane_writes, torirs_maped.c; a BUILD's or RUN's end). This
 * server's own lane, while it holds the lock, has no other writer the
 * server would hear of, so it holds until then; another lane (another
 * editor may be saving there), or this one while another server holds it,
 * also expires after TORIRSMAPED_LANE_COUNT_STALE_S. An edit made outside
 * every editor (a text editor, git) shows on the next save or Refresh. */

static struct ToriRSMapEdLaneCountCache*
lanes_count_slot(
    struct ToriRSMapEd* maped,
    const char* name)
{
    struct ToriRSMapEdLaneCountCache* oldest = &maped->lane_counts[0];
    for( int i = 0; i < TORIRSMAPED_LANES_MAX; i++ )
    {
        struct ToriRSMapEdLaneCountCache* entry = &maped->lane_counts[i];
        if( entry->used && strcmp(entry->name, name) == 0 )
            return entry;
    }
    for( int i = 0; i < TORIRSMAPED_LANES_MAX; i++ )
    {
        struct ToriRSMapEdLaneCountCache* entry = &maped->lane_counts[i];
        if( !entry->used )
            return entry;
        if( entry->walked_at < oldest->walked_at )
            oldest = entry;
    }
    return oldest;
}

static void
lanes_count_store(
    struct ToriRSMapEd* maped,
    const struct ToriRSMapEdLaneDef* lane,
    int total,
    const struct ToriRSMapEdLaneCounts* counts)
{
    struct ToriRSMapEdLaneCountCache* entry = lanes_count_slot(maped, lane->name);
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->name, sizeof(entry->name), "%s", lane->name);
    entry->used = 1;
    entry->total = total;
    entry->counts = *counts;
    entry->writes_own = maped->lane_writes_own;
    entry->writes_all = maped->lane_writes_all;
    entry->walked_at = (long long)time(NULL);
}

static void
lanes_scan_want(
    struct ToriRSMapEd* maped,
    const struct ToriRSMapEdLaneDef* lane);

/** 1 when the cache's entry for `lane` answers for it now (above). */
static int
lanes_count_holds(
    const struct ToriRSMapEd* maped,
    const struct ToriRSMapEdLaneCountCache* entry,
    const struct ToriRSMapEdLaneDef* lane)
{
    int const own = strcmp(lane->name, maped->lane.name) == 0;
    return entry->used && strcmp(entry->name, lane->name) == 0 && entry->writes_all == maped->lane_writes_all
           && (!own || entry->writes_own == maped->lane_writes_own)
           && ((own && maped->writable) || (long long)time(NULL) - entry->walked_at < TORIRSMAPED_LANE_COUNT_STALE_S);
}

/** `lane`'s change count (and its M/A/D in `counts`), from the cache when
 *  it holds (above). When it does not, or `rescan` asks, the walk is wanted
 *  of the worker (lanes_scan_want) and the answer is the cache as it stands:
 *  the last count, or -1 ("counting") for a lane never walked; the landing
 *  broadcasts FACT_LANE_COUNT. Never a walk on this thread: it is the UI's. */
static int
lanes_count(
    struct ToriRSMapEd* maped,
    const struct ToriRSMapEdLaneDef* lane,
    int rescan,
    struct ToriRSMapEdLaneCounts* counts)
{
    struct ToriRSMapEdLaneCountCache* entry = lanes_count_slot(maped, lane->name);

    if( !rescan && lanes_count_holds(maped, entry, lane) )
    {
        *counts = entry->counts;
        return entry->total;
    }
    if( getenv("TORIRSMAPED_LANES_DEBUG") )
        fprintf(stderr,
            "torirsmaped: lanes: count of '%s' wants a walk: rescan %d used %d writes_all %u/%u writes_own %u/%u"
            " writable %d age %lld s\n",
            lane->name, rescan, entry->used, entry->writes_all, maped->lane_writes_all, entry->writes_own,
            maped->lane_writes_own, maped->writable, (long long)time(NULL) - entry->walked_at);
    lanes_scan_want(maped, lane);
    /* Inline (no worker thread): the walk has landed into the cache. */
    entry = lanes_count_slot(maped, lane->name);
    if( entry->used && strcmp(entry->name, lane->name) == 0 )
    {
        *counts = entry->counts;
        return entry->total;
    }
    memset(counts, 0, sizeof(*counts));
    return -1;
}

static void
handle_lanes(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    uint32_t type,
    const uint8_t* body,
    uint32_t length)
{
    struct ToriRSMapEdBuf payload;

    memset(&payload, 0, sizeof(payload));
    /* TORIRSMAPED_LANES_DEBUG=1: every lane request as it arrives. */
    if( getenv("TORIRSMAPED_LANES_DEBUG") )
        fprintf(stderr, "torirsmaped: lanes: request %u (%u bytes) from client %u\n", type, length,
            session->client_id);
    if( type == TORIRSMAPED_REQ_LANE_INFO )
    {
        ToriRSMapEd_PutString(&payload, maped->lane.name);
        ToriRSMapEd_PutString(&payload, maped->lane.content);
        ToriRSMapEd_PutString(&payload, maped->lane.live);
        ToriRSMapEd_PutString(&payload, maped->lane.base);
        ToriRSMapEd_PutU32(&payload, (uint32_t)maped->lane.is_main);
        ToriRSMapEd_PutU32(&payload, (uint32_t)maped->writable);
        {
            char next_boot[TORIRSMAPED_LANE_NAME_MAX + 8];
            lanes_next_boot(maped, next_boot, sizeof(next_boot));
            ToriRSMapEd_PutString(&payload, next_boot);
        }
        send_buf(session, TORIRSMAPED_FACT_LANE_INFO, &payload);
    }
    else if( type == TORIRSMAPED_REQ_LANE_LIST )
    {
        struct ToriRSMapEdLaneDef* lanes =
            ToriMem_Alloc(ToriMem_Editor(), TORIRSMAPED_LANES_MAX * sizeof(*lanes));
        int count;
        assert(lanes);
        count = maped->repo_root[0]
                    ? ToriRSMapEdLanes_List(maped->repo_root, lanes_base_tree(maped), lanes, TORIRSMAPED_LANES_MAX)
                    : 0;
        ToriRSMapEd_PutU32(&payload, (uint32_t)count);
        for( int i = 0; i < count; i++ )
            lanes_put_def(&payload, &lanes[i], maped);
        /* The workbench tail (TORIRSMAPED_CAP_LANES_V2): each lane's change
         * count and its tree's lock holder. A count is the cache's (above);
         * the panel's Refresh sends TORIRSMAPED_LANE_LIST_RESCAN to walk
         * every mirror again. */
        int rescan = 0;
        if( length >= 4 )
            rescan = (ToriRSMapEd_ReadU32(body) & TORIRSMAPED_LANE_LIST_RESCAN) != 0;
        ToriRSMapEd_PutU32(&payload, (uint32_t)count);
        for( int i = 0; i < count; i++ )
        {
            char held[256];
            int changes = 0;
            struct ToriRSMapEdLaneCounts counts;
            if( !lanes[i].is_main )
                changes = lanes_count(maped, &lanes[i], rescan, &counts);
            if( !ToriRSMapEdLanes_TreeHeld(lanes[i].content, held, sizeof(held)) )
                held[0] = '\0';
            ToriRSMapEd_PutU32(&payload, (uint32_t)changes);
            ToriRSMapEd_PutString(&payload, held);
        }
        ToriMem_Free(ToriMem_Editor(), lanes);
        send_buf(session, TORIRSMAPED_FACT_LANE_LIST, &payload);
    }
    else if( type == TORIRSMAPED_REQ_LANE_CHANGES && length > 0 )
    {
        /* The count alone (the status bar's chip): FACT_LANE_COUNT. */
        struct ToriRSMapEdReader reader;
        struct ToriRSMapEdLaneCounts counts;
        uint32_t flags;
        ToriRSMapEd_ReaderInit(&reader, body, length);
        flags = ToriRSMapEd_GetU32(&reader);
        if( !ToriRSMapEd_ReaderDone(&reader) || !(flags & TORIRSMAPED_LANE_CHANGES_COUNT_ONLY) )
        {
            session->alive = 0;
            ToriRSMapEd_BufFree(&payload);
            return;
        }
        /* -1: no count yet; the scan just wanted broadcasts it when it lands
         * (lanes_service), to this window among every other. */
        if( lanes_count(maped, &maped->lane, 0, &counts) >= 0 )
            lanes_send_count(maped, session, maped->lane.name, maped->lane.is_main, &counts);
    }
    else if( type == TORIRSMAPED_REQ_LANE_CHANGES )
        lanes_scan_begin(session, maped);
    else if( type == TORIRSMAPED_REQ_LANE_PUBLISH )
        handle_lane_publish(session, maped, body, length);
    else if( type == TORIRSMAPED_REQ_LANE_DISCARD )
        handle_lane_discard(maped);
    else if( type == TORIRSMAPED_REQ_LANE_SWITCH )
        handle_lane_switch(session, maped, body, length);
    else if( type == TORIRSMAPED_REQ_LANE_CREATE )
    {
        struct ToriRSMapEdReader reader;
        char name[TORIRSMAPED_LANE_NAME_MAX + 8];
        char why[512];
        char message[800];
        struct ToriRSMapEdLaneDef made;
        ToriRSMapEd_ReaderInit(&reader, body, length);
        ToriRSMapEd_GetString(&reader, name, sizeof(name));
        if( !ToriRSMapEd_ReaderDone(&reader) )
        {
            session->alive = 0;
            ToriRSMapEd_BufFree(&payload);
            return;
        }
        if( !maped->repo_root[0] )
            lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_CREATE, EDITOR_HOST_UNSUPPORTED,
                "this server has no repo root: no build/lanes to make one in", name, NULL, 0);
        else if( !ToriRSMapEdLanes_Create(maped->repo_root, lanes_base_tree(maped), name, &made, why, sizeof(why)) )
        {
            snprintf(message, sizeof(message), "cannot make lane '%s': %s", name, why);
            lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_CREATE, EDITOR_HOST_REFUSED, message, name, NULL, 0);
        }
        else
        {
            snprintf(message, sizeof(message), "lane '%s' made from %s (%s)", name, lanes_base_tree(maped),
                made.content);
            lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_CREATE, EDITOR_HOST_OK, message, name, NULL, 0);
        }
    }
    else
        session->alive = 0;
    ToriRSMapEd_BufFree(&payload);
}
