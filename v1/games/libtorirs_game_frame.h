#ifndef LIBTORIRS_GAME_FRAME_H
#define LIBTORIRS_GAME_FRAME_H

/**
 * Greedy render evaluation: each game owns a frame state machine.
 * The platform drives LibToriRS_FrameBegin / FrameNextCommand / FrameEnd;
 * the instance delegates to the active game, which must implement:
 *
 *   void <game>_frame_begin(struct <Game>* game);
 *   bool <game>_frame_next_command(struct <Game>* game,
 *                                  struct LibToriRS_RenderCommand* command);
 *   void <game>_frame_end(struct <Game>* game);
 *
 * Commands are not buffered; the game produces one command per next_command call.
 */

struct LibToriRS_RenderCommand;

#endif
