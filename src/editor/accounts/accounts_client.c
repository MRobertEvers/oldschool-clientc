/*
 * The accounts client model (accounts_client.h): decoders for the four facts,
 * encoders for the nine requests, and the transport helpers. Pure: no socket,
 * no chrome, no App.
 */

#include "accounts_client.h"

#include "editor/editor_host.h"
#include "torirsmaped/torirs_maped.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

void
Editor_AccountsModelInit(struct Editor_AccountsModel* model)
{
    assert(model);
    memset(model, 0, sizeof(*model));
    model->list_status = EDITOR_HOST_UNSUPPORTED;
    model->view.status = EDITOR_HOST_ABSENT;
}

int
Editor_AccountsIsFact(uint32_t type)
{
    return type >= TORIRSMAPED_FACT_ACCOUNTS_RANGE_FIRST && type <= TORIRSMAPED_FACT_ACCOUNTS_RANGE_LAST;
}

static void
model_log(
    struct Editor_AccountsModel* model,
    const char* text)
{
    snprintf(model->log[model->log_head], EDITOR_ACCOUNTS_LOG_LINE_MAX, "%s", text);
    model->log_head = (model->log_head + 1) % EDITOR_ACCOUNTS_LOG_LINES;
    if( model->log_count < EDITOR_ACCOUNTS_LOG_LINES )
        model->log_count++;
    model->log_serial++;
}

void
Editor_AccountsModelNote(
    struct Editor_AccountsModel* model,
    const char* text)
{
    assert(model);
    assert(text);
    model_log(model, text);
}

const char*
Editor_AccountsLogLine(
    const struct Editor_AccountsModel* model,
    int index)
{
    assert(model);
    assert(index >= 0);
    if( index >= model->log_count )
        return NULL;
    return model->log[(model->log_head - 1 - index + 2 * EDITOR_ACCOUNTS_LOG_LINES) % EDITOR_ACCOUNTS_LOG_LINES];
}

static int
apply_list(
    struct Editor_AccountsModel* model,
    struct ToriRSMapEdReader* reader)
{
    uint32_t count;
    struct Editor_AccountRow rows[EDITOR_ACCOUNTS_MAX];
    int kept = 0;
    uint32_t status = ToriRSMapEd_GetU32(reader);
    char live[1024];
    char play[64];
    uint32_t attached;

    ToriRSMapEd_GetString(reader, live, sizeof(live));
    ToriRSMapEd_GetString(reader, play, sizeof(play));
    attached = ToriRSMapEd_GetU32(reader);
    count = ToriRSMapEd_GetU32(reader);
    if( reader->bad )
        return 0;
    for( uint32_t i = 0; i < count && !reader->bad; i++ )
    {
        struct Editor_AccountRow row;
        memset(&row, 0, sizeof(row));
        ToriRSMapEd_GetString(reader, row.name, sizeof(row.name));
        row.flags = ToriRSMapEd_GetU32(reader);
        row.x = (int32_t)ToriRSMapEd_GetU32(reader);
        row.z = (int32_t)ToriRSMapEd_GetU32(reader);
        row.level = (int32_t)ToriRSMapEd_GetU32(reader);
        row.queued = ToriRSMapEd_GetU32(reader);
        if( kept < EDITOR_ACCOUNTS_MAX )
            rows[kept++] = row;
    }
    if( !ToriRSMapEd_ReaderDone(reader) )
        return 0;
    model->list_status = status;
    snprintf(model->live_dir, sizeof(model->live_dir), "%s", live);
    if( strcmp(model->play_account, play) != 0 )
        model->play_look_known = 0;
    snprintf(model->play_account, sizeof(model->play_account), "%s", play);
    model->attached = attached != 0;
    memcpy(model->rows, rows, (size_t)kept * sizeof(rows[0]));
    model->row_count = kept;
    model->list_serial++;
    return 1;
}

static int
apply_view(
    struct Editor_AccountsModel* model,
    const char* follow,
    struct ToriRSMapEdReader* reader)
{
    static struct Editor_AccountView view;
    uint32_t n;

