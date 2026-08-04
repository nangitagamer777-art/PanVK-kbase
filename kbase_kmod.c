/*
 * Copyright © 2024 Noin Haxel
 * SPDX-License-Identifier: MIT
 *
 * kbase_kmod.c - PanVK backend for Mali kbase driver (/dev/mali0)
 *
 * Replaces panthor_kmod.c for MediaTek devices with mali_kbase
 * instead of the open-source Panthor DRM driver.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include "util/macros.h"
#include "util/u_memory.h"
#include "pan_kmod_backend.h"
#include "pan_props.h"

extern const struct pan_kmod_ops kbase_kmod_ops;

/* ── kbase ioctl defines ── */
#define KBASE_IOCTL_TYPE  0x80
#define BASE_MEM_PROT_CPU_RD  (1U << 0)
#define BASE_MEM_PROT_CPU_WR  (1U << 1)
#define BASE_MEM_PROT_GPU_RD  (1U << 2)
#define BASE_MEM_PROT_GPU_WR  (1U << 3)
#define BASE_MEM_PROT_GPU_EX  (1U << 4)

struct kbase_ioctl_version_check { __u16 major; __u16 minor; };
#define KBASE_IOCTL_VERSION_CHECK _IOWR(0x80, 52, struct kbase_ioctl_version_check)

struct kbase_ioctl_set_flags { __u32 create_flags; };
#define KBASE_IOCTL_SET_FLAGS _IOW(0x80, 1, struct kbase_ioctl_set_flags)

struct kbase_ioctl_get_context_id { __u32 id; };
#define KBASE_IOCTL_GET_CONTEXT_ID _IOR(0x80, 17, struct kbase_ioctl_get_context_id)

union kbase_ioctl_mem_alloc {
    struct { __u64 va_pages; __u64 commit_pages; __u64 extension; __u64 flags; } in;
    struct { __u64 flags; __u64 gpu_va; } out;
};
#define KBASE_IOCTL_MEM_ALLOC _IOWR(0x80, 5, union kbase_ioctl_mem_alloc)

union kbase_ioctl_cs_queue_group_create_1_6 {
    struct { __u64 tiler_mask; __u64 fragment_mask; __u64 compute_mask;
             __u8 cs_min; __u8 priority; __u8 tiler_max; __u8 fragment_max;
             __u8 compute_max; __u8 padding[3]; } in;
    struct { __u8 group_handle; __u8 padding[3]; __u32 group_uid; } out;
};
#define KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_1_6 _IOWR(0x80, 42, union kbase_ioctl_cs_queue_group_create_1_6)

/* ── kbase device ── */
struct kbase_dev {
    struct pan_kmod_dev base;
    uint8_t group_handle;
    uint32_t context_id;
};

/* ── kbase buffer object ── */
struct kbase_bo {
    struct pan_kmod_bo base;
    uint64_t gpu_va;
    int mmap_fd;
};

/* ── kbase VM ── */
struct kbase_vm {
    struct pan_kmod_vm base;
};

/* ================================================================
 * Device creation / destruction
 * ================================================================ */
static struct pan_kmod_dev *
kbase_dev_create(int fd, uint32_t flags,
                 const struct pan_kmod_driver *drv_info,
                 const struct pan_kmod_allocator *allocator)
{
    /* fd is the /dev/mali0 fd passed from pan_kmod_dev_create */
    struct kbase_dev *kdev = pan_kmod_alloc(allocator, sizeof(*kdev));
    if (!kdev) return NULL;

    /* Handshake */
    struct kbase_ioctl_version_check ver = { .major = 11, .minor = 11 };
    if (ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver) < 0) {
        fprintf(stderr, "[kbase] version check failed: %s\n", strerror(errno));
        pan_kmod_free(allocator, kdev);
        return NULL;
    }
    fprintf(stderr, "[kbase] handshake ok - kbase %d.%d\n", ver.major, ver.minor);

    /* Create context */
    struct kbase_ioctl_set_flags sf = { .create_flags = 0 };
    if (ioctl(fd, KBASE_IOCTL_SET_FLAGS, &sf) < 0) {
        fprintf(stderr, "[kbase] set_flags failed: %s\n", strerror(errno));
        pan_kmod_free(allocator, kdev);
        return NULL;
    }

    /* Get context ID */
    struct kbase_ioctl_get_context_id ctx = {0};
    if (ioctl(fd, KBASE_IOCTL_GET_CONTEXT_ID, &ctx) == 0)
        kdev->context_id = ctx.id;

    /* Create queue group (cmd 42 for r44) */
    union kbase_ioctl_cs_queue_group_create_1_6 grp = {0};
    grp.in.tiler_mask = ~0ULL;
    grp.in.fragment_mask = ~0ULL;
    grp.in.compute_mask = ~0ULL;
    grp.in.cs_min = 1;
    grp.in.tiler_max = 8;
    grp.in.fragment_max = 8;
    grp.in.compute_max = 8;

    if (ioctl(fd, KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_1_6, &grp) < 0) {
        fprintf(stderr, "[kbase] group create failed: %s\n", strerror(errno));
        pan_kmod_free(allocator, kdev);
        return NULL;
    }
    kdev->group_handle = grp.out.group_handle;
    fprintf(stderr, "[kbase] device ready, group=%u, ctx=%u\n",
            kdev->group_handle, kdev->context_id);

    pan_kmod_dev_init(&kdev->base, fd, flags, drv_info, &kbase_kmod_ops, allocator);

    /* Fill device properties */
    struct pan_kmod_dev_props *props = &kdev->base.props;
    props->gpu_id = 0x7002; /* Mali-G615 */
    props->shader_present = 0x3; /* 2 cores */
    props->pgsize_bitmap = 0x1; /* 4K pages */
    props->timestamp_frequency = 1000000;
    props->gpu_can_query_timestamp = false;
    props->supported_bo_flags = 0;

    return &kdev->base;
}

