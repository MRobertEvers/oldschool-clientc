#ifndef RSCACHE_RSCACHESHARED_XTEA_H
#define RSCACHE_RSCACHESHARED_XTEA_H

#include <stdint.h>

/**
 * @brief xtea keys are 16 bytes long. Decrypts in place.
 *
 * @param data
 * @param data_length
 * @param key
 */
void RSCacheShared_XteaDecrypt(char* data, int data_length, int32_t* key);

#endif // RSCACHE_SHARED_XTEA_H