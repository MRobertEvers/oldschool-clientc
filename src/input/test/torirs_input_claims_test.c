/*
 * The claims on one frame's input (input/torirs_input_claims.h).
 *
 * Each claim on its own lifetime: the frame begin drops only the frame claim,
 * the key clear only the keys, and the pointer latch holds until the next
 * latch whatever else is cleared. Keys are claimed one OSRS code at a time,
 * at both ends of the range, and a claim made twice is still one claim.
 *
 * Run: make -C src test-input-claims
 */

#include "input/torirs_input_claims.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;

#define TEST_ASSERT(cond, what)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static int
claimed_key_count(struct ToriRS_InputClaims const* claims)
{
    int n = 0;
    for( int k = 0; k < TORIRS_OSRSKEY_COUNT; k++ )
        if( ToriRS_InputClaimsKeyClaimed(claims, k) )
            n++;
    return n;
}

static void
test_reset_claims_nothing(void)
{
    struct ToriRS_InputClaims claims;
    memset(&claims, 0x5a, sizeof(claims));
    ToriRS_InputClaimsReset(&claims);
    TEST_ASSERT(!ToriRS_InputClaimsFrameTaken(&claims), "reset leaves the frame untaken");
    TEST_ASSERT(
        !ToriRS_InputClaimsChromePointer(&claims), "reset leaves the pointer with the game");
    TEST_ASSERT(claimed_key_count(&claims) == 0, "reset claims no key");

    struct ToriRS_InputClaims zeroed;
    memset(&zeroed, 0, sizeof(zeroed));
    TEST_ASSERT(memcmp(&zeroed, &claims, sizeof(claims)) == 0, "reset is the zeroed struct");
}

static void
test_keys(void)
{
    struct ToriRS_InputClaims claims;
    ToriRS_InputClaimsReset(&claims);

    ToriRS_InputClaimsClaimKey(&claims, 2);
    TEST_ASSERT(ToriRS_InputClaimsKeyClaimed(&claims, 2), "a claimed key reads claimed");
    TEST_ASSERT(!ToriRS_InputClaimsKeyClaimed(&claims, 3), "its neighbour does not");
    ToriRS_InputClaimsClaimKey(&claims, 2);
    TEST_ASSERT(claimed_key_count(&claims) == 1, "claiming a key twice is one claim");

    ToriRS_InputClaimsClaimKey(&claims, 0);
    ToriRS_InputClaimsClaimKey(&claims, TORIRS_OSRSKEY_COUNT - 1);
    TEST_ASSERT(ToriRS_InputClaimsKeyClaimed(&claims, 0), "the first code can be claimed");
    TEST_ASSERT(
        ToriRS_InputClaimsKeyClaimed(&claims, TORIRS_OSRSKEY_COUNT - 1),
        "the last code can be claimed");
    TEST_ASSERT(claimed_key_count(&claims) == 3, "three keys claimed");

    ToriRS_InputClaimsClearKeys(&claims);
    TEST_ASSERT(claimed_key_count(&claims) == 0, "the clear drops every key");

    /* A claim made before the clear that must survive it is made again. */
    ToriRS_InputClaimsClaimKey(&claims, 2);
    TEST_ASSERT(
        ToriRS_InputClaimsKeyClaimed(&claims, 2), "a key claimed again after the clear holds");
}

static void
test_frame(void)
{
    struct ToriRS_InputClaims claims;
    ToriRS_InputClaimsReset(&claims);

    ToriRS_InputClaimsTakeFrame(&claims);
    TEST_ASSERT(ToriRS_InputClaimsFrameTaken(&claims), "a taken frame reads taken");
    ToriRS_InputClaimsTakeFrame(&claims);
    TEST_ASSERT(ToriRS_InputClaimsFrameTaken(&claims), "taking it twice is still taken");
    ToriRS_InputClaimsBeginFrame(&claims);
    TEST_ASSERT(!ToriRS_InputClaimsFrameTaken(&claims), "the next frame begins untaken");
    ToriRS_InputClaimsBeginFrame(&claims);
    TEST_ASSERT(!ToriRS_InputClaimsFrameTaken(&claims), "beginning twice is still untaken");
}

static void
test_pointer_latch(void)
{
    struct ToriRS_InputClaims claims;
    ToriRS_InputClaimsReset(&claims);

    ToriRS_InputClaimsLatchChromePointer(&claims, 1);
    TEST_ASSERT(ToriRS_InputClaimsChromePointer(&claims) == 1, "the latch holds a yes");
    ToriRS_InputClaimsBeginFrame(&claims);
    TEST_ASSERT(
        ToriRS_InputClaimsChromePointer(&claims) == 1,
        "a new frame keeps the latch until relatched");
    ToriRS_InputClaimsLatchChromePointer(&claims, 0);
    TEST_ASSERT(ToriRS_InputClaimsChromePointer(&claims) == 0, "relatching a no replaces the yes");
}

static void
test_claims_are_independent(void)
{
    struct ToriRS_InputClaims claims;
    ToriRS_InputClaimsReset(&claims);

    ToriRS_InputClaimsClaimKey(&claims, 49);
    ToriRS_InputClaimsTakeFrame(&claims);
    ToriRS_InputClaimsLatchChromePointer(&claims, 1);

    ToriRS_InputClaimsBeginFrame(&claims);
    TEST_ASSERT(ToriRS_InputClaimsKeyClaimed(&claims, 49), "the frame begin leaves the keys");
    TEST_ASSERT(ToriRS_InputClaimsChromePointer(&claims), "the frame begin leaves the pointer");

    ToriRS_InputClaimsTakeFrame(&claims);
    ToriRS_InputClaimsClearKeys(&claims);
    TEST_ASSERT(ToriRS_InputClaimsFrameTaken(&claims), "the key clear leaves the frame claim");
    TEST_ASSERT(ToriRS_InputClaimsChromePointer(&claims), "the key clear leaves the pointer");

    ToriRS_InputClaimsClaimKey(&claims, 49);
    ToriRS_InputClaimsLatchChromePointer(&claims, 0);
    TEST_ASSERT(ToriRS_InputClaimsKeyClaimed(&claims, 49), "the latch leaves the keys");
    TEST_ASSERT(ToriRS_InputClaimsFrameTaken(&claims), "the latch leaves the frame claim");
}

int
main(void)
{
    test_reset_claims_nothing();
    test_keys();
    test_frame();
    test_pointer_latch();
    test_claims_are_independent();
    if( g_failures )
    {
        printf("torirs_input_claims_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("torirs_input_claims_test: ok\n");
    return 0;
}
