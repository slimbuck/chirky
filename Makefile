CC ?= cc
.DEFAULT_GOAL := all
NODE ?= node

CPPFLAGS += -D_GNU_SOURCE -Iinclude -Isrc -Isrc/platform/linux
CFLAGS += -std=c11 -O2 -Wall -Wextra -Wpedantic
LDLIBS += -pthread -ldl -Wl,--no-as-needed -l:libdrm.so.2 -l:libgbm.so.1 -l:libEGL.so.1 -l:libGLESv2.so.2

TARGET := build/chirky-host
CORE_SOURCES := src/asset_store.c src/runtime.c src/console.c
PLATFORM_SOURCES := src/platform/linux/asset_platform.c src/platform/linux/audio_mixer.c src/platform/linux/director_client.c src/platform/linux/save_store.c
LINUX_HEADERS := $(wildcard src/platform/linux/*.h)
SOURCES := src/platform/linux/host.c src/platform/linux/input_bindings.c src/rect_renderer.c $(CORE_SOURCES) $(PLATFORM_SOURCES)
GAME_SOURCES := $(wildcard games/*/game.c)
GAME_TARGETS := $(patsubst games/%/game.c,build/games/%.so,$(GAME_SOURCES))

.PHONY: all clean test benchmark performance-benchmark web

EMSDK ?= $(HOME)/.cache/chirky/emsdk-$(shell cat .emscripten-version)
EMCC ?= $(if $(wildcard $(EMSDK)/upstream/emscripten/emcc),$(EMSDK)/upstream/emscripten/emcc,emcc)
WEB_FLAGS = -D_GNU_SOURCE -Iinclude -Isrc -std=c11 -O2 -Wall -Wextra -sGL_PREINITIALIZED_CONTEXT=1 -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1 -sFORCE_FILESYSTEM=1 -sEXPORTED_RUNTIME_METHODS=FS,ccall --no-entry
WEB_COMMON = src/platform/web/host.c src/platform/web/asset_platform.c src/rect_renderer.c $(CORE_SOURCES)
WEB_HEADERS = $(wildcard include/*.h src/*.h src/platform/web/*.h)
WEB_TARGETS = $(patsubst games/%/game.c,build/web/%.wasm,$(wildcard games/*/game.c))
.SECONDEXPANSION:

web: build/web/launcher.js $(WEB_TARGETS)
	$(NODE) tools/web-assets.js

.PHONY: check-web-toolchain
check-web-toolchain:
	python3 tools/check-web-toolchain.py "$(EMCC)"

build/web/launcher.js $(WEB_TARGETS): | check-web-toolchain

build/web/launcher.js: $(WEB_COMMON) $(WEB_HEADERS) .emscripten-version
	mkdir -p $(@D)
	$(EMCC) $(WEB_FLAGS) -sMAIN_MODULE=1 $(WEB_COMMON) -o $@

