/*
 * ev_catalog — which animations can apply to which npc.
 *
 * Two passes, kept apart because they carry very different weight.
 *
 * ## Pass 1: rigging matches (concrete)
 *
 * An animation frame addresses bones by index into a *framemap* (the rig). A
 * sequence built against one rig applied to a model skinned for another moves
 * the wrong vertices — so sharing a rig is the hard precondition for an
 * animation being applicable at all.
 *
 * The walk: an npc names its own idle and walk sequences (and its turn/run/crawl
 * variants, and on RS2 a BasType); each of those resolves to a framemap; every
 * other sequence built on those framemaps is a rigging match. That is a
 * *possibility* set, not a fact about what the game plays: framemap 0 is the
 * shared human rig and 3,905 sequences use it, so every human npc rig-matches
 * all of them, which is true and not very selective. For a boss with its own
 * rig the same walk returns a handful, and those are almost certainly its
 * complete animation set.
 *
 * `strict` additionally requires the rig to cover every bone label the npc's
 * models actually reference. It is reported as a column rather than used as a
 * filter, because a model that uses a subset of a rig still animates correctly.
 *
 * ## Pass 2: name matches (a guess, and labelled as one)
 *
 * Sequences and npcs carry the content team's own gameval names —
 * `slayer_abberant_spectre_1`, `zuk_attack` — which is the only handle on an
 * animation nothing points at. A sequence whose name shares a distinctive token
 * with the npc's name is *probably* that npc's. This finds attack, death and
 * spawn animations, which no id walk can reach, and it also produces false
 * positives, which is why it is a separate column and carries its score.
 *
 * Usage:
 *   ev_catalog --rev osrs239 <cache_dir> [--names <content_dir>] [--out DIR]
 *
 * Writes four CSVs. They are normalised rather than one wide table because the
 * un-normalised join is 16,292 npcs x up to 3,905 sequences — around 20 million
 * rows, most of them repeating the same rig membership:
 *
 *   npc_rigs.csv        npc -> its seed sequences and the framemaps they use
 *   framemap_seqs.csv   framemap -> every sequence built on it
 *   npc_name_matches.csv  npc -> name-guessed sequences, with scores
 *   npc_catalog.csv     one row per npc: counts, so the table is browsable
 *
 * Rigging matches for an npc are `npc_rigs x framemap_seqs` on framemap id.
 */

#include "anim_affinity.h"
#include <assert.h>
#include "asset_access.h"
#include "lc_pack.h"
#include "tool_profile.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- gameval names ------------------------------------------------------ */

/*
 * `id=name`, from OSRS-Content's `configs/all.<type>.compack`.
 *
 * Read with the tree's own pack reader rather than a private parser: these are
 * the same files `cachepack` and `port_lostcity` read, comments and all, and a
 * second parser for them is a second thing to keep in step with the format.
 *
 * The names matter because a display name says nothing about animation —
 * "Aberrant spectre" gives no handle at all, where `slayer_abberant_spectre_1`
 * and `abberant_spectre_death` share a word.
 */
static const char*
name_of(const struct LC_Pack* pack, int id)
{
    if( id < 0 || id > pack->max || !pack->names )
        return NULL;
    return pack->names[id];
}

/* ---- name tokenising ---------------------------------------------------- */

#define MAX_TOKENS 24
#define MAX_TOKEN_LEN 48

struct Tokens
{
    char t[MAX_TOKENS][MAX_TOKEN_LEN];
    int count;
};

/*
 * Words that describe what an animation *does* rather than who it belongs to,
 * plus qualifiers that appear on so many names they carry no signal.
 *
 * Dropping these is what stops `dragon_attack` matching every npc whose name
 * contains `attack`. They are only removed from the *matching* set — a token
 * here still appears in the name, it just cannot be the thing two names have in
 * common.
 */
static const char* const NOISE_TOKENS[] = {
    "walk",   "run",     "idle",     "stand",   "ready",  "attack",  "block",
    "death",  "die",     "dead",     "spawn",   "hit",    "hurt",    "cast",
    "shoot",  "bow",     "melee",    "range",   "ranged", "magic",   "turn",
    "left",   "right",   "back",     "forward", "emote",  "anim",    "animation",
    "seq",    "talk",    "open",     "close",   "sit",    "jump",    "climb",
    "fall",   "land",    "sleep",    "eat",     "drink",  "spec",    "special",
    "north",  "south",   "east",     "west",    "start",  "end",     "loop",
    "intro",  "outro",   "phase",    "teleport", "tele",  "gfx",     "fx",
    "proj",   "projectile", "impact", "travel", "npc",    "monster", "boss",
    "male",   "female",  "man",      "woman",   "human",  "player",  "quest",
    "misc",   "new",     "old",      "big",     "small",  "large",   "mini",
    "chathead", "head",  "body",     "front",   "down",   "up",      "in",
    "out",    "the",     "and",      "with",    "from",   "type",    "var",
    "default", "base",   "main",     "extra",   "temp",   "test",    "dummy",
};

