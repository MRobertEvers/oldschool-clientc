# cachelib-js

Native Node.js bindings for OSRS cache library.

## Installation

```bash
npm install
# or
npm run build
```

## Usage

```javascript
const { CacheLib, CACHE_MODE_DAT1, CACHE_MODE_DAT2 } = require('cachelib-js');

// Open a DAT2 cache
const cache = new CacheLib({
  mode: CACHE_MODE_DAT2,
  directory: '/path/to/cache'
});

// Load an archive (returns a Node Buffer)
const archive = cache.loadArchive(tableId, archiveId);

console.log(`Archive size: ${archive.length} bytes`);

// Free resources when done
cache.free();
```

## Cache Modes

- `CACHE_MODE_DAT1` (0): Old RuneScape cache format (.dat)
- `CACHE_MODE_DAT2` (1): Newer RuneScape cache format (.dat2)

## Example

Run the included example script:

```bash
node example.js /path/to/cache dat2
```

## API

### `new CacheLib(options)`

Creates a new cache instance.

**Parameters:**
- `options.mode` (number): Cache mode (`CACHE_MODE_DAT1` or `CACHE_MODE_DAT2`)
- `options.directory` (string): Path to the cache directory

**Throws:** Error if cache initialization fails

### `cache.loadArchive(tableId, archiveId, flags = 0)`

Loads an archive from the cache and returns its contents as a Node Buffer.

**Parameters:**
- `tableId` (number): Table ID
- `archiveId` (number): Archive ID within the table
- `flags` (number, optional): Load flags (default: 0)

**Returns:** Buffer containing the archive data

**Throws:** Error if archive loading fails

### `cache.free()`

Frees native resources. The cache instance cannot be used after calling this method.
Calling `free()` multiple times is safe (idempotent).

## Building from Source

Requirements:
- Node.js and npm
- Python (for node-gyp)
- C compiler (gcc, clang, or MSVC)

```bash
npm install
```

## License

ISC
