#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <time.h>

#define CS_ACTIVE 0x0008
#define CS_ACTIVE_WORD (CS_ACTIVE / 4)

static void *get_real(const char *name) {
    static void *handle;
    if (!handle) handle = dlopen("libdrm.so", RTLD_NOW);
    return dlsym(handle ? handle : RTLD_DEFAULT, name);
}

static volatile uint32_t *get_kbase_output_page(void) {
    void *lib = dlopen("libvulkan_panfrost.so", RTLD_NOW);
    if (!lib) return NULL;
    typeof(void* (*)(void)) fn = (typeof(void* (*)(void)))dlsym(lib, "pan_kmod_get_output_page");
    return fn ? (volatile uint32_t*)fn() : NULL;
}

static volatile uint32_t *get_kbase_input_page(void) {
    void *lib = dlopen("libvulkan_panfrost.so", RTLD_NOW);
    if (!lib) return NULL;
    typeof(void* (*)(void)) fn = (typeof(void* (*)(void)))dlsym(lib, "pan_kmod_get_input_page");
    return fn ? (volatile uint32_t*)fn() : NULL;
}

int drmGetDevices2(uint32_t flags, void **devices, int max) {
    typeof(drmGetDevices2) *real = get_real("drmGetDevices2");
    return real ? real(flags, devices, max) : 0;
}

int drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle) {
    static uint32_t next = 1;
    *handle = next++;
    return 0;
}

int drmSyncobjDestroy(int fd, uint32_t handle) {
    return 0;
}

int drmSyncobjWait(int fd, uint32_t *handles, unsigned num, int64_t timeout, unsigned flags, uint32_t *first) {
    volatile uint32_t *output = get_kbase_output_page();
    if (!output || !handles || num == 0) return 0;
    struct timespec ts = {0, 100000000};
    while (output[CS_ACTIVE_WORD] & 1) nanosleep(&ts, NULL);
    return 0;
}

int drmSyncobjTimelineWait(int fd, uint32_t *handles, uint64_t *points, unsigned num, int64_t timeout, unsigned flags, uint32_t *first) {
    return drmSyncobjWait(fd, handles, num, timeout, flags, first);
}

int drmSyncobjReset(int fd, const uint32_t *handles, uint32_t count) {
    volatile uint32_t *output = get_kbase_output_page();
    volatile uint32_t *input = get_kbase_input_page();
    if (output) *output = 0;
    if (input) *input = 0;
    return 0;
}

int drmSyncobjExportSyncFile(int fd, uint32_t handle, int *sync_file_fd) {
    *sync_file_fd = open("/dev/null", O_RDONLY);
    return *sync_file_fd >= 0 ? 0 : -1;
}

int drmSyncobjImportSyncFile(int fd, uint32_t handle, int sync_file_fd) {
    close(sync_file_fd);
    return 0;
}

int drmIoctl(int fd, unsigned long request, void *arg) {
    typeof(drmIoctl) *real = get_real("drmIoctl");
    if (!real) { errno = ENOSYS; return -1; }
    return real(fd, request, arg);
}

int drmCloseBufferHandle(int fd, uint32_t handle) { return 0; }

int drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle) {
    static uint32_t next = 5000;
    *handle = next++;
    close(prime_fd);
    return 0;
}

int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd) {
    *prime_fd = open("/dev/null", O_RDONLY);
    return *prime_fd >= 0 ? 0 : -1;
}

int drmGetCap(int fd, uint64_t capability, uint64_t *value) {
    if (value) *value = 0;
    return 0;
}

int drmSyncobjTransfer(int fd, uint32_t dst_handle, uint64_t dst_point,
                        uint32_t src_handle, uint64_t src_point, uint32_t flags) {
    return 0;
}
