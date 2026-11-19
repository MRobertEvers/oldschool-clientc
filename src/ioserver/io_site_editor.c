/*
 * Staff: the player editor -- one player account's stats, items and varps,
 * changed from the account site (`/admin/players/<key>`).
 *
 * Compiled INTO io_site.c (included above its routes): it is that file's
 * pages and uses its page frame, buffers and staff test. Never linked alone.
 *
 * Where the truth is, and how an edit lands, depends on whether the player is
 * logged in, which the game's control link answers (io_ctl.h):
 *
 *   offline  the player account server's copy of the save is the account
 *            (playerserver/player_client.h, PlayerClient_SaveGet / SavePut).
 *            It is edited with the map editor's line-preserving codec
 *            (torirsmaped/torirs_maped_acctsave.h): one line changes, every
 *            other line -- keys this build does not know included -- comes
 *            back byte for byte. The server refuses the write while any world
 *            has the character (a world on another machine, which this site's
 *            control link does not see, included): that world's copy is the
 *            newer, and its logout would replace the edit.
 *   online   the save is stale until the next logout, and that logout
 *            overwrites it. The page shows a SNAPSHOT the game writes on
 *            request (CTL_SNAPSHOT, `<saves>/snapshots/<key>.ini`), and an
 *            edit is a cheat run as the player through the link -- what a
 *            cheat can do: a level, a varp by name, an item into the backpack
 *            (and wielded for an equipment slot), the backpack cleared.
 *            Anything else asks for the player to be logged out first
 *            (Log out: CTL_LOGOUT, then wait for the world to hand the save
 *            back to the player account server, so the edit cannot be
 *            overwritten by it).
 *
 * The page is a shell: the account as JSON, and the editor itself
 * (`/admin-art/player_editor.js` + `.css`, src/ioserver/site/) beside the
 * icon art tools/entity_viewer/ev_icons renders per cache. The deploy package
 * puts both under build-web/admin-art/.
 *
 *   GET  /admin/players/<key>          the page
 *   POST /admin/players/<key>/set      k=<what> v=<value> [n=<varp name>]
 *                                      -> {"ok","message","state"}
 *   POST /admin/players/<key>/logout   -> {"ok","message","state"}
 *   POST /admin/players/<key>/state    -> {"ok","state"} (a fresh read)
 *
 * The `k` vocabulary (one per field the page edits):
 *   stat.<id>.level   <1..99>          stat.<id>.xp   <0..200000000>
 *   item.<inv|worn|bank|container.<inv id>>.<slot>   "<obj> <count>" | ""
 *   clear.<inv|worn|bank|container.<inv id>>
 *   varp.<id>         <value> (0 removes the line)
 *   pos               "<x> <z> <level>"
 */

#include "io_ctl.h"
#include "playerserver/player_client.h"
#include "torirsmaped/torirs_maped_acctsave.h"

#ifdef _WIN32
#include <windows.h>
#define pe_sleep_ms(ms) Sleep(ms)
#else
#include <unistd.h>
#define pe_sleep_ms(ms) usleep((useconds_t)(ms) * 1000)
#endif

/* Stats a save carries (TORIRSSERVER_STAT_COUNT; 24 is Summoning). */
#define PE_STATS 25
#define PE_SLOT_MAX 4096
#define PE_LOGOUT_WAIT_MS 8000

/* A value for an attribute or a script block: quotes, backslashes, control
 * bytes and `<` escaped, so `</script>` can never close the block. */
static void
buf_json(
    struct SiteBuf* b,
    char const* text)
{
    buf_puts(b, "\"");
    for( ; *text; text++ )
    {
        unsigned char ch = (unsigned char)*text;

        if( ch == '"' || ch == '\\' )
            buf_printf(b, "\\%c", ch);
        else if( ch < 0x20 || ch == '<' || ch == '>' || ch == '&' )
            buf_printf(b, "\\u%04x", ch);
        else
        {
            char one[2] = { (char)ch, '\0' };

            buf_puts(b, one);
        }
    }
    buf_puts(b, "\"");
}

