#include "pow_sha256.h"

#include "sha256.h"

#include <stdio.h>
#include <string.h>

#define POW_MAX_INPUT 512

/* Java's Long.toHexString: lowercase, no leading zeros, "0" for zero. */
static int
hex_lower(uint64_t v, char* out)
{
    static char const digits[] = "0123456789abcdef";
    char tmp[17];
    int n = 0;
    if( v == 0 )
    {
        out[0] = '0';
        return 1;
    }
    while( v )
    {
        tmp[n++] = digits[v & 0xf];
        v >>= 4;
    }
    for( int i = 0; i < n; i++ )
        out[i] = tmp[n - 1 - i];
    return n;
}

int
pow_sha256_base_string(
    int version,
    int difficulty,
    char const* salt,
    char* out,
    size_t out_size)
{
    char vbuf[17];
    char dbuf[17];
    int vn = hex_lower((uint32_t)version, vbuf);
    int dn = hex_lower((uint32_t)difficulty, dbuf);
    size_t sn = salt ? strlen(salt) : 0;

    if( (size_t)vn + (size_t)dn + sn + 1 > out_size )
        return -1;
    memcpy(out, vbuf, (size_t)vn);
    memcpy(out + vn, dbuf, (size_t)dn);
    if( sn )
        memcpy(out + vn + dn, salt, sn);
    out[vn + dn + sn] = '\0';
    return vn + dn + (int)sn;
}

int
pow_sha256_verify(
    int version,
    int difficulty,
    char const* salt,
    uint64_t result)
{
    char buf[POW_MAX_INPUT];
    uint8_t digest[SHA256_DIGEST_BYTES];
    int base_len = pow_sha256_base_string(version, difficulty, salt, buf, sizeof(buf) - 17);
    if( base_len < 0 )
        return 0;
    int n = base_len + hex_lower(result, buf + base_len);
    sha256((uint8_t const*)buf, (size_t)n, digest);
    return sha256_leading_zero_bits(digest) >= difficulty;
}

int
pow_sha256_solve(
    int version,
    int difficulty,
    char const* salt,
    uint64_t max_attempts,
    uint64_t* out_result)
{
    char buf[POW_MAX_INPUT];
    uint8_t digest[SHA256_DIGEST_BYTES];
    int base_len = pow_sha256_base_string(version, difficulty, salt, buf, sizeof(buf) - 17);
    if( base_len < 0 )
        return 0;

    /* The base string never changes, so only the hex suffix is rewritten per
     * candidate. The hash itself is the cost; there is nothing to save above
     * it short of a midstate, which is not worth the subtlety at 18 bits. */
    for( uint64_t n = 0; !max_attempts || n < max_attempts; n++ )
    {
        int len = base_len + hex_lower(n, buf + base_len);
        sha256((uint8_t const*)buf, (size_t)len, digest);
        if( sha256_leading_zero_bits(digest) >= difficulty )
        {
            if( out_result )
                *out_result = n;
            return 1;
        }
    }
    return 0;
}
