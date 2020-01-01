#include "cgpu.h"
#include "resource_store.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <volk.h>

/* Internal structures. */

typedef struct cgpu_iinstance {
  VkInstance instance;
} cgpu_iinstance;

typedef struct cgpu_idevice {
  VkDevice                    logical_device;
  VkPhysicalDevice            physical_device;
  VkQueue                     compute_queue;
  VkCommandPool               command_pool;
  struct VolkDeviceTable      table;
  cgpu_physical_device_limits limits;
} cgpu_idevice;

typedef struct cgpu_ibuffer {
  VkBuffer       buffer;
  VkDeviceMemory memory;
  uint64_t       size_in_bytes;
} cgpu_ibuffer;

typedef struct cgpu_iimage {
  VkImage        image;
  VkDeviceMemory memory;
  uint64_t       size_in_bytes;
} cgpu_iimage;

typedef struct cgpu_ipipeline {
  VkPipeline            pipeline;
  VkPipelineLayout      layout;
  VkDescriptorSetLayout descriptor_set_layout;
  VkDescriptorSet       descriptor_set;
  VkDescriptorPool      descriptor_pool;
} cgpu_ipipeline;

typedef struct cgpu_ishader {
  VkShaderModule module;
} cgpu_ishader;

typedef struct cgpu_ifence {
  VkFence fence;
} cgpu_ifence;

typedef struct cgpu_icommand_buffer {
  VkCommandBuffer command_buffer;
} cgpu_icommand_buffer;

/* Handle and structure storage. */

static resource_store idevice_store;
static resource_store ishader_store;
static resource_store ibuffer_store;
static resource_store iimage_store;
static resource_store ipipeline_store;
static resource_store icommand_buffer_store;
static resource_store ifence_store;
static cgpu_iinstance iinstance;

/* Helper functions. */

#define CGPU_RESOLVE_HANDLE(                                   \
  RESOURCE_TYPE, IRESOURCE_TYPE, RESOURCE_STORE)               \
CGPU_INLINE bool cgpu_resolve_##RESOURCE_TYPE(                 \
  uint64_t handle,                                             \
  IRESOURCE_TYPE** idata)                                      \
{                                                              \
  if (!resource_store_get(                                     \
      &RESOURCE_STORE, handle, (void**) idata)) {              \
    return false;                                              \
  }                                                            \
  return true;                                                 \
}

CGPU_RESOLVE_HANDLE(device,         cgpu_idevice,         idevice_store        )
CGPU_RESOLVE_HANDLE(buffer,         cgpu_ibuffer,         ibuffer_store        )
CGPU_RESOLVE_HANDLE(shader,         cgpu_ishader,         ishader_store        )
CGPU_RESOLVE_HANDLE(image,          cgpu_iimage,          iimage_store         )
CGPU_RESOLVE_HANDLE(pipeline,       cgpu_ipipeline,       ipipeline_store      )
CGPU_RESOLVE_HANDLE(fence,          cgpu_ifence,          ifence_store         )
CGPU_RESOLVE_HANDLE(command_buffer, cgpu_icommand_buffer, icommand_buffer_store)

#undef CGPU_RESOLVE_HANDLE

static VkMemoryPropertyFlags cgpu_translate_memory_properties(
  CgpuMemoryPropertyFlags memory_properties)
{
  VkMemoryPropertyFlags mem_flags = 0;
  if ((memory_properties & CGPU_MEMORY_PROPERTY_FLAG_DEVICE_LOCAL)
        == CGPU_MEMORY_PROPERTY_FLAG_DEVICE_LOCAL) {
    mem_flags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  }
  if ((memory_properties & CGPU_MEMORY_PROPERTY_FLAG_HOST_VISIBLE)
        == CGPU_MEMORY_PROPERTY_FLAG_HOST_VISIBLE) {
    mem_flags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
  }
  if ((memory_properties & CGPU_MEMORY_PROPERTY_FLAG_HOST_COHERENT)
        == CGPU_MEMORY_PROPERTY_FLAG_HOST_COHERENT) {
    mem_flags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  }
  if ((memory_properties & CGPU_MEMORY_PROPERTY_FLAG_HOST_CACHED)
        == CGPU_MEMORY_PROPERTY_FLAG_HOST_CACHED) {
    mem_flags |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
  }
  if ((memory_properties & CGPU_MEMORY_PROPERTY_FLAG_LAZILY_ALLOCATED)
        == CGPU_MEMORY_PROPERTY_FLAG_LAZILY_ALLOCATED) {
    mem_flags |= VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
  }
  if ((memory_properties & CGPU_MEMORY_PROPERTY_FLAG_PROTECTED)
        == CGPU_MEMORY_PROPERTY_FLAG_PROTECTED) {
    mem_flags |= VK_MEMORY_PROPERTY_PROTECTED_BIT;
  }
  return mem_flags;
}

static VkAccessFlags cgpu_translate_access_flags(
  CgpuMemoryAccessFlags flags)
{
  VkAccessFlags vk_flags = 0;
  if ((flags & CGPU_MEMORY_ACCESS_FLAG_UNIFORM_READ)
              == CGPU_MEMORY_ACCESS_FLAG_UNIFORM_READ) {
    vk_flags |= VK_ACCESS_UNIFORM_READ_BIT;
  }
  if ((flags & CGPU_MEMORY_ACCESS_FLAG_SHADER_READ)
              == CGPU_MEMORY_ACCESS_FLAG_SHADER_READ) {
    vk_flags |= VK_ACCESS_SHADER_READ_BIT;
  }
  if ((flags & CGPU_MEMORY_ACCESS_FLAG_SHADER_WRITE)
              == CGPU_MEMORY_ACCESS_FLAG_SHADER_WRITE) {
    vk_flags |= VK_ACCESS_SHADER_WRITE_BIT;
  }
  if ((flags & CGPU_MEMORY_ACCESS_FLAG_TRANSFER_READ)
              == CGPU_MEMORY_ACCESS_FLAG_TRANSFER_READ) {
    vk_flags |= VK_ACCESS_TRANSFER_READ_BIT;
  }
  if ((flags & CGPU_MEMORY_ACCESS_FLAG_TRANSFER_WRITE)
              == CGPU_MEMORY_ACCESS_FLAG_TRANSFER_WRITE) {
    vk_flags |= VK_ACCESS_TRANSFER_WRITE_BIT;
  }
  if ((flags & CGPU_MEMORY_ACCESS_FLAG_HOST_READ)
              == CGPU_MEMORY_ACCESS_FLAG_HOST_READ) {
    vk_flags |= VK_ACCESS_HOST_READ_BIT;
  }
  if ((flags & CGPU_MEMORY_ACCESS_FLAG_HOST_WRITE)
              == CGPU_MEMORY_ACCESS_FLAG_HOST_WRITE) {
    vk_flags |= VK_ACCESS_HOST_WRITE_BIT;
  }
  if ((flags & CGPU_MEMORY_ACCESS_FLAG_MEMORY_READ)
              == CGPU_MEMORY_ACCESS_FLAG_MEMORY_READ) {
    vk_flags |= VK_ACCESS_MEMORY_READ_BIT;
  }
  if ((flags & CGPU_MEMORY_ACCESS_FLAG_MEMORY_WRITE)
              == CGPU_MEMORY_ACCESS_FLAG_MEMORY_WRITE) {
    vk_flags |= VK_ACCESS_MEMORY_WRITE_BIT;
  }
  return vk_flags;
}

static CgpuSampleCountFlags cgpu_translate_sample_count_flags(
  VkSampleCountFlags vk_flags)
{
  CgpuSampleCountFlags flags = 0;
  if ((vk_flags & VK_SAMPLE_COUNT_1_BIT)
        == VK_SAMPLE_COUNT_1_BIT) {
    flags |= CGPU_SAMPLE_COUNT_FLAG_1;
  }
  if ((vk_flags & VK_SAMPLE_COUNT_2_BIT)
        == VK_SAMPLE_COUNT_2_BIT) {
    flags |= CGPU_SAMPLE_COUNT_FLAG_2;
  }
  if ((vk_flags & VK_SAMPLE_COUNT_4_BIT)
        == VK_SAMPLE_COUNT_4_BIT) {
    flags |= CGPU_SAMPLE_COUNT_FLAG_4;
  }
  if ((vk_flags & VK_SAMPLE_COUNT_8_BIT)
        == VK_SAMPLE_COUNT_8_BIT) {
    flags |= CGPU_SAMPLE_COUNT_FLAG_8;
  }
  if ((vk_flags & VK_SAMPLE_COUNT_16_BIT)
        == VK_SAMPLE_COUNT_16_BIT) {
    flags |= CGPU_SAMPLE_COUNT_FLAG_16;
  }
  if ((vk_flags & VK_SAMPLE_COUNT_32_BIT)
        == VK_SAMPLE_COUNT_32_BIT) {
    flags |= CGPU_SAMPLE_COUNT_FLAG_32;
  }
  if ((vk_flags & VK_SAMPLE_COUNT_64_BIT)
        == VK_SAMPLE_COUNT_64_BIT) {
    flags |= CGPU_SAMPLE_COUNT_FLAG_64;
  }
  return flags;
}

static cgpu_physical_device_limits cgpu_translate_physical_device_limits(
  VkPhysicalDeviceLimits vk_limits)
{
  cgpu_physical_device_limits limits;
  limits.maxImageDimension1D = vk_limits.maxImageDimension1D;
  limits.maxImageDimension2D = vk_limits.maxImageDimension2D;
  limits.maxImageDimension3D = vk_limits.maxImageDimension3D;
  limits.maxImageDimensionCube = vk_limits.maxImageDimensionCube;
  limits.maxImageArrayLayers = vk_limits.maxImageArrayLayers;
  limits.maxTexelBufferElements = vk_limits.maxTexelBufferElements;
  limits.maxUniformBufferRange = vk_limits.maxUniformBufferRange;
  limits.maxStorageBufferRange = vk_limits.maxStorageBufferRange;
  limits.maxPushConstantsSize = vk_limits.maxPushConstantsSize;
  limits.maxMemoryAllocationCount = vk_limits.maxMemoryAllocationCount;
  limits.maxSamplerAllocationCount = vk_limits.maxSamplerAllocationCount;
  limits.bufferImageGranularity = vk_limits.bufferImageGranularity;
  limits.sparseAddressSpaceSize = vk_limits.sparseAddressSpaceSize;
  limits.maxBoundDescriptorSets = vk_limits.maxBoundDescriptorSets;
  limits.maxPerStageDescriptorSamplers = vk_limits.maxPerStageDescriptorSamplers;
  limits.maxPerStageDescriptorUniformBuffers = vk_limits.maxPerStageDescriptorUniformBuffers;
  limits.maxPerStageDescriptorStorageBuffers = vk_limits.maxPerStageDescriptorStorageBuffers;
  limits.maxPerStageDescriptorSampledImages = vk_limits.maxPerStageDescriptorSampledImages;
  limits.maxPerStageDescriptorStorageImages = vk_limits.maxPerStageDescriptorStorageImages;
  limits.maxPerStageDescriptorInputAttachments = vk_limits.maxPerStageDescriptorInputAttachments;
  limits.maxPerStageResources = vk_limits.maxPerStageResources;
  limits.maxDescriptorSetSamplers = vk_limits.maxDescriptorSetSamplers;
  limits.maxDescriptorSetUniformBuffers = vk_limits.maxDescriptorSetUniformBuffers;
  limits.maxDescriptorSetUniformBuffersDynamic = vk_limits.maxDescriptorSetUniformBuffersDynamic;
  limits.maxDescriptorSetStorageBuffers = vk_limits.maxDescriptorSetStorageBuffers;
  limits.maxDescriptorSetStorageBuffersDynamic = vk_limits.maxDescriptorSetStorageBuffersDynamic;
  limits.maxDescriptorSetSampledImages = vk_limits.maxDescriptorSetSampledImages;
  limits.maxDescriptorSetStorageImages = vk_limits.maxDescriptorSetStorageImages;
  limits.maxDescriptorSetInputAttachments = vk_limits.maxDescriptorSetInputAttachments;
  limits.maxVertexInputAttributes = vk_limits.maxVertexInputAttributes;
  limits.maxVertexInputBindings = vk_limits.maxVertexInputBindings;
  limits.maxVertexInputAttributeOffset = vk_limits.maxVertexInputAttributeOffset;
  limits.maxVertexInputBindingStride = vk_limits.maxVertexInputBindingStride;
  limits.maxVertexOutputComponents = vk_limits.maxVertexOutputComponents;
  limits.maxTessellationGenerationLevel = vk_limits.maxTessellationGenerationLevel;
  limits.maxTessellationPatchSize = vk_limits.maxTessellationPatchSize;
  limits.maxTessellationControlPerVertexInputComponents = vk_limits.maxTessellationControlPerVertexInputComponents;
  limits.maxTessellationControlPerVertexOutputComponents = vk_limits.maxTessellationControlPerVertexOutputComponents;
  limits.maxTessellationControlPerPatchOutputComponents = vk_limits.maxTessellationControlPerPatchOutputComponents;
  limits.maxTessellationControlTotalOutputComponents = vk_limits.maxTessellationControlTotalOutputComponents;
  limits.maxTessellationEvaluationInputComponents = vk_limits.maxTessellationEvaluationInputComponents;
  limits.maxTessellationEvaluationOutputComponents = vk_limits.maxTessellationEvaluationOutputComponents;
  limits.maxGeometryShaderInvocations = vk_limits.maxGeometryShaderInvocations;
  limits.maxGeometryInputComponents = vk_limits.maxGeometryInputComponents;
  limits.maxGeometryOutputComponents = vk_limits.maxGeometryOutputComponents;
  limits.maxGeometryOutputVertices = vk_limits.maxGeometryOutputVertices;
  limits.maxGeometryTotalOutputComponents = vk_limits.maxGeometryTotalOutputComponents;
  limits.maxFragmentInputComponents = vk_limits.maxFragmentInputComponents;
  limits.maxFragmentOutputAttachments = vk_limits.maxFragmentOutputAttachments;
  limits.maxFragmentDualSrcAttachments = vk_limits.maxFragmentDualSrcAttachments;
  limits.maxFragmentCombinedOutputResources = vk_limits.maxFragmentCombinedOutputResources;
  limits.maxComputeSharedMemorySize = vk_limits.maxComputeSharedMemorySize;
  limits.maxComputeWorkGroupCount[0] = vk_limits.maxComputeWorkGroupCount[0];
  limits.maxComputeWorkGroupCount[1] = vk_limits.maxComputeWorkGroupCount[1];
  limits.maxComputeWorkGroupCount[2] = vk_limits.maxComputeWorkGroupCount[2];
  limits.maxComputeWorkGroupInvocations = vk_limits.maxComputeWorkGroupInvocations;
  limits.maxComputeWorkGroupSize[0] = vk_limits.maxComputeWorkGroupSize[0];
  limits.maxComputeWorkGroupSize[1] = vk_limits.maxComputeWorkGroupSize[1];
  limits.maxComputeWorkGroupSize[2] = vk_limits.maxComputeWorkGroupSize[2];
  limits.subPixelPrecisionBits = vk_limits.subPixelPrecisionBits;
  limits.subTexelPrecisionBits = vk_limits.subTexelPrecisionBits;
  limits.mipmapPrecisionBits = vk_limits.mipmapPrecisionBits;
  limits.maxDrawIndexedIndexValue = vk_limits.maxDrawIndexedIndexValue;
  limits.maxDrawIndirectCount = vk_limits.maxDrawIndirectCount;
  limits.maxSamplerLodBias = vk_limits.maxSamplerLodBias;
  limits.maxSamplerAnisotropy = vk_limits.maxSamplerAnisotropy;
  limits.maxViewports = vk_limits.maxViewports;
  limits.maxViewportDimensions[0] = vk_limits.maxViewportDimensions[0];
  limits.maxViewportDimensions[1] = vk_limits.maxViewportDimensions[1];
  limits.viewportBoundsRange[0] = vk_limits.viewportBoundsRange[0];
  limits.viewportBoundsRange[1] = vk_limits.viewportBoundsRange[1];
  limits.viewportSubPixelBits = vk_limits.viewportSubPixelBits;
  limits.minMemoryMapAlignment = vk_limits.minMemoryMapAlignment;
  limits.minTexelBufferOffsetAlignment = vk_limits.minTexelBufferOffsetAlignment;
  limits.minUniformBufferOffsetAlignment = vk_limits.minUniformBufferOffsetAlignment;
  limits.minStorageBufferOffsetAlignment = vk_limits.minStorageBufferOffsetAlignment;
  limits.minTexelOffset = vk_limits.minTexelOffset;
  limits.maxTexelOffset = vk_limits.maxTexelOffset;
  limits.minTexelGatherOffset = vk_limits.minTexelGatherOffset;
  limits.maxTexelGatherOffset = vk_limits.maxTexelGatherOffset;
  limits.minInterpolationOffset = vk_limits.minInterpolationOffset;
  limits.maxInterpolationOffset = vk_limits.maxInterpolationOffset;
  limits.subPixelInterpolationOffsetBits = vk_limits.subPixelInterpolationOffsetBits;
  limits.maxFramebufferWidth = vk_limits.maxFramebufferWidth;
  limits.maxFramebufferHeight = vk_limits.maxFramebufferHeight;
  limits.maxFramebufferLayers = vk_limits.maxFramebufferLayers;
  limits.framebufferColorSampleCounts =
      cgpu_translate_sample_count_flags(vk_limits.framebufferColorSampleCounts);
  limits.framebufferDepthSampleCounts =
      cgpu_translate_sample_count_flags(vk_limits.framebufferDepthSampleCounts);
  limits.framebufferStencilSampleCounts = cgpu_translate_sample_count_flags(
      vk_limits.framebufferStencilSampleCounts);
  limits.framebufferNoAttachmentsSampleCounts =
      cgpu_translate_sample_count_flags(
          vk_limits.framebufferNoAttachmentsSampleCounts);
  limits.maxColorAttachments = vk_limits.maxColorAttachments;
  limits.sampledImageColorSampleCounts = cgpu_translate_sample_count_flags(
      vk_limits.sampledImageColorSampleCounts);
  limits.sampledImageIntegerSampleCounts = cgpu_translate_sample_count_flags(
      vk_limits.sampledImageIntegerSampleCounts);
  limits.sampledImageDepthSampleCounts = cgpu_translate_sample_count_flags(
      vk_limits.sampledImageDepthSampleCounts);
  limits.sampledImageStencilSampleCounts = cgpu_translate_sample_count_flags(
      vk_limits.sampledImageStencilSampleCounts);
  limits.storageImageSampleCounts =
      cgpu_translate_sample_count_flags(vk_limits.storageImageSampleCounts);
  limits.maxSampleMaskWords = vk_limits.maxSampleMaskWords;
  limits.timestampComputeAndGraphics = vk_limits.timestampComputeAndGraphics;
  limits.timestampPeriod = vk_limits.timestampPeriod;
  limits.maxClipDistances = vk_limits.maxClipDistances;
  limits.maxCullDistances = vk_limits.maxCullDistances;
  limits.maxCombinedClipAndCullDistances = vk_limits.maxCombinedClipAndCullDistances;
  limits.discreteQueuePriorities = vk_limits.discreteQueuePriorities;
  limits.pointSizeGranularity = vk_limits.pointSizeGranularity;
  limits.lineWidthGranularity = vk_limits.lineWidthGranularity;
  limits.strictLines = vk_limits.strictLines;
  limits.standardSampleLocations = vk_limits.standardSampleLocations;
  limits.optimalBufferCopyOffsetAlignment = vk_limits.optimalBufferCopyOffsetAlignment;
  limits.optimalBufferCopyRowPitchAlignment = vk_limits.optimalBufferCopyRowPitchAlignment;
  limits.nonCoherentAtomSize = vk_limits.nonCoherentAtomSize;
  return limits;
}

