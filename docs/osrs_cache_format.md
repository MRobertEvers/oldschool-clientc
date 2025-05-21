# OSRS Cache Format

## Files

```
- main_file_cache.dat2

Contains all the actual data in the cache.
The other files are simply indexes into this file.

- main_file_cache.idx255

A special index that describes the other tables.

The entries referenced by this indexed are
1. "IndexData" in RuneLite
2. "ReferenceTable" in OpenRS


- main_file_cache.idx0-N

These files contain information such as models, animations, config etc.

These tables each have their own entry structure.

Each of these tables has a corresponding entry "ReferenceTable" indexed by index 255.
```

## main_file_cache.idx255 - Reference Tables

References records of the form.

```
struct ReferenceTableEntry
{
    int index;
    int identifier;
    int crc;
    int hash;
    unsigned char whirlpool[64];
    int compressed;
    int uncompressed;
    int version;
    struct
    {
        int* entries;
        int count;
    } children;
};

struct ReferenceTable {
    int format;
    int version;
    int flags;
    int id_count;
    int* ids; // entries is a sparse array. ids is the list of indices that are valid.
    struct ReferenceTableEntry* entries;
    int entry_count;
}
```

##
