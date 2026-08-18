/*
 * Copyright 2026 Google LLC
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */
#include "compiler/libcl/libcl.h"

#if PAN_ARCH >= 6
KERNEL(32)
panlib_fill(global uint32_t *address, uint32_t value)
{
   address[cl_global_id.x] = value;
}

KERNEL(32)
panlib_fill_uint4(global uint4 *address, uint a, uint b, uint c, uint d)
{
   address[cl_global_id.x] = (uint4)(a, b, c, d);
}

KERNEL(1)
panlib_fill_scalar(global uint32_t *address, uint32_t value)
{
   address[cl_global_id.x] = value;
}

KERNEL(1)
panlib_fill_uint4_scalar(global uint4 *address, uint a, uint b, uint c, uint d)
{
   address[cl_global_id.x] = (uint4)(a, b, c, d);
}

KERNEL(32)
panlib_copy_buffer(uint64_t dst_addr, uint64_t src_addr)
{
   uint32_t idx = get_global_id(0);
   uint32_t total = get_global_size(0);
   global uint32_t *dst = (global uint32_t *)(uintptr_t)dst_addr;
   global uint32_t *src = (global uint32_t *)(uintptr_t)src_addr;
   if (idx < total) {
      uint32_t val = src[idx];
      dst[idx] = val;
   }
}
#endif
