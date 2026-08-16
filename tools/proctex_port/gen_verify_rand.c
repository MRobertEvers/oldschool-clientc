/* Quick check that our Java Random / nextIntJagex match node java-random. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

struct proctex_jrand { uint64_t seed; };
static void proctex_jrand_seed(struct proctex_jrand* r, int64_t seedval) {
    r->seed = ((uint64_t)seedval ^ 0x5DEECE66DULL) & ((1ULL << 48) - 1);
}
static int32_t proctex_jrand_next(struct proctex_jrand* r, int bits) {
    r->seed = (r->seed * 0x5DEECE66DULL + 0xBULL) & ((1ULL << 48) - 1);
    return (int32_t)(r->seed >> (48 - bits));
}
static int32_t proctex_jrand_next_int(struct proctex_jrand* r) {
    return proctex_jrand_next(r, 32);
}
static int proctex_is_pow2(int32_t n) { return n != 0 && (n & -n) == n; }
static int32_t proctex_next_int_jagex(struct proctex_jrand* r, int32_t bound) {
    int32_t rnd, max_value, i78;
    if (proctex_is_pow2(bound)) {
        uint32_t u = (uint32_t)proctex_jrand_next_int(r);
        return (int32_t)(((uint64_t)(uint32_t)bound * (uint64_t)u) >> 32);
    }
    max_value = (int32_t)(-0x80000000 - (int32_t)(0x100000000ULL % (uint32_t)bound));
    do { rnd = proctex_jrand_next_int(r); } while (rnd >= max_value);
    i78 = (rnd >> 31) & (bound - 1);
    return i78 + ((rnd + (int32_t)((uint32_t)rnd >> 31)) % bound);
}

int main(void) {
    struct proctex_jrand r;
    int i;
    proctex_jrand_seed(&r, 0);
    printf("seed0 ");
    for (i = 0; i < 16; i++) printf("%s%d", i?",":"", proctex_next_int_jagex(&r, 4096));
    printf("\nseed8 ");
    proctex_jrand_seed(&r, 8);
    for (i = 0; i < 8; i++) printf("%s%d", i?",":"", proctex_next_int_jagex(&r, 4096));
    printf("\n");
    return 0;
}
