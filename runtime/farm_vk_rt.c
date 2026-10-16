/* M9 hardware RT: Vulkan AS + VK_KHR_ray_query inside a compute shader.
   Dynamically loads the Vulkan loader. Not a load-time import. OptiX is not used. */
#include "farm_vk_rt.h"
#include "farm_vk_rt_path_spv.h"
#include "farm_rt.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

typedef uint32_t VkFlags;
typedef uint32_t VkBool32;
typedef uint64_t VkDeviceSize;
typedef uint64_t VkDeviceAddress;
typedef uint32_t VkResult;
typedef void* VkInstance;
typedef void* VkPhysicalDevice;
typedef void* VkDevice;
typedef void* VkQueue;
typedef void* VkCommandBuffer;
typedef uint64_t VkBuffer;
typedef uint64_t VkDeviceMemory;
typedef uint64_t VkShaderModule;
typedef uint64_t VkDescriptorSetLayout;
typedef uint64_t VkPipelineLayout;
typedef uint64_t VkPipeline;
typedef uint64_t VkDescriptorPool;
typedef uint64_t VkDescriptorSet;
typedef uint64_t VkCommandPool;
typedef uint64_t VkAccelerationStructureKHR;

#define VK_SUCCESS 0
#define VK_STRUCTURE_TYPE_APPLICATION_INFO 0
#define VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO 1
#define VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO 2
#define VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO 3
#define VK_STRUCTURE_TYPE_SUBMIT_INFO 4
#define VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO 5
#define VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO 12
#define VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO 16
#define VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO 27
#define VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO 29
#define VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO 30
#define VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO 32
#define VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO 33
#define VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO 34
#define VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET 35
#define VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO 39
#define VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO 40
#define VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO 42
#define VK_STRUCTURE_TYPE_MEMORY_BARRIER 46
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 1000059000
#define VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO 1000060000
#define VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR 1000150000
#define VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR 1000150002
#define VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR 1000150004
#define VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR 1000150005
#define VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR 1000150006
#define VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR 1000150007
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR 1000150013
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR 1000150014
#define VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR 1000150017
#define VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR 1000150020
#define VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO 1000244001
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES 1000257000
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR 1000348013
#define VK_QUEUE_COMPUTE_BIT 2
#define VK_BUFFER_USAGE_STORAGE_BUFFER_BIT 0x00000020
#define VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT 0x00020000
#define VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR 0x00080000
#define VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR 0x00100000
#define VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT 0x00000001
#define VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT 0x00000002
#define VK_MEMORY_PROPERTY_HOST_COHERENT_BIT 0x00000004
#define VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT 0x00000002
#define VK_SHARING_MODE_EXCLUSIVE 0
#define VK_DESCRIPTOR_TYPE_STORAGE_BUFFER 7
#define VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR 1000150000
#define VK_SHADER_STAGE_COMPUTE_BIT 0x00000020
#define VK_PIPELINE_BIND_POINT_COMPUTE 1
#define VK_COMMAND_BUFFER_LEVEL_PRIMARY 0
#define VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT 0x00000002
#define VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT 0x00000001
#define VK_GEOMETRY_TYPE_TRIANGLES_KHR 0
#define VK_GEOMETRY_TYPE_INSTANCES_KHR 2
#define VK_GEOMETRY_OPAQUE_BIT_KHR 1
#define VK_INDEX_TYPE_NONE_KHR 1000165000
#define VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR 0
#define VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR 1
#define VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR 0
#define VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR 4
#define VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR 1
#define VK_FORMAT_R32G32B32_SFLOAT 106
#define VK_MAKE_VERSION(maj, min, pat) (((maj)<<22)|((min)<<12)|(pat))
#define VK_API_VERSION_1_2 VK_MAKE_VERSION(1, 2, 0)
#define VK_WHOLE_SIZE (~(VkDeviceSize)0)
#define VK_NULL_HANDLE 0
#define VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT 0x00000800u
#define VK_PIPELINE_STAGE_HOST_BIT 0x00004000u
#define VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR 0x02000000u
#define VK_ACCESS_SHADER_READ_BIT 0x00000020u
#define VK_ACCESS_SHADER_WRITE_BIT 0x00000040u
#define VK_ACCESS_HOST_READ_BIT 0x00002000u
#define VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR 0x00400000u
#define VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR 0x00200000u

typedef struct { uint32_t width, height, depth; } VkExtent3D;
typedef struct {
  uint32_t sType; const void* pNext; uint32_t flags;
  uint32_t queueFamilyIndex; uint32_t queueCount; const float* pQueuePriorities;
} VkDeviceQueueCreateInfo;
typedef struct {
  uint32_t sType; const void* pNext; const char* pApplicationName; uint32_t applicationVersion;
  const char* pEngineName; uint32_t engineVersion; uint32_t apiVersion;
} VkApplicationInfo;
typedef struct {
  uint32_t sType; const void* pNext; uint32_t flags; const VkApplicationInfo* pApplicationInfo;
  uint32_t enabledLayerCount; const char* const* ppEnabledLayerNames;
  uint32_t enabledExtensionCount; const char* const* ppEnabledExtensionNames;
} VkInstanceCreateInfo;
typedef struct {
  uint32_t sType; const void* pNext; uint32_t flags;
  uint32_t queueCreateInfoCount; const VkDeviceQueueCreateInfo* pQueueCreateInfos;
  uint32_t enabledLayerCount; const char* const* ppEnabledLayerNames;
  uint32_t enabledExtensionCount; const char* const* ppEnabledExtensionNames;
  const void* pEnabledFeatures;
} VkDeviceCreateInfo;
typedef struct {
  uint32_t queueFlags; uint32_t queueCount; uint32_t timestampValidBits; VkExtent3D minImageTransferGranularity;
} VkQueueFamilyProperties;
typedef struct {
  uint32_t sType; const void* pNext; uint32_t srcAccessMask; uint32_t dstAccessMask;
} VkMemoryBarrier;
typedef struct {
  uint32_t sType; const void* pNext; uint32_t flags; uint64_t size; uint32_t usage; uint32_t sharingMode;
  uint32_t queueFamilyIndexCount; const uint32_t* pQueueFamilyIndices;
} VkBufferCreateInfo;
typedef struct { VkDeviceSize size; VkDeviceSize alignment; uint32_t memoryTypeBits; } VkMemoryRequirements;
typedef struct {
  uint32_t sType; const void* pNext; uint64_t allocationSize; uint32_t memoryTypeIndex;
} VkMemoryAllocateInfo;
typedef struct { uint32_t propertyFlags; uint32_t heapIndex; } VkMemoryType;
typedef struct { uint64_t size; uint32_t flags; } VkMemoryHeap;
typedef struct {
  uint32_t memoryTypeCount; VkMemoryType memoryTypes[32];
  uint32_t memoryHeapCount; VkMemoryHeap memoryHeaps[16];
} VkPhysicalDeviceMemoryProperties;
typedef struct {
  uint32_t sType; const void* pNext; uint32_t flags; uint64_t codeSize; const uint32_t* pCode;
} VkShaderModuleCreateInfo;
typedef struct {
  uint32_t binding; uint32_t descriptorType; uint32_t descriptorCount; uint32_t stageFlags; const void* pImmutableSamplers;
} VkDescriptorSetLayoutBinding;
typedef struct {
  uint32_t sType; const void* pNext; uint32_t flags; uint32_t bindingCount; const VkDescriptorSetLayoutBinding* pBindings;
} VkDescriptorSetLayoutCreateInfo;
typedef struct {
  uint32_t sType; const void* pNext; uint32_t flags; uint32_t setLayoutCount; const VkDescriptorSetLayout* pSetLayouts;
  uint32_t pushConstantRangeCount; const void* pPushConstantRanges;
} VkPipelineLayoutCreateInfo;
typedef struct {
  uint32_t sType; const void* pNext; uint32_t flags; uint32_t stage; VkShaderModule module; const char* pName; const void* pSpecializationInfo;
} VkPipelineShaderStageCreateInfo;
typedef struct {
  uint32_t sType; const void* pNext; uint32_t flags; VkPipelineShaderStageCreateInfo stage;
  VkPipelineLayout layout; VkPipeline basePipelineHandle; int32_t basePipelineIndex;
} VkComputePipelineCreateInfo;
typedef struct { uint32_t type; uint32_t descriptorCount; } VkDescriptorPoolSize;
typedef struct {
  uint32_t sType; const void* pNext; uint32_t flags; uint32_t maxSets; uint32_t poolSizeCount; const VkDescriptorPoolSize* pPoolSizes;
} VkDescriptorPoolCreateInfo;
typedef struct {
  uint32_t sType; const void* pNext; VkDescriptorPool descriptorPool; uint32_t descriptorSetCount; const VkDescriptorSetLayout* pSetLayouts;
} VkDescriptorSetAllocateInfo;
typedef struct { VkBuffer buffer; VkDeviceSize offset; VkDeviceSize range; } VkDescriptorBufferInfo;
typedef struct {
  uint32_t sType; const void* pNext; VkDescriptorSet dstSet; uint32_t dstBinding; uint32_t dstArrayElement;
  uint32_t descriptorCount; uint32_t descriptorType;
  const void* pImageInfo; const VkDescriptorBufferInfo* pBufferInfo; const void* pTexelBufferView;
} VkWriteDescriptorSet;
typedef struct {
  uint32_t sType; const void* pNext; uint32_t flags; uint32_t queueFamilyIndex;
} VkCommandPoolCreateInfo;
typedef struct {
  uint32_t sType; const void* pNext; VkCommandPool commandPool; uint32_t level; uint32_t commandBufferCount;
} VkCommandBufferAllocateInfo;
typedef struct {
  uint32_t sType; const void* pNext; uint32_t flags; const void* pInheritanceInfo;
} VkCommandBufferBeginInfo;
typedef struct {
  uint32_t sType; const void* pNext; uint32_t waitSemaphoreCount; const void* pWaitSemaphores; const uint32_t* pWaitDstStageMask;
  uint32_t commandBufferCount; const VkCommandBuffer* pCommandBuffers;
  uint32_t signalSemaphoreCount; const void* pSignalSemaphores;
} VkSubmitInfo;
typedef struct { char extensionName[256]; uint32_t specVersion; } VkExtensionProperties;

