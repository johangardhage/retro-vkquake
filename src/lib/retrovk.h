//
// Retro graphics library - generic Vulkan backend
//
// Author: Johan Gardhage <johan.gardhage@gmail.com>
//
// This header owns all the map-agnostic Vulkan setup: instance, surface,
// device, swapchain, depth buffer, render pass, framebuffers, command buffers
// and frame synchronisation, plus a handful of low-level helpers (buffers,
// images, mipmaps, shader modules) that the demo composes into its own
// pipeline and resources. On top of those it offers an optional RETROVK_Mesh
// object: a self-contained textured mesh the demo can own and draw without
// touching any Vulkan handle itself.
//
#ifndef _RETROVK_H_
#define _RETROVK_H_

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <stdio.h>  // printf
#include <stdlib.h> // exit
#include <string.h> // strcmp, memcpy
#include <stddef.h> // offsetof

#define RETROVK_MAX_FRAMES_IN_FLIGHT 2

// Validation layers are off by default; flip to true (and install the Vulkan
// validation layer) when diagnosing rendering problems.
static const bool RETROVK_ENABLE_VALIDATION = false;

struct {
	VkInstance instance = VK_NULL_HANDLE;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	uint32_t queueFamily = 0;
	VkQueue graphicsQueue = VK_NULL_HANDLE;	// Also used for presentation (single family)
	bool wireframeSupported = false;	// fillModeNonSolid (VK_POLYGON_MODE_LINE); absent on KosmicKrisp

	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	VkFormat swapchainFormat;
	VkExtent2D swapchainExtent;
	uint32_t swapchainImageCount = 0;
	VkImage *swapchainImages = NULL;
	VkImageView *swapchainImageViews = NULL;
	VkFramebuffer *framebuffers = NULL;

	VkFormat depthFormat;
	VkImage depthImage = VK_NULL_HANDLE;
	VkDeviceMemory depthMemory = VK_NULL_HANDLE;
	VkImageView depthImageView = VK_NULL_HANDLE;

	VkRenderPass renderPass = VK_NULL_HANDLE;
	VkCommandPool commandPool = VK_NULL_HANDLE;

	VkCommandBuffer commandBuffers[RETROVK_MAX_FRAMES_IN_FLIGHT];
	VkSemaphore imageAvailable[RETROVK_MAX_FRAMES_IN_FLIGHT];	// Per frame-in-flight
	VkSemaphore *renderFinished = NULL;	// Per swapchain image (present waits on it)
	VkFence inFlight[RETROVK_MAX_FRAMES_IN_FLIGHT];

	SDL_Window *window = NULL;			// Owning window, needed to rebuild the swapchain
	bool vsync = true;					// Present mode choice, applied by RETROVK_CreateSwapchain
	uint32_t currentFrame = 0;			// Frame-in-flight slot (0..MAX-1)
	uint32_t currentImageIndex = 0;		// Acquired swapchain image
	VkCommandBuffer currentCommandBuffer = VK_NULL_HANDLE; // Recorded by the demo this frame
} RETROVK;

// The vertex a RETROVK_Mesh consumes: world position plus base texture and lightmap coordinates.
struct RETROVK_Vertex {
	float pos[3];
	float uv[2];
	float lightUv[2];
};

// How the mesh is rasterised. Passed to RETROVK_PrepareMeshDraws and forwarded to
// the fragment shader as the push-constant `mode`.
enum RETROVK_RenderMode {
	RETROVK_RENDER_TEXTURED,	// Filled triangles, sampling the texture
	RETROVK_RENDER_SOLID,		// Filled triangles, flat colour per surface
	RETROVK_RENDER_WIREFRAME,	// Triangle edges (single colour)
	RETROVK_RENDER_POINTS,		// Vertices as points (single colour)
};

// The push constant block, matching the layout the shaders declare. Member order
// matches std430 so the C and GLSL layouts line up without padding.
struct RETROVK_PushConstants {
	float mvp[16];
	float color[3];
	int mode;
	float time;
	int surfaceFlags;
	float cameraOrigin[3];
};

// The demo-specific knobs of a graphics pipeline. Everything else (dynamic
// viewport/scissor, single-sample rasterisation, LEQUAL depth test + write,
// opaque colour blending, CCW front face, the backend render pass) is fixed by
// RETROVK_CreateGraphicsPipeline.
struct RETROVK_PipelineConfig {
	VkShaderModule vertShader;
	VkShaderModule fragShader;
	uint32_t vertexStride;								// Bytes per vertex (binding 0)
	const VkVertexInputAttributeDescription *attributes;
	uint32_t attributeCount;
	VkPipelineLayout layout;
	VkPrimitiveTopology topology;
	VkPolygonMode polygonMode;
	VkCullModeFlags cullMode;
};

// A RETROVK_Mesh bundles every Vulkan handle needed to draw a textured mesh:
// one device-local vertex buffer, a set of textures (each with its own descriptor
// set, sharing a sampler) and the pipelines for each render mode. The demo owns
// one and drives it through the mesh functions (defined after the
// helpers they build on); it never touches the handles directly.
struct RETROVK_Mesh {
	// Geometry: the whole mesh in one device-local vertex buffer
	VkBuffer vertexBuffer = VK_NULL_HANDLE;
	VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;

	// Textures: one image + descriptor set per slot, all sharing one sampler
	int numTextures = 0;
	VkImage *images = NULL;
	VkDeviceMemory *memories = NULL;
	VkImageView *views = NULL;
	VkDescriptorSet *descriptorSets = NULL;
	VkSampler sampler = VK_NULL_HANDLE;
	VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;

	// Fullbright/luma overlays: one transparent-or-luma texture per base texture.
	VkImage *lumaImages = NULL;
	VkDeviceMemory *lumaMemories = NULL;
	VkImageView *lumaViews = NULL;
	VkDescriptorSet *lumaDescriptorSets = NULL;
	VkDescriptorPool lumaDescriptorPool = VK_NULL_HANDLE;

	// Lightmaps: one atlas image sampled with clamp.
	int numLightmaps = 0;
	VkImage *lightImages = NULL;
	VkDeviceMemory *lightMemories = NULL;
	VkImageView *lightViews = NULL;
	VkDescriptorSet *lightDescriptorSets = NULL;
	VkSampler lightSampler = VK_NULL_HANDLE;
	VkDescriptorPool lightDescriptorPool = VK_NULL_HANDLE;

	// Pipelines (one layout). Textured and Solid share the filled-triangle
	// pipeline and differ only by the pushed mode; wireframe and points need their
	// own topology/polygon mode.
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	VkPipeline pipelineFill = VK_NULL_HANDLE;
	VkPipeline pipelineWireframe = VK_NULL_HANDLE;
	VkPipeline pipelinePoints = VK_NULL_HANDLE;
};

// *******************************************************************
// Private helpers
// *******************************************************************

static void RETROVK_Die(const char *message)
{
	printf("%s\n", message);
	exit(-1);
}

static void RETROVK_Check(VkResult result, const char *message)
{
	if (result != VK_SUCCESS) {
		printf("%s (VkResult %d)\n", message, result);
		exit(-1);
	}
}

// True when the named extension appears in the property list
static bool RETROVK_HasExtension(const VkExtensionProperties *props, uint32_t count, const char *name)
{
	for (uint32_t i = 0; i < count; i++) {
		if (strcmp(props[i].extensionName, name) == 0) {
			return true;
		}
	}
	return false;
}

// *******************************************************************
// Memory / buffer / image helpers (used by the demo)
// *******************************************************************

uint32_t RETROVK_FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(RETROVK.physicalDevice, &memProperties);
	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
		if ((typeFilter & (1 << i)) &&
			(memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
			return i;
		}
	}
	RETROVK_Die("RETROVK_FindMemoryType: no suitable memory type");
	return 0;
}

