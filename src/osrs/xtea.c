#include "xtea.h"

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

static int
read_32(char* data, int position)
{
    return ((data[position] & 0xFF) << 24) | ((data[position + 1] & 0xFF) << 16) |
           ((data[position + 2] & 0xFF) << 8) | (data[position + 3] & 0xFF);
}

static void
write_32(char* data, uint32_t value)
{
    data[0] = (value >> 24) & 0xFF;
    data[1] = (value >> 16) & 0xFF;
    data[2] = (value >> 8) & 0xFF;
    data[3] = value & 0xFF;
}

void
xtea_decrypt(char* data, int length, int32_t key[4])
{
    int num_blocks = length / 8;

    int position = 0;

    for( int block = 0; block < num_blocks; block++ )
    {
        uint32_t v0 = read_32(data, position);
        position += 4;
        uint32_t v1 = read_32(data, position);
        position += 4;

        uint32_t sum = XTEA_DELTA * XTEA_ROUNDS;

        for( int i = 0; i < XTEA_ROUNDS; i++ )
        {
            v1 -= (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key[(sum >> 11) & 3]);
            sum -= XTEA_DELTA;
            v0 -= (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key[sum & 3]);
        }

        write_32(data + block * 8, v0);
        write_32(data + block * 8 + 4, v1);
    }
}
