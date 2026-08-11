#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <signal.h>
#include <unistd.h>

static PFN_vkGetInstanceProcAddr GPA;

int main() {
    // Ignorar crashes externos (ya estamos blindados)
    signal(SIGSEGV, SIG_IGN);
    signal(SIGBUS, SIG_IGN);
    signal(SIGABRT, SIG_IGN);

    void *lib = dlopen("/data/local/tmp/libvulkan_panfrost.so", RTLD_NOW);
    if (!lib) { printf("FATAL: dlopen failed\n"); return 1; }
    GPA = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vk_icdGetInstanceProcAddr");

    // --- 1. Crear Instancia ---
    PFN_vkCreateInstance vkCreateInstance = (PFN_vkCreateInstance)GPA(NULL, "vkCreateInstance");
    VkApplicationInfo ai = {.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="final_test",.apiVersion=VK_API_VERSION_1_0};
    VkInstanceCreateInfo ci = {.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&ai};
    VkInstance inst;
    vkCreateInstance(&ci, NULL, &inst);

    // --- 2. Enumerar Dispositivos Físicos ---
    PFN_vkEnumeratePhysicalDevices vkEnumPD = (PFN_vkEnumeratePhysicalDevices)GPA(inst, "vkEnumeratePhysicalDevices");
    VkPhysicalDevice pd;
    uint32_t pdCount = 1;
    vkEnumPD(inst, &pdCount, &pd);

    // --- 3. Crear Dispositivo Lógico (LA PRUEBA DE FUEGO) ---
    PFN_vkCreateDevice vkCreateDevice = (PFN_vkCreateDevice)GPA(inst, "vkCreateDevice");
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=0,.queueCount=1,.pQueuePriorities=&prio};
    VkPhysicalDeviceFeatures feats = {0};
    VkDeviceCreateInfo dci = {.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qci,.pEnabledFeatures=&feats};

    VkDevice dev;
    VkResult result = vkCreateDevice(pd, &dci, NULL, &dev);
    
    printf("\n=== TEST COMPLETED ===\n");
    printf("vkCreateDevice returned: %d\n", result);
    if (result == VK_SUCCESS) {
        printf("STATUS: SUCCESS (Device Created)\n");
    } else if (result == VK_ERROR_INITIALIZATION_FAILED) {
        printf("STATUS: INITIALIZATION FAILED (-3)\n");
    }

    // Salir limpiamente para evitar cualquier bucle de crash en la limpieza
    _exit(0);
}
