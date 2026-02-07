# Direct Model Loading Solution

## Problem

The Lua script approach didn't work because:
1. `luaL_dofile()` from C++ can't use async I/O (`HostIOUtils.await()`)
2. The script needs to run in a coroutine context with the event loop
3. Models weren't being loaded before inventory rendering

## Solution

Load models **synchronously** from the cache using the direct C API in `init_example_interface()`.

## Implementation

### Key Functions Used

1. **`gioqb_cache_dat_models_new_load()`** - Reads model file from cache
   ```c
   struct CacheDatArchive* archive = 
       gioqb_cache_dat_models_new_load(cache_dat, model_id);
   ```

2. **`buildcachedat_loader_cache_model()`** - Decodes and caches the model
   ```c
   buildcachedat_loader_cache_model(
       game->buildcachedat, model_id, archive->data_size, archive->data);
   ```

### Loading Flow

```cpp
int obj_ids[] = { 1204, 994, 378, 553, 1274, 384, 4586, 2433 };

for each obj_id:
    1. Get obj config: buildcachedat_get_obj(buildcachedat, obj_id)
    2. Check if obj->model exists and isn't loaded yet
    3. Load from disk: gioqb_cache_dat_models_new_load(cache_dat, obj->model)
    4. Decode & cache: buildcachedat_loader_cache_model(...)
```

### Updated init_example_interface()

```cpp
struct CacheDat* cache_dat = game->cache_dat;
if( cache_dat )
{
    for( int i = 0; i < obj_count; i++ )
    {
        struct CacheDatConfigObj* obj = buildcachedat_get_obj(...);
        
        if( obj && obj->model != 0 )
        {
            // Check if already loaded
            if( buildcachedat_get_model(..., obj->model) == NULL )
            {
                // Load from disk
                struct CacheDatArchive* archive = 
                    gioqb_cache_dat_models_new_load(cache_dat, obj->model);
                
                if( archive && archive->data )
                {
                    // Decode and cache
                    buildcachedat_loader_cache_model(...);
                }
            }
        }
    }
}
```

## Why This Works

1. **Synchronous** - No coroutines or async I/O needed
2. **Direct cache access** - Reads files directly from `cache_dat`
3. **Same API** - Uses the same functions Lua calls under the hood
4. **Immediate** - Models are available right after this function returns

## Reference

Based on:
- `gio_cache_dat.c:87-92` - `gioqb_cache_dat_models_new_load()`
- `buildcachedat_loader.c:250-258` - `buildcachedat_loader_cache_model()`
- `pkt_player_info.lua:27-42` - Pattern for getting obj model IDs and loading

## Object IDs in Inventory

| Obj ID | Item | Model ID |
|--------|------|----------|
| 1204 | Bronze dagger | ? |
| 994 | Coins | ? |
| 378 | Lobster | ? |
| 553 | Fire runes | 2388 |
| 1274 | Rune pickaxe | 0 (needs check) |
| 384 | Sharks | 0 (needs check) |
| 4586 | Dragon scimitar | ? |
| 2433 | Prayer potion | 0 (needs check) |

Some objects may have `model = 0` which means they have no inventory model defined in the config.

## Testing

```bash
cd build && make && ./osx
```

Click "Toggle Inventory" - you should now see:
- Colored circles based on actual model face colors
- Different colors per item type
- Item count overlays
- No "model not found" errors

The models are now properly loaded from the cache files before rendering begins.
