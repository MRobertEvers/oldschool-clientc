/*
 * Player persistence. See mock230_save.h.
 */

#include "mock230_save.h"

#include "mock230.h"
#include "mock230_content.h"

#include "3rd/ini/ini.h"
#include "content/content_register.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define save_mkdir(p) _mkdir(p)
#else
#define save_mkdir(p) mkdir(p, 0755)
#endif

/*
 * Bumped when a field changes meaning rather than when one is added.
 *
 * Adding a key needs no bump: an unknown key is skipped on load and a missing
 * one keeps its default, so old and new saves both work. A bump is for the case
 * where the same key now means something different, which is the only situation
 * a reader cannot detect on its own.
 */
#define MOCK230_SAVE_VERSION 1

static const char*
save_dir(void)
{
    const char* configured = getenv("MOCK230_SAVES");

    return configured ? configured : "saves";
}

const char*
mock230_save_path(const char* display_name)
{
    static char path[1024];
    char name[64];
    int written = 0;

    /*
     * The name comes off the login screen, so it is whatever the client sent.
     * Sanitising is not tidiness: `../../etc/passwd` is a valid login name and
     * must not become a path. Anything outside [a-z0-9_] becomes '_', and a name
     * that produces nothing at all yields an empty path the caller refuses.
     */
    for( const char* scan = display_name; scan && *scan && written < (int)sizeof(name) - 1;
         scan++ )
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

    if( written == 0 )
    {
        path[0] = '\0';
        return path;
    }
    snprintf(path, sizeof(path), "%s/%s.ini", save_dir(), name);
    return path;
}

/* ------------------------------------------------------------------ */
/* Write                                                               */
/* ------------------------------------------------------------------ */

static void
write_items(
    FILE* file,
    const char* section,
    const struct Mock230Item* slots,
    int slot_count)
{
    fprintf(file, "\n[%s]\n; <slot> = <obj> <count>\n", section);
    for( int slot = 0; slot < slot_count; slot++ )
    {
        if( slots[slot].obj_id < 0 || slots[slot].count <= 0 )
            continue;
        fprintf(file, "%d = %d %d\n", slot, slots[slot].obj_id, slots[slot].count);
    }
}