void RETROVK_CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
						  VkBuffer *buffer, VkDeviceMemory *memory)
{
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	RETROVK_Check(vkCreateBuffer(RETROVK.device, &bufferInfo, NULL, buffer), "vkCreateBuffer failed");

	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(RETROVK.device, *buffer, &memRequirements);

	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = RETROVK_FindMemoryType(memRequirements.memoryTypeBits, properties);
	RETROVK_Check(vkAllocateMemory(RETROVK.device, &allocInfo, NULL, memory), "vkAllocateMemory failed");

	vkBindBufferMemory(RETROVK.device, *buffer, *memory, 0);
}

void RETROVK_CreateImage(uint32_t width, uint32_t height, uint32_t mipLevels, VkFormat format,
						 VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
						 VkImage *image, VkDeviceMemory *memory)
{
	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = width;
	imageInfo.extent.height = height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = mipLevels;
	imageInfo.arrayLayers = 1;
	imageInfo.format = format;
	imageInfo.tiling = tiling;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = usage;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	RETROVK_Check(vkCreateImage(RETROVK.device, &imageInfo, NULL, image), "vkCreateImage failed");

	VkMemoryRequirements memRequirements;
	vkGetImageMemoryRequirements(RETROVK.device, *image, &memRequirements);

	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = RETROVK_FindMemoryType(memRequirements.memoryTypeBits, properties);
	RETROVK_Check(vkAllocateMemory(RETROVK.device, &allocInfo, NULL, memory), "vkAllocateMemory failed");

	vkBindImageMemory(RETROVK.device, *image, *memory, 0);
}

VkImageView RETROVK_CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect, uint32_t mipLevels)
{
	VkImageViewCreateInfo viewInfo = {};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = format;
	viewInfo.subresourceRange.aspectMask = aspect;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = mipLevels;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	VkImageView view;
	RETROVK_Check(vkCreateImageView(RETROVK.device, &viewInfo, NULL, &view), "vkCreateImageView failed");
	return view;
}

VkCommandBuffer RETROVK_BeginSingleTimeCommands(void)
{
	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandPool = RETROVK.commandPool;
	allocInfo.commandBufferCount = 1;

	VkCommandBuffer commandBuffer;
	vkAllocateCommandBuffers(RETROVK.device, &allocInfo, &commandBuffer);

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(commandBuffer, &beginInfo);
	return commandBuffer;
}

void RETROVK_EndSingleTimeCommands(VkCommandBuffer commandBuffer)
{
	vkEndCommandBuffer(commandBuffer);

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	vkQueueSubmit(RETROVK.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
	vkQueueWaitIdle(RETROVK.graphicsQueue);

	vkFreeCommandBuffers(RETROVK.device, RETROVK.commandPool, 1, &commandBuffer);
}

void RETROVK_CopyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size)
{
	VkCommandBuffer commandBuffer = RETROVK_BeginSingleTimeCommands();
	VkBufferCopy copyRegion = {};
	copyRegion.size = size;
	vkCmdCopyBuffer(commandBuffer, src, dst, 1, &copyRegion);
	RETROVK_EndSingleTimeCommands(commandBuffer);
}

// Create a device-local buffer of the given usage and fill it with data, copying
// through a temporary host-visible staging buffer. The staging buffer is freed
// before returning; TRANSFER_DST is added to the usage automatically.
void RETROVK_CreateDeviceLocalBuffer(const void *data, VkDeviceSize size, VkBufferUsageFlags usage,
									 VkBuffer *buffer, VkDeviceMemory *memory)
{
	VkBuffer staging;
	VkDeviceMemory stagingMemory;
	RETROVK_CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
						 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
						 &staging, &stagingMemory);
	void *mapped;
	vkMapMemory(RETROVK.device, stagingMemory, 0, size, 0, &mapped);
	memcpy(mapped, data, (size_t)size);
	vkUnmapMemory(RETROVK.device, stagingMemory);

	RETROVK_CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
						 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffer, memory);
	RETROVK_CopyBuffer(staging, *buffer, size);

	vkDestroyBuffer(RETROVK.device, staging, NULL);
	vkFreeMemory(RETROVK.device, stagingMemory, NULL);
}

// Destroy a buffer and free its memory (counterpart to RETROVK_CreateBuffer /
// RETROVK_CreateDeviceLocalBuffer).
void RETROVK_DestroyBuffer(VkBuffer buffer, VkDeviceMemory memory)
{
	vkDestroyBuffer(RETROVK.device, buffer, NULL);
	vkFreeMemory(RETROVK.device, memory, NULL);
}

void RETROVK_TransitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels)
{
	VkCommandBuffer commandBuffer = RETROVK_BeginSingleTimeCommands();

	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = mipLevels;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	VkPipelineStageFlags srcStage;
	VkPipelineStageFlags dstStage;
	if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	} else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	} else {
		RETROVK_Die("RETROVK_TransitionImageLayout: unsupported layout transition");
		return;
	}

	vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, NULL, 0, NULL, 1, &barrier);
	RETROVK_EndSingleTimeCommands(commandBuffer);
}

void RETROVK_CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
{
	VkCommandBuffer commandBuffer = RETROVK_BeginSingleTimeCommands();

	VkBufferImageCopy region = {};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageExtent.width = width;
	region.imageExtent.height = height;
	region.imageExtent.depth = 1;

	vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	RETROVK_EndSingleTimeCommands(commandBuffer);
}

// Generate the mip chain by successively blitting each level down by half.
// Replaces gluBuild2DMipmaps. Leaves the whole image in SHADER_READ_ONLY_OPTIMAL.
void RETROVK_GenerateMipmaps(VkImage image, int32_t width, int32_t height, uint32_t mipLevels)
{
	VkCommandBuffer commandBuffer = RETROVK_BeginSingleTimeCommands();

	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.image = image;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.subresourceRange.levelCount = 1;

	int32_t mipWidth = width;
	int32_t mipHeight = height;
	for (uint32_t i = 1; i < mipLevels; i++) {
		// Transition the previous level to a transfer source
		barrier.subresourceRange.baseMipLevel = i - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
							 0, 0, NULL, 0, NULL, 1, &barrier);

		VkImageBlit blit = {};
		blit.srcOffsets[0] = { 0, 0, 0 };
		blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.mipLevel = i - 1;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = 1;
		blit.dstOffsets[0] = { 0, 0, 0 };
		blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.mipLevel = i;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount = 1;
		vkCmdBlitImage(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					   image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

		// Previous level is done: hand it to the fragment shader
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
							 0, 0, NULL, 0, NULL, 1, &barrier);

		if (mipWidth > 1) mipWidth /= 2;
		if (mipHeight > 1) mipHeight /= 2;
	}

	// The last level was never a blit source and is still TRANSFER_DST
	barrier.subresourceRange.baseMipLevel = mipLevels - 1;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
						 0, 0, NULL, 0, NULL, 1, &barrier);

	RETROVK_EndSingleTimeCommands(commandBuffer);
}

// Number of mip levels for a width x height image (full chain down to 1x1)
uint32_t RETROVK_CountMipLevels(int width, int height)
{
	int size = width > height ? width : height;
	uint32_t levels = 1;
	while (size > 1) {
		size /= 2;
		levels++;
	}
	return levels;
}

