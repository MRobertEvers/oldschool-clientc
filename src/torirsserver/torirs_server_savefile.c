/*
 * The save directory and the files in it. See torirs_server_savefile.h.
 */

#include "torirs_server_savefile.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define save_mkdir(p) _mkdir(p)
#else
#define save_mkdir(p) mkdir(p, 0755)
#endif

/*
 * Put the finished temp file where the save lives, replacing it.
 *
 * Not plain rename() on Windows: the CRT's rename refuses an existing
 * destination, so there every save after a character's first failed here, the
 * temp was removed, and the character logged back in as they were the first
 * time they left -- or, if that first save never happened, as a new character.
 * MoveFileEx with REPLACE_EXISTING is the atomic replace POSIX rename already is.
 */
int
ToriRSServer_SaveReplace(
    const char* temp,
    const char* path)
{
#ifdef _WIN32
    return MoveFileExA(temp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : -1;
#else
    return rename(temp, path);
#endif
}

const char*
ToriRSServer_SaveDir(void)
{
    const char* configured = getenv("TORIRSSERVER_SAVES");

    return configured ? configured : "saves";
}

/*
 * `mkdir -p` for the save directory.
 *
 * One `mkdir` was enough while the only path was the bare `saves`, and it stops
 * being enough the moment anything configures `TORIRSSERVER_SAVES` to something
 * nested — the mkdir fails with ENOENT, the fopen behind it fails too, and the
 * save is lost with one line on stderr that reads like a permissions problem.
 * `ToriRSServer_EmbedTest` points it at `build/embed_test_saves` so its runs cannot
 * contaminate each other, which is what found this.
 */
static void
save_mkdir_p(const char* dir)
{
    char path[1024];
    size_t used = 0;

    for( const char* scan = dir; *scan; scan++ )
    {
        if( used + 1 >= sizeof(path) )
            return;
        if( (*scan == '/' || *scan == '\\') && used > 0 )
        {
            path[used] = '\0';
            save_mkdir(path);
        }
        path[used++] = *scan;
    }
    path[used] = '\0';
    if( used > 0 )
        save_mkdir(path);
}

/*
 * The file name a login name saves under: `[a-z0-9_]`, or "" when nothing is
 * left. The character save and the account record share it, so one name is
 * one character is one password.
 */
void
ToriRSServer_SaveFileName(
    const char* display_name,
    char* name,
    size_t cap)
{
    size_t written = 0;

    /*
     * The name comes off the login screen, so it is whatever the client sent.
     * Sanitising is not tidiness: `../../etc/passwd` is a valid login name and
     * must not become a path. Anything outside [a-z0-9_] becomes '_', and a name
     * that produces nothing at all yields an empty path the caller refuses.
     */
    for( const char* scan = display_name; scan && *scan && written < cap - 1; scan++ )
    {
        unsigned char ch = (unsigned char)*scan;

        if( isalnum(ch) )
            name[written++] = (char)tolower(ch);
        else if( ch == '_' || ch == '-' || ch == ' ' )
            name[written++] = '_';
        /* Everything else — dots, slashes, NULs — is dropped rather than
         * mapped, so `..` cannot survive as `__`. */
    }
    name[written] = '\0';
}

const char*
ToriRSServer_SavePath(const char* display_name)
{
    static char path[1024];
    char name[64];

    ToriRSServer_SaveFileName(display_name, name, sizeof(name));
    if( !name[0] )
    {
        path[0] = '\0';
        return path;
    }
    snprintf(path, sizeof(path), "%s/%s.ini", ToriRSServer_SaveDir(), name);
    return path;
}

const char*
ToriRSServer_AccountPath(const char* display_name)
{
    static char path[1024];
    char name[64];

    ToriRSServer_SaveFileName(display_name, name, sizeof(name));
    if( !name[0] )
    {
        path[0] = '\0';
        return path;
    }
    snprintf(path, sizeof(path), "%s/accounts/%s.ini", ToriRSServer_SaveDir(), name);
    return path;
}

const char*
ToriRSServer_GateAccountPath(const char* name)
{
    static char path[1024];
    char file[64];

    ToriRSServer_SaveFileName(name, file, sizeof(file));
    if( !file[0] )
    {
        path[0] = '\0';
        return path;
    }
    snprintf(path, sizeof(path), "%s/gate/%s.ini", ToriRSServer_SaveDir(), file);
    return path;
}

/* `mkdir -p` the directory `path` is in; the save directory when it names none. */
void
ToriRSServer_SaveMkdirParent(const char* path)
{
    char parent[1024];
    char* slash;
    char* backslash;

    snprintf(parent, sizeof(parent), "%s", path);
    slash = strrchr(parent, '/');
    backslash = strrchr(parent, '\\');
    if( backslash && (!slash || backslash > slash) )
        slash = backslash;
    if( slash )
        *slash = '\0';
    else
        snprintf(parent, sizeof(parent), "%s", ToriRSServer_SaveDir());
    save_mkdir_p(parent);
}

int
ToriRSServer_SaveWriteText(
    const char* path,
    const char* text)
{
    char temp[1100];
    FILE* file;
    size_t len;

    assert(path);
    assert(path[0]);
    assert(text);
    ToriRSServer_SaveMkdirParent(path);
    snprintf(temp, sizeof(temp), "%s.tmp", path);
    file = fopen(temp, "wb");
    if( !file )
    {
        fprintf(stderr, "torirsserver: cannot write %s\n", temp);
        return 0;
    }
    len = strlen(text);
    if( fwrite(text, 1, len, file) != len )
    {
        fclose(file);
        remove(temp);
        fprintf(stderr, "torirsserver: cannot write %s\n", temp);
        return 0;
    }
    if( fclose(file) != 0 || ToriRSServer_SaveReplace(temp, path) != 0 )
    {
        fprintf(stderr, "torirsserver: cannot replace %s\n", path);
        remove(temp);
        return 0;
    }
    return 1;
}

/* `<save dir>/names/<key>`, the reservation of one display name. */
static void
display_name_path(
    const char* key,
    char* path,
    size_t cap)
{
    snprintf(path, cap, "%s/names/%s", ToriRSServer_SaveDir(), key);
}

static int
file_exists(const char* path)
{
    struct stat info;

    return path[0] && stat(path, &info) == 0;
}

/* The account a reservation file names: its first line. 0 when there is no
 * file; a file that cannot be read counts as held, by nobody nameable. */
static int
display_name_read(
    const char* path,
    char* owner,
    size_t cap)
{
    FILE* file = fopen(path, "rb");
    size_t len;

    if( !file )
        return 0;
    if( !fgets(owner, (int)cap, file) )
        owner[0] = '\0';
    fclose(file);
    len = strlen(owner);
    while( len > 0 && (owner[len - 1] == '\n' || owner[len - 1] == '\r') )
        owner[--len] = '\0';
    return 1;
}

int
ToriRSServer_DisplayNameOwner(
    const char* name,
    char* owner,
    size_t cap)
{
    char key[64];
    char path[1024];

    assert(name);
    assert(owner);
    ToriRSServer_SaveFileName(name, key, sizeof(key));
    assert(key[0]);
    display_name_path(key, path, sizeof(path));
    if( display_name_read(path, owner, cap) )
        return 1;
    if( file_exists(ToriRSServer_SavePath(key)) || file_exists(ToriRSServer_AccountPath(key)) )
    {
        snprintf(owner, cap, "%s", key);
        return 1;
    }
    return 0;
}

int
ToriRSServer_DisplayNameClaim(
    const char* name,
    const char* account)
{
    char key[64];
    char account_key[64];
    char owner[64];
    char path[1024];
    FILE* file;

    assert(name);
    assert(account);
    ToriRSServer_SaveFileName(name, key, sizeof(key));
    ToriRSServer_SaveFileName(account, account_key, sizeof(account_key));
    assert(key[0]);
    assert(account_key[0]);
    if( ToriRSServer_DisplayNameOwner(name, owner, sizeof(owner)) )
        return strcmp(owner, account_key) == 0;
    display_name_path(key, path, sizeof(path));
    ToriRSServer_SaveMkdirParent(path);
    /* "x": the create fails when the file exists, so the check above and
     * this write cannot let two accounts through between them. */
    file = fopen(path, "wbx");
    if( !file )
    {
        if( display_name_read(path, owner, sizeof(owner)) )
            return strcmp(owner, account_key) == 0;
        fprintf(stderr, "torirsserver: cannot write %s\n", path);
        return 0;
    }
    if( fprintf(file, "%s\n", account_key) < 0 || fclose(file) != 0 )
    {
        remove(path);
        fprintf(stderr, "torirsserver: cannot write %s\n", path);
        return 0;
    }
    return 1;
}

void
ToriRSServer_DisplayNameRelease(
    const char* name,
    const char* account)
{
    char key[64];
    char account_key[64];
    char owner[64];
    char path[1024];

    assert(name);
    assert(account);
    ToriRSServer_SaveFileName(name, key, sizeof(key));
    ToriRSServer_SaveFileName(account, account_key, sizeof(account_key));
    assert(key[0]);
    display_name_path(key, path, sizeof(path));
    if( display_name_read(path, owner, sizeof(owner)) && strcmp(owner, account_key) == 0 )
        remove(path);
}
