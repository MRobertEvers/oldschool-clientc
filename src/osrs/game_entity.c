#include "game_entity.h"

void
entity_add_hitmark(
    int* damage_values,
    int* damage_types,
    int* damage_cycles,
    int loop_cycle,
    int damage_type,
    int damage_value)
{
    for( int i = 0; i < ENTITY_DAMAGE_SLOTS; i++ )
    {
        if( damage_cycles[i] <= loop_cycle )
        {
            damage_values[i] = damage_value;
            damage_types[i] = damage_type;
            damage_cycles[i] = loop_cycle + 70;
            return;
        }
    }
}
