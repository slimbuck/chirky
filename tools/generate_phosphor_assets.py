"""Generate deterministic Phosphor Run sound effects with Python's stdlib."""

from __future__ import annotations

import math
import struct
import wave
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "games" / "phosphor-run" / "assets"
RATE = 48_000


def write_sound(name: str, duration: float, voice) -> None:
    frames = round(RATE * duration)
    with wave.open(str(OUTPUT / name), "wb") as audio:
        audio.setnchannels(2)
        audio.setsampwidth(2)
        audio.setframerate(RATE)
        data = bytearray()
        for index in range(frames):
            t = index / RATE
            progress = index / frames
            sample = max(-1.0, min(1.0, voice(t, progress)))
            value = round(sample * 12_000)
            data.extend(struct.pack("<hh", value, value))
        audio.writeframes(data)


def sine(frequency: float, t: float) -> float:
    return math.sin(2 * math.pi * frequency * t)


def main() -> None:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    write_sound("jump.wav", 0.10,
                lambda t, p: sine(330 + 520 * p, t) * (1 - p) ** 1.5)
    write_sound("dash.wav", 0.09,
                lambda t, p: (sine(140 - 70 * p, t) + .35 * sine(920, t)) * (1 - p))
    write_sound("shard.wav", 0.16,
                lambda t, p: sine((660, 880, 1100)[min(2, int(p * 3))], t) * (1 - p) ** .7)
    write_sound("checkpoint.wav", 0.28,
                lambda t, p: sine(440 + 440 * p, t) * (1 - p) ** .6)
    write_sound("death.wav", 0.28,
                lambda t, p: (sine(240 - 170 * p, t) + .25 * sine(61, t)) * (1 - p))
    write_sound("win.wav", 0.65,
                lambda t, p: sine((523, 659, 784, 1047)[min(3, int(p * 4))], t) * (1 - .55 * p))
    # Quiet UI feedback with smooth attack/release; confirmation is two notes.
    write_sound("ui-tick.wav", 0.045,
                lambda t, p: .6 * sine(600, t) * math.sin(math.pi * p) ** 2)
    write_sound("ui-select.wav", 0.10,
                lambda t, p: .6 * sine(660 if p < .5 else 880, t)
                * math.sin(math.pi * ((p * 2) % 1)) ** 2)
    for file in sorted(OUTPUT.glob("*.wav")):
        print(file)


if __name__ == "__main__":
    main()
