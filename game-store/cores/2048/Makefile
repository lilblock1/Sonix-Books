# Original native game core; make check uses host compiler, make core uses CC.
CC ?= cc
HOST_CC ?= cc
CFLAGS ?= -O2
OUT ?= core.so

.PHONY: core check
core:
	$(CC) -std=c11 $(CFLAGS) -Wall -Wextra -Werror -fPIC -shared -Wl,--no-undefined core.c -o $(OUT)
check:
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -DSONIX_CORE_TEST core.c -o core-test
	./core-test
	rm -f core-test
