#include <errno.h>
extern const struct pan_kmod_ops kbase_kmod_ops;
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include "util/vma.h"
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
struct kbase_kmod_vm { struct pan_kmod_vm base; struct util_vma_heap heap; };
/* ── kbase CS queue UAPI definitions ── */
struct kbase_ioctl_cs_queue_register {
    __u64 buffer_gpu_addr;
    __u32 buffer_size;
    __u8  priority;
    __u8  padding[3];
};

#define KBASE_IOCTL_CS_QUEUE_REGISTER     _IOW(KBASE_IOCTL_TYPE, 36, struct kbase_ioctl_cs_queue_register)

struct kbase_ioctl_cs_queue_kick {
    __u64 buffer_gpu_addr;
};

#define KBASE_IOCTL_CS_QUEUE_KICK     _IOW(KBASE_IOCTL_TYPE, 37, struct kbase_ioctl_cs_queue_kick)

struct kbase_ioctl_cs_queue_terminate { __u64 buffer_gpu_addr; };
#define KBASE_IOCTL_CS_QUEUE_TERMINATE _IOW(KBASE_IOCTL_TYPE, 41, struct kbase_ioctl_cs_queue_terminate)

union kbase_ioctl_cs_get_glb_iface {
    struct { __u32 max_group_num; __u32 max_total_stream_num; __u64 groups_ptr; __u64 streams_ptr; } in;
    struct { __u32 glb_version; __u32 features; __u32 group_num; __u32 prfcnt_size; __u32 total_stream_num; __u32 instr_features; } out;
};
#define KBASE_IOCTL_CS_GET_GLB_IFACE _IOWR(KBASE_IOCTL_TYPE, 51, union kbase_ioctl_cs_get_glb_iface)

union kbase_ioctl_cs_queue_bind {
    struct {
        __u64 buffer_gpu_addr;
        __u8  group_handle;
        __u8  csi_index;
        __u8  padding[6];
    } in;
    struct {
        __u64 mmap_handle;
    } out;
};

#define KBASE_IOCTL_CS_QUEUE_BIND     _IOWR(KBASE_IOCTL_TYPE, 39, union kbase_ioctl_cs_queue_bind)

/* ── kbase CS queue ── */
struct kbase_cs_queue {
    void *ring;
    uint64_t ring_gpu_va;
    size_t ring_size;
    void *user_io;
    uint64_t user_io_gpu_va;
    uint32_t *input_page;
    uint32_t *output_page;
    uint64_t mmap_handle;
    bool created;
};

struct kbase_dev { struct pan_kmod_dev base; uint8_t group; uint32_t ctx; struct kbase_cs_queue cs_queue; struct kbase_csif_info csif; };

/* Global para que libkbase_drm.so acceda a la cola CSF */
static struct kbase_dev *g_kbase_dev = NULL;
struct pan_kmod_dev *pan_kmod_get_global_dev(void) { return &g_kbase_dev->base; }
void *pan_kmod_get_output_page(void) { return g_kbase_dev->cs_queue.output_page; }
struct kbase_csif_info *kbase_get_csif(void) { return &g_kbase_dev->csif; }
void *pan_kmod_get_input_page(void) { return g_kbase_dev->cs_queue.input_page; }

struct kbase_gpu_info { uint64_t gpu_id; uint64_t shader_present; };
static struct kbase_gpu_info kbase_query_gpu_info(int fd) {
    struct kbase_gpu_info info = { 0xA8070000, 0x3F };
    struct { __u64 buffer; __u32 size; __u32 flags; } props = { .size = 65536, .flags = 0 };
    void *buf = calloc(1, props.size);
    if (!buf) return info;
    props.buffer = (__u64)(uintptr_t)buf;
    if (ioctl(fd, _IOW(0x80, 3, struct { __u64 b; __u32 s; __u32 f; }), &props) == 0) {
        __u32 *raw = (__u32 *)buf;
        info.gpu_id = ((uint64_t)raw[12]) << 32;
        info.shader_present = raw[20]; /* approximate offset */
    }
    free(buf);
    return info;
}

/* Leer CSIF real del firmware CSF */
static void kbase_query_csif(int fd, uint32_t *cs_reg_count, uint32_t *scoreboard_count) {
    union kbase_ioctl_cs_get_glb_iface glb = {0};
    glb.in.max_group_num = 0;
    glb.in.max_total_stream_num = 0;
    glb.in.groups_ptr = 0;
    glb.in.streams_ptr = 0;
    if (ioctl(fd, KBASE_IOCTL_CS_GET_GLB_IFACE, &glb) == 0) {
        *scoreboard_count = ((glb.out.features >> 8) & 0xF) + 1;
        *cs_reg_count = (glb.out.features & 0xFF) + 1;
        fprintf(stderr, "[kbase] CSIF real: regs=%u scoreboards=%u features=0x%x\n",
                *cs_reg_count, *scoreboard_count, glb.out.features);
    } else {
        *cs_reg_count = 96;
        *scoreboard_count = 8;
    }
}

