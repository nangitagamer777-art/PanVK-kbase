/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>

#include "vk_alloc.h"
#include "vk_log.h"

#include "panvk_device.h"
#include "panvk_priv_bo.h"

#include "kmod/pan_kmod.h"
#include "pan_props.h"

#include "genxml/decode.h"

VkResult
panvk_priv_bo_create(struct panvk_device *dev, uint64_t size, uint32_t flags,
                     VkSystemAllocationScope scope, struct panvk_priv_bo **out)
{
   VkResult result;
   int ret;

   fprintf(stderr,
           "[kbase_dbg] priv_bo_create: size=%llu flags=0x%x "
           "NO_MMAP=%s\n",
           (unsigned long long)size,
           flags,
           (flags & PAN_KMOD_BO_FLAG_NO_MMAP) ? "YES" : "NO");

   /*
    * kbase:
    * Los BO privados deben ser CPU-visible para los pools RW.
    */
   uint32_t original_flags = flags;
   flags &= ~PAN_KMOD_BO_FLAG_NO_MMAP;

   fprintf(stderr,
           "[kbase_dbg] priv_bo_create: flags 0x%x -> 0x%x "
           "(NO_MMAP forced off)\n",
           original_flags,
           flags);
   struct panvk_priv_bo *priv_bo =
      vk_zalloc(&dev->vk.alloc, sizeof(*priv_bo), 8, scope);

   if (!priv_bo)
      return panvk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct pan_kmod_bo *bo =
      pan_kmod_bo_alloc(dev->kmod.dev, dev->kmod.vm, size, flags);

   fprintf(stderr,
           "[kbase_dbg] priv_bo_create: alloc bo=%p size=%llu flags=0x%x\n",
           (void *)bo,
           (unsigned long long)size,
           flags);
   if (!bo) {
      result = panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      goto err_free_priv_bo;
   }

   priv_bo->bo = bo;
   priv_bo->dev = dev;

   if (!(flags & PAN_KMOD_BO_FLAG_NO_MMAP)) {
      priv_bo->addr.host =
         pan_kmod_bo_mmap(bo, PROT_READ | PROT_WRITE, MAP_SHARED, NULL);

      fprintf(stderr,
              "[kbase_dbg] priv_bo: mmap returned %p\n",
              priv_bo->addr.host);

      /*
       * IMPORTANT:
       * MAP_FAILED debe comprobarse ANTES de cualquier acceso.
       */
      if (priv_bo->addr.host == MAP_FAILED) {
         fprintf(stderr,
                 "[kbase_dbg] priv_bo: mmap FAILED\n");

         result = panvk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);
         goto err_put_bo;
      }

      if (priv_bo->addr.host == NULL) {
         fprintf(stderr,
                 "[kbase_dbg] priv_bo: mmap returned NULL\n");

         result = panvk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);
         goto err_put_bo;
      }

      fprintf(stderr,
              "[kbase_dbg] priv_bo: memset host=%p size=%llu\n",
              priv_bo->addr.host,
              (unsigned long long)pan_kmod_bo_size(bo));

      memset(priv_bo->addr.host, 0, pan_kmod_bo_size(bo));

      fprintf(stderr,
              "[kbase_dbg] priv_bo: memset OK\n");

      /*
       * Test directo de CPU RW.
       *
       * Si esto falla, el problema está en mmap/kbase.
       * Si pasa, addr.host es realmente escribible.
       */
      volatile uint32_t *cpu_test =
         (volatile uint32_t *)priv_bo->addr.host;

      *cpu_test = 0x12345678;

      uint32_t cpu_value = *cpu_test;

      fprintf(stderr,
              "[kbase_dbg] priv_bo: CPU RW test "
              "wrote=0x12345678 read=0x%x\n",
              cpu_value);

      if (cpu_value != 0x12345678) {
         fprintf(stderr,
                 "[kbase_dbg] priv_bo: CPU RW TEST FAILED\n");

         result = panvk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);
         goto err_put_bo;
      }

      fprintf(stderr,
              "[kbase_dbg] priv_bo: CPU RW test OK\n");
   }

   struct pan_kmod_vm_op op = {
      .type = PAN_KMOD_VM_OP_TYPE_MAP,
      .va = {
         .start = PAN_KMOD_VM_MAP_AUTO_VA,
         .size = pan_kmod_bo_size(bo),
      },
      .map = {
         .bo = priv_bo->bo,
         .bo_offset = 0,
      },
   };

    fprintf(stderr, "[kbase] priv_bo: about to call panvk_as_alloc, priv_heap=%p\n", dev->as.priv_heap);
   if (!(dev->kmod.vm->flags & PAN_KMOD_VM_FLAG_AUTO_VA)) {
      op.va.start =
    fprintf(stderr, "[kbase] priv_bo: inside AUTO_VA block, calling panvk_as_alloc\n");
         panvk_as_alloc(dev, dev->as.priv_heap, op.va.size,
                        pan_choose_gpu_va_alignment(dev->kmod.vm, op.va.size));
      if (!op.va.start) {
         result = panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);
         goto err_munmap_bo;
      }
   }

    fprintf(stderr, "[kbase] priv_bo: before vm_bind\n");
   ret = pan_kmod_vm_bind(dev->kmod.vm, PAN_KMOD_VM_OP_MODE_IMMEDIATE, &op, 1);
    fprintf(stderr, "[kbase] priv_bo: after vm_bind, ret=%d\n", ret);
    if (ret) {
        result = panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);
        goto err_return_va;
    }
   priv_bo->addr.dev = op.va.start;

   fprintf(stderr,
           "[kbase_dbg] priv_bo: FINAL "
           "bo=%p handle=%u size=%llu "
           "host=%p dev=0x%llx flags=0x%x\n",
           (void *)bo,
           pan_kmod_bo_handle(bo),
           (unsigned long long)pan_kmod_bo_size(bo),
           priv_bo->addr.host,
           (unsigned long long)priv_bo->addr.dev,
           flags);

    panvk_address_binding_report(dev, NULL, priv_bo->addr.dev,
                                 pan_kmod_bo_size(priv_bo->bo),
                                 VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT);
    fprintf(stderr, "[kbase] priv_bo: address_binding_report done\n");
                            priv_bo->addr.host, pan_kmod_bo_size(priv_bo->bo),

   *out = priv_bo;
   return VK_SUCCESS;

