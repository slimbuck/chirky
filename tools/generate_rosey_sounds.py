"""Regenerate Rosey Chop's original mono PCM effects (no dependencies)."""
from pathlib import Path
import math
import random
import struct
import wave

OUT = Path(__file__).resolve().parents[1] / "games/rosey-chop/assets"


def write(name, notes, duration, noise=0.0):
    rng = random.Random(240)
    rate = 22050
    samples = []
    for i in range(int(rate * duration)):
        t = i / rate
        position = t / duration * len(notes)
        note = notes[min(int(position), len(notes) - 1)]
        envelope = min(1, t * 100) * (1 - position % 1) ** 1.3
        tone = math.sin(2 * math.pi * note * t)
        value = (tone * (1 - noise) + rng.uniform(-1, 1) * noise) * envelope * 0.28
        samples.append(struct.pack("<h", int(value * 32767)))
    with wave.open(str(OUT / f"{name}.wav"), "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(rate)
        f.writeframes(b"".join(samples))


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    write("chop", [180, 95, 640], 0.16, 0.65)
    write("swing", [240, 120], 0.10, 0.85)
    write("jump", [330, 440, 660], 0.18)
    write("warning", [220, 330, 220, 330], 0.42, 0.18)
    write("sting", [620, 440, 180, 80], 0.5, 0.3)
    write("storm", [85, 65, 45], 0.75, 0.8)
    write("win", [392, 494, 587, 784, 988, 784], 0.9)
