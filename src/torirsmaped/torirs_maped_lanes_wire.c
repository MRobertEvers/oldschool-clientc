/*
 * Editor lanes on the wire (torirs_maped_lanes.h): LANE_* requests over the
 * lane module, and the server's own lane. Compiled INTO torirs_maped.c after
 * the accounts half (BUILD NOTE there): it uses the session helpers and the
 * save path. Never on a link line of its own.
 */

/* The change scan runs on a worker thread where the platform has one (the
 * catalog's warm build has the same shape): pthreads, and on Windows its own
 * threads (the tree's rule: src/makefile, "Windows uses its own threads"); the
 * web lane scans inline. Nothing on the thread allocates through ToriMem: the
 * rows buffer is the main thread's, filled by the scan and read after the
 * join.
 *
 * Windows scanned inline until 2026-11-10, on the editor's UI thread, and
 * there the walk was at its slowest (torirs_maped_lanes.c, "a directory as
 * the volume lists it"): TORIRS_EDITOR_FRAME_STATS showed one frame of 8.2 s
 * after every save and one of 9.4 s at the end of every RUN.
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
#if defined(__EMSCRIPTEN__)
#define LANES_SCAN_THREAD 0
#elif defined(_WIN32)
#include <process.h>
#define LANES_SCAN_THREAD 1
typedef HANDLE lanes_scan_thread_t;
#else
#include <pthread.h>
#define LANES_SCAN_THREAD 1
typedef pthread_t lanes_scan_thread_t;
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
    lanes_scan_thread_t thread;
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
#ifdef _WIN32
static unsigned __stdcall
lanes_scan_thread_main(void* arg)
{
    lanes_scan_run(arg);
    return 0;
}

/** 1 when the worker took the scan; 0: no thread, the caller walks inline. */
static int
lanes_scan_thread_start(struct ToriRSMapEdLaneScan* scan)
{
    /* _beginthreadex, not CreateThread: the walk uses the C runtime. */
    uintptr_t const handle = _beginthreadex(NULL, 0, lanes_scan_thread_main, scan, 0, NULL);
    if( handle == 0 )
        return 0;
    scan->thread = (HANDLE)handle;
    return 1;
}

static void
lanes_scan_thread_join(struct ToriRSMapEdLaneScan* scan)
{
    WaitForSingleObject(scan->thread, INFINITE);
    CloseHandle(scan->thread);
}
#else
static void*
lanes_scan_thread_main(void* arg)
{
    lanes_scan_run(arg);
    return NULL;
}

/** 1 when the worker took the scan; 0: no thread, the caller walks inline. */
static int
lanes_scan_thread_start(struct ToriRSMapEdLaneScan* scan)
{
    return pthread_create(&scan->thread, NULL, lanes_scan_thread_main, scan) == 0;
}

static void
lanes_scan_thread_join(struct ToriRSMapEdLaneScan* scan)
{
    pthread_join(scan->thread, NULL);
}
#endif
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
        lanes_scan_thread_join(scan);
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
    if( lanes_scan_thread_start(scan) )
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

static void
lanes_make_service(struct ToriRSMapEd* maped);

/** This server's own lane, counted before a write it has served since (a
 *  New square's create and its save, a record made by Copy to new): walked
 *  again unasked, so the status bar's chip and the Changes list follow a
 *  write the panel saw no save of. They asked on an unsaved-to-clean turn of
 *  the document and on the chip's 15 s period, and a New square, saved as it
 *  is made, never turns: its files stayed out of the list until a Refresh.
 *  Only a lane counted once already (nothing is walked before somebody asks),
 *  and only between requests, so the walk sees the finished write. */
static void
lanes_own_recount(struct ToriRSMapEd* maped)
{
    if( maped->lane.is_main || !maped->lane.base[0] || !maped->writable )
        return;
    for( int i = 0; i < TORIRSMAPED_LANES_MAX; i++ )
    {
        const struct ToriRSMapEdLaneCountCache* entry = &maped->lane_counts[i];
        if( !entry->used || strcmp(entry->name, maped->lane.name) != 0 )
            continue;
        if( entry->writes_own != maped->lane_writes_own || entry->writes_all != maped->lane_writes_all )
            lanes_scan_want(maped, &maped->lane);
        return;
    }
}

