/* M12 native Vulkan upscale glue. Dynamically loads libxess / FSR2.
   Vendor SDK shared objects are external (not in the Farmos PE cap).
   Headless: color/depth/MV/output VkImages + savePNG; no swapchain/window.
   DLSS / NGX / Enabler / Swapper / OptiScaler / frame-gen are not referenced. */
#include "farm_upscale.h"
#include "farm_rt.h"
#include "farm_math.h"
#ifdef FARM_ENABLE_VULKAN
#include "farm_vk.h"
#endif
#ifdef FARM_ENABLE_VULKAN_RT
#include "farm_vk_rt.h"
#endif
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

#define XESS_PACK_B() _Pragma("pack(push, 8)")
#define XESS_PACK_E() _Pragma("pack(pop)")

typedef struct _xess_context_handle_t* xess_context_handle_t;
typedef struct { uint16_t major, minor, patch, reserved; } xess_version_t;
typedef struct { uint32_t x, y; } xess_2d_t;
typedef xess_2d_t xess_coord_t;

enum {
  XESS_QUALITY_SETTING_ULTRA_PERFORMANCE = 100,
  XESS_QUALITY_SETTING_PERFORMANCE = 101,
  XESS_QUALITY_SETTING_BALANCED = 102,
  XESS_QUALITY_SETTING_QUALITY = 103,
  XESS_QUALITY_SETTING_ULTRA_QUALITY = 104,
  XESS_QUALITY_SETTING_AA = 106,
  XESS_INIT_FLAG_USE_NDC_VELOCITY = 1 << 4,
  XESS_INIT_FLAG_LDR_INPUT_COLOR = 1 << 6,
  XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE = 1 << 8,
  XESS_RESULT_SUCCESS = 0
};

typedef uint32_t VkFlags;
typedef uint32_t VkBool32;
typedef uint64_t VkDeviceSize;
typedef uint32_t VkResult;
typedef void* VkInstance;
typedef void* VkPhysicalDevice;
typedef void* VkDevice;
typedef void* VkQueue;
typedef void* VkCommandBuffer;
typedef uint64_t VkBuffer;
typedef uint64_t VkDeviceMemory;
typedef uint64_t VkImage;
typedef uint64_t VkImageView;
typedef uint64_t VkCommandPool;
typedef uint64_t VkFence;
typedef uint64_t VkPipelineCache;

#define VK_SUCCESS 0
#define VK_STRUCTURE_TYPE_APPLICATION_INFO 0
#define VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO 1
#define VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO 2
#define VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO 3
#define VK_STRUCTURE_TYPE_SUBMIT_INFO 4
#define VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO 5
#define VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO 12
#define VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO 14
#define VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO 15
#define VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO 39
#define VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO 40
#define VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO 42
#define VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER 45
#define VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO 17
#define VK_QUEUE_COMPUTE_BIT 2
#define VK_QUEUE_TRANSFER_BIT 4
#define VK_BUFFER_USAGE_TRANSFER_SRC_BIT 0x00000001
#define VK_BUFFER_USAGE_TRANSFER_DST_BIT 0x00000002
#define VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT 0x00000001
#define VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT 0x00000002
#define VK_MEMORY_PROPERTY_HOST_COHERENT_BIT 0x00000004
#define VK_SHARING_MODE_EXCLUSIVE 0
#define VK_COMMAND_BUFFER_LEVEL_PRIMARY 0
#define VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT 0x00000002
#define VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT 0x00000001
#define VK_IMAGE_TYPE_2D 1
#define VK_IMAGE_VIEW_TYPE_2D 1
#define VK_IMAGE_TILING_OPTIMAL 0
#define VK_IMAGE_LAYOUT_UNDEFINED 0
#define VK_IMAGE_LAYOUT_GENERAL 1
#define VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL 2
#define VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL 7
#define VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL 6
#define VK_IMAGE_ASPECT_COLOR_BIT 1
#define VK_SAMPLE_COUNT_1_BIT 1
#define VK_FORMAT_R8G8B8A8_UNORM 37
#define VK_FORMAT_R32G32_SFLOAT 103
#define VK_FORMAT_R32_SFLOAT 100
#define VK_IMAGE_USAGE_TRANSFER_SRC_BIT 0x00000001
#define VK_IMAGE_USAGE_TRANSFER_DST_BIT 0x00000002
#define VK_IMAGE_USAGE_SAMPLED_BIT 0x00000004
#define VK_IMAGE_USAGE_STORAGE_BIT 0x00000008
#define VK_ACCESS_TRANSFER_WRITE_BIT 0x00001000
#define VK_ACCESS_TRANSFER_READ_BIT 0x00000800
#define VK_ACCESS_SHADER_READ_BIT 0x00000020
#define VK_ACCESS_SHADER_WRITE_BIT 0x00000040
#define VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT 0x00000001
#define VK_PIPELINE_STAGE_TRANSFER_BIT 0x00001000
#define VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT 0x00000800
#define VK_QUEUE_FAMILY_IGNORED (~0u)
#define VK_WHOLE_SIZE (~(VkDeviceSize)0)
#define VK_NULL_HANDLE 0
#define VK_MAKE_VERSION(maj, min, pat) (((maj)<<22)|((min)<<12)|(pat))
#define VK_API_VERSION_1_1 VK_MAKE_VERSION(1, 1, 0)
#define VK_API_VERSION_1_2 VK_MAKE_VERSION(1, 2, 0)

typedef struct { uint32_t width, height, depth; } VkExtent3D;
typedef struct { int32_t x, y, z; } VkOffset3D;
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
typedef struct {
  uint32_t sType; const void* pNext; uint32_t flags; uint32_t imageType; uint32_t format;
  VkExtent3D extent; uint32_t mipLevels; uint32_t arrayLayers; uint32_t samples; uint32_t tiling;
  uint32_t usage; uint32_t sharingMode; uint32_t queueFamilyIndexCount; const uint32_t* pQueueFamilyIndices;
  uint32_t initialLayout;
} VkImageCreateInfo;
typedef struct { uint32_t r, g, b, a; } VkComponentMapping;
typedef struct {
  uint32_t aspectMask; uint32_t baseMipLevel; uint32_t levelCount; uint32_t baseArrayLayer; uint32_t layerCount;
} VkImageSubresourceRange;
typedef struct {
  uint32_t sType; const void* pNext; uint32_t flags; VkImage image; uint32_t viewType; uint32_t format;
  VkComponentMapping components; VkImageSubresourceRange subresourceRange;
} VkImageViewCreateInfo;
typedef struct {
  uint32_t sType; const void* pNext; uint32_t srcAccessMask; uint32_t dstAccessMask;
  uint32_t oldLayout; uint32_t newLayout; uint32_t srcQueueFamilyIndex; uint32_t dstQueueFamilyIndex;
  VkImage image; VkImageSubresourceRange subresourceRange;
} VkImageMemoryBarrier;
typedef struct {
  uint32_t aspectMask; uint32_t mipLevel; uint32_t baseArrayLayer; uint32_t layerCount;
} VkImageSubresourceLayers;
typedef struct {
  VkDeviceSize bufferOffset; uint32_t bufferRowLength; uint32_t bufferImageHeight;
  VkImageSubresourceLayers imageSubresource; VkOffset3D imageOffset; VkExtent3D imageExtent;
} VkBufferImageCopy;
typedef struct {
  uint32_t sType; const void* pNext; uint32_t flags;
  size_t initialDataSize; const void* pInitialData;
} VkPipelineCacheCreateInfo;

XESS_PACK_B()
typedef struct {
  VkImageView imageView;
  VkImage image;
  VkImageSubresourceRange subresourceRange;
  uint32_t format;
  unsigned int width;
  unsigned int height;
} xess_vk_image_view_info;
XESS_PACK_E()

