CC ?= cc
.DEFAULT_GOAL := all
NODE ?= node

CPPFLAGS += -D_GNU_SOURCE -Iinclude
CFLAGS += -std=c11 -O2 -Wall -Wextra -Wpedantic
LDLIBS += -Wl,--no-as-needed -l:libdrm.so.2 -l:libgbm.so.1 -l:libEGL.so.1 -l:libGLESv2.so.2

TARGET := build/chirky-host
SOURCES := src/host.c src/input_bindings.c src/rect_renderer.c
GAME_SOURCES := $(wildcard games/*/game.c)
GAME_TARGETS := $(patsubst games/%/game.c,build/games/%.so,$(GAME_SOURCES))

.PHONY: all clean test benchmark performance-benchmark web

EMCC ?= emcc
WEB_FLAGS = -Iinclude -Isrc -std=c11 -O2 -Wall -Wextra -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1 -sFORCE_FILESYSTEM=1 -sEXPORTED_RUNTIME_METHODS=FS,ccall --no-entry
WEB_COMMON = web/host.c src/rect_renderer.c
WEB_HEADERS = $(wildcard include/*.h) src/rect_renderer.h
WEB_TARGETS = $(patsubst games/%/game.c,build/web/%.js,$(wildcard games/*/game.c))
.SECONDEXPANSION:

web: build/web/launcher.js $(WEB_TARGETS)
	$(NODE) tools/web-assets.js

build/web/launcher.js: $(WEB_COMMON) $(WEB_HEADERS)
	mkdir -p $(@D)
	$(EMCC) $(WEB_FLAGS) -DCHIRKY_WEB_LAUNCHER $(WEB_COMMON) -o $@

build/web/%.js: games/%/game.c $$(wildcard games/$$*/*.c games/$$*/*.h) $(WEB_COMMON) $(WEB_HEADERS)
	mkdir -p $(@D)
	$(EMCC) $(WEB_FLAGS) $(WEB_COMMON) $(wildcard games/$*/*.c) -o $@

all: $(TARGET) $(GAME_TARGETS)

$(TARGET): $(SOURCES) src/input_bindings.h src/rect_renderer.h src/frame_timing.h src/gpu_timing.h src/profile.h include/pixel_font.h include/launcher_config.h include/launcher_wordmark.h include/splash_art.h include/chirky.h include/input_gate.h
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -o $@.next $(LDLIBS)
	mv $@.next $@

.SECONDEXPANSION:
build/games/%.so: games/%/game.c $$(wildcard games/$$*/*.c games/$$*/*.h) include/chirky.h include/input_gate.h include/splash_art.h
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Iinclude -fPIC -shared $(filter %.c,$^) -lm -o $@.next
	mv $@.next $@

clean:
	rm -rf build

# EGL pbuffer benchmark; leaves the running CRT host and its input untouched.
benchmark: build/games/rosey-chop.so
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -fdata-sections tools/render_benchmark.c src/rect_renderer.c -Wl,--gc-sections -ldl -l:libEGL.so.1 -l:libGLESv2.so.2 -o build/render-benchmark
	./build/render-benchmark

# Build only: the explicit supervisor command temporarily takes over the CRT.
performance-benchmark: build/performance-benchmark $(GAME_TARGETS)

build/performance-benchmark: tools/performance_benchmark.c $(SOURCES) $(wildcard src/*.h include/*.h)
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -fdata-sections tools/performance_benchmark.c src/input_bindings.c -Wl,--gc-sections -ldl $(LDLIBS) -o $@

test:
	python3 tests/performance_analysis.py
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/profile.c -o /tmp/profile-test
	/tmp/profile-test
	python3 tests/gpu_profiler.py
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/gpu_timing.c -o /tmp/gpu-timing-test
	/tmp/gpu-timing-test
	$(NODE) --test dashboard/editors.test.js dashboard/ppm.test.js dashboard/launcher.test.js
	sh tests/service_install.sh
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/splash_runtime.c -o /tmp/splash-runtime-test
	/tmp/splash-runtime-test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/launcher_runtime.c -o /tmp/launcher-runtime-test
	/tmp/launcher-runtime-test
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -fdata-sections tests/host_runtime.c src/input_bindings.c src/rect_renderer.c -Wl,--gc-sections -ldl -l:libGLESv2.so.2 -o /tmp/host-runtime-test
	/tmp/host-runtime-test $(CURDIR)/build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Igames/phosphor-run tests/phosphor_runtime.c games/phosphor-run/*.c -lm -o /tmp/phosphor-runtime-test
	/tmp/phosphor-runtime-test
	$(CC) $(CPPFLAGS) $(CFLAGS) -Igames/phosphor-run tests/robot_runtime.c games/phosphor-run/robot.c -lm -o /tmp/robot-runtime-test
	/tmp/robot-runtime-test
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -fdata-sections tests/rosey_runtime.c games/rosey-chop/*.c -Wl,--gc-sections -ldl -o /tmp/rosey-runtime-test
	/tmp/rosey-runtime-test
