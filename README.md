# PanVK-kbase — Mesa with kbase backend for Mali Valhall GPUs

Fork of Mesa 26.2.0-rc2 with a third Vulkan backend that talks directly
to `/dev/mali0` via the kbase ioctl interface.

## What this is

This repo contains the patches and precompiled binary to run PanVK
on MediaTek devices with `mali_kbase` instead of the open-source
Panthor DRM driver.

## Files

- `kbase_kmod.c` — Backend implementing pan_kmod_ops via /dev/mali0
- `pan_kmod.c` — Patched to detect kbase backend
- `meson.build` — Build file with kbase_kmod.c added
- `libvulkan_panfrost.so` — Precompiled binary

## Build

Follow the standard Mesa cross-compile for Android with these changes:
1. Copy `kbase_kmod.c` to `src/panfrost/lib/kmod/`
2. Replace `pan_kmod.c` and `meson.build`
3. Run `meson setup build --wipe` and `ninja -C build`

## Status

- Mesa 26.2.0-rc2 compiled successfully
- libvulkan_panfrost.so loads on Android
- vk_icdGetInstanceProcAddr exported
- Ready for Vulkan app testing

## Credits

Noin Haxel (@nangitagamer777-art)

## License

MIT (matches Mesa)
