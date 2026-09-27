# Bramble Director

The director is a standalone Node.js service. It owns the OpenAI credential,
persists each world, accepts sequenced game events over HTTP, and returns the
latest validated state document. The browser and Pi use the same `/v1/sync`
protocol; gameplay continues with its last state while the service is offline.

Start it on the Windows machine connected to the Pi:

```powershell
$env:OPENAI_API_KEY = "..."
node director/server.mjs
```

It listens on `0.0.0.0:3040` by default. Bramble's checked-in configuration
uses `http://192.168.137.1:3040`, the Windows side of the console's private
network. Set `DIRECTOR_HOST`, `DIRECTOR_PORT`, `DIRECTOR_INTERVAL_MS`, or
`BRAMBLE_MODEL` to override the defaults. The first model is `gpt-6-luna`.

Useful read-only checks:

```powershell
Invoke-RestMethod http://127.0.0.1:3040/health
Invoke-RestMethod http://127.0.0.1:3040/v1/worlds/bramble-hollow-main
```

Force one generation after events have arrived:

```powershell
Invoke-RestMethod -Method Post http://127.0.0.1:3040/v1/worlds/bramble-hollow-main/generate
```

World files live under `director/data/` and are not committed. Requests are
bounded and model calls are rate-limited, but this proof of concept has no user
authentication. Keep port 3040 on the private console network rather than
exposing it to the public internet.
