/*
 * mock230_pack — check the content tree against the cache. A validator, only:
 *
 *   make -C src mock230-pack
 *   src/build/mock230_pack --check-only
 *
 * Why this exists: every id in the tree came from OpenRune's gameval table,
 * whose cache is revision 235.10, and the mock runs against 230. An id that
 * moved between the two does not fail loudly — it resolves to a *different*
 * npc, which spawns and fights and looks entirely plausible. `npcs.goblin` is
 * the worked example: id 3028 at both revisions, while the mock's old roster
 * used 655, which cache.osrs230 also calls "Goblin" and OpenRune calls
 * `goblin_red_soldier_2`. Two monsters, one display name, and nothing but a
 * check to tell them apart.
 *
 * So the rule is: the importers state, this validates. A non-zero exit means
 * the tree and the cache disagree.
 *
 * This tool used to also *write* — `--cache-out` baked authored fields into
 * each record's param table in a copied cache. That was the second baker
 * CONTENT_PACK_PLAN.md §5.4 retires: `cachepack pack` now emits the cache
 * projection and the server band from one merged record, driven by
 * `fields/<type>.ini`, so a baker here could only disagree with it. Deleted,
 * not moved; the validator role is the whole tool now.
 */

#include "content/content_register.h"
#include "cp_membership.h"
#include "mock230.h"
#include "mock230_content.h"
#include "mock230_db.h"
#include "mock230_ids.h"

#include <rscache.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Reporting                                                           */
/* ------------------------------------------------------------------ */

static int g_errors;
static int g_warnings;
static int g_verbose;

