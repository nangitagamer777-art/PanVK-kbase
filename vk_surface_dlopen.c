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
    
    printf("ICD loaded\n");
    return 0;
}
