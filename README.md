# PanVK-kbase — Mesa with kbase backend for Mali Valhall GPUs
Fork of Mesa 26.2.0-rc2 with a third Vulkan backend that talks directly to /dev/mali0 via the kbase ioctl interface.

## What this is
This repo contains the patches needed to compile Mesa's PanVK driver with a custom backend for MediaTek devices running mali_kbase instead of the open-source Panthor DRM driver.

## Files
- kbase_kmod.c — Backend implementing pan_kmod_ops via /dev/mali0 ioctls
- pan_kmod.c — Patched pan_kmod.c to detect and load the kbase backend
- meson.build — Build file with kbase_kmod.c added to the library

## How to compile
1. Place these files in your Mesa source tree: kbase_kmod.c → src/panfrost/lib/kmod/kbase_kmod.c, pan_kmod.c → src/panfrost/lib/kmod/pan_kmod.c, meson.build → src/panfrost/lib/kmod/meson.build
2. Follow the standard Mesa cross-compile for Android with: meson setup build --wipe --cross-file=android-aarch64-35 -Dvulkan-drivers=panfrost -Dgallium-drivers=panfrost -Dplatforms=android && ninja -C build
3. Output: build/src/panfrost/vulkan/libvulkan_panfrost.so

## Status
- Mesa 26.2.0-rc2 compiles successfully with kbase backend
- libvulkan_panfrost.so loads on Android via dlopen
- vk_icdGetInstanceProcAddr exported and callable
- All 15 pan_kmod_ops functions implemented
- Ready for Vulkan app testing

## Requirements
- Android device with /dev/mali0 (MediaTek Dimensity with Mali GPU)
- Shizuku or root to access the device node
- Mesa cross-compile environment (see Panvk_Kmod docs)

## Credits
Noin Haxel (@nangitagamer777-art)

## Related
Panvk_Kmod — Standalone kbase shim and GPU execution tests

## License
MIT