// Upload one RGBA8 texture to a device-local, sampled image with a full mip
// chain, and create a matching image view. The pixels are staged through a
// host-visible buffer (freed before returning); the image ends in
// SHADER_READ_ONLY_OPTIMAL across every level.
void RETROVK_CreateTextureImage(int width, int height, const void *pixels,
								VkImage *image, VkDeviceMemory *memory, VkImageView *view)
{
	uint32_t mipLevels = RETROVK_CountMipLevels(width, height);
	VkDeviceSize imageSize = (VkDeviceSize)width * height * 4;

	// Stage the texels in a host-visible buffer
	VkBuffer staging;
	VkDeviceMemory stagingMemory;
	RETROVK_CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
						 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
						 &staging, &stagingMemory);
	void *data;
	vkMapMemory(RETROVK.device, stagingMemory, 0, imageSize, 0, &data);
	memcpy(data, pixels, (size_t)imageSize);
	vkUnmapMemory(RETROVK.device, stagingMemory);

	// The image is both a blit source (for mip generation) and destination
	RETROVK_CreateImage(width, height, mipLevels, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
						VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, memory);

	// Upload base level and generate mipmaps in one command submission. The old
	// path used separate one-shot submissions for layout transition, copy and mip
	// generation; doing that for every BSP texture made startup noticeably slow.
	VkCommandBuffer commandBuffer = RETROVK_BeginSingleTimeCommands();

	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = *image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = mipLevels;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.srcAccessMask = 0;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
						 0, 0, NULL, 0, NULL, 1, &barrier);

	VkBufferImageCopy region = {};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageExtent.width = width;
	region.imageExtent.height = height;
	region.imageExtent.depth = 1;
	vkCmdCopyBufferToImage(commandBuffer, staging, *image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	barrier.subresourceRange.levelCount = 1;
	int32_t mipWidth = width;
	int32_t mipHeight = height;
	for (uint32_t i = 1; i < mipLevels; i++) {
		barrier.subresourceRange.baseMipLevel = i - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
							 0, 0, NULL, 0, NULL, 1, &barrier);

		VkImageBlit blit = {};
		blit.srcOffsets[0] = { 0, 0, 0 };
		blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.mipLevel = i - 1;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = 1;
		blit.dstOffsets[0] = { 0, 0, 0 };
		blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.mipLevel = i;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount = 1;
		vkCmdBlitImage(commandBuffer, *image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					   *image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
							 0, 0, NULL, 0, NULL, 1, &barrier);

		if (mipWidth > 1) mipWidth /= 2;
		if (mipHeight > 1) mipHeight /= 2;
	}

	barrier.subresourceRange.baseMipLevel = mipLevels - 1;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
						 0, 0, NULL, 0, NULL, 1, &barrier);

	RETROVK_EndSingleTimeCommands(commandBuffer);

	vkDestroyBuffer(RETROVK.device, staging, NULL);
	vkFreeMemory(RETROVK.device, stagingMemory, NULL);

	*view = RETROVK_CreateImageView(*image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);
}

// Upload one RGBA8 image without mipmaps. This is used for lightmaps: they are
// already tiny, should clamp rather than repeat, and generating a mip chain for
// every surface is expensive at startup.
void RETROVK_CreateTextureImageNoMip(int width, int height, const void *pixels,
									 VkImage *image, VkDeviceMemory *memory, VkImageView *view)
{
	VkDeviceSize imageSize = (VkDeviceSize)width * height * 4;

	VkBuffer staging;
	VkDeviceMemory stagingMemory;
	RETROVK_CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
						 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
						 &staging, &stagingMemory);
	void *data;
	vkMapMemory(RETROVK.device, stagingMemory, 0, imageSize, 0, &data);
	memcpy(data, pixels, (size_t)imageSize);
	vkUnmapMemory(RETROVK.device, stagingMemory);

	RETROVK_CreateImage(width, height, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
						VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, memory);

	VkCommandBuffer commandBuffer = RETROVK_BeginSingleTimeCommands();

	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = *image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.srcAccessMask = 0;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
						 0, 0, NULL, 0, NULL, 1, &barrier);

	VkBufferImageCopy region = {};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageExtent.width = width;
	region.imageExtent.height = height;
	region.imageExtent.depth = 1;
	vkCmdCopyBufferToImage(commandBuffer, staging, *image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
						 0, 0, NULL, 0, NULL, 1, &barrier);

	RETROVK_EndSingleTimeCommands(commandBuffer);

	vkDestroyBuffer(RETROVK.device, staging, NULL);
	vkFreeMemory(RETROVK.device, stagingMemory, NULL);

	*view = RETROVK_CreateImageView(*image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1);
}

// Replace the pixels of an existing single-level RGBA8 sampled image.
void RETROVK_UpdateTextureImageNoMip(VkImage image, int width, int height, const void *pixels)
{
	VkDeviceSize imageSize = (VkDeviceSize)width * height * 4;

	VkBuffer staging;
	VkDeviceMemory stagingMemory;
	RETROVK_CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
						 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
						 &staging, &stagingMemory);
	void *data;
	vkMapMemory(RETROVK.device, stagingMemory, 0, imageSize, 0, &data);
	memcpy(data, pixels, (size_t)imageSize);
	vkUnmapMemory(RETROVK.device, stagingMemory);

	VkCommandBuffer commandBuffer = RETROVK_BeginSingleTimeCommands();

	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
						 0, 0, NULL, 0, NULL, 1, &barrier);

	VkBufferImageCopy region = {};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageExtent.width = width;
	region.imageExtent.height = height;
	region.imageExtent.depth = 1;
	vkCmdCopyBufferToImage(commandBuffer, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
						 0, 0, NULL, 0, NULL, 1, &barrier);

	RETROVK_EndSingleTimeCommands(commandBuffer);

	vkDestroyBuffer(RETROVK.device, staging, NULL);
	vkFreeMemory(RETROVK.device, stagingMemory, NULL);
}

// Destroy a texture image: its view, image and backing memory (counterpart to
// RETROVK_CreateTextureImage).
void RETROVK_DestroyTexture(VkImage image, VkDeviceMemory memory, VkImageView view)
{
	vkDestroyImageView(RETROVK.device, view, NULL);
	vkDestroyImage(RETROVK.device, image, NULL);
	vkFreeMemory(RETROVK.device, memory, NULL);
}

// Create a sampler with the given filters and wrap mode.
VkSampler RETROVK_CreateSampler(VkFilter magFilter, VkFilter minFilter, VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT)
{
	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = magFilter;
	samplerInfo.minFilter = minFilter;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.addressModeU = addressMode;
	samplerInfo.addressModeV = addressMode;
	samplerInfo.addressModeW = addressMode;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

	VkSampler sampler;
	RETROVK_Check(vkCreateSampler(RETROVK.device, &samplerInfo, NULL, &sampler), "vkCreateSampler failed");
	return sampler;
}

// Descriptor set layout for a single combined image sampler at binding 0, visible
// to the fragment shader: the canonical "one texture" descriptor layout.
VkDescriptorSetLayout RETROVK_CreateTextureDescriptorSetLayout(void)
{
	VkDescriptorSetLayoutBinding binding = {};
	binding.binding = 0;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 1;
	layoutInfo.pBindings = &binding;

	VkDescriptorSetLayout layout;
	RETROVK_Check(vkCreateDescriptorSetLayout(RETROVK.device, &layoutInfo, NULL, &layout), "vkCreateDescriptorSetLayout failed");
	return layout;
}

// Descriptor pool sized to hand out maxSets combined-image-sampler descriptor sets.
VkDescriptorPool RETROVK_CreateTextureDescriptorPool(uint32_t maxSets)
{
	if (maxSets == 0) {
		maxSets = 1;
	}
	VkDescriptorPoolSize poolSize = {};
	poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSize.descriptorCount = maxSets;

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &poolSize;
	poolInfo.maxSets = maxSets;

	VkDescriptorPool pool;
	RETROVK_Check(vkCreateDescriptorPool(RETROVK.device, &poolInfo, NULL, &pool), "vkCreateDescriptorPool failed");
	return pool;
}

// Allocate one descriptor set from the pool with the given layout and point its
// binding 0 at (view, sampler). Returns the ready-to-bind set.
VkDescriptorSet RETROVK_CreateTextureDescriptorSet(VkDescriptorPool pool, VkDescriptorSetLayout layout,
												   VkImageView view, VkSampler sampler)
{
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = pool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &layout;

	VkDescriptorSet set;
	RETROVK_Check(vkAllocateDescriptorSets(RETROVK.device, &allocInfo, &set), "vkAllocateDescriptorSets failed");

	VkDescriptorImageInfo imageInfo = {};
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageInfo.imageView = view;
	imageInfo.sampler = sampler;

	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = set;
	write.dstBinding = 0;
	write.dstArrayElement = 0;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.descriptorCount = 1;
	write.pImageInfo = &imageInfo;
	vkUpdateDescriptorSets(RETROVK.device, 1, &write, 0, NULL);
	return set;
}

