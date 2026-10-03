"""Author non-shipping proportion studies from the current editable robot.

blender --background --python tools/blender/compare_phosphor_proportions.py
Outputs Blender sources and normal PRB1 exports under build/robot-options/.
The shipped model is never overwritten. These are silhouette studies; animation
pivots and runtime eye/neck anchors must be adapted after a design is selected.
"""
import bpy
import json
import math
import sys
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
BASELINE = ROOT / "build/robot-options/current/robot.blend"
if not BASELINE.exists():
    BASELINE = ROOT / "games/phosphor-run/assets/models/phosphor-robot.blend"
sys.path.insert(0, str(Path(__file__).resolve().parent))
from export_phosphor_robot import export_robot

OPTIONS = [
    ("current", "Current", 1, 1, 1, 1),
    ("a", "A - Splash proportions", 1.22, .72, 1.18, 1.55),
    ("b", "B - Extra cute", 1.30, .62, 1.23, 1.85),
    ("c", "C - Subtle change", 1.12, .82, 1.10, 1.30),
]
CRT = "--crt" in sys.argv
manifest = []
for key, label, head, body, face, eyes in (OPTIONS[1:2] if CRT else OPTIONS):
    bpy.ops.wm.open_mainfile(filepath=str(BASELINE))
    rig = next(obj for obj in bpy.data.objects if obj.type == "ARMATURE" and obj.get("chirky_robot"))
    if "face_rig" in rig:
        raise RuntimeError("Historical studies need the original PRB1 Blender source at build/robot-options/current/robot.blend")
    rig.animation_data.action = bpy.data.actions["idle"]
    bpy.context.scene.frame_set(1)
    for obj in bpy.data.objects:
        if obj.type != "MESH" or obj.parent != rig or "rigid_bone" not in obj:
            continue
        center = sum((v.co for v in obj.data.vertices), Vector()) / len(obj.data.vertices)
        for vertex in obj.data.vertices:
            p = vertex.co
            if obj["rigid_bone"] == "body":
                p *= body
            else:
                if obj.name.startswith(("Face gasket", "Recessed glass visor")):
                    p.x *= face
                    p.z = 1.51 + (p.z - 1.51) * face
                if obj.name.startswith(("Phosphor eye", "Optic bezel")):
                    scale = eyes if obj.name.startswith("Phosphor eye") else 1 + (eyes - 1) * .35
                    p.x = center.x * (1.04 if key != "current" else 1) + (p.x - center.x) * scale
                    p.z = center.z + (p.z - center.z) * scale
                if obj.name.startswith("Heavy brow"):
                    p.z = center.z + (p.z - center.z) * (.75 if key != "current" else 1)
                # Keep the neck base at the ball's new top, with a larger helmet.
                p.x *= head
                p.y *= head
                p.z = 1.10 * body + (p.z - 1.10) * head
    output = ROOT / "build/robot-options" / key
    stats = export_robot(output)
    bpy.context.scene.frame_set(1)
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(output / "robot.blend"))
    manifest.append(dict(id=key, label=label, head=head, body=body, face=face, eyes=eyes, **stats))