static int
is_noise(const char* tok)
{
    for( size_t i = 0; i < sizeof(NOISE_TOKENS) / sizeof(NOISE_TOKENS[0]); i++ )
        if( strcmp(tok, NOISE_TOKENS[i]) == 0 )
            return 1;
    return 0;
}

/**
 * Split a gameval name into matchable tokens.
 *
 * Separators are anything that is not a letter, so `zuk_attack2` and
 * `slayer_abberant_spectre_1` both lose their trailing digits. Tokens shorter
 * than four letters are dropped along with the noise list: `orc` and `imp` are
 * real, but at three letters they collide with substrings of unrelated names
 * often enough to bury the signal.
 */
static void
tokenise(const char* name, struct Tokens* out)
{
    out->count = 0;
    assert(name);

    const char* p = name;
    while( *p && out->count < MAX_TOKENS )
    {
        while( *p && !isalpha((unsigned char)*p) )
            p++;
        const char* start = p;
        while( isalpha((unsigned char)*p) )
            p++;
        size_t len = (size_t)(p - start);
        if( len < 4 || len >= MAX_TOKEN_LEN )
            continue;

        char tok[MAX_TOKEN_LEN];
        for( size_t i = 0; i < len; i++ )
            tok[i] = (char)tolower((unsigned char)start[i]);
        tok[len] = '\0';
        if( is_noise(tok) )
            continue;

        int dup = 0;
        for( int i = 0; i < out->count; i++ )
            if( strcmp(out->t[i], tok) == 0 )
                dup = 1;
        if( !dup )
            strcpy(out->t[out->count++], tok);
    }
}

/**
 * How strongly two names agree: the total length of the tokens they share.
 *
 * Length-weighted because a shared `spectre` means far more than a shared
 * `fire`. The caller's threshold is what turns this into a yes/no.
 */
static int
token_score(const struct Tokens* a, const struct Tokens* b)
{
    int score = 0;
    for( int i = 0; i < a->count; i++ )
        for( int j = 0; j < b->count; j++ )
            if( strcmp(a->t[i], b->t[j]) == 0 )
                score += (int)strlen(a->t[i]);
    return score;
}

/** A shared token of at least this length, or this much total agreement, is
 *  reported. Below it the matches are almost all coincidence. */
#define NAME_MATCH_MIN_SCORE 5
/** Ranked by score; a name like `dragon` would otherwise pull in hundreds. */
#define NAME_MATCH_MAX_PER_NPC 40

/* ---- csv ---------------------------------------------------------------- */

static void
csv_name(FILE* f, const char* s)
{
    assert(s);
    int quote = strchr(s, ',') || strchr(s, '"') ? 1 : 0;
    if( quote )
        fputc('"', f);
    for( const char* p = s; *p; p++ )
    {
        if( *p == '"' )
            fputc('"', f);
        if( *p == '\n' || *p == '\r' )
            continue;
        fputc(*p, f);
    }
    if( quote )
        fputc('"', f);
}

/* ---- sweep -------------------------------------------------------------- */

struct NameMatch
{
    int seq_id;
    int score;
};

static int
name_match_cmp(const void* a, const void* b)
{
    const struct NameMatch* x = a;
    const struct NameMatch* y = b;
    if( x->score != y->score )
        return y->score - x->score;
    return x->seq_id - y->seq_id;
}

static void
usage(const char* argv0)
{
    fprintf(
        stderr,
        "Usage:\n"
        "  %s --rev NAME <cache_dir> [--names <content_dir>] [--out DIR] [--npc-scan-to ID]\n"
        "      [--npc-only-range FIRST LAST]\n"
        "\n"
        "  --names DIR  OSRS-Content revision dir holding configs/all.{npc,seq}.compack;\n"
        "               without it pass 2 (name matches) is skipped\n"
        "  --out DIR    where the CSVs go (default \".\")\n"
        "  --npc-scan-to ID  also probe sparse NPC ids through ID (for overlay-minted ids)\n",
        argv0);
}