VkShaderModule RETROVK_CreateShaderModule(const uint32_t *code, size_t sizeBytes)
{
	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = sizeBytes;
	createInfo.pCode = code;

	VkShaderModule module;
	RETROVK_Check(vkCreateShaderModule(RETROVK.device, &createInfo, NULL, &module), "vkCreateShaderModule failed");
	return module;
}

// Create a pipeline layout from a single descriptor set layout and a push
// constant range. Pass VK_NULL_HANDLE for no set, or pushConstantSize 0 for no
// push constants.
VkPipelineLayout RETROVK_CreatePipelineLayout(VkDescriptorSetLayout setLayout,
											  VkShaderStageFlags pushConstantStages, uint32_t pushConstantSize)
{
	VkPushConstantRange pushRange = {};
	pushRange.stageFlags = pushConstantStages;
	pushRange.offset = 0;
	pushRange.size = pushConstantSize;

	VkPipelineLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = (setLayout != VK_NULL_HANDLE) ? 1 : 0;
	layoutInfo.pSetLayouts = (setLayout != VK_NULL_HANDLE) ? &setLayout : NULL;
	layoutInfo.pushConstantRangeCount = (pushConstantSize > 0) ? 1 : 0;
	layoutInfo.pPushConstantRanges = (pushConstantSize > 0) ? &pushRange : NULL;

	VkPipelineLayout layout;
	RETROVK_Check(vkCreatePipelineLayout(RETROVK.device, &layoutInfo, NULL, &layout), "vkCreatePipelineLayout failed");
	return layout;
}

