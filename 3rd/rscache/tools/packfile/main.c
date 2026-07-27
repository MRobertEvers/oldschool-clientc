/*
 * packfile — patch LostCity `content/pack/<type>.pack` id registries.
 *
 * A pack file is the id authority for a LostCity build: nothing in the content
 * tree carries a number, so `52=inferno_zuk` in `npc.pack` is the only thing
 * that makes `[inferno_zuk]` in some `.npc` file exist. Editing them by hand is
 * easy to get subtly wrong — a duplicated name resolves to whichever line the
 * engine reads last, a duplicated id silently rebinds one of the two, and a
 * re-used id rebinds every reference an older build still holds.
 *
 * This does the same allocation `port_lostcity` does (append at max, never fill
 * a hole, idempotent by name) for the packs that tool does not touch — obj,
 * inv, param, script, varp and the rest — and can validate every pack in a tree.
 *
 *   ./packfile <content_dir> list npc zuk
 *   ./packfile <content_dir> add obj inferno_cape --apply
 *   ./packfile <content_dir> add texture 60=inferno_lava --apply
 *   ./packfile <content_dir> rename npc old_name new_name --apply
 *   ./packfile <content_dir> remove obj inferno_cape --apply
 *   ./packfile <content_dir> patch ports/inferno_ids.ini --apply
 *   ./packfile <content_dir> check
 *
 * Dry run by default, like every other tool here: without `--apply` the change
 * list is printed and nothing is written.
 */

#include "lc_pack.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void
usage(const char* argv0)
{
    fprintf(
        stderr,
        "Usage:\n"
        "  %s <content_dir> list   <type> [substring]\n"
        "  %s <content_dir> add    <type> <name|id=name>...   [--apply]\n"
        "  %s <content_dir> remove <type> <name|id>...        [--apply]\n"
        "  %s <content_dir> rename <type> <old> <new>         [--apply]\n"
        "  %s <content_dir> patch  <file.ini>                 [--apply]\n"
        "  %s <content_dir> check  [type]...\n"
        "\n"
        "  <type>     pack basename, e.g. npc for pack/npc.pack\n"
        "  add        appends at the next id past the highest listed one, and is\n"
        "             idempotent: a name already listed keeps the id it has. The\n"
        "             id=name form binds an explicit id and refuses an id in use\n"
        "  remove     drops the line. The id is not handed out again unless it was\n"
        "             the highest one, since a hole is usually a retired id\n"
        "  patch      batch the above from an INI file:\n"
        "               [pack:npc]\n"
        "               add=inferno_zuk\n"
        "               add=7000=inferno_jad\n"
        "               rename=old_name=new_name\n"
        "               remove=stale_name\n"
        "  check      report duplicate names, duplicate or malformed ids, and names\n"
        "             the engine's own loader would not accept. No type = every pack\n"
        "  --apply    write; without it nothing touches disk\n",
        argv0,
        argv0,
        argv0,
        argv0,
        argv0,
        argv0);
}

/* ---- change log ---------------------------------------------------------- */

struct changes
{
    char** lines;
    int count;
    int cap;
    int failures;
};

