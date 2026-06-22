#include "platforms/platform_x/cachelib.h"
#include "platforms/platform_x/cachelib_internal.h"
#include "platforms/platform_x/cachelib_platform.h"
#include "platforms/platform_x/cachelib_serialized.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "osrs/rscache/dat1disk/dat1disk.h"
#include <node_api.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// Native CacheLib wrapper
typedef struct
{
    struct RSCacheDat2DiskLib* cache;
    char* directory;
    int mode;
    bool is_freed;
} CacheLibWrapper;

// Helper macros for NAPI error checking
#define NAPI_CALL(env, call)                                                                       \
    do                                                                                             \
    {                                                                                              \
        napi_status status = (call);                                                               \
        if( status != napi_ok )                                                                    \
        {                                                                                          \
            return NULL;                                                                           \
        }                                                                                          \
    } while( 0 )

#define NAPI_CALL_RETURN_VOID(env, call)                                                           \
    do                                                                                             \
    {                                                                                              \
        napi_status status = (call);                                                               \
        if( status != napi_ok )                                                                    \
        {                                                                                          \
            return;                                                                                \
        }                                                                                          \
    } while( 0 )

// Throw a JS error
static void
throw_error(
    napi_env env,
    const char* msg)
{
    napi_throw_error(env, NULL, msg);
}

// Throw a type error
static void
throw_type_error(
    napi_env env,
    const char* msg)
{
    napi_throw_type_error(env, NULL, msg);
}

// Finalizer callback
static void
cachelib_finalizer(
    napi_env env,
    void* finalize_data,
    void* finalize_hint)
{
    CacheLibWrapper* wrapper = (CacheLibWrapper*)finalize_data;
    if( wrapper && !wrapper->is_freed && wrapper->cache )
    {
        cachelib_free(wrapper->cache);
        wrapper->cache = NULL;
    }
    if( wrapper && wrapper->directory )
    {
        free(wrapper->directory);
        wrapper->directory = NULL;
    }
    free(wrapper);
}

// Constructor
static napi_value
cachelib_constructor(
    napi_env env,
    napi_callback_info info)
{
    napi_value jsthis;
    size_t argc = 1;
    napi_value args[1];

    NAPI_CALL(env, napi_get_cb_info(env, info, &argc, args, &jsthis, NULL));

    if( argc < 1 )
    {
        throw_type_error(env, "Expected options object as first argument");
        return NULL;
    }

    // Get mode property
    napi_value mode_val;
    NAPI_CALL(env, napi_get_named_property(env, args[0], "mode", &mode_val));

    int32_t mode;
    NAPI_CALL(env, napi_get_value_int32(env, mode_val, &mode));

    if( mode != CACHE_MODE_DAT1 && mode != CACHE_MODE_DAT2 )
    {
        throw_error(env, "Invalid cache mode. Must be CACHE_MODE_DAT1 or CACHE_MODE_DAT2");
        return NULL;
    }

    // Get directory property
    napi_value directory_val;
    NAPI_CALL(env, napi_get_named_property(env, args[0], "directory", &directory_val));

    size_t directory_len;
    NAPI_CALL(env, napi_get_value_string_utf8(env, directory_val, NULL, 0, &directory_len));

    char* directory = malloc(directory_len + 1);
    if( !directory )
    {
        throw_error(env, "Failed to allocate memory for directory");
        return NULL;
    }

    NAPI_CALL(
        env,
        napi_get_value_string_utf8(
            env, directory_val, directory, directory_len + 1, &directory_len));

    // Create cachelib
    struct RSCacheDat2DiskLib* cache = cachelib_new(mode);
    if( !cache )
    {
        free(directory);
        throw_error(env, "Failed to create cache");
        return NULL;
    }

    // Initialize cache with directory
    int init_result = cachelib_platform_init(cache, directory);
    if( init_result != 1 )
    {
        cachelib_free(cache);
        free(directory);
        throw_error(env, "Failed to initialize cache from directory");
        return NULL;
    }

    // Create wrapper
    CacheLibWrapper* wrapper = malloc(sizeof(CacheLibWrapper));
    if( !wrapper )
    {
        cachelib_free(cache);
        free(directory);
        throw_error(env, "Failed to allocate wrapper");
        return NULL;
    }

    wrapper->cache = cache;
    wrapper->directory = directory;
    wrapper->mode = mode;
    wrapper->is_freed = false;

    // Wrap the native instance
    NAPI_CALL(env, napi_wrap(env, jsthis, wrapper, cachelib_finalizer, NULL, NULL));

    return jsthis;
}

