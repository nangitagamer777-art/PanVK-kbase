#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <sys/mman.h>

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    typeof(mmap) *real = dlsym(RTLD_NEXT, "mmap");
    if (addr && (uintptr_t)addr >= 0x100000000ULL) {
        fprintf(stderr, "[mmap_fix] fixing addr 0x%llx -> NULL\n", (unsigned long long)addr);
        addr = NULL;
    }
    return real(addr, length, prot, flags, fd, offset);
}