err_return_va:
   if (!(dev->kmod.vm->flags & PAN_KMOD_VM_FLAG_AUTO_VA)) {
      panvk_as_free(dev, dev->as.priv_heap, op.va.start, op.va.size);
   }

err_munmap_bo:
   if (priv_bo->addr.host) {
      ret = os_munmap(priv_bo->addr.host, pan_kmod_bo_size(bo));
      assert(!ret);
   }

err_put_bo:
   pan_kmod_bo_put(bo);

err_free_priv_bo:
   vk_free(&dev->vk.alloc, priv_bo);
   return result;
}

void
panvk_priv_bo_flush(struct panvk_priv_bo *priv_bo, size_t offset, size_t size)
{
   assert(priv_bo->addr.host != NULL);
   pan_kmod_queue_bo_map_sync(priv_bo->bo, offset, priv_bo->addr.host + offset,
                              size, PAN_KMOD_BO_SYNC_CPU_CACHE_FLUSH);
}

void
panvk_priv_bo_invalidate(struct panvk_priv_bo *priv_bo, size_t offset,
                         size_t size)
{
   assert(priv_bo->addr.host != NULL);
   pan_kmod_queue_bo_map_sync(priv_bo->bo, offset, priv_bo->addr.host + offset,
                              size,
                              PAN_KMOD_BO_SYNC_CPU_CACHE_FLUSH_AND_INVALIDATE);
}

static void
panvk_priv_bo_destroy(struct panvk_priv_bo *priv_bo)
{
   struct panvk_device *dev = priv_bo->dev;

   panvk_address_binding_report(dev, NULL, priv_bo->addr.dev,
                                pan_kmod_bo_size(priv_bo->bo),
                                VK_DEVICE_ADDRESS_BINDING_TYPE_UNBIND_EXT);

   if (dev->debug.decode_ctx) {
      pandecode_inject_free(dev->debug.decode_ctx, priv_bo->addr.dev,
                            pan_kmod_bo_size(priv_bo->bo));
   }

   struct pan_kmod_vm_op op = {
      .type = PAN_KMOD_VM_OP_TYPE_UNMAP,
      .va = {
         .start = priv_bo->addr.dev,
         .size = pan_kmod_bo_size(priv_bo->bo),
      },
   };
   ASSERTED int ret =
      pan_kmod_vm_bind(dev->kmod.vm, PAN_KMOD_VM_OP_MODE_IMMEDIATE, &op, 1);
   assert(!ret);

   if (!(dev->kmod.vm->flags & PAN_KMOD_VM_FLAG_AUTO_VA)) {
      panvk_as_free(dev, dev->as.priv_heap, op.va.start, op.va.size);
   }

   if (priv_bo->addr.host) {
      ret = os_munmap(priv_bo->addr.host, pan_kmod_bo_size(priv_bo->bo));
      assert(!ret);
   }

   pan_kmod_bo_put(priv_bo->bo);
   vk_free(&dev->vk.alloc, priv_bo);
}

void
panvk_priv_bo_unref(struct panvk_priv_bo *priv_bo)
{
   if (!priv_bo || p_atomic_dec_return(&priv_bo->refcnt))
      return;

   panvk_priv_bo_destroy(priv_bo);
}