/** Once per service pass: a finished scan is joined, its count remembered
 *  (the chip and the LIST after it need no walk of their own), its count
 *  broadcast (and its rows, when it is this server's lane), and the next
 *  queued lane's scan started. With none running, this server's own lane is
 *  walked again if a write has outdated its count (lanes_own_recount). */
static void
lanes_service(struct ToriRSMapEd* maped)
{
    struct ToriRSMapEdLaneScan* scan;
    struct ToriRSMapEdLaneCounts counts;
    char name[TORIRSMAPED_LANE_NAME_MAX + 8];
    int is_main;
    int own;

    /* First: a promote landing frees the scan in flight. */
    lanes_make_service(maped);
    scan = maped->lane_scan;
    if( !scan )
    {
        lanes_own_recount(maped);
        return;
    }
    if( !__atomic_load_n(&scan->done, __ATOMIC_ACQUIRE) )
        return;
#if LANES_SCAN_THREAD
    if( scan->thread_live )
    {
        lanes_scan_thread_join(scan);
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

/** The local host's write guard on a lane (Editor_HostLocalSetWriteGuard):
 *  every directory above a save, a mkdir or a delete is made real first. It
 *  reads maped->lane at the call, not a copy: Follow moves its `created`. */
static int
lanes_write_guard(
    void* ctx,
    const char* rel,
    char* why,
    size_t why_size)
{
    struct ToriRSMapEd* maped = ctx;

    assert(maped);
    return ToriRSMapEdLanes_MakeWritable(&maped->lane, rel, why, why_size);
}

/** The guard ToriRSMapEd_Open installs on a lane's content opened as a plain
 *  tree: `ctx` is maped->plain_lane (ToriRSMapEdLanes_OfContent). */
static int
lanes_plain_write_guard(
    void* ctx,
    const char* rel,
    char* why,
    size_t why_size)
{
    const struct ToriRSMapEdLaneDef* lane = ctx;

    assert(lane);
    return ToriRSMapEdLanes_MakeWritable(lane, rel, why, why_size);
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
    /* A lane's directories may be links to its base (a sparse lane's, and a
     * legacy lane's asset trees): no save may land through one. `maped` is
     * the guard's context for the server's life, so it must not move. */
    if( !lane.is_main )
        Editor_HostLocalSetWriteGuard(&maped->files, lanes_write_guard, maped);
    TORIRS_LOG("torirsmaped: lane '%s': %s%s%s\n", lane.name, lane.content, lane.is_main ? "" : ", mirrors ",
        lane.is_main ? "" : lane.base);
    return 1;
}

void
ToriRSMapEd_SetLaneAsked(
    struct ToriRSMapEd* maped,
    const char* asked)
{
    assert(maped);
    snprintf(maped->lane_asked, sizeof(maped->lane_asked), "%s", asked && asked[0] ? asked : "main");
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
        : kind == TORIRSMAPED_LANE_PUBLISHED_DELETE  ? "delete"
        : kind == TORIRSMAPED_LANE_PUBLISHED_CHECK   ? "publish check"
                                                     : "create",
        message);
}

/** The base every lane is made from: this lane's, or this tree itself. */
static const char*
lanes_base_tree(const struct ToriRSMapEd* maped)
{
    return maped->lane.is_main ? maped->lane.content : maped->lane.base;
}

/** LANE_PUBLISH's dry run (TORIRSMAPED_LANE_PUBLISH_CHECK): what a publish
 *  into `target` would write, and which of those files the target lane has
 *  changed too -- they are overwritten, whole, with no merge. Nothing is
 *  saved or written. `written` carries the file count, `removed` the
 *  overlap. Both lanes are walked here, on the caller's press. */
static void
lanes_publish_check(
    struct ToriRSMapEd* maped,
    const char* target)
{
    enum
    {
        NAMED = 3,
    };
    static char overlap[NAMED][TORIRSMAPED_LANE_PATH];
    struct ToriRSMapEdLaneDef to;
    struct ToriRSMapEdLaneCounts counts;
    char message[1200];
    int ours = 0;
    int theirs = 0;
    int count;
    size_t at;

    memset(&counts, 0, sizeof(counts));
    if( !ToriRSMapEdLanes_Lookup(maped->repo_root, lanes_base_tree(maped), target, &to) )
    {
        snprintf(message, sizeof(message), "no lane named '%s'", target);
        lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_CHECK, EDITOR_HOST_ABSENT, message, target, NULL, 0);
        return;
    }
    if( strcmp(to.content, maped->lane.content) == 0 )
    {
        snprintf(message, sizeof(message), "lane '%s' cannot publish into itself", maped->lane.name);
        lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_CHECK, EDITOR_HOST_REFUSED, message, target, NULL, 0);
        return;
    }
    {
        /* What the publish itself would refuse (ToriRSMapEdLanes_Publish),
         * said before the confirm rather than after it. */
        char holder[512];
        if( ToriRSMapEdLanes_TreeHeld(to.content, holder, sizeof(holder)) )
        {
            snprintf(message, sizeof(message), "%s is open in an editor (%s): close that editor first", to.name, holder);
            lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_CHECK, EDITOR_HOST_LOCKED, message, target, NULL, 0);
            return;
        }
    }
    count = ToriRSMapEdLanes_PublishOverlap(&maped->lane, &to, overlap, NAMED, &ours, &theirs);
    counts.written = ours;
    counts.removed = count;
    if( to.is_main )
        snprintf(message, sizeof(message), "%d file(s) would be written into the MAIN tree, %s", ours, to.content);
    else if( count == 0 )
        snprintf(message, sizeof(message),
            "%d file(s) would be written into lane %s; none of them is a file %s changed too (it has %d change(s))",
            ours, to.name, to.name, theirs);
    else
    {
        at = (size_t)snprintf(message, sizeof(message),
            "%d file(s) would be written into lane %s, OVERWRITING %d file(s) it changed too:", ours, to.name, count);
        for( int i = 0; i < count && i < NAMED && at < sizeof(message); i++ )
            at += (size_t)snprintf(message + at, sizeof(message) - at, "%s %s", i ? "," : "", overlap[i]);
        if( count > NAMED && at < sizeof(message) )
            snprintf(message + at, sizeof(message) - at, " and %d more", count - NAMED);
    }
    lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_CHECK, EDITOR_HOST_OK, message, to.name, &counts, 0);
}