build/web/%.wasm: games/%/game.c $$(wildcard games/$$*/*.c games/$$*/*.h) $(WEB_HEADERS) .emscripten-version
	mkdir -p $(@D)
	$(EMCC) -D_GNU_SOURCE -Iinclude -std=c11 -O2 -Wall -Wextra -fvisibility=hidden -sSIDE_MODULE=2 -sEXPORTED_FUNCTIONS=_chirky_game_entry $(wildcard games/$*/*.c) -o $@

all: $(TARGET) $(GAME_TARGETS)

$(TARGET): $(SOURCES) $(wildcard include/*.h src/*.h) $(LINUX_HEADERS)
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -o $@.next $(LDLIBS)
	mv $@.next $@

.SECONDEXPANSION:
build/games/%.so: games/%/game.c $$(wildcard games/$$*/*.c games/$$*/*.h) $(wildcard include/*.h)
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Iinclude -fPIC -shared $(filter %.c,$^) -lm -o $@.next
	mv $@.next $@

clean:
	rm -rf build

# EGL pbuffer benchmark; leaves the running CRT host and its input untouched.
benchmark: build/games/rosey-chop.so
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -fdata-sections tools/render_benchmark.c src/rect_renderer.c $(PLATFORM_SOURCES) -pthread -Wl,--gc-sections -ldl -l:libEGL.so.1 -l:libGLESv2.so.2 -o build/render-benchmark
	./build/render-benchmark

# Build only: the explicit supervisor command temporarily takes over the CRT.
performance-benchmark: build/performance-benchmark $(GAME_TARGETS)

build/performance-benchmark: tools/performance_benchmark.c $(SOURCES) $(wildcard src/*.h include/*.h) $(LINUX_HEADERS)
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -fdata-sections tools/performance_benchmark.c src/platform/linux/input_bindings.c $(CORE_SOURCES) $(PLATFORM_SOURCES) -Wl,--gc-sections -ldl $(LDLIBS) -o $@

test: $(GAME_TARGETS)
	mkdir -p build
	$(NODE) --test tests/launcher_art.cjs
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/console.c src/console.c -o /tmp/console-test
	/tmp/console-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/runtime.c src/runtime.c -o /tmp/runtime-test
	/tmp/runtime-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/save_store.c src/platform/linux/save_store.c -o /tmp/save-store-test
	/tmp/save-store-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/asset_store.c src/asset_store.c src/platform/linux/asset_platform.c -pthread -o /tmp/asset-store-test
	/tmp/asset-store-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/audio_mixer.c -pthread -ldl -o /tmp/audio-mixer-test
	/tmp/audio-mixer-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/audio_samples.c src/asset_store.c src/platform/linux/asset_platform.c -pthread -ldl -o /tmp/audio-samples-test
	/tmp/audio-samples-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/texture_splash.c -o /tmp/texture-splash-test
	/tmp/texture-splash-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/platform_runtime.c src/asset_store.c src/platform/linux/asset_platform.c -pthread -ldl -lm -o /tmp/platform-runtime-test
	/tmp/platform-runtime-test $(CURDIR)/build/games
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/trace.c -o /tmp/trace-test
	/tmp/trace-test
	python3 tests/profile_report.py
	python3 tests/capture_profile.py
	python3 tests/performance_analysis.py
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/profile.c -o /tmp/profile-test
	/tmp/profile-test
	python3 tests/gpu_profiler.py
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/gpu_timing.c -o /tmp/gpu-timing-test
	/tmp/gpu-timing-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/director_client.c src/platform/linux/director_client.c -pthread -o /tmp/director-client-test
	/tmp/director-client-test
	$(NODE) --test dashboard/editors.test.js dashboard/ppm.test.js dashboard/launcher.test.js dashboard/play.test.js tests/architecture.test.cjs tests/director_server.test.mjs tests/game_catalog.test.cjs tests/player_audio.cjs tests/player_input.cjs tests/web_package.cjs
	sh tests/service_install.sh
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/splash_runtime.c -o /tmp/splash-runtime-test
	/tmp/splash-runtime-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/launcher_runtime.c -o /tmp/launcher-runtime-test
	/tmp/launcher-runtime-test
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -fdata-sections tests/host_runtime.c src/platform/linux/input_bindings.c src/rect_renderer.c $(CORE_SOURCES) $(PLATFORM_SOURCES) -pthread -Wl,--gc-sections -ldl -l:libGLESv2.so.2 -o /tmp/host-runtime-test
	/tmp/host-runtime-test $(CURDIR)/build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Igames/phosphor-run tests/phosphor_runtime.c games/phosphor-run/*.c -lm -o /tmp/phosphor-runtime-test
	/tmp/phosphor-runtime-test
	$(CC) $(CPPFLAGS) $(CFLAGS) -Igames/phosphor-run tests/robot_runtime.c games/phosphor-run/robot.c -lm -o /tmp/robot-runtime-test
	/tmp/robot-runtime-test
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -fdata-sections tests/rosey_runtime.c games/rosey-chop/*.c -Wl,--gc-sections -ldl -o /tmp/rosey-runtime-test
	/tmp/rosey-runtime-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/bramble_runtime.c games/bramble-hollow/*.c src/asset_store.c src/platform/linux/asset_platform.c -pthread -lm -o /tmp/bramble-runtime-test
	/tmp/bramble-runtime-test