// loadArchive method
static napi_value
cachelib_load_archive(
    napi_env env,
    napi_callback_info info)
{
    napi_value jsthis;
    size_t argc = 3;
    napi_value args[3];

    NAPI_CALL(env, napi_get_cb_info(env, info, &argc, args, &jsthis, NULL));

    if( argc < 2 )
    {
        throw_type_error(env, "Expected tableId and archiveId as arguments");
        return NULL;
    }

    // Get wrapper
    CacheLibWrapper* wrapper;
    NAPI_CALL(env, napi_unwrap(env, jsthis, (void**)&wrapper));

    if( !wrapper || wrapper->is_freed || !wrapper->cache )
    {
        throw_error(env, "Cache has been freed");
        return NULL;
    }

    // Get arguments
    int32_t table_id, archive_id, flags = 0;
    NAPI_CALL(env, napi_get_value_int32(env, args[0], &table_id));
    NAPI_CALL(env, napi_get_value_int32(env, args[1], &archive_id));

    if( argc >= 3 )
    {
        NAPI_CALL(env, napi_get_value_int32(env, args[2], &flags));
    }

    // Build IO request
    struct RSCacheDat2DiskLib_IORequest request;
    request.table_id = table_id;
    request.archive_id = archive_id;
    request.flags = flags;

    // Load archive using platform abstraction
    void* archive_ptr = cachelib_platform_load_io(wrapper->cache, &request);

    if( !archive_ptr )
    {
        throw_error(env, "Failed to load archive");
        return NULL;
    }

    // Extract data based on mode
    void* archive_data = NULL;
    int archive_data_size = 0;

    if( wrapper->mode == CACHE_MODE_DAT1 )
    {
        struct RSCacheDat1Disk_Archive* archive = (struct RSCacheDat1Disk_Archive*)archive_ptr;
        archive_data = archive->data;
        archive_data_size = archive->data_size;
    }
    else if( wrapper->mode == CACHE_MODE_DAT2 )
    {
        struct RSCacheDat2Disk_Archive* archive = (struct RSCacheDat2Disk_Archive*)archive_ptr;
        archive_data = archive->data;
        archive_data_size = archive->data_size;
    }
    else
    {
        throw_error(env, "Unknown cache mode");
        return NULL;
    }

    // Copy data to Node Buffer
    napi_value buffer;
    void* buffer_data;
    NAPI_CALL(
        env, napi_create_buffer_copy(env, archive_data_size, archive_data, &buffer_data, &buffer));

    // Free the archive using the correct function for the mode
    if( wrapper->mode == CACHE_MODE_DAT1 )
    {
        RSCacheDat1Disk_ArchiveFree((struct RSCacheDat1Disk_Archive*)archive_ptr);
    }
    else if( wrapper->mode == CACHE_MODE_DAT2 )
    {
        RSCacheDat2Disk_ArchiveFree((struct RSCacheDat2Disk_Archive*)archive_ptr);
    }

    return buffer;
}