/** LANE_DISCARD with a path: that ONE change back to the base. Every dirty
 *  document is saved first -- a discard of one file must not cost the
 *  unsaved edits of the others, and the server's documents are dropped with
 *  it (the file's own document would otherwise save it straight back). */
static void
handle_lane_discard_one(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    const uint8_t* body,
    uint32_t length)
{
    struct ToriRSMapEdReader reader;
    struct ToriRSMapEdLaneCounts counts;
    char rel[TORIRSMAPED_LANE_PATH];
    char why[512];
    char message[TORIRSMAPED_LANE_PATH + 600];
    enum EditorHost_Status status;
    int saved = 0;
    int left;

    ToriRSMapEd_ReaderInit(&reader, body, length);
    ToriRSMapEd_GetString(&reader, rel, sizeof(rel));
    if( !ToriRSMapEd_ReaderDone(&reader) )
    {
        session->alive = 0;
        return;
    }
    memset(&counts, 0, sizeof(counts));
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
    status = save_all_docs(maped, &saved);
    catalog_saved(maped);
    if( status != EDITOR_HOST_OK )
    {
        lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_DISCARD, status,
            "a document did not save; nothing was discarded", "", NULL, 0);
        return;
    }
    if( !ToriRSMapEdLanes_DiscardOne(&maped->lane, rel, &counts, why, sizeof(why)) )
    {
        snprintf(message, sizeof(message), "discard of %s refused: %s", rel, why);
        lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_DISCARD, EDITOR_HOST_REFUSED, message, "", &counts, 0);
        return;
    }
    /* The documents go with it, as a whole discard's do: every window
     * reopens what it shows from the files. */
    Editor_DocFree(&maped->doc);
    Editor_UndoFree(&maped->undo);
    Editor_DocInit(&maped->doc, NULL);
    Editor_UndoInit(&maped->undo);
    catalog_atlas_changed(maped, 1);
    left = ToriRSMapEdLanes_Changes(&maped->lane, NULL, 0, NULL);
    {
        /* A file the lane ADDED is gone now, not "the base's again". */
        char base_file[2 * TORIRSMAPED_LANE_PATH];
        FILE* in;
        int in_base;
        snprintf(base_file, sizeof(base_file), "%s/%s", maped->lane.base, rel);
        in = fopen(base_file, "rb");
        in_base = in != NULL;
        if( in )
            fclose(in);
        snprintf(message, sizeof(message),
            in_base ? "discarded %s: it is the base's again (%d change(s) left)"
                    : "discarded %s: removed, as the base has none (%d change(s) left)",
            rel, left);
    }
    lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_DISCARD, EDITOR_HOST_OK, message, "", &counts, 1);
}

