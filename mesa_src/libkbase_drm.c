#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

/* ── Real libdrm functions ── */
static void *get_real(const char *name) {
    static void *handle;
    if (!handle) handle = dlopen("libdrm.so", RTLD_NOW);
    return dlsym(handle ? handle : RTLD_DEFAULT, name);
}

/* ── Intercepted functions ── */

/* drmGetDevices2 - inject /dev/mali0 as fake DRM device */
int drmGetDevices2(uint32_t flags, void **devices, int max) {
    fprintf(stderr, "[kbase_drm] ENTER %s\n", __func__);
    typeof(drmGetDevices2) *real = get_real("drmGetDevices2");
    return real ? real(flags, devices, max) : 0;
}

/* drmSyncobjCreate - succeed silently */
int drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle) {
    fprintf(stderr, "[kbase_drm] ENTER %s\n", __func__);
    fprintf(stderr, "[kbase_drm] drmSyncobjCreate stub\n");
    static uint32_t next = 1000;
    *handle = next++;
    return 0;
}

/* drmSyncobjDestroy - succeed silently */
int drmSyncobjDestroy(int fd, uint32_t handle) {
    fprintf(stderr, "[kbase_drm] ENTER %s\n", __func__);
    fprintf(stderr, "[kbase_drm] drmSyncobjDestroy stub (handle=%u)\n", handle);
    return 0;
}

/* drmSyncobjWait - pretend it's already signaled */
int drmSyncobjWait(int fd, uint32_t *handles, unsigned num, int64_t timeout, unsigned flags, uint32_t *first) {
    fprintf(stderr, "[kbase_drm] ENTER %s\n", __func__);
    fprintf(stderr, "[kbase_drm] drmSyncobjWait stub (returns success)\n");
    return 0;
}

/* drmSyncobjTimelineWait - pretend signaled */
int drmSyncobjTimelineWait(int fd, uint32_t *handles, uint64_t *points, unsigned num, int64_t timeout, unsigned flags, uint32_t *first) {
    fprintf(stderr, "[kbase_drm] ENTER %s\n", __func__);
    fprintf(stderr, "[kbase_drm] drmSyncobjTimelineWait stub\n");
    return 0;
}

/* drmSyncobjExportSyncFile - return a dummy fd */
int drmSyncobjExportSyncFile(int fd, uint32_t handle, int *sync_file_fd) {
    fprintf(stderr, "[kbase_drm] ENTER %s\n", __func__);
    fprintf(stderr, "[kbase_drm] drmSyncobjExportSyncFile stub\n");
    *sync_file_fd = open("/dev/null", O_RDONLY);
    return *sync_file_fd >= 0 ? 0 : -1;
}

/* drmSyncobjImportSyncFile - succeed */
int drmSyncobjImportSyncFile(int fd, uint32_t handle, int sync_file_fd) {
    fprintf(stderr, "[kbase_drm] ENTER %s\n", __func__);
    fprintf(stderr, "[kbase_drm] drmSyncobjImportSyncFile stub\n");
    close(sync_file_fd);
    return 0;
}

/* drmIoctl - diagnostic: return the real ioctl result */
int drmIoctl(int fd, unsigned long request, void *arg)
{
    fprintf(stderr,
            "[kbase_drm] ENTER drmIoctl fd=%d request=0x%lx\n",
            fd, request);

    typeof(drmIoctl) *real = get_real("drmIoctl");

    if (!real) {
        fprintf(stderr,
                "[kbase_drm] ERROR: real drmIoctl not found\n");
        errno = ENOSYS;
        return -1;
    }

    errno = 0;

    int r = real(fd, request, arg);
    int saved_errno = errno;

    fprintf(stderr,
            "[kbase_drm] REAL drmIoctl r=%d errno=%d (%s)\n",
            r, saved_errno, strerror(saved_errno));

    return r;
}

/* drmCloseBufferHandle - succeed */
int drmCloseBufferHandle(int fd, uint32_t handle) {
    fprintf(stderr, "[kbase_drm] ENTER %s\n", __func__);
    fprintf(stderr, "[kbase_drm] drmCloseBufferHandle stub (handle=%u)\n", handle);
    return 0;
}

/* drmPrimeFDToHandle - succeed with fake handle */
int drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle) {
    fprintf(stderr, "[kbase_drm] ENTER %s\n", __func__);
    fprintf(stderr, "[kbase_drm] drmPrimeFDToHandle stub\n");
    static uint32_t next = 5000;
    *handle = next++;
    close(prime_fd);
    return 0;
}

/* drmPrimeHandleToFD - return dummy fd */
int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd) {
    fprintf(stderr, "[kbase_drm] ENTER %s\n", __func__);
    fprintf(stderr, "[kbase_drm] drmPrimeHandleToFD stub\n");
    *prime_fd = open("/dev/null", O_RDONLY);
    return *prime_fd >= 0 ? 0 : -1;
}


/* Stub for drmGetCap - DRM_IOCTL_GET_CAP wrapper */
int drmGetCap(int fd, uint64_t capability, uint64_t *value) {
    fprintf(stderr, "[kbase_drm] ENTER %s\n", __func__);
    fprintf(stderr, "[kbase_drm] drmGetCap stub (cap=%llu)\n", (unsigned long long)capability);
    if (value) *value = 0;
    return 0;
}



/* drmSyncobjReset - succeed silently */
int drmSyncobjReset(int fd, const uint32_t *handles, uint32_t count) {
    fprintf(stderr, "[kbase_drm] ENTER %s\n", __func__);
    fprintf(stderr, "[kbase_drm] drmSyncobjReset stub\n");
    return 0;
}

/* drmSyncobjTransfer - succeed silently */
int drmSyncobjTransfer(int fd, uint32_t dst_handle, uint64_t dst_point,
                        uint32_t src_handle, uint64_t src_point, uint32_t flags) {
    fprintf(stderr, "[kbase_drm] drmSyncobjTransfer stub\n");
    return 0;
}
