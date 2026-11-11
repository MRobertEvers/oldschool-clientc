/*
 * Editor lanes: the registry under build/lanes, the content mirror, a lane's
 * changes against its base, publish and discard (torirs_maped_lanes.h).
 *
 * Every walk here is over a content tree of ~100 k entries; a changes scan
 * stats the lane's own directories (symlinks are not followed, so the
 * directory-linked asset trees cost one entry each) and reads only the files
 * that are real in the mirror, which is the edited few. The deleted set
 * walks the base's directories that are real in the mirror.
 *
 * Two layouts (lane.ini): a LEGACY lane (`base = `) is a full mirror, a real
 * directory for every base directory and one link per base file; a SPARSE
 * lane (`layout = sparse`, `sparse_base = `; every lane Create makes since
 * 2026-11-10) links whole directories and makes one real only when
 * something is about to be written inside it (ToriRSMapEdLanes_MakeWritable,
 * "sparse lanes" below). The walks are the same for both: they go into what
 * is real in the lane and nothing else.
 */
#include "torirs_maped_lanes.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

/** What a pair of directory listings settles about one entry of a walk, in
 *  place of the walk's stat() and lstat() of it. Only Windows has listings
 *  (struct lanes_listing below): elsewhere every answer is LANES_ASK and the
 *  walks make the calls they always made. */
enum lanes_settled
{
    /** Not settled: make the per-entry calls. */
    LANES_ASK = 0,
    /** The per-entry calls would leave this entry alone. */
    LANES_NOTHING,
    /** Both sides hold a real directory of this name: the walk goes in. */
    LANES_DESCEND,
};

#ifdef _WIN32
/*
 * The POSIX calls a lane is built from, over Win32. A lane IS symlinks -- the
 * mirror links every base file and the asset trees whole, and a changes scan
 * tells an edited file from an untouched one by whether it is still a link --
 * so these keep that meaning rather than degrading to copies, which would make
 * every file in a mirror look edited.
 *
 * Links need Developer Mode (or an elevated process) on Windows 10+; without
 * it CreateSymbolicLinkA fails and the caller reports "cannot link" as usual.
 */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <direct.h>
#include <windows.h>
#include <winioctl.h> /* FSCTL_SET_REPARSE_POINT: WIN32_LEAN_AND_MEAN leaves it out */

#ifndef S_IFLNK
#define S_IFLNK 0120000
#endif
#ifndef S_ISLNK
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#endif

/* `_mkdir("C:")` fails with EACCES, not EEXIST, and mkdir_p walks every
 * prefix of an absolute path; an existing directory is EEXIST here as there. */
static int
lanes_mkdir(const char* path)
{
    DWORD attrs;

    if( _mkdir(path) == 0 )
        return 0;
    attrs = GetFileAttributesA(path);
    if( attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) )
        errno = EEXIST;
    return -1;
}

/* A directory junction at `link_path` to the absolute directory `target`
 * (backslashes): the unprivileged directory link, a mount-point reparse
 * point. 1 on success. */
static int
lanes_junction(
    const char* target,
    const char* link_path)
{
    /* REPARSE_DATA_BUFFER's MountPointReparseBuffer arm (ntifs.h, which
     * MinGW's headers do not carry). */
    struct
    {
        DWORD tag;
        WORD data_length;
        WORD reserved;
        WORD substitute_offset;
        WORD substitute_length;
        WORD print_offset;
        WORD print_length;
        WCHAR path[TORIRSMAPED_LANE_PATH * 2 + 8];
    } buffer;
    WCHAR wide[TORIRSMAPED_LANE_PATH];
    HANDLE handle;
    DWORD returned = 0;
    int length;
    int ok;

    length = MultiByteToWideChar(CP_ACP, 0, target, -1, wide, TORIRSMAPED_LANE_PATH);
    if( length <= 1 )
        return 0;
    length--; /* without the NUL */
    memset(&buffer, 0, sizeof(buffer));
    buffer.tag = IO_REPARSE_TAG_MOUNT_POINT;
    /* "\??\C:\dir" then NUL, then the print name "C:\dir" then NUL. */
    memcpy(buffer.path, L"\\??\\", 4 * sizeof(WCHAR));
    memcpy(buffer.path + 4, wide, (size_t)length * sizeof(WCHAR));
    buffer.substitute_length = (WORD)((length + 4) * sizeof(WCHAR));
    buffer.print_offset = (WORD)(buffer.substitute_length + sizeof(WCHAR));
    memcpy(buffer.path + length + 5, wide, (size_t)length * sizeof(WCHAR));
    buffer.print_length = (WORD)(length * sizeof(WCHAR));
    buffer.data_length = (WORD)(4 * sizeof(WORD) + buffer.print_offset + buffer.print_length + sizeof(WCHAR));

    if( !CreateDirectoryA(link_path, NULL) )
        return 0;
    handle = CreateFileA(link_path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if( handle == INVALID_HANDLE_VALUE )
    {
        RemoveDirectoryA(link_path);
        return 0;
    }
    ok = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, &buffer, 8 + buffer.data_length, NULL, 0,
        &returned, NULL) != 0;
    CloseHandle(handle);
    if( !ok )
        RemoveDirectoryA(link_path);
    return ok;
}

/*
 * A link the way the mirror means it. A symbolic link where the account may
 * make one (Developer Mode, or elevated); otherwise what Windows lets every
 * account make: a directory junction for a directory, a hard link for a file.
 * Either way lstat() below reads it as a link, and a save -- which writes a
 * temporary and replaces the entry -- turns it into a real file, so the
 * changes scan sees exactly the edited files on every lane kind.
 *
 * The hard-link difference: a publish that replaces a base file leaves the
 * other lanes' hard links on the old bytes, where a symbolic link would show
 * the new ones (docs/MAPED_STATUS.md, "Windows run").
 */
static int
symlink(
    const char* target,
    const char* link_path)
{
    static int symbolic_refused = 0;
    char native[TORIRSMAPED_LANE_PATH];
    DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
    DWORD attrs;
    int is_dir;

    /* The target is stored verbatim and Windows does not resolve '/' in it. */
    snprintf(native, sizeof(native), "%s", target);
    for( char* scan = native; *scan; scan++ )
        if( *scan == '/' )
            *scan = '\\';
    attrs = GetFileAttributesA(native);
    is_dir = attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
    if( is_dir )
        flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
    if( !symbolic_refused )
    {
        if( CreateSymbolicLinkA(link_path, native, flags) )
            return 0;
        if( GetLastError() != ERROR_PRIVILEGE_NOT_HELD )
        {
            errno = EACCES;
            return -1;
        }
        symbolic_refused = 1;
        fprintf(stderr, "lanes: this account may not make symbolic links (Developer Mode is off); "
                   "linking directories by junction and files by hard link\n");
    }
    if( is_dir ? lanes_junction(native, link_path) : CreateHardLinkA(link_path, native, NULL) != 0 )
        return 0;
    errno = EACCES;
    return -1;
}

/* stat() of the entry itself, as POSIX lstat: a reparse point (a symbolic
 * link or a junction) reads as S_IFLNK and is not followed, and so does a
 * file with more than one hard link (symlink() above: the mirror's link). */
static int
lstat(
    const char* path,
    struct stat* st)
{
    WIN32_FILE_ATTRIBUTE_DATA data;

    if( !GetFileAttributesExA(path, GetFileExInfoStandard, &data) )
    {
        errno = ENOENT;
        return -1;
    }
    if( !(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) )
    {
        if( !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) )
        {
            HANDLE handle = CreateFileA(path, FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
            BY_HANDLE_FILE_INFORMATION info;
            int linked = 0;
            if( handle != INVALID_HANDLE_VALUE )
            {
                linked = GetFileInformationByHandle(handle, &info) && info.nNumberOfLinks > 1;
                CloseHandle(handle);
            }
            if( linked )
            {
                memset(st, 0, sizeof(*st));
                st->st_mode = S_IFLNK;
                return 0;
            }
        }
        return stat(path, st);
    }
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFLNK;
    return 0;
}

/* POSIX unlink of a link: a junction is a directory to Win32. */
static int
lanes_unlink(const char* path)
{
    DWORD const attrs = GetFileAttributesA(path);

    if( attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT)
        && (attrs & FILE_ATTRIBUTE_DIRECTORY) )
        return RemoveDirectoryA(path) ? 0 : (errno = EACCES, -1);
    return unlink(path);
}
#define unlink(path) lanes_unlink(path)

/* POSIX rename replaces its target atomically; Win32's rename refuses an
 * existing one. MoveFileEx with REPLACE_EXISTING is the same swap (as
 * platform_x_io.c and torirs_server_savefile.c do it). */
static int
lanes_rename(
    const char* from,
    const char* to)
{
    if( MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) )
        return 0;
    errno = EACCES;
    return -1;
}
#define rename(from, to) lanes_rename(from, to)

/* An absolute path with '/' separators, as the callers compare and join it.
 * NULL when `path` does not exist, as POSIX realpath. */
static char*
realpath(
    const char* path,
    char* resolved)
{
    if( GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES )
    {
        errno = ENOENT;
        return NULL;
    }
    if( !_fullpath(resolved, path, PATH_MAX) )
        return NULL;
    for( char* scan = resolved; *scan; scan++ )
        if( *scan == '\\' )
            *scan = '/';
    return resolved;
}

/* Only the existence probe, `kill(pid, 0)`: ESRCH when no such process is
 * running, 0 when one is (including one we may not open, as POSIX EPERM). */
static int
kill(
    pid_t pid,
    int sig)
{
    HANDLE process;
    DWORD code = 0;

    assert(sig == 0);
    (void)sig;
    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if( !process )
    {
        if( GetLastError() == ERROR_ACCESS_DENIED )
            return 0;
        errno = ESRCH;
        return -1;
    }
    if( GetExitCodeProcess(process, &code) && code != STILL_ACTIVE )
    {
        CloseHandle(process);
        errno = ESRCH;
        return -1;
    }
    CloseHandle(process);
    return 0;
}

/* ---- a directory as the volume lists it ------------------------------------
 *
 * Every walk below asks two questions of every entry of a ~100 k entry tree:
 * stat() of the base's file and lstat() of the mirror's. lstat() above is
 * four system calls for a file (its attributes, an open, the link count, a
 * close) and then stat() again, because a hard link is only told from a
 * file by its link count and that is only read through a handle. Measured
 * on 2026-11-10 with TORIRS_EDITOR_FRAME_STATS on a mirror of 102,358 files:
 * the walk after a save held one frame for 8.2 s, RUN's follow for 9.3 s
 * and the walk after its build for 9.4 s more.
 *
 * A directory can be read whole instead: one call returns each entry's name,
 * attributes, size, write time and FILE ID, and two names on one volume with
 * one file id are one file -- which is what "this mirror file is still the
 * base's hard link" means. So each walk reads the two directories it stands
 * in once, and asks per entry only where the listings cannot settle what the
 * per-entry calls would have answered (lanes_settled below). Nothing is
 * decided differently: a listing that says less than "certain" falls through
 * to the same stat() and lstat() as before.
 *
 * FILE_ID_BOTH_DIR_INFO and its info class are spelled here and the call is
 * resolved at run time: the XP lane's headers (_WIN32_WINNT 0x0501) declare
 * neither and XP's kernel32 does not export it. Without it every listing is
 * "not read" and the walks ask per entry, as they always did there.
 */
struct lanes_dir_info
{
    DWORD next_entry_offset;
    DWORD file_index;
    LARGE_INTEGER creation_time;
    LARGE_INTEGER last_access_time;
    LARGE_INTEGER last_write_time;
    LARGE_INTEGER change_time;
    LARGE_INTEGER end_of_file;
    LARGE_INTEGER allocation_size;
    DWORD file_attributes;
    DWORD file_name_length; /* bytes */
    DWORD ea_size;
    CCHAR short_name_length;
    WCHAR short_name[12];
    LARGE_INTEGER file_id;
    WCHAR file_name[1];
};
_Static_assert(offsetof(struct lanes_dir_info, file_attributes) == 56, "FILE_ID_BOTH_DIR_INFO.FileAttributes");
_Static_assert(offsetof(struct lanes_dir_info, file_id) == 96, "FILE_ID_BOTH_DIR_INFO.FileId");
_Static_assert(offsetof(struct lanes_dir_info, file_name) == 104, "FILE_ID_BOTH_DIR_INFO.FileName");

enum
{
    /** FILE_INFO_BY_HANDLE_CLASS: FileIdBothDirectoryInfo and its restart form. */
    LANES_DIR_INFO_CLASS = 10,
    LANES_DIR_INFO_RESTART_CLASS = 11,
    LANES_DIR_INFO_BUFFER = 256 * 1024,
};

struct lanes_listing_row
{
    const char* name;
    /** `name`'s offset in the names block, which moves as it grows. */
    size_t name_at;
    DWORD attrs;
    unsigned long long id;
    unsigned long long size;
    long long write_time;
};

struct lanes_listing
{
    /** 0: not read -- the directory is absent, the call is, or an entry's
     *  name has no spelling in the ANSI code page the per-entry calls use.
     *  Every question of a listing that is not valid is "ask per entry". */
    int valid;
    DWORD volume;
    struct lanes_listing_row* rows;
    int count;
    int capacity;
    char* names;
    size_t names_used;
    size_t names_capacity;
};

typedef BOOL(WINAPI* lanes_dir_info_fn)(HANDLE, int, LPVOID, DWORD);

static lanes_dir_info_fn
lanes_dir_info_call(void)
{
    /* Resolved once; a second thread racing the first writes the same value. */
    static lanes_dir_info_fn call;
    static int resolved;

    if( !resolved )
    {
        HMODULE const kernel = GetModuleHandleA("kernel32.dll");
        call = kernel ? (lanes_dir_info_fn)(void*)GetProcAddress(kernel, "GetFileInformationByHandleEx") : NULL;
        resolved = 1;
    }
    return call;
}

static void
lanes_listing_free(struct lanes_listing* listing)
{
    free(listing->rows);
    free(listing->names);
    memset(listing, 0, sizeof(*listing));
}

static int
lanes_listing_row_compare(
    const void* a,
    const void* b)
{
    return strcmp(((const struct lanes_listing_row*)a)->name, ((const struct lanes_listing_row*)b)->name);
}

/** Read `dir` whole into `out` (valid 0 when it cannot be). malloc, not
 *  ToriMem: the change scan runs on a worker thread. */
static void
lanes_listing_read(
    const char* dir,
    struct lanes_listing* out)
{
    lanes_dir_info_fn const call = lanes_dir_info_call();
    BY_HANDLE_FILE_INFORMATION volume;
    HANDLE handle;
    char* buffer;
    int info_class = LANES_DIR_INFO_RESTART_CLASS;
    int complete = 0;
    int unspellable = 0;

