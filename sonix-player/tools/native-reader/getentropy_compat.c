/* R1-only link wrapper for getentropy (introduced in glibc 2.25).
 * Linux /dev/urandom provides cryptographic randomness on the running R1.
 * Never substitute a PRNG, return uninitialized data or fake success.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <unistd.h>

int __wrap_getentropy(void *buffer, size_t length)
{
    if (length > 256) {
        errno = EIO;
        return -1;
    }
    if (length == 0) return 0;
    if (!buffer) {
        errno = EFAULT;
        return -1;
    }

    int fd;
    do { fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC); }
    while (fd < 0 && errno == EINTR);
    if (fd < 0) return -1;

    unsigned char *p = buffer;
    while (length) {
        ssize_t n = read(fd, p, length);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            int failure = n == 0 ? EIO : errno;
            (void)close(fd);
            errno = failure;
            return -1;
        }
        p += (size_t)n;
        length -= (size_t)n;
    }
    (void)close(fd);
    return 0;
}

#ifdef SONIX_TEST_GETENTROPY
#include <assert.h>
#include <string.h>

int main(void)
{
    unsigned char buffer[258];
    memset(buffer, 0xA5, sizeof(buffer));
    assert(__wrap_getentropy(buffer + 1, 256) == 0);
    assert(buffer[0] == 0xA5 && buffer[257] == 0xA5);
    assert(__wrap_getentropy(NULL, 0) == 0);
    errno = 0;
    assert(__wrap_getentropy(buffer, 257) == -1 && errno == EIO);
    errno = 0;
    assert(__wrap_getentropy(NULL, 1) == -1 && errno == EFAULT);
    return 0;
}
#endif