    memset(&view, 0, sizeof(view));
    view.status = ToriRSMapEd_GetU32(reader);
    ToriRSMapEd_GetString(reader, view.name, sizeof(view.name));
    if( reader->bad )
        return 0;
    if( view.status == EDITOR_HOST_OK )
    {
        view.flags = ToriRSMapEd_GetU32(reader);
        ToriRSMapEd_GetString(reader, view.status_text, sizeof(view.status_text));
        ToriRSMapEd_GetString(reader, view.display, sizeof(view.display));
        view.x = (int32_t)ToriRSMapEd_GetU32(reader);
        view.z = (int32_t)ToriRSMapEd_GetU32(reader);
        view.level = (int32_t)ToriRSMapEd_GetU32(reader);
        n = ToriRSMapEd_GetU32(reader);
        if( reader->bad || n != TORIRSMAPED_ACCT_STATS )
            return 0;
        for( uint32_t i = 0; i < n; i++ )
        {
            view.boosted[i] = (int32_t)ToriRSMapEd_GetU32(reader);
            view.xp_tenths[i] = (int32_t)ToriRSMapEd_GetU32(reader);
        }
        n = ToriRSMapEd_GetU32(reader);
        for( uint32_t i = 0; i < n && !reader->bad; i++ )
        {
            struct Editor_AccountVarp varp;
            varp.id = ToriRSMapEd_GetU32(reader);
            varp.value = (int32_t)ToriRSMapEd_GetU32(reader);
            ToriRSMapEd_GetString(reader, varp.name, sizeof(varp.name));
            if( view.varp_count < EDITOR_ACCOUNTS_VARPS_MAX )
                view.varps[view.varp_count++] = varp;
        }
        for( int section = 0; section < 3 && !reader->bad; section++ )
        {
            n = ToriRSMapEd_GetU32(reader);
            for( uint32_t i = 0; i < n && !reader->bad; i++ )
            {
                struct Editor_AccountItem item;
                item.section = section;
                item.slot = ToriRSMapEd_GetU32(reader);
                item.obj = (int32_t)ToriRSMapEd_GetU32(reader);
                item.count = (int32_t)ToriRSMapEd_GetU32(reader);
                if( view.item_count < EDITOR_ACCOUNTS_ITEMS_MAX )
                    view.items[view.item_count++] = item;
            }
        }
        n = ToriRSMapEd_GetU32(reader);
        for( uint32_t i = 0; i < n && !reader->bad; i++ )
        {
            char line[200];
            ToriRSMapEd_GetString(reader, line, sizeof(line));
            if( view.queue_count < EDITOR_ACCOUNTS_QUEUE_MAX )
                snprintf(view.queue[view.queue_count++], sizeof(view.queue[0]), "%s", line);
        }
        n = ToriRSMapEd_GetU32(reader);
        for( uint32_t i = 0; i < n && !reader->bad; i++ )
        {
            struct Editor_AccountQuestState quest;
            ToriRSMapEd_GetString(reader, quest.dbrow, sizeof(quest.dbrow));
            quest.state = ToriRSMapEd_GetU32(reader);
            quest.value = (int32_t)ToriRSMapEd_GetU32(reader);
            if( view.quest_count < EDITOR_ACCOUNTS_QUESTS_MAX )
                view.quests[view.quest_count++] = quest;
        }
        /* The look and the other containers: a server older than them
         * ends the fact here. */
        if( !reader->bad && reader->at < reader->length )
        {
            view.has_look = 1;
            snprintf(view.look.name, sizeof(view.look.name), "%s", view.name);
            view.look.gender = (int)ToriRSMapEd_GetU32(reader);
            n = ToriRSMapEd_GetU32(reader);
            if( reader->bad || n != TORIRSMAPED_ACCT_LOOK_SLOTS )
                return 0;
            for( uint32_t i = 0; i < n; i++ )
                view.look.kits[i] = (int32_t)ToriRSMapEd_GetU32(reader);
            n = ToriRSMapEd_GetU32(reader);
            if( reader->bad || n != TORIRSMAPED_ACCT_LOOK_COLOURS )
                return 0;
            for( uint32_t i = 0; i < n; i++ )
                view.look.colours[i] = (int32_t)ToriRSMapEd_GetU32(reader);
            n = ToriRSMapEd_GetU32(reader);
            for( uint32_t i = 0; i < n && !reader->bad; i++ )
            {
                struct Editor_AccountContainer container;
                uint32_t items;
                container.inv = ToriRSMapEd_GetU32(reader);
                ToriRSMapEd_GetString(reader, container.name, sizeof(container.name));
                container.size = ToriRSMapEd_GetU32(reader);
                items = ToriRSMapEd_GetU32(reader);
                for( uint32_t k = 0; k < items && !reader->bad; k++ )
                {
                    struct Editor_AccountItem item;
                    item.section = EDITOR_ACCOUNTS_SECTION_CONTAINER + view.container_count;
                    item.slot = ToriRSMapEd_GetU32(reader);
                    item.obj = (int32_t)ToriRSMapEd_GetU32(reader);
                    item.count = (int32_t)ToriRSMapEd_GetU32(reader);
                    if( view.container_count < TORIRSMAPED_ACCT_CONTAINERS_MAX
                        && view.item_count < EDITOR_ACCOUNTS_ITEMS_MAX )
                        view.items[view.item_count++] = item;
                }
                if( view.container_count < TORIRSMAPED_ACCT_CONTAINERS_MAX )
                    view.containers[view.container_count++] = container;
            }
        }
        for( int i = 0; i < TORIRSMAPED_ACCT_WORN_SLOTS; i++ )
            view.look.worn[i] = -1;
        for( int i = 0; i < view.item_count; i++ )
            if( view.items[i].section == EDITOR_ACCOUNTS_SECTION_WORN && view.items[i].slot < TORIRSMAPED_ACCT_WORN_SLOTS )
                view.look.worn[view.items[i].slot] = view.items[i].obj;
    }
    if( !ToriRSMapEd_ReaderDone(reader) )
        return 0;
    /* The play account's look, whichever account this window follows. */
    if( view.status == EDITOR_HOST_OK && view.has_look && model->play_account[0]
        && strcmp(view.name, model->play_account) == 0 )
    {
        model->play_look = view.look;
        model->play_look_known = 1;
        model->play_look_serial++;
    }
    if( follow && follow[0] && strcmp(follow, view.name) != 0 )
    {
        char line[EDITOR_ACCOUNTS_LOG_LINE_MAX];
        snprintf(line, sizeof(line), "%s changed (another window's account)", view.name);
        model_log(model, line);
        return 1;
    }
    model->view = view;
    model->view_serial++;
    return 1;
}

