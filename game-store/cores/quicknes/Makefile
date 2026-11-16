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
	@set -eu; for mode in crop full; do for rom in tone picture; do \
	  ./smoke "$(UPSTREAM)/quicknes_libretro.so" "$$mode" "$$rom" > "baseline-$$mode-$$rom.txt"; \
	  ./smoke "$(UPSTREAM)/quicknes_libretro.so" "$$mode" "$$rom" > "repeat-$$mode-$$rom.txt"; \
	  diff -u "baseline-$$mode-$$rom.txt" "repeat-$$mode-$$rom.txt"; \
	  cat "baseline-$$mode-$$rom.txt"; done; done
ifeq ($(CORE_VERSION),v1.1.0)
	python3 optimize.py "$(UPSTREAM)"
	$(MAKE) -C "$(UPSTREAM)" -j2 platform=unix GIT_VERSION=r1-test
	@set -eu; for mode in crop full; do for rom in tone picture; do \
	  ./smoke "$(UPSTREAM)/quicknes_libretro.so" "$$mode" "$$rom" > "optimized-$$mode-$$rom.txt"; \
	  diff -u "baseline-$$mode-$$rom.txt" "optimized-$$mode-$$rom.txt"; done; done
	@echo "Identical visible pixels, audio samples and input counts in all four fresh-process scenarios"
endif
	rm -f smoke baseline-*.txt repeat-*.txt optimized-*.txt
	$(MAKE) -C "$(UPSTREAM)" clean
core:
	$(MAKE) -C "$(UPSTREAM)" -j2 platform=unix CC="$(CC)" CXX="$(CXX)" GIT_VERSION=r1-test PLATFORM_DEFINES="$(CFLAGS) -fno-rtti -fno-exceptions" LDFLAGS="-static-libstdc++ -static-libgcc"
	cp "$(UPSTREAM)/quicknes_libretro.so" "$(OUT)"
