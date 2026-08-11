#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

/* Intercept vkCreateDevice - return a fake success */
VkResult vkCreateDevice(VkPhysicalDevice physicalDevice,
                        const VkDeviceCreateInfo *pCreateInfo,
                        const VkAllocationCallbacks *pAllocator,
                        VkDevice *pDevice) {
    typeof(vkCreateDevice) *real = dlsym(RTLD_NEXT, "vkCreateDevice");
    
    fprintf(stderr, "[kbase_vk] vkCreateDevice intercepted - returning fake success\n");
    
    /* Return a dummy device pointer */
    static int dummy_dev = 0x42;
    *pDevice = (VkDevice)&dummy_dev;
    
    return VK_SUCCESS;
}

/* Intercept vkDestroyDevice - no-op */
void vkDestroyDevice(VkDevice device, const VkAllocationCallbacks *pAllocator) {
    fprintf(stderr, "[kbase_vk] vkDestroyDevice called - ignoring\n");
}

/* Intercept vkGetDeviceQueue - return a dummy queue */
void vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex,
                      uint32_t queueIndex, VkQueue *pQueue) {
    static int dummy_queue = 0x24;
    *pQueue = (VkQueue)&dummy_queue;
    fprintf(stderr, "[kbase_vk] vkGetDeviceQueue - returning fake queue\n");
}
