#ifndef GAME_HANDLE_H
#define GAME_HANDLE_H

struct GameModelViewer;

enum GameHandleKind
{
    GAME_HANDLE_KIND_MODEL_VIEWER = 0,
};

struct GameHandle
{
    enum GameHandleKind kind;
    union
    {
        struct GameModelViewer* model_viewer;
    } u;
};

#endif