/** LANE_DELETE: a lane this server does not serve, gone
 *  (ToriRSMapEdLanes_Delete: never through a link into the base). */
static void
handle_lane_delete(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    const uint8_t* body,
    uint32_t length)
{
    struct ToriRSMapEdReader reader;
    struct ToriRSMapEdLaneDeleted deleted;
    struct ToriRSMapEdLaneCounts counts;
    char name[TORIRSMAPED_LANE_NAME_MAX + 8];
    char why[TORIRSMAPED_LANE_PATH * 2 + 512];
    char message[TORIRSMAPED_LANE_PATH * 2 + 700];

    ToriRSMapEd_ReaderInit(&reader, body, length);
    ToriRSMapEd_GetString(&reader, name, sizeof(name));
    if( !ToriRSMapEd_ReaderDone(&reader) )
    {
        session->alive = 0;
        return;
    }
    memset(&counts, 0, sizeof(counts));
    if( !maped->repo_root[0] )
    {
        lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_DELETE, EDITOR_HOST_UNSUPPORTED,
            "this server has no repo root: no build/lanes to delete from", name, NULL, 0);
        return;
    }
    /* Its own lane: the server holds the tree's lock when it may write, and
     * a read-only server's lane is somebody else's session. Said in the
     * user's terms either way. */
    if( strcmp(name, maped->lane.name) == 0 )
    {
        snprintf(message, sizeof(message),
            "%s is the lane this editor is on; launch the editor on another lane to delete it",
            name);
        lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_DELETE, EDITOR_HOST_REFUSED, message, name, NULL, 0);
        return;
    }
    /* A walk in flight may be listing the lane that is about to go. */
    lanes_scan_free(maped);
    maped->lane_scan_queue_count = 0;
    if( !ToriRSMapEdLanes_Delete(maped->repo_root, lanes_base_tree(maped), name, &deleted, why, sizeof(why)) )
    {
        snprintf(message, sizeof(message), "%s", why);
        counts.written = deleted.links;
        counts.removed = deleted.files;
        lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_DELETE, EDITOR_HOST_REFUSED, message, name, &counts, 0);
        return;
    }
    {
        /* Its count is no lane's any more. */
        struct ToriRSMapEdLaneCountCache* entry = lanes_count_slot(maped, name);
        if( entry->used && strcmp(entry->name, name) == 0 )
            memset(entry, 0, sizeof(*entry));
    }
    counts.written = deleted.links;
    counts.removed = deleted.files;
    snprintf(message, sizeof(message),
        "lane %s deleted: %d link(s) to the base dropped, %d file(s) of its own removed; the base tree was not touched",
        name, deleted.links, deleted.files);
    lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_DELETE, EDITOR_HOST_OK, message, name, &counts, 0);
}

/** LANE_DIFF: the base's copy of one of the lane's files, for an external
 *  diff (ToriRSMapEdLanes_DiffBase). To the asking connection only. */
static void
handle_lane_diff(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    const uint8_t* body,
    uint32_t length)
{
    struct ToriRSMapEdReader reader;
    struct ToriRSMapEdBuf payload;
    char rel[TORIRSMAPED_LANE_PATH];
    char lane_path[TORIRSMAPED_LANE_PATH * 2 + 8];
    char copy[TORIRSMAPED_LANE_PATH + 64];
    char why[TORIRSMAPED_LANE_PATH + 256];
    enum EditorHost_Status status = EDITOR_HOST_OK;

    ToriRSMapEd_ReaderInit(&reader, body, length);
    ToriRSMapEd_GetString(&reader, rel, sizeof(rel));
    if( !ToriRSMapEd_ReaderDone(&reader) )
    {
        session->alive = 0;
        return;
    }
    snprintf(lane_path, sizeof(lane_path), "%s/%s", maped->lane.content, rel);
    if( !ToriRSMapEdLanes_DiffBase(&maped->lane, rel, copy, sizeof(copy), why, sizeof(why)) )
        status = EDITOR_HOST_REFUSED;
    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutU32(&payload, (uint32_t)status);
    ToriRSMapEd_PutString(&payload, lane_path);
    ToriRSMapEd_PutString(&payload, copy);
    ToriRSMapEd_PutString(&payload, why);
    send_buf(session, TORIRSMAPED_FACT_LANE_DIFF, &payload);
    ToriRSMapEd_BufFree(&payload);
}