static int
apply_quests(
    struct Editor_AccountsModel* model,
    struct ToriRSMapEdReader* reader)
{
    static struct Editor_AccountQuest quests[EDITOR_ACCOUNTS_QUESTS_MAX];
    int kept = 0;
    uint32_t count = ToriRSMapEd_GetU32(reader);

    for( uint32_t i = 0; i < count && !reader->bad; i++ )
    {
        struct Editor_AccountQuest quest;
        ToriRSMapEd_GetString(reader, quest.dbrow, sizeof(quest.dbrow));
        ToriRSMapEd_GetString(reader, quest.display, sizeof(quest.display));
        ToriRSMapEd_GetString(reader, quest.var, sizeof(quest.var));
        quest.complete_value = (int32_t)ToriRSMapEd_GetU32(reader);
        if( kept < EDITOR_ACCOUNTS_QUESTS_MAX )
            quests[kept++] = quest;
    }
    if( !ToriRSMapEd_ReaderDone(reader) )
        return 0;
    memcpy(model->quests, quests, (size_t)kept * sizeof(quests[0]));
    model->quest_count = kept;
    model->quests_serial++;
    return 1;
}

static int
apply_loadouts(
    struct Editor_AccountsModel* model,
    struct ToriRSMapEdReader* reader)
{
    static struct Editor_AccountLoadout loadouts[TORIRSMAPED_ACCT_LOADOUTS_MAX];
    uint32_t status = ToriRSMapEd_GetU32(reader);
    uint32_t count = ToriRSMapEd_GetU32(reader);
    int kept = 0;