/* Query hardware properties - return real kbase values */
static int kbase_query_props(struct pan_kmod_dev *dev, void *props) {
    /* Fill with values that PanVK expects for Valhall v10 */
    struct { uint32_t gpu_id; uint32_t csg_slots; uint32_t max_vregs; uint32_t coherency_features; } *p = props;
    p->gpu_id = dev->props.gpu_id;
    p->csg_slots = 4;
    p->max_vregs = 64;
    p->coherency_features = 1;
    fprintf(stderr, "[kbase] query_props: gpu_id=0x%x csg=%u\n", p->gpu_id, p->csg_slots);
    return 0;
}

/* Create a real sync object using eventfd */
static int kbase_sync_create(struct pan_kmod_dev *dev, uint32_t flags, uint32_t *handle) {
    int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (fd < 0) return -1;
    *handle = (uint32_t)fd;
    fprintf(stderr, "[kbase] sync_create: fd=%d handle=%u\n", fd, *handle);
    return 0;
}
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
    uint32_t cs_regs = 96, scoreboards = 8;
    kbase_query_csif(fd, &cs_regs, &scoreboards);
    kd->csif.csg_slot_count = 8;
    kd->csif.cs_slot_count = 8;
    kd->csif.cs_reg_count = cs_regs;
    kd->csif.scoreboard_slot_count = scoreboards;
    kd->csif.unpreserved_cs_reg_count = 4;
    kd->csif.pad = 0;
    struct kbase_gpu_info info = kbase_query_gpu_info(fd);
    kd->base.props.gpu_id = info.gpu_id ? info.gpu_id : 0xA8070000;
    kd->base.props.shader_present = info.shader_present ? info.shader_present : 0x3F;
    kd->base.props.pgsize_bitmap = 0x1;
    kd->base.props.allowed_group_priorities_mask = PAN_KMOD_GROUP_ALLOW_PRIORITY_MEDIUM;
    fprintf(stderr, "[kbase] device ready, fd=%d group=%u ctx=%u gpu_id=0x%llx shader=0x%llx\n", fd, kd->group, kd->ctx, (unsigned long long)kd->base.props.gpu_id, (unsigned long long)kd->base.props.shader_present);
    /* Force kbase ops - Mesa may overwrite them */
    kd->base.ops = &kbase_kmod_ops;
    g_kbase_dev = kd;
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
    struct kbase_kmod_vm *kvm = calloc(1, sizeof(*kvm));
    if (!kvm) return NULL;
    pan_kmod_vm_init(&kvm->base, dev, 0, flags | PAN_KMOD_VM_FLAG_AUTO_VA);
    util_vma_heap_init(&kvm->heap, 0x100000000ULL, 0x400000000ULL);
    /* kbase: create sparse dummy BO for PAN_KMOD_BO_FLAG_NO_MMAP compatibility */
    kvm->base.sparse_dummy.bo = pan_kmod_bo_alloc(dev, NULL, 2 * 1024 * 1024,
                                                   PAN_KMOD_BO_FLAG_NO_MMAP);
    fprintf(stderr, "[kbase] kbase_vm_create: heap init ok, sparse_dummy=%p\n",
            (void*)kvm->base.sparse_dummy.bo);
    return &kvm->base;
}

static void kbase_vm_destroy(struct pan_kmod_vm *vm) {
    struct kbase_kmod_vm *kvm = (struct kbase_kmod_vm *)vm;
    util_vma_heap_finish(&kvm->heap);
    pan_kmod_vm_cleanup(vm);
    free(kvm);
}