int
main(int argc, char** argv)
{
    const char* rev_name = NULL;
    const char* cache_dir = NULL;
    const char* names_dir = NULL;
    const char* out_dir = ".";
    int npc_scan_to = -1;
    int npc_only_first = -1;
    int npc_only_last = -1;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--rev") == 0 && i + 1 < argc )
            rev_name = argv[++i];
        else if( strcmp(argv[i], "--names") == 0 && i + 1 < argc )
            names_dir = argv[++i];
        else if( strcmp(argv[i], "--out") == 0 && i + 1 < argc )
            out_dir = argv[++i];
        else if( strcmp(argv[i], "--npc-scan-to") == 0 && i + 1 < argc )
            npc_scan_to = atoi(argv[++i]);
        else if( strcmp(argv[i], "--npc-only-range") == 0 && i + 2 < argc )
        {
            npc_only_first = atoi(argv[++i]);
            npc_only_last = atoi(argv[++i]);
        }
        else if( argv[i][0] == '-' )
        {
            usage(argv[0]);
            return 1;
        }
        else
            cache_dir = argv[i];
    }

    if( !rev_name || !cache_dir )
    {
        usage(argv[0]);
        return 1;
    }
    if( npc_only_first >= 0 && npc_only_last < npc_only_first )
    {
        fprintf(stderr, "--npc-only-range requires FIRST <= LAST\n");
        return 1;
    }

    struct RSCache profile;
    if( !tool_resolve_profile(rev_name, NULL, NULL, NULL, NULL, &profile) )
        return 1;

    struct Tool_Dat2Cache cache;
    if( !tool_dat2_open(cache_dir, &profile, &cache) )
    {
        fprintf(stderr, "Failed to open cache at %s\n", cache_dir);
        return 1;
    }
    tool_print_profile(cache_dir, &profile);

    struct LC_Pack npc_names = { 0 };
    struct LC_Pack seq_names = { 0 };
    if( names_dir )
    {
        char path[2048];
        snprintf(path, sizeof(path), "%s/configs/all.npc.compack", names_dir);
        if( !lc_pack_load(&npc_names, path, "npc", 1) )
            fprintf(stderr, "warning: no npc gameval names at %s\n", path);
        snprintf(path, sizeof(path), "%s/configs/all.seq.compack", names_dir);
        if( !lc_pack_load(&seq_names, path, "seq", 1) )
            fprintf(stderr, "warning: no seq gameval names at %s\n", path);
        fprintf(
            stderr,
            "gameval names: %d npc, %d seq\n",
            lc_pack_named_count(&npc_names),
            lc_pack_named_count(&seq_names));
    }

    /* One sweep of every sequence to its framemap. This is the expensive part
     * and it is shared by every npc, so it happens once. */
    struct Tool_FramemapIndex fm_index;
    memset(&fm_index, 0, sizeof(fm_index));
    if( !tool_dat2_build_framemap_index(&cache, &fm_index) )
    {
        fprintf(stderr, "Failed to build the sequence -> framemap index\n");
        return 1;
    }
    fprintf(stderr, "framemap index: %d sequences\n", fm_index.count);

    /*
     * seq id -> framemap id, as a flat array.
     *
     * Every lookup below is "what rig is this sequence built on", asked once per
     * npc seed and once per name match. Against the index's 14,413 entries that
     * is a linear scan each time, and 16,292 npcs turn it into tens of billions
     * of comparisons — the first run of this tool did not finish. The index is
     * keyed by sequence id, so a flat array answers it in one load.
     */
    int max_seq_id = 0;
    int max_framemap = 0;
    for( int i = 0; i < fm_index.count; i++ )
    {
        if( fm_index.entries[i].seq_id > max_seq_id )
            max_seq_id = fm_index.entries[i].seq_id;
        if( fm_index.entries[i].framemap_id > max_framemap )
            max_framemap = fm_index.entries[i].framemap_id;
    }

    int distinct_framemaps = 0;
    int framemap_aliases = 0;
    tool_dat2_canonicalise_framemap_index(
        &cache, &fm_index, max_framemap, &distinct_framemaps, &framemap_aliases);
    fprintf(
        stderr,
        "framemap identity: %d distinct rigs, %d duplicate ids unified\n",
        distinct_framemaps,
        framemap_aliases);

    int* seq_framemap = malloc(((size_t)max_seq_id + 1) * sizeof(int));
    assert(seq_framemap);
    for( int i = 0; i <= max_seq_id; i++ )
        seq_framemap[i] = -1;
    for( int i = 0; i < fm_index.count; i++ )
        seq_framemap[fm_index.entries[i].seq_id] = fm_index.entries[i].framemap_id;

    /* Pre-tokenise every sequence name once; doing it inside the npc loop would
     * re-tokenise 14,413 names 16,292 times. */
    struct Tokens* seq_tokens = NULL;
    int seq_name_capacity = seq_names.max + 1;
    if( seq_name_capacity > 1 )
    {
        seq_tokens = calloc((size_t)seq_name_capacity, sizeof(struct Tokens));
        assert(seq_tokens);
        for( int i = 0; i < seq_name_capacity; i++ )
            tokenise(name_of(&seq_names, i), &seq_tokens[i]);
    }

    char path[2048];
    snprintf(path, sizeof(path), "%s/framemap_seqs.csv", out_dir);
    FILE* f_fm = fopen(path, "wb");
    snprintf(path, sizeof(path), "%s/npc_rigs.csv", out_dir);
    FILE* f_rig = fopen(path, "wb");
    snprintf(path, sizeof(path), "%s/npc_name_matches.csv", out_dir);
    FILE* f_nm = fopen(path, "wb");
    snprintf(path, sizeof(path), "%s/npc_catalog.csv", out_dir);
    FILE* f_cat = fopen(path, "wb");
    if( !f_fm || !f_rig || !f_nm || !f_cat )
    {
        fprintf(stderr, "Cannot write CSVs under %s\n", out_dir);
        return 1;
    }

    /* framemap -> sequences. Written once; an npc's rigging matches are this
     * table joined on the framemaps in npc_rigs.csv. */
    fputs("framemap_id,seq_id,kind,frame_count,seq_name\n", f_fm);
    int* fm_seq_count = calloc((size_t)max_framemap + 1, sizeof(int));
    int* fm_skeletal_count = calloc((size_t)max_framemap + 1, sizeof(int));
    for( int i = 0; i < fm_index.count; i++ )
    {
        const struct Tool_FramemapIndexEntry* e = &fm_index.entries[i];
        if( e->framemap_id < 0 )
            continue;
        fm_seq_count[e->framemap_id]++;
        if( e->skeletal )
            fm_skeletal_count[e->framemap_id]++;
        /* `kind` is how the rig was reached, not a different id space: a
         * skeletal sequence's base id and a classic sequence's framemap id are
         * the same idx1 archive, so both kinds sit in this one table and an
         * npc's rig set is the union. It matters because playing a skeletal
         * sequence needs a model with an Animaya skin. */
        fprintf(
            f_fm,
            "%d,%d,%s,%d,",
            e->framemap_id,
            e->seq_id,
            e->skeletal ? "skeletal" : "classic",
            e->frame_count);
        csv_name(f_fm, name_of(&seq_names, e->seq_id));
        fputc('\n', f_fm);
    }

    fputs("npc_id,npc_name,gameval,framemap_id,seed_seqs,strict_covers\n", f_rig);
    fputs("npc_id,gameval,seq_id,seq_name,score,in_rig_set\n", f_nm);
    fputs(
        "npc_id,npc_name,gameval,models,seed_seqs,framemaps,rig_match_seqs,"
        "rig_match_skeletal,animaya_skinned,strict_covers,name_match_seqs,"
        "name_matches_outside_rig\n",
        f_cat);

    int* npc_ids = NULL;
    int npc_count = 0;
    if( !tool_dat2_config_ids(
            &cache, RSCACHE_TYPE_NPC, RSCACHE_DAT2_CONFIG_KIND_NPC, &npc_ids, &npc_count) )
    {
        fprintf(stderr, "Failed to list npc ids\n");
        return 1;
    }
    if( npc_only_first >= 0 )
    {
        free(npc_ids);
        npc_ids = NULL;
        npc_count = 0;
        for( int id = npc_only_first; id <= npc_only_last; id++ )
        {
            struct RSCache_Dat2ConfigNpc* npc = tool_dat2_npc_load(&cache, id);
            if( !npc ) continue;
            RSCache_Dat2ConfigNpcFree(npc);
            int* grown = realloc(npc_ids, (size_t)(npc_count + 1) * sizeof(*npc_ids));
            assert(grown);
            npc_ids = grown;
            npc_ids[npc_count++] = id;
        }
    }
    if( npc_scan_to >= 0 )
    {
        int listed_max = -1;
        for( int i = 0; i < npc_count; i++ )
            if( npc_ids[i] > listed_max ) listed_max = npc_ids[i];
        for( int id = listed_max + 1; id <= npc_scan_to; id++ )
        {
            struct RSCache_Dat2ConfigNpc* npc = tool_dat2_npc_load(&cache, id);
            if( !npc ) continue;
            RSCache_Dat2ConfigNpcFree(npc);
            int* grown = realloc(npc_ids, (size_t)(npc_count + 1) * sizeof(*npc_ids));
            assert(grown);
            npc_ids = grown;
            npc_ids[npc_count++] = id;
        }
    }
    fprintf(stderr, "npcs: %d\n", npc_count);

    long total_rig = 0;
    long total_name = 0;
    int npcs_with_rig = 0;
    int npcs_with_name = 0;

    struct NameMatch* matches =
        seq_tokens ? malloc((size_t)seq_name_capacity * sizeof(struct NameMatch)) : NULL;

    for( int n = 0; n < npc_count; n++ )
    {
        int npc_id = npc_ids[n];
        struct RSCache_Dat2ConfigNpc* npc = tool_dat2_npc_load(&cache, npc_id);
        if( !npc )
            continue;

        struct Tool_AnimSeeds seeds;
        tool_dat2_npc_anim_seeds(&cache, npc, &seeds);

        /* Seed sequences -> the framemaps they are built on. */
        int fms[64];
        int fm_count = 0;
        for( int s = 0; s < seeds.count && fm_count < 64; s++ )
        {
            int sid = seeds.seq_ids[s];
            if( sid < 0 || sid > max_seq_id )
                continue;
            int fm = seq_framemap[sid];
            if( fm < 0 )
                continue;
            int dup = 0;
            for( int k = 0; k < fm_count; k++ )
                if( fms[k] == fm )
                    dup = 1;
            if( !dup )
                fms[fm_count++] = fm;
        }

        /* Does every rig cover the bone labels the npc's models actually use?
         * Reported, not enforced: a model using a subset of a rig animates
         * correctly, so this is a confidence signal rather than a filter. */
        /*
         * Two questions about the npc's own geometry, asked in one pass over
         * its models because decoding them is the expensive part of this sweep.
         *
         * `animaya_skinned` — can it be posed by a skeletal sequence at all? A
         * skeletal animation drives per-vertex bone influences; a model without
         * them is left in its bind pose rather than mis-animated, so a skeletal
         * rig match on an unskinned npc is not playable and the two columns have
         * to be read together.
         *
         * `strict_covers` — does every rig cover the bone labels the models
         * actually reference? Reported, not enforced: a model using a subset of
         * a rig animates correctly.
         */
        int animaya_skinned = 0;
        int strict_covers = fm_count > 0 ? 1 : 0;

        if( npc->models_count > 0 )
        {
            struct RSCache_Dat2Framemap** rig_maps =
                fm_count > 0 ? calloc((size_t)fm_count, sizeof(*rig_maps)) : NULL;
            for( int k = 0; k < fm_count; k++ )
            {
                rig_maps[k] = tool_dat2_framemap_load(&cache, fms[k]);
                if( !rig_maps[k] )
                    strict_covers = 0;
            }

            for( int m = 0; m < npc->models_count; m++ )
            {
                struct RSCache_Model* model = tool_dat2_model_load(&cache, npc->models[m]);
                if( !model )
                    continue;
                if( model->animaya_vertex_count > 0 )
                    animaya_skinned = 1;
                for( int k = 0; k < fm_count && strict_covers; k++ )
                    if( rig_maps[k] && !tool_framemap_covers_model(rig_maps[k], model) )
                        strict_covers = 0;
                RSCache_ModelFree(model);
            }

            for( int k = 0; k < fm_count; k++ )
                RSCache_Dat2FramemapFree(rig_maps[k]);
            free(rig_maps);
        }

        int rig_matches = 0;
        int rig_skeletal = 0;
        for( int k = 0; k < fm_count; k++ )
        {
            if( fms[k] >= 0 && fms[k] <= max_framemap )
            {
                rig_matches += fm_seq_count[fms[k]];
                rig_skeletal += fm_skeletal_count[fms[k]];
            }

            fprintf(f_rig, "%d,", npc_id);
            csv_name(f_rig, npc->name);
            fputc(',', f_rig);
            csv_name(f_rig, name_of(&npc_names, npc_id));
            fprintf(f_rig, ",%d,", fms[k]);
            for( int s = 0; s < seeds.count; s++ )
                fprintf(f_rig, s ? " %d" : "%d", seeds.seq_ids[s]);
            fprintf(f_rig, ",%s\n", strict_covers ? "true" : "false");
        }

        /* Pass 2. Scored against the npc's gameval name, then ranked, because a
         * common token would otherwise dump hundreds of rows per npc. */
        int name_match_count = 0;
        int name_outside_rig = 0;
        const char* gv = name_of(&npc_names, npc_id);
        if( matches && gv )
        {
            struct Tokens npc_tok;
            tokenise(gv, &npc_tok);

            int found = 0;
            if( npc_tok.count > 0 )
            {
                for( int s = 0; s < seq_name_capacity; s++ )
                {
                    if( seq_tokens[s].count == 0 )
                        continue;
                    int score = token_score(&npc_tok, &seq_tokens[s]);
                    if( score < NAME_MATCH_MIN_SCORE )
                        continue;
                    matches[found].seq_id = s;
                    matches[found].score = score;
                    found++;
                }
            }

            qsort(matches, (size_t)found, sizeof(struct NameMatch), name_match_cmp);
            if( found > NAME_MATCH_MAX_PER_NPC )
                found = NAME_MATCH_MAX_PER_NPC;

            for( int i = 0; i < found; i++ )
            {
                /* Whether the rig walk already found it. A name match inside
                 * the rig set corroborates; one outside it is either a genuine
                 * find the id walk cannot reach, or a false positive. */
                int in_rig = 0;
                int mfm = matches[i].seq_id <= max_seq_id ? seq_framemap[matches[i].seq_id] : -1;
                for( int k = 0; k < fm_count && mfm >= 0; k++ )
                    if( fms[k] == mfm )
                        in_rig = 1;

                fprintf(f_nm, "%d,", npc_id);
                csv_name(f_nm, gv);
                fprintf(f_nm, ",%d,", matches[i].seq_id);
                csv_name(f_nm, name_of(&seq_names, matches[i].seq_id));
                fprintf(f_nm, ",%d,%s\n", matches[i].score, in_rig ? "true" : "false");

                if( !in_rig )
                    name_outside_rig++;
            }
            name_match_count = found;
        }

        fprintf(f_cat, "%d,", npc_id);
        csv_name(f_cat, npc->name);
        fputc(',', f_cat);
        csv_name(f_cat, gv);
        fputc(',', f_cat);
        for( int m = 0; m < npc->models_count; m++ )
            fprintf(f_cat, m ? " %d" : "%d", npc->models[m]);
        fputc(',', f_cat);
        for( int s = 0; s < seeds.count; s++ )
            fprintf(f_cat, s ? " %d" : "%d", seeds.seq_ids[s]);
        fputc(',', f_cat);
        for( int k = 0; k < fm_count; k++ )
            fprintf(f_cat, k ? " %d" : "%d", fms[k]);
        fprintf(
            f_cat,
            ",%d,%d,%s,%s,%d,%d\n",
            rig_matches,
            rig_skeletal,
            animaya_skinned ? "true" : "false",
            strict_covers ? "true" : "false",
            name_match_count,
            name_outside_rig);

        total_rig += rig_matches;
        total_name += name_match_count;
        if( rig_matches > 0 )
            npcs_with_rig++;
        if( name_match_count > 0 )
            npcs_with_name++;

        tool_anim_seeds_free(&seeds);
        RSCache_Dat2ConfigNpcFree(npc);

        if( (n % 2000) == 0 )
            fprintf(stderr, "  %d/%d npcs\n", n, npc_count);
    }

    fclose(f_fm);
    fclose(f_rig);
    fclose(f_nm);
    fclose(f_cat);

    fprintf(
        stderr,
        "\n%d npcs; %d have a rig match (%ld rows if joined), "
        "%d have a name match (%ld rows)\n",
        npc_count,
        npcs_with_rig,
        total_rig,
        npcs_with_name,
        total_name);

    free(matches);
    free(fm_skeletal_count);
    free(seq_framemap);
    free(seq_tokens);
    free(fm_seq_count);
    free(npc_ids);
    tool_framemap_index_free(&fm_index);
    lc_pack_free(&npc_names);
    lc_pack_free(&seq_names);
    tool_dat2_close(&cache);
    return 0;
}