// Build a graphics pipeline from the demo-specific config and sensible fixed
// defaults. Shader modules stay owned by the caller (so the same pair can build
// several pipeline variants differing only in config).
VkPipeline RETROVK_CreateGraphicsPipeline(const RETROVK_PipelineConfig *config)
{
	VkPipelineShaderStageCreateInfo stages[2] = {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = config->vertShader;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = config->fragShader;
	stages[1].pName = "main";

	VkVertexInputBindingDescription bindingDesc = {};
	bindingDesc.binding = 0;
	bindingDesc.stride = config->vertexStride;
	bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkPipelineVertexInputStateCreateInfo vertexInput = {};
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInput.vertexBindingDescriptionCount = 1;
	vertexInput.pVertexBindingDescriptions = &bindingDesc;
	vertexInput.vertexAttributeDescriptionCount = config->attributeCount;
	vertexInput.pVertexAttributeDescriptions = config->attributes;

	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = config->topology;

	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	VkPipelineMultisampleStateCreateInfo multisample = {};
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineDepthStencilStateCreateInfo depthStencil = {};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = VK_TRUE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_FALSE;

	VkPipelineColorBlendStateCreateInfo colorBlend = {};
	colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlend.attachmentCount = 1;
	colorBlend.pAttachments = &colorBlendAttachment;

	VkDynamicState dynamicStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState = {};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = 2;
	dynamicState.pDynamicStates = dynamicStates;

	VkPipelineRasterizationStateCreateInfo rasterizer = {};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.cullMode = config->cullMode;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.polygonMode = config->polygonMode;
	rasterizer.lineWidth = 1.0f;

	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = stages;
	pipelineInfo.pVertexInputState = &vertexInput;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisample;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &colorBlend;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = config->layout;
	pipelineInfo.renderPass = RETROVK.renderPass;
	pipelineInfo.subpass = 0;

	VkPipeline pipeline;
	RETROVK_Check(vkCreateGraphicsPipelines(RETROVK.device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &pipeline), "vkCreateGraphicsPipelines failed");
	return pipeline;
}

// *******************************************************************
// Mesh functions
// *******************************************************************
//
// The demo drives a RETROVK_Mesh through these functions (the struct is declared
// near the top of this file). Defined here, after the helpers they build on. The
// demo's shaders must match this fixed contract:
//   vertex   : location 0 = vec3 position, location 1 = vec2 texcoord
//   fragment : set 0, binding 0 = sampler2D
//   both     : push_constant { mat4 mvp; vec3 color; int mode; float time; int surfaceFlags; }
//              The vertex stage uses mvp/time/flags; the fragment stage uses color/mode/time/flags.

// Reserve `count` texture slots and create the shared sampler and the descriptor
// set layout/pool. Call RETROVK_UploadMeshTexture once per slot afterwards.
void RETROVK_CreateMeshTextures(RETROVK_Mesh *mesh, int count, VkFilter magFilter, VkFilter minFilter)
{
	mesh->numTextures = count;
	mesh->images = new VkImage[count];
	mesh->memories = new VkDeviceMemory[count];
	mesh->views = new VkImageView[count];
	mesh->descriptorSets = new VkDescriptorSet[count];
	mesh->lumaImages = new VkImage[count];
	mesh->lumaMemories = new VkDeviceMemory[count];
	mesh->lumaViews = new VkImageView[count];
	mesh->lumaDescriptorSets = new VkDescriptorSet[count];
	mesh->sampler = RETROVK_CreateSampler(magFilter, minFilter);
	mesh->descriptorSetLayout = RETROVK_CreateTextureDescriptorSetLayout();
	mesh->descriptorPool = RETROVK_CreateTextureDescriptorPool(count);
	mesh->lumaDescriptorPool = RETROVK_CreateTextureDescriptorPool(count);
}

// Reserve one lightmap atlas texture slot. Uses the mesh texture descriptor
// layout, but a separate pool/sampler because lightmaps clamp.
void RETROVK_CreateMeshLightmaps(RETROVK_Mesh *mesh, int count, VkFilter magFilter, VkFilter minFilter)
{
	(void)count;
	mesh->numLightmaps = 1;
	mesh->lightImages = new VkImage[1];
	mesh->lightMemories = new VkDeviceMemory[1];
	mesh->lightViews = new VkImageView[1];
	mesh->lightDescriptorSets = new VkDescriptorSet[1];
	mesh->lightSampler = RETROVK_CreateSampler(magFilter, minFilter, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
	mesh->lightDescriptorPool = RETROVK_CreateTextureDescriptorPool(1);
}

// Upload one RGBA8 texture into slot `index` and point its descriptor set at it.
void RETROVK_UploadMeshTexture(RETROVK_Mesh *mesh, int index, int width, int height, const void *rgba)
{
	RETROVK_CreateTextureImage(width, height, rgba, &mesh->images[index], &mesh->memories[index], &mesh->views[index]);
	mesh->descriptorSets[index] = RETROVK_CreateTextureDescriptorSet(mesh->descriptorPool, mesh->descriptorSetLayout,
																	 mesh->views[index], mesh->sampler);
}

// Upload one RGBA8 fullbright/luma texture into slot `index` and point its descriptor set at it.
void RETROVK_UploadMeshLumaTexture(RETROVK_Mesh *mesh, int index, int width, int height, const void *rgba)
{
	RETROVK_CreateTextureImage(width, height, rgba, &mesh->lumaImages[index], &mesh->lumaMemories[index], &mesh->lumaViews[index]);
	mesh->lumaDescriptorSets[index] = RETROVK_CreateTextureDescriptorSet(mesh->lumaDescriptorPool, mesh->descriptorSetLayout,
																		 mesh->lumaViews[index], mesh->sampler);
}

// Upload one RGBA8 lightmap atlas and point its descriptor set at it.
void RETROVK_UploadMeshLightmap(RETROVK_Mesh *mesh, int index, int width, int height, const void *rgba)
{
	(void)index;
	RETROVK_CreateTextureImageNoMip(width, height, rgba, &mesh->lightImages[0], &mesh->lightMemories[0], &mesh->lightViews[0]);
	mesh->lightDescriptorSets[0] = RETROVK_CreateTextureDescriptorSet(mesh->lightDescriptorPool, mesh->descriptorSetLayout,
																	  mesh->lightViews[0], mesh->lightSampler);
}

void RETROVK_UpdateMeshLightmap(RETROVK_Mesh *mesh, int width, int height, const void *rgba)
{
	RETROVK_UpdateTextureImageNoMip(mesh->lightImages[0], width, height, rgba);
}

// Upload the mesh vertices into a device-local vertex buffer.
void RETROVK_UploadMeshVertices(RETROVK_Mesh *mesh, const RETROVK_Vertex *vertices, int count)
{
	VkDeviceSize size = (VkDeviceSize)sizeof(RETROVK_Vertex) * count;
	RETROVK_CreateDeviceLocalBuffer(vertices, size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &mesh->vertexBuffer, &mesh->vertexBufferMemory);
}

// Build the render-mode pipelines from the demo's vertex/fragment SPIR-V. Requires
// the texture descriptor layout, so call after RETROVK_CreateMeshTextures.
void RETROVK_CreateMeshPipelines(RETROVK_Mesh *mesh, const uint32_t *vertSpv, size_t vertSpvSize, const uint32_t *fragSpv, size_t fragSpvSize)
{
	VkShaderModule vertModule = RETROVK_CreateShaderModule(vertSpv, vertSpvSize);
	VkShaderModule fragModule = RETROVK_CreateShaderModule(fragSpv, fragSpvSize);

	VkVertexInputAttributeDescription attrs[3] = {};
	attrs[0].location = 0;
	attrs[0].binding = 0;
	attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	attrs[0].offset = offsetof(RETROVK_Vertex, pos);
	attrs[1].location = 1;
	attrs[1].binding = 0;
	attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
	attrs[1].offset = offsetof(RETROVK_Vertex, uv);
	attrs[2].location = 2;
	attrs[2].binding = 0;
	attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
	attrs[2].offset = offsetof(RETROVK_Vertex, lightUv);

	VkPushConstantRange pushRange = {};
	pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	pushRange.offset = 0;
	pushRange.size = sizeof(RETROVK_PushConstants);

	VkDescriptorSetLayout setLayouts[3] = { mesh->descriptorSetLayout, mesh->descriptorSetLayout, mesh->descriptorSetLayout };
	VkPipelineLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 3;
	layoutInfo.pSetLayouts = setLayouts;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &pushRange;
	RETROVK_Check(vkCreatePipelineLayout(RETROVK.device, &layoutInfo, NULL, &mesh->pipelineLayout), "vkCreatePipelineLayout failed");

	RETROVK_PipelineConfig config = {};
	config.vertShader = vertModule;
	config.fragShader = fragModule;
	config.vertexStride = sizeof(RETROVK_Vertex);
	config.attributes = attrs;
	config.attributeCount = 3;
	config.layout = mesh->pipelineLayout;
	config.cullMode = VK_CULL_MODE_NONE;	// Draw both sides (viewer may sit inside the mesh)

	// Textured and Solid share this filled-triangle pipeline (they differ only by
	// the pushed mode); wireframe and points get their own topology/polygon mode.
	config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	config.polygonMode = VK_POLYGON_MODE_FILL;
	mesh->pipelineFill = RETROVK_CreateGraphicsPipeline(&config);

	// Without fillModeNonSolid there is no line polygon mode; wireframe falls back to fill
	if (RETROVK.wireframeSupported) {
		config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		config.polygonMode = VK_POLYGON_MODE_LINE;
		mesh->pipelineWireframe = RETROVK_CreateGraphicsPipeline(&config);
	}

	config.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
	config.polygonMode = VK_POLYGON_MODE_FILL;
	mesh->pipelinePoints = RETROVK_CreateGraphicsPipeline(&config);

	vkDestroyShaderModule(RETROVK.device, vertModule, NULL);
	vkDestroyShaderModule(RETROVK.device, fragModule, NULL);
}

// Bind the mesh for this frame: select the pipeline for `mode`, bind the vertex
// buffer and push the MVP. Call between RETROVK_BeginFrame/EndFrame.
void RETROVK_PrepareMeshDraws(RETROVK_Mesh *mesh, const float *mvp, RETROVK_RenderMode mode, float time, const float *cameraOrigin)
{
	VkPipeline selected = mesh->pipelineFill;
	if (mode == RETROVK_RENDER_WIREFRAME && mesh->pipelineWireframe != VK_NULL_HANDLE) {
		selected = mesh->pipelineWireframe;
	} else if (mode == RETROVK_RENDER_POINTS) {
		selected = mesh->pipelinePoints;
	}

	VkCommandBuffer cmd = RETROVK.currentCommandBuffer;
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, selected);
	VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(cmd, 0, 1, &mesh->vertexBuffer, &offset);

	// mvp and mode are constant for the frame; the per-surface colour is pushed in Draw
	VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	int m = (int)mode;
	vkCmdPushConstants(cmd, mesh->pipelineLayout, stages, offsetof(RETROVK_PushConstants, mvp), sizeof(float) * 16, mvp);
	vkCmdPushConstants(cmd, mesh->pipelineLayout, stages, offsetof(RETROVK_PushConstants, mode), sizeof(int), &m);
	vkCmdPushConstants(cmd, mesh->pipelineLayout, stages, offsetof(RETROVK_PushConstants, time), sizeof(float), &time);
	vkCmdPushConstants(cmd, mesh->pipelineLayout, stages, offsetof(RETROVK_PushConstants, cameraOrigin), sizeof(float) * 3, cameraOrigin);
}

// Draw a range of the vertex buffer using the texture and lightmap in the given slots. `color`
// is the flat colour used in solid render mode (ignored by the others).
void RETROVK_DrawMeshRange(RETROVK_Mesh *mesh, int textureIndex, int lightmapIndex, int firstVertex, int vertexCount, const float *color, int surfaceFlags)
{
	if (vertexCount == 0) {
		return;
	}
	VkCommandBuffer cmd = RETROVK.currentCommandBuffer;
	// The surface's flat colour for solid mode (ignored by the other modes)
	vkCmdPushConstants(cmd, mesh->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
					   offsetof(RETROVK_PushConstants, color), sizeof(float) * 3, color);
	vkCmdPushConstants(cmd, mesh->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
					   offsetof(RETROVK_PushConstants, surfaceFlags), sizeof(int), &surfaceFlags);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mesh->pipelineLayout, 0, 1, &mesh->descriptorSets[textureIndex], 0, NULL);
	(void)lightmapIndex;
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mesh->pipelineLayout, 1, 1, &mesh->lightDescriptorSets[0], 0, NULL);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mesh->pipelineLayout, 2, 1, &mesh->lumaDescriptorSets[textureIndex], 0, NULL);
	vkCmdDraw(cmd, vertexCount, 1, firstVertex, 0);
}

// Wait for the device to go idle, then destroy every resource the mesh owns.
void RETROVK_DestroyMesh(RETROVK_Mesh *mesh)
{
	vkDeviceWaitIdle(RETROVK.device);

	vkDestroyPipeline(RETROVK.device, mesh->pipelinePoints, NULL);
	vkDestroyPipeline(RETROVK.device, mesh->pipelineWireframe, NULL);
	vkDestroyPipeline(RETROVK.device, mesh->pipelineFill, NULL);
	vkDestroyPipelineLayout(RETROVK.device, mesh->pipelineLayout, NULL);

	vkDestroyDescriptorPool(RETROVK.device, mesh->descriptorPool, NULL);
	vkDestroyDescriptorPool(RETROVK.device, mesh->lumaDescriptorPool, NULL);
	vkDestroyDescriptorSetLayout(RETROVK.device, mesh->descriptorSetLayout, NULL);
	vkDestroySampler(RETROVK.device, mesh->sampler, NULL);
	for (int i = 0; i < mesh->numTextures; i++) {
		RETROVK_DestroyTexture(mesh->images[i], mesh->memories[i], mesh->views[i]);
		RETROVK_DestroyTexture(mesh->lumaImages[i], mesh->lumaMemories[i], mesh->lumaViews[i]);
	}
	delete[] mesh->images;
	delete[] mesh->memories;
	delete[] mesh->views;
	delete[] mesh->descriptorSets;
	delete[] mesh->lumaImages;
	delete[] mesh->lumaMemories;
	delete[] mesh->lumaViews;
	delete[] mesh->lumaDescriptorSets;

	vkDestroyDescriptorPool(RETROVK.device, mesh->lightDescriptorPool, NULL);
	vkDestroySampler(RETROVK.device, mesh->lightSampler, NULL);
	for (int i = 0; i < mesh->numLightmaps; i++) {
		RETROVK_DestroyTexture(mesh->lightImages[i], mesh->lightMemories[i], mesh->lightViews[i]);
	}
	delete[] mesh->lightImages;
	delete[] mesh->lightMemories;
	delete[] mesh->lightViews;
	delete[] mesh->lightDescriptorSets;

	RETROVK_DestroyBuffer(mesh->vertexBuffer, mesh->vertexBufferMemory);
}

