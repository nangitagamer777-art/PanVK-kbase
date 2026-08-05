# PanVK-kbase Integration Guide

Step-by-step guide to patch Mesa 26.2.0-rc2 with kbase backend.

## Prerequisites
- Mesa 26.2.0-rc2 source tree
- Android NDK r27+
- Device with `/dev/mali0` (MediaTek Mali Valhall GPU)

## Files modified

### pan_kmod.c
- Add `kbase_dev_create()` (handshake, context, queue group via cmd 42)
- Add `kbase_query_gpu_info()` to read `gpu_id` and `shader_present` from kernel via ioctl cmd 3
- Add `kbase` entry to `drivers[]` table
- Fallback to `/dev/mali0` when `drmGetVersion()` fails
- Stubs: `pan_kmod_flush_bo_map_syncs`, `pan_kmod_queue_bo_map_sync`, `pan_kmod_flush_bo_map_syncs_locked`

### kbase_kmod.c
- All `pan_kmod_ops`: dev_create/destroy, bo_alloc/free, vm_create/destroy, bo_wait, etc.
- ioctls: VERSION_CHECK (52), SET_FLAGS (1), MEM_ALLOC (5), QUEUE_GROUP_CREATE_1_6 (42)
- GPU_EX (1<<4) mandatory for CSF buffers

### panvk_physical_device.c
- `create_kmod_dev()`: accepts `drm_device=NULL`, uses `/dev/mali0`
- `get_drm_device_ids()`: handles `drm_device=NULL`
- `get_device_sync_types()`: fallback when DRM syncobj fails
- Fixed `goto fail` logic in heaps/sync_types

### panvk_instance.c
- Fallback to `/dev/mali0` when no DRM devices found

### pan_model.c / pan_model.h
- Added Mali-G615 (variant 0 and 1)
- Fallback in `pan_get_model()` to ignore `gpu_variant`

### meson.build
- Added `kbase_kmod.c` to build

## Key discoveries
- `gpu_id` format: `(arch_major<<28) | (arch_minor<<24) | (prod_major<<16)` → G615 = 0xA8070000
- `shader_present` from KBASE_IOCTL_GET_GPUPROPS offset 20
- cmd 42 required for MediaTek r44 scheduler
- CS_INSERT must be set before kick; no END opcode needed
- libkbase_drm.so stubs DRM syncobj/prime functions via LD_PRELOAD

## Result
- vk_info: Mali-G615 MC6, Vulkan 1.4.354, 154 extensions
- vkEnumeratePhysicalDevices returns count=1
