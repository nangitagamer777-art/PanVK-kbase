#define _GNU_SOURCE
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#define CHECK(expr) do { if ((expr) != VK_SUCCESS) { fprintf(stderr, "FAIL: %s\n", #expr); return 1; } } while(0)

static int alloc_count = 0, map_count = 0, write_count = 0, read_count = 0, flush_count = 0, inval_count = 0;

int main() {
    printf("╔══════════════════════════════════════════╗\n");
    printf("║ PanVK-kbase VM/Memory Torture Test       ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    void *lib = dlopen("/data/local/tmp/libvulkan_panfrost.so", RTLD_NOW);
    if (!lib) { printf("FAIL: driver\n"); return 1; }

    PFN_vkGetInstanceProcAddr GPA = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vk_icdGetInstanceProcAddr");
    PFN_vkCreateInstance vkCreateInstance = (PFN_vkCreateInstance)GPA(NULL, "vkCreateInstance");

    VkApplicationInfo ai = {.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName="Torture", .apiVersion=VK_API_VERSION_1_0};
    VkInstanceCreateInfo ci = {.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo=&ai};
    VkInstance inst;
    CHECK(vkCreateInstance(&ci, NULL, &inst));

    PFN_vkEnumeratePhysicalDevices vkEnumPD = (PFN_vkEnumeratePhysicalDevices)GPA(inst, "vkEnumeratePhysicalDevices");
    VkPhysicalDevice pd;
    uint32_t pdCount = 1;
    vkEnumPD(inst, &pdCount, &pd);

    PFN_vkCreateDevice vkCreateDevice = (PFN_vkCreateDevice)GPA(inst, "vkCreateDevice");
    PFN_vkDestroyDevice vkDestroyDevice = (PFN_vkDestroyDevice)GPA(inst, "vkDestroyDevice");
    PFN_vkGetPhysicalDeviceMemoryProperties vkGetMem = (PFN_vkGetPhysicalDeviceMemoryProperties)GPA(inst, "vkGetPhysicalDeviceMemoryProperties");
    PFN_vkAllocateMemory vkAllocMem = (PFN_vkAllocateMemory)GPA(inst, "vkAllocateMemory");
    PFN_vkFreeMemory vkFreeMem = (PFN_vkFreeMemory)GPA(inst, "vkFreeMemory");
    PFN_vkMapMemory vkMapMem = (PFN_vkMapMemory)GPA(inst, "vkMapMemory");
    PFN_vkUnmapMemory vkUnmapMem = (PFN_vkUnmapMemory)GPA(inst, "vkUnmapMemory");
    PFN_vkFlushMappedMemoryRanges vkFlush = (PFN_vkFlushMappedMemoryRanges)GPA(inst, "vkFlushMappedMemoryRanges");
    PFN_vkInvalidateMappedMemoryRanges vkInvalidate = (PFN_vkInvalidateMappedMemoryRanges)GPA(inst, "vkInvalidateMappedMemoryRanges");

    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetMem(pd, &mem_props);
    uint32_t host_visible_type = 0;
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
            host_visible_type = i;
            break;
        }
    }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex=0, .queueCount=1, .pQueuePriorities=&prio};
    VkDeviceCreateInfo dci = {.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount=1, .pQueueCreateInfos=&qci};
    VkDevice dev;
    CHECK(vkCreateDevice(pd, &dci, NULL, &dev));

    printf("[001] SMALL ALLOC/MAP/WRITE/READ x1000\n");
    for (int i = 0; i < 1000; i++) {
        uint32_t size = 64 + (i % 10) * 64;  // 64..640 bytes
        VkMemoryAllocateInfo alloc_info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = size,
            .memoryTypeIndex = host_visible_type,
        };
        VkDeviceMemory mem;
        if (vkAllocMem(dev, &alloc_info, NULL, &mem) != VK_SUCCESS) {
            printf("  FAIL: alloc #%d size=%u\n", i, size);
            return 1;
        }
        alloc_count++;

        void *mapped = NULL;
        if (vkMapMem(dev, mem, 0, size, 0, &mapped) != VK_SUCCESS) {
            printf("  FAIL: map #%d\n", i);
            return 1;
        }
        map_count++;

        memset(mapped, i & 0xFF, size);
        write_count++;

        int ok = 1;
        for (uint32_t j = 0; j < size; j++) {
            if (((unsigned char *)mapped)[j] != (i & 0xFF)) {
                ok = 0;
                break;
            }
        }
        if (!ok) {
            printf("  FAIL: readback #%d\n", i);
            return 1;
        }
        read_count++;

        VkMappedMemoryRange range = {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = mem,
            .offset = 0,
            .size = size,
        };
        vkFlush(dev, 1, &range);
        vkInvalidate(dev, 1, &range);
        flush_count++;
        inval_count++;

        vkUnmapMem(dev, mem);
        vkFreeMem(dev, mem, NULL);
    }
    printf("  OK\n\n");

    printf("[002] LARGE ALLOC x50 (1MB each)\n");
    for (int i = 0; i < 50; i++) {
        VkMemoryAllocateInfo alloc_info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = 1024 * 1024,
            .memoryTypeIndex = host_visible_type,
        };
        VkDeviceMemory mem;
        if (vkAllocMem(dev, &alloc_info, NULL, &mem) != VK_SUCCESS) {
            printf("  FAIL: large alloc #%d\n", i);
            return 1;
        }
        alloc_count++;
        void *mapped;
        vkMapMem(dev, mem, 0, 1024 * 1024, 0, &mapped);
        map_count++;
        memset(mapped, i, 1024 * 1024);
        write_count++;
        vkUnmapMem(dev, mem);
        vkFreeMem(dev, mem, NULL);
    }
    printf("  OK\n\n");

    printf("[003] MIXED SIZES x200\n");
    for (int i = 0; i < 200; i++) {
        uint32_t size = (i % 5 == 0) ? 4096 : ((i % 3 == 0) ? 16384 : 65536);
        VkMemoryAllocateInfo alloc_info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = size,
            .memoryTypeIndex = host_visible_type,
        };
        VkDeviceMemory mem;
        if (vkAllocMem(dev, &alloc_info, NULL, &mem) != VK_SUCCESS) {
            printf("  FAIL: mixed #%d size=%u\n", i, size);
            return 1;
        }
        alloc_count++;
        void *mapped;
        vkMapMem(dev, mem, 0, size, 0, &mapped);
        map_count++;
        memset(mapped, 0xAA, size);
        write_count++;
        vkUnmapMem(dev, mem);
        vkFreeMem(dev, mem, NULL);
    }
    printf("  OK\n\n");

    printf("[FINAL] VM TORTURE PASS\n");
    printf("  Allocations:  %d\n", alloc_count);
    printf("  Maps:         %d\n", map_count);
    printf("  Writes:       %d\n", write_count);
    printf("  Reads:        %d\n", read_count);
    printf("  Flushes:      %d\n", flush_count);
    printf("  Invalidates:  %d\n", inval_count);

    vkDestroyDevice(dev, NULL);
    return 0;
}
