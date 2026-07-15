#ifndef RSCACHE_RSCACHEDAT1DISK_H
#define RSCACHE_RSCACHEDAT1DISK_H

#include "../shared/shared_archive_decompress.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

enum RSCacheDat1Disk_Table
{
    RSCacheDat1Disk_Table_Configs = 0,
    RSCacheDat1Disk_Table_Models = 1,
    RSCacheDat1Disk_Table_Animations = 2,
    RSCacheDat1Disk_Table_Sounds = 3,
    RSCacheDat1Disk_Table_Maps = 4,
};

/**
 * Cache Dat has 5 tables.
 *
 * The Config table contains "JagFiles" (LostCity naming).
 * JagFiles are like our FileLists.
 *
 * The other tables contain single blob archives.
 *
 * In the above code, 'this.getJagFile' loads the archive in the
 * second argument from the CONFIG table.
 *
 * The "OnDemand" class in later versions of LostCity JavaClient is responsible for loading the
 * archives of tables NOT the config table. It uses the version list to load archives.
 *
 * The "OnDemand" class consults the "Version List" and then loads
 * the archive requested.
 *
 * Table: CONFIG
 * Archive: RSCacheDat1A_ConfigKind_TitleAndFonts
 * Files:
 * - "index.dat"
 * // Seen in loadTitleBackground in LostCity JavaClient
 * - "title.dat"
 * - "logo.dat"
 * // Seen in loadTitleImages
 * - "titlebox.dat"
 * - "titlebutton.dat"
 * - "runes.dat"
 * // Seen in Client.load in LostCity JavaClient
 * - "p11.dat"
 * - "p12.dat"
 * - "b12.dat"
 * - "q8.dat"
 *
 * Table: CONFIG
 * Archive: RSCacheDat1A_ConfigKind_Configs
 * Files:
 * // Seen in Client.load. This appears to be all the files.
 * - "seq.dat"	// SeqType.unpack(jagConfig);
 * - "loc.dat"	// LocType.unpack(jagConfig);
 * - "loc.idx"	// LocType.unpack(jagConfig);
 * - "flo.dat"	// FloType.unpack(jagConfig);
 * - "obj.dat"	// ObjType.unpack(jagConfig);
 * - "obj.idx"	// ObjType.unpack(jagConfig);
 * - "npc.dat"	// NpcType.unpack(jagConfig);
 * - "npc.idx"	// NpcType.unpack(jagConfig);
 * - "idk.dat"	// IdkType.unpack(jagConfig);
 * - "spotanim.dat"	// SpotAnimType.unpack(jagConfig);
 * - "varp.dat"	// VarpType.unpack(jagConfig);
 * ".idx" files are lists of offsets in the corresponding ".dat" files.
 * [count, offset, offset, ...] where count is the number of elements in the list.
 * Accessing entry 0 of an indexed ".dat" file is done by reading the offset at index 0 of the
 * ".idx" file.
 * ".dat" files otherwise have no specific structure.
 *
 * Table: CONFIG
 * Archive: RSCacheDat1A_ConfigKind_Textures
 * Files:
 * // Seen in "Pix3D.unpackTextures" in LostCity JavaClient
 * - "0.dat" - "49.dat"
 * - "index.dat"
 *
 * Some known files seen in "OnDemand.java"
 * This archive behaves much like the ArchiveReferenceTables in Dat2.
 * Table: CONFIG
 * Archive: RSCacheDat1A_ConfigKind_VersionList
 * Files:
 * - "model_version"
 * - "anim_version"
 * - "midi_version"
 * - "map_version"
 * - "model_crc"
 * - "anim_crc"
 * - "midi_crc"
 * - "map_crc"
 * - "model_index"
 * - "map_index" // Contains mapSquares
 * - "anim_index"
 * - "midi_index"
 *
 * Table: CONFIG
 * Archive: RSCacheDat1A_ConfigKind_Interfaces
 * Files:
 * // Seen in "RSCacheDat2A_Component.unpack" in LostCity JavaClient
 * // This appears to be the only file in this archive.
 * // Note: NO ".dat" extension.
 * - "data"
 *
 * Table: CONFIG
 * Archive: RSCacheDat1A_ConfigKind_Media2dGraphics
 * Files:
 * // Seen in Pix32 in JavaClient
 * // the name comes from the RSCacheDat2A_Component object parsed from the interfaces
 * - <component name>.dat
 * - "index.dat"
 * - "invback.dat" // Seen in client.java
 * - "chatback.dat" // Seen in client.java
 * - "mapback.dat" // Seen in client.java
 * - "backbase1.dat"
 * - "backbase2.dat"
 * - "backhmid1.dat"
 * - "sideicons.dat"
 * - "compass.dat"
 * - "mapedge.dat"
 * - "mapscene.dat"
 * // These are the map icons like general store
 * - "mapfunction.dat"
 * - "hitmarks.dat"
 * - "headicons.dat"
 * - "cross.dat"
 * etc.
 *
 * Table: CONFIG
 * Archive: RSCacheDat1A_ConfigKind_SoundEffects
 * Files:
 * // This appears to be the only file in this archive.
 * // Seen in Client.load
 * - "sounds.dat"
 *
 * @param path
 * @return struct RSCacheDat1Disk*
 */
