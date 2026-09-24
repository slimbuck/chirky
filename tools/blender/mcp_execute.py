"""Run a repository Python script through Blender's installed MCP extension.

Start Blender with --background --command blender_mcp first. This speaks the
extension's documented null-delimited execute protocol on localhost only.
"""
import json
import socket
import sys
from pathlib import Path

script = Path(sys.argv[1]).resolve()
code = script.read_text(encoding="utf-8")
with socket.create_connection(("127.0.0.1", 9876), timeout=300) as connection:
    connection.sendall((json.dumps({"type": "execute", "strict_json": True,
                                   "code": "__file__ = " + repr(str(script)) + "\n" + code}) + "\0").encode())
    response = bytearray()
    while b"\0" not in response:
        chunk = connection.recv(65536)
        if not chunk:
            raise RuntimeError("Blender disconnected before responding")
        response.extend(chunk)
    data = json.loads(response.split(b"\0", 1)[0])
    print(json.dumps(data, indent=2))
    if data["status"] != "ok":
        raise SystemExit(1)
