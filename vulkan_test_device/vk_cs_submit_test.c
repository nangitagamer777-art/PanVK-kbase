#define _GNU_SOURCE
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#define CHECK(expr) do { if ((expr) != VK_SUCCESS) { fprintf(stderr, "FAIL: %s\n", #expr); return 1; } } while(0)

int main() {
    printf("╔══════════════════════════════════════════╗\n");
    printf("║  PanVK-kbase CS Submit Test (No Render)  ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    void *lib = dlopen("/data/local/tmp/libvulkan_panfrost.so", RTLD_NOW);
    if (!lib) { printf("FAIL: driver\n"); return 1; }

    PFN_vkGetInstanceProcAddr GPA = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vk_icdGetInstanceProcAddr");
    PFN_vkCreateInstance vkCreateInstance = (PFN_vkCreateInstance)GPA(NULL, "vkCreateInstance");

    VkApplicationInfo ai = {.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName="CS Submit", .apiVersion=VK_API_VERSION_1_0};
    VkInstanceCreateInfo ci = {.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo=&ai};
    VkInstance inst;
    CHECK(vkCreateInstance(&ci, NULL, &inst));

    PFN_vkEnumeratePhysicalDevices vkEnumPD = (PFN_vkEnumeratePhysicalDevices)GPA(inst, "vkEnumeratePhysicalDevices");
    VkPhysicalDevice pd;
    uint32_t pdCount = 1;
    vkEnumPD(inst, &pdCount, &pd);

    PFN_vkCreateDevice vkCreateDevice = (PFN_vkCreateDevice)GPA(inst, "vkCreateDevice");
    PFN_vkDestroyDevice vkDestroyDevice = (PFN_vkDestroyDevice)GPA(inst, "vkDestroyDevice");
    PFN_vkGetDeviceQueue vkGetQueue = (PFN_vkGetDeviceQueue)GPA(inst, "vkGetDeviceQueue");
    PFN_vkCreateCommandPool vkCreatePool = (PFN_vkCreateCommandPool)GPA(inst, "vkCreateCommandPool");
    PFN_vkAllocateCommandBuffers vkAllocCmd = (PFN_vkAllocateCommandBuffers)GPA(inst, "vkAllocateCommandBuffers");
    PFN_vkBeginCommandBuffer vkBegin = (PFN_vkBeginCommandBuffer)GPA(inst, "vkBeginCommandBuffer");
    PFN_vkEndCommandBuffer vkEnd = (PFN_vkEndCommandBuffer)GPA(inst, "vkEndCommandBuffer");
    PFN_vkQueueSubmit vkSubmit = (PFN_vkQueueSubmit)GPA(inst, "vkQueueSubmit");
    PFN_vkQueueWaitIdle vkWait = (PFN_vkQueueWaitIdle)GPA(inst, "vkQueueWaitIdle");
    PFN_vkCreateFence vkCreateFence = (PFN_vkCreateFence)GPA(inst, "vkCreateFence");
    PFN_vkDestroyFence vkDestroyFence = (PFN_vkDestroyFence)GPA(inst, "vkDestroyFence");
    PFN_vkWaitForFences vkWaitFence = (PFN_vkWaitForFences)GPA(inst, "vkWaitForFences");

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex=0, .queueCount=1, .pQueuePriorities=&prio};
    VkDeviceCreateInfo dci = {.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount=1, .pQueueCreateInfos=&qci};
    VkDevice dev;
    CHECK(vkCreateDevice(pd, &dci, NULL, &dev));

    VkQueue queue;
    vkGetQueue(dev, 0, 0, &queue);

    printf("[001] COMMAND POOL\n");
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0,
    };
    VkCommandPool pool;
    CHECK(vkCreatePool(dev, &pool_info, NULL, &pool));
    printf("  OK\n\n");

    printf("[002] COMMAND BUFFER ALLOC x3\n");
    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .commandBufferCount = 3,
    };
    VkCommandBuffer cmds[3];
    CHECK(vkAllocCmd(dev, &alloc_info, cmds));
    printf("  OK\n\n");

    printf("[003] RECORD + SUBMIT x10\n");
    for (int i = 0; i < 10; i++) {
        VkCommandBufferBeginInfo begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        };
        CHECK(vkBegin(cmds[0], &begin_info));
        CHECK(vkEnd(cmds[0]));

        VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence;
        CHECK(vkCreateFence(dev, &fence_info, NULL, &fence));

        VkSubmitInfo submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmds[0],
        };
        CHECK(vkSubmit(queue, 1, &submit_info, fence));
        CHECK(vkWaitFence(dev, 1, &fence, VK_TRUE, UINT64_MAX));

        vkDestroyFence(dev, fence, NULL);
        printf("  [%02d] OK\n", i);
    }
    printf("\n");

    printf("[004] MULTI-SUBMIT x5 (3 cmd buffers)\n");
    for (int i = 0; i < 5; i++) {
        VkCommandBufferBeginInfo begin_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        for (int c = 0; c < 3; c++) {
            CHECK(vkBegin(cmds[c], &begin_info));
            CHECK(vkEnd(cmds[c]));
        }

        VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence;
        CHECK(vkCreateFence(dev, &fence_info, NULL, &fence));

        VkSubmitInfo submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 3,
            .pCommandBuffers = cmds,
        };
        CHECK(vkSubmit(queue, 1, &submit_info, fence));
        CHECK(vkWaitFence(dev, 1, &fence, VK_TRUE, UINT64_MAX));

        vkDestroyFence(dev, fence, NULL);
        printf("  [%02d] OK\n", i);
    }
    printf("\n");

    printf("[005] QUEUE WAIT IDLE\n");
    CHECK(vkWait(queue));
    printf("  OK\n\n");

    printf("[FINAL] CS SUBMIT PASS\n");
    vkDestroyDevice(dev, NULL);
    return 0;
}
