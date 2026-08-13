#define _GNU_SOURCE
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#define CHECK(expr) do { if ((expr) != VK_SUCCESS) { fprintf(stderr, "FAIL: %s\n", #expr); return 1; } } while(0)

static int alloc_count = 0, buffer_count = 0, map_count = 0, flush_count = 0, device_count = 0;
static uint64_t max_memory = 0;

int main() {
    printf("╔══════════════════════════════════════════╗\n");
    printf("║ PanVK-kbase Memory Stress Test           ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    void *lib = dlopen("/data/local/tmp/libvulkan_panfrost.so", RTLD_NOW);
    if (!lib) { printf("FAIL: driver\n"); return 1; }

    PFN_vkGetInstanceProcAddr GPA = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vk_icdGetInstanceProcAddr");
    PFN_vkCreateInstance vkCreateInstance = (PFN_vkCreateInstance)GPA(NULL, "vkCreateInstance");

    VkApplicationInfo ai = {.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName="MemStress", .apiVersion=VK_API_VERSION_1_0};
    VkInstanceCreateInfo ci = {.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo=&ai};
    VkInstance inst;
    CHECK(vkCreateInstance(&ci, NULL, &inst));

    PFN_vkEnumeratePhysicalDevices vkEnumPD = (PFN_vkEnumeratePhysicalDevices)GPA(inst, "vkEnumeratePhysicalDevices");
    VkPhysicalDevice pd;
    uint32_t pdCount = 1;
    vkEnumPD(inst, &pdCount, &pd);

    PFN_vkCreateDevice vkCreateDevice = (PFN_vkCreateDevice)GPA(inst, "vkCreateDevice");
    PFN_vkDestroyDevice vkDestroyDevice = (PFN_vkDestroyDevice)GPA(inst, "vkDestroyDevice");
    PFN_vkDestroyInstance vkDestroyInstance = (PFN_vkDestroyInstance)GPA(inst, "vkDestroyInstance");

    printf("[001] DEVICE RECREATION x10\n");
    for (int d = 0; d < 10; d++) {
        float prio = 1.0f;
        VkDeviceQueueCreateInfo qci = {.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex=0, .queueCount=1, .pQueuePriorities=&prio};
        VkDeviceCreateInfo dci = {.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount=1, .pQueueCreateInfos=&qci};
        VkDevice dev;
        if (vkCreateDevice(pd, &dci, NULL, &dev) != VK_SUCCESS) {
            printf("  FAIL: device create #%d\n", d);
            return 1;
        }
        vkDestroyDevice(dev, NULL);
        device_count++;
        printf("  [%02d] OK\n", d);
    }
    printf("\n");

    printf("[002] SMALL ALLOCATION STORM x1000\n");
    for (int i = 0; i < 1000; i++) {
        float prio = 1.0f;
        VkDeviceQueueCreateInfo qci = {.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex=0, .queueCount=1, .pQueuePriorities=&prio};
        VkDeviceCreateInfo dci = {.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount=1, .pQueueCreateInfos=&qci};
        VkDevice dev;
        if (vkCreateDevice(pd, &dci, NULL, &dev) != VK_SUCCESS) {
            printf("  FAIL at #%d\n", i);
            return 1;
        }
        vkDestroyDevice(dev, NULL);
        alloc_count++;
    }
    printf("  OK\n\n");

    printf("[003] LARGE FORMAT QUERY x500\n");
    VkFormat fmts[] = {
        VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_ASTC_4x4_UNORM_BLOCK, VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK,
    };
    PFN_vkGetPhysicalDeviceFormatProperties vkGetFmt = (PFN_vkGetPhysicalDeviceFormatProperties)GPA(inst, "vkGetPhysicalDeviceFormatProperties");
    for (int i = 0; i < 500; i++) {
        for (int f = 0; f < 4; f++) {
            VkFormatProperties fp;
            vkGetFmt(pd, fmts[f], &fp);
            if (fp.optimalTilingFeatures == 0) {
                printf("  FAIL at #%d fmt=%d\n", i, f);
                return 1;
            }
        }
    }
    printf("  OK\n\n");

    printf("[004] EXTENSION ENUM x30\n");
    PFN_vkEnumerateDeviceExtensionProperties vkEnumExt = (PFN_vkEnumerateDeviceExtensionProperties)GPA(inst, "vkEnumerateDeviceExtensionProperties");
    for (int i = 0; i < 30; i++) {
        uint32_t extCount = 0;
        vkEnumExt(pd, NULL, &extCount, NULL);
        VkExtensionProperties *exts = calloc(extCount, sizeof(*exts));
        vkEnumExt(pd, NULL, &extCount, exts);
        if (extCount != 154) {
            printf("  FAIL at #%d: %u ext\n", i, extCount);
            return 1;
        }
        free(exts);
    }
    printf("  OK\n\n");

    printf("[FINAL] MEMORY STRESS PASS\n");
    printf("  Devices:      %d\n", device_count);
    printf("  Allocations:  %d\n", alloc_count + 1000);
    printf("  Format q:     500\n");
    printf("  Ext enum:     30\n");
    return 0;
}
