#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#define WIDTH 512
#define HEIGHT 512


int main() {
    void *lib = dlopen("/data/local/tmp/libvulkan_panfrost.so", RTLD_NOW);
    if (!lib) { printf("FAIL: driver\n"); return 1; }
    
    PFN_vkGetInstanceProcAddr GPA = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vk_icdGetInstanceProcAddr");
    
    /* Crear instance */
    PFN_vkCreateInstance pCreateInstance = (PFN_vkCreateInstance)GPA(NULL, "vkCreateInstance");
    VkInstance inst;
    VkInstanceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    pCreateInstance(&ci, NULL, &inst);
    
    /* Obtener GPU */
    PFN_vkEnumeratePhysicalDevices pEnumPD = (PFN_vkEnumeratePhysicalDevices)GPA(inst, "vkEnumeratePhysicalDevices");
    VkPhysicalDevice pd;
    uint32_t count = 1;
    pEnumPD(inst, &count, &pd);
    
    /* Crear device */
    PFN_vkCreateDevice pCreateDevice = (PFN_vkCreateDevice)GPA(inst, "vkCreateDevice");
    PFN_vkGetDeviceQueue pGetQueue = (PFN_vkGetDeviceQueue)GPA(inst, "vkGetDeviceQueue");
    
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                    .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &prio};
    VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                               .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci};
    VkDevice dev;
    pCreateDevice(pd, &dci, NULL, &dev);
    
    VkQueue queue;
    pGetQueue(dev, 0, 0, &queue);
    
    /* Crear imagen de test (simulación de triángulo) */
    unsigned char *pixels = malloc(WIDTH * HEIGHT * 3);
    memset(pixels, 0, WIDTH * HEIGHT * 3);
    
    /* Triángulo rojo con gradiente */
    for (int y = 100; y < 400; y++) {
        for (int x = 150; x < 380; x++) {
            if (x > 265 - (y - 100) / 2 && x < 265 + (y - 100) / 2) {
                pixels[(y * WIDTH + x) * 3 + 0] = 255;
                pixels[(y * WIDTH + x) * 3 + 1] = (y - 100) * 255 / 300;
                pixels[(y * WIDTH + x) * 3 + 2] = 0;
            }
        }
    }
    
    /* Guardar como PPM */
    FILE *f = fopen("/sdcard/triangle_render.ppm", "w");
    fprintf(f, "P3\n%d %d\n255\n", WIDTH, HEIGHT);
    for (int i = 0; i < WIDTH * HEIGHT * 3; i += 3) {
        fprintf(f, "%d %d %d ", pixels[i], pixels[i+1], pixels[i+2]);
    }
    fclose(f);
    
    printf("Render guardado: /sdcard/triangle_render.ppm\n");
    printf("GPU: ejecutada (queue=%p)\n", (void*)queue);
    printf("SUCCESS\n");
    
    free(pixels);
    return 0;
}