static void
pe_snapshot_path(
    char const* key,
    char* out,
    size_t cap)
{
    snprintf(out, cap, "%s/snapshots/%s.ini", ToriRSServer_SaveDir(), key);
}

/* `[section]`'s `<slot> = <obj> <count>` lines as `[[slot,obj,count],...]`. */
static void
pe_items_json(
    struct SiteBuf* b,
    struct ToriRSMapEdAcctSave const* save,
    char const* section)
{
    char k[64];
    char v[128];

    buf_puts(b, "[");
    for( int i = 0, first = 1; ToriRSMapEd_AcctSaveNth(save, section, i, k, sizeof(k), v, sizeof(v));
         i++ )
    {
        int obj = -1;
        int count = 0;

        if( sscanf(v, "%d %d", &obj, &count) != 2 || obj < 0 || count <= 0 )
            continue;
        buf_printf(b, "%s[%d,%d,%d]", first ? "" : ",", atoi(k), obj, count);
        first = 0;
    }
    buf_puts(b, "]");
}

/* The account as the page's JSON. `live` is the game's row when online; a
 * snapshot means the save read is the game's live write. */
static void
pe_state_json(
    struct SiteBuf* b,
    struct IoGate* gate,
    char const* key,
    struct ToriRSMapEdAcctSave const* save,
    int have_save,
    struct IoCtlPlayer const* live,
    enum IoCtlResult ctl,
    int snapshot)
{
    char v[256];
    char k[64];
    char owner[64] = "";
    int boosted[PE_STATS];
    long long xp_tenths[PE_STATS];

    buf_puts(b, "{\"key\":");
    buf_json(b, key);
    buf_puts(b, ",\"name\":");
    if( ToriRSMapEd_AcctSaveGet(save, "player", "name", v, sizeof(v)) && v[0] )
        buf_json(b, v);
    else
        buf_json(b, live ? live->account : key);
    buf_printf(b, ",\"save\":%s,\"snapshot\":%s,\"ctl\":\"%s\"", have_save ? "true" : "false",
               snapshot ? "true" : "false",
               ctl == IO_CTL_OK ? "ok" : ctl == IO_CTL_DOWN ? "down" : "refused");
    buf_printf(b, ",\"admin\":%s", PlayerClient_IsAdmin(key) ? "true" : "false");
    buf_puts(b, ",\"linked\":");
    if( PlayerClient_ManagedBy(key, owner, sizeof(owner)) == 1 )
        buf_json(b, owner);
    else
        buf_puts(b, "null");
    if( live )
    {
        buf_puts(b, ",\"online\":{\"as\":");
        buf_json(b, live->account);
        buf_printf(b, ",\"x\":%d,\"z\":%d,\"level\":%d}", live->x, live->z, live->level);
    }
    else
        buf_puts(b, ",\"online\":null");
    {
        int x = 0;
        int z = 0;
        int level = 0;

        if( ToriRSMapEd_AcctSaveGet(save, "player", "x", v, sizeof(v)) )
            x = atoi(v);
        if( ToriRSMapEd_AcctSaveGet(save, "player", "z", v, sizeof(v)) )
            z = atoi(v);
        if( ToriRSMapEd_AcctSaveGet(save, "player", "level", v, sizeof(v)) )
            level = atoi(v);
        buf_printf(b, ",\"pos\":[%d,%d,%d]", x, z, level);
    }

    /* A stat the save leaves out is a fresh character's: 1, and Hitpoints 10
     * at its starting experience (the server's load defaults). */
    for( int s = 0; s < PE_STATS; s++ )
    {
        boosted[s] = s == 3 ? 10 : 1;
        xp_tenths[s] = s == 3 ? 11540 : 0;
    }
    for( int i = 0; ToriRSMapEd_AcctSaveNth(save, "stats", i, k, sizeof(k), v, sizeof(v)); i++ )
    {
        int s = atoi(k);
        int boost = 0;
        long long xp = 0;

        if( s < 0 || s >= PE_STATS || sscanf(v, "%d %lld", &boost, &xp) != 2 )
            continue;
        boosted[s] = boost;
        xp_tenths[s] = xp;
    }
    buf_puts(b, ",\"stats\":[");
    for( int s = 0; s < PE_STATS; s++ )
        buf_printf(b, "%s[%d,%lld]", s ? "," : "", boosted[s], xp_tenths[s]);
    buf_puts(b, "]");

    buf_puts(b, ",\"inv\":");
    pe_items_json(b, save, "inv");
    buf_puts(b, ",\"worn\":");
    pe_items_json(b, save, "worn");
    buf_puts(b, ",\"bank\":");
    pe_items_json(b, save, "bank");

    /* Every `[container.<inv>]` the save holds, in file order. */
    buf_puts(b, ",\"containers\":[");
    for( int i = 0, first = 1; i < save->count; i++ )
    {
        char const* line = save->lines[i];
        char section[64];
        int inv;

        if( sscanf(line, "[container.%d]", &inv) != 1 )
            continue;
        snprintf(section, sizeof(section), "container.%d", inv);
        buf_printf(b, "%s{\"inv\":%d,\"items\":", first ? "" : ",", inv);
        pe_items_json(b, save, section);
        buf_puts(b, "}");
        first = 0;
    }
    buf_puts(b, "]");

    buf_puts(b, ",\"varps\":[");
    for( int i = 0, first = 1; ToriRSMapEd_AcctSaveNth(save, "varps", i, k, sizeof(k), v, sizeof(v));
         i++ )
    {
        buf_printf(b, "%s[%d,%d]", first ? "" : ",", atoi(k), atoi(v));
        first = 0;
    }
    buf_puts(b, "]}");
    (void)gate;
}

