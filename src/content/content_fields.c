/*
 * The field register. See content_fields.h.
 */

#include "content_fields.h"

#include "content_register.h"

#include "3rd/ini/ini.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The defaults, transcribed rather than designed.
 *
 * Two tables already held most of the answer, which is what made this tractable
 * (docs/CONTENT_PACK_PLAN.md §5.2):
 *
 *   the server column   `mock230_content.c`'s npc key ladder — hitpoints, attack,
 *                       strength, defence, magic, ranged, respawnrate, wanderrange,
 *                       moverestrict, huntmode
 *   the `param:` column `mock230_pack.c`'s `BakedParam` table, verbatim
 *
 * The two key sets overlap on exactly `name` and `param`, so the union is nearly
 * disjoint and unifying them was additive on both sides rather than a conflict to
 * resolve.
 *
 * The fields the client's own record carries *are* listed, as
 * `scope = client, client = native`, and that is not a second copy of `cp_npc.c`'s
 * emitter — it is what lets the config parser stop hardcoding them. LostCity authors
 * `name=` and `op1=` because it *builds* the npc record; ours comes from the cache,
 * so those keys are inert here. They used to be a `k_from_cache[]` array in
 * `mock230_content.c` that the parser checked before rejecting a key, which meant
 * "the client already states this" lived in C where a content author could not see
 * it or add to it.
 *
 * Only the ones a config might plausibly carry are listed. The full 43 keys
 * `cp_npc.c` emits are the encoder's business; a field nobody has written in a
 * config has nothing to declare.
 */

struct FieldDefault
{
    const char* type;
    const char* name;
    enum ContentFieldScope scope;
    enum ContentFieldClient client;
    const char* param_name;
};

