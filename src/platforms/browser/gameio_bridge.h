/**
 * GameIO Bridge - C++ interface to JavaScript GameIO
 * 
 * This header provides C++ functions to interact with the JavaScript GameIO
 * instance from Emscripten/WASM code.
 * 
 * Usage:
 *   #include "gameio_bridge.h"
 *   
 *   // Request an archive
 *   int requestId = gameio_request_archive(5, 0);
 *   
 *   // Check status
 *   const char* status = gameio_get_request_status(requestId);
 *   
 *   // Get data when ready
 *   if (strcmp(status, "completed") == 0) {
 *       int size = gameio_get_archive_data_size(5, 0);
 *       char* buffer = malloc(size);
 *       int copied = gameio_copy_archive_data_to_heap(5, 0, buffer, size);
 *       // Use buffer...
 *       free(buffer);
 *       gameio_clear_archive_data(5, 0);
 *   }
 */

#ifndef GAMEIO_BRIDGE_H
#define GAMEIO_BRIDGE_H

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Request status enum (matches TypeScript RequestStatus)
 */
typedef enum {
    GAMEIO_STATUS_PENDING = 0,
    GAMEIO_STATUS_QUEUED = 1,
    GAMEIO_STATUS_SENDING = 2,
    GAMEIO_STATUS_RECEIVING = 3,
    GAMEIO_STATUS_COMPLETED = 4,
    GAMEIO_STATUS_ERROR = 5,
    GAMEIO_STATUS_CACHED = 6,
    GAMEIO_STATUS_UNKNOWN = 99
} GameIOStatus;

/**
 * Request an archive from the server
 * 
 * @param tableId Cache table ID (0-254 for normal, 255 for reference tables)
 * @param archiveId Archive ID within the table
 * @return Request ID (>0), 0 if cached, -1 on error
 */
static inline int gameio_request_archive(int tableId, int archiveId) {
    return EM_ASM_INT({
        if (!window.GameIOAPI) {
            console.error("GameIO API not available");
            return -1;
        }
        return window.GameIOAPI.requestArchive($0, $1);
    }, tableId, archiveId);
}

/**
 * Get request status as string
 * 
 * @param requestId Request ID returned from gameio_request_archive
 * @return Status string: "pending", "queued", "sending", "receiving", 
 *         "completed", "error", "cached", or "unknown"
 * 
 * Note: Returned pointer is temporary and should be copied if needed
 */
static inline const char* gameio_get_request_status(int requestId) {
    static char statusBuffer[32];
    EM_ASM({
        if (!window.GameIOAPI) {
            stringToUTF8("unknown", $1, 32);
            return;
        }
        const status = window.GameIOAPI.getRequestStatus($0);
        stringToUTF8(status || "unknown", $1, 32);
    }, requestId, statusBuffer);
    return statusBuffer;
}

/**
 * Check if a specific archive request is pending or in progress
 * 
 * @param tableId Cache table ID
 * @param archiveId Archive ID
 * @return true if pending, false otherwise
 */
static inline bool gameio_is_request_pending(int tableId, int archiveId) {
    return EM_ASM_INT({
        if (!window.GameIOAPI) return 0;
        return window.GameIOAPI.isRequestPending($0, $1) ? 1 : 0;
    }, tableId, archiveId);
}

/**
 * Get current queue length
 * 
 * @return Number of requests in queue
 */
static inline int gameio_get_queue_length() {
    return EM_ASM_INT({
        if (!window.GameIOAPI) return 0;
        return window.GameIOAPI.getQueueLength();
    });
}

/**
 * Check if GameIO is connected to server
 * 
 * @return true if connected, false otherwise
 */
static inline bool gameio_is_connected() {
    return EM_ASM_INT({
        if (!window.GameIOAPI) return 0;
        return window.GameIOAPI.isConnected() ? 1 : 0;
    });
}

/**
 * Get size of archive data in bytes
 * 
 * @param tableId Cache table ID
 * @param archiveId Archive ID
 * @return Size in bytes, or 0 if not available
 */
static inline int gameio_get_archive_data_size(int tableId, int archiveId) {
    return EM_ASM_INT({
        if (!window.GameIOAPI) return 0;
        return window.GameIOAPI.getArchiveDataSize($0, $1);
    }, tableId, archiveId);
}

/**
 * Copy archive data from JavaScript to WASM heap
 * 
 * @param tableId Cache table ID
 * @param archiveId Archive ID
 * @param heapPtr Pointer to WASM heap memory
 * @param maxSize Maximum size to copy
 * @return Number of bytes copied, or 0 on error
 * 
 * Note: Caller must allocate heapPtr with sufficient size
 */
static inline int gameio_copy_archive_data_to_heap(int tableId, int archiveId, 
                                                     void* heapPtr, int maxSize) {
    return EM_ASM_INT({
        if (!window.GameIOAPI) return 0;
        return window.GameIOAPI.copyArchiveDataToHeap($0, $1, $2, $3);
    }, tableId, archiveId, heapPtr, maxSize);
}

/**
 * Clear archive data from JavaScript memory
 * Should be called after copying data to free memory
 * 
 * @param tableId Cache table ID
 * @param archiveId Archive ID
 */
static inline void gameio_clear_archive_data(int tableId, int archiveId) {
    EM_ASM({
        if (!window.GameIOAPI) return;
        window.GameIOAPI.clearArchiveData($0, $1);
    }, tableId, archiveId);
}

/**
 * Print current request info to console (for debugging)
 */
static inline void gameio_print_current_request() {
    EM_ASM({
        if (!window.GameIOAPI) {
            console.log("GameIO API not available");
            return;
        }
        const current = window.GameIOAPI.getCurrentRequest();
        if (current) {
            console.log("Current request:", current);
        } else {
            console.log("No current request");
        }
    });
}

/**
 * Print all active requests to console (for debugging)
 */
static inline void gameio_print_active_requests() {
    EM_ASM({
        if (!window.GameIOAPI) {
            console.log("GameIO API not available");
            return;
        }
        const active = window.GameIOAPI.getActiveRequests();
        console.log(`Active requests (${active.length}):`, active);
    });
}

#ifdef __cplusplus
}
#endif

#endif // __EMSCRIPTEN__

#endif // GAMEIO_BRIDGE_H

