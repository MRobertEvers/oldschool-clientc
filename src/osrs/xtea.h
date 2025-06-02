#ifndef XTEA_H
#define XTEA_H

#include <stdint.h>

/**
 * @brief xtea keys are 16 bytes long.
 *
 * @param data
 * @param data_length
 * @param key
 * @return char*
 */
char* xtea_decrypt(char* data, int data_length, uint32_t* key);

#endif // XTEA_H