#define VK_FN(name, ret, args) typedef ret (*PFN_##name) args; static PFN_##name name
VK_FN(vkDestroyInstance, void, (VkInstance, const void*));
VK_FN(vkEnumeratePhysicalDevices, VkResult, (VkInstance, uint32_t*, VkPhysicalDevice*));
VK_FN(vkGetPhysicalDeviceQueueFamilyProperties, void, (VkPhysicalDevice, uint32_t*, VkQueueFamilyProperties*));
VK_FN(vkGetPhysicalDeviceMemoryProperties, void, (VkPhysicalDevice, VkPhysicalDeviceMemoryProperties*));
VK_FN(vkEnumerateDeviceExtensionProperties, VkResult, (VkPhysicalDevice, const char*, uint32_t*, VkExtensionProperties*));
VK_FN(vkGetPhysicalDeviceFeatures2, void, (VkPhysicalDevice, void*));
VK_FN(vkCreateDevice, VkResult, (VkPhysicalDevice, const VkDeviceCreateInfo*, const void*, VkDevice*));
VK_FN(vkDestroyDevice, void, (VkDevice, const void*));
VK_FN(vkGetDeviceQueue, void, (VkDevice, uint32_t, uint32_t, VkQueue*));
VK_FN(vkGetDeviceProcAddr, void*, (VkDevice, const char*));
VK_FN(vkCreateBuffer, VkResult, (VkDevice, const VkBufferCreateInfo*, const void*, VkBuffer*));
VK_FN(vkDestroyBuffer, void, (VkDevice, VkBuffer, const void*));
VK_FN(vkGetBufferMemoryRequirements, void, (VkDevice, VkBuffer, VkMemoryRequirements*));
VK_FN(vkAllocateMemory, VkResult, (VkDevice, const VkMemoryAllocateInfo*, const void*, VkDeviceMemory*));
VK_FN(vkFreeMemory, void, (VkDevice, VkDeviceMemory, const void*));
VK_FN(vkBindBufferMemory, VkResult, (VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize));
VK_FN(vkMapMemory, VkResult, (VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkFlags, void**));
VK_FN(vkUnmapMemory, void, (VkDevice, VkDeviceMemory));
VK_FN(vkCreateShaderModule, VkResult, (VkDevice, const VkShaderModuleCreateInfo*, const void*, VkShaderModule*));
VK_FN(vkDestroyShaderModule, void, (VkDevice, VkShaderModule, const void*));
VK_FN(vkCreateDescriptorSetLayout, VkResult, (VkDevice, const VkDescriptorSetLayoutCreateInfo*, const void*, VkDescriptorSetLayout*));
VK_FN(vkDestroyDescriptorSetLayout, void, (VkDevice, VkDescriptorSetLayout, const void*));
VK_FN(vkCreatePipelineLayout, VkResult, (VkDevice, const VkPipelineLayoutCreateInfo*, const void*, VkPipelineLayout*));
VK_FN(vkDestroyPipelineLayout, void, (VkDevice, VkPipelineLayout, const void*));
VK_FN(vkCreateComputePipelines, VkResult, (VkDevice, uint64_t, uint32_t, const VkComputePipelineCreateInfo*, const void*, VkPipeline*));
VK_FN(vkDestroyPipeline, void, (VkDevice, VkPipeline, const void*));
VK_FN(vkCreateDescriptorPool, VkResult, (VkDevice, const VkDescriptorPoolCreateInfo*, const void*, VkDescriptorPool*));
VK_FN(vkDestroyDescriptorPool, void, (VkDevice, VkDescriptorPool, const void*));
VK_FN(vkAllocateDescriptorSets, VkResult, (VkDevice, const VkDescriptorSetAllocateInfo*, VkDescriptorSet*));
VK_FN(vkUpdateDescriptorSets, void, (VkDevice, uint32_t, const VkWriteDescriptorSet*, uint32_t, const void*));
VK_FN(vkCreateCommandPool, VkResult, (VkDevice, const VkCommandPoolCreateInfo*, const void*, VkCommandPool*));
VK_FN(vkDestroyCommandPool, void, (VkDevice, VkCommandPool, const void*));
VK_FN(vkAllocateCommandBuffers, VkResult, (VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer*));
VK_FN(vkBeginCommandBuffer, VkResult, (VkCommandBuffer, const VkCommandBufferBeginInfo*));
VK_FN(vkEndCommandBuffer, VkResult, (VkCommandBuffer));
VK_FN(vkCmdBindPipeline, void, (VkCommandBuffer, uint32_t, VkPipeline));
VK_FN(vkCmdBindDescriptorSets, void, (VkCommandBuffer, uint32_t, VkPipelineLayout, uint32_t, uint32_t, const VkDescriptorSet*, uint32_t, const uint32_t*));
VK_FN(vkCmdDispatch, void, (VkCommandBuffer, uint32_t, uint32_t, uint32_t));
VK_FN(vkCmdPipelineBarrier, void, (VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t, const VkMemoryBarrier*, uint32_t, const void*, uint32_t, const void*));
VK_FN(vkQueueSubmit, VkResult, (VkQueue, uint32_t, const VkSubmitInfo*, uint64_t));
VK_FN(vkQueueWaitIdle, VkResult, (VkQueue));
VK_FN(vkGetBufferDeviceAddress, VkDeviceAddress, (VkDevice, const void*));
VK_FN(vkCreateAccelerationStructureKHR, VkResult, (VkDevice, const void*, const void*, VkAccelerationStructureKHR*));
VK_FN(vkDestroyAccelerationStructureKHR, void, (VkDevice, VkAccelerationStructureKHR, const void*));
VK_FN(vkGetAccelerationStructureBuildSizesKHR, void, (VkDevice, uint32_t, const void*, const uint32_t*, void*));
VK_FN(vkCmdBuildAccelerationStructuresKHR, void, (VkCommandBuffer, uint32_t, const void*, const void* const*));
VK_FN(vkGetAccelerationStructureDeviceAddressKHR, VkDeviceAddress, (VkDevice, const void*));
#undef VK_FN

