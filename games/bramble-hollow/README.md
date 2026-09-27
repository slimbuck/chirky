# Bramble Hollow

A playable proof of concept for a gentle, LLM-directed woodland adventure on
Chirky Platform API 10.

## Controls

- D-pad: walk or steer
- Y: mount/dismount the bicycle
- B: interact, talk, chop, plant, harvest, or tend the fire
- X: open the world controls for weather, time, and plant growth
- A: close dialogue or the world controls

The game is complete without a network connection. `assets/director.conf` holds
its defaults. On API10 hosts it sends bounded events to the URL in `game.conf`
and polls revisioned long-, medium-, and short-term state without blocking a
frame. Start the standalone service with `node director/server.mjs`; it reads
`OPENAI_API_KEY`, and `BRAMBLE_MODEL` defaults to `gpt-6-luna`. See
[`director/README.md`](../../director/README.md) for operation and diagnostics.

Older hosts without the network callbacks retain the `runtime/director.conf`
file fallback. Networked hosts do not mix remote state with that local file.

The model never controls collision, inventory arithmetic, coordinates, or C
code. It can choose bounded weather/growth values and write story and dialogue
strings. Chirky remains authoritative over the simulation.
