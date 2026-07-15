#ifndef RSCACHE_RSCACHEDAT1A_CONFIGSDAT_H
#define RSCACHE_RSCACHEDAT1A_CONFIGSDAT_H

// static readonly DAT = {
//     title: 1,
//     configs: 2,
//     interfaces: 3,
//     media: 4,
//     versionList: 5,
//     textures: 6,
// };

enum RSCacheDat1A_ConfigKind
{
    RSCacheDat1A_ConfigKind_TitleAndFonts = 1,
    RSCacheDat1A_ConfigKind_Configs = 2,
    RSCacheDat1A_ConfigKind_Interfaces = 3,
    RSCacheDat1A_ConfigKind_Media2dGraphics = 4,
    RSCacheDat1A_ConfigKind_VersionList = 5,
    RSCacheDat1A_ConfigKind_Textures = 6,
    RSCacheDat1A_ConfigKind_ChatSystem = 7,
    RSCacheDat1A_ConfigKind_SoundEffects = 8,
};

#endif