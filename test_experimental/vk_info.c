#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <dlfcn.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>

static PFN_vkGetInstanceProcAddr GPA;

int main() {
    signal(SIGSEGV, SIG_IGN);
    signal(SIGABRT, SIG_IGN);
    
    dlerror();
    void *lib = dlopen("/data/local/tmp/libvulkan_panfrost.so", RTLD_NOW);
    if (!lib) { printf("dlopen failed: %s\n", dlerror()); return 1; }
    GPA = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vk_icdGetInstanceProcAddr");
    if (!GPA) { printf("no GPA\n"); return 1; }
    
    PFN_vkCreateInstance vkCreateInstance = (PFN_vkCreateInstance)GPA(NULL, "vkCreateInstance");
    VkApplicationInfo appInfo = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "vk_info", .apiVersion = VK_API_VERSION_1_0 };
    VkInstanceCreateInfo createInfo = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &appInfo };
    VkInstance instance;
    if (vkCreateInstance(&createInfo, NULL, &instance) != VK_SUCCESS) { printf("Instance failed\n"); return 1; }
    
    PFN_vkEnumeratePhysicalDevices vkEnumerate = (PFN_vkEnumeratePhysicalDevices)GPA(instance, "vkEnumeratePhysicalDevices");
    uint32_t count = 0;
    vkEnumerate(instance, &count, NULL);
    printf("Physical devices: %u\n", count);
    
    if (count > 0) {
        VkPhysicalDevice devs[4];
        vkEnumerate(instance, &count, devs);
        
        /* Try to get properties safely */
        PFN_vkGetPhysicalDeviceProperties vkGetProps = (PFN_vkGetPhysicalDeviceProperties)GPA(instance, "vkGetPhysicalDeviceProperties");
        PFN_vkGetPhysicalDeviceFeatures vkGetFeat = (PFN_vkGetPhysicalDeviceFeatures)GPA(instance, "vkGetPhysicalDeviceFeatures");
        PFN_vkEnumerateDeviceExtensionProperties vkEnumExt = (PFN_vkEnumerateDeviceExtensionProperties)GPA(instance, "vkEnumerateDeviceExtensionProperties");
        
        printf("\n=== GPU Information ===\n");
        
        /* Properties */
        if (vkGetProps) {
            VkPhysicalDeviceProperties props;
            memset(&props, 0, sizeof(props));
            vkGetProps(devs[0], &props);
            printf("GPU Name: %s\n", props.deviceName);
            printf("API Version: %d.%d.%d\n",
                   VK_VERSION_MAJOR(props.apiVersion),
                   VK_VERSION_MINOR(props.apiVersion),
                   VK_VERSION_PATCH(props.apiVersion));
            printf("Driver Version: %d.%d.%d\n",
                   VK_VERSION_MAJOR(props.driverVersion),
                   VK_VERSION_MINOR(props.driverVersion),
                   VK_VERSION_PATCH(props.driverVersion));
            printf("Vendor ID: 0x%x\n", props.vendorID);
            printf("Device ID: 0x%x\n", props.deviceID);
            printf("Device Type: %d\n", props.deviceType);
        } else {
            printf("vkGetPhysicalDeviceProperties not available\n");
        }
        
        /* Features */
        if (vkGetFeat) {
            VkPhysicalDeviceFeatures feats;
            memset(&feats, 0, sizeof(feats));
            vkGetFeat(devs[0], &feats);
            printf("\n=== Features ===\n");
            printf("Geometry Shader: %d\n", feats.geometryShader);
            printf("Tessellation: %d\n", feats.tessellationShader);
            printf("Compute Shader: %d\n", feats.logicOp);
        }
        
        /* Extensions */
        if (vkEnumExt) {
            uint32_t extCount = 0;
            vkEnumExt(devs[0], NULL, &extCount, NULL);
            printf("\n=== Extensions (%u) ===\n", extCount);
            if (extCount > 0) {
                VkExtensionProperties *exts = calloc(extCount, sizeof(*exts));
                if (exts) {
                    vkEnumExt(devs[0], NULL, &extCount, exts);
                    for (uint32_t i = 0; i < extCount && i < 10; i++)
                        printf("  %s\n", exts[i].extensionName);
                    if (extCount > 10) printf("  ... and %u more\n", extCount - 10);
                    free(exts);
                }
            }
        }
    }
    
    dlclose(lib);
    return 0;
}
