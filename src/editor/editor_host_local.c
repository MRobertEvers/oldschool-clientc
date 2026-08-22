/**
 * The desktop EditorHost: the content tree, reached directly.
 *
 * No daemon and no socket — on this binding the seam collapses to a function
 * call, which is the whole point of putting it behind a vtable rather than
 * behind a process boundary that the desktop case would then have to pay for.
 */

#include "editor_host.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef _WIN32
#include <direct.h>
#define editor_mkdir(path) _mkdir(path)
#else
#define editor_mkdir(path) mkdir(path, 0755)
#endif

struct local_host
{
    char content_dir[1024];
    /** Empty when baking is disabled for this session. */
    char repo_root[1024];
    /** Path of the lockfile while this session holds it; empty when it does not. */
    char lock_path[1100];
};

static void
square_path(
    const struct local_host* host,
    char* out,
    size_t out_size,
    int map_x,
    int map_z,
    const char* extension)
{
    assert(host);
    assert(out);
    assert(extension);

    snprintf(out, out_size, "%s/maps/m%d_%d.%s", host->content_dir, map_x, map_z, extension);
}

static enum EditorHost_Status
read_whole_file(
    const char* path,
    struct EditorHost_Blob* out_blob)
{
    FILE* file;
    long size;
    char* data;

    assert(path);
    assert(out_blob);

    out_blob->data = NULL;
    out_blob->size = 0;

    file = fopen(path, "rb");
    if( !file )
        return EDITOR_HOST_ABSENT;

    if( fseek(file, 0, SEEK_END) != 0 )
    {
        fclose(file);
        return EDITOR_HOST_IO_ERROR;
    }
    size = ftell(file);
    if( size < 0 )
    {
        fclose(file);
        return EDITOR_HOST_IO_ERROR;
    }
    rewind(file);

    data = malloc((size_t)size + 1);
    assert(data);

    if( size > 0 && fread(data, 1, (size_t)size, file) != (size_t)size )
    {
        free(data);
        fclose(file);
        return EDITOR_HOST_IO_ERROR;
    }
    fclose(file);

    data[size] = '\0';
    out_blob->data = data;
    out_blob->size = (size_t)size;
    return EDITOR_HOST_OK;
}

/**
 * Write via a temporary beside the target, then rename.
 *
 * The rename is what makes this atomic: a reader sees either the old file or
 * the new one. Writing in place would leave a truncated square on disk if the
 * process died mid-write, and a truncated `.jm2` is not a recoverable state —
 * the tiles it lost are simply gone.
 *
 * The temporary sits in the same directory so the rename stays within one
 * filesystem, where it is atomic.
 */
static enum EditorHost_Status
write_file_atomic(
    const char* path,
    const char* text,
    size_t length)
{
    char temp_path[1200];
    FILE* file;

    assert(path);
    assert(text || length == 0);

    snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);

    file = fopen(temp_path, "wb");
    if( !file )
    {
        fprintf(stderr, "editor: cannot open %s: %s\n", temp_path, strerror(errno));
        return EDITOR_HOST_IO_ERROR;
    }
    if( length > 0 && fwrite(text, 1, length, file) != length )
    {
        fprintf(stderr, "editor: short write to %s\n", temp_path);
        fclose(file);
        remove(temp_path);
        return EDITOR_HOST_IO_ERROR;
    }
    if( fclose(file) != 0 )
    {
        fprintf(stderr, "editor: cannot close %s: %s\n", temp_path, strerror(errno));
        remove(temp_path);
        return EDITOR_HOST_IO_ERROR;
    }
    if( rename(temp_path, path) != 0 )
    {
        fprintf(stderr, "editor: cannot rename onto %s: %s\n", path, strerror(errno));
        remove(temp_path);
        return EDITOR_HOST_IO_ERROR;
    }
    return EDITOR_HOST_OK;
}

