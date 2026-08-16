#ifndef RSCACHE_DATATYPES_DAT2_CONFIGS_H
#define RSCACHE_DATATYPES_DAT2_CONFIGS_H

enum RSCache_Dat2ConfigKind
{
    RSCACHE_DAT2_CONFIG_KIND_UNDERLAY = 1,
    RSCACHE_DAT2_CONFIG_KIND_IDENTKIT = 3,
    RSCACHE_DAT2_CONFIG_KIND_OVERLAY = 4,
    RSCACHE_DAT2_CONFIG_KIND_INV = 5,
    /** Scenery / locs in RuneLite naming. */
    RSCACHE_DAT2_CONFIG_KIND_LOCS = 6,
    RSCACHE_DAT2_CONFIG_KIND_ENUM = 8,
    RSCACHE_DAT2_CONFIG_KIND_NPC = 9,
    /** Items in RuneLite naming. */
    RSCACHE_DAT2_CONFIG_KIND_OBJECT = 10,
    RSCACHE_DAT2_CONFIG_KIND_PARAMS = 11,
    RSCACHE_DAT2_CONFIG_KIND_SEQUENCE = 12,
    RSCACHE_DAT2_CONFIG_KIND_SPOTANIM = 13,
    RSCACHE_DAT2_CONFIG_KIND_VARBIT = 14,
    /**
     * Group 15 is era-dependent, and the two readings are nothing like each
     * other.
     *
     * In OldSchool it is the ambient soundscape type (`dat2_config_soundscape`),
     * added somewhere in 231..239: `cache.osrs239` holds eight records averaging
     * 58 bytes, `cache.osrs230` has no group 15 at all. The varclient-string
     * reading comes from the config-kind numbering other games use, and is kept
     * only because callers name it; nothing decodes it. Telling them apart by
     * shape is easy if it is ever needed -- a varclient-string group has
     * hundreds of one-byte records, this one has a handful of long ones.
     */
    RSCACHE_DAT2_CONFIG_KIND_SOUNDSCAPE = 15,
    RSCACHE_DAT2_CONFIG_KIND_VARCLIENT_STRING = 15,
    RSCACHE_DAT2_CONFIG_KIND_VARPLAYER = 16,
    RSCACHE_DAT2_CONFIG_KIND_VARCLIENT = 19,
    /**
     * Group 32 is era-dependent:
     *   OSRS — hitsplat
     *   RS2  — BasType / render animations (rs-map-viewer ConfigType.RS2.bas)
     * Same numeric id; the open cache decides which records live there.
     */
    RSCACHE_DAT2_CONFIG_KIND_HITSPLAT = 32,
    RSCACHE_DAT2_CONFIG_KIND_BAS = 32,
    RSCACHE_DAT2_CONFIG_KIND_HEALTHBAR = 33,
    RSCACHE_DAT2_CONFIG_KIND_STRUCT = 34,
    /** "mapFunctions" / map element config — what the MEC_* scripts read. */
    RSCACHE_DAT2_CONFIG_KIND_AREA = 35,
    RSCACHE_DAT2_CONFIG_KIND_DBROW = 38,
    RSCACHE_DAT2_CONFIG_KIND_DBTABLE = 39
};

#endif