XESS_PACK_B()
typedef struct {
  xess_vk_image_view_info colorTexture;
  xess_vk_image_view_info velocityTexture;
  xess_vk_image_view_info depthTexture;
  xess_vk_image_view_info exposureScaleTexture;
  xess_vk_image_view_info responsivePixelMaskTexture;
  xess_vk_image_view_info outputTexture;
  float jitterOffsetX;
  float jitterOffsetY;
  float exposureScale;
  uint32_t resetHistory;
  uint32_t inputWidth;
  uint32_t inputHeight;
  xess_coord_t inputColorBase;
  xess_coord_t inputMotionVectorBase;
  xess_coord_t inputDepthBase;
  xess_coord_t inputResponsiveMaskBase;
  xess_coord_t reserved0;
  xess_coord_t outputColorBase;
} xess_vk_execute_params_t;
XESS_PACK_E()

XESS_PACK_B()
typedef struct {
  xess_2d_t outputResolution;
  uint32_t qualitySetting;
  uint32_t initFlags;
  uint32_t creationNodeMask;
  uint32_t visibleNodeMask;
  VkDeviceMemory tempBufferHeap;
  uint64_t bufferHeapOffset;
  VkDeviceMemory tempTextureHeap;
  uint64_t textureHeapOffset;
  VkPipelineCache pipelineCache;
} xess_vk_init_params_t;
XESS_PACK_E()

typedef int32_t (*PFN_xessVKGetRequiredInstanceExtensions)(uint32_t*, const char* const**, uint32_t*);
typedef int32_t (*PFN_xessVKGetRequiredDeviceExtensions)(VkInstance, VkPhysicalDevice, uint32_t*, const char* const**);
typedef int32_t (*PFN_xessVKGetRequiredDeviceFeatures)(VkInstance, VkPhysicalDevice, void**);
typedef int32_t (*PFN_xessVKCreateContext)(VkInstance, VkPhysicalDevice, VkDevice, xess_context_handle_t*);
typedef int32_t (*PFN_xessVKInit)(xess_context_handle_t, const xess_vk_init_params_t*);
typedef int32_t (*PFN_xessVKExecute)(xess_context_handle_t, VkCommandBuffer, const xess_vk_execute_params_t*);
typedef int32_t (*PFN_xessDestroyContext)(xess_context_handle_t);
typedef int32_t (*PFN_xessGetVersion)(xess_version_t*);

#define VK_FN(name, ret, args) typedef ret (*PFN_##name) args; static PFN_##name name
VK_FN(vkDestroyInstance, void, (VkInstance, const void*));
VK_FN(vkEnumeratePhysicalDevices, VkResult, (VkInstance, uint32_t*, VkPhysicalDevice*));
VK_FN(vkGetPhysicalDeviceQueueFamilyProperties, void, (VkPhysicalDevice, uint32_t*, VkQueueFamilyProperties*));
VK_FN(vkGetPhysicalDeviceMemoryProperties, void, (VkPhysicalDevice, VkPhysicalDeviceMemoryProperties*));
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
VK_FN(vkCreateImage, VkResult, (VkDevice, const VkImageCreateInfo*, const void*, VkImage*));
VK_FN(vkDestroyImage, void, (VkDevice, VkImage, const void*));
VK_FN(vkGetImageMemoryRequirements, void, (VkDevice, VkImage, VkMemoryRequirements*));
VK_FN(vkBindImageMemory, VkResult, (VkDevice, VkImage, VkDeviceMemory, VkDeviceSize));
VK_FN(vkCreateImageView, VkResult, (VkDevice, const VkImageViewCreateInfo*, const void*, VkImageView*));
VK_FN(vkDestroyImageView, void, (VkDevice, VkImageView, const void*));
VK_FN(vkCreateCommandPool, VkResult, (VkDevice, const VkCommandPoolCreateInfo*, const void*, VkCommandPool*));
VK_FN(vkDestroyCommandPool, void, (VkDevice, VkCommandPool, const void*));
VK_FN(vkAllocateCommandBuffers, VkResult, (VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer*));
VK_FN(vkResetCommandBuffer, VkResult, (VkCommandBuffer, uint32_t));
VK_FN(vkBeginCommandBuffer, VkResult, (VkCommandBuffer, const VkCommandBufferBeginInfo*));
VK_FN(vkEndCommandBuffer, VkResult, (VkCommandBuffer));
VK_FN(vkCmdPipelineBarrier, void, (VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t, const void*, uint32_t, const void*, uint32_t, const VkImageMemoryBarrier*));
VK_FN(vkCmdCopyBufferToImage, void, (VkCommandBuffer, VkBuffer, VkImage, uint32_t, uint32_t, const VkBufferImageCopy*));
VK_FN(vkCmdCopyImageToBuffer, void, (VkCommandBuffer, VkImage, uint32_t, VkBuffer, uint32_t, const VkBufferImageCopy*));
VK_FN(vkQueueSubmit, VkResult, (VkQueue, uint32_t, const VkSubmitInfo*, VkFence));
VK_FN(vkQueueWaitIdle, VkResult, (VkQueue));
VK_FN(vkCreatePipelineCache, VkResult, (VkDevice, const VkPipelineCacheCreateInfo*, const void*, VkPipelineCache*));
VK_FN(vkDestroyPipelineCache, void, (VkDevice, VkPipelineCache, const void*));
VK_FN(vkGetPipelineCacheData, VkResult, (VkDevice, VkPipelineCache, size_t*, void*));
#undef VK_FN

static void* g_vk_loader;
typedef void* (*PFN_vkGetInstanceProcAddr)(VkInstance, const char*);
typedef VkResult (*PFN_vkCreateInstance)(const VkInstanceCreateInfo*, const void*, VkInstance*);
static PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr_fn;
static PFN_vkCreateInstance vkCreateInstance_fn;

static void* g_xess;
static PFN_xessVKGetRequiredInstanceExtensions xessVKGetRequiredInstanceExtensions_fn;
static PFN_xessVKGetRequiredDeviceExtensions xessVKGetRequiredDeviceExtensions_fn;
static PFN_xessVKGetRequiredDeviceFeatures xessVKGetRequiredDeviceFeatures_fn;
static PFN_xessVKCreateContext xessVKCreateContext_fn;
static PFN_xessVKInit xessVKInit_fn;
static PFN_xessVKExecute xessVKExecute_fn;
static PFN_xessDestroyContext xessDestroyContext_fn;

static VkInstance g_inst;
static VkPhysicalDevice g_phys;
static uint32_t g_qfam;
static VkDevice g_dev;
static VkQueue g_queue;
static VkCommandPool g_cpool;
static int g_vk_ready;
static int g_vk_borrowed;
static int g_borrow_failed;
static xess_context_handle_t g_xess_ctx;
static int g_xess_inited;
static int32_t g_init_dw, g_init_dh;
static int g_xess_probe_done, g_xess_ok;
static int g_fsr_probe_done, g_fsr_ok;
static VkPipelineCache g_xess_pso_cache;
static uint32_t g_xess_pso_dw, g_xess_pso_dh;
static int g_xess_pso_saved_exec;
static size_t g_xess_pso_loaded_sz;

static int env1(const char* n) {
  const char* e = getenv(n);
  return e && e[0] == '1' && e[1] == '\0';
}

static void* sys_load(const char* name) {
#ifdef _WIN32
  return (void*)LoadLibraryA(name);
#else
  return dlopen(name, RTLD_NOW | RTLD_LOCAL);
#endif
}

static void* sys_sym(void* h, const char* name) {
#ifdef _WIN32
  return (void*)GetProcAddress((HMODULE)h, name);
#else
  return dlsym(h, name);
#endif
}