/* The player account server's copy of `key`'s save into `save`: 1 read, 0
 * none (or nothing answered: the page then shows no save to edit). */
static int
pe_load_server(
    struct ToriRSMapEdAcctSave* save,
    char const* key)
{
    char world[PLAYER_WORLD_MAX];
    char* text = NULL;

    if( PlayerClient_SaveGet(key, &text, world, sizeof(world)) != 1 )
        return 0;
    ToriRSMapEd_AcctSaveParse(save, text, strlen(text));
    ToriMem_Free(ToriMem_Net(), text);
    return 1;
}

/* `save` as the player account server's copy: NULL when written, else why
 * not, as a sentence for the page. */
static char const*
pe_store_server(
    struct ToriRSMapEdAcctSave const* save,
    char const* key,
    char* why,
    size_t why_cap)
{
    char world[PLAYER_WORLD_MAX];
    size_t len = ToriRSMapEd_AcctSaveEmit(save, NULL, 0);
    char* text = ToriMem_Alloc(ToriMem_Net(), len + 1);
    int written;

    assert(text);
    ToriRSMapEd_AcctSaveEmit(save, text, len + 1);
    written = text[0] ? PlayerClient_SavePut(key, text, world, sizeof(world)) : 0;
    ToriMem_Free(ToriMem_Net(), text);
    if( written == 1 )
        return NULL;
    if( written < 0 )
        snprintf(why, why_cap, "The player account service is not answering; nothing was saved.");
    else if( world[0] )
        snprintf(why, why_cap,
                 "%s is logged in on world %s, whose copy of the save is the newer: log them "
                 "out there first.",
                 key, world);
    else
        snprintf(why, why_cap, "The save could not be written.");
    return why;
}

/* Who is online, the row for `key`, and the account as it is now: the
 * game's snapshot when online, else the save. 1 when there is an account to
 * show. */