static enum EditorHost_Status
local_square_list(
    void* user_data,
    int* out_coords,
    int max,
    int* out_count)
{
    struct local_host* host = user_data;
    char maps_dir[1100];
    DIR* dir;
    struct dirent* entry;
    int found = 0;

    assert(host);
    assert(out_count);
    assert(out_coords || max == 0);

    snprintf(maps_dir, sizeof(maps_dir), "%s/maps", host->content_dir);
    dir = opendir(maps_dir);
    if( !dir )
        return EDITOR_HOST_IO_ERROR;

    while( (entry = readdir(dir)) != NULL )
    {
        int map_x;
        int map_z;
        size_t name_length = strlen(entry->d_name);

        if( name_length < 5 || strcmp(entry->d_name + name_length - 4, ".jm2") != 0 )
            continue;
        if( sscanf(entry->d_name, "m%d_%d.jm2", &map_x, &map_z) != 2 )
            continue;

        if( found < max )
        {
            out_coords[found * 2] = map_x;
            out_coords[found * 2 + 1] = map_z;
        }
        found++;
    }
    closedir(dir);

    *out_count = found;
    return EDITOR_HOST_OK;
}

static enum EditorHost_Status
local_square_load(
    void* user_data,
    int map_x,
    int map_z,
    struct EditorHost_Blob* out_jm2,
    struct EditorHost_Blob* out_jl2)
{
    struct local_host* host = user_data;
    char path[1200];
    enum EditorHost_Status status;

    assert(host);
    assert(out_jm2);
    assert(out_jl2);

    square_path(host, path, sizeof(path), map_x, map_z, "jm2");
    status = read_whole_file(path, out_jm2);
    if( status != EDITOR_HOST_OK )
        return status;

    /* A square with no scenery ships no `.jl2`, so an absent one is data, not
     * a failure — it comes back as an empty blob and parses to zero locs. */
    square_path(host, path, sizeof(path), map_x, map_z, "jl2");
    read_whole_file(path, out_jl2);

    return EDITOR_HOST_OK;
}

static enum EditorHost_Status
local_square_save(
    void* user_data,
    int map_x,
    int map_z,
    const char* jm2_text,
    size_t jm2_length,
    const char* jl2_text,
    size_t jl2_length)
{
    struct local_host* host = user_data;
    char path[1200];
    enum EditorHost_Status status;

    assert(host);

    if( host->lock_path[0] == '\0' )
    {
        fprintf(stderr, "editor: refusing to save without the session lock\n");
        return EDITOR_HOST_LOCKED;
    }

    if( jm2_text )
    {
        square_path(host, path, sizeof(path), map_x, map_z, "jm2");
        status = write_file_atomic(path, jm2_text, jm2_length);
        if( status != EDITOR_HOST_OK )
            return status;
    }
    if( jl2_text )
    {
        square_path(host, path, sizeof(path), map_x, map_z, "jl2");
        status = write_file_atomic(path, jl2_text, jl2_length);
        if( status != EDITOR_HOST_OK )
            return status;
    }
    return EDITOR_HOST_OK;
}

static enum EditorHost_Status
local_bake(
    void* user_data,
    EditorHost_ProgressFn on_progress,
    void* progress_user_data)
{
    struct local_host* host = user_data;
    char command[2200];
    FILE* pipe;
    char line[512];
    int exit_status;

    assert(host);

    if( host->repo_root[0] == '\0' )
    {
        fprintf(stderr, "editor: baking is disabled for this session\n");
        return EDITOR_HOST_BAKE_FAILED;
    }

    /* The project's own target, not a hand-assembled cachepack line: it knows
     * about the base cache, the asset flags and the CS2 name tables, and a
     * second copy of that here would drift from the one the build uses. */
    snprintf(command, sizeof(command), "cd '%s' && make -C src torirsserver-cache 2>&1", host->repo_root);

    pipe = popen(command, "r");
    if( !pipe )
        return EDITOR_HOST_BAKE_FAILED;

    while( fgets(line, sizeof(line), pipe) )
    {
        size_t length = strlen(line);
        if( length > 0 && line[length - 1] == '\n' )
            line[length - 1] = '\0';
        if( on_progress )
            on_progress(progress_user_data, line);
    }

    exit_status = pclose(pipe);
    return exit_status == 0 ? EDITOR_HOST_OK : EDITOR_HOST_BAKE_FAILED;
}