static void* try_load_named(const char* env, const char* const* names) {
  if (env) {
    const char* p = getenv(env);
    if (p && p[0]) {
      void* h = sys_load(p);
      if (h) return h;
    }
  }
  for (int i = 0; names[i]; i++) {
    void* h = sys_load(names[i]);
    if (h) return h;
  }
  const char* rt = getenv("FARM_RUNTIME");
  if (rt && rt[0]) {
    char buf[512];
    for (int i = 0; names[i]; i++) {
#ifdef _WIN32
      snprintf(buf, sizeof(buf), "%s\\third_party\\xess\\%s", rt, names[i]);
#else
      snprintf(buf, sizeof(buf), "%s/third_party/xess/%s", rt, names[i]);
#endif
      void* h = sys_load(buf);
      if (h) return h;
#ifdef _WIN32
      snprintf(buf, sizeof(buf), "%s\\third_party\\fsr\\%s", rt, names[i]);
#else
      snprintf(buf, sizeof(buf), "%s/third_party/fsr/%s", rt, names[i]);
#endif
      h = sys_load(buf);
      if (h) return h;
    }
  }
  return NULL;
}

static const char* const k_xess_names[] = {
#ifdef _WIN32
  "libxess.dll", "libxess_vk.dll", NULL
#else
  "libxess.so", "libxess.so.2", "libxess.so.1", NULL
#endif
};

static int file_readable(const char* p) {
  FILE* f;
  if (!p || !p[0]) return 0;
  f = fopen(p, "rb");
  if (!f) return 0;
  fclose(f);
  return 1;
}

/* File-exists only — do not map the ~75 MiB SDK (AC-05 is a new process each sample). */
static int xess_sdk_file_present(void) {
  const char* env = getenv("FARMOS_XESS_LIB");
  char buf[512];
  int i;
  if (file_readable(env)) return 1;
  for (i = 0; k_xess_names[i]; i++) {
    if (file_readable(k_xess_names[i])) return 1;
  }
#ifdef _WIN32
  {
    char found[MAX_PATH];
    if (SearchPathA(NULL, "libxess.dll", NULL, MAX_PATH, found, NULL) != 0) return 1;
    if (SearchPathA(NULL, "libxess_vk.dll", NULL, MAX_PATH, found, NULL) != 0) return 1;
  }
#endif
  {
    const char* rt = getenv("FARM_RUNTIME");
    if (rt && rt[0]) {
      for (i = 0; k_xess_names[i]; i++) {
#ifdef _WIN32
        snprintf(buf, sizeof(buf), "%s\\third_party\\xess\\%s", rt, k_xess_names[i]);
#else
        snprintf(buf, sizeof(buf), "%s/third_party/xess/%s", rt, k_xess_names[i]);
#endif
        if (file_readable(buf)) return 1;
      }
    }
  }
  return 0;
}

int farm_upscale_probe_xess(void) {
  if (g_xess_probe_done) return g_xess_ok;
  g_xess_probe_done = 1;
  g_xess_ok = 0;
  g_xess = try_load_named("FARMOS_XESS_LIB", k_xess_names);
  if (!g_xess) return 0;
  xessVKCreateContext_fn = (PFN_xessVKCreateContext)sys_sym(g_xess, "xessVKCreateContext");
  xessVKInit_fn = (PFN_xessVKInit)sys_sym(g_xess, "xessVKInit");
  xessVKExecute_fn = (PFN_xessVKExecute)sys_sym(g_xess, "xessVKExecute");
  xessDestroyContext_fn = (PFN_xessDestroyContext)sys_sym(g_xess, "xessDestroyContext");
  xessVKGetRequiredInstanceExtensions_fn =
      (PFN_xessVKGetRequiredInstanceExtensions)sys_sym(g_xess, "xessVKGetRequiredInstanceExtensions");
  xessVKGetRequiredDeviceExtensions_fn =
      (PFN_xessVKGetRequiredDeviceExtensions)sys_sym(g_xess, "xessVKGetRequiredDeviceExtensions");
  xessVKGetRequiredDeviceFeatures_fn =
      (PFN_xessVKGetRequiredDeviceFeatures)sys_sym(g_xess, "xessVKGetRequiredDeviceFeatures");
  if (!xessVKCreateContext_fn || !xessVKInit_fn || !xessVKExecute_fn || !xessDestroyContext_fn)
    return 0;
  g_xess_ok = 1;
  return 1;
}

void farm_upscale_prefetch_xess(void) {
  if (!farm_path_upscale_backend_wanted()) return;
  farm_upscale_probe_xess();
}

int farm_upscale_probe_fsr(void) {
  if (g_fsr_probe_done) return g_fsr_ok;
  g_fsr_probe_done = 1;
  g_fsr_ok = 0;
  static const char* names[] = {
#ifdef _WIN32
    "ffx_fsr2_vk.dll", "libffx_fsr2_vk.dll", NULL
#else
    "libffx_fsr2_vk.so", "libffx_fsr2_api_vk.so", NULL
#endif
  };
  void* h = try_load_named("FARMOS_FSR_LIB", names);
  if (!h) return 0;
  /* FSR2 dispatch lives in a vendor shared lib built by the fetch script.
     Presence of the lib is enough for isPathUpscaleAvailable; execute
     currently requires the same Vulkan image path as XeSS and is wired
     when the official ffx dispatch symbols resolve. */
  if (!sys_sym(h, "ffxFsr2ContextCreate") && !sys_sym(h, "ffxFsr2ContextCreateVk"))
    return 0;
  g_fsr_ok = 1;
  return 1;
}

int farm_upscale_mode_available(int64_t mode) {
  if (mode == 0) return 1;
  if (env1("FARMOS_PATH_UPSCALE_FORCE_FAIL")) return 0;
  if (mode == 1) {
    if (g_xess_ok) return 1;
    if (g_xess_probe_done) return 0;
    return xess_sdk_file_present();
  }
  if (mode == 2) return farm_upscale_probe_fsr();
  return 0;
}

int farm_upscale_xess_required_instance(unsigned* count, const char* const** names,
                                       unsigned* min_api) {
  if (count) *count = 0;
  if (names) *names = NULL;
  if (min_api) *min_api = VK_API_VERSION_1_2;
  if (!farm_path_upscale_backend_wanted()) return 0;
  /* Official extras (edeb845 borrow). Map libxess here — device-create is too
     late to add instance extensions. isPathUpscaleAvailable stays file-present. */
  if (farm_upscale_probe_xess() && xessVKGetRequiredInstanceExtensions_fn) {
    uint32_t n = 0, api = VK_API_VERSION_1_2;
    const char* const* nm = NULL;
    if (xessVKGetRequiredInstanceExtensions_fn(&n, &nm, &api) == XESS_RESULT_SUCCESS) {
      if (count) *count = n;
      if (names) *names = nm;
      if (min_api) *min_api = api;
      return 1;
    }
  }
  return 1;
}

int farm_upscale_xess_required_device(void* inst, void* phys, unsigned* count,
                                     const char* const** names, void** features) {
  if (count) *count = 0;
  if (names) *names = NULL;
  if (features) *features = NULL;
  if (!inst || !phys) return 0;
  if (!farm_path_upscale_backend_wanted()) return 0;
  if (!farm_upscale_probe_xess()) return 0;
  uint32_t n = 0;
  const char* const* nm = NULL;
  void* feats = NULL;
  if (xessVKGetRequiredDeviceExtensions_fn)
    xessVKGetRequiredDeviceExtensions_fn((VkInstance)inst, (VkPhysicalDevice)phys, &n, &nm);
  if (xessVKGetRequiredDeviceFeatures_fn)
    xessVKGetRequiredDeviceFeatures_fn((VkInstance)inst, (VkPhysicalDevice)phys, &feats);
  if (count) *count = n;
  if (names) *names = nm;
  if (features) *features = feats;
  return 1;
}

