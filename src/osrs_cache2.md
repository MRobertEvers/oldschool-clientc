# OSRS Cache Format and Usage

## Overview

The OSRS cache is a structured data storage system used by Old School RuneScape to store game assets and data. It consists of several key files that work together to organize and access the game's content.

## Core Files

### main_file_cache.dat2

- The primary data file containing all actual game data
- Contains compressed and uncompressed data blocks
- Data is organized into sectors of 520 bytes each
- Supports multiple compression formats:
  - No compression (type 0)
  - BZip2 compression (type 1)
  - GZip compression (type 2)

### main_file_cache.idx255

- Special index file that describes all other tables
- Known as "ReferenceTable" in OpenRS or "IndexData" in RuneLite
- Contains metadata about other cache tables including:
  - Format version
  - Entry counts
  - Compression flags
  - CRC checksums
  - Child entry information

### main_file_cache.idx0-N

- Individual index files for different types of game data
- Each index file points to specific data in the .dat2 file
- Common indexes include:
  - idx0: Animations
  - idx1: Skeletons
  - idx2: Configs
  - idx7: Models

## Cache Structure Diagram

```
main_file_cache.idx255 (Reference Table)
    │
    ├───┐
    │   │
    │   ├───> main_file_cache.idx0 (Animations)
    │   │       │
    │   │       └───> main_file_cache.dat2
    │   │               │
    │   │               ├───> Animation Data Blocks
    │   │               │
    │   │               └───> ...
    │   │
    │   ├───> main_file_cache.idx1 (Skeletons)
    │   │       │
    │   │       └───> main_file_cache.dat2
    │   │               │
    │   │               ├───> Skeleton Data Blocks
    │   │               │
    │   │               └───> ...
    │   │
    │   ├───> main_file_cache.idx2 (Configs)
    │   │       │
    │   │       └───> main_file_cache.dat2
    │   │               │
    │   │               ├───> Config Data Blocks
    │   │               │
    │   │               └───> ...
    │   │
    │   └───> ... (Other Index Files)
    │           │
    │           └───> main_file_cache.dat2
    │                   │
    │                   ├───> Various Data Blocks
    │                   │
    │                   └───> ...
    │
    └───┘

Legend:
    │   └───> Points to data location
    ├───> Index relationship
    └───┘   Grouping
```

Each index file (idx0-N) contains records that point to specific sectors in the .dat2 file. The idx255 file acts as a master index, containing metadata about all other index files and their contents. When reading data:

1. First, consult idx255 to get metadata about the desired index
2. Then use the appropriate idx file to locate the data in .dat2
3. Finally, read and decompress the data from .dat2

## Archive Layouts

The cache supports two main types of archive layouts:

### Single-File Archives

Some archives (like models) contain a single file of data. These are simpler in structure:

```
Archive (e.g., Model)
    │
    └───> Single Data Block
            │
            ├───> Compression Type (1 byte)
            ├───> Compressed Length (4 bytes)
            ├───> Uncompressed Length (4 bytes, if compressed)
            └───> Data Content
```

Example: Model archives (idx7) typically contain a single model file with vertex data, face indices, and texture information.

### Multi-File Archives

Other archives (like configs) can contain multiple files. These use a more complex structure:

```
Archive (e.g., Config)
    │
    ├───> File 1
    │       │
    │       ├───> File Name Hash
    │       └───> File Data
    │
    ├───> File 2
    │       │
    │       ├───> File Name Hash
    │       └───> File Data
    │
    └───> ... (More Files)
            │
            ├───> File Name Hash
            └───> File Data
```

The multi-file archive structure includes:

1. A header containing the number of files
2. For each file:
   - File name hash (identifier)
   - File size information
   - Actual file data

### Reading Multi-File Archives

When reading a multi-file archive:

1. Read the number of files from the archive header
2. For each file:
   - Read the file name hash
   - Read the file size information
   - Read and decompress the file data
3. The files are typically stored in a packed format where:
   - File sizes are delta-encoded
   - Data is stored in chunks
   - Each chunk's size is stored at the end of the archive

## Data Structure

### Index Records

Each index file contains fixed-size records (6 bytes) with the following structure:

```c
struct IndexRecord {
    int idx_file_id;    // The file extension (e.g., 2 := idx2)
    int record_id;      // Unique identifier for the record
    int sector;         // Starting sector in .dat2 file
    int length;         // Length of the data
};
```

### Reference Table

The reference table (idx255) contains detailed metadata about each archive:

```c
struct ReferenceTableEntry {
    int index;          // Index identifier
    int identifier;     // Additional identifier
    int crc;           // CRC32 checksum
    int hash;          // Hash value
    unsigned char whirlpool[64];  // Whirlpool hash
    int compressed;     // Compressed size
    int uncompressed;   // Uncompressed size
    int version;        // Version number
    struct {
        int* entries;   // Child entries
        int count;      // Number of children
    } children;
};
```

## Usage

### Reading Data

1. Open the required index file (idx0-N)
2. Read the index record for the desired data
3. Use the sector and length information to locate data in the .dat2 file
4. Decompress the data if necessary
5. Parse the data according to its type (model, animation, etc.)

### Common Data Types

#### Models (idx7)

- Contains 3D model data
- Includes vertex positions, face indices, textures, and colors
- Supports both old and new format models
- Includes features like:
  - Face render priorities
  - Transparency
  - Texture coordinates
  - Vertex groups

#### NPCs (idx9)

- Contains NPC definitions
- Includes:
  - Model IDs
  - Animations
  - Combat stats
  - Interaction options
  - Visual properties

#### Animations (idx0)

- Contains animation sequences
- Includes:
  - Frame data
  - Timing information
  - Transformation data

## Entity Types

The cache uses specific bit masks to identify different entity types:

- Player: 0x0000_0000
- NPC: 0x2000_0000
- Location: 0x4000_0000
- Object Stack: 0x6000_0000

## Notes

- All data in the .dat2 file is aligned to sector boundaries
- Compression is handled per-archive
- Some data may be encrypted (e.g., map data using XTEA)
- The cache format has evolved over time, with different versions supporting different features
