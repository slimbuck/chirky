"""Export the editable rig from an opened .blend into the bounded C runtime format.

blender --background games/phosphor-run/assets/models/phosphor-robot.blend \
  --python tools/blender/export_phosphor_robot.py
"""
import bpy
import json
import math
import struct
from pathlib import Path

def export_robot(root):
    rig = next(obj for obj in bpy.data.objects if obj.type == "ARMATURE" and obj.get("chirky_robot"))
    clips = json.loads(rig["clip_specs"])
    bones = list(rig.data.bones)
    names = [bone.name for bone in bones]
    parts = [obj for obj in bpy.data.objects if obj.type == "MESH" and obj.parent == rig and "rigid_bone" in obj]
    parts.sort(key=lambda obj: obj.name)
    materials = {}
    vertices, triangles, frames, table = [], [], [], []
    for obj in parts:
        bone = names.index(obj["rigid_bone"])
        base = len(vertices)
        # Apply object placement as well as mesh edits; weights remain rigid.
        transform = rig.matrix_world.inverted() @ obj.matrix_world
        vertices.extend((*(transform @ vertex.co), bone) for vertex in obj.data.vertices)
        mat = int(obj["palette"])
        material = obj.data.materials[0]
        shader = material.node_tree.nodes.get("Principled BSDF") if material.use_nodes else None
        colour = shader.inputs["Base Color"].default_value if shader else material.diffuse_color
        materials[mat] = ([round(max(0, min(1, c))*255) for c in colour[:3]] +
                          [int(material.get("runtime_emissive", False))])
        obj.data.calc_loop_triangles()
        triangles.extend((*(base+i for i in tri.vertices), mat) for tri in obj.data.loop_triangles)
    previous_action = rig.animation_data.action
    previous_frame = bpy.context.scene.frame_current
    try:
        for name, count, ticks, loop in clips:
            rig.animation_data.action = bpy.data.actions[name]
            table.append((len(frames), count, ticks, int(loop)))
            for frame in range(count):
                bpy.context.scene.frame_set(frame+1)
                bpy.context.view_layer.update()
                matrices = []
                for bone in bones:
                    m = rig.pose.bones[bone.name].matrix @ bone.matrix_local.inverted()
                    matrices.extend(m[row][col] for row in range(3) for col in range(4))
                frames.append(matrices)
    finally:
        rig.animation_data.action = previous_action
        bpy.context.scene.frame_set(previous_frame)
    assert 0 < len(vertices) <= 2048 and 0 < len(triangles) <= 4096
    assert 0 < len(bones) <= 32 and len(clips) == 6 and 0 < len(frames) <= 256
    assert sorted(materials) == list(range(len(materials))) and len(materials) <= 32
    assert all(math.isfinite(value) and abs(value) <= 64 for frame in frames for value in frame)
    out = Path(root) / "games/phosphor-run/assets/models"
    out.mkdir(parents=True, exist_ok=True)
    with (out / "player.robot").open("wb") as file:
        file.write(struct.pack("<4s6I", b"PRB1", len(vertices), len(triangles), len(bones), len(materials), len(clips), len(frames)))
        for index in range(len(materials)):
            file.write(bytes(materials[index]))
        for vertex in vertices:
            file.write(struct.pack("<3fI", *vertex))
        for triangle in triangles:
            file.write(struct.pack("<4H", *triangle))
        for clip in table:
            file.write(struct.pack("<4I", *clip))
        for frame in frames:
            file.write(struct.pack("<"+"f"*len(frame), *frame))
    stats = {"vertices": len(vertices), "triangles": len(triangles), "bones": len(bones),
             "clips": [clip[0] for clip in clips], "pose_frames": len(frames),
             "runtime_bytes": (out/"player.robot").stat().st_size}
    (out/"model-info.json").write_text(json.dumps(stats, indent=2)+"\n")
    return stats

if __name__ == "__main__":
    print(export_robot(Path(__file__).resolve().parents[2]))
