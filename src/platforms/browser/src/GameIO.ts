/**
 * GameIO - WebSocket-based archive loader with IndexedDB caching
 *
 * Protocol (matches asset_server.c):
 * Request:  [request_code(1), table(1), archive_id(4)]
 * Response: [status(4), size(4), data(size bytes)]
 *
 * PUBLIC API (called from WASM via Module.requestArchive/requestArchiveRead):
 *
 * 1. requestArchive(requestId: number, tableId: number, archiveId: number): void
 *    - Initiates an archive request with a specific request ID
 *    - First checks IndexedDB cache, then requests from server if not found
 *    - Non-blocking - returns immediately
 *
 * 2. requestArchiveRead(requestId: number): number
 *    - Checks if a request is complete and ready to read
 *    - If ready: allocates memory in WASM heap, copies data, returns pointer
 *    - If not ready: returns 0
 *    - Clears the completed request after reading
 *    - Memory format: [size(4 bytes, big endian), data(size bytes)]
 */

const ARCHIVE_REQUEST_MAGIC = 12345678;
function w4(heapU32: Uint32Array, ptr: number, value: number) {
  const heapU32Index = ptr / 4;
  const valueBigEndian =
    (value & 0xff) |
    (((value >> 8) & 0xff) << 8) |
    (((value >> 16) & 0xff) << 16) |
    (((value >> 24) & 0xff) << 24);
  heapU32[heapU32Index] = valueBigEndian;
}

interface JSArchiveRequest {
  requestId: number;
  tableId: number;
  archiveId: number;
  status: number;
  size: number;
  filled: number;
  dataPtr: number;
}

// request->magic = r4(data);
// request->request_id = r4(data + 4);
// request->table_id = r4(data + 8);
// request->archive_id = r4(data + 12);
// request->status = r4(data + 16);
// request->size = r4(data + 20);
// request->filled = r4(data + 24);
// request->data = (char*)r4(data + 28);
function writeJSArchiveRequest(
  heapU32: Uint32Array,
  ptr: number,
  request: JSArchiveRequest
) {
  w4(heapU32, ptr, ARCHIVE_REQUEST_MAGIC);
  w4(heapU32, ptr + 1 * 4, request.requestId);
  w4(heapU32, ptr + 2 * 4, request.tableId);
  w4(heapU32, ptr + 3 * 4, request.archiveId);
  w4(heapU32, ptr + 4 * 4, request.status);
  w4(heapU32, ptr + 5 * 4, request.size);
  w4(heapU32, ptr + 6 * 4, request.filled);
  w4(heapU32, ptr + 7 * 4, request.dataPtr);
}

interface ArchiveRequest {
  tableId: number;
  archiveId: number;
  resolve: (data: ArrayBuffer | null) => void;
  reject: (error: Error) => void;
  requestId: number;
  status: RequestStatus;
  timestamp: number;
}

export enum RequestStatus {
  Pending = "pending",
  Queued = "queued",
  Sending = "sending",
  Receiving = "receiving",
  Completed = "completed",
  Error = "error",
  Cached = "cached",
}

enum ResponseState {
  ReadingStatus,
  ReadingSize,
  ReadingData,
}

export class GameIO {
  private db: IDBDatabase | null = null;
  private assetSocket: WebSocket | null = null;
  private readonly wsUrl: string;
  private readonly dbName: string = "GameArchiveCache";
  private readonly dbVersion: number = 1;

  // Request queue
  private requestQueue: ArchiveRequest[] = [];
  private currentRequest: ArchiveRequest | null = null;

  // Response parsing state machine
  private responseState: ResponseState = ResponseState.ReadingStatus;
  private responseBuffer: ArrayBuffer = new ArrayBuffer(0);
  private responseStatus: number = 0;
  private responseSize: number = 0;
  private responseBytesRead: number = 0;

  // Request tracking
  private nextRequestId: number = 1;
  private activeRequests: Map<number, ArchiveRequest> = new Map();
  private completedRequests: Map<number, ArchiveRequest> = new Map();

