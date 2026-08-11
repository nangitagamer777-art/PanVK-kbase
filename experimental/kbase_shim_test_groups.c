#include "kbase_shim.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>

int kbase_shim_create_group_v16(kbase_shim_device_t *dev) {
    if (!dev || dev->mali_fd < 0) return -1;

    union kbase_ioctl_cs_queue_group_create_1_6 grp = {0};
    grp.in.tiler_mask = ~0ULL;
    grp.in.fragment_mask = ~0ULL;
    grp.in.compute_mask = ~0ULL;
    grp.in.cs_min = 1;
    grp.in.priority = 0;
    grp.in.tiler_max = 8;
    grp.in.fragment_max = 8;
    grp.in.compute_max = 8;

    printf("[kb] creating group (cmd 42)...\n");
    int res = ioctl(dev->mali_fd, KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_1_6, &grp);
    if (res < 0) {
        printf("[kb] group create failed: %s\n", strerror(errno));
        return -1;
    }

    printf("[kb] group handle %u, uid %u\n", grp.out.group_handle, grp.out.group_uid);
    dev->group_handle = grp.out.group_handle;
    return 0;
}