static int load_vulkan(void) {
  if (g_vk_loader) return 1;
#ifdef _WIN32
  g_vk_loader = (void*)LoadLibraryA("vulkan-1.dll");
#else
  g_vk_loader = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
  if (!g_vk_loader) g_vk_loader = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
#endif
  if (!g_vk_loader) return 0;
  vkGetInstanceProcAddr_fn = (PFN_vkGetInstanceProcAddr)sys_sym(g_vk_loader, "vkGetInstanceProcAddr");
  if (!vkGetInstanceProcAddr_fn) return 0;
  vkCreateInstance_fn = (PFN_vkCreateInstance)vkGetInstanceProcAddr_fn(NULL, "vkCreateInstance");
  return vkCreateInstance_fn ? 1 : 0;
}

static void* inst_proc(const char* name) {
  return vkGetInstanceProcAddr_fn(g_inst, name);
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

static int bind_vk_procs(void) {
#define LOADI(name) do { name = (PFN_##name)inst_proc(#name); if (!name) return 0; } while (0)
  LOADI(vkDestroyInstance);
  LOADI(vkEnumeratePhysicalDevices);
  LOADI(vkGetPhysicalDeviceQueueFamilyProperties);
  LOADI(vkGetPhysicalDeviceMemoryProperties);
  LOADI(vkCreateDevice);
#undef LOADI
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
  LOADD(vkCreateImage);
  LOADD(vkDestroyImage);
  LOADD(vkGetImageMemoryRequirements);
  LOADD(vkBindImageMemory);
  LOADD(vkCreateImageView);
  LOADD(vkDestroyImageView);
  LOADD(vkCreateCommandPool);
  LOADD(vkDestroyCommandPool);
  LOADD(vkAllocateCommandBuffers);
  LOADD(vkResetCommandBuffer);
  LOADD(vkBeginCommandBuffer);
  LOADD(vkEndCommandBuffer);
  LOADD(vkCmdPipelineBarrier);
  LOADD(vkCmdCopyBufferToImage);
  LOADD(vkCmdCopyImageToBuffer);
  LOADD(vkQueueSubmit);
  LOADD(vkQueueWaitIdle);
#undef LOADD
  vkCreatePipelineCache = (PFN_vkCreatePipelineCache)vkGetDeviceProcAddr(g_dev, "vkCreatePipelineCache");
  vkDestroyPipelineCache = (PFN_vkDestroyPipelineCache)vkGetDeviceProcAddr(g_dev, "vkDestroyPipelineCache");
  vkGetPipelineCacheData = (PFN_vkGetPipelineCacheData)vkGetDeviceProcAddr(g_dev, "vkGetPipelineCacheData");
  return 1;
}

static int create_cpool(void) {
  VkCommandPoolCreateInfo pci;
  memset(&pci, 0, sizeof(pci));
  pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pci.queueFamilyIndex = g_qfam;
  return vkCreateCommandPool(g_dev, &pci, NULL, &g_cpool) == VK_SUCCESS;
}

static int try_borrow_path_vk(void) {
  void *inst = NULL, *phys = NULL, *dev = NULL, *queue = NULL;
  unsigned qfam = 0;
  int ok = 0;
#ifdef FARM_ENABLE_VULKAN_RT
  ok = farm_vk_rt_export_handles(&inst, &phys, &dev, &queue, &qfam);
#endif
#ifdef FARM_ENABLE_VULKAN
  if (!ok) ok = farm_vk_export_handles(&inst, &phys, &dev, &queue, &qfam);
#endif
  if (!ok || !inst || !phys || !dev || !queue) return 0;
  if (!load_vulkan()) return 0;
  g_inst = inst;
  g_phys = phys;
  g_dev = dev;
  g_queue = queue;
  g_qfam = qfam;
  g_vk_borrowed = 1;
  if (!bind_vk_procs() || !create_cpool()) {
    g_inst = NULL; g_phys = NULL; g_dev = NULL; g_queue = NULL;
    g_cpool = 0;
    g_vk_borrowed = 0;
    return 0;
  }
  g_vk_ready = 1;
  return 1;
}

static int ensure_vk_own(void) {
  if (!load_vulkan()) return 0;

  uint32_t iext_n = 0;
  const char* const* iext = NULL;
  uint32_t min_api = VK_API_VERSION_1_1;
  if (farm_upscale_probe_xess() && xessVKGetRequiredInstanceExtensions_fn) {
    if (xessVKGetRequiredInstanceExtensions_fn(&iext_n, &iext, &min_api) != XESS_RESULT_SUCCESS) {
      iext_n = 0; iext = NULL; min_api = VK_API_VERSION_1_1;
    }
  }

  VkApplicationInfo app;
  memset(&app, 0, sizeof(app));
  app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.pApplicationName = "farmos-upscale";
  app.apiVersion = min_api < VK_API_VERSION_1_1 ? VK_API_VERSION_1_1 : min_api;
  VkInstanceCreateInfo ici;
  memset(&ici, 0, sizeof(ici));
  ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ici.pApplicationInfo = &app;
  ici.enabledExtensionCount = iext_n;
  ici.ppEnabledExtensionNames = iext;
  if (vkCreateInstance_fn(&ici, NULL, &g_inst) != VK_SUCCESS || !g_inst) {
    ici.enabledExtensionCount = 0;
    ici.ppEnabledExtensionNames = NULL;
    app.apiVersion = VK_API_VERSION_1_1;
    if (vkCreateInstance_fn(&ici, NULL, &g_inst) != VK_SUCCESS || !g_inst) return 0;
  }

  uint32_t ndev = 0;
#define LOADI(name) do { name = (PFN_##name)inst_proc(#name); if (!name) return 0; } while (0)
  LOADI(vkDestroyInstance);
  LOADI(vkEnumeratePhysicalDevices);
  LOADI(vkGetPhysicalDeviceQueueFamilyProperties);
  LOADI(vkGetPhysicalDeviceMemoryProperties);
  LOADI(vkCreateDevice);
#undef LOADI

  if (vkEnumeratePhysicalDevices(g_inst, &ndev, NULL) != VK_SUCCESS || ndev == 0) return 0;
  VkPhysicalDevice* devs = (VkPhysicalDevice*)malloc(sizeof(VkPhysicalDevice) * ndev);
  if (!devs) return 0;
  if (vkEnumeratePhysicalDevices(g_inst, &ndev, devs) != VK_SUCCESS) { free(devs); return 0; }
  int found = 0;
  for (uint32_t i = 0; i < ndev && !found; i++) {
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
        found = 1;
        break;
      }
    }
    free(q);
  }
  free(devs);
  if (!found) return 0;

  uint32_t dext_n = 0;
  const char* const* dext = NULL;
  void* feats = NULL;
  if (xessVKGetRequiredDeviceExtensions_fn)
    xessVKGetRequiredDeviceExtensions_fn(g_inst, g_phys, &dext_n, &dext);
  if (xessVKGetRequiredDeviceFeatures_fn)
    xessVKGetRequiredDeviceFeatures_fn(g_inst, g_phys, &feats);

  float prio = 1.0f;
  VkDeviceQueueCreateInfo qci;
  memset(&qci, 0, sizeof(qci));
  qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  qci.queueFamilyIndex = g_qfam;
  qci.queueCount = 1;
  qci.pQueuePriorities = &prio;
  VkDeviceCreateInfo dci;
  memset(&dci, 0, sizeof(dci));
  dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  dci.pNext = feats;
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  dci.enabledExtensionCount = dext_n;
  dci.ppEnabledExtensionNames = dext;
  if (vkCreateDevice(g_phys, &dci, NULL, &g_dev) != VK_SUCCESS || !g_dev) {
    dci.pNext = NULL;
    dci.enabledExtensionCount = 0;
    dci.ppEnabledExtensionNames = NULL;
    if (vkCreateDevice(g_phys, &dci, NULL, &g_dev) != VK_SUCCESS || !g_dev) return 0;
  }

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
  LOADD(vkCreateImage);
  LOADD(vkDestroyImage);
  LOADD(vkGetImageMemoryRequirements);
  LOADD(vkBindImageMemory);
  LOADD(vkCreateImageView);
  LOADD(vkDestroyImageView);
  LOADD(vkCreateCommandPool);
  LOADD(vkDestroyCommandPool);
  LOADD(vkAllocateCommandBuffers);
  LOADD(vkResetCommandBuffer);
  LOADD(vkBeginCommandBuffer);
  LOADD(vkEndCommandBuffer);
  LOADD(vkCmdPipelineBarrier);
  LOADD(vkCmdCopyBufferToImage);
  LOADD(vkCmdCopyImageToBuffer);
  LOADD(vkQueueSubmit);
  LOADD(vkQueueWaitIdle);