static void* g_loader;
typedef void* (*PFN_vkGetInstanceProcAddr)(VkInstance, const char*);
typedef VkResult (*PFN_vkCreateInstance)(const VkInstanceCreateInfo*, const void*, VkInstance*);
static PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr_fn;
static PFN_vkCreateInstance vkCreateInstance_fn;

static VkInstance g_inst;
static VkPhysicalDevice g_phys;
static uint32_t g_qfam;
static uint32_t g_scratch_align = 256;
static int g_probe_done;
static int g_probe_ok;
static VkDevice g_dev;
static VkQueue g_queue;
static VkShaderModule g_shader;
static VkDescriptorSetLayout g_dsl;
static VkPipelineLayout g_pl;
static VkPipeline g_pipe;
static VkCommandPool g_cpool;
static int g_device_ready;

static const char* k_req_ext[] = {
  "VK_KHR_acceleration_structure",
  "VK_KHR_ray_query",
  "VK_KHR_deferred_host_operations",
};

static void* load_sym(const char* name) {
#ifdef _WIN32
  return (void*)GetProcAddress((HMODULE)g_loader, name);
#else
  return dlsym(g_loader, name);
#endif
}

static int load_loader(void) {
  if (g_loader) return 1;
#ifdef _WIN32
  g_loader = (void*)LoadLibraryA("vulkan-1.dll");
#else
  g_loader = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
  if (!g_loader) g_loader = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
#endif
  if (!g_loader) return 0;
  vkGetInstanceProcAddr_fn = (PFN_vkGetInstanceProcAddr)load_sym("vkGetInstanceProcAddr");
  if (!vkGetInstanceProcAddr_fn) return 0;
  vkCreateInstance_fn = (PFN_vkCreateInstance)vkGetInstanceProcAddr_fn(NULL, "vkCreateInstance");
  if (!vkCreateInstance_fn) return 0;
  return 1;
}

static void* inst_proc(const char* name) {
  return vkGetInstanceProcAddr_fn(g_inst, name);
}

static int has_all_exts(VkPhysicalDevice pd) {
  uint32_t n = 0;
  if (vkEnumerateDeviceExtensionProperties(pd, NULL, &n, NULL) != VK_SUCCESS) return 0;
  if (n == 0) return 0;
  VkExtensionProperties* p = (VkExtensionProperties*)malloc(sizeof(VkExtensionProperties) * n);
  if (!p) return 0;
  if (vkEnumerateDeviceExtensionProperties(pd, NULL, &n, p) != VK_SUCCESS) { free(p); return 0; }
  int ok = 1;
  for (size_t i = 0; i < sizeof(k_req_ext) / sizeof(k_req_ext[0]); i++) {
    int found = 0;
    for (uint32_t j = 0; j < n; j++) {
      if (strcmp(p[j].extensionName, k_req_ext[i]) == 0) { found = 1; break; }
    }
    if (!found) { ok = 0; break; }
  }
  free(p);
  return ok;
}

static int device_rt_features(VkPhysicalDevice pd, uint32_t* scratch_align_out) {
  uint8_t as_feat[40];
  uint8_t rq_feat[24];
  uint8_t bda_feat[32];
  uint8_t feats2[240];
  memset(as_feat, 0, sizeof(as_feat));
  memset(rq_feat, 0, sizeof(rq_feat));
  memset(bda_feat, 0, sizeof(bda_feat));
  memset(feats2, 0, sizeof(feats2));
  *(uint32_t*)bda_feat = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
  *(uint32_t*)as_feat = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
  *(const void**)(as_feat + 8) = bda_feat;
  *(uint32_t*)rq_feat = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
  *(const void**)(rq_feat + 8) = as_feat;
  *(uint32_t*)feats2 = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  *(const void**)(feats2 + 8) = rq_feat;
  vkGetPhysicalDeviceFeatures2(pd, feats2);
  VkBool32 bda = *(VkBool32*)(bda_feat + 16);
  VkBool32 as = *(VkBool32*)(as_feat + 16);
  VkBool32 rq = *(VkBool32*)(rq_feat + 16);
  if (!bda || !as || !rq) return 0;
  if (scratch_align_out) *scratch_align_out = 256;
  (void)pd;
  return 1;
}

static int create_instance_and_pick(void) {
  if (g_inst) return 1;
  VkApplicationInfo app;
  memset(&app, 0, sizeof(app));
  app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.pApplicationName = "farmos";
  app.apiVersion = VK_API_VERSION_1_2;
  VkInstanceCreateInfo ci;
  memset(&ci, 0, sizeof(ci));
  ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ci.pApplicationInfo = &app;
  if (vkCreateInstance_fn(&ci, NULL, &g_inst) != VK_SUCCESS || !g_inst) return 0;

#define LOADI(name) do { name = (PFN_##name)inst_proc(#name); if (!name) return 0; } while (0)
  LOADI(vkDestroyInstance);
  LOADI(vkEnumeratePhysicalDevices);
  LOADI(vkGetPhysicalDeviceQueueFamilyProperties);
  LOADI(vkGetPhysicalDeviceMemoryProperties);
  LOADI(vkEnumerateDeviceExtensionProperties);
  LOADI(vkGetPhysicalDeviceFeatures2);
  LOADI(vkCreateDevice);
#undef LOADI

  uint32_t ndev = 0;
  if (vkEnumeratePhysicalDevices(g_inst, &ndev, NULL) != VK_SUCCESS || ndev == 0) return 0;
  VkPhysicalDevice* devs = (VkPhysicalDevice*)malloc(sizeof(VkPhysicalDevice) * ndev);
  if (!devs) return 0;
  if (vkEnumeratePhysicalDevices(g_inst, &ndev, devs) != VK_SUCCESS) { free(devs); return 0; }
  int found = 0;
  for (uint32_t i = 0; i < ndev && !found; i++) {
    if (!has_all_exts(devs[i])) continue;
    uint32_t align = 256;
    if (!device_rt_features(devs[i], &align)) continue;
    uint32_t nq = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &nq, NULL);
    if (!nq) continue;
    VkQueueFamilyProperties* q = (VkQueueFamilyProperties*)malloc(sizeof(VkQueueFamilyProperties) * nq);
    if (!q) continue;
    vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &nq, q);
    for (uint32_t f = 0; f < nq; f++) {
      if (q[f].queueCount > 0 && (q[f].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
        g_phys = devs[i];
        g_qfam = f;
        g_scratch_align = align;
        found = 1;
        break;
      }
    }
    free(q);
  }
  free(devs);
  return found;
}

int farm_vk_rt_probe(void) {
  if (g_probe_done) return g_probe_ok;
  g_probe_done = 1;
  g_probe_ok = 0;
  if (!load_loader()) return 0;
  if (!create_instance_and_pick()) return 0;
  g_probe_ok = 1;
  return 1;
}

static int find_mem_type(uint32_t bits, uint32_t props) {
  VkPhysicalDeviceMemoryProperties mp;
  vkGetPhysicalDeviceMemoryProperties(g_phys, &mp);
  for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
    if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
      return (int)i;
  }
  return -1;
}

typedef struct {
  VkBuffer buf;
  VkDeviceMemory mem;
  void* mapped;
  VkDeviceAddress addr;
  int mapped_ok;
} RtBuf;