// *******************************************************************
// Initialization (private)
// *******************************************************************

static void RETROVK_CreateInstance(SDL_Window *window)
{
	uint32_t sdlExtCount = 0;
	const char * const *sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);
	if (!sdlExtensions) {
		RETROVK_Die("SDL_Vulkan_GetInstanceExtensions failed");
	}

	// Inspect available instance extensions to decide on portability enumeration
	uint32_t availCount = 0;
	vkEnumerateInstanceExtensionProperties(NULL, &availCount, NULL);
	VkExtensionProperties *avail = new VkExtensionProperties[availCount];
	vkEnumerateInstanceExtensionProperties(NULL, &availCount, avail);
	bool portability = RETROVK_HasExtension(avail, availCount, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
	delete[] avail;

	// Copy SDL's required extensions and append portability enumeration if present (MoltenVK)
	uint32_t extCount = sdlExtCount;
	const char **extensions = new const char *[sdlExtCount + 1];
	for (uint32_t i = 0; i < sdlExtCount; i++) {
		extensions[i] = sdlExtensions[i];
	}
	if (portability) {
		extensions[extCount++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
	}

	const char *validationLayer = "VK_LAYER_KHRONOS_validation";

	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "retro-vkquake";
	appInfo.apiVersion = VK_API_VERSION_1_1;

	VkInstanceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;
	createInfo.enabledExtensionCount = extCount;
	createInfo.ppEnabledExtensionNames = extensions;
	if (portability) {
		createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
	}
	if (RETROVK_ENABLE_VALIDATION) {
		createInfo.enabledLayerCount = 1;
		createInfo.ppEnabledLayerNames = &validationLayer;
	}

	RETROVK_Check(vkCreateInstance(&createInfo, NULL, &RETROVK.instance), "vkCreateInstance failed");
	delete[] extensions;

	if (!SDL_Vulkan_CreateSurface(window, RETROVK.instance, NULL, &RETROVK.surface)) {
		RETROVK_Die("SDL_Vulkan_CreateSurface failed");
	}
}

// Pick the first device that exposes a queue family supporting both graphics
// and presentation, and remember whether it needs the portability subset.
static bool RETROVK_PickPhysicalDevice(bool *needsPortabilitySubset)
{
	uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices(RETROVK.instance, &deviceCount, NULL);
	if (deviceCount == 0) {
		return false;
	}
	VkPhysicalDevice *devices = new VkPhysicalDevice[deviceCount];
	vkEnumeratePhysicalDevices(RETROVK.instance, &deviceCount, devices);

	// Score every usable device and keep the best: a hardware GPU beats a CPU
	// rasterizer (lavapipe), and a fully conformant driver beats one that needs
	// the portability subset. On Apple silicon that ranks KosmicKrisp first,
	// then MoltenVK, then lavapipe. VK_DRIVER_FILES still forces one driver.
	int bestScore = -1;
	for (uint32_t d = 0; d < deviceCount; d++) {
		uint32_t familyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &familyCount, NULL);
		VkQueueFamilyProperties *families = new VkQueueFamilyProperties[familyCount];
		vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &familyCount, families);

		int family = -1;
		for (uint32_t f = 0; f < familyCount; f++) {
			VkBool32 presentSupport = VK_FALSE;
			vkGetPhysicalDeviceSurfaceSupportKHR(devices[d], f, RETROVK.surface, &presentSupport);
			if ((families[f].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport) {
				family = (int)f;
				break;
			}
		}
		delete[] families;
		if (family < 0) {
			continue;
		}

		uint32_t extCount = 0;
		vkEnumerateDeviceExtensionProperties(devices[d], NULL, &extCount, NULL);
		VkExtensionProperties *exts = new VkExtensionProperties[extCount];
		vkEnumerateDeviceExtensionProperties(devices[d], NULL, &extCount, exts);
		bool portability = RETROVK_HasExtension(exts, extCount, "VK_KHR_portability_subset");
		delete[] exts;

		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(devices[d], &props);
		int score = 0;
		if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU || props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
			score += 2;
		}
		if (!portability) {
			score += 1;
		}

		if (score > bestScore) {
			bestScore = score;
			RETROVK.physicalDevice = devices[d];
			RETROVK.queueFamily = (uint32_t)family;
			*needsPortabilitySubset = portability;
		}
	}

	delete[] devices;
	return bestScore >= 0;
}

static void RETROVK_CreateDevice(bool needsPortabilitySubset)
{
	float priority = 1.0f;
	VkDeviceQueueCreateInfo queueInfo = {};
	queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueInfo.queueFamilyIndex = RETROVK.queueFamily;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = &priority;

	const char *deviceExtensions[2];
	uint32_t deviceExtCount = 0;
	deviceExtensions[deviceExtCount++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
	if (needsPortabilitySubset) {
		deviceExtensions[deviceExtCount++] = "VK_KHR_portability_subset";
	}

	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(RETROVK.physicalDevice, &props);
	VkPhysicalDeviceFeatures supported;
	vkGetPhysicalDeviceFeatures(RETROVK.physicalDevice, &supported);
	RETROVK.wireframeSupported = supported.fillModeNonSolid == VK_TRUE;
	printf("Vulkan device: %s (API %u.%u.%u)%s\n", props.deviceName,
		VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion), VK_API_VERSION_PATCH(props.apiVersion),
		RETROVK.wireframeSupported ? "" : ", no fillModeNonSolid: wireframe drawn filled");

	VkPhysicalDeviceFeatures features = {};
	features.fillModeNonSolid = supported.fillModeNonSolid;	// Wireframe render mode (VK_POLYGON_MODE_LINE), when available
	features.samplerAnisotropy = VK_FALSE;

	VkDeviceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.queueCreateInfoCount = 1;
	createInfo.pQueueCreateInfos = &queueInfo;
	createInfo.enabledExtensionCount = deviceExtCount;
	createInfo.ppEnabledExtensionNames = deviceExtensions;
	createInfo.pEnabledFeatures = &features;

	RETROVK_Check(vkCreateDevice(RETROVK.physicalDevice, &createInfo, NULL, &RETROVK.device), "vkCreateDevice failed");
	vkGetDeviceQueue(RETROVK.device, RETROVK.queueFamily, 0, &RETROVK.graphicsQueue);
}

static VkFormat RETROVK_FindDepthFormat(void)
{
	VkFormat candidates[] = { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT };
	for (VkFormat format : candidates) {
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(RETROVK.physicalDevice, format, &props);
		if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
			return format;
		}
	}
	RETROVK_Die("RETROVK_FindDepthFormat: no supported depth format");
	return VK_FORMAT_UNDEFINED;
}