#undef LOADD
  vkCreatePipelineCache = (PFN_vkCreatePipelineCache)vkGetDeviceProcAddr(g_dev, "vkCreatePipelineCache");
  vkDestroyPipelineCache = (PFN_vkDestroyPipelineCache)vkGetDeviceProcAddr(g_dev, "vkDestroyPipelineCache");
  vkGetPipelineCacheData = (PFN_vkGetPipelineCacheData)vkGetDeviceProcAddr(g_dev, "vkGetPipelineCacheData");
  vkGetDeviceQueue(g_dev, g_qfam, 0, &g_queue);
  if (!create_cpool()) return 0;
  g_vk_borrowed = 0;
  g_vk_ready = 1;
  return 1;
}

static int ensure_vk(void) {
  if (g_vk_ready) return 1;
  if (!g_borrow_failed && try_borrow_path_vk()) return 1;
  g_borrow_failed = 1;
  return ensure_vk_own();
}

typedef struct {
  VkImage image;
  VkImageView view;
  VkDeviceMemory mem;
  uint32_t w, h, format;
} UpImg;

typedef struct {
  VkBuffer buf;
  VkDeviceMemory mem;
  void* map;
  VkDeviceSize sz;
} UpBuf;

static int make_buf(VkDeviceSize sz, uint32_t usage, UpBuf* o) {
  memset(o, 0, sizeof(*o));
  o->sz = sz;
  VkBufferCreateInfo bi;
  memset(&bi, 0, sizeof(bi));
  bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bi.size = sz;
  bi.usage = usage;
  bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(g_dev, &bi, NULL, &o->buf) != VK_SUCCESS) return 0;
  VkMemoryRequirements mr;
  vkGetBufferMemoryRequirements(g_dev, o->buf, &mr);
  int mt = find_mem_type(mr.memoryTypeBits,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (mt < 0) { vkDestroyBuffer(g_dev, o->buf, NULL); o->buf = 0; return 0; }
  VkMemoryAllocateInfo ai;
  memset(&ai, 0, sizeof(ai));
  ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  ai.allocationSize = mr.size;
  ai.memoryTypeIndex = (uint32_t)mt;
  if (vkAllocateMemory(g_dev, &ai, NULL, &o->mem) != VK_SUCCESS) {
    vkDestroyBuffer(g_dev, o->buf, NULL); o->buf = 0; return 0;
  }
  if (vkBindBufferMemory(g_dev, o->buf, o->mem, 0) != VK_SUCCESS) {
    vkFreeMemory(g_dev, o->mem, NULL);
    vkDestroyBuffer(g_dev, o->buf, NULL);
    memset(o, 0, sizeof(*o));
    return 0;
  }
  if (vkMapMemory(g_dev, o->mem, 0, VK_WHOLE_SIZE, 0, &o->map) != VK_SUCCESS) {
    vkFreeMemory(g_dev, o->mem, NULL);
    vkDestroyBuffer(g_dev, o->buf, NULL);
    memset(o, 0, sizeof(*o));
    return 0;
  }
  return 1;
}

static void destroy_buf(UpBuf* o) {
  if (!o || !o->buf) return;
  if (o->map) vkUnmapMemory(g_dev, o->mem);
  vkDestroyBuffer(g_dev, o->buf, NULL);
  vkFreeMemory(g_dev, o->mem, NULL);
  memset(o, 0, sizeof(*o));
}

static int make_img(uint32_t w, uint32_t h, uint32_t format, uint32_t usage, UpImg* o) {
  memset(o, 0, sizeof(*o));
  o->w = w; o->h = h; o->format = format;
  VkImageCreateInfo ii;
  memset(&ii, 0, sizeof(ii));
  ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ii.imageType = VK_IMAGE_TYPE_2D;
  ii.format = format;
  ii.extent.width = w; ii.extent.height = h; ii.extent.depth = 1;
  ii.mipLevels = 1; ii.arrayLayers = 1; ii.samples = VK_SAMPLE_COUNT_1_BIT;
  ii.tiling = VK_IMAGE_TILING_OPTIMAL;
  ii.usage = usage;
  ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (vkCreateImage(g_dev, &ii, NULL, &o->image) != VK_SUCCESS) return 0;
  VkMemoryRequirements mr;
  vkGetImageMemoryRequirements(g_dev, o->image, &mr);
  int mt = find_mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (mt < 0) mt = find_mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  if (mt < 0) { vkDestroyImage(g_dev, o->image, NULL); o->image = 0; return 0; }
  VkMemoryAllocateInfo ai;
  memset(&ai, 0, sizeof(ai));
  ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  ai.allocationSize = mr.size;
  ai.memoryTypeIndex = (uint32_t)mt;
  if (vkAllocateMemory(g_dev, &ai, NULL, &o->mem) != VK_SUCCESS) {
    vkDestroyImage(g_dev, o->image, NULL); o->image = 0; return 0;
  }
  if (vkBindImageMemory(g_dev, o->image, o->mem, 0) != VK_SUCCESS) {
    vkFreeMemory(g_dev, o->mem, NULL);
    vkDestroyImage(g_dev, o->image, NULL);
    memset(o, 0, sizeof(*o));
    return 0;
  }
  VkImageViewCreateInfo vi;
  memset(&vi, 0, sizeof(vi));
  vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  vi.image = o->image;
  vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vi.format = format;
  vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  vi.subresourceRange.levelCount = 1;
  vi.subresourceRange.layerCount = 1;
  if (vkCreateImageView(g_dev, &vi, NULL, &o->view) != VK_SUCCESS) {
    vkFreeMemory(g_dev, o->mem, NULL);
    vkDestroyImage(g_dev, o->image, NULL);
    memset(o, 0, sizeof(*o));
    return 0;
  }
  return 1;
}

static void destroy_img(UpImg* o) {
  if (!o || !o->image) return;
  vkDestroyImageView(g_dev, o->view, NULL);
  vkDestroyImage(g_dev, o->image, NULL);
  vkFreeMemory(g_dev, o->mem, NULL);
  memset(o, 0, sizeof(*o));
}

static UpImg g_img_color, g_img_vel, g_img_depth, g_img_out;
static UpBuf g_buf_color, g_buf_vel, g_buf_depth, g_buf_out;
static VkCommandBuffer g_xess_cmd;
static int g_res_rw, g_res_rh, g_res_dw, g_res_dh;
static int g_res_ok;

static void destroy_xess_res(void) {
  destroy_buf(&g_buf_color);
  destroy_buf(&g_buf_vel);
  destroy_buf(&g_buf_depth);
  destroy_buf(&g_buf_out);
  destroy_img(&g_img_color);
  destroy_img(&g_img_vel);
  destroy_img(&g_img_depth);
  destroy_img(&g_img_out);
  g_res_ok = 0;
}

