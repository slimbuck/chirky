"""Fail before building mixed or unsupported browser modules."""
import pathlib
import re
import subprocess
import sys

expected = (pathlib.Path(__file__).resolve().parents[1] / ".emscripten-version").read_text().strip()
try:
    output = subprocess.check_output([sys.argv[1], "--version"], text=True, stderr=subprocess.STDOUT)
except (OSError, subprocess.CalledProcessError) as error:
    sys.exit(f"Cannot run Emscripten: {error}. See web/README.md for SDK setup.")
match = re.search(r"^emcc .*\) (\d+\.\d+\.\d+)\b", output, re.MULTILINE)
if not match or match[1] != expected:
    sys.exit(f"Chirky requires Emscripten {expected}; found {output.splitlines()[0]}. "
             "Use the SDK in web/README.md and rebuild all browser modules.")
print(f"Emscripten {expected}")
