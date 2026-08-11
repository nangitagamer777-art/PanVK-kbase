#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdint.h>

/* Intercept panvk_address_binding_report - make it a no-op */
void panvk_address_binding_report(void *dev, void *object, uint64_t base,
                                   uint64_t size, int type) {
    fprintf(stderr, "[kbase_fix] panvk_address_binding_report called, ignoring\n");
    return;
}

/* Intercept panvk_priv_bo_create to fix the AUTO_VA issue */
int panvk_priv_bo_create(void *dev, uint64_t size, uint32_t flags,
                         int scope, void **out) {
    typeof(panvk_priv_bo_create) *real = dlsym(RTLD_NEXT, "panvk_priv_bo_create");
    if (!real) {
        fprintf(stderr, "[kbase_fix] panvk_priv_bo_create real not found\n");
        return -1;
    }
    int ret = real(dev, size, flags, scope, out);
    fprintf(stderr, "[kbase_fix] panvk_priv_bo_create: ret=%d, out=%p\n", ret, out ? *out : NULL);
    return ret;
}
