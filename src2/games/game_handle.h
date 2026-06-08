#ifndef GAME_HANDLE_H
#define GAME_HANDLE_H

struct GameModelViewer;
struct GameRunescape;

enum GameHandleKind
{
    GAME_HANDLE_KIND_MODEL_VIEWER = 0,
    GAME_HANDLE_KIND_RUNESCAPE,
};

struct GameHandle
{
    enum GameHandleKind kind;
    union
    {
        struct GameModelViewer* model_viewer;
        struct GameRunescape* runescape;
    } u;
};

#endif