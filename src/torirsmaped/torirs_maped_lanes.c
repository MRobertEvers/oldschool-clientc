/*
 * Editor lanes: the registry under build/lanes, the content mirror, a lane's
 * changes against its base, publish and discard (torirs_maped_lanes.h).
 *
 * Every walk here is over a content tree of ~100 k entries; a changes scan
 * stats the lane's own directories (symlinks are not followed, so the
 * directory-linked asset trees cost one entry each) and reads only the files
 * that are real in the mirror, which is the edited few. The deleted set
 * walks the base's directories that are real in the mirror.
 */
#include "torirs_maped_lanes.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

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
#else
#define lanes_mkdir(path) mkdir(path, 0755)
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
 *  each at the tree's root. */
static const char* const k_linked[] = { "animayas", "animsets", "binary", "fonts", "framemaps", "jingles",
    "models", "samples", "songs", "sprites", "synth", "textures", "wiki", "npc_combat", NULL };
/** Copied, not linked: tools/ss_allocate.py rewrites a ledger in place. */
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
    out = fopen(temp, "wb");
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
        if( rel_skipped(sub_rel) )
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
        if( rel_skipped(entry->d_name) )
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
    while( fgets(line, sizeof(line), file) )
    {
        char* end = line + strlen(line);
        while( end > line && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ') )
            *--end = '\0';
        if( strncmp(line, "base = ", 7) == 0 )
            snprintf(lane->base, sizeof(lane->base), "%s", line + 7);
        else if( strncmp(line, "created = ", 10) == 0 )
            lane->created = strtoll(line + 10, NULL, 10);
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
    int files;

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
    files = ToriRSMapEdLanes_Mirror(out->base, out->content, why, why_size);
    if( files < 0 )
        return 0;
    snprintf(path, sizeof(path), "%s/lane.ini", out->dir);
    file = fopen(path, "wb");
    if( !file )
    {
        snprintf(why, why_size, "cannot write %s: %s", path, strerror(errno));
        return 0;
    }
    fprintf(file,
        "; An editor lane (torirsmaped/torirs_maped_lanes.h): content/ mirrors the\n"
        "; base tree copy-on-write, live/ holds its packs, cache and servers.\n"
        "base = %s\n"
        "created = %lld\n"
        "mirrored_files = %d\n",
        out->base, out->created, files);
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
    DIR* dir;
    struct dirent* entry;

    if( rel[0] )
        snprintf(dir_path, sizeof(dir_path), "%s/%s", lane->content, rel);
    else
        snprintf(dir_path, sizeof(dir_path), "%s", lane->content);
    dir = opendir(dir_path);
    if( !dir )
        return;
    while( (entry = readdir(dir)) )
    {
        char sub_rel[TORIRSMAPED_LANE_PATH];
        char path[TORIRSMAPED_LANE_PATH];
        char base_path[TORIRSMAPED_LANE_PATH];
        struct stat st;
        struct stat bst;

        if( strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 )
            continue;
        if( rel[0] )
            snprintf(sub_rel, sizeof(sub_rel), "%s/%s", rel, entry->d_name);
        else
            snprintf(sub_rel, sizeof(sub_rel), "%s", entry->d_name);
        if( rel_transient(sub_rel) )
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
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
        if( stat(base_path, &bst) != 0 )
            change_add(sink, TORIRSMAPED_LANE_CHANGE_ADDED, sub_rel, (uint32_t)st.st_size);
        else if( !files_equal(path, base_path) )
            change_add(sink, TORIRSMAPED_LANE_CHANGE_MODIFIED, sub_rel, (uint32_t)st.st_size);
    }
    closedir(dir);
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
    DIR* dir;
    struct dirent* entry;

    if( rel[0] )
        snprintf(dir_path, sizeof(dir_path), "%s/%s", lane->base, rel);
    else
        snprintf(dir_path, sizeof(dir_path), "%s", lane->base);
    dir = opendir(dir_path);
    if( !dir )
        return;
    while( (entry = readdir(dir)) )
    {
        char sub_rel[TORIRSMAPED_LANE_PATH];
        char base_path[TORIRSMAPED_LANE_PATH];
        char lane_path[TORIRSMAPED_LANE_PATH];
        struct stat bst;
        struct stat lst;

        if( strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 )
            continue;
        if( rel[0] )
            snprintf(sub_rel, sizeof(sub_rel), "%s/%s", rel, entry->d_name);
        else
            snprintf(sub_rel, sizeof(sub_rel), "%s", entry->d_name);
        if( rel_transient(sub_rel) )
            continue;
        if( !rel[0] && in_list(k_linked, entry->d_name) )
            continue;
        snprintf(base_path, sizeof(base_path), "%s/%s", dir_path, entry->d_name);
        snprintf(lane_path, sizeof(lane_path), "%s/%s", lane->content, sub_rel);
        if( stat(base_path, &bst) != 0 )
            continue;
        if( lstat(lane_path, &lst) != 0 )
        {
            /* A base file the mirror never had (younger than the lane) is
             * new in the base, not a deletion by the lane. */
            if( S_ISREG(bst.st_mode) && !(lane->created > 0 && (long long)bst.st_mtime > lane->created) )
                change_add(sink, TORIRSMAPED_LANE_CHANGE_DELETED, sub_rel, 0);
            continue;
        }
        if( S_ISDIR(bst.st_mode) && S_ISDIR(lst.st_mode) )
            changes_walk_base(lane, sub_rel, sink);
    }
    closedir(dir);
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
    changes_walk_lane(lane, "", &sink);
    changes_walk_base(lane, "", &sink);
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
            if( !relink(src, base_path, why, why_size) )
            {
                ToriMem_Free(ToriMem_Editor(), changes);
                return 0;
            }
        }
    }
    ToriMem_Free(ToriMem_Editor(), changes);
    return 1;
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