static VkFormat cgpu_translate_image_format(
  CgpuImageFormat image_format)
{
  if ((image_format & CGPU_IMAGE_FORMAT_UNDEFINED)
        == CGPU_IMAGE_FORMAT_UNDEFINED) { return VK_FORMAT_UNDEFINED; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R4G4_UNORM_PACK8)
        == CGPU_IMAGE_FORMAT_R4G4_UNORM_PACK8) { return VK_FORMAT_R4G4_UNORM_PACK8; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R4G4B4A4_UNORM_PACK16)
        == CGPU_IMAGE_FORMAT_R4G4B4A4_UNORM_PACK16) { return VK_FORMAT_R4G4B4A4_UNORM_PACK16; }
  else if ((image_format & CGPU_IMAGE_FORMAT_B4G4R4A4_UNORM_PACK16)
        == CGPU_IMAGE_FORMAT_B4G4R4A4_UNORM_PACK16) { return VK_FORMAT_B4G4R4A4_UNORM_PACK16; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R5G6B5_UNORM_PACK16)
        == CGPU_IMAGE_FORMAT_R5G6B5_UNORM_PACK16) { return VK_FORMAT_R5G6B5_UNORM_PACK16; }
  else if ((image_format & CGPU_IMAGE_FORMAT_B5G6R5_UNORM_PACK16)
        == CGPU_IMAGE_FORMAT_B5G6R5_UNORM_PACK16) { return VK_FORMAT_B5G6R5_UNORM_PACK16; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R5G5B5A1_UNORM_PACK16)
        == CGPU_IMAGE_FORMAT_R5G5B5A1_UNORM_PACK16) { return VK_FORMAT_R5G5B5A1_UNORM_PACK16; }
  else if ((image_format & CGPU_IMAGE_FORMAT_B5G5R5A1_UNORM_PACK16)
        == CGPU_IMAGE_FORMAT_B5G5R5A1_UNORM_PACK16) { return VK_FORMAT_B5G5R5A1_UNORM_PACK16; }
  else if ((image_format & CGPU_IMAGE_FORMAT_A1R5G5B5_UNORM_PACK16)
        == CGPU_IMAGE_FORMAT_A1R5G5B5_UNORM_PACK16) { return VK_FORMAT_A1R5G5B5_UNORM_PACK16; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R8_UNORM)
        == CGPU_IMAGE_FORMAT_R8_UNORM) { return VK_FORMAT_R8_UNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R8_SNORM)
        == CGPU_IMAGE_FORMAT_R8_SNORM) { return VK_FORMAT_R8_SNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R8_USCALED)
        == CGPU_IMAGE_FORMAT_R8_USCALED) { return VK_FORMAT_R8_USCALED; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R8_SSCALED)
        == CGPU_IMAGE_FORMAT_R8_SSCALED) { return VK_FORMAT_R8_SSCALED; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R8_UINT)
        == CGPU_IMAGE_FORMAT_R8_UINT) { return VK_FORMAT_R8_UINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R8_SINT)
        == CGPU_IMAGE_FORMAT_R8_SINT) { return VK_FORMAT_R8_SINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R8_SRGB)
        == CGPU_IMAGE_FORMAT_R8_SRGB) { return VK_FORMAT_R8_SRGB; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R8G8_UNORM)
        == CGPU_IMAGE_FORMAT_R8G8_UNORM) { return VK_FORMAT_R8G8_UNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R8G8_SNORM)
        == CGPU_IMAGE_FORMAT_R8G8_SNORM) { return VK_FORMAT_R8G8_SNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R8G8_USCALED)
        == CGPU_IMAGE_FORMAT_R8G8_USCALED) { return VK_FORMAT_R8G8_USCALED; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R8G8_SSCALED)
        == CGPU_IMAGE_FORMAT_R8G8_SSCALED) { return VK_FORMAT_R8G8_SSCALED; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R8G8_UINT)
        == CGPU_IMAGE_FORMAT_R8G8_UINT) { return VK_FORMAT_R8G8_UINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R8G8_SINT)
        == CGPU_IMAGE_FORMAT_R8G8_SINT) { return VK_FORMAT_R8G8_SINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R8G8_SRGB)
        == CGPU_IMAGE_FORMAT_R8G8_SRGB) { return VK_FORMAT_R8G8_SRGB; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R8G8B8_UNORM)
        == CGPU_IMAGE_FORMAT_R8G8B8_UNORM) { return VK_FORMAT_R8G8B8_UNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R8G8B8_SNORM)
        == CGPU_IMAGE_FORMAT_R8G8B8_SNORM) { return VK_FORMAT_R8G8B8_SNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R8G8B8_USCALED)
        == CGPU_IMAGE_FORMAT_R8G8B8_USCALED) { return VK_FORMAT_R8G8B8_USCALED; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R8G8B8_SSCALED)
        == CGPU_IMAGE_FORMAT_R8G8B8_SSCALED) { return VK_FORMAT_R8G8B8_SSCALED; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R8G8B8_UINT)
        == CGPU_IMAGE_FORMAT_R8G8B8_UINT) { return VK_FORMAT_R8G8B8_UINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R8G8B8_SINT)
        == CGPU_IMAGE_FORMAT_R8G8B8_SINT) { return VK_FORMAT_R8G8B8_SINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R8G8B8_SRGB)
        == CGPU_IMAGE_FORMAT_R8G8B8_SRGB) { return VK_FORMAT_R8G8B8_SRGB; }
  else if ((image_format & CGPU_IMAGE_FORMAT_B8G8R8_UNORM)
        == CGPU_IMAGE_FORMAT_B8G8R8_UNORM) { return VK_FORMAT_B8G8R8_UNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_B8G8R8_SNORM)
        == CGPU_IMAGE_FORMAT_B8G8R8_SNORM) { return VK_FORMAT_B8G8R8_SNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_B8G8R8_USCALED)
        == CGPU_IMAGE_FORMAT_B8G8R8_USCALED) { return VK_FORMAT_B8G8R8_USCALED; }
  else if ((image_format & CGPU_IMAGE_FORMAT_B8G8R8_SSCALED)
        == CGPU_IMAGE_FORMAT_B8G8R8_SSCALED) { return VK_FORMAT_B8G8R8_SSCALED; }
  else if ((image_format & CGPU_IMAGE_FORMAT_B8G8R8_UINT)
        == CGPU_IMAGE_FORMAT_B8G8R8_UINT) { return VK_FORMAT_B8G8R8_UINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_B8G8R8_SINT)
        == CGPU_IMAGE_FORMAT_B8G8R8_SINT) { return VK_FORMAT_B8G8R8_SINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_B8G8R8_SRGB)
        == CGPU_IMAGE_FORMAT_B8G8R8_SRGB) { return VK_FORMAT_B8G8R8_SRGB; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R8G8B8A8_UNORM)
        == CGPU_IMAGE_FORMAT_R8G8B8A8_UNORM) { return VK_FORMAT_R8G8B8A8_UNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R8G8B8A8_SNORM)
        == CGPU_IMAGE_FORMAT_R8G8B8A8_SNORM) { return VK_FORMAT_R8G8B8A8_SNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R8G8B8A8_USCALED)
        == CGPU_IMAGE_FORMAT_R8G8B8A8_USCALED) { return VK_FORMAT_R8G8B8A8_USCALED; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R8G8B8A8_SSCALED)
        == CGPU_IMAGE_FORMAT_R8G8B8A8_SSCALED) { return VK_FORMAT_R8G8B8A8_SSCALED; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R8G8B8A8_UINT)
        == CGPU_IMAGE_FORMAT_R8G8B8A8_UINT) { return VK_FORMAT_R8G8B8A8_UINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R8G8B8A8_SINT)
        == CGPU_IMAGE_FORMAT_R8G8B8A8_SINT) { return VK_FORMAT_R8G8B8A8_SINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R8G8B8A8_SRGB)
        == CGPU_IMAGE_FORMAT_R8G8B8A8_SRGB) { return VK_FORMAT_R8G8B8A8_SRGB; }
  else if ((image_format & CGPU_IMAGE_FORMAT_B8G8R8A8_UNORM)
        == CGPU_IMAGE_FORMAT_B8G8R8A8_UNORM) { return VK_FORMAT_B8G8R8A8_UNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_B8G8R8A8_SNORM)
        == CGPU_IMAGE_FORMAT_B8G8R8A8_SNORM) { return VK_FORMAT_B8G8R8A8_SNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_B8G8R8A8_USCALED)
        == CGPU_IMAGE_FORMAT_B8G8R8A8_USCALED) { return VK_FORMAT_B8G8R8A8_USCALED; }
  else if ((image_format & CGPU_IMAGE_FORMAT_B8G8R8A8_SSCALED)
        == CGPU_IMAGE_FORMAT_B8G8R8A8_SSCALED) { return VK_FORMAT_B8G8R8A8_SSCALED; }
  else if ((image_format & CGPU_IMAGE_FORMAT_B8G8R8A8_UINT)
        == CGPU_IMAGE_FORMAT_B8G8R8A8_UINT) { return VK_FORMAT_B8G8R8A8_UINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_B8G8R8A8_SINT)
        == CGPU_IMAGE_FORMAT_B8G8R8A8_SINT) { return VK_FORMAT_B8G8R8A8_SINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_B8G8R8A8_SRGB)
        == CGPU_IMAGE_FORMAT_B8G8R8A8_SRGB) { return VK_FORMAT_B8G8R8A8_SRGB; }
  else if ((image_format & CGPU_IMAGE_FORMAT_A8B8G8R8_UNORM_PACK32)
        == CGPU_IMAGE_FORMAT_A8B8G8R8_UNORM_PACK32) { return VK_FORMAT_A8B8G8R8_UNORM_PACK32; }
  else if ((image_format & CGPU_IMAGE_FORMAT_A8B8G8R8_SNORM_PACK32)
        == CGPU_IMAGE_FORMAT_A8B8G8R8_SNORM_PACK32) { return VK_FORMAT_A8B8G8R8_SNORM_PACK32; }
  else if ((image_format & CGPU_IMAGE_FORMAT_A8B8G8R8_USCALED_PACK32)
        == CGPU_IMAGE_FORMAT_A8B8G8R8_USCALED_PACK32) { return VK_FORMAT_A8B8G8R8_USCALED_PACK32; }
  else if ((image_format & CGPU_IMAGE_FORMAT_A8B8G8R8_SSCALED_PACK32)
        == CGPU_IMAGE_FORMAT_A8B8G8R8_SSCALED_PACK32) { return VK_FORMAT_A8B8G8R8_SSCALED_PACK32; }
  else if ((image_format & CGPU_IMAGE_FORMAT_A8B8G8R8_UINT_PACK32)
        == CGPU_IMAGE_FORMAT_A8B8G8R8_UINT_PACK32) { return VK_FORMAT_A8B8G8R8_UINT_PACK32; }
  else if ((image_format & CGPU_IMAGE_FORMAT_A8B8G8R8_SINT_PACK32)
        == CGPU_IMAGE_FORMAT_A8B8G8R8_SINT_PACK32) { return VK_FORMAT_A8B8G8R8_SINT_PACK32; }
  else if ((image_format & CGPU_IMAGE_FORMAT_A8B8G8R8_SRGB_PACK32)
        == CGPU_IMAGE_FORMAT_A8B8G8R8_SRGB_PACK32) { return VK_FORMAT_A8B8G8R8_SRGB_PACK32; }
  else if ((image_format & CGPU_IMAGE_FORMAT_A2R10G10B10_UNORM_PACK32)
        == CGPU_IMAGE_FORMAT_A2R10G10B10_UNORM_PACK32) { return VK_FORMAT_A2R10G10B10_UNORM_PACK32; }
  else if ((image_format & CGPU_IMAGE_FORMAT_A2R10G10B10_SNORM_PACK32)
        == CGPU_IMAGE_FORMAT_A2R10G10B10_SNORM_PACK32) { return VK_FORMAT_A2R10G10B10_SNORM_PACK32; }
  else if ((image_format & CGPU_IMAGE_FORMAT_A2R10G10B10_USCALED_PACK32)
        == CGPU_IMAGE_FORMAT_A2R10G10B10_USCALED_PACK32) { return VK_FORMAT_A2R10G10B10_USCALED_PACK32; }
  else if ((image_format & CGPU_IMAGE_FORMAT_A2R10G10B10_SSCALED_PACK32)
        == CGPU_IMAGE_FORMAT_A2R10G10B10_SSCALED_PACK32) { return VK_FORMAT_A2R10G10B10_SSCALED_PACK32; }
  else if ((image_format & CGPU_IMAGE_FORMAT_A2R10G10B10_UINT_PACK32)
        == CGPU_IMAGE_FORMAT_A2R10G10B10_UINT_PACK32) { return VK_FORMAT_A2R10G10B10_UINT_PACK32; }
  else if ((image_format & CGPU_IMAGE_FORMAT_A2R10G10B10_SINT_PACK32)
        == CGPU_IMAGE_FORMAT_A2R10G10B10_SINT_PACK32) { return VK_FORMAT_A2R10G10B10_SINT_PACK32; }
  else if ((image_format & CGPU_IMAGE_FORMAT_A2B10G10R10_UNORM_PACK32)
        == CGPU_IMAGE_FORMAT_A2B10G10R10_UNORM_PACK32) { return VK_FORMAT_A2B10G10R10_UNORM_PACK32; }
  else if ((image_format & CGPU_IMAGE_FORMAT_A2B10G10R10_SNORM_PACK32)
        == CGPU_IMAGE_FORMAT_A2B10G10R10_SNORM_PACK32) { return VK_FORMAT_A2B10G10R10_SNORM_PACK32; }
  else if ((image_format & CGPU_IMAGE_FORMAT_A2B10G10R10_USCALED_PACK32)
        == CGPU_IMAGE_FORMAT_A2B10G10R10_USCALED_PACK32) { return VK_FORMAT_A2B10G10R10_USCALED_PACK32; }
  else if ((image_format & CGPU_IMAGE_FORMAT_A2B10G10R10_SSCALED_PACK32)
        == CGPU_IMAGE_FORMAT_A2B10G10R10_SSCALED_PACK32) { return VK_FORMAT_A2B10G10R10_SSCALED_PACK32; }
  else if ((image_format & CGPU_IMAGE_FORMAT_A2B10G10R10_UINT_PACK32)
        == CGPU_IMAGE_FORMAT_A2B10G10R10_UINT_PACK32) { return VK_FORMAT_A2B10G10R10_UINT_PACK32; }
  else if ((image_format & CGPU_IMAGE_FORMAT_A2B10G10R10_SINT_PACK32)
        == CGPU_IMAGE_FORMAT_A2B10G10R10_SINT_PACK32) { return VK_FORMAT_A2B10G10R10_SINT_PACK32; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R16_UNORM)
        == CGPU_IMAGE_FORMAT_R16_UNORM) { return VK_FORMAT_R16_UNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R16_SNORM)
        == CGPU_IMAGE_FORMAT_R16_SNORM) { return VK_FORMAT_R16_SNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R16_USCALED)
        == CGPU_IMAGE_FORMAT_R16_USCALED) { return VK_FORMAT_R16_USCALED; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R16_SSCALED)
        == CGPU_IMAGE_FORMAT_R16_SSCALED) { return VK_FORMAT_R16_SSCALED; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R16_UINT)
        == CGPU_IMAGE_FORMAT_R16_UINT) { return VK_FORMAT_R16_UINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R16_SINT)
        == CGPU_IMAGE_FORMAT_R16_SINT) { return VK_FORMAT_R16_SINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R16_SFLOAT)
        == CGPU_IMAGE_FORMAT_R16_SFLOAT) { return VK_FORMAT_R16_SFLOAT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R16G16_UNORM)
        == CGPU_IMAGE_FORMAT_R16G16_UNORM) { return VK_FORMAT_R16G16_UNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R16G16_SNORM)
        == CGPU_IMAGE_FORMAT_R16G16_SNORM) { return VK_FORMAT_R16G16_SNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R16G16_USCALED)
        == CGPU_IMAGE_FORMAT_R16G16_USCALED) { return VK_FORMAT_R16G16_USCALED; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R16G16_SSCALED)
        == CGPU_IMAGE_FORMAT_R16G16_SSCALED) { return VK_FORMAT_R16G16_SSCALED; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R16G16_UINT)
        == CGPU_IMAGE_FORMAT_R16G16_UINT) { return VK_FORMAT_R16G16_UINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R16G16_SINT)
        == CGPU_IMAGE_FORMAT_R16G16_SINT) { return VK_FORMAT_R16G16_SINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R16G16_SFLOAT)
        == CGPU_IMAGE_FORMAT_R16G16_SFLOAT) { return VK_FORMAT_R16G16_SFLOAT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R16G16B16_UNORM)
        == CGPU_IMAGE_FORMAT_R16G16B16_UNORM) { return VK_FORMAT_R16G16B16_UNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R16G16B16_SNORM)
        == CGPU_IMAGE_FORMAT_R16G16B16_SNORM) { return VK_FORMAT_R16G16B16_SNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R16G16B16_USCALED)
        == CGPU_IMAGE_FORMAT_R16G16B16_USCALED) { return VK_FORMAT_R16G16B16_USCALED; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R16G16B16_SSCALED)
        == CGPU_IMAGE_FORMAT_R16G16B16_SSCALED) { return VK_FORMAT_R16G16B16_SSCALED; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R16G16B16_UINT)
        == CGPU_IMAGE_FORMAT_R16G16B16_UINT) { return VK_FORMAT_R16G16B16_UINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R16G16B16_SINT)
        == CGPU_IMAGE_FORMAT_R16G16B16_SINT) { return VK_FORMAT_R16G16B16_SINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R16G16B16_SFLOAT)
        == CGPU_IMAGE_FORMAT_R16G16B16_SFLOAT) { return VK_FORMAT_R16G16B16_SFLOAT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R16G16B16A16_UNORM)
        == CGPU_IMAGE_FORMAT_R16G16B16A16_UNORM) { return VK_FORMAT_R16G16B16A16_UNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R16G16B16A16_SNORM)
        == CGPU_IMAGE_FORMAT_R16G16B16A16_SNORM) { return VK_FORMAT_R16G16B16A16_SNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R16G16B16A16_USCALED)
        == CGPU_IMAGE_FORMAT_R16G16B16A16_USCALED) { return VK_FORMAT_R16G16B16A16_USCALED; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R16G16B16A16_SSCALED)
        == CGPU_IMAGE_FORMAT_R16G16B16A16_SSCALED) { return VK_FORMAT_R16G16B16A16_SSCALED; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R16G16B16A16_UINT)
        == CGPU_IMAGE_FORMAT_R16G16B16A16_UINT) { return VK_FORMAT_R16G16B16A16_UINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R16G16B16A16_SINT)
        == CGPU_IMAGE_FORMAT_R16G16B16A16_SINT) { return VK_FORMAT_R16G16B16A16_SINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R16G16B16A16_SFLOAT)
        == CGPU_IMAGE_FORMAT_R16G16B16A16_SFLOAT) { return VK_FORMAT_R16G16B16A16_SFLOAT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R32_UINT)
        == CGPU_IMAGE_FORMAT_R32_UINT) { return VK_FORMAT_R32_UINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R32_SINT)
        == CGPU_IMAGE_FORMAT_R32_SINT) { return VK_FORMAT_R32_SINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R32_SFLOAT)
        == CGPU_IMAGE_FORMAT_R32_SFLOAT) { return VK_FORMAT_R32_SFLOAT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R32G32_UINT)
        == CGPU_IMAGE_FORMAT_R32G32_UINT) { return VK_FORMAT_R32G32_UINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R32G32_SINT)
        == CGPU_IMAGE_FORMAT_R32G32_SINT) { return VK_FORMAT_R32G32_SINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R32G32_SFLOAT)
        == CGPU_IMAGE_FORMAT_R32G32_SFLOAT) { return VK_FORMAT_R32G32_SFLOAT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R32G32B32_UINT)
        == CGPU_IMAGE_FORMAT_R32G32B32_UINT) { return VK_FORMAT_R32G32B32_UINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R32G32B32_SINT)
        == CGPU_IMAGE_FORMAT_R32G32B32_SINT) { return VK_FORMAT_R32G32B32_SINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R32G32B32_SFLOAT)
        == CGPU_IMAGE_FORMAT_R32G32B32_SFLOAT) { return VK_FORMAT_R32G32B32_SFLOAT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R32G32B32A32_UINT)
        == CGPU_IMAGE_FORMAT_R32G32B32A32_UINT) { return VK_FORMAT_R32G32B32A32_UINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R32G32B32A32_SINT)
        == CGPU_IMAGE_FORMAT_R32G32B32A32_SINT) { return VK_FORMAT_R32G32B32A32_SINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R32G32B32A32_SFLOAT)
        == CGPU_IMAGE_FORMAT_R32G32B32A32_SFLOAT) { return VK_FORMAT_R32G32B32A32_SFLOAT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R64_UINT)
        == CGPU_IMAGE_FORMAT_R64_UINT) { return VK_FORMAT_R64_UINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R64_SINT)
        == CGPU_IMAGE_FORMAT_R64_SINT) { return VK_FORMAT_R64_SINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R64_SFLOAT)
        == CGPU_IMAGE_FORMAT_R64_SFLOAT) { return VK_FORMAT_R64_SFLOAT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R64G64_UINT)
        == CGPU_IMAGE_FORMAT_R64G64_UINT) { return VK_FORMAT_R64G64_UINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R64G64_SINT)
        == CGPU_IMAGE_FORMAT_R64G64_SINT) { return VK_FORMAT_R64G64_SINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R64G64_SFLOAT)
        == CGPU_IMAGE_FORMAT_R64G64_SFLOAT) { return VK_FORMAT_R64G64_SFLOAT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R64G64B64_UINT)
        == CGPU_IMAGE_FORMAT_R64G64B64_UINT) { return VK_FORMAT_R64G64B64_UINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R64G64B64_SINT)
        == CGPU_IMAGE_FORMAT_R64G64B64_SINT) { return VK_FORMAT_R64G64B64_SINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R64G64B64_SFLOAT)
        == CGPU_IMAGE_FORMAT_R64G64B64_SFLOAT) { return VK_FORMAT_R64G64B64_SFLOAT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R64G64B64A64_UINT)
        == CGPU_IMAGE_FORMAT_R64G64B64A64_UINT) { return VK_FORMAT_R64G64B64A64_UINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R64G64B64A64_SINT)
        == CGPU_IMAGE_FORMAT_R64G64B64A64_SINT) { return VK_FORMAT_R64G64B64A64_SINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R64G64B64A64_SFLOAT)
        == CGPU_IMAGE_FORMAT_R64G64B64A64_SFLOAT) { return VK_FORMAT_R64G64B64A64_SFLOAT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_B10G11R11_UFLOAT_PACK32)
        == CGPU_IMAGE_FORMAT_B10G11R11_UFLOAT_PACK32) { return VK_FORMAT_B10G11R11_UFLOAT_PACK32; }
  else if ((image_format & CGPU_IMAGE_FORMAT_E5B9G9R9_UFLOAT_PACK32)
        == CGPU_IMAGE_FORMAT_E5B9G9R9_UFLOAT_PACK32) { return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32; }
  else if ((image_format & CGPU_IMAGE_FORMAT_D16_UNORM)
        == CGPU_IMAGE_FORMAT_D16_UNORM) { return VK_FORMAT_D16_UNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_X8_D24_UNORM_PACK32)
        == CGPU_IMAGE_FORMAT_X8_D24_UNORM_PACK32) { return VK_FORMAT_X8_D24_UNORM_PACK32; }
  else if ((image_format & CGPU_IMAGE_FORMAT_D32_SFLOAT)
        == CGPU_IMAGE_FORMAT_D32_SFLOAT) { return VK_FORMAT_D32_SFLOAT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_S8_UINT)
        == CGPU_IMAGE_FORMAT_S8_UINT) { return VK_FORMAT_S8_UINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_D16_UNORM_S8_UINT)
        == CGPU_IMAGE_FORMAT_D16_UNORM_S8_UINT) { return VK_FORMAT_D16_UNORM_S8_UINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_D24_UNORM_S8_UINT)
        == CGPU_IMAGE_FORMAT_D24_UNORM_S8_UINT) { return VK_FORMAT_D24_UNORM_S8_UINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_D32_SFLOAT_S8_UINT)
        == CGPU_IMAGE_FORMAT_D32_SFLOAT_S8_UINT) { return VK_FORMAT_D32_SFLOAT_S8_UINT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_BC1_RGB_UNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_BC1_RGB_UNORM_BLOCK) { return VK_FORMAT_BC1_RGB_UNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_BC1_RGB_SRGB_BLOCK)
        == CGPU_IMAGE_FORMAT_BC1_RGB_SRGB_BLOCK) { return VK_FORMAT_BC1_RGB_SRGB_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_BC1_RGBA_UNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_BC1_RGBA_UNORM_BLOCK) { return VK_FORMAT_BC1_RGBA_UNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_BC1_RGBA_SRGB_BLOCK)
        == CGPU_IMAGE_FORMAT_BC1_RGBA_SRGB_BLOCK) { return VK_FORMAT_BC1_RGBA_SRGB_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_BC2_UNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_BC2_UNORM_BLOCK) { return VK_FORMAT_BC2_UNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_BC2_SRGB_BLOCK)
        == CGPU_IMAGE_FORMAT_BC2_SRGB_BLOCK) { return VK_FORMAT_BC2_SRGB_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_BC3_UNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_BC3_UNORM_BLOCK) { return VK_FORMAT_BC3_UNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_BC3_SRGB_BLOCK)
        == CGPU_IMAGE_FORMAT_BC3_SRGB_BLOCK) { return VK_FORMAT_BC3_SRGB_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_BC4_UNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_BC4_UNORM_BLOCK) { return VK_FORMAT_BC4_UNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_BC4_SNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_BC4_SNORM_BLOCK) { return VK_FORMAT_BC4_SNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_BC5_UNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_BC5_UNORM_BLOCK) { return VK_FORMAT_BC5_UNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_BC5_SNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_BC5_SNORM_BLOCK) { return VK_FORMAT_BC5_SNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_BC6H_UFLOAT_BLOCK)
        == CGPU_IMAGE_FORMAT_BC6H_UFLOAT_BLOCK) { return VK_FORMAT_BC6H_UFLOAT_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_BC6H_SFLOAT_BLOCK)
        == CGPU_IMAGE_FORMAT_BC6H_SFLOAT_BLOCK) { return VK_FORMAT_BC6H_SFLOAT_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_BC7_UNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_BC7_UNORM_BLOCK) { return VK_FORMAT_BC7_UNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_BC7_SRGB_BLOCK)
        == CGPU_IMAGE_FORMAT_BC7_SRGB_BLOCK) { return VK_FORMAT_BC7_SRGB_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ETC2_R8G8B8_UNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_ETC2_R8G8B8_UNORM_BLOCK) { return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ETC2_R8G8B8_SRGB_BLOCK)
        == CGPU_IMAGE_FORMAT_ETC2_R8G8B8_SRGB_BLOCK) { return VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK) { return VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK)
        == CGPU_IMAGE_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK) { return VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK) { return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK)
        == CGPU_IMAGE_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK) { return VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_EAC_R11_UNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_EAC_R11_UNORM_BLOCK) { return VK_FORMAT_EAC_R11_UNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_EAC_R11_SNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_EAC_R11_SNORM_BLOCK) { return VK_FORMAT_EAC_R11_SNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_EAC_R11G11_UNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_EAC_R11G11_UNORM_BLOCK) { return VK_FORMAT_EAC_R11G11_UNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_EAC_R11G11_SNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_EAC_R11G11_SNORM_BLOCK) { return VK_FORMAT_EAC_R11G11_SNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_4x4_UNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_ASTC_4x4_UNORM_BLOCK) { return VK_FORMAT_ASTC_4x4_UNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_4x4_SRGB_BLOCK)
        == CGPU_IMAGE_FORMAT_ASTC_4x4_SRGB_BLOCK) { return VK_FORMAT_ASTC_4x4_SRGB_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_5x4_UNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_ASTC_5x4_UNORM_BLOCK) { return VK_FORMAT_ASTC_5x4_UNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_5x4_SRGB_BLOCK)
        == CGPU_IMAGE_FORMAT_ASTC_5x4_SRGB_BLOCK) { return VK_FORMAT_ASTC_5x4_SRGB_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_5x5_UNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_ASTC_5x5_UNORM_BLOCK) { return VK_FORMAT_ASTC_5x5_UNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_5x5_SRGB_BLOCK)
        == CGPU_IMAGE_FORMAT_ASTC_5x5_SRGB_BLOCK) { return VK_FORMAT_ASTC_5x5_SRGB_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_6x5_UNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_ASTC_6x5_UNORM_BLOCK) { return VK_FORMAT_ASTC_6x5_UNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_6x5_SRGB_BLOCK)
        == CGPU_IMAGE_FORMAT_ASTC_6x5_SRGB_BLOCK) { return VK_FORMAT_ASTC_6x5_SRGB_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_6x6_UNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_ASTC_6x6_UNORM_BLOCK) { return VK_FORMAT_ASTC_6x6_UNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_6x6_SRGB_BLOCK)
        == CGPU_IMAGE_FORMAT_ASTC_6x6_SRGB_BLOCK) { return VK_FORMAT_ASTC_6x6_SRGB_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_8x5_UNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_ASTC_8x5_UNORM_BLOCK) { return VK_FORMAT_ASTC_8x5_UNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_8x5_SRGB_BLOCK)
        == CGPU_IMAGE_FORMAT_ASTC_8x5_SRGB_BLOCK) { return VK_FORMAT_ASTC_8x5_SRGB_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_8x6_UNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_ASTC_8x6_UNORM_BLOCK) { return VK_FORMAT_ASTC_8x6_UNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_8x6_SRGB_BLOCK)
        == CGPU_IMAGE_FORMAT_ASTC_8x6_SRGB_BLOCK) { return VK_FORMAT_ASTC_8x6_SRGB_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_8x8_UNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_ASTC_8x8_UNORM_BLOCK) { return VK_FORMAT_ASTC_8x8_UNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_8x8_SRGB_BLOCK)
        == CGPU_IMAGE_FORMAT_ASTC_8x8_SRGB_BLOCK) { return VK_FORMAT_ASTC_8x8_SRGB_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_10x5_UNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_ASTC_10x5_UNORM_BLOCK) { return VK_FORMAT_ASTC_10x5_UNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_10x5_SRGB_BLOCK)
        == CGPU_IMAGE_FORMAT_ASTC_10x5_SRGB_BLOCK) { return VK_FORMAT_ASTC_10x5_SRGB_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_10x6_UNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_ASTC_10x6_UNORM_BLOCK) { return VK_FORMAT_ASTC_10x6_UNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_10x6_SRGB_BLOCK)
        == CGPU_IMAGE_FORMAT_ASTC_10x6_SRGB_BLOCK) { return VK_FORMAT_ASTC_10x6_SRGB_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_10x8_UNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_ASTC_10x8_UNORM_BLOCK) { return VK_FORMAT_ASTC_10x8_UNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_10x8_SRGB_BLOCK)
        == CGPU_IMAGE_FORMAT_ASTC_10x8_SRGB_BLOCK) { return VK_FORMAT_ASTC_10x8_SRGB_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_10x10_UNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_ASTC_10x10_UNORM_BLOCK) { return VK_FORMAT_ASTC_10x10_UNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_10x10_SRGB_BLOCK)
        == CGPU_IMAGE_FORMAT_ASTC_10x10_SRGB_BLOCK) { return VK_FORMAT_ASTC_10x10_SRGB_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_12x10_UNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_ASTC_12x10_UNORM_BLOCK) { return VK_FORMAT_ASTC_12x10_UNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_12x10_SRGB_BLOCK)
        == CGPU_IMAGE_FORMAT_ASTC_12x10_SRGB_BLOCK) { return VK_FORMAT_ASTC_12x10_SRGB_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_12x12_UNORM_BLOCK)
        == CGPU_IMAGE_FORMAT_ASTC_12x12_UNORM_BLOCK) { return VK_FORMAT_ASTC_12x12_UNORM_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_12x12_SRGB_BLOCK)
        == CGPU_IMAGE_FORMAT_ASTC_12x12_SRGB_BLOCK) { return VK_FORMAT_ASTC_12x12_SRGB_BLOCK; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G8B8G8R8_422_UNORM)
        == CGPU_IMAGE_FORMAT_G8B8G8R8_422_UNORM) { return VK_FORMAT_G8B8G8R8_422_UNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_B8G8R8G8_422_UNORM)
        == CGPU_IMAGE_FORMAT_B8G8R8G8_422_UNORM) { return VK_FORMAT_B8G8R8G8_422_UNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G8_B8_R8_3PLANE_420_UNORM)
        == CGPU_IMAGE_FORMAT_G8_B8_R8_3PLANE_420_UNORM) { return VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G8_B8R8_2PLANE_420_UNORM)
        == CGPU_IMAGE_FORMAT_G8_B8R8_2PLANE_420_UNORM) { return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G8_B8_R8_3PLANE_422_UNORM)
        == CGPU_IMAGE_FORMAT_G8_B8_R8_3PLANE_422_UNORM) { return VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G8_B8R8_2PLANE_422_UNORM)
        == CGPU_IMAGE_FORMAT_G8_B8R8_2PLANE_422_UNORM) { return VK_FORMAT_G8_B8R8_2PLANE_422_UNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G8_B8_R8_3PLANE_444_UNORM)
        == CGPU_IMAGE_FORMAT_G8_B8_R8_3PLANE_444_UNORM) { return VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R10X6_UNORM_PACK16)
        == CGPU_IMAGE_FORMAT_R10X6_UNORM_PACK16) { return VK_FORMAT_R10X6_UNORM_PACK16; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R10X6G10X6_UNORM_2PACK16)
        == CGPU_IMAGE_FORMAT_R10X6G10X6_UNORM_2PACK16) { return VK_FORMAT_R10X6G10X6_UNORM_2PACK16; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16)
        == CGPU_IMAGE_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16) { return VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16)
        == CGPU_IMAGE_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16) { return VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16; }
  else if ((image_format & CGPU_IMAGE_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16)
        == CGPU_IMAGE_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16) { return VK_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16)
        == CGPU_IMAGE_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16) { return VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16)
        == CGPU_IMAGE_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16) { return VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16)
        == CGPU_IMAGE_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16) { return VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16)
        == CGPU_IMAGE_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16) { return VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16)
        == CGPU_IMAGE_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16) { return VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R12X4_UNORM_PACK16)
        == CGPU_IMAGE_FORMAT_R12X4_UNORM_PACK16) { return VK_FORMAT_R12X4_UNORM_PACK16; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R12X4G12X4_UNORM_2PACK16)
        == CGPU_IMAGE_FORMAT_R12X4G12X4_UNORM_2PACK16) { return VK_FORMAT_R12X4G12X4_UNORM_2PACK16; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16)
        == CGPU_IMAGE_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16) { return VK_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16)
        == CGPU_IMAGE_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16) { return VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16; }
  else if ((image_format & CGPU_IMAGE_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16)
        == CGPU_IMAGE_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16) { return VK_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16)
        == CGPU_IMAGE_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16) { return VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16)
        == CGPU_IMAGE_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16) { return VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16)
        == CGPU_IMAGE_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16) { return VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16)
        == CGPU_IMAGE_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16) { return VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16)
        == CGPU_IMAGE_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16) { return VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G16B16G16R16_422_UNORM)
        == CGPU_IMAGE_FORMAT_G16B16G16R16_422_UNORM) { return VK_FORMAT_G16B16G16R16_422_UNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_B16G16R16G16_422_UNORM)
        == CGPU_IMAGE_FORMAT_B16G16R16G16_422_UNORM) { return VK_FORMAT_B16G16R16G16_422_UNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G16_B16_R16_3PLANE_420_UNORM)
        == CGPU_IMAGE_FORMAT_G16_B16_R16_3PLANE_420_UNORM) { return VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G16_B16R16_2PLANE_420_UNORM)
        == CGPU_IMAGE_FORMAT_G16_B16R16_2PLANE_420_UNORM) { return VK_FORMAT_G16_B16R16_2PLANE_420_UNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G16_B16_R16_3PLANE_422_UNORM)
        == CGPU_IMAGE_FORMAT_G16_B16_R16_3PLANE_422_UNORM) { return VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G16_B16R16_2PLANE_422_UNORM)
        == CGPU_IMAGE_FORMAT_G16_B16R16_2PLANE_422_UNORM) { return VK_FORMAT_G16_B16R16_2PLANE_422_UNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G16_B16_R16_3PLANE_444_UNORM)
        == CGPU_IMAGE_FORMAT_G16_B16_R16_3PLANE_444_UNORM) { return VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM; }
  else if ((image_format & CGPU_IMAGE_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG)
        == CGPU_IMAGE_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG) { return VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG; }
  else if ((image_format & CGPU_IMAGE_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG)
        == CGPU_IMAGE_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG) { return VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG; }
  else if ((image_format & CGPU_IMAGE_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG)
        == CGPU_IMAGE_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG) { return VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG; }
  else if ((image_format & CGPU_IMAGE_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG)
        == CGPU_IMAGE_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG) { return VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG; }
  else if ((image_format & CGPU_IMAGE_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG)
        == CGPU_IMAGE_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG) { return VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG; }
  else if ((image_format & CGPU_IMAGE_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG)
        == CGPU_IMAGE_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG) { return VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG; }
  else if ((image_format & CGPU_IMAGE_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG)
        == CGPU_IMAGE_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG) { return VK_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG; }
  else if ((image_format & CGPU_IMAGE_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG)
        == CGPU_IMAGE_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG) { return VK_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_4x4_SFLOAT_BLOCK_EXT)
        == CGPU_IMAGE_FORMAT_ASTC_4x4_SFLOAT_BLOCK_EXT) { return VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK_EXT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_5x4_SFLOAT_BLOCK_EXT)
        == CGPU_IMAGE_FORMAT_ASTC_5x4_SFLOAT_BLOCK_EXT) { return VK_FORMAT_ASTC_5x4_SFLOAT_BLOCK_EXT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_5x5_SFLOAT_BLOCK_EXT)
        == CGPU_IMAGE_FORMAT_ASTC_5x5_SFLOAT_BLOCK_EXT) { return VK_FORMAT_ASTC_5x5_SFLOAT_BLOCK_EXT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_6x5_SFLOAT_BLOCK_EXT)
        == CGPU_IMAGE_FORMAT_ASTC_6x5_SFLOAT_BLOCK_EXT) { return VK_FORMAT_ASTC_6x5_SFLOAT_BLOCK_EXT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_6x6_SFLOAT_BLOCK_EXT)
        == CGPU_IMAGE_FORMAT_ASTC_6x6_SFLOAT_BLOCK_EXT) { return VK_FORMAT_ASTC_6x6_SFLOAT_BLOCK_EXT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_8x5_SFLOAT_BLOCK_EXT)
        == CGPU_IMAGE_FORMAT_ASTC_8x5_SFLOAT_BLOCK_EXT) { return VK_FORMAT_ASTC_8x5_SFLOAT_BLOCK_EXT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_8x6_SFLOAT_BLOCK_EXT)
        == CGPU_IMAGE_FORMAT_ASTC_8x6_SFLOAT_BLOCK_EXT) { return VK_FORMAT_ASTC_8x6_SFLOAT_BLOCK_EXT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_8x8_SFLOAT_BLOCK_EXT)
        == CGPU_IMAGE_FORMAT_ASTC_8x8_SFLOAT_BLOCK_EXT) { return VK_FORMAT_ASTC_8x8_SFLOAT_BLOCK_EXT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_10x5_SFLOAT_BLOCK_EXT)
        == CGPU_IMAGE_FORMAT_ASTC_10x5_SFLOAT_BLOCK_EXT) { return VK_FORMAT_ASTC_10x5_SFLOAT_BLOCK_EXT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_10x6_SFLOAT_BLOCK_EXT)
        == CGPU_IMAGE_FORMAT_ASTC_10x6_SFLOAT_BLOCK_EXT) { return VK_FORMAT_ASTC_10x6_SFLOAT_BLOCK_EXT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_10x8_SFLOAT_BLOCK_EXT)
        == CGPU_IMAGE_FORMAT_ASTC_10x8_SFLOAT_BLOCK_EXT) { return VK_FORMAT_ASTC_10x8_SFLOAT_BLOCK_EXT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_10x10_SFLOAT_BLOCK_EXT)
        == CGPU_IMAGE_FORMAT_ASTC_10x10_SFLOAT_BLOCK_EXT) { return VK_FORMAT_ASTC_10x10_SFLOAT_BLOCK_EXT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_12x10_SFLOAT_BLOCK_EXT)
        == CGPU_IMAGE_FORMAT_ASTC_12x10_SFLOAT_BLOCK_EXT) { return VK_FORMAT_ASTC_12x10_SFLOAT_BLOCK_EXT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_ASTC_12x12_SFLOAT_BLOCK_EXT)
        == CGPU_IMAGE_FORMAT_ASTC_12x12_SFLOAT_BLOCK_EXT) { return VK_FORMAT_ASTC_12x12_SFLOAT_BLOCK_EXT; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G8B8G8R8_422_UNORM_KHR)
        == CGPU_IMAGE_FORMAT_G8B8G8R8_422_UNORM_KHR) { return VK_FORMAT_G8B8G8R8_422_UNORM_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_B8G8R8G8_422_UNORM_KHR)
        == CGPU_IMAGE_FORMAT_B8G8R8G8_422_UNORM_KHR) { return VK_FORMAT_B8G8R8G8_422_UNORM_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G8_B8_R8_3PLANE_420_UNORM_KHR)
        == CGPU_IMAGE_FORMAT_G8_B8_R8_3PLANE_420_UNORM_KHR) { return VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR)
        == CGPU_IMAGE_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR) { return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G8_B8_R8_3PLANE_422_UNORM_KHR)
        == CGPU_IMAGE_FORMAT_G8_B8_R8_3PLANE_422_UNORM_KHR) { return VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G8_B8R8_2PLANE_422_UNORM_KHR)
        == CGPU_IMAGE_FORMAT_G8_B8R8_2PLANE_422_UNORM_KHR) { return VK_FORMAT_G8_B8R8_2PLANE_422_UNORM_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G8_B8_R8_3PLANE_444_UNORM_KHR)
        == CGPU_IMAGE_FORMAT_G8_B8_R8_3PLANE_444_UNORM_KHR) { return VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R10X6_UNORM_PACK16_KHR)
        == CGPU_IMAGE_FORMAT_R10X6_UNORM_PACK16_KHR) { return VK_FORMAT_R10X6_UNORM_PACK16_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R10X6G10X6_UNORM_2PACK16_KHR)
        == CGPU_IMAGE_FORMAT_R10X6G10X6_UNORM_2PACK16_KHR) { return VK_FORMAT_R10X6G10X6_UNORM_2PACK16_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16_KHR)
        == CGPU_IMAGE_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16_KHR) { return VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16_KHR)
        == CGPU_IMAGE_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16_KHR) { return VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16_KHR)
        == CGPU_IMAGE_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16_KHR) { return VK_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16_KHR)
      == CGPU_IMAGE_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16_KHR) { return VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16_KHR)
        == CGPU_IMAGE_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16_KHR) { return VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16_KHR)
        == CGPU_IMAGE_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16_KHR) { return VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16_KHR)
        == CGPU_IMAGE_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16_KHR) { return VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16_KHR)
        == CGPU_IMAGE_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16_KHR) { return VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R12X4_UNORM_PACK16_KHR)
        == CGPU_IMAGE_FORMAT_R12X4_UNORM_PACK16_KHR) { return VK_FORMAT_R12X4_UNORM_PACK16_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R12X4G12X4_UNORM_2PACK16_KHR)
        == CGPU_IMAGE_FORMAT_R12X4G12X4_UNORM_2PACK16_KHR) { return VK_FORMAT_R12X4G12X4_UNORM_2PACK16_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16_KHR)
        == CGPU_IMAGE_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16_KHR) { return VK_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16_KHR)
        == CGPU_IMAGE_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16_KHR) { return VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16_KHR)
        == CGPU_IMAGE_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16_KHR) { return VK_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16_KHR)
        == CGPU_IMAGE_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16_KHR) { return VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16_KHR)
        == CGPU_IMAGE_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16_KHR) { return VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16_KHR)
        == CGPU_IMAGE_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16_KHR) { return VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16_KHR)
        == CGPU_IMAGE_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16_KHR) { return VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16_KHR)
        == CGPU_IMAGE_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16_KHR) { return VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G16B16G16R16_422_UNORM_KHR)
        == CGPU_IMAGE_FORMAT_G16B16G16R16_422_UNORM_KHR) { return VK_FORMAT_G16B16G16R16_422_UNORM_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_B16G16R16G16_422_UNORM_KHR)
        == CGPU_IMAGE_FORMAT_B16G16R16G16_422_UNORM_KHR) { return VK_FORMAT_B16G16R16G16_422_UNORM_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G16_B16_R16_3PLANE_420_UNORM_KHR)
        == CGPU_IMAGE_FORMAT_G16_B16_R16_3PLANE_420_UNORM_KHR) { return VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G16_B16R16_2PLANE_420_UNORM_KHR)
        == CGPU_IMAGE_FORMAT_G16_B16R16_2PLANE_420_UNORM_KHR) { return VK_FORMAT_G16_B16R16_2PLANE_420_UNORM_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G16_B16_R16_3PLANE_422_UNORM_KHR)
        == CGPU_IMAGE_FORMAT_G16_B16_R16_3PLANE_422_UNORM_KHR) { return VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G16_B16R16_2PLANE_422_UNORM_KHR)
        == CGPU_IMAGE_FORMAT_G16_B16R16_2PLANE_422_UNORM_KHR) { return VK_FORMAT_G16_B16R16_2PLANE_422_UNORM_KHR; }
  else if ((image_format & CGPU_IMAGE_FORMAT_G16_B16_R16_3PLANE_444_UNORM_KHR)
        == CGPU_IMAGE_FORMAT_G16_B16_R16_3PLANE_444_UNORM_KHR) { return VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM_KHR; }
  return VK_FORMAT_UNDEFINED;
}

