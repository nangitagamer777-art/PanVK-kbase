# PanVK-kbase Integration Guide

Step-by-step guide to patch Mesa 26.2.0-rc2 with the kbase backend.

## Status: vkCreateDevice SUCCESS

`vkCreateDevice` returns `VK_SUCCESS` on the tested Mali-G615 MC6.

The CSF queue lifecycle is working:

    REGISTER (36) -> BIND (39) -> KICK (37)

The tested configuration uses three subqueues.

## Prerequisites

- Mesa 26.2.0-rc2 source tree
- Android NDK
- Android AArch64 cross-compilation environment
- Android device exposing `/dev/mali0`
- Mali Valhall GPU
- Proprietary `mali_kbase` kernel driver

## Files modified

### pan_kmod.c

Implements the kbase device backend.

Important changes:

- `kbase_dev_create()`
- `/dev/mali0` fallback
- kbase context creation
- GPU property querying
- GPU ID detection
- `shader_present` detection
- CS queue creation
- CS queue registration
- CS queue binding
- `user_io` mmap
- CS queue submission
- queue KICK handling
- kbase entry in the driver table

### pan_kmod.h

Contains declarations required by the kbase queue implementation.

Important declarations:

- `kbase_cs_queue_create`
- `kbase_cs_queue_submit`

Also contains the required forward declarations for the kbase queue
structures.

### kbase_kmod.c

Provides the kbase implementation of the `pan_kmod_ops` interface.

Implemented areas include:

- device creation/destruction
- BO allocation/free
- VM creation/destruction
- memory operations
- kbase ioctl handling
- queue group creation

Important kbase ioctls include:

- VERSION_CHECK (52)
- SET_FLAGS (1)
- MEM_ALLOC (5)
- QUEUE_GROUP_CREATE_1_6 (42)

### panthor_kmod.c

Contains compatibility handling for Panthor-specific structures that are
still referenced by existing PanVK code.

The fake CSIF/scoreboard handling exists to satisfy the existing PanVK
code path while actual queue submission is handled by kbase.

### panvk_vX_gpu_queue.c

The original Panthor submission path was replaced with the kbase CS queue
path.

Important changes:

- Panthor submission replaced by `kbase_cs_queue_submit`
- `pandecode_next_frame` adapted for the kbase path
- queue initialization adapted
- kbase-compatible queue/group handling
- `init_tiler` adapted
- `create_group` adapted
- `init_render_desc_ringbuf` adapted
- kbase-compatible sync handling
- unnecessary Panthor queue operations bypassed

### panvk_vX_device.c

Device initialization changes for kbase.

Important changes:

- global priority handling adapted because the tested kbase driver does
  not expose the same priority interface
- MEDIUM priority handling fixed
- utrace initialization paths bypassed where unsupported
- Perfetto utrace initialization bypassed where unsupported

### panvk_priv_bo.c

BO creation was adapted for kbase memory.

Important changes:

- `PAN_KMOD_BO_FLAG_NO_MMAP` handling adjusted
- mmap performed through the kbase backend
- `MAP_FAILED` is checked before accessing the mapping
- host memory initialization occurs only after a valid mapping exists

Important bug fixed:

Previously, `memset()` could execute immediately after `mmap()` before
checking whether the result was `MAP_FAILED`.

The corrected order is:

    mmap()
    check MAP_FAILED
    memset()

This prevents an invalid mapping from being accessed.

### panvk_mempool.h

Contains memory-pool compatibility macros required by the kbase BO path.

### panvk_physical_device.c

Physical-device initialization was modified to tolerate the absence of a
normal DRM device.

Important changes:

- `drm_device == NULL` is accepted by the kbase path
- `/dev/mali0` is used as the actual GPU backend
- DRM device enumeration failure no longer prevents kbase initialization
- device sync handling has a kbase-compatible fallback

### panvk_instance.c

Instance initialization contains a fallback to `/dev/mali0` when normal
DRM device enumeration does not produce a usable Panthor device.

### pan_model.c

Adds Mali-G615 model handling.

