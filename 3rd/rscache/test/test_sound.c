/*
 * Sound effect codec over the real caches on disk.
 *
 * Three questions, per cache:
 *
 *   framing     does the whole container consume exactly? A dat1 `sounds.dat`
 *               is one long chain of variable-length records, so a single
 *               mis-sized field desyncs everything after it and the stream fails
 *               to end on its 65535 terminator. This is the check that pins the
 *               era gate down: parse 254 as if it had filters and it dies on the
 *               second record.
 *   byte-exact  encode(decode(bytes)) == bytes, for every effect. The format has
 *               no optional-field ordering to guess at, so unlike the config
 *               types this should be 100% — anything less means a field was
 *               consumed and dropped.
 *   render      the synth program actually produces sound, and produces the same
 *               sound twice.
 *
 * Skips cleanly when a cache directory is absent, so this runs anywhere.
 */

#include "rscache_test.h"

#include <rscache.h>

#include <stdlib.h>

#ifndef RSCACHE_TEST_REPO_ROOT
#define RSCACHE_TEST_REPO_ROOT "../.."
#endif

/* dat1 caches. No revision module names rs377, so its identity is stated here;
 * the sound codec only consults game/epoch/revision. */
struct dat1_entry
{
    const char* dir;
    int revision;
    /* Expected codec, so a gate regression fails loudly rather than silently
     * decoding the other flavour. */
    int expect_codec;
};

static const struct dat1_entry DAT1_CACHES[] = {
    { "cache254", 254, RSCACHE_CODEC_SOUND_WAVE },
    { "cache254.lostcity", 254, RSCACHE_CODEC_SOUND_WAVE },
    { "cache.rs254_zuk", 254, RSCACHE_CODEC_SOUND_WAVE },
    { "cache.rs254_steeltitan", 254, RSCACHE_CODEC_SOUND_WAVE },
    { "cache.rs377", 377, RSCACHE_CODEC_SOUND_SYNTH },
};

struct dat2_entry
{
    const char* dir;
    const char* profile_name;
};

static const struct dat2_entry DAT2_CACHES[] = {
    { "cache.osrs184", "osrs184" },
    { "cache.kronos", "kronos" },
    { "cache.osrs230", "osrs230" },
    { "cache.osrs239", "osrs239" },
    { "cache.rs643", "643" },
};

static void
cache_path(
    char* out,
    size_t out_size,
    const char* dir)
{
    snprintf(out, out_size, "%s/%s", RSCACHE_TEST_REPO_ROOT, dir);
}

/* --- round trip ---------------------------------------------------------- */

/**
 * encode(effect) must reproduce the record's original bytes.
 *
 * `original`/`original_size` is the slice of the container the record was read
 * from; only the first `consumed` bytes belong to it.
 */
static bool
check_effect_roundtrip(
    const struct RSCache* profile,
    const struct RSCache_SoundEffect* effect,
    const uint8_t* original,
    int consumed)
{
    uint32_t bound = RSCache_SoundEffectEncodeBound(effect);
    uint8_t* encoded = malloc(bound);
    uint32_t written;
    bool ok;

    if( !encoded )
        return false;
    written = RSCache_SoundEffectEncode(profile, effect, encoded, bound);
    ok = written == (uint32_t)consumed && memcmp(encoded, original, (size_t)consumed) == 0;
    free(encoded);
    return ok;
}

/* --- dat1 ---------------------------------------------------------------- */

