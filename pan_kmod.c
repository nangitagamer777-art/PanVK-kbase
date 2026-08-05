#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include "util/macros.h"
#include "util/u_memory.h"
#include "pan_kmod.h"
#include "pan_kmod_backend.h"

extern const struct pan_kmod_ops panfrost_kmod_ops;
extern const struct pan_kmod_ops panthor_kmod_ops;

/* ── kbase ioctl defines ── */
#define KBASE_IOCTL_TYPE  0x80
#define BASE_MEM_PROT_CPU_RD  (1U<<0)
#define BASE_MEM_PROT_CPU_WR  (1U<<1)
#define BASE_MEM_PROT_GPU_RD  (1U<<2)
#define BASE_MEM_PROT_GPU_WR  (1U<<3)
#define BASE_MEM_PROT_GPU_EX  (1U<<4)

struct kbase_vc { __u16 major, minor; };
#define KBASE_IOCTL_VERSION_CHECK _IOWR(0x80, 52, struct kbase_vc)
struct kbase_sf { __u32 create_flags; };
#define KBASE_IOCTL_SET_FLAGS _IOW(0x80, 1, struct kbase_sf)
struct kbase_ctxid { __u32 id; };
#define KBASE_IOCTL_GET_CONTEXT_ID _IOR(0x80, 17, struct kbase_ctxid)
union kbase_ma {
    struct { __u64 va_pages, commit_pages, extension, flags; } in;
    struct { __u64 flags, gpu_va; } out;
};
#define KBASE_IOCTL_MEM_ALLOC _IOWR(0x80, 5, union kbase_ma)
union kbase_cqgc_16 {
    struct { __u64 tiler, frag, comp; __u8 cs_min, prio, tiler_max, frag_max, comp_max; __u8 pad[3]; } in;
    struct { __u8 handle; __u8 pad[3]; __u32 uid; } out;
};
#define KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_1_6 _IOWR(0x80, 42, union kbase_cqgc_16)

/* ── kbase device ── */
struct kbase_dev { struct pan_kmod_dev base; uint8_t group; uint32_t ctx; };

static struct pan_kmod_dev *
kbase_dev_create(int fd, uint32_t flags, const struct pan_kmod_driver *drv, const struct pan_kmod_allocator *alloc)
{
    struct kbase_dev *kd = pan_kmod_alloc(alloc, sizeof(*kd));
    if (!kd) return NULL;
    struct kbase_vc ver = { .major = 11, .minor = 11 };
    if (ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver) < 0) { pan_kmod_free(alloc, kd); return NULL; }
    struct kbase_sf sf = { .create_flags = 0 };
    if (ioctl(fd, KBASE_IOCTL_SET_FLAGS, &sf) < 0) { pan_kmod_free(alloc, kd); return NULL; }
    struct kbase_ctxid ctx = {0}; ioctl(fd, KBASE_IOCTL_GET_CONTEXT_ID, &ctx);
    kd->ctx = ctx.id;
    union kbase_cqgc_16 grp = {0};
    grp.in.tiler = grp.in.frag = grp.in.comp = ~0ULL;
    grp.in.cs_min = 1; grp.in.tiler_max = grp.in.frag_max = grp.in.comp_max = 8;
    if (ioctl(fd, KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_1_6, &grp) < 0) { pan_kmod_free(alloc, kd); return NULL; }
    kd->group = grp.out.handle;
    pan_kmod_dev_init(&kd->base, fd, flags, drv, &panfrost_kmod_ops, alloc);
    kd->base.props.gpu_id = 0xA8070000;
	fprintf(stderr, "[kbase] kbase_dev_create: setting gpu_id=0x%llx\n", (unsigned long long)kd->base.props.gpu_id);
    kd->base.props.shader_present = 0x3;
    kd->base.props.pgsize_bitmap = 0x1;
    fprintf(stderr, "[kbase] device ready, fd=%d group=%u ctx=%u\n", fd, kd->group, kd->ctx);
    return &kd->base;
}

static void kbase_dev_destroy(struct pan_kmod_dev *dev) { close(dev->fd); pan_kmod_dev_cleanup(dev); pan_kmod_free(dev->allocator, dev); }
static struct pan_kmod_bo *kbase_bo_alloc(struct pan_kmod_dev *dev, struct pan_kmod_vm *vm, uint64_t size, uint32_t flags) {
    union kbase_ma mem = {0};
    mem.in.va_pages = (size+4095)/4096; mem.in.commit_pages = mem.in.va_pages;
    mem.in.flags = BASE_MEM_PROT_CPU_RD|BASE_MEM_PROT_CPU_WR|BASE_MEM_PROT_GPU_RD;
    mem.in.flags |= (flags&1)?BASE_MEM_PROT_GPU_EX:BASE_MEM_PROT_GPU_WR;
    if (ioctl(dev->fd, KBASE_IOCTL_MEM_ALLOC, &mem) < 0) return NULL;
    struct pan_kmod_bo *bo = pan_kmod_dev_alloc(dev, sizeof(*bo));
    if (!bo) return NULL;
    pan_kmod_bo_init(bo, dev, vm, size, flags, (uint32_t)mem.out.gpu_va);
    return bo;
}
static void kbase_bo_free(struct pan_kmod_bo *bo) { pan_kmod_bo_cleanup(bo); pan_kmod_dev_free(bo->dev, bo); }
static off_t kbase_bo_get_mmap_offset(struct pan_kmod_bo *bo) { return (off_t)bo->handle; }
static bool kbase_bo_wait(struct pan_kmod_bo *bo, int64_t t, bool r) { return true; }
static void kbase_bo_set_label(struct pan_kmod_dev *d, struct pan_kmod_bo *b, const char *l) {}
static struct pan_kmod_bo *kbase_bo_import(struct pan_kmod_dev *d, uint32_t h, uint64_t s) { return NULL; }
static int kbase_flush(struct pan_kmod_dev *d) { return 0; }
static uint64_t kbase_timestamp(const struct pan_kmod_dev *d) { return 0; }
static struct pan_kmod_vm *kbase_vm_create(struct pan_kmod_dev *dev, uint32_t flags, uint64_t start, uint64_t range) {
    struct pan_kmod_vm *vm = pan_kmod_dev_alloc(dev, sizeof(*vm));
    if (vm) pan_kmod_vm_init(vm, dev, 0, flags);
    return vm;
}
static void kbase_vm_destroy(struct pan_kmod_vm *vm) { pan_kmod_vm_cleanup(vm); pan_kmod_dev_free(vm->dev, vm); }
static int kbase_vm_bind(struct pan_kmod_vm *vm, enum pan_kmod_vm_op_mode mode, struct pan_kmod_vm_op *ops, uint32_t n) { return 0; }

