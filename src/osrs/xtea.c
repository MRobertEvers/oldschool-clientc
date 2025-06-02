#include "buffer.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XTEA_DELTA 0x9E3779B9
#define XTEA_ROUNDS 32

// public byte[] decrypt(byte[] data, int len)
// {
// 	InputStream in = new InputStream(data);
// 	OutputStream out = new OutputStream(len);
// 	int numBlocks = len / 8;
// 	for (int block = 0; block < numBlocks; ++block)
// 	{
// 		int v0 = in.readInt();
// 		int v1 = in.readInt();
// 		int sum = GOLDEN_RATIO * ROUNDS;
// 		for (int i = 0; i < ROUNDS; ++i)
// 		{
// 			v1 -= (((v0 << 4) ^ (v0 >>> 5)) + v0) ^ (sum + key[(sum >>> 11) & 3]);
// 			sum -= GOLDEN_RATIO;
// 			v0 -= (((v1 << 4) ^ (v1 >>> 5)) + v1) ^ (sum + key[sum & 3]);
// 		}
// 		out.writeInt(v0);
// 		out.writeInt(v1);
// 	}
// 	out.writeBytes(in.getRemaining());
// 	return out.flip();
// }

static void
write_32(char* data, uint32_t value)
{
    data[0] = (value >> 24) & 0xFF;
    data[1] = (value >> 16) & 0xFF;
    data[2] = (value >> 8) & 0xFF;
    data[3] = value & 0xFF;
}

void
xtea_decrypt(char* data, int length, uint32_t key[4])
{
    int num_blocks = length / 8;
    struct Buffer buffer = { .data = data, .position = 0, .data_size = length };

    for( int block = 0; block < num_blocks; block++ )
    {
        uint32_t v0 = read_32(&buffer);
        uint32_t v1 = read_32(&buffer);

        uint32_t sum = XTEA_DELTA * XTEA_ROUNDS;

        for( int i = 0; i < XTEA_ROUNDS; i++ )
        {
            uint32_t v0_shift_left = (v0 << 4);
            uint32_t v0_shift_right = ((uint32_t)v0 >> 5);
            v1 -= ((v0_shift_left ^ v0_shift_right) + v0) ^ (sum + key[(sum >> 11) & 3]);
            sum -= XTEA_DELTA;

            uint32_t v1_shift_left = (v1 << 4);
            uint32_t v1_shift_right = ((uint32_t)v1 >> 5);
            v0 -= ((v1_shift_left ^ v1_shift_right) + v1) ^ (sum + key[sum & 3]);
        }

        write_32(data + block * 8, v0);
        write_32(data + block * 8 + 4, v1);
    }
}