static int ensure_xess_res(int rw, int rh, int dw, int dh) {
  if (g_res_ok && g_res_rw == rw && g_res_rh == rh && g_res_dw == dw && g_res_dh == dh &&
      g_img_color.image && g_buf_color.map && g_buf_out.map)
    return 1;
  destroy_xess_res();
  uint32_t sampled = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  uint32_t out_usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                       VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  int np = rw * rh;
  if (!make_img((uint32_t)rw, (uint32_t)rh, VK_FORMAT_R8G8B8A8_UNORM, sampled, &g_img_color) ||
      !make_img((uint32_t)rw, (uint32_t)rh, VK_FORMAT_R32G32_SFLOAT, sampled, &g_img_vel) ||
      !make_img((uint32_t)rw, (uint32_t)rh, VK_FORMAT_R32_SFLOAT, sampled, &g_img_depth) ||
      !make_img((uint32_t)dw, (uint32_t)dh, VK_FORMAT_R8G8B8A8_UNORM, out_usage, &g_img_out) ||
      !make_buf((VkDeviceSize)np * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &g_buf_color) ||
      !make_buf((VkDeviceSize)np * 2 * sizeof(float), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &g_buf_vel) ||
      !make_buf((VkDeviceSize)np * sizeof(float), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &g_buf_depth) ||
      !make_buf((VkDeviceSize)dw * dh * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &g_buf_out)) {
    destroy_xess_res();
    return 0;
  }
  if (g_buf_vel.map)
    memset(g_buf_vel.map, 0, (size_t)np * 2 * sizeof(float));
  g_res_rw = rw;
  g_res_rh = rh;
  g_res_dw = dw;
  g_res_dh = dh;
  g_res_ok = 1;
  return 1;
}

static void xess_pso_cache_path(char* out, size_t outsz, uint32_t dw, uint32_t dh) {
#ifdef _WIN32
  const char* tmp = getenv("TEMP");
  if (!tmp) tmp = getenv("TMP");
  if (!tmp) tmp = ".";
  snprintf(out, outsz, "%s\\farmos_xess_pso_%ux%u.bin", tmp, (unsigned)dw, (unsigned)dh);
#else
  snprintf(out, outsz, "/tmp/farmos_xess_pso_%ux%u.bin", (unsigned)dw, (unsigned)dh);
#endif
}

static void destroy_xess_pso_cache(void) {
  if (g_xess_pso_cache && vkDestroyPipelineCache && g_dev)
    vkDestroyPipelineCache(g_dev, g_xess_pso_cache, NULL);
  g_xess_pso_cache = 0;
  g_xess_pso_dw = 0;
  g_xess_pso_dh = 0;
  g_xess_pso_saved_exec = 0;
  g_xess_pso_loaded_sz = 0;
}

static void save_xess_pso_cache(uint32_t dw, uint32_t dh) {
  /* Warm AC-05 processes already have a disk blob; vkGetPipelineCacheData
     after execute is a per-process stall and was rewriting the same file. */
  if (g_xess_pso_loaded_sz > 0) return;
  if (!g_xess_pso_cache || !vkGetPipelineCacheData || !g_dev) return;
  size_t n = 0;
  if (vkGetPipelineCacheData(g_dev, g_xess_pso_cache, &n, NULL) != VK_SUCCESS) return;
  if (n == 0 || n >= 16u * 1024u * 1024u) return;
  void* blob = malloc(n);
  if (!blob) return;
  if (vkGetPipelineCacheData(g_dev, g_xess_pso_cache, &n, blob) == VK_SUCCESS) {
    char path[512];
    xess_pso_cache_path(path, sizeof(path), dw, dh);
    FILE* cf = fopen(path, "wb");
    if (cf) {
      fwrite(blob, 1, n, cf);
      fclose(cf);
    }
  }
  free(blob);
}

static void ensure_xess_pso_cache(uint32_t dw, uint32_t dh) {
  if (g_xess_pso_cache && g_xess_pso_dw == dw && g_xess_pso_dh == dh) return;
  destroy_xess_pso_cache();
  if (!vkCreatePipelineCache || !g_dev) return;
  char path[512];
  xess_pso_cache_path(path, sizeof(path), dw, dh);
  void* blob = NULL;
  size_t blob_sz = 0;
  FILE* cf = fopen(path, "rb");
  if (cf) {
    if (fseek(cf, 0, SEEK_END) == 0) {
      long sz = ftell(cf);
      if (sz > 0 && sz < 16 * 1024 * 1024) {
        blob_sz = (size_t)sz;
        blob = malloc(blob_sz);
        if (blob) {
          rewind(cf);
          if (fread(blob, 1, blob_sz, cf) != blob_sz) {
            free(blob);
            blob = NULL;
            blob_sz = 0;
          }
        }
      }
    }
    fclose(cf);
  }
  VkPipelineCacheCreateInfo cci;
  memset(&cci, 0, sizeof(cci));
  cci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  cci.initialDataSize = blob_sz;
  cci.pInitialData = blob;
  if (vkCreatePipelineCache(g_dev, &cci, NULL, &g_xess_pso_cache) != VK_SUCCESS)
    g_xess_pso_cache = 0;
  else {
    g_xess_pso_dw = dw;
    g_xess_pso_dh = dh;
    g_xess_pso_loaded_sz = blob_sz;
  }
  free(blob);
}

static void abandon_borrowed_vk(void) {
  destroy_xess_res();
  destroy_xess_pso_cache();
  g_xess_cmd = NULL;
  if (g_cpool && vkDestroyCommandPool && g_dev)
    vkDestroyCommandPool(g_dev, g_cpool, NULL);
  g_cpool = 0;
  g_inst = NULL;
  g_phys = NULL;
  g_dev = NULL;
  g_queue = NULL;
  g_vk_ready = 0;
  g_vk_borrowed = 0;
  g_borrow_failed = 1;
}

static VkImageSubresourceRange color_range(void) {
  VkImageSubresourceRange r;
  memset(&r, 0, sizeof(r));
  r.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  r.levelCount = 1;
  r.layerCount = 1;
  return r;
}