/**
 * Single-writer lock over the content tree.
 *
 * Exclusive-create on a lockfile: whoever makes it owns the tree. This is what
 * keeps two editor sessions — or an editor and a content-watching rebake —
 * from interleaving writes into the same squares. A session that cannot take
 * it still runs; it just cannot save.
 */
static enum EditorHost_Status
local_session(
    void* user_data,
    int acquire)
{
    struct local_host* host = user_data;
    char path[1100];
    int fd;

    assert(host);

    snprintf(path, sizeof(path), "%s/.editor-session.lock", host->content_dir);

    if( !acquire )
    {
        if( host->lock_path[0] != '\0' )
        {
            remove(host->lock_path);
            host->lock_path[0] = '\0';
        }
        return EDITOR_HOST_OK;
    }

    fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0644);
    if( fd < 0 )
        return EDITOR_HOST_LOCKED;

    {
        char note[64];
        int length = snprintf(note, sizeof(note), "pid %ld\n", (long)getpid());
        if( length > 0 )
            (void)!write(fd, note, (size_t)length);
    }
    close(fd);

    snprintf(host->lock_path, sizeof(host->lock_path), "%s", path);
    return EDITOR_HOST_OK;
}

static void
local_free(void* user_data)
{
    struct local_host* host = user_data;

    if( !host )
        return;

    /* Never strand the lock: a session that dies holding it would lock every
     * later one out of a tree nobody is editing. */
    if( host->lock_path[0] != '\0' )
        remove(host->lock_path);
    free(host);
}

static enum EditorHost_Status
local_spawn_save(
    void* user_data,
    int map_x,
    int map_z,
    const char* text,
    size_t length)
{
    struct local_host* host = user_data;
    char dir[1200];
    char path[1300];

    assert(host);

    if( host->lock_path[0] == '\0' )
    {
        fprintf(stderr, "editor: refusing to save without the session lock\n");
        return EDITOR_HOST_LOCKED;
    }

    /* mkdir -p the edited lane; every component may already exist, and EEXIST
     * is success for this purpose. */
    {
        static char const* const legs[] = {
            "server", "server/scripts", "server/scripts/areas", "server/scripts/areas/edited",
            "server/scripts/areas/edited/configs"
        };
        for( size_t i = 0; i < sizeof(legs) / sizeof(legs[0]); i++ )
        {
            snprintf(dir, sizeof(dir), "%s/%s", host->content_dir, legs[i]);
            if( editor_mkdir(dir) != 0 && errno != EEXIST )
            {
                fprintf(stderr, "editor: cannot create %s\n", dir);
                return EDITOR_HOST_IO_ERROR;
            }
        }
    }

    snprintf(
        path, sizeof(path), "%s/server/scripts/areas/edited/configs/m%d_%d.spawn",
        host->content_dir, map_x, map_z);
    if( !text || length == 0 )
    {
        remove(path); /* absent == no edited spawns; missing is already that */
        return EDITOR_HOST_OK;
    }
    return write_file_atomic(path, text, length);
}

static const struct EditorHost_VTable local_vtable = {
    local_square_list, local_square_load, local_square_save, local_spawn_save,
    local_bake,        local_session,     local_free,
};

int
Editor_HostOpenLocal(
    struct EditorHost* host,
    const char* content_dir,
    const char* repo_root)
{
    struct local_host* local;

    assert(host);
    assert(content_dir);

    local = malloc(sizeof(*local));
    assert(local);
    memset(local, 0, sizeof(*local));

    snprintf(local->content_dir, sizeof(local->content_dir), "%s", content_dir);
    if( repo_root )
        snprintf(local->repo_root, sizeof(local->repo_root), "%s", repo_root);

    host->vtable = &local_vtable;
    host->user_data = local;
    return 1;
}