/* API method implementation. */

CgpuResult cgpu_initialize(
  const char* p_app_name,
  uint32_t version_major,
  uint32_t version_minor,
  uint32_t version_patch)
{
  VkResult result = volkInitialize();

  if (result != VK_SUCCESS) {
    return CGPU_FAIL_UNABLE_TO_INITIALIZE_VOLK;
  }

#ifndef NDEBUG
  const char* validation_layers[] = {
      "VK_LAYER_LUNARG_standard_validation"
  };
  const char* instance_extensions[] = {
      VK_EXT_DEBUG_UTILS_EXTENSION_NAME
  };
  uint32_t validation_layer_count = 1u;
  uint32_t instance_extension_count = 1u;
#else
  const char** validation_layers = NULL;
  uint32_t validation_layer_count = 0u;
  const char** instance_extensions = NULL;
  uint32_t instance_extension_count = 0u;
#endif

  VkApplicationInfo app_info = {};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pNext = NULL;
  app_info.pApplicationName = p_app_name;
  app_info.applicationVersion = VK_MAKE_VERSION(
    version_major,
    version_minor,
    version_patch
  );
  app_info.pEngineName = p_app_name;
  app_info.engineVersion = VK_MAKE_VERSION(
    version_major,
    version_minor,
    version_patch);
  app_info.apiVersion = VK_API_VERSION_1_1;

  VkInstanceCreateInfo create_info = {};
  create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  create_info.pNext = NULL;
  create_info.pApplicationInfo = &app_info;
  create_info.enabledExtensionCount = instance_extension_count;
  create_info.ppEnabledExtensionNames = instance_extensions;
  create_info.enabledLayerCount = validation_layer_count;
  create_info.ppEnabledLayerNames = validation_layers;

  result = vkCreateInstance(
    &create_info,
    NULL,
    &iinstance.instance
  );
  if (result != VK_SUCCESS) {
    return CGPU_FAIL_UNABLE_TO_INITIALIZE_VULKAN;
  }

  volkLoadInstance(iinstance.instance);

  resource_store_create(&idevice_store, sizeof(cgpu_idevice), 1u);
  resource_store_create(&ishader_store, sizeof(cgpu_ishader), 16u);
  resource_store_create(&ibuffer_store, sizeof(cgpu_ibuffer), 16u);
  resource_store_create(&iimage_store, sizeof(cgpu_iimage), 64u);
  resource_store_create(&ipipeline_store, sizeof(cgpu_ipipeline), 8u);
  resource_store_create(&icommand_buffer_store, sizeof(cgpu_icommand_buffer), 16u);
  resource_store_create(&ifence_store, sizeof(cgpu_ifence), 8u);

  return CGPU_OK;
}

