# Build the pinned upstream source, checked out by game-store-core.yml.
UPSTREAM ?= ../../../../quicknes-upstream
CC ?= cc
CXX ?= c++
OUT ?= core.so
CFLAGS ?= -O2
HOST_CC ?= cc
CORE_VERSION ?= v1.0.0
.PHONY: check core
check:
	$(MAKE) -C "$(UPSTREAM)" clean
	$(MAKE) -C "$(UPSTREAM)" -j2 platform=unix GIT_VERSION=r1-test
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -I"$(UPSTREAM)/libretro/libretro-common/include" smoke.c -ldl -o smoke
	./smoke "$(UPSTREAM)/quicknes_libretro.so" > baseline-crop.txt
	./smoke "$(UPSTREAM)/quicknes_libretro.so" full > baseline-full.txt
	cat baseline-crop.txt baseline-full.txt
ifeq ($(CORE_VERSION),v1.1.0)
	python3 optimize.py "$(UPSTREAM)"
	$(MAKE) -C "$(UPSTREAM)" -j2 platform=unix GIT_VERSION=r1-test
	./smoke "$(UPSTREAM)/quicknes_libretro.so" > optimized-crop.txt
	./smoke "$(UPSTREAM)/quicknes_libretro.so" full > optimized-full.txt
	diff -u baseline-crop.txt optimized-crop.txt
	diff -u baseline-full.txt optimized-full.txt
	@echo "Identical visible pixels, audio samples and input counts for both modes"
endif
	rm -f smoke baseline-crop.txt baseline-full.txt optimized-crop.txt optimized-full.txt
	$(MAKE) -C "$(UPSTREAM)" clean
core:
	$(MAKE) -C "$(UPSTREAM)" -j2 platform=unix CC="$(CC)" CXX="$(CXX)" GIT_VERSION=r1-test PLATFORM_DEFINES="$(CFLAGS) $(if $(filter v1.1.0,$(CORE_VERSION)),-O2) -fno-rtti -fno-exceptions" LDFLAGS="-static-libstdc++ -static-libgcc"
	cp "$(UPSTREAM)/quicknes_libretro.so" "$(OUT)"
