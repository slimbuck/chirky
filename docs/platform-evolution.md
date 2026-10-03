# Platform evolution

Status: design direction, not an implemented API or a commitment to a backend
or transport. The current ABI is 15. This plan preserves equally supported
Pi/Linux and desktop/mobile browser consoles while making room for personal
content, global scores and multiplayer.

## Starting point

Chirky already shares its console, lifecycle and game code across native and
browser hosts. Games receive logical input and host-owned rendering, assets,
audio, local storage and optional world-director services. The game catalog
comes from game manifests. These are useful boundaries to retain.

| Area | Implemented today | Missing for the proposed features |
| --- | --- | --- |
| Games | Three unfinished games and a hardware diagnostic | No player-to-player sessions |
| Content | Game-owned data, editor descriptions, validation and local dashboard saves | Personal content overlays and portable user-content packages |
| Scores | Phosphor Run stores local per-level records | Online identity, board definitions, submission and verification |
| Services | Bramble director exchanges revisioned world state | General player-facing services and real-time session transport |
| Deployment | Dashboard copies source/assets and builds on one configured Pi; web publishes a static package | Content installation independent of developer source deployment |
| Release identity | Web `build.json` records source commit and file hashes | A shared native/web description of gameplay compatibility |

The dashboard binds to loopback and holds Pi administration powers. Its
editors save into the developer's repository. The public site contains the
player, not these authoring or administration APIs. The optional director is
a private-network proof of concept without player authentication. Neither
service should become a public backend just by exposing its existing port.

## First foundation: identify the content and rules of a run

Before global scores or multiplayer, give each launch an immutable description
of what will run. A proposed description includes:

- Stable game ID and explicit gameplay/rules revision.
- Content package ID, schema version and digest of the relevant content.
- Level ID plus its content revision; an ID alone survives edits and is not
  enough to distinguish two challenges.
- Effective gameplay settings and mode, including playtest overrides, starting
  state and a seed when randomness affects the challenge.
- Provenance: official release, local draft or imported community content.
- Simulation/protocol compatibility version when sessions or replay require it.

Generate this description from the configuration and assets actually loaded,
not just the checked-in defaults. Freeze it for the run; restarting with edits
creates a new description. Define canonical serialization, included files and
default handling so Pi and browser produce the same gameplay identity. Keep
machine settings, display preferences, service URLs and secrets out of it.
If viewport dimensions affect gameplay or visibility, define a competitive
viewport or include the relevant dimensions in the rules policy.

Reuse existing catalog and package tooling rather than adding a second game
registry. The web release hash identifies a deployment, but native binaries
and WASM necessarily differ. Use compatible rules and content identities for
cross-platform play, with platform build hashes retained separately for
diagnostics. Cosmetic-only releases need not create new score boards when the
rules policy explicitly classifies those assets as cosmetic.

This metadata describes a run; it does not establish that a client honestly
executed it. The server must recognize approved releases and apply its own
score-validation policy. A client-provided `official` flag or hash is not proof.

## Personal editing and upstream contributions

Keep the shipped game package immutable during personal editing. Store a
user's draft separately, based on a named package revision, and resolve its
validated overrides at launch. The host prepares one effective asset view
before game initialization, so game code keeps ordinary game-relative paths.
Host caches must distinguish content revisions and invalidate changed files;
browser files cached by game ID alone cannot support switching drafts safely.

Separate content from executable code. Initial user packages should contain
supported levels and bounded settings, with additional asset formats added
deliberately. They must not replace C, JS, WASM or shared libraries, supply
arbitrary service URLs, or change host configuration. Code contributions can
still use the normal reviewed source/PR workflow.

Extract reusable editor validation from the local dashboard as needed. A public
editor should use a draft-storage adapter rather than invoking repository writes
or SSH actions. A browser can persist drafts in IndexedDB and export/import
them; a Pi can install the same validated package into a user-content directory.
This is a proposed storage choice, not current behavior. Do not put large asset
bundles into the existing 64 KiB save-record interface.

Namespace local records by the relevant content/rules identity where needed,
so a tweaked level does not overwrite or compete with official scores. Define
how the existing Phosphor records remain accessible; do not silently treat
historic local records as verified online results. The current local boards
use level IDs only and do not yet isolate edited revisions.

The contribution path can start with an exported package attached to an issue
or PR. A later submission service can accept a package, creator attribution,
parent revision and permission to redistribute, validate it, and queue it for
review. Submission and approval are distinct. Inclusion in the official console
still goes through reviewed source/content and the existing release process.
Automatic publishing, a public marketplace and arbitrary executable mods are
not prerequisites for useful local editing.

## Global scores