    for( uint32_t i = 0; i < count && !reader->bad; i++ )
    {
        struct Editor_AccountLoadout row;
        uint32_t items;
        memset(&row, 0, sizeof(row));
        ToriRSMapEd_GetString(reader, row.name, sizeof(row.name));
        items = ToriRSMapEd_GetU32(reader);
        for( uint32_t k = 0; k < items && !reader->bad; k++ )
        {
            struct Editor_AccountItem item;
            item.section = (int)ToriRSMapEd_GetU32(reader);
            item.slot = ToriRSMapEd_GetU32(reader);
            item.obj = (int32_t)ToriRSMapEd_GetU32(reader);
            item.count = (int32_t)ToriRSMapEd_GetU32(reader);
            if( row.item_count < (int)(sizeof(row.items) / sizeof(row.items[0])) )
                row.items[row.item_count++] = item;
        }
        if( kept < TORIRSMAPED_ACCT_LOADOUTS_MAX )
            loadouts[kept++] = row;
    }
    if( !ToriRSMapEd_ReaderDone(reader) )
        return 0;
    model->loadouts_status = status;
    memcpy(model->loadouts, loadouts, (size_t)kept * sizeof(loadouts[0]));
    model->loadout_count = kept;
    model->loadouts_serial++;
    return 1;
}

static int
apply_invs(
    struct Editor_AccountsModel* model,
    struct ToriRSMapEdReader* reader)
{
    uint32_t count = ToriRSMapEd_GetU32(reader);
    int kept = 0;

    for( uint32_t i = 0; i < count && !reader->bad; i++ )
    {
        struct Editor_AccountInv inv;
        inv.id = ToriRSMapEd_GetU32(reader);
        ToriRSMapEd_GetString(reader, inv.name, sizeof(inv.name));
        inv.size = ToriRSMapEd_GetU32(reader);
        if( kept < EDITOR_ACCOUNTS_INVS_MAX )
            model->invs[kept++] = inv;
    }
    if( !ToriRSMapEd_ReaderDone(reader) )
        return 0;
    model->inv_count = kept;
    model->invs_serial++;
    return 1;
}

static int
apply_status(
    struct Editor_AccountsModel* model,
    struct ToriRSMapEdReader* reader)
{
    uint32_t request = ToriRSMapEd_GetU32(reader);
    uint32_t status = ToriRSMapEd_GetU32(reader);
    char account[64];
    char message[1024];
    char line[EDITOR_ACCOUNTS_LOG_LINE_MAX];

    ToriRSMapEd_GetString(reader, account, sizeof(account));
    ToriRSMapEd_GetString(reader, message, sizeof(message));
    if( !ToriRSMapEd_ReaderDone(reader) )
        return 0;
    model->last_request = request;
    model->last_status = status;
    snprintf(model->last_account, sizeof(model->last_account), "%s", account);
    snprintf(model->last_message, sizeof(model->last_message), "%s", message);
    model->status_serial++;
    snprintf(line, sizeof(line), "%s%s%s%s", account, account[0] ? ": " : "", message,
        status == EDITOR_HOST_OK ? "" : " (refused)");
    model_log(model, line);
    return 1;
}

int
Editor_AccountsModelApply(
    struct Editor_AccountsModel* model,
    const char* follow,
    uint32_t type,
    const uint8_t* body,
    uint32_t length)
{
    struct ToriRSMapEdReader reader;
    int ok;

    assert(model);
    assert(body || length == 0);
    if( !Editor_AccountsIsFact(type) )
        return 0;
    ToriRSMapEd_ReaderInit(&reader, body, length);
    if( type == TORIRSMAPED_FACT_ACCOUNTS )
        ok = apply_list(model, &reader);
    else if( type == TORIRSMAPED_FACT_ACCOUNT )
        ok = apply_view(model, follow, &reader);
    else if( type == TORIRSMAPED_FACT_QUESTS )
        ok = apply_quests(model, &reader);
    else if( type == TORIRSMAPED_FACT_ACCOUNT_STATUS )
        ok = apply_status(model, &reader);
    else if( type == TORIRSMAPED_FACT_LOADOUTS )
        ok = apply_loadouts(model, &reader);
    else if( type == TORIRSMAPED_FACT_INVS )
        ok = apply_invs(model, &reader);
    else
        ok = 1; /* a fact a newer server added: nothing to show */
    if( !ok )
    {
        char line[EDITOR_ACCOUNTS_LOG_LINE_MAX];
        snprintf(line, sizeof(line), "accounts: malformed fact %u (%u bytes) dropped", type, length);
        model_log(model, line);
        return -1;
    }
    return 1;
}

