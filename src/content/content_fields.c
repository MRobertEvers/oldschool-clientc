/*
 * The server's view of the field register. See content_fields.h.
 */

#include "content_fields.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The defaults, transcribed rather than designed, and written as register text so
 * they go through the same parser as a tree's own file.
 *
 * Two tables already held most of the answer, which is what made this tractable
 * (docs/CONTENT_PACK_PLAN.md §5.2):
 *
 *   the server column   `torirs_server_content.c`'s npc key ladder — hitpoints, attack,
 *                       strength, defence, magic, ranged, respawnrate, wanderrange,
 *                       moverestrict, huntmode
 *   the `param:` column `torirs_server_pack.c`'s `BakedParam` table, verbatim
 *
 * The fields the client's own record carries *are* listed, as
 * `scope = client, client = native`, and that is not a second copy of `cp_npc.c`'s
 * emitter — it is what lets the config parser stop hardcoding them. LostCity authors
 * `name=` and `op1=` because it *builds* the npc record; ours comes from the cache,
 * so those keys are inert here. They used to be a `k_from_cache[]` array in
 * `torirs_server_content.c` that the parser checked before rejecting a key, which meant
 * "the client already states this" lived in C where a content author could not see
 * it or add to it.
 *
 * Only the ones a config might plausibly carry are listed. The full 43 keys
 * `cp_npc.c` emits are the encoder's business; a field nobody has written in a
 * config has nothing to declare.
 *
 * **No default carries `server = opcode:...`.** The band is the tree's to declare
 * (its numbers are ours and live in data, `fields/<type>.ini`), and a default opcode
 * would be a band field the server reads that cachepack, reading the file alone,
 * never writes.
 */

static const char k_npc_defaults[] =
    /*
     * npc combat, as `ToriRSServer_Pack --cache-out` has always baked it.
     *
     * The param names are the server's own (`pack/param.pack` 2634 and up) except
     * `attackrate`, which is param 14 — a param the *cache* defines, so an npc's
     * attack rate lands where a client reading the cache alone would look for it.
     */
    "[npc.hitpoints]\n"   "scope = server\n" "client = param:hitpoints\n"
    "[npc.attack]\n"      "scope = server\n" "client = param:attacklevel\n"
    "[npc.strength]\n"    "scope = server\n" "client = param:strengthlevel\n"
    "[npc.defence]\n"     "scope = server\n" "client = param:defencelevel\n"
    "[npc.respawnrate]\n" "scope = server\n" "client = param:respawnrate\n"
    "[npc.wanderrange]\n" "scope = server\n" "client = param:wanderrange\n"
    "[npc.huntrange]\n"   "scope = server\n" "client = param:huntrange\n"
    "[npc.attackrate]\n"  "scope = server\n" "client = param:attackrate\n"
    "[npc.death_drop]\n"  "scope = server\n" "client = param:death_drop\n"
    "[npc.attack_anim]\n" "scope = server\n" "client = param:attack_anim\n"
    "[npc.defend_anim]\n" "scope = server\n" "client = param:defend_anim\n"
    "[npc.death_anim]\n"  "scope = server\n" "client = param:death_anim\n"
    /*
     * Authored, and deliberately not projected.
     *
     * `magic` and `ranged` have no param the cache defines and no server param has
     * been allocated for them, so they are `drop` rather than silently baked into a
     * number nobody agreed on. `huntmode` and `nomove` are behaviour the server
     * decides and the client never asks about.
     */
    "[npc.magic]\n"       "scope = server\n" "client = drop\n"
    "[npc.ranged]\n"      "scope = server\n" "client = drop\n"
    "[npc.huntmode]\n"    "scope = server\n" "client = drop\n"
    "[npc.nomove]\n"      "scope = server\n" "client = drop\n"
    /*
     * Stated by the client's own record, so an overlay that repeats one is patching
     * the cache rather than adding to it — which the loader says out loud instead
     * of silently accepting.
     */
    "[npc.name]\n"        "scope = client\n" "client = native\n"
    "[npc.desc]\n"        "scope = client\n" "client = native\n"
    "[npc.vislevel]\n"    "scope = client\n" "client = native\n"
    "[npc.size]\n"        "scope = client\n" "client = native\n"
    "[npc.category]\n"    "scope = client\n" "client = native\n"
    "[npc.walkanim]\n"    "scope = client\n" "client = native\n"
    "[npc.readyanim]\n"   "scope = client\n" "client = native\n"
    "[npc.op1]\n"         "scope = client\n" "client = native\n"
    "[npc.op2]\n"         "scope = client\n" "client = native\n"
    "[npc.op3]\n"         "scope = client\n" "client = native\n"
    "[npc.op4]\n"         "scope = client\n" "client = native\n"
    "[npc.op5]\n"         "scope = client\n" "client = native\n";

