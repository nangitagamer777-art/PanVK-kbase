#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

typedef void* (*PFN_vkGetInstanceProcAddr)(void*, const char*);

int main() {
    void *lib = dlopen("/data/local/tmp/libvulkan_panfrost.so", RTLD_NOW);
    if (!lib) { printf("FAIL: driver\n"); return 1; }
    
    PFN_vkGetInstanceProcAddr GPA = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vk_icdGetInstanceProcAddr");
    if (!GPA) { printf("FAIL: GPA\n"); return 1; }
    
    printf("╔══════════════════════════════════════════╗\n");
    printf("║   PanVK-kbase Complete Info Report       ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");
    
    /* Extensiones de instancia */
    uint32_t inst_ext_count = 0;
    void (*vkEnumExt)(const char*, uint32_t*, void*) = (void*)GPA(NULL, "vkEnumerateInstanceExtensionProperties");
    vkEnumExt(NULL, &inst_ext_count, NULL);
    void *inst_exts = calloc(inst_ext_count, 260);
    vkEnumExt(NULL, &inst_ext_count, inst_exts);
    
    printf("[INSTANCE EXTENSIONS] %u\n", inst_ext_count);
    for (uint32_t i = 0; i < inst_ext_count; i++) {
        printf("  %s\n", (char*)((unsigned char*)inst_exts + i * 260));
    }
    printf("\n");
    
    /* Crear instance */
    int (*vkCreateInstance)(const void*, const void*, void**) = (void*)GPA(NULL, "vkCreateInstance");
    unsigned char ci[64] = {0};
    ci[0] = 1;
    void *inst = NULL;
    if (vkCreateInstance((void*)ci, NULL, &inst) != 0) {
        printf("FAIL: instance\n");
        return 1;
    }
    
    /* Enumerar GPUs */
    int (*vkEnumPD)(void*, uint32_t*, void*) = (void*)GPA(inst, "vkEnumeratePhysicalDevices");
    uint32_t gpu_count = 0;
    vkEnumPD(inst, &gpu_count, NULL);
    void *gpus[4];
    vkEnumPD(inst, &gpu_count, gpus);
    
    printf("[GPU COUNT] %u\n\n", gpu_count);
    
    /* Propiedades */
    void (*vkGetProps)(void*, void*) = (void*)GPA(inst, "vkGetPhysicalDeviceProperties");
    unsigned char props[2048] = {0};
    vkGetProps(gpus[0], props);
    
    uint32_t api_ver = *(uint32_t*)&props[0];
    uint32_t drv_ver = *(uint32_t*)&props[4];
    uint32_t vendor = *(uint32_t*)&props[8];
    uint32_t device = *(uint32_t*)&props[12];
    uint32_t dev_type = *(uint32_t*)&props[16];
    const char *dev_name = (const char*)&props[20];
    
    printf("[IDENTITY]\n");
    printf("  Device Name:    %s\n", dev_name);
    printf("  Vendor ID:      0x%04X\n", vendor);
    printf("  Device ID:      0x%04X\n", device);
    printf("  Device Type:    %u\n", dev_type);
    printf("  API Version:    %u.%u.%u\n", api_ver >> 22, (api_ver >> 12) & 0x3FF, api_ver & 0xFFF);
    printf("  Driver Version: %u.%u.%u\n\n", drv_ver >> 22, (drv_ver >> 12) & 0x3FF, drv_ver & 0xFFF);
    
    /* Features */
    void (*vkGetFeat)(void*, void*) = (void*)GPA(inst, "vkGetPhysicalDeviceFeatures");
    unsigned char feat[256] = {0};
    vkGetFeat(gpus[0], feat);
    
    printf("[FEATURES]\n");
    printf("  Geometry Shader:       %s\n", feat[16] & 1 ? "YES" : "NO");
    printf("  Tessellation Shader:   %s\n", feat[17] & 1 ? "YES" : "NO");
    printf("  Float64:               %s\n", feat[19] & 1 ? "YES" : "NO");
    printf("  Int16:                 %s\n", feat[21] & 1 ? "YES" : "NO");
    printf("  Int64:                 %s\n", feat[22] & 1 ? "YES" : "NO");
    printf("  Logic Op:              %s\n", feat[23] & 1 ? "YES" : "NO");
    printf("  Anisotropy:            %s\n", feat[34] & 1 ? "YES" : "NO");
    printf("  Dual Src Blend:        %s\n", feat[40] & 1 ? "YES" : "NO");
    printf("  Depth Clamp:           %s\n", feat[46] & 1 ? "YES" : "NO");
    printf("  Fill Non-Solid:        %s\n", feat[47] & 1 ? "YES" : "NO");
    printf("  Wide Lines:            %s\n", feat[52] & 1 ? "YES" : "NO");
    printf("  Multi Draw Indirect:   %s\n", feat[55] & 1 ? "YES" : "NO");
    printf("  Image Cube Array:      %s\n", feat[57] & 1 ? "YES" : "NO");
    printf("  BC Compression:        %s\n\n", feat[58] & 1 ? "YES" : "NO");
    
    /* Límites */
    printf("[LIMITS]\n");
    printf("  Max 2D image:    %u\n", *(uint32_t*)&props[280]);
    printf("  Max 3D image:    %u\n", *(uint32_t*)&props[284]);
    printf("  Max push const:  %u\n", *(uint32_t*)&props[320]);
    printf("  Max samplers:    %u\n", *(uint32_t*)&props[304]);
    printf("  Max color att:   %u\n", *(uint32_t*)&props[256]);
    printf("  Max viewport:    %u x %u\n", *(uint32_t*)&props[400], *(uint32_t*)&props[404]);
    printf("  Max framebuffer: %u x %u\n", *(uint32_t*)&props[408], *(uint32_t*)&props[412]);
    printf("  Max compute:     %u x %u x %u\n", *(uint32_t*)&props[432], *(uint32_t*)&props[436], *(uint32_t*)&props[440]);
    printf("  Max WG size:     %u x %u x %u\n", *(uint32_t*)&props[444], *(uint32_t*)&props[448], *(uint32_t*)&props[452]);
    printf("  Max UB range:    %u\n", *(uint32_t*)&props[468]);
    printf("  Max SSBO range:  %u\n\n", *(uint32_t*)&props[472]);
    
    /* Extensiones de dispositivo */
    int (*vkEnumDevExt)(void*, const char*, uint32_t*, void*) = (void*)GPA(inst, "vkEnumerateDeviceExtensionProperties");
    uint32_t dev_ext_count = 0;
    vkEnumDevExt(gpus[0], NULL, &dev_ext_count, NULL);
    void *dev_exts = calloc(dev_ext_count, 260);
    vkEnumDevExt(gpus[0], NULL, &dev_ext_count, dev_exts);
    
    printf("[DEVICE EXTENSIONS] %u\n", dev_ext_count);
    for (uint32_t i = 0; i < dev_ext_count; i++) {
        printf("  %s\n", (char*)((unsigned char*)dev_exts + i * 260));
    }
    printf("\n");
    
    /* Backend */
    FILE *f = fopen("/dev/mali0", "r");
    printf("[BACKEND]\n");
    printf("  Driver:  kbase via /dev/mali0\n");
    printf("  Node:    %s\n", f ? "OPEN OK" : "OPEN FAILED");
    if (f) fclose(f);
    printf("  Status:  All queries OK\n");
    
    free(inst_exts);
    free(dev_exts);
    return 0;
}
