# Bramble Hollow

A playable proof of concept for a gentle, LLM-directed woodland adventure on
Chirky Platform API 9.

## Controls

- D-pad: walk or steer
- Y: mount/dismount the bicycle
- B: interact, talk, chop, plant, harvest, or tend the fire
- X: open the world controls for weather, time, and plant growth
- A: close dialogue or the world controls

The game is complete without a network connection. `assets/director.conf` holds
the defaults; `runtime/director.conf` is a small hot-reloaded control plane with
long-, medium-, and short-term state. Run
`node tools/bramble-director.mjs --once` from the repository root to have a
model revise it from recent game events, or use `--watch` to keep it running.
The process reads `OPENAI_API_KEY`; `BRAMBLE_MODEL` defaults to `gpt-6-luna`.

The model never controls collision, inventory arithmetic, coordinates, or C
code. It can choose bounded weather/growth values and write story and dialogue
strings. Chirky remains authoritative over the simulation.
