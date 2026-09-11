/*
 * Lane descriptors: `<content>/ported/<lane>/lane.ini`.
 *
 * The dialect is the tree's ordinary INI — `[section]`, `key=value`, `;` or `//`
 * comments — parsed here rather than through the boot manifest's reader, which
 * knows about caches and RSA keys and would have to grow a content section to
 * serve a compiler that never boots anything.
 *
 * Every path is written relative to the content root and resolved against it
 * here. Relative because a lane is moved between trees (a porting branch, a
 * build/ checkout) far more often than it is moved within one, and an absolute
 * path in a committed descriptor is wrong for everybody but its author.
 */

#include "ssc_lane.h"

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void
strip_eol(char* text)
{
    size_t length = strlen(text);

    while( length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r' ||
                          text[length - 1] == ' ' || text[length - 1] == '\t') )
        text[--length] = '\0';
}

static int
lane_path_is_dir(const char* path)
{
    struct stat info;

    return stat(path, &info) == 0 && (info.st_mode & S_IFDIR) != 0;
}

/**
 * Collapse `.` and `..` textually, in place.
 *
 * Lexical, not realpath(): these paths are compared against the ones the source
 * walk builds, and the walk builds them by concatenation. The default content
 * root is `<src>/../..`, so a lane's script directory arrives here as
 * `…/server/scripts/../../server/scripts/ported_x` while the walk reaches the
 * same directory as `…/server/scripts/ported_x`. Those are the same place and
 * were not the same string — which is not a cosmetic difference: the exclusion
 * silently matched nothing and a lane that was switched off compiled anyway.
 */
static void
lane_normalise(char* path)
{
    char* out = path;
    char* cursor = path;
    char* segments[64];
    int depth = 0;
    int absolute = (path[0] == '/');

    if( absolute )
    {
        out++;
        cursor++;
    }
    while( *cursor )
    {
        char* segment = cursor;
        size_t length;

        while( *cursor && *cursor != '/' )
            cursor++;
        length = (size_t)(cursor - segment);
        if( *cursor == '/' )
            *cursor++ = '\0';

        if( length == 0 || (length == 1 && segment[0] == '.') )
            continue;
        if( length == 2 && segment[0] == '.' && segment[1] == '.' && depth > 0 )
        {
            /* Rewind to the segment this `..` cancels. */
            out = segments[--depth];
            continue;
        }
        if( depth == (int)(sizeof(segments) / sizeof(segments[0])) )
            return; /* Absurdly deep; leave it as written. */
        segments[depth++] = out;
        if( out != segment )
            memmove(out, segment, length);
        out += length;
        *out++ = '/';
    }
    /* Drop the trailing separator, keeping a bare "/" and "" intact. */
    if( out > path && out[-1] == '/' && (out - 1) != path )
        out--;
    *out = '\0';
}

/**
 * Append `<root>/<relative>` to a path list.
 *
 * A full list is an error rather than a silent drop: the symptom of a dropped
 * `pack=` is a name that does not resolve in a file that never mentions the
 * lane, which is a bad afternoon.
 */
static int
lane_add_path(
    char (*list)[SSC_LANE_PATH_MAX],
    int* count,
    const char* root,
    const char* relative,
    const char* key,
    const char* descriptor)
{
    assert(list);
    assert(count);
    assert(root);
    assert(relative);

    if( *count == SSC_LANE_PATHS_MAX )
    {
        fprintf(stderr, "%s: more than %d '%s' entries\n", descriptor, SSC_LANE_PATHS_MAX, key);
        return 0;
    }
    /* An absolute path is taken as written: a generated descriptor in a staging
     * directory has no useful relationship to the content root. */
    if( relative[0] == '/' )
        snprintf(list[*count], SSC_LANE_PATH_MAX, "%s", relative);
    else
        snprintf(list[*count], SSC_LANE_PATH_MAX, "%s/%s", root, relative);
    lane_normalise(list[*count]);
    (*count)++;
    return 1;
}