static void
kbase_dev_destroy(struct pan_kmod_dev *dev)
{
    struct kbase_dev *kdev = (struct kbase_dev *)dev;
    close(dev->fd);
    pan_kmod_dev_cleanup(dev);
    pan_kmod_free(dev->allocator, kdev);
}

/* ================================================================
 * Buffer object allocation
 * ================================================================ */
static struct pan_kmod_bo *
kbase_bo_alloc(struct pan_kmod_dev *dev, struct pan_kmod_vm *exclusive_vm,
               uint64_t size, uint32_t flags)
{
    struct kbase_bo *bo = pan_kmod_dev_alloc(dev, sizeof(*bo));
    if (!bo) return NULL;

    union kbase_ioctl_mem_alloc mem = {0};
    mem.in.va_pages = (size + 4095) / 4096;
    mem.in.commit_pages = mem.in.va_pages;
    mem.in.flags = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR |
                   BASE_MEM_PROT_GPU_RD;
    
    if (flags & PAN_KMOD_BO_FLAG_EXECUTABLE)
        mem.in.flags |= BASE_MEM_PROT_GPU_EX;
    else
        mem.in.flags |= BASE_MEM_PROT_GPU_WR;

    if (ioctl(dev->fd, KBASE_IOCTL_MEM_ALLOC, &mem) < 0) {
        pan_kmod_dev_free(dev, bo);
        return NULL;
    }

    bo->gpu_va = mem.out.gpu_va;
    bo->mmap_fd = dev->fd;

    pan_kmod_bo_init(&bo->base, dev, exclusive_vm, size,
                     flags & ~PAN_KMOD_BO_FLAG_EXECUTABLE,
                     (uint32_t)bo->gpu_va);

    return &bo->base;
}

static void
kbase_bo_free(struct pan_kmod_bo *bo)
{
    struct kbase_bo *kbo = (struct kbase_bo *)bo;
    pan_kmod_bo_cleanup(bo);
    pan_kmod_dev_free(bo->dev, kbo);
}

static off_t
kbase_bo_get_mmap_offset(struct pan_kmod_bo *bo)
{
    struct kbase_bo *kbo = (struct kbase_bo *)bo;
    return (off_t)kbo->gpu_va;
}

static bool
kbase_bo_wait(struct pan_kmod_bo *bo, int64_t timeout_ns, bool for_read_only)
{
    /* kbase doesn't support sync wait at this level */
    return true;
}

static void
kbase_bo_set_label(struct pan_kmod_dev *dev, struct pan_kmod_bo *bo,
                   const char *label)
{
    /* no-op for kbase */
}

/* ================================================================
 * VM operations (minimal - kbase doesn't have MMU per context)
 * ================================================================ */
static struct pan_kmod_vm *
kbase_vm_create(struct pan_kmod_dev *dev, uint32_t flags,
                uint64_t va_start, uint64_t va_range)
{
    struct kbase_vm *vm = pan_kmod_dev_alloc(dev, sizeof(*vm));
    if (!vm) return NULL;
    pan_kmod_vm_init(&vm->base, dev, 0, flags);
    return &vm->base;
}

static void
kbase_vm_destroy(struct pan_kmod_vm *vm)
{
    struct kbase_vm *kvm = (struct kbase_vm *)vm;
    pan_kmod_vm_cleanup(vm);
    pan_kmod_dev_free(vm->dev, kvm);
}

static int
kbase_vm_bind(struct pan_kmod_vm *vm, enum pan_kmod_vm_op_mode mode,
              struct pan_kmod_vm_op *ops, uint32_t op_count)
{
    /* kbase doesn't need explicit VM bind */
    return 0;
}

/* ================================================================
 * Optional operations (stubs)
 * ================================================================ */
static struct pan_kmod_bo *
kbase_bo_import(struct pan_kmod_dev *dev, uint32_t handle, uint64_t size)
{
    return NULL;
}

static int
kbase_flush_bo_map_syncs(struct pan_kmod_dev *dev)
{
    return 0;
}

static uint64_t
kbase_query_timestamp(const struct pan_kmod_dev *dev)
{
    return 0;
}

/* ================================================================
 * Ops table
 * ================================================================ */
const struct pan_kmod_ops kbase_kmod_ops = {
    .dev_create = kbase_dev_create,
    .dev_destroy = kbase_dev_destroy,
    .bo_alloc = kbase_bo_alloc,
    .bo_free = kbase_bo_free,
    .bo_import = kbase_bo_import,
    .bo_get_mmap_offset = kbase_bo_get_mmap_offset,
    .bo_wait = kbase_bo_wait,
    .flush_bo_map_syncs = kbase_flush_bo_map_syncs,
    .vm_create = kbase_vm_create,
    .vm_destroy = kbase_vm_destroy,
    .vm_bind = kbase_vm_bind,
    .query_timestamp = kbase_query_timestamp,
    .bo_set_label = kbase_bo_set_label,
};
