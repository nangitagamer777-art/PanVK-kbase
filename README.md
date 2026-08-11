# PanVK-kbase — Mesa with kbase backend for ARM Mali Valhall GPUs

Patched Mesa 26.2.0-rc2 with a third Vulkan backend that talks directly
to `/dev/mali0` via the kbase ioctl interface.

## What this is

This repo contains the patched source files needed to compile Mesa's PanVK
driver with a custom backend for devices running the proprietary `mali_kbase`
kernel driver (MediaTek, Samsung, and other Android OEMs) instead of the
open-source Panthor DRM driver.

The actual GPU execution path is handled by the kbase backend and
`/dev/mali0`. The remaining DRM calls used by PanVK during initialization
are provided by `libkbase_drm.so`, a small compatibility shim that supplies
the DRM symbols PanVK still expects.

**Status:** `vkCreateDevice` returns `VK_SUCCESS` on Mali-G615 MC6.
CSF queue lifecycle fully working: REGISTER -> BIND -> KICK with 3 subqueues.

## Files

| File | Purpose |
|------|---------|
| `pan_kmod.c` | Backend implementing `pan_kmod_ops` via `/dev/mali0` ioctls |
| `pan_kmod.h` | Declarations for `kbase_cs_queue_create` and `kbase_cs_queue_submit` |
| `kbase_kmod.c` | kbase implementation of the remaining `pan_kmod_ops` |
| `panthor_kmod.c` | fake_csif scoreboard compatibility hack |
| `panvk_vX_gpu_queue.c` | GPU queue: kbase submit, pandecode adapted for kbase |
| `panvk_vX_device.c` | Device init: utrace stubs, MEDIUM priority fixed |
| `panvk_priv_bo.c` | BO creation: NO_MMAP handling and mmap validation |
| `panvk_mempool.h` | Memory pool macros |
| `panvk_physical_device.c` | Accepts `drm_device=NULL`, fallback to `/dev/mali0` |
| `panvk_instance.c` | Fallback to `/dev/mali0` when no DRM devices found |
| `pan_model.c` | Added Mali-G615 (variant 0 and 1) with fallback |
| `pan_model.h` | Hardware `gpu_id` format fix |
| `meson.build` | Build configuration with kbase backend |
| `libkbase_drm.so` | DRM compatibility shim for remaining PanVK DRM symbols |

## DRM compatibility shim

`libkbase_drm.so` is **still required**.

It does not perform the actual GPU execution. The kbase backend handles
GPU communication through `/dev/mali0`.

The shim exists because PanVK/Mesa still reaches several DRM/libdrm symbols
during initialization and device setup. These symbols must exist even though
the real GPU backend has been replaced with kbase.

The shim currently provides compatibility implementations/forwarding for
functions including:

- `drmGetDevices2`
- `drmSyncobjCreate`
- `drmSyncobjDestroy`
- `drmSyncobjWait`
- `drmSyncobjTimelineWait`
- `drmSyncobjExportSyncFile`
- `drmSyncobjImportSyncFile`
- `drmSyncobjReset`
- `drmSyncobjTransfer`
- `drmGetCap`
- `drmIoctl`
- `drmCloseBufferHandle`
- `drmPrimeFDToHandle`
- `drmPrimeHandleToFD`

Some functions are forwarded to the real `libdrm`, while others provide
minimal compatibility behavior because their DRM functionality is no longer
used for actual GPU execution.

### Why it is still needed

Testing without the shim:

```text
[kbase] device ready, fd=4 ...
[kbase] after get_drm_device_ids: result=0
Segmentation fault

Testing with "libkbase_drm.so":

=== TEST COMPLETED ===
vkCreateDevice returned: 0
STATUS: SUCCESS (Device Created)

Therefore the current runtime dependency is:

PanVK
  |
  +-- kbase backend --> /dev/mali0 --> GPU
  |
  +-- libkbase_drm.so --> remaining DRM/libdrm symbols

"libkbase_drm.so" should therefore be considered part of the current
PanVK-kbase runtime compatibility layer.

Scudo fix

"libkbase_scudo_fix.so" is not required for the current working setup.

The Scudo fix was tested as a possible workaround for "mprotect()" failures,
but removing it still resulted in:

vkCreateDevice returned: 0
STATUS: SUCCESS (Device Created)

Therefore it should not be included in the normal runtime command.

The standalone source remains available in the "Panvk_Kmod" project:

~/Panvk_Kmod/src/libkbase_scudo_fix.c

but it is currently not part of the required PanVK-kbase runtime.

How to compile

1. Place the patched files in a Mesa 26.2.0-rc2 source tree at the paths
   indicated in the integration guide.

2. Configure Mesa for Android:

meson setup build --wipe --cross-file=android-aarch64-35 android-stub=true -Dplatforms=panfrost -Dgallium-drivers=panfrost -Dllvm=false -Dmesa-clc=system -Dprecompiled-compiler=system -Degl=true -Dzstd=false

3. Build PanVK:

ninja -C build src/panfrost/vulkan/libvulkan_panfrost.so

4. Output:

build/src/panfrost/vulkan/libvulkan_panfrost.so

Requirements

- Android device with "/dev/mali0"
- MediaTek Dimensity with Mali Valhall GPU
- Shizuku or root access to the device node
- Mesa cross-compile environment
- Android NDK r27+

Runtime test

Copy the required libraries and test executable to "/data/local/tmp".

Then:

cd /data/local/tmp
LD_LIBRARY_PATH=/data/local/tmp LD_PRELOAD=libkbase_drm.so ./vk_final_test

The important part is that "libkbase_drm.so" is loaded.

"libkbase_scudo_fix.so" is not required.

Expected result

=== TEST COMPLETED ===
vkCreateDevice returned: 0
STATUS: SUCCESS (Device Created)

Test device

- Poco X6 Pro
- MediaTek Dimensity 8300
- Mali-G615 MC6
- Valhall GPU
- CSF firmware
- Kernel driver: "mali_kbase_mt6897_r44"

Key discoveries

- "gpu_id" format:
  "(arch_major<<28) | (arch_minor<<24) | (prod_major<<16)"
- G615 = "0xA8070000"
- "shader_present" obtained from "KBASE_IOCTL_GET_GPUPROPS"
- "GPU_EX" ("1<<4") is mandatory for CSF queue buffers
- "BASEP_QUEUE_NR_MMAP_USER_PAGES = 3" required for user_io mmap
- "CS_QUEUE_BIND" returns "mmap_handle" for user_io mapping, not GPU VA
- Ring buffer uses GPU VA directly as mmap offset
- cmd 42 ("QUEUE_GROUP_CREATE_1_6") required for the r44 scheduler
- No END opcode needed; GPU runs until INSERT == EXTRACT
- PanVK still requires several DRM/libdrm symbols during initialization
- "libkbase_drm.so" provides the required DRM compatibility layer
- "libkbase_scudo_fix.so" is not required

Panthor vs kbase architecture

The project does not use Panthor for actual GPU submission.

PanVK
  |
  +-- PanVK device/queue logic
  |
  +-- kbase pan_kmod backend
  |       |
  |       +-- kbase ioctls
  |       |
  |       +-- /dev/mali0
  |       |
  |       +-- CS_QUEUE_REGISTER
  |       +-- CS_QUEUE_BIND
  |       +-- CS_QUEUE_KICK
  |
  +-- libkbase_drm.so
          |
          +-- compatibility for remaining DRM symbols

Credits

Noin Haxel ("@nangitagamer777-art" (https://github.com/nangitagamer777-art))

DeepSeek AI, Claude AI

Related

"Panvk_Kmod" (https://github.com/nangitagamer777-art/Panvk_Kmod) — Standalone
kbase shim with GPU execution tests.

License

MIT