CgpuResult cgpu_destroy(void)
{
  resource_store_destroy(&idevice_store);
  resource_store_destroy(&ishader_store);
  resource_store_destroy(&ibuffer_store);
  resource_store_destroy(&iimage_store);
  resource_store_destroy(&ipipeline_store);
  resource_store_destroy(&icommand_buffer_store);
  resource_store_destroy(&ifence_store);

  vkDestroyInstance(iinstance.instance, NULL);

  return CGPU_OK;
}

CgpuResult cgpu_get_device_count(uint32_t* p_device_count)
{
  vkEnumeratePhysicalDevices(
    iinstance.instance,
    p_device_count,
    NULL
  );
  return CGPU_OK;
}

CgpuResult cgpu_create_device(
  uint32_t index,
  uint32_t required_extension_count,
  const char** pp_required_extensions,
  cgpu_device* device)
{
  device->handle = resource_store_create_handle(&idevice_store);

  cgpu_idevice* idevice;
  if (!cgpu_resolve_device(device->handle, &idevice)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }

  uint32_t num_phys_devices;
  vkEnumeratePhysicalDevices(
    iinstance.instance,
    &num_phys_devices,
    NULL
  );

  if (num_phys_devices == 0u ||
      index > (num_phys_devices - 1u))
  {
    resource_store_free_handle(&idevice_store, device->handle);
    return CGPU_FAIL_NO_DEVICE_AT_INDEX;
  }

  VkPhysicalDevice* phys_devices =
      malloc(sizeof(VkPhysicalDevice) * num_phys_devices);

  vkEnumeratePhysicalDevices(
    iinstance.instance,
    &num_phys_devices,
    phys_devices
  );

  idevice->physical_device = phys_devices[index];
  free(phys_devices);

  VkPhysicalDeviceProperties device_properties;
  vkGetPhysicalDeviceProperties(
    idevice->physical_device,
    &device_properties
  );

  idevice->limits =
    cgpu_translate_physical_device_limits(device_properties.limits);

  uint32_t num_device_extensions;
  vkEnumerateDeviceExtensionProperties(
    idevice->physical_device,
    NULL,
    &num_device_extensions,
    NULL
  );

  VkExtensionProperties* device_extensions =
      malloc(sizeof(VkExtensionProperties) * num_device_extensions);

  vkEnumerateDeviceExtensionProperties(
    idevice->physical_device,
    NULL,
    &num_device_extensions,
    device_extensions
  );

  for (uint32_t i = 0u; i < required_extension_count; ++i)
  {
    const char* required_extension = *(pp_required_extensions + i);

    bool has_extension = false;
    for (uint32_t e = 0u; e < num_device_extensions; ++e) {
      const VkExtensionProperties* extension = &device_extensions[e];
      if (strcmp(extension->extensionName, required_extension) == 0) {
        has_extension = true;
        break;
      }
    }

    if (!has_extension) {
      free(device_extensions);
      resource_store_free_handle(&idevice_store, device->handle);
      return CGPU_FAIL_DEVICE_EXTENSION_NOT_SUPPORTED;
    }
  }
  free(device_extensions);

  uint32_t num_queue_families = 0u;
  vkGetPhysicalDeviceQueueFamilyProperties(
    idevice->physical_device,
    &num_queue_families,
    NULL
  );

  VkQueueFamilyProperties* queue_families =
      malloc(sizeof(VkQueueFamilyProperties) * num_queue_families);

  vkGetPhysicalDeviceQueueFamilyProperties(
    idevice->physical_device,
    &num_queue_families,
    queue_families
  );

  /* Since ray tracing is a continuous, compute-heavy task, we don't need
     to schedule work or translate command buffers very often. Therefore,
     we also don't need async execution and can operate on a single queue. */
  uint32_t queue_family_index = -1;
  for (uint32_t i = 0u; i < num_queue_families; ++i) {
    const VkQueueFamilyProperties* queue_family = &queue_families[i];
    if (queue_family->queueFlags & VK_QUEUE_COMPUTE_BIT) {
      queue_family_index = i;
    }
  }
  free(queue_families);
  if (queue_family_index == -1) {
    resource_store_free_handle(&idevice_store, device->handle);
    return CGPU_FAIL_DEVICE_HAS_NO_COMPUTE_QUEUE_FAMILY;
  }

  VkDeviceQueueCreateInfo queue_create_info = {};
  queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_create_info.pNext = NULL;
  queue_create_info.queueFamilyIndex = queue_family_index;
  queue_create_info.queueCount = 1u;
  const float queue_priority = 1.0f;
  queue_create_info.pQueuePriorities = &queue_priority;

  VkPhysicalDeviceFeatures device_features = {};

  VkDeviceCreateInfo device_create_info = {};
  device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_create_info.pNext = NULL;
  device_create_info.queueCreateInfoCount = 1;
  device_create_info.pQueueCreateInfos = &queue_create_info;
  device_create_info.pEnabledFeatures = &device_features;
  device_create_info.enabledExtensionCount = required_extension_count;
  device_create_info.ppEnabledExtensionNames = pp_required_extensions;
  /* These two fields are ignored by up-to-date implementations since
     nowadays, there is no difference to instance validation layers. */
  device_create_info.enabledLayerCount = 0u;
  device_create_info.ppEnabledLayerNames = NULL;

  VkResult result = vkCreateDevice(
    idevice->physical_device,
    &device_create_info,
    NULL,
    &idevice->logical_device
  );
  if (result != VK_SUCCESS) {
    resource_store_free_handle(&idevice_store, device->handle);
    return CGPU_FAIL_CAN_NOT_CREATE_LOGICAL_DEVICE;
  }

  volkLoadDeviceTable(
    &idevice->table,
    idevice->logical_device
  );

  vkGetDeviceQueue(
    idevice->logical_device,
    queue_family_index,
    0u,
    &idevice->compute_queue
  );

  VkCommandPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool_info.pNext = NULL;
  pool_info.queueFamilyIndex = queue_family_index;
  pool_info.flags = 0u;

  result = vkCreateCommandPool(
    idevice->logical_device,
    &pool_info,
    NULL,
    &idevice->command_pool
  );

  if (result != VK_SUCCESS)
  {
    resource_store_free_handle(&idevice_store, device->handle);

    vkDestroyDevice(
      idevice->logical_device,
      NULL
    );
    return CGPU_FAIL_CAN_NOT_CREATE_COMMAND_POOL;
  }

  return CGPU_OK;
}