static void
change(struct changes* log, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void
change(struct changes* log, const char* fmt, ...)
{
    assert(log && fmt);
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if( log->count >= log->cap )
    {
        int cap = log->cap == 0 ? 32 : log->cap * 2;
        char** grown = realloc(log->lines, (size_t)cap * sizeof(char*));
        if( !grown )
            return;
        log->lines = grown;
        log->cap = cap;
    }
    size_t len = strlen(buf);
    log->lines[log->count] = malloc(len + 1);
    if( !log->lines[log->count] )
        return;
    memcpy(log->lines[log->count], buf, len + 1);
    log->count++;
}

static void
changes_free(struct changes* log)
{
    for( int i = 0; i < log->count; i++ )
        free(log->lines[i]);
    free(log->lines);
    memset(log, 0, sizeof(*log));
}

/* ---- one pack, open for editing ------------------------------------------ */

struct pack_edit
{
    struct LC_Pack pack;
    char path[1024];
    int dirty;
};

static void
pack_path(char* dst, size_t cap, const char* content_dir, const char* type)
{
    snprintf(dst, cap, "%s/pack/%s.pack", content_dir, type);
}

static int
pack_edit_open(
    struct pack_edit* edit,
    const char* content_dir,
    const char* type,
    int allow_missing)
{
    memset(edit, 0, sizeof(*edit));
    pack_path(edit->path, sizeof(edit->path), content_dir, type);
    return lc_pack_load(&edit->pack, edit->path, type, allow_missing);
}

static int
pack_edit_close(
    struct pack_edit* edit,
    int apply)
{
    int ok = 1;
    if( edit->dirty && apply )
    {
        /* The dir exists in any real content tree; created here only so a fresh
         * one can take its first pack line. */
        char dir[1024];
        snprintf(dir, sizeof(dir), "%s", edit->path);
        char* slash = strrchr(dir, '/');
        if( slash )
        {
            *slash = '\0';
            mkdir(dir, 0755);
        }
        ok = lc_pack_save(&edit->pack, edit->path);
    }
    lc_pack_free(&edit->pack);
    return ok;
}

/* ---- operations ---------------------------------------------------------- */

/**
 * `name` or `id=name`.
 *
 * The bare form appends at the next free id; the explicit form is for an id the
 * destination has already committed to somewhere else, and refuses to overwrite
 * a different name already holding it.
 */
static void
op_add(
    struct pack_edit* edit,
    struct changes* log,
    const char* arg)
{
    const char* eq = strchr(arg, '=');
    if( !eq )
    {
        int existing = lc_pack_find(&edit->pack, arg);
        if( existing >= 0 )
        {
            change(log, "  = %s %d=%s (already listed)", edit->pack.type, existing, arg);
            return;
        }
        int id = lc_pack_alloc(&edit->pack, arg);
        if( id < 0 )
        {
            change(log, "  ! %s %s: allocation failed", edit->pack.type, arg);
            log->failures++;
            return;
        }
        edit->dirty = 1;
        change(log, "  + %s %d=%s", edit->pack.type, id, arg);
        return;
    }

    char* end = NULL;
    long id = strtol(arg, &end, 10);
    if( end == arg || end != eq || id < 0 )
    {
        change(log, "  ! %s '%s': want <name> or <id>=<name>", edit->pack.type, arg);
        log->failures++;
        return;
    }
    const char* name = eq + 1;
    if( name[0] == '\0' )
    {
        change(log, "  ! %s '%s': empty name", edit->pack.type, arg);
        log->failures++;
        return;
    }

    const char* held =
        ((int)id < edit->pack.capacity) ? edit->pack.names[(int)id] : NULL;
    if( held && strcmp(held, name) == 0 )
    {
        change(log, "  = %s %ld=%s (already listed)", edit->pack.type, id, name);
        return;
    }
    if( held )
    {
        change(
            log,
            "  ! %s %ld is already '%s'; remove it first to rebind",
            edit->pack.type,
            id,
            held);
        log->failures++;
        return;
    }
    int elsewhere = lc_pack_find(&edit->pack, name);
    if( elsewhere >= 0 )
    {
        change(
            log,
            "  ! %s '%s' is already id %d; two ids for one name resolve to "
            "whichever line loads last",
            edit->pack.type,
            name,
            elsewhere);
        log->failures++;
        return;
    }
    if( !lc_pack_set(&edit->pack, (int)id, name) )
    {
        change(log, "  ! %s %ld=%s: allocation failed", edit->pack.type, id, name);
        log->failures++;
        return;
    }
    edit->pack.added++;
    edit->dirty = 1;
    change(log, "  + %s %ld=%s", edit->pack.type, id, name);
}

/** `name` or a bare id. */
static void
op_remove(
    struct pack_edit* edit,
    struct changes* log,
    const char* arg)
{
    char* end = NULL;
    long parsed = strtol(arg, &end, 10);
    int id = (end != arg && *end == '\0' && parsed >= 0) ? (int)parsed
                                                         : lc_pack_find(&edit->pack, arg);
    if( id < 0 || id >= edit->pack.capacity || !edit->pack.names[id] )
    {
        change(log, "  ! %s '%s': not listed", edit->pack.type, arg);
        log->failures++;
        return;
    }
    char name[256];
    snprintf(name, sizeof(name), "%s", edit->pack.names[id]);
    lc_pack_remove(&edit->pack, id);
    edit->dirty = 1;
    change(log, "  - %s %d=%s", edit->pack.type, id, name);
}

static void
op_rename(
    struct pack_edit* edit,
    struct changes* log,
    const char* from,
    const char* to)
{
    int id = lc_pack_find(&edit->pack, from);
    if( id < 0 )
    {
        change(log, "  ! %s '%s': not listed", edit->pack.type, from);
        log->failures++;
        return;
    }
    int taken = lc_pack_find(&edit->pack, to);
    if( taken >= 0 && taken != id )
    {
        change(log, "  ! %s '%s' is already id %d", edit->pack.type, to, taken);
        log->failures++;
        return;
    }
    if( !lc_pack_set(&edit->pack, id, to) )
    {
        change(log, "  ! %s %d=%s: allocation failed", edit->pack.type, id, to);
        log->failures++;
        return;
    }
    edit->dirty = 1;
    change(log, "  ~ %s %d=%s (was %s)", edit->pack.type, id, to, from);
}

/* ---- check --------------------------------------------------------------- */

/*
 * Read the file rather than the parsed pack: the loader — ours and the
 * engine's — skips a line it cannot read, so a typo'd id is invisible from the
 * loaded table. It is exactly the lines that get skipped that this looks for.
 */
static int
check_pack(
    const char* content_dir,
    const char* type,
    int* out_entries)
{
    char path[1024];
    pack_path(path, sizeof(path), content_dir, type);
    FILE* file = fopen(path, "rb");
    if( !file )
    {
        printf("  %-10s (no file)\n", type);
        return 0;
    }

    struct LC_Pack seen;
    memset(&seen, 0, sizeof(seen));
    snprintf(seen.type, sizeof(seen.type), "%s", type);

    int problems = 0;
    int entries = 0;
    int max_id = -1;
    int line_no = 0;
    char line[512];
    while( fgets(line, sizeof(line), file) )
    {
        line_no++;
        size_t len = strlen(line);
        while( len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r') )
            line[--len] = '\0';
        if( len == 0 )
            continue;

        char* eq = strchr(line, '=');
        char* end = NULL;
        long id = -1;
        if( eq )
        {
            *eq = '\0';
            id = strtol(line, &end, 10);
            *eq = '=';
        }
        if( !eq || end == line || end != eq || id < 0 )
        {
            printf("  %s:%d  skipped by the loader: %s\n", type, line_no, line);
            problems++;
            continue;
        }
        const char* name = eq + 1;
        if( name[0] == '\0' )
        {
            printf("  %s:%d  id %ld has no name\n", type, line_no, id);
            problems++;
            continue;
        }
        for( const char* p = name; *p; p++ )
        {
            if( !islower((unsigned char)*p) && !isdigit((unsigned char)*p) && *p != '_' &&
                *p != '+' && *p != '.' )
            {
                printf("  %s:%d  '%s' has a character outside [a-z0-9_+.]\n", type, line_no, name);
                problems++;
                break;
            }
        }
        if( (int)id < seen.capacity && seen.names[id] )
        {
            printf(
                "  %s:%d  id %ld listed twice ('%s' then '%s')\n",
                type,
                line_no,
                id,
                seen.names[id],
                name);
            problems++;
        }
        int dup = lc_pack_find(&seen, name);
        if( dup >= 0 )
        {
            printf("  %s:%d  '%s' listed twice (ids %d and %ld)\n", type, line_no, name, dup, id);
            problems++;
        }
        if( id < max_id )
        {
            printf("  %s:%d  id %ld is out of order (after %d)\n", type, line_no, id, max_id);
            problems++;
        }
        if( (int)id > max_id )
            max_id = (int)id;
        lc_pack_set(&seen, (int)id, name);
        entries++;
    }

    fclose(file);
    lc_pack_free(&seen);
    *out_entries = entries;
    if( problems == 0 )
        printf("  %-10s %d ids, max %d, %d holes\n", type, entries, max_id, max_id + 1 - entries);
    return problems;
}

/* ---- patch file ---------------------------------------------------------- */

/*
 * A patch file is an INI whose sections name packs and whose keys name
 * operations, so one file describes a whole id change across several packs:
 *
 *   [pack:npc]
 *   add=inferno_zuk
 *   add=7000=inferno_jad
 *   rename=old_name=new_name
 *   remove=stale_name
 *
 * Parsed line by line rather than through the shared INI reader because a value
 * here can itself contain '=' twice over, and because this tool links nothing
 * else.
 *
 * Every pack the file names stays open until the last line is read, so a patch
 * either lands whole or not at all — a half-applied id change is worse than a
 * refused one, and the failure is usually in a later section than the pack that
 * would already have been written.
 */
#define PATCH_MAX_PACKS 32

static int
run_patch(
    const char* content_dir,
    const char* patch_path,
    int apply,
    struct changes* log)
{
    FILE* file = fopen(patch_path, "rb");
    if( !file )
    {
        fprintf(stderr, "cannot open %s\n", patch_path);
        return 0;
    }

    struct pack_edit edits[PATCH_MAX_PACKS];
    int edit_count = 0;
    struct pack_edit* edit = NULL;
    int ok = 1;
    int line_no = 0;
    char line[1024];
    while( fgets(line, sizeof(line), file) )
    {
        line_no++;
        char* text = line;
        while( *text == ' ' || *text == '\t' )
            text++;
        char* cut = strpbrk(text, ";#\r\n");
        if( cut )
            *cut = '\0';
        size_t len = strlen(text);
        while( len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t') )
            text[--len] = '\0';
        if( len == 0 )
            continue;

        if( text[0] == '[' )
        {
            char* close = strchr(text, ']');
            if( !close )
            {
                fprintf(stderr, "%s:%d: unterminated section header\n", patch_path, line_no);
                ok = 0;
                break;
            }
            *close = '\0';
            const char* type = text + 1;
            if( strncmp(type, "pack:", 5) == 0 )
                type += 5;

            /* A pack named twice reopens the edit already in flight, so the
             * second section sees the first one's allocations. */
            edit = NULL;
            for( int i = 0; i < edit_count; i++ )
            {
                if( strcmp(edits[i].pack.type, type) == 0 )
                {
                    edit = &edits[i];
                    break;
                }
            }
            if( edit )
                continue;
            if( edit_count >= PATCH_MAX_PACKS )
            {
                fprintf(
                    stderr,
                    "%s:%d: more than %d packs in one patch\n",
                    patch_path,
                    line_no,
                    PATCH_MAX_PACKS);
                ok = 0;
                break;
            }
            if( !pack_edit_open(&edits[edit_count], content_dir, type, 1) )
            {
                ok = 0;
                break;
            }
            edit = &edits[edit_count++];
            continue;
        }

        if( !edit )
        {
            fprintf(stderr, "%s:%d: '%s' outside a [pack:<type>] section\n", patch_path, line_no, text);
            ok = 0;
            break;
        }

        char* eq = strchr(text, '=');
        if( !eq )
        {
            fprintf(stderr, "%s:%d: want <op>=<argument>\n", patch_path, line_no);
            ok = 0;
            break;
        }
        *eq = '\0';
        char* op = text;
        char* arg = eq + 1;
        while( *arg == ' ' || *arg == '\t' )
            arg++;
        size_t op_len = strlen(op);
        while( op_len > 0 && (op[op_len - 1] == ' ' || op[op_len - 1] == '\t') )
            op[--op_len] = '\0';

        if( strcmp(op, "add") == 0 )
            op_add(edit, log, arg);
        else if( strcmp(op, "remove") == 0 )
            op_remove(edit, log, arg);
        else if( strcmp(op, "rename") == 0 )
        {
            char* sep = strchr(arg, '=');
            if( !sep )
            {
                fprintf(stderr, "%s:%d: rename wants <old>=<new>\n", patch_path, line_no);
                ok = 0;
                break;
            }
            *sep = '\0';
            op_rename(edit, log, arg, sep + 1);
        }
        else
        {
            fprintf(stderr, "%s:%d: unknown operation '%s'\n", patch_path, line_no, op);
            ok = 0;
            break;
        }
    }

    fclose(file);
    /* Written only once the whole file has parsed and every operation in it has
     * succeeded, so a patch lands whole or not at all. */
    int write = apply && ok && log->failures == 0;
    for( int i = 0; i < edit_count; i++ )
    {
        if( !pack_edit_close(&edits[i], write) )
            ok = 0;
    }
    return ok;
}

/* ---- main ---------------------------------------------------------------- */

/** Every pack `check` looks at with no type named. */
static const char* const known_types[] = {
    "anim",   "animset", "base",  "category", "dbrow", "dbtable", "enum",  "flo",
    "hunt",   "idk",     "interface", "inv",  "loc",   "map",     "mesanim", "midi",
    "model",  "npc",     "obj",   "param",    "script", "seq",    "spotanim", "struct",
    "synth",  "texture", "varbit", "varn",    "varp",  "vars",
};

int
main(int argc, char** argv)
{
    int apply = 0;
    const char* positional[16];
    int positional_count = 0;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--apply") == 0 )
            apply = 1;
        else if( strcmp(argv[i], "--help") == 0 )
        {
            usage(argv[0]);
            return 0;
        }
        else if( positional_count < (int)(sizeof(positional) / sizeof(positional[0])) )
            positional[positional_count++] = argv[i];
        else
        {
            fprintf(stderr, "too many arguments\n");
            return 2;
        }
    }

    if( positional_count < 2 )
    {
        usage(argv[0]);
        return 2;
    }

    const char* content_dir = positional[0];
    const char* command = positional[1];

    struct changes log;
    memset(&log, 0, sizeof(log));
    int status = 0;

    if( strcmp(command, "check") == 0 )
    {
        int problems = 0;
        int entries = 0;
        if( positional_count > 2 )
        {
            for( int i = 2; i < positional_count; i++ )
                problems += check_pack(content_dir, positional[i], &entries);
        }
        else
        {
            for( size_t i = 0; i < sizeof(known_types) / sizeof(known_types[0]); i++ )
                problems += check_pack(content_dir, known_types[i], &entries);
        }
        printf("\n%s: %d problems\n", content_dir, problems);
        return problems ? 1 : 0;
    }

    if( strcmp(command, "patch") == 0 )
    {
        if( positional_count < 3 )
        {
            usage(argv[0]);
            return 2;
        }
        if( !run_patch(content_dir, positional[2], apply, &log) )
            status = 1;
    }
    else if( strcmp(command, "list") == 0 )
    {
        if( positional_count < 3 )
        {
            usage(argv[0]);
            return 2;
        }
        struct pack_edit edit;
        if( !pack_edit_open(&edit, content_dir, positional[2], 0) )
            return 1;
        const char* filter = positional_count > 3 ? positional[3] : NULL;
        int shown = 0;
        for( int id = 0; id < edit.pack.capacity; id++ )
        {
            if( !edit.pack.names[id] )
                continue;
            if( filter && !strstr(edit.pack.names[id], filter) )
                continue;
            printf("%d=%s\n", id, edit.pack.names[id]);
            shown++;
        }
        printf("\n%s: %d shown, max %d\n", edit.pack.type, shown, edit.pack.max);
        pack_edit_close(&edit, 0);
        return 0;
    }
    else if(
        strcmp(command, "add") == 0 || strcmp(command, "remove") == 0 ||
        strcmp(command, "rm") == 0 || strcmp(command, "rename") == 0 )
    {
        if( positional_count < 4 )
        {
            usage(argv[0]);
            return 2;
        }
        struct pack_edit edit;
        /* `add` may be the line that creates the pack; the others need it to
         * already hold what they are being asked to change. */
        if( !pack_edit_open(&edit, content_dir, positional[2], strcmp(command, "add") == 0) )
            return 1;

        if( strcmp(command, "rename") == 0 )
        {
            if( positional_count < 5 )
            {
                usage(argv[0]);
                pack_edit_close(&edit, 0);
                return 2;
            }
            op_rename(&edit, &log, positional[3], positional[4]);
        }
        else
        {
            for( int i = 3; i < positional_count; i++ )
            {
                if( strcmp(command, "add") == 0 )
                    op_add(&edit, &log, positional[i]);
                else
                    op_remove(&edit, &log, positional[i]);
            }
        }
        /* A run that reported a failure writes nothing: the operations were
         * given together and half of them landing is worse than none. */
        if( !pack_edit_close(&edit, apply && log.failures == 0) )
            status = 1;
    }
    else
    {
        fprintf(stderr, "unknown command: %s\n", command);
        usage(argv[0]);
        return 2;
    }

    for( int i = 0; i < log.count; i++ )
        printf("%s\n", log.lines[i]);
    printf("\n%s\n", apply ? "== applied ==" : "== dry run (pass --apply to write) ==");
    if( log.failures )
    {
        printf("failures: %d (nothing written)\n", log.failures);
        status = 1;
    }
    changes_free(&log);
    return status;
}