static int
pe_read(
    char const* who,
    char const* key,
    struct ToriRSMapEdAcctSave* save,
    struct IoCtlPlayers* players,
    struct IoCtlPlayer const** live,
    enum IoCtlResult* ctl,
    int* snapshot)
{
    struct IoCtlReply reply;
    char path[1200];

    *live = NULL;
    *snapshot = 0;
    *ctl = IoCtl_Players(who, players);
    if( *ctl == IO_CTL_OK )
        *live = IoCtl_Find(players, key);
    if( *live && IoCtl_Snapshot(who, (*live)->account, &reply) == IO_CTL_OK )
    {
        pe_snapshot_path(key, path, sizeof(path));
        *snapshot = ToriRSMapEd_AcctSaveLoad(save, path);
    }
    if( *snapshot )
        return 1;
    return pe_load_server(save, key);
}

static void
pe_json_send(
    struct SiteBuf* b,
    struct HttpResponse* res,
    int status)
{
    res->status = status;
    snprintf(res->content_type, sizeof(res->content_type), "application/json; charset=utf-8");
    res->body = b->data;
    res->body_len = (int)b->len;
    res->owns_body = 1;
}

/* `{"ok":..,"message":..,"state":{..}}`, the state read afresh. */
static void
pe_reply(
    struct IoGate* gate,
    char const* who,
    char const* key,
    struct HttpResponse* res,
    int ok,
    char const* message)
{
    struct SiteBuf b;
    struct ToriRSMapEdAcctSave save;
    struct IoCtlPlayers* players = ToriMem_Alloc(ToriMem_Net(), sizeof(*players));
    struct IoCtlPlayer const* live;
    enum IoCtlResult ctl;
    int snapshot;
    int have;

    assert(players);
    ToriRSMapEd_AcctSaveInit(&save);
    have = pe_read(who, key, &save, players, &live, &ctl, &snapshot);
    buf_init(&b);
    buf_printf(&b, "{\"ok\":%s,\"message\":", ok ? "true" : "false");
    buf_json(&b, message ? message : "");
    buf_puts(&b, ",\"state\":");
    pe_state_json(&b, gate, key, &save, have, live, ctl, snapshot);
    buf_puts(&b, "}");
    ToriRSMapEd_AcctSaveFree(&save);
    ToriMem_Free(ToriMem_Net(), players);
    pe_json_send(&b, res, 200);
}

/* An item section the vocabulary names: inv, worn, bank, container.<n>.
 * Its var section and its slot limit into the outs; 0 when not one. */
static int
pe_section(
    char const* name,
    char* out_var,
    size_t var_cap,
    int* out_slots)
{
    int inv;
    char tail;

    if( strcmp(name, "inv") == 0 || strcmp(name, "worn") == 0 || strcmp(name, "bank") == 0 )
    {
        snprintf(out_var, var_cap, "%s_var", name);
        *out_slots = name[0] == 'i' ? 28 : name[0] == 'w' ? 14 : PE_SLOT_MAX;
        return 1;
    }
    if( sscanf(name, "container.%d%c", &inv, &tail) == 1 && inv >= 0 )
    {
        snprintf(out_var, var_cap, "container_var.%d", inv);
        *out_slots = PE_SLOT_MAX;
        return 1;
    }
    return 0;
}

/* Drop the item vars of `slot` (or every slot, -1) from `[var_section]`. */
static void
pe_drop_vars(
    struct ToriRSMapEdAcctSave* save,
    char const* var_section,
    int slot)
{
    char k[64];
    char v[64];

    for( int i = 0; ToriRSMapEd_AcctSaveNth(save, var_section, i, k, sizeof(k), v, sizeof(v)); )
    {
        if( slot < 0 || atoi(k) == slot )
            ToriRSMapEd_AcctSaveRemove(save, var_section, k);
        else
            i++;
    }
}

/* An offline edit applied to `save`. NULL when it applied (the text of what
 * changed in `what`), else why not. */