    memset(out, 0, sizeof(*out));
    if( !call )
        return;
    handle = CreateFileA(dir, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if( handle == INVALID_HANDLE_VALUE )
        return;
    if( !GetFileInformationByHandle(handle, &volume) )
    {
        CloseHandle(handle);
        return;
    }
    out->volume = volume.dwVolumeSerialNumber;
    buffer = malloc(LANES_DIR_INFO_BUFFER);
    assert(buffer);
    for( ;; )
    {
        const char* at = buffer;

        if( !call(handle, info_class, buffer, LANES_DIR_INFO_BUFFER) )
        {
            complete = GetLastError() == ERROR_NO_MORE_FILES;
            break;
        }
        info_class = LANES_DIR_INFO_CLASS;
        for( ;; )
        {
            const struct lanes_dir_info* info = (const struct lanes_dir_info*)(const void*)at;
            char name[MAX_PATH * 2];
            BOOL replaced = FALSE;
            int const length = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, info->file_name,
                (int)(info->file_name_length / sizeof(WCHAR)), name, (int)sizeof(name) - 1, NULL, &replaced);

            if( length <= 0 || replaced )
                unspellable = 1;
            else if( !(length == 1 && name[0] == '.') && !(length == 2 && name[0] == '.' && name[1] == '.') )
            {
                struct lanes_listing_row* row;

                if( out->count == out->capacity )
                {
                    out->capacity = out->capacity ? out->capacity * 2 : 256;
                    out->rows = realloc(out->rows, (size_t)out->capacity * sizeof(*out->rows));
                    assert(out->rows);
                }
                if( out->names_used + (size_t)length + 1 > out->names_capacity )
                {
                    out->names_capacity = (out->names_capacity ? out->names_capacity * 2 : 8192) + (size_t)length + 1;
                    out->names = realloc(out->names, out->names_capacity);
                    assert(out->names);
                }
                row = &out->rows[out->count++];
                row->name = NULL;
                row->name_at = out->names_used;
                memcpy(out->names + out->names_used, name, (size_t)length);
                out->names[out->names_used + (size_t)length] = '\0';
                out->names_used += (size_t)length + 1;
                row->attrs = info->file_attributes;
                row->id = (unsigned long long)info->file_id.QuadPart;
                row->size = (unsigned long long)info->end_of_file.QuadPart;
                row->write_time = (long long)info->last_write_time.QuadPart;
            }
            if( info->next_entry_offset == 0 )
                break;
            at += info->next_entry_offset;
        }
    }
    free(buffer);
    CloseHandle(handle);
    if( !complete || unspellable )
    {
        lanes_listing_free(out);
        return;
    }
    for( int i = 0; i < out->count; i++ )
        out->rows[i].name = out->names + out->rows[i].name_at;
    if( out->count > 1 )
        qsort(out->rows, (size_t)out->count, sizeof(*out->rows), lanes_listing_row_compare);
    out->valid = 1;
}

/** The row named `name` (as readdir spells it), NULL when the listing has
 *  none -- or was not read, which the callers treat the same: ask. */
static const struct lanes_listing_row*
lanes_listing_find(
    const struct lanes_listing* listing,
    const char* name)
{
    struct lanes_listing_row key;

    if( !listing->valid || listing->count == 0 )
        return NULL;
    key.name = name;
    return bsearch(&key, listing->rows, (size_t)listing->count, sizeof(*listing->rows), lanes_listing_row_compare);
}

/** What stat() answers S_ISDIR for without following anything. */
static int
lanes_row_is_real_dir(const struct lanes_listing_row* row)
{
    return (row->attrs & FILE_ATTRIBUTE_DIRECTORY) && !(row->attrs & FILE_ATTRIBUTE_REPARSE_POINT);
}

/** 1 when `lane_row` (of `lane`) and `base_row` (of `base`) are two names of
 *  one file: lstat() above would count its links, find more than one, and
 *  answer S_IFLNK. The size and the write time are the file's, so they agree
 *  whenever the id does; asked anyway, against a volume whose 64-bit ids are
 *  not the whole id (ReFS). */
static int
lanes_rows_same_file(
    const struct lanes_listing* lane,
    const struct lanes_listing_row* lane_row,
    const struct lanes_listing* base,
    const struct lanes_listing_row* base_row)
{
    return lane->volume == base->volume && lane_row->id != 0 && lane_row->id == base_row->id
           && !((lane_row->attrs | base_row->attrs) & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
           && lane_row->size == base_row->size && lane_row->write_time == base_row->write_time;
}

/** The base's and the mirror's directory for one `rel`, read once by the
 *  walk that stands in them. */
struct lanes_pair
{
    struct lanes_listing base;
    struct lanes_listing lane;
};

static void
lanes_pair_read(
    struct lanes_pair* pair,
    const char* base_dir,
    const char* lane_dir)
{
    /* TORIRSMAPED_LANES_LISTINGS=0: no listings, so every walk asks per
     * entry -- the code every other platform runs, for a test or a timing
     * to set beside this one. */
    const char* const env = getenv("TORIRSMAPED_LANES_LISTINGS");

    if( env && env[0] == '0' )
    {
        memset(pair, 0, sizeof(*pair));
        return;
    }
    lanes_listing_read(base_dir, &pair->base);
    lanes_listing_read(lane_dir, &pair->lane);
}

static void
lanes_pair_free(struct lanes_pair* pair)
{
    lanes_listing_free(&pair->base);
    lanes_listing_free(&pair->lane);
}

/** The names of one directory, for a walk's loop: from its listing when the
 *  walk has read one (no second enumeration of it), else from readdir. */
struct lanes_names
{
    const struct lanes_listing* listing;
    int at;
    DIR* dir;
};

/** 1 when `path` can be read; `listing` is the walk's listing of it. */
static int
lanes_names_open(
    struct lanes_names* names,
    const char* path,
    const struct lanes_listing* listing)
{
    memset(names, 0, sizeof(*names));
    if( listing->valid )
    {
        names->listing = listing;
        return 1;
    }
    names->dir = opendir(path);
    return names->dir != NULL;
}

/** The next entry's name ("." and ".." never), NULL after the last. */
static const char*
lanes_names_next(struct lanes_names* names)
{
    struct dirent* entry;

    if( names->listing )
        return names->at < names->listing->count ? names->listing->rows[names->at++].name : NULL;
    while( (entry = readdir(names->dir)) )
    {
        if( strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0 )
            return entry->d_name;
    }
    return NULL;
}

static void
lanes_names_close(struct lanes_names* names)
{
    if( names->dir )
        closedir(names->dir);
    memset(names, 0, sizeof(*names));
}

/** Which of a pair's two directories a walk's loop names. */
#define lanes_names_open_base(names, path, pair) lanes_names_open(names, path, &(pair)->base)
#define lanes_names_open_lane(names, path, pair) lanes_names_open(names, path, &(pair)->lane)

/* The three questions below each restate one walk's per-entry branches
 * (follow_walk, changes_walk_lane, changes_walk_base) for the entries whose
 * answer the listings already hold. Read each beside the branches it
 * stands for: an answer other than LANES_ASK must be the one those
 * branches reach. */

/** follow_walk, entry `name`. Pass 0 (the base's entries): the branches act
 *  on an entry the mirror lacks, on two real directories, and on a file
 *  under a copied tree (`copied`); anything else they leave alone. Pass 1
 *  (the mirror's entries): they act only on one the base lacks. */
static enum lanes_settled
lanes_follow_settled(
    const struct lanes_pair* pair,
    int pass,
    const char* name,
    int copied)
{
    const struct lanes_listing_row* base;
    const struct lanes_listing_row* lane;

    if( !pair->base.valid || !pair->lane.valid )
        return LANES_ASK;
    base = lanes_listing_find(&pair->base, name);
    /* stat() follows a link in the base; the listing does not. */
    if( !base || (base->attrs & FILE_ATTRIBUTE_REPARSE_POINT) )
        return LANES_ASK;
    if( pass == 1 )
        return LANES_NOTHING;
    lane = lanes_listing_find(&pair->lane, name);
    if( !lane )
        return LANES_ASK;
    if( base->attrs & FILE_ATTRIBUTE_DIRECTORY )
        return lanes_row_is_real_dir(lane) ? LANES_DESCEND : LANES_NOTHING;
    if( copied )
        return LANES_ASK;
    /* A file both have: the base's own file under the mirror's name (a live
     * hard link) or a symbolic link is left alone; a file of its own -- the
     * lane's edit, or a link's file the base has replaced -- is asked. */
    if( (lane->attrs & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))
        || lanes_rows_same_file(&pair->lane, lane, &pair->base, base) )
        return LANES_NOTHING;
    return LANES_ASK;
}

/** changes_walk_lane, entry `name` of the mirror: a link is unchanged, a
 *  real directory is walked, a real file is compared with the base's. */
static enum lanes_settled
lanes_changes_lane_settled(
    const struct lanes_pair* pair,
    const char* name)
{
    const struct lanes_listing_row* lane = lanes_listing_find(&pair->lane, name);
    const struct lanes_listing_row* base;

    if( !lane )
        return LANES_ASK;
    if( lane->attrs & FILE_ATTRIBUTE_REPARSE_POINT )
        return LANES_NOTHING;
    if( lane->attrs & FILE_ATTRIBUTE_DIRECTORY )
        return LANES_DESCEND;
    base = lanes_listing_find(&pair->base, name);
    return base && lanes_rows_same_file(&pair->lane, lane, &pair->base, base) ? LANES_NOTHING : LANES_ASK;
}

/** changes_walk_base, entry `name` of the base: one the mirror lacks may be
 *  a deletion, two real directories are walked, anything else is left. */
static enum lanes_settled
lanes_changes_base_settled(
    const struct lanes_pair* pair,
    const char* name)
{
    const struct lanes_listing_row* base;
    const struct lanes_listing_row* lane;

    if( !pair->base.valid || !pair->lane.valid )
        return LANES_ASK;
    base = lanes_listing_find(&pair->base, name);
    if( !base || (base->attrs & FILE_ATTRIBUTE_REPARSE_POINT) )
        return LANES_ASK;
    lane = lanes_listing_find(&pair->lane, name);
    if( !lane )
        return LANES_ASK;
    return (base->attrs & FILE_ATTRIBUTE_DIRECTORY) && lanes_row_is_real_dir(lane) ? LANES_DESCEND : LANES_NOTHING;
}
#else
#define lanes_mkdir(path) mkdir(path, 0755)

/* No listings here (see the Windows half): lstat() is one call and the
 * mirror's links are symbolic, so every entry is asked for, as always. */
struct lanes_pair
{
    int unused;
};

static void
lanes_pair_read(
    struct lanes_pair* pair,
    const char* base_dir,
    const char* lane_dir)
{
    (void)pair;
    (void)base_dir;
    (void)lane_dir;
}

static void
lanes_pair_free(struct lanes_pair* pair)
{
    (void)pair;
}

/** The names of one directory, for a walk's loop: readdir's. */
struct lanes_names
{
    DIR* dir;
};

/** 1 when `path` can be read. */
static int
lanes_names_open(
    struct lanes_names* names,
    const char* path)
{
    names->dir = opendir(path);
    return names->dir != NULL;
}

/** The next entry's name ("." and ".." never), NULL after the last. */
static const char*
lanes_names_next(struct lanes_names* names)
{
    struct dirent* entry;

    while( (entry = readdir(names->dir)) )
    {
        if( strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0 )
            return entry->d_name;
    }
    return NULL;
}

static void
lanes_names_close(struct lanes_names* names)
{
    closedir(names->dir);
    names->dir = NULL;
}

/** Which of a pair's two directories a walk's loop names: nothing here. */
#define lanes_names_open_base(names, path, pair) lanes_names_open(names, path)
#define lanes_names_open_lane(names, path, pair) lanes_names_open(names, path)

static enum lanes_settled
lanes_follow_settled(
    const struct lanes_pair* pair,
    int pass,
    const char* name,
    int copied)
{
    (void)pair;
    (void)pass;
    (void)name;
    (void)copied;
    return LANES_ASK;
}

static enum lanes_settled
lanes_changes_lane_settled(
    const struct lanes_pair* pair,
    const char* name)
{
    (void)pair;
    (void)name;
    return LANES_ASK;
}

static enum lanes_settled
lanes_changes_base_settled(
    const struct lanes_pair* pair,
    const char* name)
{
    (void)pair;
    (void)name;
    return LANES_ASK;
}
#endif

int
ToriRSMapEdLanes_IsLink(const char* path)
{
    struct stat st;

    assert(path);
    return lstat(path, &st) == 0 && S_ISLNK(st.st_mode);
}

int
ToriRSMapEdLanes_LinkDirectory(
    const char* target,
    const char* link_path)
{
    assert(target);
    assert(link_path);
    return symlink(target, link_path) == 0;
}

/* ---- mirror.py's rules ---------------------------------------------------- */

/** Pure inputs nothing in a build or a save writes: one directory symlink
 *  each at the tree's root of a LEGACY mirror, whose walks never look into
 *  them while they are links (root_linked_skipped). A sparse lane gives
 *  them no special treatment: they are links like every other directory,
 *  and explode on a write like every other. */
static const char* const k_linked[] = { "animayas", "animsets", "binary", "fonts", "framemaps", "jingles",
    "models", "samples", "songs", "sprites", "synth", "textures", "wiki", "npc_combat", NULL };
/** Copied, not linked. Not because a ledger is rewritten in place:
 *  tools/ss_allocate.py has written by a temporary and os.replace since
 *  2026-11-07, which a link would survive. pack/ stays a copy because a
 *  lane and its base both append ids to the same ledgers, and Follow's
 *  three-way check (follow_copied) needs the lane's own copy and the
 *  ancestor it was taken from (<lane>/ancestor/pack) to tell the lane's
 *  appends from the base's. */
static const char* const k_copied[] = { "pack", NULL };
/** Pack outputs of the tree: a lane has its own, under its live dir. */
static const char* const k_skipped[] = { "server/pack", "server/scripts/build", NULL };

static int
in_list(
    const char* const* list,
    const char* name)
{
    for( int i = 0; list[i]; i++ )
    {
        if( strcmp(list[i], name) == 0 )
            return 1;
    }
    return 0;
}

/** mirror.py `skipped(rel)`: locks, the pack outputs, server/scripts/build_*. */
static int
rel_skipped(const char* rel)
{
    size_t const length = strlen(rel);
    const char* tail;

    if( length >= 5 && strcmp(rel + length - 5, ".lock") == 0 )
        return 1;
    if( in_list(k_skipped, rel) )
        return 1;
    tail = strrchr(rel, '/');
    if( tail && (size_t)(tail - rel) == strlen("server/scripts") && strncmp(rel, "server/scripts", tail - rel) == 0
        && strncmp(tail + 1, "build_", 6) == 0 )
        return 1;
    return 0;
}

/** A save's temporary (editor_host_local.c write_file_atomic): never a change. */
static int
rel_transient(const char* rel)
{
    size_t const length = strlen(rel);
    return (length >= 4 && strcmp(rel + length - 4, ".tmp") == 0) || rel_skipped(rel);
}

static int
mkdir_p(const char* path)
{
    char copy[TORIRSMAPED_LANE_PATH];
    size_t length;

    snprintf(copy, sizeof(copy), "%s", path);
    length = strlen(copy);
    for( size_t i = 1; i < length; i++ )
    {
        if( copy[i] != '/' )
            continue;
        copy[i] = '\0';
        if( lanes_mkdir(copy) != 0 && errno != EEXIST )
            return 0;
        copy[i] = '/';
    }
    return lanes_mkdir(copy) == 0 || errno == EEXIST;
}

/** A NEW file at `path`, open for writing; never whatever is there already.
 *  A name left at `path` -- a stray temporary, or a LINK to the base's file
 *  (a mirror made before temporaries were left out of mirrors linked them) --
 *  loses its own name first, and the file is then created exclusively, so a
 *  link at a temporary's name is never opened and the base's file it names is
 *  never written. NULL with errno when it cannot be. */
static FILE*
fopen_new(const char* path)
{
    FILE* file;
    int fd;

    if( unlink(path) != 0 && errno != ENOENT )
        return NULL;
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0644);
    if( fd < 0 )
        return NULL;
    file = fdopen(fd, "wb");
    assert(file);
    return file;
}