static void
test_dat1_cache(const struct dat1_entry* entry)
{
    char path[512];
    struct RSCache profile = RSCache_ProfileZero();
    struct RSCache_Dat1Disk* disk;
    struct RSCache_Dat1DiskArchive* archive;
    struct RSCache_FileListDat* jagfile;
    struct RSCache_SoundBank* bank;
    int sounds_index;
    int effects = 0;
    int byte_exact = 0;
    uint32_t reencoded;
    uint8_t* buffer;
    uint32_t bound;

    profile.game = RSCACHE_GAME_RS2;
    profile.epoch = RSCACHE_EPOCH_DAT1;
    profile.revision = entry->revision;

    cache_path(path, sizeof(path), entry->dir);
    disk = RSCache_Dat1DiskNewFromDirectory(path);
    if( !disk )
    {
        printf("   SKIP %s: not present\n", entry->dir);
        return;
    }

    RSCACHE_CHECK_EQ(RSCache_SoundCodecVersion(&profile), entry->expect_codec);

    archive =
        RSCache_Dat1DiskArchiveNewLoad(disk, RSCACHE_DAT1_DISK_TABLE_CONFIGS,
                                       RSCACHE_DAT1_CONFIG_SOUND_EFFECTS);
    if( !archive )
    {
        printf("   SKIP %s: no sound-effects archive\n", entry->dir);
        RSCache_Dat1DiskFree(disk);
        return;
    }
    jagfile = RSCache_FileListDatNewFromDecode(archive->data, archive->data_size);
    RSCACHE_CHECK(jagfile != NULL);
    if( !jagfile )
    {
        RSCache_Dat1DiskArchiveFree(archive);
        RSCache_Dat1DiskFree(disk);
        return;
    }
    sounds_index = RSCache_FileListDatFindFileByName(jagfile, "sounds.dat");
    RSCACHE_CHECK(sounds_index >= 0);
    if( sounds_index < 0 )
    {
        RSCache_FileListDatFree(jagfile);
        RSCache_Dat1DiskArchiveFree(archive);
        RSCache_Dat1DiskFree(disk);
        return;
    }

    bank = RSCache_SoundBankNewDecode(
        &profile, jagfile->files[sounds_index], jagfile->file_sizes[sounds_index]);
    RSCACHE_CHECK(bank != NULL);
    if( bank )
    {
        /* The whole file, not a byte less: the terminator was reached exactly
         * where the file ends. */
        RSCACHE_CHECK_EQ(bank->_consumed, jagfile->file_sizes[sounds_index]);

        for( int id = 0; id < bank->count; id++ )
            if( bank->effects[id] )
                effects++;
        RSCACHE_CHECK(effects > 0);

        /* Whole-bank byte-exact round trip. Doing it at bank level (rather than
         * per record) also checks the id order and the terminator. */
        bound = RSCache_SoundBankEncodeBound(bank);
        buffer = malloc(bound);
        RSCACHE_CHECK(buffer != NULL);
        if( buffer )
        {
            reencoded = RSCache_SoundBankEncode(&profile, bank, buffer, bound);
            RSCACHE_CHECK_EQ(reencoded, (uint32_t)jagfile->file_sizes[sounds_index]);
            if( reencoded == (uint32_t)jagfile->file_sizes[sounds_index] )
            {
                RSCACHE_CHECK_BYTES_EQ(
                    buffer, jagfile->files[sounds_index], jagfile->file_sizes[sounds_index]);
                byte_exact = 1;
            }
            free(buffer);
        }

        printf(
            "   %-24s codec=%d effects=%4d bytes=%7d byte_exact=%s\n",
            entry->dir,
            RSCache_SoundCodecVersion(&profile),
            effects,
            bank->_consumed,
            byte_exact ? "yes" : "NO");

        RSCache_SoundBankFree(bank);
    }

    RSCache_FileListDatFree(jagfile);
    RSCache_Dat1DiskArchiveFree(archive);
    RSCache_Dat1DiskFree(disk);
}

/* --- dat2 ---------------------------------------------------------------- */

