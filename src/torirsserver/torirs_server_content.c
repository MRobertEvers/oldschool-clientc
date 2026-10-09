/*
 * The LostCity content tree, read at boot.
 *
 * Three grammars, all of them LostCity's:
 *
 *   .pack   `id=name`, one line each, one file per namespace
 *   configs `[symbol]` sections of `key=value`, with `param=<name>,<value>`
 *   .spawn  `==== NPC ====` / `==== OBJ ====` banners over `name x z level` rows
 *
 * The map squares (`maps/m<x>_<z>.jm2`) are not read here: they are the cache's
 * terrain, and a spawn is never in one (cachepack refuses a square that has
 * one; see map_read in cp_decode.c).
 *
 * Nothing here is clever. The value of matching the reference's syntax exactly
 * is that a LostCity config can be pasted in and a config written here can be
 * pasted back, so the two content trees stay one skill rather than two.
 *
 * Load order matters and is documented on ToriRSServer_ContentLoad: bonuses are
 * seeded from the cache params that ToriRSServer_ObjInfo / ToriRSServer_NpcInfo decoded,
 * and a config block overlays that.
 */

#include "torirs_server_content.h"
#include <assert.h>

#include "content/content_fields.h"
#include "content/content_name_index.h"
#include "content/content_register.h"
#include "content/content_value.h"
#include "torirs_server.h"
#include "torirs_server_paramtable.h"
#include "torirs_server_db.h"
/* A `.loc` block's `opN=` is pushed straight to the scene as it is parsed —
 * see the note beside `struct ToriRSServerLocDef`. */
#include "torirs_server_scene.h"
#include "torirs_server_servercodec.h"
#include "torirs_server_servpack.h"
#include "torirs_server_shop.h"

#include <rscache.h>
#include "rscache_valuetype.h"

#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* dirent's d_type is a BSD/Linux extension MinGW's dirent lacks, so classify by
 * path with stat() instead -- portable across the unix and win32 builds. */