static char const*
pe_apply_offline(
    struct ToriRSMapEdAcctSave* save,
    char const* k,
    char const* v,
    char* what,
    size_t what_cap)
{
    int stat;
    char field[16];
    char section[64];
    char var_section[64];
    char text[64];
    int slots;
    int slot;

    if( sscanf(k, "stat.%d.%15s", &stat, field) == 2 )
    {
        long long xp;
        int level;

        if( stat < 0 || stat >= PE_STATS )
            return "No such stat.";
        if( strcmp(field, "level") == 0 )
        {
            level = atoi(v);
            if( level < 1 || level > 99 )
                return "A level is 1 to 99.";
            xp = ToriRSMapEd_AcctXpForLevel(level);
        }
        else if( strcmp(field, "xp") == 0 )
        {
            xp = atoll(v);
            if( xp < 0 || xp > 200000000 )
                return "Experience is 0 to 200,000,000.";
            level = ToriRSMapEd_AcctLevelForXp((int)xp);
        }
        else
            return "No such stat field.";
        snprintf(section, sizeof(section), "%d", stat);
        snprintf(text, sizeof(text), "%d %lld", level, xp * 10);
        ToriRSMapEd_AcctSaveSet(save, "stats", section, text);
        snprintf(what, what_cap, "%s level %d (%lld xp)", ToriRSMapEd_AcctStatName(stat), level, xp);
        return NULL;
    }
    if( strncmp(k, "item.", 5) == 0 )
    {
        char const* dot = strrchr(k, '.');
        int obj;
        long long count;

        if( dot == k + 4 || (size_t)(dot - (k + 5)) >= sizeof(section) )
            return "No such item slot.";
        snprintf(section, sizeof(section), "%.*s", (int)(dot - (k + 5)), k + 5);
        if( !pe_section(section, var_section, sizeof(var_section), &slots) )
            return "No such container.";
        slot = atoi(dot + 1);
        if( slot < 0 || slot >= slots || !isdigit((unsigned char)dot[1]) )
            return "No such slot.";
        snprintf(text, sizeof(text), "%d", slot);
        pe_drop_vars(save, var_section, slot);
        if( !v[0] )
        {
            ToriRSMapEd_AcctSaveRemove(save, section, text);
            snprintf(what, what_cap, "%s slot %d emptied", section, slot);
            return NULL;
        }
        if( sscanf(v, "%d %lld", &obj, &count) != 2 || obj < 0 || count < 1 || count > 2147483647LL )
            return "An item is an obj id and a count of at least 1.";
        {
            char line[64];

            snprintf(line, sizeof(line), "%d %lld", obj, count);
            ToriRSMapEd_AcctSaveSet(save, section, text, line);
        }
        snprintf(what, what_cap, "%s slot %d = obj %d x %lld", section, slot, obj, count);
        return NULL;
    }
    if( strncmp(k, "clear.", 6) == 0 )
    {
        char key_text[64];
        char value[64];

        snprintf(section, sizeof(section), "%s", k + 6);
        if( !pe_section(section, var_section, sizeof(var_section), &slots) )
            return "No such container.";
        while( ToriRSMapEd_AcctSaveNth(save, section, 0, key_text, sizeof(key_text), value,
                                       sizeof(value)) )
            ToriRSMapEd_AcctSaveRemove(save, section, key_text);
        pe_drop_vars(save, var_section, -1);
        snprintf(what, what_cap, "%s emptied", section);
        return NULL;
    }
    if( strncmp(k, "varp.", 5) == 0 )
    {
        int id = atoi(k + 5);
        long long value = atoll(v);

        if( !isdigit((unsigned char)k[5]) || id < 0 || id > 65535 )
            return "No such varp.";
        if( value < -2147483648LL || value > 2147483647LL )
            return "A varp holds a 32-bit number.";
        snprintf(section, sizeof(section), "%d", id);
        if( value == 0 )
            ToriRSMapEd_AcctSaveRemove(save, "varps", section);
        else
        {
            snprintf(text, sizeof(text), "%lld", value);
            ToriRSMapEd_AcctSaveSet(save, "varps", section, text);
        }
        snprintf(what, what_cap, "varp %d = %lld", id, value);
        return NULL;
    }
    if( strcmp(k, "pos") == 0 )
    {
        int x;
        int z;
        int level;

        if( sscanf(v, "%d %d %d", &x, &z, &level) != 3 || x < 0 || x > 16383 || z < 0 ||
            z > 16383 || level < 0 || level > 3 )
            return "A position is x and z (0..16383) and a level 0 to 3.";
        snprintf(text, sizeof(text), "%d", x);
        ToriRSMapEd_AcctSaveSet(save, "player", "x", text);
        snprintf(text, sizeof(text), "%d", z);
        ToriRSMapEd_AcctSaveSet(save, "player", "z", text);
        snprintf(text, sizeof(text), "%d", level);
        ToriRSMapEd_AcctSaveSet(save, "player", "level", text);
        snprintf(what, what_cap, "position %d, %d, level %d", x, z, level);
        return NULL;
    }
    return "That cannot be edited.";
}