static void
test_dat2_cache(const struct dat2_entry* entry)
{
    char path[512];
    struct RSCache profile;
    struct RSCache_Dat2Disk* disk;
    struct RSCache_Dat2DiskArchive* reference_archive;
    struct RSCache_ReferenceTable* reference;
    int table;
    int decoded = 0;
    int exact_consumption = 0;
    int byte_exact = 0;
    int samples = 0;
    int failed = 0;

    cache_path(path, sizeof(path), entry->dir);
    disk = RSCache_Dat2DiskNewFromDirectory(path);
    if( !disk )
    {
        printf("   SKIP %s: not present\n", entry->dir);
        return;
    }
    if( !RSCache_ProfileByName(entry->profile_name, &profile) )
    {
        printf("   SKIP %s: no profile named %s\n", entry->dir, entry->profile_name);
        RSCache_Dat2DiskFree(disk);
        return;
    }
    RSCache_Dat2DiskSetProfile(disk, &profile);

    /* Every dat2 cache measured carries the filtered layout. */
    RSCACHE_CHECK_EQ(RSCache_SoundCodecVersion(&profile), RSCACHE_CODEC_SOUND_SYNTH);

    table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_SOUND_EFFECTS);
    RSCACHE_CHECK(table >= 0);
    if( table < 0 )
    {
        RSCache_Dat2DiskFree(disk);
        return;
    }

    reference_archive = RSCache_Dat2DiskArchiveNewReferenceTableLoad(disk, table);
    RSCACHE_CHECK(reference_archive != NULL);
    if( !reference_archive )
    {
        RSCache_Dat2DiskFree(disk);
        return;
    }
    reference =
        RSCache_ReferenceTableNewDecode(reference_archive->data, reference_archive->data_size);
    RSCache_Dat2DiskArchiveFree(reference_archive);
    RSCACHE_CHECK(reference != NULL);
    if( !reference )
    {
        RSCache_Dat2DiskFree(disk);
        return;
    }

    for( int index = 0; index < reference->id_count; index++ )
    {
        int archive_id = reference->ids[index];
        struct RSCache_Dat2DiskArchive* archive =
            RSCache_Dat2DiskArchiveNewLoad(disk, table, archive_id);
        struct RSCache_FileList* group = NULL;
        struct RSCache_SoundEffect* effect;
        char* record;
        int record_size;

        if( !archive )
        {
            failed++;
            continue;
        }
        RSCache_Dat2DiskArchiveInitMetadata(disk, archive);

        record = archive->data;
        record_size = archive->data_size;
        if( archive->file_count > 1 )
        {
            /* Modern OldSchool pairs the synth record (file 0) with a
             * compressed sample (file 1). */
            group = RSCache_FileListNewFromDecode(
                archive->data, archive->data_size, archive->file_count);
            if( group && group->file_count > 0 )
            {
                record = group->files[0];
                record_size = group->file_sizes[0];
            }
        }

        if( RSCache_SoundIsSampleBlob(record, record_size) )
        {
            /* Sample-only entry: no synth program to decode. */
            samples++;
            goto next;
        }

        effect = RSCache_SoundEffectNewDecode(&profile, record, record_size);
        if( !effect )
        {
            failed++;
            printf("   FAIL %s: archive %d did not decode\n", entry->dir, archive_id);
            goto next;
        }
        decoded++;
        if( effect->_consumed == record_size )
            exact_consumption++;
        else
            printf(
                "   FAIL %s: archive %d consumed %d of %d\n",
                entry->dir,
                archive_id,
                effect->_consumed,
                record_size);
        if( check_effect_roundtrip(&profile, effect, (const uint8_t*)record, effect->_consumed) )
            byte_exact++;
        RSCache_SoundEffectFree(effect);

    next:
        if( group )
            RSCache_FileListFree(group);
        RSCache_Dat2DiskArchiveFree(archive);
    }

    printf(
        "   %-16s decoded=%5d exact=%5d byte_exact=%5d samples=%3d failed=%d\n",
        entry->dir,
        decoded,
        exact_consumption,
        byte_exact,
        samples,
        failed);
    RSCACHE_CHECK_EQ(failed, 0);
    RSCACHE_CHECK_EQ(exact_consumption, decoded);
    RSCACHE_CHECK_EQ(byte_exact, decoded);
    RSCACHE_CHECK(decoded > 1000);

    RSCache_ReferenceTableFree(reference);
    RSCache_Dat2DiskFree(disk);
}

/* --- render -------------------------------------------------------------- */

