/*
 * Player persistence. See mock230_save.h.
 */

#include "mock230_save.h"
#include <assert.h>

#include "mock230.h"
#include "mock230_container.h"
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

/*
 * `mkdir -p` for the save directory.
 *
 * One `mkdir` was enough while the only path was the bare `saves`, and it stops
 * being enough the moment anything configures `MOCK230_SAVES` to something
 * nested — the mkdir fails with ENOENT, the fopen behind it fails too, and the
 * save is lost with one line on stderr that reads like a permissions problem.
 * `mock230_embed_test` points it at `build/embed_test_saves` so its runs cannot
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

static void
write_item_vars(
    FILE* file,
    const char* section,
    const struct Mock230Item* slots,
    int slot_count)
{
    int any = 0;

    for( int slot = 0; slot < slot_count; slot++ )
    {
        if( slots[slot].obj_id < 0 )
            continue;
        for( int v = 0; v < MOCK230_ITEM_VAR_MAX; v++ )
        {
            if( slots[slot].var_key[v] < 0 )
                continue;
            if( !any )
            {
                fprintf(file, "\n[%s]\n; <slot> <key_obj> = <value>\n", section);
                any = 1;
            }
            fprintf(file, "%d %d = %d\n", slot, slots[slot].var_key[v],
                    slots[slot].var_val[v]);
        }
    }
}

static void
write_poh(
    FILE* file,
    const struct Mock230PohState* poh)
{
    fprintf(file,
            "\n[poh]\n"
            "schema_version = %d\n"
            "owns_house = %d\n"
            "location = %d\n"
            "style = %d\n"
            "locked = %d\n"
            "door_mode = %d\n"
            "teleport_inside = %d\n"
            "default_build_mode = %d\n"
            "grid_size = %d\n"
            "servant_type = %d\n"
            "servant_paid = %d\n"
            "servant_last_task = %d\n"
            "money_bag = %d\n"
            "family_crest = %d\n",
            poh->schema_version, poh->owns_house, poh->location, poh->style,
            poh->locked, poh->door_mode, poh->teleport_inside,
            poh->default_build_mode, poh->grid_size, poh->servant_type,
            poh->servant_paid, poh->servant_last_task, poh->money_bag,
            poh->family_crest);

    fprintf(file,
            "\n[poh_rooms]\n"
            "; <slot> = <room_dbrow> <x> <z> <level> <rotation> <door_mask>\n");
    for( int i = 0; i < poh->room_count; i++ )
    {
        const struct Mock230PohRoom* room = &poh->rooms[i];

        fprintf(file, "%d = %d %d %d %d %d %d\n", i, room->dbrow, room->x,
                room->z, room->level, room->rotation, room->door_mask);
    }

    fprintf(file,
            "\n[poh_decorations]\n"
            "; <slot> = <room> <hotspot> <furniture_dbrow> <rotation> <flags>\n");
    for( int i = 0; i < poh->decoration_count; i++ )
    {
        const struct Mock230PohDecoration* decoration = &poh->decorations[i];

        fprintf(file, "%d = %d %d %d %d %d\n", i, decoration->room,
                decoration->hotspot, decoration->furniture_dbrow,
                decoration->rotation, decoration->flags);
    }
}

int
mock230_save_player(
    const struct Mock230Player* player,
    const char* path)
{
    char temp[1100];
    FILE* file;

    assert(path);
    if( !*path )
        return 0;

    save_mkdir_p(save_dir());

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
            "client_layout_mode = %d\n",
            MOCK230_SAVE_VERSION, player->display_name, player->x, player->z, player->level,
            player->run_energy, player->run_toggle, player->client_layout_mode);

    fprintf(file, "\n[stats]\n; <stat> = <boosted> <xp_tenths>\n");
    for( int stat = 0; stat < MOCK230_STAT_COUNT; stat++ )
    {
        /* Skip the untouched majority: 23 stats of `1 0` is noise in a file
         * whose whole point is being readable. Base level is derived from XP
         * on load, so it is not written. */
        if( player->stat_xp_tenths[stat] == 0 && player->stat_boosted[stat] <= 1 )
            continue;
        fprintf(file, "%d = %d %d\n", stat, player->stat_boosted[stat],
                player->stat_xp_tenths[stat]);
    }

    write_items(file, "inv", player->inv, MOCK230_INV_SLOTS);
    write_item_vars(file, "inv_var", player->inv, MOCK230_INV_SLOTS);
    write_items(file, "worn", player->worn, MOCK230_WORN_SLOTS);
    write_item_vars(file, "worn_var", player->worn, MOCK230_WORN_SLOTS);
    if( player->bank.slots && player->bank.size > 0 )
    {
        write_items(file, "bank", player->bank.slots, player->bank.size);
        write_item_vars(file, "bank_var", player->bank.slots, player->bank.size);
    }

    /*
     * Everything else the registry holds, keyed by inv id.
     *
     * Three hardcoded `write_items` calls and a fixed `enum SaveSection` were
     * the third copy of `container_for`'s three cases — persistence could only
     * save the containers the resolver could name, which is how a container the
     * client can be shown became one that vanishes at logout.
     *
     * The three above keep their spelled sections: they are what a person
     * hand-editing a save is looking for, and an old file still loads. What
     * separates them from the rest is not their ids but that the registry does
     * not own their storage, so the test is `owns_items` — no inv id in the
     * loop.
     */
    for( int i = 0; i < MOCK230_CONTAINER_MAX; i++ )
    {
        const struct Mock230Container* row = &player->containers[i];
        char section[64];

        if( !row->used || !row->owns_items || !row->items || row->slots <= 0 )
            continue;
        snprintf(section, sizeof(section), "container.%d", (int)row->inv_id);
        write_items(file, section, row->items, row->slots);
        snprintf(section, sizeof(section), "container_var.%d", (int)row->inv_id);
        write_item_vars(file, section, row->items, row->slots);
    }

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

    write_poh(file, &player->poh);

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
    SAVE_INV_VAR,
    SAVE_WORN_VAR,
    SAVE_BANK_VAR,
    SAVE_VARPS,
    SAVE_POH,
    SAVE_POH_ROOMS,
    SAVE_POH_DECORATIONS,
    /** A `[container.<inv>]` section; the id is in `section_inv`. */
    SAVE_CONTAINER,
    /** A `[container_var.<inv>]` section; same `section_inv` / `section_row`. */
    SAVE_CONTAINER_VAR,
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