/* The cheat lines an online edit is, into `lines` (at most 2). 0 with
 * `*why` set when it has no online form. */
static int
pe_online_cheats(
    char const* k,
    char const* v,
    char const* varp_name,
    char lines[2][128],
    char const** why)
{
    int stat;
    char field[16];
    int slot;
    int obj;
    int count;

    *why = NULL;
    if( sscanf(k, "stat.%d.%15s", &stat, field) == 2 )
    {
        if( strcmp(field, "level") != 0 )
        {
            *why = "Online, a level can be set but exact experience cannot: log the player out "
                   "to set experience.";
            return 0;
        }
        snprintf(lines[0], 128, "setlevel %d %d", stat, atoi(v));
        return 1;
    }
    if( strncmp(k, "varp.", 5) == 0 )
    {
        if( !varp_name || !varp_name[0] )
        {
            *why = "Online, a varp is set by its name, and this one has none: log the player "
                   "out to set it.";
            return 0;
        }
        snprintf(lines[0], 128, "setvar %s %d", varp_name, atoi(v));
        return 1;
    }
    if( sscanf(k, "item.inv.%d", &slot) == 1 && v[0] && sscanf(v, "%d %d", &obj, &count) == 2 )
    {
        snprintf(lines[0], 128, "item %d %d", obj, count);
        return 1;
    }
    if( sscanf(k, "item.worn.%d", &slot) == 1 && v[0] && sscanf(v, "%d %d", &obj, &count) == 2 )
    {
        snprintf(lines[0], 128, "item %d %d", obj, count);
        snprintf(lines[1], 128, "wield %d", obj);
        return 2;
    }
    if( strcmp(k, "clear.inv") == 0 )
    {
        snprintf(lines[0], 128, "clearinv");
        return 1;
    }
    *why = "That needs the player logged out: online, only levels, named varps, items given "
           "to the backpack or worn, and an emptied backpack go through the game.";
    return 0;
}

