"""Build Circuit Clash's original synthetic effects; no external samples.

python tools/build-circuit-sounds.py [--output DIRECTORY]
Standard library only. Output contains four PCM16 mono WAVs and manifest.json.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import struct
import wave

RATE = 24000
SOUNDS = [('punch', 0.095), ('kick', 0.135), ('block', 0.075), ('miss', 0.085)]


def samples(name, duration):
    rng = random.Random(121 + [s[0] for s in SOUNDS].index(name))
    low = phase = 0.0
    values = []
    for i in range(round(duration * RATE)):
        t = i / RATE
        noise = rng.uniform(-1, 1)
        low += 0.18 * (noise - low)
        high = noise - low
        if name == 'punch':
            phase += math.tau * (115 + 170 * math.exp(-t * 65)) / RATE
            value = .60 * math.sin(phase) * math.exp(-t * 53) + .42 * high * math.exp(-t * 130)
        elif name == 'kick':
            phase += math.tau * (66 + 150 * math.exp(-t * 48)) / RATE
            value = .72 * math.sin(phase) * math.exp(-t * 36) + .32 * high * math.exp(-t * 105)
        elif name == 'block':
            value = (.33 * math.sin(math.tau * 740 * t) + .23 * math.sin(math.tau * 1130 * t)) * math.exp(-t * 90)
            value += .38 * high * math.exp(-t * 135)
        else:
            value = 2.2 * low * math.sin(math.pi * t / duration) ** 2 * math.exp(-t * 15)
        # Sub-millisecond attack, soft ending and headroom for simultaneous hits.
        envelope = min(1, t / .0007) * min(1, (duration - 1 / RATE - t) / .008)
        values.append(value * max(0, envelope))
    peak = max(abs(v) for v in values)
    return [round(v / peak * 21000) for v in values]


def build(output):
    output.mkdir(parents=True, exist_ok=True)
    manifest = {'version': 1, 'generator': 'tools/build-circuit-sounds.py',
                'sample_rate': RATE, 'channels': 1, 'bits': 16, 'sounds': []}
    for cue, (name, duration) in enumerate(SOUNDS, 1):
        pcm = samples(name, duration)
        path = output / (name + '.wav')
        with wave.open(str(path), 'wb') as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(RATE)
            wav.writeframes(struct.pack('<' + 'h' * len(pcm), *pcm))
        manifest['sounds'].append({'cue': cue, 'name': name, 'file': path.name,
                                   'frames': len(pcm), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', newline='\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1] / 'games/circuit-clash/assets/sounds')
    build(parser.parse_args().output)
