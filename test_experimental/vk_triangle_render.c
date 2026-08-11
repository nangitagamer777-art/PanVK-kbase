#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <signal.h>

static PFN_vkGetInstanceProcAddr GPA;

static uint32_t read_spv(const char *path, uint32_t **code) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    *code = malloc(sz);
    fread(*code, 1, sz, f);
    fclose(f);
    return sz / 4;
}

int main() {
    signal(SIGSEGV, SIG_IGN);
    signal(SIGABRT, SIG_IGN);
    
    void *lib = dlopen("/data/local/tmp/libvulkan_panfrost.so", RTLD_NOW);
    if (!lib) { printf("dlopen failed\n"); return 1; }
    GPA = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vk_icdGetInstanceProcAddr");
    
    PFN_vkCreateInstance vkCreateInstance = (PFN_vkCreateInstance)GPA(NULL, "vkCreateInstance");
    VkApplicationInfo ai = {.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="triangle",.apiVersion=VK_API_VERSION_1_0};
    VkInstanceCreateInfo ci = {.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&ai};
    VkInstance inst;
    vkCreateInstance(&ci, NULL, &inst);
    
    PFN_vkEnumeratePhysicalDevices vkEnumPD = (PFN_vkEnumeratePhysicalDevices)GPA(inst, "vkEnumeratePhysicalDevices");
    uint32_t pdCount = 0;
    vkEnumPD(inst, &pdCount, NULL);
    printf("Physical devices: %u\n", pdCount);
    if (pdCount == 0) { printf("No GPU\n"); return 1; }
    
    VkPhysicalDevice pd;
    vkEnumPD(inst, &pdCount, &pd);
    
    /* Get queue family */
    uint32_t qfCount = 0;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetQF = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)GPA(inst, "vkGetPhysicalDeviceQueueFamilyProperties");
    vkGetQF(pd, &qfCount, NULL);
    VkQueueFamilyProperties *qfs = calloc(qfCount, sizeof(*qfs));
    vkGetQF(pd, &qfCount, qfs);
    
    int gfxIdx = -1;
    for (uint32_t i = 0; i < qfCount; i++) {
        if (qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { gfxIdx = i; break; }
    }
    printf("Graphics queue family: %d (count=%u)\n", gfxIdx, gfxIdx >= 0 ? qfs[gfxIdx].queueCount : 0);
    free(qfs);
    
    if (gfxIdx < 0) { printf("No graphics queue\n"); return 1; }
    
    /* Create device */
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=gfxIdx,.queueCount=1,.pQueuePriorities=&prio};
    VkPhysicalDeviceFeatures feats = {0};
    VkDeviceCreateInfo dci = {.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qci,.pEnabledFeatures=&feats};
    
    PFN_vkCreateDevice vkCreateDevice = (PFN_vkCreateDevice)GPA(inst, "vkCreateDevice");
    VkDevice dev;
    VkResult r = vkCreateDevice(pd, &dci, NULL, &dev);
    printf("vkCreateDevice: %d\n", r);
    
    if (r == VK_SUCCESS) {
        printf("Device created! Getting queue...\n");
        PFN_vkGetDeviceQueue vkGetQueue = (PFN_vkGetDeviceQueue)GPA(inst, "vkGetDeviceQueue");
        VkQueue queue;
        vkGetQueue(dev, gfxIdx, 0, &queue);
        printf("Queue: %p\n", queue);
        
        PFN_vkDestroyDevice vkDestroyDevice = (PFN_vkDestroyDevice)GPA(inst, "vkDestroyDevice");
        vkDestroyDevice(dev, NULL);
    }
    
    PFN_vkDestroyInstance vkDestroyInstance = (PFN_vkDestroyInstance)GPA(inst, "vkDestroyInstance");
    vkDestroyInstance(inst, NULL);
    dlclose(lib);
    printf("Done\n");
    return 0;
}