The tested GPU ID is:

    0xA8070000

The model lookup also handles the GPU variant used by the tested device.

### pan_model.h

Contains the hardware ID definitions and model declarations required by
the Mali-G615 support.

### meson.build

Integrates the kbase backend source into the Mesa PanVK build.

## Kbase CS queue flow

The working queue flow is:

    MEM_ALLOC
        |
        v
    CS_QUEUE_REGISTER
        |
        v
    CS_QUEUE_BIND
        |
        v
    user_io mmap
        |
        v
    write command stream
        |
        v
    update INSERT
        |
        v
    CS_QUEUE_KICK

The tested kbase r44 scheduler requires:

    QUEUE_GROUP_CREATE_1_6 (42)

## GPU identification

The GPU ID is decoded using:

    (arch_major << 28) |
    (arch_minor << 24) |
    (prod_major << 16)

For the Mali-G615 used during development:

    0xA8070000

The shader-present mask is obtained from the GPU properties returned by
the kbase driver.

## User IO mapping

The kbase queue `user_io` mapping requires:

    BASEP_QUEUE_NR_MMAP_USER_PAGES = 3

`CS_QUEUE_BIND` supplies the `mmap_handle` used for this mapping.

The `mmap_handle` is a mapping handle and must not be confused with the
GPU virtual address.

## Ring buffer

The ring buffer uses its GPU virtual address as the mmap offset in the
tested kbase implementation.

## Queue submission

The command stream is copied into the ring buffer.

The INSERT position is updated.

Then:

    CS_QUEUE_KICK (37)

is issued.

The tested queue path does not require an END opcode.

## Panthor to kbase mapping

| Panthor | kbase |
|---------|-------|
| `DRM_IOCTL_PANTHOR_GROUP_SUBMIT` | `CS_QUEUE_KICK` |
| `drm_panthor_queue_submit` | `kbase_cs_queue_submit` |
| Panthor BO allocation | kbase `MEM_ALLOC` |
| Panthor queue registration | `CS_QUEUE_REGISTER` |
| Panthor queue binding | `CS_QUEUE_BIND` |
| Panthor user_io | kbase `mmap_handle` |
| Panthor GPU submission | kbase queue KICK |
| Panthor DRM device | `/dev/mali0` |
| DRM syncobj interface | compatibility symbols |

The important distinction is that the kbase backend performs the actual
GPU execution.

The DRM compatibility layer does not replace the kbase backend.

## DRM compatibility layer

The standalone DRM compatibility source is:

    ~/Panvk_Kmod/src/libkbase_drm.c

It is compiled into:

    libkbase_drm.so

The purpose of this library is to provide DRM symbols that PanVK/Mesa
still expects to resolve.

The current source contains compatibility implementations for functions
including:

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

The actual GPU work remains in the kbase backend.

## Testing without libkbase_drm.so

The DRM compatibility layer was tested independently.

Running:

    cd /data/local/tmp
    LD_LIBRARY_PATH=/data/local/tmp ./vk_final_test

reaches kbase initialization:

    [kbase] trying fallback to /dev/mali0...
    [kbase] drmGetVersion failed on /dev/mali0, trying kbase backend anyway
    [kbase] device ready, fd=4 ...
    [kbase] after get_drm_device_ids: result=0

but then produces:

    Segmentation fault

Therefore the current working runtime still requires
`libkbase_drm.so`.

This does not mean DRM is being used for actual GPU submission.
It means some later PanVK/Mesa code still depends on the DRM-facing
interface or symbols.

## Scudo compatibility layer

The standalone Scudo workaround is:

    ~/Panvk_Kmod/src/libkbase_scudo_fix.c

It intercepts `mprotect()` and ignores `EPERM`.

The resulting library is:

    libkbase_scudo_fix.so

It was tested and found unnecessary for the current successful
`vkCreateDevice` path.

Current working runtime:

    LD_LIBRARY_PATH=/data/local/tmp \
    LD_PRELOAD=libkbase_drm.so \
    ./vk_final_test

No Scudo fix is required for this test.

