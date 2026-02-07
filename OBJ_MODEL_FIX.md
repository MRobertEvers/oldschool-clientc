# Object Model Loading Fix

## The Problem

Grey circles were displayed instead of proper item sprites because the wrong model loading logic was being used.

## Root Cause Analysis

### Wrong Approach (What I Was Doing)
I was copying the logic from `entity_scenebuild.c`'s `obj_model()` function, which:
- Uses `obj->manwear`, `obj->manwear2`, `obj->manwear3`
- Merges multiple models together
- Applies recoloring and transformations
- **This is for WORN EQUIPMENT** (when a player wears the item)

### Correct Approach (ObjType.getInvModel)
From `ObjType.ts` lines 342-368:

```typescript
getInvModel(count: number): Model | null {
    // Handle count variations...
    
    const model = Model.tryGet(this.model);  // ← Just get the model directly!
    if (!model) {
        return null;
    }

    if (this.recol_s && this.recol_d) {
        for (let i: number = 0; i < this.recol_s.length; i++) {
            model.recolour(this.recol_s[i], this.recol_d[i]);
        }
    }

    return model;  // No scaling, no normals calculation
}
```

**Key differences:**
1. Uses `obj->model` field **directly** (not manwear fields)
2. Only applies recoloring (if specified)
3. **NO scaling** (`resizex/y/z`)
4. **NO normal calculation** (`calculateNormals`)
5. **NO merging** - it's a single model

## The Fix

### Updated `get_obj_inv_model()` function:

```c
static struct CacheModel*
get_obj_inv_model(
    struct GGame* game,
    struct CacheDatConfigObj* obj)
{
    // For inventory, use the base model field directly
    if( obj->model == 0 || obj->model == -1 )
    {
        return NULL;
    }

    struct CacheModel* model = buildcachedat_get_model(game->buildcachedat, obj->model);
    if( !model )
    {
        printf("get_obj_inv_model: Could not load model %d\n", obj->model);
        return NULL;
    }

    return model;
}
```

## Verification from Lua Scripts

From `lua_scripts.h` line 508-512:

```c
if( obj->model != -1 )
{
    idx++;
    lua_pushinteger(L, obj->model);  // ← Returns obj->model for inventory
    lua_rawseti(L, -2, idx);
}
```

This confirms `obj->model` is the correct field for inventory items.

## Model Field Usage Summary

| Field | Purpose | Function |
|-------|---------|----------|
| `obj->model` | **Inventory icon & ground model** | `getInvModel()`, `getIcon()` |
| `obj->manwear` | Worn equipment (body) | `getWornModel()` |
| `obj->manwear2` | Worn equipment (arms) | `getWornModel()` |
| `obj->manwear3` | Worn equipment (hands) | `getWornModel()` |
| `obj->manhead` | Worn equipment (head) | `getHeadModel()` |

## Next Steps

The model should now load correctly. However, the rendering still creates placeholder circles because:
1. We're extracting color from `model->face_colors[0]`
2. Drawing a circular gradient manually

To display actual item sprites, we need to:
1. Render the 3D model to a 32x32 buffer
2. Apply rotation (`xan2d`, `yan2d`, `zan2d`)
3. Apply zoom (`zoom2d`)
4. Apply offsets (`xof2d`, `yof2d`)
5. Add outline/shadow post-processing

This matches the full `ObjType.getIcon()` implementation in Client.ts lines 403-524.