CgpuResult cgpu_destroy_device(
  cgpu_device device)
{
  cgpu_idevice* idevice;
  if (!cgpu_resolve_device(device.handle, &idevice)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }

  vkDestroyCommandPool(
    idevice->logical_device,
    idevice->command_pool,
    NULL
  );

  vkDestroyDevice(
    idevice->logical_device,
    NULL
  );

  resource_store_free_handle(&idevice_store, device.handle);
  return CGPU_OK;
}

CgpuResult cgpu_create_shader(
  cgpu_device device,
  uint64_t source_size_in_bytes,
  const uint32_t* p_source,
  cgpu_shader* shader)
{
  cgpu_idevice* idevice;
  if (!cgpu_resolve_device(device.handle, &idevice)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }

  shader->handle = resource_store_create_handle(&ishader_store);

  cgpu_ishader* ishader;
  if (!cgpu_resolve_shader(shader->handle, &ishader)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }

  VkShaderModuleCreateInfo shader_module_create_info = {};
  shader_module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shader_module_create_info.pNext = NULL;
  shader_module_create_info.codeSize = source_size_in_bytes;
  shader_module_create_info.pCode = p_source;

  const VkResult result = vkCreateShaderModule(
    idevice->logical_device,
    &shader_module_create_info,
    NULL,
    &ishader->module
  );
  if (result != VK_SUCCESS) {
    resource_store_free_handle(&ishader_store, shader->handle);
    return CGPU_FAIL_UNABLE_TO_CREATE_SHADER_MODULE;
  }

  return CGPU_OK;
}

