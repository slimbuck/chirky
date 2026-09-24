"""Reproducible original robot, rigid rig and six animation actions.

Run in Blender via mcp_execute.py or blender --background --python this_file.
Only clears the scene in this dedicated background Blender process.
"""
import bpy
import math
import json
import sys
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "games/phosphor-run/assets/models"
OUT.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
for action in list(bpy.data.actions):
    bpy.data.actions.remove(action)

# Z is up; the robot looks towards -Y. Warm enamel, cool machinery, luminous eyes.
palette = [
    ("Ochre enamel", (0.78, 0.43, 0.10), 0),
    ("Sunlit brass", (1.0, 0.73, 0.27), 0),
    ("Deep teal casing", (0.045, 0.23, 0.24), 0),
    ("Visor glass", (0.014, 0.065, 0.077), 0),
    ("Rubber and joints", (0.033, 0.063, 0.068), 0),
    ("Pale metal", (0.70, 0.84, 0.73), 0),
    ("Phosphor lenses", (0.55, 1.0, 0.47), 1),
    ("Teal status light", (0.14, 0.94, 0.90), 1),
    ("Amber beacon", (1.0, 0.49, 0.08), 1),
]
materials = []
for name, colour, emissive in palette:
    material = bpy.data.materials.new(name)
    material.diffuse_color = (*colour, 1)
    material.use_nodes = True
    shader = material.node_tree.nodes.get("Principled BSDF")
    shader.inputs["Base Color"].default_value = (*colour, 1)
    shader.inputs["Metallic"].default_value = 0.55 if not emissive else 0.1
    shader.inputs["Roughness"].default_value = 0.32
    if emissive:
        shader.inputs["Emission Color"].default_value = (*colour, 1)
        shader.inputs["Emission Strength"].default_value = 2.5
    material["runtime_emissive"] = bool(emissive)
    materials.append(material)

# The 1.10-unit ball diameter approximately matches the 1.02-unit helmet width.
BALL_SCALE = .55 / .78
HEAD_DROP = 2 * (.78 - .55)

# All bones use a common local orientation, making the exported matrices simple.
bones = [
    ("root", (0, 0, 0), None),
    ("body", (0, 0, .55), "root"),
    ("head", (0, 0, 1.62 - HEAD_DROP), "root"),
]
rig_data = bpy.data.armatures.new("Phosphor rigid skeleton")
rig = bpy.data.objects.new("Phosphor Robot | 3 rigid bones", rig_data)
bpy.context.collection.objects.link(rig)
bpy.context.view_layer.objects.active = rig
rig.select_set(True)
bpy.ops.object.mode_set(mode="EDIT")
for name, pivot, parent in bones:
    bone = rig_data.edit_bones.new(name)
    bone.head = pivot
    bone.tail = Vector(pivot) + Vector((0, 0, 0.22))
    if parent:
        bone.parent = rig_data.edit_bones[parent]
bpy.ops.object.mode_set(mode="OBJECT")
rig.show_in_front = True
parts = []

def finish(obj, name, mat, bone):
    obj.name = name
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    obj.data.materials.append(materials[mat])
    group = obj.vertex_groups.new(name=bone)
    group.add(list(range(len(obj.data.vertices))), 1.0, "REPLACE")
    modifier = obj.modifiers.new("Rigid one-bone skin", "ARMATURE")
    modifier.object = rig
    obj.parent = rig
    obj["rigid_bone"] = bone
    obj["palette"] = mat
    parts.append(obj)
    return obj

def box(name, center, size, mat, bone, bevel=0.035):
    bpy.ops.mesh.primitive_cube_add(size=1, location=center)
    obj = bpy.context.object
    obj.dimensions = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel:
        modifier = obj.modifiers.new("Machined corners", "BEVEL")
        modifier.width = bevel
        modifier.segments = 1
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    return finish(obj, name, mat, bone)