  // Completed archive data storage (for requestArchiveRead)
  private archiveQueue: Map<
    number,
    {
      status: "Inflight" | "Done";
      tableId: number;
      archiveId: number;
      data: ArrayBuffer | null;
    }
  > = new Map();

  // WASM Module reference
  private wasmModule: any = null;

  constructor(wsUrl: string = "ws://localhost:8080/") {
    this.wsUrl = wsUrl;
  }

  /**
   * Set the WASM module reference for memory operations
   */
  setWasmModule(module: any): void {
    this.wasmModule = module;
    console.log("WASM module reference set on GameIO instance", {
      hasModule: !!module,
      hasMalloc: !!(module && module._malloc),
      hasHEAPU32: !!(module && module.HEAPU32),
    });
  }

  /**
   * Initialize IndexedDB and WebSocket connection
   */
  async initialize(): Promise<void> {
    await this.initializeIndexedDB();
    await this.initializeWebSocket();
  }

  /**
   * Initialize IndexedDB with object stores for each cache table
   */
  private async initializeIndexedDB(): Promise<void> {
    return new Promise((resolve, reject) => {
      const request = indexedDB.open(this.dbName, this.dbVersion);

      request.onerror = () => {
        reject(new Error("Failed to open IndexedDB"));
      };

      request.onsuccess = () => {
        this.db = request.result;
        console.log("IndexedDB initialized successfully");
        resolve();
      };

      request.onupgradeneeded = (event) => {
        const db = (event.target as IDBOpenDBRequest).result;

        // Create object stores for each cache table (0-255)
        // Table 255 is for reference tables
        if (!db.objectStoreNames.contains("archives")) {
          const objectStore = db.createObjectStore("archives", {
            keyPath: ["tableId", "archiveId"],
          });
          objectStore.createIndex("tableId", "tableId", { unique: false });
        }

        console.log("IndexedDB schema created");
      };
    });
  }

  /**
   * Initialize WebSocket connection to asset server
   */
  private async initializeWebSocket(): Promise<void> {
    return new Promise((resolve, reject) => {
      try {
        this.assetSocket = new WebSocket(this.wsUrl);
        this.assetSocket.binaryType = "arraybuffer";

        this.assetSocket.onopen = () => {
          console.log(`WebSocket connected to ${this.wsUrl}`);
          resolve();
        };

        this.assetSocket.onerror = (error) => {
          console.error("WebSocket error:", error);
          reject(new Error("WebSocket connection failed"));
        };

        this.assetSocket.onclose = () => {
          console.log("WebSocket connection closed");
          this.handleWebSocketClose();
        };

        this.assetSocket.onmessage = (event) => {
          console.log("WebSocket message received");
          this.handleWebSocketMessage(event.data);
        };
      } catch (error) {
        reject(error);
      }
    });
  }

