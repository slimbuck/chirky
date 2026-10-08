# Circuit Clash

A first local two-player fighter, using geometric placeholder fighters and a
small arena. There are no generated or scaled sprite assets in this prototype.

Choose Circuit Clash in the launcher, release the launch button, then press a
Primary or Secondary action on each input source to join. A source cannot join
twice. One keyboard provides two sources: P1 arrows + N/M and P2 WASD + F/G, remappable
independently through Input Settings. Enter (Start) and Esc (Menu) are shared;
neither joins a player. P1 also controls menus and solo games. Two controllers,
controller plus either keyboard layout, and browser touch plus another source
are also supported.

- Left/right move, with opposing directions cancelling.
- Primary punches: short wind-up, one hit per press, recovery.
- Secondary or Up jumps.
- Five landed punches win. Simultaneous strikes can produce a double KO.
- After a KO, both players press their own punch button to rematch.
- Menu uses the normal console pause menu. Either player can pause.

A missing device freezes the match and shows a join card for that player. Press
an action button on an unassigned source to replace it. Health and positions survive;
release all buttons for the resume countdown. Unplugged gamepads never inherit
another player's assignment automatically. Leaving/relaunching clears slots.

Pi pads currently share the configured native controller bindings and legend
profile, so use compatible SNES adapters or one pad and keyboard. Browser pads
use their existing saved model profiles; identical models share a mapping but
have distinct connection IDs and player slots. Prompts follow each assigned
source, including keyboard remaps. Other games keep combined input.

Validation: `make test`, `make web`, and
`node tools/circuit-clash-browser.cjs http://127.0.0.1:3030/play/`.
The browser check simulates pads; real USB controller testing remains a separate
hardware check. This game is local only and adds no network multiplayer.
