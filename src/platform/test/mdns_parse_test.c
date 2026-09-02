/*
 * mDNS response parser, offline: no socket, no responder, no network.
 *
 * The reason this test exists separately from the live probe is that the live
 * path can only ever show the WELL-FORMED case. Everything the parser has to
 * survive -- a compression pointer chain, a pointer that loops, an RDLENGTH
 * that runs past the datagram, a truncated record, a reserved label type -- is
 * a packet no cooperating responder will ever send, so it has to be built by
 * hand. This is data off the network: getting it wrong is an out-of-bounds
 * read, not a failed lookup.
 *
 * The .c is INCLUDED rather than linked: mdns_response_find_a is static, and
 * it is static because nothing outside the resolver has any business calling
 * it. Widening its linkage to make it testable would be the test changing the
 * design.
 */
#include "platform/platform_mdns.c"

#include <stdio.h>

static int fails = 0;

static void
check(const char* what, int got, int want)
{
    printf("%-52s got=%d want=%d %s\n", what, got, want, got == want ? "ok" : "FAIL");
    if( got != want )
        fails++;
}

/* header(12) + question("matthewllm"."local") + answer with a COMPRESSED
 * owner name pointing back at offset 12. */
static int
build_compressed(uint8_t* p, int rdlength_override, int truncate_to)
{
    int n = 0;
    memset(p, 0, 512);
    p[2] = 0x84;             /* QR + AA */
    p[5] = 1;                /* qdcount */
    p[7] = 1;                /* ancount */
    n = 12;
    p[n++] = 10; memcpy(p + n, "matthewllm", 10); n += 10;
    p[n++] = 5;  memcpy(p + n, "local", 5);       n += 5;
    p[n++] = 0;
    p[n++] = 0; p[n++] = 1;  /* QTYPE A */
    p[n++] = 0x80; p[n++] = 1; /* QCLASS IN + QU */
    /* answer: name = pointer to offset 12 */
    p[n++] = 0xC0; p[n++] = 12;
    p[n++] = 0; p[n++] = 1;                 /* TYPE A */
    p[n++] = 0x80; p[n++] = 1;              /* CLASS IN + cache-flush */
    p[n++] = 0; p[n++] = 0; p[n++] = 0x00; p[n++] = 0x78; /* TTL */
    p[n++] = 0; p[n++] = (uint8_t)(rdlength_override >= 0 ? rdlength_override : 4);
    p[n++] = 192; p[n++] = 168; p[n++] = 1; p[n++] = 146;
    return truncate_to >= 0 ? truncate_to : n;
}

int
main(void)
{
    uint8_t pkt[512];
    uint32_t addr = 0;
    int len;

    /* 1. compressed answer name resolves */
    len = build_compressed(pkt, -1, -1);
    addr = 0;
    check("compressed owner name -> match", mdns_response_find_a(pkt, len, "matthewllm.local", &addr), 1);
    check("  address is 192.168.1.146", addr == htonl(0xC0A80192u), 1);

    /* 2. same packet, different queried name -> no match */
    check("name mismatch rejected", mdns_response_find_a(pkt, len, "other.local", &addr), 0);

    /* 3. RDLENGTH that runs off the end of the buffer */
    len = build_compressed(pkt, 200, -1);
    check("RDLENGTH past end rejected", mdns_response_find_a(pkt, len, "matthewllm.local", &addr), 0);

    /* 4. truncated mid-record */
    len = build_compressed(pkt, -1, 34);
    check("truncated packet rejected", mdns_response_find_a(pkt, len, "matthewllm.local", &addr), 0);

    /* The answer's owner name lives at offset 34: header 12 + name 18 + 4. */
#define ANSWER_NAME_OFFSET 34

    /* 5. forward/self pointer (decompression loop) */
    len = build_compressed(pkt, -1, -1);
    pkt[ANSWER_NAME_OFFSET] = 0xC0;
    pkt[ANSWER_NAME_OFFSET + 1] = ANSWER_NAME_OFFSET;
    check("self-referential pointer rejected", mdns_response_find_a(pkt, len, "matthewllm.local", &addr), 0);

    /* 6. pointer past the end of the buffer */
    len = build_compressed(pkt, -1, -1);
    pkt[ANSWER_NAME_OFFSET] = 0xC0;
    pkt[ANSWER_NAME_OFFSET + 1] = 250;
    check("out-of-range pointer rejected", mdns_response_find_a(pkt, len, "matthewllm.local", &addr), 0);

    /* 6b. a CHAIN: second answer's name points at the first answer's name,
     * which is itself a pointer back into the question. Real responders emit
     * exactly this. */
    len = build_compressed(pkt, -1, -1);
    pkt[7] = 2; /* ancount = 2 */
    {
        int n = len;
        pkt[n++] = 0xC0; pkt[n++] = ANSWER_NAME_OFFSET;
        pkt[n++] = 0; pkt[n++] = 1;
        pkt[n++] = 0x80; pkt[n++] = 1;
        pkt[n++] = 0; pkt[n++] = 0; pkt[n++] = 0; pkt[n++] = 0x78;
        pkt[n++] = 0; pkt[n++] = 4;
        pkt[n++] = 10; pkt[n++] = 0; pkt[n++] = 0; pkt[n++] = 7;
        len = n;
    }
    /* Truncate the FIRST answer's rdata match by asking for a name the first
     * record does not carry, so only the chained second record can answer. */
    pkt[13] = 'a'; /* question label now "aatthewllm" -> first answer's name too */
    addr = 0;
    check("pointer chain followed", mdns_response_find_a(pkt, len, "aatthewllm.local", &addr), 1);
    check("  chain picked the first A record", addr == htonl(0xC0A80192u), 1);

    /* 7. reserved label type 0x40 */
    len = build_compressed(pkt, -1, -1);
    pkt[12] = 0x40;
    check("reserved label type rejected", mdns_response_find_a(pkt, len, "matthewllm.local", &addr), 0);

    /* 8. header shorter than 12 bytes */
    check("short header rejected", mdns_response_find_a(pkt, 5, "matthewllm.local", &addr), 0);

    /* 9. query (QR clear) is not an answer */
    len = build_compressed(pkt, -1, -1);
    pkt[2] = 0x00;
    check("QR-clear packet rejected", mdns_response_find_a(pkt, len, "matthewllm.local", &addr), 0);

    /* 10. label length running past the buffer */
    len = build_compressed(pkt, -1, -1);
    pkt[12] = 200;
    check("oversized label rejected", mdns_response_find_a(pkt, len, "matthewllm.local", &addr), 0);

    /* 11. name suffix classifier */
    check("IsLocalName(matthewllm.local)", PlatformMdns_IsLocalName("matthewllm.local"), 1);
    check("IsLocalName(MatthewLLM.LOCAL.)", PlatformMdns_IsLocalName("MatthewLLM.LOCAL."), 1);
    check("IsLocalName(example.com)", PlatformMdns_IsLocalName("example.com"), 0);
    check("IsLocalName(.local)", PlatformMdns_IsLocalName(".local"), 0);
    check("IsLocalName(notlocal)", PlatformMdns_IsLocalName("notlocal"), 0);

    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASS", fails);
    return fails != 0;
}