static const struct FieldDefault k_defaults[] = {
    /*
     * npc combat, as `mock230_pack --cache-out` has always baked it.
     *
     * The param names are the server's own (`pack/param.pack` 2634 and up) except
     * `attackrate`, which is param 14 — a param the *cache* defines, so an npc's
     * attack rate lands where a client reading the cache alone would look for it.
     */
    { "npc", "hitpoints",     CONTENT_SCOPE_SERVER, CONTENT_CLIENT_PARAM, "hitpoints"     },
    { "npc", "attack",        CONTENT_SCOPE_SERVER, CONTENT_CLIENT_PARAM, "attacklevel"   },
    { "npc", "strength",      CONTENT_SCOPE_SERVER, CONTENT_CLIENT_PARAM, "strengthlevel" },
    { "npc", "defence",       CONTENT_SCOPE_SERVER, CONTENT_CLIENT_PARAM, "defencelevel"  },
    { "npc", "respawnrate",   CONTENT_SCOPE_SERVER, CONTENT_CLIENT_PARAM, "respawnrate"   },
    { "npc", "wanderrange",   CONTENT_SCOPE_SERVER, CONTENT_CLIENT_PARAM, "wanderrange"   },
    { "npc", "huntrange",     CONTENT_SCOPE_SERVER, CONTENT_CLIENT_PARAM, "huntrange"     },
    { "npc", "attackrate",    CONTENT_SCOPE_SERVER, CONTENT_CLIENT_PARAM, "attackrate"    },
    { "npc", "death_drop",    CONTENT_SCOPE_SERVER, CONTENT_CLIENT_PARAM, "death_drop"    },
    { "npc", "attack_anim",   CONTENT_SCOPE_SERVER, CONTENT_CLIENT_PARAM, "attack_anim"   },
    { "npc", "defend_anim",   CONTENT_SCOPE_SERVER, CONTENT_CLIENT_PARAM, "defend_anim"   },
    { "npc", "death_anim",    CONTENT_SCOPE_SERVER, CONTENT_CLIENT_PARAM, "death_anim"    },
    /*
     * Authored, and deliberately not projected.
     *
     * `magic` and `ranged` have no param the cache defines and no server param has
     * been allocated for them, so they are `drop` rather than silently baked into a
     * number nobody agreed on. `huntmode` and `nomove` are behaviour the server
     * decides and the client never asks about.
     */
    { "npc", "magic",         CONTENT_SCOPE_SERVER, CONTENT_CLIENT_DROP,  ""              },
    { "npc", "ranged",        CONTENT_SCOPE_SERVER, CONTENT_CLIENT_DROP,  ""              },
    { "npc", "huntmode",      CONTENT_SCOPE_SERVER, CONTENT_CLIENT_DROP,  ""              },
    { "npc", "nomove",        CONTENT_SCOPE_SERVER, CONTENT_CLIENT_DROP,  ""              },

    /*
     * Stated by the client's own record, so an overlay that repeats one is patching
     * the cache rather than adding to it — which the loader now says out loud
     * instead of silently accepting.
     */
    { "npc", "name",          CONTENT_SCOPE_CLIENT, CONTENT_CLIENT_NATIVE, ""              },
    { "npc", "desc",          CONTENT_SCOPE_CLIENT, CONTENT_CLIENT_NATIVE, ""              },
    { "npc", "vislevel",      CONTENT_SCOPE_CLIENT, CONTENT_CLIENT_NATIVE, ""              },
    { "npc", "size",          CONTENT_SCOPE_CLIENT, CONTENT_CLIENT_NATIVE, ""              },
    { "npc", "category",      CONTENT_SCOPE_CLIENT, CONTENT_CLIENT_NATIVE, ""              },
    { "npc", "walkanim",      CONTENT_SCOPE_CLIENT, CONTENT_CLIENT_NATIVE, ""              },
    { "npc", "readyanim",     CONTENT_SCOPE_CLIENT, CONTENT_CLIENT_NATIVE, ""              },
    { "npc", "op1",           CONTENT_SCOPE_CLIENT, CONTENT_CLIENT_NATIVE, ""              },
    { "npc", "op2",           CONTENT_SCOPE_CLIENT, CONTENT_CLIENT_NATIVE, ""              },
    { "npc", "op3",           CONTENT_SCOPE_CLIENT, CONTENT_CLIENT_NATIVE, ""              },
    { "npc", "op4",           CONTENT_SCOPE_CLIENT, CONTENT_CLIENT_NATIVE, ""              },
    { "npc", "op5",           CONTENT_SCOPE_CLIENT, CONTENT_CLIENT_NATIVE, ""              },

    /*
     * A door's other half.
     *
     * The cache states which locs exist and what they look like; nothing in it says
     * that closing `poordooropen` produces `poordoor`. LostCity records the pairing
     * as a param and so does this, which is what lets the engine's door handler be
     * one generic rule.
     */
    { "loc", "next_loc_stage", CONTENT_SCOPE_SERVER, CONTENT_CLIENT_PARAM, "next_loc_stage" },

    /* As above: stated by the client's own record, so an overlay repeating one is
     * patching the cache. */
    { "loc", "name",           CONTENT_SCOPE_CLIENT, CONTENT_CLIENT_NATIVE, ""               },
    { "loc", "desc",           CONTENT_SCOPE_CLIENT, CONTENT_CLIENT_NATIVE, ""               },
};

static void
push(
    struct ContentFields* fields,
    const struct FieldDefault* row)
{
    struct ContentField* entry;

    if( fields->count >= CONTENT_FIELDS_MAX )
        return;
    entry = &fields->entries[fields->count++];
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->name, sizeof(entry->name), "%s", row->name);
    entry->scope = row->scope;
    entry->client = row->client;
    snprintf(entry->param_name, sizeof(entry->param_name), "%s", row->param_name);
}

int
ContentFields_Defaults(
    struct ContentFields* fields,
    const char* type)
{
    memset(fields, 0, sizeof(*fields));
    for( size_t i = 0; i < sizeof(k_defaults) / sizeof(k_defaults[0]); i++ )
    {
        if( strcmp(k_defaults[i].type, type) == 0 )
            push(fields, &k_defaults[i]);
    }
    return fields->count;
}