static void
load_item_var(
    struct Mock230Item* slots,
    int slot_count,
    const char* key,
    const char* value)
{
    int slot = -1;
    int key_obj = -1;
    int val = 0;

    if( sscanf(key, "%d %d", &slot, &key_obj) != 2 )
        return;
    if( slot < 0 || slot >= slot_count || key_obj < 0 )
        return;
    val = atoi(value);
    mock230_item_set_var(&slots[slot], key_obj, val);
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
    int section_inv = -1;
    struct Mock230Container* section_row = NULL;
    int version = 0;
    int poh_version = 0;

    assert(path);
    if( !*path )
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
    assert(data);
    if( fread(data, 1, (size_t)size, file) != (size_t)size )
    {
        free(data);
        fclose(file);
        fprintf(stderr, "mock230: could not read %s\n", path);
        return 0;
    }
    fclose(file);

    /* Loading overlays a live player in selftests as well as a fresh login.
     * Clear the old house first so absent rows cannot survive from the previous
     * record. An absent save returned above and leaves the new-character
     * default alone. */
    mock230_poh_init(&player->poh);

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
            else if( strcmp(element._section.name, "inv_var") == 0 )
                section = SAVE_INV_VAR;
            else if( strcmp(element._section.name, "worn_var") == 0 )
                section = SAVE_WORN_VAR;
            else if( strcmp(element._section.name, "bank_var") == 0 )
                section = SAVE_BANK_VAR;
            else if( strcmp(element._section.name, "varps") == 0 )
                section = SAVE_VARPS;
            else if( strcmp(element._section.name, "poh") == 0 )
                section = SAVE_POH;
            else if( strcmp(element._section.name, "poh_rooms") == 0 )
                section = SAVE_POH_ROOMS;
            else if( strcmp(element._section.name, "poh_decorations") == 0 )
                section = SAVE_POH_DECORATIONS;
            else if( strncmp(element._section.name, "container_var.", 14) == 0 )
            {
                section = SAVE_CONTAINER_VAR;
                section_inv = atoi(element._section.name + 14);
                section_row =
                    mock230_container_resolve(player->world, player, (int32_t)section_inv);
            }
            else if( strncmp(element._section.name, "container.", 10) == 0 )
            {
                /*
                 * Resolve-or-create, exactly as a script naming the container
                 * would: the row does not exist on a fresh login, and the save
                 * is what brings it back. An inv the cache no longer sizes
                 * resolves to NULL and its rows are skipped, which is the same
                 * "a key this server does not know is ignored" rule the rest of
                 * the format has.
                 */
                section = SAVE_CONTAINER;
                section_inv = atoi(element._section.name + 10);
                section_row =
                    mock230_container_resolve(player->world, player, (int32_t)section_inv);
            }
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
            else if( strcmp(key, "client_layout_mode") == 0 )
            {
                int mode = atoi(value);

                /* Display-panel Fixed / Resizable Classic / Resizable Modern.
                 * Out-of-range keeps the init default (resizable classic). */
                if( mode >= 0 && mode <= 2 )
                    player->client_layout_mode = mode;
            }
            /* `prayer_active` was a saved bitmask until prayer became
             * content. The varbits carry it now and they ride the saved varps,
             * so an old save's line is ignored rather than migrated — reading
             * it would restore prayers the varps already disagree about. */
            /* An unknown key is a save from a newer server. Skipping it is the
             * whole reason this is a text format. */
            break;

        case SAVE_STATS:
        {
            int stat = atoi(key);
            int a = 1;
            int b = 1;
            int c = 0;
            int boosted;
            int xp;
            int n;

            if( stat < 0 || stat >= MOCK230_STAT_COUNT )
                break;
            n = sscanf(value, "%d %d %d", &a, &b, &c);
            if( n == 3 )
            {
                /* Legacy `<level> <boosted> <xp_tenths>` — ignore stored level;
                 * base is derived from XP (LostCity PlayerLoading). */
                boosted = b;
                xp = c;
            }
            else if( n == 2 )
            {
                boosted = a;
                xp = b;
            }
            else
                break;
            /* Clamped on the way in as well as at every grant: the file is
             * hand-editable text, and a total outside the range would put the
             * player somewhere `mock230_combat_add_xp` can never take them. */
            xp = mock230_combat_clamp_xp(xp);
            player->stat_xp_tenths[stat] = xp;
            player->stat_level[stat] = mock230_combat_level_for_xp(xp / 10);
            player->stat_boosted[stat] = boosted;
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
        case SAVE_INV_VAR:
            load_item_var(player->inv, MOCK230_INV_SLOTS, key, value);
            break;
        case SAVE_WORN_VAR:
            load_item_var(player->worn, MOCK230_WORN_SLOTS, key, value);
            break;
        case SAVE_BANK_VAR:
            if( player->bank.slots && player->bank.size > 0 )
                load_item_var(player->bank.slots, player->bank.size, key, value);
            break;

        case SAVE_CONTAINER:
            if( section_row && section_row->items )
                load_item(section_row->items, section_row->slots, key, value);
            break;
        case SAVE_CONTAINER_VAR:
            if( section_row && section_row->items )
                load_item_var(section_row->items, section_row->slots, key, value);
            break;

        case SAVE_VARPS:
        {
            int varp = atoi(key);

            if( varp >= 0 && varp < MOCK230_VARP_COUNT )
                player->varps[varp] = (int32_t)atoi(value);
            break;
        }

        case SAVE_POH:
            if( strcmp(key, "schema_version") == 0 )
                poh_version = atoi(value);
            else if( strcmp(key, "owns_house") == 0 )
                mock230_poh_set(&player->poh, MOCK230_POH_FIELD_OWNS_HOUSE, atoi(value));
            else if( strcmp(key, "location") == 0 )
                mock230_poh_set(&player->poh, MOCK230_POH_FIELD_LOCATION, atoi(value));
            else if( strcmp(key, "style") == 0 )
                mock230_poh_set(&player->poh, MOCK230_POH_FIELD_STYLE, atoi(value));
            else if( strcmp(key, "locked") == 0 )
                mock230_poh_set(&player->poh, MOCK230_POH_FIELD_LOCKED, atoi(value));
            else if( strcmp(key, "door_mode") == 0 )
                mock230_poh_set(&player->poh, MOCK230_POH_FIELD_DOOR_MODE, atoi(value));
            else if( strcmp(key, "teleport_inside") == 0 )
                mock230_poh_set(
                    &player->poh, MOCK230_POH_FIELD_TELEPORT_INSIDE, atoi(value));
            else if( strcmp(key, "default_build_mode") == 0 )
                mock230_poh_set(
                    &player->poh, MOCK230_POH_FIELD_DEFAULT_BUILD_MODE, atoi(value));
            else if( strcmp(key, "grid_size") == 0 )
                mock230_poh_set(&player->poh, MOCK230_POH_FIELD_GRID_SIZE, atoi(value));
            else if( strcmp(key, "servant_type") == 0 )
                mock230_poh_set(&player->poh, MOCK230_POH_FIELD_SERVANT_TYPE, atoi(value));
            else if( strcmp(key, "servant_paid") == 0 )
                mock230_poh_set(&player->poh, MOCK230_POH_FIELD_SERVANT_PAID, atoi(value));
            else if( strcmp(key, "servant_last_task") == 0 )
                mock230_poh_set(
                    &player->poh, MOCK230_POH_FIELD_SERVANT_LAST_TASK, atoi(value));
            else if( strcmp(key, "money_bag") == 0 )
                mock230_poh_set(&player->poh, MOCK230_POH_FIELD_MONEY_BAG, atoi(value));
            else if( strcmp(key, "family_crest") == 0 )
                mock230_poh_set(
                    &player->poh, MOCK230_POH_FIELD_FAMILY_CREST, atoi(value));
            break;

        case SAVE_POH_ROOMS:
        {
            int slot = atoi(key);
            int dbrow;
            int x;
            int z;
            int level;
            int rotation;
            int door_mask;

            if( slot != player->poh.room_count )
                break;
            if( sscanf(value, "%d %d %d %d %d %d", &dbrow, &x, &z, &level,
                       &rotation, &door_mask) != 6 )
                break;
            mock230_poh_room_add(&player->poh, dbrow, x, z, level, rotation, door_mask);
            break;
        }

        case SAVE_POH_DECORATIONS:
        {
            int slot = atoi(key);
            int room;
            int hotspot;
            int furniture;
            int rotation;
            int flags;

            if( slot != player->poh.decoration_count )
                break;
            if( sscanf(value, "%d %d %d %d %d", &room, &hotspot, &furniture,
                       &rotation, &flags) != 5 )
                break;
            mock230_poh_decoration_set(
                &player->poh, room, hotspot, furniture, rotation, flags);
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
    if( poh_version > MOCK230_POH_SCHEMA_VERSION )
        fprintf(stderr,
                "mock230: %s has POH schema %d and this server writes %d — "
                "unknown POH fields were ignored\n",
                path, poh_version, MOCK230_POH_SCHEMA_VERSION);
    if( !mock230_poh_validate(&player->poh) )
    {
        fprintf(stderr, "mock230: %s contains an invalid POH record; using an empty house\n",
                path);
        mock230_poh_init(&player->poh);
    }
    return 1;
}
