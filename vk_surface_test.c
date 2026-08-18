#include <vulkan/vulkan.h>
#include <android/native_window.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    printf("Surface Test\n");
    
    VkApplicationInfo ai = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                            .pApplicationName = "Surface",
                            .apiVersion = VK_API_VERSION_1_0};
    VkInstanceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                .pApplicationInfo = &ai};
    VkInstance inst;
    if (vkCreateInstance(&ci, NULL, &inst) != VK_SUCCESS) {
        printf("FAIL: instance\n");
        return 1;
    }
    
    uint32_t ext_count = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &ext_count, NULL);
    VkExtensionProperties *exts = malloc(sizeof(VkExtensionProperties) * ext_count);
    vkEnumerateInstanceExtensionProperties(NULL, &ext_count, exts);
    
    printf("Extensiones disponibles: %u\n", ext_count);
    for (uint32_t i = 0; i < ext_count; i++) {
        if (strstr(exts[i].extensionName, "surface") ||
            strstr(exts[i].extensionName, "KHR_android")) {
            printf("  %s\n", exts[i].extensionName);
        }
    }
    
    free(exts);
    vkDestroyInstance(inst, NULL);
    return 0;
}
