#ifndef ENGINE_PLAYER_APPEARANCE_H
#define ENGINE_PLAYER_APPEARANCE_H

/*
 * Minimal player appearance used to composite a default avatar for interface
 * model widgets (clientCode 327/328) when there is no server-driven appearance.
 *
 * Body parts (male IdentityKit body_part_id): 0 hair, 1 jaw, 2 torso, 3 arms,
 * 4 hands, 5 legs, 6 feet. colors[] index the OSRS design palettes
 * (hair, torso, legs, feet, skin); the default appearance uses index 0, which
 * is the identity palette entry (see player_appearance.c).
 */

#define PLAYER_APPEARANCE_PARTS 7
#define PLAYER_APPEARANCE_COLORS 5
/* How far to scan IdentityKit configs when discovering default body kits. */
#define PLAYER_IDK_SCAN_MAX 1024

struct CacheProvider;

struct PlayerAppearance
{
    int gender;                          /* 0 = male, 1 = female */
    int kits[PLAYER_APPEARANCE_PARTS];   /* IdentityKit id per body part, -1 = none */
    int colors[PLAYER_APPEARANCE_COLORS];
};

/*
 * Fill `out` with the default male appearance: colors all 0 and the first
 * selectable IdentityKit for each body part, discovered from the provider's
 * ALREADY-LOADED idk configs (call CreateTask_PlayerAppearanceLoad first to
 * prefetch them). Returns the number of body parts resolved (0..7).
 */
int
PlayerAppearance_ResolveDefaultMale(
    struct CacheProvider* provider,
    struct PlayerAppearance* out);

/*
 * Async task: scan/load idk configs to discover the default male body kits, then
 * load every model referenced by those kits, so the synchronous compositor can
 * build the avatar without touching IO. WASM-safe (yields through CacheProvider).
 */
struct ToriRS_Task*
CreateTask_PlayerAppearanceLoad(struct CacheProvider* provider);

/*
 * Default-appearance body recolor pairs (colors[] all 0). Apply
 * `From(c) -> To(c)` with ToriDraw_ModelRecolor for each c where From(c) >= 0.
 */
int
PlayerAppearance_DefaultBodyRecolorCount(void);
int
PlayerAppearance_DefaultBodyRecolorFrom(int c);
int
PlayerAppearance_DefaultBodyRecolorTo(int c);

#endif
