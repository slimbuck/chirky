#!/bin/sh
# Run from the repository root. Offscreen software GL only; no display or Pi access.
set -eu
cc=${CC:-cc}
out=${TEXTURE_TEST_DIR:-/tmp/chirky-texture-tests}
mkdir -p "$out"
"$cc" -D_GNU_SOURCE -Iinclude -std=c11 -O2 -Wall -Wextra -Wpedantic \
    tests/texture_renderer.c src/rect_renderer.c -lEGL -lGLESv2 -lm -o "$out/texture-renderer"
LIBGL_ALWAYS_SOFTWARE=1 "$out/texture-renderer"
"$cc" -D_GNU_SOURCE -Iinclude -Isrc -std=c11 -O2 -Wall -Wextra -Wpedantic \
    tests/phosphor_atlas.c games/phosphor-run/*.c src/rect_renderer.c -lEGL -lGLESv2 -lm -o "$out/phosphor-atlas"
LIBGL_ALWAYS_SOFTWARE=1 "$out/phosphor-atlas"
"$cc" -D_GNU_SOURCE -Iinclude -std=c11 -O2 -Wall -Wextra -Wpedantic \
    tests/texture_splash.c -o "$out/texture-splash"
"$out/texture-splash"
"$cc" -D_GNU_SOURCE -Iinclude -std=c11 -O2 -Wall -Wextra -Wpedantic \
    tests/splash_runtime.c -o "$out/texture-splash-legacy"
"$out/texture-splash-legacy"
if [ "${1:-}" = "--web" ]; then
    emcc=${EMCC:-emcc}
    python3 tools/check-web-toolchain.py "$emcc"
    "$emcc" -Iinclude -std=c11 -O2 -Wall -Wextra -Wpedantic \
        tests/texture_renderer.c src/rect_renderer.c -sASSERTIONS=1 -sEXIT_RUNTIME=1 -sMINIFY_HTML=0 \
        --shell-file tests/texture_shell.html -o "$out/texture-renderer.html"
    "$emcc" -D_GNU_SOURCE -Iinclude -Isrc -std=c11 -O2 -Wall -Wextra -Wpedantic \
        tests/phosphor_atlas.c games/phosphor-run/*.c src/rect_renderer.c \
        --embed-file games/phosphor-run/game.conf --embed-file games/phosphor-run/content.conf \
        --embed-file games/phosphor-run/assets/sprites --embed-file games/phosphor-run/assets/levels \
        --embed-file games/phosphor-run/assets/models/player.robot --embed-file games/phosphor-run/assets/models/robot.conf \
        -sASSERTIONS=1 -sEXIT_RUNTIME=1 -sMINIFY_HTML=0 \
        --shell-file tests/texture_shell.html -o "$out/phosphor-atlas.html"
    printf 'WebGL test page: %s/texture-renderer.html\n' "$out"
fi
