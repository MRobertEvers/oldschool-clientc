#ifndef TEX_SHARD_SWAP_H
#define TEX_SHARD_SWAP_H

#ifndef SWAP
#define SWAP(a, b)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        int tex_shard_swap_tmp_ = (a);                                                             \
        (a) = (b);                                                                                 \
        (b) = tex_shard_swap_tmp_;                                                                 \
    } while(0)
#endif

#endif