static int
copy_file(
    const char* src,
    const char* dst,
    char* why,
    size_t why_size)
{
    char temp[TORIRSMAPED_LANE_PATH + 8];
    char buffer[65536];
    FILE* in;
    FILE* out;
    size_t got;
    struct stat st;

    in = fopen(src, "rb");
    if( !in )
    {
        snprintf(why, why_size, "cannot read %s: %s", src, strerror(errno));
        return 0;
    }
    snprintf(temp, sizeof(temp), "%s.tmp", dst);
    out = fopen_new(temp);
    if( !out )
    {
        fclose(in);
        snprintf(why, why_size, "cannot write %s: %s", temp, strerror(errno));
        return 0;
    }
    while( (got = fread(buffer, 1, sizeof(buffer), in)) > 0 )
    {
        if( fwrite(buffer, 1, got, out) != got )
        {
            fclose(in);
            fclose(out);
            remove(temp);
            snprintf(why, why_size, "short write to %s", temp);
            return 0;
        }
    }
    fclose(in);
    if( fclose(out) != 0 )
    {
        remove(temp);
        snprintf(why, why_size, "cannot close %s: %s", temp, strerror(errno));
        return 0;
    }
    if( stat(src, &st) == 0 )
        chmod(temp, st.st_mode & 0777);
    if( rename(temp, dst) != 0 )
    {
        remove(temp);
        snprintf(why, why_size, "cannot rename onto %s: %s", dst, strerror(errno));
        return 0;
    }
    return 1;
}

/** Byte equality of two files (sizes first). */
static int
files_equal(
    const char* a,
    const char* b)
{
    struct stat sa;
    struct stat sb;
    FILE* fa;
    FILE* fb;
    int equal = 1;

    if( stat(a, &sa) != 0 || stat(b, &sb) != 0 )
        return 0;
    if( sa.st_size != sb.st_size )
        return 0;
    fa = fopen(a, "rb");
    fb = fopen(b, "rb");
    if( !fa || !fb )
    {
        if( fa )
            fclose(fa);
        if( fb )
            fclose(fb);
        return 0;
    }
    while( equal )
    {
        char ba[32768];
        char bb[32768];
        size_t ga = fread(ba, 1, sizeof(ba), fa);
        size_t gb = fread(bb, 1, sizeof(bb), fb);
        if( ga != gb || memcmp(ba, bb, ga) != 0 )
            equal = 0;
        if( ga == 0 )
            break;
    }
    fclose(fa);
    fclose(fb);
    return equal;
}

/** mirror_tree(src, dst, rel, copy): real directories, a symlink (or a
 *  copy) per file. Returns files mirrored, -1 on an error. */
static int
mirror_tree(
    const char* src,
    const char* dst,
    const char* rel,
    int copy,
    char* why,
    size_t why_size)
{
    DIR* dir;
    struct dirent* entry;
    int count = 0;

    if( !mkdir_p(dst) )
    {
        snprintf(why, why_size, "cannot create %s: %s", dst, strerror(errno));
        return -1;
    }
    dir = opendir(src);
    if( !dir )
    {
        snprintf(why, why_size, "cannot read %s: %s", src, strerror(errno));
        return -1;
    }
    while( (entry = readdir(dir)) )
    {
        char sub_rel[TORIRSMAPED_LANE_PATH];
        char source[TORIRSMAPED_LANE_PATH];
        char target[TORIRSMAPED_LANE_PATH];
        struct stat st;

        if( strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 )
            continue;
        if( rel[0] )
            snprintf(sub_rel, sizeof(sub_rel), "%s/%s", rel, entry->d_name);
        else
            snprintf(sub_rel, sizeof(sub_rel), "%s", entry->d_name);
        /* A temporary is never mirrored: linked, it is the base's file under
         * the name the next save opens to write (rel_transient). */
        if( rel_transient(sub_rel) )
            continue;
        snprintf(source, sizeof(source), "%s/%s", src, entry->d_name);
        snprintf(target, sizeof(target), "%s/%s", dst, entry->d_name);
        if( stat(source, &st) == 0 && S_ISDIR(st.st_mode) )
        {
            int const sub = mirror_tree(source, target, sub_rel, copy, why, why_size);
            if( sub < 0 )
            {
                closedir(dir);
                return -1;
            }
            count += sub;
        }
        else if( copy )
        {
            if( !copy_file(source, target, why, why_size) )
            {
                closedir(dir);
                return -1;
            }
            count++;
        }
        else
        {
            if( symlink(source, target) != 0 )
            {
                snprintf(why, why_size, "cannot link %s: %s", target, strerror(errno));
                closedir(dir);
                return -1;
            }
            count++;
        }
    }
    closedir(dir);
    return count;
}

int
ToriRSMapEdLanes_Mirror(
    const char* base,
    const char* dst,
    char* why,
    size_t why_size)
{
    char real_base[PATH_MAX];
    DIR* dir;
    struct dirent* entry;
    struct stat st;
    int files = 0;

    assert(base);
    assert(dst);
    assert(why);
    why[0] = '\0';
    if( !realpath(base, real_base) )
    {
        snprintf(why, why_size, "no such tree: %s", base);
        return -1;
    }
    if( lstat(dst, &st) == 0 )
    {
        snprintf(why, why_size, "%s exists; remove it first", dst);
        return -1;
    }
    if( strncmp(dst, real_base, strlen(real_base)) == 0 && dst[strlen(real_base)] == '/' )
    {
        snprintf(why, why_size, "the mirror may not be inside the tree");
        return -1;
    }
    if( !mkdir_p(dst) )
    {
        snprintf(why, why_size, "cannot create %s: %s", dst, strerror(errno));
        return -1;
    }
    dir = opendir(real_base);
    if( !dir )
    {
        snprintf(why, why_size, "cannot read %s: %s", real_base, strerror(errno));
        return -1;
    }
    while( (entry = readdir(dir)) )
    {
        char source[TORIRSMAPED_LANE_PATH];
        char target[TORIRSMAPED_LANE_PATH];

        if( strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 )
            continue;
        if( rel_transient(entry->d_name) )
            continue;
        snprintf(source, sizeof(source), "%s/%s", real_base, entry->d_name);
        snprintf(target, sizeof(target), "%s/%s", dst, entry->d_name);
        if( !(stat(source, &st) == 0 && S_ISDIR(st.st_mode)) || in_list(k_linked, entry->d_name) )
        {
            if( symlink(source, target) != 0 )
            {
                snprintf(why, why_size, "cannot link %s: %s", target, strerror(errno));
                closedir(dir);
                return -1;
            }
        }
        else
        {
            int const sub =
                mirror_tree(source, target, entry->d_name, in_list(k_copied, entry->d_name), why, why_size);
            if( sub < 0 )
            {
                closedir(dir);
                return -1;
            }
            files += sub;
        }
    }
    closedir(dir);
    return files;
}

/** 1 when `rel` lies under a tree the mirror copies (k_copied). */
static int
rel_copied(const char* rel)
{
    for( int i = 0; k_copied[i]; i++ )
    {
        size_t const length = strlen(k_copied[i]);
        if( strncmp(rel, k_copied[i], length) == 0 && (rel[length] == '\0' || rel[length] == '/') )
            return 1;
    }
    return 0;
}

/** <lane>/ancestor/<rel>: the base's copy of a copied file as the mirror last
 *  took it. A copy cannot follow the base the way a link does, and without
 *  the common ancestor a lane's own edit of it (the id allocation appending
 *  to a ledger) and the base moving on under it look the same. */
static void
ancestor_path(
    const struct ToriRSMapEdLaneDef* lane,
    const char* rel,
    char* out,
    size_t out_size)
{
    snprintf(out, out_size, "%s/ancestor/%s", lane->dir, rel);
}

/** A new lane's ancestors: the base's copied trees as the mirror took them. */
static int
ancestor_seed(
    const struct ToriRSMapEdLaneDef* lane,
    char* why,
    size_t why_size)
{
    for( int i = 0; k_copied[i]; i++ )
    {
        char source[TORIRSMAPED_LANE_PATH];
        char target[TORIRSMAPED_LANE_PATH];
        struct stat st;

        snprintf(source, sizeof(source), "%s/%s", lane->base, k_copied[i]);
        if( stat(source, &st) != 0 || !S_ISDIR(st.st_mode) )
            continue;
        ancestor_path(lane, k_copied[i], target, sizeof(target));
        if( mirror_tree(source, target, k_copied[i], 1, why, why_size) < 0 )
            return 0;
    }
    return 1;
}

/* ---- sparse lanes -----------------------------------------------------------
 *
 * A sparse lane's content is a thin skeleton over its base: a directory of
 * the lane is ONE LINK to the base's directory until something is about to
 * be written inside it, and then it is EXPLODED one level -- replaced by a
 * real directory holding one link per base entry (sub-directories as
 * directory links, files as file links). The walks recurse only into the
 * lane's real directories, so they cost what the lane changed, and the
 * build tools and the game server read one complete directory through the
 * links.
 *
 * THE HAZARD: a write (create, temporary + rename, mkdir, delete) that
 * reaches a directory which is still a link lands in the BASE tree. So every
 * write this file makes into a lane's content goes through
 * lanes_make_writable first, every write the editor makes must go through
 * ToriRSMapEdLanes_MakeWritable, and the directories external build tools
 * write into are real from creation (sparse_real).
 */

/** Exploded at creation besides sparse_real's ancestors: the first thing a
 *  map editor writes, one flat directory of ~11,800 squares. */
static const char* const k_exploded[] = { "maps", NULL };

/** 1 when a sparse lane keeps directory `rel` real, never a link: an
 *  ancestor of a k_skipped or k_copied path (so the base's pack outputs
 *  never show through and the tools' outputs land in the lane: "server",
 *  "server/scripts"), or k_exploded. */
static int
sparse_real(const char* rel)
{
    const char* const* const lists[2] = { k_skipped, k_copied };
    size_t const length = strlen(rel);

    if( in_list(k_exploded, rel) )
        return 1;
    for( int l = 0; l < 2; l++ )
    {
        for( int i = 0; lists[l][i]; i++ )
        {
            const char* const path = lists[l][i];
            if( strlen(path) > length && strncmp(path, rel, length) == 0 && path[length] == '/' )
                return 1;
        }
    }
    return 0;
}

/** `dst` made a real directory showing `src` (the base's directory for
 *  `rel`, "" the root): one link per entry, except a directory a sparse lane
 *  keeps real (filled the same way) and a copied tree (mirror_tree's
 *  copies). rel_skipped entries are left out, as the mirror leaves them.
 *  Only ever writes under `dst`; only ever reads `src`. Returns the entries
 *  linked or files copied, -1 with `why`. */
static int
sparse_fill(
    const char* src,
    const char* dst,
    const char* rel,
    char* why,
    size_t why_size)
{
    DIR* dir;
    struct dirent* entry;
    int count = 0;

    if( !mkdir_p(dst) )
    {
        snprintf(why, why_size, "cannot create %s: %s", dst, strerror(errno));
        return -1;
    }
    dir = opendir(src);
    if( !dir )
    {
        snprintf(why, why_size, "cannot read %s: %s", src, strerror(errno));
        return -1;
    }
    while( (entry = readdir(dir)) )
    {
        char sub_rel[TORIRSMAPED_LANE_PATH];
        char source[TORIRSMAPED_LANE_PATH];
        char target[TORIRSMAPED_LANE_PATH];
        struct stat st;
        int is_dir;
        int sub;

        if( strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 )
            continue;
        snprintf(sub_rel, sizeof(sub_rel), "%s%s%s", rel, rel[0] ? "/" : "", entry->d_name);
        /* Temporaries too (mirror_tree): a linked one is the base's file. */
        if( rel_transient(sub_rel) )
            continue;
        snprintf(source, sizeof(source), "%s/%s", src, entry->d_name);
        snprintf(target, sizeof(target), "%s/%s", dst, entry->d_name);
        is_dir = stat(source, &st) == 0 && S_ISDIR(st.st_mode);
        if( is_dir && rel_copied(sub_rel) )
            sub = mirror_tree(source, target, sub_rel, 1, why, why_size);
        else if( is_dir && sparse_real(sub_rel) )
            sub = sparse_fill(source, target, sub_rel, why, why_size);
        else if( symlink(source, target) != 0 )
        {
            snprintf(why, why_size, "cannot link %s: %s", target, strerror(errno));
            sub = -1;
        }
        else
            sub = 1;
        if( sub < 0 )
        {
            closedir(dir);
            return -1;
        }
        count += sub;
    }
    closedir(dir);
    return count;
}

/**
 * Remove `path` WITHOUT following a link. A link -- to a file or to a
 * directory -- and a file lose their own name only (unlink: on Windows a
 * junction or a directory symlink goes by RemoveDirectory, which removes the
 * reparse point and never touches what it points at). Only a REAL directory
 * is entered and emptied, and lstat() says S_IFDIR for nothing else: on
 * Windows it reads every reparse point as S_IFLNK. Deleting THROUGH a link
 * would delete the base's files, so this is the only recursive delete in the
 * file, and it only ever runs on an explode's temporary that an explode built
 * out of links. 1 when `path` is gone.
 */
static int
remove_tree_no_follow(
    const char* path,
    char* why,
    size_t why_size)
{
    struct stat st;
    DIR* dir;
    struct dirent* entry;

    if( lstat(path, &st) != 0 )
        return 1;
    if( !S_ISDIR(st.st_mode) )
    {
        if( unlink(path) != 0 )
        {
            snprintf(why, why_size, "cannot remove %s: %s", path, strerror(errno));
            return 0;
        }
        return 1;
    }
    dir = opendir(path);
    if( !dir )
    {
        snprintf(why, why_size, "cannot read %s: %s", path, strerror(errno));
        return 0;
    }
    while( (entry = readdir(dir)) )
    {
        char sub[TORIRSMAPED_LANE_PATH];

        if( strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 )
            continue;
        snprintf(sub, sizeof(sub), "%s/%s", path, entry->d_name);
        if( !remove_tree_no_follow(sub, why, why_size) )
        {
            closedir(dir);
            return 0;
        }
    }
    closedir(dir);
    if( rmdir(path) != 0 )
    {
        snprintf(why, why_size, "cannot remove %s: %s", path, strerror(errno));
        return 0;
    }
    return 1;
}

/** "<content>/<parent of rel>/.<last of rel>.explode.tmp": an explode's
 *  temporary. It starts with a dot because every tool that reads a lane
 *  (sscompile, cachepack, the catalog, the game server) passes over
 *  dot-names, and it ends in ".tmp" because the lane's own walks pass over
 *  those (rel_transient): a leftover is read as content by nobody. */
static void
explode_temp_path(
    const struct ToriRSMapEdLaneDef* lane,
    const char* rel,
    char* out,
    size_t out_size)
{
    const char* const slash = strrchr(rel, '/');

    if( slash )
        snprintf(out, out_size, "%s/%.*s/.%s.explode.tmp", lane->content, (int)(slash - rel), rel, slash + 1);
    else
        snprintf(out, out_size, "%s/.%s.explode.tmp", lane->content, rel);
}

/** 1 when directory entry `name` is an explode's temporary (explode_temp_path),
 *  with the name of the directory it stands for in `out`. */
static int
explode_temp_name(
    const char* name,
    char* out,
    size_t out_size)
{
    static const char k_suffix[] = ".explode.tmp";
    size_t const length = strlen(name);
    size_t const suffix = sizeof(k_suffix) - 1;

    if( name[0] != '.' || length <= suffix + 1 || strcmp(name + length - suffix, k_suffix) != 0 )
        return 0;
    snprintf(out, out_size, "%.*s", (int)(length - suffix - 1), name + 1);
    return 1;
}

/* ---- one explode at a time, per lane ----------------------------------------
 *
 * Two processes can make a directory of one lane real at once: the editor
 * serving the lane saves into it while another editor publishes into it. An
 * explode builds its temporary under a fixed name and removes a temporary it
 * finds beside the link as a crashed one's -- so two at once would delete
 * each other's half-built directory and rename a partial one into place,
 * and every base file it lacks would read as deleted by the lane (and be
 * deleted from the base by its next publish). "<lane dir>/explode.lock"
 * (outside the content: no walk sees it) makes them take turns, and makes a
 * temporary found by the lock's holder a crashed explode's, never a live one.
 */

/** A short wait (Windows' usleep is the CRT's, which spins on some runtimes:
 *  Sleep yields, as torirs_maped_build.c's jobs_sleep_ms). */
static void
lanes_sleep_ms(int ms)
{
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)ms * 1000);
#endif
}

