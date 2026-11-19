/*
 * The redraw request (render/redraw_request.h): a raise is pending until it is
 * cleared or taken, raising twice is one request, and a take answers once.
 */
#include "render/redraw_request.h"

#include <stdio.h>
#include <string.h>

static int g_fail;

#define CHECK(c, what)                                                                             \
    do                                                                                             \
    {                                                                                              \
        if( !(c) )                                                                                 \
        {                                                                                          \
            printf("FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__);                                \
            g_fail++;                                                                              \
        }                                                                                          \
    } while( 0 )

static void
test_zeroed_is_not_pending(void)
{
    struct RedrawRequest req;
    memset(&req, 0, sizeof(req));
    CHECK(!RedrawRequest_Pending(&req), "a zeroed request is not pending");
    CHECK(!RedrawRequest_Take(&req), "taking a zeroed request answers false");
}

static void
test_raise_is_pending_until_cleared(void)
{
    struct RedrawRequest req;
    memset(&req, 0, sizeof(req));
    RedrawRequest_Raise(&req);
    CHECK(RedrawRequest_Pending(&req), "raised is pending");
    CHECK(RedrawRequest_Pending(&req), "reading pending does not consume it");
    RedrawRequest_Clear(&req);
    CHECK(!RedrawRequest_Pending(&req), "cleared is not pending");
    RedrawRequest_Clear(&req);
    CHECK(!RedrawRequest_Pending(&req), "clearing twice is harmless");
}

static void
test_raise_twice_is_one_request(void)
{
    struct RedrawRequest req;
    memset(&req, 0, sizeof(req));
    RedrawRequest_Raise(&req);
    RedrawRequest_Raise(&req);
    CHECK(RedrawRequest_Take(&req), "the take answers the request");
    CHECK(!RedrawRequest_Pending(&req), "the take consumed it");
    CHECK(!RedrawRequest_Take(&req), "a second take answers false");
}

static void
test_raise_after_take_is_new(void)
{
    struct RedrawRequest req;
    memset(&req, 0, sizeof(req));
    RedrawRequest_Raise(&req);
    (void)RedrawRequest_Take(&req);
    RedrawRequest_Raise(&req);
    CHECK(RedrawRequest_Pending(&req), "a raise after a take is pending again");
}

int
main(void)
{
    test_zeroed_is_not_pending();
    test_raise_is_pending_until_cleared();
    test_raise_twice_is_one_request();
    test_raise_after_take_is_new();
    if( g_fail )
    {
        printf("redraw_request_test: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("redraw_request_test: ok\n");
    return 0;
}
