# PanVK-kbase Integration Guide

Step-by-step guide to patch Mesa 26.2.0-rc2 with a kbase backend.

## Status: vkCreateDevice SUCCESS

CSF queue lifecycle fully working:

REGISTER (36) -> BIND (39) -> KICK (37) with 3 subqueues.

The GPU execution path is handled directly through the proprietary kbase
driver at `/dev/mali0`.

## Prerequisites

- Mesa 26.2.0-rc2 source tree
- Android NDK r27+
- Android cross-file `android-aarch64-35`
- Device with `/dev/mali0`
- Mali Valhall GPU
- Access to `/dev/mali0`

## Files modified

### pan_kmod.c

- `kbase_dev_create()`:
  - Opens `/dev/mali0`
  - Performs kbase handshake
  - Creates kbase context
  - Creates queue group using cmd 42
- `kbase_query_gpu_info()`:
  - Reads `gpu_id`
  - Reads `shader_present`
- `kbase_cs_queue_create()`:
  - Allocates ring memory using kbase MEM_ALLOC
  - Registers CS queue with cmd 36
  - Binds queue with cmd 39
  - Maps user_io pages
  - Handles the kbase `mmap_handle`
- `kbase_cs_queue_submit()`:
  - Copies command stream into ring
  - Updates INSERT
  - Sends CS_QUEUE_KICK cmd 37
- `kbase` entry in the backend driver table
- Fallback to `/dev/mali0` when `drmGetVersion()` fails

The `drmGetVersion()` failure is expected on the proprietary kbase device
because `/dev/mali0` is not a normal Panthor DRM device.

### pan_kmod.h

- Declarations for:
  - `kbase_cs_queue_create`
  - `kbase_cs_queue_submit`
- Forward declarations:
  - `struct kbase_dev`
  - `struct kbase_cs_queue`

### kbase_kmod.c

Implements the required `pan_kmod_ops` for the kbase backend.

Includes handling for:

- device creation/destruction
- BO allocation/free
- VM creation/destruction
- kbase ioctl communication
- VERSION_CHECK (52)
- SET_FLAGS (1)
- MEM_ALLOC (5)
- QUEUE_GROUP_CREATE_1_6 (42)

### panthor_kmod.c

Contains the remaining fake_csif/scoreboard compatibility logic required by
parts of PanVK that were originally written around the Panthor CSF path.

It does not perform the actual GPU submission.

### panvk_vX_gpu_queue.c

Panthor submission was replaced with the kbase submission path:

