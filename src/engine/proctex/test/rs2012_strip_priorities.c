/*
 * Strip face render priorities from the imported RS2012 model lane.
 *
 * Face render priorities are a painter's-algorithm crutch: with no depth
 * buffer, the artist pins a face into a draw band so it lands in front of or
 * behind its neighbours regardless of depth. The RS727 client these models
 * came from resolved visibility with a z-buffer, so their priority bytes never
 * had to order anything and do not describe a usable painter order.
 *
 * ToriDraw honours priorities when a model carries them, which overrides the
 * depth sort with those unused values. On the Queen Black Dragon that shows up
 * as the neck sorting inside-out - near faces behind far ones - because the
 * priority bands, not depth, decide the order. Dropping them puts every face
 * back under the full depth sort, which is what a model authored against a
 * z-buffer expects. See docs/qbd_toridraw_streaks_debug.md.
 *
 * This rewrites the lane's OB3 assets in place rather than gating the engine,
 * so the property travels with the content that has it and OSRS models keep
 * their (meaningful) priorities.
 *
 * Build and run:
 *   make -C src rs2012-strip-priorities
 *   src/build/rs2012_strip_priorities            # report only
 *   src/build/rs2012_strip_priorities --apply    # rewrite the .ob3 files
 *
 * Re-pack the cache afterwards: the models are assets, so the change only
 * reaches a client through `make -C src mock230-cache-rs2012`.
 */

#include "datatypes/model.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_LANE "OSRS-Content/osrs239-content/models/ported/rs2012_qbd_td"

struct stats
{
    int seen;
    int per_face;    /* model_priority == 255: a byte per face */
    int whole_model; /* a single non-zero priority for every face */
    int already_none;
    int rewritten;
    int failed;
};

static uint8_t*
read_file(const char* path, long* out_size)
{
    FILE* f = fopen(path, "rb");
    uint8_t* bytes;
    long size;

    if( !f )
        return NULL;
    if( fseek(f, 0, SEEK_END) != 0 )
    {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if( size <= 0 || fseek(f, 0, SEEK_SET) != 0 )
    {
        fclose(f);
        return NULL;
    }
    bytes = (uint8_t*)malloc((size_t)size);
    if( !bytes || fread(bytes, 1, (size_t)size, f) != (size_t)size )
    {
        free(bytes);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = size;
    return bytes;
}

static bool
write_file(const char* path, const uint8_t* bytes, uint32_t size)
{
    FILE* f = fopen(path, "wb");
    bool ok;

    if( !f )
        return false;
    ok = fwrite(bytes, 1, size, f) == size;
    fclose(f);
    return ok;
}

/** Decode, drop priorities, re-encode, and prove the result still decodes to
 *  the same face count with no priorities left. */
static bool
strip_one(const char* path, bool apply, struct stats* st)
{
    long size = 0;
    uint8_t* bytes = read_file(path, &size);
    struct RSCache_ModelProvenance* provenance = NULL;
    struct RSCache_Model* model = NULL;
    struct RSCache_ModelProvenance* check_provenance = NULL;
    struct RSCache_Model* check = NULL;
    uint8_t* encoded = NULL;
    uint32_t bound;
    uint32_t written;
    bool ok = false;

    if( !bytes )
    {
        fprintf(stderr, "rs2012_strip_priorities: cannot read %s\n", path);
        st->failed++;
        return false;
    }

    model = RSCache_ModelNewDecodeProvenance(bytes, (int)size, &provenance);
    if( !model || !provenance )
    {
        fprintf(stderr, "rs2012_strip_priorities: cannot decode %s\n", path);
        st->failed++;
        goto done;
    }

    st->seen++;

    if( model->face_priorities )
        st->per_face++;
    else if( model->model_priority )
        st->whole_model++;
    else
    {
        st->already_none++;
        ok = true;
        goto done;
    }

    /* Both forms go: a NULL face table with a zero model priority is how the
     * encoder writes "no priorities", and how ToriDraw reads "depth sort". */
    free(model->face_priorities);
    model->face_priorities = NULL;
    model->model_priority = 0;

    /* The encoder prefers the provenance's recorded header over anything
     * derived from the model, and rejects a header claiming per-face
     * priorities (255) when the array is gone. header_flags[1] is that byte;
     * clearing it keeps the rest of the provenance - notably the OB3 tail
     * carrying particle/billboard sections - intact. */
    if( provenance->header_flag_count > 1 )
        provenance->header_flags[1] = 0;

    bound = RSCache_ModelEncodeBound(model, provenance);
    encoded = bound ? (uint8_t*)malloc(bound) : NULL;
    written = encoded ? RSCache_ModelEncodeFormat(
                            model, provenance, provenance->format, encoded, bound)
                      : 0;
    if( !written )
    {
        fprintf(stderr, "rs2012_strip_priorities: cannot encode %s\n", path);
        st->failed++;
        goto done;
    }

    check = RSCache_ModelNewDecodeProvenance(encoded, (int)written, &check_provenance);
    if( !check || !check_provenance || check->face_count != model->face_count ||
        check_provenance->format != provenance->format || check->face_priorities ||
        check->model_priority )
    {
        fprintf(stderr, "rs2012_strip_priorities: %s failed its decode check\n", path);
        st->failed++;
        goto done;
    }

    if( apply && !write_file(path, encoded, written) )
    {
        fprintf(stderr, "rs2012_strip_priorities: cannot write %s\n", path);
        st->failed++;
        goto done;
    }

    st->rewritten++;
    ok = true;

done:
    RSCache_ModelFree(check);
    RSCache_ModelProvenanceFree(check_provenance);
    RSCache_ModelFree(model);
    RSCache_ModelProvenanceFree(provenance);
    free(encoded);
    free(bytes);
    return ok;
}

static bool
has_suffix(const char* name, const char* suffix)
{
    size_t n = strlen(name);
    size_t s = strlen(suffix);
    return n >= s && strcmp(name + n - s, suffix) == 0;
}

static void
usage(const char* argv0)
{
    fprintf(
        stderr,
        "Usage: %s [--lane DIR] [--apply]\n"
        "Default lane: " DEFAULT_LANE "\n",
        argv0);
}

int
main(int argc, char** argv)
{
    const char* lane = DEFAULT_LANE;
    bool apply = false;
    struct stats st = { 0 };
    DIR* dir;
    struct dirent* entry;
    char path[1024];

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--lane") == 0 && i + 1 < argc )
            lane = argv[++i];
        else if( strcmp(argv[i], "--apply") == 0 )
            apply = true;
        else
        {
            usage(argv[0]);
            return 2;
        }
    }

    dir = opendir(lane);
    if( !dir )
    {
        fprintf(stderr, "rs2012_strip_priorities: cannot open lane %s: %s\n", lane,
                strerror(errno));
        return 1;
    }

    while( (entry = readdir(dir)) != NULL )
    {
        if( !has_suffix(entry->d_name, ".ob3") )
            continue;
        snprintf(path, sizeof(path), "%s/%s", lane, entry->d_name);
        strip_one(path, apply, &st);
    }
    closedir(dir);

    printf(
        "rs2012_strip_priorities: %s\n"
        "  models            %d\n"
        "  per-face priority %d\n"
        "  model priority    %d\n"
        "  already none      %d\n"
        "  rewritten         %d\n"
        "  failed            %d\n",
        apply ? "applied" : "report only (pass --apply to write)",
        st.seen,
        st.per_face,
        st.whole_model,
        st.already_none,
        st.rewritten,
        st.failed);

    return st.failed ? 1 : 0;
}
