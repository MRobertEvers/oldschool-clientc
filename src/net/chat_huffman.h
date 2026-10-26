#ifndef CHAT_HUFFMAN_H
#define CHAT_HUFFMAN_H

#include <stdint.h>

/**
 * OldSchool chat text: the cache's Huffman code (deob class318, built at boot
 * from the binary table's "huffman" file, Statics ~32220), as every OldSchool
 * chat packet carries it -- MESSAGE_PUBLIC and MESSAGE_PRIVATE both ways, the
 * player-info chat block, the clan and friend channels.
 *
 * The 256 bit lengths are compiled in rather than loaded: they are the same
 * bytes in every OldSchool cache this project carries (184 and 222-241, sha1
 * 664e89cf25a0...), and a table in the binary is one the client, the server and
 * every test share without a cache read before the first line of chat.
 * test-chat-huffman checks it against cache.osrs241's file.
 *
 * The LostCity revisions carry chat as wordpack (net/wordpack.h); a revision
 * states which through GameProtoRevTable.chat_unpack / chat_pack.
 */

/** The longest line the client sends (the chatbox input's limit). */
#define CHAT_HUFFMAN_TEXT_MAX 80

/** The cache's table: one code length in bits per byte value. */
extern uint8_t const g_chat_huffman_osrs[256];

/**
 * The text a chat packet carries -- pSmart1or2 byte count, then the codes
 * (deob Statics.method6719) -- as a ToriMem_Net string. Bits that run out
 * before the count read "Cabbage", which is what the deob's catch makes of
 * them.
 */
char*
chat_huffman_unpack(
    uint8_t const* data,
    int length);

/** `text` encoded as above into `out`; returns the bytes written. */
int
chat_huffman_pack(
    uint8_t* out,
    int cap,
    char const* text);

/** 1 when `text` is a line the chatbox would send: 1 to CHAT_HUFFMAN_TEXT_MAX
 *  bytes. Every byte has a code. */
int
chat_huffman_says(char const* text);

#endif