  /**
   * Handle incoming WebSocket message (response from server)
   */
  private handleWebSocketMessage(data: ArrayBuffer): void {
    if (!this.currentRequest) {
      console.error("Received message but no current request");
      return;
    }

    // Append new data to response buffer
    const combinedBuffer = this.appendBuffers(this.responseBuffer, data);
    this.responseBuffer = combinedBuffer;

    // State machine for parsing response
    while (true) {
      console.log("Reading response state:", this.responseState);
      switch (this.responseState) {
        case ResponseState.ReadingStatus:
          if (this.responseBuffer.byteLength >= 4) {
            // Read 4-byte status (big endian)
            const statusView = new DataView(this.responseBuffer, 0, 4);
            this.responseStatus = statusView.getUint32(0, false); // false = big endian
            this.responseBuffer = this.responseBuffer.slice(4);
            this.responseState = ResponseState.ReadingSize;
          } else {
            return; // Need more data
          }
          break;

        case ResponseState.ReadingSize:
          if (this.responseBuffer.byteLength >= 4) {
            // Read 4-byte size (big endian)
            const sizeView = new DataView(this.responseBuffer, 0, 4);
            this.responseSize = sizeView.getUint32(0, false); // false = big endian
            this.responseBuffer = this.responseBuffer.slice(4);

            if (this.responseSize === 0) {
              // No data to read, complete the request
              this.completeCurrentRequest();
              return;
            }

            this.responseState = ResponseState.ReadingData;
            this.responseBytesRead = 0;
          } else {
            return; // Need more data
          }
          break;

        case ResponseState.ReadingData:
          if (this.responseBuffer.byteLength >= this.responseSize) {
            // Read archive data
            const archiveData = this.responseBuffer.slice(0, this.responseSize);
            this.responseBuffer = this.responseBuffer.slice(this.responseSize);

            // Save to IndexedDB and resolve request
            if (this.responseStatus === 1) {
              this.saveArchiveToIndexedDB(
                this.currentRequest.tableId,
                this.currentRequest.archiveId,
                archiveData
              )
                .then(() => {
                  this.currentRequest!.resolve(archiveData);
                  this.completeCurrentRequest();
                })
                .catch((error) => {
                  console.error("Failed to save to IndexedDB:", error);
                  this.currentRequest!.resolve(archiveData); // Still resolve with data
                  this.completeCurrentRequest();
                });
            } else {
              // Server returned error status
              console.error(`Server error: status=${this.responseStatus}`);
              this.currentRequest!.resolve(null);
              this.completeCurrentRequest(false);
            }
            return;
          } else {
            return; // Need more data
          }
      }
    }
  }

  /**
   * Complete current request and process next in queue
   */
  private completeCurrentRequest(success: boolean = true): void {
    if (this.currentRequest) {
      this.currentRequest.status = success
        ? RequestStatus.Completed
        : RequestStatus.Error;

      // Move to completed requests
      this.activeRequests.delete(this.currentRequest.requestId);
      this.completedRequests.set(
        this.currentRequest.requestId,
        this.currentRequest
      );

      // Keep only last 100 completed requests
      if (this.completedRequests.size > 100) {
        const firstKey = this.completedRequests.keys().next().value;
        if (firstKey !== undefined) {
          this.completedRequests.delete(firstKey);
        }
      }
    }

    this.currentRequest = null;
    this.responseState = ResponseState.ReadingStatus;
    this.responseBuffer = new ArrayBuffer(0);
    this.responseStatus = 0;
    this.responseSize = 0;
    this.responseBytesRead = 0;

    // Process next request in queue
    this.processNextRequest();
  }

  /**
   * Append two ArrayBuffers
   */
  private appendBuffers(
    buffer1: ArrayBuffer,
    buffer2: ArrayBuffer
  ): ArrayBuffer {
    const tmp = new Uint8Array(buffer1.byteLength + buffer2.byteLength);
    tmp.set(new Uint8Array(buffer1), 0);
    tmp.set(new Uint8Array(buffer2), buffer1.byteLength);
    return tmp.buffer;
  }

  /**
   * Handle WebSocket close
   */
  private handleWebSocketClose(): void {
    this.assetSocket = null;

    // Reject all pending requests
    if (this.currentRequest) {
      this.currentRequest.reject(new Error("WebSocket connection closed"));
      this.currentRequest = null;
    }

    for (const request of this.requestQueue) {
      request.reject(new Error("WebSocket connection closed"));
    }
    this.requestQueue = [];
  }

