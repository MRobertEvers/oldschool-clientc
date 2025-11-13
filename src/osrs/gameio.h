#ifndef GAMEIO_H
#define GAMEIO_H

#include <stdbool.h>

enum GameIORequestKind
{
    E_GAMEIO_REQUEST_ARCHIVE_LOAD,
};

enum GameIOStatus
{
    E_GAMEIO_STATUS_OK,
    E_GAMEIO_STATUS_ERROR,
    E_GAMEIO_STATUS_WAIT,
    E_GAMEIO_STATUS_PENDING,
};

struct GameIORequest
{
    struct GameIORequest* next;

    enum GameIORequestKind kind;
    enum GameIOStatus status;
    int request_id;

    void* sidecar_nullable;

    struct
    {
        int table_id;
        int archive_id;

        struct CacheArchive* out_archive_nullable;
    } _archive_load;

    struct
    {
        int table_id;
        struct ReferenceTable* out_table_nullable;
    } _cache_load;
};

struct GameIO
{
    int request_counter;
    struct GameIORequest* requests;

    int quit;

    int w_pressed;
    int a_pressed;
    int s_pressed;
    int d_pressed;

    int up_pressed;
    int down_pressed;
    int left_pressed;
    int right_pressed;
    int space_pressed;

    int f_pressed;
    int r_pressed;

    int m_pressed;
    int n_pressed;
    int i_pressed;
    int k_pressed;
    int l_pressed;
    int j_pressed;
    int q_pressed;
    int e_pressed;

    int comma_pressed;
    int period_pressed;

    double time_delta_accumulator_seconds;
};

struct GameIO* gameio_new(void);
void gameio_free(struct GameIO* gameio);

enum GameIOStatus gameio_request_new_archive_load(
    struct GameIO* io, int table_id, int archive_id, struct GameIORequest** out);

struct CacheArchive* gameio_request_archive(struct GameIORequest* request);

bool gameio_next(struct GameIO* io, enum GameIOStatus status, struct GameIORequest** out);
void gameio_remove(struct GameIO* io, int request_id);
bool gameio_is_idle(struct GameIO* io);
bool gameio_resolved(enum GameIOStatus status);

char const* gameio_status_cstr(enum GameIOStatus status);

#endif