Introduce a separate score service with a narrow, versioned request/response
contract shared by Pi and browser adapters. Keep single-player play and local
scores working when the service is unavailable. A first UI can be web-first,
but score semantics and player identity should not depend on browser APIs.

Define each board by game, rules revision, content revision and mode, including
units, ordering and tie handling. Phosphor Run's elapsed simulation ticks are
a useful first metric: lower is better. Edited levels and tuning need distinct
boards or explicitly unranked play, rather than entering an official board.

Use a server-issued stable player ID; initials are display text, not identity.
Guests can be a first step, with account linking or device pairing added when
cross-device identity is needed. Browser credentials and Pi pairing credentials
belong to host/service adapters. No shared secret embedded in a downloadable
client can establish score legitimacy.

Submission needs a unique run ID for deduplication, bounded payloads, explicit
pending/accepted/rejected/offline states, rate limits and validated display
names. Retrying a timed-out request must not insert another result. A persisted
retry queue must also have bounds and an expiry policy. Reads should support
bounded pages rather than downloading all players.

Choose and communicate the initial trust level. A casual board can accept
client reports with sanity checks and abuse controls, labeled accordingly.
Competitive verification requires stronger evidence, such as input replay
against known rules/content or an authoritative simulation. Authentication,
TLS and release hashes alone do not prevent fabricated scores. Replay would
first require controlled seeds, initial state and tested simulation behavior.

## Two-console multiplayer

Use a small two-player LAN game as the first experiment, primarily on two Pis.
Start with explicit host/join and a configured address; discovery and internet
matchmaking can follow. For a simple prototype, one peer can own the
authoritative simulation while the other sends tick-numbered inputs and
receives snapshots. Measure latency and gameplay feel before choosing input
delay, prediction or rollback. This is a prototype recommendation, not a
universal networking model for every game.

Keep simulation, session policy and transport distinct. A future portable
session contract needs player slots, local/remote inputs, protocol and content
compatibility, bounded message sizes/queues, and connection state. Linux owns
sockets; browser adapters own browser-supported transport. Gameplay defines
its state and authority rules. Do not serialize raw C structs across the wire.

The current game update API receives one logical controller, so a networked
game will need a deliberate extension for multiple players. Build and test that
contract with its first consumer on both hosts, rather than adding unused
callbacks now. Networking must remain asynchronous and pending work must be
cancelled or ignored after leaving a session or switching games.

The browser runs fixed 60 Hz updates; the native loop currently advances with
display presentation. Establish an explicit simulation tick for network games
independent of rendering and scheduling. Fixed ticks alone do not prove
determinism: seeds, input ordering, floating-point differences and state
initialization matter. Require replay/checksum evidence across ARM native and
WASM before depending on lockstep or rollback determinism.

Normal browser pages cannot join an arbitrary native UDP/TCP game protocol.
Evaluate a browser-compatible WebSocket relay or WebRTC data channels alongside
the LAN prototype; WebRTC also brings signaling and possible relay needs.
Keep the portable session semantics consistent even if the transports differ.
Do not make direct Pi sockets the only supported session model.

Specify host departure, packet loss, timeout, rejoin and mismatched content
behavior. Decide how Menu, pause and a backgrounded mobile tab affect a match:
one player's local pause cannot silently stop both simulations. Bramble's
eventually-consistent `/v1/sync` world updates are unsuitable for frame-level
player input; keep that service separate.

## Suggested implementation order and acceptance checks

1. **Content/run identity and isolated drafts.** Implement canonical identity
   generation and one local draft/playtest flow, starting with Phosphor Run.
   Verify the same content/settings yield the same identity on both hosts;
   editing a level or physics changes it, and draft saves leave official data
   intact. Cover path traversal, size limits, malformed imports, stale edits,
   cache invalidation and switching back to the official game.
2. **One online board.** Add a small backend and the first host service contract
   for Phosphor Run. Test accepted/rejected submissions, duplicate retries,
   offline play, storage failure, edited-content separation and game unload
   during a request. Choose the identity and score-trust policy explicitly.
3. **One two-player LAN experiment.** Add a session contract and a minimal game,
   exercise two real Pis, and prove a browser-compatible transport early.
   Test version mismatch, latency/loss, disconnect, pause/background behavior
   and orderly return to the launcher. Let this establish what abstractions
   are worth sharing with later games.
4. **Community submission workflow.** Build on portable content exports and
   validation. Add hosted review tooling only when manual submissions justify
   it. Verify that submitting content never publishes it or changes a release.

Scores and multiplayer can proceed independently once their identity needs are
met. This order does not require completing all existing games first. Backend
provider, account system, transport and anti-cheat strength remain decisions
for their first concrete implementations. No cloud infrastructure, unused ABI
extensions or wholesale engine rewrite are required by this documentation pass.
