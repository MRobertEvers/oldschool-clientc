#ifndef SRC_GAME_RS_ATTACK_OPTION_H
#define SRC_GAME_RS_ATTACK_OPTION_H

/*
 * The Controls-settings "Attack" options (reference AttackOption, deob
 * osrs239 class75) — the two dropdowns on interface `settings_side`:
 *
 *   Player 'Attack' options   varp option_attackpriority (1107), clientcode 18
 *   NPC 'Attack' options      varp option_attackpriority_npc (1306), clientcode 22
 *
 * The numbers below are the enum's OWN ids, not positions in an array: the
 * reference looks a varp value up by id (Statics.method6898 over the values
 * array, which is stored out of order) and falls back to DEPENDS when the
 * value names nothing. Cache enums 3993 (player, keys 0..4) and 3991 (NPC,
 * keys 0..3) map a dropdown row to the same numbers, and the server writes the
 * chosen one straight into the varp (settings_side.rs2).
 *
 * The client is the only consumer: these change which menu rows exist and how
 * they sort, never what the server will accept. A player who sets Hidden can
 * still be walked into an attack by a script.
 */
enum RS_AttackOption
{
    /** "Depends on combat levels" — deprioritized when the target out-levels
     *  the local player. */
    RS_ATTACK_OPTION_DEPENDS = 0,
    /** "Always right-click" — never the left-click default. */
    RS_ATTACK_OPTION_RIGHTCLICK = 1,
    /** "Left-click where available" — normal priority, so Attack wins the
     *  left click whenever the target offers it. */
    RS_ATTACK_OPTION_LEFTCLICK = 2,
    /** "Hidden" — the Attack row is not emitted at all. */
    RS_ATTACK_OPTION_HIDDEN = 3,
    /** "Right-click for clanmates" — player dropdown only; behaves like
     *  Left-click except against a clan channel member. The NPC dropdown's
     *  enum has no key 4, but the reference decodes both varps through the
     *  same values array, so a stray 4 there is a value the NPC builder simply
     *  never tests. */
    RS_ATTACK_OPTION_CLAN = 4,
};

/**
 * Login/reset value (reference client.<clinit> and the game-state reset both
 * assign HIDDEN). Deliberately NOT the zero a fresh varp table holds: the
 * reference never derives these two from its varp array, so a server that does
 * not transmit the setting leaves every Attack row suppressed. That is why
 * `[proc,settings_side_login]` writes both varps even when the value is the
 * default 0.
 */
#define RS_ATTACK_OPTION_DEFAULT RS_ATTACK_OPTION_HIDDEN

/** Varp value -> option, with the reference's unknown-value fallback. */
static inline int
RS_AttackOption_FromVarp(int value)
{
    switch( value )
    {
    case RS_ATTACK_OPTION_DEPENDS:
    case RS_ATTACK_OPTION_RIGHTCLICK:
    case RS_ATTACK_OPTION_LEFTCLICK:
    case RS_ATTACK_OPTION_HIDDEN:
    case RS_ATTACK_OPTION_CLAN:
        return value;
    default:
        return RS_ATTACK_OPTION_DEPENDS;
    }
}

/** Varp clientcodes that carry the two settings (reference updateVarp arms 18
 *  and 22). */
enum RS_AttackOptionClientCode
{
    RS_ATTACK_OPTION_CLIENTCODE_PLAYER = 18,
    RS_ATTACK_OPTION_CLIENTCODE_NPC = 22,
};

#endif
