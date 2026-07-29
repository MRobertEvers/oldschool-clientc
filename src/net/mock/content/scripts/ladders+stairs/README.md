# Ladders and stairs

Empty on purpose.

A ladder's or a staircase's direction is already in the cache, as the loc's own
menu text — "Climb-up", "Climb-down", or a bare "Climb" on the middle of a
spiral staircase. `handle_oploc` reads it there and moves the player a level,
which covers every ladder and staircase in Lumbridge without a line of content.

What would go here is the exceptions: the stairs that land you somewhere other
than directly above or below, which need a destination coordinate the cache does
not carry. None of the ones around Lumbridge do.