def cylinder(name, center, radius, depth, mat, bone, axis="Z", sides=8):
    rotation = (math.pi/2, 0, 0) if axis == "Y" else (0, math.pi/2, 0) if axis == "X" else (0, 0, 0)
    bpy.ops.mesh.primitive_cylinder_add(vertices=sides, radius=radius, depth=depth,
                                      location=center, rotation=rotation)
    return finish(bpy.context.object, name, mat, bone)

# A chunky brow and inset two-eyed face reproduce the small robot in the cover.
box("Helmet shell", (0, 0, 1.93), (1.02, 0.67, 0.66), 0, "head", .10)
box("Helmet top plate", (0, -.025, 2.25), (.77, .50, .09), 1, "head", .03)
box("Face gasket", (0, -.348, 1.96), (.84, .055, .47), 4, "head", .055)
box("Recessed glass visor", (0, -.385, 1.97), (.74, .04, .36), 3, "head", .055)
box("Heavy brow", (0, -.40, 2.20), (.89, .13, .10), 1, "head", .025)
for side in (-1, 1):
    cylinder("Optic bezel", (side*.205, -.415, 2.0), .112, .035, 2, "head", "Y")
    box("Phosphor eye", (side*.205, -.442, 2.01), (.115, .016, .16), 6, "head", .016)
    cylinder("Temple pivot", (side*.535, .015, 1.94), .145, .085, 2, "head", "X")
    cylinder("Temple bolt", (side*.584, .015, 1.94), .059, .018, 5, "head", "X", 6)
box("Lower jaw vent", (0, -.396, 1.78), (.33, .04, .065), 2, "head", .01)
cylinder("Antenna mast", (-.37, .12, 2.40), .025, .29, 5, "head", sides=6)
cylinder("Amber antenna cap", (-.37, .12, 2.56), .055, .07, 8, "head")
# A single rolling ball replaces the complete torso and all four limbs.
# The head rides a separate stabilised joint so it never spins with the shell.
cylinder("Head stabiliser", (0, 0, 1.58), .18, .15, 4, "head")
bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=10, radius=.78, location=(0, 0, .78))
finish(bpy.context.object, "Rolling ball shell", 2, "body")
# Two narrow brass bands and a luminous marker make rotation readable at 320x240.
for side in (-1, 1):
    bpy.ops.mesh.primitive_torus_add(major_segments=16, minor_segments=4,
        major_radius=.735, minor_radius=.035, location=(side*.245, 0, .78),
        rotation=(0, math.pi/2, 0))
    finish(bpy.context.object, "Ball brass track", 1, "body")
    cylinder("Ball axle cap", (side*.77, 0, .78), .15, .055, 0, "body", "X")
box("Rolling status marker", (0, -.776, .78), (.18, .022, .10), 7, "body", .012)

# Resize the entire ball assembly together, keeping its ground contact at Z=0.
# Move the unchanged head down by the diameter reduction, including its joint.
for obj in parts:
    for vertex in obj.data.vertices:
        if obj["rigid_bone"] == "body":
            vertex.co *= BALL_SCALE
        else:
            vertex.co.z -= HEAD_DROP

# Hand-authored mechanical poses: no external animation account or asset licence.
clips = [("idle", 16, 6, True), ("run", 16, 2, True),
         ("jump", 8, 3, False), ("fall", 8, 3, False),
         ("dash", 8, 1, False), ("death", 12, 3, False)]
