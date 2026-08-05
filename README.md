# PanVK-kbase — Mesa with kbase backend for ARM Mali Valhall GPUs

Patched Mesa 26.2.0-rc2 with a third Vulkan backend that talks directly
to `/dev/mali0` via the kbase ioctl interface.

## What this is

This repo contains the patched source files needed to compile Mesa's PanVK
driver with a custom backend for devices running the proprietary `mali_kbase`
kernel driver (MediaTek, Samsung, and other Android OEMs) instead of the
open-source Panthor DRM driver.

**Status:** PanVK successfully detects Mali-G615 MC6 (`vkEnumeratePhysicalDevices` returns `count=1`).

## Files

| File | Purpose |
|------|---------|
| `kbase_kmod.c` | Backend implementing `pan_kmod_ops` via `/dev/mali0` ioctls |
| `pan_kmod.c` | Patched to detect kbase when DRM is unavailable |
| `panvk_physical_device.c` | Accepts `drm_device=NULL`, uses `/dev/mali0` |
| `panvk_instance.c` | Fallback to `/dev/mali0` when no DRM devices found |
| `pan_model.c` | Added Mali-G615 (variant 0 and 1) with fallback |
| `pan_model.h` | Hardware `gpu_id` format fix |
| `meson.build` | Build file with `kbase_kmod.c` added |

## How to compile

1. Place these files in a Mesa 26.2.0-rc2 source tree at the paths indicated
2. Follow standard Mesa cross-compile for Android with:
   `meson setup build --wipe --cross-file=android-aarch64-35 -Dvulkan-drivers=panfrost -Dgallium-drivers=panfrost -Dplatforms=android && ninja -C build`
3. Output: `build/src/panfrost/vulkan/libvulkan_panfrost.so`

## Requirements
- Android device with `/dev/mali0` (MediaTek Dimensity with Mali Valhall GPU)
- Shizuku or root to access the device node
- Mesa cross-compile environment

## Test device
- Poco X6 Pro — MediaTek Dimensity 8300
- Mali-G615 MC6 (Valhall, CSF firmware)
- Kernel driver: `mali_kbase_mt6897_r44`

## Credits
**Noin Haxel** ([@nangitagamer777-art](https://github.com/nangitagamer777-art))

## Related
[Panvk_Kmod](https://github.com/nangitagamer777-art/Panvk_Kmod) — Standalone kbase shim with GPU execution tests

## License
MIT

## Phase 11 — vk_info working (August 5, 2026)

PanVK now fully enumerates the Mali-G615 MC6:
- **GPU Name:** Mali-G615 MC6
- **API:** Vulkan 1.4.354
- **Driver:** Mesa 26.2.0-rc2
- **Extensions:** 154
- **Compute Shader:** Supported
- **Device ID:** 0xA8070000

`libkbase_drm.so` provides DRM stubs (syncobj, prime, etc.) for compatibility.