static void rt_buf_destroy(RtBuf* b) {
  if (!g_dev) return;
  if (b->mapped_ok && b->mem) vkUnmapMemory(g_dev, b->mem);
  if (b->mem) vkFreeMemory(g_dev, b->mem, NULL);
  if (b->buf) vkDestroyBuffer(g_dev, b->buf, NULL);
  memset(b, 0, sizeof(*b));
}

static int rt_buf_create(VkDeviceSize sz, uint32_t usage, uint32_t mem_props, int want_addr, int map, RtBuf* out) {
  memset(out, 0, sizeof(*out));
  if (sz < 16) sz = 16;
  VkBufferCreateInfo bi;
  memset(&bi, 0, sizeof(bi));
  bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bi.size = sz;
  bi.usage = usage;
  bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(g_dev, &bi, NULL, &out->buf) != VK_SUCCESS) return 0;
  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(g_dev, out->buf, &req);
  int mt = find_mem_type(req.memoryTypeBits, mem_props);
  if (mt < 0 && (mem_props & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
    mt = find_mem_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (mt < 0) return 0;
  uint8_t flags_info[24];
  memset(flags_info, 0, sizeof(flags_info));
  *(uint32_t*)flags_info = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
  *(uint32_t*)(flags_info + 16) = want_addr ? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT : 0;
  VkMemoryAllocateInfo ai;
  memset(&ai, 0, sizeof(ai));
  ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  ai.pNext = want_addr ? flags_info : NULL;
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = (uint32_t)mt;
  if (vkAllocateMemory(g_dev, &ai, NULL, &out->mem) != VK_SUCCESS) return 0;
  if (vkBindBufferMemory(g_dev, out->buf, out->mem, 0) != VK_SUCCESS) return 0;
  if (map) {
    if (vkMapMemory(g_dev, out->mem, 0, VK_WHOLE_SIZE, 0, &out->mapped) != VK_SUCCESS) return 0;
    out->mapped_ok = 1;
  }
  if (want_addr) {
    uint8_t info[24];
    memset(info, 0, sizeof(info));
    *(uint32_t*)info = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    memcpy(info + 16, &out->buf, 8);
    out->addr = vkGetBufferDeviceAddress(g_dev, info);
    if (!out->addr) return 0;
  }
  return 1;
}

static int ensure_rt_device(void) {
  if (g_device_ready) return 1;
  if (!farm_vk_rt_probe()) return 0;
  float prio = 1.0f;
  VkDeviceQueueCreateInfo qci;
  memset(&qci, 0, sizeof(qci));
  qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  qci.queueFamilyIndex = g_qfam;
  qci.queueCount = 1;
  qci.pQueuePriorities = &prio;

  uint8_t bda_feat[32];
  uint8_t as_feat[40];
  uint8_t rq_feat[24];
  memset(bda_feat, 0, sizeof(bda_feat));
  memset(as_feat, 0, sizeof(as_feat));
  memset(rq_feat, 0, sizeof(rq_feat));
  *(uint32_t*)bda_feat = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
  *(VkBool32*)(bda_feat + 16) = 1;
  *(uint32_t*)as_feat = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
  *(const void**)(as_feat + 8) = bda_feat;
  *(VkBool32*)(as_feat + 16) = 1;
  *(uint32_t*)rq_feat = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
  *(const void**)(rq_feat + 8) = as_feat;
  *(VkBool32*)(rq_feat + 16) = 1;

  VkDeviceCreateInfo dci;
  memset(&dci, 0, sizeof(dci));
  dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  dci.pNext = rq_feat;
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  dci.enabledExtensionCount = (uint32_t)(sizeof(k_req_ext) / sizeof(k_req_ext[0]));
  dci.ppEnabledExtensionNames = k_req_ext;
  if (vkCreateDevice(g_phys, &dci, NULL, &g_dev) != VK_SUCCESS || !g_dev) return 0;

  vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)inst_proc("vkGetDeviceProcAddr");
  if (!vkGetDeviceProcAddr) return 0;
#define LOADD(name) do { name = (PFN_##name)vkGetDeviceProcAddr(g_dev, #name); if (!name) return 0; } while (0)
  LOADD(vkDestroyDevice);
  LOADD(vkGetDeviceQueue);
  LOADD(vkCreateBuffer);
  LOADD(vkDestroyBuffer);
  LOADD(vkGetBufferMemoryRequirements);
  LOADD(vkAllocateMemory);
  LOADD(vkFreeMemory);
  LOADD(vkBindBufferMemory);
  LOADD(vkMapMemory);
  LOADD(vkUnmapMemory);
  LOADD(vkCreateShaderModule);
  LOADD(vkDestroyShaderModule);
  LOADD(vkCreateDescriptorSetLayout);
  LOADD(vkDestroyDescriptorSetLayout);
  LOADD(vkCreatePipelineLayout);
  LOADD(vkDestroyPipelineLayout);
  LOADD(vkCreateComputePipelines);
  LOADD(vkDestroyPipeline);
  LOADD(vkCreateDescriptorPool);
  LOADD(vkDestroyDescriptorPool);
  LOADD(vkAllocateDescriptorSets);
  LOADD(vkUpdateDescriptorSets);
  LOADD(vkCreateCommandPool);
  LOADD(vkDestroyCommandPool);
  LOADD(vkAllocateCommandBuffers);
  LOADD(vkBeginCommandBuffer);
  LOADD(vkEndCommandBuffer);
  LOADD(vkCmdBindPipeline);
  LOADD(vkCmdBindDescriptorSets);
  LOADD(vkCmdDispatch);
  LOADD(vkCmdPipelineBarrier);
  LOADD(vkQueueSubmit);
  LOADD(vkQueueWaitIdle);
  LOADD(vkGetBufferDeviceAddress);
  LOADD(vkCreateAccelerationStructureKHR);
  LOADD(vkDestroyAccelerationStructureKHR);
  LOADD(vkGetAccelerationStructureBuildSizesKHR);
  LOADD(vkCmdBuildAccelerationStructuresKHR);
  LOADD(vkGetAccelerationStructureDeviceAddressKHR);
#undef LOADD

  vkGetDeviceQueue(g_dev, g_qfam, 0, &g_queue);

  VkShaderModuleCreateInfo sm;
  memset(&sm, 0, sizeof(sm));
  sm.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  sm.codeSize = farm_vk_rt_path_spv_size;
  sm.pCode = farm_vk_rt_path_spv;
  if (vkCreateShaderModule(g_dev, &sm, NULL, &g_shader) != VK_SUCCESS) return 0;

  VkDescriptorSetLayoutBinding b[7];
  memset(b, 0, sizeof(b));
  for (int i = 0; i < 6; i++) {
    b[i].binding = (uint32_t)i;
    b[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    b[i].descriptorCount = 1;
    b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  }
  b[6].binding = 6;
  b[6].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
  b[6].descriptorCount = 1;
  b[6].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  VkDescriptorSetLayoutCreateInfo dl;
  memset(&dl, 0, sizeof(dl));
  dl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  dl.bindingCount = 7;
  dl.pBindings = b;
  if (vkCreateDescriptorSetLayout(g_dev, &dl, NULL, &g_dsl) != VK_SUCCESS) return 0;

  VkPipelineLayoutCreateInfo plc;
  memset(&plc, 0, sizeof(plc));
  plc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  plc.setLayoutCount = 1;
  plc.pSetLayouts = &g_dsl;
  if (vkCreatePipelineLayout(g_dev, &plc, NULL, &g_pl) != VK_SUCCESS) return 0;

  VkComputePipelineCreateInfo pci;
  memset(&pci, 0, sizeof(pci));
  pci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  pci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  pci.stage.module = g_shader;
  pci.stage.pName = "main";
  pci.layout = g_pl;
  pci.basePipelineIndex = -1;
  if (vkCreateComputePipelines(g_dev, VK_NULL_HANDLE, 1, &pci, NULL, &g_pipe) != VK_SUCCESS) return 0;

  VkCommandPoolCreateInfo cpi;
  memset(&cpi, 0, sizeof(cpi));
  cpi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  cpi.queueFamilyIndex = g_qfam;
  if (vkCreateCommandPool(g_dev, &cpi, NULL, &g_cpool) != VK_SUCCESS) return 0;

  g_device_ready = 1;
  return 1;
}

typedef struct {
  float v0[4], v1[4], v2[4], uv01[4], uv2[4];
} GpuTriHost;

typedef struct {
  const double* mw;
  farm_Mesh* mesh;
  farm_GeometryData* geo;
  int32_t mesh_index;
} InstAcc;

typedef struct { farm_Color color; double intensity; } AmbAcc;
typedef struct { double tx, ty, tz, ler, leg, leb; } DirAcc;
typedef struct { double px, py, pz, distance, decay, ler, leg, leb; } PtAcc;
typedef struct { const double* mw; double width, height, ler, leg, leb; } RectAcc;

static void xform_p(const double* e, double x, double y, double z, double* ox, double* oy, double* oz) {
  *ox = e[0]*x + e[4]*y + e[8]*z + e[12];
  *oy = e[1]*x + e[5]*y + e[9]*z + e[13];
  *oz = e[2]*x + e[6]*y + e[10]*z + e[14];
}

static void grow(void** p, int32_t* cap, int32_t n, size_t elem) {
  if (n <= *cap) return;
  int32_t nc = *cap ? *cap * 2 : 8;
  while (nc < n) nc *= 2;
  void* np = malloc((size_t)nc * elem);
  if (!np) return;
  if (*p && *cap) memcpy(np, *p, (size_t)(*cap) * elem);
  else if (np) memset(np, 0, (size_t)nc * elem);
  free(*p);
  *p = np;
  *cap = nc;
}

static void collect(farm_Object3D* obj, InstAcc** inst, int32_t* ninst, int32_t* cap_i, int32_t* mesh_i,
                    AmbAcc** amb, int32_t* namb, int32_t* cap_a,
                    DirAcc** dirs, int32_t* ndir, int32_t* cap_d,
                    PtAcc** pts, int32_t* npt, int32_t* cap_p,
                    RectAcc** rects, int32_t* nrect, int32_t* cap_r) {
  if (!obj || !obj->f_visible) return;
  if (obj->type == FARM_OBJECT3D_TYPE_MESH) {
    farm_Mesh* mesh = (farm_Mesh*)obj;
    grow((void**)inst, cap_i, *ninst + 1, sizeof(InstAcc));
    InstAcc* in = &(*inst)[(*ninst)++];
    in->mesh = mesh;
    in->geo = farm_mesh_geometry_data(mesh);
    in->mw = mesh->f_matrixWorld.elements;
    in->mesh_index = (*mesh_i)++;
  } else if (obj->type == FARM_OBJECT3D_TYPE_AMBIENT_LIGHT) {
    farm_AmbientLight* L = (farm_AmbientLight*)obj;
    grow((void**)amb, cap_a, *namb + 1, sizeof(AmbAcc));
    (*amb)[(*namb)++] = (AmbAcc){L->f_color, L->f_intensity};
  } else if (obj->type == FARM_OBJECT3D_TYPE_DIRECTIONAL_LIGHT) {
    farm_DirectionalLight* L = (farm_DirectionalLight*)obj;
    const double* e = L->f_matrixWorld.elements;
    double dx = e[0]*0 + e[4]*0 + e[8]*(-1);
    double dy = e[1]*0 + e[5]*0 + e[9]*(-1);
    double dz = e[2]*0 + e[6]*0 + e[10]*(-1);
    double len = sqrt(dx*dx+dy*dy+dz*dz);
    if (len == 0.0) { dx = 0; dy = 0; dz = -1; len = 1; }
    dx /= len; dy /= len; dz /= len;
    grow((void**)dirs, cap_d, *ndir + 1, sizeof(DirAcc));
    (*dirs)[(*ndir)++] = (DirAcc){dx, dy, dz,
      L->f_color.r*L->f_intensity, L->f_color.g*L->f_intensity, L->f_color.b*L->f_intensity};
  } else if (obj->type == FARM_OBJECT3D_TYPE_POINT_LIGHT) {
    farm_PointLight* L = (farm_PointLight*)obj;
    const double* e = L->f_matrixWorld.elements;
    grow((void**)pts, cap_p, *npt + 1, sizeof(PtAcc));
    (*pts)[(*npt)++] = (PtAcc){e[12], e[13], e[14], L->f_distance, L->f_decay,
      L->f_color.r*L->f_intensity, L->f_color.g*L->f_intensity, L->f_color.b*L->f_intensity};
  } else if (obj->type == FARM_OBJECT3D_TYPE_RECT_AREA_LIGHT) {
    farm_RectAreaLight* L = (farm_RectAreaLight*)obj;
    grow((void**)rects, cap_r, *nrect + 1, sizeof(RectAcc));
    (*rects)[(*nrect)++] = (RectAcc){L->f_matrixWorld.elements, L->width, L->height,
      L->f_color.r*L->f_intensity, L->f_color.g*L->f_intensity, L->f_color.b*L->f_intensity};
  }
  for (int32_t i = 0; i < obj->children.count; i++)
    collect(obj->children.items[i], inst, ninst, cap_i, mesh_i, amb, namb, cap_a,
            dirs, ndir, cap_d, pts, npt, cap_p, rects, nrect, cap_r);
}

static int quantize_f(float c) {
  if (c < 0.f) c = 0.f;
  if (c > 1.f) c = 1.f;
  return (int)floor((double)c * 255.0 + 0.5);
}

static VkDeviceSize align_up(VkDeviceSize v, VkDeviceSize a) {
  if (a <= 1) return v;
  return (v + a - 1) / a * a;
}

static int fill_tri_geom(uint8_t* geom96, VkDeviceAddress vtx_addr, uint32_t nverts) {
  memset(geom96, 0, 96);
  *(uint32_t*)geom96 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
  *(uint32_t*)(geom96 + 16) = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
  uint8_t* tri = geom96 + 24;
  *(uint32_t*)tri = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
  *(uint32_t*)(tri + 16) = VK_FORMAT_R32G32B32_SFLOAT;
  memcpy(tri + 24, &vtx_addr, 8);
  *(uint64_t*)(tri + 32) = 12;
  *(uint32_t*)(tri + 40) = nverts ? nverts - 1 : 0;
  *(uint32_t*)(tri + 44) = VK_INDEX_TYPE_NONE_KHR;
  *(uint32_t*)(geom96 + 88) = VK_GEOMETRY_OPAQUE_BIT_KHR;
  return 1;
}

static int fill_inst_geom(uint8_t* geom96, VkDeviceAddress inst_addr) {
  memset(geom96, 0, 96);
  *(uint32_t*)geom96 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
  *(uint32_t*)(geom96 + 16) = VK_GEOMETRY_TYPE_INSTANCES_KHR;
  uint8_t* inst = geom96 + 24;
  *(uint32_t*)inst = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
  memcpy(inst + 24, &inst_addr, 8);
  *(uint32_t*)(geom96 + 88) = VK_GEOMETRY_OPAQUE_BIT_KHR;
  return 1;
}

static void fill_build_info(uint8_t* inf80, uint32_t type, VkAccelerationStructureKHR dst,
                            uint32_t geom_count, const void* pgeoms, VkDeviceAddress scratch) {
  memset(inf80, 0, 80);
  *(uint32_t*)inf80 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
  *(uint32_t*)(inf80 + 16) = type;
  *(uint32_t*)(inf80 + 20) = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
  *(uint32_t*)(inf80 + 24) = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
  memcpy(inf80 + 40, &dst, 8);
  *(uint32_t*)(inf80 + 48) = geom_count;
  memcpy(inf80 + 56, &pgeoms, sizeof(void*));
  memcpy(inf80 + 72, &scratch, 8);
}

int farm_vk_rt_render_path(farm_Renderer* r, farm_Scene* scene, farm_PerspectiveCamera* camera) {
  if (!ensure_rt_device()) return 0;

  InstAcc* inst = NULL; int32_t ninst = 0, cap_i = 0, mesh_i = 0;
  AmbAcc* amb = NULL; int32_t namb = 0, cap_a = 0;
  DirAcc* dirs = NULL; int32_t ndir = 0, cap_d = 0;
  PtAcc* pts = NULL; int32_t npt = 0, cap_p = 0;
  RectAcc* rects = NULL; int32_t nrect = 0, cap_r = 0;
  collect((farm_Object3D*)scene, &inst, &ninst, &cap_i, &mesh_i, &amb, &namb, &cap_a,
          &dirs, &ndir, &cap_d, &pts, &npt, &cap_p, &rects, &nrect, &cap_r);

  int32_t ntri = 0;
  for (int32_t i = 0; i < ninst; i++) {
    if (inst[i].geo && inst[i].geo->index_count > 0)
      ntri += inst[i].geo->index_count / 3;
  }
  int32_t as_tri = ntri > 0 ? ntri : 1;
  GpuTriHost* tris = (GpuTriHost*)calloc((size_t)as_tri, sizeof(GpuTriHost));
  float* positions = (float*)calloc((size_t)as_tri * 3 * 3, sizeof(float));
  typedef struct { void* key; int32_t id; } MatMap;
  MatMap* mmap = NULL; int32_t nmap = 0, capm = 0;
  float* mat4 = NULL; int32_t nmat = 0, capmat = 0;
  float* texels = NULL; int32_t ntex = 0, captex = 0;
  int32_t tcount = 0;
  for (int32_t i = 0; i < ninst; i++) {
    farm_Mesh* mesh = inst[i].mesh;
    farm_GeometryData* geo = inst[i].geo;
    if (!geo) continue;
    int32_t mat_id = -1;
    for (int32_t k = 0; k < nmap; k++) {
      if (mmap[k].key == mesh->f_material) { mat_id = mmap[k].id; break; }
    }
    if (mat_id < 0) {
      grow((void**)&mmap, &capm, nmap + 1, sizeof(MatMap));
      grow((void**)&mat4, &capmat, (nmat + 1) * 16, sizeof(float));
      mat_id = nmat;
      mmap[nmap].key = mesh->f_material;
      mmap[nmap].id = mat_id;
      nmap++;
      float* m = mat4 + nmat * 16;
      memset(m, 0, 16 * sizeof(float));
      if (mesh->material_type == 0) {
        farm_MeshBasicMaterial* mat = (farm_MeshBasicMaterial*)mesh->f_material;
        m[0] = (float)mat->f_color.r; m[1] = (float)mat->f_color.g; m[2] = (float)mat->f_color.b;
        m[3] = 0.f;
      } else {
        farm_MeshStandardMaterial* mat = (farm_MeshStandardMaterial*)mesh->f_material;
        m[0] = (float)mat->f_color.r; m[1] = (float)mat->f_color.g; m[2] = (float)mat->f_color.b;
        m[3] = 1.f;
        m[4] = (float)mat->roughness; m[5] = (float)mat->metalness;
        m[6] = (float)mat->transmission; m[7] = (float)mat->ior;
        m[8] = (float)mat->f_emissive.r; m[9] = (float)mat->f_emissive.g; m[10] = (float)mat->f_emissive.b;
        m[11] = (float)mat->emissiveIntensity;
        if (mat->map && geo->uvs) {
          m[12] = 1.f;
          m[13] = (float)mat->map->width;
          m[14] = (float)mat->map->height;
          m[15] = (float)ntex;
          int32_t npx = mat->map->width * mat->map->height * 3;
          grow((void**)&texels, &captex, ntex + npx, sizeof(float));
          for (int32_t p = 0; p < npx; p++) texels[ntex + p] = (float)mat->map->rgb[p];
          ntex += npx;
        }
      }
      nmat++;
    }
    int32_t nt = geo->index_count / 3;
    for (int32_t t = 0; t < nt; t++) {
      int32_t i0 = geo->indices[t * 3 + 0];
      int32_t i1 = geo->indices[t * 3 + 1];
      int32_t i2 = geo->indices[t * 3 + 2];
      double ax, ay, az, bx, by, bz, cx, cy, cz;
      xform_p(inst[i].mw, geo->positions[i0*3], geo->positions[i0*3+1], geo->positions[i0*3+2], &ax, &ay, &az);
      xform_p(inst[i].mw, geo->positions[i1*3], geo->positions[i1*3+1], geo->positions[i1*3+2], &bx, &by, &bz);
      xform_p(inst[i].mw, geo->positions[i2*3], geo->positions[i2*3+1], geo->positions[i2*3+2], &cx, &cy, &cz);
      GpuTriHost* tr = &tris[tcount];
      tr->v0[0] = (float)ax; tr->v0[1] = (float)ay; tr->v0[2] = (float)az; tr->v0[3] = (float)mat_id;
      tr->v1[0] = (float)bx; tr->v1[1] = (float)by; tr->v1[2] = (float)bz; tr->v1[3] = (float)inst[i].mesh_index;
      tr->v2[0] = (float)cx; tr->v2[1] = (float)cy; tr->v2[2] = (float)cz; tr->v2[3] = (float)t;
      if (geo->uvs) {
        tr->uv01[0] = (float)geo->uvs[i0*2]; tr->uv01[1] = (float)geo->uvs[i0*2+1];
        tr->uv01[2] = (float)geo->uvs[i1*2]; tr->uv01[3] = (float)geo->uvs[i1*2+1];
        tr->uv2[0] = (float)geo->uvs[i2*2]; tr->uv2[1] = (float)geo->uvs[i2*2+1];
      }
      float* vp = positions + tcount * 9;
      vp[0] = (float)ax; vp[1] = (float)ay; vp[2] = (float)az;
      vp[3] = (float)bx; vp[4] = (float)by; vp[5] = (float)bz;
      vp[6] = (float)cx; vp[7] = (float)cy; vp[8] = (float)cz;
      tcount++;
    }
  }
  if (ntri == 0) {
    /* Dummy degenerate triangle so AS build is valid; shader sees ntri==0 and misses. */
    positions[0] = 0; positions[1] = 0; positions[2] = 0;
    positions[3] = 0; positions[4] = 0; positions[5] = 0;
    positions[6] = 0; positions[7] = 0; positions[8] = 0;
  }
  if (nmat < 1) {
    grow((void**)&mat4, &capmat, 16, sizeof(float));
    memset(mat4, 0, 16 * sizeof(float));
    nmat = 1;
  }

  int32_t nlight_vec4 = namb + ndir * 2 + npt * 2 + nrect * 6;
  if (nlight_vec4 < 1) nlight_vec4 = 1;
  float* lights = (float*)calloc((size_t)nlight_vec4 * 4, sizeof(float));
  int32_t lo = 0;
  for (int32_t i = 0; i < namb; i++) {
    lights[lo++] = (float)amb[i].color.r; lights[lo++] = (float)amb[i].color.g;
    lights[lo++] = (float)amb[i].color.b; lights[lo++] = (float)amb[i].intensity;
  }
  for (int32_t i = 0; i < ndir; i++) {
    lights[lo++] = (float)dirs[i].tx; lights[lo++] = (float)dirs[i].ty;
    lights[lo++] = (float)dirs[i].tz; lights[lo++] = 0;
    lights[lo++] = (float)dirs[i].ler; lights[lo++] = (float)dirs[i].leg;
    lights[lo++] = (float)dirs[i].leb; lights[lo++] = 0;
  }
  for (int32_t i = 0; i < npt; i++) {
    lights[lo++] = (float)pts[i].px; lights[lo++] = (float)pts[i].py;
    lights[lo++] = (float)pts[i].pz; lights[lo++] = (float)pts[i].distance;
    lights[lo++] = (float)pts[i].ler; lights[lo++] = (float)pts[i].leg;
    lights[lo++] = (float)pts[i].leb; lights[lo++] = (float)pts[i].decay;
  }
  for (int32_t i = 0; i < nrect; i++) {
    const double* e = rects[i].mw;
    for (int k = 0; k < 16; k++) lights[lo++] = (float)e[k];
    lights[lo++] = (float)rects[i].ler; lights[lo++] = (float)rects[i].leg;
    lights[lo++] = (float)rects[i].leb; lights[lo++] = (float)rects[i].width;
    lights[lo++] = (float)rects[i].height; lights[lo++] = 0; lights[lo++] = 0; lights[lo++] = 0;
  }

  typedef struct {
    int32_t size[4];
    int32_t counts[4];
    int32_t counts2[4];
    int32_t flags[4];
    float background[4];
    float cam_pos[4];
    float cam_x[4];
    float cam_y[4];
    float cam_z[4];
    float extra[4];
  } GpuParams;
  GpuParams params;
  memset(&params, 0, sizeof(params));
  params.size[0] = r->width; params.size[1] = r->height;
  params.size[2] = r->samples; params.size[3] = r->accum_count;
  params.counts[0] = r->max_bounces; params.counts[1] = ntri;
  params.counts[2] = nmat; params.counts[3] = namb;
  params.counts2[0] = ndir; params.counts2[1] = npt;
  params.counts2[2] = nrect; params.counts2[3] = ntex;
  params.flags[0] = farm_renderer_roulette_on(r);
  params.flags[1] = farm_renderer_sampling_mode(r);
  params.flags[2] = r->path_adaptive_spp ? 1 : 0;
  params.extra[0] = (float)r->path_adaptive_target;
  if (scene->hasBackground) {
    params.background[0] = (float)scene->background.r;
    params.background[1] = (float)scene->background.g;
    params.background[2] = (float)scene->background.b;
  }
  params.background[3] = (float)camera->f_fov;
  const double* e = camera->f_matrixWorld.elements;
  params.cam_pos[0] = (float)e[12]; params.cam_pos[1] = (float)e[13];
  params.cam_pos[2] = (float)e[14]; params.cam_pos[3] = (float)camera->f_aspect;
  params.cam_x[0] = (float)e[0]; params.cam_x[1] = (float)e[1]; params.cam_x[2] = (float)e[2];
  params.cam_y[0] = (float)e[4]; params.cam_y[1] = (float)e[5]; params.cam_y[2] = (float)e[6];
  params.cam_z[0] = (float)e[8]; params.cam_z[1] = (float)e[9]; params.cam_z[2] = (float)e[10];

  int32_t npix = r->width * r->height;
  uint32_t host_props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  uint32_t ssbo_usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  RtBuf ssbo[6];
  memset(ssbo, 0, sizeof(ssbo));
  VkDeviceSize sizes[6];
  sizes[0] = sizeof(GpuParams);
  sizes[1] = sizeof(GpuTriHost) * (size_t)as_tri;
  sizes[2] = sizeof(float) * 16 * (size_t)nmat;
  sizes[3] = sizeof(float) * 4 * (size_t)nlight_vec4;
  sizes[4] = sizeof(float) * (size_t)(ntex > 0 ? ntex : 4);
  sizes[5] = sizeof(float) * 3 * (size_t)npix;
  int ok = 1;
  for (int i = 0; i < 6; i++) {
    if (!rt_buf_create(sizes[i], ssbo_usage, host_props, 0, 1, &ssbo[i])) { ok = 0; break; }
  }
  if (!ok) {
    for (int i = 0; i < 6; i++) rt_buf_destroy(&ssbo[i]);
    free(inst); free(amb); free(dirs); free(pts); free(rects);
    free(tris); free(positions); free(mmap); free(mat4); free(texels); free(lights);
    return 0;
  }
  memcpy(ssbo[0].mapped, &params, sizeof(params));
  memcpy(ssbo[1].mapped, tris, sizes[1]);
  memcpy(ssbo[2].mapped, mat4, sizes[2]);
  memcpy(ssbo[3].mapped, lights, sizes[3]);
  if (ntex > 0) memcpy(ssbo[4].mapped, texels, (size_t)ntex * sizeof(float));
  memset(ssbo[5].mapped, 0, (size_t)sizes[5]);

  uint32_t as_in_usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  uint32_t as_store_usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  uint32_t scratch_usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

  RtBuf vtx = {0}, instb = {0}, blas_store = {0}, tlas_store = {0}, scratch = {0};
  VkAccelerationStructureKHR blas = 0, tlas = 0;
  VkDescriptorPool pool = 0;
  VkCommandBuffer cmd = NULL;
  int success = 0;

  if (!rt_buf_create((VkDeviceSize)as_tri * 9 * sizeof(float), as_in_usage, host_props, 1, 1, &vtx))
    goto done;
  memcpy(vtx.mapped, positions, (size_t)as_tri * 9 * sizeof(float));

  uint8_t blas_geom[96];
  fill_tri_geom(blas_geom, vtx.addr, (uint32_t)as_tri * 3);
  uint8_t blas_info[80];
  fill_build_info(blas_info, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, 0, 1, blas_geom, 0);
  uint8_t blas_sizes[40];
  memset(blas_sizes, 0, sizeof(blas_sizes));
  *(uint32_t*)blas_sizes = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
  uint32_t prim_count = (uint32_t)as_tri;
  vkGetAccelerationStructureBuildSizesKHR(g_dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                          blas_info, &prim_count, blas_sizes);
  VkDeviceSize blas_sz = *(VkDeviceSize*)(blas_sizes + 16);
  VkDeviceSize blas_scratch_sz = *(VkDeviceSize*)(blas_sizes + 32);

  uint8_t inst_geom[96];
  /* Instance buffer filled after BLAS address is known; query TLAS sizes with a dummy address. */
  if (!rt_buf_create(64, as_in_usage, host_props, 1, 1, &instb)) goto done;
  fill_inst_geom(inst_geom, instb.addr);
  uint8_t tlas_info[80];
  fill_build_info(tlas_info, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, 0, 1, inst_geom, 0);
  uint8_t tlas_sizes[40];
  memset(tlas_sizes, 0, sizeof(tlas_sizes));
  *(uint32_t*)tlas_sizes = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
  uint32_t inst_count = 1;
  vkGetAccelerationStructureBuildSizesKHR(g_dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                          tlas_info, &inst_count, tlas_sizes);
  VkDeviceSize tlas_sz = *(VkDeviceSize*)(tlas_sizes + 16);
  VkDeviceSize tlas_scratch_sz = *(VkDeviceSize*)(tlas_sizes + 32);
  VkDeviceSize scratch_need = align_up(blas_scratch_sz > tlas_scratch_sz ? blas_scratch_sz : tlas_scratch_sz,
                                       g_scratch_align);

  uint32_t dev_local = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  if (!rt_buf_create(blas_sz, as_store_usage, dev_local, 1, 0, &blas_store)) goto done;
  if (!rt_buf_create(tlas_sz, as_store_usage, dev_local, 1, 0, &tlas_store)) goto done;
  if (!rt_buf_create(scratch_need, scratch_usage, dev_local, 1, 0, &scratch)) goto done;

  uint8_t blas_ci[64];
  memset(blas_ci, 0, sizeof(blas_ci));
  *(uint32_t*)blas_ci = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
  memcpy(blas_ci + 24, &blas_store.buf, 8);
  memcpy(blas_ci + 40, &blas_sz, 8);
  *(uint32_t*)(blas_ci + 48) = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
  if (vkCreateAccelerationStructureKHR(g_dev, blas_ci, NULL, &blas) != VK_SUCCESS) goto done;

  uint8_t tlas_ci[64];
  memset(tlas_ci, 0, sizeof(tlas_ci));
  *(uint32_t*)tlas_ci = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
  memcpy(tlas_ci + 24, &tlas_store.buf, 8);
  memcpy(tlas_ci + 40, &tlas_sz, 8);
  *(uint32_t*)(tlas_ci + 48) = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
  if (vkCreateAccelerationStructureKHR(g_dev, tlas_ci, NULL, &tlas) != VK_SUCCESS) goto done;

  uint8_t addr_info[24];
  memset(addr_info, 0, sizeof(addr_info));
  *(uint32_t*)addr_info = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
  memcpy(addr_info + 16, &blas, 8);
  VkDeviceAddress blas_addr = vkGetAccelerationStructureDeviceAddressKHR(g_dev, addr_info);
  if (!blas_addr) goto done;

  /* VkAccelerationStructureInstanceKHR: 12 floats transform, custom+mask, sbt+flags, as ref. */
  uint8_t inst_bytes[64];
  memset(inst_bytes, 0, 64);
  float* xf = (float*)inst_bytes;
  xf[0] = 1.f; xf[5] = 1.f; xf[10] = 1.f;
  uint32_t custom_mask = 0xFFu << 24; /* mask=0xFF, customIndex=0 */
  memcpy(inst_bytes + 48, &custom_mask, 4);
  /* flags = TRIANGLE_FACING_CULL_DISABLE (bit 0 of the flags byte). */
  uint32_t sbt_flags = 1u << 24;
  memcpy(inst_bytes + 52, &sbt_flags, 4);
  memcpy(inst_bytes + 56, &blas_addr, 8);
  memcpy(instb.mapped, inst_bytes, 64);

  fill_build_info(blas_info, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, blas, 1, blas_geom, scratch.addr);
  fill_inst_geom(inst_geom, instb.addr);
  fill_build_info(tlas_info, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, tlas, 1, inst_geom, scratch.addr);

  uint32_t blas_range[4] = { (uint32_t)as_tri, 0, 0, 0 };
  uint32_t tlas_range[4] = { 1, 0, 0, 0 };
  const uint32_t* blas_range_ptr = blas_range;
  const uint32_t* tlas_range_ptr = tlas_range;

  VkDescriptorPoolSize psz[2];
  psz[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  psz[0].descriptorCount = 6;
  psz[1].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
  psz[1].descriptorCount = 1;
  VkDescriptorPoolCreateInfo dpi;
  memset(&dpi, 0, sizeof(dpi));
  dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  dpi.maxSets = 1;
  dpi.poolSizeCount = 2;
  dpi.pPoolSizes = psz;
  if (vkCreateDescriptorPool(g_dev, &dpi, NULL, &pool) != VK_SUCCESS) goto done;
  VkDescriptorSetAllocateInfo dai;
  memset(&dai, 0, sizeof(dai));
  dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  dai.descriptorPool = pool;
  dai.descriptorSetCount = 1;
  dai.pSetLayouts = &g_dsl;
  VkDescriptorSet dset = 0;
  if (vkAllocateDescriptorSets(g_dev, &dai, &dset) != VK_SUCCESS) goto done;

  VkDescriptorBufferInfo bin[6];
  VkWriteDescriptorSet wr[7];
  memset(wr, 0, sizeof(wr));
  for (int i = 0; i < 6; i++) {
    bin[i].buffer = ssbo[i].buf;
    bin[i].offset = 0;
    bin[i].range = VK_WHOLE_SIZE;
    wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr[i].dstSet = dset;
    wr[i].dstBinding = (uint32_t)i;
    wr[i].descriptorCount = 1;
    wr[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    wr[i].pBufferInfo = &bin[i];
  }
  uint8_t as_write[32];
  memset(as_write, 0, sizeof(as_write));
  *(uint32_t*)as_write = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
  *(uint32_t*)(as_write + 16) = 1;
  const VkAccelerationStructureKHR* tlas_ptr = &tlas;
  memcpy(as_write + 24, &tlas_ptr, sizeof(void*));
  wr[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  wr[6].pNext = as_write;
  wr[6].dstSet = dset;
  wr[6].dstBinding = 6;
  wr[6].descriptorCount = 1;
  wr[6].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
  vkUpdateDescriptorSets(g_dev, 7, wr, 0, NULL);

  VkCommandBufferAllocateInfo cai;
  memset(&cai, 0, sizeof(cai));
  cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cai.commandPool = g_cpool;
  cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cai.commandBufferCount = 1;
  if (vkAllocateCommandBuffers(g_dev, &cai, &cmd) != VK_SUCCESS) goto done;
  VkCommandBufferBeginInfo bi;
  memset(&bi, 0, sizeof(bi));
  bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (vkBeginCommandBuffer(cmd, &bi) != VK_SUCCESS) goto done;

  vkCmdBuildAccelerationStructuresKHR(cmd, 1, blas_info, (const void* const*)&blas_range_ptr);
  {
    VkMemoryBarrier mb;
    memset(&mb, 0, sizeof(mb));
    mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    mb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &mb, 0, NULL, 0, NULL);
  }
  vkCmdBuildAccelerationStructuresKHR(cmd, 1, tlas_info, (const void* const*)&tlas_range_ptr);
  {
    VkMemoryBarrier mb;
    memset(&mb, 0, sizeof(mb));
    mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    mb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
  }
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_pipe);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_pl, 0, 1, &dset, 0, NULL);
  uint32_t gx = ((uint32_t)r->width + 7u) / 8u;
  uint32_t gy = ((uint32_t)r->height + 7u) / 8u;
  if (gx < 1) gx = 1;
  if (gy < 1) gy = 1;
  vkCmdDispatch(cmd, gx, gy, 1);
  {
    VkMemoryBarrier mb;
    memset(&mb, 0, sizeof(mb));
    mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 1, &mb, 0, NULL, 0, NULL);
  }
  if (vkEndCommandBuffer(cmd) != VK_SUCCESS) goto done;
  VkSubmitInfo si;
  memset(&si, 0, sizeof(si));
  si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  if (vkQueueSubmit(g_queue, 1, &si, VK_NULL_HANDLE) != VK_SUCCESS ||
      vkQueueWaitIdle(g_queue) != VK_SUCCESS) goto done;

  float* gpu_acc = (float*)ssbo[5].mapped;
  int start_s = r->accum_count;
  int samples = r->samples;
  r->accum_count = start_s + samples;
  int adaptive = r->path_adaptive_spp ? 1 : 0;
  for (int32_t i = 0; i < npix; i++) {
    r->accum[i * 3 + 0] += (double)gpu_acc[i * 3 + 0];
    r->accum[i * 3 + 1] += (double)gpu_acc[i * 3 + 1];
    r->accum[i * 3 + 2] += (double)gpu_acc[i * 3 + 2];
    if (r->spp_count) r->spp_count[i] += samples;
    int32_t c = r->accum_count;
    if (adaptive && r->spp_count && r->spp_count[i] > 0) c = r->spp_count[i];
    double n = (double)(c > 0 ? c : 1);
    float mx = (float)(r->accum[i * 3 + 0] / n);
    float my = (float)(r->accum[i * 3 + 1] / n);
    float mz = (float)(r->accum[i * 3 + 2] / n);
    r->framebuffer[i * 3 + 0] = (uint8_t)quantize_f(mx);
    r->framebuffer[i * 3 + 1] = (uint8_t)quantize_f(my);
    r->framebuffer[i * 3 + 2] = (uint8_t)quantize_f(mz);
  }
  success = 1;

done:
  if (pool) vkDestroyDescriptorPool(g_dev, pool, NULL);
  if (tlas) vkDestroyAccelerationStructureKHR(g_dev, tlas, NULL);
  if (blas) vkDestroyAccelerationStructureKHR(g_dev, blas, NULL);
  rt_buf_destroy(&vtx);
  rt_buf_destroy(&instb);
  rt_buf_destroy(&blas_store);
  rt_buf_destroy(&tlas_store);
  rt_buf_destroy(&scratch);
  for (int i = 0; i < 6; i++) rt_buf_destroy(&ssbo[i]);
  free(inst); free(amb); free(dirs); free(pts); free(rects);
  free(tris); free(positions); free(mmap); free(mat4); free(texels); free(lights);
  return success;
}
