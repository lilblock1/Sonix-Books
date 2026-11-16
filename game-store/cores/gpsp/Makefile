# gpSP R1 initial interpreter build; upstream source supplied by CI.
UPSTREAM ?= ../../../../gpsp-upstream
CC ?= cc
CXX ?= c++
OUT ?= core.so
HOST_CC ?= cc
CFLAGS ?= -O2
# Do not propagate command-line CFLAGS via MAKEFLAGS: upstream must append
# its required feature macros, RGB565 support and PIC flags. Pass target
# tuning through CODE_DEFINES explicitly instead.
MAKEOVERRIDES :=
unexport CFLAGS CXXFLAGS
.PHONY: check core
check:
	$(MAKE) -C "$(UPSTREAM)" clean platform=unix HAVE_DYNAREC=0 CPU_ARCH=
	$(MAKE) -C "$(UPSTREAM)" -j2 platform=unix HAVE_DYNAREC=0 CPU_ARCH= CODE_DEFINES="-DROM_BUFFER_SIZE=4" OPTIMIZE="-O2 -DNDEBUG"
	python3 test_rom.py self-test.gba
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -I"$(UPSTREAM)/libretro/libretro-common/include" smoke.c -ldl -o smoke
	./smoke "$(UPSTREAM)/gpsp_libretro.so" "$$(pwd)/self-test.gba"
	rm -f smoke self-test.gba
	$(MAKE) -C "$(UPSTREAM)" clean platform=unix HAVE_DYNAREC=0 CPU_ARCH=
core:
	$(MAKE) -C "$(UPSTREAM)" -j2 platform=unix HAVE_DYNAREC=0 CPU_ARCH= CC="$(CC)" CXX="$(CXX)" CODE_DEFINES="$(CFLAGS) -DROM_BUFFER_SIZE=4" OPTIMIZE="-O2 -DNDEBUG"
	cp "$(UPSTREAM)/gpsp_libretro.so" "$(OUT)"
