// Example: Loading interface components from cache data

#include "osrs/buildcachedat_loader.h"
#include "osrs/rscache/tables_dat/config_component.h"
#include "osrs/gio_cache_dat.h"
#include <stdio.h>

/*
 * This example shows how to load interface components from the cache
 * Similar to how other game data (models, textures, etc.) is loaded
 */

// Add this function to buildcachedat_loader.c or create a new loader file

void
buildcachedat_loader_load_interfaces(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    void* data,
    int data_size)
{
    printf("Loading interface components...\n");
    
    // Decode the component list from cache data
    struct CacheDatConfigComponentList* component_list =
        cache_dat_config_component_list_new_decode(data, data_size);
    
    if (!component_list) {
        printf("Failed to decode component list\n");
        return;
    }
    
    printf("Loaded %d components\n", component_list->components_count);
    
    // Register each component in the buildcachedat
    for (int i = 0; i < component_list->components_count; i++) {
        struct CacheDatConfigComponent* component = component_list->components[i];
        
        if (component) {
            printf("  Component %d: type=%d, layer=%d, width=%d, height=%d\n",
                   component->id,
                   component->type,
                   component->layer,
                   component->width,
                   component->height);
            
            buildcachedat_add_component(buildcachedat, component->id, component);
            
            // Load sprites referenced by this component
            if (component->type == COMPONENT_TYPE_GRAPHIC) {
                if (component->graphic && strlen(component->graphic) > 0) {
                    // TODO: Load the sprite file and register it
                    // This would require:
                    // 1. Parse the sprite name/path from component->graphic
                    // 2. Load the sprite from cache (similar to load_sprite_pix32)
                    // 3. buildcachedat_add_component_sprite(buildcachedat, name, sprite)
                    printf("    Need to load sprite: %s\n", component->graphic);
                }
                
                if (component->activeGraphic && strlen(component->activeGraphic) > 0) {
                    printf("    Need to load active sprite: %s\n", component->activeGraphic);
                }
            }
            
            // Load sprites for inventory slot graphics
            if (component->type == COMPONENT_TYPE_INV && component->invSlotGraphic) {
                for (int j = 0; j < 20; j++) {
                    if (component->invSlotGraphic[j]) {
                        printf("    Need to load inv slot sprite: %s\n", 
                               component->invSlotGraphic[j]);
                    }
                }
            }
        }
    }
    
    // Note: Don't free component_list or components, they're now owned by buildcachedat
    free(component_list->components);
    free(component_list);
}

// Example: Loading a specific interface file from cache
void
load_interface_from_cache_example(struct GGame* game)
{
    /*
     * In the real implementation, you would:
     * 
     * 1. Load the interface.dat file from cache archive
     *    (similar to how media.dat, models, etc. are loaded)
     * 
     * 2. Pass the data to buildcachedat_loader_load_interfaces()
     * 
     * Example with GIO system:
     */
    
    // Create a GIO request to load the interface file
    // struct GIORequest request;
    // request.kind = GIO_REQUEST_CACHE_DAT_FILE;
    // request.path = "interface.dat";  // Or whatever the file is called
    // request.callback = on_interface_loaded;
    // gio_queue_push(game->io, &request);
}

// Callback when interface data is loaded
void
on_interface_loaded(
    struct GGame* game,
    void* data,
    int data_size,
    void* user_data)
{
    if (!data || data_size == 0) {
        printf("Failed to load interface data\n");
        return;
    }
    
    // Load the components
    buildcachedat_loader_load_interfaces(
        game->buildcachedat,
        game,
        data,
        data_size);
    
    printf("Interface components loaded successfully\n");
    
    // Now you can display interfaces by setting their IDs
    // game->viewport_interface_id = some_component_id;
}

/*
 * SPRITE LOADING HELPER
 * 
 * Once you know the sprite name from a component, load it like this:
 */