  /**
   * PUBLIC API: Request an archive from the server
   * First checks IndexedDB cache, then requests from server if not found
   * This is the main entry point called from WASM via Module.requestArchive
   */
  public async requestArchive(
    requestId: number,
    tableId: number,
    archiveId: number
  ): Promise<void> {
    console.log(
      `Javascript: requestArchive: requestId=${requestId}, tableId=${tableId}, archiveId=${archiveId}`
    );

    const status = this.archiveQueue.get(requestId)?.status;
    if (status === "Inflight") {
      console.log(`Request ${requestId}: Archive already in flight`);
      return;
    }

    this.archiveQueue.set(requestId, {
      status: "Inflight",
      tableId,
      archiveId,
      data: null,
    });

    const cached = await this.loadArchiveFromIndexedDB(tableId, archiveId);
    if (cached !== null) {
      console.log(
        `Request ${requestId}: Archive loaded from cache: table=${tableId}, archive=${archiveId}`
      );
      this.archiveQueue.set(requestId, {
        status: "Done",
        tableId,
        archiveId,
        data: cached,
      });
    } else {
      // Not in cache, request from server
      console.log(
        `Request ${requestId}: Requesting archive from server: table=${tableId}, archive=${archiveId}`
      );
      this.requestArchiveFromServerWithId(requestId, tableId, archiveId);
    }
  }

  /**
   * PUBLIC API: Read completed archive data and copy to WASM heap
   * Returns pointer to data in WASM heap, or 0 if not ready
   * Clears the request after reading
   */
  public requestArchiveRead(bufferPtr: number, requestId: number): number {
    const archiveData = this.archiveQueue.get(requestId);

    if (archiveData === undefined) {
      // Request not in queue
      return -1;
    }

    if (archiveData.status === "Inflight" || !archiveData.data) {
      return 0;
    }

    // Allocate memory in WASM heap
    if (!this.wasmModule || !this.wasmModule._malloc) {
      console.error("WASM module not initialized", {
        hasWasmModule: !!this.wasmModule,
        hasMalloc: !!(this.wasmModule && this.wasmModule._malloc),
        requestId: requestId,
      });
      this.archiveQueue.delete(requestId);
      return -1;
    }

    try {
      const ptr = this.wasmModule._malloc(archiveData.data.byteLength);

      if (!ptr) {
        console.error(
          `Request ${requestId}: Failed to allocate ${archiveData.data.byteLength} bytes in WASM heap`
        );
        this.archiveQueue.delete(requestId);
        return -1;
      }

      // Write size (4 bytes, big endian)
      // Use HEAPU32.buffer to get the underlying ArrayBuffer (HEAPU32 is already exported)
      const heap = new Uint8Array(
        this.wasmModule.HEAPU32.buffer,
        ptr,
        archiveData.data.byteLength
      );

      // Copy data
      const dataBytes = new Uint8Array(archiveData.data);
      heap.set(dataBytes);

      console.log(
        `Javascript: requestArchiveRead: requestId=${requestId}, bufferPtr=${bufferPtr}, size=${archiveData.data.byteLength}`
      );

      writeJSArchiveRequest(this.wasmModule.HEAPU32, bufferPtr, {
        requestId,
        tableId: archiveData.tableId,
        archiveId: archiveData.archiveId,
        status: 1,
        size: archiveData.data.byteLength,
        filled: 1,
        dataPtr: ptr,
      });

      // Clear the completed request
      this.archiveQueue.delete(requestId);

      return 1;
    } catch (error) {
      console.error(
        `Request ${requestId}: Failed to copy to WASM heap:`,
        error
      );
      this.archiveQueue.delete(requestId);
      return -1;
    }
  }

  /**
   * Request an archive from the server via WebSocket (legacy API)
   */
  private async requestArchiveFromServer(
    tableId: number,
    archiveId: number
  ): Promise<ArrayBuffer | null> {
    if (!this.assetSocket || this.assetSocket.readyState !== WebSocket.OPEN) {
      throw new Error("WebSocket not connected");
    }

    return new Promise((resolve, reject) => {
      const requestId = this.nextRequestId++;
      const request: ArchiveRequest = {
        tableId,
        archiveId,
        resolve,
        reject,
        requestId,
        status: RequestStatus.Pending,
        timestamp: Date.now(),
      };

      // Track the request
      this.activeRequests.set(requestId, request);

      // Add to queue
      request.status = RequestStatus.Queued;
      this.requestQueue.push(request);

      // Process if no current request
      if (!this.currentRequest) {
        this.processNextRequest();
      }
    });
  }