/** LANE_PUBLISH once its target exists: this lane follows its base, then its
 *  changes go into `to`. `promoted`: `to` was made for it a moment ago (a
 *  promote), which the sentence says. `saved`: documents saved first. */
static void
lanes_publish_into(
    struct ToriRSMapEd* maped,
    struct ToriRSMapEdLaneDef* to,
    int promoted,
    uint32_t flags,
    int saved)
{
    struct ToriRSMapEdLaneCounts counts;
    char why[512];
    char message[1200];

    memset(&counts, 0, sizeof(counts));
    /* A pack/ copy the base has moved past would publish as a revert. */
    if( !ToriRSMapEdLanes_Follow(&maped->lane, &counts, why, sizeof(why)) )
    {
        snprintf(message, sizeof(message), "lane '%s' cannot follow its base: %s; nothing was published",
            maped->lane.name, why);
        lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_PUBLISH, EDITOR_HOST_REFUSED, message, to->name, NULL, 0);
        return;
    }
    if( !ToriRSMapEdLanes_Publish(&maped->lane, to, flags, &counts, why, sizeof(why)) )
    {
        snprintf(message, sizeof(message), "publish to %s stopped: %s (%d written, %d removed before it)", to->name,
            why, counts.written, counts.removed);
        lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_PUBLISH, EDITOR_HOST_IO_ERROR, message, to->name, &counts,
            0);
        return;
    }
    if( promoted )
        /* A promote: say what exists where now, not just a count. */
        snprintf(message, sizeof(message), "lane %s made with %d file(s)%s; this lane (%s) is unchanged", to->name,
            counts.written + counts.removed, counts.removed ? ", deletions among them" : "", maped->lane.name);
    else
        snprintf(message, sizeof(message), "published %d file(s) to %s%s%s%s", counts.written + counts.removed,
            to->name, to->is_main ? " (the main tree)" : "", counts.removed ? ", with deletions" : "",
            (flags & TORIRSMAPED_LANE_PUBLISH_RESET) && strcmp(to->content, maped->lane.base) == 0
                ? "; this lane is clean again"
                : "");
    if( saved )
    {
        size_t const at = strlen(message);
        snprintf(message + at, sizeof(message) - at, " (%d saved first)", saved);
    }
    lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_PUBLISH, EDITOR_HOST_OK, message, to->name, &counts, 0);
}

/* ---- making a lane off the UI thread ---------------------------------------
 *
 * A new lane's mirror is ~12,000 links, 13-18 s on Windows, and the embedded
 * server runs on the editor's UI thread: Promote, Lane from main and Scratch
 * lane froze the window that long -- Windows greyed it "Not Responding" and
 * the panel's "making the lane..." line was never drawn (2026-11-11).
 * ToriRSMapEdLanes_Create touches only the disk (its listings are malloc'd,
 * as the change scan's are; no server state), so it runs on a worker like
 * that scan. Its landing (lanes_make_service) answers on the UI thread, and a
 * promote's publish -- which reads this server's lane -- runs there too.
 * One make at a time per process; while it runs, what would change this
 * lane (a publish, a discard) or make another is refused with the reason.
 */
struct lanes_make
{
    struct ToriRSMapEd* owner;
    char repo_root[sizeof(((struct ToriRSMapEd*)0)->repo_root)];
    char base[TORIRSMAPED_LANE_PATH];
    char name[TORIRSMAPED_LANE_NAME_MAX + 8];
    /** A promote: on landing, this lane's changes are published into the
     *  new lane with `flags`; `saved` documents were saved at the press. */
    int promote;
    uint32_t flags;
    int saved;
    struct ToriRSMapEdLaneDef made;
    char why[512];
    int ok;
    int done;
#if LANES_SCAN_THREAD
    lanes_scan_thread_t thread;
    int thread_live;
#endif
};