/** The pid holding the lock file `path`; 0 when nobody does any more (that
 *  process is gone, or it died before writing its pid long ago); -1 when it
 *  cannot be said yet (just created, its pid not written). */
static long
explode_lock_holder(const char* path)
{
    char text[64];
    struct stat st;
    long pid = 0;
    size_t got;
    FILE* file = fopen(path, "rb");

    if( !file )
        return -1; /* gone between our create and this read: try again */
    got = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[got] = '\0';
    if( strncmp(text, "pid ", 4) == 0 )
        pid = strtol(text + 4, NULL, 10);
    if( pid > 0 )
        return kill((pid_t)pid, 0) != 0 && errno == ESRCH ? 0 : pid;
    if( stat(path, &st) == 0 && (long long)time(NULL) - (long long)st.st_mtime > 10 )
        return 0;
    return -1;
}

static void
explode_lock_path(
    const struct ToriRSMapEdLaneDef* lane,
    char* out,
    size_t out_size)
{
    snprintf(out, out_size, "%s/explode.lock", lane->dir);
}

/** Take the lane's explode lock, waiting for a live holder (up to
 *  TORIRSMAPED_LANES_LOCK_WAIT_MS, 60 s by default) and breaking a dead
 *  one's. 1 when held, 0 with `why`. */
static int
explode_lock(
    const struct ToriRSMapEdLaneDef* lane,
    char* why,
    size_t why_size)
{
    char path[TORIRSMAPED_LANE_PATH + 16];
    const char* const env = getenv("TORIRSMAPED_LANES_LOCK_WAIT_MS");
    long const limit = env && env[0] ? strtol(env, NULL, 10) : 60000;
    long waited = 0;

    assert(lane->dir[0]);
    explode_lock_path(lane, path, sizeof(path));
    for( ;; )
    {
        char text[64];
        long holder;
        int const fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0644);

        if( fd >= 0 )
        {
            int const length = snprintf(text, sizeof(text), "pid %ld\n", (long)getpid());
            int const wrote = (int)write(fd, text, (size_t)length);
            close(fd);
            if( wrote != length )
            {
                unlink(path);
                snprintf(why, why_size, "cannot write %s", path);
                return 0;
            }
            return 1;
        }
        if( errno != EEXIST )
        {
            snprintf(why, why_size, "cannot take %s: %s", path, strerror(errno));
            return 0;
        }
        holder = explode_lock_holder(path);
        if( holder == 0 )
        {
            /* Its process is gone (a crash mid-explode). Two waiters can
             * both find it so and both remove it, one of them the other's
             * fresh lock; that needs a crash and a race in the same tens of
             * milliseconds, and costs no more than this lock did before. */
            if( unlink(path) != 0 && errno != ENOENT )
            {
                snprintf(why, why_size, "cannot remove the dead lock %s: %s", path, strerror(errno));
                return 0;
            }
            continue;
        }
        if( waited >= limit )
        {
            snprintf(why, why_size,
                "%s is held by pid %ld: another editor is making a directory of lane '%s' real; try again, or "
                "remove the file if no such process runs",
                path, holder, lane->name);
            return 0;
        }
        lanes_sleep_ms(10);
        waited += 10;
    }
}

static void
explode_unlock(const struct ToriRSMapEdLaneDef* lane)
{
    char path[TORIRSMAPED_LANE_PATH + 16];

    explode_lock_path(lane, path, sizeof(path));
    unlink(path);
}

/* ---- when the lane last looked at a directory ---------------------------------
 *
 * A base entry the lane lacks is the lane's deletion when the lane had it,
 * and new in the base when it never did; the walks tell the two apart by
 * the entry's time against the lane's last look at the base (`created`, then
 * each follow). A directory made real AFTER that -- exploded, or filled from
 * the base, by a write -- was looked at again then: everything the base had
 * in it was linked, including entries newer than `created` that the lane
 * showed through its link. Without its own time, deleting one of those reads
 * as nothing (it is "newer than the lane") and the next follow links it back.
 * So each such directory's time goes into "<lane dir>/exploded.ini", one
 * "<time> <rel>" line, and the walks take the later of it and `created`.
 */

struct lanes_look
{
    long long at;
    char* rel;
};

struct lanes_looks
{
    struct lanes_look* rows;
    int count;
    int capacity;
};

static void
looks_path(
    const struct ToriRSMapEdLaneDef* lane,
    char* out,
    size_t out_size)
{
    snprintf(out, out_size, "%s/exploded.ini", lane->dir);
}

/** `rel` looked at, at `at`: appended (under the explode lock). 1, or 0 with
 *  `why`. */
static int
look_record(
    const struct ToriRSMapEdLaneDef* lane,
    const char* rel,
    long long at,
    char* why,
    size_t why_size)
{
    char path[TORIRSMAPED_LANE_PATH + 16];
    FILE* file;

    looks_path(lane, path, sizeof(path));
    file = fopen(path, "ab");
    if( !file )
    {
        snprintf(why, why_size, "cannot write %s: %s", path, strerror(errno));
        return 0;
    }
    fprintf(file, "%lld %s\n", at, rel);
    if( fclose(file) != 0 )
    {
        snprintf(why, why_size, "cannot write %s: %s", path, strerror(errno));
        return 0;
    }
    return 1;
}

/** The lane's looks, read whole (none for main or a lane that exploded
 *  nothing). malloc, not ToriMem: the change scan runs on a worker thread. A
 *  last line without its newline is one being appended: left out. */
static void
looks_read(
    const struct ToriRSMapEdLaneDef* lane,
    struct lanes_looks* out)
{
    char path[TORIRSMAPED_LANE_PATH + 16];
    char line[TORIRSMAPED_LANE_PATH + 32];
    FILE* file;

    memset(out, 0, sizeof(*out));
    if( lane->is_main || !lane->dir[0] )
        return;
    looks_path(lane, path, sizeof(path));
    file = fopen(path, "rb");
    if( !file )
        return;
    while( fgets(line, sizeof(line), file) )
    {
        size_t length = strlen(line);
        char* space;
        struct lanes_look* row;

        if( length == 0 || line[length - 1] != '\n' )
            continue;
        while( length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r') )
            line[--length] = '\0';
        space = strchr(line, ' ');
        if( !space || !space[1] )
            continue;
        *space = '\0';
        if( out->count == out->capacity )
        {
            out->capacity = out->capacity ? out->capacity * 2 : 16;
            out->rows = realloc(out->rows, (size_t)out->capacity * sizeof(*out->rows));
            assert(out->rows);
        }
        row = &out->rows[out->count++];
        row->at = strtoll(line, NULL, 10);
        row->rel = strdup(space + 1);
        assert(row->rel);
    }
    fclose(file);
}

static void
looks_free(struct lanes_looks* looks)
{
    for( int i = 0; i < looks->count; i++ )
        free(looks->rows[i].rel);
    free(looks->rows);
    memset(looks, 0, sizeof(*looks));
}

/** The lane's last look at its real directory `rel` ("" the root): the base
 *  entries of it newer than this are new in the base, older ones the lane
 *  had. */
static long long
looked_at(
    const struct ToriRSMapEdLaneDef* lane,
    const struct lanes_looks* looks,
    const char* rel)
{
    long long at = lane->created;

    for( int i = 0; i < looks->count; i++ )
    {
        if( looks->rows[i].at > at && strcmp(looks->rows[i].rel, rel) == 0 )
            at = looks->rows[i].at;
    }
    return at;
}

/** 1 when base entry time `mtime` is newer than the look `looked` (so the lane
 *  never had it). A lane with no known look had everything. */
static int
newer_than_look(
    long long mtime,
    long long looked)
{
    return looked > 0 && mtime > looked;
}

/* ---- what the lane wrote, and what only fell behind -------------------------
 *
 * On Windows without Developer Mode a lane's file link is a HARD link, and a
 * hard link does not follow its base: when the base's file is replaced (a
 * pull, a publish from another lane, a save in main) the lane keeps the old
 * file under its own name. That file is real, it differs from the base's, and
 * the lane never wrote it. Until 2026-11-10 it read as the lane's change: the
 * owner's four-day-old lane showed "974 changed" with two edits in it, RUN
 * refused it over a pack file nobody had touched, and a publish to main would
 * have written the old bytes over ~960 newer files.
 *
 * What tells the two apart: a file the lane wrote was written AFTER the lane
 * took its links, and a file that fell behind was not. So a real file that
 * differs from the base's is the lane's when its time is not before the
 * lane's last look at its directory (looked_at), and otherwise it fell
 * behind: the change scan passes over it and the follow links it again.
 *
 * A follow moves that look forward, and a file the lane wrote before it would
 * then read as fallen behind. So the follow writes down every file it found
 * to be the lane's -- "<lane dir>/owned.ini", one path a line -- before it
 * moves the look, and a file named there is the lane's whatever its time.
 * Publish with reset and Discard take the files they link again back out.
 *
 * A lane followed by a build older than this has no list for the files it
 * wrote before that follow, so nothing in it is said to have fallen behind
 * until its first follow here writes one (`known`).
 *
 * The one thing this gets wrong: a file put into a lane by something that
 * keeps its old time (an Explorer copy, cp -p) over a name the lane linked
 * reads as fallen behind. The editor and its tools stamp what they write.
 */

struct lanes_owned
{
    char** rels;
    int count;
    int capacity;
    /** 0: the list cannot be trusted to be whole (see above). */
    int known;
    /** A lane that was never followed: when its creation ended (lane.ini is
     *  written last, once). 0 for any other lane. Its copied files (pack/)
     *  dated up to then are the copies the creation made. */
    long long creation_end;
};

static void
owned_path(
    const struct ToriRSMapEdLaneDef* lane,
    char* out,
    size_t out_size)
{
    snprintf(out, out_size, "%s/owned.ini", lane->dir);
}

static int
owned_compare(
    const void* a,
    const void* b)
{
    return strcmp(*(char* const*)a, *(char* const*)b);
}

static void
owned_add(
    struct lanes_owned* owned,
    const char* rel)
{
    if( owned->count == owned->capacity )
    {
        owned->capacity = owned->capacity ? owned->capacity * 2 : 16;
        owned->rels = realloc(owned->rels, (size_t)owned->capacity * sizeof(*owned->rels));
        assert(owned->rels);
    }
    owned->rels[owned->count] = strdup(rel);
    assert(owned->rels[owned->count]);
    owned->count++;
}

/** The lane's list, read whole and sorted. malloc, not ToriMem: the change
 *  scan runs on a worker thread. */
static void
owned_read(
    const struct ToriRSMapEdLaneDef* lane,
    struct lanes_owned* out)
{
    char path[TORIRSMAPED_LANE_PATH + 16];
    char line[TORIRSMAPED_LANE_PATH + 32];
    struct stat st;
    int followed = 0;
    FILE* file;