  /**
   * Request an archive from the server with a specific request ID
   * Used by the public requestArchive API
   */
  private requestArchiveFromServerWithId(
    requestId: number,
    tableId: number,
    archiveId: number
  ): void {
    if (!this.assetSocket || this.assetSocket.readyState !== WebSocket.OPEN) {
      console.error(`Request ${requestId}: WebSocket not connected`);
      this.archiveQueue.delete(requestId);
      return;
    }

    const request: ArchiveRequest = {
      tableId,
      archiveId,
      resolve: (data: ArrayBuffer | null) => {
        // Store in completedArchives for requestArchiveRead to pick up
        this.archiveQueue.set(requestId, {
          tableId,
          archiveId,
          data: data,
          status: "Done",
        });
      },
      reject: (error: Error) => {
        console.error(`Request ${requestId}: Error:`, error);
        this.archiveQueue.delete(requestId);
      },
      requestId,
      status: RequestStatus.Pending,
      timestamp: Date.now(),
    };

    // Track the request
    this.activeRequests.set(requestId, request);

    // Add to queue
    request.status = RequestStatus.Queued;
    this.requestQueue.push(request);

    // Process if no current request
    if (!this.currentRequest) {
      this.processNextRequest();
    }
  }

  /**
   * Process the next request in the queue
   */
  private processNextRequest(): void {
    if (this.currentRequest || this.requestQueue.length === 0) {
      return;
    }

    if (!this.assetSocket || this.assetSocket.readyState !== WebSocket.OPEN) {
      console.error("Cannot process request: WebSocket not connected");
      return;
    }

    // Take next request from queue
    this.currentRequest = this.requestQueue.shift()!;
    this.currentRequest.status = RequestStatus.Sending;

    // Build request packet: [request_code(1), table(1), archive_id(4)]
    const buffer = new ArrayBuffer(6);
    const view = new DataView(buffer);

    view.setUint8(0, 1); // request_code = 1
    view.setUint8(1, this.currentRequest.tableId); // table_id
    view.setUint32(2, this.currentRequest.archiveId, false); // archive_id (big endian)

    // Send request
    this.assetSocket.send(buffer);
    this.currentRequest.status = RequestStatus.Receiving;
    console.log(
      `Sent request: table=${this.currentRequest.tableId}, archive=${this.currentRequest.archiveId}`
    );
  }

  /**
   * Save archive to IndexedDB
   */
  private async saveArchiveToIndexedDB(
    tableId: number,
    archiveId: number,
    data: ArrayBuffer
  ): Promise<void> {
    if (!this.db) {
      throw new Error("IndexedDB not initialized");
    }

    return new Promise((resolve, reject) => {
      const transaction = this.db!.transaction(["archives"], "readwrite");
      const objectStore = transaction.objectStore("archives");

      const record = {
        tableId,
        archiveId,
        data,
        timestamp: Date.now(),
      };

      const request = objectStore.put(record);

      request.onsuccess = () => {
        console.log(
          `Saved to IndexedDB: table=${tableId}, archive=${archiveId}, size=${data.byteLength}`
        );
        resolve();
      };

      request.onerror = () => {
        reject(new Error("Failed to save to IndexedDB"));
      };
    });
  }

  /**
   * Load archive from IndexedDB
   */
  private async loadArchiveFromIndexedDB(
    tableId: number,
    archiveId: number
  ): Promise<ArrayBuffer | null> {
    console.log(
      `Loading archive from IndexedDB: table=${tableId}, archive=${archiveId}`
    );
    if (!this.db) {
      console.error("IndexedDB not initialized");
      throw new Error("IndexedDB not initialized");
    }

    return new Promise((resolve, reject) => {
      const transaction = this.db!.transaction(["archives"], "readonly");
      const objectStore = transaction.objectStore("archives");

      const request = objectStore.get([tableId, archiveId]);

      request.onsuccess = () => {
        const result = request.result;
        if (result && result.data) {
          resolve(result.data);
        } else {
          resolve(null);
        }
      };

      request.onerror = () => {
        console.error("Failed to load from IndexedDB");
        resolve(null); // Return null on error, will trigger server request
      };
    });
  }

