#ifndef HT_H_
#define HT_H_

struct Hash
{
    char hash[16];
};

struct HashTable
{
    char* data;
    int size;
    int capacity;
    int element_size;
    int key_size;
};

struct HashTableInit
{
    int element_size;
    int key_size;
    int capacity_hint;
};

void ht_hash_init(struct Hash* hash);
void ht_hash_update(struct Hash* hash, char const* data, int len);
void ht_hash_end(struct Hash* hash);

/**
 * @brief A linear probe general purpose hash table.
 *
 * By default, the max probe length is 2. That means some keys may receive a "not enough space" *
 * error even when others might not. It is up to the caller to resize the table.
 *
 * @param ht
 * @param init
 */
void ht_init(struct HashTable* ht, struct HashTableInit init);
void ht_cleanup(struct HashTable* ht);

struct HashTableIter
{
    void* key;
    void* value;
    char empty;
    char tombstone;
    char at_end;

    struct Hash _hash;
    int _slot;
    int _step;
};
struct HashTableIter ht_lookuph(struct HashTable* ht, struct Hash* hash);
int ht_nexth(struct HashTable* ht, struct HashTableIter* iter);
void* ht_emplaceh(struct HashTable* ht, struct HashTableIter* iter, void const* key);
void* ht_removeh(struct HashTable* ht, struct HashTableIter* iter, void const* key);

void* ht_emplace(struct HashTable* ht, char const* key, int len);
void* ht_lookup(struct HashTable* ht, char const* key, int len);
void* ht_remove(struct HashTable* ht, char const* key, int len);

void ht_resize(struct HashTable* ht, int new_capacity);

int ht_keyslot(struct HashTable* ht, char const* key, int len);
int ht_memusage(struct HashTable* ht);

#endif