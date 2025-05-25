#ifndef XTEA_H
#define XTEA_H

#include <stdint.h>

// Load XTEA keys from a JSON file
// Returns number of keys loaded or -1 on error
int load_xtea_keys(const char* filename);

// Decrypt data using XTEA
// Returns malloc'd buffer with decrypted data or NULL on error
// out_length will be set to the length of the decrypted data
char* decrypt_xtea_data(const char* data, int data_length, int archive, int group, int* out_length);

// Clean up XTEA key storage
void cleanup_xtea_keys(void);

#endif // XTEA_H