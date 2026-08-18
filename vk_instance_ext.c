#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

typedef void* (*PFN_vkGetInstanceProcAddr)(void*, const char*);

int main() {
    void *lib = dlopen("/data/local/tmp/libvulkan_panfrost.so", RTLD_NOW);
    if (!lib) { printf("dlopen failed\n"); return 1; }
    
    PFN_vkGetInstanceProcAddr GPA = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vk_icdGetInstanceProcAddr");
    if (!GPA) { printf("GPA not found\n"); return 1; }
    
    /* Enumerar extensiones de instancia */
    unsigned int ext_count = 0;
    void *vkEnumExt = GPA(NULL, "vkEnumerateInstanceExtensionProperties");
    if (!vkEnumExt) { printf("vkEnumExt not found\n"); return 1; }
    
    ((void (*)(const char*, unsigned int*, void*))vkEnumExt)(NULL, &ext_count, NULL);
    void *exts = calloc(ext_count, 256);
    ((void (*)(const char*, unsigned int*, void*))vkEnumExt)(NULL, &ext_count, exts);
    
    printf("Extensiones de instancia: %u\n", ext_count);
    for (unsigned int i = 0; i < ext_count; i++) {
        const char *name = (const char*)((unsigned char*)exts + i * 256);
        if (strstr(name, "surface") || strstr(name, "android")) {
            printf("  %s\n", name);
        }
    }
    
    free(exts);
    return 0;
}
