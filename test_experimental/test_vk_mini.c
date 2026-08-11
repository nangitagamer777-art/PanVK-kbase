#include <stdio.h>
#include <dlfcn.h>
int main() {
    void *lib = dlopen("/data/local/tmp/libvulkan_panfrost.so", RTLD_NOW);
    if (lib) {
        printf("Library loaded!\n");
        void *vk = dlsym(lib, "vk_icdGetInstanceProcAddr");
        printf("vk_icdGetInstanceProcAddr: %p\n", vk);
        dlclose(lib);
    } else {
        printf("Failed: %s\n", dlerror());
    }
    return 0;
}
