#ifndef DAT2_TASKS_H
#define DAT2_TASKS_H

#include "engine/cache_provider.h"

struct ToriRS_Task*
CreateTask_Dat2ModelLoad(
    struct CacheProvider* provider,
    int model_id);

struct ToriRS_Task*
CreateTask_Dat2ComponentPackLoad(
    struct CacheProvider* provider,
    int iface_id);

struct ToriRS_Task*
CreateTask_Dat2ClientScriptTableLoad(struct CacheProvider* provider);

/** The clientscript group whose name hashes to `name_hash`, or -1 (which also
 *  covers "the reference table has not landed yet"). */
int
dat2_clientscript_id_by_name_hash(struct CacheProvider* provider, int name_hash);

struct ToriRS_Task*
CreateTask_Dat2ClientScriptLoad(
    struct CacheProvider* provider,
    int script_id);

struct ToriRS_Task*
CreateTask_Dat2ObjLoad(
    struct CacheProvider* provider,
    int obj_id);

struct ToriRS_Task*
CreateTask_Dat2ObjLoadAll(struct CacheProvider* provider);

struct ToriRS_Task*
CreateTask_Dat2NpcLoad(
    struct CacheProvider* provider,
    int npc_id);

struct ToriRS_Task*
CreateTask_Dat2SpotanimLoad(
    struct CacheProvider* provider,
    int spotanim_id);

struct ToriRS_Task*
CreateTask_Dat2SoundLoad(
    struct CacheProvider* provider,
    int sound_id);

struct ToriRS_Task*
CreateTask_Dat2IdkLoad(
    struct CacheProvider* provider,
    int idk_id);

struct ToriRS_Task*
CreateTask_Dat2MapTerrainLoad(
    struct CacheProvider* provider,
    int map_x,
    int map_z);

struct ToriRS_Task*
CreateTask_Dat2MapSceneryLoad(
    struct CacheProvider* provider,
    int map_x,
    int map_z);

struct ToriRS_Task*
CreateTask_Dat2LocLoad(
    struct CacheProvider* provider,
    int loc_id);

struct ToriRS_Task*
CreateTask_Dat2FlotypeLoad(
    struct CacheProvider* provider,
    int flo_id);

struct ToriRS_Task*
CreateTask_Dat2UnderlayLoad(
    struct CacheProvider* provider,
    int underlay_id);

struct ToriRS_Task*
CreateTask_Dat2TextureLoad(
    struct CacheProvider* provider,
    int texture_id);

struct ToriRS_Task*
CreateTask_Dat2SpriteLoad(
    struct CacheProvider* provider,
    int sprite_id);

struct ToriRS_Task*
CreateTask_Dat2SpriteLoadByName(
    struct CacheProvider* provider,
    char const* archive_name);

struct ToriRS_Task*
CreateTask_Dat2FontLoad(
    struct CacheProvider* provider,
    int font_id);

struct ToriRS_Task*
CreateTask_Dat2EnumLoad(
    struct CacheProvider* provider,
    int enum_id);

struct ToriRS_Task*
CreateTask_Dat2StructLoad(
    struct CacheProvider* provider,
    int struct_id);

/**
 * Load every varbit type into `varps`, once, at boot.
 *
 * Whole-group and eager, unlike the per-id lazy loaders around it:
 * VarPManager_SetVarbitTypes takes the whole table at once, and a varbit read happens
 * deep inside script execution where there is nowhere to yield to a load.
 */
struct VarPManager;
struct ToriRS_Task*
CreateTask_Dat2VarbitLoad(
    struct CacheProvider* provider,
    struct VarPManager* varps);

/**
 * Load every varplayer type into `varps`, once, at boot.
 *
 * The client reads exactly one field off these records — `clientcode`, which
 * marks a varp as driving built-in client behaviour (sound volume, the Controls
 * panel's Attack options). Without the table every clientcode reads 0 and none
 * of that behaviour can fire. Must run BEFORE CreateTask_Dat2VarbitLoad's
 * install, since SetVarpTypes reallocates the var value arrays.
 */
