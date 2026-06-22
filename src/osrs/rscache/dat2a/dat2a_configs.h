#ifndef RSCACHE_RSCACHEDAT2A_CONFIGS_H
#define RSCACHE_RSCACHEDAT2A_CONFIGS_H

enum RSCacheDat2A_ConfigKind
{
    // From Runelite
    // UNDERLAY(1),
    // 	IDENTKIT(3),
    // 	OVERLAY(4),
    // 	INV(5),
    // 	OBJECT(6),
    // 	ENUM(8),
    // 	NPC(9),
    // 	ITEM(10),
    // 	PARAMS(11),
    // 	SEQUENCE(12),
    // 	SPOTANIM(13),
    // 	VARBIT(14),
    // 	VARCLIENT(19),
    // 	VARCLIENTSTRING(15),
    // 	VARPLAYER(16),
    // 	HITSPLAT(32),
    // 	HEALTHBAR(33),
    // 	STRUCT(34),
    // 	AREA(35),
    // 	DBROW(38),
    // 	DBTABLE(39);
    RSCacheDat2A_ConfigKind_Underlay = 1,
    RSCacheDat2A_ConfigKind_Identkit = 3,
    RSCacheDat2A_ConfigKind_Overlay = 4,
    RSCacheDat2A_ConfigKind_Inv = 5,
    // Runelite calls this "object", RSCacheDat2A_ConfigKind_Object
    // This is really closer to "scenery"
    RSCacheDat2A_ConfigKind_Locs = 6,
    RSCacheDat2A_ConfigKind_Enum = 8,
    RSCacheDat2A_ConfigKind_Npc = 9,
    // Runelite calls this "item", CONFIG_ITEM
    RSCacheDat2A_ConfigKind_Object = 10,
    RSCacheDat2A_ConfigKind_Params = 11,
    RSCacheDat2A_ConfigKind_Sequence = 12,
    RSCacheDat2A_ConfigKind_Spotanim = 13,
    RSCacheDat2A_ConfigKind_Varbit = 14,
    RSCacheDat2A_ConfigKind_Varclient = 19,
    RSCacheDat2A_ConfigKind_VarclientString = 15,
    RSCacheDat2A_ConfigKind_Varplayer = 16,
    RSCacheDat2A_ConfigKind_Hitsplat = 32,
    RSCacheDat2A_ConfigKind_Healthbar = 33,
    RSCacheDat2A_ConfigKind_Struct = 34,
    RSCacheDat2A_ConfigKind_Area = 35,
    RSCacheDat2A_ConfigKind_Dbrow = 38,
    RSCacheDat2A_ConfigKind_Dbtable = 39
};

#endif