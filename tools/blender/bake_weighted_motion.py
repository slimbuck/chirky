"""Bake the C simulation's start/stop trace into an inspectable Blender action.

Run make test first, then run this with Blender --background --python.
The preview action is not exported as a loop: gameplay evaluates inertia live.
"""
import bpy
import csv
import math
from pathlib import Path

root = Path(__file__).resolve().parents[2]
model = root / "games/phosphor-run/assets/models/phosphor-robot.blend"
bpy.ops.wm.open_mainfile(filepath=str(model))
rig = next(obj for obj in bpy.data.objects if obj.get("chirky_robot"))
old = bpy.data.actions.get("Start and stop - weighted preview")
if old:
    bpy.data.actions.remove(old)
action = bpy.data.actions.new("Start and stop - weighted preview")
action.use_fake_user = True
rig.animation_data.action = action
with (root/"build/robot-motion.csv").open() as file:
    rows = list(csv.DictReader(file))
angle = previous = 0.0
for row in rows:
    frame = int(row["frame"])
    lean = float(row["lean"])
    value = float(row["roll"])
    angle += (value-previous+math.pi) % math.tau-math.pi
    previous = value
    for bone in rig.pose.bones:
        bone.rotation_mode = "XYZ"
        bone.rotation_euler = (0, 0, 0)
        bone.location = (0, 0, 0)
    rig.pose.bones["body"].rotation_euler.x = angle
    head = rig.pose.bones["head"]
    head.rotation_euler.x = lean
    head.location.z = lean*.55  # local Z points towards world -Y.
    for bone in rig.pose.bones:
        bone.keyframe_insert(data_path="rotation_euler", frame=frame, group=bone.name)
        bone.keyframe_insert(data_path="location", frame=frame, group=bone.name)
scene = bpy.context.scene
scene.frame_start, scene.frame_end, scene.render.fps = 1, len(rows), 60
scene.frame_set(1)
bpy.context.preferences.filepaths.save_version = 0
bpy.ops.wm.save_as_mainfile(filepath=str(model))
scene.render.resolution_x = scene.render.resolution_y = 320
scene.cycles.samples = 12
scene.render.film_transparent = True
folder = root/"build/robot-weighted"
folder.mkdir(parents=True, exist_ok=True)
for frame in range(1, len(rows)+1, 3):
    scene.frame_set(frame)
    scene.render.filepath = str(folder/f"{frame:03d}.png")
    bpy.ops.render.render(write_still=True)