static struct lanes_make* s_lanes_make;

static void
lanes_make_run(struct lanes_make* make)
{
    make->ok = ToriRSMapEdLanes_Create(make->repo_root, make->base, make->name, &make->made, make->why,
        sizeof(make->why));
    __atomic_store_n(&make->done, 1, __ATOMIC_RELEASE);
}

#if LANES_SCAN_THREAD
#ifdef _WIN32
static unsigned __stdcall
lanes_make_thread_main(void* arg)
{
    lanes_make_run(arg);
    return 0;
}
#else
static void*
lanes_make_thread_main(void* arg)
{
    lanes_make_run(arg);
    return NULL;
}
#endif
#endif

static void
lanes_make_join(struct lanes_make* make)
{
#if LANES_SCAN_THREAD
    if( !make->thread_live )
        return;
#ifdef _WIN32
    WaitForSingleObject(make->thread, INFINITE);
    CloseHandle(make->thread);
#else
    pthread_join(make->thread, NULL);
#endif
    make->thread_live = 0;
#else
    (void)make;
#endif
}

/** 1 when a lane is being made: the request's answer (`kind`, about
 *  `target`) says so, and the request is not served. */
static int
lanes_make_refused(
    struct ToriRSMapEd* maped,
    uint32_t kind,
    const char* target)
{
    char message[200];

    if( !s_lanes_make )
        return 0;
    snprintf(message, sizeof(message), "lane %s is still being made; wait for it (about 15 s)", s_lanes_make->name);
    lanes_published(maped, kind, EDITOR_HOST_BUSY, message, target, NULL, 0);
    return 1;
}

/** Make lane `name` from this server's base on the worker; lanes_make_service
 *  answers when it lands. */
static void
lanes_make_begin(
    struct ToriRSMapEd* maped,
    const char* name,
    int promote,
    uint32_t flags,
    int saved)
{
    struct lanes_make* make;

    assert(!s_lanes_make);
    make = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*make));
    assert(make);
    make->owner = maped;
    snprintf(make->repo_root, sizeof(make->repo_root), "%s", maped->repo_root);
    snprintf(make->base, sizeof(make->base), "%s", lanes_base_tree(maped));
    snprintf(make->name, sizeof(make->name), "%s", name);
    make->promote = promote;
    make->flags = flags;
    make->saved = saved;
    s_lanes_make = make;
#if LANES_SCAN_THREAD
    {
#ifdef _WIN32
        uintptr_t const handle = _beginthreadex(NULL, 0, lanes_make_thread_main, make, 0, NULL);
        make->thread_live = handle != 0;
        if( handle )
            make->thread = (HANDLE)handle;
#else
        make->thread_live = pthread_create(&make->thread, NULL, lanes_make_thread_main, make) == 0;
#endif
    }
    if( !make->thread_live )
        lanes_make_run(make);
#else
    lanes_make_run(make);
#endif
}

/** Once per service pass: a finished make is joined and answered (a
 *  promote's publish runs now, on this thread). */
static void
lanes_make_service(struct ToriRSMapEd* maped)
{
    struct lanes_make* make = s_lanes_make;
    char message[1200];

    if( !make || make->owner != maped || !__atomic_load_n(&make->done, __ATOMIC_ACQUIRE) )
        return;
    lanes_make_join(make);
    s_lanes_make = NULL;
    if( !make->ok )
    {
        snprintf(message, sizeof(message), "cannot make lane '%s': %s", make->name, make->why);
        lanes_published(maped,
            make->promote ? TORIRSMAPED_LANE_PUBLISHED_PUBLISH : TORIRSMAPED_LANE_PUBLISHED_CREATE,
            EDITOR_HOST_REFUSED, message, make->name, NULL, 0);
    }
    else if( make->promote )
    {
        /* A scan in flight reads the trees this is about to rewrite. */
        lanes_scan_free(maped);
        maped->lane_scan_queue_count = 0;
        lanes_publish_into(maped, &make->made, 1, make->flags, make->saved);
    }
    else
    {
        snprintf(message, sizeof(message), "lane '%s' made from %s (%s)", make->name, make->base,
            make->made.content);
        lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_CREATE, EDITOR_HOST_OK, message, make->name, NULL, 0);
    }
    ToriMem_Free(ToriMem_Editor(), make);
}

