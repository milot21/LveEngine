//
// Created by milot on 10/20/2025.
//
#include "lve_texture.hpp"
#include <vulkan/vulkan_core.h>
#include <stdexcept>
#include "lve_device.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "../external/stb/stb_image.hpp"
#include "lve_buffer.hpp"
#include <stdexcept>

namespace lve {
Texture::Texture(LveDevice &device, const std::string &filepath) : lveDevice{device} {
  int channels;
  int m_BytesPerPixel;

  auto data = stbi_load(filepath.c_str(), &width, &height, &m_BytesPerPixel, 4);

  mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

  LveBuffer stagingBuffer{
      lveDevice,
      4,
      static_cast<uint32_t>(width * height),
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
  };

  stagingBuffer.map();
  stagingBuffer.writeToBuffer(data);

  imageFormat = VK_FORMAT_R8G8B8A8_SRGB;

  //IMAGE INFORMATION
  VkImageCreateInfo imageInfo = {};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = imageFormat;
  imageInfo.mipLevels = mipLevels;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
  imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT; //flags


  //create and allocate gpu memory
  lveDevice.createImageWithInfo(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, imageMemory);

  transitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  //to gpu image
  lveDevice.copyBufferToImage(stagingBuffer.getBuffer(), image, static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1);

  generateMipmaps(); //create smaller versions of texture for distant rendering
  imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  //sampler info defines how texture is read, filtering, wrapping
  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.mipLodBias = 0.0f;
  samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
  samplerInfo.minLod = 0.0f;
  samplerInfo.maxLod = static_cast<float>(mipLevels);
  samplerInfo.maxAnisotropy = 4.0;
  samplerInfo.anisotropyEnable = VK_TRUE;
  samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

  vkCreateSampler(lveDevice.device(), &samplerInfo, nullptr, &sampler);

  //image view is how shaders access the image
  VkImageViewCreateInfo imageViewInfo {};
  imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  imageViewInfo.format = imageFormat;
  imageViewInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
  imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  imageViewInfo.subresourceRange.baseMipLevel = 0;
  imageViewInfo.subresourceRange.baseArrayLayer = 0;
  imageViewInfo.subresourceRange.layerCount = 1;
  imageViewInfo.subresourceRange.levelCount = mipLevels;
  imageViewInfo.image = image;

  vkCreateImageView(lveDevice.device(), &imageViewInfo, nullptr, &imageView);

  stbi_image_free(data); // free cpu memory
}

Texture::~Texture() { //cleanup all vulkan resources
  vkDestroyImage(lveDevice.device(), image, nullptr);
  vkFreeMemory(lveDevice.device(), imageMemory, nullptr);
  vkDestroyImageView(lveDevice.device(), imageView, nullptr);
  vkDestroySampler(lveDevice.device(), sampler, nullptr);
}

/**
 * changes image memory layout using a pipeline barrier without modifying pixels
 * @param oldLayout
 * @param newLayout
 */
void Texture::transitionImageLayout(VkImageLayout oldLayout, VkImageLayout newLayout) {
  VkCommandBuffer commandBuffer = lveDevice.beginSingleTimeCommands();

  //creates the memory barrier to synchronize the transition
  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  //which part of image to transition which is all miplevels
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = mipLevels;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  VkPipelineStageFlags sourceStage;
  VkPipelineStageFlags destinationStage;
  //from undefined to transfer, uploading data to image
  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  }
  //used after upload, prepares for shader access
  else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  }
  else {
    throw std::runtime_error("unsupported layout transition!");
  }

  //submit barrier command
  vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
//execute and wait for completion
  lveDevice.endSingleTimeCommands(commandBuffer);
}
//create smaller versions of the texture for efficient rendering
//creates new image data
//this helped because it was flickering at a distance but not since piecing it
void Texture::generateMipmaps() {
  VkFormatProperties formatProperties;
  vkGetPhysicalDeviceFormatProperties(lveDevice.getPhysicalDevice(), imageFormat, &formatProperties);

  if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
    throw std::runtime_error("texture image format does not support linear blitting!");
  }

  VkCommandBuffer commandBuffer = lveDevice.beginSingleTimeCommands();

  //barrier for transitioning mip levels
  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.image = image;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  barrier.subresourceRange.levelCount = 1;

  //dimensions for each level
  int32_t mipWidth = width;
  int32_t mipHeight = height;

  //generate each level by blitting from the previous one
  for (uint32_t i = 1; i < mipLevels; i++) {
    barrier.subresourceRange.baseMipLevel = i - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkImageBlit blit{};
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.mipLevel = i - 1; // might be i
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount = 1;
    blit.dstOffsets[0] = {0, 0, 0};
    blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.mipLevel = i;
    blit.dstSubresource.baseArrayLayer = 0;
    blit.dstSubresource.layerCount = 1;
    // Perform the blit with linear filtering (smooth downsampling)
    vkCmdBlitImage(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    // Halve dimensions for next iteration
    if (mipWidth > 1) mipWidth /= 2;
    if (mipHeight > 1) mipHeight /= 2;
  }

  //last level is only written for transition to shader read only
  barrier.subresourceRange.baseMipLevel = mipLevels - 1;
  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

  lveDevice.endSingleTimeCommands(commandBuffer);
}
}