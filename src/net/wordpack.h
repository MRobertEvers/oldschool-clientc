#ifndef WORDPACK_H
#define WORDPACK_H

#include <rsbuffer.h>

#include <stddef.h>

/**
 * WordPack: RuneScape chat text compression.
 * Client-TS WordPack.ts - pack/unpack for MESSAGE_PUBLIC and MESSAGE_PRIVATE.
 */

void
wordpack_pack(
    struct RSCache_Buffer* buffer,
    const char* str);

char*
wordpack_unpack(
    struct RSCache_Buffer* buffer,
    int length);

/** The longest line wordpack_pack writes; longer text is cut there. */
#define WORDPACK_TEXT_MAX 80

/**
 * MESSAGE_PUBLIC's body -- p1 colour, p1 effect, the wordpacked text -- the ONE
 * writer of it: the client's net_out_message_public (the chatbox's send) and
 * scriptrun's api_drive.say both call this, so the two lanes put the same bytes
 * on the server's handle_message_public. `colour_effect`: high byte colour, low
 * byte effect.
 */
void
wordpack_message_public_body(
    struct RSCache_Buffer* buffer,
    const char* str,
    int colour_effect);

/**
 * 1 when `str` is a line wordpack carries unchanged apart from case: 1 to
 * WORDPACK_TEXT_MAX characters, each (lower-cased) in the pack table. A line
 * that fails this would arrive cut short or with characters turned into
 * spaces, so a sender that must be understood refuses it instead.
 */
int
wordpack_says(const char* str);

#endif
