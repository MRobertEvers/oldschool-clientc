#ifndef RSCACHE_DAT1DISK_H
#define RSCACHE_DAT1DISK_H

#include "archive.h"

#include <stdio.h>

enum RSCache_Dat1DiskTable
{
    RSCACHE_DAT1_DISK_TABLE_CONFIGS = 0,
    RSCACHE_DAT1_DISK_TABLE_MODELS = 1,
    RSCACHE_DAT1_DISK_TABLE_ANIMATIONS = 2,
    RSCACHE_DAT1_DISK_TABLE_SOUNDS = 3,
    RSCACHE_DAT1_DISK_TABLE_MAPS = 4,
};

enum RSCache_Dat1ConfigKind
{
    RSCACHE_DAT1_CONFIG_TITLE_AND_FONTS = 1,
    RSCACHE_DAT1_CONFIG_CONFIGS = 2,
    RSCACHE_DAT1_CONFIG_INTERFACES = 3,
    RSCACHE_DAT1_CONFIG_MEDIA_2D = 4,
    RSCACHE_DAT1_CONFIG_VERSION_LIST = 5,
    RSCACHE_DAT1_CONFIG_TEXTURES = 6,
    RSCACHE_DAT1_CONFIG_CHAT_SYSTEM = 7,
    RSCACHE_DAT1_CONFIG_SOUND_EFFECTS = 8,
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
 * Archive: RSCACHE_DAT1_CONFIG_TITLE_AND_FONTS
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
 * Archive: RSCACHE_DAT1_CONFIG_CONFIGS
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
 * Archive: RSCACHE_DAT1_CONFIG_TEXTURES
 * Files:
 * // Seen in "Pix3D.unpackTextures" in LostCity JavaClient
 * - "0.dat" - "49.dat"
 * - "index.dat"
 *
 * Some known files seen in "OnDemand.java"
 * This archive behaves much like the ArchiveReferenceTables in Dat2.
 * Table: CONFIG
 * Archive: RSCACHE_DAT1_CONFIG_VERSION_LIST
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
 * Archive: RSCACHE_DAT1_CONFIG_INTERFACES
 * Files:
 * // Seen in "RSCacheDat2A_Component.unpack" in LostCity JavaClient
 * // This appears to be the only file in this archive.
 * // Note: NO ".dat" extension.
 * - "data"
 *
 * Table: CONFIG
 * Archive: RSCACHE_DAT1_CONFIG_MEDIA_2D
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
 * Archive: RSCACHE_DAT1_CONFIG_SOUND_EFFECTS
 * Files:
 * // This appears to be the only file in this archive.
 * // Seen in Client.load
 * - "sounds.dat"
 */

struct RSCache_MapSquares;

struct RSCache_Dat1Disk
{
    char* directory;
    FILE* dat_file;
    struct RSCache_MapSquares* map_squares;
};

struct RSCache_Dat1Disk*
RSCache_Dat1DiskNewFromDirectory(char const* path);

void
RSCache_Dat1DiskFree(struct RSCache_Dat1Disk* cache);

struct RSCache_Dat1DiskArchive
{
    char* data;
    int data_size;
    int archive_id;
    int table_id;
    int revision;
    int file_count;
    enum RSCache_ArchiveFormat format;
};

struct RSCache_Dat1DiskArchive*
RSCache_Dat1DiskArchiveNewLoad(
    struct RSCache_Dat1Disk* cache,
    int table_id,
    int archive_id);

void
RSCache_Dat1DiskArchiveFree(struct RSCache_Dat1DiskArchive* archive);

/**
 * Append `data` into `main_file_cache.dat` and point `main_file_cache.idx<table_id>`
 * at the new sectors. Mirrors RSCache_Dat2DiskWriteArchive but uses the dat1
 * filename and passes `table_id + 1` as the sector-header index id (dat1's
 * off-by-one).
 *
 * For tables 1–4, `data` is typically raw gzip bytes (RSCACHE_GZIP_NO_FOOTER
 * compatible); for table 0 it is plain jagfile bytes. Returns 0 on success.
 */
int
RSCache_Dat1DiskWriteArchive(
    const char* directory,
    int table_id,
    int archive_id,
    const uint8_t* data,
    int data_size);

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

#endif
