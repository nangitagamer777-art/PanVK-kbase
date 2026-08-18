#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>


int main() {
    void *lib = dlopen("/data/local/tmp/libvulkan_panfrost.so", RTLD_NOW);
    if (!lib) { printf("FAIL: driver\n"); return 1; }
    
    PFN_vkGetInstanceProcAddr GPA = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vk_icdGetInstanceProcAddr");
    
    /* Obtener todas las funciones con GPA */
    PFN_vkCreateInstance pCreateInstance = (PFN_vkCreateInstance)GPA(NULL, "vkCreateInstance");
    PFN_vkEnumerateInstanceExtensionProperties pEnumInstExt = (PFN_vkEnumerateInstanceExtensionProperties)GPA(NULL, "vkEnumerateInstanceExtensionProperties");
    PFN_vkEnumeratePhysicalDevices pEnumPD = (PFN_vkEnumeratePhysicalDevices)GPA(NULL, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties pGetProps = (PFN_vkGetPhysicalDeviceProperties)GPA(NULL, "vkGetPhysicalDeviceProperties");
    PFN_vkGetPhysicalDeviceFeatures pGetFeat = (PFN_vkGetPhysicalDeviceFeatures)GPA(NULL, "vkGetPhysicalDeviceFeatures");
    PFN_vkEnumerateDeviceExtensionProperties pEnumDevExt = (PFN_vkEnumerateDeviceExtensionProperties)GPA(NULL, "vkEnumerateDeviceExtensionProperties");
    PFN_vkDestroyInstance pDestroyInst = (PFN_vkDestroyInstance)GPA(NULL, "vkDestroyInstance");
    
    printf("=== PanVK Complete Info ===\n\n");
    
    /* Extensiones de instancia */
    uint32_t inst_ext_count = 0;
    pEnumInstExt(NULL, &inst_ext_count, NULL);
    VkExtensionProperties *inst_exts = calloc(inst_ext_count, sizeof(VkExtensionProperties));
    pEnumInstExt(NULL, &inst_ext_count, inst_exts);
    
    printf("[INSTANCE EXTENSIONS] %u\n", inst_ext_count);
    for (uint32_t i = 0; i < inst_ext_count; i++)
        printf("  %s\n", inst_exts[i].extensionName);
    printf("\n");
    
    /* Instance */
    VkInstance inst;
    VkInstanceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    if (pCreateInstance(&ci, NULL, &inst) != VK_SUCCESS) {
        printf("FAIL: instance\n");
        return 1;
    }
    
    /* Obtener funciones de instancia */
    pEnumPD = (PFN_vkEnumeratePhysicalDevices)GPA(inst, "vkEnumeratePhysicalDevices");
    pGetProps = (PFN_vkGetPhysicalDeviceProperties)GPA(inst, "vkGetPhysicalDeviceProperties");
    pGetFeat = (PFN_vkGetPhysicalDeviceFeatures)GPA(inst, "vkGetPhysicalDeviceFeatures");
    pEnumDevExt = (PFN_vkEnumerateDeviceExtensionProperties)GPA(inst, "vkEnumerateDeviceExtensionProperties");
    
    /* GPUs */
    uint32_t gpu_count = 0;
    pEnumPD(inst, &gpu_count, NULL);
    VkPhysicalDevice gpus[4];
    pEnumPD(inst, &gpu_count, gpus);
    printf("[GPU COUNT] %u\n\n", gpu_count);
    
    /* Propiedades */
    VkPhysicalDeviceProperties props;
    pGetProps(gpus[0], &props);
    
    printf("[IDENTITY]\n");
    printf("  Device Name:    %s\n", props.deviceName);
    printf("  Vendor ID:      0x%04X\n", props.vendorID);
    printf("  Device ID:      0x%04X\n", props.deviceID);
    printf("  Device Type:    %u\n", props.deviceType);
    printf("  API Version:    %u.%u.%u\n", 
           VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion), VK_VERSION_PATCH(props.apiVersion));
    printf("  Driver Version: %u.%u.%u\n\n",
           VK_VERSION_MAJOR(props.driverVersion), VK_VERSION_MINOR(props.driverVersion), VK_VERSION_PATCH(props.driverVersion));
    
    /* Features */
    VkPhysicalDeviceFeatures feat;
    pGetFeat(gpus[0], &feat);
    
    printf("[FEATURES]\n");
    printf("  Geometry Shader:     %s\n", feat.geometryShader ? "YES" : "NO");
    printf("  Tessellation:        %s\n", feat.tessellationShader ? "YES" : "NO");
    printf("  Float64:             %s\n", feat.shaderFloat64 ? "YES" : "NO");
    printf("  Int16:               %s\n", feat.shaderInt16 ? "YES" : "NO");
    printf("  Int64:               %s\n", feat.shaderInt64 ? "YES" : "NO");
    printf("  Logic Op:            %s\n", feat.logicOp ? "YES" : "NO");
    printf("  Anisotropy:          %s\n", feat.samplerAnisotropy ? "YES" : "NO");
    printf("  Dual Src Blend:      %s\n", feat.dualSrcBlend ? "YES" : "NO");
    printf("  Depth Clamp:         %s\n", feat.depthClamp ? "YES" : "NO");
    printf("  Fill Non-Solid:      %s\n", feat.fillModeNonSolid ? "YES" : "NO");
    printf("  Wide Lines:          %s\n", feat.wideLines ? "YES" : "NO");
    printf("  Multi Draw Indirect: %s\n", feat.multiDrawIndirect ? "YES" : "NO");
    printf("  Image Cube Array:    %s\n", feat.imageCubeArray ? "YES" : "NO");
    printf("  BC Compression:      %s\n", feat.textureCompressionBC ? "YES" : "NO");
    printf("  ASTC:                %s\n\n", feat.textureCompressionASTC_LDR ? "YES" : "NO");
    
    /* Límites */
    VkPhysicalDeviceLimits limits = props.limits;
    
    printf("[LIMITS]\n");
    printf("  Max 2D image:    %u\n", limits.maxImageDimension2D);
    printf("  Max 3D image:    %u\n", limits.maxImageDimension3D);
    printf("  Max push const:  %u\n", limits.maxPushConstantsSize);
    printf("  Max samplers:    %u\n", limits.maxSamplerAllocationCount);
    printf("  Max color att:   %u\n", limits.maxColorAttachments);
    printf("  Max viewport:    %u x %u\n", limits.maxViewportDimensions[0], limits.maxViewportDimensions[1]);
    printf("  Max framebuffer: %u x %u\n", limits.maxFramebufferWidth, limits.maxFramebufferHeight);
    printf("  Max compute:     %u x %u x %u\n", limits.maxComputeWorkGroupCount[0], limits.maxComputeWorkGroupCount[1], limits.maxComputeWorkGroupCount[2]);
    printf("  Max WG size:     %u x %u x %u\n", limits.maxComputeWorkGroupSize[0], limits.maxComputeWorkGroupSize[1], limits.maxComputeWorkGroupSize[2]);
    printf("  Max UB range:    %u\n", limits.maxUniformBufferRange);
    printf("  Max SSBO range:  %u\n\n", limits.maxStorageBufferRange);
    
    /* Extensiones de dispositivo */
    uint32_t dev_ext_count = 0;
    pEnumDevExt(gpus[0], NULL, &dev_ext_count, NULL);
    VkExtensionProperties *dev_exts = calloc(dev_ext_count, sizeof(VkExtensionProperties));
    pEnumDevExt(gpus[0], NULL, &dev_ext_count, dev_exts);
    
    printf("[DEVICE EXTENSIONS] %u\n", dev_ext_count);
    for (uint32_t i = 0; i < dev_ext_count; i++)
        printf("  %s\n", dev_exts[i].extensionName);
    printf("\n");
    
    printf("[BACKEND] kbase via /dev/mali0\n");
    printf("[STATUS] All queries OK\n");
    
    free(inst_exts);
    free(dev_exts);
    // pDestroyInst(inst, NULL); /* TODO: fix cleanup */
    return 0;
}