## Build configuration

Configure Mesa:

    meson setup build --wipe --cross-file=android-aarch64-35 android-stub=true -Dplatforms=panfrost -Dgallium-drivers=panfrost -Dllvm=false -Dmesa-clc=system -Dprecompiled-compiler=system -Degl=true -Dzstd=false

Build:

    ninja -C build src/panfrost/vulkan/libvulkan_panfrost.so

Result:

    build/src/panfrost/vulkan/libvulkan_panfrost.so

## Runtime test

Current working configuration:

    cd /data/local/tmp
    LD_LIBRARY_PATH=/data/local/tmp LD_PRELOAD=libkbase_drm.so ./vk_final_test

Expected:

    === TEST COMPLETED ===
    vkCreateDevice returned: 0
    STATUS: SUCCESS (Device Created)

## Test hardware

- Poco X6 Pro
- MediaTek Dimensity 8300
- Mali-G615 MC6
- Valhall
- CSF firmware
- Proprietary kbase kernel driver
- `/dev/mali0`

## Related project

Panvk_Kmod — standalone kbase shim and GPU execution tests.

## License

MIT

## Full Mesa Setup (Tested)

How to reproduce the build environment:

    cd ~/mesa-26.2.0-rc2
    meson setup build --wipe \
      --cross-file=android-aarch64-35 \
      android-stub=true \
      -Dplatforms=panfrost \
      -Dgallium-drivers=panfrost \
      -Dllvm=false \
      -Dmesa-clc=system \
      -Dprecompiled-compiler=system \
      -Degl=true \
      -Dzstd=false

    ninja -C build src/panfrost/vulkan/libvulkan_panfrost.so
    cp build/src/panfrost/vulkan/libvulkan_panfrost.so /sdcard/libvulkan_panfrost_kbase.so

Wrappers:

    cd ~/Panvk_Kmod
    clang -shared -fPIC -o build/libkbase_drm.so src/libkbase_drm.c -ldl
    cp build/libkbase_drm.so /sdcard/libkbase_drm.so

Runtime:

    cp /sdcard/libvulkan_panfrost_kbase.so /data/local/tmp/libvulkan_panfrost.so
    cp /sdcard/libkbase_drm.so /data/local/tmp/libkbase_drm.so
    cd /data/local/tmp
    LD_LIBRARY_PATH=/data/local/tmp LD_PRELOAD=libkbase_drm.so ./vk_final_test
    LD_LIBRARY_PATH=/data/local/tmp LD_PRELOAD=libkbase_drm.so ./vk_destroy_test

## Environment Paths

Proot Debian (Mesa build):
    ~/mesa-26.2.0-rc2/          # Mesa source tree
    ~/mesa-26.2.0-rc2/build/    # Ninja build
    ~/android-ndk/              # NDK r27

Termux (wrappers and tests):
    ~/PanVK-kbase/              # patched Mesa repo
    ~/Panvk_Kmod/               # standalone kbase shim
    ~/mali-kbase-src/           # kbase kernel source (reference)

Android (runtime):
    /sdcard/                    # file transfer
    /data/local/tmp/            # test binaries and .so

## Debug Tips

Get crash backtrace:
    logcat -b crash -d

Trace kbase ioctls:
    LD_LIBRARY_PATH=/data/local/tmp LD_PRELOAD=libkbase_drm.so \
      strace -e trace=ioctl ./vk_final_test 2>&1 | grep -E "0x80|ioctl"

Show exported symbols:
    llvm-nm -n build/src/panfrost/vulkan/libvulkan_panfrost.so | grep kbase

## Known Working State

- CSIF real: regs=17 scoreboards=1 features=0x10
- user_io: 3 pages (BASEP_QUEUE_NR_MMAP_USER_PAGES)
- ring: GPU VA as mmap offset
- sparse_dummy: 2MB BO with PAN_KMOD_BO_FLAG_NO_MMAP
- syncobjs: poll output_page CS_ACTIVE until idle
- libkbase_scudo_fix.so: NOT required
- libkbase_drm.so: REQUIRED
