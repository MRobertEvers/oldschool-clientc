#ifndef BENCH_RBTREE_H
#define BENCH_RBTREE_H

/*
 * Red-black tree keyed on uint32_t, storing BenchObject by value.
 * CLRS insert with rotations and recolor fixup. Iterative find.
 * No delete — the benchmark only builds then looks up.
 */

#include "bench_object.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum BenchRbColor
{
    BENCH_RB_RED = 0,
    BENCH_RB_BLACK = 1
};

struct BenchRbNode
{
    uint32_t id;
    enum BenchRbColor color;
    struct BenchObject obj;
    struct BenchRbNode* parent;
    struct BenchRbNode* left;
    struct BenchRbNode* right;
};

struct BenchRbTree
{
    struct BenchRbNode* root;
    size_t size;
};

static inline void
bench_rb_init(struct BenchRbTree* t)
{
    assert(t);
    t->root = NULL;
    t->size = 0;
}

static inline int
bench_rb_is_red(const struct BenchRbNode* n)
{
    return n && n->color == BENCH_RB_RED;
}

static inline void
bench_rb_rotate_left(struct BenchRbTree* t, struct BenchRbNode* x)
{
    assert(t);
    assert(x);
    assert(x->right);
    struct BenchRbNode* y = x->right;
    x->right = y->left;
    if( y->left )
        y->left->parent = x;
    y->parent = x->parent;
    if( !x->parent )
        t->root = y;
    else if( x == x->parent->left )
        x->parent->left = y;
    else
        x->parent->right = y;
    y->left = x;
    x->parent = y;
}

static inline void
bench_rb_rotate_right(struct BenchRbTree* t, struct BenchRbNode* y)
{
    assert(t);
    assert(y);
    assert(y->left);
    struct BenchRbNode* x = y->left;
    y->left = x->right;
    if( x->right )
        x->right->parent = y;
    x->parent = y->parent;
    if( !y->parent )
        t->root = x;
    else if( y == y->parent->left )
        y->parent->left = x;
    else
        y->parent->right = x;
    x->right = y;
    y->parent = x;
}

static inline void
bench_rb_insert_fixup(struct BenchRbTree* t, struct BenchRbNode* z)
{
    assert(t);
    assert(z);
    while( bench_rb_is_red(z->parent) )
    {
        struct BenchRbNode* p = z->parent;
        struct BenchRbNode* gp = p->parent;
        assert(gp && "red parent of root would violate black-height");

        if( p == gp->left )
        {
            struct BenchRbNode* uncle = gp->right;
            if( bench_rb_is_red(uncle) )
            {
                p->color = BENCH_RB_BLACK;
                uncle->color = BENCH_RB_BLACK;
                gp->color = BENCH_RB_RED;
                z = gp;
            }
            else
            {
                if( z == p->right )
                {
                    z = p;
                    bench_rb_rotate_left(t, z);
                    p = z->parent;
                    gp = p->parent;
                }
                p->color = BENCH_RB_BLACK;
                gp->color = BENCH_RB_RED;
                bench_rb_rotate_right(t, gp);
            }
        }
        else
        {
            struct BenchRbNode* uncle = gp->left;
            if( bench_rb_is_red(uncle) )
            {
                p->color = BENCH_RB_BLACK;
                uncle->color = BENCH_RB_BLACK;
                gp->color = BENCH_RB_RED;
                z = gp;
            }
            else
            {
                if( z == p->left )
                {
                    z = p;
                    bench_rb_rotate_right(t, z);
                    p = z->parent;
                    gp = p->parent;
                }
                p->color = BENCH_RB_BLACK;
                gp->color = BENCH_RB_RED;
                bench_rb_rotate_left(t, gp);
            }
        }
    }
    t->root->color = BENCH_RB_BLACK;
}

static inline void
bench_rb_insert(struct BenchRbTree* t, const struct BenchObject* obj)
{
    assert(t);
    assert(obj);

    struct BenchRbNode* z =
        (struct BenchRbNode*)calloc(1, sizeof(struct BenchRbNode));
    if( !z )
        bench_die("bench_rb_insert alloc failed");
    z->id = obj->id;
    z->obj = *obj;
    z->color = BENCH_RB_RED;
    z->left = NULL;
    z->right = NULL;
    z->parent = NULL;

    struct BenchRbNode* y = NULL;
    struct BenchRbNode* x = t->root;
    while( x )
    {
        y = x;
        if( z->id < x->id )
            x = x->left;
        else if( z->id > x->id )
            x = x->right;
        else
        {
            /* Duplicate key — overwrite payload, free the new node. */
            x->obj = *obj;
            free(z);
            return;
        }
    }

    z->parent = y;
    if( !y )
        t->root = z;
    else if( z->id < y->id )
        y->left = z;
    else
        y->right = z;

    t->size++;
    bench_rb_insert_fixup(t, z);
}

static inline const struct BenchObject*
bench_rb_find(const struct BenchRbTree* t, uint32_t id)
{
    assert(t);
    const struct BenchRbNode* n = t->root;
    while( n )
    {
        if( id < n->id )
            n = n->left;
        else if( id > n->id )
            n = n->right;
        else
            return &n->obj;
    }
    return NULL;
}

static inline void
bench_rb_free_node(struct BenchRbNode* n)
{
    if( !n )
        return;
    bench_rb_free_node(n->left);
    bench_rb_free_node(n->right);
    free(n);
}

static inline void
bench_rb_free(struct BenchRbTree* t)
{
    assert(t);
    bench_rb_free_node(t->root);
    t->root = NULL;
    t->size = 0;
}

/* Returns black-height of subtree, or -1 on invariant failure. */
static inline int
bench_rb_validate_node(
    const struct BenchRbNode* n,
    uint32_t min_id,
    uint32_t max_id,
    int min_open,
    int max_open)
{
    if( !n )
        return 1; /* NIL is black; contributes 1 to black-height convention
                     used below as the leaf sentinel count. */

    /* BST order: (min_id, max_id) with open/closed ends via flags. */
    if( min_open )
    {
        if( !(n->id > min_id) )
            return -1;
    }
    else if( !(n->id >= min_id) )
    {
        return -1;
    }
    if( max_open )
    {
        if( !(n->id < max_id) )
            return -1;
    }
    else if( !(n->id <= max_id) )
    {
        return -1;
    }

    /* No red-red. */
    if( n->color == BENCH_RB_RED )
    {
        if( bench_rb_is_red(n->left) || bench_rb_is_red(n->right) )
            return -1;
    }

    if( n->left && n->left->parent != n )
        return -1;
    if( n->right && n->right->parent != n )
        return -1;

    int lh = bench_rb_validate_node(n->left, min_id, n->id, min_open, 1);
    if( lh < 0 )
        return -1;
    int rh = bench_rb_validate_node(n->right, n->id, max_id, 1, max_open);
    if( rh < 0 )
        return -1;
    if( lh != rh )
        return -1;

    return lh + (n->color == BENCH_RB_BLACK ? 1 : 0);
}

static inline int
bench_rb_validate(const struct BenchRbTree* t)
{
    assert(t);
    if( !t->root )
        return 1;
    if( t->root->color != BENCH_RB_BLACK )
        return 0;
    if( t->root->parent != NULL )
        return 0;
    int bh = bench_rb_validate_node(
        t->root, 0u, UINT32_MAX, 0, 0);
    return bh >= 0;
}

#endif /* BENCH_RBTREE_H */
