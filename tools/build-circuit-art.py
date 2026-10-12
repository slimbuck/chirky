"""Export the approved native pixel grid, without resizing or palette changes.

python tools/build-circuit-art.py [--output DIRECTORY]
Requires Pillow. Alternate output supports byte-for-byte regeneration checks.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT / 'games/circuit-clash'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, default=GAME)
out = parser.parse_args().output
source = GAME / 'art/arthur/idle-approved.png'
image = Image.open(source).convert('RGBA')
assert image.size == (384, 96)
assert set(image.getchannel('A').tobytes()) == {0, 255}
frames = []
for n, name in enumerate(['neutral', 'descend', 'low', 'rise']):
    cell = image.crop((96*n, 0, 96*(n+1), 96))
    bounds = cell.getbbox()
    assert bounds[0] > 0 and bounds[1] > 0 and bounds[2] < 96 and bounds[3] == 80
    # Each foot reaches the same baseline in each authored frame.
    assert cell.crop((22, 0, 48, 96)).getbbox()[3] == 80
    assert cell.crop((48, 0, 75, 96)).getbbox()[3] == 80
    frames.append({'index': n, 'name': name, 'bounds': bounds})
pixels = image.tobytes()
art = out / 'assets/characters/arthur-idle.pam'
art.parent.mkdir(parents=True, exist_ok=True)
art.write_bytes(b'P7\nWIDTH 384\nHEIGHT 96\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n' + pixels)
manifest = {
    'version': 1, 'character': 'arthur', 'source': 'art/arthur/idle-approved.png',
    'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
    'runtime': 'assets/characters/arthur-idle.pam', 'rgba_sha256': hashlib.sha256(pixels).hexdigest(),
    'atlas_size': [384, 96], 'cell_size': [96, 96], 'draw_size': [96, 96],
    'anchor': [48, 80], 'baseline_row': 79, 'alpha': 'binary',
    'frames': frames, 'timing': 'assets/characters/arthur.conf: idle_0_ticks through idle_3_ticks',
    'pending_clips': ['walk', 'jump', 'crouch', 'punch', 'kick', 'block', 'hit', 'knockdown'],
    'provenance': 'Approved four-frame idle, including the single white-highlight correction. '
                  'Already reduced to its final native grid; this export copies pixels exactly.'
}
(out / 'art').mkdir(parents=True, exist_ok=True)
(out / 'art/manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
header = (GAME / 'tuning.h').read_text()
schema = {'version': 1, 'format': 'key=value', 'unknown_fields': 'reject',
          'duplicate_fields': 'reject', 'missing_fields': 'reject', 'reload': 'restart game',
          'tick_rate_note': 'Game updates; nominal 60 Hz. Native currently follows presentation.',
          'fp8_note': '1 = 1/8 framebuffer pixel; velocity per update, acceleration per update squared.'}
for kind in ['CHARACTER', 'STAGE']:
    block = header.split(f'#define CC_{kind}_FIELDS(X)', 1)[1].split('\n#define', 1)[0]
    fields = [{'key': name, 'type': 'integer', 'minimum': int(lo), 'maximum': int(hi), 'required': True}
              for name, lo, hi in re.findall(r'X\((\w+),(\d+),(\d+)\)', block)]
    fields.append({'key': 'name', 'type': 'string', 'minLength': 1, 'maxLength': 23, 'required': True})
    if kind == 'CHARACTER':
        fields.append({'key': 'atlas', 'type': 'asset-path', 'required': True, 'cellSize': [96, 96], 'frameCount': 4})
    schema[kind.lower()] = fields
schema['constraints'] = ['crouching_height_px <= standing_height_px',
                         'punch_bottom_px < punch_top_px', 'kick_bottom_px < kick_top_px']
(out / 'content-schema.json').write_text(json.dumps(schema, indent=2)+'\n')
print('Exported approved 384x96 atlas, art manifest and tuning schema.')