/** The server is closing: a make in flight is waited for (a lane is never
 *  left half-made by a quit) and dropped unanswered. */
static void
lanes_make_drop(struct ToriRSMapEd* maped)
{
    struct lanes_make* make = s_lanes_make;

    if( !make || make->owner != maped )
        return;
    lanes_make_join(make);
    s_lanes_make = NULL;
    ToriMem_Free(ToriMem_Editor(), make);
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
    char message[1200];
    uint32_t flags;
    struct ToriRSMapEdLaneDef to;
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
    if( flags & TORIRSMAPED_LANE_PUBLISH_CHECK )
    {
        lanes_publish_check(maped, target);
        return;
    }
    if( lanes_make_refused(maped, TORIRSMAPED_LANE_PUBLISHED_PUBLISH, new_name[0] ? new_name : target) )
        return;
    /* A scan in flight reads the tree this is about to rewrite. */
    lanes_scan_free(maped);
    maped->lane_scan_queue_count = 0;
    /* What is on disk is what gets published: every dirty document first. */
    status = save_all_docs(maped, &saved);
    /* The catalog takes in what those saves wrote (the publish below writes
     * another tree; a reset of this one is caught by the next sweep). */
    catalog_saved(maped);
    if( status != EDITOR_HOST_OK )
    {
        snprintf(message, sizeof(message), "a document did not save; nothing was published");
        lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_PUBLISH, status, message, target, NULL, 0);
        return;
    }
    if( new_name[0] )
    {
        /* A promote: the new lane is made on the worker, and the publish
         * into it runs when it lands (lanes_make_service). */
        lanes_make_begin(maped, new_name, 1, flags, saved);
        return;
    }
    if( !ToriRSMapEdLanes_Lookup(maped->repo_root, lanes_base_tree(maped), target, &to) )
    {
        snprintf(message, sizeof(message), "no lane named '%s'", target);
        lanes_published(maped, TORIRSMAPED_LANE_PUBLISHED_PUBLISH, EDITOR_HOST_ABSENT, message, target, NULL, 0);
        return;
    }
    lanes_publish_into(maped, &to, 0, flags, saved);
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
    /* A run that ignores the file (a test, an agent) launches again on the
     * lane its manifest names, whatever the file says: naming the file's
     * lane as "next" told every such session it was about to leave. */
    if( maped->repo_root[0] && !ToriRSMapEdLanes_BootIgnored() )
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
        snprintf(message, sizeof(message), "this editor is on lane '%s' already; every launch opens it from now on",
            name);
    else
    {
        /* The file the switch went to: a test's own (TORIRSMAPED_BOOT_LANE_FILE)
         * was called build/lanes/lane.ini here, which it never touched. */
        const char* const own = getenv("TORIRSMAPED_BOOT_LANE_FILE");
        char file[TORIRSMAPED_LANE_PATH + 32];
        if( own && own[0] )
            ToriRSMapEdLanes_BootFilePath(maped->repo_root, file, sizeof(file));
        else
            snprintf(file, sizeof(file), "build/lanes/lane.ini");
        snprintf(message, sizeof(message),
            "lane '%s' opens from the next launch on (%s); quit and launch the editor again to go there, this one "
            "stays on '%s': a running server cannot re-point its lane",
            name, file, maped->lane.name);
    }
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
            /* And what the boot asked for, so the panel can tell a lane
             * pinned by Switch from the manifest's own ("" never told). */
            ToriRSMapEd_PutString(&payload, maped->lane_asked);
        }
        {
            /* The life-cycle tail (torirs_maped_lanes.h): what this server
             * answers, whether its boot ignored the boot lane file, and
             * where that file is. */
            char boot_file[TORIRSMAPED_LANE_PATH + 32];
            boot_file[0] = '\0';
            if( maped->repo_root[0] )
                ToriRSMapEdLanes_BootFilePath(maped->repo_root, boot_file, sizeof(boot_file));
            ToriRSMapEd_PutU32(&payload, (uint32_t)(TORIRSMAPED_LANE_INFO_LIFECYCLE
                                                    | (ToriRSMapEdLanes_BootIgnored()
                                                              ? TORIRSMAPED_LANE_INFO_BOOT_IGNORED
                                                              : 0)));
            ToriRSMapEd_PutString(&payload, boot_file);
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
    else if( type == TORIRSMAPED_REQ_LANE_DISCARD && lanes_make_refused(maped, TORIRSMAPED_LANE_PUBLISHED_DISCARD, "") )
    {
        /* A promote in flight publishes this lane as it lands. */
    }
    else if( type == TORIRSMAPED_REQ_LANE_DISCARD && length > 0 )
        handle_lane_discard_one(session, maped, body, length);
    else if( type == TORIRSMAPED_REQ_LANE_DISCARD )
        handle_lane_discard(maped);
    else if( type == TORIRSMAPED_REQ_LANE_DELETE )
        handle_lane_delete(session, maped, body, length);
    else if( type == TORIRSMAPED_REQ_LANE_DIFF )
        handle_lane_diff(session, maped, body, length);
    else if( type == TORIRSMAPED_REQ_LANE_SWITCH )
        handle_lane_switch(session, maped, body, length);
    else if( type == TORIRSMAPED_REQ_LANE_HANDOUT )
    {
        /* A path of this tree is about to leave the editor (Open in editor,
         * Show in Explorer, Copy path): the lane's own first, and the path
         * as this server's tree spells it. A lane's content served as a
         * plain tree is that lane all the same. */
        const struct ToriRSMapEdLaneDef* lane =
            maped->lane.is_main && maped->plain_lane.base[0] ? &maped->plain_lane : &maped->lane;
        struct ToriRSMapEdReader reader;
        char rel[TORIRSMAPED_LANE_PATH];
        char path[TORIRSMAPED_LANE_PATH * 2];
        char why[512];
        enum EditorHost_Status status = EDITOR_HOST_OK;
        uint32_t folder_max;
        int files = 0;
        ToriRSMapEd_ReaderInit(&reader, body, length);
        ToriRSMapEd_GetString(&reader, rel, sizeof(rel));
        folder_max = ToriRSMapEd_GetU32(&reader);
        if( !ToriRSMapEd_ReaderDone(&reader) || folder_max > 100000u )
        {
            session->alive = 0;
            ToriRSMapEd_BufFree(&payload);
            return;
        }
        why[0] = '\0';
        snprintf(path, sizeof(path), "%s/%s", lane->content, rel);
        if( !lane->is_main && !maped->writable )
        {
            status = EDITOR_HOST_LOCKED;
            snprintf(why, sizeof(why), "read-only: this server does not hold its lane's lock");
        }
        else if( folder_max == 0 ? !ToriRSMapEdLanes_HandOut(lane, rel, why, sizeof(why))
                                 : !ToriRSMapEdLanes_HandOutFolder(lane, rel, (int)folder_max, &files, why, sizeof(why)) )
            status = EDITOR_HOST_IO_ERROR;
        else if( files < 0 )
            /* Said back in `why` with OK: the folder is the lane's, its files
             * past the cap are still links. */
            snprintf(why, sizeof(why), "more than %u files: only the folder itself was made the lane's", folder_max);
        ToriRSMapEd_PutU32(&payload, (uint32_t)status);
        ToriRSMapEd_PutString(&payload, path);
        ToriRSMapEd_PutString(&payload, why);
        send_buf(session, TORIRSMAPED_FACT_LANE_HANDED_OUT, &payload);
        if( status != EDITOR_HOST_OK )
            TORIRS_LOG("torirsmaped: lane hand-out of %s refused: %s\n", rel, why);
    }
    else if( type == TORIRSMAPED_REQ_LANE_CREATE )
    {
        struct ToriRSMapEdReader reader;
        char name[TORIRSMAPED_LANE_NAME_MAX + 8];
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
        else if( !lanes_make_refused(maped, TORIRSMAPED_LANE_PUBLISHED_CREATE, name) )
            /* Made on the worker; lanes_make_service answers. */
            lanes_make_begin(maped, name, 0, 0u, 0);
    }
    else
        session->alive = 0;
    ToriRSMapEd_BufFree(&payload);
}