rig.animation_data_create()
all_frames = []
clip_table = []
for clip, count, ticks, loop in clips:
    action = bpy.data.actions.new(clip)
    action.use_fake_user = True
    rig.animation_data.action = action
    offset = len(all_frames)
    for frame in range(count + (1 if loop else 0)):
        t = frame / (count if loop else count-1)
        phase = t*math.tau
        for pb in rig.pose.bones:
            pb.rotation_mode = "XYZ"
            pb.rotation_euler = (0, 0, 0)
            pb.location = (0, 0, 0)
        def rotate(name, x=0, y=0, z=0):
            # Bone axes: local X=world X, local Y=world Z, local Z=-world Y.
            rig.pose.bones[name].rotation_euler = (x, z, -y)
        root = rig.pose.bones["root"]
        if clip == "idle":
            rotate("body", .04*math.sin(phase))
            rotate("head", .025*math.sin(phase), 0, .075*math.sin(phase))
        elif clip == "run":
            rotate("body", phase)
            rotate("head", -.06, 0, -.035*math.sin(phase))
        elif clip == "jump":
            rotate("body", t*1.8)
            rotate("head", -.25*t)
            rig.pose.bones["head"].location.y = .08*math.sin(t*math.pi)
        elif clip == "fall":
            rotate("body", 1.8+t*1.5)
            rotate("head", .14*t)
        elif clip == "dash":
            rotate("body", t*math.tau)
            rotate("head", .25, 0, -.06)
        else:
            rotate("body", .8*t)
            rotate("head", .30*t, .65*t)
            rig.pose.bones["head"].location.y = -.12*t
        for pb in rig.pose.bones:
            pb.keyframe_insert(data_path="rotation_euler", frame=frame+1, group=pb.name)
            pb.keyframe_insert(data_path="location", frame=frame+1, group=pb.name)
        bpy.context.scene.frame_set(frame+1)
        bpy.context.view_layer.update()
        if frame < count:
            matrices = []
            for name, _, _ in bones:
                m = rig.pose.bones[name].matrix @ rig.data.bones[name].matrix_local.inverted()
                matrices.extend(m[row][col] for row in range(3) for col in range(4))
            all_frames.append(matrices)
    clip_table.append((offset, count, ticks, int(loop)))

# The same exporter can be run after hand-editing the saved Blender file.
rig["chirky_robot"] = True
rig["clip_specs"] = json.dumps(clips)
sys.path.insert(0, str(Path(__file__).resolve().parent))
from export_phosphor_robot import export_robot
stats = export_robot(ROOT)

rig.animation_data.action = bpy.data.actions["idle"]
bpy.context.scene.frame_set(1)
scene = bpy.context.scene
scene.frame_start, scene.frame_end = 1, 17
scene.render.fps = 30
scene.world.color = (.055, .055, .055)
scene.render.engine = "CYCLES"
scene.cycles.samples = 32
scene.render.resolution_x = 840
scene.render.resolution_y = 840
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"
scene.view_settings.view_transform = "AgX"
def aim(obj, target):
    obj.rotation_euler = (Vector(target)-obj.location).to_track_quat("-Z", "Y").to_euler()
bpy.ops.object.camera_add(location=(3.5, -6.5, 3.0))
camera = bpy.context.object
camera.name = "Robot portrait camera"
camera.data.type = "ORTHO"
camera.data.ortho_scale = 2.8
aim(camera, (0, 0, 1.05))
scene.camera = camera
for name, location, energy, colour, size in [
    ("Warm key", (-3, -4, 6), 500, (1, .83, .61), 4),
    ("Teal rim", (2, 3, 4), 650, (.22, .85, 1), 3),
    ("Face fill", (3, -4, 2), 170, (.65, .89, 1), 3)]:
    bpy.ops.object.light_add(type="AREA", location=location)
    light=bpy.context.object
    light.name=name;light.data.energy=energy;light.data.color=colour;light.data.shape="DISK";light.data.size=size
    aim(light, (0, 0, 1.2))
# Reference stays packed in the editable source file, not in the runtime package.
reference=bpy.data.images.load(str(ROOT/"games/phosphor-run/assets/artwork/splash.png"), check_existing=True)
reference.pack()
rig["reference"] = "Splash robot: amber helmet, dark visor, twin phosphor eyes, antenna; redesigned with a single rolling ball body."
rig["runtime"] = "One rigid influence per vertex. Six matrix-sampled clips. No texture or engine dependency."
bpy.ops.object.select_all(action="DESELECT")
rig.select_set(True)
bpy.context.view_layer.objects.active=rig
bpy.context.preferences.filepaths.save_version = 0
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/"phosphor-robot.blend"))
scene.render.film_transparent = True
scene.render.filepath = str(OUT/"phosphor-robot.png")
bpy.ops.render.render(write_still=True)
result=stats