struct ToriRS_Task*
CreateTask_Dat2VarpLoad(
    struct CacheProvider* provider,
    struct VarPManager* varps);

/*
 * Hitsplat types (config group 32), whole-group and eager.
 *
 * At OldSchool revisions the splat graphic is a plain sprite id held in a
 * hitsplat config record, not a frame of a named "hitmarks" archive — so
 * without this the overlay draws the damage number with nothing behind it.
 */
struct RS_Hitsplats;
struct ToriRS_Task*
CreateTask_Dat2HitsplatLoad(
    struct CacheProvider* provider,
    struct RS_Hitsplats* hitsplats);

/*
 * WorldEntityConfig types (config group 72 — sailing boats, deob class387),
 * whole-group and eager. OldSchool 239+ only; an absent group leaves the
 * table empty, which is the pre-sailing world rather than an error.
 */
struct WevConfigTable;
struct ToriRS_Task*
CreateTask_Dat2WevConfigLoad(
    struct CacheProvider* provider,
    struct WevConfigTable* table);

/*
 * Healthbar types (config group 33), whole-group and eager.
 *
 * The overhead bar's pixel width, its fill denominator and its fade are all in
 * this record; without it every bar falls back to the reference constructor's
 * 30-wide rectangle, which is right only for the standard bar.
 */
struct RS_Healthbars;
struct ToriRS_Task*
CreateTask_Dat2HealthbarLoad(
    struct CacheProvider* provider,
    struct RS_Healthbars* healthbars);

/*
 * Ambient soundscapes (config group 15), whole-group and eager.
 *
 * An OldSchool 231+ type; a cache without the group leaves the table empty,
 * which the audio layer reads as "AMBIENTSOUND_START ids are sound-effect ids
 * on this revision".
 */
struct RS_Soundscapes;
struct ToriRS_Task*
CreateTask_Dat2SoundscapeLoad(
    struct CacheProvider* provider,
    struct RS_Soundscapes* soundscapes);

struct ToriRS_Task*
CreateTask_Dat2ParamLoad(
    struct CacheProvider* provider,
    int param_id);

/** Load one inventory type capacity from config group 5. An absent record is
 *  installed as a cached size of zero. */
struct ToriRS_Task*
CreateTask_Dat2InvtypeLoad(
    struct CacheProvider* provider,
    int inv_id);

struct ToriRS_Task*
CreateTask_Dat2DbRowLoad(
    struct CacheProvider* provider,
    int row_id);

struct ToriRS_Task*
CreateTask_Dat2DbTableLoad(
    struct CacheProvider* provider,
    int table_id);

struct ToriRS_Task*
CreateTask_Dat2DbTableIndexLoad(
    struct CacheProvider* provider,
    int table_id);

struct ToriRS_Task*
CreateTask_Dat2ComponentLoad(
    struct CacheProvider* provider,
    int packed_component_id);

struct ToriRS_Task*
CreateTask_Dat2WorldMapLoad(struct CacheProvider* provider);

struct ToriRS_Task*
CreateTask_Dat2WorldMapGeographyLoad(
    struct CacheProvider* provider,
    int key,
    struct ToriRS_WorldMapRegionSource const* sources,
    int source_count);

struct ToriRS_Task*
CreateTask_Dat2MapElementLoad(
    struct CacheProvider* provider,
    int element_id);

/**
 * Load a music track (or jingle) and everything it needs to play: the packed
 * MIDI, the instrument patches its manifest names, and the samples those
 * patches' used notes reference. See task_dat2_music_load.c.
 */
struct ToriRS_Task*
CreateTask_Dat2MusicLoad(
    struct CacheProvider* provider,
    struct ToriRS_MusicPlayer* player,
    int song_id,
    int source);

#endif
