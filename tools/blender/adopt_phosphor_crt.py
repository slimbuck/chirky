"""Adopt the selected Pocket CRT study and adapt its rig for runtime animation.

First generate studies with compare_phosphor_proportions.py -- --crt, then run
Blender --background --python tools/blender/adopt_phosphor_crt.py. Subsequent
edits use the saved .blend and the normal export_phosphor_robot.py command.
"""
import bpy
import json
import sys
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from export_phosphor_robot import export_robot
bpy.ops.wm.open_mainfile(filepath=str(ROOT / "build/robot-options/crt1/robot.blend"))
rig = next(o for o in bpy.data.objects if o.type == "ARMATURE" and o.get("chirky_robot"))
rig["face_rig"] = json.dumps(dict(body_radius=.396, head_pivot_z=.85,
    neck_min_z=.7249, neck_max_z=.9079, origin=[0,-.472,1.25],
    u=[1,0,0], v=[0,0,1], normal=[0,-1,0], half_width=.5725,
    half_height=.3675, bulge=.09, eye_spacing=.255, eye_v=.055))
for obj in bpy.data.objects:
    if obj.type != "MESH" or obj.parent != rig:
        continue
    if obj.name.startswith("Phosphor eye"):
        obj["face_part"] = 1 if sum(v.co.x for v in obj.data.vertices) < 0 else 2
    elif obj.name == "Tiny smile": obj["face_part"] = 3
    elif obj.name == "Head stabiliser": obj["face_part"] = 4
    else: obj["face_part"] = 0
bpy.ops.object.select_all(action="DESELECT")
rig.select_set(True)
bpy.context.view_layer.objects.active = rig
bpy.ops.object.mode_set(mode="EDIT")
for name, z in [("body",.396),("head",.85)]:
    bone = rig.data.edit_bones[name]
    delta = Vector((0,0,z))-bone.head
    bone.head += delta
    bone.tail += delta
bpy.ops.object.mode_set(mode="OBJECT")
rig.animation_data.action = bpy.data.actions["idle"]
bpy.context.scene.frame_set(1)
rig["reference"] = "Selected Pocket CRT, A proportions, larger eyes. Face UV basis and vertex semantics are exported in PRB2."
out = ROOT / "games/phosphor-run/assets/models"
bpy.context.preferences.filepaths.save_version = 0
bpy.ops.wm.save_as_mainfile(filepath=str(out / "phosphor-robot.blend"))
print(export_robot(ROOT))
bpy.context.scene.render.filepath = str(out / "phosphor-robot.png")
bpy.context.scene.cycles.samples = 16
bpy.ops.render.render(write_still=True)
