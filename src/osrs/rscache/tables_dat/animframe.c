#include "animframe.h"

#include "../rsbuf.h"

#include <stdlib.h>
#include <string.h>

// const buf = new Packet(data);
// buf.pos = data.length - 8;

// const headLength = buf.g2();
// const tran1Length = buf.g2();
// const tran2Length = buf.g2();
// const delLength = buf.g2();
// let pos = 0;

// const head = new Packet(data);
// head.pos = pos;
// pos += headLength + 2;

// const tran1 = new Packet(data);
// tran1.pos = pos;
// pos += tran1Length;

// const tran2 = new Packet(data);
// tran2.pos = pos;
// pos += tran2Length;

// const del = new Packet(data);
// del.pos = pos;
// pos += delLength;

// const baseBuf = new Packet(data);
// baseBuf.pos = pos;
// const base = new AnimBase(baseBuf);

// const total = head.g2();
// const labels: Int32Array = new Int32Array(500);
// const x: Int32Array = new Int32Array(500);
// const y: Int32Array = new Int32Array(500);
// const z: Int32Array = new Int32Array(500);

// for (let i: number = 0; i < total; i++) {
//     const id: number = head.g2();

//     const frame: AnimFrame = (this.instances[id] = new AnimFrame());
//     frame.delay = del.g1();
//     frame.base = base;

//     const groupCount: number = head.g1();
//     let lastGroup: number = -1;
//     let current: number = 0;

//     for (let j: number = 0; j < groupCount; j++) {
//         if (!base.types) {
//             throw new Error();
//         }

//         const flags: number = tran1.g1();
//         if (flags > 0) {
//             if (base.types[j] !== 0) {
//                 for (let group: number = j - 1; group > lastGroup; group--) {
//                     if (base.types[group] === 0) {
//                         labels[current] = group;
//                         x[current] = 0;
//                         y[current] = 0;
//                         z[current] = 0;
//                         current++;
//                         break;
//                     }
//                 }
//             }

//             labels[current] = j;

//             let defaultValue: number = 0;
//             if (base.types[labels[current]] === 3) {
//                 defaultValue = 128;
//             }

//             if ((flags & 0x1) === 0) {
//                 x[current] = defaultValue;
//             } else {
//                 x[current] = tran2.gsmart();
//             }

//             if ((flags & 0x2) === 0) {
//                 y[current] = defaultValue;
//             } else {
//                 y[current] = tran2.gsmart();
//             }

//             if ((flags & 0x4) === 0) {
//                 z[current] = defaultValue;
//             } else {
//                 z[current] = tran2.gsmart();
//             }

//             lastGroup = j;
//             current++;
//         }
//     }

//     frame.length = current;
//     frame.groups = new Int32Array(current);
//     frame.x = new Int32Array(current);
//     frame.y = new Int32Array(current);
//     frame.z = new Int32Array(current);

//     for (let j: number = 0; j < current; j++) {
//         frame.groups[j] = labels[j];
//         frame.x[j] = x[j];
//         frame.y[j] = y[j];
//         frame.z[j] = z[j];
//     }
// }

