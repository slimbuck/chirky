"""Validate shipped effects and byte-compare a clean regeneration."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import wave

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('sounds', root / 'tools/build-circuit-sounds.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
source = root / 'games/circuit-clash/assets/sounds'
manifest = json.loads((source / 'manifest.json').read_text())
with tempfile.TemporaryDirectory() as temp:
    output = Path(temp)
    builder.build(output)
    for file in output.iterdir():
        assert file.read_bytes() == (source / file.name).read_bytes(), file.name
    for sound in manifest['sounds']:
        with wave.open(str(source / sound['file']), 'rb') as wav:
            assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (1, 2, 24000)
            assert wav.getnframes() == sound['frames']
            pcm = struct.unpack('<' + 'h' * wav.getnframes(), wav.readframes(wav.getnframes()))
            assert .06 <= len(pcm) / 24000 <= .15
            assert pcm[0] == pcm[-1] == 0
            assert max(abs(v) for v in pcm) == 21000
            assert abs(sum(pcm) / len(pcm)) < 350
            assert max(abs(v) for v in pcm[-48:]) < 500
print('Circuit audio: reproducible PCM16 effects, headroom and soft endings passed.')