/**
 * Render every effect in one dat1 bank and one dat2 table, and check the output
 * is audible, bounded and reproducible.
 *
 * "Audible" is the load-bearing assertion: a silent render is exactly what a
 * subtly wrong envelope or a skipped oscillator produces, and it is invisible to
 * the framing and round-trip checks above.
 */
static void
test_render_dat1(void)
{
    char path[512];
    struct RSCache profile = RSCache_ProfileZero();
    struct RSCache_Dat1Disk* disk;
    struct RSCache_Dat1DiskArchive* archive;
    struct RSCache_FileListDat* jagfile;
    struct RSCache_SoundBank* bank;
    int sounds_index;
    int rendered = 0;
    int audible = 0;
    int longest = 0;

    profile.game = RSCACHE_GAME_RS2;
    profile.epoch = RSCACHE_EPOCH_DAT1;
    profile.revision = 254;

    cache_path(path, sizeof(path), "cache254.lostcity");
    disk = RSCache_Dat1DiskNewFromDirectory(path);
    if( !disk )
    {
        printf("   SKIP render(dat1): cache254.lostcity not present\n");
        return;
    }
    archive = RSCache_Dat1DiskArchiveNewLoad(
        disk, RSCACHE_DAT1_DISK_TABLE_CONFIGS, RSCACHE_DAT1_CONFIG_SOUND_EFFECTS);
    jagfile = archive ? RSCache_FileListDatNewFromDecode(archive->data, archive->data_size) : NULL;
    sounds_index = jagfile ? RSCache_FileListDatFindFileByName(jagfile, "sounds.dat") : -1;
    bank = sounds_index >= 0 ? RSCache_SoundBankNewDecode(
                                   &profile,
                                   jagfile->files[sounds_index],
                                   jagfile->file_sizes[sounds_index])
                             : NULL;
    RSCACHE_CHECK(bank != NULL);

    for( int id = 0; bank && id < bank->count; id++ )
    {
        struct RSCache_SoundPcm pcm;
        if( !bank->effects[id] )
            continue;
        if( !RSCache_SoundEffectRender(&profile, bank->effects[id], &pcm) )
            continue;
        rendered++;
        if( pcm.sample_count > longest )
            longest = pcm.sample_count;
        for( int sample = 0; sample < pcm.sample_count; sample++ )
        {
            if( pcm.samples[sample] != 0x80 )
            {
                audible++;
                break;
            }
        }
        RSCache_SoundPcmRelease(&pcm);
    }

    printf("   render(dat1 254)  rendered=%d audible=%d longest=%d samples\n",
           rendered, audible, longest);
    RSCACHE_CHECK(rendered > 500);
    /* Nearly every effect must make sound; a handful of cache entries are
     * genuinely empty programs. */
    RSCACHE_CHECK(audible > rendered - 20);

    /* Reproducibility: the same effect rendered twice must be byte-identical,
     * which is what makes the noise waveform's seeded table matter. */
    if( bank )
    {
        for( int id = 0; id < bank->count; id++ )
        {
            struct RSCache_SoundPcm first;
            struct RSCache_SoundPcm second;
            if( !bank->effects[id] )
                continue;
            if( !RSCache_SoundEffectRender(&profile, bank->effects[id], &first) )
                continue;
            RSCACHE_CHECK(RSCache_SoundEffectRender(&profile, bank->effects[id], &second));
            RSCACHE_CHECK_EQ(second.sample_count, first.sample_count);
            RSCACHE_CHECK_BYTES_EQ(second.samples, first.samples, first.sample_count);
            RSCache_SoundPcmRelease(&first);
            RSCache_SoundPcmRelease(&second);
            break;
        }
    }

    /* Loop expansion lengthens the buffer by whole loop spans. */
    if( bank )
    {
        for( int id = 0; id < bank->count; id++ )
        {
            struct RSCache_SoundPcm pcm;
            int span;
            if( !bank->effects[id] )
                continue;
            if( !RSCache_SoundEffectRender(&profile, bank->effects[id], &pcm) )
                continue;
            if( pcm.loop_start < 0 )
            {
                RSCache_SoundPcmRelease(&pcm);
                continue;
            }
            span = pcm.loop_end - pcm.loop_start;
            {
                int before = pcm.sample_count;
                RSCACHE_CHECK(RSCache_SoundPcmExpandLoops(&pcm, 3));
                RSCACHE_CHECK_EQ(pcm.sample_count, before + span * 2);
            }
            RSCache_SoundPcmRelease(&pcm);
            break;
        }
    }

    RSCache_SoundBankFree(bank);
    RSCache_FileListDatFree(jagfile);
    RSCache_Dat1DiskArchiveFree(archive);
    RSCache_Dat1DiskFree(disk);
}

