# Build the pinned upstream source, checked out by game-store-core.yml.
UPSTREAM ?= ../../../../quicknes-upstream
CC ?= cc
CXX ?= c++
OUT ?= core.so
CFLAGS ?= -O2
HOST_CC ?= cc
.PHONY: check core
check:
	$(MAKE) -C "$(UPSTREAM)" clean
	$(MAKE) -C "$(UPSTREAM)" -j2 platform=unix GIT_VERSION=r1-test
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -I"$(UPSTREAM)/libretro/libretro-common/include" smoke.c -ldl -o smoke
	./smoke "$(UPSTREAM)/quicknes_libretro.so"
	rm -f smoke
	$(MAKE) -C "$(UPSTREAM)" clean
core:
	$(MAKE) -C "$(UPSTREAM)" -j2 platform=unix CC="$(CC)" CXX="$(CXX)" GIT_VERSION=r1-test PLATFORM_DEFINES="$(CFLAGS) -fno-rtti -fno-exceptions" LDFLAGS="-static-libstdc++ -static-libgcc"
	cp "$(UPSTREAM)/quicknes_libretro.so" "$(OUT)"
