#include "input/torirs_input_claims.h"

#include <assert.h>
#include <string.h>

void
ToriRS_InputClaimsReset(struct ToriRS_InputClaims* claims)
{
    assert(claims);
    memset(claims, 0, sizeof(*claims));
}

void
ToriRS_InputClaimsBeginFrame(struct ToriRS_InputClaims* claims)
{
    assert(claims);
    claims->frame = 0;
}

void
ToriRS_InputClaimsTakeFrame(struct ToriRS_InputClaims* claims)
{
    assert(claims);
    claims->frame = 1;
}

int
ToriRS_InputClaimsFrameTaken(struct ToriRS_InputClaims const* claims)
{
    assert(claims);
    return claims->frame;
}

void
ToriRS_InputClaimsClearKeys(struct ToriRS_InputClaims* claims)
{
    assert(claims);
    memset(claims->key, 0, sizeof(claims->key));
}

void
ToriRS_InputClaimsClaimKey(
    struct ToriRS_InputClaims* claims,
    int osrs_key)
{
    assert(claims);
    assert(osrs_key >= 0);
    assert(osrs_key < TORIRS_OSRSKEY_COUNT);
    claims->key[osrs_key] = 1;
}

int
ToriRS_InputClaimsKeyClaimed(
    struct ToriRS_InputClaims const* claims,
    int osrs_key)
{
    assert(claims);
    assert(osrs_key >= 0);
    assert(osrs_key < TORIRS_OSRSKEY_COUNT);
    return claims->key[osrs_key];
}

void
ToriRS_InputClaimsLatchChromePointer(
    struct ToriRS_InputClaims* claims,
    int owned)
{
    assert(claims);
    claims->chrome_pointer = owned;
}

int
ToriRS_InputClaimsChromePointer(struct ToriRS_InputClaims const* claims)
{
    assert(claims);
    return claims->chrome_pointer;
}