struct CacheDatAnimBaseFrames*
cache_dat_animbaseframes_new_decode(
    char* data,
    int data_size)
{
    struct CacheDatAnimBaseFrames* animbaseframes = malloc(sizeof(struct CacheDatAnimBaseFrames));
    memset(animbaseframes, 0, sizeof(struct CacheDatAnimBaseFrames));

    struct RSBuffer buffer = { .data = data, .size = data_size, .position = 0 };
    struct RSBuffer head_buffer = { .data = data, .size = data_size, .position = 0 };
    struct RSBuffer tran1_buffer = { .data = data, .size = data_size, .position = 0 };
    struct RSBuffer tran2_buffer = { .data = data, .size = data_size, .position = 0 };
    struct RSBuffer del_buffer = { .data = data, .size = data_size, .position = 0 };

    buffer.position = data_size - 8;

    int head_length = g2(&buffer);
    int tran1_length = g2(&buffer);
    int tran2_length = g2(&buffer);
    int del_length = g2(&buffer);

    int pos = 0;
    head_buffer.position = pos;

    pos += head_length + 2;
    tran1_buffer.position = pos;

    pos += tran1_length;
    tran2_buffer.position = pos;

    pos += tran2_length;
    del_buffer.position = pos;

    pos += del_length;
    animbaseframes->base = cache_dat_animbase_new_decode(data + pos, data_size - pos);

    int total = g2(&head_buffer);
    static int labels[500];
    static int x[500];
    static int y[500];
    static int z[500];
    memset(labels, 0, 500 * sizeof(int));
    memset(x, 0, 500 * sizeof(int));
    memset(y, 0, 500 * sizeof(int));
    memset(z, 0, 500 * sizeof(int));

    animbaseframes->frames = malloc(total * sizeof(struct CacheAnimframe));
    memset(animbaseframes->frames, 0, total * sizeof(struct CacheAnimframe));
    animbaseframes->frame_count = total;

    for( int i = 0; i < total; i++ )
    {
        struct CacheAnimframe* animframe = &animbaseframes->frames[i];
        int id = g2(&head_buffer);

        animframe->id = id;
        animframe->base = animbaseframes->base;
        animframe->delay = g1(&del_buffer);

        int group_count = g1(&head_buffer);
        int last_group = -1;
        int current = 0;

        for( int j = 0; j < group_count; j++ )
        {
            int flags = g1(&tran1_buffer);
            if( flags > 0 )
            {
                if( animframe->base->types[j] != 0 )
                {
                    for( int group = j - 1; group > last_group; group-- )
                    {
                        if( animframe->base->types[group] == 0 )
                        {
                            labels[current] = group;
                            x[current] = 0;
                            y[current] = 0;
                            z[current] = 0;
                            current++;
                            break;
                        }
                    }
                }
            }

            labels[current] = j;

            int default_value = 0;
            if( animframe->base->types[labels[current]] == 3 )
            {
                default_value = 128;
            }

            if( (flags & 1) == 0 )
            {
                x[current] = default_value;
            }
            else
            {
                x[current] = gshortsmart(&tran2_buffer);
            }

            if( (flags & 2) == 0 )
            {
                y[current] = default_value;
            }
            else
            {
                y[current] = gshortsmart(&tran2_buffer);
            }

            if( (flags & 4) == 0 )
            {
                z[current] = default_value;
            }
            else
            {
                z[current] = gshortsmart(&tran2_buffer);
            }

            last_group = j;
            current++;
        }

        animframe->length = current;
        animframe->groups = malloc(current * sizeof(int));
        animframe->x = malloc(current * sizeof(int));
        animframe->y = malloc(current * sizeof(int));
        animframe->z = malloc(current * sizeof(int));
        memset(animframe->groups, 0, current * sizeof(int));
        memset(animframe->x, 0, current * sizeof(int));
        memset(animframe->y, 0, current * sizeof(int));
        memset(animframe->z, 0, current * sizeof(int));

        for( int j = 0; j < current; j++ )
        {
            animframe->groups[j] = labels[j];
            animframe->x[j] = x[j];
            animframe->y[j] = y[j];
            animframe->z[j] = z[j];
        }
    }

    return animbaseframes;
}

// this.length = buf.g1();

// this.types = new Uint8Array(this.length);
// this.labels = new TypedArray1d(this.length, null);

// for (let i = 0; i < this.length; i++) {
//     this.types[i] = buf.g1();
// }

// for (let i = 0; i < this.length; i++) {
//     const count = buf.g1();
//     this.labels[i] = new Uint8Array(count);

//     for (let j = 0; j < count; j++) {
//         this.labels[i]![j] = buf.g1();
//     }
// }

struct CacheAnimBase*
cache_dat_animbase_new_decode(
    char* data,
    int data_size)
{
    struct CacheAnimBase* animbase = malloc(sizeof(struct CacheAnimBase));
    memset(animbase, 0, sizeof(struct CacheAnimBase));

    struct RSBuffer buffer = { .data = data, .size = data_size, .position = 0 };

    int length = g1(&buffer);
    animbase->length = length;
    animbase->types = malloc(length * sizeof(int));
    animbase->labels = malloc(length * sizeof(int*));
    memset(animbase->types, 0, length * sizeof(int));
    memset(animbase->labels, 0, length * sizeof(int*));

    animbase->label_counts = malloc(length * sizeof(int));
    memset(animbase->label_counts, 0, length * sizeof(int));

    for( int i = 0; i < length; i++ )
    {
        animbase->types[i] = g1(&buffer);
    }

    for( int i = 0; i < length; i++ )
    {
        int count = g1(&buffer);
        animbase->labels[i] = malloc(count * sizeof(int));
        animbase->label_counts[i] = count;
        memset(animbase->labels[i], 0, count * sizeof(int));

        for( int j = 0; j < count; j++ )
        {
            animbase->labels[i][j] = g1(&buffer);
        }
    }
    return animbase;
}