CgpuResult cgpu_destroy_shader(
  cgpu_device device,
  cgpu_shader shader)
{
  cgpu_idevice* idevice;
  cgpu_ishader* ishader;
  if (!cgpu_resolve_device(device.handle, &idevice)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  if (!cgpu_resolve_shader(shader.handle, &ishader)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }

  vkDestroyShaderModule(
    idevice->logical_device,
    ishader->module,
    NULL
  );

  resource_store_free_handle(&ishader_store, shader.handle);

  return CGPU_OK;
}

CgpuResult cgpu_create_buffer(
  cgpu_device device,
  CgpuBufferUsageFlags usage,
  CgpuMemoryPropertyFlags memory_properties,
  uint64_t size_in_bytes,
  cgpu_buffer* buffer)
{
  cgpu_idevice* idevice;
  if (!cgpu_resolve_device(device.handle, &idevice)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }

  buffer->handle = resource_store_create_handle(&ibuffer_store);

  cgpu_ibuffer* ibuffer;
  if (!cgpu_resolve_buffer(buffer->handle, &ibuffer)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }

  VkBufferUsageFlags vk_buffer_usage = 0u;
  if ((usage & CGPU_BUFFER_USAGE_FLAG_TRANSFER_SRC)
        == CGPU_BUFFER_USAGE_FLAG_TRANSFER_SRC) {
    vk_buffer_usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  }
  if ((usage & CGPU_BUFFER_USAGE_FLAG_TRANSFER_DST)
        == CGPU_BUFFER_USAGE_FLAG_TRANSFER_DST) {
    vk_buffer_usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  }
  if ((usage & CGPU_BUFFER_USAGE_FLAG_UNIFORM_BUFFER)
        == CGPU_BUFFER_USAGE_FLAG_UNIFORM_BUFFER) {
    vk_buffer_usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  }
  if ((usage & CGPU_BUFFER_USAGE_FLAG_STORAGE_BUFFER)
        == CGPU_BUFFER_USAGE_FLAG_STORAGE_BUFFER) {
    vk_buffer_usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  }
  if ((usage & CGPU_BUFFER_USAGE_FLAG_UNIFORM_TEXEL_BUFFER)
        == CGPU_BUFFER_USAGE_FLAG_UNIFORM_TEXEL_BUFFER) {
    vk_buffer_usage |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
  }
  if ((usage & CGPU_BUFFER_USAGE_FLAG_STORAGE_TEXEL_BUFFER)
        == CGPU_BUFFER_USAGE_FLAG_STORAGE_TEXEL_BUFFER) {
    vk_buffer_usage |= VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
  }

  VkBufferCreateInfo buffer_info = {};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.pNext = NULL;
  buffer_info.size = size_in_bytes;
  buffer_info.usage = vk_buffer_usage;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VkResult result = vkCreateBuffer(
    idevice->logical_device,
    &buffer_info,
    NULL,
    &ibuffer->buffer
  );
  if (result != VK_SUCCESS) {
    resource_store_free_handle(&ibuffer_store, buffer->handle);
    return CGPU_FAIL_UNABLE_TO_CREATE_BUFFER;
  }

  VkPhysicalDeviceMemoryProperties physical_device_memory_properties;
  vkGetPhysicalDeviceMemoryProperties(
    idevice->physical_device,
    &physical_device_memory_properties
  );

  VkMemoryRequirements mem_requirements;
  vkGetBufferMemoryRequirements(
    idevice->logical_device,
    ibuffer->buffer,
    &mem_requirements
  );

  VkMemoryAllocateInfo mem_alloc_info = {};
  mem_alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  mem_alloc_info.pNext = NULL;
  mem_alloc_info.allocationSize = mem_requirements.size;

  const VkMemoryPropertyFlags mem_flags =
      cgpu_translate_memory_properties(memory_properties);

  int32_t mem_index = -1;
  for (uint32_t i = 0u; i < physical_device_memory_properties.memoryTypeCount; ++i) {
    if ((mem_requirements.memoryTypeBits & (1u << i)) &&
        (physical_device_memory_properties.memoryTypes[i].propertyFlags & mem_flags) == mem_flags) {
      mem_index = i;
      break;
    }
  }
  if (mem_index == -1) {
    resource_store_free_handle(&ibuffer_store, buffer->handle);
    return CGPU_FAIL_NO_SUITABLE_MEMORY_TYPE;
  }
  mem_alloc_info.memoryTypeIndex = mem_index;

  result = vkAllocateMemory(
    idevice->logical_device,
    &mem_alloc_info,
    NULL,
    &ibuffer->memory
  );
  if (result != VK_SUCCESS) {
    resource_store_free_handle(&ibuffer_store, buffer->handle);
    return CGPU_FAIL_UNABLE_TO_ALLOCATE_MEMORY;
  }

  vkBindBufferMemory(
    idevice->logical_device,
    ibuffer->buffer,
    ibuffer->memory,
    0u
  );

  ibuffer->size_in_bytes = size_in_bytes;

  return CGPU_OK;
}

CgpuResult cgpu_destroy_buffer(
  cgpu_device device,
  cgpu_buffer buffer)
{
  cgpu_idevice* idevice;
  if (!cgpu_resolve_device(device.handle, &idevice)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  cgpu_ibuffer* ibuffer;
  if (!cgpu_resolve_buffer(buffer.handle, &ibuffer)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }

  vkDestroyBuffer(
    idevice->logical_device,
    ibuffer->buffer,
    NULL
  );
  vkFreeMemory(
    idevice->logical_device,
    ibuffer->memory,
    NULL
  );

  resource_store_free_handle(&ibuffer_store, buffer.handle);

  return CGPU_OK;
}

CgpuResult cgpu_map_buffer(
  cgpu_device device,
  cgpu_buffer buffer,
  uint64_t source_byte_offset,
  uint64_t byte_count,
  void** pp_mapped_mem)
{
  cgpu_idevice* idevice;
  if (!cgpu_resolve_device(device.handle, &idevice)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  cgpu_ibuffer* ibuffer;
  if (!cgpu_resolve_buffer(buffer.handle, &ibuffer)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }

  const VkResult result = vkMapMemory(
    idevice->logical_device,
    ibuffer->memory,
    source_byte_offset,
    (byte_count == CGPU_WHOLE_SIZE) ? ibuffer->size_in_bytes : byte_count,
    0u,
    pp_mapped_mem
  );

  if (result != VK_SUCCESS) {
    return CGPU_FAIL_UNABLE_TO_MAP_MEMORY;
  }

  return CGPU_OK;
}

CgpuResult cgpu_unmap_buffer(
  cgpu_device device,
  cgpu_buffer buffer)
{
  cgpu_idevice* idevice;
  if (!cgpu_resolve_device(device.handle, &idevice)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  cgpu_ibuffer* ibuffer;
  if (!cgpu_resolve_buffer(buffer.handle, &ibuffer)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  vkUnmapMemory(
    idevice->logical_device,
    ibuffer->memory
  );
  return CGPU_OK;
}

CgpuResult cgpu_create_image(
  cgpu_device device,
  uint32_t width,
  uint32_t height,
  CgpuImageFormat format,
  CgpuImageUsageFlags usage,
  CgpuMemoryPropertyFlags memory_properties,
  cgpu_image* image)
{
  cgpu_idevice* idevice;
  if (!cgpu_resolve_device(device.handle, &idevice)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }

  image->handle = resource_store_create_handle(&iimage_store);

  cgpu_iimage* iimage;
  if (!cgpu_resolve_image(image->handle, &iimage)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }

  VkImageTiling vk_image_tiling = VK_IMAGE_TILING_OPTIMAL;
  if ((usage & CGPU_IMAGE_USAGE_FLAG_TRANSFER_SRC)
        == CGPU_IMAGE_USAGE_FLAG_TRANSFER_SRC) {
    vk_image_tiling = VK_IMAGE_TILING_LINEAR;
  }
  else if ((usage & CGPU_IMAGE_USAGE_FLAG_TRANSFER_DST)
        == CGPU_IMAGE_USAGE_FLAG_TRANSFER_DST) {
    vk_image_tiling = VK_IMAGE_TILING_LINEAR;
  }

  VkImageUsageFlags vk_image_usage = 0;
  if ((usage & CGPU_IMAGE_USAGE_FLAG_TRANSFER_SRC)
        == CGPU_IMAGE_USAGE_FLAG_TRANSFER_SRC) {
    vk_image_usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  }
  if ((usage & CGPU_IMAGE_USAGE_FLAG_TRANSFER_DST)
        == CGPU_IMAGE_USAGE_FLAG_TRANSFER_DST) {
    vk_image_usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  }
  if ((usage & CGPU_IMAGE_USAGE_FLAG_SAMPLED)
        == CGPU_IMAGE_USAGE_FLAG_SAMPLED) {
    vk_image_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
  }
  if ((usage & CGPU_IMAGE_USAGE_FLAG_STORAGE)
        == CGPU_IMAGE_USAGE_FLAG_STORAGE) {
    vk_image_usage |= VK_IMAGE_USAGE_STORAGE_BIT;
  }

  const VkFormat vk_format = cgpu_translate_image_format(format);

  VkImageCreateInfo image_info = {};
  image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_info.pNext = NULL;
  image_info.imageType = VK_IMAGE_TYPE_2D;
  image_info.extent.width = width;
  image_info.extent.height = height;
  image_info.extent.depth = 1u;
  image_info.mipLevels = 1u;
  image_info.arrayLayers = 1u;
  image_info.format = vk_format;
  image_info.tiling = vk_image_tiling;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  image_info.usage = vk_image_usage;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VkResult result = vkCreateImage(
    idevice->logical_device,
    &image_info,
    NULL,
    &iimage->image
  );
  if (result != VK_SUCCESS) {
    resource_store_free_handle(&iimage_store, image->handle);
    return CGPU_FAIL_UNABLE_TO_CREATE_IMAGE;
  }

  VkPhysicalDeviceMemoryProperties physical_device_memory_properties;
  vkGetPhysicalDeviceMemoryProperties(
    idevice->physical_device,
    &physical_device_memory_properties
  );

  VkMemoryRequirements mem_requirements;
  vkGetImageMemoryRequirements(
    idevice->logical_device,
    iimage->image,
    &mem_requirements
  );

  VkMemoryAllocateInfo mem_alloc_info = {};
  mem_alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  mem_alloc_info.pNext = NULL;
  mem_alloc_info.allocationSize = mem_requirements.size;

  const VkMemoryPropertyFlags mem_flags =
      cgpu_translate_memory_properties(memory_properties);

  int32_t mem_index = -1;
  for (uint32_t i = 0u; i < physical_device_memory_properties.memoryTypeCount; ++i) {
    if ((mem_requirements.memoryTypeBits & (1u << i)) &&
        (physical_device_memory_properties.memoryTypes[i].propertyFlags & mem_flags) == mem_flags) {
      mem_index = i;
      break;
    }
  }
  if (mem_index == -1) {
    resource_store_free_handle(&iimage_store, image->handle);
    return CGPU_FAIL_NO_SUITABLE_MEMORY_TYPE;
  }
  mem_alloc_info.memoryTypeIndex = mem_index;

  result = vkAllocateMemory(
    idevice->logical_device,
    &mem_alloc_info,
    NULL,
    &iimage->memory
  );
  if (result != VK_SUCCESS) {
    resource_store_free_handle(&iimage_store, image->handle);
    return CGPU_FAIL_UNABLE_TO_ALLOCATE_MEMORY;
  }

  vkBindImageMemory(
    idevice->logical_device,
    iimage->image,
    iimage->memory,
    0u
  );

  iimage->size_in_bytes = mem_requirements.size;

  return CGPU_OK;
}

CgpuResult cgpu_destroy_image(
  cgpu_device device,
  cgpu_image image)
{
  cgpu_idevice* idevice;
  if (!cgpu_resolve_device(device.handle, &idevice)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  cgpu_iimage* iimage;
  if (!cgpu_resolve_image(image.handle, &iimage)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }

  vkDestroyImage(
    idevice->logical_device,
    iimage->image,
    NULL
  );

  resource_store_free_handle(&iimage_store, image.handle);

  return CGPU_OK;
}

CgpuResult cgpu_map_image(
  cgpu_device device,
  cgpu_image image,
  uint64_t source_byte_offset,
  uint64_t byte_count,
  void** pp_mapped_mem)
{
  cgpu_idevice* idevice;
  if (!cgpu_resolve_device(device.handle, &idevice)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  cgpu_iimage* iimage;
  if (!cgpu_resolve_image(image.handle, &iimage)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }

  const VkResult result = vkMapMemory(
    idevice->logical_device,
    iimage->memory,
    source_byte_offset,
    (byte_count == CGPU_WHOLE_SIZE) ? iimage->size_in_bytes : byte_count,
    0u,
    pp_mapped_mem
  );

  if (result != VK_SUCCESS) {
    return CGPU_FAIL_UNABLE_TO_MAP_MEMORY;
  }

  return CGPU_OK;
}

CgpuResult cgpu_unmap_image(
  cgpu_device device,
  cgpu_image image)
{
  cgpu_idevice* idevice;
  if (!cgpu_resolve_device(device.handle, &idevice)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  cgpu_iimage* iimage;
  if (!cgpu_resolve_image(image.handle, &iimage)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  vkUnmapMemory(
    idevice->logical_device,
    iimage->memory
  );
  return CGPU_OK;
}

CgpuResult cgpu_create_pipeline(
  cgpu_device device,
  uint32_t num_shader_resources_buffers,
  const cgpu_shader_resource_buffer* p_shader_resources_buffers,
  uint32_t num_shader_resources_images,
  const cgpu_shader_resource_image* p_shader_resources_images,
  cgpu_shader shader,
  const char* p_shader_entry_point,
  cgpu_pipeline* pipeline)
{
  cgpu_idevice* idevice;
  if (!cgpu_resolve_device(device.handle, &idevice)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  cgpu_ishader* ishader;
  if (!cgpu_resolve_shader(shader.handle, &ishader)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }

  pipeline->handle = resource_store_create_handle(&ipipeline_store);

  cgpu_ipipeline* ipipeline;
  if (!cgpu_resolve_pipeline(pipeline->handle, &ipipeline)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }

  const uint32_t num_descriptor_set_bindings =
      num_shader_resources_buffers + num_shader_resources_images;
  VkDescriptorSetLayoutBinding* descriptor_set_bindings =
    malloc(sizeof(VkDescriptorSetLayoutBinding) * num_descriptor_set_bindings);

  for (uint32_t i = 0u; i < num_shader_resources_buffers; ++i)
  {
    const cgpu_shader_resource_buffer* shader_resource_buffer = &p_shader_resources_buffers[i];
    VkDescriptorSetLayoutBinding* descriptor_set_layout_binding = &descriptor_set_bindings[i];
    descriptor_set_layout_binding->binding = shader_resource_buffer->binding;
    descriptor_set_layout_binding->descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptor_set_layout_binding->descriptorCount = 1u;
    descriptor_set_layout_binding->stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    descriptor_set_layout_binding->pImmutableSamplers = NULL;
  }

  for (uint32_t i = 0u; i < num_shader_resources_images; ++i)
  {
    const cgpu_shader_resource_image* shader_resource_buffer = &p_shader_resources_images[i];
    VkDescriptorSetLayoutBinding* descriptor_set_layout_binding =
        &descriptor_set_bindings[num_shader_resources_buffers + i];
    descriptor_set_layout_binding->binding = shader_resource_buffer->binding;
    descriptor_set_layout_binding->descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptor_set_layout_binding->descriptorCount = 1u;
    descriptor_set_layout_binding->stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    descriptor_set_layout_binding->pImmutableSamplers = NULL;
  }

  VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = {};
  descriptor_set_layout_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptor_set_layout_create_info.pNext = NULL;
  descriptor_set_layout_create_info.flags = 0u;
  descriptor_set_layout_create_info.bindingCount = num_descriptor_set_bindings;
  descriptor_set_layout_create_info.pBindings = descriptor_set_bindings;

  VkResult result = vkCreateDescriptorSetLayout(
    idevice->logical_device,
    &descriptor_set_layout_create_info,
    NULL,
    &ipipeline->descriptor_set_layout
  );
  free(descriptor_set_bindings);

  if (result != VK_SUCCESS) {
    resource_store_free_handle(&ipipeline_store, pipeline->handle);
    return CGPU_FAIL_UNABLE_TO_CREATE_DESCRIPTOR_LAYOUT;
  }

  VkPipelineLayoutCreateInfo pipeline_layout_create_info = {};
  pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_create_info.pNext = NULL;
  pipeline_layout_create_info.flags = 0u;
  pipeline_layout_create_info.setLayoutCount = 1u;
  pipeline_layout_create_info.pSetLayouts = &ipipeline->descriptor_set_layout;
  pipeline_layout_create_info.pushConstantRangeCount = 0u;
  pipeline_layout_create_info.pPushConstantRanges = NULL;

  result = vkCreatePipelineLayout(
    idevice->logical_device,
    &pipeline_layout_create_info,
    NULL,
    &ipipeline->layout
  );
  if (result != VK_SUCCESS) {
    resource_store_free_handle(&ipipeline_store, pipeline->handle);
    vkDestroyDescriptorSetLayout(
      idevice->logical_device,
      ipipeline->descriptor_set_layout,
      NULL
    );
    return CGPU_FAIL_UNABLE_TO_CREATE_PIPELINE_LAYOUT;
  }

  VkPipelineShaderStageCreateInfo pipeline_shader_stage_create_info = {};
  pipeline_shader_stage_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  pipeline_shader_stage_create_info.pNext = NULL;
  pipeline_shader_stage_create_info.flags = 0u;
  pipeline_shader_stage_create_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  pipeline_shader_stage_create_info.module = ishader->module;
  pipeline_shader_stage_create_info.pName = p_shader_entry_point;
  pipeline_shader_stage_create_info.pSpecializationInfo = NULL;

  VkComputePipelineCreateInfo pipeline_create_info = {};
  pipeline_create_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipeline_create_info.pNext = NULL;
  pipeline_create_info.flags = VK_PIPELINE_CREATE_DISPATCH_BASE;
  pipeline_create_info.stage = pipeline_shader_stage_create_info;
  pipeline_create_info.layout = ipipeline->layout;
  pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
  pipeline_create_info.basePipelineIndex = 0;

  result = vkCreateComputePipelines(
    idevice->logical_device,
    NULL,
    1u,
    &pipeline_create_info,
    NULL,
    &ipipeline->pipeline
  );
  if (result != VK_SUCCESS) {
    resource_store_free_handle(&ipipeline_store, pipeline->handle);
    vkDestroyPipelineLayout(
      idevice->logical_device,
      ipipeline->layout,
      NULL
    );
    vkDestroyDescriptorSetLayout(
      idevice->logical_device,
      ipipeline->descriptor_set_layout,
      NULL
    );
    return CGPU_FAIL_UNABLE_TO_CREATE_COMPUTE_PIPELINE;
  }

  VkDescriptorPoolSize descriptor_pool_size = {};
  descriptor_pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  descriptor_pool_size.descriptorCount = num_descriptor_set_bindings;

  VkDescriptorPoolCreateInfo descriptor_pool_create_info = {};
  descriptor_pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  descriptor_pool_create_info.pNext = NULL;
  descriptor_pool_create_info.poolSizeCount = 1u;
  descriptor_pool_create_info.pPoolSizes = &descriptor_pool_size;
  descriptor_pool_create_info.maxSets = 1u;

  result = vkCreateDescriptorPool(
    idevice->logical_device,
    &descriptor_pool_create_info,
    NULL,
    &ipipeline->descriptor_pool
  );
  if (result != VK_SUCCESS) {
    resource_store_free_handle(&ipipeline_store, pipeline->handle);
    vkDestroyPipeline(
      idevice->logical_device,
      ipipeline->pipeline,
      NULL
    );
    vkDestroyPipelineLayout(
      idevice->logical_device,
      ipipeline->layout,
      NULL
    );
    vkDestroyDescriptorSetLayout(
      idevice->logical_device,
      ipipeline->descriptor_set_layout,
      NULL
    );
    return CGPU_FAIL_UNABLE_TO_CREATE_DESCRIPTOR_POOL;
  }

  VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {};
  descriptor_set_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  descriptor_set_allocate_info.pNext = NULL;
  descriptor_set_allocate_info.descriptorPool = ipipeline->descriptor_pool;
  descriptor_set_allocate_info.descriptorSetCount = 1u;
  descriptor_set_allocate_info.pSetLayouts = &ipipeline->descriptor_set_layout;

  result = vkAllocateDescriptorSets(
    idevice->logical_device,
    &descriptor_set_allocate_info,
    &ipipeline->descriptor_set
  );
  if (result != VK_SUCCESS) {
    resource_store_free_handle(&ipipeline_store, pipeline->handle);
    vkDestroyDescriptorPool(
      idevice->logical_device,
      ipipeline->descriptor_pool,
      NULL
    );
    vkDestroyPipeline(
      idevice->logical_device,
      ipipeline->pipeline,
      NULL
    );
    vkDestroyPipelineLayout(
      idevice->logical_device,
      ipipeline->layout,
      NULL
    );
    vkDestroyDescriptorSetLayout(
      idevice->logical_device,
      ipipeline->descriptor_set_layout,
      NULL
    );
    return CGPU_FAIL_UNABLE_TO_ALLOCATE_DESCRIPTOR_SET;
  }

  VkDescriptorBufferInfo* descriptor_buffer_infos =
      malloc(sizeof(VkDescriptorBufferInfo) * num_shader_resources_buffers);
  VkDescriptorImageInfo* descriptor_image_infos =
      malloc(sizeof(VkDescriptorImageInfo) * num_shader_resources_images);

  const uint32_t num_max_write_descriptor_sets =
      num_shader_resources_buffers + num_shader_resources_images;
  VkWriteDescriptorSet* write_descriptor_sets =
      malloc(sizeof(VkWriteDescriptorSet) * num_max_write_descriptor_sets);

  uint32_t num_write_descriptor_sets = 0u;

  for (uint32_t i = 0u; i < num_shader_resources_buffers; ++i)
  {
    const cgpu_shader_resource_buffer* shader_resource_buffer = &p_shader_resources_buffers[i];

    cgpu_ibuffer* ibuffer;
    const cgpu_buffer buffer = shader_resource_buffer->buffer;
    if (!cgpu_resolve_buffer(buffer.handle, &ibuffer)) {
      return CGPU_FAIL_INVALID_HANDLE;
    }

    if ((shader_resource_buffer->offset % idevice->limits.minStorageBufferOffsetAlignment) != 0) {
      return CGPU_FAIL_BUFFER_OFFSET_NOT_ALIGNED;
    }

    VkDescriptorBufferInfo* descriptor_buffer_info = &descriptor_buffer_infos[i];
    descriptor_buffer_info->buffer = ibuffer->buffer;
    descriptor_buffer_info->offset = shader_resource_buffer->offset;
    descriptor_buffer_info->range =
      (shader_resource_buffer->count == CGPU_WHOLE_SIZE) ?
        ibuffer->size_in_bytes - shader_resource_buffer->offset :
        shader_resource_buffer->count;

    VkWriteDescriptorSet* write_descriptor_set = &write_descriptor_sets[num_write_descriptor_sets];
    write_descriptor_set->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_descriptor_set->pNext = NULL;
    write_descriptor_set->dstSet = ipipeline->descriptor_set;
    write_descriptor_set->dstBinding = shader_resource_buffer->binding;
    write_descriptor_set->dstArrayElement = 0u;
    write_descriptor_set->descriptorCount = 1u;
    write_descriptor_set->descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write_descriptor_set->pImageInfo = NULL;
    write_descriptor_set->pBufferInfo = descriptor_buffer_info;
    write_descriptor_set->pTexelBufferView = NULL;
    num_write_descriptor_sets++;
  }

  for (uint32_t i = 0u; i < num_shader_resources_images; ++i)
  {
    const cgpu_shader_resource_image* shader_resource_image = &p_shader_resources_images[i];

    cgpu_iimage* iimage;
    const cgpu_image image = shader_resource_image->image;
    if (!cgpu_resolve_image(image.handle, &iimage)) {
      return CGPU_FAIL_INVALID_HANDLE;
    }

    VkDescriptorImageInfo* descriptor_image_info = &descriptor_image_infos[i];
    // TODO:
    //descriptor_image_info.sampler = 0u;
    //descriptor_image_info.imageView = 0u;
    //descriptor_image_info.imageLayout = 0u;

    VkWriteDescriptorSet* write_descriptor_set =
      &write_descriptor_sets[num_write_descriptor_sets];
    write_descriptor_set->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_descriptor_set->pNext = NULL;
    write_descriptor_set->dstSet = ipipeline->descriptor_set;
    write_descriptor_set->dstBinding = shader_resource_image->binding;
    write_descriptor_set->dstArrayElement = 0u;
    write_descriptor_set->descriptorCount = 1u;
    write_descriptor_set->descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write_descriptor_set->pImageInfo = descriptor_image_info;
    write_descriptor_set->pBufferInfo = NULL;
    write_descriptor_set->pTexelBufferView = NULL;
    num_write_descriptor_sets++;
  }

  vkUpdateDescriptorSets(
    idevice->logical_device,
    num_write_descriptor_sets,
    write_descriptor_sets,
    0u,
    NULL
  );

  free(descriptor_buffer_infos);
  free(descriptor_image_infos);
  free(write_descriptor_sets);

  return CGPU_OK;
}

CgpuResult cgpu_destroy_pipeline(
  cgpu_device device,
  cgpu_pipeline pipeline)
{
  cgpu_idevice* idevice;
  if (!cgpu_resolve_device(device.handle, &idevice)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  cgpu_ipipeline* ipipeline;
  if (!cgpu_resolve_pipeline(pipeline.handle, &ipipeline)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }

  vkDestroyDescriptorPool(
    idevice->logical_device,
    ipipeline->descriptor_pool,
    NULL
  );
  vkDestroyPipeline(
    idevice->logical_device,
    ipipeline->pipeline,
    NULL
  );
  vkDestroyPipelineLayout(
    idevice->logical_device,
    ipipeline->layout,
    NULL
  );
  vkDestroyDescriptorSetLayout(
    idevice->logical_device,
    ipipeline->descriptor_set_layout,
    NULL
  );

  resource_store_free_handle(&ipipeline_store, pipeline.handle);

  return CGPU_OK;
}

CgpuResult cgpu_create_command_buffer(
  cgpu_device device,
  cgpu_command_buffer* command_buffer)
{
  cgpu_idevice* idevice;
  if (!cgpu_resolve_device(device.handle, &idevice)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }

  command_buffer->handle = resource_store_create_handle(&icommand_buffer_store);

  cgpu_icommand_buffer* icommand_buffer;
  if (!cgpu_resolve_command_buffer(command_buffer->handle, &icommand_buffer)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }

  VkCommandBufferAllocateInfo cmdbuf_alloc_info = {};
  cmdbuf_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdbuf_alloc_info.pNext = NULL;
  cmdbuf_alloc_info.commandPool = idevice->command_pool;
  cmdbuf_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdbuf_alloc_info.commandBufferCount = 1u;

  const VkResult result = vkAllocateCommandBuffers(
    idevice->logical_device,
    &cmdbuf_alloc_info,
    &icommand_buffer->command_buffer
  );
  if (result != VK_SUCCESS) {
    return CGPU_FAIL_UNABLE_TO_ALLOCATE_COMMAND_BUFFER;
  }

  return CGPU_OK;
}

CgpuResult cgpu_destroy_command_buffer(
  cgpu_device device,
  cgpu_command_buffer command_buffer)
{
  cgpu_idevice* idevice;
  if (!cgpu_resolve_device(device.handle, &idevice)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  cgpu_icommand_buffer* icommand_buffer;
  if (!cgpu_resolve_command_buffer(command_buffer.handle, &icommand_buffer)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }

  vkFreeCommandBuffers(
    idevice->logical_device,
    idevice->command_pool,
    1u,
    &icommand_buffer->command_buffer
  );

  resource_store_free_handle(&icommand_buffer_store, command_buffer.handle);
  return CGPU_OK;
}

CgpuResult cgpu_begin_command_buffer(
  cgpu_command_buffer command_buffer)
{
  cgpu_icommand_buffer* icommand_buffer;
  if (!cgpu_resolve_command_buffer(command_buffer.handle, &icommand_buffer)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }

  VkCommandBufferBeginInfo command_buffer_begin_info = {};
  command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  command_buffer_begin_info.pNext = NULL;
  command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; // TODO
  command_buffer_begin_info.pInheritanceInfo = NULL;

  const VkResult result = vkBeginCommandBuffer(
    icommand_buffer->command_buffer,
    &command_buffer_begin_info
  );

  if (result != VK_SUCCESS) {
    return CGPU_FAIL_UNABLE_TO_BEGIN_COMMAND_BUFFER;
  }
  return CGPU_OK;
}

CgpuResult cgpu_cmd_bind_pipeline(
  cgpu_command_buffer command_buffer,
  cgpu_pipeline pipeline)
{
  cgpu_icommand_buffer* icommand_buffer;
  if (!cgpu_resolve_command_buffer(command_buffer.handle, &icommand_buffer)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  cgpu_ipipeline* ipipeline;
  if (!cgpu_resolve_pipeline(pipeline.handle, &ipipeline)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  vkCmdBindPipeline(
    icommand_buffer->command_buffer,
    VK_PIPELINE_BIND_POINT_COMPUTE,
    ipipeline->pipeline
  );
  vkCmdBindDescriptorSets(
    icommand_buffer->command_buffer,
    VK_PIPELINE_BIND_POINT_COMPUTE,
    ipipeline->layout,
    0u,
    1u,
    &ipipeline->descriptor_set,
    0u,
    0u
  );
  return CGPU_OK;
}

CgpuResult cgpu_cmd_copy_buffer(
  cgpu_command_buffer command_buffer,
  cgpu_buffer source_buffer,
  uint64_t source_byte_offset,
  cgpu_buffer destination_buffer,
  uint64_t destination_byte_offset,
  uint64_t byte_count)
{
  cgpu_icommand_buffer* icommand_buffer;
  if (!cgpu_resolve_command_buffer(command_buffer.handle, &icommand_buffer)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  cgpu_ibuffer* isource_buffer;
  if (!cgpu_resolve_buffer(source_buffer.handle, &isource_buffer)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  cgpu_ibuffer* idestination_buffer;
  if (!cgpu_resolve_buffer(destination_buffer.handle, &idestination_buffer)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  VkBufferCopy region = {};
  region.srcOffset = source_byte_offset;
  region.dstOffset = destination_byte_offset;
  region.size = (byte_count == CGPU_WHOLE_SIZE) ?
      isource_buffer->size_in_bytes : byte_count;
  vkCmdCopyBuffer(
    icommand_buffer->command_buffer,
    isource_buffer->buffer,
    idestination_buffer->buffer,
    1u,
    &region
  );
  return CGPU_OK;
}

CgpuResult cgpu_cmd_dispatch(
  cgpu_command_buffer command_buffer,
  uint32_t dim_x,
  uint32_t dim_y,
  uint32_t dim_z)
{
  cgpu_icommand_buffer* icommand_buffer;
  if (!cgpu_resolve_command_buffer(command_buffer.handle, &icommand_buffer)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  vkCmdDispatch(
    icommand_buffer->command_buffer,
    dim_x,
    dim_y,
    dim_z
  );
  return CGPU_OK;
}

CgpuResult cgpu_cmd_pipeline_barrier(
  cgpu_command_buffer command_buffer,
  uint32_t num_memory_barriers,
  const cgpu_memory_barrier* p_memory_barriers,
  uint32_t num_buffer_memory_barriers,
  const cgpu_buffer_memory_barrier* p_buffer_memory_barriers,
  uint32_t num_image_memory_barriers,
  const cgpu_image_memory_barrier* p_image_memory_barriers)
{
  cgpu_icommand_buffer* icommand_buffer;
  if (!cgpu_resolve_command_buffer(command_buffer.handle, &icommand_buffer)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }

  VkMemoryBarrier* vk_memory_barriers = malloc(num_memory_barriers * sizeof(VkMemoryBarrier));

  for (uint32_t i = 0u; i < num_memory_barriers; ++i)
  {
    const cgpu_memory_barrier* b_cgpu = &p_memory_barriers[i];
    VkMemoryBarrier* b_vk = &vk_memory_barriers[i];
    b_vk->sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    b_vk->pNext = NULL;
    b_vk->srcAccessMask = cgpu_translate_access_flags(b_cgpu->src_access_flags);
    b_vk->dstAccessMask = cgpu_translate_access_flags(b_cgpu->dst_access_flags);
  }

  VkBufferMemoryBarrier* vk_buffer_memory_barriers
    = malloc(num_buffer_memory_barriers * sizeof(VkBufferMemoryBarrier));

  for (uint32_t i = 0u; i < num_buffer_memory_barriers; ++i)
  {
    const cgpu_buffer_memory_barrier* b_cgpu = &p_buffer_memory_barriers[i];

    cgpu_ibuffer* ibuffer;
    if (!cgpu_resolve_buffer(b_cgpu->buffer.handle, &ibuffer)) {
      return CGPU_FAIL_INVALID_HANDLE;
    }

    VkBufferMemoryBarrier* b_vk = &vk_buffer_memory_barriers[i];
    b_vk->sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    b_vk->pNext = NULL;
    b_vk->srcAccessMask = cgpu_translate_access_flags(b_cgpu->src_access_flags);
    b_vk->dstAccessMask = cgpu_translate_access_flags(b_cgpu->dst_access_flags);
    b_vk->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b_vk->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b_vk->buffer = ibuffer->buffer;
    b_vk->offset = b_cgpu->byte_offset;
    b_vk->size = b_cgpu->num_bytes;
  }

  // TODO: translate image barrier

  vkCmdPipelineBarrier(
    icommand_buffer->command_buffer,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
      VK_PIPELINE_STAGE_TRANSFER_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
      VK_PIPELINE_STAGE_TRANSFER_BIT,
    0u,
    num_memory_barriers,
    vk_memory_barriers,
    num_buffer_memory_barriers,
    vk_buffer_memory_barriers,
    0u,//TODO: num_image_memory_barriers,
    NULL//TODO" const VkImageMemoryBarrier* p_image_memory_barriers
  );

  free(vk_memory_barriers);
  free(vk_buffer_memory_barriers);

  return CGPU_OK;
}

CgpuResult cgpu_end_command_buffer(
  cgpu_command_buffer command_buffer)
{
  cgpu_icommand_buffer* icommand_buffer;
  if (!cgpu_resolve_command_buffer(command_buffer.handle, &icommand_buffer)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  vkEndCommandBuffer(icommand_buffer->command_buffer);
  return CGPU_OK;
}

CgpuResult cgpu_create_fence(
  cgpu_device device,
  cgpu_fence* fence)
{
  cgpu_idevice* idevice;
  if (!cgpu_resolve_device(device.handle, &idevice)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }

  fence->handle = resource_store_create_handle(&ifence_store);

  cgpu_ifence* ifence;
  if (!cgpu_resolve_fence(fence->handle, &ifence)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }

  VkFenceCreateInfo fence_create_info = {};
  fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence_create_info.pNext = NULL;
  fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  const VkResult result = vkCreateFence(
    idevice->logical_device,
    &fence_create_info,
    NULL,
    &ifence->fence
  );
  if (result != VK_SUCCESS) {
    resource_store_free_handle(&ifence_store, fence->handle);
    return CGPU_FAIL_UNABLE_TO_CREATE_FENCE;
  }
  return CGPU_OK;
}

CgpuResult cgpu_destroy_fence(
  cgpu_device device,
  cgpu_fence fence)
{
  cgpu_idevice* idevice;
  if (!cgpu_resolve_device(device.handle, &idevice)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  cgpu_ifence* ifence;
  if (!cgpu_resolve_fence(fence.handle, &ifence)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  vkDestroyFence(
    idevice->logical_device,
    ifence->fence,
    NULL
  );
  resource_store_free_handle(&ifence_store, fence.handle);
  return CGPU_OK;
}

CgpuResult cgpu_reset_fence(
  cgpu_device device,
  cgpu_fence fence)
{
  cgpu_idevice* idevice;
  if (!cgpu_resolve_device(device.handle, &idevice)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  cgpu_ifence* ifence;
  if (!cgpu_resolve_fence(fence.handle, &ifence)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  const VkResult result = vkResetFences(
    idevice->logical_device,
    1u,
    &ifence->fence
  );
  if (result != VK_SUCCESS) {
    return CGPU_FAIL_UNABLE_TO_RESET_FENCE;
  }
  return CGPU_OK;
}

CgpuResult cgpu_wait_for_fence(
  cgpu_device device,
  cgpu_fence fence)
{
  cgpu_idevice* idevice;
  if (!cgpu_resolve_device(device.handle, &idevice)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  cgpu_ifence* ifence;
  if (!cgpu_resolve_fence(fence.handle, &ifence)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  const VkResult result = vkWaitForFences(
    idevice->logical_device,
    1u,
    &ifence->fence,
    VK_TRUE,
    UINT64_MAX
  );
  if (result != VK_SUCCESS) {
    return CGPU_FAIL_UNABLE_TO_WAIT_FOR_FENCE;
  }
  return CGPU_OK;
}

CgpuResult cgpu_submit_command_buffer(
  cgpu_device device,
  cgpu_command_buffer command_buffer,
  cgpu_fence fence)
{
  cgpu_idevice* idevice;
  if (!cgpu_resolve_device(device.handle, &idevice)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  cgpu_icommand_buffer* icommand_buffer;
  if (!cgpu_resolve_command_buffer(command_buffer.handle, &icommand_buffer)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  cgpu_ifence* ifence;
  if (!cgpu_resolve_fence(fence.handle, &ifence)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }

  VkSubmitInfo submit_info = {};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.pNext = NULL;
  submit_info.waitSemaphoreCount = 0u;
  submit_info.pWaitSemaphores = NULL;
  submit_info.pWaitDstStageMask = NULL;
  submit_info.commandBufferCount = 1u;
  submit_info.pCommandBuffers = &icommand_buffer->command_buffer;
  submit_info.signalSemaphoreCount = 0u;
  submit_info.pSignalSemaphores = NULL;

  const VkResult result = vkQueueSubmit(
    idevice->compute_queue,
    1u,
    &submit_info,
    ifence->fence
  );

  if (result != VK_SUCCESS) {
    return CGPU_FAIL_UNABLE_TO_SUBMIT_COMMAND_BUFFER;
  }
  return CGPU_OK;
}

CgpuResult cgpu_flush_mapped_memory(
  cgpu_device device,
  cgpu_buffer buffer,
  uint64_t byte_offset,
  uint64_t byte_count)
{
  cgpu_idevice* idevice;
  if (!cgpu_resolve_device(device.handle, &idevice)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  cgpu_ibuffer* ibuffer;
  if (!cgpu_resolve_buffer(buffer.handle, &ibuffer)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }

  VkMappedMemoryRange memory_range;
  memory_range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
  memory_range.pNext = NULL;
  memory_range.memory = ibuffer->memory;
  memory_range.offset = byte_offset;
  memory_range.size =
    (byte_count == CGPU_WHOLE_SIZE) ? ibuffer->size_in_bytes : byte_count;

  const VkResult result = vkFlushMappedMemoryRanges(
    idevice->logical_device,
    1u,
    &memory_range
  );

  if (result != VK_SUCCESS) {
    return CGPU_FAIL_UNABLE_TO_INVALIDATE_MEMORY;
  }
  return CGPU_OK;
}

CgpuResult cgpu_invalidate_mapped_memory(
  cgpu_device device,
  cgpu_buffer buffer,
  uint64_t byte_offset,
  uint64_t byte_count)
{
  cgpu_idevice* idevice;
  if (!cgpu_resolve_device(device.handle, &idevice)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  cgpu_ibuffer* ibuffer;
  if (!cgpu_resolve_buffer(buffer.handle, &ibuffer)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }

  VkMappedMemoryRange memory_range = {};
  memory_range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
  memory_range.pNext = NULL;
  memory_range.memory = ibuffer->memory;
  memory_range.offset = byte_offset;
  memory_range.size =
    (byte_count == CGPU_WHOLE_SIZE) ? ibuffer->size_in_bytes : byte_count;

  const VkResult result = vkInvalidateMappedMemoryRanges(
    idevice->logical_device,
    1,
    &memory_range
  );

  if (result != VK_SUCCESS) {
    return CGPU_FAIL_UNABLE_TO_INVALIDATE_MEMORY;
  }
  return CGPU_OK;
}

CgpuResult cgpu_get_physical_device_limits(
  cgpu_device device,
  cgpu_physical_device_limits* limits)
{
  cgpu_idevice* idevice;
  if (!cgpu_resolve_device(device.handle, &idevice)) {
    return CGPU_FAIL_INVALID_HANDLE;
  }
  memcpy(
    limits,
    &idevice->limits,
    sizeof(cgpu_physical_device_limits)
  );
  return CGPU_OK;
}