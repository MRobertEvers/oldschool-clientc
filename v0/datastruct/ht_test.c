
#include "ht.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// clang-format off
char const* words[] = {
    "hello",
    "wow",
    "dog",
    "fast",
    "broseph",
    "billy_bob",
    "sweat",
    "tread",
    "tock",
    "lock",
    "clock",
    "clearance"
};
// clang-format on

static struct Hash
hash_word(char const* word)
{
    struct Hash hash = { 0 };
    ht_hash_init(&hash);
    ht_hash_update(&hash, word, strlen(word));
    ht_hash_end(&hash);

    return hash;
}

int
ht_htest()
{
    int* elem;
    struct HashTable ht = { 0 };

    struct HashTableInit init = { 0 };
    init.capacity_hint = 10;
    init.element_size = sizeof(int);
    init.key_size = sizeof(char*);

    ht_init(&ht, init);

    struct Hash hash = { 0 };
    struct HashTableIter iter;

    for( int i = 0; i < init.capacity_hint; i += 1 )
    {
        hash = hash_word(words[i]);

        iter = ht_lookuph(&ht, &hash);
        do
        {
            if( !iter.empty )
                continue;

            elem = ht_emplaceh(&ht, &iter, &words[i]);
            *elem = i;
            break;
        } while( ht_nexth(&ht, &iter) );
    }

    int collisions;
    int slot;
    int* array = (int*)malloc(sizeof(int) * init.capacity_hint);
    for( int i = 0; i < init.capacity_hint; i += 1 )
    {
        collisions = 0;
        hash = hash_word(words[i]);
        iter = ht_lookuph(&ht, &hash);

        do
        {
            if( strncmp(words[i], *(char const**)iter.key, strlen(words[i])) == 0 )
            {
                elem = ht_emplaceh(&ht, &iter, &words[i]);
                assert(elem != NULL);
                array[i] = *elem;
                slot = iter._slot;

                printf("%08x, %d, %s\n", *elem, collisions, words[i]);
                printf("Slot: %d\n", slot);
                break;
            }
            collisions += 1;
        } while( ht_nexth(&ht, &iter) );
    }

    // elem = ht_emplace(&ht, words[init.capacity_hint + 1], strlen(words[init.capacity_hint + 1]));
    // assert(elem == NULL);

    printf("Pre Resize: %d bytes\n", ht_memusage(&ht));
    ht_resize(&ht, 200);
    printf("Post Resize: %d bytes\n", ht_memusage(&ht));

    for( int i = 0; i < init.capacity_hint; i += 1 )
    {
        collisions = 0;
        hash = hash_word(words[i]);
        iter = ht_lookuph(&ht, &hash);

        if( iter.empty )
            continue;
        char const* key = *(char const**)iter.key;
        do
        {
            if( strncmp(words[i], key, strlen(words[i])) == 0 )
            {
                elem = ht_emplaceh(&ht, &iter, &words[i]);
                assert(elem != NULL);
                array[i] = *elem;
                slot = iter._slot;

                printf("%08x, %d, %s\n", *elem, collisions, words[i]);
                printf("Slot: %d\n", slot);
                break;
            }
            collisions += 1;
        } while( ht_nexth(&ht, &iter) );
    }

    ht_cleanup(&ht);
    return 1;
}

void
ht_str_test()
{
    struct HashTable ht = { 0 };
    struct HashTableInit init = { 0 };
    init.capacity_hint = 10;
    init.element_size = sizeof(int);

    ht_init(&ht, init);

    char const* key = "hello";
    int* elem = ht_emplace(&ht, key, strlen(key));
    *elem = 10;

    elem = ht_lookup(&ht, key, strlen(key));
    assert(*elem == 10);

    printf("Keyslot: %d\n", ht_keyslot(&ht, key, strlen(key)));

    elem = ht_lookup(&ht, "hi", 2);
    assert(elem == NULL);

    printf("Keyslot: %d\n", ht_keyslot(&ht, "hi", 2));
    assert(ht_keyslot(&ht, "hi", 2) == -1);

    ht_cleanup(&ht);
}

void
ht_remove_test()
{
    struct HashTable ht = { 0 };
    struct HashTableInit init = { 0 };
    init.capacity_hint = 10;
    init.element_size = sizeof(int);

    ht_init(&ht, init);

    int key = 10;
    int* elem = ht_emplace(&ht, &key, sizeof(key));
    *elem = 10;

    elem = ht_lookup(&ht, &key, sizeof(key));
    assert(*elem == 10);

    int slot = ht_keyslot(&ht, &key, sizeof(key));

    printf("Keyslot: %d\n", slot);
    ht_remove(&ht, &key, sizeof(key));
    elem = ht_lookup(&ht, &key, sizeof(key));
    assert(elem == NULL);

    assert(ht_keyslot(&ht, &key, sizeof(key)) == -1);

    elem = ht_emplace(&ht, &key, sizeof(key));
    *elem = 10;

    elem = ht_lookup(&ht, &key, sizeof(key));
    assert(*elem == 10);
    printf("Keyslot: %d\n", ht_keyslot(&ht, &key, sizeof(key)));
    assert(ht_keyslot(&ht, &key, sizeof(key)) == slot);

    ht_cleanup(&ht);
}

void
ht_int_test()
{
    struct HashTable ht = { 0 };
    struct HashTableInit init = { 0 };
    init.capacity_hint = 10;
    init.element_size = sizeof(int);
}

int
main()
{
    ht_htest();
    ht_str_test();
    ht_remove_test();
    return 0;
}