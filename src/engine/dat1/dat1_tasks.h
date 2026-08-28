#ifndef DAT1_TASKS_H
#define DAT1_TASKS_H

#include "engine/cache_provider.h"

/**
 * Load every varbit type into `varps`, once, at boot — the dat1 counterpart of
 * CreateTask_Dat2VarbitLoad. dat1 packs them all into `varbit.dat` in the config
 * jagfile rather than one file per id; the per-record shape is the same.
 */
struct VarPManager;
struct ToriRS_Task*
CreateTask_Dat1VarbitLoad(
    struct CacheProvider* provider,
    struct VarPManager* varps);

struct ToriRS_Task*
CreateTask_Dat1ModelLoad(
    struct CacheProvider* provider,
    int model_id);

struct ToriRS_Task*
CreateTask_Dat1ComponentPackLoad(
    struct CacheProvider* provider,
    int iface_id);

struct ToriRS_Task*
CreateTask_Dat1ClientScriptLoad(
    struct CacheProvider* provider,
    int script_id);

struct ToriRS_Task*
CreateTask_Dat1ObjLoad(
    struct CacheProvider* provider,
    int obj_id);

struct ToriRS_Task*
CreateTask_Dat1NpcLoad(
    struct CacheProvider* provider,
    int npc_id);

struct ToriRS_Task*
CreateTask_Dat1SpotanimLoad(
    struct CacheProvider* provider,
    int spotanim_id);

struct ToriRS_Task*
CreateTask_Dat1SoundLoad(
    struct CacheProvider* provider,
    int sound_id);

struct ToriRS_Task*
CreateTask_Dat1IdkLoad(
    struct CacheProvider* provider,
    int idk_id);

struct ToriRS_Task*
CreateTask_Dat1SpriteLoad(
    struct CacheProvider* provider,
    int sprite_id);

struct ToriRS_Task*
CreateTask_Dat1SpriteLoadByName(
    struct CacheProvider* provider,
    char const* archive_name);

struct ToriRS_Task*
CreateTask_Dat1SpriteLoadFromSource(
    struct CacheProvider* provider,
    struct CacheProviderSpriteSource const* source);

struct ToriRS_Task*
CreateTask_Dat1FontLoad(
    struct CacheProvider* provider,
    int font_id);

struct ToriRS_Task*
CreateTask_Dat1FontLoadByName(
    struct CacheProvider* provider,
    char const* font_name,
    int cache_font_id);

struct ToriRS_Task*
CreateTask_Dat1ComponentLoad(
    struct CacheProvider* provider,
    int packed_component_id);

struct ToriRS_Task*
CreateTask_Dat1MapTerrainLoad(
    struct CacheProvider* provider,
    int map_x,
    int map_z);

struct ToriRS_Task*
CreateTask_Dat1MapSceneryLoad(
    struct CacheProvider* provider,
    int map_x,
    int map_z);

struct ToriRS_Task*
CreateTask_Dat1LocLoad(
    struct CacheProvider* provider,
    int loc_id);

/** Decodes the whole flo.dat table into both the flotype and underlay caches;
 * dat1 has one floor table serving both roles. */
struct ToriRS_Task*
CreateTask_Dat1FlotypeLoad(
    struct CacheProvider* provider,
    int flo_id);

struct ToriRS_Task*
CreateTask_Dat1UnderlayLoad(
    struct CacheProvider* provider,
    int underlay_id);

struct ToriRS_Task*
CreateTask_Dat1TextureLoad(
    struct CacheProvider* provider,
    int texture_id);

struct ToriRS_Task*
CreateTask_Dat1SequenceLoad(
    struct CacheProvider* provider,
    struct ToriDraw_Scene* scene,
    int seq_id);

struct RS_PreloadTable;
struct RS_LoginReplyTable;
/**
 * Fetch the jag archives the profile's [preload:] list names, showing the
 * bar move as each is requested -- and, on a `source=ondemand` world, perform
 * the list's kind=ondemand prefetch passes (anims, flagged models, map
 * squares) the same way the reference does before its title screen. A disk
 * world skips the ondemand steps: there is no wire to warm.
 *
 * NULL when the profile names none this client holds whole -- an absent
 * list is a revision that does not preload, not an error.
 */
struct ToriRS_Task*
CreateTask_Dat1Preload(
    struct CacheProvider* provider,
    struct RS_PreloadTable const* steps,
    struct RS_LoginReplyTable const* strings,
    int on_demand);

#endif
