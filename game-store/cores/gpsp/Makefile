# gpSP R1 initial interpreter build; upstream source supplied by CI.
UPSTREAM ?= ../../../../gpsp-upstream
CC ?= cc
CXX ?= c++
OUT ?= core.so
HOST_CC ?= cc
CORE_VERSION ?= v1.0.0
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
ifeq ($(CORE_VERSION),v1.1.0)
	python3 r1_dynarec.py "$(UPSTREAM)"
	$(MAKE) -C "$(UPSTREAM)" -j2 platform=unix HAVE_DYNAREC=1 CPU_ARCH=mips MMAP_JIT_CACHE=1 CC="$(CC)" CXX="$(CXX)" CODE_DEFINES="$(CFLAGS) -DROM_BUFFER_SIZE=4 -DSMALL_TRANSLATION_CACHE -DMIPS_HAS_R2_INSTS -DPIC" OPTIMIZE="-O2 -DNDEBUG" LDFLAGS="-Wl,--no-undefined -Wl,-z,noexecstack"
else
	$(MAKE) -C "$(UPSTREAM)" -j2 platform=unix HAVE_DYNAREC=0 CPU_ARCH= CC="$(CC)" CXX="$(CXX)" CODE_DEFINES="$(CFLAGS) -DROM_BUFFER_SIZE=4" OPTIMIZE="-O2 -DNDEBUG"
endif
	cp "$(UPSTREAM)/gpsp_libretro.so" "$(OUT)"
