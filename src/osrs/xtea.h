#ifndef XTEA_H
#define XTEA_H

#include <stdint.h>

/**
 * @brief xtea keys are 16 bytes long. Decrypts in place.
 *
 * @param data
 * @param data_length
 * @param key
 */
void xtea_decrypt(char* data, int data_length, int32_t* key);

#endif // XTEA_H