const struct Editor_AccountRow*
Editor_AccountsRow(
    const struct Editor_AccountsModel* model,
    const char* name)
{
    assert(model);
    assert(name);
    for( int i = 0; i < model->row_count; i++ )
        if( strcmp(model->rows[i].name, name) == 0 )
            return &model->rows[i];
    return NULL;
}

const struct Editor_AccountQuest*
Editor_AccountsQuest(
    const struct Editor_AccountsModel* model,
    const char* dbrow)
{
    assert(model);
    assert(dbrow);
    for( int i = 0; i < model->quest_count; i++ )
        if( strcmp(model->quests[i].dbrow, dbrow) == 0 )
            return &model->quests[i];
    return NULL;
}

const struct Editor_AccountItem*
Editor_AccountsItem(
    const struct Editor_AccountView* view,
    int section,
    uint32_t slot)
{
    assert(view);
    for( int i = 0; i < view->item_count; i++ )
        if( view->items[i].section == section && view->items[i].slot == slot )
            return &view->items[i];
    return NULL;
}

int
Editor_AccountsVarp(
    const struct Editor_AccountView* view,
    uint32_t id,
    int32_t* out_value)
{
    assert(view);
    assert(out_value);
    for( int i = 0; i < view->varp_count; i++ )
        if( view->varps[i].id == id )
        {
            *out_value = view->varps[i].value;
            return 1;
        }
    return 0;
}

/* ---- requests --------------------------------------------------------------- */

uint32_t
Editor_AccountsEncodeList(struct ToriRSMapEdBuf* out)
{
    assert(out);
    (void)out;
    return TORIRSMAPED_REQ_ACCOUNTS_LIST;
}

uint32_t
Editor_AccountsEncodeQuests(struct ToriRSMapEdBuf* out)
{
    assert(out);
    (void)out;
    return TORIRSMAPED_REQ_ACCOUNTS_QUESTS;
}

uint32_t
Editor_AccountsEncodeLoadouts(struct ToriRSMapEdBuf* out)
{
    assert(out);
    (void)out;
    return TORIRSMAPED_REQ_LOADOUTS_LIST;
}

uint32_t
Editor_AccountsEncodeInvs(struct ToriRSMapEdBuf* out)
{
    assert(out);
    (void)out;
    return TORIRSMAPED_REQ_ACCOUNTS_INVS;
}

uint32_t
Editor_AccountsEncodeLoadoutSave(
    struct ToriRSMapEdBuf* out,
    const char* loadout,
    const char* account)
{
    assert(out);
    assert(loadout);
    assert(account);
    ToriRSMapEd_PutString(out, loadout);
    ToriRSMapEd_PutString(out, account);
    return TORIRSMAPED_REQ_LOADOUT_SAVE;
}

uint32_t
Editor_AccountsEncodeLoadoutDelete(
    struct ToriRSMapEdBuf* out,
    const char* loadout)
{
    assert(out);
    assert(loadout);
    ToriRSMapEd_PutString(out, loadout);
    return TORIRSMAPED_REQ_LOADOUT_DELETE;
}

const struct Editor_AccountLoadout*
Editor_AccountsLoadout(
    const struct Editor_AccountsModel* model,
    const char* name)
{
    assert(model);
    assert(name);
    for( int i = 0; i < model->loadout_count; i++ )
        if( strcmp(model->loadouts[i].name, name) == 0 )
            return &model->loadouts[i];
    return NULL;
}

const struct Editor_AccountInv*
Editor_AccountsInv(
    const struct Editor_AccountsModel* model,
    uint32_t id)
{
    assert(model);
    for( int i = 0; i < model->inv_count; i++ )
        if( model->invs[i].id == id )
            return &model->invs[i];
    return NULL;
}

uint32_t
Editor_AccountsEncodeView(
    struct ToriRSMapEdBuf* out,
    const char* name)
{
    assert(out);
    assert(name);
    ToriRSMapEd_PutString(out, name);
    return TORIRSMAPED_REQ_ACCOUNT_VIEW;
}