static const char k_loc_defaults[] =
    /*
     * A door's other half.
     *
     * The cache states which locs exist and what they look like; nothing in it says
     * that closing `poordooropen` produces `poordoor`. LostCity records the pairing
     * as a param and so does this, which is what lets the engine's door handler be
     * one generic rule.
     */
    "[loc.next_loc_stage]\n" "scope = server\n" "client = param:next_loc_stage\n" "ref = loc\n"
    /* As above: stated by the client's own record, so an overlay repeating one is
     * patching the cache. */
    "[loc.name]\n"        "scope = client\n" "client = native\n"
    "[loc.desc]\n"        "scope = client\n" "client = native\n"
    "[loc.op1]\n"         "scope = client\n" "client = native\n"
    "[loc.op2]\n"         "scope = client\n" "client = native\n"
    "[loc.op3]\n"         "scope = client\n" "client = native\n"
    "[loc.op4]\n"         "scope = client\n" "client = native\n"
    "[loc.op5]\n"         "scope = client\n" "client = native\n";

const char*
ContentFields_DefaultsText(const char* type)
{
    assert(type);
    if( strcmp(type, "npc") == 0 )
        return k_npc_defaults;
    if( strcmp(type, "loc") == 0 )
        return k_loc_defaults;
    return "";
}

int
ContentFields_Defaults(
    struct RSCache_Register* fields,
    const char* type)
{
    const char* text;

    assert(fields);
    assert(type);
    text = ContentFields_DefaultsText(type);
    return RSCache_RegisterParse(fields, type, text, strlen(text));
}

int
ContentFields_Load(
    struct RSCache_Register* fields,
    const char* dir,
    const char* type)
{
    const char* defaults;
    size_t defaults_size;
    char path[1024];
    FILE* file;
    long size;
    char* text;
    size_t text_size;

    assert(fields);
    assert(dir);
    assert(type);

    snprintf(path, sizeof(path), "%s/fields/%s.ini", dir, type);
    file = fopen(path, "rb");
    if( !file )
        return ContentFields_Defaults(fields, type);

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if( size <= 0 )
    {
        fclose(file);
        return ContentFields_Defaults(fields, type);
    }

    /* The defaults, a line break, then the file: one text, so the grammar's own
     * section merge is the overlay (content_fields.h). */
    defaults = ContentFields_DefaultsText(type);
    defaults_size = strlen(defaults);
    text = malloc(defaults_size + 1 + (size_t)size);
    assert(text);
    memcpy(text, defaults, defaults_size);
    text[defaults_size] = '\n';
    text_size = defaults_size + 1;
    if( fread(text + text_size, 1, (size_t)size, file) != (size_t)size )
    {
        fprintf(stderr, "%s: short read\n", path);
        fclose(file);
        free(text);
        ContentFields_Defaults(fields, type);
        fields->rejected++;
        return fields->count;
    }
    fclose(file);
    text_size += (size_t)size;

    RSCache_RegisterParse(fields, type, text, text_size);
    fields->from_file = 1;
    free(text);
    return fields->count;
}
