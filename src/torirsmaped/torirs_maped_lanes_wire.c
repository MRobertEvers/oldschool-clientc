/*
 * Editor lanes on the wire (torirs_maped_lanes.h): LANE_* requests over the
 * lane module, and the server's own lane. Compiled INTO torirs_maped.c after
 * the accounts half (BUILD NOTE there): it uses the session helpers and the
 * save path. Never on a link line of its own.
 */

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
         * count and its tree's lock holder. A count is a scan of the lane's
         * mirror, done here on an ask (the panel's open, Refresh), never on
         * a timer. */
        ToriRSMapEd_PutU32(&payload, (uint32_t)count);
        for( int i = 0; i < count; i++ )
        {
            char held[256];
            int changes = 0;
            if( !lanes[i].is_main )
                changes = ToriRSMapEdLanes_Changes(&lanes[i], NULL, 0, NULL);
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
        char next_boot[TORIRSMAPED_LANE_NAME_MAX + 8];
        uint32_t flags;
        ToriRSMapEd_ReaderInit(&reader, body, length);
        flags = ToriRSMapEd_GetU32(&reader);
        if( !ToriRSMapEd_ReaderDone(&reader) || !(flags & TORIRSMAPED_LANE_CHANGES_COUNT_ONLY) )
        {
            session->alive = 0;
            ToriRSMapEd_BufFree(&payload);
            return;
        }
        memset(&counts, 0, sizeof(counts));
        (void)ToriRSMapEdLanes_Changes(&maped->lane, NULL, 0, &counts);
        lanes_next_boot(maped, next_boot, sizeof(next_boot));
        ToriRSMapEd_PutString(&payload, maped->lane.name);
        ToriRSMapEd_PutU32(&payload, (uint32_t)maped->lane.is_main);
        ToriRSMapEd_PutU32(&payload, (uint32_t)counts.modified);
        ToriRSMapEd_PutU32(&payload, (uint32_t)counts.added);
        ToriRSMapEd_PutU32(&payload, (uint32_t)counts.deleted);
        ToriRSMapEd_PutString(&payload, next_boot);
        send_buf(session, TORIRSMAPED_FACT_LANE_COUNT, &payload);
    }
    else if( type == TORIRSMAPED_REQ_LANE_CHANGES )
    {
        struct ToriRSMapEdLaneChange* rows =
            ToriMem_Alloc(ToriMem_Editor(), TORIRSMAPED_LANE_CHANGE_ROWS_MAX * sizeof(*rows));
        struct ToriRSMapEdLaneCounts counts;
        int count;
        int shown;
        assert(rows);
        count = ToriRSMapEdLanes_Changes(&maped->lane, rows, TORIRSMAPED_LANE_CHANGE_ROWS_MAX, &counts);
        shown = count < TORIRSMAPED_LANE_CHANGE_ROWS_MAX ? count : TORIRSMAPED_LANE_CHANGE_ROWS_MAX;
        ToriRSMapEd_PutU32(&payload, (uint32_t)counts.modified);
        ToriRSMapEd_PutU32(&payload, (uint32_t)counts.added);
        ToriRSMapEd_PutU32(&payload, (uint32_t)counts.deleted);
        ToriRSMapEd_PutU32(&payload, (uint32_t)shown);
        for( int i = 0; i < shown; i++ )
        {
            ToriRSMapEd_PutU32(&payload, (uint32_t)rows[i].kind);
            ToriRSMapEd_PutString(&payload, rows[i].path);
            ToriRSMapEd_PutU32(&payload, rows[i].size);
        }
        ToriMem_Free(ToriMem_Editor(), rows);
        send_buf(session, TORIRSMAPED_FACT_LANE_CHANGES, &payload);
    }
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
