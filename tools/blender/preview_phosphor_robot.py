"""Render one run cycle for the model's review animation; intermediates stay in build/."""
import bpy
from pathlib import Path
root = Path(__file__).resolve().parents[2]
bpy.ops.wm.open_mainfile(filepath=str(root/"games/phosphor-run/assets/models/phosphor-robot.blend"))
rig = next(obj for obj in bpy.data.objects if obj.get("chirky_robot"))
rig.animation_data.action = bpy.data.actions["run"]
scene = bpy.context.scene
scene.cycles.samples = 16
scene.render.resolution_x = scene.render.resolution_y = 384
scene.render.film_transparent = True
folder = root/"build/robot-animation"
folder.mkdir(parents=True, exist_ok=True)
for frame in range(1, 17):
    scene.frame_set(frame)
    scene.render.filepath = str(folder/f"{frame:02d}.png")
    bpy.ops.render.render(write_still=True)
result = {"frames": 16, "directory": str(folder)}