const struct ContentField*
ContentFields_Find(
    const struct ContentFields* fields,
    const char* name)
{
    if( !name )
        return NULL;
    for( int i = 0; i < fields->count; i++ )
    {
        if( strcmp(fields->entries[i].name, name) == 0 )
            return &fields->entries[i];
    }
    return NULL;
}

static enum ContentFieldScope
parse_scope(
    const char* value,
    enum ContentFieldScope fallback)
{
    if( strcmp(value, "server") == 0 )
        return CONTENT_SCOPE_SERVER;
    if( strcmp(value, "client") == 0 )
        return CONTENT_SCOPE_CLIENT;
    return fallback;
}

/** `native` | `drop` | `error` | `param:<name>`. */
static enum ContentFieldClient
parse_client(
    const char* value,
    char* out_param,
    size_t out_param_size,
    enum ContentFieldClient fallback)
{
    if( strcmp(value, "native") == 0 )
        return CONTENT_CLIENT_NATIVE;
    if( strcmp(value, "drop") == 0 )
        return CONTENT_CLIENT_DROP;
    if( strcmp(value, "error") == 0 )
        return CONTENT_CLIENT_ERROR;
    if( strncmp(value, "param:", 6) == 0 && value[6] )
    {
        snprintf(out_param, out_param_size, "%s", value + 6);
        return CONTENT_CLIENT_PARAM;
    }
    return fallback;
}

int
ContentFields_Load(
    struct ContentFields* fields,
    const char* dir,
    const char* type)
{
    char path[1024];
    char prefix[64];
    FILE* file;
    long size;
    uint8_t* data;
    struct INIReader reader;
    struct INIElement element;
    struct ContentField* current = NULL;

    /* Defaults first, then the file *overlays* them — the same rule the namespace
     * register follows, and for the same reason: a tree that declares only the one
     * field it invented still gets the twelve that already worked. */
    ContentFields_Defaults(fields, type);

    snprintf(path, sizeof(path), "%s/fields/%s.ini", dir, type);
    file = fopen(path, "rb");
    if( !file )
        return fields->count;

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if( size <= 0 )
    {
        fclose(file);
        return fields->count;
    }
    data = (uint8_t*)malloc((size_t)size);
    if( !data )
    {
        fclose(file);
        return fields->count;
    }
    if( fread(data, 1, (size_t)size, file) != (size_t)size )
    {
        free(data);
        fclose(file);
        return fields->count;
    }
    fclose(file);

    /* `[npc.hitpoints]`, so one file could describe several types without the
     * sections colliding. */
    snprintf(prefix, sizeof(prefix), "%s.", type);

    ini_reader_init(&reader);
    while( ini_reader_next(&reader, data, (uint32_t)size, &element) == TORI_INI_ERR_OK )
    {
        if( element.kind == INI_ELEMENT_SECTION )
        {
            const char* name = element._section.name;

            ContentIni_Trim(element._section.name);
            current = NULL;
            if( strncmp(name, prefix, strlen(prefix)) != 0 )
                continue;
            name += strlen(prefix);

            for( int i = 0; i < fields->count; i++ )
            {
                if( strcmp(fields->entries[i].name, name) == 0 )
                {
                    current = &fields->entries[i];
                    break;
                }
            }
            if( !current && fields->count < CONTENT_FIELDS_MAX )
            {
                current = &fields->entries[fields->count++];
                memset(current, 0, sizeof(*current));
                snprintf(current->name, sizeof(current->name), "%s", name);
            }
            continue;
        }

        if( element.kind != INI_ELEMENT_KEYVAL || !current )
            continue;

        ContentIni_Trim(element._keyval.name);
        ContentIni_Trim(element._keyval.value);

        if( strcmp(element._keyval.name, "scope") == 0 )
            current->scope = parse_scope(element._keyval.value, current->scope);
        else if( strcmp(element._keyval.name, "client") == 0 )
            current->client = parse_client(element._keyval.value, current->param_name,
                                           sizeof(current->param_name), current->client);
    }

    free(data);
    fields->from_file = 1;
    return fields->count;
}
