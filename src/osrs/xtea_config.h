#ifndef XTEA_CONFIG_H
#define XTEA_CONFIG_H

#include <stdint.h>

// Load XTEA keys from a JSON file
// Returns number of keys loaded or -1 on error
int xtea_config_load_keys(const char* filename);

/**
 * The archive should always be 5. The only index with encrypted entries is the map index. Index 5.
 *
 * Group X refers the the Xth archive in the Map Index.
 */
int32_t* xtea_config_find_key(int archive, int group);

// Clean up XTEA key storage
void xtea_config_cleanup_keys(void);

#endif