static void
report_error(const char* fmt, ...)
{
    va_list args;

    fprintf(stderr, "  ERROR ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    g_errors++;
}

static void
report_warning(const char* fmt, ...)
{
    va_list args;

    fprintf(stderr, "  warn  ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    g_warnings++;
}

static void
report_info(const char* fmt, ...)
{
    va_list args;

    if( !g_verbose )
        return;
    fprintf(stderr, "        ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

/* ------------------------------------------------------------------ */
/* Validation                                                          */
/* ------------------------------------------------------------------ */

/*
 * Is this npc attackable?
 *
 * Duplicated from mock230_combat.c rather than linked: pulling that in would
 * drag the whole server — world, scripts, encoders, the socket — into a tool
 * that only reads files. Three lines against a stable cache field is the
 * cheaper coupling, and the selftest asserts the engine's copy separately.
 */
static int
npc_attackable(int npc_type)
{
    const struct Mock230NpcInfo* info = mock230_npcinfo(npc_type);

    for( int i = 0; i < 5; i++ )
    {
        if( info->ops[i] && strcmp(info->ops[i], "Attack") == 0 )
            return 1;
    }
    return 0;
}

/** Does this loc's cache record carry an Open-family action? */
static int
loc_has_open_op(
    const struct RSCache_Dat2ConfigLoc* config,
    const char* verb)
{
    if( !config )
        return 0;
    for( int i = 0; i < 5; i++ )
    {
        if( config->actions[i] && strcmp(config->actions[i], verb) == 0 )
            return 1;
    }
    return 0;
}

/*
 * Every npc id the maps spawn, checked against both the cache record and the
 * content block.
 *
 * Driven from the spawn list rather than from the definition list because a
 * definition nothing spawns is inert, while a spawn with no definition is a
 * monster running on engine defaults — the second is worth a line of output and
 * the first is not.
 */
static void
validate_spawns(void)
{
    int npc_count = 0;
    int obj_count = 0;
    const struct Mock230MapNpcSpawn* npcs = mock230_content_npc_spawns(&npc_count);
    const struct Mock230MapObjSpawn* objs = mock230_content_obj_spawns(&obj_count);
    int checked[4096];
    int checked_count = 0;

    fprintf(stderr, "spawns: %d npc, %d obj\n", npc_count, obj_count);

    for( int i = 0; i < npc_count; i++ )
    {
        const struct Mock230NpcInfo* info = mock230_npcinfo(npcs[i].npc_id);
        const struct Mock230NpcDef* def = mock230_content_npc(npcs[i].npc_id);
        const char* symbol = mock230_content_symbol_name(MOCK230_PACK_NPC, npcs[i].npc_id);
        int already = 0;

        if( !info->name )
        {
            report_error("npc %d spawned at %d,%d is not in the cache", npcs[i].npc_id,
                         npcs[i].x, npcs[i].z);
            continue;
        }
        for( int j = 0; j < checked_count; j++ )
        {
            if( checked[j] == npcs[i].npc_id )
                already = 1;
        }
        if( already )
            continue;
        if( checked_count < (int)(sizeof(checked) / sizeof(checked[0])) )
            checked[checked_count++] = npcs[i].npc_id;

        if( !symbol )
            report_warning("npc %d (%s) has no symbol in pack/npc.pack", npcs[i].npc_id,
                           info->name);
        if( !def )
        {
            report_warning("npc %d (%s) has no config block — engine defaults apply",
                           npcs[i].npc_id, info->name);
            continue;
        }

        /*
         * The cache's combat level beside the authored hitpoints. Neither
         * derives from the other, so this is not an assertion — it is the one
         * line that makes a transposed block visible, because a level-2 goblin
         * with 75 hitpoints is obviously the huge spider's row.
         */
        report_info("%-18s id %-6d %-16s vislevel %-4d hp %-4d att/str/def %d/%d/%d",
                    symbol ? symbol : "?", npcs[i].npc_id, info->name, info->combat_level,
                    def->hitpoints, def->attack, def->strength, def->defence);

        if( info->combat_level > 0 && def->authored_combat &&
            def->hitpoints > info->combat_level * 8 )
            report_warning(
                "%s has %d hitpoints but the cache says combat level %d — "
                "check the block is not transposed",
                symbol ? symbol : info->name, def->hitpoints, info->combat_level);

        if( def->authored_combat && !npc_attackable(npcs[i].npc_id) )
            report_warning(
                "%s has a combat block but the cache gives it no Attack op, so "
                "nothing can fight it",
                symbol ? symbol : info->name);

        /*
         * A spawn whose name belongs to somewhere else.
         *
         * OldSchool's gameval table qualifies most npcs by the content that
         * introduced them, so a roster imported by id rather than by name can
         * end up standing `sos_pest_giantspider1` — Pest Control's level-50
         * one — in the Lumbridge Swamp beside the level-2 `giantspider1`, and
         * nothing says so. That is precisely what it was doing, four times over,
         * until spawns moved to a name-keyed file
         * (docs/LOSTCITY_PORT_TRIAGE.md §10.2).
         *
         * The signal is narrow on purpose. A prefix alone means nothing — plenty
         * of variants are legitimately used outside the area they are named for —
         * so this fires only when the *un-prefixed* creature is also spawned in
         * this world. Having both is what says one of them is probably a
         * mis-import. Once God Wars is a real area with its own `.spawn` file,
         * `godwars_goblin2` standing there alone is silent, which is correct.
         *
         * A warning, never an error: it is a prompt to check a second source
         * (LostCity's own roster covers the same ground), not a fact.
         */
        if( symbol )
        {
            static const char* const k_area_prefixes[] = {
                "sos_pest_", "sos_fam_", "poh_", "godwars_", "barrows_",
                "raids_", "nightmare_", "dragonslayer_", "slayer_",
            };

            for( size_t k = 0; k < sizeof(k_area_prefixes) / sizeof(k_area_prefixes[0]); k++ )
            {
                size_t length = strlen(k_area_prefixes[k]);
                const char* base;
                int base_id;

                if( strncmp(symbol, k_area_prefixes[k], length) != 0 )
                    continue;
                base = symbol + length;
                base_id = mock230_content_symbol(MOCK230_PACK_NPC, base);
                if( base_id < 0 )
                    break;
                for( int j = 0; j < npc_count; j++ )
                {
                    if( npcs[j].npc_id != base_id )
                        continue;
                    report_warning(
                        "%s is spawned here and so is `%s` — a name qualified by "
                        "another content area beside the plain one is usually an "
                        "id-imported roster; check both against a second source",
                        symbol, base);
                    break;
                }
                break;
            }
        }
    }

    for( int i = 0; i < obj_count; i++ )
    {
        if( !mock230_objinfo(objs[i].obj_id)->name )
            report_error("obj %d spawned at %d,%d is not in the cache", objs[i].obj_id,
                         objs[i].x, objs[i].z);
    }
}

/*
 * Equipment requirements.
 *
 * The table is merged from two sources that disagree — cache.osrs239's own
 * params 434/436 + 435/437, and `skill_combat/configs/equipment.obj` imported
 * from Kronos — so what this checks is not "is it there" but "is it *sane*",
 * which for a requirement means four things:
 *
 *   - the skill is one the wire has (0..22),
 *   - the level is 1..99,
 *   - the obj it gates is actually wearable, because a requirement on something
 *     you cannot equip can never fire and is therefore a mis-typed symbol,
 *   - and the ladder still climbs. A scimitar ladder that reads 5/10/20/30/40/60
 *     is the one line of this report worth reading at a glance; a source swapped
 *     for a worse one shows up as a number out of order rather than as silence.
 *
 * The numbers were transcribed from a Kronos dump once and are authored now;
 * docs/mock230_content.md §5 for why neither source is trusted alone.
 */
static void
validate_requirements(void)
{
    /* The ladders. Every one of these is an OldSchool value nobody should be
     * able to change by accident, and they span both sources: steel through
     * adamant come from the overlay, rune and dragon from the cache. */
    static const struct
    {
        const char* symbol;
        int stat;
        int level;
    } k_expect[] = {
        { "bronze_scimitar", -1, 0 },  /* no requirement at all */
        { "iron_scimitar", -1, 0 },
        { "steel_scimitar", MOCK230_STAT_ATTACK, 5 },
        { "black_scimitar", MOCK230_STAT_ATTACK, 10 },
        { "mithril_scimitar", MOCK230_STAT_ATTACK, 20 },
        { "adamant_scimitar", MOCK230_STAT_ATTACK, 30 },
        { "rune_scimitar", MOCK230_STAT_ATTACK, 40 },
        { "dragon_scimitar", MOCK230_STAT_ATTACK, 60 },
        { "rune_platebody", MOCK230_STAT_DEFENCE, 40 },
        { "abyssal_whip", MOCK230_STAT_ATTACK, 70 },
        { "magic_shortbow", MOCK230_STAT_RANGED, 50 },
    };
    int total = 0;
    int from_cache = 0;
    int checked = 0;

    mock230_obj_require_counts(&total, &from_cache);
    fprintf(stderr, "equipment requirements\n");

    for( int obj_id = 0; obj_id < 40000; obj_id++ )
    {
        const struct Mock230ObjRequire* require = mock230_obj_require(obj_id);
        const struct Mock230ObjInfo* info;

        if( !require )
            continue;
        checked++;
        info = mock230_objinfo(obj_id);
        if( info->wearpos < 0 )
            report_error("obj %d (%s) has an equip requirement but cannot be worn",
                         obj_id, info->name ? info->name : "?");
        for( int i = 0; i < require->count; i++ )
        {
            if( require->req[i].stat < 0 || require->req[i].stat >= MOCK230_STAT_COUNT )
                report_error("obj %d (%s) requires skill %d, which is not a skill", obj_id,
                             info->name ? info->name : "?", require->req[i].stat);
            if( require->req[i].level < 1 || require->req[i].level > 99 )
                report_error("obj %d (%s) requires level %d", obj_id,
                             info->name ? info->name : "?", require->req[i].level);
        }
    }

    for( size_t i = 0; i < sizeof(k_expect) / sizeof(k_expect[0]); i++ )
    {
        int obj_id = mock230_content_symbol(MOCK230_PACK_OBJ, k_expect[i].symbol);
        const struct Mock230ObjRequire* require;
        int found = 0;

        if( obj_id < 0 )
        {
            report_error("%s is not in pack/obj.pack", k_expect[i].symbol);
            continue;
        }
        require = mock230_obj_require(obj_id);
        if( k_expect[i].stat < 0 )
        {
            if( require )
                report_error("%s should have no requirement, has %d", k_expect[i].symbol,
                             require->count);
            continue;
        }
        if( !require )
        {
            report_error("%s should require %d in skill %d, has no requirement",
                         k_expect[i].symbol, k_expect[i].level, k_expect[i].stat);
            continue;
        }
        for( int r = 0; r < require->count; r++ )
        {
            if( require->req[r].stat != k_expect[i].stat )
                continue;
            found = 1;
            if( require->req[r].level != k_expect[i].level )
                report_error("%s requires level %d in skill %d, expected %d",
                             k_expect[i].symbol, require->req[r].level, k_expect[i].stat,
                             k_expect[i].level);
        }
        if( !found )
            report_error("%s does not require skill %d at all", k_expect[i].symbol,
                         k_expect[i].stat);
    }

    report_info("%d objs carry a requirement — %d out of the cache's own params, %d "
                "added or corrected by the .obj overlay",
                total, from_cache, total - from_cache);
    fprintf(stderr, "        %d requirement rows checked, %zu ladder values pinned\n", checked,
            sizeof(k_expect) / sizeof(k_expect[0]));
}

/*
 * Every name in `pack/category.pack` names something.
 *
 * `category` is the tree's one *derived* namespace: LostCity never authors it,
 * it regenerates the table by crawling the `category=` key out of every
 * `.npc`/`.loc`/`.obj` (CONTENT_ARCHITECTURE.md §2.1, and `CategoryType.ts` says
 * so in a sentence). Here most ids are the cache's — npc config opcode 18, obj
 * opcode 94, loc opcode 61 — so the crawl only attaches a name, and every line
 * below the base is therefore a *claim* that this cache groups something under
 * that id.
 *
 * A claim nothing carries is the failure this rule is for, and it is invisible
 * everywhere else. A category is a trigger *subject*: `[ai_queue3,_bank_teller]`
 * with `bank_teller` naming an id no record holds compiles, loads, resolves,
 * reports nothing at any verbosity and simply never fires. There is no wrong
 * behaviour to notice — a whole drop table just does not exist. The same line
 * with the name misspelled fails loudly at compile time, so the *typo* is the
 * safe case and the plausible-but-empty id is not.
 *
 * Deliberately not an obj-only or npc-only check: the three domains share one id
 * space (21 ids in this cache are carried by both obj and npc records, 9 by both
 * loc and npc, 3 by both loc and obj), so "carried by nothing at all" is the only
 * form of the question that has a stable answer. The report prints the split,
 * because which side carries a name is what the next person needs and re-deriving
 * it costs a scan.
 *
 * **The loc arm is new (2026-08-02) and it is not merely completeness.** A loc's
 * category was unreadable in this tree until then — the linked decoder consumed
 * config opcode 61 and dropped the value — so a name that only loc records carry
 * would have been reported as carried by nothing and the check would have been
 * *wrong*, not incomplete. It is 8,407 records across 712 ids.
 *
 * ## Two id authorities, two rules
 *
 * `content.ini` gives `category` a `server_base` (8192). That splits the file in
 * two and the rule differs above and below it:
 *
 *   below the base   the id is the CACHE's, so the name is a claim about a cache
 *                    record and the evidence is a record carrying it;
 *   at or above      the id is OURS, so no cache record can carry it by
 *                    construction, and the evidence is an authored config block
 *                    *stating* it.
 *
 * Both rules catch the same failure and it is the one that hides: a category is a
 * trigger *subject*, so `[oploc1,_door_closed]` where `door_closed` names an id
 * nothing carries compiles, loads, resolves, reports nothing at any verbosity and
 * simply never fires. There is no wrong behaviour to notice — a whole door table
 * just does not exist. The same line with the name misspelled fails loudly at
 * compile time, so the *typo* is the safe case and the plausible-but-empty id is
 * not.
 *
 * Note what the second rule does NOT do: it does not require the id to be above
 * the base *because* the members are authored. `mock230_loc_category_members`
 * counts the overlay and the cache together, so an authored `.loc` block that
 * restates a cache id is counted once and lands in the first rule, correctly.
 */
static void
validate_categories(const char* content_dir)
{
    int total = mock230_content_symbol_walk(MOCK230_PACK_CATEGORY, -1, NULL, NULL);
    struct ContentRegister reg;
    const struct ContentNamespace* row;
    int server_base;
    int obj_named = 0;
    int npc_named = 0;
    int loc_named = 0;
    int shared = 0;
    int allocated = 0;

    ContentRegister_Load(&reg, content_dir);
    row = ContentRegister_Find(&reg, "category");
    server_base = row ? row->server_base : 0;

    fprintf(stderr, "categories\n");
    for( int i = 0; i < total; i++ )
    {
        int id = -1;
        const char* name = NULL;
        int objs;
        int npcs;
        int locs;
        int domains;

        if( !mock230_content_symbol_walk(MOCK230_PACK_CATEGORY, i, &id, &name) || !name )
            continue;
        /* Zero is the decoder's "unstated" on all three record types and
         * `content.ini` reserves it (`zero = reserved`): a trigger bound to it
         * would match every uncategorised record in the cache. */
        if( id <= 0 )
        {
            report_error("category `%s` is id %d — 0 is the decoder's \"unstated\" "
                         "and must never be a name",
                         name, id);
            continue;
        }
        objs = mock230_obj_category_members(id);
        npcs = mock230_npc_category_members(id);
        locs = mock230_loc_category_members(id);
        if( objs == 0 && npcs == 0 && locs == 0 )
        {
            if( server_base > 0 && id >= server_base )
                report_error("category %d (%s) is at or above the allocation base %d, so "
                             "no cache record can carry it, and no authored .npc/.obj/.loc "
                             "block states it either — a trigger bound to it can never fire",
                             id, name, server_base);
            else
                report_error("category %d (%s) is carried by no obj, npc or loc record — "
                             "a trigger bound to it can never fire, and nothing else "
                             "in this tree can tell you that",
                             id, name);
            continue;
        }
        if( server_base > 0 && id >= server_base )
            allocated++;
        domains = (objs > 0) + (npcs > 0) + (locs > 0);
        if( domains > 1 )
            shared++;
        else if( objs )
            obj_named++;
        else if( npcs )
            npc_named++;
        else
            loc_named++;
        if( g_verbose )
            report_info("%-22s id %-5d %d obj(s), %d npc(s), %d loc(s)%s", name, id, objs,
                        npcs, locs,
                        (server_base > 0 && id >= server_base) ? "  [allocated]" : "");
    }
    fprintf(stderr,
            "        %d category name(s): %d obj-only, %d npc-only, %d loc-only, %d shared; "
            "%d allocated above %d\n",
            total, obj_named, npc_named, loc_named, shared, allocated, server_base);
}

/*
 * Loc ids the door validator rejected, so --prune-doors can rewrite the config
 * without them. Collected rather than acted on immediately because a pair is
 * two lines in the file and both halves have to go.
 */
static int g_rejected[2048];
static int g_rejected_count;

static void
reject_door(int loc_id)
{
    if( g_rejected_count < (int)(sizeof(g_rejected) / sizeof(g_rejected[0])) )
        g_rejected[g_rejected_count++] = loc_id;
}

static int
door_rejected(int loc_id)
{
    for( int i = 0; i < g_rejected_count; i++ )
    {
        if( g_rejected[i] == loc_id )
            return 1;
    }
    return 0;
}

/*
 * Rewrite doors.loc without the pairs the cache rejected.
 *
 * This is the step that turns the door pairings' guesswork into data. They were
 * derived from naming conventions — the only way past OpenRune's 13 curated ones —
 * and about one in seven of those turns out to be
 * scenery that merely reads like a door (`wooden_fur_door_always_closed`,
 * `lassar_door_closed_noop`, a dozen Colosseum gates). Deriving broadly and
 * then deleting whatever the cache disagrees with is more honest than either
 * trusting the names or hand-curating four hundred pairs.
 *
 * Both halves of a rejected pair are removed: half a door is worse than none.
 */
static int
prune_doors(const char* content)
{
    char path[1024];
    char temp[1100];
    FILE* in;
    FILE* out;
    char raw[1024];
    int dropped = 0;
    int skipping = 0;

    snprintf(path, sizeof(path), "%s/server/scripts/doors/configs/doors.loc", content);
    snprintf(temp, sizeof(temp), "%s.new", path);
    in = fopen(path, "rb");
    if( !in )
    {
        fprintf(stderr, "mock230_pack: no %s to prune\n", path);
        return 0;
    }
    out = fopen(temp, "wb");
    if( !out )
    {
        fclose(in);
        return 0;
    }

    while( fgets(raw, sizeof(raw), in) )
    {
        if( raw[0] == '[' )
        {
            char symbol[256];
            char* end;
            int loc_id;
            const struct Mock230LocDef* def;

            snprintf(symbol, sizeof(symbol), "%s", raw + 1);
            end = strchr(symbol, ']');
            if( end )
                *end = '\0';
            loc_id = mock230_content_symbol(MOCK230_PACK_LOC, symbol);
            def = loc_id >= 0 ? mock230_content_loc(loc_id) : NULL;

            /* A section is dropped when either half of its pair was rejected. */
            skipping = def && (door_rejected(def->loc_id) ||
                               door_rejected(def->next_loc_stage));
            if( skipping )
                dropped++;
        }
        else if( raw[0] == '\n' || raw[0] == '\r' )
        {
            if( skipping )
            {
                skipping = 0;
                continue;
            }
        }
        if( !skipping )
            fputs(raw, out);
    }
    fclose(in);
    fclose(out);
    if( rename(temp, path) != 0 )
    {
        fprintf(stderr, "mock230_pack: cannot replace %s\n", path);
        return 0;
    }
    fprintf(stderr, "        pruned %d loc sections from doors.loc\n", dropped);
    return 1;
}

/*
 * The `names/pins.ini` symbol closure used to live here, and it is gone.
 *
 * It existed to make absorbing a *new cache* reviewable: pin every symbol the
 * server names to the id it resolved to, so `cachepack unpack --compare` could
 * report the six changes that land on something content refers to rather than
 * the 1,877 that do not. Real value, for a problem this tree no longer has —
 * the client cache is pinned to one revision (docs/CONTENT_PACK_PLAN.md §0,
 * decision 1), and a future release is absorbed by exporting it into a second
 * content tree and running `diff -r`, which is ordinary reviewable text and a
 * better artifact than a fingerprint file.
 *
 * Listed here rather than merely deleted so nobody re-adds it by reflex: what
 * would justify it is the cache stopping being frozen, and nothing else.
 */

/*
 * Every declared allocation base has to clear the cache's own high-water mark.
 *
 * The param block used to start at 2000 with the note "above every real param id
 * so they cannot collide with one". cache.osrs239's param group holds 2,634
 * records covering 0..2633 with no gaps, so all fifteen of them named a param the
 * cache already defines — `hitpoints` sat on 2100, which is a real record, and
 * `mock230_pack --cache-out` wrote over it.
 *
 * The check is not "are they above 2000", it is "are they above whatever this
 * cache actually goes up to", which is the only version that survives a bump. And
 * it is not about params: the register now declares a base for every namespace
 * (`server_base`, docs/CONTENT_PACK_PLAN.md §4.2), so the same question applies to
 * all of them and the same answer is checkable for all of them. A base that is a
 * *guess* is what went wrong; a base that is checked against the cache in front of
 * it is a fact.
 *
 * Two directions, both reported, and the second one is an *error* for a namespace
 * whose ids are ours:
 *
 *   base vs cache    a declared base at or below the cache's largest id would
 *                    hand out ids the client already uses.
 *   ids vs base      an id in the pack file above the cache's maximum but below
 *                    the declared base. Nothing hands it out twice — the
 *                    allocator takes `max(base, highest + 1)` on both sides — but
 *                    for an `ids = server` namespace it means the register is
 *                    describing an id space that is not the one in use, and the
 *                    *next* allocation jumps over the gap into the declared band.
 *
 * That second case is not hypothetical and it is why it stopped being a silent
 * `report_info`. `varp` declared 8000 while nineteen server varps sat at
 * 5705..5723, and `MOCK230_VARP_COUNT` is 6217 — so the first varp allocated at
 * the declared floor would have been past the end of `Mock230Player.varps` and
 * `mock230_world_set_varp` would have dropped the write and returned, which is
 * docs/CONTENT_ARCHITECTURE.md §8.3's named failure mode. It stayed invisible for
 * as long as `tools/ss_allocate.py` read only `content.ini` for its floors and
 * `content.ini` declares no `base =` key anywhere: every number in this table was
 * advisory, and an advisory number cannot disagree with anything.
 *
 * A namespace the *cache* numbers is left as information. `obj` has twelve ids
 * between the cache's 33834 and its base of 40000, and all twelve are `obj_<id>`
 * placeholders from layer 0 rather than allocations — for `ids = cache` the base
 * is where *new* records would start, not a claim about what is below it.
 */
/* The config group each namespace's records live in. Asset tables are left out
 * — they are addressed by reference table rather than by config group, and
 * cachepack is what walks those.
 *
 * File scope because two validators need it: the allocation bases below, and the
 * membership id check after it, which asks the cache the same "does this group
 * hold this id" question. A second copy is how a namespace ends up checked by one
 * and not the other. */
static const struct
{
    enum Mock230PackKind kind;
    int config_kind;
} k_config_groups[] = {
    { MOCK230_PACK_NPC,      RSCACHE_DAT2_CONFIG_KIND_NPC       },
    { MOCK230_PACK_OBJ,      RSCACHE_DAT2_CONFIG_KIND_OBJECT    },
    { MOCK230_PACK_LOC,      RSCACHE_DAT2_CONFIG_KIND_LOCS      },
    { MOCK230_PACK_SEQ,      RSCACHE_DAT2_CONFIG_KIND_SEQUENCE  },
    { MOCK230_PACK_SPOTANIM, RSCACHE_DAT2_CONFIG_KIND_SPOTANIM  },
    { MOCK230_PACK_INV,      RSCACHE_DAT2_CONFIG_KIND_INV       },
    { MOCK230_PACK_VARP,     RSCACHE_DAT2_CONFIG_KIND_VARPLAYER },
    { MOCK230_PACK_VARBIT,   RSCACHE_DAT2_CONFIG_KIND_VARBIT    },
    { MOCK230_PACK_PARAM,    RSCACHE_DAT2_CONFIG_KIND_PARAMS    },
    { MOCK230_PACK_STRUCT,   RSCACHE_DAT2_CONFIG_KIND_STRUCT    },
    { MOCK230_PACK_ENUM,     RSCACHE_DAT2_CONFIG_KIND_ENUM      },
    { MOCK230_PACK_HITSPLAT, RSCACHE_DAT2_CONFIG_KIND_HITSPLAT  },
    { MOCK230_PACK_DBTABLE,  RSCACHE_DAT2_CONFIG_KIND_DBTABLE   },
};

static void
validate_id_bases(
    struct RSCache_Dat2Disk* disk,
    const char* content_dir)
{
    int table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);
    struct ContentRegister reg;
    int checked = 0;
    int problems = 0;

    /*
     * `Load`, not `Defaults`. This read the built-in table and never opened the
     * tree's `content.ini`, so every overlay the tree states was invisible here:
     * `varp`, `param`, `dbtable` and `dbrow` are all promoted to `ids = server` by
     * the file (each with a paragraph saying why), and this check was reading them
     * as `cache` and skipping the half of the rule that only applies to ids we
     * allocate. A validator that reads a different register than the runtime is
     * checking a tree that does not exist.
     */
    ContentRegister_Load(&reg, content_dir);

    for( size_t g = 0; g < sizeof(k_config_groups) / sizeof(k_config_groups[0]); g++ )
    {
        const char* ns = mock230_content_pack_name(k_config_groups[g].kind);
        const struct ContentNamespace* row = ContentRegister_Find(&reg, ns);
        struct RSCache_Dat2DiskArchive* archive;
        int cache_max = -1;

        if( !row || row->server_base == 0 )
            continue; /* nothing to allocate here — see `server_base`'s docs */

        archive = RSCache_Dat2DiskArchiveNewLoad(disk, table, k_config_groups[g].config_kind);
        if( !archive || !RSCache_Dat2DiskArchiveInitMetadata(disk, archive) )
        {
            report_warning("no %s config archive — its allocation base is unchecked", ns);
            if( archive )
                RSCache_Dat2DiskArchiveFree(archive);
            continue;
        }
        for( int i = 0; i < archive->file_count; i++ )
        {
            int id = archive->file_ids ? archive->file_ids[i] : i;
            if( id > cache_max )
                cache_max = id;
        }
        RSCache_Dat2DiskArchiveFree(archive);
        checked++;

        if( row->server_base <= cache_max )
        {
            report_error("%s: the allocation base is %d, but the cache defines ids up to "
                         "%d — raise the base in content_register.c",
                         ns, row->server_base, cache_max);
            problems++;
            continue;
        }

        /*
         * Ids above the cache's maximum but below the declared base.
         *
         * This used to be an error where `ids = server` and information where
         * `ids = cache`. That split is gone with the axis: `ids` turned out to
         * distinguish nothing — the pack file assigns ids, and `npc` declared
         * `ids = cache` while allocating from a base of 20000 — so the two
         * branches were reporting the same fact at two severities on the strength
         * of a label that did not mean what it said.
         *
         * Information, then, and honestly so. Restoring the error needs the one
         * thing neither branch ever had: whether an id in this gap is an
         * *allocation* of ours or a layer-0 name. The register cannot tell them
         * apart, which is precisely why the old code used `ids` as a proxy for it.
         * A base that is a round number above the cache's maximum leaves this gap
         * BY DESIGN (see `server_base`'s own comment), so a hit here is expected
         * rather than suspicious.
         */
        int early = 0;
        int lowest = -1;
        for( int id = cache_max + 1; id < row->server_base; id++ )
        {
            if( !mock230_content_symbol_name(k_config_groups[g].kind, id) )
                continue;
            if( lowest < 0 )
                lowest = id;
            early++;
        }
        if( early )
        {
            report_info("%s: %d id(s) sit between the cache's %d and the base %d (lowest %d) "
                        "— layer-0 names rather than allocations; the base is only where new "
                        "records start",
                        ns, early, cache_max, row->server_base, lowest);
        }
    }

    fprintf(stderr, "id bases\n        %d namespace(s) checked against the cache%s\n", checked,
            problems ? ", WITH COLLISIONS" : "");
}

/* ------------------------------------------------------------------ */
/* Membership vs the id range                                          */
/* ------------------------------------------------------------------ */

/*
 * The second of docs/PACK_ENTITY_SPLIT_PLAN.md §3.3's two agreement checks:
 * `pack/<ns>.client` and `pack/<ns>.server` held against the namespace's
 * allocation base and against the cache itself.
 *
 * It runs here and not in cachepack because this is where the facts are.
 * `server_base` is a field of `struct ContentNamespace`, defaulted in
 * `content/content_register.c` and overlayable from `content.ini`, and cachepack
 * deliberately links nothing from `src/` (`cp_register.h`) — so a base over there
 * would have to be a second copy of this number, which is the failure the whole
 * register exists to prevent. The provenance half of §3.3 runs in cachepack for
 * the mirror-image reason: it needs the two-rank merge, and that is the packer's.
 *
 * The format is not re-implemented either: `cp_membership.c` compiles into this
 * binary. One reader, two callers.
 *
 * ## What the base does and does not say
 *
 * `server_base` is **where a new name gets its number**, and that is all — §5 of
 * the plan retired the `ids = cache|server` axis precisely because it claimed to
 * say more. So an id at or above the base means "allocated by this tree", never
 * "belongs to the server", and the check is written to that meaning:
 *
 *   in `<ns>.server`, below the base    fine when the cache holds the id — that is
 *                                       §2's overlap, a cache record with a server
 *                                       half. An ERROR when it does not: a record
 *                                       nothing on the client defines, sitting on
 *                                       a cache-range id.
 *   in `<ns>.client`, below the base     the same question, and the same error when
 *                                       the cache does not hold it.
 *   in `<ns>.client`, at or above        counted. The tree allocated the id and
 *                                       tells the client about the record. Whether
 *                                       it *also* has a server half is what the
 *                                       file pair is for, and 44 params say it
 *                                       does not while their ids came out of the
 *                                       server allocator (§8.5 finding 2).
 *
 * A name that resolves to nothing is an error whatever its side: the format's
 * whole justification for holding names instead of ids is that a misspelling
 * fails the moment something looks it up (`cp_membership.h`), and this is that
 * lookup.
 */
static int
archive_holds_id(
    const struct RSCache_Dat2DiskArchive* archive,
    int id)
{
    if( !archive )
        return 0;
    if( !archive->file_ids )
        return id >= 0 && id < archive->file_count;
    for( int i = 0; i < archive->file_count; i++ )
    {
        if( archive->file_ids[i] == id )
            return 1;
    }
    return 0;
}

static void
validate_membership_ids(
    struct RSCache_Dat2Disk* disk,
    const char* content_dir)
{
    int table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);
    struct ContentRegister reg;
    int namespaces = 0;
    int names = 0;
    int overlap = 0;
    int allocated_client = 0;

    ContentRegister_Load(&reg, content_dir);

    for( size_t g = 0; g < sizeof(k_config_groups) / sizeof(k_config_groups[0]); g++ )
    {
        enum Mock230PackKind kind = k_config_groups[g].kind;
        const char* ns = mock230_content_pack_name(kind);
        const struct ContentNamespace* row = ContentRegister_Find(&reg, ns);
        struct CP_Membership side[CP_MEMBERSHIP_SIDES];
        struct RSCache_Dat2DiskArchive* archive = NULL;
        int loaded = 0;

        for( int s = 0; s < CP_MEMBERSHIP_SIDES; s++ )
        {
            char path[1200];

            cp_membership_path(path, sizeof(path), content_dir, ns, (enum CP_MembershipSide)s);
            if( !cp_membership_load(&side[s], path, ns, (enum CP_MembershipSide)s, 1) )
            {
                report_error("%s: pack/%s.%s exists and cannot be read", ns, ns,
                             cp_membership_side_name((enum CP_MembershipSide)s));
                for( int f = 0; f < s; f++ )
                    cp_membership_free(&side[f]);
                loaded = -1;
                break;
            }
            loaded++;
        }
        if( loaded < 0 )
            continue;
        if( side[CP_MEMBERSHIP_CLIENT].count == 0 && side[CP_MEMBERSHIP_SERVER].count == 0 )
        {
            /* No pair, which §8.3 says means every record here is the cache's.
             * Nothing to hold against an id range. */
            for( int s = 0; s < CP_MEMBERSHIP_SIDES; s++ )
                cp_membership_free(&side[s]);
            continue;
        }
        namespaces++;

        archive = RSCache_Dat2DiskArchiveNewLoad(disk, table, k_config_groups[g].config_kind);
        if( archive && !RSCache_Dat2DiskArchiveInitMetadata(disk, archive) )
        {
            RSCache_Dat2DiskArchiveFree(archive);
            archive = NULL;
        }
        if( !archive )
        {
            report_warning("no %s config archive — its membership ids are unchecked", ns);
            for( int s = 0; s < CP_MEMBERSHIP_SIDES; s++ )
                cp_membership_free(&side[s]);
            continue;
        }

        for( int s = 0; s < CP_MEMBERSHIP_SIDES; s++ )
        {
            const char* half = cp_membership_side_name((enum CP_MembershipSide)s);

            for( int i = 0; i < side[s].count; i++ )
            {
                const char* name = side[s].names[i];
                int id = -1;
                int in_cache;

                names++;
                if( !mock230_content_symbol_checked(kind, name, &id) || id < 0 )
                {
                    report_error("pack/%s.%s names `%s`, and nothing in the %s namespace is "
                                 "spelled that way — a membership file holds names so that a "
                                 "misspelling fails here",
                                 ns, half, name, ns);
                    continue;
                }
                in_cache = archive_holds_id(archive, id);

                if( row && row->server_base > 0 && id >= row->server_base )
                {
                    if( s == CP_MEMBERSHIP_CLIENT )
                    {
                        /*
                         * Allocated by this tree and stated client-side. Counted,
                         * not failed: the base says where a number comes from and
                         * not which side owns the record. What makes it worth
                         * printing is the pair — an id out of the server
                         * allocator whose entity states no server half.
                         */
                        allocated_client++;
                        if( !cp_membership_has(&side[CP_MEMBERSHIP_SERVER], name) )
                            report_info("%s `%s` is id %d, at or above the allocation base %d, "
                                        "and pack/%s.server does not name it",
                                        ns, name, id, row->server_base, ns);
                    }
                    continue;
                }
                if( in_cache )
                {
                    /* Below the base and the cache defines it: the overlap §2
                     * describes, and the normal case for a record with a server
                     * half. */
                    overlap++;
                    continue;
                }
                report_error("pack/%s.%s names `%s` at id %d, which is below the allocation "
                             "base %d and which the cache's %s group does not define — the "
                             "record sits on an id no side owns",
                             ns, half, name, id, row ? row->server_base : 0, ns);
            }
        }

        if( allocated_client && !g_verbose )
            report_info("(-v lists them)");
        RSCache_Dat2DiskArchiveFree(archive);
        for( int s = 0; s < CP_MEMBERSHIP_SIDES; s++ )
            cp_membership_free(&side[s]);
    }

    fprintf(stderr,
            "membership ids\n        %d namespace(s), %d name(s): %d below the base and in the "
            "cache, %d allocated and stated client-side\n",
            namespaces, names, overlap, allocated_client);
}

static void
validate_doors(
    struct RSCache_Dat2Disk* disk,
    struct RSCache* profile)
{
    int table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);
    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(disk, table, RSCACHE_DAT2_CONFIG_KIND_LOCS);
    struct RSCache_FileList* files;
    struct RSCache_Dat2ConfigLoc** configs;
    int highest = -1;
    int pairs = 0;
    int dropped = 0;
    /*
     * The two ids this validator is *about*, resolved through the pack.
     *
     * They were `MOCK230_LOC_CATEGORY_DOOR_CLOSED` / `_OPENED`, a private enum
     * whose two values happened to be 1 and 2, until the loc category became a
     * real namespace. Naming them here is what `k_expect` above already does with
     * obj symbols and is what a validator is for — it is checking a claim the
     * content tree makes, so it has to be able to say which claim. Nothing in
     * `src/net/mock/` outside this file and that table may spell one.
     */
    int door_closed = mock230_content_symbol(MOCK230_PACK_CATEGORY, "door_closed");
    int door_opened = mock230_content_symbol(MOCK230_PACK_CATEGORY, "door_opened");

    if( door_closed <= 0 || door_opened <= 0 )
    {
        report_error("pack/category.pack names no door_closed/door_opened — every pair in "
                     "the door table would go unchecked, which is worse than a bad pair");
        if( archive )
            RSCache_Dat2DiskArchiveFree(archive);
        return;
    }

    if( !archive || !RSCache_Dat2DiskArchiveInitMetadata(disk, archive) )
    {
        report_error("no loc config archive in the cache");
        return;
    }
    RSCache_ProfileSetGroupRevision(profile, RSCACHE_TYPE_LOC, archive->revision);
    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size,
                                          archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return;
    }
    for( int i = 0; i < archive->file_count; i++ )
    {
        if( archive->file_ids[i] > highest )
            highest = archive->file_ids[i];
    }
    configs = calloc((size_t)(highest + 1), sizeof(*configs));
    for( int i = 0; i < archive->file_count; i++ )
    {
        int id = archive->file_ids[i];

        if( id < 0 || files->file_sizes[i] <= 0 )
            continue;
        configs[id] = RSCache_Dat2ConfigLocNewDecodeProfile(profile, files->files[i],
                                                            files->file_sizes[i]);
    }

    /*
     * Every door pair, checked against the cache.
     *
     * Most of them are *derived* — proposed from OpenRune's gameval names by
     * a naming convention, which cannot tell a real pair from a
     * coincidence. This is the check that makes that guesswork safe: a closed
     * door has an Open action and its partner does not, and a pair that fails
     * either half is reported so it can be dropped from the config rather than
     * left to produce a door that opens into nothing.
     */
    fprintf(stderr, "door pairings\n");
    for( int loc_id = 0; loc_id <= highest; loc_id++ )
    {
        const struct Mock230LocDef* def = mock230_content_loc(loc_id);

        if( !def || (def->category != door_closed && def->category != door_opened) )
            continue;
        pairs++;

        if( !configs[loc_id] )
        {
            report_warning("loc %d (%s) is not in the cache", loc_id,
                           def->symbol ? def->symbol : "?");
            reject_door(loc_id);
            dropped++;
            continue;
        }
        if( def->next_loc_stage < 0 || def->next_loc_stage > highest ||
            !configs[def->next_loc_stage] )
        {
            report_warning("loc %d (%s) opens into %d, which is not in the cache", loc_id,
                           def->symbol ? def->symbol : "?", def->next_loc_stage);
            reject_door(loc_id);
            dropped++;
            continue;
        }
        /* The test that separates a door from a wall that looks like one. A
         * closed door offers "Open"; scenery with a door-shaped name does not,
         * and would otherwise sit in the table waiting to swallow a click. */
        if( def->category == door_closed && !loc_has_open_op(configs[loc_id], "Open") )
        {
            report_warning("loc %d (%s) is marked door_closed but has no Open action",
                           loc_id, def->symbol ? def->symbol : "?");
            reject_door(loc_id);
            dropped++;
        }
    }
    fprintf(stderr, "        %d door locs, %d questionable\n", pairs, dropped);

    for( int i = 0; i <= highest; i++ )
    {
        if( configs[i] )
            RSCache_Dat2ConfigLocFree(configs[i]);
    }
    free(configs);
    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

static void
usage(void)
{
    fprintf(stderr,
            "usage: mock230_pack [--check-only] [--content DIR] [--cache DIR] [-v]\n"
            "\n"
            "  --check-only     validate and exit non-zero on any error. This is\n"
            "                   all the tool does, so the flag is accepted for the\n"
            "                   documented invocation rather than changing anything\n"
            "  --content DIR    content tree (default OSRS-Content/osrs239-content)\n"
            "  --cache DIR      source cache (default cache.osrs239)\n"
            "  --prune-doors    rewrite doors.loc without the pairs the cache\n"
            "                   rejects\n"
            "  -v               list every definition as it is checked\n");
}

int
main(
    int argc,
    char** argv)
{
    const char* content = "OSRS-Content/osrs239-content";
    /* Pack validates against the pristine unpack source, not the baked client
     * cache the live server shares with the client. */
    const char* cache = "cache.osrs239";
    int prune = 0;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--content") == 0 && i + 1 < argc )
            content = argv[++i];
        else if( strcmp(argv[i], "--cache") == 0 && i + 1 < argc )
            cache = argv[++i];
        else if( strcmp(argv[i], "--check-only") == 0 )
            ; /* validation is all there is; accepted so the documented
               * `mock230_pack --check-only` invocation says what it means */
        else if( strcmp(argv[i], "--prune-doors") == 0 )
            prune = 1;
        else if( strcmp(argv[i], "-v") == 0 )
            g_verbose = 1;
        else
        {
            usage();
            return 2;
        }
    }

    mock230_objinfo_load(cache);
    mock230_npcinfo_load(cache);
    mock230_seqinfo_load(cache);
    /* The loc config table, for `validate_categories`' loc arm. Without it every
     * loc-only category name in `pack/category.pack` would be reported as carried
     * by nothing — a validator answering from a table it did not load is worse
     * than one that does not check at all. */
    mock230_locinfo_load(cache);
    mock230_content_load(content);
    /*
     * The `.dbtable`/`.dbrow` half of the tree, which this validator did not read.
     *
     * `mock230_db_load` had exactly one caller — `mock230_boot.c` — so every table
     * and every row in the content tree was checked only by starting the server.
     * That is the one namespace family whose ids are allocated rather than the
     * cache's (docs/CONTENT_ARCHITECTURE.md §3.3), so "the id is missing", "the
     * table has no such column" and "the value names nothing" were all boot-time
     * discoveries in a tree `mock230_pack --check-only` called clean. Its errors
     * go through `mock230_content_report_error`, so they reach the exit status
     * from here without any further plumbing.
     */
    mock230_db_load(content);
    /* The engine's own symbol table, which is a claim about the packs in
     * exactly the way a config line is: every name the C addresses has to be in
     * one. Resolving it here is what makes a renamed or dropped symbol a
     * validator failure rather than a dead interface at runtime. */
    mock230_ids_resolve();

    /*
     * The server band, held to the text parse it is replacing.
     *
     * This is the permanent home of the equivalence check PORTING_GUIDE §3.6
     * item 1 demands: a band on disk that disagrees with the tree is a
     * validator failure, exactly like a config line that stopped meaning
     * anything. An *absent* band is not — a fresh checkout has none until
     * `cachepack pack --server-only` runs, the way it has no script pack until
     * `mock230-scripts` does.
     */
    {
        struct Mock230BandReport band;

        switch( mock230_content_load_server_band(content, &band) )
        {
        case MOCK230_BAND_LOADED:
            printf("server band: %d archive(s) verified against the text parse "
                   "(%d overlay authored defs, %d field value(s) text-only, %d over "
                   "records the runtime never loads)\n",
                   band.archives, band.overlaid, band.text_only, band.unseeded);
            break;
        case MOCK230_BAND_MISSING:
            printf("server band: no server/pack — run `make -C src mock230-servpack`; "
                   "skipped\n");
            break;
        case MOCK230_BAND_STALE:
            fprintf(stderr,
                    "mock230_pack: server band disagrees with the tree (%d unreadable, "
                    "%d mismatched archive(s)) — re-run `make -C src mock230-servpack`\n",
                    band.invalid, band.mismatched);
            g_errors++;
            break;
        }
    }

    g_errors += mock230_content_error_count();

    validate_spawns();
    validate_requirements();
    validate_categories(content);
    {
        struct RSCache profile = RSCache_ProfileZero();
        struct RSCache_Dat2Disk* disk;

        profile.game = RSCACHE_GAME_OLDSCHOOL;
        profile.epoch = RSCACHE_EPOCH_DAT2;
        profile.revision = MOCK230_CACHE_REVISION;
        disk = RSCache_Dat2DiskNewFromDirectory(cache);
        if( disk )
        {
            RSCache_Dat2DiskSetProfile(disk, &profile);
            validate_id_bases(disk, content);
            validate_membership_ids(disk, content);
            validate_doors(disk, &profile);
            RSCache_Dat2DiskFree(disk);
            if( prune )
                prune_doors(content);
        }
        else
        {
            report_error("cannot open the cache at %s", cache);
        }
    }

    fprintf(stderr, "mock230_pack: %d error(s), %d warning(s)\n", g_errors, g_warnings);
    return g_errors ? 1 : 0;
}