static void
test_render_dat2(void)
{
    char path[512];
    struct RSCache profile;
    struct RSCache_Dat2Disk* disk;
    int table;
    int rendered = 0;
    int audible = 0;
    int filtered = 0;

    cache_path(path, sizeof(path), "cache.osrs230");
    disk = RSCache_Dat2DiskNewFromDirectory(path);
    if( !disk || !RSCache_ProfileByName("osrs230", &profile) )
    {
        printf("   SKIP render(dat2): cache.osrs230 not present\n");
        RSCache_Dat2DiskFree(disk);
        return;
    }
    RSCache_Dat2DiskSetProfile(disk, &profile);
    table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_SOUND_EFFECTS);

    for( int archive_id = 0; archive_id < 400; archive_id++ )
    {
        struct RSCache_Dat2DiskArchive* archive =
            RSCache_Dat2DiskArchiveNewLoad(disk, table, archive_id);
        struct RSCache_SoundEffect* effect;
        struct RSCache_SoundPcm pcm;

        if( !archive )
            continue;
        effect = RSCache_SoundEffectNewDecode(&profile, archive->data, archive->data_size);
        RSCache_Dat2DiskArchiveFree(archive);
        if( !effect )
            continue;
        for( int slot = 0; slot < RSCACHE_SOUND_TONES; slot++ )
            if( effect->tones[slot] &&
                (effect->tones[slot]->filter.pairs[0] > 0 ||
                 effect->tones[slot]->filter.pairs[1] > 0) )
            {
                filtered++;
                break;
            }
        if( RSCache_SoundEffectRender(&profile, effect, &pcm) )
        {
            rendered++;
            for( int sample = 0; sample < pcm.sample_count; sample++ )
                if( pcm.samples[sample] != 0x80 )
                {
                    audible++;
                    break;
                }
            RSCache_SoundPcmRelease(&pcm);
        }
        RSCache_SoundEffectFree(effect);
    }

    printf("   render(dat2 230)  rendered=%d audible=%d with_filter=%d\n",
           rendered, audible, filtered);
    RSCACHE_CHECK(rendered > 300);
    RSCACHE_CHECK(audible > rendered - 20);
    /* The filter path must actually be exercised by this sample of the table,
     * or the port above is untested. */
    RSCACHE_CHECK(filtered > 0);
}

/*
 * Golden renders.
 *
 * The render was checked against the reference implementation itself — Client3's
 * src/sound/{envelope,tone,wave}.c, run over the same `sounds.dat` with its
 * unseeded noise table replaced by the seeded one this renderer uses — and every
 * effect in all four dat1 caches came out **byte-identical**: 696 in
 * cache254.lostcity, 579 in cache254, 696 each in rs254_zuk and rs254_steeltitan.
 *
 * That comparison needs the reference's source next to ours, so it cannot live
 * here. What lives here is its result: a CRC over every rendered sample, which
 * fails the moment a change moves a byte. The constants below are that verified
 * output, not merely what the code did when they were recorded.
 */
struct golden_dat1
{
    const char* dir;
    int revision;
    int rendered;
    long samples;
    uint32_t crc;
};