if not CRT:
    (ROOT / "build/robot-options/manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
else:
    # Continue the same source/export pipeline with A's accepted body proportions.
    studies = [
        ("crt1", "Pocket CRT", 1.25, .84, .84, .18, .09, .34, .40),
        ("crt2", "Bubble TV", 1.25, .86, .90, .26, .16, .36, .42),
        ("crt3", "Wide CRT", 1.34, .77, .80, .21, .12, .37, .38),
    ]
    manifest = []
    for key, label, width, height, depth, radius, bulge, eye_width, eye_height in studies:
        bpy.ops.wm.open_mainfile(filepath=str(ROOT / "build/robot-options/a/robot.blend"))
        rig = next(obj for obj in bpy.data.objects if obj.type == "ARMATURE" and obj.get("chirky_robot"))
        palette = {int(obj["palette"]): obj.data.materials[0] for obj in bpy.data.objects
                   if obj.type == "MESH" and "palette" in obj}
        reflection = bpy.data.materials.new("Soft CRT glass reflection")
        reflection.diffuse_color = (.065, .18, .19, 1)
        reflection.use_nodes = True
        reflection.node_tree.nodes.get("Principled BSDF").inputs["Base Color"].default_value = reflection.diffuse_color
        palette[9] = reflection
        for obj in list(bpy.data.objects):
            if obj.type == "MESH" and obj.parent == rig and obj.get("rigid_bone") == "head":
                if not obj.name.startswith(("Antenna mast", "Amber antenna cap", "Head stabiliser")):
                    bpy.data.objects.remove(obj, do_unlink=True)
        center_z = 1.25
        front = -depth / 2 - .012

        def finish(obj, name, material):
            obj.name = name
            bpy.context.view_layer.objects.active = obj
            obj.select_set(True)
            bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
            obj.data.materials.append(palette[material])
            obj.parent = rig
            obj["rigid_bone"] = "head"
            obj["palette"] = material
            group = obj.vertex_groups.new(name="head")
            group.add(list(range(len(obj.data.vertices))), 1, "REPLACE")
            obj.modifiers.new("Rigid head", "ARMATURE").object = rig
            obj.select_set(False)

        def rounded_box(name, size, y, material, bevel):
            bpy.ops.mesh.primitive_cube_add(size=1, location=(0, y, center_z))
            obj = bpy.context.object
            obj.dimensions = size
            bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
            modifier = obj.modifiers.new("Soft moulded corners", "BEVEL")
            modifier.width = bevel
            modifier.segments = 4
            bpy.ops.object.modifier_apply(modifier=modifier.name)
            finish(obj, name, material)

        def outline(w, h, r):
            result = []
            for cx, cz, angle in [(w/2-r,h/2-r,0),(-w/2+r,h/2-r,90),
                                  (-w/2+r,-h/2+r,180),(w/2-r,-h/2+r,270)]:
                for i in range(8):
                    a = math.radians(angle + i*90/7)
                    result.append((cx+r*math.cos(a), cz+r*math.sin(a)))
            return result

        screen_w, screen_h = width-.105, height-.105

        def glass_y(x, z):
            return front - .040 - bulge * max(0, 1-(x/(screen_w/2))**2) * max(0, 1-(z/(screen_h/2))**2)

        def surface(name, boundary, material, offset=0, eye_center=None):
            # Concentric curved rings give real convex glass, not a flat face card.
            cx, cz = eye_center or (0, 0)
            vertices = [(cx, glass_y(cx,cz)-offset, center_z+cz)]
            for ring in range(1, 6):
                t = ring/5
                for x, z in boundary:
                    x, z = cx+x*t, cz+z*t
                    vertices.append((x, glass_y(x,z)-offset, center_z+z))
            n = len(boundary)
            faces = [(0, 1+i, 1+(i+1)%n) for i in range(n)]
            for ring in range(4):
                a, b = 1+ring*n, 1+(ring+1)*n
                faces.extend((a+i,b+i,b+(i+1)%n,a+(i+1)%n) for i in range(n))
            mesh = bpy.data.meshes.new(name)
            mesh.from_pydata(vertices, [], faces)
            obj = bpy.data.objects.new(name, mesh)
            bpy.context.collection.objects.link(obj)
            finish(obj, name, material)

        rounded_box("Rounded CRT cabinet", (width,depth,height), 0, 0, radius)
        # A narrow continuous lip; no separate brow, top plate or jaw vent.
        surface("Thin brass screen lip", outline(width-.035,height-.035,radius*.96), 1, -.012)
        surface("Convex CRT glass", outline(screen_w,screen_h,radius*.87), 3, .002)
        # A short curved reflection describes the glass bulge without a brow.
        vertices, faces = [], []
        rr = radius*.87-.05
        for i in range(17):
            angle = math.radians(97+i*80/16)
            taper = .006 + .007*math.sin(i*math.pi/16)
            for delta in (-taper,taper):
                x = -screen_w/2 + radius*.87 + (rr+delta)*math.cos(angle)
                z = screen_h/2 - radius*.87 + (rr+delta)*math.sin(angle)
                vertices.append((x,glass_y(x,z)-.007,center_z+z))
            if i: faces.append((2*i-2,2*i,2*i+1,2*i-1))
        mesh = bpy.data.meshes.new("Glass glint")
        mesh.from_pydata(vertices, [], faces)
        obj = bpy.data.objects.new("Glass glint",mesh)
        bpy.context.collection.objects.link(obj)
        finish(obj,"Glass glint",9)
        for sign in (-1, 1):
            boundary = outline(eye_width,eye_height,min(eye_width,eye_height)*.44)
            # Separate the eye surface from the coarser glass triangulation and
            # the 16-bit depth buffer; tiny overlap produces holes at native size.
            surface("Phosphor eye", boundary, 6, .035, (sign*.255,.055))
        # A small upward smile replaces the mechanical jaw vent.
        vertices, faces = [], []
        for i in range(13):
            x = -.09 + i*.18/12
            z = -.205 + .035*(x/.09)**2
            for dz in (-.008,.008):
                vertices.append((x,glass_y(x,z+dz)-.012,center_z+z+dz))
            if i: faces.append((2*i-2,2*i,2*i+1,2*i-1))
        mesh = bpy.data.meshes.new("Tiny smile")
        mesh.from_pydata(vertices, [], faces)
        obj = bpy.data.objects.new("Tiny smile",mesh)
        bpy.context.collection.objects.link(obj)
        finish(obj,"Tiny smile",6)
        output = ROOT / "build/robot-options" / key
        stats = export_robot(output)
        bpy.context.preferences.filepaths.save_version = 0
        bpy.ops.wm.save_as_mainfile(filepath=str(output / "robot.blend"))
        manifest.append(dict(id=key,label=label,proportions="A",width=width,height=height,
                             depth=depth,corner_radius=radius,glass_bulge=bulge,
                             eye_width=eye_width,eye_height=eye_height,eye_spacing=.51,**stats))
    (ROOT / "build/robot-options/crt-manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