struct RSCacheDat1A_MapSquares;
struct RSCacheDat1Disk
{
    char const* directory;

    FILE* _dat_file;

    struct RSCacheDat1A_MapSquares* map_squares;
    // This is just because there is no way to look up an animframe by id
    // unless you unpack all the animframes up front.
    struct RSCacheDat1A_AnimBaseFrames** animbaseframes;
    int animbaseframes_count;
};

struct RSCacheDat1Disk*
RSCacheDat1Disk_NewFromDirectory(char const* path);

void
RSCacheDat1Disk_Free(struct RSCacheDat1Disk* cache_dat);

struct RSCacheDat1Disk_Archive
{
    char* data;
    int data_size;

    int archive_id;
    int table_id;
    int revision;

    int file_count;

    enum RSCacheShared_ArchiveFormat format;
};

struct RSCacheDat1Disk_Archive*
RSCacheDat1Disk_ArchiveNewLoad(
    struct RSCacheDat1Disk* cache_dat,
    int table_id,
    int archive_id);

void
RSCacheDat1Disk_ArchiveFree(struct RSCacheDat1Disk_Archive* archive);

// @ObfuscatedName("vb.a(Lyb;Lclient;)V")
// public void unpack(Jagfile jag, Client app) {
//     String[] version = new String[] { "model_version", "anim_version", "midi_version",
//     "map_version" }; for (int archive = 0; archive < 4; archive++) {
//         byte[] data = jag.read(version[archive], null);
//         int count = data.length / 2;
//         Packet buf = new Packet(data);

//         this.versions[archive] = new int[count];
//         this.priorities[archive] = new byte[count];

//         for (int file = 0; file < count; file++) {
//             this.versions[archive][file] = buf.g2();
//         }
//     }

//     String[] crc = new String[] { "model_crc", "anim_crc", "midi_crc", "map_crc" };
//     for (int archive = 0; archive < 4; archive++) {
//         byte[] data = jag.read(crc[archive], null);
//         int count = data.length / 4;
//         Packet buf = new Packet(data);

//         this.crcs[archive] = new int[count];

//         for (int file = 0; file < count; file++) {
//             this.crcs[archive][file] = buf.g4();
//         }
//     }

//     byte[] data = jag.read("model_index", null);
//     int count = this.versions[0].length;

//     this.models = new byte[count];

//     for (int file = 0; file < count; file++) {
//         if (file < data.length) {
//             this.models[file] = data[file];
//         } else {
//             this.models[file] = 0;
//         }
//     }

//     data = jag.read("map_index", null);
//     Packet buf = new Packet(data);
//     count = data.length / 7;

//     this.mapIndex = new int[count];
//     this.mapLand = new int[count];
//     this.mapLoc = new int[count];
//     this.mapMembers = new int[count];

//     for (int i = 0; i < count; i++) {
//         this.mapIndex[i] = buf.g2();
//         this.mapLand[i] = buf.g2();
//         this.mapLoc[i] = buf.g2();
//         this.mapMembers[i] = buf.g1();
//     }

//     data = jag.read("anim_index", null);
//     buf = new Packet(data);
//     count = data.length / 2;

//     this.animIndex = new int[count];

//     for (int frame = 0; frame < count; frame++) {
//         this.animIndex[frame] = buf.g2();
//     }

//     data = jag.read("midi_index", null);
//     buf = new Packet(data);
//     count = data.length;

//     this.midiIndex = new int[count];

//     for (int file = 0; file < count; file++) {
//         this.midiIndex[file] = buf.g1();
//     }

//     this.app = app;
//     this.running = true;
//     this.app.startThread(this, 2);
// }

// Jagfile jagConfig = this.getJagFile("config", 2, this.jagChecksum[2], "config", 30);
// Jagfile jagInterface = this.getJagFile("interface", 3, this.jagChecksum[3], "interface", 35);
// Jagfile jagMedia = this.getJagFile("2d graphics", 4, this.jagChecksum[4], "media", 40);
// Jagfile jagTextures = this.getJagFile("textures", 6, this.jagChecksum[6], "textures", 45);
// Jagfile jagWordenc = this.getJagFile("chat system", 7, this.jagChecksum[7], "wordenc", 50);
// Jagfile jagSounds = this.getJagFile("sound effects", 8, this.jagChecksum[8], "sounds", 55);

#ifdef __cplusplus
}
#endif

#endif