```text
Panthor DRM submit
       |
       v
kbase_cs_queue_submit()
       |
       v
CS_QUEUE_KICK
       |
       v
/dev/mali0

Other adaptations include:

- "pandecode_next_frame" adapted for the kbase path
- decode context checks
- queue initialization adaptations
- "init_tiler" adaptation
- "create_group" adaptation
- "init_render_desc_ringbuf" adaptation
- "syncobj_handle = 0" for the kbase path

panvk_vX_device.c

- "check_global_priority":
  - MEDIUM priority fixed because kbase does not expose the same DRM priority
    mechanism
- "utrace_context_init" disabled/adapted
- "utrace_perfetto_init" disabled/adapted

panvk_priv_bo.c

Important memory handling fixes:

- Correct handling of "PAN_KMOD_BO_FLAG_NO_MMAP"
- mmap result checked before using the returned pointer
- "memset" moved after the "MAP_FAILED" validation

This avoids writing through an invalid mmap result.

panvk_mempool.h

Memory pool macros adapted for kbase compatibility.

panvk_physical_device.c

- "create_kmod_dev()" accepts "drm_device=NULL"
- Uses "/dev/mali0" through the kbase backend
- "get_drm_device_ids()" handles the absence of a normal DRM device
- "get_device_sync_types()" has a kbase-compatible fallback

panvk_instance.c

When no usable DRM device is discovered, PanVK can fall back to the kbase
device at:

/dev/mali0

pan_model.c / pan_model.h

- Added Mali-G615 support
- Added G615 variants 0 and 1
- Fallback in "pan_get_model()" to avoid rejecting the detected GPU variant
- Corrected hardware "gpu_id" handling

Panthor -> kbase mapping

Original Panthor path| kbase path
DRM queue submission| "CS_QUEUE_KICK"
"drm_panthor_queue_submit"| "kbase_cs_queue_submit"
Panthor queue registration| "CS_QUEUE_REGISTER"
Panthor queue binding| "CS_QUEUE_BIND"
Panthor BO allocation| kbase "MEM_ALLOC"
Panthor user_io mapping| kbase "mmap_handle"
Panthor GPU submission| "/dev/mali0" kbase ioctl
DRM sync objects| "libkbase_drm.so" compatibility layer

The important distinction is that "libkbase_drm.so" is not replacing the kbase
backend. It only satisfies the remaining DRM/libdrm interface expected by
PanVK.

libkbase_drm.so

Source:

~/Panvk_Kmod/src/libkbase_drm.c

There is also a copy/reference under:

~/Panvk_Kmod/tools/libkbase_drm.c

The current runtime library is:

libkbase_drm.so

Its purpose is to provide the remaining DRM symbols that PanVK still reaches.

Examples include:

- "drmGetDevices2"
- "drmSyncobjCreate"
- "drmSyncobjDestroy"
- "drmSyncobjWait"
- "drmSyncobjTimelineWait"
- "drmSyncobjExportSyncFile"
- "drmSyncobjImportSyncFile"
- "drmSyncobjReset"
- "drmSyncobjTransfer"
- "drmGetCap"
- "drmIoctl"
- "drmCloseBufferHandle"
- "drmPrimeFDToHandle"
- "drmPrimeHandleToFD"

Some calls are forwarded to the real "libdrm"; others are minimal
compatibility implementations.

Why it is required

Without the shim:

[kbase] trying fallback to /dev/mali0...
[kbase] drmGetVersion failed on /dev/mali0, trying kbase backend anyway
[kbase] device ready, fd=4 ...
[kbase] after get_drm_device_ids: result=0
Segmentation fault

With the shim:

=== TEST COMPLETED ===
vkCreateDevice returned: 0
STATUS: SUCCESS (Device Created)

Therefore "libkbase_drm.so" is currently a required runtime component.

libkbase_scudo_fix.so

The Scudo fix is currently not required.

Source:

~/Panvk_Kmod/src/libkbase_scudo_fix.c

It intercepts "mprotect()" and ignores "EPERM", but testing showed that
"vkCreateDevice()" succeeds without loading this library.

Therefore the normal runtime should NOT include:

libkbase_scudo_fix.so

Only "libkbase_drm.so" is required by the current working configuration.

Build

From the Mesa source tree:

cd ~/PanVK-kbase

Configure:

meson setup build --wipe --cross-file=android-aarch64-35 android-stub=true -Dplatforms=panfrost -Dgallium-drivers=panfrost -Dllvm=false -Dmesa-clc=system -Dprecompiled-compiler=system -Degl=true -Dzstd=false

Build PanVK:

ninja -C build src/panfrost/vulkan/libvulkan_panfrost.so

Output:

build/src/panfrost/vulkan/libvulkan_panfrost.so

Runtime test

On the Android device:

cd /data/local/tmp
LD_LIBRARY_PATH=/data/local/tmp LD_PRELOAD=libkbase_drm.so ./vk_final_test

Do not include "libkbase_scudo_fix.so" in the normal test.

Expected result

=== TEST COMPLETED ===
vkCreateDevice returned: 0
STATUS: SUCCESS (Device Created)

Test device

- Poco X6 Pro
- MediaTek Dimensity 8300
- Mali-G615 MC6
- Valhall
- CSF firmware
- Kernel driver: "mali_kbase_mt6897_r44"

Key discoveries

1. G615 "gpu_id":

0xA8070000

2. "gpu_id" format:

(arch_major << 28) |
(arch_minor << 24) |
(prod_major << 16)

3. "shader_present" comes from "KBASE_IOCTL_GET_GPUPROPS".

4. "GPU_EX" ("1 << 4") is mandatory for the CSF queue buffers.

5. "BASEP_QUEUE_NR_MMAP_USER_PAGES = 3" is required for user_io mmap.

6. "CS_QUEUE_BIND" returns an "mmap_handle" used for user_io mapping.

7. The "mmap_handle" is not the GPU virtual address.

8. The ring buffer uses the GPU VA as the mmap offset where required by the
   kbase interface.

9. cmd 42 ("QUEUE_GROUP_CREATE_1_6") is required for the r44 scheduler.

10. No END opcode is required; the GPU runs until INSERT reaches EXTRACT.

11. PanVK can continue when "drmGetVersion()" fails on "/dev/mali0".

12. The remaining DRM symbols used by PanVK still need to be supplied.

13. "libkbase_drm.so" supplies those compatibility symbols.

14. Removing "libkbase_drm.so" currently causes a segmentation fault after
    "get_drm_device_ids()".

15. "libkbase_scudo_fix.so" is not required for "vkCreateDevice()" success.

Current architecture

                         PanVK
                           |
              +------------+------------+
              |                         |
              v                         v
       kbase backend              DRM compatibility
       pan_kmod.c                 libkbase_drm.so
              |                         |
              v                         v
        /dev/mali0                remaining DRM
              |                    symbols/calls
              v
         kbase ioctls
              |
              v
          Mali-G615
              |
              v
          CSF queues
       REGISTER/BIND/KICK

The kbase backend is the actual GPU backend.

"libkbase_drm.so" is only the compatibility layer required to keep PanVK's
remaining DRM-facing code paths satisfied.

Credits

Noin Haxel ("@nangitagamer777-art" (https://github.com/nangitagamer777-art))

DeepSeek AI, Claude AI

Related

"Panvk_Kmod" (https://github.com/nangitagamer777-art/Panvk_Kmod) — Standalone
kbase shim with GPU execution tests.

License

MIT
