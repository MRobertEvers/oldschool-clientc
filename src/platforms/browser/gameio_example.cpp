/**
 * GameIO Bridge Usage Example
 * 
 * This example shows how to use the GameIO bridge from C++ in Emscripten.
 */

#include "gameio_bridge.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/**
 * Example 1: Simple archive request with polling
 */
void example_simple_request() {
    printf("=== Example 1: Simple Archive Request ===\n");
    
    // Request map archive (table 5, archive 0)
    int requestId = gameio_request_archive(5, 0);
    
    if (requestId < 0) {
        printf("Failed to request archive\n");
        return;
    }
    
    if (requestId == 0) {
        printf("Archive was cached!\n");
        // Data is immediately available
        int size = gameio_get_archive_data_size(5, 0);
        printf("Archive size: %d bytes\n", size);
        
        // Allocate buffer and copy data
        char* buffer = (char*)malloc(size);
        int copied = gameio_copy_archive_data_to_heap(5, 0, buffer, size);
        printf("Copied %d bytes to buffer\n", copied);
        
        // Use the data...
        printf("First 16 bytes (hex):");
        for (int i = 0; i < 16 && i < copied; i++) {
            printf(" %02x", (unsigned char)buffer[i]);
        }
        printf("\n");
        
        // Clean up
        free(buffer);
        gameio_clear_archive_data(5, 0);
    } else {
        printf("Request ID: %d (not cached, will download)\n", requestId);
        printf("Check status with gameio_get_request_status(%d)\n", requestId);
    }
}

/**
 * Example 2: Check request status
 */
void example_check_status() {
    printf("\n=== Example 2: Check Request Status ===\n");
    
    // Request an archive
    int requestId = gameio_request_archive(5, 1);
    printf("Request ID: %d\n", requestId);
    
    if (requestId > 0) {
        // Poll status (in real code, you'd do this in your game loop)
        const char* status = gameio_get_request_status(requestId);
        printf("Status: %s\n", status);
        
        // Check if request is still pending
        bool isPending = gameio_is_request_pending(5, 1);
        printf("Is pending: %s\n", isPending ? "yes" : "no");
    }
}

/**
 * Example 3: Monitor queue and connection
 */
void example_monitor_queue() {
    printf("\n=== Example 3: Monitor Queue ===\n");
    
    // Check connection
    bool connected = gameio_is_connected();
    printf("Connected: %s\n", connected ? "yes" : "no");
    
    // Check queue length
    int queueLen = gameio_get_queue_length();
    printf("Queue length: %d\n", queueLen);
    
    // Print debug info
    gameio_print_current_request();
    gameio_print_active_requests();
}

/**
 * Example 4: Request multiple archives
 */
void example_batch_request() {
    printf("\n=== Example 4: Batch Request ===\n");
    
    const int NUM_REQUESTS = 5;
    int requestIds[NUM_REQUESTS];
    
    // Request multiple archives
    for (int i = 0; i < NUM_REQUESTS; i++) {
        requestIds[i] = gameio_request_archive(5, i);
        printf("Request %d: ID=%d\n", i, requestIds[i]);
    }
    
    printf("Queue length after batch: %d\n", gameio_get_queue_length());
}

/**
 * Example 5: Request reference table
 */
void example_reference_table() {
    printf("\n=== Example 5: Reference Table ===\n");
    
    // Request reference table for table 5 (maps)
    // Reference tables are in table 255
    int requestId = gameio_request_archive(255, 5);
    printf("Reference table request ID: %d\n", requestId);
    
    if (requestId == 0) {
        // Cached
        int size = gameio_get_archive_data_size(255, 5);
        printf("Reference table size: %d bytes\n", size);
    } else if (requestId > 0) {
        printf("Reference table will be downloaded\n");
    }
}

/**
 * Example 6: Complete workflow with data processing
 */
void example_complete_workflow(int tableId, int archiveId) {
    printf("\n=== Example 6: Complete Workflow ===\n");
    printf("Requesting table=%d, archive=%d\n", tableId, archiveId);
    
    // 1. Request the archive
    int requestId = gameio_request_archive(tableId, archiveId);
    
    if (requestId < 0) {
        printf("ERROR: Failed to request archive\n");
        return;
    }
    
    // 2. Check if it was cached
    if (requestId == 0) {
        printf("Archive was in cache\n");
        process_archive_data(tableId, archiveId);
        return;
    }
    
    // 3. Wait for download (in real code, poll in game loop)
    printf("Archive is downloading, request ID: %d\n", requestId);
    printf("In your game loop, check:\n");
    printf("  const char* status = gameio_get_request_status(%d);\n", requestId);
    printf("  if (strcmp(status, \"completed\") == 0) {\n");
    printf("    process_archive_data(%d, %d);\n", tableId, archiveId);
    printf("  }\n");
}

