#ifndef RS_MINIMAP_STATE_H
#define RS_MINIMAP_STATE_H

enum RS_MinimapPermission
{
    RS_MINIMAP_DRAW_MAP = 1,
    RS_MINIMAP_DRAW_COMPASS = 2,
    RS_MINIMAP_WALK = 4,
};

/* The six states in the versioned RSProt239 MinimapToggle message model.
 * Keep presentation and action permission separate: state 1 shows the map
 * but disables walking; state 3 hides only the compass. Unknown states do
 * not gain a permission by falling through a != comparison. */
static inline unsigned RS_MinimapPermissions(int state)
{
    switch( state )
    {
    case 0: return RS_MINIMAP_DRAW_MAP | RS_MINIMAP_DRAW_COMPASS | RS_MINIMAP_WALK;
    case 1: return RS_MINIMAP_DRAW_MAP | RS_MINIMAP_DRAW_COMPASS;
    case 2: return RS_MINIMAP_DRAW_COMPASS;
    case 3: return RS_MINIMAP_DRAW_MAP | RS_MINIMAP_WALK;
    case 4: return RS_MINIMAP_DRAW_MAP;
    default: return 0;
    }
}
#endif