static int
lane_read(
    struct SSC_Lane* lane,
    const char* content_root,
    const char* lane_dir,
    const char* dir_name)
{
    char descriptor[SSC_LANE_PATH_MAX];
    char line[2048];
    FILE* file;
    int ok = 1;

    assert(lane);
    assert(content_root);
    assert(lane_dir);
    assert(dir_name);

    snprintf(descriptor, sizeof(descriptor), "%s/lane.ini", lane_dir);
    file = fopen(descriptor, "rb");
    if( !file )
        return 0;

    memset(lane, 0, sizeof(*lane));
    /* The directory name is the lane's name unless the descriptor says
     * otherwise, so the common case states nothing twice. */
    snprintf(lane->name, sizeof(lane->name), "%s", dir_name);
    snprintf(lane->dir, sizeof(lane->dir), "%s", lane_dir);

    while( ok && fgets(line, sizeof(line), file) )
    {
        char* cursor = line;
        char* key;
        char* value;

        while( *cursor == ' ' || *cursor == '\t' )
            cursor++;
        if( *cursor == ';' || *cursor == '#' || *cursor == '\0' || *cursor == '\n' ||
            *cursor == '\r' )
            continue;
        if( cursor[0] == '/' && cursor[1] == '/' )
            continue;
        /* Sections are read for shape only. `[lane]` and `[compile]` separate
         * identity from inputs for a human; every key below is unique across
         * both, so nothing here has to track which one it is in. */
        if( *cursor == '[' )
            continue;

        key = cursor;
        while( *cursor && *cursor != '=' )
            cursor++;
        if( *cursor != '=' )
            continue;
        *cursor++ = '\0';
        strip_eol(key);
        value = cursor;
        while( *value == ' ' || *value == '\t' )
            value++;
        strip_eol(value);
        if( value[0] == '\0' )
            continue;

        if( strcmp(key, "name") == 0 )
            snprintf(lane->name, sizeof(lane->name), "%s", value);
        else if( strcmp(key, "default") == 0 )
            lane->enabled_by_default = (strcmp(value, "on") == 0 || strcmp(value, "1") == 0 ||
                                        strcmp(value, "yes") == 0);
        else if( strcmp(key, "constant") == 0 )
            snprintf(lane->constant, sizeof(lane->constant), "%s", value);
        else if( strcmp(key, "scripts") == 0 )
            ok = lane_add_path(lane->scripts, &lane->script_count, content_root, value, key,
                               descriptor);
        else if( strcmp(key, "pack") == 0 )
            ok = lane_add_path(lane->packs, &lane->pack_count, content_root, value, key,
                               descriptor);
        else if( strcmp(key, "pack_file") == 0 )
            ok = lane_add_path(lane->pack_files, &lane->pack_file_count, content_root, value,
                               key, descriptor);
        else if( strcmp(key, "component_root") == 0 )
            ok = lane_add_path(lane->component_roots, &lane->component_root_count,
                               content_root, value, key, descriptor);
        else if( strcmp(key, "title") == 0 )
            continue; /* documentation */
        else
            fprintf(stderr, "%s: ignoring unknown key '%s'\n", descriptor, key);
    }
    fclose(file);

    if( !ok )
        return -1;
    if( lane->name[0] == '\0' )
    {
        fprintf(stderr, "%s: lane has no name\n", descriptor);
        return -1;
    }
    return 1;
}

static int
compare_names(const void* a, const void* b)
{
    return strcmp((const char*)a, (const char*)b);
}

int
SSC_LanesDiscover(
    struct SSC_LaneSet* set,
    const char* content_root)
{
    char ported[SSC_LANE_PATH_MAX];
    char names[SSC_LANE_MAX][SSC_LANE_NAME_MAX];
    int name_count = 0;
    DIR* handle;
    struct dirent* entry;
    int i;

    assert(set);
    assert(content_root);

    memset(set, 0, sizeof(*set));
    /* Normalised before anything is opened. The default root is `<src>/../..`,
     * and `opendir` resolves `..` through the filesystem — so an intermediate
     * directory that does not exist makes the whole path unopenable even though
     * the place it names does exist. Collapsing it here also makes every path
     * built from it comparable with the ones the source walk builds. */
    snprintf(set->content_root, sizeof(set->content_root), "%s", content_root);
    lane_normalise(set->content_root);
    snprintf(ported, sizeof(ported), "%s/ported", set->content_root);

    handle = opendir(ported);
    if( !handle )
        return 0;

    while( (entry = readdir(handle)) != NULL )
    {
        char lane_dir[SSC_LANE_PATH_MAX];
        char descriptor[SSC_LANE_PATH_MAX];
        struct stat info;

        if( entry->d_name[0] == '.' )
            continue;
        snprintf(lane_dir, sizeof(lane_dir), "%s/%s", ported, entry->d_name);
        if( !lane_path_is_dir(lane_dir) )
            continue;
        snprintf(descriptor, sizeof(descriptor), "%s/lane.ini", lane_dir);
        if( stat(descriptor, &info) != 0 )
            continue;
        if( name_count == SSC_LANE_MAX )
        {
            fprintf(stderr, "sscompile: more than %d lanes under %s\n", SSC_LANE_MAX, ported);
            closedir(handle);
            return -1;
        }
        snprintf(names[name_count++], SSC_LANE_NAME_MAX, "%s", entry->d_name);
    }
    closedir(handle);

    qsort(names, (size_t)name_count, SSC_LANE_NAME_MAX, compare_names);

    for( i = 0; i < name_count; i++ )
    {
        char lane_dir[SSC_LANE_PATH_MAX];
        int read;

        snprintf(lane_dir, sizeof(lane_dir), "%s/%s", ported, names[i]);
        read = lane_read(&set->lanes[set->count], set->content_root, lane_dir, names[i]);
        if( read < 0 )
            return -1;
        if( read > 0 )
            set->count++;
    }
    return set->count;
}

struct SSC_Lane*
SSC_LaneFind(
    struct SSC_LaneSet* set,
    const char* name)
{
    int i;

    assert(set);
    assert(name);

    for( i = 0; i < set->count; i++ )
    {
        if( strcmp(set->lanes[i].name, name) == 0 )
            return &set->lanes[i];
    }
    return NULL;
}