void
load_component_sprite_example(
    struct GGame* game,
    struct FileListDat* filelist,
    const char* sprite_name)
{
    // Example: sprite_name might be "button,0" meaning button.dat index 0
    // Parse the name and index
    char filename[256];
    int sprite_idx = 0;
    
    // Simple parsing (you may need more sophisticated logic)
    if (sscanf(sprite_name, "%[^,],%d", filename, &sprite_idx) == 2) {
        // Name with index
        strcat(filename, ".dat");
    } else {
        // Just a name, assume index 0
        snprintf(filename, sizeof(filename), "%s.dat", sprite_name);
        sprite_idx = 0;
    }
    
    printf("Loading sprite: %s index %d\n", filename, sprite_idx);
    
    // Find index.dat
    int index_file_idx = filelist_dat_find_file_by_name(filelist, "index.dat");
    if (index_file_idx == -1) {
        printf("Failed to find index.dat\n");
        return;
    }
    
    // Try loading as pix32 first
    struct CacheDatPix32* pix32 = cache_dat_pix32_decode_from_archive(
        filelist->files[filelist_dat_find_file_by_name(filelist, filename)],
        filelist->file_sizes[filelist_dat_find_file_by_name(filelist, filename)],
        sprite_idx);
    
    if (pix32) {
        struct DashSprite* sprite = dashsprite_new_from_cache_pix32(pix32);
        buildcachedat_add_component_sprite(game->buildcachedat, sprite_name, sprite);
        cache_dat_pix32_free(pix32);
        printf("Loaded pix32 sprite: %s\n", sprite_name);
        return;
    }
    
    // Try loading as pix8
    struct CacheDatPix8Palette* pix8_palette = cache_dat_pix8_palette_decode_from_archive(
        filelist->files[filelist_dat_find_file_by_name(filelist, filename)],
        filelist->file_sizes[filelist_dat_find_file_by_name(filelist, filename)],
        sprite_idx);
    
    if (pix8_palette) {
        struct DashSprite* sprite = dashsprite_new_from_cache_pix8_palette(pix8_palette);
        buildcachedat_add_component_sprite(game->buildcachedat, sprite_name, sprite);
        cache_dat_pix8_palette_free(pix8_palette);
        printf("Loaded pix8 sprite: %s\n", sprite_name);
        return;
    }
    
    printf("Failed to load sprite: %s\n", sprite_name);
}

/*
 * FULL INTEGRATION EXAMPLE
 * 
 * Add this to your game initialization sequence:
 */
void
init_interfaces_full_example(struct GGame* game)
{
    // 1. Load the interface definitions file
    // This would be done via GIO in the real implementation
    // For now, assume we have the data:
    // void* interface_data = ...;
    // int interface_data_size = ...;
    
    // 2. Load all components
    // buildcachedat_loader_load_interfaces(
    //     game->buildcachedat,
    //     game,
    //     interface_data,
    //     interface_data_size);
    
    // 3. Load the media archive (this already exists in buildcachedat_loader.c)
    // This loads sprites like compass, invback, etc.
    
    // 4. For each component that references a sprite, load that sprite
    // This would iterate through all components and load their graphics
    
    // 5. Now interfaces are ready to use!
    // Display an interface by setting its ID:
    // game->viewport_interface_id = 123;
}

/*
 * CACHE FILE STRUCTURE (for reference)
 * 
 * The interface data is typically stored in one of these formats:
 * 
 * Option 1: Single file with all components
 *   - File: interface.dat
 *   - Format: Binary with component count + component data
 *   - Decode: cache_dat_config_component_list_new_decode()
 * 
 * Option 2: Multiple files, one per interface
 *   - Files: interface_0.dat, interface_1.dat, etc.
 *   - Format: Binary component data
 *   - Decode: One component per file
 * 
 * Option 3: Archive with indexed components
 *   - Archive: interface.jag or interface archive
 *   - Format: JAG archive with component files
 *   - Decode: Extract each file, decode component
 * 
 * Check your cache254 directory structure to determine which format is used.
 */

/*
 * DEBUGGING TIPS
 * 
 * 1. Print component info:
 *    - Print all loaded component IDs
 *    - Print component hierarchy (parent/child relationships)
 *    - Print component bounds (x, y, width, height)
 * 
 * 2. Test with simple components first:
 *    - Start with a single RECT component
 *    - Add a TEXT component
 *    - Then try a GRAPHIC component
 *    - Finally try LAYER with children
 * 
 * 3. Verify clipping bounds:
 *    - Print clip_left/top/right/bottom before rendering
 *    - Ensure bounds are set correctly for nested components
 * 
 * 4. Check sprite loading:
 *    - Verify sprite is loaded: buildcachedat_get_component_sprite()
 *    - Check sprite dimensions match expected values
 *    - Verify sprite pixels are not all zero/transparent
 */