int
mock230_save_player(
    const struct Mock230Player* player,
    const char* path)
{
    char temp[1100];
    FILE* file;

    if( !path || !*path )
        return 0;

    save_mkdir(save_dir());

    /* Write-then-rename: a crash mid-write leaves the previous save intact
     * rather than a truncated one. For a save file that is the difference
     * between losing a session and losing a character. */
    snprintf(temp, sizeof(temp), "%s.tmp", path);
    file = fopen(temp, "wb");
    if( !file )
    {
        fprintf(stderr, "mock230: cannot write %s\n", temp);
        return 0;
    }

    fprintf(file,
            "; mock230 player save. Written by mock230_save.c; safe to hand-edit.\n"
            "; A key this server does not know is skipped, and a missing one keeps\n"
            "; whatever a fresh character would have — so an old save still loads.\n"
            "\n[player]\n"
            "version = %d\n"
            "name = %s\n"
            "x = %d\n"
            "z = %d\n"
            "level = %d\n"
            "run_energy = %d\n"
            "run_toggle = %d\n"
            "prayer_active = %u\n",
            MOCK230_SAVE_VERSION, player->display_name, player->x, player->z, player->level,
            player->run_energy, player->run_toggle, (unsigned)player->prayer_active);

    fprintf(file, "\n[stats]\n; <stat> = <level> <boosted> <xp_tenths>\n");
    for( int stat = 0; stat < MOCK230_STAT_COUNT; stat++ )
    {
        /* Skip the untouched majority: 23 stats of `1 1 0` is noise in a file
         * whose whole point is being readable. */
        if( player->stat_level[stat] <= 1 && player->stat_xp_tenths[stat] == 0 &&
            player->stat_boosted[stat] <= 1 )
            continue;
        fprintf(file, "%d = %d %d %d\n", stat, player->stat_level[stat],
                player->stat_boosted[stat], player->stat_xp_tenths[stat]);
    }

    write_items(file, "inv", player->inv, MOCK230_INV_SLOTS);
    write_items(file, "worn", player->worn, MOCK230_WORN_SLOTS);
    if( player->bank.slots && player->bank.size > 0 )
        write_items(file, "bank", player->bank.slots, player->bank.size);

    /*
     * Only `scope=perm` varps.
     *
     * Content's decision, not the engine's — the same rule LostCity uses, and
     * the reason `Mock230VarpDef.scope_perm` has been read off `.varp` configs
     * and ignored since that reader was written. An *undeclared* varp is server
     * bookkeeping and is deliberately not saved, matching the transmit gate's
     * default: if content never said the variable exists, the engine has no
     * business making it outlive the session.
     */
    fprintf(file, "\n[varps]\n; only varps whose .varp config says scope=perm\n");
    for( int varp = 0; varp < MOCK230_VARP_COUNT; varp++ )
    {
        const struct Mock230VarpDef* def;

        if( player->varps[varp] == 0 )
            continue;
        def = mock230_content_varp(varp);
        if( !def || !def->scope_perm )
            continue;
        fprintf(file, "%d = %d\n", varp, (int)player->varps[varp]);
    }

    fclose(file);

    if( rename(temp, path) != 0 )
    {
        fprintf(stderr, "mock230: cannot replace %s\n", path);
        remove(temp);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Read                                                                */
/* ------------------------------------------------------------------ */

enum SaveSection
{
    SAVE_NONE = 0,
    SAVE_PLAYER,
    SAVE_STATS,
    SAVE_INV,
    SAVE_WORN,
    SAVE_BANK,
    SAVE_VARPS,
};

static void
load_item(
    struct Mock230Item* slots,
    int slot_count,
    const char* key,
    const char* value)
{
    int slot = atoi(key);
    int obj_id = -1;
    int count = 0;

    if( slot < 0 || slot >= slot_count )
        return;
    if( sscanf(value, "%d %d", &obj_id, &count) != 2 )
        return;
    if( obj_id < 0 || count <= 0 )
        return;
    slots[slot].obj_id = obj_id;
    slots[slot].count = count;
}

int
mock230_load_player(
    struct Mock230Player* player,
    const char* path)
{
    FILE* file;
    long size;
    uint8_t* data;
    struct INIReader reader;
    struct INIElement element;
    enum SaveSection section = SAVE_NONE;
    int version = 0;

    if( !path || !*path )
        return 0;
    file = fopen(path, "rb");
    if( !file )
        return 0; /* a new character, not an error */

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if( size <= 0 )
    {
        fclose(file);
        return 0;
    }
    data = (uint8_t*)malloc((size_t)size);
    if( !data || fread(data, 1, (size_t)size, file) != (size_t)size )
    {
        free(data);
        fclose(file);
        fprintf(stderr, "mock230: could not read %s\n", path);
        return 0;
    }
    fclose(file);

    ini_reader_init(&reader);
    while( ini_reader_next(&reader, data, (uint32_t)size, &element) == TORI_INI_ERR_OK )
    {
        const char* key;
        const char* value;

        if( element.kind == INI_ELEMENT_SECTION )
        {
            ContentIni_Trim(element._section.name);
            if( strcmp(element._section.name, "player") == 0 )
                section = SAVE_PLAYER;
            else if( strcmp(element._section.name, "stats") == 0 )
                section = SAVE_STATS;
            else if( strcmp(element._section.name, "inv") == 0 )
                section = SAVE_INV;
            else if( strcmp(element._section.name, "worn") == 0 )
                section = SAVE_WORN;
            else if( strcmp(element._section.name, "bank") == 0 )
                section = SAVE_BANK;
            else if( strcmp(element._section.name, "varps") == 0 )
                section = SAVE_VARPS;
            else
                section = SAVE_NONE; /* a section from a newer server */
            continue;
        }
        if( element.kind != INI_ELEMENT_KEYVAL )
            continue;

        /* 3rd/ini does not trim — see ContentIni_Trim. */
        ContentIni_Trim(element._keyval.name);
        ContentIni_Trim(element._keyval.value);
        key = element._keyval.name;
        value = element._keyval.value;

        switch( section )
        {
        case SAVE_PLAYER:
            if( strcmp(key, "version") == 0 )
                version = atoi(value);
            else if( strcmp(key, "name") == 0 )
                snprintf(player->display_name, sizeof(player->display_name), "%s", value);
            else if( strcmp(key, "x") == 0 )
                player->x = atoi(value);
            else if( strcmp(key, "z") == 0 )
                player->z = atoi(value);
            else if( strcmp(key, "level") == 0 )
                player->level = atoi(value);
            else if( strcmp(key, "run_energy") == 0 )
                player->run_energy = atoi(value);
            else if( strcmp(key, "run_toggle") == 0 )
                player->run_toggle = atoi(value);
            else if( strcmp(key, "prayer_active") == 0 )
                player->prayer_active = (uint32_t)strtoul(value, NULL, 10);
            /* An unknown key is a save from a newer server. Skipping it is the
             * whole reason this is a text format. */
            break;

        case SAVE_STATS:
        {
            int stat = atoi(key);
            int level = 1;
            int boosted = 1;
            int xp = 0;

            if( stat < 0 || stat >= MOCK230_STAT_COUNT )
                break;
            if( sscanf(value, "%d %d %d", &level, &boosted, &xp) != 3 )
                break;
            player->stat_level[stat] = level;
            player->stat_boosted[stat] = boosted;
            player->stat_xp_tenths[stat] = xp;
            break;
        }

        case SAVE_INV:
            load_item(player->inv, MOCK230_INV_SLOTS, key, value);
            break;
        case SAVE_WORN:
            load_item(player->worn, MOCK230_WORN_SLOTS, key, value);
            break;
        case SAVE_BANK:
            if( player->bank.slots && player->bank.size > 0 )
                load_item(player->bank.slots, player->bank.size, key, value);
            break;

        case SAVE_VARPS:
        {
            int varp = atoi(key);

            if( varp >= 0 && varp < MOCK230_VARP_COUNT )
                player->varps[varp] = (int32_t)atoi(value);
            break;
        }

        case SAVE_NONE:
        default:
            break;
        }
    }

    free(data);

    if( version > MOCK230_SAVE_VERSION )
        fprintf(stderr,
                "mock230: %s is version %d and this server writes %d — fields this "
                "server does not know were ignored\n",
                path, version, MOCK230_SAVE_VERSION);
    return 1;
}