static const struct pan_kmod_ops kbase_kmod_ops = {
    .dev_create = kbase_dev_create, .dev_destroy = kbase_dev_destroy,
    .bo_alloc = kbase_bo_alloc, .bo_free = kbase_bo_free, .bo_import = kbase_bo_import,
    .bo_get_mmap_offset = kbase_bo_get_mmap_offset, .bo_wait = kbase_bo_wait,
    .flush_bo_map_syncs = kbase_flush, .vm_create = kbase_vm_create,
    .vm_destroy = kbase_vm_destroy, .vm_bind = kbase_vm_bind,
    .query_timestamp = kbase_timestamp, .bo_set_label = kbase_bo_set_label,
};

/* ── driver table ── */
static const struct { const char *name; const struct pan_kmod_ops *ops; } drivers[] = {
    { "panfrost", &panfrost_kmod_ops },
    { "panthor", &panthor_kmod_ops },
    { "kbase", &kbase_kmod_ops },
};

static void *default_zalloc(const struct pan_kmod_allocator *a, size_t s, bool t) { return os_calloc(1, s); }
static void default_free(const struct pan_kmod_allocator *a, void *d) { os_free(d); }
static const struct pan_kmod_allocator default_allocator = { .zalloc = default_zalloc, .free = default_free };

/* ── pan_kmod_dev_create with kbase fallback ── */
struct pan_kmod_dev *
pan_kmod_dev_create(int fd, uint32_t flags, const struct pan_kmod_allocator *allocator)
{
    if (!allocator) allocator = &default_allocator;
    struct pan_kmod_dev *dev = NULL;
    drmVersionPtr version = drmGetVersion(fd);

    if (version) {
        const struct pan_kmod_driver drv_info = {
            .version = { .major = version->version_major, .minor = version->version_minor },
        };
        for (unsigned i = 0; i < ARRAY_SIZE(drivers); i++) {
            if (!strcmp(drivers[i].name, version->name)) {
                dev = drivers[i].ops->dev_create(fd, flags, &drv_info, allocator);
                break;
            }
        }
        drmFreeVersion(version);
        if (dev) return dev;
    }

    /* DRM failed - try /dev/mali0 */
    int kbase_fd = open("/dev/mali0", O_RDWR);
    if (kbase_fd >= 0) {
        const struct pan_kmod_driver drv_info = { .version = { .major = 1, .minor = 0 } };
        dev = kbase_dev_create(kbase_fd, flags, &drv_info, allocator);
        if (dev) { dev->flags |= PAN_KMOD_DEV_FLAG_OWNS_FD; return dev; }
        close(kbase_fd);
    }
    return NULL;
}

void pan_kmod_dev_destroy(struct pan_kmod_dev *dev) { dev->ops->dev_destroy(dev); }

struct pan_kmod_bo *pan_kmod_bo_alloc(struct pan_kmod_dev *dev, struct pan_kmod_vm *vm, uint64_t size, uint32_t flags) {
    struct pan_kmod_bo *bo = dev->ops->bo_alloc(dev, vm, size, flags);
    if (bo) { simple_mtx_lock(&dev->handle_to_bo.lock);
        struct pan_kmod_bo **slot = util_sparse_array_get(&dev->handle_to_bo.array, bo->handle);
        *slot = bo; simple_mtx_unlock(&dev->handle_to_bo.lock); }
    return bo;
}
void pan_kmod_bo_put(struct pan_kmod_bo *bo) {
    if (p_atomic_dec_return(&bo->refcnt) == 0) bo->dev->ops->bo_free(bo);
}
struct pan_kmod_bo *pan_kmod_bo_import(struct pan_kmod_dev *dev, int fd) { return NULL; }

void pan_kmod_flush_bo_map_syncs(struct pan_kmod_dev *dev) {}
void pan_kmod_queue_bo_map_sync(struct pan_kmod_bo *bo, uint64_t o, void *p, uint64_t r, enum pan_kmod_bo_sync_type t) {}
void pan_kmod_flush_bo_map_syncs_locked(struct pan_kmod_dev *dev) {}
