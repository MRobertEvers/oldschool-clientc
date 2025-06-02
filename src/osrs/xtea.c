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

char*
xtea_decrypt(char* data, int length, uint32_t key[4])
{
    int num_blocks = length / 8;
    uint32_t* blocks = (uint32_t*)data;
    char* out_buffer = (char*)malloc(length);
    if( !out_buffer )
        return NULL;

    uint32_t* out_blocks = (uint32_t*)out_buffer;

    for( int block = 0; block < num_blocks; block++ )
    {
        uint32_t v0 = blocks[block * 2];
        uint32_t v1 = blocks[block * 2 + 1];

        uint32_t sum = 0xC6EF3720; // GOLDEN_RATIO * ROUNDS

        for( int i = 0; i < 32; i++ )
        { // ROUNDS = 32
            v1 -= (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key[(sum >> 11) & 3]);
            sum -= 0x9E3779B9; // GOLDEN_RATIO
            v0 -= (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key[sum & 3]);
        }

        out_blocks[block * 2] = v0;
        out_blocks[block * 2 + 1] = v1;
    }

    return out_buffer;
}