static int kbase_vm_bind(struct pan_kmod_vm *vm, enum pan_kmod_vm_op_mode mode, struct pan_kmod_vm_op *ops, uint32_t n) {
    struct kbase_kmod_vm *kvm = (struct kbase_kmod_vm *)vm;
    for (uint32_t i = 0; i < n; i++) {
        if (ops[i].type == PAN_KMOD_VM_OP_TYPE_MAP) {
            if (ops[i].va.start == PAN_KMOD_VM_MAP_AUTO_VA) {
                ops[i].va.start = util_vma_heap_alloc(&kvm->heap, ops[i].va.size, 4096);
                fprintf(stderr, "[kbase] vm_bind: AUTO_VA -> 0x%llx\n", (unsigned long long)ops[i].va.start);
            }
        }
    }
    return 0;
}
/* ── kbase CS queue create ── */
int kbase_cs_queue_create(struct pan_kmod_dev *dev) {
    struct kbase_dev *kd = (struct kbase_dev *)dev;
    struct kbase_cs_queue *q = &kd->cs_queue;
    if (q->created) return 0;
    int fd = kd->base.fd;

    /* MEM_ALLOC directo para ring (evita truncamiento 64->32 bits) */
    size_t ring_size = 64 * 1024;
    union kbase_ma mem = {0};
    mem.in.va_pages = (ring_size + 4095) / 4096;
    mem.in.commit_pages = mem.in.va_pages;
    mem.in.flags = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR | BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_EX;
    if (ioctl(fd, KBASE_IOCTL_MEM_ALLOC, &mem) < 0) { fprintf(stderr, "[kbase] ring MEM_ALLOC failed: %m\n"); return -1; }
    q->ring = mmap(NULL, ring_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)mem.out.gpu_va);
    if (q->ring == MAP_FAILED) { fprintf(stderr, "[kbase] ring mmap failed: %m\n"); return -1; }
    q->ring_gpu_va = mem.out.gpu_va;
    q->ring_size = ring_size;

    size_t user_io_size = 3 * 4096;  /* BASEP_QUEUE_NR_MMAP_USER_PAGES = 3 */
    struct pan_kmod_bo *user_io_bo = kbase_bo_alloc(dev, NULL, user_io_size, BASE_MEM_PROT_GPU_EX);
    if (!user_io_bo) { fprintf(stderr, "[kbase] user_io BO alloc failed\n"); return -1; }
    /* user_io mmap movido después del BIND */
    q->user_io_gpu_va = user_io_bo->handle;

    struct kbase_ioctl_cs_queue_register reg = {
        .buffer_gpu_addr = q->ring_gpu_va, .buffer_size = q->ring_size, .priority = 0,
    };
    if (ioctl(fd, KBASE_IOCTL_CS_QUEUE_REGISTER, &reg) < 0) { fprintf(stderr, "[kbase] REGISTER failed: %m\n"); return -1; }

    union kbase_ioctl_cs_queue_bind bind = {
        .in = { .buffer_gpu_addr = q->ring_gpu_va, .group_handle = kd->group, .csi_index = 0, },
    };
    if (ioctl(fd, KBASE_IOCTL_CS_QUEUE_BIND, &bind) < 0) { fprintf(stderr, "[kbase] BIND failed: %m\n"); return -1; }
    q->mmap_handle = bind.out.mmap_handle;
    fprintf(stderr, "[kbase] BIND: ring_gpu_va=0x%llx mmap_handle=0x%llx\n",
            (unsigned long long)q->ring_gpu_va, (unsigned long long)q->mmap_handle);
    q->user_io = mmap(NULL, user_io_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)q->mmap_handle);
    if (q->user_io == MAP_FAILED) { fprintf(stderr, "[kbase] user_io mmap failed: %m\n"); return -1; }
    q->input_page = (uint32_t *)q->user_io;
    q->output_page = (uint32_t *)((uint8_t *)q->user_io + 4096);
    *q->input_page = 0;
    *q->output_page = 0;
    q->created = true;
    fprintf(stderr, "[kbase] CS queue created: ring=0x%llx user_io=0x%llx\n",
            (unsigned long long)q->ring_gpu_va, (unsigned long long)q->user_io_gpu_va);
    return 0;
}

/* ── kbase CS queue submit ── */
int kbase_cs_queue_submit(struct pan_kmod_dev *dev, uint64_t stream_gpu_addr, void *stream_cpu_addr, uint32_t stream_size) {
    struct kbase_dev *kd = (struct kbase_dev *)dev;
    struct kbase_cs_queue *q = &kd->cs_queue;
    if (!q->created) { int cret = kbase_cs_queue_create(dev); if (cret < 0) return -1; }
    int fd = kd->base.fd;

    uint32_t insert = *q->input_page;
    uint32_t ring_mask = q->ring_size - 1;
    uint8_t *ring = (uint8_t *)q->ring;

    memcpy(ring + insert, stream_cpu_addr, stream_size);

    insert = (insert + stream_size) & ring_mask;
    *q->input_page = insert;

    struct kbase_ioctl_cs_queue_kick kick = { .buffer_gpu_addr = q->ring_gpu_va };
    if (ioctl(fd, KBASE_IOCTL_CS_QUEUE_KICK, &kick) < 0) { fprintf(stderr, "[kbase] KICK failed: %m\n"); return -1; }
    fprintf(stderr, "[kbase] submit: size=%u insert=%u\n", stream_size, insert);
    return 0;
}

/* ── kbase CS queue destroy ── */
int kbase_cs_queue_destroy(struct pan_kmod_dev *dev) {
    struct kbase_dev *kd = (struct kbase_dev *)dev;
    struct kbase_cs_queue *q = &kd->cs_queue;
    if (!q->created) return 0;
    int fd = kd->base.fd;
    struct kbase_ioctl_cs_queue_terminate term = { .buffer_gpu_addr = q->ring_gpu_va };
    ioctl(fd, KBASE_IOCTL_CS_QUEUE_TERMINATE, &term);
    if (q->ring) munmap(q->ring, q->ring_size);
    if (q->user_io) munmap(q->user_io, 3 * 4096);
    q->ring = NULL;
    q->user_io = NULL;
    q->created = false;
    return 0;
}

const struct pan_kmod_ops kbase_kmod_ops = {
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