static void
pe_handle_set(
    struct IoGate* gate,
    char const* who,
    char const* key,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    char k[96] = "";
    char v[128] = "";
    char n[96] = "";
    char what[256] = "";
    char message[1200];
    struct IoCtlPlayers* players = ToriMem_Alloc(ToriMem_Net(), sizeof(*players));
    struct IoCtlPlayer const* live = NULL;
    enum IoCtlResult ctl;
    int ok = 0;

    assert(players);
    IoGate_FormField(req, "k", k, sizeof(k));
    IoGate_FormField(req, "v", v, sizeof(v));
    IoGate_FormField(req, "n", n, sizeof(n));
    ctl = IoCtl_Players(who, players);
    if( ctl == IO_CTL_OK )
        live = IoCtl_Find(players, key);

    if( ctl == IO_CTL_REFUSED )
        snprintf(message, sizeof(message),
                 "The game server did not answer whether %s is online, so nothing was changed.",
                 key);
    else if( live )
    {
        char lines[2][128];
        char const* why;
        int count = pe_online_cheats(k, v, n, lines, &why);

        if( count == 0 )
            snprintf(message, sizeof(message), "%s", why);
        else
        {
            ok = 1;
            message[0] = '\0';
            for( int i = 0; i < count && ok; i++ )
            {
                struct IoCtlReply reply;

                if( IoCtl_Cheat(who, live->account, lines[i], &reply) != IO_CTL_OK )
                {
                    ok = 0;
                    snprintf(message, sizeof(message), "The game did not take ::%s.", lines[i]);
                    break;
                }
                ok = reply.count > 0 && strcmp(reply.lines[reply.count - 1], "cheat: ran") == 0;
                for( int l = 0; l < reply.count; l++ )
                    if( strncmp(reply.lines[l], "cheat: ", 7) != 0 )
                        snprintf(message + strlen(message), sizeof(message) - strlen(message), "%s%s",
                                 message[0] ? " " : "", reply.lines[l]);
                if( !ok && !message[0] )
                    snprintf(message, sizeof(message), "The game refused ::%s.", lines[i]);
            }
            if( ok )
            {
                fprintf(stderr, "io_server: '%s' edited online '%s': %s%s%s\n", who, key, lines[0],
                        count > 1 ? " + " : "", count > 1 ? lines[1] : "");
                if( !message[0] )
                    snprintf(message, sizeof(message), "Done, in the game: ::%s.", lines[0]);
            }
        }
    }
    else
    {
        struct ToriRSMapEdAcctSave save;
        char why[256];

        ToriRSMapEd_AcctSaveInit(&save);
        if( !pe_load_server(&save, key) )
            snprintf(message, sizeof(message),
                     "%s has no save yet: it has never logged in, so there is nothing to edit.",
                     key);
        else
        {
            char const* refused = pe_apply_offline(&save, k, v, what, sizeof(what));
            char const* unsaved = refused ? NULL : pe_store_server(&save, key, why, sizeof(why));

            if( refused )
                snprintf(message, sizeof(message), "%s", refused);
            else if( unsaved )
                snprintf(message, sizeof(message), "%s", unsaved);
            else
            {
                ok = 1;
                fprintf(stderr, "io_server: '%s' edited the save of '%s': %s\n", who, key, what);
                snprintf(message, sizeof(message), "Saved: %s.", what);
            }
        }
        ToriRSMapEd_AcctSaveFree(&save);
    }
    ToriMem_Free(ToriMem_Net(), players);
    pe_reply(gate, who, key, res, ok, message);
}

/* 1 once no world has `key` -- its save is back with the player account
 * server, where an edit lands and nothing overwrites it. */
static int
pe_save_returned(char const* key)
{
    struct PlayerAccountInfo account;

    return PlayerClient_Account(key, &account) >= 0 && !account.world[0];
}

/* Log the player out and wait until the world has handed their save back,
 * so an edit after it lands on the real copy and nothing overwrites it. */
static void
pe_handle_logout(
    struct IoGate* gate,
    char const* who,
    char const* key,
    struct HttpResponse* res)
{
    struct IoCtlPlayers* players = ToriMem_Alloc(ToriMem_Net(), sizeof(*players));
    struct IoCtlPlayer const* live = NULL;
    struct IoCtlReply reply;
    char message[256];
    int ok = 0;