// loadArchiveSerialized method
static napi_value
cachelib_load_archive_serialized(
    napi_env env,
    napi_callback_info info)
{
    napi_value jsthis;
    size_t argc = 3;
    napi_value args[3];

    NAPI_CALL(env, napi_get_cb_info(env, info, &argc, args, &jsthis, NULL));

    if( argc < 2 )
    {
        throw_type_error(env, "Expected tableId and archiveId as arguments");
        return NULL;
    }

    // Get wrapper
    CacheLibWrapper* wrapper;
    NAPI_CALL(env, napi_unwrap(env, jsthis, (void**)&wrapper));

    if( !wrapper || wrapper->is_freed || !wrapper->cache )
    {
        throw_error(env, "Cache has been freed");
        return NULL;
    }

    // Get arguments
    int32_t table_id, archive_id, flags = 0;
    NAPI_CALL(env, napi_get_value_int32(env, args[0], &table_id));
    NAPI_CALL(env, napi_get_value_int32(env, args[1], &archive_id));

    if( argc >= 3 )
    {
        NAPI_CALL(env, napi_get_value_int32(env, args[2], &flags));
    }

    // Build IO request
    struct RSCacheDat2DiskLib_IORequest request;
    request.table_id = table_id;
    request.archive_id = archive_id;
    request.flags = flags;

    // Load archive using platform abstraction
    void* archive_ptr = cachelib_platform_load_io(wrapper->cache, &request);

    if( !archive_ptr )
    {
        throw_error(env, "Failed to load archive");
        return NULL;
    }

    // Serialize based on mode
    int serialized_size = 0;
    void* temp_buffer = NULL;
    napi_value buffer = NULL;
    void* buffer_data = NULL;

    if( wrapper->mode == CACHE_MODE_DAT1 )
    {
        struct RSCacheDat1Disk_Archive* archive = (struct RSCacheDat1Disk_Archive*)archive_ptr;
        serialized_size = cachelib_cache_dat_archive_serialized_size(archive);
        
        if( serialized_size < 0 )
        {
            RSCacheDat1Disk_ArchiveFree(archive);
            throw_error(env, "Failed to compute serialized size");
            return NULL;
        }

        temp_buffer = malloc(serialized_size);
        if( !temp_buffer )
        {
            RSCacheDat1Disk_ArchiveFree(archive);
            throw_error(env, "Failed to allocate serialization buffer");
            return NULL;
        }

        int written = cachelib_cache_dat_archive_serialize_to_buffer(archive, temp_buffer, serialized_size);
        if( written != serialized_size )
        {
            free(temp_buffer);
            RSCacheDat1Disk_ArchiveFree(archive);
            throw_error(env, "Failed to serialize archive");
            return NULL;
        }

        NAPI_CALL(env, napi_create_buffer_copy(env, serialized_size, temp_buffer, &buffer_data, &buffer));
        free(temp_buffer);
        RSCacheDat1Disk_ArchiveFree(archive);
    }
    else if( wrapper->mode == CACHE_MODE_DAT2 )
    {
        struct RSCacheDat2Disk_Archive* archive = (struct RSCacheDat2Disk_Archive*)archive_ptr;
        serialized_size = cachelib_cache_archive_serialized_size(archive);
        
        if( serialized_size < 0 )
        {
            RSCacheDat2Disk_ArchiveFree(archive);
            throw_error(env, "Failed to compute serialized size");
            return NULL;
        }

        temp_buffer = malloc(serialized_size);
        if( !temp_buffer )
        {
            RSCacheDat2Disk_ArchiveFree(archive);
            throw_error(env, "Failed to allocate serialization buffer");
            return NULL;
        }

        int written = cachelib_cache_archive_serialize_to_buffer(archive, temp_buffer, serialized_size);
        if( written != serialized_size )
        {
            free(temp_buffer);
            RSCacheDat2Disk_ArchiveFree(archive);
            throw_error(env, "Failed to serialize archive");
            return NULL;
        }

        NAPI_CALL(env, napi_create_buffer_copy(env, serialized_size, temp_buffer, &buffer_data, &buffer));
        free(temp_buffer);
        RSCacheDat2Disk_ArchiveFree(archive);
    }
    else
    {
        throw_error(env, "Unknown cache mode");
        return NULL;
    }

    return buffer;
}

// free method
static napi_value
cachelib_free_method(
    napi_env env,
    napi_callback_info info)
{
    napi_value jsthis;
    NAPI_CALL(env, napi_get_cb_info(env, info, NULL, NULL, &jsthis, NULL));

    CacheLibWrapper* wrapper;
    NAPI_CALL(env, napi_unwrap(env, jsthis, (void**)&wrapper));

    if( wrapper && !wrapper->is_freed && wrapper->cache )
    {
        cachelib_free(wrapper->cache);
        wrapper->cache = NULL;
        wrapper->is_freed = true;
    }

    return NULL;
}

// Initialize the addon
static napi_value
init_addon(
    napi_env env,
    napi_value exports)
{
    napi_value cons;

    napi_property_descriptor properties[] = {
        { "loadArchive",           NULL, cachelib_load_archive,            NULL, NULL, NULL, napi_default, NULL },
        { "loadArchiveSerialized", NULL, cachelib_load_archive_serialized, NULL, NULL, NULL, napi_default, NULL },
        { "free",                  NULL, cachelib_free_method,             NULL, NULL, NULL, napi_default, NULL }
    };

    NAPI_CALL(
        env,
        napi_define_class(
            env, "CacheLib", NAPI_AUTO_LENGTH, cachelib_constructor, NULL, 3, properties, &cons));

    NAPI_CALL(env, napi_set_named_property(env, exports, "CacheLib", cons));

    return exports;
}

NAPI_MODULE(
    NODE_GYP_MODULE_NAME,
    init_addon)