static void RETROVK_CreateSwapchain(SDL_Window *window)
{
	VkSurfaceCapabilitiesKHR caps;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(RETROVK.physicalDevice, RETROVK.surface, &caps);

	// Surface format: prefer B8G8R8A8_UNORM (no implicit sRGB, matches the GL path)
	uint32_t formatCount = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(RETROVK.physicalDevice, RETROVK.surface, &formatCount, NULL);
	VkSurfaceFormatKHR *formats = new VkSurfaceFormatKHR[formatCount];
	vkGetPhysicalDeviceSurfaceFormatsKHR(RETROVK.physicalDevice, RETROVK.surface, &formatCount, formats);
	VkSurfaceFormatKHR chosen = formats[0];
	for (uint32_t i = 0; i < formatCount; i++) {
		if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
			formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			chosen = formats[i];
			break;
		}
	}
	delete[] formats;
	RETROVK.swapchainFormat = chosen.format;

	// Present mode: FIFO (vsync) is always supported. With vsync off, prefer
	// MAILBOX (low-latency, no tearing) and fall back to IMMEDIATE (tears, but
	// uncapped) if that's not available.
	VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
	if (!RETROVK.vsync) {
		uint32_t presentModeCount = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR(RETROVK.physicalDevice, RETROVK.surface, &presentModeCount, NULL);
		VkPresentModeKHR *presentModes = new VkPresentModeKHR[presentModeCount];
		vkGetPhysicalDeviceSurfacePresentModesKHR(RETROVK.physicalDevice, RETROVK.surface, &presentModeCount, presentModes);
		VkPresentModeKHR fallback = VK_PRESENT_MODE_FIFO_KHR;
		for (uint32_t i = 0; i < presentModeCount; i++) {
			if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
				fallback = VK_PRESENT_MODE_MAILBOX_KHR;
				break;
			} else if (presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
				fallback = VK_PRESENT_MODE_IMMEDIATE_KHR;
			}
		}
		delete[] presentModes;
		presentMode = fallback;
	}

	// Extent: use the surface's current extent, or the window pixel size when undefined
	if (caps.currentExtent.width != 0xFFFFFFFF) {
		RETROVK.swapchainExtent = caps.currentExtent;
	} else {
		int w, h;
		SDL_GetWindowSizeInPixels(window, &w, &h);
		RETROVK.swapchainExtent.width = (uint32_t)w;
		if (RETROVK.swapchainExtent.width < caps.minImageExtent.width) {
			RETROVK.swapchainExtent.width = caps.minImageExtent.width;
		} else if (RETROVK.swapchainExtent.width > caps.maxImageExtent.width) {
			RETROVK.swapchainExtent.width = caps.maxImageExtent.width;
		}
		RETROVK.swapchainExtent.height = (uint32_t)h;
		if (RETROVK.swapchainExtent.height < caps.minImageExtent.height) {
			RETROVK.swapchainExtent.height = caps.minImageExtent.height;
		} else if (RETROVK.swapchainExtent.height > caps.maxImageExtent.height) {
			RETROVK.swapchainExtent.height = caps.maxImageExtent.height;
		}
	}

	uint32_t imageCount = caps.minImageCount + 1;
	if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
		imageCount = caps.maxImageCount;
	}

	VkSwapchainCreateInfoKHR createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	createInfo.surface = RETROVK.surface;
	createInfo.minImageCount = imageCount;
	createInfo.imageFormat = chosen.format;
	createInfo.imageColorSpace = chosen.colorSpace;
	createInfo.imageExtent = RETROVK.swapchainExtent;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	createInfo.preTransform = caps.currentTransform;
	createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	createInfo.presentMode = presentMode;
	createInfo.clipped = VK_TRUE;
	createInfo.oldSwapchain = VK_NULL_HANDLE;

	RETROVK_Check(vkCreateSwapchainKHR(RETROVK.device, &createInfo, NULL, &RETROVK.swapchain), "vkCreateSwapchainKHR failed");

	vkGetSwapchainImagesKHR(RETROVK.device, RETROVK.swapchain, &RETROVK.swapchainImageCount, NULL);
	RETROVK.swapchainImages = new VkImage[RETROVK.swapchainImageCount];
	vkGetSwapchainImagesKHR(RETROVK.device, RETROVK.swapchain, &RETROVK.swapchainImageCount, RETROVK.swapchainImages);

	RETROVK.swapchainImageViews = new VkImageView[RETROVK.swapchainImageCount];
	for (uint32_t i = 0; i < RETROVK.swapchainImageCount; i++) {
		RETROVK.swapchainImageViews[i] = RETROVK_CreateImageView(RETROVK.swapchainImages[i], RETROVK.swapchainFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
	}
}

