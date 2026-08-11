#include "../include/kbase_shim.h"
#include <stdio.h>

int main() {
    printf("==================================================\n");
    printf("   panvk_kmod — 32 NOPs stress test\n");
    printf("==================================================\n");

    kbase_shim_device_t dev;

    if (kbase_shim_init(&dev) == 0) {
        printf("\n[ ok ] Context ready.\n");
        kbase_shim_get_gpu_props(&dev);
        kbase_shim_create_group_v16(&dev);
        kbase_shim_csf_kick_and_wait_v4(&dev);
        kbase_shim_close(&dev);
        return 0;
    }
    printf("\n[ fail ] Init failed.\n");
    return 1;
}
