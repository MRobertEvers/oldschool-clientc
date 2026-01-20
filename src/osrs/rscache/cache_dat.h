#ifndef CACHE_DAT_H
#define CACHE_DAT_H

#include "archive_decompress.h"

#include <stdio.h>
#include <stdlib.h>

enum CacheDatTable
{
    CACHE_DAT_CONFIGS = 0,
    CACHE_DAT_MODELS = 1,
    CACHE_DAT_ANIMATIONS = 2,
    CACHE_DAT_SOUNDS = 3,
    CACHE_DAT_MAPS = 4,
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
 * Archive: CONFIG_DAT_TITLE_AND_FONTS
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
 * Archive: CONFIG_DAT_CONFIGS
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
 *
 * Table: CONFIG
 * Archive: CONFIG_DAT_TEXTURES
 * Files:
 * // Seen in "Pix3D.unpackTextures" in LostCity JavaClient
 * - "0.dat" - "49.dat"
 * - "index.dat"
 *
 * Some known files seen in "OnDemand.java"
 * This archive behaves much like the ArchiveReferenceTables in Dat2.
 * Table: CONFIG
 * Archive: CONFIG_DAT_VERSION_LIST
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
 * Archive: CONFIG_DAT_INTERFACES
 * Files:
 * // Seen in "Component.unpack" in LostCity JavaClient
 * // This appears to be the only file in this archive.
 * // Note: NO ".dat" extension.
 * - "data"
 *
 * Table: CONFIG
 * Archive: CONFIG_DAT_MEDIA_2D_GRAPHICS
 * Files:
 * // Seen in Pix32 in JavaClient
 * // the name comes from the Component object parsed from the interfaces
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
 * Archive: CONFIG_DAT_SOUND_EFFECTS
 * Files:
 * // This appears to be the only file in this archive.
 * // Seen in Client.load
 * - "sounds.dat"
 *
 * @param path
 * @return struct CacheDat*
 */
struct CacheMapSquares;
struct CacheDat
{
    char const* directory;

    FILE* _dat_file;

    struct CacheMapSquares* map_squares;
};

struct CacheDat*
cache_dat_new(char const* path);

void
cache_dat_free(struct CacheDat* cache_dat);

struct CacheDatArchive
{
    char* data;
    int data_size;

    int archive_id;
    int table_id;
    int revision;

    int file_count;

    enum ArchiveFormat format;
};

struct CacheDatArchive*
cache_dat_archive_new_load(
    struct CacheDat* cache_dat,
    int table_id,
    int archive_id);

void
cache_dat_archive_free(struct CacheDatArchive* archive);

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