static const struct golden_dat1 GOLDEN_DAT1[] = {
    /* Verified byte-identical against Client3's renderer. */
    { "cache254.lostcity", 254, 696, 16473922, 0x61a619c4u },
    { "cache254", 254, 579, 12951078, 0x5c22ca4fu },
    /* 377 has no reference port to compare against (Client3 is a 254-era client),
     * so this constant only pins the renderer against itself. Its *decode* is
     * held to the same exact-consumption and byte-exact round-trip bar as the
     * rest. */
    { "cache.rs377", 377, 2727, 65735097, 0x89b8ac32u },
};

static void
test_golden_dat1(const struct golden_dat1* golden)
{
    char path[512];
    struct RSCache profile = RSCache_ProfileZero();
    struct RSCache_Dat1Disk* disk;
    struct RSCache_Dat1DiskArchive* archive;
    struct RSCache_FileListDat* jagfile;
    struct RSCache_SoundBank* bank;
    int sounds_index;
    uint32_t crc = 0;
    long samples = 0;
    int rendered = 0;

    profile.game = RSCACHE_GAME_RS2;
    profile.epoch = RSCACHE_EPOCH_DAT1;
    profile.revision = golden->revision;

    cache_path(path, sizeof(path), golden->dir);
    disk = RSCache_Dat1DiskNewFromDirectory(path);
    if( !disk )
    {
        printf("   SKIP golden %s: not present\n", golden->dir);
        return;
    }
    archive = RSCache_Dat1DiskArchiveNewLoad(
        disk, RSCACHE_DAT1_DISK_TABLE_CONFIGS, RSCACHE_DAT1_CONFIG_SOUND_EFFECTS);
    jagfile = archive ? RSCache_FileListDatNewFromDecode(archive->data, archive->data_size) : NULL;
    sounds_index = jagfile ? RSCache_FileListDatFindFileByName(jagfile, "sounds.dat") : -1;
    bank = sounds_index >= 0 ? RSCache_SoundBankNewDecode(
                                   &profile,
                                   jagfile->files[sounds_index],
                                   jagfile->file_sizes[sounds_index])
                             : NULL;
    RSCACHE_CHECK(bank != NULL);

    for( int id = 0; bank && id < bank->count; id++ )
    {
        struct RSCache_SoundPcm pcm;
        if( !bank->effects[id] )
            continue;
        RSCache_SoundEffectTrim(bank->effects[id]);
        if( !RSCache_SoundEffectRender(&profile, bank->effects[id], &pcm) )
            continue;
        crc = RSCache_Crc32(crc, pcm.samples, (size_t)pcm.sample_count);
        samples += pcm.sample_count;
        rendered++;
        RSCache_SoundPcmRelease(&pcm);
    }

    printf(
        "   %-20s rendered=%4d samples=%9ld crc=0x%08x\n",
        golden->dir,
        rendered,
        samples,
        crc);
    RSCACHE_CHECK_EQ(rendered, golden->rendered);
    RSCACHE_CHECK_EQ(samples, golden->samples);
    RSCACHE_CHECK_EQ(crc, golden->crc);

    RSCache_SoundBankFree(bank);
    RSCache_FileListDatFree(jagfile);
    RSCache_Dat1DiskArchiveFree(archive);
    RSCache_Dat1DiskFree(disk);
}

/** Same idea for the filtered generation, over the first 400 ids of a table. */
struct golden_dat2
{
    const char* dir;
    const char* profile_name;
    int rendered;
    long samples;
    uint32_t crc;
};

static const struct golden_dat2 GOLDEN_DAT2[] = {
    { "cache.osrs230", "osrs230", 400, 9528466, 0xa9e7bf81u },
    { "cache.rs643", "643", 400, 9787004, 0x509ca709u },
};

#define GOLDEN_DAT2_IDS 400