static void img_barriers_n(VkCommandBuffer cmd, const VkImage* imgs, int n,
                           uint32_t old_l, uint32_t new_l,
                           uint32_t src_a, uint32_t dst_a, uint32_t src_st, uint32_t dst_st) {
  VkImageMemoryBarrier b[4];
  memset(b, 0, sizeof(b));
  VkImageSubresourceRange rg = color_range();
  if (n > 4) n = 4;
  for (int i = 0; i < n; i++) {
    b[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b[i].srcAccessMask = src_a;
    b[i].dstAccessMask = dst_a;
    b[i].oldLayout = old_l;
    b[i].newLayout = new_l;
    b[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b[i].image = imgs[i];
    b[i].subresourceRange = rg;
  }
  vkCmdPipelineBarrier(cmd, src_st, dst_st, 0, 0, NULL, 0, NULL, (uint32_t)n, b);
}

static void img_barrier(VkCommandBuffer cmd, VkImage img, uint32_t old_l, uint32_t new_l,
                        uint32_t src_a, uint32_t dst_a, uint32_t src_st, uint32_t dst_st) {
  img_barriers_n(cmd, &img, 1, old_l, new_l, src_a, dst_a, src_st, dst_st);
}

static int begin_cmd(VkCommandBuffer* out) {
  if (!g_xess_cmd) {
    VkCommandBufferAllocateInfo cai;
    memset(&cai, 0, sizeof(cai));
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = g_cpool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(g_dev, &cai, &g_xess_cmd) != VK_SUCCESS) return 0;
  } else if (vkResetCommandBuffer(g_xess_cmd, 0) != VK_SUCCESS) {
    return 0;
  }
  VkCommandBufferBeginInfo bi;
  memset(&bi, 0, sizeof(bi));
  bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (vkBeginCommandBuffer(g_xess_cmd, &bi) != VK_SUCCESS) return 0;
  *out = g_xess_cmd;
  return 1;
}

static int submit_wait(VkCommandBuffer cmd) {
  if (vkEndCommandBuffer(cmd) != VK_SUCCESS) return 0;
  VkSubmitInfo si;
  memset(&si, 0, sizeof(si));
  si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  if (vkQueueSubmit(g_queue, 1, &si, VK_NULL_HANDLE) != VK_SUCCESS) return 0;
  if (vkQueueWaitIdle(g_queue) != VK_SUCCESS) return 0;
  return 1;
}

static void fill_view_info(xess_vk_image_view_info* v, const UpImg* img) {
  memset(v, 0, sizeof(*v));
  v->imageView = img->view;
  v->image = img->image;
  v->subresourceRange = color_range();
  v->format = img->format;
  v->width = img->w;
  v->height = img->h;
}

static void mul4(const double* a, const double* b, double* o) {
  for (int c = 0; c < 4; c++) {
    for (int r = 0; r < 4; r++) {
      o[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1] +
                     a[2 * 4 + r] * b[c * 4 + 2] + a[3 * 4 + r] * b[c * 4 + 3];
    }
  }
}

static void xform4(const double* m, double x, double y, double z, double w,
                   double* ox, double* oy, double* oz, double* ow) {
  *ox = m[0] * x + m[4] * y + m[8] * z + m[12] * w;
  *oy = m[1] * x + m[5] * y + m[9] * z + m[13] * w;
  *oz = m[2] * x + m[6] * y + m[10] * z + m[14] * w;
  *ow = m[3] * x + m[7] * y + m[11] * z + m[15] * w;
}

static int vp_close(const double* a, const double* b) {
  for (int i = 0; i < 16; i++) {
    if (fabs(a[i] - b[i]) > 1e-9) return 0;
  }
  return 1;
}

/* O(pixels): convert path-tracer first-hit t to linear depth; skip MV when the
   camera is static. Moving camera unprojects with t (no triangle walk). */
static void fill_aux(farm_Renderer* r, farm_Scene* scene, farm_PerspectiveCamera* camera,
                     float* depth, float* mv, int rw, int rh) {
  int np = rw * rh;
  if (!scene || !camera) {
    for (int i = 0; i < np; i++) depth[i] = 1.0f;
    return;
  }
  farm_Scene_updateMatrixWorld(scene, true);
  farm_PerspectiveCamera_updateMatrixWorld(camera, true);
  camera->matrixWorldInverse = farm_Matrix4_invert(camera->f_matrixWorld);
  farm_PerspectiveCamera_updateProjectionMatrix(camera);

  double vp[16];
  mul4(camera->projectionMatrix.elements, camera->matrixWorldInverse.elements, vp);

  double nplane = camera->f_near > 0.0 ? camera->f_near : 0.1;
  double fplane = camera->f_far > nplane ? camera->f_far : 100.0;
  int have_t = r->depthbuffer && r->width == rw && r->height == rh;
  int cam_moved = r->path_upscale_prev_vp_valid && !vp_close(vp, r->path_upscale_prev_vp);

  for (int i = 0; i < np; i++) depth[i] = 1.0f;
  if (cam_moved) {
    for (int i = 0; i < np; i++) {
      mv[i * 2 + 0] = 0.0f;
      mv[i * 2 + 1] = 0.0f;
    }
  }

  float jx = 0.f, jy = 0.f;
  if (cam_moved) farm_renderer_upscale_pixel_jitter(r, &jx, &jy);
  const double* e = camera->f_matrixWorld.elements;
  double ox = e[12], oy = e[13], oz = e[14];
  double tan_half = tan((3.14159265358979323846 / 180.0) * 0.5 * camera->f_fov);

  for (int y = 0; y < rh; y++) {
    for (int x = 0; x < rw; x++) {
      int idx = y * rw + x;
      double best_t = have_t ? (double)r->depthbuffer[idx] : 1e30;
      if (!(best_t > 1e-6) || best_t >= 1e29) continue;
      float dlin = (float)((best_t - nplane) / (fplane - nplane));
      if (dlin < 0.f) dlin = 0.f;
      if (dlin > 1.f) dlin = 1.f;
      depth[idx] = dlin;
      if (!cam_moved) continue;
      double px = (double)x + 0.5 + (double)jx;
      double py = (double)y + 0.5 + (double)jy;
      double ndc_x = (px / (double)rw) * 2.0 - 1.0;
      double ndc_y = 1.0 - (py / (double)rh) * 2.0;
      double cx = ndc_x * camera->f_aspect * tan_half;
      double cy = ndc_y * tan_half;
      double dx = e[0] * cx + e[4] * cy + e[8] * (-1.0);
      double dy = e[1] * cx + e[5] * cy + e[9] * (-1.0);
      double dz = e[2] * cx + e[6] * cy + e[10] * (-1.0);
      double len = sqrt(dx * dx + dy * dy + dz * dz);
      if (len < 1e-12) continue;
      dx /= len; dy /= len; dz /= len;
      double wx = ox + dx * best_t, wy = oy + dy * best_t, wz = oz + dz * best_t;
      double ccx, ccy, ccz, ccw, pcx, pcy, pcz, pcw;
      xform4(vp, wx, wy, wz, 1.0, &ccx, &ccy, &ccz, &ccw);
      xform4(r->path_upscale_prev_vp, wx, wy, wz, 1.0, &pcx, &pcy, &pcz, &pcw);
      if (fabs(ccw) > 1e-8 && fabs(pcw) > 1e-8) {
        mv[idx * 2 + 0] = (float)(ccx / ccw - pcx / pcw);
        mv[idx * 2 + 1] = (float)(ccy / ccw - pcy / pcw);
      }
    }
  }
  memcpy(r->path_upscale_prev_vp, vp, sizeof(vp));
  r->path_upscale_prev_vp_valid = 1;
}

static int xess_bringup(farm_Renderer* r, int rw, int rh, int dw, int dh) {
  if (!farm_upscale_probe_xess()) return 0;
  if (!ensure_vk()) return 0;
  if (!g_xess_ctx) {
    if (xessVKCreateContext_fn(g_inst, g_phys, g_dev, &g_xess_ctx) != XESS_RESULT_SUCCESS || !g_xess_ctx) {
      if (g_vk_borrowed) {
        abandon_borrowed_vk();
        if (!ensure_vk_own()) return 0;
        if (xessVKCreateContext_fn(g_inst, g_phys, g_dev, &g_xess_ctx) != XESS_RESULT_SUCCESS || !g_xess_ctx)
          return 0;
      } else {
        return 0;
      }
    }
  }
  if (!g_xess_inited || g_init_dw != dw || g_init_dh != dh) {
    xess_vk_init_params_t ip;
    memset(&ip, 0, sizeof(ip));
    ip.outputResolution.x = (uint32_t)dw;
    ip.outputResolution.y = (uint32_t)dh;
    ip.initFlags = (uint32_t)(XESS_INIT_FLAG_LDR_INPUT_COLOR | XESS_INIT_FLAG_USE_NDC_VELOCITY |
                              XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE);
    ip.creationNodeMask = 1;
    ip.visibleNodeMask = 1;
    ensure_xess_pso_cache((uint32_t)dw, (uint32_t)dh);
    ip.pipelineCache = g_xess_pso_cache;
    double sx = (double)dw / (double)rw, sy = (double)dh / (double)rh;
    double sc = sx < sy ? sx : sy;
    uint32_t quals[6];
    int nq = 0;
    if (sc < 1.15) quals[nq++] = (uint32_t)XESS_QUALITY_SETTING_AA;
    else if (sc < 1.40) quals[nq++] = (uint32_t)XESS_QUALITY_SETTING_ULTRA_QUALITY;
    else if (sc < 1.65) quals[nq++] = (uint32_t)XESS_QUALITY_SETTING_QUALITY;
    else if (sc < 2.15) quals[nq++] = (uint32_t)XESS_QUALITY_SETTING_BALANCED;
    else if (sc < 2.65) quals[nq++] = (uint32_t)XESS_QUALITY_SETTING_PERFORMANCE;
    else quals[nq++] = (uint32_t)XESS_QUALITY_SETTING_ULTRA_PERFORMANCE;
    static const uint32_t k_try[] = {
      (uint32_t)XESS_QUALITY_SETTING_BALANCED,
      (uint32_t)XESS_QUALITY_SETTING_QUALITY,
      (uint32_t)XESS_QUALITY_SETTING_PERFORMANCE,
      (uint32_t)XESS_QUALITY_SETTING_ULTRA_QUALITY,
      (uint32_t)XESS_QUALITY_SETTING_ULTRA_PERFORMANCE,
      (uint32_t)XESS_QUALITY_SETTING_AA
    };
    for (int i = 0; i < 6; i++) {
      int seen = 0;
      for (int j = 0; j < nq; j++) if (quals[j] == k_try[i]) { seen = 1; break; }
      if (!seen) quals[nq++] = k_try[i];
    }
    int inited = 0;
    for (int i = 0; i < nq; i++) {
      ip.qualitySetting = quals[i];
      if (xessVKInit_fn(g_xess_ctx, &ip) == XESS_RESULT_SUCCESS) { inited = 1; break; }
    }
    if (!inited) return 0;
    g_xess_inited = 1;
    g_init_dw = dw;
    g_init_dh = dh;
    if (r) r->path_upscale_hist_valid = 0;
    /* Do not vkGetPipelineCacheData here — it can stall the in-flight path GPU.
       Cold processes save after execute. */
  }
  return ensure_xess_res(rw, rh, dw, dh);
}

void farm_upscale_prepare_xess(farm_Renderer* r) {
  int rw = 0, rh = 0, dw = 0, dh = 0;
  if (!r || farm_renderer_upscale_mode(r) != 1) return;
  farm_renderer_upscale_dims(r, &rw, &rh, &dw, &dh);
  if (rw < 1 || rh < 1 || dw < rw || dh < rh) return;
  xess_bringup(r, rw, rh, dw, dh);
}

static int run_xess(farm_Renderer* r, farm_Scene* scene, farm_PerspectiveCamera* camera,
                    int rw, int rh, int dw, int dh) {
  if (!xess_bringup(r, rw, rh, dw, dh)) return 0;

  fill_aux(r, scene, camera, (float*)g_buf_depth.map, (float*)g_buf_vel.map, rw, rh);
  int np = rw * rh;
  uint32_t* rgba32 = (uint32_t*)g_buf_color.map;
  const uint8_t* fb = r->framebuffer;
  for (int i = 0; i < np; i++) {
    rgba32[i] = (uint32_t)fb[i * 3 + 0] | ((uint32_t)fb[i * 3 + 1] << 8) |
                ((uint32_t)fb[i * 3 + 2] << 16) | 0xff000000u;
  }

  VkCommandBuffer cmd = NULL;
  if (!begin_cmd(&cmd)) return 0;

  VkBufferImageCopy cpy;
  memset(&cpy, 0, sizeof(cpy));
  cpy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  cpy.imageSubresource.layerCount = 1;
  cpy.imageExtent.depth = 1;

  {
    VkImage ins[3] = { g_img_color.image, g_img_vel.image, g_img_depth.image };
    img_barriers_n(cmd, ins, 3, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
  }
  cpy.imageExtent.width = (uint32_t)rw; cpy.imageExtent.height = (uint32_t)rh;
  vkCmdCopyBufferToImage(cmd, g_buf_color.buf, g_img_color.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cpy);
  vkCmdCopyBufferToImage(cmd, g_buf_vel.buf, g_img_vel.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cpy);
  vkCmdCopyBufferToImage(cmd, g_buf_depth.buf, g_img_depth.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cpy);
  {
    VkImage ins[3] = { g_img_color.image, g_img_vel.image, g_img_depth.image };
    img_barriers_n(cmd, ins, 3, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  }
  img_barrier(cmd, g_img_out.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
              0, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

  xess_vk_execute_params_t ex;
  memset(&ex, 0, sizeof(ex));
  fill_view_info(&ex.colorTexture, &g_img_color);
  fill_view_info(&ex.velocityTexture, &g_img_vel);
  fill_view_info(&ex.depthTexture, &g_img_depth);
  fill_view_info(&ex.outputTexture, &g_img_out);
  float jx = 0.f, jy = 0.f;
  farm_renderer_upscale_pixel_jitter(r, &jx, &jy);
  ex.jitterOffsetX = jx;
  ex.jitterOffsetY = jy;
  ex.exposureScale = 1.0f;
  ex.resetHistory = r->path_upscale_hist_valid ? 0u : 1u;
  ex.inputWidth = (uint32_t)rw;
  ex.inputHeight = (uint32_t)rh;
  int32_t xrc = xessVKExecute_fn(g_xess_ctx, cmd, &ex);
  if (xrc != XESS_RESULT_SUCCESS) {
    submit_wait(cmd);
    return 0;
  }

  img_barrier(cmd, g_img_out.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
  VkBufferImageCopy oc;
  memset(&oc, 0, sizeof(oc));
  oc.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  oc.imageSubresource.layerCount = 1;
  oc.imageExtent.width = (uint32_t)dw;
  oc.imageExtent.height = (uint32_t)dh;
  oc.imageExtent.depth = 1;
  vkCmdCopyImageToBuffer(cmd, g_img_out.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_buf_out.buf, 1, &oc);
  if (!submit_wait(cmd)) return 0;
  if (!g_xess_pso_saved_exec) {
    save_xess_pso_cache((uint32_t)dw, (uint32_t)dh);
    g_xess_pso_saved_exec = 1;
  }

  uint8_t* src = (uint8_t*)g_buf_out.map;
  uint8_t* dst = (uint8_t*)farm_arena_alloc((size_t)dw * (size_t)dh * 3);
  if (!dst) return 0;
  int nout = dw * dh;
  for (int i = 0; i < nout; i++) {
    dst[i * 3 + 0] = src[i * 4 + 0];
    dst[i * 3 + 1] = src[i * 4 + 1];
    dst[i * 3 + 2] = src[i * 4 + 2];
  }
  r->framebuffer = dst;
  r->fb_width = dw;
  r->fb_height = dh;
  r->path_upscale_hist_valid = 1;
  r->path_upscale_frame += 1;
  return 1;
}

static void upscale_fail(farm_Renderer* r) {
  (void)r;
  if (env1("FARMOS_PATH_UPSCALE_STRICT"))
    farm_trap(122, "path upscale failed");
}

void farm_renderer_apply_path_upscale(farm_Renderer* r, farm_Scene* scene,
                                     farm_PerspectiveCamera* camera) {
  if (!r) return;
  int mode = farm_renderer_upscale_mode(r);
  if (mode == 0) return;
  if (env1("FARMOS_PATH_UPSCALE_FORCE_FAIL")) {
    upscale_fail(r);
    return;
  }
  /* CPU: not required to run (OQ-M12-06 A). Leave render-res; jitter was identity. */
  if (r->path_backend == 0) {
    upscale_fail(r);
    return;
  }
  int rw = 0, rh = 0, dw = 0, dh = 0;
  farm_renderer_upscale_dims(r, &rw, &rh, &dw, &dh);
  if (rw < 1 || rh < 1 || dw < rw || dh < rh) {
    upscale_fail(r);
    return;
  }
  int ok = 0;
  if (mode == 1)
    ok = run_xess(r, scene, camera, rw, rh, dw, dh);
  else if (mode == 2) {
    /* FSR2 is selectable; execute needs the official ffx shared lib. No Enabler. */
    ok = 0;
  }
  if (!ok) upscale_fail(r);
}