static void RETROVK_CreateDepthResources(void)
{
	RETROVK.depthFormat = RETROVK_FindDepthFormat();
	RETROVK_CreateImage(RETROVK.swapchainExtent.width, RETROVK.swapchainExtent.height, 1, RETROVK.depthFormat,
						VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &RETROVK.depthImage, &RETROVK.depthMemory);
	RETROVK.depthImageView = RETROVK_CreateImageView(RETROVK.depthImage, RETROVK.depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
}

static void RETROVK_CreateRenderPass(void)
{
	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = RETROVK.swapchainFormat;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentDescription depthAttachment = {};
	depthAttachment.format = RETROVK.depthFormat;
	depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference colorRef = {};
	colorRef.attachment = 0;
	colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthRef = {};
	depthRef.attachment = 1;
	depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;
	subpass.pDepthStencilAttachment = &depthRef;

	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	VkAttachmentDescription attachments[2] = { colorAttachment, depthAttachment };
	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 2;
	renderPassInfo.pAttachments = attachments;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = 1;
	renderPassInfo.pDependencies = &dependency;

	RETROVK_Check(vkCreateRenderPass(RETROVK.device, &renderPassInfo, NULL, &RETROVK.renderPass), "vkCreateRenderPass failed");
}

static void RETROVK_CreateFramebuffers(void)
{
	RETROVK.framebuffers = new VkFramebuffer[RETROVK.swapchainImageCount];
	for (uint32_t i = 0; i < RETROVK.swapchainImageCount; i++) {
		VkImageView attachments[2] = { RETROVK.swapchainImageViews[i], RETROVK.depthImageView };
		VkFramebufferCreateInfo framebufferInfo = {};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = RETROVK.renderPass;
		framebufferInfo.attachmentCount = 2;
		framebufferInfo.pAttachments = attachments;
		framebufferInfo.width = RETROVK.swapchainExtent.width;
		framebufferInfo.height = RETROVK.swapchainExtent.height;
		framebufferInfo.layers = 1;
		RETROVK_Check(vkCreateFramebuffer(RETROVK.device, &framebufferInfo, NULL, &RETROVK.framebuffers[i]), "vkCreateFramebuffer failed");
	}
}

static void RETROVK_CreateCommandResources(void)
{
	VkCommandPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = RETROVK.queueFamily;
	RETROVK_Check(vkCreateCommandPool(RETROVK.device, &poolInfo, NULL, &RETROVK.commandPool), "vkCreateCommandPool failed");

	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = RETROVK.commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = RETROVK_MAX_FRAMES_IN_FLIGHT;
	RETROVK_Check(vkAllocateCommandBuffers(RETROVK.device, &allocInfo, RETROVK.commandBuffers), "vkAllocateCommandBuffers failed");

	VkSemaphoreCreateInfo semaphoreInfo = {};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	for (int i = 0; i < RETROVK_MAX_FRAMES_IN_FLIGHT; i++) {
		RETROVK_Check(vkCreateSemaphore(RETROVK.device, &semaphoreInfo, NULL, &RETROVK.imageAvailable[i]), "vkCreateSemaphore failed");
		RETROVK_Check(vkCreateFence(RETROVK.device, &fenceInfo, NULL, &RETROVK.inFlight[i]), "vkCreateFence failed");
	}
}

// The present-wait semaphore must be owned per swapchain image, not per
// frame-in-flight: vkQueuePresentKHR waits on it and we don't control when that
// wait completes, so it can only be safely reused once the image is reacquired.
// Recreated with the swapchain because the image count can change.
static void RETROVK_CreateRenderFinishedSemaphores(void)
{
	VkSemaphoreCreateInfo semaphoreInfo = {};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	RETROVK.renderFinished = new VkSemaphore[RETROVK.swapchainImageCount];
	for (uint32_t i = 0; i < RETROVK.swapchainImageCount; i++) {
		RETROVK_Check(vkCreateSemaphore(RETROVK.device, &semaphoreInfo, NULL, &RETROVK.renderFinished[i]), "vkCreateSemaphore failed");
	}
}

// Destroy everything tied to the swapchain's dimensions/images so it can be
// rebuilt: the per-image present semaphores, framebuffers, depth buffer, image
// views and the swapchain itself. The render pass survives (format is unchanged).
static void RETROVK_CleanupSwapchain(void)
{
	for (uint32_t i = 0; i < RETROVK.swapchainImageCount; i++) {
		vkDestroySemaphore(RETROVK.device, RETROVK.renderFinished[i], NULL);
	}
	delete[] RETROVK.renderFinished;
	RETROVK.renderFinished = NULL;

	for (uint32_t i = 0; i < RETROVK.swapchainImageCount; i++) {
		vkDestroyFramebuffer(RETROVK.device, RETROVK.framebuffers[i], NULL);
	}
	delete[] RETROVK.framebuffers;
	RETROVK.framebuffers = NULL;

	vkDestroyImageView(RETROVK.device, RETROVK.depthImageView, NULL);
	vkDestroyImage(RETROVK.device, RETROVK.depthImage, NULL);
	vkFreeMemory(RETROVK.device, RETROVK.depthMemory, NULL);

	for (uint32_t i = 0; i < RETROVK.swapchainImageCount; i++) {
		vkDestroyImageView(RETROVK.device, RETROVK.swapchainImageViews[i], NULL);
	}
	delete[] RETROVK.swapchainImageViews;
	RETROVK.swapchainImageViews = NULL;
	delete[] RETROVK.swapchainImages;
	RETROVK.swapchainImages = NULL;

	vkDestroySwapchainKHR(RETROVK.device, RETROVK.swapchain, NULL);
	RETROVK.swapchain = VK_NULL_HANDLE;
}

// Rebuild the swapchain and its dependent resources after it goes out of date
// (window resize, display change, ...). Skips while the window has no drawable
// area (e.g. minimized); the next frame's acquire retries until it is restored.
static void RETROVK_RecreateSwapchain(void)
{
	int w = 0, h = 0;
	SDL_GetWindowSizeInPixels(RETROVK.window, &w, &h);
	if (w == 0 || h == 0) {
		return;
	}

	vkDeviceWaitIdle(RETROVK.device);

	RETROVK_CleanupSwapchain();

	RETROVK_CreateSwapchain(RETROVK.window);
	RETROVK_CreateDepthResources();
	RETROVK_CreateFramebuffers();
	RETROVK_CreateRenderFinishedSemaphores();
}

// *******************************************************************
// Public lifecycle / frame API
// *******************************************************************

void RETROVK_Initialize(SDL_Window *window, bool vsync)
{
	RETROVK.window = window;
	RETROVK.vsync = vsync;
	RETROVK_CreateInstance(window);

	bool needsPortabilitySubset = false;
	if (!RETROVK_PickPhysicalDevice(&needsPortabilitySubset)) {
		RETROVK_Die("RETROVK: no suitable Vulkan device found");
	}
	RETROVK_CreateDevice(needsPortabilitySubset);
	RETROVK_CreateSwapchain(window);
	RETROVK_CreateDepthResources();
	RETROVK_CreateRenderPass();
	RETROVK_CreateFramebuffers();
	RETROVK_CreateCommandResources();
	RETROVK_CreateRenderFinishedSemaphores();
}

void RETROVK_Deinitialize(void)
{
	vkDeviceWaitIdle(RETROVK.device);

	RETROVK_CleanupSwapchain();

	for (int i = 0; i < RETROVK_MAX_FRAMES_IN_FLIGHT; i++) {
		vkDestroySemaphore(RETROVK.device, RETROVK.imageAvailable[i], NULL);
		vkDestroyFence(RETROVK.device, RETROVK.inFlight[i], NULL);
	}
	vkDestroyCommandPool(RETROVK.device, RETROVK.commandPool, NULL);

	vkDestroyRenderPass(RETROVK.device, RETROVK.renderPass, NULL);
	vkDestroyDevice(RETROVK.device, NULL);
	vkDestroySurfaceKHR(RETROVK.instance, RETROVK.surface, NULL);
	vkDestroyInstance(RETROVK.instance, NULL);
}

// Acquire the next swapchain image and begin recording the render pass. The
// demo records its draws into RETROVK.currentCommandBuffer, then RETROVK_EndFrame
// closes the pass and presents. Returns false when the frame should be skipped.
bool RETROVK_BeginFrame(void)
{
	vkWaitForFences(RETROVK.device, 1, &RETROVK.inFlight[RETROVK.currentFrame], VK_TRUE, UINT64_MAX);

	VkResult acquire = vkAcquireNextImageKHR(RETROVK.device, RETROVK.swapchain, UINT64_MAX,
											 RETROVK.imageAvailable[RETROVK.currentFrame], VK_NULL_HANDLE, &RETROVK.currentImageIndex);
	if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
		// The swapchain no longer matches the surface: rebuild it and skip this
		// frame. The fence is still signalled (not reset below), so the next
		// frame's wait passes straight through. VK_SUBOPTIMAL_KHR is a success
		// code and still yields a usable image, so it falls through and is dealt
		// with after present instead.
		RETROVK_RecreateSwapchain();
		return false;
	} else if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
		RETROVK_Die("vkAcquireNextImageKHR failed with a critical error");
	}

	vkResetFences(RETROVK.device, 1, &RETROVK.inFlight[RETROVK.currentFrame]);

	VkCommandBuffer commandBuffer = RETROVK.commandBuffers[RETROVK.currentFrame];
	RETROVK.currentCommandBuffer = commandBuffer;
	vkResetCommandBuffer(commandBuffer, 0);

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	vkBeginCommandBuffer(commandBuffer, &beginInfo);

	VkClearValue clearValues[2];
	clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };	// Black background
	clearValues[1].depthStencil = { 1.0f, 0 };

	VkRenderPassBeginInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = RETROVK.renderPass;
	renderPassInfo.framebuffer = RETROVK.framebuffers[RETROVK.currentImageIndex];
	renderPassInfo.renderArea.offset = { 0, 0 };
	renderPassInfo.renderArea.extent = RETROVK.swapchainExtent;
	renderPassInfo.clearValueCount = 2;
	renderPassInfo.pClearValues = clearValues;
	vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

	// Viewport and scissor are dynamic; set them to cover the whole swapchain
	VkViewport viewport = {};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)RETROVK.swapchainExtent.width;
	viewport.height = (float)RETROVK.swapchainExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

	VkRect2D scissor = {};
	scissor.offset = { 0, 0 };
	scissor.extent = RETROVK.swapchainExtent;
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

	return true;
}

void RETROVK_EndFrame(void)
{
	VkCommandBuffer commandBuffer = RETROVK.currentCommandBuffer;
	vkCmdEndRenderPass(commandBuffer);
	vkEndCommandBuffer(commandBuffer);

	VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &RETROVK.imageAvailable[RETROVK.currentFrame];
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &RETROVK.renderFinished[RETROVK.currentImageIndex];
	RETROVK_Check(vkQueueSubmit(RETROVK.graphicsQueue, 1, &submitInfo, RETROVK.inFlight[RETROVK.currentFrame]), "vkQueueSubmit failed");

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &RETROVK.renderFinished[RETROVK.currentImageIndex];
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &RETROVK.swapchain;
	presentInfo.pImageIndices = &RETROVK.currentImageIndex;
	VkResult present = vkQueuePresentKHR(RETROVK.graphicsQueue, &presentInfo);

	// Present can report the swapchain is now out of date or no longer optimal
	// (resize, display change); rebuild it so the next frame matches the surface.
	if (present == VK_ERROR_OUT_OF_DATE_KHR || present == VK_SUBOPTIMAL_KHR) {
		RETROVK_RecreateSwapchain();
	}

	RETROVK.currentFrame = (RETROVK.currentFrame + 1) % RETROVK_MAX_FRAMES_IN_FLIGHT;
}

#endif