  /**
   * Clear all cached archives from IndexedDB
   */
  async clearCache(): Promise<void> {
    if (!this.db) {
      throw new Error("IndexedDB not initialized");
    }

    return new Promise((resolve, reject) => {
      const transaction = this.db!.transaction(["archives"], "readwrite");
      const objectStore = transaction.objectStore("archives");

      const request = objectStore.clear();

      request.onsuccess = () => {
        console.log("Cache cleared");
        resolve();
      };

      request.onerror = () => {
        reject(new Error("Failed to clear cache"));
      };
    });
  }

  /**
   * Get cache statistics
   */
  async getCacheStats(): Promise<{
    count: number;
    tables: Map<number, number>;
  }> {
    if (!this.db) {
      throw new Error("IndexedDB not initialized");
    }

    return new Promise((resolve, reject) => {
      const transaction = this.db!.transaction(["archives"], "readonly");
      const objectStore = transaction.objectStore("archives");

      const request = objectStore.openCursor();
      let count = 0;
      const tables = new Map<number, number>();

      request.onsuccess = (event) => {
        const cursor = (event.target as IDBRequest).result;
        if (cursor) {
          count++;
          const tableId = cursor.value.tableId;
          tables.set(tableId, (tables.get(tableId) || 0) + 1);
          cursor.continue();
        } else {
          resolve({ count, tables });
        }
      };

      request.onerror = () => {
        reject(new Error("Failed to get cache stats"));
      };
    });
  }

  /**
   * Get status of a specific request by ID
   */
  getRequestStatus(requestId: number): RequestStatus | null {
    const active = this.activeRequests.get(requestId);
    if (active) {
      return active.status;
    }

    const completed = this.completedRequests.get(requestId);
    if (completed) {
      return completed.status;
    }

    return null;
  }

  /**
   * Get all active requests
   */
  getActiveRequests(): Array<{
    requestId: number;
    tableId: number;
    archiveId: number;
    status: RequestStatus;
    timestamp: number;
  }> {
    return Array.from(this.activeRequests.values()).map((req) => ({
      requestId: req.requestId,
      tableId: req.tableId,
      archiveId: req.archiveId,
      status: req.status,
      timestamp: req.timestamp,
    }));
  }

  /**
   * Get queue length
   */
  getQueueLength(): number {
    return this.requestQueue.length;
  }

  /**
   * Get current request info
   */
  getCurrentRequest(): {
    tableId: number;
    archiveId: number;
    status: RequestStatus;
  } | null {
    if (!this.currentRequest) {
      return null;
    }
    return {
      tableId: this.currentRequest.tableId,
      archiveId: this.currentRequest.archiveId,
      status: this.currentRequest.status,
    };
  }

  /**
   * Check if a specific archive request is pending or in progress
   */
  isRequestPending(tableId: number, archiveId: number): boolean {
    // Check current request
    if (
      this.currentRequest?.tableId === tableId &&
      this.currentRequest?.archiveId === archiveId
    ) {
      return true;
    }

    // Check queue
    return this.requestQueue.some(
      (req) => req.tableId === tableId && req.archiveId === archiveId
    );
  }

  /**
   * Get connection status
   */
  isConnected(): boolean {
    return (
      this.assetSocket !== null &&
      this.assetSocket.readyState === WebSocket.OPEN
    );
  }

  /**
   * Close connections and clean up
   */
  async close(): Promise<void> {
    if (this.assetSocket) {
      this.assetSocket.close();
      this.assetSocket = null;
    }

    if (this.db) {
      this.db.close();
      this.db = null;
    }
  }
}

export default GameIO;
