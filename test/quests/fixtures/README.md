# Quest fixtures

Server save files, one per quest starting state. `[varps]` carries only
`scope=perm` vars -- which is exactly the quest progress set. A fixture that
pins a temp var is pinning a number the server recomputes, and it will drift.

Named for the state, not the quest step number: `cooks_assistant_start.ini`,
not `cooks_assistant_3.ini`. A step number stops being true the first time a
step is inserted.
