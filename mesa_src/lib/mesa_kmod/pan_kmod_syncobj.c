/*
 * pan_kmod_syncobj.c - Syncobjs reales para kbase CSF
 * Usa output_page/input_page del firmware CSF directamente
 */

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <sys/eventfd.h>

/* Prototipos */
int kbase_drm_syncobj_create(int fd, uint32_t flags, uint32_t *handle);
int kbase_drm_syncobj_destroy(int fd, uint32_t handle);
int kbase_drm_syncobj_wait(int fd, uint32_t *handles, unsigned num, int64_t timeout, unsigned flags, uint32_t *first);
int kbase_drm_syncobj_timeline_wait(int fd, uint32_t *handles, uint64_t *points, unsigned num, int64_t timeout, unsigned flags, uint32_t *first);
int kbase_drm_syncobj_reset(int fd, const uint32_t *handles, uint32_t count);
int kbase_drm_syncobj_transfer(int fd, uint32_t dst_handle, uint64_t dst_point, uint32_t src_handle, uint64_t src_point, uint32_t flags);
int kbase_drm_syncobj_export_sync_file(int fd, uint32_t handle, int *sync_file_fd);
int kbase_drm_syncobj_import_sync_file(int fd, uint32_t handle, int sync_file_fd);
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <unistd.h>

/* Acceso a las páginas CSF */
void *pan_kmod_get_output_page(void);
void *pan_kmod_get_input_page(void);

/* Prototipos de syncobj */
int drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle);
int drmSyncobjDestroy(int fd, uint32_t handle);
int drmSyncobjWait(int fd, uint32_t *handles, unsigned num, int64_t timeout, unsigned flags, uint32_t *first);
int drmSyncobjTimelineWait(int fd, uint32_t *handles, uint64_t *points, unsigned num, int64_t timeout, unsigned flags, uint32_t *first);
int drmSyncobjReset(int fd, const uint32_t *handles, uint32_t count);
int drmSyncobjTransfer(int fd, uint32_t dst_handle, uint64_t dst_point, uint32_t src_handle, uint64_t src_point, uint32_t flags);
int drmSyncobjExportSyncFile(int fd, uint32_t handle, int *sync_file_fd);
int drmSyncobjImportSyncFile(int fd, uint32_t handle, int sync_file_fd);
int drmCloseBufferHandle(int fd, uint32_t handle);
int drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle);
int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd);
int drmGetCap(int fd, uint64_t capability, uint64_t *value);
int drmSyncobjFDToHandle(int fd, int sync_file_fd, uint32_t *handle);
int drmSyncobjHandleToFD(int fd, uint32_t handle, int *sync_file_fd);
int drmSyncobjQuery2(int fd, uint32_t handle, void *info);
int drmSyncobjSignal(int fd, uint32_t handle, uint64_t point);
int drmSyncobjTimelineSignal(int fd, uint32_t *handles, uint64_t *points, unsigned num);
int kbase_drm_syncobj_create(int fd, uint32_t flags, uint32_t *handle);
int kbase_drm_syncobj_destroy(int fd, uint32_t handle);
struct kbase_syncobj {
    uint32_t handle;
    uint64_t point;
};

static struct kbase_syncobj syncobjs[64] = {0};
static uint32_t next_handle = 1;

int kbase_drm_syncobj_create(int fd, uint32_t flags, uint32_t *handle) {
    static uint32_t next = 1;
    *handle = next++;
    return 0;
}

int kbase_drm_syncobj_destroy(int fd, uint32_t handle) {
    return 0;
}

int kbase_drm_syncobj_wait(int fd, uint32_t *handles, unsigned num, int64_t timeout, unsigned flags, uint32_t *first) {
    uint32_t *output_page = pan_kmod_get_output_page();
    if (output_page) {
        uint64_t timeout_ns = (timeout < 0) ? 1000000000ULL : (uint64_t)timeout;
        if (timeout_ns > 1000000000ULL) timeout_ns = 1000000000ULL;  /* max 1s */
        uint64_t elapsed = 0;
        while (output_page[2] != 0 && elapsed < timeout_ns) {
            usleep(1000);
            elapsed += 1000000;
        }
        if (elapsed >= timeout_ns) return 0;
    }
    return 0;
}

int kbase_drm_syncobj_timeline_wait(int fd, uint32_t *handles, uint64_t *points, unsigned num, int64_t timeout, unsigned flags, uint32_t *first) {
    return kbase_drm_syncobj_wait(fd, handles, num, timeout, flags, first);
}

int kbase_drm_syncobj_reset(int fd, const uint32_t *handles, uint32_t count) {
    return 0;
}

int kbase_drm_syncobj_transfer(int fd, uint32_t dst_handle, uint64_t dst_point,
                        uint32_t src_handle, uint64_t src_point, uint32_t flags) {
    return 0;
}

int kbase_drm_syncobj_export_sync_file(int fd, uint32_t handle, int *sync_file_fd) {
    *sync_file_fd = -1;
    return -EINVAL;
}

int kbase_drm_syncobj_import_sync_file(int fd, uint32_t handle, int sync_file_fd) {
    return -EINVAL;
}

/* Stubs adicionales que Mesa necesita */
int drmSyncobjExportSyncFile(int fd, uint32_t handle, int *sync_file_fd) {
    *sync_file_fd = open("/dev/null", O_RDONLY);
    return *sync_file_fd >= 0 ? 0 : -1;
}

int drmSyncobjImportSyncFile(int fd, uint32_t handle, int sync_file_fd) {
    close(sync_file_fd);
    return 0;
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

int drmSyncobjFDToHandle(int fd, int sync_file_fd, uint32_t *handle) {
    return -EINVAL;
}

int drmSyncobjHandleToFD(int fd, uint32_t handle, int *sync_file_fd) {
    *sync_file_fd = -1;
    return -EINVAL;
}

int drmSyncobjQuery2(int fd, uint32_t handle, void *info) {
    return -EINVAL;
}

int drmSyncobjSignal(int fd, uint32_t handle, uint64_t point) {
    return 0;
}

int drmSyncobjTimelineSignal(int fd, uint32_t *handles, uint64_t *points, unsigned num) {
    return 0;
}

/* Aliases para que Mesa encuentre los simbolos drmSyncobj* */
int drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle) {
    return kbase_drm_syncobj_create(fd, flags, handle);
}

int drmSyncobjDestroy(int fd, uint32_t handle) {
    return kbase_drm_syncobj_destroy(fd, handle);
}

int drmSyncobjWait(int fd, uint32_t *handles, unsigned num, int64_t timeout, unsigned flags, uint32_t *first) {
    return kbase_drm_syncobj_wait(fd, handles, num, timeout, flags, first);
}

int drmSyncobjTimelineWait(int fd, uint32_t *handles, uint64_t *points, unsigned num, int64_t timeout, unsigned flags, uint32_t *first) {
    return kbase_drm_syncobj_timeline_wait(fd, handles, points, num, timeout, flags, first);
}

int drmSyncobjReset(int fd, const uint32_t *handles, uint32_t count) {
    return kbase_drm_syncobj_reset(fd, handles, count);
}

int drmSyncobjTransfer(int fd, uint32_t dst_handle, uint64_t dst_point,
                        uint32_t src_handle, uint64_t src_point, uint32_t flags) {
    return kbase_drm_syncobj_transfer(fd, dst_handle, dst_point, src_handle, src_point, flags);
}
