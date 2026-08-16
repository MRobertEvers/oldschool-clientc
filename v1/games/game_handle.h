#ifndef GAME_HANDLE_H
#define GAME_HANDLE_H

struct GameModelViewer;
struct GameRunescape;
struct GameInterfaceEditor;

enum GameHandleKind
{
    GAME_HANDLE_KIND_MODEL_VIEWER = 0,
    GAME_HANDLE_KIND_RUNESCAPE,
    GAME_HANDLE_KIND_INTERFACE_EDITOR,
};

struct GameHandle
{
    enum GameHandleKind kind;
    union
    {
        struct GameModelViewer* model_viewer;
        struct GameRunescape* runescape;
        struct GameInterfaceEditor* interface_editor;
    } u;
};

#endif