uint32_t
Editor_AccountsEncodeCreate(
    struct ToriRSMapEdBuf* out,
    const char* name,
    const char* password,
    const char* fixture)
{
    assert(out);
    assert(name);
    assert(password);
    assert(fixture);
    ToriRSMapEd_PutString(out, name);
    ToriRSMapEd_PutString(out, password);
    ToriRSMapEd_PutString(out, fixture);
    return TORIRSMAPED_REQ_ACCOUNT_CREATE;
}

uint32_t
Editor_AccountsEncodeDuplicate(
    struct ToriRSMapEdBuf* out,
    const char* name,
    const char* new_name,
    const char* password)
{
    assert(out);
    assert(name);
    assert(new_name);
    assert(password);
    ToriRSMapEd_PutString(out, name);
    ToriRSMapEd_PutString(out, new_name);
    ToriRSMapEd_PutString(out, password);
    return TORIRSMAPED_REQ_ACCOUNT_DUPLICATE;
}

uint32_t
Editor_AccountsEncodeDelete(
    struct ToriRSMapEdBuf* out,
    const char* name)
{
    assert(out);
    assert(name);
    ToriRSMapEd_PutString(out, name);
    return TORIRSMAPED_REQ_ACCOUNT_DELETE;
}

uint32_t
Editor_AccountsEncodeUse(
    struct ToriRSMapEdBuf* out,
    const char* name)
{
    assert(out);
    assert(name);
    ToriRSMapEd_PutString(out, name);
    return TORIRSMAPED_REQ_ACCOUNT_USE;
}

uint32_t
Editor_AccountsEncodeSet(
    struct ToriRSMapEdBuf* out,
    const char* name,
    const char* key,
    const char* value)
{
    assert(out);
    assert(name);
    assert(key);
    assert(value);
    ToriRSMapEd_PutString(out, name);
    ToriRSMapEd_PutString(out, key);
    ToriRSMapEd_PutString(out, value);
    return TORIRSMAPED_REQ_ACCOUNT_SET;
}

uint32_t
Editor_AccountsEncodePlace(
    struct ToriRSMapEdBuf* out,
    const char* name,
    int x,
    int z,
    int level)
{
    assert(out);
    assert(name);
    ToriRSMapEd_PutString(out, name);
    ToriRSMapEd_PutU32(out, (uint32_t)x);
    ToriRSMapEd_PutU32(out, (uint32_t)z);
    ToriRSMapEd_PutU32(out, (uint32_t)level);
    return TORIRSMAPED_REQ_ACCOUNT_PLACE;
}

/* ---- the transport -------------------------------------------------------------- */

int
Editor_AccountsSend(
    const struct Editor_AccountsTransport* transport,
    uint32_t type,
    struct ToriRSMapEdBuf* payload)
{
    int sent;

    assert(transport);
    assert(transport->send);
    assert(payload);
    sent = transport->send(transport->user, type, payload->data ? ToriRSMapEd_BufPeek(payload) : NULL,
        (uint32_t)ToriRSMapEd_BufAvailable(payload));
    ToriRSMapEd_BufFree(payload);
    memset(payload, 0, sizeof(*payload));
    return sent;
}

int
Editor_AccountsDrain(
    const struct Editor_AccountsTransport* transport,
    struct Editor_AccountsModel* model,
    const char* follow)
{
    uint32_t type;
    uint8_t* body;
    uint32_t length;
    int applied = 0;

    assert(transport);
    assert(transport->take);
    assert(model);
    while( transport->take(transport->user, &type, &body, &length) )
    {
        if( Editor_AccountsModelApply(model, follow, type, body, length) > 0 )
            applied++;
        if( transport->release )
            transport->release(transport->user, body);
    }
    return applied;
}

void
Editor_AccountsRowLine(
    const struct Editor_AccountRow* row,
    char* out,
    size_t out_size)
{
    assert(row);
    assert(out);
    if( row->flags & TORIRSMAPED_ACCT_ONLINE )
        snprintf(out, out_size, "online at %d,%d level %d", row->x, row->z, row->level);
    else if( row->x >= 0 )
        snprintf(out, out_size, "offline at %d,%d level %d", row->x, row->z, row->level);
    else
        snprintf(out, out_size, "offline");
}
