#include "net/chat_huffman.h"

#include "mem/torirs_mem.h"
#include "rsprot_compression.h"

#include <assert.h>
#include <string.h>

/* binary table, file "huffman", cache.osrs241 (identical 184, 222-241). */
uint8_t const g_chat_huffman_osrs[256] = {
    22, 22, 22, 22, 22, 22, 21, 22, 22, 20, 22, 22, 22, 21, 22, 22,
    22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22,
     3,  8, 22, 16, 22, 16, 17,  7, 13, 13, 13, 16,  7, 10,  6, 16,
    10, 11, 12, 12, 12, 12, 13, 13, 14, 14, 11, 14, 19, 15, 17,  8,
    11,  9, 10, 10, 10, 10, 11, 10,  9,  7, 12, 11, 10, 10,  9, 10,
    10, 12, 10,  9,  8, 12, 12,  9, 14,  8, 12, 17, 16, 17, 22, 13,
    21,  4,  7,  6,  5,  3,  6,  6,  5,  4, 10,  7,  5,  6,  4,  4,
     6, 10,  5,  4,  4,  5,  7,  6, 10,  6, 10, 22, 19, 22, 14, 22,
    22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22,
    22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22,
    22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22,
    22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22,
    22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22,
    22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22,
    22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22,
    22, 22, 22, 22, 22, 22, 22, 21, 22, 21, 22, 22, 22, 21, 22, 22,
};

/*
 * The codec is built per message rather than once: the client and the
 * embedded server run on different threads, and a line of chat is rare enough
 * that 256 codewords and a lookup tree cost nothing beside it.
 */
static void
chat_huffman_create(RsprotHuffman* h)
{
    bool const ok = rsprot_huffman_create(h, g_chat_huffman_osrs);
    assert(ok);
    (void)ok;
}

/* What the deob's catch makes of a line whose bits do not decode. */
static char*
chat_huffman_unreadable(void)
{
    static char const k_unreadable[] = "Cabbage";
    char* out = ToriMem_Alloc(ToriMem_Net(), sizeof(k_unreadable));

    assert(out);
    memcpy(out, k_unreadable, sizeof(k_unreadable));
    return out;
}

char*
chat_huffman_unpack(
    uint8_t const* data,
    int length)
{
    RsprotHuffman h;
    RsprotBuf b;
    char* out;
    int32_t count;
    int32_t got;

    /* The count leads: pSmart1or2, so its first byte says how wide it is.
     * An empty tail is a frame that ended early, not a caller's bug. */
    if( length < 1 )
        return chat_huffman_unreadable();
    assert(data);
    if( data[0] < 0x80 )
        count = data[0];
    else
        count = length < 2 ? -1 : ((data[0] << 8) | data[1]) & 0x7fff;
    if( count < 0 )
        return chat_huffman_unreadable();

    out = ToriMem_Alloc(ToriMem_Net(), (size_t)count + 1);
    assert(out);
    chat_huffman_create(&h);
    rsprot_buf_wrap_read(&b, data, length);
    got = rsprot_huffman_decode(&h, &b, out, count + 1);
    rsprot_huffman_destroy(&h);
    if( got != count || !rsprot_buf_ok(&b) )
    {
        ToriMem_Free(ToriMem_Net(), out);
        return chat_huffman_unreadable();
    }
    return out;
}

int
chat_huffman_pack(
    uint8_t* out,
    int cap,
    char const* text)
{
    RsprotHuffman h;
    RsprotBuf b;

    assert(out);
    assert(text);
    chat_huffman_create(&h);
    rsprot_buf_wrap(&b, out, cap);
    rsprot_huffman_encode(&h, &b, text);
    rsprot_huffman_destroy(&h);
    /* Every byte has a code, so the only failure is a buffer the caller sized
     * too small for its text. */
    assert(rsprot_buf_ok(&b));
    return b.wpos;
}

int
chat_huffman_says(char const* text)
{
    size_t len;

    assert(text);
    len = strlen(text);
    return len >= 1 && len <= CHAT_HUFFMAN_TEXT_MAX;
}
