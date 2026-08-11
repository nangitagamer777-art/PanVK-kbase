#include "kbase_shim.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define BASEP_QUEUE_NR_MMAP_USER_PAGES 3
#define PAGE_SIZE_4K 4096
#define CS_INSERT_LO  0x0000
#define CS_INSERT_HI  0x0004
#define CS_EXTRACT_LO 0x0000
#define CS_EXTRACT_HI 0x0004
#define CS_ACTIVE      0x0008

#ifndef KBASE_IOCTL_CS_EVENT_SIGNAL
#define KBASE_IOCTL_CS_EVENT_SIGNAL _IO(0x80, 44)
#endif

int kbase_shim_csf_kick_and_wait_v4(kbase_shim_device_t *dev) {
    if (!dev || dev->mali_fd < 0) return -1;
    union kbase_ioctl_mem_alloc mem = {0};
    mem.in.va_pages = 2;
    mem.in.commit_pages = 2;
    mem.in.flags = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR |
                   BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_EX;
    printf("\n[kb] alloc queue buffer...\n");
    if (ioctl(dev->mali_fd, KBASE_IOCTL_MEM_ALLOC, &mem) < 0) {
        printf("[kb] mem_alloc failed\n"); return -1;
    }
    dev->queue_buffer_va = mem.out.gpu_va;
    printf("[kb] gpu va 0x%llx\n", (unsigned long long)dev->queue_buffer_va);

    void *buf = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED, dev->mali_fd, dev->queue_buffer_va);
    if (buf == MAP_FAILED) { printf("[kb] mmap failed\n"); return -1; }
    memset(buf, 0, 8192);
    __u64 *instr = (__u64 *)buf;

    /* 8 instrucciones: NOP, 0x01(0xDEADBEEF), NOP, 0x01(0xCAFEBABE), NOP, 0x02(0x12345678), NOP, NOP */
    instr[0] = CSF_INSTR(CSF_OPCODE_NOP, 0);
    instr[1] = CSF_INSTR(0x01, 0xDEADBEEF);
    instr[2] = CSF_INSTR(CSF_OPCODE_NOP, 0);
    instr[3] = CSF_INSTR(0x01, 0xCAFEBABE);
    instr[4] = CSF_INSTR(CSF_OPCODE_NOP, 0);
    instr[5] = CSF_INSTR(CSF_OPCODE_MOVE32, 0x12345678);
    instr[6] = CSF_INSTR(CSF_OPCODE_NOP, 0);
    instr[7] = CSF_INSTR(CSF_OPCODE_NOP, 0);
    int total_bytes = 64;
    printf("[kb] wrote 8 instructions (%d bytes)\n", total_bytes);

    struct kbase_ioctl_cs_queue_register reg = {0};
    reg.buffer_gpu_addr = dev->queue_buffer_va;
    reg.buffer_size = 0x2000;
    reg.priority = 0;
    if (ioctl(dev->mali_fd, KBASE_IOCTL_CS_QUEUE_REGISTER, &reg) < 0) {
        printf("[kb] register failed\n"); munmap(buf, 8192); return -1;
    }
    union kbase_ioctl_cs_queue_bind bind = {0};
    bind.in.buffer_gpu_addr = dev->queue_buffer_va;
    bind.in.group_handle = dev->group_handle;
    bind.in.csi_index = 0;
    if (ioctl(dev->mali_fd, KBASE_IOCTL_CS_QUEUE_BIND, &bind) < 0) {
        printf("[kb] bind failed\n"); munmap(buf, 8192); return -1;
    }
    dev->queue_mmap_handle = bind.out.mmap_handle;

    size_t io_sz = BASEP_QUEUE_NR_MMAP_USER_PAGES * PAGE_SIZE_4K;
    void *io = mmap(NULL, io_sz, PROT_READ | PROT_WRITE, MAP_SHARED, dev->mali_fd, dev->queue_mmap_handle);
    if (io == MAP_FAILED) { printf("[kb] user_io mmap failed\n"); munmap(buf, 8192); return -1; }

    volatile __u32 *out = (volatile __u32 *)((char *)io + 2 * PAGE_SIZE_4K);
    volatile __u32 *inp = (volatile __u32 *)((char *)io + PAGE_SIZE_4K);

    printf("[kb] BASELINE EXTRACT = %llu\n", (unsigned long long)out[CS_EXTRACT_HI/4] << 32 | out[CS_EXTRACT_LO/4]);
    inp[CS_INSERT_LO/4] = total_bytes;
    inp[CS_INSERT_HI/4] = 0;

    struct kbase_ioctl_cs_queue_kick kick = {0};
    kick.buffer_gpu_addr = dev->queue_buffer_va;
    ioctl(dev->mali_fd, KBASE_IOCTL_CS_QUEUE_KICK, &kick);
    ioctl(dev->mali_fd, KBASE_IOCTL_CS_EVENT_SIGNAL);
    ((volatile __u32 *)io)[0] = 1;
    usleep(50000);
    ioctl(dev->mali_fd, KBASE_IOCTL_CS_QUEUE_KICK, &kick);
    usleep(100000);

    unsigned long long consumed = (unsigned long long)out[CS_EXTRACT_HI/4] << 32 | out[CS_EXTRACT_LO/4];
    printf("[kb] POST-KICK EXTRACT = %llu bytes\n", consumed);

    /* Buscar 0xDEADBEEF y 0xCAFEBABE en todo el buffer de cola */
    __u32 *scan = (__u32 *)buf;
    for (int i = 0; i < 2048; i++) {
        if (scan[i] == 0xDEADBEEF) printf("[kb] >>> FOUND 0xDEADBEEF at buffer[%d]\n", i);
        if (scan[i] == 0xCAFEBABE) printf("[kb] >>> FOUND 0xCAFEBABE at buffer[%d]\n", i);
        if (scan[i] == 0x12345678) printf("[kb] >>> FOUND 0x12345678 at buffer[%d]\n", i);
    }

    /* Buscar en páginas I/O */
    __u32 *io_scan = (__u32 *)io;
    for (int i = 0; i < (int)(io_sz / 4); i++) {
        if (io_scan[i] == 0xDEADBEEF) printf("[kb] >>> FOUND 0xDEADBEEF at io[%d]\n", i);
        if (io_scan[i] == 0xCAFEBABE) printf("[kb] >>> FOUND 0xCAFEBABE at io[%d]\n", i);
        if (io_scan[i] == 0x12345678) printf("[kb] >>> FOUND 0x12345678 at io[%d]\n", i);
    }

    printf("[kb] scan complete\n");
    munmap(io, io_sz);
    munmap(buf, 8192);
    return 0;
}
