# PanVK-kbase — Mesa with kbase backend for ARM Mali Valhall GPUs

Patched Mesa 26.2.0-rc2 with a third Vulkan backend that talks directly
to `/dev/mali0` via the kbase ioctl interface.

## What this is

This repo contains the patched source files needed to compile Mesa's PanVK
driver with a custom backend for devices running the proprietary `mali_kbase`
kernel driver instead of the open-source Panthor DRM driver.

**Status:** `vkCreateDevice` returns `VK_SUCCESS` on Mali-G615 MC6.
CSF queue lifecycle fully working: REGISTER -> BIND -> KICK with 3 subqueues.

## Files

| File | Purpose |
|------|---------|
| `pan_kmod.c` | Backend implementing `pan_kmod_ops` via `/dev/mali0` ioctls |
| `pan_kmod.h` | Declarations for `kbase_cs_queue_create` and `kbase_cs_queue_submit` |
| `kbase_kmod.c` | kbase backend implementing the `pan_kmod_ops` interface |
| `panthor_kmod.c` | csif real and functional integration of kbase |
| `panvk_vX_gpu_queue.c` | GPU queue: kbase submit, pandecode adapted for kbase |
| `panvk_vX_device.c` | Device init: utrace handling and priority compatibility |
| `panvk_priv_bo.c` | BO creation: NO_MMAP handling and safe mmap initialization |
| `panvk_mempool.h` | Memory pool macros |
| `panvk_physical_device.c` | Physical device: accepts `drm_device=NULL`, kbase fallback |
| `panvk_instance.c` | Instance initialization and `/dev/mali0` fallback |
| `pan_model.c` | Mali-G615 model support and fallback |
| `pan_model.h` | Hardware `gpu_id` handling |
| `meson.build` | Build integration for the kbase backend |
| `I want to clarify that all mesa src files are indispensable and necessary` |

The DRM compatibility layer is maintained separately in `Panvk_Kmod`:

| File | Purpose |
|------|---------|
| `src/libkbase_drm.c` | DRM symbol compatibility layer |
| `src/libkbase_scudo_fix.c` | Optional Scudo `mprotect()` workaround |

## How to compile

Place the modified files in a Mesa 26.2.0-rc2 source tree.

Configure the Android build with:

    meson setup build --wipe --cross-file=android-aarch64-35 android-stub=true -Dplatforms=panfrost -Dgallium-drivers=panfrost -Dllvm=false -Dmesa-clc=system -Dprecompiled-compiler=system -Degl=true -Dzstd=false

Build the PanVK Vulkan driver:

    ninja -C build src/panfrost/vulkan/libvulkan_panfrost.so

Output:

    build/src/panfrost/vulkan/libvulkan_panfrost.so

## Requirements

- Android device with `/dev/mali0`
- Mali Valhall GPU
- Proprietary `mali_kbase` kernel driver
- Mesa 26.2.0-rc2 source tree
- Android AArch64 cross-compilation environment
- Android NDK

## Runtime test

Copy the built Vulkan library and the required runtime components to
`/data/local/tmp`.

Current working configuration:

    cd /data/local/tmp
    LD_LIBRARY_PATH=/data/local/tmp LD_PRELOAD=libkbase_drm.so ./vk_final_test

Expected:

    === TEST COMPLETED ===
    vkCreateDevice returned: 0
    STATUS: SUCCESS (Device Created)

`libkbase_scudo_fix.so` is currently not required for the successful
`vkCreateDevice` path.

## Test device

- Poco X6 Pro
- MediaTek Dimensity 8300
- Mali-G615 MC6
- Valhall
- CSF firmware
- Proprietary kbase kernel driver
- `/dev/mali0`

## Key discoveries

- `gpu_id` format:
  `(arch_major<<28) | (arch_minor<<24) | (prod_major<<16)`
- Mali-G615 is detected as `0xA8070000`
- `shader_present` is obtained from `KBASE_IOCTL_GET_GPUPROPS`
- `GPU_EX` (`1<<4`) is mandatory for CSF queue buffers
- `BASEP_QUEUE_NR_MMAP_USER_PAGES = 3` is required for the `user_io` mapping
- `CS_QUEUE_BIND` returns the `mmap_handle` used for the `user_io` mapping
- Ring buffer uses the GPU VA directly as the mmap offset
- Command 42 (`QUEUE_GROUP_CREATE_1_6`) is required for the r44 scheduler
- CS queue lifecycle is REGISTER -> BIND -> KICK
- No END opcode is required; the tested GPU runs until INSERT == EXTRACT
- PanVK can initialize the actual GPU through kbase without Panthor

## Panthor to kbase mapping

