/*
 * kbase_adreno_tools.c
 * Ganchos de compatibilidad para emuladores (Winlator, etc.)
 * No modifica el driver — solo añade spoofing de propiedades.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "vk_physical_device.h"

/* Prototipos */
void kbase_adreno_spoof(VkPhysicalDeviceProperties *props);
void kbase_adreno_hook_get_props(VkPhysicalDevice physicalDevice,
                                                        VkPhysicalDeviceProperties *props);
void kbase_adreno_hook_get_props2(VkPhysicalDevice physicalDevice,
                                                         VkPhysicalDeviceProperties2 *props);

/* Spoofing de propiedades Adreno para emuladores */
VKAPI_ATTR void VKAPI_CALL
kbase_adreno_spoof(VkPhysicalDeviceProperties *props)
{
   if (!props) return;

   /* Solo spoofear si el emulador lo pide */
   const char *env = getenv("KBASE_ADRENO_SPOOF");
   if (!env || !strcmp(env, "0")) return;

   props->vendorID = 0x5143;           /* Qualcomm */
   props->deviceID = 0x07000002;       /* Adreno 740 */
   snprintf(props->deviceName, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE,
            "Adreno (TM) 740");
   fprintf(stderr, "[KBASE_ADRENO] Spoofed: vendor=0x5143 device=Adreno 740\n");
}

/* Hook para vkGetPhysicalDeviceProperties */
VKAPI_ATTR void VKAPI_CALL
kbase_adreno_hook_get_props(VkPhysicalDevice physicalDevice,
                             VkPhysicalDeviceProperties *props)
{
   if (props) {
      kbase_adreno_spoof(props);
   }
}

/* Hook para vkGetPhysicalDeviceProperties2 */
VKAPI_ATTR void VKAPI_CALL
kbase_adreno_hook_get_props2(VkPhysicalDevice physicalDevice,
                              VkPhysicalDeviceProperties2 *props)
{
   if (props) {
      kbase_adreno_spoof(&props->properties);
   }
}
