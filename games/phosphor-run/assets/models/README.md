# Phosphor robot

Original low-poly model based on the robot in this game's splash artwork.
Created in Blender 5.2 through the installed Blender MCP extension. No downloaded
model, external animation service, account or retargeting licence is required.

- `phosphor-robot.blend`: editable meshes, materials, 3-bone rigid armature,
  six animation actions, packed splash reference, portrait camera and lights.
- `player.robot`: the only model file used by the game. 30,208 bytes; 654 vertices,
  1,224 triangles, 9 flat materials and 68 sampled poses.
- `phosphor-robot.png`: reference portrait of the actual Blender mesh.
- `phosphor-robot-run.webp`: animated preview of the actual Blender run action.
- `model-info.json`: generated geometry and animation counts.

The head and antenna are retained; a single teal rolling ball replaces the torso,
arms and legs. The ball diameter is 1.10 model units, approximately the helmet's 1.02-unit width.
Three rigid bones control the root, ball and stabilised head.
Brass tracks and a status marker show the ball's rotation. Idle gently rocks;
run rolls through a full revolution; jump and fall spin the ball and tilt the
head; dash rolls faster; death tips the head. All six game states remain animated.
The game resets the clip clock on transitions. Pausing also pauses animation.
Facing left mirrors the projected geometry.

In gameplay, acceleration and braking add a live inertia layer. The head leans
first; the ball takes up drive four simulation ticks later. Rolling follows
actual horizontal travel and keeps its phase across idle/run transitions.
On release the ball brakes quickly while a damped head spring follows through,
overshoots slightly and settles. This affects appearance, not collision or
control responsiveness. The same calculation runs in native C and WASM.

The Blender action **Start and stop - weighted preview** demonstrates this
timing. It is baked from `build/robot-motion.csv`, produced by the runtime test;
`tools/blender/bake_weighted_motion.py` refreshes it and its rendered preview.
This extra action is for inspection; the six exported clips remain unchanged.

The character is a live 3D mesh, not a baked sprite sheet. The C renderer
interpolates skeletal matrices, transforms vertices, lights and depth-tests
triangles in a fixed 64×64 scratch buffer, then resolves it to a 32×32 transparent
footprint. Only occupied colour runs reach the existing rectangle batcher.
The standing character is about 17 pixels tall, including the antenna. Its contact point
stays anchored to the original player position. The existing 12×14 collision body
and level geometry are unchanged; artwork extends beyond that body.

No textures, physics rig, runtime Blender dependency, full-screen software 3D or
host ABI change. Native C and browser WASM use the same renderer and asset.
The old `player-*` sprites remain a fallback if the model is missing or invalid;
editing those sprites does not change the normal 3D player.

## Edit and export

Open the `.blend`, select the armature, and choose an action in the Action Editor.
Meshes have a `rigid_bone` property and a matching single-weight vertex group.
Keep rigid weights and the existing bone/action names. Apply any additional
geometry modifiers before exporting; the runtime exporter reads the base mesh.
The custom `clip_specs` property records samples and ticks per sample (60 Hz).

After saving edits, export with:

```powershell
& 'C:/Program Files/Blender Foundation/Blender 5.2/blender.exe' --background games/phosphor-run/assets/models/phosphor-robot.blend --python tools/blender/export_phosphor_robot.py
```

Restart the browser game to load the new model. Run `make web` to refresh a
standalone web distribution. Asset changes do not require a C rebuild.

To recreate the original model from scratch, run
`tools/blender/create_phosphor_robot.py` in a **dedicated background Blender
process**. That generator clears its scene; it does not preserve hand edits.
The exporter above preserves your edits.

To use the installed MCP extension:

```powershell
& 'C:/Program Files/Blender Foundation/Blender 5.2/blender.exe' --background --command blender_mcp
# In another terminal:
python tools/blender/mcp_execute.py tools/blender/create_phosphor_robot.py
```

`tests/robot_runtime.c` checks all clips, both facings, determinism, clipping,
malformed data and repeated cleanup. It also measures 1,000 animated frames and
writes a pose sheet into `build/`. The test reports desktop frame cost including the pixel callback. This is not a Pi benchmark;
the Pi was unreachable during development.

## Runtime format

Little-endian `PRB1`, six uint32 counts (vertices, triangles, bones, materials,
clips, pose frames); RGBA-like palette bytes (RGB plus emissive flag); vertices
as three float32 positions plus uint32 bone; triangles as three uint16 indices
plus uint16 material; six clip records (first frame, count, ticks/sample, loop)
as four uint32 values; then frame-major, bone-major 3×4 float32 skin matrices.
The loader checks counts, indices, finite values, truncation and trailing data.
Limits: 2,048 vertices, 4,096 triangles, 32 bones/materials and 256 pose frames.