    memset(out, 0, sizeof(*out));
    if( lane->is_main || !lane->dir[0] )
        return;
    snprintf(path, sizeof(path), "%s/lane.ini", lane->dir);
    file = fopen(path, "rb");
    if( file )
    {
        while( fgets(line, sizeof(line), file) )
        {
            if( strncmp(line, "followed = ", 11) == 0 )
                followed = 1;
        }
        fclose(file);
    }
    if( !followed && stat(path, &st) == 0 )
        out->creation_end = (long long)st.st_mtime;
    owned_path(lane, path, sizeof(path));
    file = fopen(path, "rb");
    if( !file )
    {
        out->known = !followed;
        return;
    }
    out->known = 1;
    while( fgets(line, sizeof(line), file) )
    {
        size_t length = strlen(line);

        while( length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r') )
            line[--length] = '\0';
        if( length > 0 && line[0] != ';' )
            owned_add(out, line);
    }
    fclose(file);
    if( out->count > 1 )
        qsort(out->rels, (size_t)out->count, sizeof(*out->rels), owned_compare);
}

static void
owned_free(struct lanes_owned* owned)
{
    for( int i = 0; i < owned->count; i++ )
        free(owned->rels[i]);
    free(owned->rels);
    memset(owned, 0, sizeof(*owned));
}

/** 1 when the sorted list (owned_read) names `rel`. */
static int
owned_has(
    const struct lanes_owned* owned,
    const char* rel)
{
    return owned->count > 0
           && bsearch(&rel, owned->rels, (size_t)owned->count, sizeof(*owned->rels), owned_compare) != NULL;
}

/** The list written whole (a temporary, then a rename). 1, or 0 with `why`. */
static int
owned_write(
    const struct ToriRSMapEdLaneDef* lane,
    struct lanes_owned* owned,
    char* why,
    size_t why_size)
{
    char path[TORIRSMAPED_LANE_PATH + 16];
    char temp[TORIRSMAPED_LANE_PATH + 32];
    FILE* file;

    owned_path(lane, path, sizeof(path));
    snprintf(temp, sizeof(temp), "%s.tmp", path);
    if( owned->count > 1 )
        qsort(owned->rels, (size_t)owned->count, sizeof(*owned->rels), owned_compare);
    file = fopen(temp, "wb");
    if( !file )
    {
        snprintf(why, why_size, "cannot write %s: %s", temp, strerror(errno));
        return 0;
    }
    fprintf(file, "; The files this lane wrote itself (torirs_maped_lanes.c, \"what the lane\n"
                  "; wrote\"): any other file that differs from the base's only fell behind it.\n");
    for( int i = 0; i < owned->count; i++ )
    {
        if( i == 0 || strcmp(owned->rels[i], owned->rels[i - 1]) != 0 )
            fprintf(file, "%s\n", owned->rels[i]);
    }
    if( fclose(file) != 0 || rename(temp, path) != 0 )
    {
        remove(temp);
        snprintf(why, why_size, "cannot write %s: %s", path, strerror(errno));
        return 0;
    }
    return 1;
}

/** After a publish with reset or a discard linked files again: the list
 *  without the paths that are no longer the lane's own files. A lane with no
 *  list keeps none. 1, or 0 with `why`. */
static int
owned_prune(
    const struct ToriRSMapEdLaneDef* lane,
    char* why,
    size_t why_size)
{
    struct lanes_owned owned;
    struct lanes_owned kept;
    char path[TORIRSMAPED_LANE_PATH];
    struct stat st;
    int ok = 1;

    owned_path(lane, path, sizeof(path));
    if( lane->is_main || stat(path, &st) != 0 )
        return 1;
    owned_read(lane, &owned);
    memset(&kept, 0, sizeof(kept));
    for( int i = 0; i < owned.count; i++ )
    {
        snprintf(path, sizeof(path), "%s/%s", lane->content, owned.rels[i]);
        if( lstat(path, &st) == 0 && S_ISREG(st.st_mode) )
            owned_add(&kept, owned.rels[i]);
    }
    if( kept.count != owned.count )
        ok = owned_write(lane, &kept, why, why_size);
    owned_free(&owned);
    owned_free(&kept);
    return ok;
}

/** 1 when the lane's real file `rel` (in its real directory `dir_rel`, with
 *  `st` its own stat) was not written by the lane: it is a link's file that
 *  the base has since replaced or removed. Only where a file link is a hard
 *  link; a symbolic link shows the base's file as it is now. */
static int
lane_file_fell_behind(
    const struct ToriRSMapEdLaneDef* lane,
    const struct lanes_looks* looks,
    const struct lanes_owned* owned,
    const char* dir_rel,
    const char* rel,
    const struct stat* st)
{
#ifdef _WIN32
    long long const looked = looked_at(lane, looks, dir_rel);

    /* Strictly older: the clock here is whole seconds, and a file the lane
     * wrote in the very second of its look is the lane's. */
    if( !owned->known || looked <= 0 || (long long)st->st_mtime >= looked )
        return 0;
    return !owned_has(owned, rel);
#else
    (void)lane;
    (void)looks;
    (void)owned;
    (void)dir_rel;
    (void)rel;
    (void)st;
    return 0;
#endif
}

/** 1 when the lane's copy `lane_path` of the copied file `rel` (pack/) is
 *  still the copy the lane took: equal to the ancestor it recorded, or, in a
 *  never-followed lane with no ancestor, dated within the lane's creation. */
static int
lane_copy_untouched(
    const struct ToriRSMapEdLaneDef* lane,
    const struct lanes_owned* owned,
    const char* rel,
    const char* lane_path,
    const struct stat* st)
{
    char ancestor[TORIRSMAPED_LANE_PATH + 16];
    struct stat ast;

    ancestor_path(lane, rel, ancestor, sizeof(ancestor));
    if( stat(ancestor, &ast) == 0 )
        return files_equal(lane_path, ancestor);
    return owned->creation_end > 0 && (long long)st->st_mtime < owned->creation_end;
}

/** 1 when the lane's entry `lane_path` is the base's own file `base_path`
 *  under a second name, or a symbolic link (which shows the base's as it
 *  is). 0: a hard link to a file the base has since replaced. */
static int
lane_link_is_live(
    const char* lane_path,
    const char* base_path)
{
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION info[2];
    const char* const paths[2] = { lane_path, base_path };
    DWORD const attrs = GetFileAttributesA(lane_path);

    if( attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT) )
        return 1;
    for( int i = 0; i < 2; i++ )
    {
        HANDLE const handle = CreateFileA(paths[i], FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
        int ok;

        if( handle == INVALID_HANDLE_VALUE )
            return 1; /* cannot say: left alone */
        ok = GetFileInformationByHandle(handle, &info[i]) != 0;
        CloseHandle(handle);
        if( !ok )
            return 1;
    }
    return info[0].dwVolumeSerialNumber == info[1].dwVolumeSerialNumber
           && info[0].nFileIndexHigh == info[1].nFileIndexHigh && info[0].nFileIndexLow == info[1].nFileIndexLow;
#else
    (void)lane_path;
    (void)base_path;
    return 1;
#endif
}

/** The real directory `dst` showing the base's directory `src` for `rel`:
 *  its entries linked (sparse_fill), or copied under a copied tree. */
static int
fill_dir(
    const char* src,
    const char* dst,
    const char* rel,
    char* why,
    size_t why_size)
{
    if( rel_copied(rel) )
        return mirror_tree(src, dst, rel, 1, why, why_size);
    return sparse_fill(src, dst, rel, why, why_size);
}

/** rename(), tried again for a while: on Windows a scanner or an indexer
 *  holding a just-made entry open fails a directory's rename for a moment. */
static int
rename_patiently(
    const char* from,
    const char* to)
{
    for( int attempt = 0;; attempt++ )
    {
        if( rename(from, to) == 0 )
            return 1;
        if( attempt == 20 )
            return 0;
        lanes_sleep_ms(25);
    }
}

/** `temp` (complete) renamed into place at `path`, where nothing is. */
static int
temp_into_place(
    const char* temp,
    const char* path,
    char* why,
    size_t why_size)
{
    if( rename_patiently(temp, path) )
        return 1;
    snprintf(why, why_size, "cannot rename %s onto %s: %s (the next write or follow completes it)", temp, path,
        strerror(errno));
    return 0;
}

/**
 * The link at `path` (the lane's directory `rel`, still showing the base's
 * `base_dir`) replaced by a real directory with one link per base entry.
 * Called with the explode lock held.
 *
 * Crash-safe in steps: the new directory is built COMPLETE as `temp`
 * (explode_temp_path: a name no walk and no tool reads), the look is
 * recorded, only then is the link removed, then `temp` is renamed into place.
 * So a `temp` found with `path` absent is complete (make_real_dir_locked
 * renames it), and a `temp` found beside the link is an explode that stopped
 * part way (the lock says nobody else is building it: removed here without
 * following anything, and built again). A failure after the link is gone puts
 * the link back, so the lane never shows the directory missing.
 *
 * Nothing here can write or delete through the link: the fill reads the
 * BASE's directory by its own path and writes under `temp`; the link itself
 * goes by unlink of its own name, after lstat() said it is one.
 */
static int
explode(
    const struct ToriRSMapEdLaneDef* lane,
    const char* rel,
    const char* path,
    const char* temp,
    const char* base_dir,
    long long look,
    char* why,
    size_t why_size)
{
    char ignored[256];
    struct stat st;

    if( stat(base_dir, &st) != 0 || !S_ISDIR(st.st_mode) )
    {
        /* The base has no such directory any more: a dangling link, which
         * shows nothing. The lane's directory becomes a new empty one; the
         * link's own name is all that goes. */
        if( stat(path, &st) == 0 && !S_ISDIR(st.st_mode) )
        {
            snprintf(why, why_size, "%s is a file, not a directory", path);
            return 0;
        }
        if( !look_record(lane, rel, look, why, why_size) )
            return 0;
        if( unlink(path) != 0 || (lanes_mkdir(path) != 0 && errno != EEXIST) )
        {
            snprintf(why, why_size, "cannot replace the dangling link %s: %s", path, strerror(errno));
            return 0;
        }
        return 1;
    }
    if( stat(path, &st) != 0 || !S_ISDIR(st.st_mode) )
    {
        snprintf(why, why_size, "%s is a link to a file, not to a directory", path);
        return 0;
    }
    if( lstat(temp, &st) == 0 && !remove_tree_no_follow(temp, why, why_size) )
        return 0;
    if( fill_dir(base_dir, temp, rel, why, why_size) < 0 || !look_record(lane, rel, look, why, why_size) )
    {
        (void)remove_tree_no_follow(temp, ignored, sizeof(ignored));
        return 0;
    }
    if( lstat(path, &st) != 0 || !S_ISLNK(st.st_mode) || unlink(path) != 0 )
    {
        snprintf(why, why_size, "cannot remove the link %s: %s", path, strerror(errno));
        (void)remove_tree_no_follow(temp, ignored, sizeof(ignored));
        return 0;
    }
    if( !rename_patiently(temp, path) )
    {
        int const error = errno;

        /* The link is gone and the directory is not in place: put the link
         * back, so the lane (and every build reading it) still shows the
         * base's directory; then the temporary is only a leftover. */
        if( symlink(base_dir, path) == 0 )
        {
            (void)remove_tree_no_follow(temp, ignored, sizeof(ignored));
            snprintf(why, why_size, "cannot rename %s onto %s: %s (the link is back; nothing changed)", temp, path,
                strerror(error));
        }
        else
            snprintf(why, why_size, "cannot rename %s onto %s: %s (left complete; the next write or follow puts it "
                "in place)", temp, path, strerror(error));
        return 0;
    }
    return 1;
}

/** make_real_dir with the explode lock held: everything asked again, since
 *  another process may have finished the same directory while we waited. */
static int
make_real_dir_locked(
    const struct ToriRSMapEdLaneDef* lane,
    const char* rel,
    const char* path,
    char* why,
    size_t why_size)
{
    char temp[TORIRSMAPED_LANE_PATH + 32];
    char base_dir[TORIRSMAPED_LANE_PATH];
    struct stat st;
    /* Taken before anything of the base is read, a second early as Follow's
     * own: a base entry written in this second is caught as new. */
    long long const look = (long long)time(NULL) - 1;

    explode_temp_path(lane, rel, temp, sizeof(temp));
    snprintf(base_dir, sizeof(base_dir), "%s/%s", lane->base, rel);
    if( lstat(path, &st) == 0 )
    {
        if( S_ISDIR(st.st_mode) )
            return 1;
        if( !S_ISLNK(st.st_mode) )
        {
            snprintf(why, why_size, "%s is a file, not a directory", path);
            return 0;
        }
        return explode(lane, rel, path, temp, base_dir, look, why, why_size);
    }
    if( lstat(temp, &st) == 0 && S_ISDIR(st.st_mode) )
    {
        /* An explode stopped between removing the link and the rename: the
         * temporary is complete (the link only goes once it is), and its
         * look was recorded before the link went. */
        return temp_into_place(temp, path, why, why_size);
    }
    if( stat(base_dir, &st) == 0 && S_ISDIR(st.st_mode) )
    {
        /* A directory the base HAS and the lane does not (one the base
         * gained under an old time, as a rename keeps it, so no follow
         * linked it): filled from the base, never made empty. An empty one
         * would read as every base file in it deleted by the lane, and a
         * publish would delete them from the base. */
        char ignored[256];

        if( lstat(temp, &st) == 0 && !remove_tree_no_follow(temp, why, why_size) )
            return 0;
        if( fill_dir(base_dir, temp, rel, why, why_size) < 0 || !look_record(lane, rel, look, why, why_size) )
        {
            (void)remove_tree_no_follow(temp, ignored, sizeof(ignored));
            return 0;
        }
        return temp_into_place(temp, path, why, why_size);
    }
    /* One the base does not have: a new empty directory. */
    if( !look_record(lane, rel, look, why, why_size) )
        return 0;
    if( lanes_mkdir(path) != 0 && errno != EEXIST )
    {
        snprintf(why, why_size, "cannot create %s: %s", path, strerror(errno));
        return 0;
    }
    return 1;
}

/** The lane's directory `rel` (relative to its content, not "") made real:
 *  kept when it is, exploded when it is a link, the leftover of an
 *  interrupted explode completed, filled from the base when absent and the
 *  base has it, created when the base does not. */
static int
make_real_dir(
    const struct ToriRSMapEdLaneDef* lane,
    const char* rel,
    char* why,
    size_t why_size)
{
    char path[TORIRSMAPED_LANE_PATH];
    struct stat st;
    int ok;

    snprintf(path, sizeof(path), "%s/%s", lane->content, rel);
    if( lstat(path, &st) == 0 && S_ISDIR(st.st_mode) )
        return 1;
    if( !explode_lock(lane, why, why_size) )
        return 0;
    ok = make_real_dir_locked(lane, rel, path, why, why_size);
    explode_unlock(lane);
    return ok;
}

/** An explode's temporary found in the lane's real directory `rel` (Follow):
 *  completed when its directory is absent, removed (without following
 *  anything) when the directory is there. Either way nobody is building it:
 *  that is checked under the lock. 1, or 0 with `why`. */
static int
explode_tidy(
    const struct ToriRSMapEdLaneDef* lane,
    const char* rel,
    char* why,
    size_t why_size)
{
    char path[TORIRSMAPED_LANE_PATH];
    char temp[TORIRSMAPED_LANE_PATH + 32];
    struct stat st;
    int ok = 1;

    snprintf(path, sizeof(path), "%s/%s", lane->content, rel);
    explode_temp_path(lane, rel, temp, sizeof(temp));
    if( !explode_lock(lane, why, why_size) )
        return 0;
    if( lstat(temp, &st) == 0 )
    {
        if( lstat(path, &st) == 0 )
            ok = remove_tree_no_follow(temp, why, why_size);
        else if( lstat(temp, &st) == 0 && S_ISDIR(st.st_mode) )
            ok = temp_into_place(temp, path, why, why_size);
    }
    explode_unlock(lane);
    return ok;
}

/** ToriRSMapEdLanes_MakeWritable without clearing `why` on success (a
 *  caller may hold a message in it already: Follow's first conflict). */
static int
lanes_make_writable(
    const struct ToriRSMapEdLaneDef* lane,
    const char* rel,
    char* why,
    size_t why_size)
{
    char clean[TORIRSMAPED_LANE_PATH];
    size_t start = 0;

    if( lane->is_main )
        return 1;
    assert(lane->base[0]);
    snprintf(clean, sizeof(clean), "%s", rel);
#ifdef _WIN32
    for( char* scan = clean; *scan; scan++ )
        if( *scan == '\\' )
            *scan = '/';
#endif
    if( clean[0] == '/' || (clean[0] && clean[1] == ':') )
    {
        snprintf(why, why_size, "'%s' is not a path inside the lane", rel);
        return 0;
    }
    for( size_t i = 0;; i++ )
    {
        char const c = clean[i];
        size_t const length = i - start;

        if( c != '/' && c != '\0' )
            continue;
        if( length == 0 || (length == 1 && clean[start] == '.')
            || (length == 2 && clean[start] == '.' && clean[start + 1] == '.') )
        {
            snprintf(why, why_size, "'%s' is not a path inside the lane", rel);
            return 0;
        }
        if( c == '\0' )
            return 1;
        /* Top down: each ancestor's parent is real before it is touched. */
        clean[i] = '\0';
        if( !make_real_dir(lane, clean, why, why_size) )
            return 0;
        clean[i] = '/';
        start = i + 1;
    }
}

int
ToriRSMapEdLanes_MakeWritable(
    const struct ToriRSMapEdLaneDef* lane,
    const char* rel,
    char* why,
    size_t why_size)
{
    assert(lane);
    assert(rel);
    assert(why);
    why[0] = '\0';
    return lanes_make_writable(lane, rel, why, why_size);
}

/** 1 when the walks pass over root entry `name` (`rel` "" only): a k_linked
 *  tree of a LEGACY lane while the lane links it whole -- today's rule, so a
 *  full mirror never mirrors one the base gains. One a write exploded is
 *  walked like any other directory, and so is one an interrupted explode
 *  left absent beside its complete temporary (Follow completes it). A sparse
 *  lane skips nothing: its links are walked past as links wherever they are. */
static int
root_linked_skipped(
    const struct ToriRSMapEdLaneDef* lane,
    const char* rel,
    const char* name)
{
    char path[TORIRSMAPED_LANE_PATH];
    char temp[TORIRSMAPED_LANE_PATH + 32];
    struct stat st;

    if( rel[0] || lane->sparse || !in_list(k_linked, name) )
        return 0;
    snprintf(path, sizeof(path), "%s/%s", lane->content, name);
    if( lstat(path, &st) == 0 )
        return !S_ISDIR(st.st_mode);
    explode_temp_path(lane, name, temp, sizeof(temp));
    return !(lstat(temp, &st) == 0 && S_ISDIR(st.st_mode));
}

/* ---- the registry ---------------------------------------------------------- */

int
ToriRSMapEdLanes_NameValid(const char* name)
{
    size_t length;

    assert(name);
    length = strlen(name);
    if( length == 0 || length > TORIRSMAPED_LANE_NAME_MAX || strcmp(name, "main") == 0 )
        return 0;
    for( size_t i = 0; i < length; i++ )
    {
        char const c = name[i];
        if( !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') )
            return 0;
    }
    return 1;
}

/** 1 when `path` is absolute: "/x", and on Windows "C:/x", "C:\x" or "\x"
 *  too (a drive-letter path read as relative gets the working directory
 *  glued in front of it: "C:\repo/C:/repo/build/..."). */
static int
lanes_path_absolute(const char* path)
{
#ifdef _WIN32
    if( path[0] == '\\' || (((path[0] | 0x20) >= 'a' && (path[0] | 0x20) <= 'z') && path[1] == ':') )
        return 1;
#endif
    return path[0] == '/';
}

static void
resolve_abs(
    const char* path,
    char* out,
    size_t out_size)
{
    char real[PATH_MAX];
    if( realpath(path, real) )
        snprintf(out, out_size, "%s", real);
    else if( lanes_path_absolute(path) )
        snprintf(out, out_size, "%s", path);
    else
    {
        char cwd[PATH_MAX];
        if( getcwd(cwd, sizeof(cwd)) )
            snprintf(out, out_size, "%s/%s", cwd, path);
        else
            snprintf(out, out_size, "%s", path);
    }
}

void
ToriRSMapEdLanes_Main(
    const char* repo_root,
    const char* base_tree,
    struct ToriRSMapEdLaneDef* out)
{
    char repo[TORIRSMAPED_LANE_PATH];

    assert(repo_root);
    assert(base_tree);
    assert(out);
    memset(out, 0, sizeof(*out));
    snprintf(out->name, sizeof(out->name), "main");
    resolve_abs(repo_root, repo, sizeof(repo));
    resolve_abs(base_tree, out->content, sizeof(out->content));
    snprintf(out->live, sizeof(out->live), "%s/build/live/default", repo);
    out->is_main = 1;
}

static void
lane_paths(
    const char* repo_root,
    const char* name,
    struct ToriRSMapEdLaneDef* out)
{
    char repo[TORIRSMAPED_LANE_PATH];

    memset(out, 0, sizeof(*out));
    resolve_abs(repo_root, repo, sizeof(repo));
    snprintf(out->name, sizeof(out->name), "%s", name);
    snprintf(out->dir, sizeof(out->dir), "%s/build/lanes/%s", repo, name);
    snprintf(out->content, sizeof(out->content), "%s/content", out->dir);
    snprintf(out->live, sizeof(out->live), "%s/live", out->dir);
}

static int
lane_ini_read(struct ToriRSMapEdLaneDef* lane)
{
    char path[TORIRSMAPED_LANE_PATH + 16];
    char line[TORIRSMAPED_LANE_PATH + 32];
    FILE* file;

    snprintf(path, sizeof(path), "%s/lane.ini", lane->dir);
    file = fopen(path, "rb");
    if( !file )
        return 0;
    lane->base[0] = '\0';
    lane->sparse = 0;
    while( fgets(line, sizeof(line), file) )
    {
        char* end = line + strlen(line);
        while( end > line && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ') )
            *--end = '\0';
        /* A legacy full mirror names its base `base = `; a sparse lane
         * `sparse_base = ` and never `base = `, so an editor that predates
         * sparse lanes finds no base and refuses the lane rather than write
         * through its links. */
        if( strncmp(line, "base = ", 7) == 0 )
            snprintf(lane->base, sizeof(lane->base), "%s", line + 7);
        else if( strncmp(line, "sparse_base = ", 14) == 0 )
        {
            snprintf(lane->base, sizeof(lane->base), "%s", line + 14);
            lane->sparse = 1;
        }
        else if( strcmp(line, "layout = sparse") == 0 )
            lane->sparse = 1;
        else if( strncmp(line, "created = ", 10) == 0 )
        {
            long long const created = strtoll(line + 10, NULL, 10);
            if( created > lane->created )
                lane->created = created;
        }
        else if( strncmp(line, "followed = ", 11) == 0 )
        {
            long long const followed = strtoll(line + 11, NULL, 10);
            if( followed > lane->created )
                lane->created = followed;
        }
    }
    fclose(file);
    return lane->base[0] != '\0';
}

int
ToriRSMapEdLanes_Lookup(
    const char* repo_root,
    const char* base_tree,
    const char* name,
    struct ToriRSMapEdLaneDef* out)
{
    assert(repo_root);
    assert(base_tree);
    assert(name);
    assert(out);
    if( strcmp(name, "main") == 0 )
    {
        ToriRSMapEdLanes_Main(repo_root, base_tree, out);
        return 1;
    }
    if( !ToriRSMapEdLanes_NameValid(name) )
        return 0;
    lane_paths(repo_root, name, out);
    return lane_ini_read(out);
}

int
ToriRSMapEdLanes_OfContent(
    const char* content_dir,
    struct ToriRSMapEdLaneDef* out)
{
    char dir[TORIRSMAPED_LANE_PATH];
    size_t length;
    char* slash;

    assert(content_dir);
    assert(out);
    memset(out, 0, sizeof(*out));
    resolve_abs(content_dir, dir, sizeof(dir));
#ifdef _WIN32
    for( char* scan = dir; *scan; scan++ )
        if( *scan == '\\' )
            *scan = '/';
#endif
    length = strlen(dir);
    while( length > 1 && dir[length - 1] == '/' )
        dir[--length] = '\0';
    slash = strrchr(dir, '/');
    if( !slash || strcmp(slash + 1, "content") != 0 )
        return 0;
    *slash = '\0';
    slash = strrchr(dir, '/');
    if( !slash || !ToriRSMapEdLanes_NameValid(slash + 1) )
        return 0;
    snprintf(out->name, sizeof(out->name), "%s", slash + 1);
    snprintf(out->dir, sizeof(out->dir), "%s", dir);
    snprintf(out->content, sizeof(out->content), "%s/content", out->dir);
    snprintf(out->live, sizeof(out->live), "%s/live", out->dir);
    if( lane_ini_read(out) )
        return 1;
    memset(out, 0, sizeof(*out));
    return 0;
}

int
ToriRSMapEdLanes_Create(
    const char* repo_root,
    const char* base_tree,
    const char* name,
    struct ToriRSMapEdLaneDef* out,
    char* why,
    size_t why_size)
{
    char path[TORIRSMAPED_LANE_PATH + 16];
    struct stat st;
    FILE* file;
    int entries;

    assert(repo_root);
    assert(base_tree);
    assert(name);
    assert(out);
    assert(why);
    why[0] = '\0';
    if( !ToriRSMapEdLanes_NameValid(name) )
    {
        snprintf(why, why_size, "'%s' is not a lane name (a-z, 0-9, _ and -, up to %d, not main)", name,
            TORIRSMAPED_LANE_NAME_MAX);
        return 0;
    }
    lane_paths(repo_root, name, out);
    resolve_abs(base_tree, out->base, sizeof(out->base));
    if( lstat(out->dir, &st) == 0 )
    {
        snprintf(why, why_size, "lane '%s' exists (%s)", name, out->dir);
        return 0;
    }
    if( stat(out->base, &st) != 0 || !S_ISDIR(st.st_mode) )
    {
        snprintf(why, why_size, "no such tree to mirror: %s", out->base);
        return 0;
    }
    if( !mkdir_p(out->dir) || !mkdir_p(out->live) )
    {
        snprintf(why, why_size, "cannot create %s: %s", out->dir, strerror(errno));
        return 0;
    }
    out->created = (long long)time(NULL);
    out->sparse = 1;
    entries = sparse_fill(out->base, out->content, "", why, why_size);
    if( entries < 0 )
        return 0;
    if( !ancestor_seed(out, why, why_size) )
        return 0;
    snprintf(path, sizeof(path), "%s/lane.ini", out->dir);
    file = fopen(path, "wb");
    if( !file )
    {
        snprintf(why, why_size, "cannot write %s: %s", path, strerror(errno));
        return 0;
    }
    /* No `base = ` line: an editor that predates sparse lanes must refuse
     * this lane (it would write through its directory links into the base). */
    fprintf(file,
        "; An editor lane (torirsmaped/torirs_maped_lanes.h): content/ is a SPARSE\n"
        "; view of the base tree -- a directory is a link to the base's until the\n"
        "; editor writes inside it -- and live/ holds its packs, cache and servers.\n"
        "layout = sparse\n"
        "sparse_base = %s\n"
        "created = %lld\n"
        "linked_entries = %d\n",
        out->base, out->created, entries);
    fclose(file);
    return 1;
}

int
ToriRSMapEdLanes_Open(
    const char* repo_root,
    const char* base_tree,
    const char* name,
    struct ToriRSMapEdLaneDef* out,
    char* why,
    size_t why_size)
{
    assert(why);
    why[0] = '\0';
    if( ToriRSMapEdLanes_Lookup(repo_root, base_tree, name, out) )
        return 1;
    return ToriRSMapEdLanes_Create(repo_root, base_tree, name, out, why, why_size);
}

static int
name_compare(
    const void* a,
    const void* b)
{
    return strcmp(((const struct ToriRSMapEdLaneDef*)a)->name, ((const struct ToriRSMapEdLaneDef*)b)->name);
}

int
ToriRSMapEdLanes_List(
    const char* repo_root,
    const char* base_tree,
    struct ToriRSMapEdLaneDef* out,
    int max)
{
    char repo[TORIRSMAPED_LANE_PATH];
    char root[TORIRSMAPED_LANE_PATH];
    DIR* dir;
    struct dirent* entry;
    int count = 0;

    assert(repo_root);
    assert(base_tree);
    assert(out);
    if( max <= 0 )
        return 0;
    ToriRSMapEdLanes_Main(repo_root, base_tree, &out[count++]);
    resolve_abs(repo_root, repo, sizeof(repo));
    snprintf(root, sizeof(root), "%s/build/lanes", repo);
    dir = opendir(root);
    if( !dir )
        return count;
    while( (entry = readdir(dir)) && count < max )
    {
        if( !ToriRSMapEdLanes_NameValid(entry->d_name) )
            continue;
        if( ToriRSMapEdLanes_Lookup(repo_root, base_tree, entry->d_name, &out[count]) )
            count++;
    }
    closedir(dir);
    if( count > 2 )
        qsort(out + 1, (size_t)(count - 1), sizeof(*out), name_compare);
    return count;
}

/* ---- changes --------------------------------------------------------------- */

struct change_sink
{
    struct ToriRSMapEdLaneChange* out;
    int max;
    int count;
    struct ToriRSMapEdLaneCounts counts;
    /** When the lane last looked at each directory it made real (looks_read). */
    struct lanes_looks looks;
    /** The files the lane wrote (owned_read). */
    struct lanes_owned owned;
};

static void
change_add(
    struct change_sink* sink,
    enum ToriRSMapEdLaneChangeKind kind,
    const char* rel,
    uint32_t size)
{
    if( kind == TORIRSMAPED_LANE_CHANGE_MODIFIED )
        sink->counts.modified++;
    else if( kind == TORIRSMAPED_LANE_CHANGE_ADDED )
        sink->counts.added++;
    else
        sink->counts.deleted++;
    if( sink->out && sink->count < sink->max )
    {
        struct ToriRSMapEdLaneChange* row = &sink->out[sink->count];
        row->kind = kind;
        snprintf(row->path, sizeof(row->path), "%s", rel);
        row->size = size;
    }
    sink->count++;
}

/** The lane's real files under `rel` (a real directory of the mirror). */
static void
changes_walk_lane(
    const struct ToriRSMapEdLaneDef* lane,
    const char* rel,
    struct change_sink* sink)
{
    char dir_path[TORIRSMAPED_LANE_PATH];
    char base_dir[TORIRSMAPED_LANE_PATH];
    struct lanes_pair pair;
    struct lanes_names names;
    const char* name;

    if( rel[0] )
        snprintf(dir_path, sizeof(dir_path), "%s/%s", lane->content, rel);
    else
        snprintf(dir_path, sizeof(dir_path), "%s", lane->content);
    snprintf(base_dir, sizeof(base_dir), "%s%s%s", lane->base, rel[0] ? "/" : "", rel);
    lanes_pair_read(&pair, base_dir, dir_path);
    if( !lanes_names_open_lane(&names, dir_path, &pair) )
    {
        lanes_pair_free(&pair);
        return;
    }
    while( (name = lanes_names_next(&names)) )
    {
        char sub_rel[TORIRSMAPED_LANE_PATH];
        char path[TORIRSMAPED_LANE_PATH];
        char base_path[TORIRSMAPED_LANE_PATH];
        struct stat st;
        struct stat bst;
        enum lanes_settled settled;

        if( rel[0] )
            snprintf(sub_rel, sizeof(sub_rel), "%s/%s", rel, name);
        else
            snprintf(sub_rel, sizeof(sub_rel), "%s", name);
        if( rel_transient(sub_rel) )
            continue;
        /* The listings first (lanes_changes_lane_settled): a link, a real
         * directory, or the base's own file under a second name. */
        settled = lanes_changes_lane_settled(&pair, name);
        if( settled == LANES_NOTHING )
            continue;
        if( settled == LANES_DESCEND )
        {
            changes_walk_lane(lane, sub_rel, sink);
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", dir_path, name);
        if( lstat(path, &st) != 0 || S_ISLNK(st.st_mode) )
            continue; /* a link to the base: unchanged (a dangling one is a base file gone, not ours) */
        if( S_ISDIR(st.st_mode) )
        {
            changes_walk_lane(lane, sub_rel, sink);
            continue;
        }
        if( !S_ISREG(st.st_mode) )
            continue;
        snprintf(base_path, sizeof(base_path), "%s/%s", lane->base, sub_rel);
        /* A file the lane never wrote is no change of the lane's: a link's
         * file the base has since replaced or removed, or the copy the lane
         * took of a file the base has since changed. The follow brings it
         * up to date. */
        if( rel_copied(sub_rel) ? lane_copy_untouched(lane, &sink->owned, sub_rel, path, &st)
                                : lane_file_fell_behind(lane, &sink->looks, &sink->owned, rel, sub_rel, &st) )
            continue;
        if( stat(base_path, &bst) != 0 )
            change_add(sink, TORIRSMAPED_LANE_CHANGE_ADDED, sub_rel, (uint32_t)st.st_size);
        else if( !files_equal(path, base_path) )
            change_add(sink, TORIRSMAPED_LANE_CHANGE_MODIFIED, sub_rel, (uint32_t)st.st_size);
    }
    lanes_names_close(&names);
    lanes_pair_free(&pair);
}

/** The base's files under `rel` that the lane no longer has (its directory
 *  for `rel` is real; a directory the lane links whole cannot lose a file). */
static void
changes_walk_base(
    const struct ToriRSMapEdLaneDef* lane,
    const char* rel,
    struct change_sink* sink)
{
    char dir_path[TORIRSMAPED_LANE_PATH];
    char lane_dir[TORIRSMAPED_LANE_PATH];
    struct lanes_pair pair;
    struct lanes_names names;
    const char* name;
    /* The lane's last look at this directory: what the base had then, the
     * lane had (lanes_looks). */
    long long const looked = looked_at(lane, &sink->looks, rel);

    if( rel[0] )
        snprintf(dir_path, sizeof(dir_path), "%s/%s", lane->base, rel);
    else
        snprintf(dir_path, sizeof(dir_path), "%s", lane->base);
    snprintf(lane_dir, sizeof(lane_dir), "%s%s%s", lane->content, rel[0] ? "/" : "", rel);
    lanes_pair_read(&pair, dir_path, lane_dir);
    if( !lanes_names_open_base(&names, dir_path, &pair) )
    {
        lanes_pair_free(&pair);
        return;
    }
    while( (name = lanes_names_next(&names)) )
    {
        char sub_rel[TORIRSMAPED_LANE_PATH];
        char base_path[TORIRSMAPED_LANE_PATH];
        char lane_path[TORIRSMAPED_LANE_PATH];
        struct stat bst;
        struct stat lst;
        enum lanes_settled settled;

        if( rel[0] )
            snprintf(sub_rel, sizeof(sub_rel), "%s/%s", rel, name);
        else
            snprintf(sub_rel, sizeof(sub_rel), "%s", name);
        if( rel_transient(sub_rel) )
            continue;
        if( root_linked_skipped(lane, rel, name) )
            continue;
        /* The listings first (lanes_changes_base_settled): the mirror has
         * the entry, so it is no deletion; two real directories go in. */
        settled = lanes_changes_base_settled(&pair, name);
        if( settled == LANES_NOTHING )
            continue;
        if( settled == LANES_DESCEND )
        {
            changes_walk_base(lane, sub_rel, sink);
            continue;
        }
        snprintf(base_path, sizeof(base_path), "%s/%s", dir_path, name);
        snprintf(lane_path, sizeof(lane_path), "%s/%s", lane->content, sub_rel);
        if( stat(base_path, &bst) != 0 )
            continue;
        if( lstat(lane_path, &lst) != 0 )
        {
            /* A base file the mirror never had (younger than the lane's last
             * look at this directory) is new in the base, not a deletion by
             * the lane. */
            if( S_ISREG(bst.st_mode) && !newer_than_look((long long)bst.st_mtime, looked) )
                change_add(sink, TORIRSMAPED_LANE_CHANGE_DELETED, sub_rel, 0);
            continue;
        }
        if( S_ISDIR(bst.st_mode) && S_ISDIR(lst.st_mode) )
            changes_walk_base(lane, sub_rel, sink);
    }
    lanes_names_close(&names);
    lanes_pair_free(&pair);
}

static int
change_compare(
    const void* a,
    const void* b)
{
    return strcmp(((const struct ToriRSMapEdLaneChange*)a)->path, ((const struct ToriRSMapEdLaneChange*)b)->path);
}

int
ToriRSMapEdLanes_Changes(
    const struct ToriRSMapEdLaneDef* lane,
    struct ToriRSMapEdLaneChange* out,
    int max,
    struct ToriRSMapEdLaneCounts* counts)
{
    struct change_sink sink;

    assert(lane);
    assert(out || max == 0);
    memset(&sink, 0, sizeof(sink));
    sink.out = out;
    sink.max = max;
    if( counts )
        memset(counts, 0, sizeof(*counts));
    if( lane->is_main || !lane->base[0] )
        return 0;
    looks_read(lane, &sink.looks);
    owned_read(lane, &sink.owned);
    changes_walk_lane(lane, "", &sink);
    changes_walk_base(lane, "", &sink);
    looks_free(&sink.looks);
    owned_free(&sink.owned);
    if( out && sink.count > 1 )
        qsort(out, (size_t)(sink.count < max ? sink.count : max), sizeof(*out), change_compare);
    if( counts )
        *counts = sink.counts;
    return sink.count;
}

/* ---- publish and discard --------------------------------------------------- */

int
ToriRSMapEdLanes_TreeHeld(
    const char* tree,
    char* out,
    size_t out_size)
{
    char path[TORIRSMAPED_LANE_PATH + 32];
    char text[1024];
    FILE* file;
    size_t got;
    long pid = 0;
    char cmd[400] = { 0 };
    const char* line;

    assert(tree);
    assert(out);
    out[0] = '\0';
    snprintf(path, sizeof(path), "%s/.editor-session.lock", tree);
    file = fopen(path, "rb");
    if( !file )
        return 0;
    got = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[got] = '\0';
    for( line = text; line && *line; )
    {
        const char* end = strchr(line, '\n');
        size_t const length = end ? (size_t)(end - line) : strlen(line);
        if( length > 4 && strncmp(line, "pid ", 4) == 0 )
            pid = strtol(line + 4, NULL, 10);
        else if( length > 4 && strncmp(line, "cmd ", 4) == 0 )
            snprintf(cmd, sizeof(cmd), "%.*s", (int)(length - 4), line + 4);
        line = end ? end + 1 : NULL;
    }
    if( pid <= 0 )
        return 0;
    if( kill((pid_t)pid, 0) != 0 && errno == ESRCH )
        return 0;
    snprintf(out, out_size, "pid %ld (%s)", pid, cmd[0] ? cmd : "an editor");
    return 1;
}

/** Make `lane_path` a link to `base_path` again (or nothing, when the base
 *  has no such file). */
static int
relink(
    const char* lane_path,
    const char* base_path,
    char* why,
    size_t why_size)
{
    struct stat st;

    if( unlink(lane_path) != 0 && errno != ENOENT )
    {
        snprintf(why, why_size, "cannot remove %s: %s", lane_path, strerror(errno));
        return 0;
    }
    if( stat(base_path, &st) != 0 )
        return 1;
    if( symlink(base_path, lane_path) != 0 )
    {
        snprintf(why, why_size, "cannot link %s: %s", lane_path, strerror(errno));
        return 0;
    }
    return 1;
}

static int
parent_dir(
    const char* path,
    char* out,
    size_t out_size)
{
    const char* slash = strrchr(path, '/');
    if( !slash )
        return 0;
    snprintf(out, out_size, "%.*s", (int)(slash - path), path);
    return 1;
}

int
ToriRSMapEdLanes_Publish(
    const struct ToriRSMapEdLaneDef* from,
    const struct ToriRSMapEdLaneDef* to,
    uint32_t flags,
    struct ToriRSMapEdLaneCounts* counts,
    char* why,
    size_t why_size)
{
    struct ToriRSMapEdLaneChange* changes;
    int count;
    int reset;

    assert(from);
    assert(to);
    assert(counts);
    assert(why);
    why[0] = '\0';
    memset(counts, 0, sizeof(*counts));
    if( from->is_main )
    {
        snprintf(why, why_size, "the main tree is published into, never from");
        return 0;
    }
    if( strcmp(from->content, to->content) == 0 )
    {
        snprintf(why, why_size, "lane '%s' cannot publish into itself", from->name);
        return 0;
    }
    if( to->is_main && !(flags & TORIRSMAPED_LANE_PUBLISH_FORCE) )
    {
        char holder[512];
        if( ToriRSMapEdLanes_TreeHeld(to->content, holder, sizeof(holder)) )
        {
            snprintf(why, why_size, "%s is being edited by %s; publish with force to write into it anyway",
                to->content, holder);
            return 0;
        }
    }
    count = ToriRSMapEdLanes_Changes(from, NULL, 0, counts);
    if( count == 0 )
        return 1;
    changes = ToriMem_Alloc(ToriMem_Editor(), (size_t)count * sizeof(*changes));
    assert(changes);
    (void)ToriRSMapEdLanes_Changes(from, changes, count, NULL);
    reset = (flags & TORIRSMAPED_LANE_PUBLISH_RESET) && strcmp(to->content, from->base) == 0;
    for( int i = 0; i < count; i++ )
    {
        const struct ToriRSMapEdLaneChange* change = &changes[i];
        char src[TORIRSMAPED_LANE_PATH];
        char dst[TORIRSMAPED_LANE_PATH];
        char dir[TORIRSMAPED_LANE_PATH];

        snprintf(src, sizeof(src), "%s/%s", from->content, change->path);
        snprintf(dst, sizeof(dst), "%s/%s", to->content, change->path);
        /* A target lane's directory that still links ITS base is exploded
         * first, or the write (or the deletion) below lands in that base. */
        if( !lanes_make_writable(to, change->path, why, why_size) )
        {
            ToriMem_Free(ToriMem_Editor(), changes);
            return 0;
        }
        if( change->kind == TORIRSMAPED_LANE_CHANGE_DELETED )
        {
            if( unlink(dst) != 0 && errno != ENOENT )
            {
                snprintf(why, why_size, "cannot remove %s: %s", dst, strerror(errno));
                ToriMem_Free(ToriMem_Editor(), changes);
                return 0;
            }
            counts->removed++;
            continue;
        }
        if( parent_dir(dst, dir, sizeof(dir)) && !mkdir_p(dir) )
        {
            snprintf(why, why_size, "cannot create %s: %s", dir, strerror(errno));
            ToriMem_Free(ToriMem_Editor(), changes);
            return 0;
        }
        /* A target lane's link to ITS base must go first: the rename lands
         * the real file in the lane, never through the link. copy_file
         * renames over whatever is there. */
        if( !copy_file(src, dst, why, why_size) )
        {
            ToriMem_Free(ToriMem_Editor(), changes);
            return 0;
        }
        counts->written++;
        if( reset )
        {
            char base_path[TORIRSMAPED_LANE_PATH];
            snprintf(base_path, sizeof(base_path), "%s/%s", from->base, change->path);
            /* The source's directory is real (the change scan found a real
             * file in it); asked anyway, as every write into a lane is. */
            if( !lanes_make_writable(from, change->path, why, why_size)
                || !relink(src, base_path, why, why_size) )
            {
                ToriMem_Free(ToriMem_Editor(), changes);
                return 0;
            }
        }
    }
    ToriMem_Free(ToriMem_Editor(), changes);
    /* A reset linked the source's files again: they are the base's now. */
    return !reset || owned_prune(from, why, why_size);
}

int
ToriRSMapEdLanes_Discard(
    const struct ToriRSMapEdLaneDef* lane,
    struct ToriRSMapEdLaneCounts* counts,
    char* why,
    size_t why_size)
{
    struct ToriRSMapEdLaneChange* changes;
    int count;

    assert(lane);
    assert(counts);
    assert(why);
    why[0] = '\0';
    memset(counts, 0, sizeof(*counts));
    if( lane->is_main )
    {
        snprintf(why, why_size, "the main tree has no base to go back to");
        return 0;
    }
    count = ToriRSMapEdLanes_Changes(lane, NULL, 0, counts);
    if( count == 0 )
        return 1;
    changes = ToriMem_Alloc(ToriMem_Editor(), (size_t)count * sizeof(*changes));
    assert(changes);
    (void)ToriRSMapEdLanes_Changes(lane, changes, count, NULL);
    for( int i = 0; i < count; i++ )
    {
        char lane_path[TORIRSMAPED_LANE_PATH];
        char base_path[TORIRSMAPED_LANE_PATH];
        snprintf(lane_path, sizeof(lane_path), "%s/%s", lane->content, changes[i].path);
        snprintf(base_path, sizeof(base_path), "%s/%s", lane->base, changes[i].path);
        /* Every change's directory is real already (the scan only finds
         * changes in real directories); asked anyway, as every write into a
         * lane is. A directory a discard leaves holding only links stays
         * exploded: a real directory of links reads exactly as the link it
         * replaced, and the next write there needs it real again. */
        if( !lanes_make_writable(lane, changes[i].path, why, why_size) )
        {
            ToriMem_Free(ToriMem_Editor(), changes);
            return 0;
        }
        if( changes[i].kind == TORIRSMAPED_LANE_CHANGE_ADDED )
        {
            if( unlink(lane_path) != 0 && errno != ENOENT )
            {
                snprintf(why, why_size, "cannot remove %s: %s", lane_path, strerror(errno));
                ToriMem_Free(ToriMem_Editor(), changes);
                return 0;
            }
            counts->removed++;
        }
        else
        {
            if( !relink(lane_path, base_path, why, why_size) )
            {
                ToriMem_Free(ToriMem_Editor(), changes);
                return 0;
            }
            counts->written++;
        }
    }
    ToriMem_Free(ToriMem_Editor(), changes);
    /* The files linked again are the base's, not the lane's. */
    return owned_prune(lane, why, why_size);
}

/* ---- following the base ------------------------------------------------------ */

static int
mkdir_p_parent(const char* path)
{
    char dir[TORIRSMAPED_LANE_PATH + 16];
    return !parent_dir(path, dir, sizeof(dir)) || mkdir_p(dir);
}

struct follow_state
{
    const struct ToriRSMapEdLaneDef* lane;
    struct ToriRSMapEdLaneCounts* counts;
    int conflicts;
    char* why;
    size_t why_size;
    /** When the lane last looked at each directory it made real (looks_read). */
    struct lanes_looks looks;
    /** The files the lane wrote, as the last follow left the list
     *  (owned_read), and the list this follow is making of them. */
    struct lanes_owned owned;
    struct lanes_owned owned_now;
};

/** A copied file both sides have: take the base's when the lane never
 *  changed its copy, keep the lane's when the base never moved, and refuse
 *  when both did. Returns 0 on an IO error. */
static int
follow_copied(
    struct follow_state* state,
    const char* rel,
    const char* lane_path,
    const char* base_path)
{
    char ancestor[TORIRSMAPED_LANE_PATH + 16];
    struct stat st;
    int has_ancestor;

    ancestor_path(state->lane, rel, ancestor, sizeof(ancestor));
    has_ancestor = stat(ancestor, &st) == 0;
    if( files_equal(lane_path, base_path) )
    {
        if( has_ancestor && files_equal(ancestor, base_path) )
            return 1;
        return mkdir_p_parent(ancestor) && copy_file(base_path, ancestor, state->why, state->why_size);
    }
    if( has_ancestor && files_equal(lane_path, ancestor) )
    {
        if( !lanes_make_writable(state->lane, rel, state->why, state->why_size)
            || !copy_file(base_path, lane_path, state->why, state->why_size)
            || !copy_file(base_path, ancestor, state->why, state->why_size) )
            return 0;
        state->counts->written++;
        return 1;
    }
    if( has_ancestor && files_equal(base_path, ancestor) )
        return 1; /* the lane's own edit */
    /* No record of the copy, in a lane that was never followed: a copy dated
     * within the lane's creation is the one the creation made, never
     * written since. The base's is taken, and recorded this time. */
    if( !has_ancestor && stat(lane_path, &st) == 0
        && lane_copy_untouched(state->lane, &state->owned, rel, lane_path, &st) )
    {
        if( !lanes_make_writable(state->lane, rel, state->why, state->why_size)
            || !copy_file(base_path, lane_path, state->why, state->why_size) || !mkdir_p_parent(ancestor)
            || !copy_file(base_path, ancestor, state->why, state->why_size) )
            return 0;
        state->counts->written++;
        return 1;
    }
    if( state->conflicts++ == 0 )
        snprintf(state->why, state->why_size,
            "%s was changed in this lane AND in the main tree since the lane took it, and the two cannot be "
            "merged. In the Lanes panel, Discard takes the main tree's copy and Publish keeps the lane's", rel);
    return 1;
}

/** A file both sides have, the lane's a file of its own that differs from
 *  the base's: the lane's edit stays (and goes on the list of what the lane
 *  wrote); a link's file the base has since replaced is linked again.
 *  Returns 0 on an IO error. */
static int
follow_real(
    struct follow_state* state,
    const char* dir_rel,
    const char* rel,
    const char* lane_path,
    const char* base_path,
    const struct stat* lst)
{
    if( files_equal(lane_path, base_path) )
        return 1;
    if( !lane_file_fell_behind(state->lane, &state->looks, &state->owned, dir_rel, rel, lst) )
    {
        owned_add(&state->owned_now, rel);
        return 1;
    }
    if( !lanes_make_writable(state->lane, rel, state->why, state->why_size)
        || !relink(lane_path, base_path, state->why, state->why_size) )
        return 0;
    state->counts->written++;
    return 1;
}

/** A file both sides have, the lane's a hard link to a file the base has
 *  since replaced (other lanes still hold the old one too, so it still reads
 *  as a link): linked again. Returns 0 on an IO error. */
static int
follow_relink_stale(
    struct follow_state* state,
    const char* rel,
    const char* lane_path,
    const char* base_path)
{
    if( lane_link_is_live(lane_path, base_path) )
        return 1;
    if( !lanes_make_writable(state->lane, rel, state->why, state->why_size)
        || !relink(lane_path, base_path, state->why, state->why_size) )
        return 0;
    state->counts->written++;
    return 1;
}

/** A copied file that is a link (a publish with reset or a discard re-links
 *  it): a copy again, or the next ledger rewrite writes through into the
 *  base. Not counted: the lane reads the same bytes either way. */
static int
follow_recopy(
    struct follow_state* state,
    const char* rel,
    const char* lane_path,
    const char* base_path)
{
    char ancestor[TORIRSMAPED_LANE_PATH + 16];

    ancestor_path(state->lane, rel, ancestor, sizeof(ancestor));
    return lanes_make_writable(state->lane, rel, state->why, state->why_size)
           && copy_file(base_path, lane_path, state->why, state->why_size) && mkdir_p_parent(ancestor)
           && copy_file(base_path, ancestor, state->why, state->why_size);
}

/** A base entry the mirror has nothing for: a file or directory new in the
 *  base since the mirror's last look is linked (copied, under k_copied); an
 *  older one is a deletion by the lane and stays deleted. A new directory
 *  is mirrored in a legacy lane and ONE directory link in a sparse lane
 *  (filled real when sparse_real says so). `looked` is the lane's last look
 *  at the directory holding `rel` (looked_at). Returns 0 on an IO error. */
static int
follow_new(
    struct follow_state* state,
    const char* rel,
    const char* lane_path,
    const char* base_path,
    const struct stat* bst,
    long long looked)
{
    char ancestor[TORIRSMAPED_LANE_PATH + 16];
    char temp[TORIRSMAPED_LANE_PATH + 32];
    struct stat tst;
    int const copied = rel_copied(rel);

    if( S_ISDIR(bst->st_mode) )
    {
        /* The lane lacks the directory because an explode stopped between
         * removing its link and renaming the complete temporary into place:
         * finish that, whatever the directory's age. */
        explode_temp_path(state->lane, rel, temp, sizeof(temp));
        if( lstat(temp, &tst) == 0 && S_ISDIR(tst.st_mode) )
            return make_real_dir(state->lane, rel, state->why, state->why_size);
    }
    if( !((long long)bst->st_mtime > looked) )
        return 1;
    if( !lanes_make_writable(state->lane, rel, state->why, state->why_size) )
        return 0;
    ancestor_path(state->lane, rel, ancestor, sizeof(ancestor));
    if( S_ISDIR(bst->st_mode) )
    {
        int files;

        if( copied || !state->lane->sparse )
            files = mirror_tree(base_path, lane_path, rel, copied, state->why, state->why_size);
        else if( sparse_real(rel) )
            files = sparse_fill(base_path, lane_path, rel, state->why, state->why_size);
        else if( symlink(base_path, lane_path) != 0 )
        {
            snprintf(state->why, state->why_size, "cannot link %s: %s", lane_path, strerror(errno));
            files = -1;
        }
        else
            files = 1;
        if( files < 0 )
            return 0;
        state->counts->written += files;
        return !copied || mirror_tree(base_path, ancestor, rel, 1, state->why, state->why_size) >= 0;
    }
    if( !S_ISREG(bst->st_mode) )
        return 1;
    if( copied )
    {
        if( !copy_file(base_path, lane_path, state->why, state->why_size) || !mkdir_p_parent(ancestor)
            || !copy_file(base_path, ancestor, state->why, state->why_size) )
            return 0;
    }
    else if( symlink(base_path, lane_path) != 0 )
    {
        snprintf(state->why, state->why_size, "cannot link %s: %s", lane_path, strerror(errno));
        return 0;
    }
    state->counts->written++;
    return 1;
}

/** A mirror entry the base no longer has: a link left dangling goes, and so
 *  does a copy the lane never changed; the lane's own files stay. Returns 0
 *  on an IO error. */
static int
follow_gone(
    struct follow_state* state,
    const char* dir_rel,
    const char* rel,
    const char* lane_path,
    const struct stat* lst)
{
    char ancestor[TORIRSMAPED_LANE_PATH + 16];
    struct stat ast;

    ancestor_path(state->lane, rel, ancestor, sizeof(ancestor));
    if( !S_ISLNK(lst->st_mode)
        && !(rel_copied(rel) && S_ISREG(lst->st_mode) && stat(ancestor, &ast) == 0 && files_equal(lane_path, ancestor)) )
    {
        /* A file of the lane's own. One it wrote stays, and goes on the
         * list of what the lane wrote; a link's file that the base has
         * since removed (the hard link kept it alive) goes with it. */
        if( !S_ISREG(lst->st_mode) || rel_copied(rel) )
            return 1;
        if( !lane_file_fell_behind(state->lane, &state->looks, &state->owned, dir_rel, rel, lst) )
        {
            owned_add(&state->owned_now, rel);
            return 1;
        }
    }
    /* A dangling directory link of a sparse lane goes the same way: unlink
     * removes the link's own name (lanes_unlink: RemoveDirectory of a
     * junction), never anything it pointed at. */
    if( !lanes_make_writable(state->lane, rel, state->why, state->why_size) )
        return 0;
    if( unlink(lane_path) != 0 )
    {
        snprintf(state->why, state->why_size, "cannot remove %s: %s", lane_path, strerror(errno));
        return 0;
    }
    if( !S_ISLNK(lst->st_mode) )
        remove(ancestor);
    state->counts->removed++;
    return 1;
}

/** One real directory of the mirror against the base's: both directions,
 *  recursing into the directories both have. Returns 0 on an IO error (`why`
 *  says which). */
static int
follow_walk(
    struct follow_state* state,
    const char* rel)
{
    const struct ToriRSMapEdLaneDef* lane = state->lane;
    char dirs[2][TORIRSMAPED_LANE_PATH];
    struct lanes_pair pair;
    int ok = 1;
    long long const looked = looked_at(lane, &state->looks, rel);

    snprintf(dirs[0], sizeof(dirs[0]), "%s%s%s", lane->base, rel[0] ? "/" : "", rel);
    snprintf(dirs[1], sizeof(dirs[1]), "%s%s%s", lane->content, rel[0] ? "/" : "", rel);
    /* Both directories as they stand now. Pass 0 only ever adds to the
     * mirror what the base has, and nothing below removes from the base, so
     * what the listings settle (lanes_follow_settled) holds through both
     * passes. */
    lanes_pair_read(&pair, dirs[0], dirs[1]);
    /* Pass 0 walks the base (new entries, copies, subdirectories), pass 1
     * the mirror (what the base no longer has). */
    for( int pass = 0; pass < 2 && ok; pass++ )
    {
        struct lanes_names names;
        const char* name;

        if( !(pass == 0 ? lanes_names_open_base(&names, dirs[0], &pair) : lanes_names_open_lane(&names, dirs[1], &pair)) )
            continue;
        while( ok && (name = lanes_names_next(&names)) )
        {
            char sub_rel[TORIRSMAPED_LANE_PATH];
            char base_path[TORIRSMAPED_LANE_PATH];
            char lane_path[TORIRSMAPED_LANE_PATH];
            struct stat bst;
            struct stat lst;
            int has_base;
            int has_lane;
            enum lanes_settled settled;
            char exploding[TORIRSMAPED_LANE_PATH];

            /* An explode's leftover temporary (only the lane has one):
             * finished, or removed when its directory is in place. */
            if( pass == 1 && explode_temp_name(name, exploding, sizeof(exploding)) )
            {
                snprintf(sub_rel, sizeof(sub_rel), "%s%s%s", rel, rel[0] ? "/" : "", exploding);
                ok = explode_tidy(lane, sub_rel, state->why, state->why_size);
                continue;
            }
            snprintf(sub_rel, sizeof(sub_rel), "%s%s%s", rel, rel[0] ? "/" : "", name);
            if( rel_transient(sub_rel) || root_linked_skipped(lane, rel, name) )
                continue;
            settled = lanes_follow_settled(&pair, pass, name, rel_copied(sub_rel));
            if( settled == LANES_NOTHING )
                continue;
            if( settled == LANES_DESCEND )
            {
                ok = follow_walk(state, sub_rel);
                continue;
            }
            snprintf(base_path, sizeof(base_path), "%s/%s", dirs[0], name);
            snprintf(lane_path, sizeof(lane_path), "%s/%s", dirs[1], name);
            has_base = stat(base_path, &bst) == 0;
            has_lane = lstat(lane_path, &lst) == 0;
            if( pass == 1 )
                ok = has_base || !has_lane || follow_gone(state, rel, sub_rel, lane_path, &lst);
            else if( !has_base )
                continue;
            else if( !has_lane )
                ok = follow_new(state, sub_rel, lane_path, base_path, &bst, looked);
            else if( S_ISDIR(bst.st_mode) && S_ISDIR(lst.st_mode) )
                ok = follow_walk(state, sub_rel);
            else if( S_ISREG(bst.st_mode) && S_ISREG(lst.st_mode) && rel_copied(sub_rel) )
                ok = follow_copied(state, sub_rel, lane_path, base_path);
            else if( S_ISREG(bst.st_mode) && S_ISLNK(lst.st_mode) && rel_copied(sub_rel) )
                ok = follow_recopy(state, sub_rel, lane_path, base_path);
            else if( S_ISREG(bst.st_mode) && S_ISREG(lst.st_mode) )
                ok = follow_real(state, rel, sub_rel, lane_path, base_path, &lst);
            else if( S_ISREG(bst.st_mode) && S_ISLNK(lst.st_mode) )
                ok = follow_relink_stale(state, sub_rel, lane_path, base_path);
        }
        lanes_names_close(&names);
    }
    lanes_pair_free(&pair);
    return ok;
}

/** lane.ini's `followed` line, replaced (the rest of the file kept). */
static int
lane_ini_set_followed(
    const struct ToriRSMapEdLaneDef* lane,
    long long followed,
    char* why,
    size_t why_size)
{
    char path[TORIRSMAPED_LANE_PATH + 16];
    char temp[TORIRSMAPED_LANE_PATH + 32];
    char line[TORIRSMAPED_LANE_PATH + 32];
    FILE* in;
    FILE* out;

    snprintf(path, sizeof(path), "%s/lane.ini", lane->dir);
    snprintf(temp, sizeof(temp), "%s.tmp", path);
    in = fopen(path, "rb");
    if( !in )
    {
        snprintf(why, why_size, "cannot read %s: %s", path, strerror(errno));
        return 0;
    }
    out = fopen(temp, "wb");
    if( !out )
    {
        fclose(in);
        snprintf(why, why_size, "cannot write %s: %s", temp, strerror(errno));
        return 0;
    }
    while( fgets(line, sizeof(line), in) )
    {
        if( strncmp(line, "followed = ", 11) != 0 )
            fputs(line, out);
    }
    fclose(in);
    fprintf(out, "followed = %lld\n", followed);
    if( fclose(out) != 0 || rename(temp, path) != 0 )
    {
        remove(temp);
        snprintf(why, why_size, "cannot write %s: %s", path, strerror(errno));
        return 0;
    }
    return 1;
}

int
ToriRSMapEdLanes_Follow(
    struct ToriRSMapEdLaneDef* lane,
    struct ToriRSMapEdLaneCounts* counts,
    char* why,
    size_t why_size)
{
    struct follow_state state;
    /* A base write in the walk's own second is caught by the next follow. */
    long long const started = (long long)time(NULL) - 1;

    assert(lane);
    assert(counts);
    assert(why);
    why[0] = '\0';
    memset(counts, 0, sizeof(*counts));
    if( lane->is_main || !lane->base[0] )
        return 1;
    memset(&state, 0, sizeof(state));
    state.lane = lane;
    state.counts = counts;
    state.why = why;
    state.why_size = why_size;
    looks_read(lane, &state.looks);
    owned_read(lane, &state.owned);
    if( !follow_walk(&state, "") )
    {
        looks_free(&state.looks);
        owned_free(&state.owned);
        owned_free(&state.owned_now);
        return 0;
    }
    looks_free(&state.looks);
    owned_free(&state.owned);
    if( state.conflicts > 0 )
    {
        if( state.conflicts > 1 )
        {
            size_t const used = strlen(why);
            snprintf(why + used, why_size - used, " (and %d more files)", state.conflicts - 1);
        }
        owned_free(&state.owned_now);
        return 0;
    }
    /* The list of what the lane wrote FIRST, then the look moves: a file on
     * it is the lane's whatever its time, and without it the files the lane
     * wrote before this follow would read as fallen behind from here on. */
    {
        int const listed = owned_write(lane, &state.owned_now, why, why_size);

        owned_free(&state.owned_now);
        if( !listed )
            return 0;
    }
    if( !lane_ini_set_followed(lane, started, why, why_size) )
        return 0;
    lane->created = started;
    return 1;
}

/* ---- the next boot's lane (workbench 6.5) ----------------------------------- */

static void
boot_file_path(
    const char* repo_root,
    char* out,
    size_t out_size)
{
    char repo[TORIRSMAPED_LANE_PATH];

    resolve_abs(repo_root, repo, sizeof(repo));
    snprintf(out, out_size, "%s/build/lanes/lane.ini", repo);
}

int
ToriRSMapEdLanes_BootName(
    const char* repo_root,
    char* out,
    size_t out_size)
{
    char path[TORIRSMAPED_LANE_PATH + 32];
    char line[256];
    FILE* file;

    assert(repo_root);
    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    boot_file_path(repo_root, path, sizeof(path));
    file = fopen(path, "rb");
    if( !file )
        return 0;
    while( fgets(line, sizeof(line), file) )
    {
        char* end = line + strlen(line);
        while( end > line && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ') )
            *--end = '\0';
        if( strncmp(line, "lane = ", 7) == 0 )
            snprintf(out, out_size, "%s", line + 7);
    }
    fclose(file);
    return out[0] != '\0';
}

int
ToriRSMapEdLanes_SetBoot(
    const char* repo_root,
    const char* base_tree,
    const char* name,
    char* why,
    size_t why_size)
{
    char path[TORIRSMAPED_LANE_PATH + 32];
    char temp[TORIRSMAPED_LANE_PATH + 48];
    char dir[TORIRSMAPED_LANE_PATH + 16];
    char repo[TORIRSMAPED_LANE_PATH];
    struct ToriRSMapEdLaneDef lane;
    FILE* file;

    assert(repo_root);
    assert(base_tree);
    assert(name);
    assert(why);
    why[0] = '\0';
    if( strcmp(name, "main") != 0 && !ToriRSMapEdLanes_NameValid(name) )
    {
        snprintf(why, why_size, "'%s' is not a lane name", name);
        return 0;
    }
    if( !ToriRSMapEdLanes_Lookup(repo_root, base_tree, name, &lane) )
    {
        snprintf(why, why_size, "there is no lane '%s' (build/lanes/%s/lane.ini)", name, name);
        return 0;
    }
    resolve_abs(repo_root, repo, sizeof(repo));
    snprintf(dir, sizeof(dir), "%s/build/lanes", repo);
    if( !mkdir_p(dir) )
    {
        snprintf(why, why_size, "cannot create %s: %s", dir, strerror(errno));
        return 0;
    }
    boot_file_path(repo_root, path, sizeof(path));
    snprintf(temp, sizeof(temp), "%s.tmp", path);
    file = fopen(temp, "wb");
    if( !file )
    {
        snprintf(why, why_size, "cannot write %s: %s", temp, strerror(errno));
        return 0;
    }
    fprintf(file,
        "; The lane every editor boot on this repo serves (torirsmaped/torirs_maped_lanes.h),\n"
        "; whatever its manifest or --lane asks: written by the Lanes panel's Switch and\n"
        "; Scratch lane (LANE_SWITCH). Delete this file to boot as the manifest says.\n"
        "lane = %s\n",
        name);
    if( fclose(file) != 0 )
    {
        snprintf(why, why_size, "cannot write %s: %s", temp, strerror(errno));
        remove(temp);
        return 0;
    }
    if( rename(temp, path) != 0 )
    {
        snprintf(why, why_size, "cannot rename onto %s: %s", path, strerror(errno));
        remove(temp);
        return 0;
    }
    return 1;
}

int
ToriRSMapEdLanes_BootLane(
    const char* repo_root,
    const char* base_tree,
    const char* configured,
    char* out,
    size_t out_size,
    char* note,
    size_t note_size)
{
    char named[TORIRSMAPED_LANE_NAME_MAX + 8];
    const char* asked = configured && configured[0] ? configured : "main";
    struct ToriRSMapEdLaneDef lane;

    assert(repo_root);
    assert(base_tree);
    assert(out);
    assert(note);
    note[0] = '\0';
    snprintf(out, out_size, "%s", asked);
    /* A test run keeps to the lane it was started on. The file is the
     * owner's switch for their own boots, and it is repo-wide: a test editor
     * started on a private manifest (lane=perf1110) opened the lane the
     * owner had switched to an hour before, with nothing said but this
     * note (2026-11-10). */
    {
        const char* const ignore = getenv("TORIRSMAPED_IGNORE_BOOT_LANE");
        if( ignore && ignore[0] && strcmp(ignore, "0") != 0 )
        {
            snprintf(note, note_size, "lane '%s' (TORIRSMAPED_IGNORE_BOOT_LANE is set: build/lanes/lane.ini was not read)",
                asked);
            return 0;
        }
    }
    if( !ToriRSMapEdLanes_BootName(repo_root, named, sizeof(named)) )
        return 0;
    if( !ToriRSMapEdLanes_Lookup(repo_root, base_tree, named, &lane) )
    {
        snprintf(note, note_size, "build/lanes/lane.ini names lane '%s', which does not exist; serving '%s'", named,
            asked);
        return 0;
    }
    snprintf(out, out_size, "%s", named);
    if( strcmp(named, asked) == 0 )
        snprintf(note, note_size, "lane '%s' (build/lanes/lane.ini, as the boot asked)", named);
    else
        snprintf(note, note_size, "lane '%s' (build/lanes/lane.ini, switched in the editor; the boot asked for '%s')",
            named, asked);
    return 1;
}