/**
 * Helper function to process archive data
 */
void process_archive_data(int tableId, int archiveId) {
    // Get size
    int size = gameio_get_archive_data_size(tableId, archiveId);
    if (size == 0) {
        printf("No data available yet\n");
        return;
    }
    
    printf("Processing archive: %d bytes\n", size);
    
    // Allocate buffer
    char* buffer = (char*)malloc(size);
    if (!buffer) {
        printf("Failed to allocate memory\n");
        return;
    }
    
    // Copy data from JS to C++
    int copied = gameio_copy_archive_data_to_heap(tableId, archiveId, buffer, size);
    printf("Copied %d bytes to C++ heap\n", copied);
    
    // Process the data...
    // For example, parse it, decompress it, etc.
    printf("Processing data...\n");
    
    // Show first few bytes
    printf("First 8 bytes:");
    for (int i = 0; i < 8 && i < copied; i++) {
        printf(" %02x", (unsigned char)buffer[i]);
    }
    printf("\n");
    
    // Clean up
    free(buffer);
    
    // Clear from JS memory
    gameio_clear_archive_data(tableId, archiveId);
    printf("Cleaned up memory\n");
}

/**
 * Game loop integration example
 */
struct PendingRequest {
    int requestId;
    int tableId;
    int archiveId;
    bool processed;
};

#define MAX_PENDING_REQUESTS 10
static PendingRequest pendingRequests[MAX_PENDING_REQUESTS];
static int numPendingRequests = 0;

void request_archive_async(int tableId, int archiveId) {
    int requestId = gameio_request_archive(tableId, archiveId);
    
    if (requestId == 0) {
        // Cached, process immediately
        process_archive_data(tableId, archiveId);
    } else if (requestId > 0 && numPendingRequests < MAX_PENDING_REQUESTS) {
        // Add to pending list
        pendingRequests[numPendingRequests].requestId = requestId;
        pendingRequests[numPendingRequests].tableId = tableId;
        pendingRequests[numPendingRequests].archiveId = archiveId;
        pendingRequests[numPendingRequests].processed = false;
        numPendingRequests++;
    }
}

void update_pending_requests() {
    for (int i = 0; i < numPendingRequests; i++) {
        if (pendingRequests[i].processed) {
            continue;
        }
        
        const char* status = gameio_get_request_status(pendingRequests[i].requestId);
        
        if (strcmp(status, "completed") == 0) {
            // Request completed, process the data
            process_archive_data(
                pendingRequests[i].tableId,
                pendingRequests[i].archiveId
            );
            pendingRequests[i].processed = true;
        } else if (strcmp(status, "error") == 0) {
            // Request failed
            printf("Request %d failed\n", pendingRequests[i].requestId);
            pendingRequests[i].processed = true;
        }
    }
}

void example_game_loop_integration() {
    printf("\n=== Example 7: Game Loop Integration ===\n");
    
    // Request some archives asynchronously
    request_archive_async(5, 0);
    request_archive_async(5, 1);
    request_archive_async(7, 100); // Model
    
    printf("Requested 3 archives asynchronously\n");
    printf("In your game loop, call update_pending_requests()\n");
    
    // Simulate game loop
    update_pending_requests();
}

/**
 * Main function (for testing)
 */
#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// Export function so it can be called from JavaScript
extern "C" {
    void EMSCRIPTEN_KEEPALIVE run_gameio_examples() {
        printf("========================================\n");
        printf("GameIO Bridge Examples\n");
        printf("========================================\n");
        
        example_simple_request();
        example_check_status();
        example_monitor_queue();
        example_batch_request();
        example_reference_table();
        example_complete_workflow(5, 0);
        example_game_loop_integration();
        
        printf("\n========================================\n");
        printf("Examples complete!\n");
        printf("========================================\n");
    }
}
#endif

int main() {
    #ifdef __EMSCRIPTEN__
    printf("Compiled for Emscripten\n");
    printf("Call Module._run_gameio_examples() from JavaScript\n");
    #else
    printf("This example requires Emscripten\n");
    #endif
    return 0;
}