static void
test_golden_dat2(const struct golden_dat2* golden)
{
    char path[512];
    struct RSCache profile;
    struct RSCache_Dat2Disk* disk;
    int table;
    uint32_t crc = 0;
    long samples = 0;
    int rendered = 0;

    cache_path(path, sizeof(path), golden->dir);
    disk = RSCache_Dat2DiskNewFromDirectory(path);
    if( !disk || !RSCache_ProfileByName(golden->profile_name, &profile) )
    {
        printf("   SKIP golden %s: not present\n", golden->dir);
        RSCache_Dat2DiskFree(disk);
        return;
    }
    RSCache_Dat2DiskSetProfile(disk, &profile);
    table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_SOUND_EFFECTS);

    for( int id = 0; id < GOLDEN_DAT2_IDS; id++ )
    {
        struct RSCache_Dat2DiskArchive* archive =
            RSCache_Dat2DiskArchiveNewLoad(disk, table, id);
        struct RSCache_SoundEffect* effect;
        struct RSCache_SoundPcm pcm;

        if( !archive )
            continue;
        effect = RSCache_SoundEffectNewDecode(&profile, archive->data, archive->data_size);
        RSCache_Dat2DiskArchiveFree(archive);
        if( !effect )
            continue;
        RSCache_SoundEffectTrim(effect);
        if( RSCache_SoundEffectRender(&profile, effect, &pcm) )
        {
            crc = RSCache_Crc32(crc, pcm.samples, (size_t)pcm.sample_count);
            samples += pcm.sample_count;
            rendered++;
            RSCache_SoundPcmRelease(&pcm);
        }
        RSCache_SoundEffectFree(effect);
    }

    printf(
        "   %-20s rendered=%4d samples=%9ld crc=0x%08x\n",
        golden->dir,
        rendered,
        samples,
        crc);
    RSCACHE_CHECK_EQ(rendered, golden->rendered);
    RSCACHE_CHECK_EQ(samples, golden->samples);
    RSCACHE_CHECK_EQ(crc, golden->crc);

    RSCache_Dat2DiskFree(disk);
}

static void
test_wav_header(void)
{
    struct RSCache_SoundPcm pcm;
    uint8_t out[64];
    uint8_t samples[4] = { 0x80, 0x90, 0x70, 0x80 };

    memset(&pcm, 0, sizeof(pcm));
    pcm.samples = samples;
    pcm.sample_count = 4;
    pcm.sample_rate = RSCACHE_SOUND_SAMPLE_RATE;
    RSCACHE_CHECK_EQ(RSCache_SoundPcmWriteWav(&pcm, out, sizeof(out)), 48u);
    RSCACHE_CHECK(memcmp(out, "RIFF", 4) == 0);
    RSCACHE_CHECK(memcmp(out + 8, "WAVE", 4) == 0);
    RSCACHE_CHECK(memcmp(out + 36, "data", 4) == 0);
    RSCACHE_CHECK(memcmp(out + 44, samples, 4) == 0);
    /* Too small: refuse rather than truncate. */
    RSCACHE_CHECK_EQ(RSCache_SoundPcmWriteWav(&pcm, out, 40), 0u);
}

int
main(void)
{
    RSCACHE_TEST_GROUP("dat1 sound banks");
    for( size_t i = 0; i < sizeof(DAT1_CACHES) / sizeof(DAT1_CACHES[0]); i++ )
        test_dat1_cache(&DAT1_CACHES[i]);

    RSCACHE_TEST_GROUP("dat2 sound effects");
    for( size_t i = 0; i < sizeof(DAT2_CACHES) / sizeof(DAT2_CACHES[0]); i++ )
        test_dat2_cache(&DAT2_CACHES[i]);

    RSCACHE_TEST_GROUP("render");
    test_render_dat1();
    test_render_dat2();

    RSCACHE_TEST_GROUP("golden renders");
    for( size_t i = 0; i < sizeof(GOLDEN_DAT1) / sizeof(GOLDEN_DAT1[0]); i++ )
        test_golden_dat1(&GOLDEN_DAT1[i]);
    for( size_t i = 0; i < sizeof(GOLDEN_DAT2) / sizeof(GOLDEN_DAT2[0]); i++ )
        test_golden_dat2(&GOLDEN_DAT2[i]);

    RSCACHE_TEST_GROUP("wav framing");
    test_wav_header();

    return rscache_test_report("test_sound");
}