| Panthor | kbase |
|---------|-------|
| `DRM_IOCTL_PANTHOR_GROUP_SUBMIT` | `CS_QUEUE_KICK` |
| `drm_panthor_queue_submit` | `kbase_cs_queue_submit` |
| Panthor BO allocation | kbase `MEM_ALLOC` |
| Panthor queue registration | `CS_QUEUE_REGISTER` |
| Panthor queue binding | `CS_QUEUE_BIND` |
| Panthor user_io | kbase `mmap_handle` |
| Panthor GPU device | `/dev/mali0` |
| DRM syncobj interface | compatibility Layer |

## DRM compatibility layer

`libkbase_drm.so` does not perform the actual GPU execution.

The real GPU execution path is handled by the kbase backend through
`/dev/mali0`.

The DRM library exists because parts of PanVK/Mesa still expect DRM
symbols or DRM-related interfaces during initialization and later
execution paths.

The current compatibility layer provides symbols including:

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

The syncobj operations required by the current execution path are
implemented using real kbase-backed synchronization.

The remaining DRM interfaces that have no direct equivalent in kbase
are kept as compatibility stubs. These include sync-file
import/export, syncobj transfer, DRM capability queries, PRIME/DMA-BUF
operations, and GEM buffer-handle operations.

These stubs are intentional and are not part of the actual GPU
execution path.

## Testing without libkbase_drm.so

Running without the DRM compatibility layer currently reaches kbase
device initialization successfully:

    [kbase] trying fallback to /dev/mali0...
    [kbase] drmGetVersion failed on /dev/mali0, trying kbase backend anyway
    [kbase] device ready, fd=4 ...
    [kbase] after get_drm_device_ids: result=0

but then crashes with:

    Segmentation fault

Therefore `libkbase_drm.so` is currently required by the working runtime
configuration even

## Kbase Backend Status

| Component | Status | Verified Value / Details |
|---|---|---|
| CSIF | REAL | `regs=17`, `scoreboard=1`, `features=0x10` |
| Sparse dummy | REAL | `0xb4000072bac45010` (2 MB BO) |
| BO allocation | REAL | `mmap` + `memset` + CPU RW test OK |
| VM_BIND | REAL | `AUTO_VA -> 0x4fffff000` |
| Syncobj | REAL | `host=0x730b0f5380`, `dev=0x4fffe4380` + RW test |
| `u_printf_init` | REAL | DONE, `ctx=0xb4000071dac44ae8` |
| `meta_init` | REAL | Real initialization completed |
| CS builder | REAL | `INIT → END → VALID → FINI → REQ_RESOURCE → CONTEXT_ALLOC → CONTEXT_WRITE` |
| Submit | REAL | 3 real submits, `ret=0`, insert `32 → 64 → 80` |
| `drmSyncobjWait` | REAL | `BEFORE → AFTER`, `ret=0` (polls `output_page`) |
| `drmSyncobjReset` | REAL | Resets output/input state |
| `vkCreateDevice` | SUCCESS | `VK_SUCCESS` |
| `vkDestroyDevice` | OK | Completes without crash |
| CSIF naming | CLEAN | Uses `csif` instead of the old `fake_csif` label |

## DRM / Syncobj Compatibility Layer

| Function | Status | Implementation |
|---|---|---|
| `drmSyncobjCreate` | REAL | Real handle map |
| `drmSyncobjWait` | REAL | Polls `output_page[CS_ACTIVE]` |
| `drmSyncobjReset` | REAL | Resets output/input state |
| `drmSyncobjDestroy` | REAL | Removes handle from the map |
| `drmSyncobjTimelineWait` | REAL | Uses the real wait implementation |
| `drmGetDevices2` | WRAPPER | Calls the real libdrm implementation |
| `drmIoctl` | WRAPPER | Calls the real libdrm implementation |
| `drmGetCap` | ! STUB | Works as a compatibility stub; no equivalent concept is needed/provided by kbase |
| `drmPrime*` | ! STUB | Works as compatibility stubs; kbase has no DMA-BUF equivalent in this backend |
| `drmCloseBufferHandle` | ! STUB | Works as a compatibility stub; kbase has no GEM handle equivalent |

> **Note:** The entries are intentionally kept as stubs. They are not current blockers or broken implementations; the corresponding DRM concepts (`DMA-BUF`, GEM handles, DRM capabilities, etc.) have no direct equivalent in the kbase backend and are not required by the current PanVK-kbase execution path.

## Credits

**Noin Haxel**

[@nangitagamer777-art](https://github.com/nangitagamer777-art)

## Reference

* 0x36/Pixel_GPU_Exploit — ioctl command numbers
 * Google Project Zero, CVE-2023-4211 — kbase handshake pattern
 * Man Yue Mo, "Bypassing MTE with CVE-2025-0072" — CSF queue flow analysis
 * android.googlesource.com/kernel/google-modules/gpu — kbase kernel source

## Related

[Panvk_Kmod](https://github.com/nangitagamer777-art/Panvk_Kmod) —
Repository of the driver already compiled for use.

## License

MIT