static int
ToriRSServer_PathIsDir(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0 && (st.st_mode & S_IFDIR) != 0;
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

static int g_errors;

/* The npc and loc field registers (content/content_fields.h), loaded once per
 * content load: each band field's opcode and width, and what a stated field
 * means beyond its member (`text = param`, `param = <name>`). */
static struct RSCache_Register g_npc_fields;
static struct RSCache_Register g_loc_fields;
static struct RSCache_Register g_obj_fields;
static struct RSCache_Register g_varp_fields;
static struct RSCache_Register g_inv_fields;
static struct RSCache_Register g_dbtable_fields;
/* Problems between a register and the server's band bindings, found at content
 * load (ToriRSServer_ServerCheck). Non-zero refuses the pack: it is a startup
 * error, already counted in g_errors. */
static int g_band_register_problems;

/*
 * Every rejection prints and counts. A content tree that half-loads is the
 * worst outcome available: the server runs, the fight is subtly wrong, and
 * nothing in the log says which line stopped meaning anything. `ToriRSServer_Pack`
 * turns a non-zero count into a non-zero exit status.
 */
#define CONTENT_ERROR(...)                                                                         \
    do                                                                                             \
    {                                                                                              \
        fprintf(stderr, "torirsserver: content: " __VA_ARGS__);                                         \
        g_errors++;                                                                                \
    } while( 0 )

int
ToriRSServer_ContentErrorCount(void)
{
    return g_errors;
}

void
ToriRSServer_ContentReportError(
    const char* fmt,
    ...)
{
    va_list args;

    /* Same prefix and the same counter as CONTENT_ERROR, so a bad line in a
     * `.dbtable` read by torirs_server_db.c is indistinguishable from a bad line in a
     * `.npc` read here — which is the point. A second reader with its own error
     * channel is a reader whose failures do not stop the server. */
    fprintf(stderr, "torirsserver: content: ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    g_errors++;
}

/* ------------------------------------------------------------------ */
/* Text helpers                                                        */
/* ------------------------------------------------------------------ */

/** Strip a `//` comment and surrounding whitespace, in place. */
char*
ToriRSServer_ContentCleanLine(char* line)
{
    char* comment = strstr(line, "//");
    size_t length;

    if( comment )
        *comment = '\0';
    while( *line == ' ' || *line == '\t' )
        line++;
    length = strlen(line);
    while( length && (unsigned char)line[length - 1] <= ' ' )
        line[--length] = '\0';
    return line;
}

/** Split `key=value` in place. Returns the value, or NULL when there is no `=`. */
char*
ToriRSServer_ContentSplitKeyValue(char* line)
{
    char* equals = strchr(line, '=');

    if( !equals )
        return NULL;
    *equals = '\0';
    /* Trim the key's trailing space so `hitpoints = 5` reads the same as
     * `hitpoints=5`. LostCity's own files never put a space there, but a config
     * that silently ignores the key it cannot parse is a bad neighbour. */
    for( size_t i = strlen(line); i > 0 && (unsigned char)line[i - 1] <= ' '; i-- )
        line[i - 1] = '\0';
    equals++;
    while( *equals == ' ' || *equals == '\t' )
        equals++;
    return equals;
}

/** `[name]` section header; returns the name in place, or NULL. */
char*
ToriRSServer_ContentSectionHeader(char* line)
{
    size_t length = strlen(line);

    if( length < 2 || line[0] != '[' || line[length - 1] != ']' )
        return NULL;
    line[length - 1] = '\0';
    return line + 1;
}

/* ------------------------------------------------------------------ */
/* Packs                                                               */
/* ------------------------------------------------------------------ */

/*
 * One file per namespace: `pack/<ns>.pack`, `id=name` per line.
 *
 * There used to be two — `pack/` for what the cache's gameval table said and
 * `names/` for what a human wrote — and the reason was never that a namespace has
 * two name domains. It was that a pack *save* emitted nothing but `id=name` lines,
 * so any tool regenerating `configs/all.varp.compack` deleted every comment in it and every
 * line it had not generated. `configs/all.param.compack` lost all 58 of its header lines to
 * one `cachepack unpack`, and that header was the record of which param-id claims
 * had been checked against a cache and how.
 *
 * That hazard is fixed at the writer rather than worked around by the directory
 * layout (docs/CONTENT_PACK_PLAN.md §1): a pack save preserves comments and
 * *merges*, so one file can hold the cache's names and this world's. Where the two
 * disagree about an id, the authored name is the line and the cache's is a
 * trailing note:
 *
 *     <id>=<authored name>  // cache: <gameval name>
 *
 * The one thing a single file cannot hold is an *alias* — a second name for an id
 * the cache already names — because a line binds one name to one id. That is a
 * deliberate loss, decided per case during the migration; no alias in this tree
 * needed keeping, and `validate_symbols` refuses a file that tries.
 *
 * Which makes an override a *rename*, not an addition, and worth spending only
 * where the cache's name is wrong for what the id now holds. No file in this
 * tree spends it: there is not one `// cache:` note left. There were eleven, all
 * in `configs/all.varp.compack`, and none of them earned it — eight varps
 * relabelled after a varbit they merely carry (`843=varp_weapon_category`,
 * `867=bank_tab_a`, …) plus three holding this server's scratch counters. Each
 * cost the cache's own spelling: `randomhitsound` and `prayer23` resolved to
 * nothing while the rename stood. One of them cost more — varp 1 put a
 * whole-varp counter on top of twelve Dwarf Cannon varbits. `--selftest` now
 * pins the eight cache names and the three scratch ids.
 */
struct PackEntry
{
    char* name;
    int id;
    /*
     * 1 when this name came from `pack/<ns>.alloc` rather than from the
     * cache's own `configs/all.<ns>.compack`.
     *
     * The two files are one namespace on purpose -- a lookup is one pass and a
     * rename across them is caught like a duplicate inside one -- so nothing
     * downstream could tell a MINTED record from a cache one afterwards. It
     * has to, in exactly one place: a `.obj` block for a record content
     * invented is the whole record and legitimately states the cache-side keys
     * (`name`, `model`, `2dzoom`...) for `cachepack` to bake, while the same
     * keys on an overlay of a cache record are a paste from a LostCity tree and
     * are reported. See obj_apply_key.
     */
    int minted;
};

/* Names are bump-allocated into a chain of these, never realloc'd (entries
 * point straight into the bytes) and never freed one at a time — every free
 * site tears down a whole pack. One strdup per symbol put ~212k tiny blocks on
 * the heap for ~5MB of names; the chain carries the same bytes in a few
 * hundred blocks. */
#define PACK_NAME_CHUNK_BYTES (16 * 1024)

struct PackNameChunk
{
    struct PackNameChunk* next;
    size_t used;
    char bytes[PACK_NAME_CHUNK_BYTES];
};

struct Pack
{
    struct PackEntry* entries;
    struct PackEntry** by_name;
    struct PackEntry** by_id;
    /* Var packs only: the entries spelled `varp<id>_<base>` / `varb<id>_<base>`,
     * sorted by `<base>`, for ToriRSServer_ContentVarSymbol. */
    struct PackEntry** by_base;
    int base_count;
    int count;
    int capacity;
    struct PackNameChunk* names;
};

static struct Pack g_packs[TORIRSSERVER_PACK_COUNT];

static int
pack_entry_name_compare(
    const void* lhs,
    const void* rhs)
{
    const struct PackEntry* left = *(const struct PackEntry* const*)lhs;
    const struct PackEntry* right = *(const struct PackEntry* const*)rhs;
    int order = strcmp(left->name, right->name);

    if( order != 0 )
        return order;
    if( left->id != right->id )
        return left->id < right->id ? -1 : 1;
    return left < right ? -1 : left > right;
}

static int
pack_entry_id_compare(
    const void* lhs,
    const void* rhs)
{
    const struct PackEntry* left = *(const struct PackEntry* const*)lhs;
    const struct PackEntry* right = *(const struct PackEntry* const*)rhs;

    if( left->id != right->id )
        return left->id < right->id ? -1 : 1;
    return left < right ? -1 : left > right;
}

/* `varb542_cutscene_status` -> `cutscene_status`; NULL for a name that does not
 * carry a var's kind-and-id prefix. */
static const char*
pack_var_base(const char* name)
{
    const char* c;

    if( strncmp(name, "varp", 4) != 0 && strncmp(name, "varb", 4) != 0
        && strncmp(name, "varc", 4) != 0 )
        return NULL;
    c = name + 4;
    if( !isdigit((unsigned char)*c) )
        return NULL;
    while( isdigit((unsigned char)*c) )
        c++;
    return *c == '_' && c[1] ? c + 1 : NULL;
}

static int
pack_entry_base_compare(
    const void* lhs,
    const void* rhs)
{
    const struct PackEntry* left = *(const struct PackEntry* const*)lhs;
    const struct PackEntry* right = *(const struct PackEntry* const*)rhs;

    return strcmp(pack_var_base(left->name), pack_var_base(right->name));
}

static void
pack_build_indexes(struct Pack* pack)
{
    if( pack->count <= 0 )
        return;
    pack->by_name = malloc((size_t)pack->count * sizeof(*pack->by_name));
    pack->by_id = malloc((size_t)pack->count * sizeof(*pack->by_id));
    assert(pack->by_name);
    assert(pack->by_id);
    for( int i = 0; i < pack->count; i++ )
    {
        pack->by_name[i] = &pack->entries[i];
        pack->by_id[i] = &pack->entries[i];
    }
    qsort(pack->by_name,
          (size_t)pack->count,
          sizeof(*pack->by_name),
          pack_entry_name_compare);
    qsort(pack->by_id, (size_t)pack->count, sizeof(*pack->by_id), pack_entry_id_compare);

    if( pack != &g_packs[TORIRSSERVER_PACK_VARP] && pack != &g_packs[TORIRSSERVER_PACK_VARBIT] )
        return;
    pack->by_base = malloc((size_t)pack->count * sizeof(*pack->by_base));
    assert(pack->by_base);
    pack->base_count = 0;
    for( int i = 0; i < pack->count; i++ )
    {
        if( pack_var_base(pack->entries[i].name) )
            pack->by_base[pack->base_count++] = &pack->entries[i];
    }
    qsort(pack->by_base,
          (size_t)pack->base_count,
          sizeof(*pack->by_base),
          pack_entry_base_compare);
}

static char*
pack_name_intern(
    struct Pack* pack,
    const char* name)
{
    size_t len = strlen(name) + 1;
    struct PackNameChunk* chunk = pack->names;

    /* Symbols come out of 512-byte line buffers, so a name can never outgrow a
     * chunk. */
    assert(len <= PACK_NAME_CHUNK_BYTES);
    if( !chunk || chunk->used + len > PACK_NAME_CHUNK_BYTES )
    {
        chunk = malloc(sizeof(*chunk));
        assert(chunk);
        chunk->next = pack->names;
        chunk->used = 0;
        pack->names = chunk;
    }
    memcpy(chunk->bytes + chunk->used, name, len);
    chunk->used += len;
    return chunk->bytes + chunk->used - len;
}

static void
pack_names_free(struct Pack* pack)
{
    struct PackNameChunk* chunk = pack->names;

    while( chunk )
    {
        struct PackNameChunk* next = chunk->next;
        free(chunk);
        chunk = next;
    }
    pack->names = NULL;
}

static void
pack_add(
    struct Pack* pack,
    const char* name,
    int id,
    int minted)
{
    if( pack->count == pack->capacity )
    {
        int capacity = pack->capacity ? pack->capacity * 2 : 64;
        struct PackEntry* grown = realloc(pack->entries, (size_t)capacity * sizeof(*grown));

        assert(grown);
        pack->entries = grown;
        pack->capacity = capacity;
    }
    pack->entries[pack->count].name = pack_name_intern(pack, name);
    pack->entries[pack->count].id = id;
    pack->entries[pack->count].minted = minted;
    pack->count++;
}

static int
pack_load(
    struct Pack* pack,
    const char* path,
    int minted)
{
    FILE* file = fopen(path, "rb");
    char raw[512];
    int loaded = 0;

    /* A missing file is not an error: a namespace nothing has named yet has no
     * file at all, which is a different claim from an empty one. */
    if( !file )
        return 0;
    while( fgets(raw, sizeof(raw), file) )
    {
        char* line = ToriRSServer_ContentCleanLine(raw);
        char* name;

        if( !*line )
            continue;
        name = ToriRSServer_ContentSplitKeyValue(line);
        if( !name || !*name )
            continue;
        pack_add(pack, name, atoi(line), minted);
        loaded++;
    }
    fclose(file);
    return loaded;
}

/**
 * Build the component symbols the way the client addresses a component: an
 * interface id in the high 16 bits and a child id in the low 16.
 *
 * There is no `pack/component.pack` to read. There used to be — 26,491 lines of
 * `786432=bankmain:infinite` — and it was a second index over members that
 * `interfaces/bankmain.compack` already indexes, one named and one filler. The
 * compack carries the cache's own names now, so the id composes from the two files
 * that remain: `pack/3_interfaces.pack` says `bankmain` is 12, the compack says
 * `infinite` is child 0, and `(12 << 16) | 0` is 786432.
 *
 * Composed at load into the same flat table the other namespaces use, so a lookup
 * stays one pass and `torirs_server_ids.c` still asks for `"bankmain:items"`.
 */
static int
load_component_symbols_from_root(
    const char* dir,
    int additions_only)
{
    const struct Pack* interfaces = &g_packs[TORIRSSERVER_PACK_INTERFACE];
    int loaded = 0;

    for( int i = 0; i < interfaces->count; i++ )
    {
        const char* iface_name = interfaces->entries[i].name;
        int iface_id = interfaces->entries[i].id;
        struct Pack children;
        char path[1024];

        if( iface_id < 0 || !iface_name )
            continue;
        memset(&children, 0, sizeof(children));
        snprintf(path, sizeof(path), "%s/interfaces/%s.compack", dir, iface_name);
        if( pack_load(&children, path, 0) == 0 )
        {
            pack_names_free(&children);
            free(children.entries);
            continue;
        }
        for( int c = 0; c < children.count; c++ )
        {
            char full[512];
            int uid;
            const char* existing;

            if( children.entries[c].id < 0 || children.entries[c].id > 0xffff )
                continue;
            snprintf(full, sizeof(full), "%s:%s", iface_name, children.entries[c].name);
            uid = (iface_id << 16) | children.entries[c].id;
            existing = ToriRSServer_ContentSymbolName(TORIRSSERVER_PACK_COMPONENT, uid);
            if( additions_only && existing )
            {
                if( strcmp(existing, full) != 0 )
                    CONTENT_ERROR("%s: component %d:%d is both `%s` and `%s`\n",
                                  path, iface_id, children.entries[c].id, existing, full);
                continue;
            }
            pack_add(&g_packs[TORIRSSERVER_PACK_COMPONENT], full, uid, 0);
            loaded++;
        }
        pack_names_free(&children);
        free(children.entries);
    }
    return loaded;
}

/*
 * Marked client-content lanes may extend an existing interface without making
 * that extension part of a flag-off cache bake.  Their RuneScript compiler is
 * given the lane as a --component-root; the runtime needs the same name->uid
 * view so it can find name-addressed component triggers.  Component uids above
 * the script lookup-key's 21-bit subject range (including every stats child)
 * cannot be recovered from the numeric trigger index alone.
 *
 * Loading these names does not mount an interface or expose a cache record.  It
 * only lets an already-selected server script pack resolve a packet uid back to
 * the spelling under which that pack was compiled.  Existing children must be
 * restated identically; a lane can add a child, but cannot silently rename one.
 */
static int
load_ported_component_symbols(const char* dir)
{
    DIR* handle;
    struct dirent* entry;
    char ported[1024];
    char lane[1024];
    int loaded = 0;

    snprintf(ported, sizeof(ported), "%s/ported", dir);
    handle = opendir(ported);
    if( !handle )
        return 0;
    while( (entry = readdir(handle)) != NULL )
    {
        char overlays[1152];

        if( entry->d_name[0] == '.' )
            continue;
        snprintf(lane, sizeof(lane), "%s/%s", ported, entry->d_name);
        if( ToriRSServer_PathIsDir(lane) )
        {
            loaded += load_component_symbols_from_root(lane, 1);
            /* Existing-interface additions live here so the feature-off lane
             * never replaces the base archive. The script compiler receives
             * this as an explicit component root; give runtime packet dispatch
             * the same name-to-uid view. */
            snprintf(overlays, sizeof(overlays), "%s/interface_overlays", lane);
            loaded += load_component_symbols_from_root(overlays, 1);
        }
    }
    closedir(handle);
    return loaded;
}

/*
 * One past the highest id this pack names.
 *
 * `count` is the number of ENTRIES, and a `.compack` is dense in practice but
 * not by contract, so the answer is the last id plus one rather than the entry
 * count. Callers size an id-indexed array with it; getting that wrong is an
 * out-of-bounds write on the one record whose id is the highest.
 */
/*
 * Whether this id was MINTED by content rather than named by the cache.
 *
 * `pack/<ns>.alloc` is the server's allocation ledger and `configs/all.<ns>.compack`
 * is the cache's member index; both load into one namespace, so this is the
 * only thing that can still tell them apart afterwards. A lane's own
 * `pack/<ns>.{pack,alloc}` counts as minted too -- a lane exists to bring
 * records the base cache does not have.
 */
int
ToriRSServer_ContentSymbolIsMinted(
    enum ToriRSServerPackKind kind,
    int id)
{
    const struct Pack* pack;

    if( kind < 0 || kind >= TORIRSSERVER_PACK_COUNT )
        return 0;
    pack = &g_packs[kind];
    for( int i = 0; i < pack->count; i++ )
        if( pack->entries[i].id == id )
            return pack->entries[i].minted;
    return 0;
}

int
ToriRSServer_ContentSymbolCount(enum ToriRSServerPackKind kind)
{
    const struct Pack* pack;

    if( kind < 0 || kind >= TORIRSSERVER_PACK_COUNT )
        return 0;
    pack = &g_packs[kind];
    if( !pack->by_id || pack->count <= 0 )
        return 0;
    return pack->by_id[pack->count - 1]->id + 1;
}

int
ToriRSServer_ContentSymbol(
    enum ToriRSServerPackKind kind,
    const char* name)
{
    const struct Pack* pack;

    assert(name);
    if( !*name )
        return -1;
    /* LostCity spells "no value" as the literal `null`, in configs and as a
     * param default. Resolving it to -1 without complaint is what lets
     * `param=death_drop,null` mean "drops nothing". */
    if( strcmp(name, "null") == 0 )
        return -1;
    if( kind < 0 || kind >= TORIRSSERVER_PACK_COUNT )
        return -1;

    /* One file, one pass. There is no precedence question left: a name means one
     * id or the loader refused to start (see validate_symbols). */
    pack = &g_packs[kind];
    if( pack->by_name )
    {
        int low = 0;
        int high = pack->count;

        while( low < high )
        {
            int mid = low + (high - low) / 2;

            if( strcmp(pack->by_name[mid]->name, name) < 0 )
                low = mid + 1;
            else
                high = mid;
        }
        if( low < pack->count && strcmp(pack->by_name[low]->name, name) == 0 )
            return pack->by_name[low]->id;
        return -1;
    }
    for( int i = 0; i < pack->count; i++ )
    {
        if( strcmp(pack->entries[i].name, name) == 0 )
            return pack->entries[i].id;
    }
    return -1;
}

int
ToriRSServer_ContentSymbolChecked(
    enum ToriRSServerPackKind kind,
    const char* name,
    int* out_id)
{
    *out_id = -1;
    assert(name);
    if( !*name )
        return 0;
    /* The one spelling of -1 that is an answer rather than a miss. Kept here and
     * not in the caller so "how LostCity writes nothing" is stated once. */
    if( strcmp(name, "null") == 0 )
        return 1;
    *out_id = ToriRSServer_ContentSymbol(kind, name);
    return *out_id >= 0;
}

int
ToriRSServer_ContentVarSymbol(
    enum ToriRSServerPackKind kind,
    const char* base)
{
    const struct Pack* pack;
    int low = 0;
    int high;

    assert(base);
    assert(kind == TORIRSSERVER_PACK_VARP || kind == TORIRSSERVER_PACK_VARBIT);
    pack = &g_packs[kind];
    if( !pack->by_base )
        return -1;
    high = pack->base_count;
    while( low < high )
    {
        int mid = low + (high - low) / 2;

        if( strcmp(pack_var_base(pack->by_base[mid]->name), base) < 0 )
            low = mid + 1;
        else
            high = mid;
    }
    if( low < pack->base_count && strcmp(pack_var_base(pack->by_base[low]->name), base) == 0 )
        return pack->by_base[low]->id;
    return -1;
}

const char*
ToriRSServer_ContentSymbolName(
    enum ToriRSServerPackKind kind,
    int symbol_id)
{
    const struct Pack* pack;

    if( kind < 0 || kind >= TORIRSSERVER_PACK_COUNT )
        return NULL;
    pack = &g_packs[kind];
    if( pack->by_id )
    {
        int low = 0;
        int high = pack->count;

        while( low < high )
        {
            int mid = low + (high - low) / 2;

            if( pack->by_id[mid]->id < symbol_id )
                low = mid + 1;
            else
                high = mid;
        }
        if( low < pack->count && pack->by_id[low]->id == symbol_id )
            return pack->by_id[low]->name;
        return NULL;
    }
    for( int i = 0; i < pack->count; i++ )
    {
        if( pack->entries[i].id == symbol_id )
            return pack->entries[i].name;
    }
    return NULL;
}

int
ToriRSServer_ContentSymbolWalk(
    enum ToriRSServerPackKind kind,
    int index,
    int* out_id,
    const char** out_name)
{
    const struct Pack* pack;

    if( kind < 0 || kind >= TORIRSSERVER_PACK_COUNT )
        return 0;
    pack = &g_packs[kind];
    if( index < 0 || index >= pack->count )
        return pack->count;
    if( out_id )
        *out_id = pack->entries[index].id;
    if( out_name )
        *out_name = pack->entries[index].name;
    return pack->count;
}

/* Forward: the diagnostic namespace name, defined just below. */
static const char*
pack_kind_name(enum ToriRSServerPackKind kind);

/**
 * Refuse to start on a symbol table that answers a name two ways.
 *
 * Two rules, both LostCity's (`packConfigs()`), and neither of them about how many
 * *files* a namespace has — which is why both survived the collapse to one file
 * per namespace while the third rule (an authored name shadowing a cache name in a
 * different file) simply stopped being expressible:
 *
 * 1. **A name means one id within a namespace.** Two lines binding `bankcert` to
 *    115 and to 843 make it mean one thing to a reader and another to the loader,
 *    and last-one-wins is not a resolution. This is also the check that catches
 *    the one thing the single-file format cannot hold: an alias for an id the
 *    cache already names, which a migration might try to reintroduce.
 *
 * 2. **varp, varbit, varn and vars share one RuneScript name domain.** `%name`
 *    does not say which of the four it is, so a name meaning varp 115 and varbit 4
 *    cannot coexist however separate their pack files look. The membership comes
 *    from the register's `vardomain` column rather than a list here, so adding a
 *    namespace to the domain is one declaration.
 *
 * Returns the number of collisions; each is reported through the content error
 * count, so `ToriRSServer_Pack` fails on them too.
 */
/**
 * One symbol, tagged with the namespace it came from, for the collision sort.
 *
 * Sorted rather than compared pairwise, and that is not a micro-optimisation: the
 * merged tables are large — 62,194 locs, 26,491 components — and the pairwise form
 * of this check is billions of `strcmp`s before the server prints its first line.
 */
struct SymbolRow
{
    const char* name;
    int id;
    int kind;
};

static int
compare_symbol_row(
    const void* lhs,
    const void* rhs)
{
    const struct SymbolRow* left = lhs;
    const struct SymbolRow* right = rhs;
    int order = strcmp(left->name, right->name);

    if( order != 0 )
        return order;
    /* Ties by id so the report is stable, and so a name listed twice for the same
     * id lands adjacent and reads as redundant rather than ambiguous. */
    return left->id - right->id;
}

/** Sorted copy of one or more namespaces' rows. Caller frees. */
static struct SymbolRow*
collect_rows(
    const int* kinds,
    int kind_count,
    int* out_count)
{
    int total = 0;

    for( int k = 0; k < kind_count; k++ )
        total += g_packs[kinds[k]].count;
    *out_count = total;
    if( total == 0 )
        return NULL;

    struct SymbolRow* rows = malloc((size_t)total * sizeof(*rows));
    assert(rows);
    int at = 0;
    for( int k = 0; k < kind_count; k++ )
    {
        const struct Pack* pack = &g_packs[kinds[k]];
        for( int i = 0; i < pack->count; i++ )
        {
            rows[at].name = pack->entries[i].name;
            rows[at].id = pack->entries[i].id;
            rows[at].kind = kinds[k];
            at++;
        }
    }
    qsort(rows, (size_t)total, sizeof(*rows), compare_symbol_row);
    return rows;
}

static int
validate_symbols(const struct ContentRegister* reg)
{
    int collisions = 0;
    int shared[TORIRSSERVER_PACK_COUNT];
    int shared_count = 0;

    for( int kind = 0; kind < TORIRSSERVER_PACK_COUNT; kind++ )
    {
        int only[1] = { kind };
        int count = 0;
        struct SymbolRow* rows = collect_rows(only, 1, &count);
        const struct ContentNamespace* ns =
            ContentRegister_Find(reg, pack_kind_name((enum ToriRSServerPackKind)kind));

        if( ns && ns->shared_var_domain )
            shared[shared_count++] = kind;

        for( int i = 1; i < count; i++ )
        {
            if( strcmp(rows[i].name, rows[i - 1].name) != 0 )
                continue;
            if( rows[i].id == rows[i - 1].id )
                continue; /* the same line twice is redundant, not ambiguous */
            CONTENT_ERROR("pack/%s.pack: `%s` is both %d and %d — a name means one id\n",
                          pack_kind_name((enum ToriRSServerPackKind)kind), rows[i].name,
                          rows[i - 1].id, rows[i].id);
            collisions++;
        }
        free(rows);
    }

    /* The `%name` domain, as the register declares it: all its namespaces sorted
     * together, so a name appearing under two of them lands adjacent. */
    if( shared_count > 1 )
    {
        int count = 0;
        struct SymbolRow* rows = collect_rows(shared, shared_count, &count);

        for( int i = 1; i < count; i++ )
        {
            if( strcmp(rows[i].name, rows[i - 1].name) != 0 )
                continue;
            if( rows[i].kind == rows[i - 1].kind )
                continue; /* already reported by the per-namespace pass */
            CONTENT_ERROR("`%s` is both %s %d and %s %d — one RuneScript name domain "
                          "covers both\n",
                          rows[i].name, pack_kind_name((enum ToriRSServerPackKind)rows[i - 1].kind),
                          rows[i - 1].id, pack_kind_name((enum ToriRSServerPackKind)rows[i].kind),
                          rows[i].id);
            collisions++;
        }
        free(rows);
    }

    return collisions;
}

/**
 * The namespace a kind is spelled with: `pack/<name>.pack`, and the register row.
 *
 * One table rather than two. This used to name only the thirteen kinds a
 * diagnostic had ever mentioned, with the other eight left NULL, while a second
 * list inside `ToriRSServer_ContentLoad` said which files to read — so a kind could be
 * loadable and unnameable, or named and never loaded, and nothing compared them.
 * Every kind is spelled here and the loader walks this.
 */
const char*
ToriRSServer_ContentPackName(enum ToriRSServerPackKind kind)
{
    return pack_kind_name(kind);
}

static const char*
pack_kind_name(enum ToriRSServerPackKind kind)
{
    static const char* k_names[TORIRSSERVER_PACK_COUNT] = {
        [TORIRSSERVER_PACK_NPC] = "npc",
        [TORIRSSERVER_PACK_OBJ] = "obj",
        [TORIRSSERVER_PACK_LOC] = "loc",
        [TORIRSSERVER_PACK_SEQ] = "seq",
        [TORIRSSERVER_PACK_SPOTANIM] = "spotanim",
        [TORIRSSERVER_PACK_INV] = "inv",
        [TORIRSSERVER_PACK_VARP] = "varp",
        [TORIRSSERVER_PACK_VARBIT] = "varbit",
        [TORIRSSERVER_PACK_VARN] = "varn",
        [TORIRSSERVER_PACK_VARS] = "vars",
        /* The archive index of cache index 3; see content_register.h `cache_index`. */
        [TORIRSSERVER_PACK_INTERFACE] = "3_interfaces",
        [TORIRSSERVER_PACK_COMPONENT] = "component",
        [TORIRSSERVER_PACK_STAT] = "stat",
        [TORIRSSERVER_PACK_PARAM] = "param",
        [TORIRSSERVER_PACK_HITSPLAT] = "hitsplat",
        [TORIRSSERVER_PACK_HEALTHBAR] = "healthbar",
        /* Cache index 4's archive index, same shape as 3_interfaces above. */
        [TORIRSSERVER_PACK_SYNTH] = "4_soundeffects",
        [TORIRSSERVER_PACK_ENUM] = "enum",
        [TORIRSSERVER_PACK_STRUCT] = "struct",
        [TORIRSSERVER_PACK_DBTABLE] = "dbtable",
        [TORIRSSERVER_PACK_DBROW] = "dbrow",
        [TORIRSSERVER_PACK_CATEGORY] = "category",
        [TORIRSSERVER_PACK_IDK] = "idk",
    };

    if( kind < 0 || kind >= TORIRSSERVER_PACK_COUNT )
        return "?";
    return k_names[kind];
}

/*
 * Imported client lanes own minted ids outside the rank-0 cache's member
 * compacks.  The ServerScript compiler layers each lane's pack directory over
 * the ordinary symbols; the runtime must resolve server overlay headers through
 * the same view (`[rs2012_tormented_demon_melee]`, for example), or the script
 * compiles while its npc definition silently fails to load.
 *
 * Keep this generic over marked directories below `ported/`: Summoning and the
 * RS2012 QBD/TD lane use the same `.alloc` contract, and future lanes should not
 * require another hard-coded root in C.  `validate_symbols` below still rejects
 * either a duplicate id with a different name or a duplicate name with a
 * different id after all layers have been read.
 */
static int
load_ported_pack_symbols(const char* dir)
{
    DIR* handle;
    struct dirent* entry;
    char ported[1024];
    char lane[1024];
    char path[1280];
    int loaded = 0;

    snprintf(ported, sizeof(ported), "%s/ported", dir);
    handle = opendir(ported);
    if( !handle )
        return 0;
    while( (entry = readdir(handle)) != NULL )
    {
        if( entry->d_name[0] == '.' )
            continue;
        snprintf(lane, sizeof(lane), "%s/%s", ported, entry->d_name);
        if( !ToriRSServer_PathIsDir(lane) )
            continue;
        for( int kind = 0; kind < TORIRSSERVER_PACK_COUNT; kind++ )
        {
            const char* name = pack_kind_name((enum ToriRSServerPackKind)kind);

            if( kind == TORIRSSERVER_PACK_COMPONENT )
                continue; /* composed from lane interface compacks below */
            snprintf(path, sizeof(path), "%s/pack/%s.pack", lane, name);
            loaded += pack_load(&g_packs[kind], path, 1);
            snprintf(path, sizeof(path), "%s/pack/%s.alloc", lane, name);
            loaded += pack_load(&g_packs[kind], path, 1);
        }
    }
    closedir(handle);
    return loaded;
}

/**
 * 1 when this kind's records are files of a config archive, so its index is a
 * `.compack` beside `configs/all.<type>` rather than a pack in `pack/`.
 *
 * The five that are not: interfaces and sound effects are whole cache archives
 * (index 3 and 4) and get an archive-level pack; components are named across
 * every interface archive at once; `stat` is fixed by the wire; `category` is an
 * obj field the cache numbers and nothing names.
 */
static int
pack_kind_is_config(enum ToriRSServerPackKind kind)
{
    switch( kind )
    {
    case TORIRSSERVER_PACK_INTERFACE:
    case TORIRSSERVER_PACK_COMPONENT:
    case TORIRSSERVER_PACK_STAT:
    case TORIRSSERVER_PACK_CATEGORY:
    case TORIRSSERVER_PACK_VARN:
    case TORIRSSERVER_PACK_VARS:
    /* Sound effects are cache index 4 and get an archive-level pack, the same
     * shape as interfaces above: there is no `configs/all.synth`, because a
     * sound effect is a whole archive rather than a file inside one. */
    case TORIRSSERVER_PACK_SYNTH:
        return 0;
    default:
        return 1;
    }
}

int
ToriRSServer_ContentResolve(
    const char* what,
    const struct ToriRSServerSymbolRef* refs,
    int count)
{
    int failed = 0;

    for( int i = 0; i < count; i++ )
    {
        *refs[i].out = ToriRSServer_ContentSymbol(refs[i].kind, refs[i].name);
        if( *refs[i].out >= 0 )
            continue;
        CONTENT_ERROR("%s: no `%s` in %s.pack\n", what, refs[i].name,
                      pack_kind_name(refs[i].kind));
        failed++;
    }
    return failed;
}

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

struct Constant
{
    char* name; /* without the caret */
    char* text;
};

static struct Constant* g_constants;
static int g_constant_count;
static int g_constant_capacity;
/* name -> position in g_constants. The loader asks for every new line's name
 * to refuse a duplicate, which a scan made O(n^2) over the tree's constants. */
static struct ContentNameIndex g_constant_index;

const char*
ToriRSServer_ContentConstant(const char* name)
{
    int at;

    assert(name);
    if( *name == '^' )
        name++;
    at = ContentNameIndex_Find(&g_constant_index, name);
    return at < 0 ? NULL : g_constants[at].text;
}

int
ToriRSServer_ContentConstantInt(
    const char* name,
    int fallback)
{
    const char* text = ToriRSServer_ContentConstant(name);
    char* end;
    long value;

    if( !text )
    {
        CONTENT_ERROR("no `^%s` in any .constant\n", *name == '^' ? name + 1 : name);
        return fallback;
    }
    value = strtol(text, &end, 10);
    while( *end == ' ' || *end == '\t' )
        end++;
    if( end == text || *end )
    {
        CONTENT_ERROR("`^%s` is `%s`, which is not a number\n",
                      *name == '^' ? name + 1 : name, text);
        return fallback;
    }
    return (int)value;
}

/* ------------------------------------------------------------------ */
/* Definition tables                                                   */
/* ------------------------------------------------------------------ */

/*
 * Sparse ids over a dense array would be 15,000 entries of mostly nothing, so
 * definitions live in a growable list and lookup is a scan. The list is the
 * size of the content tree — a few dozen — not the size of the cache.
 */

static struct ToriRSServerNpcDef* g_npc_defs;
static int g_npc_def_count;
static int g_npc_def_capacity;

static struct ToriRSServerEnumDef* g_enum_defs;
static int g_enum_def_count;
static int g_enum_def_capacity;
/* symbol -> position in g_enum_defs, for the first g_enum_indexed defs. The
 * list holds every server-pack enum (~5,900), and a scan of it was asked for
 * by name for every appearance slot of every player every tick. Defs only
 * append between frees, so the index catches up on the next lookup. */
static struct ContentNameIndex g_enum_index;
static int g_enum_indexed;

static struct ToriRSServerVarpDef* g_varp_defs;
static int g_varp_def_count;
static int g_varp_def_capacity;

static struct ToriRSServerLocDef* g_loc_defs;
static int g_loc_def_count;
static int g_loc_def_capacity;

static struct ToriRSServerMapNpcSpawn* g_npc_spawns;
static int g_npc_spawn_count;
static int g_npc_spawn_capacity;

static struct ToriRSServerMapObjSpawn* g_obj_spawns;
static int g_obj_spawn_count;
static int g_obj_spawn_capacity;

/** Engine defaults. OpenRune's NpcCombatDef.DEFAULT, with LostCity's animation
 *  names — the two agree on all of it. */
static struct ToriRSServerNpcDef g_npc_default;

static void*
grow(
    void* array,
    int* capacity,
    int count,
    size_t element)
{
    void* grown;

    if( count < *capacity )
        return array;
    *capacity = *capacity ? *capacity * 2 : 32;
    grown = realloc(array, (size_t)*capacity * element);
    return grown ? grown : array;
}

/*
 * Both def tables are ascending by id: the pack loader creates them in the
 * order the pack's band archives are read, which is id order, and sorts the loc
 * table once more after the category pass appends to it. So a lookup is a binary
 * search rather than the linear scan the text loader's arrival order needed.
 */
const struct ToriRSServerNpcDef*
ToriRSServer_ContentNpc(int npc_id)
{
    int low = 0;
    int high = g_npc_def_count - 1;

    while( low <= high )
    {
        int mid = low + (high - low) / 2;

        if( g_npc_defs[mid].npc_id == npc_id )
            return &g_npc_defs[mid];
        if( g_npc_defs[mid].npc_id < npc_id )
            low = mid + 1;
        else
            high = mid - 1;
    }
    return NULL;
}

const struct ToriRSServerNpcDef*
ToriRSServer_ContentNpcDefault(void)
{
    return &g_npc_default;
}

int
ToriRSServer_ContentNpcParam(
    const struct ToriRSServerNpcDef* def,
    int param_id,
    int32_t* out)
{
    assert(def);
    for( int i = 0; i < def->param_count; i++ )
    {
        if( def->params[i].key != param_id )
            continue;
        if( out )
            *out = def->params[i].value;
        return 1;
    }
    return 0;
}

const struct ToriRSServerEnumDef*
ToriRSServer_ContentEnum(const char* symbol)
{
    int at;

    assert(symbol);
    /* In storage order, and the first symbol added wins: the def a front-to-back
     * scan met first. */
    for( ; g_enum_indexed < g_enum_def_count; g_enum_indexed++ )
    {
        if( g_enum_defs[g_enum_indexed].symbol )
            ContentNameIndex_Add(&g_enum_index, g_enum_defs[g_enum_indexed].symbol,
                                 g_enum_indexed);
    }
    at = ContentNameIndex_Find(&g_enum_index, symbol);
    return at < 0 ? NULL : &g_enum_defs[at];
}

const struct ToriRSServerEnumDef*
ToriRSServer_ContentEnumById(int enum_id)
{
    const char* symbol;

    if( enum_id < 0 )
        return NULL;
    /*
     * Id -> name -> def, rather than storing the id on the def.
     *
     * The name is the only thing a `.enum` block states; the id comes from
     * `configs/all.enum.compack`, which is loaded by the time any config is read. Going
     * through the pack keeps one answer to "what number is this enum" — the
     * alternative, resolving it while parsing and caching it on the def, is a
     * second answer that silently disagrees when a pack is regenerated.
     */
    symbol = ToriRSServer_ContentSymbolName(TORIRSSERVER_PACK_ENUM, enum_id);
    if( !symbol )
        return NULL;
    return ToriRSServer_ContentEnum(symbol);
}

const struct ToriRSServerVarpDef*
ToriRSServer_ContentVarp(int varp_id)
{
    for( int i = 0; i < g_varp_def_count; i++ )
    {
        if( g_varp_defs[i].varp_id == varp_id )
            return &g_varp_defs[i];
    }
    return NULL;
}

const struct ToriRSServerLocDef*
ToriRSServer_ContentLoc(int loc_id)
{
    int low = 0;
    int high = g_loc_def_count - 1;

    while( low <= high )
    {
        int mid = low + (high - low) / 2;

        if( g_loc_defs[mid].loc_id == loc_id )
            return &g_loc_defs[mid];
        if( g_loc_defs[mid].loc_id < loc_id )
            low = mid + 1;
        else
            high = mid - 1;
    }
    return NULL;
}

const struct ToriRSServerLocDef*
ToriRSServer_ContentLocAt(int index)
{
    if( index < 0 || index >= g_loc_def_count )
        return NULL;
    return &g_loc_defs[index];
}

const struct ToriRSServerMapNpcSpawn*
ToriRSServer_ContentNpcSpawns(int* count)
{
    *count = g_npc_spawn_count;
    return g_npc_spawns;
}

const struct ToriRSServerMapObjSpawn*
ToriRSServer_ContentObjSpawns(int* count)
{
    *count = g_obj_spawn_count;
    return g_obj_spawns;
}

/* ------------------------------------------------------------------ */
/* npc definitions                                                     */
/* ------------------------------------------------------------------ */

static void
npc_def_seed_from_cache(
    struct ToriRSServerNpcDef* def,
    int npc_id)
{
    /*
     * The ungated row: the bonuses are the record's whether or not it has a
     * name. The name-gated accessor answers a placeholder for a nameless record
     * (every multinpc shell), and seeding through it gave a shell none of the
     * params its own record states — invisible while the text parse restated
     * them on the def, wrong once the record is the one place they live.
     */
    const struct ToriRSServerNpcInfo* info = ToriRSServer_NpcInfoRecord(npc_id);

    *def = g_npc_default;
    /* The struct copy aliased the default's heap arrays — a `[default]` block
     * may state `param=` or a patrol, and every def's free would then be a
     * double free. Own copies, or nothing. */
    if( g_npc_default.params )
    {
        def->params = malloc((size_t)g_npc_default.param_count * sizeof(*def->params));
        assert(def->params);
        memcpy(def->params, g_npc_default.params,
               (size_t)g_npc_default.param_count * sizeof(*def->params));
    }
    if( g_npc_default.patrol )
    {
        def->patrol = calloc(TORIRSSERVER_NPC_PATROL_MAX, sizeof(*def->patrol));
        assert(def->patrol);
        memcpy(def->patrol, g_npc_default.patrol,
               TORIRSSERVER_NPC_PATROL_MAX * sizeof(*def->patrol));
    }
    def->npc_id = npc_id;
    if( info && info->has_params )
    {
        for( int i = 0; i < TORIRSSERVER_PARAM_BONUS_COUNT; i++ )
            def->bonus[i] = info->bonus[i];
        def->attackrate = info->attackrate;
    }
}

/*
 * Also file the param under its *id*, so `npc_param` can find it.
 *
 * Every branch below writes a named C field, which is what the engine reads.
 * That is fine for the engine and useless to a script: `npc_param` is handed a
 * param *id* and has no way back to a field name. Until this list existed the
 * only opcode-visible authored param was `death_drop`, and it was visible
 * because `torirs_server_scripts.c` compared the popped id against
 * `ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_PARAM, "death_drop")` — one game-facing
 * name in C, answering one param, pushing 0 for the rest.
 *
 * These rows are rank 1 in the sense CONTENT_ARCHITECTURE.md §3.1 uses: an
 * authored overlay value that overrides the cache record's own param table.
 * `npc_param` reads them first and the cache row second, which is the same
 * precedence `npc_def_seed_from_cache` already gives the bonuses.
 *
 * A name the param pack does not know is filed under no id and still writes its
 * field — `attack_anim`, `defend_anim` and the rest are engine spellings that
 * predate the pack, and refusing them here would break loading rather than
 * teach anyone anything.
 */
static void
record_authored_param(
    struct ToriRSServerNpcDef* def,
    const char* name,
    int32_t resolved,
    const char* where)
{
    int param_id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_PARAM, name);

    if( param_id < 0 )
        return;
    for( int i = 0; i < def->param_count; i++ )
    {
        if( def->params[i].key == param_id )
        {
            def->params[i].value = resolved;
            return;
        }
    }
    if( def->param_count >= TORIRSSERVER_NPCDEF_PARAM_MAX )
    {
        CONTENT_ERROR("%s: more than %d authored params on one npc\n", where,
                      TORIRSSERVER_NPCDEF_PARAM_MAX);
        return;
    }
    /* Exact-size realloc: appends are capped at PARAM_MAX per def and happen
     * only at load, so growth games buy nothing here. */
    def->params = realloc(
        def->params, ((size_t)def->param_count + 1) * sizeof(*def->params));
    assert(def->params);
    def->params[def->param_count].key = param_id;
    def->params[def->param_count].value = resolved;
    def->param_count++;
}

/* ------------------------------------------------------------------ */
/* .constant configs                                                   */
/* ------------------------------------------------------------------ */

/*
 * `^name = value`, one per line, no sections. LostCity writes the caret on the
 * declaration as well as at every use, so the file is greppable for either.
 *
 * The text is kept verbatim rather than parsed: a constant expands to whatever
 * the grammar accepts — a number here, but a coord or a string in a `.rs2` — and
 * the serverscript compiler reads the same files through ssc_symbols.
 */
static void
load_constant_config(const char* path)
{
    FILE* file = fopen(path, "rb");
    char raw[1024];
    int line_number = 0;

    if( !file )
        return;
    while( fgets(raw, sizeof(raw), file) )
    {
        char* line = ToriRSServer_ContentCleanLine(raw);
        char* value;

        line_number++;
        if( !*line )
            continue;
        if( *line != '^' )
        {
            CONTENT_ERROR("%s:%d: a .constant holds `^name = value` lines only\n", path,
                          line_number);
            continue;
        }
        value = ToriRSServer_ContentSplitKeyValue(line);
        if( !value )
        {
            CONTENT_ERROR("%s:%d: `%s` has no `=`\n", path, line_number, line);
            continue;
        }
        if( ToriRSServer_ContentConstant(line + 1) )
        {
            CONTENT_ERROR("%s:%d: `%s` is declared twice\n", path, line_number, line);
            continue;
        }
        g_constants =
            grow(g_constants, &g_constant_capacity, g_constant_count, sizeof(*g_constants));
        g_constants[g_constant_count].name = strdup(line + 1);
        assert(g_constants[g_constant_count].name);
        g_constants[g_constant_count].text = strdup(value);
        assert(g_constants[g_constant_count].text);
        ContentNameIndex_Add(&g_constant_index, g_constants[g_constant_count].name,
                             g_constant_count);
        g_constant_count++;
    }
    fclose(file);
}

/* ------------------------------------------------------------------ */
/* .enum configs                                                       */
/* ------------------------------------------------------------------ */

/** Map a `.enum` type name onto the pack it resolves against. */
static enum ToriRSServerPackKind
pack_kind_for_type(const char* name)
{
    static const struct
    {
        const char* name;
        enum ToriRSServerPackKind kind;
    } k_map[] = {
        { "npc", TORIRSSERVER_PACK_NPC },         { "namedobj", TORIRSSERVER_PACK_OBJ },
        { "obj", TORIRSSERVER_PACK_OBJ },         { "loc", TORIRSSERVER_PACK_LOC },
        { "seq", TORIRSSERVER_PACK_SEQ },         { "spotanim", TORIRSSERVER_PACK_SPOTANIM },
        { "4_soundeffects", TORIRSSERVER_PACK_SYNTH },
        /* And the same namespace under the name a person writes. A sound param
         * declared `type=int` has no namespace, so `obj_resolve_param_value`
         * guesses — and it guesses seq, then obj, before synth, so
         * `param=attack_sound_stance1,longbow` resolved to the longbow *item*
         * and every bow in the game swung with the wrong noise. */
        { "synth", TORIRSSERVER_PACK_SYNTH },
        { "inv", TORIRSSERVER_PACK_INV },         { "varp", TORIRSSERVER_PACK_VARP },
        { "varbit", TORIRSSERVER_PACK_VARBIT },
        { "interface", TORIRSSERVER_PACK_INTERFACE },
        { "component", TORIRSSERVER_PACK_COMPONENT },
        { "stat", TORIRSSERVER_PACK_STAT },       { "param", TORIRSSERVER_PACK_PARAM },
        { "hitsplat", TORIRSSERVER_PACK_HITSPLAT },
        { "hitmark", TORIRSSERVER_PACK_HITSPLAT }, /* cachepack's (Jagex's) word */
        { "enum", TORIRSSERVER_PACK_ENUM },       { "struct", TORIRSSERVER_PACK_STRUCT },
        { "dbtable", TORIRSSERVER_PACK_DBTABLE }, { "dbrow", TORIRSSERVER_PACK_DBROW },
        { "category", TORIRSSERVER_PACK_CATEGORY },
    };

    for( size_t i = 0; i < sizeof(k_map) / sizeof(k_map[0]); i++ )
    {
        if( strcmp(name, k_map[i].name) == 0 )
            return k_map[i].kind;
    }
    /* `int` and anything else unlisted: the operand is a literal, not a
     * symbol. TORIRSSERVER_PACK_COUNT is the sentinel for that. */
    return TORIRSSERVER_PACK_COUNT;
}

/*
 * Rank-0 enums — the cache export at `configs/all.enum`.
 *
 * ServerScript's `enum` / `enum_getoutputcount` used to answer only for the
 * handful of authored `.enum` files under `server/scripts`. Everything the
 * client's own CS2 reads — Combat Achievement tier lists, the collection-log
 * catalog, the popout strip's panel list (`enum_4067`) — lives here, and
 * content that needed a count hardcoded it. Loading this file is what retires
 * that limitation.
 *
 * The grammar is the authored one (cachepack writes `inputtype=`/`outputtype=`
 * words, `val=<key>,<value>` and `default=` spelled by those types, names for
 * references), so the same reader reads both -- `load_enum_file`.
 */
/* ------------------------------------------------------------------ */
/* Identity kits (configs/all.idk)                                     */
/* ------------------------------------------------------------------ */

/*
 * One number per idk record: `bodypart`.
 *
 * That is the whole of what a server needs from the idk table, and it is what
 * `SETIDKIT` cannot work without: content names a KIT ("give me hair 7") and
 * the appearance array is indexed by WEAR POSITION, so something has to know
 * that hair 7 is a hair. The cache states it and nothing here read it, which is
 * why the opcode was declared and unimplemented.
 *
 * The numbering is the classic one and this cache still uses it (checked by
 * reading `configs/all.idk`: idk 0/10/18/26/33/36/42 are bodyparts 0..6, which
 * are the seven ids `player/configs/appearance.enum` deals a new character):
 *
 *     0 hair   1 jaw   2 torso   3 arms   4 hands   5 legs   6 feet
 *
 * and 7..13 are the same seven for the female body. `ToriRSServer_ContentIdkWearpos`
 * collapses both halves onto the wear position, because the appearance array
 * has one slot for "hair" whichever body is wearing it.
 *
 * A machine dump, like `configs/all.enum` below: no symbol resolution, no
 * `^constants`, and a record this does not understand is skipped rather than
 * refused.
 */
static int8_t* g_idk_bodypart;
static int g_idk_count;

/*
 * bodypart -> wear position, for both bodies.
 *
 * The seven are the reference's own (`Player.body = [0, 10, 18, 26, 33, 36,
 * 42]` paired with `player/configs/appearance.enum`'s keys) and the 239
 * client's own (`Statics.method8884`'s design-part table, transcribed in
 * app.c as `app_ifplayer_design_slots`). Both agree, which is the check worth
 * recording: the two were derived from different sources.
 */
static const int k_idk_bodypart_wearpos[7] = {
    8,  /* 0 hair  */
    11, /* 1 jaw   */
    4,  /* 2 torso */
    6,  /* 3 arms  */
    9,  /* 4 hands */
    7,  /* 5 legs  */
    10, /* 6 feet  */
};

int
ToriRSServer_ContentIdkBodypart(int idk_id)
{
    if( !g_idk_bodypart || idk_id < 0 || idk_id >= g_idk_count )
        return -1;
    return g_idk_bodypart[idk_id];
}

int
ToriRSServer_ContentIdkWearpos(int idk_id)
{
    int part = ToriRSServer_ContentIdkBodypart(idk_id);

    if( part < 0 )
        return -1;
    /* The female half is the same seven parts on the same seven positions. */
    if( part >= 7 )
        part -= 7;
    if( part >= 7 )
        return -1;
    return k_idk_bodypart_wearpos[part];
}

int
ToriRSServer_ContentIdkIsFemale(int idk_id)
{
    return ToriRSServer_ContentIdkBodypart(idk_id) >= 7;
}

/*
 * The same kit on the other body.
 *
 * The reference hand-writes two maps for this (`Player.MALE_FEMALE_MAP` and
 * its inverse) because 2004's two bodies have different kit counts and no
 * arithmetic relation. They DO have a relation, and the cache states it: kits
 * are grouped by body part, so "the third hairstyle" is the third record with
 * `bodypart=0` for a man and the third with `bodypart=7` for a woman. Counting
 * offsets within a part gets the same answer from the data and survives a cache
 * that adds a hairstyle, which a pinned table does not.
 *
 * Where the other body has fewer kits in that part, the last is used rather
 * than nothing: a character mid-design must always be wearing something, and
 * the reference's `?? -1` (fall back to the content default) would silently
 * undo a choice the player had just made on a neighbouring row.
 *
 * Returns the input unchanged when it is already on the wanted body, and -1
 * when the id is not an idk at all.
 */
int
ToriRSServer_ContentIdkForGender(int idk_id, int gender)
{
    int part = ToriRSServer_ContentIdkBodypart(idk_id);
    int want_part;
    int offset = 0;
    int last = -1;
    int seen = 0;

    if( part < 0 )
        return -1;
    if( gender != 0 && gender != 1 )
        return idk_id;
    want_part = (part >= 7 ? part - 7 : part) + (gender == 1 ? 7 : 0);
    if( want_part == part )
        return idk_id;

    for( int id = 0; id < g_idk_count; id++ )
    {
        if( g_idk_bodypart[id] != (int8_t)part )
            continue;
        if( id == idk_id )
            break;
        offset++;
    }
    for( int id = 0; id < g_idk_count; id++ )
    {
        if( g_idk_bodypart[id] != (int8_t)want_part )
            continue;
        last = id;
        if( seen++ == offset )
            return id;
    }
    /* The other body is short of this part: its last kit, or -1 if it has none
     * at all (which sends the encoder back to the content default). */
    return last;
}

/* ------------------------------------------------------------------ */
/* .varp configs                                                       */
/* ------------------------------------------------------------------ */
/* .struct configs                                                     */
/* ------------------------------------------------------------------ */

/*
 * A struct record is a param map and nothing else, so a `.struct` block is
 * `[name]` plus `param=<name>,<value>` rows and nothing else either.
 *
 * Most struct ids are the cache's, and `ToriRSServer_StructInfoLoad` decodes
 * them. Content also ALLOCATES its own (`pack/struct.alloc`, 8000 and up --
 * Mort'ton's `pyre_logs` .. `pyre_teak_logs` and `*_shades`, the gnome
 * cooking trays), and those records exist nowhere but in these text files.
 * Until 2026-09-22 nothing here read them: `struct_param(pyre_logs,
 * pyre_log_output)` answered the declared default `null`, so sacred oil on
 * logs consumed the logs and gave an obj named `item`. A block for a cache id
 * is the same overlay a `.loc` block is -- its rows replace the cache's.
 *
 * Values resolve through the param's DECLARED namespace (`type=namedobj`,
 * `type=loc`, ... in the server `.param` files, walked before this), exactly
 * as `.obj` params do, so `param=pyre_logs_loc,temple_pyre_logs` is a loc id
 * and a misspelling is a load error rather than a zero.
 *
 * The rows live HERE rather than in torirs_server_structinfo.c's cache table:
 * the pack validator links this file without that one, and a separate table
 * does not care whether the cache decode ran before or after this load.
 * `ToriRSServer_StructParam` asks `ToriRSServer_ContentStructParam` first.
 */
static struct ToriRSServerParamTable g_struct_overlay;

const struct ToriRSServerParamRow*
ToriRSServer_ContentStructParam(
    int struct_id,
    int param_id)
{
    return ToriRSServer_ParamTableFind(&g_struct_overlay, struct_id, param_id);
}

/* ------------------------------------------------------------------ */
/* .spawn configs                                                      */
/* ------------------------------------------------------------------ */

/*
 * Where the world's npcs and ground objs stand.
 *
 *     ==== NPC ====
 *     cook                         3208 3213 0
 *
 *     ==== OBJ ====
 *     bones                        3210 3215 0 28
 *
 * Names rather than ids, and absolute world tiles rather than a square-local
 * pair, which is the whole reason this is not a `.jm2` section any more. The
 * `====` markers are kept from that format so the file reads the same way.
 *
 * A name that does not resolve is an error. That is not politeness: a spawn was
 * the last place in this tree carrying a bare id, so it was the last place a
 * cache bump could silently repoint at a different creature — and it already
 * had (`sos_pest_giantspider1`, level 50, standing in the Lumbridge Swamp).
 */
enum SpawnSection
{
    SPAWN_NONE = 0,
    SPAWN_NPC,
    SPAWN_OBJ,
};

static void
load_spawn_config(const char* path)
{
    FILE* file = fopen(path, "rb");
    char raw[512];
    enum SpawnSection section = SPAWN_NONE;
    int line_number = 0;

    if( !file )
        return;
    while( fgets(raw, sizeof(raw), file) )
    {
        char* line = ToriRSServer_ContentCleanLine(raw);
        char name[128];
        int x;
        int z;
        int level;
        int count;
        int id;
        int fields;

        line_number++;
        if( !*line )
            continue;

        if( strncmp(line, "====", 4) == 0 )
        {
            if( strstr(line, "NPC") )
                section = SPAWN_NPC;
            else if( strstr(line, "OBJ") )
                section = SPAWN_OBJ;
            else
            {
                CONTENT_ERROR("%s:%d: `%s` — a .spawn file has ==== NPC ==== and "
                              "==== OBJ ==== sections and nothing else\n",
                              path, line_number, line);
                section = SPAWN_NONE;
            }
            continue;
        }
        if( section == SPAWN_NONE )
        {
            CONTENT_ERROR("%s:%d: `%s` before any ==== NPC ==== / ==== OBJ ==== header\n",
                          path, line_number, line);
            continue;
        }

        count = 1;
        fields = sscanf(line, "%127s %d %d %d %d", name, &x, &z, &level, &count);
        if( fields < 4 )
        {
            CONTENT_ERROR("%s:%d: expected `<name> <x> <z> <level>%s`, got `%s`\n",
                          path, line_number,
                          section == SPAWN_OBJ ? " [count]" : "", line);
            continue;
        }
        if( section == SPAWN_NPC && fields > 4 )
        {
            CONTENT_ERROR("%s:%d: an npc spawn has no count\n", path, line_number);
            continue;
        }
        if( level < 0 || level > 3 )
        {
            CONTENT_ERROR("%s:%d: level %d is not 0..3\n", path, line_number, level);
            continue;
        }
        if( count < 1 )
        {
            CONTENT_ERROR("%s:%d: count %d is not at least 1\n", path, line_number, count);
            continue;
        }

        /* `#<id>` is the numeric escape the MAP EDITOR writes: it holds cache
         * display names, not pack symbols, and a display name here would
         * resolve to nothing -- or to the wrong record, which is worse. The
         * escape is explicit (a leading `#`) rather than "digits mean id", so
         * a pack symbol that happens to be numeric can never be shadowed. */
        if( name[0] == '#' )
        {
            char* end = NULL;
            id = (int)strtol(name + 1, &end, 10);
            if( !end || *end != '\0' || id < 0 )
            {
                CONTENT_ERROR("%s:%d: `%s` is not a valid #id\n", path, line_number, name);
                continue;
            }
        }
        else
        {
            id = ToriRSServer_ContentSymbol(
                section == SPAWN_NPC ? TORIRSSERVER_PACK_NPC : TORIRSSERVER_PACK_OBJ, name);
            if( id < 0 )
            {
                CONTENT_ERROR("%s:%d: `%s` is not in configs/all.%s.compack\n", path, line_number,
                              name, section == SPAWN_NPC ? "npc" : "obj");
                continue;
            }
        }

        if( section == SPAWN_NPC )
        {
            g_npc_spawns = grow(g_npc_spawns, &g_npc_spawn_capacity, g_npc_spawn_count,
                                sizeof(*g_npc_spawns));
            g_npc_spawns[g_npc_spawn_count].npc_id = id;
            g_npc_spawns[g_npc_spawn_count].x = x;
            g_npc_spawns[g_npc_spawn_count].z = z;
            g_npc_spawns[g_npc_spawn_count].level = level;
            g_npc_spawn_count++;
        }
        else
        {
            g_obj_spawns = grow(g_obj_spawns, &g_obj_spawn_capacity, g_obj_spawn_count,
                                sizeof(*g_obj_spawns));
            g_obj_spawns[g_obj_spawn_count].obj_id = id;
            g_obj_spawns[g_obj_spawn_count].count = count;
            g_obj_spawns[g_obj_spawn_count].x = x;
            g_obj_spawns[g_obj_spawn_count].z = z;
            g_obj_spawns[g_obj_spawn_count].level = level;
            g_obj_spawn_count++;
        }
    }
    fclose(file);
}

/* ------------------------------------------------------------------ */
/* Param types                                                         */
/* ------------------------------------------------------------------ */

/*
 * What each param id's value *is*, from `configs/all.param`.
 *
 * The reason this exists is `oc_param` and its three siblings, which the
 * opcode meta marks `runtime_typed`: the declared type of the param decides
 * whether the result lands on the VM's int stack or its string stack. Without
 * it the opcode cannot be implemented at all, only guessed at — which is why
 * `docs/osrs230_mockserver.md` §3.13d listed the whole family as blocked on
 * data rather than on effort.
 *
 * It also makes `configs/` stop being write-only. `CONTENT_ARCHITECTURE.md`
 * §3.5's complaint was that `configs/all.<type>` is 8.6 MB of text whose only
 * consumer is `cachepack pack`; this is the first runtime reader of any of it.
 *
 * The type is one character, the cache's own code. Only `s` is a string; every
 * other letter is some flavour of integer as far as the stacks are concerned
 * (`i` plain, `d` a coordinate, `o` an obj, `1` a boolean, and so on), and the
 * VM has one int stack for all of them.
 */
static char* g_param_types;
static int g_param_type_count;
/* The `default=` of each param. The reference relies on these: an obj that
 * does not carry the row answers with the param's declared default
 * (LostCity's ObjConfigOps pushes `paramType.defaultInt`), and 365 of the 469
 * declared defaults are -1 — "no id", which no zero can spell because 0 is a
 * real obj. A param that declares no default is 0, matching the cache decode:
 * `RSCache_Dat2ConfigParamInit` leaves `default_int` on the zeroed record.
 * There is deliberately no "undeclared" sentinel — -1 is the most-declared
 * default of all, so any in-band marker would collide with real data. */
static int* g_param_defaults;

int
ToriRSServer_ContentParamDefault(int param_id)
{
    if( param_id < 0 || param_id >= g_param_type_count || !g_param_defaults )
        return 0;
    return g_param_defaults[param_id];
}

char
ToriRSServer_ContentParamType(int param_id)
{
    if( param_id < 0 || param_id >= g_param_type_count )
        return 0;
    return g_param_types[param_id];
}

/* ------------------------------------------------------------------ */
/* Walking the tree                                                    */
/* ------------------------------------------------------------------ */

static int
has_suffix(
    const char* name,
    const char* suffix)
{
    size_t name_length = strlen(name);
    size_t suffix_length = strlen(suffix);

    return name_length >= suffix_length &&
           strcmp(name + name_length - suffix_length, suffix) == 0;
}

static int
dirent_name_compare(
    const void* a,
    const void* b)
{
    const struct dirent* const* da = a;
    const struct dirent* const* db = b;
    return strcmp((*da)->d_name, (*db)->d_name);
}

/* mingw-w64's dirent.h implements opendir/readdir but not the BSD/glibc
 * scandir/alphasort extensions, so walk the directory by hand and sort the
 * results ourselves -- portable across the unix and win32 builds. */
static int
ToriRSServer_Scandir(
    const char* dir,
    struct dirent*** out_entries)
{
    DIR* d = opendir(dir);
    struct dirent** entries = NULL;
    int count = 0;
    int capacity = 0;
    struct dirent* ent;

    if( !d )
        return -1;

    while( (ent = readdir(d)) != NULL )
    {
        /* `struct dirent` is a variable-length record: `d_name` is declared at
         * the struct's maximum but the entry readdir hands back is only
         * `d_reclen` bytes long, and on macOS that buffer is a heap allocation
         * sized to exactly that. A whole-struct assignment (`*dst = *ent`)
         * therefore reads up to sizeof(struct dirent) - d_reclen bytes past it
         * -- ASan reports it as a 1048-byte heap-buffer-overflow READ at boot.
         * Copy the record's own length into a zeroed full-size slot instead. */
        size_t reclen = (size_t)ent->d_reclen;
        struct dirent* slot;

        if( reclen == 0 || reclen > sizeof(*slot) )
            reclen = sizeof(*slot);
        if( count == capacity )
        {
            int const grown = capacity ? capacity * 2 : 16;
            struct dirent** resized =
                realloc(entries, (size_t)grown * sizeof(*entries));
            if( !resized )
                break;
            entries = resized;
            capacity = grown;
        }
        slot = calloc(1, sizeof(*slot));
        assert(slot);
        memcpy(slot, ent, reclen);
        /* A truncated copy must still be a valid C string for the sort. */
        ((char*)slot)[sizeof(*slot) - 1] = '\0';
        entries[count] = slot;
        count++;
    }
    closedir(d);

    if( count > 0 )
        qsort(entries, (size_t)count, sizeof(*entries), dirent_name_compare);
    *out_entries = entries;
    return count;
}

/**
 * Recursively load every matching config in deterministic path order.
 *
 * Sorted via `ToriRSServer_Scandir`, not `readdir` walked as it comes: `readdir` makes no
 * ordering promise at all, it hands back entries in whatever order the
 * filesystem's directory storage happens to hold them. Sorting makes overlay
 * order reproducible; NPCs additionally split generated baselines from authored
 * overlays at the call site below.
 */
static void
walk_configs(
    const char* dir,
    const char* suffix,
    void (*load)(const char*))
{
    struct dirent** entries;
    char path[1024];
    int count = ToriRSServer_Scandir(dir, &entries);

    if( count < 0 )
        return;
    for( int i = 0; i < count; i++ )
    {
        if( entries[i]->d_name[0] != '.' )
        {
            snprintf(path, sizeof(path), "%s/%s", dir, entries[i]->d_name);
            if( ToriRSServer_PathIsDir(path) )
                walk_configs(path, suffix, load);
            else if( has_suffix(entries[i]->d_name, suffix) )
                load(path);
        }
        free(entries[i]);
    }
    free(entries);
}

/*
 * The multi-combat zone set, from `maps/multiway.csv`.
 *
 * Where LostCity puts it: `content/maps/multiway.csv`, read by `GameMap.ts`
 * into a `Set<number>` of zone indices and answered by `GameMap.isMulti`. Engine
 * code, content data — so the file ports across and only the reader is written
 * here.
 *
 * A *set of zones*, not a list of rectangles, because that is the shape of the
 * fact: a zone is either multi or it is not, the wilderness edge is ragged, and
 * 4,697 zones do not reduce to a handful of boxes. Kept sorted so a query is a
 * binary search — `player_combat.rs2` asks once per swing, per fighter.
 */
static int* g_multiway;
static int g_multiway_count;
static int g_multiway_capacity;

/*
 * Same packing as `ToriRSServer_ZoneIndex` / the reference's `ZoneMap.zoneIndex`.
 * Kept local so `ToriRSServer_Pack` need not link the zone module.
 */
static int
content_zone_index(
    int x,
    int z,
    int level)
{
    return ((x >> 3) & 0x7ff) | (((z >> 3) & 0x7ff) << 11) | ((level & 3) << 22);
}

static int
compare_zone_index(
    const void* a,
    const void* b)
{
    int left = *(const int*)a;
    int right = *(const int*)b;

    return left < right ? -1 : (left > right ? 1 : 0);
}

static void
load_multiway(const char* path)
{
    FILE* file = fopen(path, "rb");
    char line[256];

    if( !file )
        return;
    while( fgets(line, sizeof(line), file) )
    {
        int level, map_x, map_z, local_x, local_z;

        if( sscanf(line, "%d_%d_%d_%d_%d", &level, &map_x, &map_z, &local_x, &local_z) != 5 )
            continue; /* a comment or a blank line — the reference skips both. */
        /*
         * The reference warns on a line that is not zone-aligned and then files
         * it anyway, which files the *containing* zone. Reproduced by rounding
         * rather than by warning: the zone index shifts the tile down to its
         * zone, so an unaligned line lands where the reference puts it, and a
         * warning nobody can act on in a ported data file is noise.
         */
        if( g_multiway_count == g_multiway_capacity )
        {
            int capacity = g_multiway_capacity ? g_multiway_capacity * 2 : 1024;
            int* grown = realloc(g_multiway, (size_t)capacity * sizeof(*grown));

            assert(grown);
            g_multiway = grown;
            g_multiway_capacity = capacity;
        }
        g_multiway[g_multiway_count++] =
            content_zone_index((map_x << 6) + local_x, (map_z << 6) + local_z, level);
    }
    fclose(file);
    qsort(g_multiway, (size_t)g_multiway_count, sizeof(*g_multiway), compare_zone_index);
}

int
ToriRSServer_ContentMultiway(
    int x,
    int z,
    int level)
{
    int key = content_zone_index(x, z, level);
    int low = 0;
    int high = g_multiway_count - 1;

    while( low <= high )
    {
        int mid = low + (high - low) / 2;

        if( g_multiway[mid] == key )
            return 1;
        if( g_multiway[mid] < key )
            low = mid + 1;
        else
            high = mid - 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Load / free                                                         */
/* ------------------------------------------------------------------ */

/*
 * What an npc is before any content describes it.
 *
 * Numbers only. The four *ids* that used to be here — `human_unarmedpunch`,
 * `human_unarmedblock`, `human_death`, `bones` — were the engine naming content
 * it does not own, and they are now the `[default]` block of a `.npc` config
 * (`server/scripts/general/configs/npc_default.npc`). An engine holding no ids
 * is the whole architecture; four of them hiding in an initialiser is exactly
 * how that erodes.
 *
 * The animation fields start at -1, which is "play nothing", so a tree that
 * declares no `[default]` block degrades to silent npcs rather than to npcs
 * animating with sequence 0.
 */
static void
npc_def_builtin(struct ToriRSServerNpcDef* out)
{
    memset(out, 0, sizeof(*out));
    out->npc_id = -1;
    out->hitpoints = 10;
    out->attack = 1;
    out->strength = 1;
    out->defence = 1;
    out->respawnrate = 25;
    /*
     * How long the corpse lies there once the death animation has *started* —
     * `npc_delay(1)` in `[proc,npc_death]`, which is `tick + 1 + 1` and so two
     * ticks. It used to be 3 and to be measured from the killing blow, which is
     * a different thing: the animation now starts a tick later (and later still
     * if the npc was mid-step), so the same 3 ticks would have been a different
     * death from the reference's.
     *
     * Not 0, which would despawn the corpse on the tick the animation started
     * and eat it entirely. A tree that states `death_delay` overrides it; this
     * is only what a tree stating nothing degrades to.
     */
    out->death_delay = 2;
    out->attackrate = 4;
    out->attackrange = 1;
    /*
     * The reference's NpcType defaults, and the pair that decides how far a
     * monster will roam and follow: 5 tiles of wander from where it spawned,
     * and 7 of leash — `maxrange` defaults to `wanderrange + 2` in LostCity's
     * `NpcType`, which is where both numbers come from.
     *
     * `wanderrange` was missing while `maxrange` was here, and the two are a
     * pair. The consequence was invisible for as long as the roster was the 63
     * hand-authored Lumbridge npcs, because their `.npc` blocks state it: an
     * npc nothing describes got `wanderrange = 0`, which
     * `ToriRSServer_WorldNpcDefaultMode` reads as MODE_NONE and the roam block
     * skips outright. On a world roster of 23,139 npcs, of which 22 files state
     * a wanderrange, that is "most npcs never move".
     */
    out->wanderrange = 5;
    out->maxrange = 7;
    out->givechase = 1;
    /* Everything fights back unless it says otherwise. */
    out->retaliate = 1;
    /* Single-way unless the zone set or the record says otherwise. See the
     * field: the zone set describes 2004, so every post-2004 multi-combat
     * encounter states this. */
    out->forcemulti = 0;
    /* Unstated, so the encoder uses the standard bar — see the field's note. */
    out->healthbar = TORIRSSERVER_NPC_HEALTHBAR_UNSET;
    /* Everything that can be hit shows the number unless it says otherwise. */
    out->hitsplat = 1;
    /* LostCity NpcType defaults: blockwalk=NPC, no sight block, normal move. */
    out->blockwalk = 1;
    out->blocksight = 0;
    out->moverestrict = 0;
    out->nomove = 0;
    out->turnspeed = -1; /* unstated: defer to the cache record */
    out->facing = -1;    /* unstated: spawn facing south */
    out->damagetype = TORIRSSERVER_DAMAGE_CRUSH;
    out->attack_anim = -1;
    out->defend_anim = -1;
    out->death_anim = -1;
    out->death_drop = -1;
    /* Silent unless something states otherwise. Sound effect 0 is a real clip,
     * so this cannot be 0 — see the field comment in torirs_server_content.h. */
    out->attack_sound = -1;
    out->defend_sound = -1;
    out->death_sound = -1;
    out->defaultmode = TORIRSSERVER_NPCMODE_NONE;
    /* Unstated, so an npc with no block keeps the radius-derived default. */
    out->defaultmode_stated = 0;
}

static void
init_defaults(void)
{
    npc_def_builtin(&g_npc_default);
}

/*
 * A field register, held to itself and to the server's band bindings.
 *
 * Only a register that declares a band for a type the server binds is held to the
 * bindings: a tree with no `fields/<type>.ini` (or one that declares no server
 * opcodes) has no band for cachepack to write, and the text overlays stand — the
 * documented fallback. A tree that does declare one has made it the contract, and
 * a binding the register does not carry, a member too narrow for its field, or a
 * band field nothing receives is a startup error rather than a value that quietly
 * lands nowhere.
 */
static int band_decode_whole(const char* type_name, const struct RSCache_Register* fields,
                             struct RSCache_BandRecord* record, void* object, const uint8_t* band,
                             uint32_t size, int id);

/* ------------------------------------------------------------------ */
/* Config records from the server pack                                 */
/* ------------------------------------------------------------------ */

/*
 * Everything below reads the pack's client records (the merge of configs/ and
 * server/scripts, encoded by cachepack with the client codec) and the bands beside
 * them. No config text is read at run time: what a `.enum`, `.param`, `.idk`,
 * `.varp` or `.inv` block says reaches the server because cachepack merged it into
 * the record the pack holds.
 */

/** The pack kind a value of type character `ch` names, through the one
 *  ScriptVarType table (rscache_valuetype.h), or TORIRSSERVER_PACK_COUNT. */
static enum ToriRSServerPackKind
pack_kind_for_char(int ch)
{
    const struct RSCache_ValueType* type = RSCache_ValueTypeOfChar(ch);

    return type ? pack_kind_for_type(type->word) : TORIRSSERVER_PACK_COUNT;
}

static int
load_param_types_pack(struct RSCache_ServerPack* pack)
{
    struct ToriRSServerKindRecords records;

    if( !ToriRSServer_ServPackKindLoad(pack, RSCACHE_DAT2_CONFIG_KIND_PARAMS, &records) )
        return -1;
    g_param_type_count = ToriRSServer_ServPackKindIdBound(&records);
    if( g_param_type_count < ToriRSServer_ContentSymbolCount(TORIRSSERVER_PACK_PARAM) )
        g_param_type_count = ToriRSServer_ContentSymbolCount(TORIRSSERVER_PACK_PARAM);
    g_param_types = (char*)calloc((size_t)g_param_type_count + 1, 1);
    assert(g_param_types);
    g_param_defaults = (int*)calloc((size_t)g_param_type_count + 1, sizeof(int));
    assert(g_param_defaults);
    for( int i = 0; i < records.count; i++ )
    {
        struct RSCache_Dat2ConfigParam param;
        int id = records.ids[i];

        memset(&param, 0, sizeof(param));
        RSCache_Dat2ConfigParamDecodeInplace(&param, records.files[i], (int)records.sizes[i]);
        g_param_types[id] = param.type;
        g_param_defaults[id] = param.default_int;
        RSCache_Dat2ConfigParamFreeInplace(&param);
    }
    ToriRSServer_ServPackKindFree(&records);
    return g_param_type_count;
}

static int
load_enums_pack(struct RSCache_ServerPack* pack)
{
    struct ToriRSServerKindRecords records;
    int loaded = 0;

    if( !ToriRSServer_ServPackKindLoad(pack, RSCACHE_DAT2_CONFIG_KIND_ENUM, &records) )
        return -1;
    for( int i = 0; i < records.count; i++ )
    {
        struct RSCache_Dat2ConfigEnum entry;
        struct ToriRSServerEnumDef* def;
        const char* symbol = ToriRSServer_ContentSymbolName(TORIRSSERVER_PACK_ENUM, records.ids[i]);
        char fallback[32];

        memset(&entry, 0, sizeof(entry));
        RSCache_Dat2ConfigEnumDecodeInplace(&entry, records.files[i], (int)records.sizes[i]);
        if( !symbol )
        {
            snprintf(fallback, sizeof(fallback), "enum_%d", records.ids[i]);
            symbol = fallback;
        }
        g_enum_defs =
            grow(g_enum_defs, &g_enum_def_capacity, g_enum_def_count, sizeof(*g_enum_defs));
        def = &g_enum_defs[g_enum_def_count++];
        memset(def, 0, sizeof(*def));
        def->symbol = strdup(symbol);
        assert(def->symbol);
        def->input_kind = pack_kind_for_char((unsigned char)entry.input_type);
        def->output_kind = pack_kind_for_char((unsigned char)entry.output_type);
        def->input_is_string = entry.input_type == 's';
        def->output_is_string = entry.output_type == 's' || entry.output_is_string;
        def->default_int = entry.default_int;
        def->default_text = "null";
        if( def->output_is_string && entry.default_string )
        {
            def->default_text = strdup(entry.default_string);
            assert(def->default_text);
        }
        if( entry.count > 0 )
        {
            def->values = (struct ToriRSServerEnumValue*)calloc((size_t)entry.count,
                                                                sizeof(*def->values));
            assert(def->values);
            def->capacity = entry.count;
        }
        for( int v = 0; v < entry.count; v++ )
        {
            def->values[v].key = entry.keys[v];
            if( def->output_is_string )
            {
                def->values[v].text = strdup(entry.string_values && entry.string_values[v]
                                                 ? entry.string_values[v]
                                                 : "");
                assert(def->values[v].text);
            }
            else
                def->values[v].value = entry.int_values ? entry.int_values[v] : 0;
        }
        def->count = entry.count;
        RSCache_Dat2ConfigEnumFreeInplace(&entry);
        loaded++;
    }
    ToriRSServer_ServPackKindFree(&records);
    return loaded;
}

static int
load_idk_pack(struct RSCache_ServerPack* pack)
{
    struct ToriRSServerKindRecords records;
    int loaded = 0;

    if( !ToriRSServer_ServPackKindLoad(pack, RSCACHE_DAT2_CONFIG_KIND_IDENTKIT, &records) )
        return -1;
    g_idk_count = ToriRSServer_ServPackKindIdBound(&records);
    if( g_idk_count <= 0 )
    {
        ToriRSServer_ServPackKindFree(&records);
        return 0;
    }
    g_idk_bodypart = (int8_t*)malloc((size_t)g_idk_count);
    assert(g_idk_bodypart);
    memset(g_idk_bodypart, -1, (size_t)g_idk_count);
    for( int i = 0; i < records.count; i++ )
    {
        struct RSCache_Dat2ConfigIdk idk;

        memset(&idk, 0, sizeof(idk));
        RSCache_Dat2ConfigIdkDecodeInplace(&idk, (char*)records.files[i], (int)records.sizes[i]);
        if( RSCache_PresenceHas(&idk.present, RSCACHE_IDK_FIELD_BODY_PART) )
        {
            g_idk_bodypart[records.ids[i]] = (int8_t)idk.body_part_id;
            loaded++;
        }
        free(idk.model_ids);
        free(idk.recolors_from);
        free(idk.recolors_to);
        free(idk.retextures_from);
        free(idk.retextures_to);
    }
    ToriRSServer_ServPackKindFree(&records);
    return loaded;
}

static void
varp_load_band(
    void* context,
    int id,
    const uint8_t* band,
    uint32_t size)
{
    int* failed = (int*)context;
    struct ToriRSServerVarpDef* def;
    struct RSCache_BandRecord record;

    g_varp_defs = grow(g_varp_defs, &g_varp_def_capacity, g_varp_def_count, sizeof(*g_varp_defs));
    def = &g_varp_defs[g_varp_def_count++];
    memset(def, 0, sizeof(*def));
    def->varp_id = id;
    def->symbol = ToriRSServer_ContentSymbolName(TORIRSSERVER_PACK_VARP, id);
    def->clientcode = -1;
    if( !band_decode_whole("varp", &g_varp_fields, &record, def, band, size, id) )
        (*failed)++;
    RSCache_BandRecordFree(&record);
}

/** Varp defs: one per varp the band states a server field for, with the client
 *  record's `clientcode`. */
static int
load_varps_pack(struct RSCache_ServerPack* pack)
{
    struct ToriRSServerKindRecords records;
    int failed = 0;
    int bands = ToriRSServer_ServPackEachBand(pack, RSCACHE_DAT2_CONFIG_KIND_VARPLAYER,
                                              varp_load_band, &failed);

    if( bands < 0 || failed )
        return -1;
    if( !ToriRSServer_ServPackKindLoad(pack, RSCACHE_DAT2_CONFIG_KIND_VARPLAYER, &records) )
        return -1;
    for( int i = 0; i < g_varp_def_count; i++ )
    {
        for( int r = 0; r < records.count; r++ )
        {
            struct RSCache_Dat2ConfigVarplayer varp;

            if( records.ids[r] != g_varp_defs[i].varp_id )
                continue;
            memset(&varp, 0, sizeof(varp));
            RSCache_Dat2ConfigVarplayerDecodeInplace(&varp, records.files[r], (int)records.sizes[r]);
            if( RSCache_PresenceHas(&varp.present, RSCACHE_VARP_FIELD_CLIENTCODE) )
                g_varp_defs[i].clientcode = varp.clientcode;
            break;
        }
    }
    ToriRSServer_ServPackKindFree(&records);
    return bands;
}

static void
inv_load_band(
    void* context,
    int id,
    const uint8_t* band,
    uint32_t size)
{
    int* failed = (int*)context;
    struct ToriRSServerShopDef* def = ToriRSServer_ShopDefBegin(id);
    struct RSCache_BandRecord record;

    if( !band_decode_whole("inv", &g_inv_fields, &record, def, band, size, id) )
        (*failed)++;
    RSCache_BandRecordFree(&record);
}

/** Shop definitions: one per inv the band states a server field for. The size
 *  is the client record's, which the bank table reads from the same pack. */
static int
load_shops_pack(struct RSCache_ServerPack* pack)
{
    int failed = 0;
    int bands;

    ToriRSServer_ShopReset();
    bands = ToriRSServer_ServPackEachBand(pack, RSCACHE_DAT2_CONFIG_KIND_INV, inv_load_band,
                                          &failed);
    return bands < 0 || failed ? -1 : bands;
}

static int
content_fields_check(const struct RSCache_Register* fields)
{
    const struct ToriRSServerBandType* type = ToriRSServer_ServerTypeFor(fields->type);
    int problems;

    if( type && fields->band_count > 0 )
        problems = ToriRSServer_ServerCheck(type, fields);
    else
        problems = RSCache_RegisterCheck(fields);
    if( problems )
        CONTENT_ERROR("fields/%s.ini: %d problem(s) in the field register or between it and "
                      "this server's band bindings; see above\n",
                      fields->type, problems);
    return problems;
}

int
ToriRSServer_ContentLoad(
    const char* dir,
    struct RSCache_ServerPack* pack)
{
    struct ContentRegister reg;
    char path[1024];
    DIR* probe;
    int symbols = 0;

    ToriRSServer_ContentFree();

    probe = opendir(dir);
    if( !probe )
    {
        /* Same fallback as the cache loaders: `make` leaves the binary in src/
         * but the tree is addressed from the repo root. Not finding it is not
         * an error — the engine defaults keep the mock running. */
        static char parent[1024];

        snprintf(parent, sizeof(parent), "../%s", dir);
        probe = opendir(parent);
        if( !probe )
        {
            fprintf(stderr, "torirsserver: no content tree at %s — engine defaults only\n", dir);
            init_defaults();
            return 0;
        }
        dir = parent;
    }
    closedir(probe);

    /*
     * The register, then one file per namespace.
     *
     * `ContentRegister_Load` overlays the tree's `content.ini` onto the built-in
     * defaults and never fails, so a tree without one still boots. What *does*
     * refuse to boot is a tree whose declaration contradicts itself — a namespace
     * claiming `names = cache` with no gameval archive behind it tells cachepack it
     * may rewrite a hand-written file, and that has already cost this tree
     * `configs/all.param.compack`'s 58-line header once.
     */
    ContentRegister_Load(&reg, dir);
    ContentFields_Load(&g_npc_fields, dir, "npc");
    ContentFields_Load(&g_loc_fields, dir, "loc");
    ContentFields_Load(&g_obj_fields, dir, "obj");
    ContentFields_Load(&g_varp_fields, dir, "varp");
    ContentFields_Load(&g_inv_fields, dir, "inv");
    ContentFields_Load(&g_dbtable_fields, dir, "dbtable");
    g_band_register_problems = content_fields_check(&g_npc_fields) +
                               content_fields_check(&g_loc_fields) +
                               content_fields_check(&g_obj_fields) +
                               content_fields_check(&g_varp_fields) +
                               content_fields_check(&g_inv_fields) +
                               content_fields_check(&g_dbtable_fields);
    if( ContentRegister_Validate(&reg) != 0 )
        CONTENT_ERROR("content.ini contradicts the gameval evidence; see above\n");

    for( int kind = 0; kind < TORIRSSERVER_PACK_COUNT; kind++ )
    {
        /*
         * Two levels of index, two places.
         *
         * A config record is a *file* of a config archive — `[swarm_walk]` is file 0
         * of archive 12 — so what binds `0=swarm_walk` is a member index and lives
         * beside the archive it indexes, as `configs/all.seq.compack`. `pack/` holds
         * the other level: one file per cache index, naming that index's archives.
         *
         * Both are `id=name` and used to share a name and a directory, which is why
         * the distinction had to be known rather than read.
         */
        const char* name = pack_kind_name((enum ToriRSServerPackKind)kind);

        if( kind == TORIRSSERVER_PACK_COMPONENT )
            continue; /* composed below, from the interfaces and their compacks */
        if( pack_kind_is_config((enum ToriRSServerPackKind)kind) )
            snprintf(path, sizeof(path), "%s/configs/all.%s.compack", dir, name);
        else
            snprintf(path, sizeof(path), "%s/pack/%s.pack", dir, name);
        symbols += pack_load(&g_packs[kind], path, 0);
        /* The server's allocation ledger, layered into the same namespace. The
         * compack is the cache's member index and `pack/<ns>.alloc` holds the
         * ids ss_allocate.py handed out past its high-water mark — one
         * namespace, two files, so a rename in either is caught by
         * `validate_symbols` exactly as a duplicate inside one file is. Absent
         * for most kinds, and `pack_load` reads absence as zero symbols. */
        snprintf(path, sizeof(path), "%s/pack/%s.alloc", dir, name);
        symbols += pack_load(&g_packs[kind], path, 1);
    }
    /* Same imported namespace layers passed to sscompile in the build. */
    symbols += load_ported_pack_symbols(dir);
    /* After the loop: it needs the interface pack to already be loaded. */
    symbols += load_component_symbols_from_root(dir, 0);
    symbols += load_ported_component_symbols(dir);

    /* Packs are immutable from here through boot and runtime. Keep their
     * insertion-order arrays for walking and diagnostics, and build sorted
     * pointer views for the far more frequent name/id lookups. */
    for( int kind = 0; kind < TORIRSSERVER_PACK_COUNT; kind++ )
        pack_build_indexes(&g_packs[kind]);

    /* A symbol table that answers a name two ways is refused here rather than
     * resolved silently later. */
    validate_symbols(&reg);

    /*
     * A varp the per-player array cannot hold, refused at boot.
     *
     * `ToriRSServerPlayer.varps` is a flat array and its size is a constant, so the
     * tree can outgrow it — and it has: the `%com_*` combat block reached
     * 6223 against an array of 6216, and the seven ids over the end were not
     * dropped quietly, they aborted `[proc,player_combat_stat]` on every npc
     * swing. From the outside that is "npcs do not fight back", with the real
     * message buried in a script trace nobody reads during a fight.
     *
     * Allocating a varp is content's to do, so this does not cap it — it says
     * exactly which line of the engine has to move, and it says so once at
     * boot instead of once per swing.
     */
    {
        int highest = -1;
        int count = ToriRSServer_ContentSymbolWalk(TORIRSSERVER_PACK_VARP, -1, NULL, NULL);

        for( int i = 0; i < count; i++ )
        {
            int id = -1;

            ToriRSServer_ContentSymbolWalk(TORIRSSERVER_PACK_VARP, i, &id, NULL);
            if( id > highest )
                highest = id;
        }
        if( highest >= TORIRSSERVER_VARP_COUNT )
            CONTENT_ERROR(
                "the tree declares varp %d and ToriRSServerPlayer.varps holds %d — raise "
                "TORIRSSERVER_VARP_SERVER_HEADROOM by at least %d (torirs_server.h)\n",
                highest,
                TORIRSSERVER_VARP_COUNT,
                highest - TORIRSSERVER_VARP_COUNT + 1);
    }

    /* Per-NPC and world variables are authored allocation ledgers too. Refuse
     * the tree at boot if either one outgrows the array its bytecode indexes. */
    {
        int highest = -1;
        int count = ToriRSServer_ContentSymbolWalk(TORIRSSERVER_PACK_VARN, -1, NULL, NULL);

        for( int i = 0; i < count; i++ )
        {
            int id = -1;

            ToriRSServer_ContentSymbolWalk(TORIRSSERVER_PACK_VARN, i, &id, NULL);
            if( id > highest )
                highest = id;
        }
        if( highest >= TORIRSSERVER_NPC_VAR_MAX )
            CONTENT_ERROR(
                "the tree declares varn %d and ToriRSServerNpc.script_vars holds %d\n",
                highest, TORIRSSERVER_NPC_VAR_MAX);
    }
    {
        int highest = -1;
        int count = ToriRSServer_ContentSymbolWalk(TORIRSSERVER_PACK_VARS, -1, NULL, NULL);

        for( int i = 0; i < count; i++ )
        {
            int id = -1;

            ToriRSServer_ContentSymbolWalk(TORIRSSERVER_PACK_VARS, i, &id, NULL);
            if( id > highest )
                highest = id;
        }
        if( highest >= TORIRSSERVER_VARS_COUNT )
            CONTENT_ERROR(
                "the tree declares vars %d and ToriRSServer.vars holds %d\n",
                highest, TORIRSSERVER_VARS_COUNT);
    }

    /* After the packs (a default names its animations by symbol) and before the
     * configs (each block starts from a copy of it). */
    init_defaults();

    /*
     * The config records: param types, enums, identity kits, varps and shops, all
     * from the server pack (merged by cachepack from configs/ and server/scripts),
     * never from config text. An obj's overlays and a struct's are in the pack's
     * client records too (ToriRSServer_ObjInfoLoad, ToriRSServer_StructInfoLoad);
     * an obj's skill requirements are its band (ToriRSServer_ContentLoadPack).
     * Constants are not config records: the scripts' own `^name` table.
     */
    snprintf(path, sizeof(path), "%s/server/scripts", dir);
    walk_configs(path, ".constant", load_constant_config);
    {
        int param_types = load_param_types_pack(pack);
        int enums = load_enums_pack(pack);
        int idks = load_idk_pack(pack);
        int varps = load_varps_pack(pack);
        int shops = load_shops_pack(pack);

        if( param_types < 0 || enums < 0 || idks < 0 || varps < 0 || shops < 0 )
            CONTENT_ERROR("server pack %s: a config archive does not validate — rebuild it with "
                          "`%s`\n",
                          pack->dir, TORIRSSERVER_SERVPACK_FIX);
        fprintf(stderr,
                "torirsserver: from the server pack: %d param types, %d enums, %d idk body "
                "parts, %d varp defs, %d shop defs\n",
                param_types, enums, idks, varps, shops);
    }
    /* After the configs: a spawn names an npc or an obj, and the name has to
     * resolve against the packs the loader has already read. */
    walk_configs(path, ".spawn", load_spawn_config);

    snprintf(path, sizeof(path), "%s/maps/multiway.csv", dir);
    load_multiway(path);

    {
        int requires_total = 0;
        int requires_from_cache = 0;

        ToriRSServer_ObjRequireCounts(&requires_total, &requires_from_cache);
        fprintf(stderr,
                "torirsserver: content loaded (%d symbols, %d constants, "
                "%d varp defs, %d equip reqs (%d from the cache), %d npc spawns, "
                "%d obj spawns%s)\n",
                symbols, g_constant_count, g_varp_def_count,
                requires_total, requires_from_cache, g_npc_spawn_count, g_obj_spawn_count,
                g_errors ? ", WITH ERRORS" : "");
    }
    return symbols;
}

/* ------------------------------------------------------------------ */
/* npc and loc definitions, from server/pack                           */
/* ------------------------------------------------------------------ */

/*
 * The server half of an npc or loc record is its band in `server/pack`, and
 * nothing else: no `.npc` or `.loc` text is read at run time.
 *
 * A record's two halves come from one source. The client half — name, ops,
 * category, params, size, the multinpc table — is the pack's client-record
 * archive, decoded into `ToriRSServer_NpcInfo` / `ToriRSServer_LocInfo` by the
 * same client codec a cache read uses (step 1 of the boot). The server half is
 * the band beside it, applied here through the register bindings
 * (`torirs_server_servercodec.c`). cachepack writes both from one merge of the
 * whole tree, so the text-path derivations live in exactly two places now:
 *
 *   - the BINDINGS, for what a stated field means to its own record
 *     (`hitpoints` marks the block a combat block, `moverestrict=nomove`
 *     collapses to `nomove`, `defaultmode` is stated even when it is 0, a
 *     `patrol` list becomes the route), and
 *   - the REGISTER, read here, for what a field means beyond its member: a
 *     `text = param` field is also filed under its param id for `npc_param`,
 *     exactly as a `param=` line was; a `param = <name>` loc field lands in the
 *     loc param table `loc_param` reads.
 *
 * `[default]` is the type's own band at (192, config kind) and seeds every npc
 * def exactly as the text block did: built-in numbers, the `[default]` band over
 * them, the record's cache params over that, and the record's band last.
 *
 * Which ids get a def: every npc with a band archive (a record that states a
 * server field and is named in `pack/npc.server`); every loc with a band archive
 * or a category. An npc with no band reads `ToriRSServer_ContentNpcDefault()`,
 * as one with no text block did.
 */

/** A band that does not decode whole is not this build's to read: the pack was
 *  written from a different register than the one this boot read. */
static int
band_decode_whole(
    const char* type_name,
    const struct RSCache_Register* fields,
    struct RSCache_BandRecord* record,
    void* object,
    const uint8_t* band,
    uint32_t size,
    int id)
{
    const struct ToriRSServerBandType* type = ToriRSServer_ServerTypeFor(type_name);
    int consumed;

    assert(type);
    RSCache_BandRecordReset(record);
    consumed = ToriRSServer_ServerDecode(type, fields, record, object, band, (int)size);
    if( consumed == (int)size )
        return 1;
    CONTENT_ERROR("server pack: %s %d: the band %s — rebuild the pack with `%s`\n", type_name, id,
                  consumed < 0 ? "carries an opcode fields/<type>.ini does not declare"
                               : "has bytes past its terminator",
                  TORIRSSERVER_SERVPACK_FIX);
    return 0;
}

/**
 * File every stated `text = param` int field of an npc band under its param id —
 * what `record_authored_param` did per `param=` line. A field whose name the param
 * pack does not know is filed under no id, as before.
 */
static void
npc_record_band_params(
    struct ToriRSServerNpcDef* def,
    const struct RSCache_BandRecord* record,
    const char* where)
{
    for( int i = 0; i < g_npc_fields.band_count; i++ )
    {
        const struct RSCache_RegisterField* field = &g_npc_fields.entries[i];

        if( field->text != RSCACHE_REGISTER_TEXT_PARAM ||
            !RSCache_PresenceHas(&record->present, i) )
            continue;
        if( field->wire == RSCACHE_REGISTER_WIRE_STRING || field->wire == RSCACHE_REGISTER_WIRE_LIST )
            continue;
        record_authored_param(def, field->name, record->values[i], where);
    }
}

/** Decode one npc band over `def` and finish what the bindings cannot. */
static int
npc_apply_band(
    struct ToriRSServerNpcDef* def,
    const uint8_t* band,
    uint32_t size,
    int id,
    const char* where)
{
    struct RSCache_BandRecord record;
    int patrol = RSCache_BandIndex(&g_npc_fields, "patrol");
    int forcemulti = RSCache_BandIndex(&g_npc_fields, "forcemulti");
    int ok = band_decode_whole("npc", &g_npc_fields, &record, def, band, size, id);

    if( ok )
    {
        npc_record_band_params(def, &record, where);
        /* Mirrored into the script-visible `forcemulti` param (combat.param) so
         * content's splash tests (`~npc_combat_multiway`) ask the same three legs
         * as ToriRSServer_CombatMultiway; undeclared, this records nothing. */
        if( forcemulti >= 0 && RSCache_PresenceHas(&record.present, forcemulti) )
            record_authored_param(def, "forcemulti", def->forcemulti, where);
        if( patrol >= 0 && RSCache_PresenceHas(&record.present, patrol) && record.lists[patrol] &&
            record.lists[patrol]->count > TORIRSSERVER_NPC_PATROL_MAX )
            CONTENT_ERROR("%s: a patrol of %d waypoints; the route holds %d\n", where,
                          record.lists[patrol]->count, TORIRSSERVER_NPC_PATROL_MAX);
    }
    RSCache_BandRecordFree(&record);
    return ok;
}

static void
npc_load_band(
    void* context,
    int id,
    const uint8_t* band,
    uint32_t size)
{
    int* failed = (int*)context;
    struct ToriRSServerNpcDef* def;
    char where[96];

    g_npc_defs = grow(g_npc_defs, &g_npc_def_capacity, g_npc_def_count, sizeof(*g_npc_defs));
    assert(g_npc_def_count < g_npc_def_capacity);
    def = &g_npc_defs[g_npc_def_count++];
    npc_def_seed_from_cache(def, id);
    def->symbol = ToriRSServer_ContentSymbolName(TORIRSSERVER_PACK_NPC, id);
    snprintf(where, sizeof(where), "server/pack npc %d (%s)", id, def->symbol ? def->symbol : "?");
    if( !npc_apply_band(def, band, size, id, where) )
        (*failed)++;
}

static struct ToriRSServerLocDef*
loc_def_add(int loc_id)
{
    struct ToriRSServerLocDef* def;

    g_loc_defs = grow(g_loc_defs, &g_loc_def_capacity, g_loc_def_count, sizeof(*g_loc_defs));
    assert(g_loc_def_count < g_loc_def_capacity);
    def = &g_loc_defs[g_loc_def_count++];
    memset(def, 0, sizeof(*def));
    def->loc_id = loc_id;
    def->symbol = ToriRSServer_ContentSymbolName(TORIRSSERVER_PACK_LOC, loc_id);
    def->category = -1;
    def->next_loc_stage = -1;
    return def;
}

static void
loc_load_band(
    void* context,
    int id,
    const uint8_t* band,
    uint32_t size)
{
    int* failed = (int*)context;
    struct ToriRSServerLocDef* def = loc_def_add(id);
    struct RSCache_BandRecord record;

    if( !band_decode_whole("loc", &g_loc_fields, &record, def, band, size, id) )
    {
        (*failed)++;
        RSCache_BandRecordFree(&record);
        return;
    }
    /*
     * Into the param table, so `loc_param(<name>)` answers what the band states.
     * Which param a field is, is the register's `param = <name>`; which number
     * that param is, is the param pack's — a script asking by name reaches the
     * row this writes.
     */
    for( int i = 0; i < g_loc_fields.band_count; i++ )
    {
        const struct RSCache_RegisterField* field = &g_loc_fields.entries[i];
        int param_id;

        if( !field->param_name[0] || !RSCache_PresenceHas(&record.present, i) )
            continue;
        param_id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_PARAM, field->param_name);
        if( param_id < 0 )
        {
            CONTENT_ERROR("fields/loc.ini maps `%s` to param `%s`, which pack/param.pack does "
                          "not name\n",
                          field->name, field->param_name);
            continue;
        }
        ToriRSServer_LocInfoParamOverlay(id, param_id, record.values[i]);
    }
    RSCache_BandRecordFree(&record);
}

static void
obj_load_band(
    void* context,
    int id,
    const uint8_t* band,
    uint32_t size)
{
    int* failed = (int*)context;
    struct ToriRSServerObjBand obj;
    struct RSCache_BandRecord record;

    memset(&obj, 0, sizeof(obj));
    obj.obj_id = id;
    if( !band_decode_whole("obj", &g_obj_fields, &record, &obj, band, size, id) )
        (*failed)++;
    RSCache_BandRecordFree(&record);
}

static void
dbtable_load_band(
    void* context,
    int id,
    const uint8_t* band,
    uint32_t size)
{
    int* failed = (int*)context;
    struct ToriRSServerDbTable* table = (struct ToriRSServerDbTable*)ToriRSServer_DbTable(id);
    struct RSCache_BandRecord record;

    if( !table )
    {
        CONTENT_ERROR("server pack: dbtable %d has a band and no record\n", id);
        (*failed)++;
        return;
    }
    if( !band_decode_whole("dbtable", &g_dbtable_fields, &record, table, band, size, id) )
        (*failed)++;
    RSCache_BandRecordFree(&record);
}

static int
compare_loc_def(
    const void* a,
    const void* b)
{
    int left = ((const struct ToriRSServerLocDef*)a)->loc_id;
    int right = ((const struct ToriRSServerLocDef*)b)->loc_id;

    return left < right ? -1 : (left > right ? 1 : 0);
}

int
ToriRSServer_ContentLoadPack(struct RSCache_ServerPack* pack)
{
    const int errors_before = g_errors;
    int npc_failed = 0;
    int loc_failed = 0;
    int npc_bands;
    int loc_bands;
    int obj_failed = 0;
    int obj_bands;
    int categorised = 0;
    int ops = 0;

    assert(pack);
    /* A register that disagrees with the bindings was a startup error at content
     * load (content_fields_check); a band read under no contract is not read. */
    if( g_band_register_problems )
    {
        fprintf(stderr, "torirsserver: server pack: refused — the field registers disagree with "
                        "this server's band bindings (see the content errors above)\n");
        return -1;
    }

    /* `[default]`: what every npc def starts from, and what an npc with no def
     * reads. Over the built-in numbers init_defaults left. */
    {
        void* owned = NULL;
        const uint8_t* band = NULL;
        uint32_t size = 0;

        if( !ToriRSServer_ServPackDefaults(pack, RSCACHE_DAT2_CONFIG_KIND_NPC, &owned, &band, &size) )
            return -1;
        if( !npc_apply_band(&g_npc_default, band, size, -1, "server/pack npc [default]") )
        {
            free(owned);
            return -1;
        }
        free(owned);
    }

    npc_bands = ToriRSServer_ServPackEachBand(pack, RSCACHE_DAT2_CONFIG_KIND_NPC, npc_load_band,
                                              &npc_failed);
    if( npc_bands < 0 || npc_failed )
        return -1;
    /* The roster is complete and nothing appends after this point, so give back
     * the doubling headroom — the last grow leaves up to half the block empty.
     * Before any World spawn caches a `npc->def` pointer, or the realloc would
     * move the array out from under it. */
    if( g_npc_def_count > 0 )
    {
        g_npc_defs = realloc(g_npc_defs, (size_t)g_npc_def_count * sizeof(*g_npc_defs));
        assert(g_npc_defs);
        g_npc_def_capacity = g_npc_def_count;
    }

    loc_bands = ToriRSServer_ServPackEachBand(pack, RSCACHE_DAT2_CONFIG_KIND_LOCS, loc_load_band,
                                              &loc_failed);
    if( loc_bands < 0 || loc_failed )
        return -1;

    /* The db: tables and rows from their client records, then each table's
     * column names from its band. */
    {
        int db_failed = 0;

        if( ToriRSServer_DbLoadPack(pack) != 0 ||
            ToriRSServer_ServPackEachBand(pack, RSCACHE_DAT2_CONFIG_KIND_DBTABLE,
                                          dbtable_load_band, &db_failed) < 0 ||
            db_failed )
            return -1;
    }

    /* obj: only the server's half (skill requirements); the record itself is
     * the pack's client record, decoded by ToriRSServer_ObjInfoLoad. */
    obj_bands = ToriRSServer_ServPackEachBand(pack, RSCACHE_DAT2_CONFIG_KIND_OBJECT, obj_load_band,
                                              &obj_failed);
    if( obj_bands < 0 || obj_failed )
        return -1;

    /*
     * The category half of a loc def. A loc's category is a client field and the
     * pack's record carries the merged one (`ToriRSServer_LocInfo`), so this is
     * a copy, not a second source: it is here because the def is what the door
     * validator and `ToriRSServer_LocCategoryMembers` walk. Appended unsorted,
     * sorted once below.
     */
    {
        int banded = g_loc_def_count;

        qsort(g_loc_defs, (size_t)g_loc_def_count, sizeof(*g_loc_defs), compare_loc_def);
        for( int i = 0; i < ToriRSServer_LocInfoCategoryCount(); i++ )
        {
            int loc_id;
            int category;
            struct ToriRSServerLocDef* def;

            ToriRSServer_LocInfoCategoryAt(i, &loc_id, &category);
            /* Searched over the banded prefix only: it is sorted, and the
             * category-only defs appended past it are not, until the sort
             * below. Each loc id appears once in the category table, so an
             * appended def is never looked for again. */
            {
                struct ToriRSServerLocDef key;

                key.loc_id = loc_id;
                def = banded > 0 ? (struct ToriRSServerLocDef*)bsearch(
                                       &key, g_loc_defs, (size_t)banded, sizeof(*g_loc_defs),
                                       compare_loc_def)
                                 : NULL;
            }
            if( !def )
                def = loc_def_add(loc_id);
            def->category = category;
            categorised++;
        }
        qsort(g_loc_defs, (size_t)g_loc_def_count, sizeof(*g_loc_defs), compare_loc_def);
    }

    /*
     * The ops the server answers for, from the pack's records. `ToriRSServer_SceneLocOp`
     * reads the scene's own cache record for anything not laid over it, so every op
     * the pack states is laid over: the pack is the source, and `op3=hidden` (which
     * no client cache states) is how a skilling loop resumes.
     */
    for( int i = 0; i < ToriRSServer_LocInfoOpCount(); i++ )
    {
        int loc_id = ToriRSServer_LocInfoOpLocAt(i);

        for( int op = 1; op <= 5; op++ )
        {
            const char* text = ToriRSServer_LocInfoOp(loc_id, op);

            if( !text )
                continue;
            ToriRSServer_SceneLocOpOverlay(loc_id, op, text);
            ops++;
        }
    }

    fprintf(stderr,
            "torirsserver: server pack: %d npc defs from %d band(s) over the [default] band, %d loc "
            "defs (%d banded, %d categorised), %d loc op(s)%s\n",
            g_npc_def_count, npc_bands, g_loc_def_count, loc_bands, categorised, ops,
            g_errors != errors_before ? ", WITH ERRORS" : "");
    return g_errors != errors_before ? -1 : 0;
}

void
ToriRSServer_ContentFree(void)
{
    ToriRSServer_ParamTableFree(&g_struct_overlay);
    for( int kind = 0; kind < TORIRSSERVER_PACK_COUNT; kind++ )
    {
        pack_names_free(&g_packs[kind]);
        free(g_packs[kind].entries);
        free(g_packs[kind].by_name);
        free(g_packs[kind].by_id);
        free(g_packs[kind].by_base);
        g_packs[kind].entries = NULL;
        g_packs[kind].by_name = NULL;
        g_packs[kind].by_id = NULL;
        g_packs[kind].by_base = NULL;
        g_packs[kind].base_count = 0;
        g_packs[kind].count = 0;
        g_packs[kind].capacity = 0;
    }
    for( int i = 0; i < g_npc_def_count; i++ )
    {
        free(g_npc_defs[i].params);
        free(g_npc_defs[i].patrol);
    }
    free(g_npc_defs);
    g_npc_defs = NULL;
    g_npc_def_count = g_npc_def_capacity = 0;
    free(g_npc_default.params);
    g_npc_default.params = NULL;
    g_npc_default.param_count = 0;
    free(g_npc_default.patrol);
    g_npc_default.patrol = NULL;
    g_npc_default.patrol_count = 0;
    ToriRSServer_SceneLocOpOverlayReset();
    free(g_loc_defs);
    g_loc_defs = NULL;
    g_loc_def_count = g_loc_def_capacity = 0;
    free(g_varp_defs);
    g_varp_defs = NULL;
    g_varp_def_count = g_varp_def_capacity = 0;
    free(g_multiway);
    g_multiway = NULL;
    g_multiway_count = g_multiway_capacity = 0;
    for( int i = 0; i < g_enum_def_count; i++ )
    {
        free((void*)g_enum_defs[i].symbol);
        if( g_enum_defs[i].default_text &&
            strcmp(g_enum_defs[i].default_text, "null") != 0 )
            free((void*)g_enum_defs[i].default_text);
        for( int j = 0; j < g_enum_defs[i].count; j++ )
            free((void*)g_enum_defs[i].values[j].text);
        free(g_enum_defs[i].values);
    }
    free(g_enum_defs);
    g_enum_defs = NULL;
    g_enum_def_count = g_enum_def_capacity = 0;
    ContentNameIndex_Free(&g_enum_index);
    g_enum_indexed = 0;
    for( int i = 0; i < g_constant_count; i++ )
    {
        free(g_constants[i].name);
        free(g_constants[i].text);
    }
    free(g_constants);
    g_constants = NULL;
    g_constant_count = g_constant_capacity = 0;
    ContentNameIndex_Free(&g_constant_index);
    free(g_param_types);
    g_param_types = NULL;
    g_param_type_count = 0;

    free(g_npc_spawns);
    g_npc_spawns = NULL;
    g_npc_spawn_count = g_npc_spawn_capacity = 0;
    free(g_obj_spawns);
    g_obj_spawns = NULL;
    g_obj_spawn_count = g_obj_spawn_capacity = 0;
    g_errors = 0;
}