    assert(players);
    if( IoCtl_Players(who, players) == IO_CTL_OK )
        live = IoCtl_Find(players, key);
    if( !live )
        snprintf(message, sizeof(message), "%s is not logged in.", key);
    else if( IoCtl_Logout(who, live->account, &reply) != IO_CTL_OK )
        snprintf(message, sizeof(message), "The game did not take the logout.");
    else
    {
        snprintf(message, sizeof(message),
                 "Logged out, but the world had not handed the save back after %d seconds: "
                 "refresh before editing.",
                 PE_LOGOUT_WAIT_MS / 1000);
        fprintf(stderr, "io_server: '%s' logged out '%s'\n", who, key);
        /* The world hands the save back from its teardown, after the player
         * leaves the list, on a pool thread: the player gone AND the player
         * account server's note cleared, or an edit could be overwritten. */
        for( int waited = 0; waited < PE_LOGOUT_WAIT_MS; waited += 200 )
        {
            int gone;

            pe_sleep_ms(200);
            gone = IoCtl_Players(who, players) == IO_CTL_OK && !(live = IoCtl_Find(players, key));
            /* A client that did not leave when told: ask again, which the
             * game answers by ending the session. */
            if( !gone && waited == 3000 && live )
                IoCtl_Logout(who, live->account, &reply);
            if( gone && pe_save_returned(key) )
            {
                ok = 1;
                snprintf(message, sizeof(message), "%s is logged out; the save is theirs to edit.",
                         key);
                break;
            }
        }
    }
    ToriMem_Free(ToriMem_Net(), players);
    pe_reply(gate, who, key, res, ok, message);
}

static void
page_player_editor(
    struct IoGate* gate,
    char const* who,
    char const* key,
    struct HttpResponse* res)
{
    struct SiteBuf b;
    struct ToriRSMapEdAcctSave save;
    struct IoCtlPlayers* players = ToriMem_Alloc(ToriMem_Net(), sizeof(*players));
    struct IoCtlPlayer const* live;
    enum IoCtlResult ctl;
    int snapshot;
    int have;

    assert(players);
    ToriRSMapEd_AcctSaveInit(&save);
    have = pe_read(who, key, &save, players, &live, &ctl, &snapshot);
    page_begin(&b, gate, who, key, "/admin/players");
    buf_puts(&b, "<link rel=\"stylesheet\" href=\"/admin-art/player_editor.css\">"
                 "<div id=\"pe\" class=\"pe\"><section class=\"panel\"><h2>Loading the editor"
                 "&hellip;</h2><p class=\"note\" id=\"pe-missing\" hidden>The editor's files "
                 "(<code>/admin-art/</code>) are not in this build: the deploy package makes "
                 "them (tools/entity_viewer/ev_icons).</p></section></div>"
                 "<script id=\"pe-state\" type=\"application/json\">");
    pe_state_json(&b, gate, key, &save, have, live, ctl, snapshot);
    buf_puts(&b, "</script><script src=\"/admin-art/player_editor.js\" onerror=\""
                 "document.getElementById('pe-missing').hidden=false\"></script>");
    ToriRSMapEd_AcctSaveFree(&save);
    ToriMem_Free(ToriMem_Net(), players);
    page_send(&b, res, 200);
}

/* `/admin/players/<key>[/<action>]`: 1 when it was one of the editor's. */
static int
pe_route(
    struct IoGate* gate,
    char const* who,
    struct HttpRequest const* req,
    struct HttpResponse* res,
    int is_get,
    int is_post)
{
    char const* rest;
    char key[SITE_NAME_MAX];
    char const* action;
    size_t len;

    if( strncmp(req->path, "/admin/players/", 15) != 0 )
        return 0;
    rest = req->path + 15;
    len = strcspn(rest, "/?#");
    if( len == 0 || len >= sizeof(key) )
        return 0;
    snprintf(key, sizeof(key), "%.*s", (int)len, rest);
    /* /admin/players/link and /unlink are the list page's forms. */
    if( strcmp(key, "link") == 0 || strcmp(key, "unlink") == 0 )
        return 0;
    if( !recording_account_ok(key) )
    {
        res->status = 404;
        return 1;
    }
    action = rest[len] == '/' ? rest + len + 1 : "";
    if( is_get && !action[0] )
        page_player_editor(gate, who, key, res);
    else if( is_post && IoGate_PathIs(action, "set") )
        pe_handle_set(gate, who, key, req, res);
    else if( is_post && IoGate_PathIs(action, "logout") )
        pe_handle_logout(gate, who, key, res);
    else if( is_post && IoGate_PathIs(action, "state") )
        pe_reply(gate, who, key, res, 1, "");
    else
        res->status = 405;
    return 1;
}
